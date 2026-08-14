#!/usr/bin/env python3
"""Audit real ontology enums to inform compatibility policy."""

from collections import defaultdict
from itertools import combinations
from pathlib import Path
import re

try:
    import yaml
except ModuleNotFoundError as error:
    raise SystemExit(
        "PyYAML is required. Use the environment declared by environment.yml."
    ) from error


ROOT = Path(__file__).resolve().parent.parent
OPEN_MARKER = re.compile(
    r"\b(FLOOR|open|extensible|extensibility)\b", re.IGNORECASE
)


def walk_ranges(node, path=()):
    if isinstance(node, dict):
        for key, value in node.items():
            next_path = path + (str(key),)
            if key == "range" and isinstance(value, str):
                yield next_path, value
            yield from walk_ranges(value, next_path)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            yield from walk_ranges(value, path + (str(index),))


def main():
    if OPEN_MARKER.search("orthogonal to FloorType"):
        raise RuntimeError("open-vocabulary marker matched a type-name prefix")
    if not OPEN_MARKER.search("This enum is a FLOOR, not a closed range"):
        raise RuntimeError("open-vocabulary marker missed the schema contract")

    schema_files = sorted((ROOT / "schema").rglob("*.yaml")) + sorted(
        (ROOT / "examples").glob("*/schema/**/*.yaml")
    )
    enums = {}
    enum_sources = defaultdict(list)
    ranges = []
    open_enum_notes = []

    for schema_path in schema_files:
        relative = schema_path.relative_to(ROOT)
        schema = yaml.safe_load(schema_path.read_text()) or {}
        for enum_name, enum_def in (schema.get("enums") or {}).items():
            members = frozenset(
                (enum_def.get("permissible_values") or {}).keys()
            )
            enum_sources[enum_name].append(str(relative))
            enums[(str(relative), enum_name)] = members
            description = str(enum_def.get("description") or "")
            if OPEN_MARKER.search(description):
                open_enum_notes.append(
                    (str(relative), enum_name, " ".join(description.split()))
                )
        for path, range_name in walk_ranges(schema):
            ranges.append((str(relative), ".".join(path), range_name))

    enum_names = set(enum_sources)
    enum_ranges = [item for item in ranges if item[2] in enum_names]
    identical_pairs = []
    overlap_pairs = []
    for (left_key, left_members), (right_key, right_members) in combinations(
        enums.items(), 2
    ):
        if left_members == right_members:
            identical_pairs.append((left_key, right_key, sorted(left_members)))
        overlap = left_members & right_members
        if overlap:
            overlap_pairs.append(
                (len(overlap), left_key, right_key, sorted(overlap))
            )

    generated_enum_properties = []
    pattern = re.compile(
        r'reg\.addProperty\("([^"]+)", "([^"]+)", "enum"'
    )
    for generated in sorted(ROOT.rglob("*_ontology_registry.cpp")):
        for class_name, property_name in pattern.findall(generated.read_text()):
            generated_enum_properties.append(
                (str(generated.relative_to(ROOT)), class_name, property_name)
            )

    print(f"schema_files={len(schema_files)}")
    print(f"enum_definitions={len(enums)}")
    print(f"unique_enum_names={len(enum_names)}")
    print(
        "duplicate_enum_names="
        f"{sum(len(sources) > 1 for sources in enum_sources.values())}"
    )
    print(f"enum_ranged_schema_fields={len(enum_ranges)}")
    print(
        "generated_generic_enum_properties="
        f"{len(generated_enum_properties)}"
    )
    print(f"identical_vocabulary_pairs={len(identical_pairs)}")
    print(f"overlapping_vocabulary_pairs={len(overlap_pairs)}")
    print(f"open_enum_descriptions={len(open_enum_notes)}")

    print("\nTOP_MEMBER_OVERLAPS")
    for size, left, right, overlap in sorted(overlap_pairs, reverse=True)[:12]:
        print(f"{size}: {left[1]} <> {right[1]}: {','.join(overlap)}")

    print("\nIDENTICAL_VOCABULARIES")
    if not identical_pairs:
        print("none")
    for left, right, members in identical_pairs:
        print(f"{left[1]} == {right[1]}: {','.join(members)}")

    print("\nOPEN_ENUM_NOTES")
    if not open_enum_notes:
        print("none")
    for source, name, description in open_enum_notes:
        print(f"{source}:{name}: {description}")


if __name__ == "__main__":
    main()
