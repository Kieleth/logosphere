# Logovger — Agent Instructions

Read the README first: the discipline section is binding. Three rules
dominate everything:

1. **The book is the spec.** Never invent a mechanic, a table entry, a
   modifier. Every rule implemented cites its SRD section in a comment.
   If the two SRDs disagree, record the divergence in
   docs/ABSORPTION_INVENTORY.md, choose per section, say why.
2. **The referee cannot roll.** Dice are engine-side, seeded and
   logged. The LLM receives results as facts and narrates. Any design
   that lets the model assert a die result is wrong by construction.
3. **Every delivered feature is visually verifiable** — inherited from
   the engine repo (docs/testing_guidelines.md there, rule 12).
   Headless asserts the numbers; a watchable mode ships with the
   feature, same PR.

Legal lines, non-negotiable: SRD text under its license with the
license text present; no TSR/GDW/FFE original book text; no EA
Sentinel Worlds names, text or assets (tone inspiration only); no
Traveller trademark in anything user-facing.

This example lives in the engine repo; the root CLAUDE.md applies in
full. The engine stays generic: space-era capabilities this game needs
(star systems, ship interiors, subsector rendering) are ENGINE
mechanisms, built behind engine seams with their own tests, never
hardcoded here. If a change would make sense in a fantasy game and this
one equally, it is engine; if it smells of Traveller, it is this game's.
