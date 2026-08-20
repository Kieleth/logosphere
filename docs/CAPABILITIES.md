# What Logosphere can do, and how finished each part is

One page, so nobody has to assemble this from four documents written for
four other purposes.

Every claim here names what backs it. Where a claim has nothing automatic
behind it, it says so, because "it works" and "it will keep working" are
different promises and only one of them is worth much.

**Status vocabulary**, used exactly:

| | Meaning |
|---|---|
| **Gated** | It works, and an automatic check goes red when it stops working. The gate is named. |
| **Works, ungated** | It works today. Nothing would tell you if it broke. |
| **Decided, not built** | A dated decision is on file. No implementation yet. |
| **Designed** | A stated contract and an extension point. Nobody has committed to building it. |

This page links the detailed documents rather than restating them. Where it
disagrees with [REFLECTION_PROTOCOL.md](REFLECTION_PROTOCOL.md), the
[RPG_MODULE.md](RPG_MODULE.md) ledger, or
[tests/invariants/INVARIANTS.jsonl](../tests/invariants/INVARIANTS.jsonl),
this page is the stale one.

---

## How much you have to adopt

Nothing is all-or-nothing. Three build profiles, and each is a real product
rather than a stripped demo.

| Profile | You get | Platform |
|---|---|---|
| `core` | Knowledge graph, ontology, capability rules, damage, event bus, game time. No GPU, no window. | Any C++17 toolchain. Linux gated in CI, Windows advisory under MSVC. |
| `physics` | The above plus a render-free `Engine`: solver, contacts, gluons, dynamics, locomotion, worldgen. | Any C++17 toolchain. Linux gated in CI. |
| `full` | Everything, plus software rasterizer, Metal lighting, GLFW, the example games. | macOS arm64. |

The language model is not on that list because it is not a layer. It is
`LLMSystemHTTP`, nullable, and the engine never asks whether it is there. A
puzzle game that uses no AI loses nothing that was built for AI: see
[WHY.md](WHY.md) for why the two turn out to be the same engine.

---

## Particles, bodies, and contacts

**What it does.** Everything in the world is a particle: a body with mass,
density, and material that occupies space and collides. Spheres, boxes, and
ellipsoids. No triangle meshes anywhere. XPBD solver with iterative contact
resolution. Rotated boxes collide as rotated boxes through a 15-axis SAT
narrow phase with reference-face clipping, and the broad-phase bounds
enclose the true oriented extent rather than an axis-aligned slab.

**What proves it.** 31 invariants in
[`tests/invariants/INVARIANTS.jsonl`](../tests/invariants/INVARIANTS.jsonl),
28 active and 3 aspirational, of which 22 are verified at runtime, 7
statically, and 2 by ledger. INV-12 `true-geometry-contacts` is the oriented
contact law. INV-8 `rows-live-sized`, INV-30
`external-writers-place-nothing-illegal`. The `physics-linux` CI job builds
the render-free engine and runs the locomotion guard suite on every pull
request.

**Status: gated.**

**What it cannot do yet.** Ellipsoids collide through a conservative
per-axis bounding box, not an oriented one
(`src/core/narrow_phase.cpp:470`). Three invariants are marked aspirational,
which means the property is wanted and not yet enforced.

---

## Light and rendering

**What it does.** A software rasterizer writes the framebuffer. Metal
compute shaders trace the light. Three passes every frame: G-buffer, then
shadow and lighting with BVH ray traversal for all lights at once, then
apply. Light is computed and shadow is the absence of it, so moving a lamp
produces a correct shadow with nothing pre-baked.

**What proves it.** The `full-macos` CI job builds the whole pipeline and
runs the unit suite. Visual behaviour has its own test discipline in
[VISUAL_TESTS.md](VISUAL_TESTS.md).

**Status: works. The macOS build is advisory in CI, not merge-blocking.**

**What it cannot do yet.** The full profile is macOS arm64 only. No OpenGL,
Vulkan, or DirectX backend exists, and porting the lighting means porting
the Metal compute shaders (see [PORTING_SHADOWS.md](PORTING_SHADOWS.md)).

---

## The knowledge graph and the ontology

**What it does.** Every particle is a node. Types, slots, relations, and
events are declared in LinkML YAML; a generator emits typed C++; the
registry gates every write above it. `extend()` grows the ontology at
runtime. There is no privileged writer: not a seed, not the engine, not a
model. An undeclared write is refused, at load, with a reason.

**What proves it.** The `ontology-generation` CI job regenerates every
output from the schemas and rejects drift, so a hand-edited generated file
fails the build. Roughly 55 standalone headless tests cover the graph,
capabilities, damage, events, and game time, and run in the `headless-linux`
job. The `core` profile is proved portable by installing the package to a
prefix and building an external consumer against it
(`examples/consumer-smoke`), which is a different claim from "our own tests
pass".

**Status: gated.**

**What it cannot do yet.** No built-in save and load; persistence is the
game's job. `IApplication::create_initial_scene` still takes only a
particle-add callback (TODO[ARCH-003]).

---

## Reflection: somebody else's rulebook becomes playable

The engine reads a published body of rules and turns it into game knowledge
that cites itself. This is the newest capability and the one with the most
ground still to take, so it is documented at more length than the rest.

The protocol names nine layers from source text to arbiter, each with a
contract and a stated replacement condition:
[REFLECTION_PROTOCOL.md](REFLECTION_PROTOCOL.md).

**What runs today, and what gates it:**

