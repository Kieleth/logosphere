# Performance measurement methodology

_Written 2026-07-29. This document existed as an authority long before it
existed as a file: `optimization_flags.h`, `lighting_metrics.h` and
others cite "PERFORMANCE_RESEARCH.md" as the source of their design, but
it was never tracked in git — not even in the initial commit. The
principles were real and correct; being uncitable is why instrumentation
drifted into ad-hoc probes anyway. This is the missing document, with the
original principles restated and two amended._

## The problem this solves

Measuring a system changes it. The finer the measurement, the more it
changes. A profiler that times every call produces precise numbers about
a machine that no longer resembles the one you ship.

So instrumentation is tiered by cost, and each tier states plainly what
it can and cannot answer.

## The three tiers

### Tier 1 — Counters (aggregate, always compiled, runtime-toggleable)

Count operations. No clock, no allocation, no string keys: an increment
into a fixed array indexed by a compile-time enum.

Answers: how many shadow rays, BVH nodes, triangles, draw calls, cache
misses by proxy. Enough to find algorithmic blowups, the class of bug
where a count grows with the wrong variable. This tier is also the only
one that can tell a superlinear ALGORITHM from a superlinear WORKLOAD:
timings look identical in both cases, counts do not (kit study S7).

Cannot answer: where the time went.

Counters must survive worker-thread death. Thread-local storage that
deregisters on exit silently reports zero for every count incremented
off the main thread, and a missing count is indistinguishable from
"it did not happen" (kit study S9).

### Tier 2 — Phase timers (frame granularity, always compiled, runtime-toggleable)

One clock read at each end of a frame-level phase (update, physics,
render, present). Tens of these per frame, not millions.

Answers: which phase owns the frame, frame to frame.

Cannot answer: anything inside a phase, and — critically — anything about
GPU stages that overlap (see below).

### Tier 3 — Deep probes (compile-time gated, OFF by default)

Statistical sampling inside hot paths, per-batch or per-N-frames timers,
serialized GPU passes. Genuinely alters performance; compiled out
entirely unless requested.

Answers: the inside of a phase, isolated stage cost.

Cannot answer: what the shipping build does, because it is not the
shipping build.

## Principles

1. **Count operations; do not time individual calls.** The boundary is
   frequency, not importance: per-ray and per-pixel work gets counters,
   frame-level phases get timers. (Original principle, clarified — the
   old comments never stated the boundary, so probes landed on both
   sides of it.)

2. **Use statistical sampling for anything inside a hot path.** 1 batch
   in 100, 1 frame in 60. (Original principle, unchanged.)

3. **~~Never profile in shipping builds.~~ AMENDED 2026-07-29:** tiers 1
   and 2 stay compiled in and are runtime-toggleable, defaulting to off.
   Tier 3 remains compile-time gated and absent from shipping builds.

   _Rationale for the amendment:_ if enabling measurement requires a
   rebuild, you can never measure the binary you actually ship — and
   build provenance has already burned this project (a stale metallib
   produced a fake 15 ms "win"; two fp16 "fixes" were later falsified).
   A branch on an atomic bool, predicted-not-taken, is cheaper than that
   class of error. The cost is verified, not assumed: see "Proving
   zero-cost" below.

4. **Instrumentation is code, not decoration.** Probes go through the
   telemetry interfaces. No `std::cout` in frame paths, no static
   counters hand-rolled at a call site, no bespoke `[TAG]` printf probes
   left behind after an investigation. (New principle — written because
   the 2026-07 campaigns found exactly these: checkpoint prints in the
   GPU drain, a hardcoded stall probe in `engine.cpp`, scattered statics
   in the rasterizer.)

5. **A one-sided win is capped by the CPU/GPU gap.** REWRITTEN
   2026-08-01; the original blamed the wrong mechanism. A 2 ms stage
   win has bought zero frame time four times (ledger G4, the
   pow-to-squarings experiment, kit S8, and kit S9, which cut
   render_collect 24% for nothing). The standing explanation was that
   GPU stages overlap so their costs are not real. **That was wrong.**
   Serializing every pass changed per-stage cost by under 1% (kit S13):
   the passes have data dependencies and were already running
   effectively serially, so the stage numbers were true isolated costs
   all along.

   The actual mechanism is balance (kit S12). At retina the frame
   carries CPU 19.74 ms against GPU 18.92 ms. Cut either side and the
   other becomes the wall 0.81 ms later, whatever the stage-level
   saving looks like. Windowed there is 6.59 ms of GPU headroom, so
   CPU wins pay in full there and not at retina.

   **The rule that follows.** Before optimising, measure both sides.
   Then either (a) attack the longer side and stop at the gap, or (b)
   pick work that reduces BOTH sides, which is the only uncapped
   lever. Geometry is the clear case: fewer triangles cut CPU collect
   and prep AND GPU rasterisation. Report a stage saving as isolated
   cost and predict frame time only from the gap.

6. **Count allocations, not just operations.** The costliest thing found in
   this engine was not an algorithm. It was constant data rebuilt inside a
   per-element loop: the unit icosphere, re-subdivided per sphere per call
   with two hash maps and 640 heap allocations, twice a frame. Roughly five
   million allocations per frame to reproduce numbers that never change.
   Timings do not point at this, and neither do operation counters. The
   questions that do: what in this function is the same for every element,
   and how many allocations does one element cost? Three tells, all of them
   present here (kit study S10):
   - a constructor called inside a per-element loop, building shared data;
   - a `get_world_*` / `to_*` helper returning a container **by value**;
   - a small fixed-size struct holding `std::vector` members, which turns
     any container of it into an allocation storm.
   (New principle, 2026-07-30.)

7. **A number without provenance is an anecdote.** Every recorded run
   carries git SHA, dirty flag, build type, metallib hash, host, mode.
   Thermal state is part of the measurement: the same build has measured
   16.4 ms and 26.0 ms in one session, so A/B work interleaves trials
   rather than running sequential blocks. (New principle.)

## Proving zero-cost

The amendment in principle 3 is only defensible if the disabled cost is
measured rather than asserted. The claim to verify: with telemetry
compiled in and toggled off, frame time is indistinguishable from a
build with it compiled out.

Method: A-B-A interleaved retina benches, plus the pixel oracles to
confirm the instrumentation changes no output. Recorded in
`PERF_RESEARCH_KIT.md`'s journal, not here.

## What this replaces

`MetricsCollector` (string-keyed phase map — a hash lookup and potential
allocation per phase per frame, with call sites that ran regardless of
`ENABLE_PROFILING`) and the counter half of `LightingMetrics`. Both are
superseded by `logosphere::telemetry`; the published `EngineMetrics`
snapshot survives as the consumer-facing frame summary.

## See also

- `docs/todo_plans/PERF_RESEARCH_KIT.md` — the measurement harness,
  sweep design, and study journal (S1-S10).
- `docs/todo_plans/GPU_OPT_LEDGER.md` — optimization items, with the
  A/B protocol and the falsified results.
- `docs/todo_plans/SPHERE_LOD_DESIGN.md` — dynamic LOD design discussion.
- `src/core/telemetry.h` — the sessions/instruments/journal primitives
  this doc describes.
- [METAL.md](METAL.md) and [SHADOW_SYSTEM.md](SHADOW_SYSTEM.md) — the
  GPU pipeline the measurements target.
