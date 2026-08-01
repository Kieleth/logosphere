# Orbit rendering handover — smoothness work for the GPU session

_Written 2026-07-31 by the enablers session, after landing the
isometric azimuth orbit (#9, PR #15-#17). The orbit works and is the
demo centerpiece candidate; this document hands the remaining
smoothness work to the GPU-renderer session._

## What the orbit is

`CameraSystem::set_view_azimuth(radians)` pre-rotates view-space XY
around world +Z before the fixed 45-degree isometric math
(CW-positive, compass convention; azimuth 0 is bit-identical to the
classic view). Everything downstream honors it: projection, depth,
both inverse transforms (picking), frustum-culling probe directions,
compass widget, camera follow offset, GPU temporal shadow
reprojection. Drivers today: arrow keys (CameraController), the
Logogenesis `OrbitSeed` tween, and anything that calls the setter per
frame.

Contract tests: `test_iso_azimuth` (90 checks, headless),
`test_iso_azimuth_roundtrip` (32 checks), `test_orbit_performance`
(A/B/A study + post-orbit persistence ratchet at 1.3x).

## The symptom

Orbiting judders. Not average-FPS slowness: the study shows the
orbit costs nothing on average (orbit/static x0.98 small scene,
x0.84 big scene, zero post-orbit residue). The judder is
frame-pacing variance made visible by motion.

## Measured numbers (Release build, M-series, 1600x1200 headless)

`./build/test_orbit_performance`, 2026-07-31:

| Phase | avg | median | p90 | max |
|---|---|---|---|---|
| 366 particles, static | 9.36 ms | 9.18 | 13.82 | 18.86 |
| 366 particles, orbit | 9.17 ms | 9.07 | 13.89 | 16.10 |
| 1918 particles, static | 12.39 ms | 11.18 | 21.96 | 39.02 |
| 1918 particles, orbit | 10.38 ms | 10.11 | 15.16 | 19.26 |

Read the p90/median ratio: every ~10th frame costs 1.5-2x the
median, worst case ~4x. The spikes exist in the STATIC phases too;
a parked isometric view hides them, an orbit converts each one into
a visible angular jump. Eden interactive (19.8k particles) idles ~15
ms steady with the same spike pattern on top.

## Suspects, in order

1. **Periodic BVH rebuilds (issue #6).** The stall signature matches
   the tracked issue exactly; the study corroborates it headless at
   both scene sizes. The 2026-07-31 shadow-BVH refactor ("stop
   copying every entity") already helped; the remaining spikes look
   like the rebuild tick itself. Dirty-refit / static-dynamic split /
   rebuild throttling are the known candidate directions
   (GPU_OPT_LEDGER.md has groundwork).
2. **Temporal shadow history invalidation during orbit** (introduced
   with #15, deliberate). Translation reprojection cannot compensate
   a rotating frame, so an azimuth change pushes a huge camera delta
   and the soft-shadow running average restarts every orbit frame
   (`gpu_rasterizer.mm`, search `prev_view_azimuth_`;
   `shadow_rays_deferred.metal`, `motion_detected`). Cost is
   near-zero (rays trace every frame regardless) but QUALITY drops:
   shadows resample from count 1 continuously while orbiting, which
   reads as shimmer/roughness layered on the judder. Candidate fix:
   during pure-azimuth motion, reproject by rotating the sample
   position around the screen-space orbit center instead of
   invalidating (exact for the ground plane, approximate above it),
   or drop to a cheaper converged-lighting hold during motion and
   fast-reconverge on stop.
3. **Per-frame GPU prep.** GPU_PREP on Eden-scale scenes rebuilds
   and re-uploads gpu_tri/shadow_tri buffers every frame even when
   only the camera moves; triangles are world-space, so camera-only
   frames could reuse every buffer. The unoptimized-build log showed
   the shape clearly (56 ms prep of an 82 ms frame); Release shrinks
   it but the structure is unchanged. If prep becomes
   camera-invariant, orbiting inherits it for free.
4. **Driver-side dt jitter (game layer, minor).** The Logogenesis
   orbit tween and arrow-key orbit step azimuth by dt each frame; a
   spiky frame produces a proportionally bigger angular step, which
   amplifies perceived judder. Once 1-3 flatten the frame times this
   mostly disappears; a dt clamp in the tween is a cheap game-side
   smoother if wanted sooner.

## What NOT to relitigate

- The orbit math itself is contract-locked and cheap (4 mul + 2 add
  per projected point). Azimuth 0 is bit-identical to the old view.
- Average cost is a non-issue; do not spend time "optimizing the
  rotation."
- `cmake -S . -B build` now defaults to Release (#18). Any
  benchmarking must confirm `-O3` is active; the 6-FPS "regression"
  of 2026-07-31 was an unoptimized build, nothing else.

## Definition of done (proposed)

Orbiting Eden's garden at 19.8k particles holds p90 within 1.25x of
median frame time, no visible shadow shimmer during a 12 s
revolution, and `test_orbit_performance` gains a variance ratchet
(p90/median) locking whatever the fix achieves.
