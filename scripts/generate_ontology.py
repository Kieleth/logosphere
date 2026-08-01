#!/usr/bin/env python3
"""Generate C++ headers and runtime registry from LinkML ontology YAML.

Usage:
    python scripts/generate_ontology.py

Produces two outputs per schema:
  1. Type definitions header (enums, structs) via gen-cpp-header
  2. Runtime ontology registry (entity types, relations, properties) for KG construction
"""

import subprocess
import sys
from pathlib import Path

import shutil
import tempfile
import yaml

from generate_registry import generate_registry_cpp

ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = ROOT / "schema"
PACK_DIR = SCHEMA_DIR / "packs"
OUTPUT_DIR = ROOT / "src" / "generated"

MINIMUM_MALLEUS_VERSION = (0, 2)


def write_if_changed(path: Path, content: str) -> bool:
    """Write content to path only if it differs from current on-disk content.

    Returns True if written (or file absent), False if unchanged. Prevents
    touching file mtime on no-op regenerations, which would otherwise
    force cmake to rebuild the world on every `make`.
    """
    try:
        if path.exists() and path.read_text() == content:
            return False
    except OSError:
        pass
    path.write_text(content)
    return True

SCHEMAS = [
    {
        "yaml": SCHEMA_DIR / "logosphere.yaml",
        "namespace": "logosphere::ontology",
        "output_header": OUTPUT_DIR / "logosphere_ontology.h",
        "output_registry": OUTPUT_DIR / "logosphere_ontology_registry.cpp",
    },
]


def parse_version(version_str: str) -> tuple[int, int, int]:
    parts = version_str.split(".")
    return (int(parts[0]), int(parts[1]), int(parts[2]) if len(parts) > 2 else 0)


def check_malleus_version() -> bool:
    malleus_path = SCHEMA_DIR / "malleus.yaml"
    if not malleus_path.exists():
        print("ERROR: schema/malleus.yaml not found (pinned vendored copy expected in-repo)", file=sys.stderr)
        return False

    with open(malleus_path) as f:
        schema = yaml.safe_load(f)

    version_str = schema.get("version")
    if not version_str:
        print("ERROR: malleus.yaml has no version field", file=sys.stderr)
        return False

    major, minor, _ = parse_version(version_str)
    req_major, req_minor = MINIMUM_MALLEUS_VERSION

    if major != req_major:
        print(
            f"ERROR: malleus version {version_str} incompatible. "
            f"Requires malleus {req_major}.x.\n"
            f"See ../malleus/CHANGELOG.md for migration steps.",
            file=sys.stderr,
        )
        return False

    if minor < req_minor:
        print(
            f"ERROR: malleus version {version_str} too old. "
            f"Requires >= {req_major}.{req_minor}.0.",
            file=sys.stderr,
        )
        return False

    print(f"malleus v{version_str} OK (requires >= {req_major}.{req_minor}.0)")
    return True


def generate(schema: dict) -> bool:
    yaml_path = schema["yaml"]
    if not yaml_path.exists():
        print(f"SKIP: {yaml_path} not found")
        return True

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # LinkML resolves `imports:` relative to the importing FILE, which
    # used to require tracked symlinks in every game schema dir (and an
    # absolute one for malleus). Zero tracked symlinks: stage the schema
    # beside its import dependencies in a temp dir and generate there.
    with tempfile.TemporaryDirectory() as td:
        staged = Path(td) / yaml_path.name
        shutil.copy(yaml_path, staged)
        deps = [SCHEMA_DIR / "logosphere.yaml", SCHEMA_DIR / "malleus.yaml"]
        # Setting packs (schema/packs/*.yaml) are import dependencies
        # in exactly the same way: a schema that imports `space` has to
        # find space.yaml beside its own file when LinkML resolves.
        if PACK_DIR.is_dir():
            deps.extend(sorted(PACK_DIR.glob("*.yaml")))
        for dep_src in deps:
            if dep_src.exists() and dep_src.name != yaml_path.name:
                shutil.copy(dep_src, Path(td) / dep_src.name)

        # 1. Generate type definitions header
        cmd = [
            sys.executable,
            str(ROOT / "scripts" / "gen_cpp_header.py"),
            str(staged),
            "--namespace", schema["namespace"],
        ]

        print(f"Generating {schema['output_header'].name} from {yaml_path.name}...")
        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode != 0:
            print(f"ERROR: gen-cpp-header failed:\n{result.stderr}", file=sys.stderr)
            return False

        changed = write_if_changed(schema["output_header"], result.stdout)
        print(f"  -> {schema['output_header']}{'' if changed else ' (unchanged)'}")

        # 2. Generate runtime registry
        registry_path = schema["output_registry"]
        print(f"Generating {registry_path.name}...")
        generate_registry_cpp(
            yaml_path=str(staged),
            namespace=schema["namespace"],
            output_path=str(registry_path),
        )
        print(f"  -> {registry_path}")

    return True


def main():
    if not check_malleus_version():
        sys.exit(1)

    # Discover setting packs. A pack is an optional layer of world
    # vocabulary - celestial bodies, earth-like life - that sits above
    # the setting-agnostic core and below any one game. Games opt in by
    # importing the pack and extending the KG with its registry; a type
    # from a pack nobody loaded is rejected at createEntity, loudly,
    # which is the behaviour we want.
    for pack_schema in sorted(PACK_DIR.glob("*.yaml")) if PACK_DIR.is_dir() else []:
        if pack_schema.is_symlink():
            continue
        pack_name = pack_schema.stem
        SCHEMAS.append({
            "yaml": pack_schema,
            "namespace": f"{pack_name}::ontology",
            "output_header": OUTPUT_DIR / f"{pack_name}_ontology.h",
            "output_registry": OUTPUT_DIR / f"{pack_name}_ontology_registry.cpp",
        })

    # Discover game extension schemas (only the game's own yaml, not symlinks)
    for game_schema in sorted(SCHEMA_DIR.parent.glob("examples/*/schema/*.yaml")):
        if game_schema.is_symlink():
            continue
        game_name = game_schema.parent.parent.name
        game_output = game_schema.parent.parent / "src" / "generated"
        SCHEMAS.append({
            "yaml": game_schema,
            "namespace": f"{game_name}::ontology",
            "output_header": game_output / f"{game_name}_ontology.h",
            "output_registry": game_output / f"{game_name}_ontology_registry.cpp",
        })

    ok = True
    for schema in SCHEMAS:
        if not generate(schema):
            ok = False

    if not ok:
        sys.exit(1)

    print(f"Done. Generated {len(SCHEMAS)} schema(s) (header + registry each).")


if __name__ == "__main__":
    main()
