---
name: physics
description: Operating discipline for Logosphere physics work — invariants framework, sweep semantics, owner contract, ruling protocol. Load before touching solver, constraint, contact, or physics-ontology code.
---

# Logosphere physics — how this project operates

This skill carries PROCEDURE and POINTERS only. The machine truth
lives in the files below; if this skill ever disagrees with them, the
files win and this skill has a bug — fix it in the same commit.

"The owner" throughout means the project maintainer. The contract
applies to anyone working on physics here, human or agent.

## Owner contract (non-negotiable)

1. **The owner QAs interactively before ANY merge.** Every round of
   work ends with a windowed, interactive run of the relevant scene
   (Eden, the feature's own test in INTERACTIVE mode, or the demo the
   work touches) launched FOR the owner: tee output to /tmp, never
   kill or timeout the process, the owner closes it. Headless green is
   necessary, never sufficient. Do this unprompted.
   **Never pipe a run the owner is meant to see through `| head`, `|
   tail` or `| grep`.** It truncates the very output they asked for and,
   for a windowed run, the pipe can stop it executing at all. This one
   broke five separate runs before it was written down: "run it here
   with tee into tmp, no `| head`" and, four days later, "lets see how
   many times I need to remind you this one".
2. **The owner participates in every decision.** Present options with
   evidence and a recommendation if asked; the owner rules. Never pick
   "the obvious option" on their behalf, never merge without their
   word. Record every ruling in the ledger.
3. **Educate before asking for a ruling.** Each concept a decision
   touches gets a short plain explanation (Feynman style, concrete
   example) BEFORE the options. The owner decides everything, so the
   owner must be able to see what each option really is.
4. **No time estimates, ever.** Slices sequenced by dependency only.
   We move as slow as needed to build fundamentals.

## THE BOARD IS THE STARTING POINT (directive)

**`docs/todo_plans/PHYSICS_BOARD.md` is read before anything else, on
every physics session, without being asked.** It holds every open
front in exactly one class — CLEAN NOW (mechanism known, adds no debt,
no decision owed), NEEDS DESIGN (a study exists or is owed), OWNER
RULING (engineering ready, blocked on a decision), PARKED (real,
understood, with the reason). Every entry cites its evidence.

Four rules, non-negotiable:

1. **Start there.** Before proposing work, read the board and say
   which front you are on. "What should we do next?" is answered from
   the board, not from memory or from whatever was last discussed.
2. **A front not on the board does not get worked.** If work appears
   that is not there, put it on the board first — that is how it gets
   classified instead of just started.
3. **The board is updated in the SAME COMMIT as the work it
   describes.** A slice that lands moves its front; a front that
   changes class gets a line in `LEDGER.md` saying why. A stale board
   is worse than none, because it is consulted with authority.
4. **Protect the owner's attention.** The board exists because twenty
   open fronts cannot be held in one head. When reporting, lead with
   which class the work sits in and what decision (if any) is owed —
   never hand back an undifferentiated list.

## Read first (in this order)

- `docs/todo_plans/PHYSICS_BOARD.md` — **the board** (see the
  directive above). Where every front stands and what it needs.
- `tests/invariants/INVARIANTS.jsonl` — the laws. One INV per line;
  kind, derives_from, status (active | aspirational). Aspirational
  invariants are real commitments born red.
- `tests/invariants/LEDGER.md` — append-only rulings trail. Read the
  tail before proposing anything the owner may already have ruled on.
- `tests/invariants/TEST_AUDIT.jsonl` — every physics test: what it
  proves, which INVs it touches, expected verdict, known_open.
- `tests/invariants/GEDANKEN.jsonl` — the Gedankenexperimente. The
  simplest possible thought experiment for each design question
  (bullet into wall, sphere into mud), recorded the way an INV is
  recorded: setup, question, expected, composition, carrier, status.
  **Owner directive, 2026-08-16: "we need to gedankenxperimente hard
  from now on... we need to blast this."** Every design question spawns
  its experiments BEFORE any options are presented and long before any
  code. An experiment written after the code it describes is a failure
  of this rule, and must say so in its own notes field rather than be
  backdated (see GEDANKEN-11).
- `docs/todo_plans/PHYSICS_PIPELINE_SEQUENCE.md` — the solve loop as
  it actually executes, with the hazard edges. Any solve-loop change
  must respect (and update) this.
