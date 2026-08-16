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
    items = ", ".join(json.dumps(v) for v in sorted(values))
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


def _annotation_is_true(definition, name: str) -> bool:
    annotations = getattr(definition, "annotations", None)
    if not annotations or name not in annotations:
        return False
    return str(annotations[name].value).strip().lower() in {
        "1", "true", "yes"
    }


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


def _get_kg_key_namespaces(sv: SchemaView, class_name: str) -> list:
    """Open property-namespace prefixes from `annotations:
    kg_key_namespaces:` — comma-separated. Keys under a declared prefix
    pass the setProperty gate untyped; for engine surfaces open BY
    DESIGN (rule.<i>.payload.*), never a shortcut around exact slots."""
    cls = sv.get_class(class_name)
    ann = getattr(cls, "annotations", None)
    if not ann or "kg_key_namespaces" not in ann:
        return []
    raw = str(ann["kg_key_namespaces"].value)
    return sorted(p.strip() for p in raw.split(",") if p.strip())


def _kg_key(slot, slot_name: str) -> str:
    """Registry key for a slot: the `kg_key` annotation when present,
    else the slot name.

    Why: some engine-written KG keys are dotted (`cap.locomotion.weight`),
    which is not a legal C++ identifier, so the slot NAME must stay
    identifier-safe for gen-cpp-header (which emits a struct field per
    slot). The annotation carries the real runtime key into the registry
    so the setProperty gate validates what the engine actually writes.
    Same annotation mechanism as `facets` on classes."""
    ann = getattr(slot, "annotations", None)
    if ann and "kg_key" in ann:
        return str(ann["kg_key"].value)
    return slot_name


def _linkml_range_to_value_kind(sv: SchemaView, range_name: str | None) -> str:
    """Map a LinkML range to the registry's closed value-kind enum."""
    if not range_name:
        raise ValueError("LinkML slot has no range after schema induction")
    if range_name in sv.all_enums():
        return "Enum"
    if range_name in sv.all_classes():
        return "EntityRef"
    type_map = {
        "string": "String",
        "integer": "Integer",
        "float": "Float",
        "double": "Float",
        "decimal": "Float",
        "boolean": "Boolean",
        "datetime": "LegacyDateTime",
        "date": "LegacyDateTime",
    }
    if range_name in type_map:
        return type_map[range_name]

    custom_type = sv.get_type(range_name)
    if custom_type and custom_type.typeof:
        return _linkml_range_to_value_kind(sv, custom_type.typeof)

    raise ValueError(f"Unknown LinkML range '{range_name}'")


