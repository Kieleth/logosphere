# INV PROPOSALS — laws a test already enforces that the registry does not hold

**What this file is.** A review queue for the owner, produced by the
assert-protocol migration (physics skill, "EVERY ASSERT NAMES ITS LAW",
2026-08-21). Every existing assert was read and linked to the INV or G it
enforces. Where an assert makes a real physical claim and **no registry
record covers it**, the claim is written up here instead of being invented
into `INVARIANTS.jsonl` or `GEDANKEN.jsonl`.

**What this file is NOT.** It is not a law. Nothing here binds anything.
`INVARIANTS.jsonl` and `GEDANKEN.jsonl` are edited by the owner only, after
ratification. An assert citing a PROPOSED name is citing a queue entry, and
says so in its own text (`PROPOSED <NAME>: ...`).

**Format.** One section per proposal:
- **Name** — the slug an eventual INV/G would carry.
- **Claim** — one sentence, the law itself.
- **Derivation / reasoning** — why it is true, from first principles or
  from the mechanism, not from the code that happens to implement it.
- **Motivating assert** — the test and the exact assert that already
  enforces it today.
- **Suggested kind** — INV (a law the engine must hold) or GEDANKEN (a
  thought experiment that discriminates a design question), with the
  invariants it would derive from.

---

## FINITE-STATE

**Claim.** Every particle's position, velocity, orientation and angular
velocity remains a finite floating-point number at every frame boundary;
NaN and infinity are not physical states and no mechanism may produce one.

**Derivation / reasoning.** This is not a physical law about the world, it
is the precondition under which every other law is even measurable. INV-11
bounds speed, INV-3 bounds energy, INV-2 bounds penetration — every one of
those predicates evaluates to *false* silently when fed a NaN, because
every comparison against NaN is false. So a NaN does not trip the detonation
detector, does not trip the penetration gate, and does not trip the energy
ledger: it disables all three at once and the run reports green. That makes
finiteness strictly prior to the laws it protects, and worth its own record
rather than an implicit assumption inside each of them. It also has a
distinct mechanism story: NaN enters through division by a zero effective
mass, through a normalize() of a zero-length vector, and through
acos/sqrt of an out-of-domain argument — all three are guards at specific
sites, not consequences of any of the existing laws.

**Motivating assert.** `tests/test_falling_cube.cpp`, the `!nan_detected`
term of the final verdict (printed as "No NaN"). Also present unnamed in
`test_ancient_oak` ("no NaN") and several tree tests.

**Suggested kind.** INV, `derives_from: []` — it sits *under* INV-3 and
INV-11 rather than deriving from them. `verification: runtime`.

---

## REST-IS-REACHED

**Claim.** A scene under gravity with dissipative contacts and no external
input reaches rest in bounded time and stays there: every dynamic body's
speed and spin fall below the quietness bound and do not rise again without
a new input.

**Derivation / reasoning.** INV-24 states the *corrective* half — that every
repair mechanism terminates and a settled scene performs zero corrective
work. It does not state that the scene settles at all. Those are different
claims: a world whose repairs all terminate can still contain a body sliding
forever on a frictionless plane, which violates nothing in the registry
today. The physical content here is that the modelled dissipation channels
(Coulomb friction at contacts, material loss factor in bonds, drag) are
sufficient to remove the finite energy a bounded scene starts with, so the
settling time is finite. The distinction matters because most of the older
physics tests assert exactly this ("still moving at the end: 0 of N") and
currently have nowhere to point.

**Motivating assert.** `tests/test_settling.cpp::test_settling_wiggle`,
"a world nobody is touching must go quiet" — the `settled_at >= 0 &&
end.moving == 0` verdict, including the SETTLED-THEN-WOKE case. Same claim
in `test_baumgarte_ratchet` ("still moving at the end"), `test_falling_cube`
("Cube stopped"), `test_settling_flat` ("and is at rest").

**Suggested kind.** INV, `derives_from: ["INV-3", "INV-19"]` — energy is
never created, and damping is physical dissipation; together they give a
monotone energy budget that a bounded scene must exhaust.
`verification: runtime`.

---

## ONE-POSITION-WRITER

**Claim.** In a given frame, exactly one mechanism advances a given particle's
position, and that mechanism is determined by the particle's solver mode and
owner: the solver integrates solver-DYNAMIC bodies, the dynamics system moves
DYNAMICS-owned KINEMATIC bodies, FK writes ANIMATION-owned bodies. No particle
is integrated twice, and a particle no mechanism owns does not move at all,
whatever its velocity field says.

**Derivation / reasoning.** Position is the time integral of velocity, and an
integral evaluated twice over the same interval is not a physics error inside
any one mechanism — each one is individually correct — it is a category error
in who is allowed to speak. The observable signature is exact and unmissable
once you look for it: displacement is 2x the velocity field over the same
interval, so any velocity CAP applied by one writer fails to bind the ground
speed the world actually sees. INV-30 is adjacent but different: it governs
what STATE an external writer may hand the solver (no overlap, nothing below
the turtle), not how many writers advance the same body. G-22 asks the same
ownership question for ORIENTATION and answers "exactly one owner, and the
engine should know from ONE mechanism" — this is the position half of that
sentence, and G-22's own status (`open`, "today it knows from three, none of
which is aware of the others") argues that the position half deserves the
record rather than inheriting one.

**Motivating assert.** `tests/test_position_authority.cpp`: a1 (an unowned
KINEMATIC particle with vx=1 must not move), a2 (a solver-DYNAMIC particle's
horizontal displacement is exactly v*t, integrated once), and the b cases'
"hips velocity field reports true ground speed" term. The RCA in that file's
header is the concrete instance: `ParticleSystem::update` Euler-integrated x/y
for EVERY particle unconditionally, so humanoid and solver bodies both moved
about twice their velocity field and the FORGE speed cap did not bind.

**Suggested kind.** INV, `derives_from: ["INV-22"]` — one mechanism owns a
pair generalises to one mechanism owns a body's integration.
`verification: runtime` (a1/a2 are directly measurable) with a `static`
companion (grep for a second integrate site).

---
