# Physics invariants ledger

Append-only log of every change to `INVARIANTS.jsonl` and to test↔invariant
links in `TEST_AUDIT.jsonl`. One entry per change: what, why, evidence.
The JSONL files are the machine truth; this ledger is the human trail that
keeps them honest. Never edit an old entry; correct with a new one.

Format: `### YYYY-MM-DD · ACTION · id` then a short body.

---

### 2026-08-13 · CREATE · INV-1..INV-15

Initial set, distilled from two sources: the standing engine invariants in
CLAUDE.md (INV-1 turtle-only-immovable, INV-5 no-springs-as-patch, INV-6
no-gravity-assumptions, INV-15 physics-blind-to-game) and the laws proven
by the 2026-08-10..12 physics-surgery campaign, each carrying the RCA that
earned it:

- INV-3 energy-transforms — the owner's reframe that reset the campaign:
  energy is not lost, it transforms. The ledger (ENERGY_LEDGER=1) is its
  instrument.
- INV-4 born-at-rest — generation-debt ledger G1..G6.
- INV-7 momentum-one-door — the sleeping branch that sank 6.74 m under its
  own leaves' damping.
- INV-8 rows-live-sized — the 245 m/s blade and the ±110 m/s heel-strike
  foot, both rows sized for a stale world.
- INV-9 derived-not-declared — 28 hollow branch bonds holding a canopy on
  nothing (G6).
- INV-10 mass-uniform-limits — the 1046:1 ringing ladder.
- INV-11 no-detonation — issue #42's always-on tripwire.
- INV-12 true-geometry-contacts — the walk-gate snowplow and the tipped
  plate resting on 276 mm of air.
- INV-13 driven-joints-hold — the shoulder standing error; two integral
  attempts measured worse and preserved as evidence commits.
- INV-14 tears-need-strain — bonds tearing between two resting satisfied
  bodies; measure what the bond enforces, not a proxy (G1c).
- INV-2 no-penetration — the settling gates and the wedged-boulder 72 m/s
  ejection that bounded repair speed.

Linking to tests happens in `TEST_AUDIT.jsonl` (one line per test: status,
what it proves vs what it merely touches, INV links). The sweep runner
(`scripts/physics_sweep.py`) joins the two at run time and reports broken
invariants, not just broken tests.
