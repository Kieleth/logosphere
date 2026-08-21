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

**What it asserts.** That every fallen-log preset's oriented bottom sits
strictly ABOVE the ground it targets (`all_float`), and that this is the
passing case.

**Why that is wrong.** A log placed on the ground and floating above it is not
a correct placement. INV-4 requires a generated structure to be born at rest
with no overlap beyond slop; a hovering body is the same defect with the sign
flipped — its support is not where the generator thinks it is. The block is
honest that it is recording a measured consequence rather than a law ("Reported,
not fixed"), but as written the test goes RED the day FallenTreeGenerator's
offset is migrated to oriented bounds. A correct fix would look like a
regression.

**What is owed.** An owner ruling on whether the ratchet inverts — assert the
bottom lands ON the ground — in the same change that corrects the generator
offset. Until then the number it pins is the size of the debt and must not be
loosened.

---

## test_no_overlap_at_creation — the policy contradicts INV-30

**What it asserts.** Nothing. The function returns `true` unconditionally and
prints its findings. Its own policy block records an owner decision of
2026-08-02 that "MINIMAL OVERLAP IS ACCEPTED", because driving overlap to zero
cost the tree (rejecting a colliding branch drops its whole subtree, 149 bodies
became 59), and moves the gate to `test_foliage_stays_attached`.

**Why that needs a ruling.** INV-30
(`external-writers-place-nothing-illegal`) was ruled on 2026-08-13, eleven days
after that policy, and states the opposite in the same words this file's title
uses: a subsystem that owns a body's position from outside the solver —
generators named explicitly — "may not hand the solver a state it would never
have produced: no frame begins with an overlap beyond SLOP", with enforcement
"STRICT-FIRST by owner ruling, lenient mode only as the inventory lever". INV-30
is `status: active`. A test titled "nothing may be created inside something
else" that cannot go red on creation inside something else is the exact shape
of a green that means nothing.

**What is owed.** One of two rulings. Either INV-30's strict-first enforcement
covers generators, and this file gates (with the tree case running under the
inventory lever until the generator is fixed); or the 2026-08-02 acceptance
still stands for generated foliage, and INV-30 gets that carve-out written into
its own record rather than living only here. Nothing was changed: the reported
overlap numbers are the size of the debt under either ruling.

---

## test_sleep_diagnostics — its printed advice contradicts INV-19, INV-29 and G-44

**What it asserts.** Nothing; it returns `true` unconditionally and is honest
about being a diagnostic.

**Why it needs a rewrite.** The DIAGNOSIS block it prints tells the reader that
"Adaptive damping threshold (0.5 m/s) is TOO LOW", to "Consider raising
LOW_VEL_THRESHOLD to 1.0 m/s", and that the "damping rate (0.98) is too weak".
Three later rulings say otherwise. INV-19: damping exists only where a real
dissipation process is being modelled, and damping added for numerical
convenience or stabilisation is forbidden. INV-29: a constant with physical
meaning is a declared engine input in `schema/physics.yaml`, not a number tuned
where it is consumed. G-44 (closed): low speed is not a fixed point of the
dynamics — an inverted pendulum passes through arbitrarily low speed while
accelerating away — and quietness must price both channels in one currency,
extremity speed `sqrt(v^2 + (omega*r)^2)`. The advice would tune the exact
mechanism G-44 replaced.

**What is owed.** The measurements are still worth having; the recommendation
text needs rewriting to the ruled world, and the bands it reports should be
extremity speed rather than COM speed if it is to say anything about sleep
under G-44. Nothing was changed here.

---

## test_physics_minimal — "low oscillation, damping working" is counted as a pass

**What it asserts.** Three-branch verdict on the maximum final velocity of a
stack of tiles that nothing is touching: under 0.01 m/s passes as "at rest";
between 0.01 and 0.1 m/s ALSO passes, printed as "Low oscillation — damping
working"; above 0.1 m/s fails.

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

**What is owed.** A ruling on whether the middle branch fails, and at which
bound — most likely G-44's quietness bound in extremity speed rather than a
COM-speed band. Not weakened here; exit code unchanged.

---
