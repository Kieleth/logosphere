# Performance research kit: design + study journal

Measurement infrastructure, and the studies run with it.
Methodology: `docs/PERFORMANCE_RESEARCH.md`.
Optimization items that shipped: `GPU_OPT_LEDGER.md`.

## Findings at a glance

| # | Question | Verdict |
|---|----------|---------|
| S1 | Cost of leaving telemetry compiled in? | Below this machine's noise floor. Amendment stands. |
| S2 | First sweep: light / particle scaling? | Lights ~free; particles cost CPU. Light curve later invalidated by S3. |
| S3 | Light scaling to 24? | Free to at least 24 (shadow stage 0.27 to 0.42 ms). No action. |
| S4 | Where is the particle bottleneck? | CPU render prep. GPU is ~11% of frame at 4.5k particles. |
| S5 | Is the degradation superlinear? | Measured n^1.70 on CPU. Interpretation SUPERSEDED by S7. |
| S6 | Which sub-phase is superlinear? | collect n^1.69, cull n^2.15, triangles n^1.77. SUPERSEDED by S7. |
| S7 | CORRECTION: is the engine superlinear? | No. Linear in surfaces; S5/S6 measured scene composition drift. |
| S8 | Is the per-particle allocation the cost? | No. Cost is per-surface math. Removal was +2.5%, i.e. nothing. |
| S9 | Does caching unmoved geometry help? | Works (94-99% hits, collect -24/-36%) but no frame win. Flag OFF. |
| S10 | What actually starves the GPU? | The unit icosphere, rebuilt per sphere per call. ~5M allocations/frame. CPU render -35%. |
| S11 | Where does prep_shadow_tris go? | Two thirds was a contended mutex, not geometry. Frame -18%. |
| S12 | Who owns retina frame time? | Nobody. CPU 19.74 vs GPU 18.92 ms, BALANCED. The old GPU metric read 122% of wall clock and was double counting. |
| S13 | Do the render passes overlap each other? | No. Serialized and pipelined stage costs match within 1%, so per-stage timestamps were true isolated costs all along. |
| S25 | Does a scenario ladder expose flaws between one box and Eden? | Not at this scale. Eight scenarios, every physical check holds, every residual under the 1 mm slop band except one. The exception is the signal: a body thrown at 12 m/s peaks at 7.5 mm, 10x any gravity-driven scene. Sudden load is where the solver is furthest from legal. |
| S24 | Is constraint order load-bearing? (S19's question) | NO, on one scene. Four permutations agree on the residual to every digit against a zero noise floor, while their positions diverge. Parallelism, islands, SoA and sorting reopen IF it widens. Also: the natural order is measurably worse than an arbitrary one. |
| S23 | Can we measure whether the solve's ANSWER is good? | Yes, `SolveResidual`, and it took three wrong versions: velocity residual is blind to penetration by construction, a settled scene has zero solvable rows so the end-of-run sample reads nothing, and the published value outlived its engine. Peak penetration separates rungs the exit counters call identical: 0.68 mm to 2.12 mm. |
| S22 | Can it be fixed without an edge case? | YES, one constant. The effective-mass divide-by-zero guard returned 1.0f for immovable pairs; the physically correct value is 0. Everything converges; no branch added. |
| S21 | What actually blocks convergence? | A 2D tiled floor, and only in 2D: a 1x3 STRIP of immovable tiles converges, a 2x2 GRID never does. The phantom-impulse mechanism is necessary at most, not sufficient, and remains unisolated. |
| S20 | Why does it plateau, and how simple a scene fails? | S19's inference WITHDRAWN, but the reason given was itself wrong and is corrected in the entry: the exit compares a per-iteration DELTA, not the ~100 N s total, and post-S22 it converges on iteration 1. Standing findings: the plateau detector has no dynamic range, and the floor solves ~26 constraints per tile against itself. |
| S19 | Is constraint order load-bearing, or only a bit pattern? | ~~LOAD-BEARING~~ WITHDRAWN by S20, the counter measures the wrong quantity. Data stands, inference does not. Question is open. |
| S18 | Where does the engine stop holding 60 FPS, and why? | 3-4k bodies. Render is flat at 2.7 us/body; `apply_all_forces` is 98% of physics and scales O(n^1.38). |
| S17 | Do smooth normals survive the shadow terminator? | Yes at LOD 1, no at LOD 0. Shipped LOD 1 + smooth as default; LOD 2 kept as a quality setting because it still casts better SHADOWS (rays hit real triangles). |
| S16 | What does an LOD switch cost? | 3.94 ms more GPU work: refit 1.87 to build 5.81. accel_build is the LARGEST GPU stage and was never recorded until now. |
| S15 | Is the 1.9x shadow triangle ratio waste, and are sphere shadows worth it? | No and no. The ratio is required input for the GPU's per-ray cull, and every Eden shadow caster is a box (196,596 = 16,383 x 12 exactly). |
| S14 | Should async GPU prep be turned back on? | Yes, once the handoff stopped copying the frame's input TWICE (lambda captured by value). Pixel-identical, +1.88 ms retina, +3.43 ms windowed. |

**Standing conclusions.** Cost tracks **surfaces**, not particles and not
lights. A sphere at subdivision 2 emits 320 surfaces against a box's ~12.
Lights are nearly free. The engine's CPU render path is linear in surface
count. No CPU optimization tried so far has crossed the frame-time noise
floor. The reason is not stage overlap (S13 shows the passes barely overlap)
but CPU/GPU balance: at retina the two sides are level within about 1 ms, so a
one-sided win of any size is capped at that gap (S12).

## The decision rule this campaign produced

Everything below converges on one procedure. Follow it before optimising
anything, because four separate changes cut real work and bought nothing by
skipping it.

**1. Measure both sides, per frame.** CPU frame time, and GPU busy as the UNION
of command-buffer intervals (never their sum, see S12). `bench_report.py`
prints both plus occupancy.

**2. Read the gap.**
- Gap is large (one side clearly longer): attack the longer side. The win is
  real up to the gap, then stops.
- Gap is small (balanced): a one-sided win of ANY size is capped at the gap.
  Retina today is CPU 19.74 against GPU 18.92, a gap of 0.81 ms. This is why
  G4, pow-to-squarings, S8 and S9 all cut real work for no frame movement.

**3. When balanced, only both-sides work is uncapped.** Fewer triangles cut CPU
collect and prep AND GPU rasterisation. That is the whole argument for LOD,
analytic sphere shadows, and GPU-side geometry expansion, and it is a stronger
argument than "this is the biggest number".

**4. Prove the lever engaged before believing the result.** Every study this
session that lacked a sensitivity control was blind, and two of them silently
reported a plausible non-result: an A/B that compared sync against sync, and an
equivalence test running with telemetry disabled. Carry a counter that is
non-zero only when the change is active.

**5. Report non-zero counts for anything spiky, not just medians.**
`render_slot_wait` reads 0.00 as a median while firing on 21-35 of 199 frames
up to 6.7 ms. Two entries in this journal read that median as "the CPU never
waits". It waits on roughly one frame in eight.

## What this unlocked

- **A whole class of work is now correctly priced.** Stage-level GPU
  micro-optimisation at retina (task #22, the SSDO denoise and penumbra blurs,
  7.14 ms of 19.84) is capped at 0.81 ms no matter how well it goes. That is
  not a reason it is worthless; it is a reason it is not next.
- **Geometry work is promoted, with a reason.** LOD and analytic sphere shadows
  were previously "nice to have". They are now the only identified uncapped
  lever, because they cut both sides at once.
- **Async GPU prep has a costed path instead of a hunch.** It is pixel-identical
  (proven) and was net zero (measured) because the handoff deep-copied 103,914
  surfaces per frame, twice, costing what the prep saved. The copy is gone and
  the win is real: +1.88 ms retina, +3.43 ms windowed. It is nonetheless OFF
  (`USE_ASYNC_GPU_PREP = false`), blocked on a correctness bug found when it was
  enabled: foliage renders black because the KG mapping is read live against a
  stale particle snapshot (issue #31). The performance question is closed; the
  correctness one is not. See S14.
- **The instruments can now settle arguments they previously could not.**
  Occupancy is computable (union), isolated stage cost is checkable on demand
  (serialized mode), and both are guarded by tests that fail rather than
  mislead.
- **Two standing principles were corrected**, in `PERFORMANCE_RESEARCH.md`
  principle 5 and `GPU_OPT_LEDGER.md` learnings 6 and 7. Both had the right
  observation and the wrong mechanism, and both were actively steering work.

**The meta-lesson.** In every case this session the INSTRUMENTS were honest and
the MODEL on top of them was wrong. "Stages overlap" explained the evaporating
wins for three weeks and was false. "Residency, not work" explained the
impossible GPU sum and was false. "The machine was loaded" explained the async
result and was false. Each time the correct answer was already in the data,
one level below the summary being read. Before adding an instrument, check
whether the existing one is being read correctly.

## What the physics half taught (S18-S22)

The render studies were about cost. The physics studies (S18-S22) were about
correctness, reached three wrong conclusions on the way, and produced a
different set of rules. Recorded separately because they generalise past
physics.

1. **Trace the DECISION, not the aggregate.** Every withdrawn conclusion in
   this campaign came from reading a summary number and laying a story over it.
   A counter said the solve "plateaued"; the level-5 trace named the row, the
   pair and the three numbers that produced it, and the mechanism was then
   obvious in seconds. Counters say a contact was created. They cannot say a
   pair was skipped because both bodies were KINEMATIC. Build the tracer
   earlier: `src/core/physics_trace.h`, off by default, one relaxed atomic load
   when nobody is listening, `-p` through `-pppppp`.

2. **Read the line that ASSIGNS the variable, not the line that tests it.**
   S20 argued the solver's `converged` exit was unreachable because it compares
   against 0.01 while a resting stone box needs ~100 N s of support. That
   confused two different quantities: the test reads a per-iteration DELTA
   (`physics_system_v4.cpp:2025`), not the accumulated total, and post-S22 it
   converges on the FIRST iteration. A whole conclusion about constraint
   ordering was built on a variable whose assignment was never read. Checking an
   exit condition is reachable is still right; do it by reading the assignment
   and printing the value, not by reasoning from the variable's name.

3. **A global max over rows means ONE bad row pins the whole world.** The
   stopping test is a max across every constraint. A single corner pair of
   immovable tiles, producing an impulse of exactly 4 forever, held 14,000
   bodies above threshold. When an aggregate is a max, the interesting question
   is always "which element", never "what is the trend".

4. **Minimal scenes, or the test lies.** The convergence ladder was
   contaminated by a 25-tile floor: the exit test is global, so the floor's own
   pathological rows drowned the two-body case the ladder was built to isolate.
   A test scene should contain the thing under test and nothing else,
   especially when the metric is a max.

5. **A divide-by-zero guard is a physics statement.** `inv_mass_sum == 0` means
   both bodies are immovable, so effective mass is infinite and the row's
   correct contribution is ZERO. The guard returned `1.0f`, an arbitrary value
   chosen to avoid a NaN, and that one constant was the bug. Guards written for
   numerical safety still have to be right physically; ask what the degenerate
   case MEANS, not merely what keeps the float finite.

6. **New failure mode: the instrument itself can measure the wrong quantity.**
   The render half's meta-lesson was "instruments honest, model wrong". S20 is
   worse than that: the convergence counter was faithfully reporting a
   quantity nobody wanted. An instrument earns trust by being checked against a
   case whose answer is known independently, not by being consistent.

7. **A readout nobody can see is not a readout.** Four visual tests shipped
   with on-screen HUDs that never put a pixel on screen, and the recommended
   fix in `VISUAL_TESTS.md` had itself never been verified. It took a
   with-and-without pixel diff to establish it. Any test that reports through a
   UI surface needs a test that the UI surface renders:
   `tests/test_ui_label_actually_renders.cpp`.

## Why this exists

Nearly every error in the 2026-07 GPU campaign was a MEASUREMENT failure,
not an engineering one. A stale-metallib mirage produced a fake win. Two
fp16 "fixes" were falsified once oracle nondeterminism was found. Thermal
drift repeatedly exceeded the effect under test (same build, 16.4 and
26.0 ms in one session). Per-pass GPU timings sampled 1-in-60 gave n=9 per
stage. And everything was regex-scraped from stdout into scratchpad scripts
wiped between sessions. A performance claim should be reproducible evidence,
not a remembered number.

**The measurement problem, and how it resolved.** A 2 ms stage win repeatedly
bought zero frame time (ledger G4, S8, S9), and the working theory was that
overlapping stages made stage cost and critical-path cost different questions.
The serialized diagnostic mode was built to settle it (S13) and falsified the
theory: the passes barely overlap, and the per-stage numbers were already true
isolated costs. The real cause was CPU/GPU balance (S12). Both instruments were
telling the truth; the model built on top of them was wrong.

## Owner decisions (2026-07-29)

1. `benchmarks/` is git-tracked: trends reviewable in PRs, regressions diffable.
2. Synthetic MLP is the backbone; Eden is the reality check.
3. Two modes: `fast` (indicative) and `study` (interleaved repeats, cooldowns).
4. A serialized diagnostic mode for true isolated per-stage cost (task #28).

## Architecture

- **Instrumentation.** `logosphere::telemetry`, three tiers (counters,
  phase timers, deep probes). `LOGOSPHERE_METRICS=<path.jsonl>` writes one
  record per frame: CPU phases, GPU stages tagged with their source frame,
  counters, scene counts. No env means no sink and no cost.
- **Sweep.** `scripts/bench_sweep.py`: particles x lights x resolution,
  config order rotated per repeat so thermal drift spreads across the matrix
  instead of landing on whichever ran last; cooldowns; provenance manifest
  (git SHA, dirty flag, binary and metallib hashes, host, mode, trust string).
- **Analysis.** `scripts/bench_report.py`: frame/CPU/GPU breakdown, spread,
  THERMAL SUSPECT drift flag, GPU BUSY (interval union) with occupancy and
  stage overlap, baseline regression compare.
  `--ramp` reads a continuous-load run as a cost curve.
- **Journal.** Below, including studies that conclude "no action".

## Usage

```bash
# Matrix sweep + report
scripts/bench_sweep.py --mode study --repeats 3 --scene mlp \
  --particles 1296 --lights 1 4 8 16
scripts/bench_report.py benchmarks/runs/<dir>

# Continuous-load ramp: one run yields one cost curve
LOGOSPHERE_METRICS=/tmp/r.jsonl MLP_MODE=particles MLP_SPAWN_RATE=240 \
  PARTICLES=576 LIGHTS=8 ./build-release/logosphere-tests \
  --test test_multi_light_progressive --no-head
scripts/bench_report.py /tmp/r.jsonl --ramp

# Visual demo (records metrics too)
scripts/demo_lights.sh particles 8 576      # mode: lights | particles | both
```

Vehicle knobs: `MLP_MODE`, `PARTICLES`, `LIGHTS`, `MLP_SPAWN_RATE`,
`MLP_MAX_PARTICLES`, `MLP_SPHERE_EVERY`, `MLP_SINGLE_PHASE`, `MLP_DEMO`,
`INTERACTIVE`.

**MLP caveat that governs every number below.** The vehicle calls
`wait_for_completion` per frame so the pixel oracle stays deterministic. It
therefore has no CPU/GPU overlap by construction, and its absolute frame
times and 0.23x overlap factor are artifacts of that. MLP is valid for
scaling SHAPE, invalid for absolute cost. Eden is the pipelined reality check.

---

## Current baseline (2026-07-31) — READ BEFORE COMPARING ANY OLDER NUMBER

> **Counters and CPU phases below stand. The GPU interpretation is SUPERSEDED
> by S12 (2026-08-01).** The `gpu_busy` row was a sum of overlapping per-frame
> windows, which overstates. Corrected by union, retina is 95.8% occupied and
> the frame is BALANCED, CPU 19.74 ms against GPU 18.92 ms. The conclusion
> stated below, "Eden retina is no longer GPU-bound", does not survive that.

**Every measurement in S1-S10 was taken against a 4,195-particle Eden that no
longer exists.** Logogenesis filled the world (moon, trees, grass carpets,
scene lights). Same scene name, 7x the geometry:

| | old Eden | current Eden |
|---|---|---|
| particles | 4,195 | **19,104** |
| visible | 2,457 | **17,352** |
| surfaces | 14,538 | **103,920** |
| render triangles | 14,372 | **103,754** |
| shadow triangles | 17,700 | **196,596** |
| lights | 6 | 6 |

Frame went 16.0 to 26.7 ms for 7x the geometry, so the engine scales well. But
the REGIME FLIPPED, which changes which optimizations pay:

| phase (median, steady) | retina 3200x2102 | windowed 1600x1051 |
|---|---|---|
| frame | 26.67 | 23.62 |
| **render_slot_wait** | **0.00** | **0.00** |
| CPU render work | 19.37 | 17.47 |
| physics | 6.38 | 6.00 |
| prep_shadow_tris | 6.88 | 6.45 |
| render_collect | 4.54 | 3.87 |
| prep_triangles | 2.25 | 2.23 |
| prep_bvh | 2.16 | 2.14 |
| render_submit | 1.50 | 1.39 |
| gpu_busy | 18.16 | 7.29 |

**Eden retina is no longer GPU-bound.** `slot_wait` is 0.00 at BOTH
resolutions: the CPU never waits. Quadrupling the pixel count costs 3.05 ms of
frame time while GPU busy goes 7.29 to 18.16, all of it hidden. The conclusion
that held through S1-S10, that retina is GPU-bound and CPU wins are invisible
there, is now FALSE for this scene.

Consequences for the open work:
- GPU-side geometry expansion, long deferred as "worth nothing at retina", now
  addresses collect 4.54 + prep_triangles 2.25 + prep_shadow_tris 6.88 =
  **13.67 of the 19.37 ms CPU render**.
- `prep_shadow_tris` is the largest render item. The shadow path applies no
  back-face culling, so it carries 196,596 triangles against the render path's
  103,754, 1.9x.
- **`physics` at 6.38 ms is now the second-largest item in the frame and has
  never been profiled in this campaign.**

Method note: run 1 of a cold sequence measured 35.3 ms against 26.7 for runs 2
and 3. Discard the first run.

**How this was found, because the failure mode is instructive.** The frame
looked like an 11.6 ms regression against yesterday's 16.0 ms. It was first
misattributed to machine contention (wrong: the pre-merge binary still gave
16.1 ms on the same machine minutes later), then to a code regression in the
merged commits (wrong: a bisect landed on `feat(logogenesis): conversational
world creation`, which is content). Only the COUNTERS settled it. Same lesson
as S7: a timing change means nothing until you have checked whether the
workload changed. Scene composition is a variable, and Eden is not a fixed one.

---

# Study journal

### S1: telemetry's own cost (2026-07-29)
A-B-A retina (3200x2102, 400 frames), telemetry OFF, then ON writing every
frame, then OFF: **16.0 / 16.3 / 16.6 ms**. B sits between the A endpoints;
identical configs drifted 0.6 ms across the session. All five oracles
identical.
**Verdict:** cost is below this machine's noise floor. That is an upper bound
stated honestly, not a claim of zero. The amendment (tiers 1-2 ship compiled
in, runtime-toggleable) stands.
**Learning:** the frame boundary first finalized in `present()`, which
headless never calls, so the sink wrote an empty file. Moved to `update()`,
the one point every loop shape reaches.

### S2: first sweep (2026-07-30)
`benchmarks/runs/20260730T030040Z-267c497/`, fast mode. Lights 1 to 6 cost
4.96 to 5.18 ms at 144 particles. Particles 144 to 1296 moved the frame 4.96
to 6.82 ms with all growth in CPU render (0.71 to 2.10 ms) and none in any
GPU stage. **The light curve here is invalid, see S3.**
**Two kit defects found and fixed:** a stale `logosphere-tests` binary
silently produced ZERO GPU data, so the manifest now hashes binaries and not
just the metallib (precisely the error class this kit exists to prevent); and
nothing published the light count, so every record said `lights=0`.

### S3: light scaling to 24 (2026-07-30)
**The confound, first.** The vehicle walked phases 1..N *inside one process*,
so light count correlated perfectly with elapsed time and later phases ran
hotter. `bench_sweep`'s interleaving cannot fix a confound living below the
process boundary. A fast sweep on that vehicle "found" a step at 14 lights
that was thermal. `MLP_SINGLE_PHASE=1` drives one light count per process;
all four oracle phases stayed byte-identical across the edit.
**Study run** (`benchmarks/runs/lightscale-study/`, 1296 particles, 3
interleaved repeats, 8 s cooldowns):

| lights | 1 | 4 | 8 | 16 | 24 |
|---|---|---|---|---|---|
| frame median | 7.60 | 7.27 | 7.23 | 8.25 | 7.85 ms |
| spread | 0.27 | 0.60 | 1.21 | 0.82 | 1.78 ms |
| shadow stage | 0.27 | 0.27 | 0.30 | 0.46 | 0.42 ms |

**Verdict:** lights are close to free up to at least 24. The shadow stage
shows a real but small trend (+0.15 ms over 24 lights); the frame differences
sit inside the between-repeat spread, which at 8 and 24 lights exceeds the
effect being claimed. No action. Settling the shadow trend needs more
repeats, a cooler machine, or task #28.

### S4: RAMP, the particle bottleneck (2026-07-30)
New RAMP mode: `MLP_SPAWN_RATE` drops bodies continuously, so one run traces
a cost curve instead of a few discrete configs. 900 bodies/s, 8 lights fixed.

| particles | 2305 | 2942 | 3580 | 4217 | 4536 |
|---|---|---|---|---|---|
| frame ms | 41.2 | 61.5 | 71.3 | 84.9 | 95.3 |
| GPU sum | 5.1 | 6.6 | 8.0 | 9.8 | 10.8 |
| CPU render | 29.6 | 41.3 | 51.4 | 57.7 | 65.7 |

**Verdict:** particles cost CPU render prep, not GPU. At 4.5k particles the
GPU is roughly 11% of the frame. The full shape: lights nearly free,
resolution GPU-bound (the whole 2026-07 campaign), particles CPU-bound and
dominant past a few thousand. Weeks of GPU work do not touch the limiter
under particle load. First bins are a spawn transient; the report keeps them
visible rather than silently trimming.

### S5 / S6: numbers stand, interpretation SUPERSEDED by S7
S5 fitted CPU render at n^1.70 (frame n^1.59, GPU n^1.27) from a recorded
ramp. S6 split it by sub-phase, giving collect n^1.69, cull n^2.15, triangles
n^1.77, binning n^1.51, shadow_tris n^1.28, bvh n^1.14, and concluded the
engine had an algorithmic scaling flaw with quadratic culling as the prime
suspect. **The measurements are real; the conclusion was wrong.** Kept
because deleting them would hide how the error happened.
Two durable results survive. The lights axis is flat at both 576 and 2304
particles (light packing 0.001 ms, unmeasurable, and `prep_shadow_tris`
actually falls as lights rise, which is distance culling doing its job). And
ramp fits must exclude the first bin, which carries a one-time ~17.9 ms BVH
build that otherwise inverts the entire exponent table.

### S7: CORRECTION, the engine is linear (2026-07-30)
Tier-1 counters settled what timings could not.

| particles | surfaces | surf/particle | collect ms | us/surface |
|---|---|---|---|---|
| 859 | 37,210 | 43.3 | 2.11 | 0.057 |
| 1642 | 127,160 | 77.4 | 7.10 | 0.056 |

`particles_visible` grew exactly n^1.00, so culling was never the problem,
and **time per surface was constant**. The code is linear; the WORKLOAD was
growing superlinearly. `kSphereSubdivisions = 2` makes a sphere emit 320
surfaces against a box's ~12, and the ramp spawned every third body as a
sphere onto an all-box starting grid, so surfaces per particle climbed from
43 to 77.
**Control** (`MLP_SPHERE_EVERY=0`, all boxes): collect is FLAT, 1.18 to
1.29 ms, while particles nearly double and surfaces per particle holds at ~16.
**Learnings.** (1) Timings alone cannot distinguish a superlinear algorithm
from a superlinear workload; only counting can, which is exactly why the
methodology says count operations. (2) A ramp whose spawned items differ from
the starting population measures composition drift as well as scale, so state
the mix or hold it fixed. (3) `MLP_SPHERE_EVERY=0` silently stayed at 3
because `env_int` treats 0 as unset, and the control returned byte-identical
numbers to the experiment. Identical results across a supposedly-changed
variable means check the knob before believing the science.

### S8: NEGATIVE, allocation was not the cost (2026-07-30)
`GetSurfaces()` returned a vector by value: one heap allocation per particle
per frame on the engine's hottest path. Added appending variants
(`GetSurfacesInto`, `to_surfaces_into`, `emit_surfaces_into`) writing into a
reused `thread_local` buffer.
**Result:** mean collect 4.86 to 4.98 ms (**+2.5%**), per-bin deltas
scattered either side of zero (+0.37, +0.04, +0.15, +0.10, -0.18, +0.24).
Oracles byte-identical, so the change is correct; it simply does not pay. The
allocator serves repeated same-size requests from a size-class free list. The
real cost is per-surface MATH: `get_world_vertices` and `get_world_faces`
transform every vertex and face, `make_triangle_surface` computes centre and
normal, then each Surface is copied into a SurfaceData. That holds at
~0.06 us per surface, stable across the ramp before and after.
**This sharpens the direction rather than closing it.** The two approaches
that actually address per-surface math are not redoing it for unmoved
particles (S9), or moving the expansion to the GPU (upload O(particles)
instead of building O(surfaces)).
**Learning:** third time this month a plausible mechanism produced no frame
win (ledger G4 halton fold, the background-copy guard, now this). Modern
allocators and memory-latency-bound code absorb what looks expensive in source.

### S9: surface cache, works but no frame win (2026-07-30)
`USE_RENDER_SURFACE_CACHE` (default OFF) reuses generated geometry while a
particle has not moved. It is keyed by particle index, but each entry stores
the transform it was built from and is reused only when that still matches,
so a particle swapped in by swap-and-pop deletion fails the check and
regenerates. A particle whose transform matches exactly would produce
identical geometry anyway, which makes the check sufficient rather than
merely conservative.
**It works.** Static KINEMATIC grid: 99.7% hits, collect 0.331 to 0.213 ms
(-36%). Eden: 94.3% hits (2354 of 2496 visible), collect 0.919 to 0.703 ms
(-23.6%). Oracles byte-identical.
**No frame win.** Eden A-B-A: 5.4 / 5.4 / 5.4 ms, flat, because at 1600x1051
the scene is GPU-bound and 0.2 ms of CPU disappears under overlap. CPU-bound
retest at 8000 static particles: 17.98 (ON) / 21.05 (OFF) / 21.12 (ON). **The
two identical cache-ON runs differ by 3.1 ms**, more than the apparent gain,
so the first result was ordering or thermal.
**Verdict:** keep the code, flag OFF. Revisit when a genuinely CPU-bound
scene exists, or when enough CPU wins accumulate to cross the noise floor.
Memory caveat: a cached sphere is roughly 20 KB per particle.
**INSTRUMENTATION BUG, the important part.** The cache first reported **0%
hits on a provably static scene**. The cache was fine; the counters were
lying. `collect_surfaces` runs on per-frame `std::thread` workers, and
telemetry's thread_local counters deregistered on thread exit, discarding
counts before `frame_end()` merged them. Every counter incremented off the
main thread read zero. Exiting threads now flush into an orphan accumulator.
**Learnings.** (1) A 0% result on a scene where the mechanism MUST fire is an
instrument failure until proven otherwise; it was, twice this session. (2)
Thread-local telemetry must survive thread death, since a missing count looks
exactly like "it did not happen". (3) Stage win is not frame win, third
confirmation.

### S10: the GPU was starved, and the cause was constant data rebuilt (2026-07-30)
Started from the owner's observation that the GPU looked idle. Four instruments
disagreed and had to be sorted out first:

| instrument | Eden retina | verdict |
|---|---|---|
| `ioreg` Device Utilization % | 100% | **junk**, not shader busy time |
| engine sum of `GPUEndTime - GPUStartTime` | 21.4 ms in a 16.0 ms frame | **not additive**; it is buffer residency, not work |
| Metal HUD "GPU" | 6.04 ms / 16.19 ms | narrower accounting than "GPU busy" |
| `powermetrics --samplers gpu_power` | 77-97% residency, 1578 MHz, 21-59 W | **trustworthy** |

`powermetrics` settles it. **Eden retina: GPU pinned at the top P-state (P10
requested 100%), 77-97% busy, up to 59 W. Saturated.** Falling bodies: 18-26%
residency at 688-1054 MHz and 0.4-0.9 W, and the driver does not even request
the top clock. The GPU is downclocked *because* it is starved, not the reverse.

**Where the starvation came from.** `Particle::GetSurfacesInto` and
`GetShadowTriangles` each construct a `SphereGeometry` on the stack, whose
constructor runs `populate_icosphere`. At subdivision 2 that is, every call:
two `std::unordered_map` builds, ~160 `sqrt` for midpoints, 320 face normals
with another 320 `sqrt`, and 320 `Face` constructions. `Face` held **two
`std::vector`s** for what is always 3 or 4 elements, so that is **640 heap
allocations**. Then `emit_surfaces_into` called `get_world_faces()`, whose
entire body copies all 320 faces to rotate one `Vec3` each: **640 more**.

Per sphere per call: ~1,280 allocations and ~1,120 `sqrt`. Twice per frame.
At ~1,950 spheres that is **~5 million allocations and ~4.4 million `sqrt` per
frame**, all reproducing a unit mesh that is identical every time. Model check:
predicted 1,950 x 320 + 4,484 x 12 = 677,808 surfaces, measured 676,088.

**Fixes, all output-identical (6/6 oracle phases byte-identical):**
- A: cache the unit icosphere per subdivision level, built once, handed out const.
- B: emitters read local faces and rotate the one normal inline; `get_world_faces` off the hot path.
- C: `Face` uses fixed inline storage instead of two `std::vector`s.
- plus the same by-value fix on the box path (`FlatParticleGeometry`).

| falling bodies, 4,634 | before | after | Δ |
|---|---|---|---|
| render_collect | 21.53 | 9.05 | **-58%** |
| prep_shadow_tris | 14.83 | 8.27 | **-44%** |
| prep_triangles | 7.27 | 7.04 | -3% |
| prep_bvh | 4.90 | 4.65 | -5% |
| render_particles | 62.53 | 40.53 | **-35%** |
| frame | 93.76 | 66.38 | **-29%** |

The two phases that call the icosphere builder fell by half; the three that do
not moved 3-5%. Thermal drift moves everything together, so this is the fix.
Eden retina was unchanged (16.01 to 16.06 ms) exactly as predicted: it is
GPU-bound with ~11 ms of CPU slack, so CPU wins there are invisible.

**S8 needs amending.** S8 removed one returned-vector allocation per particle
and correctly measured nothing. That was 0.08% of the allocations on that path.
"Allocation was not the cost" should read "**that** allocation was not the cost".
The lesson is not that allocation is cheap; it is that you must count them.

**Falsified along the way, recorded so it is not retried.** Command-buffer
count was not the lever: merging the JFA seed and its 6 propagation steps into
one buffer took the frame from 18 command buffers to 12 and moved the GPU span
by +0.16 ms. Per-buffer boundary cost on this hardware is near zero.

**Learnings.** (1) Ask what in a per-element function is invariant across
elements, and how many allocations one element costs. Timings and operation
counters both stay silent on this. (2) A small struct with `std::vector`
members makes every container of it an allocation storm. (3) Four GPU
"utilization" numbers disagreed by 20x; `powermetrics` reads the hardware
counters and is the one to believe. (4) `GPUEndTime - GPUStartTime` per command
buffer is residency, not work, and must not be summed.

### S11: a phase named after geometry was two thirds mutex (2026-07-31)
Audit of the three biggest CPU render phases against the new Eden baseline
(19,104 particles, 103,920 surfaces, 196,596 shadow triangles).

**`prep_shadow_tris`, 7.71 ms.** The worker tagged every triangle with its
owning entity for BVH grouping by calling `getEntityByRenderIndex()` **once per
particle**. That accessor locks a `std::recursive_mutex` and does two hash
lookups, and **14 worker threads ran it concurrently**. Sized by diagnostic
deletion first (replace the call with a constant, rebuild, measure): 6.88 to
2.37 ms. At 16,383 particles that is **275 ns per call against roughly 25 ns
uncontended, a 10x contention penalty.**

The mapping is constant for the frame and only changes when particles are
added, removed or rebound, so it never needed reading inside a parallel region.
`KGCore::snapshotRenderIndexToEntity()` now fills it under ONE lock, walking the
bound render indices rather than probing every particle slot.

| Eden retina | before | after |
|---|---|---|
| prep_shadow_tris | 7.71 | **2.50** (-68%) |
| render_prep | 12.29 | 7.16 |
| frame | 27.09 | **22.28** (-18%, 36.9 to 44.9 FPS) |

Every other phase moved under 0.25 ms. Entity grouping bit-identical (781
entities, 196,524 triangles, same group-size distribution), six of six pixel
oracles byte-identical, harness 27/27.

**Two negatives from the same audit, recorded so they are not re-derived.**
- The second back-face cull in `convert_surface_to_lit_triangles` looked like
  pure redundancy after `CULL_SURFACES_AT_GENERATION` landed. It costs
  **0.04 ms and rejects 60 triangles**. Leave it.
- `USE_OCCLUSION_CULLING` costs **0.29 ms and culls 1 particle of 17,352** in
  this scene. Negative value, but too small to be worth a flag flip on its own.

**Learnings.** (1) A phase name describes where code lives, not what it costs:
"prep_shadow_tris" was 68% lock. (2) **Any per-element lock inside a parallel
region is a serialization point**; look for locks before looking at arithmetic.
(3) Diagnostic deletion sizes a prize in one build, before any real fix is
written: replace the suspect with a constant, measure, then decide whether the
fix is worth designing. (4) A clean A/B moves ONE phase. If unrelated phases
move too, the experiment is confounded.

---

### S12: the GPU metric was double counting, and retina is BALANCED (2026-08-01)

Opened by a contradiction, not a hunch. `slot_wait` read 0.00 at both
resolutions, saying the CPU never waits on the GPU, while `gpu_window.busy_ms`
summed to more than the frame period, saying the GPU was saturated. Both could
not be true.

**The metric was wrong, and provably so: it computed 122% of wall clock.** One
GPU cannot be busy longer than the elapsed time. `gpu_window` publishes a
per-frame window, and the GPU runs roughly a frame behind the CPU, so
consecutive frames' windows OVERLAP. Summing them counts the shared region
twice. Correct occupancy is the UNION of the intervals.

| | retina 3200x2102 | windowed 1600x1051 |
|---|---|---|
| GPU occupancy, union | **95.8%** | **52.0%** |
| GPU occupancy, naive sum | 122.0% | 51.8% |
| windows merged into intervals | 42 of 199 | 199 of 199 |

**The failure mode worth remembering: the broken metric agreed with the correct
one exactly where the answer did not matter.** Windowed, the GPU idles between
frames, nothing overlaps, and sum equals union. At retina, where the answer
decides what to optimise next, it was off by 26 points. Anyone spot-checking
the metric on a cheap scene would have concluded it was fine.

**What the corrected numbers say, per frame:**

| | CPU | GPU | headroom | |
|---|---|---|---|---|
| retina | 19.74 ms | 18.92 ms | **+0.81 ms** | balanced |
| windowed | 13.74 ms | 7.15 ms | +6.59 ms | CPU-bound |

**Retina is neither CPU-bound nor GPU-bound. It is balanced within 0.81 ms.**
That retires two earlier conclusions: S1-S10's "retina is GPU-bound, CPU wins
are invisible there", and the 2026-07-31 baseline note's "Eden retina is no
longer GPU-bound". Both were half right about a system that is level.

**It also explains the campaign's most persistent puzzle.** Two separate
stage-level GPU wins moved their stage and left frame time unchanged (the `pow`
to squarings experiment, and ledger G4). The reason stops being mysterious once
both sides are measured: shave the GPU and the CPU becomes the wall 0.81 ms
later. The reverse holds identically. **At retina a one-sided win of any size is
capped at 0.81 ms.**

The lever that is not capped is work landing on BOTH sides. Geometry is the
clear case: fewer triangles cuts CPU collect and prep AND GPU rasterisation.
That is the argument for LOD and for GPU-side geometry expansion, and it is a
stronger argument than "CPU render is the biggest number".

**Guard.** `tests/test_gpu_occupancy_sanity.cpp` computes occupancy by union,
fails if it exceeds 100%, and reports the overlap a naive sum would hide. It is
also the reference implementation: any consumer wanting occupancy should copy
`merge_busy()`. `telemetry.h` now publishes `start_s`/`end_s` for exactly this
reason, and states in the header that per-frame windows must not be summed
across frames.

**Second measurement defect, same session.** `physics` was being read as a
per-frame median. It is a fixed-rate stepper running at 30.2 Hz in both configs
(7.79 ms/step retina, 6.58 windowed). Its per-frame median tracks the frame
rate, not physics: 7.07 ms retina against 0.00 windowed, which looks like a
resolution effect and is not one. Any fixed-rate subsystem must be reported per
step and per second. Per second, physics is 23.5% of wall at retina and 19.9%
windowed.

### S13: NEGATIVE, the passes were never overlapping (2026-08-01)

Built the serialized diagnostic mode (task #28 B): every pass blocks until it
completes before the next is encoded, so each runs alone and its timestamp is
its true isolated cost. `LOGOSPHERE_GPU_SERIALIZED=1` or
`Logosphere::set_gpu_serialized_diagnostic()`.

**It confirmed the cheap instrument was already right.** Eden retina, per-stage
medians, pipelined against serialized:

| stage | pipelined | serialized | delta |
|---|---|---|---|
| pass1_gbuffer | 4.85 | 4.80 | -0.05 |
| pass28_ssdo_denoise | 3.55 | 3.52 | -0.03 |
| pass25_jfa_propagate | 2.47 | 2.46 | -0.00 |
| pass27_ssdo | 2.04 | 2.04 | -0.00 |
| pass25_blur_h | 1.71 | 1.70 | -0.01 |
| pass25_blur_v | 1.45 | 1.44 | -0.00 |
| pass2_shadow_rt | 1.19 | 1.18 | -0.01 |
| pass3_apply | 1.06 | 1.03 | -0.03 |
| **SUM** | **18.36** | **18.21** | **-0.14** |

Under 1%. The render passes have data dependencies on each other, so they were
already running effectively serially inside a frame. `bench_report` agrees
independently: stage overlap is 1.01x at retina and 0.99x windowed.

**So the per-stage timestamps ARE true isolated costs, and "what would removing
this pass buy" was answerable all along.** The answer is that stage's
milliseconds of GPU work, and then the S12 cap applies: at retina the frame only
moves by the CPU/GPU gap, whichever side you cut. The two stage wins that
evaporated were never a measurement problem. They were a balance problem, and
S12 is the explanation. This entry closes the question that opened #28 B.

**The mode works and must never ship.** Frame time went 20.08 ms pipelined to
51.00 ms serialized, 2.54x, exactly as designed: it destroys CPU/GPU overlap.
Read the per-stage split from it and ignore its frame time.
`test_gpu_occupancy_sanity` now also fails if the mode is on without the env
var, because a profiling aid left on would halve the frame rate.

**Kept anyway, cheaply.** The result is negative for this pipeline as it stands
today, not for the instrument. Any future pass that runs concurrently with
another (independent compute, async blits, a second queue) reintroduces exactly
the ambiguity this settles, and then the mode is the only way to tell isolated
cost from blended cost.

**`bench_report.py` now reports the corrected model:** GPU BUSY as the union of
command-buffer intervals, occupancy as busy over frame, and STAGE OVERLAP as
stage sum over busy. Above 85% occupancy it prints the headroom and the warning
that a one-sided win is capped at that gap. The old OVERLAP FACTOR compared
stage sum against frame time, which conflated stage overlap with CPU/GPU
balance and is gone.

### S14: async prep is pixel-identical; the speed question is UNSETTLED (2026-08-01)

`prepare_gpu_data` costs 4.58 ms of a 12.89 ms CPU render at retina and runs
synchronously on the main thread. `Optimizations::USE_ASYNC_GPU_PREP` exists to
overlap it with the GPU and was TRUE at the initial commit. Git blame: set false
on 2026-04-04 with the note "(for testing)" during an Eva-shadow investigation,
whose own commit records that the bug it chased reproduced in BOTH sync and
async modes and that the shadows were correct. Never restored. Four months
unexercised.

Made runtime-switchable (`logosphere::set_async_gpu_prep`,
`LOGOSPHERE_ASYNC_PREP=1`) so both modes live in one binary. Default unchanged.

**PROVEN: moving prep off the main thread does not change the image.**
`tests/test_async_prep_equivalence.cpp`, static camera:

| comparison | delta>=1 | delta>=8 | max |
|---|---|---|---|
| sync' vs sync (noise floor) | 18,277 | 0 | 2 |
| async vs sync | 15,527 | **0** | 2 |

Shadow triangles identical at 9,236, which matters because the April symptom was
a triangle count collapsing mid-run, and a settled pixel diff cannot see that.

**AND IT IS NET ZERO BY CONSTRUCTION. The handoff costs what the prep saves.**
A-B-A-B on Eden retina, medians:

| phase | sync | async | delta |
|---|---|---|---|
| render_prep (leaves the main thread) | 4.92 | 5.34 | +0.43 |
| **render_handoff (arrives on the main thread)** | **0.00** | **4.85** | **+4.85** |
| render_collect | 3.89 | 4.58 | +0.69 |
| render_submit | 2.15 | 1.78 | -0.37 |
| frame | 21.51 | 21.82 | +0.31 |

`render_handoff` is these two lines at the launch site:

```cpp
auto surfaces_copy = surfaces;      // 103,914 surfaces
auto particles_copy = particles;    // 19,104 particles
```

Deep copies of the whole frame's input, on the main thread, every frame. They
exist because the worker is DETACHED and outlives the caller's scope, so
references would dangle; the code says so in a comment. So the design hands 4.9
ms of prep to a worker and pays 4.85 ms of memcpy for the privilege. Frame time
does not move because nothing left the critical path.

**RESOLVED, same day. The frame's input was being copied TWICE.** Splitting
the handoff showed the two named copies accounted for only 2.51 of its 4.57 ms.
The missing 2.06 ms was a second copy nobody had written on purpose: the worker
lambda captured `surfaces_copy` and `particles_copy` BY VALUE, so each was
constructed once into the local and again into the closure. Changed to a
move-capture, one line:

```cpp
std::thread prep_worker([this, next_prep_idx,
                         surfaces_copy  = std::move(surfaces_copy),
                         particles_copy = std::move(particles_copy),
                         &camera_system]() {
```

`render_handoff` 4.57 -> 2.44 ms, and async prep now pays. A-B-A, async /
sync / async:

| | sync | async | win |
|---|---|---|---|
| retina 3200x2102 | 20.41 | 18.53 | **+1.88 ms (9.2%)** |
| windowed 1600x1051 | 12.52 | 9.08 | **+3.43 ms (27.4%)** |

GPU busy is unchanged (7.13 vs 7.11 windowed), so the win is entirely CPU-side,
and both figures land where S12 said they would: retina is capped near its
1.9 ms headroom, windowed had 6.59 ms to give and gave 3.43 of it. **This is
the decision rule predicting a result before the experiment, which is the first
time in this campaign that has happened.** Equivalence still holds: 0 pixels
differing by >=8, shadow triangles identical.

**Then the remaining copy was removed too, and the bottleneck moved twice.**
The surviving handoff was one genuine copy of the surface deque. Replaced with
an O(1) SWAP against a pool of spare deques (`surface_pool_[PREP_BUFFER_SLOTS]`):
the worker takes ownership of the filled `surface_cache_` and the main thread
gets back a deque that already owns its blocks, so next frame's
`collect_surfaces` still reuses allocations. A plain move would also avoid the
copy but leave an empty deque to reallocate every frame.

    render_handoff   4.57  ->  2.44 (move-capture)  ->  0.44 (swap)
    handoff_surfaces 2.05  ->  2.05                 ->  0.00

`render` windowed fell 8.07 to 7.24. **Frame time did not move at either
resolution** (retina 18.53 -> 18.22, windowed 9.08 -> 9.04, both inside
spread), and the reason is now directly observed rather than inferred:

| | `render_slot_wait` (CPU blocked on GPU) | `render_prep_wait` (main blocked on worker) |
|---|---|---|
| retina async | **3.00 ms, 135/149 frames** | 13/149 |
| windowed async | 2/149 | **31/149** |

Retina is GPU-bound, and this is the first time it has been shown by the CPU
actually blocking rather than deduced from a gap. Windowed has become
WORKER-bound: the main thread now outruns the prep worker, so waiting on prep
appears. Two ms of real CPU work was removed and the bottleneck relocated both
times, which is S12's cap behaving exactly as described.

The swap is kept regardless: it removes real work, it is strictly better code,
and it pays the moment either cap lifts. **No frame win is claimed for it.**

**Next levers, now that the caps are named:** retina needs GPU work removed
(or both-sides work, per the decision rule). Windowed needs `render_prep`
itself faster, since 4.8 ms on one worker is now the floor there.

**Not done, if someone wants the last of the handoff:** `handoff_particles`
is still a real 0.42 ms copy of 19,104 particles. Removing it means
double-buffering at the particle-system level, because physics mutates that
array on the main thread. Options considered for the surface side, kept here
for reference:
- double-buffer the surface and particle INPUT arrays the way the outputs
  already are (`PREP_BUFFER_SLOTS = 3`), so the worker reads a buffer the main
  thread is not writing and nothing needs copying;
- or stop detaching, and join at a defined point so references stay valid;
- or move surface collection itself into the worker, leaving nothing to hand
  over. `render_collect` is another 3.89 ms, and it rose 0.69 ms under async,
  which is consistent with cache pressure from the copies.

**RCA note, because this is the second time.** The first reading of this result
blamed machine load (average 5.7-6.0, an unrelated engine build running) and
called the experiment inconclusive. That was wrong and it is the same
misattribution recorded in the 2026-07-31 baseline entry, where an 11.6 ms
"regression" was blamed on contention and then on a bisect before counters
settled it. Load was real and the spread was real, but the mechanism was sitting
in the phase split the whole time. **Read the split before blaming the box.**

**Two defects found by reading, both still unfixed and both pre-existing:**
- A `std::thread` is created and DETACHED every frame for the prep worker. That
  is a 29th thread per frame in a render path that already creates 28, while
  `tile_thread_pool.cpp` sits unused.
- The worker captures `camera_system` BY REFERENCE while surfaces and particles
  are copied by value, with a comment explaining that references dangle because
  the detached thread outlives the caller. The camera has the same exposure, and
  `prepare_gpu_data` reads it. `CameraSystem` holds a `unique_ptr` so it is
  move-only and cannot simply be snapshotted; fixing it means passing the values
  prep needs. The equivalence test uses a STATIC camera and therefore does not
  exercise this. Note also that with a moving camera async legitimately preps
  against a one-frame-stale camera, so some divergence there is design, not bug,
  and the two causes must be separated before reading any red result.

**Two measurement lessons, both self-inflicted this session:**
- The first A/B silently compared sync against sync. The shell glob `*sync)`
  matches `a2_async`, because "async" ends in "sync". Both legs ran the same
  configuration and the table looked entirely plausible. The counter that caught
  it was `render_handoff`, non-zero only in real async mode. **Always carry a
  counter that proves the lever engaged**; the sensitivity control is not
  optional, and the same check caught the equivalence test running blind with
  telemetry disabled.
- `render_slot_wait` reads 0.00 as a MEDIAN but fires on 21 to 35 of 199 frames,
  up to 6.7 ms. Earlier entries in this journal, including the 2026-07-31
  baseline, read that median as "the CPU never waits". It does wait, on roughly
  one frame in eight. Medians hide tails; for anything spiky, report the
  non-zero count too.

### S15: NO ACTION x2, the shadow triangle ratio and sphere shadows (2026-08-01)

Two candidate levers priced and both closed, one by reading and one by
arithmetic. Total cost: no code.

**The 1.9x shadow-to-render triangle ratio is not waste.** The shadow path
carries 196,596 triangles against the render path's 103,754. Per particle that
is 10.29 against 5.98, and a box is 12 triangles, so the render path is
back-face culling (12 to 6) and the shadow path is not.

That is correct and already documented at `render_pipeline.cpp:380`. Build-time
back-face culling is the WRONG FRAME OF REFERENCE for shadows: rays run from
shaded points toward lights in every direction, so a triangle facing away from
the CAMERA still occludes. The per-ray cull that IS correct already happens on
the GPU, in `shadow_rays_deferred.metal` stage 2, which skips directional groups
whose average normal faces away from that particular ray. Every face is required
input for it. A previous session already removed the dead per-triangle normals
left over from the abandoned attempt (196,596 cross products a frame).

**Analytic sphere shadows would buy exactly zero in Eden.** 196,596 is precisely
16,383 x 12. An exact multiple of a box's 12, with no remainder for anything
else. Spheres emit 80 triangles at subdivision 1 and 320 at subdivision 2, and
neither leaves an integer count of boxes behind. Every shadow caster in Eden is
a box; there are no spheres in the shadow set.

The idea is still sound for scenes that HAVE spheres (the falling-bodies
benchmark, where sphere subdivision measured 2.6x). It is simply not an Eden
lever, and Eden is the reality check. Do not spend a block on it expecting the
Eden frame to move.

**Method note.** Both answers came from counters already being recorded, in
about ten minutes and no experiment. The divisibility check in particular
settled a question that would otherwise have cost a full implementation to
answer. Before building an optimisation, check whether the counters can price
it first.

**What survives:** render-path LOD. It is the only identified lever that cuts
CPU collect, prep_triangles, prep_shadow_tris AND the largest GPU stage
(pass1_gbuffer 5.52 ms) at once, which is what the decision rule demands while
retina is balanced.

### S16: the largest GPU stage was invisible, and it prices LOD (2026-08-01)

`GpuStage::AccelBuild` and `AccelRefit` were declared in the enum and never
recorded. The Metal acceleration-structure build was timed only by an `NSLog`
on a 1-in-60 sample, and its command buffer was created with a plain
`[commandQueue commandBuffer]` rather than `createTrackedCommandBuffer`, so it
was missing from `gpu_window` too. Both fixed.

    accel_build   5.81 ms median   <- LARGEST GPU stage in the engine
    pass1_gbuffer 4.80 ms
    accel_refit   1.87 ms

**The AS build is bigger than the G-buffer pass, and nothing measured it.**
Every "GPU stage sum" in this journal before today omitted it.

**But it is hidden.** Tracking the buffer took buffers/frame from 12 to 13 while
GPU busy moved only 18.09 to 18.22 ms, +0.13. The AS build runs almost entirely
CONCURRENT with the render passes, because it is a separate submission with no
data dependency on them.

**That corrects S13.** Stage overlap measured 1.01x there, which was true of the
render passes among themselves (the serialized experiment confirms it) but was
computed with the largest stage missing. Including the AS, stage sum over busy
is about 1.4x. Two different things were being called "overlap": the render
passes are serial with each other, and the AS overlaps all of them.

**What this prices, which is why it was instrumented.** Refit requires an
unchanged triangle count (`triangle_count == accel_triangle_count_` in
`gpu_rasterizer.mm`). Any LOD switch changes the count and forces the build
path: **3.94 ms more GPU work per frame, refit 1.87 to build 5.81.**

`SPHERE_LOD_DESIGN.md` trap 1 called hysteresis a PERFORMANCE requirement rather
than a visual nicety, and it was right, but its numbers were stale: it cited
`prep_bvh` at 4.65 ms with a 9.47 ms rebuild, and `prep_bvh` is now 0.00 because
the CPU shadow trees went dormant under `HardwareRT`. The concern relocated
intact from the CPU trees to the Metal AS, and now has a number again.

**Open, and it matters for LOD:** whether that extra 3.94 ms stays hidden.
It overlaps today at 91% GPU occupancy, which leaves roughly 9% of headroom, so
a per-frame full build may or may not keep hiding. Measure before designing the
hysteresis bands around it.

### S17: smooth normals work, and level 1 beats today's level 2 (2026-08-01)

`SPHERE_LOD_DESIGN.md` step 1: judge smooth normals on the shadow terminator
before anything else, because the answer decides which LOD levels are usable at
all. Implemented as ANALYTIC normals for spheres rather than interpolated
vertex normals: the G-buffer kernel derives `normalize(world_pos - centre)` per
pixel. That is exact, costs no bandwidth (it reuses two `TriangleLit` padding
fields, so the struct stays 176 bytes), and is the HARSHEST possible version of
the test, since perfectly smooth shading over coarse geometry maximises the
disagreement with the geometry that shadow rays actually hit.

| | silhouette | terminator | verdict |
|---|---|---|---|
| LOD 0 smooth, 20 tris | hard polygon | **hard black wedges** | artifact confirmed |
| **LOD 1 smooth, 80 tris** | **smooth** | **clean roll-off** | **viable** |
| LOD 2 flat, 320 tris (today) | visibly faceted | facet steps | current default |

**The terminator artifact is real and the test can see it.** At LOD 0 there are
hard black wedges cutting into smoothly shaded areas exactly where the sphere
rolls into shadow, which is the failure the design doc named as the sharpest
objection to the whole idea. That matters: a green result at LOD 1 means
something because the same test goes red at LOD 0.

**At LOD 1 it is gone, and LOD 1 smooth looks better than LOD 2 flat.** Today's
default renders visibly faceted spheres at 320 triangles. Smooth normals at 80
render round ones. **A quarter of the triangles for better quality.**

Correctness signal: the flat-vs-smooth pixel difference shrinks monotonically
with subdivision (3,494 pixels at delta>=8 for LOD 0, then 2,807, 1,314, 317),
which is what must happen as facets fall below a pixel.

**What this does NOT yet show.** No performance number: the doc says judge the
artifact first and that is all this did. Spheres only, because a sphere is the
one shape whose analytic normal is exact from a single centre. And Eden has no
spheres in its shadow set (S15), so the Eden frame will not move from this;
the win is in sphere-heavy scenes and in what it unlocks for LOD generally.

**What it unlocks.** Triangle count no longer has to serve SHADING, only the
SILHOUETTE, which needs far fewer triangles. That changes the LOD design from
"how much quality do we sacrifice" to "how coarse can the silhouette get", a
much better trade. The screen-size LOD work (design doc step 2) can now target
level 1 as its floor instead of level 2.

**OWNER VERDICT (2026-08-01): ship LOD 1 + smooth as the default, keep LOD 2 as
a quality setting.** Judged on `test_shadow_lod_wall`, a new scene built for
this question: four IDENTICAL spheres at increasing distance from one light,
throwing shadows magnified 10.0x, 3.3x, 1.7x and 1.1x onto a pale wall. Verdict
in the owner's words: "LOD1 with smoothing is great, but LOD2 shadows are
superbly nice, this should be a quality config as well".

**That distinction is the ray-tracing-first constraint, observed rather than
argued.** Smooth normals fix how the sphere is SHADED. They do nothing for its
SHADOW, because shadow rays hit the real triangles, so at level 1 the shadow
silhouette is still an 80-gon. On a shadow magnified 10x that reads, and level 2
still wins there. Shading can lie; geometry cannot.

**Which points straight at the next piece of work.** Render LOD and shadow LOD
are being forced to the same value and they want different ones: level 1 renders
a perfect sphere, level 2 casts a better shadow. `SPHERE_LOD_DESIGN.md` trap 2
already said the shadow path needs its own criterion; this is the first direct
evidence for it. Decoupling them would give both at close to level 1 cost, since
`GetShadowTriangles` is a separate call from the render geometry.

**No performance number is claimed for this.** `test_shadow_lod_wall` carries
4 spheres among ~1,463 particles, so level 2 to level 1 moves 960 shadow
triangles out of 18,776 and the frame delta is noise. It is a QUALITY scene, by
design. The performance figure for sphere subdivision remains the sphere-heavy
falling-bodies measurement: 2.6x, 58.02 to 22.30 ms.

Defaults now: `SPHERE_SUBDIVISIONS = 1`, smooth normals ON. Both still
overridable at runtime (`LOGOSPHERE_SPHERE_LOD`, `LOGOSPHERE_SMOOTH_SPHERES=0`).

### S18: physics IS apply_all_forces, and it is O(n^1.38) (2026-08-01)

The falling-bodies ramp, run to 16,000 bodies, put a number on where the engine
stops holding 60 FPS and why.

**Rendering is not the limit.** Render cost per body is FLAT at 2.6 to 2.9 us
from 2,000 to 16,000 bodies. Perfectly linear and well behaved.

**Physics is, and the per-frame number hides how badly.** Read per STEP:

| bodies | ms / physics step | steps/sec achieved | frame ms |
|---|---|---|---|
| ~0 | 1.3 | 41.2 | 8.8 |
| 2,000 | 12.3 | 27.9 | 16.0 |
| 4,000 | 29.6 | 15.5 | 33.0 |
| 6,000 | 51.0 | 9.9 | 52.1 |
| 16,000 | 66.0 | 5.5 | 90.1 |

**The knee is 3,000 to 4,000 bodies**, where a step crosses ~30 ms and the
stepper starts falling behind. Past it two things compound: frames get slower
AND the step rate collapses from ~41/s to 5.5/s, so the world runs in slow
motion on top of the framerate loss. Per-FRAME physics appears to plateau near
20-27 ms past 6,000 bodies, which reads as "physics stopped growing". It did
not. Fewer, far more expensive steps are being taken. Same trap as
`render_slot_wait`: the aggregate hid the tail.

**Split, and the answer is one function.** Six sub-phases added around the
substep loop. At every body count:

| bodies | phys_forces | physics | share |
|---|---|---|---|
| 2,000 | 2.02 | 2.07 | 97.6% |
| 6,000 | 17.76 | 17.96 | 98.9% |
| 12,000 | 23.82 | 24.21 | 98.4% |

Everything else is noise at the largest bin: angular velocity 0.08 ms, angular
limits 0.00, integrate 0.16, boundary 0.04, rest state 0.02. **`apply_all_forces`
is 98 to 99% of physics**, and it scales:

    2.02 -> 23.82 ms across 2,000 -> 12,000 bodies
    11.8x for 6.0x the bodies  =>  O(n^1.38)

**Where it stops, and the next split.** CORRECTED 2026-08-01: `apply_all_forces`
is **30 lines**, not 2,036. The earlier figure subtracted the next function's
line number without checking what lay between, which is a measurement mistake of
exactly the kind this journal exists to catch. What it actually contains is an
O(n) gravity loop and a single call to `solve_contacts_v3`, and THAT is
**1,981 lines** (386-2367) holding broad phase, contact detection and the
constraint solver as one number. The attribution to `apply_all_forces` stands
(it is 98-99% of physics); only the location of the bulk was wrong. The superlinearity is inside it and this ramp cannot say which part.
The plausible story is contacts (a settling pile has more contacts per body than
scattered bodies, so contact count grows superlinearly even with a perfect broad
phase), but that is a guess, and this session already produced three plausible
mechanisms that turned out wrong. **Split apply_all_forces and add contact and
broad-phase-pair counters before designing anything.**

Note there are still NO physics counters at all: not a contact count, not a
broad-phase pair count, not a solver iteration count. The render side's biggest
wins (S10, S11) both came from counting work rather than timing it.

### S19: the solver NEVER converges, so constraint order is load-bearing (2026-08-01)

Seven physics counters added, the first in the engine. The decisive one records
which door the solver's iteration loop takes: converged (impulses under
`ABSOLUTE_THRESHOLD`) or plateaued ("more iterations won't help") or exhausted
(full budget). Counters are integer increments and the characterization checksum
is unchanged (`7b597182f47cffed`), so they are provably behaviour-neutral.

| bodies | converged | **plateaued** | exhausted |
|---|---|---|---|
| 2,000 | 0 | **4** | 0 |
| 6,000 | 0 | **4** | 0 |
| 10,000 | 0 | **4** | 0 |
| 14,000 | 0 | **4** | 0 |

`SOLVER_SUBSTEPS = 4`. So the plateau exit fires on EVERY substep of EVERY
frame, and the converged exit has never once fired at any body count. **The
solve always stops short of equilibrium.**

> **THE INFERENCE BELOW IS WITHDRAWN. See S20.** The DATA stands (plateau exit
> always, converged exit never), but the counter measures the wrong quantity, so
> "never converges" does not follow from it and neither does anything built on
> that. Constraint order being load-bearing is now an OPEN question, not a
> finding, and parallelism is not closed. The original text is kept below
> because the reasoning was the mistake, not the measurement.

**What that decides.** This is a sequential-impulse (Gauss-Seidel) solver: each
constraint is applied against velocities already modified by the constraints
before it. When the solve reaches equilibrium the order is a bit-pattern detail.
When it stops short, the order is baked into the PHYSICAL answer. It always
stops short. Therefore **constraint order is load-bearing, and sorting, batching,
islanding, SoA and any parallel solve are closed** as optimisation routes. They
would not merely move the checksum, they would change the simulation.

**Why this was measured instead of experimented on.** The obvious test (reorder
constraints, compare positions under tolerance) is confounded: a settling pile
is chaotic, so a single last-bit difference diverges visibly over 150 frames
whether or not the solver converges equivalently. That experiment returns
"different" in both worlds and proves nothing. Two counters answer it directly
and cost nothing.

**And the n^1.38 decomposes.** Across 2,000 to 14,000 bodies:

| | 2,000 | 14,000 | factor | reading |
|---|---|---|---|---|
| constraints per body | 1.70 | 3.48 | 2.05x | the pile densifies; not just more bodies |
| iterations per solve | 23.5 | 13.1 | **0.56x** | it gives up SOONER as it grows |
| solver work, rows x iters | 409,558 | 916,604 | 2.24x | |
| `phys_forces` | 14.12 ms | 50.05 ms | 3.54x | |
| ns per row-iteration | 34.5 | 54.6 | **1.58x** | memory, not algorithm |
| broad-phase candidates per body | 1.0 | 3.4 | 3.4x | |

So the superlinearity is roughly 2.05x from a denser pile times 1.58x from each
row getting more expensive, and the falling iteration count is masking part of
it. **The engine is not doing more work per unit of quality as it scales; it is
doing more work AND accepting worse answers.** Iterations per solve falling from
23.5 to 13.1 means the plateau detector is firing earlier in bigger piles, which
is a quality regression hiding inside a performance measurement.

**Where the remaining levers are, now that parallelism is closed:**
1. **Fewer constraints.** Constraints per body doubling is the single largest
   term. Broad-phase candidates per body also more than tripled, so some of it
   is pair generation rather than genuine contacts.
2. **Cheaper rows.** 1.58x per row-iteration at constant algorithm is cache
   behaviour on a growing working set, which is a data-layout problem the
   refactor can address without touching order.
3. **Convergence quality.** The plateau threshold is being hit at 13 iterations
   out of a 32 budget. Whether that is the right trade at scale has never been
   examined, and it interacts with the gluon distance constraint stalling at
   0.0813 m against a 0.03 m budget (its own known red).

### S20: the convergence counter measures the wrong thing, and the floor solves itself (2026-08-01)

S19 read the solver's exit door and concluded it never converges. Before acting
on that, a ladder isolating ONE variable (contact-chain depth: 1, 2, 3, 4, 6, 8
boxes stacked on a floor, otherwise identical) was run to find the simplest
scene that fails. `tests/test_solver_convergence_ladder.cpp`.

| stack | converged | plateaued | iters/solve | rows/frame |
|---|---|---|---|---|
| 1 | 0 | 120 | 9.0 | 648 |
| 2 | 0 | 120 | 9.0 | 648 |
| 4 | 0 | 120 | 9.0 | 648 |
| 8 | 0 | 120 | 9.0 | 648 |

**Two things in that table are more informative than the verdict.**

**FINDING 1: S19's inference is withdrawn.** (The REASON given below for that
withdrawal was itself wrong. See the correction at the end of this entry.)

`iters/solve` is 9.0 at every rung. `MIN_ITERATIONS = 6` and
`PLATEAU_CONFIRM = 3`, and 6 + 3 = 9. The plateau detector fires at the earliest
moment it is legally allowed to, always, at every complexity. That is not a
solver struggling; that is a detector with no dynamic range.

~~And the other door cannot open at all. It tests
`max_impulse_this_iter < ABSOLUTE_THRESHOLD` with the threshold at 0.01. At
equilibrium the support impulse a resting body needs is exactly the impulse that
cancels gravity, m*g*dt_substep, which for a 1 m stone box is on the order of
100 N s. Thousands of times above the threshold. The converged exit is dead code
for any scene containing a body with mass at rest.~~ **WRONG, see below.**

So "plateaued" does not mean "failed to converge". Improvement falling below 5%
per iteration at a steady state is precisely what a CONVERGED solver looks like:
the solution has stopped moving. **The counter conflates "converged" with
"stuck", and reports both as stuck.**

Consequences: S19's data stands, S19's conclusion does not. Whether constraint
order is load-bearing is REOPENED. Parallelism, islands, SoA and sorting are not
closed. Answering it properly needs a CONSTRAINT RESIDUAL (how much violation
remains when the solve stops), which is a different measurement from impulse
magnitude and does not yet exist.

**CORRECTION 2026-08-01 (same day): the "unreachable by construction" argument
above is WRONG.** It is the fifth withdrawn conclusion of this campaign and the
second in this entry's own subject.

`max_impulse_this_iter` is not the support impulse. `physics_system_v4.cpp:2025`
reads `impulse = c.accumulated_impulse - old_impulse` and line 2026 takes the max
of THAT: a per-iteration DELTA, the standard residual for a sequential-impulse
solver. Warm starting (`:1729`) seeds the accumulator from the previous frame, so
at a steady state the delta is near zero from the first iteration. Comparing it
to 0.01 is entirely sensible. The ~100 N s figure is the accumulated total, which
this test never looks at.

Re-running the same ladder after the S22 fix, with nothing else changed:

| stack | converged | plateaued | iters/solve | rest z |
|---|---|---|---|---|
| 1 | **120** | 0 | **1.0** | 0.6000 |
| 4 | **120** | 0 | **1.0** | 0.6000 |
| 8 | **120** | 0 | **1.0** | 0.5987 |

Converged at every rung, on the FIRST iteration. The door was never dead code;
the phantom row was holding it shut, exactly as S22 concluded. The lesson stands
but the example was fabricated by reasoning about a variable without reading the
line that assigns it, which is the same failure this journal keeps recording.

**The residual metric is still needed, and this table is the better argument for
it.** `rest z` drifts 0.6000 to 0.5987 as the stack deepens: 1.3 mm of
penetration at 8 boxes that the solver reports as CONVERGED. A delta-of-impulse
test says the iteration stopped moving. It cannot say the constraints are
satisfied, and those are different questions.

**FINDING 2: the floor generates constraints against itself, and it dominates.**

`rows/frame` is identical at 648 whether the stack is 1 box or 8. The stack
contributes essentially nothing. Sweeping the floor extent instead:

| floor tiles | rows/frame |
|---|---|
| 1 | 8 |
| 9 | 232 |
| 25 | 648 |
| 49 | 1320 |

**About 26 constraint rows per floor tile**, against 8 rows for the single
dynamic box. Floor tiles are KINEMATIC and `is_at_rest`, and they touch their
neighbours, so the engine builds and solves contact constraints between pairs of
bodies that cannot move and cannot respond. Every substep, forever.

The skip at the top of contact detection is
`if (pi.is_at_rest && pi.solver_mode != KINEMATIC) continue;`, which deliberately
keeps KINEMATIC bodies in the loop (the comment explains that kinematic actors
can be teleported and still need contacts generated). That is right for
KINEMATIC-vs-DYNAMIC. It is waste for KINEMATIC-vs-KINEMATIC, where neither side
can take an impulse.

This is the largest concrete inefficiency found in physics so far and it is
independent of the convergence question.

**Method note, and it is the fourth time this session.** Instruments honest,
model wrong. "Stages overlap", "residency not work", "the machine was loaded",
and now "the solver never converges" all explained a real measurement with a
wrong mechanism. The new wrinkle: this time the instrument itself measured the
wrong QUANTITY. A counter is not automatically better than a timer. **Before
trusting a counter, ask what value it would take in the healthy case** and check
that value is reachable. `ABSOLUTE_THRESHOLD = 0.01` fails that test on
inspection, and would have failed it before the ramp was ever run.

### S21: a phantom impulse from immovable pairs blocks convergence (2026-08-01)

The layered physics tracer (`src/core/physics_trace.h`, levels 0-6, append-only,
one relaxed atomic load when off) answered this on its first run.

**The solver converges fine. Something else was pinning the exit test.**

Exit reasons by floor size, ladder scene, single dynamic box:

| floor | converged | plateaued | exhausted |
|---|---|---|---|
| **1 tile** | **1458** | 300 | 42 |
| 9 tiles | **0** | 1799 | 1 |
| 25 tiles | **0** | 1799 | 1 |

One KINEMATIC slab: converges on 81% of solves. Nine tiles: never once. The
floor tiles are what destroy convergence, and they are inert scenery.

> **THE MECHANISM BELOW IS NOT CONFIRMED. See the correction at the end of this
> entry.** A TDD test written to reproduce it came back GREEN twice: a line of
> two, and a line of three, touching immovable tiles all converge. The story
> predicts they break. It is kept because the code asymmetry is real and may be
> necessary; it is demonstrably not sufficient.

**THE MECHANISM, and it is a bug.** Two sites disagree about what immovable
means:

    build  (:856)  inv_ma = pi.is_at_rest ? 0 : 1/mass          // is_at_rest ONLY
    apply  (:2035) inv_ma = (is_at_rest || KINEMATIC) ? 0 : ...  // both

For two KINEMATIC floor tiles resting against each other, `inv_mass_sum` is 0,
so the guard at :859 falls back to **`effective_mass = 1.0f`**. Then at :2000
`impulse = -(v_rel - bias) * 1.0` is NON-ZERO, driven by the Baumgarte bias of
their mutual penetration, and :2007 folds it into `max_impulse_this_iter`. The
apply step then multiplies by `inv_ma = inv_mb = 0` and **nothing moves**.

A phantom impulse. No physical effect, but it pins the global max above
`ABSOLUTE_THRESHOLD = 0.01` on every iteration forever, so the converged exit
can never fire while any such pair exists.

**What this explains, all at once:**
- S19's "the solver never converges": it converges; the exit test was being held
  hostage by inert geometry. Withdrawn twice now, and this is the actual why.
- S20's ~26 constraint rows per floor tile: those rows exist AND they are
  actively harmful, not merely wasteful.
- The plateau firing at exactly `MIN_ITERATIONS + PLATEAU_CONFIRM = 9` every
  time: with the converged door nailed shut, the plateau door is the only exit,
  and improvement hits zero immediately because the phantom impulse is constant.

**The fix is one condition:** skip contact generation when both bodies have zero
inverse mass. Neither can respond, so the constraint has no meaning. Expected to
remove ~26 rows per floor tile AND restore the converged exit. That is a real
change to what the solver computes, so it needs its own re-pinned baseline and
must not ride along with a refactor.

**And it reopens the biggest question.** If the solve genuinely converges, then
constraint order is a bit-pattern detail rather than physics, and parallelism,
islands, SoA and sorting come back on the table. S19 closed them; S20 reopened
the question; this makes the answer likely to be "order does not matter" once
the phantom pairs are gone. Worth settling with a residual metric before
betting on it.

**Caveat on the ladder that produced this.** The battery design (five lenses,
independent) caught that `tests/test_solver_convergence_ladder.cpp` used a
25-tile floor, and since the exit test is a global max over ALL rows, the
original ladder measured the floor rather than the stack. Its "one box
plateaus" reading was contaminated. The floor sweep above is the corrected
experiment, and it turned the contamination into the finding.

**CORRECTION (same day, from the TDD test).**
`tests/test_immovable_pair_phantom_impulse.cpp` was written to reproduce the
mechanism above and refused to, twice. The measured cliff is not where the story
put it:

| arrangement, all KINEMATIC and at_rest | converged | plateaued | rows |
|---|---|---|---|
| 1 tile + dynamic box | 120 | 0 | 480 |
| 1x2 line | 120 | 0 | 480 |
| **1x3 line** | **120** | **0** | 960 |
| **2x2 grid** | **0** | **120** | 2,880 |
| 3x3 grid | 0 | 120 | 9,600 |

**A STRIP of touching immovable tiles converges. A GRID never does.** Both are
full of zero-inverse-mass pairs, so `inv_mass_sum == 0` falling back to
`effective_mass = 1.0f` cannot be the whole story: it is present in the 1x2 case
that works. Something about the 2D arrangement crosses the line, and the visible
difference is that every tile in a grid gains a DIAGONAL neighbour, taking rows
per tile from 320 to 720. That has not been isolated.

**What survives, and it is still the important part:** any 2D tiled floor is
past the cliff. Every real floor in this engine is 2D, which is why Eden never
converges, and why the ladder in `test_solver_convergence_ladder.cpp` was
measuring its own floor rather than its stack.

**What to do next, and NOT before:** trace level 5 on a 2x2 and on a 1x3 and
find which constraint owns `max_impulse` in the one and not the other. The
tracer exists for exactly this. Do not fix from the story; the story is 0 for 2.

**Method note.** This is what decision-level tracing buys that counters cannot.
A counter said "the solve plateaued". The trace said WHY, with the improvement
rate and the threshold side by side, and the answer was visible in one run
without a theory laid over it.

### S22: FIXED, and it was one wrong constant (2026-08-01)

Trace level 5 on each arrangement alone, which is what S21 said to do next:

| | rows with impulse > 0.01 |
|---|---|
| 1x3 line | **0** |
| 2x2 square | **17,280**, 100% `BOTH_IMMOVABLE`, always pair a=1 b=2 |

In a 2x2 laid at (0,0) (1,0) (0,1) (1,1), ids 1 and 2 are the **DIAGONAL** pair.
A line has no diagonals. That is the entire difference between an engine that
converges and one that never does.

Every one of those rows was identical: **impulse 4, effective_mass 1, bias 4**,
unchanged forever. Two corner-touching immovable tiles produce a contact whose
Baumgarte bias is 4; `inv_mass_sum` is 0 so the effective-mass guard fell back
to `1.0f`; that yields a non-zero impulse which is then applied to two bodies
with zero inverse mass and moves nothing; the next iteration recomputes 4. Since
the stopping test is a GLOBAL max over every row, one such row pinned the whole
world above threshold permanently.

**The fix is one value:**

```cpp
- float effective_mass = (inv_mass_sum > 0.0f) ? (1.0f / inv_mass_sum) : 1.0f;
+ float effective_mass = (inv_mass_sum > 0.0f) ? (1.0f / inv_mass_sum) : 0.0f;
```

`inv_mass_sum == 0` means infinite effective mass: no finite impulse changes
anything, so the row's correct contribution is zero. The `1.0f` was an arbitrary
divide-by-zero guard. **No new branch and no special case** — the conditional
already existed, only its wrong constant changed. That matters here, because
`docs/ENGINE_INVARIANTS.md` rule 1 forbids exactly the kind of edge-case
if-statement this could easily have been.

**Result:** every arrangement now converges 120/120, including 3x3 with a
dynamic body. Physics guards unchanged at 19 pass / 0 fail. Characterization
still deterministic, gluon chain still held, baseline deliberately re-pinned
`7b597182f47cffed` to `16af12523829b082`.
`tests/test_immovable_pair_phantom_impulse.cpp` goes from RED spec to
regression guard.

**What this reopens.** S19 concluded that constraint order is load-bearing and
closed parallelism, islands, SoA and sorting, on the strength of the solver
never converging. It converges now. That conclusion has been withdrawn twice and
should now be re-examined properly with a residual metric rather than the door
counters, which S20 showed cannot see equilibrium.

**What it does NOT do, deliberately:**
- The wasted rows are now HARMLESS, not absent. ~26 constraint rows per floor
  tile are still built and solved. Skipping generation when both inverse masses
  are zero is the performance half and touches contact generation.
- A corner contact between two unit tiles reporting a penetration bias of 4 is
  suspicious on its own and was not chased.

**Method note.** Four withdrawn conclusions preceded this: "stages overlap",
"residency not work", "the machine was loaded", "the solver never converges". The
thing that finally worked was tracing the DECISION rather than the aggregate.
A counter said the solve plateaued; the level-5 trace named the row, the pair,
and the three numbers that produced it, and the mechanism was then obvious. Build
the tracer earlier next time.

---

### S23: the residual instrument, and what it took to make it honest (2026-08-02)

S19's question (is constraint order load-bearing?) cannot be reopened without a
metric that answers "is the answer good", separately from "did the loop stop".
Built as `telemetry::SolveResidual`, off by default
(`LOGOSPHERE_PHYS_RESIDUAL=1`), one read-only pass after the solve.
`tests/test_solver_residual.cpp`.

**It was wrong three times before it was right, and each failure was caught by a
control rather than by inspection.** Worth recording, because the instrument
looked correct every time.

**1. It measured the wrong quantity.** The first version computed a VELOCITY
residual: how far `v_rel` sits from the target the row asked for. It read
**exactly 0.00000 on two boxes spawned 40% inside each other**. Not a bug in the
measurement. Baumgarte converts a position error into a velocity TARGET, so
hitting that target perfectly is what success looks like at the velocity level
no matter how much overlap remains. The velocity residual is blind to
penetration by construction. Penetration is now stored on the row
(`Constraint::penetration`) because it cannot be recovered from `bias`, which is
clamped to `MAX_BIAS_VELOCITY` on deep overlaps and zeroed inside the slop band.

**2. It sampled at the wrong time.** Reading once at the end of a settled run
gave zeros for every rung. Reason: once a scene settles, every body is flagged
at-rest, at-rest bodies have zero inverse mass, and a row between two of those
is unsolvable. **The final frame has nothing solvable in it at all** (`fin rows`
= 0 on every rung below). The error is not absent; it was made during settling
and then frozen when the bodies went to sleep. The test now takes the peak
across the window.

**3. A stale global outlived the engine that produced it.** The settled scene
reported its worst pair as `9-10` in a world containing ten particles, ids 0
through 9. Particle 10 was from the PREVIOUS run: the published value is
process-wide, and a second engine reads the first one's last solve until its own
overwrites it. `set_residual_enabled()` now clears on every transition. This was
only visible because the worst PAIR is printed next to the number, which is the
S22 lesson holding: a max with no named owner cannot be sanity-checked.

**The study, once the instrument was trustworthy:**

| scene | peak pen | worst pair | peak rows | final rows | unsolvable | rest z |
|---|---|---|---|---|---|---|
| 40% overlap (control) | 0.26667 | 9-10 @f1 | 40 | 40 | 80 | 0.6000 |
| stack of 1 | 0.00068 | 4-9 @f1 | 36 | **0** | 116 | 0.6000 |
| stack of 2 | 0.00068 | 4-9 @f1 | 40 | **0** | 116 | 0.6000 |
| stack of 4 | 0.00068 | 4-9 @f1 | 48 | **0** | 116 | 0.6000 |
| stack of 8 | **0.00212** | 4-9 **@f11** | 64 | **0** | 116 | 0.5987 |

- **The metric has the dynamic range the counters lacked.** The exit counters
  report 120/120 CONVERGED on every rung, identically. The residual separates
  them: 0.68 mm through stack 4, 2.12 mm at stack 8, and the peak moves from
  frame 1 to frame 11, so deeper stacks take longer to reach a worse answer.
  2.12 mm of peak penetration against 1.3 mm of final sink (0.5987 vs 0.6000) is
  the same effect measured two independent ways.
- **Pair 4-9 is the centre floor tile and the bottom box**, every time. The
  worst error in the world is the base of the stack pressing into the floor,
  which is what a person would predict, and is the first time this campaign's
  physics instrumentation has agreed with intuition unprompted.
- **At steady state the solver has nothing to solve.** `final rows` is 0 on
  every settled rung while `unsolvable` holds at 116. Every row in a settled
  world is between two bodies that both refuse to move. This generalises S20's
  "~26 rows per floor tile": it is not a floor-tile quirk, it is what every
  settled contact becomes.

**Also fixed here:** the speculative-contact path carried the same
`inv_mass_sum == 0 ? 1.0f` guard that S22 corrected in the box-box path. Latent
rather than active (its bias is 0, so the phantom impulse was 0 too), but one
wrong copy left behind is how a bug returns. Characterization checksum is
UNCHANGED at `16af12523829b082` across both edits, which is the evidence that
they are behaviour-neutral. Guards 19 pass / 0 fail.

**Still open, and now measurable:** whether constraint ORDER changes that
residual. That is S19's actual question and the instrument for it now exists.

---

### S24: order does NOT change the answer, on one scene (2026-08-02)

S19's question, finally asked properly. `LOGOSPHERE_PHYS_SHUFFLE=<seed>` permutes
constraint order deterministically before each solve;
`tests/test_constraint_order_matters.cpp` runs a leaning six-box pile (offset so
every contact couples to every other, giving order the best chance it will get).

| run | peak pen | bottom z | sum z |
|---|---|---|---|
| A, no shuffle | 0.000821 | 0.599857 | 18.597842 |
| A again, control | 0.000821 | 0.599857 | 18.597842 |
| **NOISE FLOOR** | **0.000000** | 0.000000 | 0.000000 |
| shuffled seed=1 | 0.000681 | 0.600000 | 18.599441 |
| shuffled seed=7 | 0.000681 | 0.600000 | 18.599937 |
| shuffled seed=12345 | 0.000681 | 0.600000 | 18.599884 |
| shuffled seed=99991 | 0.000681 | 0.600000 | 18.599346 |
| **SPREAD among shuffles** | **0.000000** | | 0.000591 |

**Four different permutations agree on the residual to every printed digit**,
against a same-input noise floor of exactly zero. Their POSITIONS differ
(`sum z` spread 0.000591 m), which is both the chaos S19 warned about and the
proof the lever engaged: if the shuffle had silently no-opped, positions would
match too. This is why the verdict is taken on the residual and not on where the
boxes landed, and it is the case S19 assumed was impossible.

**SCOPE, stated because it is the whole risk here.** One scene, six boxes, one
solver configuration. Not Eden at 19,000 particles. If it holds when widened,
parallelism, islands, SoA and contact sorting all reopen, and physics is the
engine's scaling limit. One scene is a lead, not a finding.

**Separate observation, not part of the verdict:** the natural order is not
neutral. Every shuffle landed on 0.000681 m and the unshuffled run on 0.000821 m,
with the bottom box at 0.599857 against an exact 0.600000. **Whatever ordering
contact generation emits is slightly WORSE here than an arbitrary one.** Unchased.

**Two ways this nearly produced a wrong answer:**

1. **The statistic did not match the sentence.** The first verdict compared every
   shuffle against the UNSHUFFLED run and announced order was load-bearing, on
   data showing four orders agreeing exactly. "Is order load-bearing" means "do
   different orders disagree with each other", so the statistic is the spread
   AMONG shuffles. Comparing to the natural order answers a different question.
   It also divided by a zero noise floor and printed "a factor of 0.0".
2. **The lever corrupted an index-keyed cache.** `warm_started_impulses` is keyed
   by constraint INDEX, filled before the solve and read back after to update the
   impulse cache. Shuffling between those points files one pair's equilibrium
   impulse under another pair's key. The first placement sat just above the
   iteration loop and did exactly that, so the experiment measured "shuffle plus
   a broken warm start". Moving the shuffle above warm starting fixed it; the
   headline survived, the position numbers moved.

Characterization unchanged at `16af12523829b082` (seed 0 does not touch the
array), guards 19 pass / 0 fail.

---

### S25: the physics battery, and it found nothing (2026-08-02)

Eight scenarios, one mechanism each, every rung adding exactly one thing to the
one before it. `tests/test_physics_battery.cpp`. Scenes are deliberately tiny
(3x3 floors) because the residual is a global max and scenery contributes to it.

| scenario | peak pen | rows | unsolv | physical check | got | want |
|---|---|---|---|---|---|---|
| 1 free fall | 0.00000 | 0 | 0 | drop after 1 s | 4.9379 | 4.9050 |
| 2 single impact | **0.00000** | 125 | 116 | rest height | 0.6007 | 0.6000 |
| 3 resting contact | 0.00068 | 36 | 116 | drift | 0.0000 | 0.0000 |
| 4 stack of 4 | 0.00068 | 48 | 116 | bottom z | 0.6000 | 0.6000 |
| 5 leaning pile | 0.00071 | 32 | 96 | top still up | 4.5999 | 4.6000 |
| 6 projectile | **0.00748** | 52 | 104 | (reports only) | | |
| 7 sphere on plane | 0.00000 | 90 | 81 | rest height | 0.6000 | 0.6000 |
| 8 gluon nails | 0.00000 | 3 | 0 | bond drift | 0.0000 | 0.0000 |

**The honest headline is that it found no flaw.** Every physical check holds and
every residual outside scenario 6 sits under the 1 mm slop band the solver is
entitled to ignore. That is a real result and it was not the expected one; the
battery was built to expose weaknesses at scales between "one box" and "Eden".

**The one signal: momentum from outside costs 10x.** Scenario 6 throws a body at
12 m/s into a tower and peaks at 7.5 mm, an order of magnitude above every
gravity-driven scene. Load arriving suddenly is where the solver is furthest
from legal, which is the correct place to look next and is consistent with
issue #5 (heavy impacts on layered ground rippling into oversized tile
explosions).

**A zero worth explaining rather than celebrating.** Scenario 2 drops a box 2.4 m
and records EXACTLY zero penetration, less than the gently-placed box in
scenario 3. That looks wrong and is not: speculative contacts create a row while
the gap is still open and allow a controlled approach that closes it without
overshoot, so a clean vertical drop is stopped at contact rather than after
overlap. The 0.00068 m in scenarios 3 and 4 is a resting body settling inside
the 1 mm slop band. Both numbers are the design working; neither would be
obvious from the table alone.

**Scenario 6 asserts nothing on purpose.** Where a struck tower ends up is
chaotic, and asserting on it would produce a test that fails for reasons that
are not bugs. It reports its residual and stops there.

**What the battery does NOT yet cover:** rotation under torque (scenario 7
settles a sphere but does not spin it), breaking gluons (scenario 8 pins
material strength to 1e9 so the bond cannot break, since holding is what it
tests), stacks deeper than 5, and anything above 12 bodies. Each is a rung
someone can add without touching the harness.

---

## Open threads

**Physics, reopened by S22.** The solver converges as of `4e773ec`. Everything
below was closed or shaped by the belief that it never does.

- **S19 must be re-run with a RESIDUAL metric.** It closed parallelism, islands,
  SoA and constraint sorting on the premise that order is load-bearing because
  the solve never settles. The premise is gone. ~~Needed first is a metric that
  reports how far from satisfied the constraint set is~~ **THE METRIC NOW EXISTS**
  (S23) ~~What remains is the experiment itself~~ **DONE, S24: order does not
  change the residual on a leaning six-box pile.** What remains is WIDTH. One
  scene is not a finding, and the scenes that matter are the ones with deep
  contact chains and many islands, where sequential impulse has the most room to
  stop somewhere order-dependent. Widen to the battery (below) and to Eden before
  acting. If it holds, parallelism, islands, SoA and sorting are all back on the
  table for the engine's actual scaling limit.
- **The natural constraint order is measurably worse than a random one** (S24:
  0.000821 m against 0.000681 m, and the bottom box lands at 0.599857 instead of
  an exact 0.600000). Whatever ordering contact generation happens to emit is not
  neutral. Small, real, and unexplained.
- **The wasted rows are harmless, not absent.** ~26 constraint rows per floor
  tile are still built and solved between pairs that can never move. Skipping
  GENERATION for zero-inverse-mass pairs is the performance half of S22 and
  touches contact generation, not the solver.
- **A corner contact between two unit tiles reports a Baumgarte bias of 4.**
  Suspicious on its own, never chased. Two tiles meeting at a corner should not
  register that much penetration.
- **`apply_all_forces` is 98% of physics and scales O(n^1.38)** (S18), and 60
  FPS breaks at 3-4k bodies. `solve_contacts_v3` is a 1,981-line function. No
  profiling has gone inside either.
- ~~**The physics test battery was designed and never built.**~~ **BUILT, S25**,
  eight rungs, `tests/test_physics_battery.cpp`. It found no flaw at this scale.
  Gaps left for whoever extends it: rotation under torque, breaking gluons,
  stacks deeper than 5, and anything above 12 bodies.
- **Sudden load is the weak spot, and it is the only one the battery found.** A
  body thrown at 12 m/s into a tower leaves 7.5 mm of penetration against under
  1 mm for every gravity-driven scene. Likely the same mechanism as issue #5
  (heavy impacts on layered ground producing oversized tile explosions), and the
  next thing to point the residual at.

**Render and infrastructure.**

- **Task #28.** Serialized diagnostic mode (true isolated stage cost) and the
  baseline regression gate. The occupancy correction (S12) is done and the
  serialized mode shipped (S13); what remains is `benchmarks/baseline.json`
  generated on a quiet machine, so a regression fails a test instead of being
  noticed later.
- **Issue #31: async GPU prep is measured, correct-looking, and OFF.** The
  performance question is closed (+1.88 ms retina, +3.43 ms windowed,
  pixel-identical). Enabling it renders foliage black: the KG mapping is read
  live against a stale particle snapshot. This is the only open thread where the
  work is already done and a correctness bug is the sole blocker.
- **Task #29.** CPU render path. Remaining options: GPU-side geometry
  expansion (removes the work rather than shaving it), or accumulating CPU
  wins until the sum is measurable. Neither attempted.
- **Sphere subdivision.** Measured 2026-07-30. Level 2 (320 tris) to level 1
  (80) is 2.6x on the falling-bodies frame, 58.02 to 22.30 ms. Visible on large
  spheres, indistinguishable below ~1 m on screen, so a global constant is the
  wrong shape for the knob. `Optimizations::SPHERE_SUBDIVISIONS` now exposes it,
  `LOGOSPHERE_SPHERE_LOD` overrides at runtime, and
  `tests/test_sphere_lod_quality.cpp` renders every level with pixel diffs plus
  an interactive mode. Design discussion, including why FPS-feedback LOD is
  rejected and why smooth normals collide with ray-traced shadows:
  `SPHERE_LOD_DESIGN.md`.
- **Hot-path scan (2026-07-30), re-verified against the code 2026-08-01.** Same
  defect class as S10. Two of the five are now done:
  - ~~`entity_bvh.cpp:74` deep-copies every `EntityTriangleData`~~ **FIXED** in
    `6a66ade`. Facing buckets live in a parallel array and `entities` is read
    only; the ~30 MB of per-frame memcpy is gone. The RCA is written at the
    site.
  - ~~`get_world_faces` still carries the copy and is called by
    `triangle_cube_geometry.cpp`~~ **NO LONGER A HOT PATH.** Those callers are
    gone (see the note at `sphere_geometry.cpp:210`); the only remaining caller
    is internal, at `particle_geometry_v2.cpp:463`. The copy still exists but is
    off the per-frame path.
  - **STILL OPEN: 28 `std::thread` created and joined per frame.** 14 in
    `collect_surfaces` (`render_pipeline.cpp:1826`) and 14 in the
    shadow-triangle pass (`:434`), plus more in `shadow_ray_batch.cpp:88`.
    `tile_thread_pool.cpp` exists and `surface_rasterizer.cpp` uses it; these
    three sites do not. Cost unmeasured: price it before building it (ledger
    learning 10).
  - **STILL OPEN: no LOD anywhere in the render path.** A two-pixel body still
    emits 320 triangles. This is the identified UNCAPPED lever, because it cuts
    CPU collect/prep and GPU rasterisation together (S12).
  - **STILL OPEN, but priced at zero for Eden: analytic sphere shadows.**
    Spheres cast as 320 triangles where one ray-sphere intersection is exact.
    S15 found every Eden shadow caster is a box (196,596 = 16,383 x 12 exactly),
    so the win there is nil; it pays only in sphere-heavy scenes.
