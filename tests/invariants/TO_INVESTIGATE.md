# TO-INVESTIGATE — tests whose asserts encode something physically wrong

**What this file is.** The book of asserts found, during the assert-protocol
migration (physics skill, 2026-08-21), to pin behaviour that is not correct
physics: a body that hovers, an outcome only a removed hack produced, a
gravity-direction assumption baked into a bound, energy appearing or leaving
without a mechanism.

**The rule this file exists to honour.** None of these were deleted, weakened
or turned off. Red is information and so is a green that is green for the
wrong reason. Each entry is booked here, carries a `TO-INVESTIGATE` comment
block at the top of its own file, and has the same reason in its
`TEST_AUDIT.jsonl` row's `known_open`. They are for the owner to study one at
a time; the exit-code behaviour of every one of them is unchanged.

---

## test_collision_bounds_rotation — part [1c], fallen-log placement offset

**ADJUDICATED 2026-08-21: REPAIRED, born red. The defect is in the generator.**

**What it asserted.** That every fallen-log preset's oriented bottom sits
strictly ABOVE the ground it targets (`all_float`), and that this is the
passing case.

**Diagnosis: generator offset, not test error.** `FallenTreeGenerator`
(`src/worldgen/fallen_tree_generator.cpp:170-177`) places each segment centre
at `world_z + seg_len_z * 0.5f` and states the assumption in its own comment:
"The half-extent is half the segment length, plus the 1.1 overlap the caller
applies." That was true only while bounds were rotation-blind. `create_segment`
(`:242-259`) lays the log down with `rotation_y = pi/2` and writes the LENGTH
into the `thickness` field, so under oriented bounds the world-Z half-extent is
half the DIAMETER. The same offset therefore lifts every preset clear of the
ground by `(seg_len_z - diameter) / 2`. The test reconstructs that arithmetic
line for line and reproduces the lift exactly.

**Measured lift, against a SLOP of 0.0010 m.**

| preset | oriented bottom | lift |
|---|---|---|
| fallen_trunk | +0.2600 m | +0.2600 m |
| fallen_log | +0.2083 m | +0.2083 m |
| fallen_branch | +0.2875 m | +0.2875 m |
| twig | +0.2400 m | +0.2400 m |

**What changed.** The check now asserts INV-4 — the oriented bottom lands ON
the target ground within SLOP, neither hovering nor buried — and prints the
residual lift per preset plus the worst case. Before: 27 checks, 0 failed
(PASS). After: 27 checks, 1 failed (FAIL). No engine code was touched: the
test is now red in the honest direction, and goes green the day the generator
offset uses the oriented half-extent.

**CI.** This file is registered with `add_headless_test`, which makes it a
ctest in the headless-only profile. It runs in the **PR-gating headless-linux
lane** and in headless-windows. `TEST_AUDIT` now carries `expect: fail`. No CI
file was edited; the gate will go red until the generator is corrected.

---

## test_no_overlap_at_creation — CONTRADICTS-A-RULING

**ADJUDICATED 2026-08-21: no side taken, and none may be taken here. Two owner
decisions eleven days apart give opposite answers to the same question. This is
the write-up; the ruling is the owner's. No code change was made — the file
already says, in its own verdict line, that it enforces nothing.**

**The question in one sentence.** May a GENERATOR hand the solver a world in
which two bodies already interpenetrate?

### Position A — the owner decision of 2026-08-02

Verbatim, from the policy block this test prints on every run
(`tests/test_no_overlap_at_creation.cpp:249-255`):

> POLICY (owner decision, 2026-08-02): MINIMAL OVERLAP IS ACCEPTED.
> Driving this to zero was tried and it cost the tree: rejecting a colliding
> branch drops its whole subtree, and 149 bodies became 59. The complexity
> IS the tree, so the gate moved to what actually shows on screen, which is
> whether bodies get LAUNCHED. That gate is test_foliage_stays_attached:
> peak speed under 2 m/s, mean canopy drift under 0.5 m.
> This file REPORTS the overlap that remains, so it cannot creep unnoticed.

**Provenance caveat, and it matters.** That block is the ONLY carrier of the
decision. `LEDGER.md` has no 2026-08-02 entry, and no design doc records it.
The wording above is a test comment written after the fact, not a quoted owner
sentence, so this write-up cannot present it as the owner's exact words — only
as the decision as the code recorded it.

### Position B — INV-30, ruled 2026-08-13, `status: active`

Verbatim from `INVARIANTS.jsonl`, INV-30 `external-writers-place-nothing-illegal`:

