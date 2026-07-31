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

**Standing conclusions.** Cost tracks **surfaces**, not particles and not
lights. A sphere at subdivision 2 emits 320 surfaces against a box's ~12.
Lights are nearly free. The engine's CPU render path is linear in surface
count. No CPU optimization tried so far has crossed the frame-time noise
floor, because stages overlap and the scenes tested are GPU-bound.

## Why this exists

Nearly every error in the 2026-07 GPU campaign was a MEASUREMENT failure,
not an engineering one. A stale-metallib mirage produced a fake win. Two
fp16 "fixes" were falsified once oracle nondeterminism was found. Thermal
drift repeatedly exceeded the effect under test (same build, 16.4 and
26.0 ms in one session). Per-pass GPU timings sampled 1-in-60 gave n=9 per
stage. And everything was regex-scraped from stdout into scratchpad scripts
wiped between sessions. A performance claim should be reproducible evidence,
not a remembered number.

**The measurement problem.** Stages overlap: at retina the per-stage medians
sum to 21.5 ms while the frame is 16.1 ms. A 2 ms stage win can buy zero
frame time, confirmed three times now (ledger G4, S8, S9). Stage cost and
critical-path cost are different questions, and only a serialized diagnostic
mode (task #28) answers the second.

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
  THERMAL SUSPECT drift flag, OVERLAP FACTOR, baseline regression compare.
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

---

## Open threads

- **Task #28.** Serialized diagnostic mode (true isolated stage cost) and the
  baseline regression gate.
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
