#!/usr/bin/env python3
"""Thin entry point for the vendored LinkML C++ generator.

The generator itself lives in scripts/cppgen/ (maintainer-authored,
vendored from the maintainer's LinkML fork). It depends on the
published `linkml` package for its base classes; see environment.yml.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cppgen.cppgen import cli

if __name__ == "__main__":
    cli()