> A subsystem that owns a body's position from outside the solver (FK
> animation, KINEMATIC drivers, chunk streaming, generators) may not hand the
> solver a state it would never have produced: no frame begins with an overlap
> beyond SLOP or a below-turtle placement created by a non-solver writer. The
> doors INTO the solver are closed, not just the physics inside it.

Its mechanism field: "Enforcement STRICT-FIRST by owner ruling 2026-08-13:
catch violators loudly and fix fast, lenient mode only as the inventory lever."
Generators are named in the statement, explicitly, among the writers it binds.

### What each would demand of this file

| | Position A (2026-08-02) | Position B (INV-30, 2026-08-13) |
|---|---|---|
| verdict on generated overlap | accepted, reported only | illegal beyond SLOP |
| this file's exit code | always 0 (what it does today) | red on any pair deeper than SLOP, unless run under the inventory lever |
| where the gate lives | `test_foliage_stays_attached` (peak speed <= 2 m/s, mean canopy drift <= 0.5 m) | here, at creation, before frame one |
| what a fix means | leave the generator alone; watch for launches | reject or re-place the colliding branch, and pay for it in canopy |
| cost named by its own advocate | 149 bodies become 59 | a subtree lost per rejected branch, same cost, judged worth paying |
| what INV-30 needs if A stands | — | a written carve-out inside INV-30's own record, naming generated foliage |

### The size of the debt, measured today

Default tree (`generate_tree_with_roots`, seed 12345, no `engine.update()`):

- 11781 body pairs compared.
- **2 pairs** overlap deeper than SLOP.
- 0 pairs coincident (nothing is placed on top of nothing).
- Deepest overlap **0.3426 m**, branch 43 vs leaf 40 — 342 times SLOP.

Two pairs out of 11781 is what Position A means by "minimal". A third of a
metre is what Position B means by "a state the solver would never have
produced". Both readings are true of the same number, which is why this needs
a ruling and not an argument.

### What is owed

One sentence from the owner picking A or B, and then one edit:

- **If B**: this file gates, the tree case runs under the inventory lever
  until the generator is fixed, and `TEST_AUDIT` flips to `expect: fail`.
- **If A**: INV-30 gains the generated-foliage carve-out in its own record,
  with the 149-to-59 cost as the reason, so the exception stops living in a
  test comment. Either way the 2026-08-02 decision should land in `LEDGER.md`,
  where a decision of that weight belongs.

---

## test_sleep_diagnostics — RETIREMENT PROPOSED

**ADJUDICATED 2026-08-21: the mechanism this file diagnoses no longer exists.
Verified in the tree, not assumed. Nothing was deleted; the output is now
marked. The owner rules on retirement.**

**What it asserts.** Nothing; it returns `true` unconditionally and is honest
about being a diagnostic.

### The mechanism is gone, and the engine says so in its own words

`src/core/physics_system_v4.cpp:5048-5067` is the obituary:

> FRAME-GATED DAMPING: ERADICATED (2026-08-14, owner decree). A speed-gated
> *0.90/tick velocity tax lived here from 2025-12-11. It was written to kill
> numerical oscillation and could not tell oscillation from coasting, because
> it looked at SPEED — and oscillation is a signature, not a speed. ... Rest
> belongs to the sleep law (INV-18/24); dissipation belongs to modeled
> processes (INV-19). Nothing else may touch velocity.

and, four lines further down:

> The `low_velocity_frames` counter still ticks (wake sites reset it); its only
> remaining reader is diagnostics.

That reader is this file. A grep over `src/` and `include/` returns the counter
at exactly two kinds of site: the tick above, and resets at wake sites
(`:623`, `:2124`, `:2128`, `:2576`, `:2580`, `:5253`, `:5987`, `:5991`).
Nothing consumes it.

### The constants in its advice do not exist either

- No `LOW_VEL_THRESHOLD`, and no 0.5 m/s gate. The surviving constant is
  `DAMPING_VELOCITY_THRESHOLD = 0.4 f`, declared in `schema/physics.yaml` and
  generated into `src/generated/physics_constants.h:286` per INV-29.
- No 0.98 damping rate anywhere in the physics engine. The two `0.98f` literals
  in the tree are `IMPULSE_MEMORY_DECAY` (`physics_constants.h:391`) and an
  animation `ANGULAR_DRAG` (`humanoid_locomotion.cpp:5303`). This file measures
  neither.

### Three rulings the advice also breaks

INV-19 (damping only where a real dissipation process is modelled), INV-29
(a constant with physical meaning is a declared input, never a number tuned at
its call site), and G-44, closed: low speed is not a fixed point of the
dynamics, and quietness must price both channels in one currency, extremity
speed `sqrt(v^2 + (omega*r)^2)`. Acting on the file's advice would tune the
exact mechanism G-44 replaced.

