#!/usr/bin/env python3
"""Entry point for LinkML's C++ header generator, plus this repo's delta.

The generator lives upstream in the published `linkml` package as
`linkml.generators.cppgen` (merged as linkml#3332, first released in
v1.11.0). Until 2026-08-13 this repo carried a vendored copy under
scripts/cppgen/ taken from the pre-merge fork branch. That copy was
never standalone: it loaded its Jinja templates through
PackageLoader("linkml.generators.cppgen", "templates"), so it already
required the installed package to provide the module. A clean
environment resolving any linkml older than 1.11.0 could not run
codegen at all. environment.yml now pins the floor.

Two behavior fixes the vendored copy grew before it was deleted are
not upstream yet, so they are applied here as a delta on the installed
generator rather than by re-vendoring the whole thing:

1. sort_classes considers class-ranged slots, not just is_a/mixins.
   A class-ranged slot becomes a by-value struct member
   (``Outcome outcome``), so the member's struct must be complete
   before its user — upstream's parent-only topological sort emits
   them in schema declaration order and only compiles by luck.
2. Required scalar fields get ``= {}``. LinkML requiredness is
   enforced by the runtime validator; leaving scalar members
   uninitialized only introduces indeterminate values before that
   validation boundary.

If a future linkml release adopts these (check sort_classes handling
class-ranged slots and generate_field defaulting required fields),
delete the overrides below and go back to the bare cli import.

See CLAUDE.md "Ontology changes" for the regeneration workflow.
"""

from linkml.generators.cppgen.cppgen import CppGenerator, cli


def _sort_classes(self, clist):
    """Sort classes so that every dependency appears before its users.

    Dependencies are (a) parents: is_a plus mixins, which become C++
    base classes, and (b) class-ranged slots, which become by-value
    struct members and so need the member's struct complete first.
    (b) mirrors exactly the slots generate_struct will emit for the
    class: direct slots when it has base classes, all slots otherwise.
    Self-references are skipped (a struct cannot embed itself by
    value; that case is broken regardless of order).

    Raises ValueError on a dependency cycle, which no declaration
    order could compile anyway.
    """
    clist = list(clist)
    class_names = {c.name for c in clist}
    sv = self.schemaview

    deps = {}
    for cls in clist:
        d = []
        if cls.is_a:
            d.append(cls.is_a)
        if cls.mixins:
            d.extend(cls.mixins)
        direct = bool(d)
        for sn in sv.class_slots(cls.name, direct=direct):
            rng = sv.induced_slot(sn, cls.name).range
            if rng in class_names and rng != cls.name and rng not in d:
                d.append(rng)
        deps[cls.name] = d

    slist = []
    while len(clist) > 0:
        can_add = False
        for i in range(len(clist)):
            candidate = clist[i]
            placed = {p.name for p in slist}
            if set(deps[candidate.name]) <= placed:
                can_add = True
                slist.append(candidate)
                del clist[i]
                break
        if not can_add:
            raise ValueError(
                f"Could not topologically sort classes: "
                f"{[c.name for c in clist]} not resolvable given "
                f"{[s.name for s in slist]}"
            )
    return slist


_upstream_generate_field = CppGenerator.generate_field


def _generate_field(self, slot, cls):
    result = _upstream_generate_field(self, slot, cls)
    if result.field.default_value is None:
        # A required field still needs deterministic C++ object state.
        result.field.default_value = "{}"
    return result


CppGenerator.sort_classes = _sort_classes
CppGenerator.generate_field = _generate_field

if __name__ == "__main__":
    cli()