def _get_relation_domain_range(
    sv: SchemaView,
) -> dict[str, tuple[set[str], set[str], object]]:
    """Extract relation names and endpoint constraints from Relation
    subclasses.

    The contract lives on the class, which is where malleus's
    `bound_endpoints` rite looks and where a reader looking at one
    relation finds its shape. A concrete subclass of Relation pins its
    predicate with `equals_string` on relation_type, and narrows
    source_id / target_id; an omitted endpoint means Entity, which for
    several engine relations is the measured truth rather than neglect.

    This replaced harvesting from enums annotated `relation_type_enum`,
    where the endpoints hung off permissible values. That path is
    deleted rather than kept as a fallback: two ways to declare one
    fact is how the two drift apart. The enums remain as the
    vocabulary, and are the range of each class's relation_type slot.
    """
    result = {}
    for class_name, cls in sorted(sv.all_classes().items()):
        if not _is_relation_subtype(sv, class_name):
            continue
        if getattr(cls, "abstract", False):
            continue

        usage = cls.slot_usage or {}
        predicate = getattr(usage.get("relation_type"), "equals_string", None)
        if not predicate:
            raise ValueError(
                f"Concrete relation '{class_name}' does not fix "
                "relation_type with equals_string, so the predicate it "
                "writes is unknowable until runtime and its endpoints "
                "cannot be checked. Either fix the predicate or mark "
                "the class abstract."
            )

        sources = {getattr(usage.get("source_id"), "range", None) or "Entity"}
        targets = {getattr(usage.get("target_id"), "range", None) or "Entity"}

        unknown = (sources | targets) - set(sv.all_classes())
        if unknown:
            raise ValueError(
                f"Relation '{predicate}' names unknown endpoint types: "
                + ", ".join(sorted(unknown))
            )
        incoming = (sources, targets, cls)
        if predicate in result and result[predicate][:2] != incoming[:2]:
            raise ValueError(
                f"Relation '{predicate}' has conflicting endpoint "
                "constraints"
            )
        result[predicate] = incoming

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

    # Enum types retain nominal identity. Two enums may share member tokens
    # without becoming interchangeable property ranges.
    lines.append("    // Enum types")
    for enum_name, enum_def in sorted(sv.all_enums().items()):
        members = set((enum_def.permissible_values or {}).keys())
        if not members:
            raise ValueError(f"Enum '{enum_name}' has no permissible values")
        # annotations: case_insensitive: true -> membership checks fold
        # case, matching consumers that normalize on read.
        ci = ", true" if _annotation_is_true(
            enum_def, "case_insensitive"
        ) else ""
        emit(
            _definition_source(enum_def, f"enum {enum_name}"),
            f"    reg.addEnumType({json.dumps(enum_name)}, "
            f"{_cpp_string_set(members)}{ci});",
        )

    lines.append("")

    # Entity types plus mixins. Mixins are retained as abstract ontology
    # classes so direct property ownership can be reflected canonically, but
    # they can never be instantiated.
    lines.append("    // Entity types")
    for cn in sorted(sv.all_classes()):
        is_mixin = _is_mixin(sv, cn)
        if not is_mixin and not _is_entity_subtype(sv, cn):
            continue
        cls = sv.get_class(cn)
        parent = _get_parent(sv, cn)
        is_abstract = "true" if cls.abstract or is_mixin else "false"
        emit(
            _definition_source(cls, f"class {cn}"),
            f'    reg.addEntityType("{cn}", "{parent}", {is_abstract});',
        )

    lines.append("")

    # Ancestors (full inheritance chains)
    lines.append("    // Inheritance chains")
    for cn in sorted(sv.all_classes()):
        if not (_is_mixin(sv, cn) or _is_entity_subtype(sv, cn)):
            continue
        ancestors = _get_ancestors(sv, cn)
        if ancestors:
            anc_set = _cpp_string_set(ancestors)
            emit(
                _definition_source(sv.get_class(cn), f"class {cn}"),
                f'    reg.addAncestors("{cn}", {anc_set});',
            )

    lines.append("")

    # Facets (annotations: facets: on entity and event classes)
    facet_lines = []
    for cn in sorted(sv.all_classes()):
        # Event subtypes belong here too. Event is a SIBLING primitive
        # of Entity in the malleus root, not a subtype of it, so a gate
        # asking only about Entity silently discards every facet
        # declared on an event class. Found when a facet added to
        # ServiceTerm (is_a Event) never reached the registry: the
        # extractor returned it correctly and this loop threw it away,
        # so the schema said one thing and the generated code said
        # nothing, with no error in between.
        if not (_is_mixin(sv, cn) or _is_entity_subtype(sv, cn)
                or _is_event_subtype(sv, cn)):
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
    # NOTE: facet_lines is built here and EMITTED AT THE END of this
    # function, after both the entity-type and event-type blocks.
    # addFacets() looks the type up and silently does nothing when it
    # is absent, so a facet emitted before its type is dropped without
    # a word. Event types are written after this point, which is
    # exactly how a facet on ServiceTerm reached the generated file and
    # still never reached the registry.

    # Open property namespaces (annotations: kg_key_namespaces:).
    # Emitted for mixins too — hasPropertyNamespace walks ancestors the
    # same way hasProperty does.
    ns_lines = []
    for cn in sorted(sv.all_classes()):
        if not (_is_entity_subtype(sv, cn) or _is_mixin(sv, cn)):
            continue
        for prefix in _get_kg_key_namespaces(sv, cn):
            ns_lines.append(f'    reg.addPropertyNamespace("{cn}", "{prefix}");')
    if ns_lines:
        lines.append("    // Open property namespaces")
        lines.extend(ns_lines)
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

    # Facets, emitted only now: addFacets() resolves the type by name
    # and silently no-ops when it is not registered yet, so this must
    # come after every addEntityType call, entity and event alike.
    if facet_lines:
        lines.append("    // Facets")
        for source, statement in facet_lines:
            emit(source, statement)
        lines.append("")

    # Relation types with domain/range
    lines.append("    // Relation types")
    rel_constraints = _get_relation_domain_range(sv)
    for rel_name, (sources, targets, relation_class) in sorted(
        rel_constraints.items()
    ):
        src_set = _cpp_string_set(sources)
        tgt_set = _cpp_string_set(targets)
        emit(
            _definition_source(relation_class, f"relation '{rel_name}'"),
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
            key = _kg_key(slot, sn)
            source = _definition_source(slot, f"slot {cn}.{sn}")
            value_kind = _linkml_range_to_value_kind(sv, slot.range)
            required = "true" if slot.required else "false"
            create_only = "true" if _annotation_is_true(
                slot, "create_only"
            ) else "false"
            if slot.identifier:
                emit(
                    source,
                    f'    reg.addIdentifierProperty("{cn}", "{key}", '
                    f"kg::PropertyValueKind::{value_kind}, {required});",
                )
                continue
            # Class-ranged slots are entity references: the registry
            # keeps the target class so the OntologyValidator can
            # check the referenced entity is-a that class.
            if value_kind == "EntityRef":
                create_only_arg = ", true" if create_only == "true" else ""
                emit(
                    source,
                    f'    reg.addRefProperty("{cn}", "{key}", {required}, '
                    f'"{slot.range}"{create_only_arg});',
                )
                continue
            if value_kind == "Enum":
                emit(
                    source,
                    f'    reg.addEnumProperty("{cn}", "{key}", '
                    f'"{slot.range}", {required}'
                    f'{", true" if create_only == "true" else ""});',
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
                    f'    reg.addProperty("{cn}", "{key}", '
                    f"kg::PropertyValueKind::{value_kind}, {required}, "
                    f"{has_min}, {min_lit}, {has_max}, {max_lit}"
                    f'{", true" if create_only == "true" else ""});',
                )
            else:
                emit(
                    source,
                    f'    reg.addProperty("{cn}", "{key}", '
                    f"kg::PropertyValueKind::{value_kind}, "
                    f'{required}{", true" if create_only == "true" else ""});',
                )

    lines.append("")
    lines.append("    reg.validateReferences();")
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