- The active campaign doc in `docs/todo_plans/` (currently
  `ROTATION_CAMPAIGN_DESIGN.md`).

## Operating rules

- **Sweep**: `scripts/physics_sweep.py`, run ALONE — concurrent load
  manufactures phantom moles at the deadline. A MOLE is a live verdict
  contradicting the audited baseline (TEST_AUDIT `expect` +
  `known_open`), nothing else. Read `SWEEP_VERDICT:` markers, never
  wrapper exit codes.
- **INV-29, zero tolerance**: not one magic number in the physics
  engine. Every constant enters `schema/physics.yaml` and regenerates
  into `src/generated/physics_constants.h`. A new literal in a physics
  TU is a defect (`test_inv29_constants_gate` ratchets the residual).
- **Doors, not fallbacks**: missing or undeclared data refuses loudly
  (abort + actionable message); `*_LENIENT=1` env levers are the only
  downgrade, for inventory runs and deliberate bad-data fixtures.
- **Ladders born red**: acceptance tests are written before the
  mechanism and stay red-by-design until it lands. Never "fix" a red
  ladder by weakening it.
- **Levers default-off**: new mechanisms land behind an env lever,
  default preserving current behavior, flipped only after owner QA.
- **Evidence unfiltered first**: read the raw log with eyes before
  any grep; a filter encodes a hypothesis. "No output" must be
  verified wide before it becomes a finding.
- **Failed experiments are evidence**: commit them honestly or park
  them behind a lever; never revert or discard without the owner
  asking. Stop and report instead.
- **Engine invariants** (particles are bodies, turtle is the only
  immovable, no gravity assumptions, no springs as patches, no
  if-statement edge fixes) are in the repo `CLAUDE.md` and bind here.

## Instrument the INTERACTIONS, not just the outcomes (directive)

**Owner ruling 2026-08-15.** A test that asserts where a body ended up
has not checked that the right thing happened to it. Every physical
interaction a test depends on must be INSTRUMENTED AND ASSERTED, at
enough granularity that a reader can see the engine did what the prose
claims.

Concretely: "the box strikes the post square in the side" is not a
position check. It is a contact — between those two bodies, at a
height, with a normal, at some approach speed — and all four are
observable. `PhysicsSystem::get_collision_events()` reports every
contact (`particle_a/b`, `contact_x/y/z`, `normal_*`,
`relative_velocity`, `penetration`), so a stage can assert the strike
it names rather than infer it from where things came to rest.

The failure this prevents is real and recent: the machine's chain
"passed" stages while nobody had checked that the ball ever touched
the ramp, that each box in the row actually struck the next, or that
the flying box hit the post rather than landing beside it. Outcomes
can be produced by the wrong mechanism — a body reaching x=7.6 because
it was struck, or because it was nudged by a spurious overlap, look
identical in a position assert.

**The longer intent (build toward this deliberately).** These
assertions are the raw material for a vocabulary. As they accumulate,
abstract them into named physical interactions — `hit`, `rolling`,
`sliding`, `impact`, `rest`, `topple` — and move that vocabulary back
INTO the engine, sourced from the ontology, so the words mean exactly
one thing everywhere: in tests, in game rules, in the KG, in the
narrative layer. A test that says "assert_hit(box, post, on_side)" and
an engine that emits `hit` with the same semantics are the same
sentence spoken twice. That is the direction; write each assertion as
if it were about to become a building block, because it is.

## GEDANKENEXPERIMENTE COME FIRST (directive, 2026-08-16)

Owner: *"we need to gedankenxperimente hard from now on... we need to
blast this."*

**Every design question spawns its thought experiments BEFORE any
options are presented, and long before any code.** The method is the
owner's, stated when the registry was created:

1. Start from the SIMPLEST possible experiment. Bullet particle into
   wall particle. Sphere into mud. Not the interesting case, the
   irreducible one.
2. For each, work out which mechanisms must be in place for the
   COMBINATION to produce the right overall behaviour, across
   animation and physics together.
3. From those, derive what the language and the semantics must allow.

Each is recorded in `tests/invariants/GEDANKEN.jsonl` the way an INV is
recorded: `setup`, `question`, `expected`, `effects_required`,
`composition`, `invariants`, `carrier` (with `[E]` file:line evidence
or `[GAP]`), `forces`, `status` (`settled` | `open`), `notes`.

