import tempfile
import unittest
from pathlib import Path
import sys

from linkml_runtime.utils.schemaview import SchemaView

from scripts.generate_registry import (
    generate_registry_cpp,
    reject_imported_redefinitions,
)


class ImportedRedefinitionTests(unittest.TestCase):
    def write_schema(self, root: Path, name: str, body: str) -> Path:
        path = root / f"{name}.yaml"
        path.write_text(body)
        return path

    def test_global_slot_redefinition_is_rejected_with_both_sources(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            self.write_schema(
                root,
                "base",
                """id: schema://base
name: base
slots:
  score:
    range: float
""",
            )
            derived = self.write_schema(
                root,
                "derived",
                """id: schema://derived
name: derived
imports:
  - base
slots:
  score:
    range: integer
""",
            )

            with self.assertRaisesRegex(
                ValueError,
                "global slot 'score'.*schema://base.*schema://derived",
            ):
                reject_imported_redefinitions(SchemaView(str(derived)))

    def test_class_local_attribute_does_not_shadow_an_imported_slot(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            self.write_schema(
                root,
                "base",
                """id: schema://base
name: base
slots:
  score:
    range: float
""",
            )
            derived = self.write_schema(
                root,
                "derived",
                """id: schema://derived
name: derived
imports:
  - base
classes:
  Character:
    attributes:
      score:
        range: integer
""",
            )

            reject_imported_redefinitions(SchemaView(str(derived)))


class VendoredCppGeneratorTests(unittest.TestCase):
    def test_mixins_remain_abstract_types_with_owned_properties(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            schema = root / "mixins.yaml"
            output = root / "registry.cpp"
            schema.write_text(
                """id: schema://mixins
name: mixins
default_range: string
classes:
  Entity: {}
  Addressable:
    mixin: true
    slots:
      - entity_key
  Record:
    is_a: Entity
    mixins:
      - Addressable
slots:
  entity_key:
    range: string
    required: true
"""
            )

            generate_registry_cpp(
                str(schema), "mixins::ontology", str(output)
            )
            rendered = output.read_text()

        self.assertIn(
            'reg.addEntityType("Addressable", "", true);', rendered
        )
        self.assertIn(
            'reg.addAncestors("Record", {"Addressable", "Entity"});',
            rendered,
        )
        self.assertIn(
            'reg.addProperty("Addressable", "entity_key", '
            'kg::PropertyValueKind::String, true);',
            rendered,
        )

    # The relation contract lives on the class. This test used to
    # declare it on the enum, with `relation_type_enum: true` and
    # valid_source_types / valid_target_types annotations on the
    # permissible value. That mechanism was deleted on 2026-08-16, so
    # the test moved with it rather than being deleted: the question it
    # asks (does a pack get typed relations of its own) is still the
    # right question.
    RELATION_PACK = """id: schema://relations
name: relations
classes:
  Entity: {}
  Relation:
    abstract: true
  Left:
    is_a: Entity
  Right:
    is_a: Entity
%s
enums:
  PackRelationType:
    permissible_values:
      LINKS:
"""

    LINKS_CLASS = """  LinksRelation:
    is_a: Relation
    slot_usage:
      relation_type:
        range: PackRelationType
        equals_string: LINKS
      source_id:
        range: Left
      target_id:
        range: Right
"""

    def _render(self, body):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            schema = root / "relations.yaml"
            output = root / "registry.cpp"
            schema.write_text(self.RELATION_PACK % body)
            generate_registry_cpp(
                str(schema), "relations::ontology", str(output)
            )
            return output.read_text()

    def test_pack_owned_relation_class_emits_typed_relations(self):
        self.assertIn(
            'reg.addRelationType("LINKS", {"Left"}, {"Right"});',
            self._render(self.LINKS_CLASS),
        )

    def test_the_deleted_enum_path_is_dead(self):
        """An enum alone emits no relation, and that is the point.

        Under the old mechanism this exact schema produced a typed
        relation. If it ever does again, two mechanisms are live and
        they will drift. This is the replaced path proving it stays
        dead, not a check that enums are ignored.
        """
        rendered = self._render("")
        self.assertNotIn("addRelationType", rendered)
        # And the control: the same schema WITH the class does emit, so
        # this test cannot pass because the generator is simply broken.
        self.assertIn("addRelationType", self._render(self.LINKS_CLASS))

    def test_concrete_relation_without_a_pinned_predicate_is_refused(self):
        """Silence is not an option here: an unpinned concrete relation
        must fail the build, not generate nothing."""
        unpinned = """  LinksRelation:
    is_a: Relation
    slot_usage:
      relation_type:
        range: PackRelationType
"""
        with self.assertRaises(ValueError) as caught:
            self._render(unpinned)
        self.assertIn("equals_string", str(caught.exception))

    def test_enum_identity_and_members_reach_registry_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            schema = root / "enums.yaml"
            output = root / "registry.cpp"
            schema.write_text(
                """id: schema://enums
name: enums
default_range: string
classes:
  Entity: {}
  Character:
    is_a: Entity
    slots:
      - mood
slots:
  mood:
    range: Mood
enums:
  Mood:
    permissible_values:
      CALM: {}
      ANGRY: {}
"""
            )

            generate_registry_cpp(str(schema), "enums::ontology", str(output))
            rendered = output.read_text()

        self.assertIn(
            'reg.addEnumType("Mood", {"ANGRY", "CALM"});', rendered
        )
        self.assertIn(
            'reg.addEnumProperty("Character", "mood", "Mood", false);',
            rendered,
        )
        self.assertNotIn('"enum"', rendered)

    def test_unknown_range_is_rejected_instead_of_becoming_string(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            schema = root / "unknown_range.yaml"
            output = root / "registry.cpp"
            schema.write_text(
                """id: schema://unknown-range
name: unknown_range
default_range: string
classes:
  Entity: {}
  Character:
    is_a: Entity
    slots:
      - mystery
slots:
  mystery:
    range: MissingType
"""
            )

            with self.assertRaisesRegex(
                ValueError, "Unknown LinkML range 'MissingType'"
            ):
                generate_registry_cpp(
                    str(schema), "unknown::ontology", str(output)
                )

    def test_create_only_annotation_reaches_registry_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            schema = root / "create_only.yaml"
            output = root / "registry.cpp"
            schema.write_text(
                """id: schema://create-only
name: create_only
default_range: string
classes:
  Entity: {}
  Context:
    is_a: Entity
  Rule:
    is_a: Entity
    slots:
      - origin
slots:
  origin:
    range: Context
    annotations:
      create_only: true
"""
            )

            generate_registry_cpp(
                str(schema), "create_only::ontology", str(output)
            )
            rendered = output.read_text()

        self.assertIn(
            'addRefProperty("Rule", "origin", false, "Context", true)',
            rendered,
        )

    def test_event_subclasses_emit_complete_ancestor_sets(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            schema = root / "events.yaml"
            output = root / "registry.cpp"
            schema.write_text(
                """id: schema://events
name: events
default_range: string
classes:
  Event: {}
  Decision:
    is_a: Event
  ClaimDecision:
    is_a: Decision
"""
            )

            generate_registry_cpp(
                str(schema), "events::ontology", str(output)
            )
            rendered = output.read_text()

        self.assertIn(
            'reg.addAncestors("Decision", {"Event"});', rendered
        )
        self.assertIn(
            'reg.addAncestors("ClaimDecision", {"Decision", "Event"});',
            rendered,
        )

    def test_generator_delta_overrides_are_installed(self):
        # scripts/cppgen/ (the vendored copy) is gone; the generator is
        # upstream linkml.generators.cppgen with this repo's two fixes
        # layered on at import by scripts/gen_cpp_header.py. When
        # upstream adopts them, this test fails and gets deleted along
        # with the overrides.
        scripts_dir = str(Path(__file__).resolve().parent)
        if scripts_dir not in sys.path:
            sys.path.insert(0, scripts_dir)

        import gen_cpp_header

        gen = gen_cpp_header.CppGenerator
        self.assertEqual(gen.sort_classes.__module__, "gen_cpp_header")
        self.assertEqual(gen.generate_field.__module__, "gen_cpp_header")

    def test_required_fields_are_value_initialized(self):
        scripts_dir = str(Path(__file__).resolve().parent)
        if scripts_dir not in sys.path:
            sys.path.insert(0, scripts_dir)

        # The value-init fix lives in the gen_cpp_header delta, not
        # upstream — import through the wrapper so it is applied.
        from gen_cpp_header import CppGenerator

        with tempfile.TemporaryDirectory() as td:
            schema = Path(td) / "required.yaml"
            schema.write_text(
                """id: schema://required
name: required
default_range: string
prefixes:
  linkml: https://w3id.org/linkml/
imports:
  - linkml:types
classes:
  RequiredRecord:
    slots:
      - count
      - label
slots:
  count:
    range: integer
    required: true
  label:
    range: string
    required: true
"""
            )
            rendered = CppGenerator(str(schema), namespace="required").serialize()

        self.assertIn("int32_t count = {};", rendered)
        self.assertIn("std::string label = {};", rendered)


if __name__ == "__main__":
    unittest.main()
