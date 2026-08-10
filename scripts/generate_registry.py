#!/usr/bin/env python3
"""Generate C++ ontology registry from LinkML YAML.

Produces a .cpp file with a build function that populates
an OntologyRegistry with all entity types, relation types,
property schemas, and inheritance chains from the schema.
"""

import json
from pathlib import Path

from linkml_runtime.utils.schemaview import SchemaView


def reject_imported_redefinitions(sv: SchemaView) -> None:
    """Reject global definitions repeated anywhere in an import closure.

    LinkML resolves repeated global names by import order. That can silently
    rewrite imported classes which happen to use the same slot. Domain-specific
    reuse belongs in class-local attributes instead.
    """
    sv.imports_closure()
    collisions = []
    for kind, collection in (
        ("class", "classes"),
        ("enum", "enums"),
        ("slot", "slots"),
    ):
        owners: dict[str, set[str]] = {}
        for schema in sv.schema_map.values():
            source = str(schema.id or schema.name)
            for name in (getattr(schema, collection, None) or {}):
                owners.setdefault(name, set()).add(source)
        for name, sources in owners.items():
            if len(sources) > 1:
                collisions.append((kind, name, sorted(sources)))

    if collisions:
        details = "; ".join(
            f"global {kind} '{name}' is defined by " + " and ".join(sources)
            for kind, name, sources in sorted(collisions)
        )
        raise ValueError(
            f"Imported schema redefinition: {details}. Use a class-local "
            "attribute or a unique global name."
        )


def _definition_source(definition, label: str) -> str:
    source = getattr(definition, "from_schema", None)
    if not source:
        raise ValueError(f"{label} has no from_schema provenance")
    return str(source)


def _write_if_changed(path: Path, content: str) -> bool:
    """Write only if different from on-disk content. Preserves mtime
    across regenerations so cmake doesn't rebuild the world unnecessarily."""
    try:
        if path.exists() and path.read_text() == content:
            return False
    except OSError:
        pass
    path.write_text(content)
    return True


def _cpp_string_set(values: set[str]) -> str:
    """Format a C++ unordered_set initializer."""
    items = ", ".join(f'"{v}"' for v in sorted(values))
    return "{" + items + "}"


def _is_entity_subtype(sv: SchemaView, class_name: str) -> bool:
    """Check if a class is a subtype of Entity (directly or transitively)."""
    ancestors = sv.class_ancestors(class_name)
    return "Entity" in ancestors


def _is_event_subtype(sv: SchemaView, class_name: str) -> bool:
    """Check if a class is a subtype of Event."""
    ancestors = sv.class_ancestors(class_name)
    return "Event" in ancestors


def _is_relation_subtype(sv: SchemaView, class_name: str) -> bool:
    """Check if a class is a subtype of Relation."""
    ancestors = sv.class_ancestors(class_name)
    return "Relation" in ancestors


def _get_facets(sv: SchemaView, class_name: str) -> list:
    """Facets from `annotations: facets:` — comma-separated string.
    Engine defines no facet names; games declare meaning."""
    cls = sv.get_class(class_name)
    ann = getattr(cls, "annotations", None)
    if not ann or "facets" not in ann:
        return []
    raw = str(ann["facets"].value)
    return sorted(f.strip() for f in raw.split(",") if f.strip())


def _is_mixin(sv: SchemaView, class_name: str) -> bool:
    """Check if a class is a mixin."""
    cls = sv.get_class(class_name)
    return cls.mixin if cls else False


def _get_parent(sv: SchemaView, class_name: str) -> str:
    """Get the is_a parent of a class, or empty string."""
    cls = sv.get_class(class_name)
    return cls.is_a if cls and cls.is_a else ""


def _get_ancestors(sv: SchemaView, class_name: str) -> set[str]:
    """Get all ancestors (is_a chain + mixin ancestors), excluding self."""
    ancestors = set(sv.class_ancestors(class_name))
    ancestors.discard(class_name)
    return ancestors


def _linkml_range_to_value_type(sv: SchemaView, range_name: str | None) -> str:
    """Map a LinkML range to a simple value type string."""
    if not range_name:
        return "string"
    if range_name in sv.all_enums():
        return "enum"
    if range_name in sv.all_classes():
        return "entity_ref"
    type_map = {
        "string": "string",
        "integer": "integer",
        "float": "float",
        "double": "float",
        "boolean": "boolean",
        "datetime": "datetime",
        "date": "datetime",
    }
    return type_map.get(range_name, "string")


