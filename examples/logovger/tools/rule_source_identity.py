"""Exact production rule-corpus identity shared by seed generators."""

import hashlib
import os


SOURCE_LAYER = "cepheus"
RULE_SOURCE_FILES = (
    "book1/character-creation.md",
    "book1/skills.md",
)


def _field(value):
    encoded = value if isinstance(value, bytes) else value.encode("utf-8")
    return str(len(encoded)).encode("ascii") + b":" + encoded


def ingestion_edition_context_key(source_root):
    """Reproduce the engine's LENGTH_PREFIXED_V1 corpus identity."""
    representations = []
    for source_file in sorted(RULE_SOURCE_FILES):
        path = os.path.join(source_root, *source_file.split("/"))
        with open(path, "rb") as source:
            content = source.read()
        representations.append((
            source_file,
            "UTF8_TEXT",
            "SHA256",
            hashlib.sha256(content).hexdigest(),
            str(len(content)),
        ))

    manifest = _field("LENGTH_PREFIXED_V1") + _field(
        str(len(representations)))
    for representation in representations:
        for value in representation:
            manifest += _field(value)
    digest = hashlib.sha256(manifest).hexdigest()
    return (f"ingestion-edition:v1:{len(SOURCE_LAYER)}:{SOURCE_LAYER}:"
            f"sha256:{digest}")