| Property | Gate |
|---|---|
| Every captured value carries the verbatim text it came from, copied and never retyped | The value verifier compares against the source at its address. Measured: **3,089 quotes, zero misses** |
| The verifier refuses rather than repairs | A verifier that fixes what it finds is a second author with no citation, so it returns a verdict and nothing else |
| Rules dispatch on type, never on a printed name | `test_renaming_every_table_changes_nothing`, `examples/logovger/test_chargen.cpp:925`: rename all 90-plus tables in the graph, play 40 lives against a control world, require zero differences |
| Nothing absorbed is left unreached | Coverage gate (no table reached by nothing) plus instantiation gate (no declared type silently uninstantiated) |
| One arbiter per open question, and it leaves a record | `ArbiterDecision`, written by the session at `examples/logovger/chargen/chargen.cpp:465`, so no driver can forget it |
| A seed may point at what another owns and never write onto it | `create_only` slots, and additive batches refuse `destroy_entity` |

**Status: gated, for one chapter of one book.**

That last clause is the honest ceiling and it is worth stating plainly. The
absorbed source is the Cepheus Engine SRD. Seeds cite two of its 32 files.
Every claim about what generalises to the next book is a prediction, and the
protocol says so itself under "Known thin ice".

**Why this is worth your attention anyway.** The discipline catches things
that ordinary testing does not. Twenty-three careers had a rank 0 skill
grant that was cited to its cell, verified against the book, and counted by
an invariant, and was read by no code at all: every generated character came
out a skill level short, with every fidelity check green. That defect is
what the coverage gate now exists to catch. Reading the book also surfaced
four defects in the published rules, reported upstream rather than quietly
corrected. See [RULES_AS_DATA.md](RULES_AS_DATA.md) on the three separate
questions (fidelity, coherence, consequence) and why passing the first two
hides a failure of the third.

**Decided 2026-08-15, not built.** Extraction moves to paired model readers,
one positive and one adversarial, with a separate arbiter deciding what
enters, and a tool that returns the exact bytes at an address so the model
judges structure and never retypes content. What killed the previous
mechanical parser, measured across the whole SRD: 8.5% of pipe-prefixed
lines unaccounted for, 14 tables cut at a width mismatch, and at
`book3/planetary-wilderness-encounters.md:326` a legal three-cell row inside
an eight-column table ends the table early and promotes a data row into a
column header, manufacturing a phantom table with no error anywhere. Silent
corruption, not honest loss. The ledger entry is in
[RPG_MODULE.md](RPG_MODULE.md).

**Designed, and it is one layer.** PDFs, OCR passes, and scanned books
replace markdown at L0 and the layers above cannot tell, provided the
replacement answers two questions: what is the address, and what are the
exact bytes there. `REFLECTION_PROTOCOL.md:58-62` states the contract and
calls it the only extension point needed to absorb a scanned book. Nothing
implements it today, and image regions are reserved in the address grammar
and unimplemented.

**Open after the 2026-08-17 provenance spike.** If a scan is the
authoritative source, treating its OCR transcript as the L0 source loses the
derivation path back to the pixels. `todo_plans/SOURCE_LOCATION_PROVENANCE_SPIKE.md`
separates the pinned image target from its model-generated transcript and
records the standards evidence. That is a research finding, not a changed
protocol. The owner must decide the boundary before the paragraph above or
`REFLECTION_PROTOCOL.md` changes.

**Ungated, and named as such.** Two rules in the protocol have no automatic
check: that content is copied rather than retyped (R3), and that
un-ingested content leaves a row saying so with a reason (R7). R7 is called
the largest hole in the protocol, and closing it is what the ledger work is
for.

---

## Determinism, record, and replay

**What it does.** Any game runs headless, with no window and no clock. What
happened is kept separate from what was decided. A run records to a tape and
replays byte for byte, so a bug found by sweeping thousands of runs arrives
as a seed rather than a description.

**What proves it.** INV-27 `deterministic-given-seeds`: the same scene
stepped the same number of times in the same build produces bit-identical
state. In CI, the character generator plays 60 lives end to end through the
shipped binary, and `test_chargen` plays 288 lives in-process. The 60 is
justified in the workflow rather than picked: against the 9.5% abort rate
the gate was built to catch, sixty lives miss it 0.25% of the time.

**Status: gated.**

See [RECORD_AND_REPLAY.md](RECORD_AND_REPLAY.md) and
[OBSERVING_CHANGE.md](OBSERVING_CHANGE.md).

---

## The language model, if you want one

**What it does.** `LLMSystemHTTP` generates text against a remote provider
or a local server. Where a model is given authority over the world, it holds
the same ops grammar the game holds and its writes are validated against the
same ontology, so it has the same hands and no more. Where a model narrates,
it is handed facts and asked only for prose, and cannot contradict what the
dice already decided.

**Status: works, optional, nullable.**

**What it cannot do yet.** No provider is bundled and no key ships with the
engine. Latency between a decision and its narration is measured by
`logovger-bench-narrator` rather than assumed.

---

## What is absent, and not planned soon

- **No scripting layer.** Games are C++ binaries linked against the library.
  This is deliberate for now and it is the single largest barrier to entry.
- **No editor.** The world is declared in ontology YAML and built in code.
- **No save/load.** The game owns persistence.
- **Windows and Linux graphics.** Headless profiles only.
- **A second game has never replaced the game-policy layer.** The boundary
  that says a different game touches nothing below it is asserted and
  untested. Until someone does it, treat it as a claim.

---

*Pre-1.0. The API breaks between commits and every break lands in
[CHANGELOG.md](../CHANGELOG.md).*
