#!/usr/bin/env python3
"""Thin entry point for LinkML's C++ header generator.

The generator lives upstream in the published `linkml` package as
`linkml.generators.cppgen` (merged as linkml#3332, first released in
v1.11.0). Until 2026-08-13 this repo carried a vendored copy under
scripts/cppgen/ taken from the pre-merge fork branch. That copy was
never standalone: it loaded its Jinja templates through
PackageLoader("linkml.generators.cppgen", "templates"), so it already
required the installed package to provide the module. A clean
environment resolving any linkml older than 1.11.0 could not run
codegen at all. environment.yml now pins the floor.

See CLAUDE.md "Ontology changes" for the regeneration workflow.
"""

from linkml.generators.cppgen.cppgen import cli

if __name__ == "__main__":
    cli()