Three rules that make the registry worth having:

- **An experiment written AFTER the code it describes is a failure of
  this directive.** Record it, say so in its own `notes` field, and do
  not backdate it. GEDANKEN-11 is the standing example.
- **Prefer the experiment that can come out either way.** One whose
  answer is already obvious teaches nothing. GEDANKEN-12 exists only
  because it removes one detail from GEDANKEN-11 and flips the answer,
  which is what exposed that effects never declare what they write.
- **`status: open` is the useful state.** An open experiment names
  exactly what is missing and why, and several of them are the best
  arguments in the design (GEDANKEN-16 is the whole justification for
  hierarchical addresses). Do not rush them to `settled`.

## FULL-STATE NARRATION: assert or waive, per degree of freedom (directive, 2026-08-19)

Owner-approved after a week in which every loose assertion had the same
shape: a test asserted ONE degree of freedom while the experiment had
thirteen, and the owner's eye kept catching what the asserts never
looked at (a cube that slid 6.36 m without ever turning; a sphere
resting 1.9 m inside a ramp that no assert could see).

**Before the asserts, the test writes the full-state narration**: for
every tracked body, what each degree of freedom does in each phase —
position, velocity, orientation (BOTH ledgers), angular velocity, and
the RELATIVE quantities (separation, approach, height above a surface).
**Then every narrated DOF gets an assertion or an explicit one-line
waiver naming why not.** An unasserted DOF is a visible decision, never
a blind spot. `tests/test_cube_drop_ladder.cpp` is the pattern.

**ARGUS is the instrument** (`src/core/argus.h`, pure engine module,
owner-named): declare the experiment's cast with `watch(id, label)`,
call `observe(ps, frame)` after each step, and assert the SAME
quantities the dump prints — `separation`, `approach_speed`, `spin`,
`peak_spin`, `divergence` (q-vs-Euler coherence). One source for the
assert and the log, so what is checked and what is seen cannot drift.
Read-only over particles by construction: the witness cannot perturb
the experiment. Zero cost unwatched. Engine-wide by owner ruling —
combat and AI observe through the same eyes.

## "FIXED" IS A PROTOCOL, NOT A SENTENCE (owner decree, 2026-08-20)

Nothing is declared fixed until ALL THREE hold:

1. **Argus-witnessed.** The claimed physics is observed by the witness
   (`src/core/argus.h`), not read off a print or a grep of test output.
2. **Assertion-clamped, in the claim's own mode.** A test enforces the
   claimed semantics in the exact configuration the claim is made for —
   a mechanism behind a lever needs its LEVER-MODE contract asserted
   (both modes audited in TEST_AUDIT), or the claim has no enforcement
   and is one refactor from silently false.
3. **Human-QA'd, informed.** The owner watches it interactively AND has
   the education to know what changed and why, before the word is used.

Until all three: say "measured", "landed behind the lever", "green
under the lever" — never "fixed". The decree's origin: four torque
slices reported as fixes on lever-mode numbers that no assertion
enforced, while the tests asserted only the default path.

## Ruling protocol

0. **Gedankenexperimente first.** Before options exist, the simplest
   experiments that discriminate between them are recorded. If the
   question cannot be reduced to an experiment, it is not yet
   understood well enough to put to the owner.
1. Bring the question with its education (rule 3) and options with
   evidence. One question at a time when the owner says so.
2. Owner rules → append the ruling to `LEDGER.md` with date and the
   owner's words where they carry the reasoning.
3. Update `INVARIANTS.jsonl` if the ruling creates or amends a law
   (aspirational status allowed; owner-dictated invariants are marked
   `origin: owner decree`).
4. If the ruling changes PROCEDURE, update this skill in the same
   commit. Stale procedure loaded with authority is worse than none.

## Ship checklist

Board updated (the front moved, or a new one classified) →
Gedankenexperiment recorded for anything new in BEHAVIOUR, and its
status honest → claimed semantics Argus-clamped by assertions IN THE
CLAIM'S MODE (lever state included), both modes audited → headless suite green → sweep ALONE, zero moles →
docker precheck
(`scripts/precheck_linux.sh`, read the markers) → CHANGELOG entry for
anything user-facing → DCO sign-off on every commit (`git commit -s`)
→ interactive QA launched for the owner → owner says merge.
