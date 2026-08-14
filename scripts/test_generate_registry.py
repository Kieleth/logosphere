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

    def test_pack_owned_relation_enum_emits_typed_relations(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            schema = root / "relations.yaml"
            output = root / "registry.cpp"
            schema.write_text(
                """id: schema://relations
name: relations
classes:
  Entity: {}
  Left:
    is_a: Entity
  Right:
    is_a: Entity
enums:
  PackRelationType:
    annotations:
      relation_type_enum: true
    permissible_values:
      LINKS:
        annotations:
          valid_source_types: Left
          valid_target_types: Right
"""
            )

            generate_registry_cpp(
                str(schema), "relations::ontology", str(output)
            )
            rendered = output.read_text()

        self.assertIn(
            'reg.addRelationType("LINKS", {"Left"}, {"Right"});',
            rendered,
        )

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

    def test_vendored_generator_loads_its_vendored_templates(self):
        scripts_dir = str(Path(__file__).resolve().parent)
        if scripts_dir not in sys.path:
            sys.path.insert(0, scripts_dir)

        from cppgen.template import CppField

        rendered = CppField(name="score", cpp_type="float").render()
        self.assertIn("float score", rendered)

    def test_required_fields_are_value_initialized(self):
        scripts_dir = str(Path(__file__).resolve().parent)
        if scripts_dir not in sys.path:
            sys.path.insert(0, scripts_dir)

        from cppgen.cppgen import CppGenerator

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