def _get_relation_domain_range(sv: SchemaView) -> dict[str, tuple[set[str], set[str]]]:
    """Extract domain/range for relation types from WorldRelationType enum.

    Since LinkML enums don't carry domain/range natively, we derive
    constraints from the schema structure. WorldRelation constrains
    source_id and target_id to string (entity IDs). The actual
    domain/range per relation type is set broadly (any entity type)
    unless we add custom annotations later.
    """
    result = {}
    enum_def = sv.get_enum("WorldRelationType")
    if not enum_def or not enum_def.permissible_values:
        return result

    # Collect all concrete entity types for default domain/range
    entity_types = set()
    for cn in sv.all_classes():
        if _is_entity_subtype(sv, cn) and not _is_mixin(sv, cn):
            entity_types.add(cn)

    # Default: any entity can participate in any relation
    # Specific constraints can be added later via annotations
    for pv_name in enum_def.permissible_values:
        result[pv_name] = ({"Entity"}, {"Entity"})

    return result


def generate_registry_cpp(yaml_path: str, namespace: str, output_path: str):
    """Generate the registry .cpp file from a LinkML schema."""
    sv = SchemaView(yaml_path)
    reject_imported_redefinitions(sv)
    lines = []
    active_source = None

    def emit(source: str, statement: str) -> None:
        nonlocal active_source
        if source != active_source:
            lines.append(f"    reg.setSource({json.dumps(source)});")
            active_source = source
        lines.append(statement)

    lines.append("// Code generated by generate_registry.py. DO NOT EDIT.")
    lines.append(f'#include "logosphere/kg/ontology_registry.h"')
    lines.append("")
    lines.append(f"namespace {namespace} {{")
    lines.append("")
    lines.append("static kg::OntologyRegistry build_registry() {")
    lines.append("    kg::OntologyRegistry reg;")
    lines.append("")

    # Entity types (classes that are subtypes of Entity, excluding mixins)
    lines.append("    // Entity types")
    for cn in sorted(sv.all_classes()):
        if _is_mixin(sv, cn):
            continue
        if not _is_entity_subtype(sv, cn):
            continue
        cls = sv.get_class(cn)
        parent = _get_parent(sv, cn)
        is_abstract = "true" if cls.abstract else "false"
        emit(
            _definition_source(cls, f"class {cn}"),
            f'    reg.addEntityType("{cn}", "{parent}", {is_abstract});',
        )

    lines.append("")

    # Ancestors (full inheritance chains)
    lines.append("    // Inheritance chains")
    for cn in sorted(sv.all_classes()):
        if _is_mixin(sv, cn):
            continue
        if not _is_entity_subtype(sv, cn):
            continue
        ancestors = _get_ancestors(sv, cn)
        if ancestors:
            anc_set = _cpp_string_set(ancestors)
            emit(
                _definition_source(sv.get_class(cn), f"class {cn}"),
                f'    reg.addAncestors("{cn}", {anc_set});',
            )

    lines.append("")

    # Facets (annotations: facets: on entity classes)
    facet_lines = []
    for cn in sorted(sv.all_classes()):
        if _is_mixin(sv, cn) or not _is_entity_subtype(sv, cn):
            continue
        facets = _get_facets(sv, cn)
        if facets:
            fset = "{" + ", ".join(f'"{f}"' for f in facets) + "}"
            facet_lines.append(
                (
                    _definition_source(sv.get_class(cn), f"class {cn}"),
                    f'    reg.addFacets("{cn}", {fset});',
                )
            )
    if facet_lines:
        lines.append("    // Facets")
        for source, statement in facet_lines:
            emit(source, statement)
        lines.append("")

    # Event types
    lines.append("    // Event types")
    for cn in sorted(sv.all_classes()):
        if _is_mixin(sv, cn):
            continue
        if not _is_event_subtype(sv, cn):
            continue
        cls = sv.get_class(cn)
        parent = _get_parent(sv, cn)
        is_abstract = "true" if cls.abstract else "false"
        emit(
            _definition_source(cls, f"class {cn}"),
            f'    reg.addEntityType("{cn}", "{parent}", {is_abstract});',
        )

    lines.append("")

    # Relation types with domain/range
    lines.append("    // Relation types")
    rel_constraints = _get_relation_domain_range(sv)
    relation_enum = sv.get_enum("WorldRelationType")
    for rel_name, (sources, targets) in sorted(rel_constraints.items()):
        src_set = _cpp_string_set(sources)
        tgt_set = _cpp_string_set(targets)
        emit(
            _definition_source(relation_enum, "enum WorldRelationType"),
            f'    reg.addRelationType("{rel_name}", {src_set}, {tgt_set});',
        )

    lines.append("")

    # Properties per entity type (direct slots only, inherited come via ancestor lookup)
    lines.append("    // Properties per entity type")
    for cn in sorted(sv.all_classes()):
        if _is_mixin(sv, cn):
            # Mixins contribute properties to classes that use them
            # We store them under the mixin name so hasProperty can walk ancestors
            pass
        if not (_is_entity_subtype(sv, cn) or _is_event_subtype(sv, cn) or _is_mixin(sv, cn)):
            continue

        cls = sv.get_class(cn)
        has_parent = bool(cls.is_a) or bool(cls.mixins)
        slot_names = sv.class_slots(cn, direct=has_parent)

        for sn in slot_names:
            slot = sv.induced_slot(sn, cn)
            source = _definition_source(slot, f"slot {cn}.{sn}")
            value_type = _linkml_range_to_value_type(sv, slot.range)
            required = "true" if slot.required else "false"
            # Class-ranged slots are entity references: the registry
            # keeps the target class so the OntologyValidator can
            # check the referenced entity is-a that class.
            if value_type == "entity_ref":
                emit(
                    source,
                    f'    reg.addRefProperty("{cn}", "{sn}", {required}, '
                    f'"{slot.range}");',
                )
                continue
            # Range annotations (LinkML's minimum_value / maximum_value
            # fields) flow through to PropertyDef so the OntologyValidator
            # can clamp LLM-proposed SetProperty values per Phase C.3.
            min_v = getattr(slot, "minimum_value", None)
            max_v = getattr(slot, "maximum_value", None)
            if min_v is not None or max_v is not None:
                has_min = "true" if min_v is not None else "false"
                has_max = "true" if max_v is not None else "false"
                # Emit literal floats; non-numeric annotations on string
                # ranges would be a schema bug but we defend defensively.
                try:
                    min_lit = f"{float(min_v) if min_v is not None else 0.0}"
                    max_lit = f"{float(max_v) if max_v is not None else 0.0}"
                except (TypeError, ValueError):
                    min_lit, max_lit = "0.0", "0.0"
                    has_min, has_max = "false", "false"
                emit(
                    source,
                    f'    reg.addProperty("{cn}", "{sn}", "{value_type}", {required}, '
                    f"{has_min}, {min_lit}, {has_max}, {max_lit});",
                )
            else:
                emit(
                    source,
                    f'    reg.addProperty("{cn}", "{sn}", "{value_type}", {required});',
                )

    lines.append("")
    lines.append("    return reg;")
    lines.append("}")
    lines.append("")
    lines.append(f"const kg::OntologyRegistry& registry() {{")
    lines.append(f"    static kg::OntologyRegistry reg = build_registry();")
    lines.append(f"    return reg;")
    lines.append(f"}}")
    lines.append("")
    lines.append(f"}} // namespace {namespace}")
    lines.append("")

    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    _write_if_changed(output, "\n".join(lines))

    # Also generate the header with the registry function declaration
    header_path = output.with_suffix(".h")
    header_lines = [
        "// Code generated by generate_registry.py. DO NOT EDIT.",
        "#pragma once",
        "",
        '#include "logosphere/kg/ontology_registry.h"',
        "",
        f"namespace {namespace} {{",
        "",
        "/// Returns the singleton ontology registry populated from the schema YAML.",
        "/// This is the TBox that defines the KG's type system.",
        "const kg::OntologyRegistry& registry();",
        "",
        f"}} // namespace {namespace}",
        "",
    ]
    _write_if_changed(header_path, "\n".join(header_lines))
