# Porting shadows to a new platform

_Written 2026-08-01, from a measurement session on M4 Max. Read this before
starting a Linux or Windows port: there is a broken fallback waiting for you,
and it fails silently._

## The one thing you need to know first

**The software shadow path renders no lighting. If your target has no hardware
ray tracing, you will get a black scene, and nothing will tell you why.**

It is not missing code. The path compiles, builds its acceleration structures,
uploads them and dispatches its kernel. The output is byte-identical to the
same scene with the lights switched off. Fixing or replacing it is the first
task of any port to hardware without ray tracing.

`test_shadow_accel_backend` proves this on demand and prints the defect in
full. It does **not** fail the suite, because every currently supported target
has Metal RT and takes the other branch, so failing would block unrelated work.
When you fix the path, flip that check to a hard failure. That is how you know
you are done.

## The seam

Shadow rays trace against exactly one spatial structure, chosen per platform:

| backend | who owns the structure | who traces it | cost of the CPU trees |
|---|---|---|---|
| `HardwareRT` | the driver (`MTLAccelerationStructure`; DXR or Vulkan RT on a port) | `trace_shadows_deterministic`, via `intersector<triangle_data>` | **none — they are not built** |
| `SoftwareBVH` | the engine (`TriangleBVH` + `EntityBVH`) | `trace_shadow_rays_deferred_batched`, walking `bvh_nodes` | built and uploaded every frame |

`GPURasterizer::shadow_accel_backend()` decides. Everything that builds
acceleration data must ask it rather than assume. That rule exists because
breaking it is what this seam was created to fix: both CPU trees were rebuilt
and uploaded every frame while the hardware structure did the actual tracing.
Measured waste: **2.16 ms of a 21.7 ms Eden frame, and up to 97 ms on a single
frame in a spawning scene, 13% of that run.**

Override with `LOGOSPHERE_SHADOW_ACCEL=hardware|software`, or
`Logosphere::set_forced_shadow_accel_backend()`. Forcing `hardware` on a device
without support is ignored — capability wins. Forcing `software` always works,
which is what lets you exercise and debug the portable path on RT hardware
before you have any other machine to test on.

## What is shared regardless of backend

The **shadow triangle array is always built**, under both backends. Do not
"optimise" it away when you make the trees dormant:

- the hardware acceleration structure is built from it, and
- `trace_shadows_deterministic` binds it at `buffer(10)` for solid-angle lookup
  after a hit.

Only the *trees over* that data are backend-specific.

## Do not confuse this with the physics BVH

`ParticleSystem::shadow_bvh_` is a **different structure**: a `BVH` over
*particles*, queried by `physics_system_v4.cpp`, `humanoid_locomotion.cpp` and
`pixel_lighting_strategy.cpp`. It shares part of a name and nothing else. It is
load-bearing for physics and animation on every platform, and none of the above
applies to it.

## Suggested order for a port

1. Get `test_shadow_accel_backend` running on your target. It will report which
   backend you are on and whether the fallback lights the scene.
2. If you are on `SoftwareBVH`, fix the fallback first. Everything visual is
   meaningless until it lights the scene.
3. When it works, flip the known-defect check to a hard failure so it cannot
   regress.
4. If your platform has ray tracing (DXR, Vulkan RT), implement it behind
   `HardwareRT` and the CPU trees go dormant automatically. No render-pipeline
   changes required — that is the point of the seam.

## How the defect was found

Not by reading the code. Five separate attempts to observe a visual difference
from suppressing the CPU trees all showed zero change, which looked like the
trees being harmless. They were not being read at all. The pixel test was blind
by construction: under `HardwareRT` the kernel never touches them, so
suppressing them cannot change anything.

Two measurement lessons came out of it, both already burned once:

- **An A-vs-A control is mandatory.** Re-rendering the identical configuration
  moved 30,274 pixels in this engine (equal-depth geometry has a documented
  ±1 LSB nondeterminism). Without that control, noise reads as signal — it did,
  and produced a confident wrong answer.
- **Judge by magnitude, not by "pixels differ".** A lost shadow moves pixels by
  tens or hundreds. Anything with a max delta of 1 or 2 is the noise floor.
