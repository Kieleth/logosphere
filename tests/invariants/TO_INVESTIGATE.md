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
