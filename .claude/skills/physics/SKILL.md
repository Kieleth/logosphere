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

## Ruling protocol

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

Board updated (the front moved, or a new one classified) → headless
suite green → sweep ALONE, zero moles → docker precheck
(`scripts/precheck_linux.sh`, read the markers) → CHANGELOG entry for
anything user-facing → DCO sign-off on every commit (`git commit -s`)
→ interactive QA launched for the owner → owner says merge.
