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
  (proven) and net zero (measured), because the handoff deep-copies 103,914
  surfaces per frame, costing what the prep saves. The fix is named: remove the
  copy. See S14.
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

**Where it stops, and the next split.** `apply_all_forces` is 2,036 lines
holding gravity, broad phase, contact detection and the constraint solver as one
number. The superlinearity is inside it and this ramp cannot say which part.
The plausible story is contacts (a settling pile has more contacts per body than
scattered bodies, so contact count grows superlinearly even with a perfect broad
phase), but that is a guess, and this session already produced three plausible
mechanisms that turned out wrong. **Split apply_all_forces and add contact and
broad-phase-pair counters before designing anything.**

Note there are still NO physics counters at all: not a contact count, not a
broad-phase pair count, not a solver iteration count. The render side's biggest
wins (S10, S11) both came from counting work rather than timing it.

---

## Open threads

- **Task #28.** Serialized diagnostic mode (true isolated stage cost) and the
  baseline regression gate. The occupancy correction (S12) is done; the
  serialized mode is still wanted, because per-stage attribution inside a
  saturated GPU remains unresolved.
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
- **Hot-path scan (2026-07-30), found but NOT fixed.** Same defect class as S10:
  - `entity_bvh.cpp:74` deep-copies every `EntityTriangleData`, each carrying a
    `std::vector<ShadowTriangle>`. ~30 MB of memcpy per frame plus two
    allocations per entity, solely so `classify_triangles` can fill
    `triangle_directions`; `triangles` is never mutated. Fix needs directions
    in a parallel array and `entities` const through `build_recursive` and
    `create_directional_groups`.
  - **28 `std::thread` created and joined per frame**: 14 in `collect_surfaces`
    (`render_pipeline.cpp:1687`) and 14 in the shadow-triangle pass (`:338`),
    plus more in `shadow_ray_batch.cpp:92`. `tile_thread_pool.cpp` already
    exists and none of these use it.
  - `get_world_faces` (`particle_geometry_v2.cpp:353`) still carries the copy
    and is still called by `triangle_cube_geometry.cpp` (:279, :408).
  - No LOD anywhere in the render path: a two-pixel body still emits 320
    triangles.
  - Spheres cast shadows as 320 triangles where one analytic ray-sphere
    intersection is exact and faster.