### What its own measurements say today

393 of 393 bodies asleep at every checkpoint; the "damping range" band empty
from first sample to last; and the summary line reading

> Band 2 (damping range): 0 → 0 ✗ (NOT reduced - damping not helping!)

about a world that is entirely at rest. The verdict is generated by a rule
written for a mechanism that no longer runs.

### What changed, and what did not

Marked, not deleted. The DIAGNOSIS block is now fenced by a "READS A DEAD
MECHANISM, DO NOT ACT ON IT" banner naming the eradication, the surviving
constant and the three rulings; the historical advice is preserved verbatim
underneath. The Band 2 summary line no longer prints as a failure. The file
still runs, still asserts nothing, still exits 0.

### What is owed

An owner ruling between two options, both real:

- **Retire it.** Its subject is gone and its counter exists only to feed it.
- **Rewrite the bands to extremity speed** `sqrt(v^2 + (omega*r)^2)`, which is
  the only version that would say anything about sleep under G-44. The velocity
  distribution and sleep fractions are worth having; the currency is wrong.

---

## test_physics_minimal — REPAIRED 2026-08-21, still green

**ADJUDICATED: the middle branch is deleted and the band is the sibling's.
INV-34 (rest-is-reached), ratified 2026-08-21, is exactly this law and its own
mechanism field names this test as its second known violation. The pass now
requires BOTH `not_at_rest == 0` — `test_physics_minimal_v2`'s own predicate,
which is the engine's quietness verdict — and a maximum final speed under
0.01 m/s. The 0.01-0.1 m/s range FAILS.**

**It did not go red, and that is a measurement, not luck.** PHASE 1: max final
speed 0.0000 m/s, 1/1 at rest. PHASE 8: 0.0000 m/s, 16/16 at rest. PHASE 9:
same verdict. The tile scene settles honestly today, so the middle branch was
dead code protecting a defect the G-44 sleep work had already removed from it.
Tightening it cost no verdict anywhere it was run. `expect` stays `pass`.

The identical branch in `test_oscillation_diagnostic` did NOT survive the same
tightening: 0.0817 m/s on a gluoned scene, now red. Same defect, two scenes,
one of them already fixed.

**What it asserted before.** Three-branch verdict on the maximum final velocity
of a stack of tiles that nothing is touching: under 0.01 m/s passes as "at
rest"; between 0.01 and 0.1 m/s ALSO passes, printed as "Low oscillation —
damping working"; above 0.1 m/s fails.

**Why the middle branch is wrong.** INV-24: at steady state a scene performs
ZERO corrective work, and a correction that fires forever on the same body is a
perpetual-motion machine pumping energy uphill in nanometre installments. The
firings that earned INV-24 its record were 290 nm each; 0.1 m/s is five orders
of magnitude above that. INV-19: damping exists only where a real dissipation
process is being modelled, so crediting "damping working" for a residual
velocity names a numerical convenience as a physical process. This is the same
mask G-44 found under `test_tree_wiggly`, where a sustained 0.0294 m/s
oscillation in depth-3/4 oaks was being absorbed by the speed-only sleep entry
and the audited green was not a green. Its own sibling `test_physics_minimal_v2`
demands zero on the same scene shape, so the two files disagree about the same
law.

**What was owed, and what was done.** A ruling on whether the middle branch
fails, and at which bound. Answered from the registry rather than by taste:
INV-34 is the law, the sibling already spelled the band, and the two files
agreeing is the point. Still open for the owner: whether the bound should
eventually be G-44's extremity speed `sqrt(v^2 + (omega*r)^2)` rather than a
COM-speed band. `is_at_rest` already prices it that way internally, which is
why both halves are asserted.

---

## test_ancient_oak — the wiggle check is computed and then dropped from the verdict

**What it asserts.** `pass = !nan_detected && max_xy < 1.0 && max_z < 0.50 &&
total_segments > 0`. `wiggle_ok` (fewer than 10 wiggly frames, max trunk
velocity under 25 mm/s) is computed one line above and never enters `pass`. A
tree that vibrates forever without drifting prints "WARNING: Tree stands but
WIGGLES" and returns true.

**Why that is wrong.** INV-24: at steady state a scene performs zero corrective
work; a sustained trunk velocity with nothing touching the tree is a correction
firing forever. INV-3: whatever sustains it is energy arriving from a source
the ledger does not see. It is the same mask G-44 found under
`test_tree_wiggly`, where TEST_AUDIT records that "the audited green was a
mask".

**What is owed.** A ruling on whether `wiggle_ok` joins the verdict, and at
which bound — G-44 argues the bound should be extremity speed, not COM speed.
Nothing changed; exit code unchanged.

---

## test_tree_wiggly — both pass bands were loosened in place

**What it asserts.** `max_velocity < 0.025f` (comment: "Relaxed from 0.01") and
`wiggly_frames < 15` (comment: "Relaxed from 10").

**Why that is wrong.** Loosening an assertion until it passes is the move
`docs/testing_guidelines.md` forbids outright — the rule is to say which was
wrong, the code or the expectation. The consequence is already booked in this
test's TEST_AUDIT row: G-44 unmasked a sustained 0.0294 m/s oscillation in
depth-3/4 oaks that the speed-only sleep entry was absorbing, which sits inside
the relaxed band and outside the original one. INV-24 and G-44 are the laws.

**What is owed.** The ruling already pending on this test should also decide
whether the two relaxations stand, and whether the band becomes extremity speed
per G-44. Nothing tightened here; exit code unchanged.

---

## test_knockback_scene — the control asserts that one body ends up inside another

**What it asserts.** In the no-rule case, `without.closest < kTouchDistance` is
a PASSING condition: the predator must end up inside the prey. The file
explains why honestly — both bodies are KINEMATIC, the solver moves neither,
and the AI drives one straight through the other.

**Why that needs a ruling.** As an assert it pins an illegal world as expected.
INV-30: a subsystem that owns a body's position from outside the solver may not
hand it a state it would never have produced, no frame beginning with an
overlap beyond SLOP, enforcement strict-first. INV-2: no two bodies
interpenetrate beyond SLOP. The day a driver stops walking its body through
another one, this control goes red and the correct fix reads as a regression —
the same shape as the fallen-log ratchet above.

**What is owed.** The contrast is real and the with-rule case genuinely needs a
baseline, so the question is only how the baseline is spelled: an explicit
expect-fail ("they overlap TODAY, here is the ticket") rather than an
expect-pass. Nothing changed; exit code unchanged.

---

## test_oscillation_diagnostic — REPAIRED 2026-08-21 as a regression test, BORN RED

**ADJUDICATED. The question was diagnostic or regression test, and it was
decided mechanically, not by taste.** A diagnostic reports and never claims, so
it would carry no PASS band at all. But this file already carried a band that
CAN return false — above 0.1 m/s it fails — and demoting it to a pure reporter
would take a test that can go red and make it unable to. That is a weakening,
and weakening is the move this whole pass exists to undo. Its structure says
the same thing: a deterministic scene, a measured final velocity, and a verdict
on that measurement is regression structure whatever the filename says. So the
band stays and becomes the law's.

**The band is now `not_at_rest == 0` AND max final speed under 0.01 m/s**, the
same as its sibling `test_physics_minimal`. Laws named in the asserts: INV-34
(rest-is-reached, ratified 2026-08-21), INV-24 (zero corrective work at steady
state), INV-22 (the file's own hypothesis — a gluon row and a turtle contact
governing one pair — stated as the law it always was), with G-44 behind
`is_at_rest`'s currency.

**Measured, default phase 11 (64x64 floor, 16 trees, 32 rocks, 8 fallen trees),
5 s.** Before: max final speed 0.0816663 m/s, verdict PASS via the middle
branch. After: same 0.0816663 m/s plus **173 of 5553 bodies never at rest**,
verdict FAIL. Booked `expect: fail`, known_open, same front as the G-44
gluon-tree oscillation RCA already on the board. It is in no CI lane: the
standalone harness tests are reached only by an explicit `--test <name>`.

**What it asserted before.** Identical three-branch verdict: under 0.01 m/s is
"at rest"; 0.01 to 0.1 m/s ALSO passes, printed as "Low oscillation — damping
working"; above 0.1 m/s fails. The subject is a gluoned tile scene on the
turtle that nothing is touching.

**Why the middle branch is wrong.** Same two laws as `test_physics_minimal`:
INV-24 (zero corrective work at steady state; the firings that earned it were
290 nm each) and INV-19 (damping only where a real dissipation process is
modelled). Sharper here, because the file's stated GOAL is to reproduce a
0.5-1 m/s oscillation and its pass band absorbs a tenth of that. Its own
hypothesis — gluon constraints fighting turtle contacts — is INV-22 (exactly
one mechanism governs a pair), so the thing it exists to find is a law
violation it is configured to pass.

**What is still owed.** Not the band — that is settled and enforced. The RCA:
what sustains 0.0817 m/s and keeps 173 bodies awake on a scene nobody is
touching. The file's own hypothesis (gluon constraints fighting turtle
contacts, INV-22) is the first thing to falsify, and it now has a red test to
falsify it against.

---
