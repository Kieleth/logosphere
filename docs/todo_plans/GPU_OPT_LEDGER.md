# GPU Optimization Ledger — zero-quality-cost campaign

Living document. Every optimization item in the campaign appends one
entry here, positive or negative — negative results are paid-for
knowledge and stay on the books. Companion to
`GPU_PIPELINE_AUDIT_2026-07.md` (the audit that seeded the campaign:
waterfalls, deferred quality-trade ledger, falsified suspects).

Ground rules (user policy, 2026-07):
- **Pure ray tracing stands.** No shadow maps, no rasterized light
  transport, no quality-reducing sampling changes in this lane.
- **Bit-identical or better output**, proven, not asserted.
- Target: 60 FPS at retina-native (3200x2102, 16.6 ms). Windowed
  1600x1051 already at ~116 FPS after the audit fixes.

## The protocol (each item)

1. Implement behind a `constexpr` flag in `src/optimization_flags.h`,
   default = current behavior, audit-style comment.
2. Headless A/B, input-immune (`--headless`), three workloads: Eden
   bench at 1600x1051 and 3200x2102, multi-light scaling curve
   (`test_multi_light_progressive`, PARTICLES/LIGHTS env).
3. Pixel oracles, byte-compared OFF vs ON: staircase-catcher frame +
   multi-light phase dumps at **PARTICLES=144** (the proven
   byte-stable config — see cross-cutting learnings). Contract
   (amended after item C): byte-identical, OR a bounded +/-1-LSB
   epsilon with metric-equal edges, quantified and explicitly
   user-approved per item. Epsilons are never silently accepted.
4. Gates: harness 27/27 (includes `test_shadow_edge_quantization`) +
   guard suite, with the flag ON.
5. Positive + gates green -> raise to the user with the numbers, then
   visual inspection (Eden windowed + multi-light INTERACTIVE).
6. User verdict flips the flag default; discards remove the code and
   keep the entry.

Entry template:

    ### <item> — <title> (<date>)
    Hypothesis / Method / Measurements / Verdict / Learnings

---

### G — Multi-light scaling vehicle (2026-07-23) — SHIPPED

**Hypothesis:** the campaign needs one deterministic workload that is
simultaneously a light-scaling benchmark, a pixel-diff oracle, and the
user's preferred visual inspection scene.

**Method:** rebuilt `test_multi_light_progressive` as a standalone
engine test: PARTICLES env (144 default, thousands supported), LIGHTS
env phases, frame-count-driven floating rainbow lights (fixed 1/60 dt,
KINEMATIC grid), per-phase mean/median/p90 stats, per-phase PPM dumps,
INTERACTIVE=1 with FPS HUD, SPACE/ESC.

**Measurements:** 2000 particles x 6 lights: ~5 ms/phase at Release
headless; light count 1->6 nearly flat (deterministic single-dispatch
shadow path scales with pixels, not lights, at this scene size).

**Verdict:** shipped; the campaign's shared instrument. User feedback
round applied (ESC exit, visible FPS, more lights/colors).

**Learnings:** an instrument the user personally enjoys gets used;
determinism must be *verified*, not designed-in (see cross-cutting).

---

### A — Private storage for GPU-only intermediates (2026-07-23) — NEGATIVE, discarded

**Hypothesis:** the per-pixel passes are bandwidth-bound; moving the
seven audited GPU-only intermediates (blocker distance, penumbra temp,
JFA pair, SSDO results+denoise, DDGI rays) from Shared to Private
storage enables Apple-Silicon lossless bandwidth compression.
(Distinct from the 2025 QW1 experiment, which privatized the three
CPU-read-per-frame buffers and correctly lost.)

**Method:** `USE_PRIVATE_INTERMEDIATE_BUFFERS` flag; storage-mode
switch at the 10 allocation sites; full protocol A/B.

**Measurements:** 1600x1051: 7.9 ms -> 7.9 ms. Retina: 22.8 ms ->
23.3 ms (slightly worse). Zero win anywhere.

**Verdict:** discarded on the metrics. (Initial oracle diffs were later
traced to scene nondeterminism, not the flag — see cross-cutting; the
"uninitialized intermediate read" suspicion remains UNCONFIRMED.)

**Learnings:**
1. **Apple's lossless bandwidth compression is a texture feature.**
   Raw `MTLBuffer` private storage buys nothing on unified memory when
   there is no CPU mapping traffic to eliminate. Check the vendor
   mechanism before building a bandwidth theory on it.
2. Prior-experiment archaeology pays: reading QW1's failure note
   prevented repeating its exact mistake — but did not save the new
   hypothesis, which failed one level deeper (API semantics).
3. A cheap negative (one flag, one A/B session) is a good trade for
   closing a whole optimization avenue permanently.

---

### B — Skip redundant frame-start clears (2026-07-23) — NEGATIVE, discarded

**Hypothesis:** four full-screen frame-start fills (shadow results,
light color, blocker distance, framebuffer clear) write values that
the deterministic shadow path provably overwrites before any read —
~3-4 full-screen writes of pure waste at retina.

**Method:** `SKIP_REDUNDANT_CLEARS` flag, runtime-guarded to the
deterministic RT path (PCSS accumulates with `+=` and needs its zero
base); depth and G-buffer sentinel clears kept. Full protocol A/B.

**Measurements:** 1600x1051: 7.9 -> 7.9 ms. Retina: 22.7 -> 23.0 ms.
Pass-1 samples statistically flat (means 6.97 vs 7.32 ms across noisy
samples). Oracle: catcher + 5 of 6 phases byte-identical; the one
differing phase was scene nondeterminism (control-run proven).

**Verdict:** discarded. The redundancy analysis was *correct*; the
cost hypothesis was not.

**Learnings:**
1. **Metal blit fills are effectively free** next to per-pixel compute
   at the same resolution — DMA-path fills don't compete with shader
   bandwidth the way a compute clear-loop mental model suggests.
2. **Measure the victim before optimizing it away.** Item C applied
   this immediately (timestamp first) and it changed the decision
   calculus. Redundant work at zero marginal cost is not a target.

---

### C — RT acceleration-structure refit (2026-07-23) — SHIPPED

**Hypothesis:** the Metal AS rebuilds all ~17.6k shadow triangles every
frame; a refit (same tree topology, recomputed bounds) recovers most of
that cost with exact hit results.

**Method:** measurement first ([GPU_TIMESTAMP] AS build handler), then
`USE_RT_AS_REFIT`: AS built once with Refit usage, refitted per frame
while triangle count is unchanged; full rebuild on count change, on AS
reallocation (an empty AS cannot be refitted), and every
AS_FULL_REBUILD_INTERVAL=240 frames (tree quality under sustained
motion). Full protocol A/B with the hardened oracle (144-particle
config, A-vs-A control PASSED).

**Measurements:** AS cost 1.85 -> 0.27 ms/frame (6.9x). Eden 1600x1051:
8.1 -> 6.4 ms (123 -> 156 FPS). Retina: 22.8 -> 22.4 ms (AS partially
overlapped there). Live windowed session: 100-137 FPS with chunk
streaming, refits 0.26-0.52 ms, count changes correctly triggered
rebuilds.

**Pixel epsilon (user-accepted):** NOT byte-identical. Every differing
pixel differs by exactly +/-1/255 in one channel, on <=0.03% of pixels
(worst frame 484 of 1.92M; best 5). Staircase edge metric identical
(RMS 0.295 both sides). Mechanism: a refit-capable tree lays out nodes
differently, ray traversal sums floats in a different order, and
near-ties on shared triangle edges round the other way in the last ulp.
Same variation class as a driver update. User verdict 2026-07-23:
"this optimization for this cost is totally acceptable... almost not
human perceivable" -> shipped; campaign contract amended (below).

**Learnings:**
1. FP non-associativity makes "byte-identical" the wrong bar for any
   change that touches traversal ORDER, even when geometry and math
   are untouched. The right bar: bounded LSB epsilon + metric-equal
   edges + explicit user sign-off.
2. Refit needs three rebuild triggers, not one: count change, AS
   reallocation (fresh AS is empty), and a periodic quality rebuild.
3. Wall-clock gain can exceed the component gain (1600: -1.7 ms wall
   from -1.58 ms AS) or undershoot it (retina: -0.4 ms) depending on
   how much the component overlapped other GPU work — attribute wins
   against the pipeline, not the component, before celebrating.

### D — Light-pack filtering + sun radius (2026-07-23) — BOTH CLOSED

**D1 (filter zero-emission lights): NEGATIVE, discarded.** Hypothesis:
foot-pin anchors (is_light_source, emission 0) cost dead shadow rays
per in-range pixel. Measured: 1600 identical (6.4/6.4 ms), retina
within noise, Pass 2 flat; oracles trivially byte-identical; packed
count instrumentation inconclusive (sampled pre-anchor frames).
LEARNING: the shadow kernel's `lambertian > 0` gate already rejects
ground-level lights for upward-facing surfaces BEFORE tracing — the
floor never traced anchor rays at all. Read the kernel's existing
early-outs before pricing "dead work."

**D2 (sun emission_radius 500 -> 60 m): WITHDRAWN before
implementation.** The world streams unboundedly, so any finite radius
has a reachable lit-circle edge; at 60 m the sun still delivers ~44 lux
(2M lumens / 4-pi-d^2), so shrinking it is a visible change for a
roaming player. LEARNING: "the play area is ~30 m" was a static-scene
assumption smuggled into a streaming world; radius tuning in an
unbounded world is a lighting-design decision, not an optimization.

---

### E — Non-profiling-path RCA (2026-07-23) — EXPLAINED, decision pending

**Question:** why does `ENABLE_PER_PASS_GPU_TIMING=false` measure 4x
SLOWER (64.8 vs 16.7 ms at 1600x1051, Phase-1 numbers)? Profiling off
should be equal or faster.

**Finding:** the flag does not toggle timing. It selects between two
complete, independently-written renderer implementations inside
`rasterize_triangles_deferred_async` (the only deferred entry point;
single call site `render_pipeline.cpp:2114`; no synchronous variant
exists anymore):

- **Flag true (shipping, gpu_rasterizer.mm:3682-5958):** per-pass
  command buffers. Deterministic single-dispatch all-lights shadow
  kernel (`trace_shadows_deterministic`, Metal RT acceleration
  structure), penumbra JFA, SSDO + denoise, DDGI, RT-intersector
  transparency. Every optimization since ITER7C landed HERE ONLY,
  including this campaign's items.
- **Flag false (:5959-7246, "SINGLE COMMAND BUFFER: Original
  implementation (baseline)"):** a frozen snapshot of the
  pre-deterministic pipeline. Per-light loop dispatching the retired
  `trace_shadow_rays_deferred` temporal/Monte-Carlo kernel, each light
  a full-screen software-BVH walk (buffers 8-11), plus two
  `newBufferWithBytes` allocations per light per frame, plus the
  software-BVH transparency kernel.

So 64.8 ms is the honest price of the 2025 shadow architecture:
N lights x full-screen software-BVH traversal, versus one deterministic
RT dispatch. Nothing about command-buffer count explains the gap. The
flag-off output is not even the same picture (temporal accumulation vs
deterministic), so the else branch is not a baseline for anything
current.

**Timing overhead in the shipping path:** already near zero. Completion
handlers are sampled (`GPU_PROFILE_SAMPLE_RATE=60`); the per-pass path
measures 6.4 ms / 156 FPS with them registered. There is no profiling
tax to recover.

**Verdict (user, 2026-07-23): delete else branch + flag.** Done:
1,288-line else branch removed, `ENABLE_PER_PASS_GPU_TIMING` retired
(per-pass command buffers are the unconditional production structure;
flag graveyard note in optimization_flags.h). Sampled timing handlers
kept. Gates after deletion: both trees build, harness green incl.
staircase catcher, guards 18/0/1/1, Eden 1600x1051 headless bench
6.4 ms steady / 155.5 FPS (unchanged). Note: the legacy per-light
kernel is NOT orphaned — the shipping branch keeps it as fallback when
the deterministic pipeline is unavailable (PCSS/batched selection);
deletion scope was the else branch only.

**Learnings:**
1. A flag named for a side effect (timing) that actually selects the
   renderer generation is a trap: anyone flipping it to "reduce
   profiling overhead" silently resurrects a retired architecture at
   4x the cost with different pixels.
2. "Keep the old path as a baseline" rots into a lie unless the old
   path is maintained — ours stopped being a baseline the day ITER7C
   landed only on one side of the branch.
3. Duplicated encoder bodies (~1,290 vs ~2,280 lines) mean every fix
   since has been single-sided. C-116 exists for exactly this.

---

### F — Retina-60 lane: instrumentation + G-buffer slim (2026-07-24)

**F0 (instrumentation): SSDO/DDGI timestamps implemented for real.**
Correction on the books: PR #29's CHANGELOG claimed Pass 2.5b/2.5c/
2.7/2.8 timestamps that no commit ever contained. Implemented now.
First fully-attributed retina waterfall (relative shares, thermally
inflated run): SSDO denoise 3x3.9=11.6 ms is the LARGEST block,
penumbra 10.4, G-buffer 10.4, SSDO trace 6.6, shadow 2.4, apply 2.0,
DDGI 0.35, refit 0.5 — sum 43.8 of 49.7 frame ms.

**F1 (G-buffer slim 36->32): SHIPPED.** `GBufferPixel.roughness` was
written every frame but its only readers were the retired SSGI /
BVH-indirect kernels (both compile-time false). Dropped: ~11% less
G-buffer traffic in every pass that touches it (the SSDO denoiser
alone reads 25 G-buffer taps/px/pass), ~54 MB GPU memory at retina.
Oracle: catcher + 4 multi-light phases byte-identical. Bench observed
-1.6 ms retina / -0.5 ms 1600 but thermally confounded — the claim is
strictly-reduced-work + bit-exact, not a number.

**Learnings:**
1. **Metal .air rules must depend on shared .metal includes.** Editing
   `gbuffer_types.metal` recompiled only the directly-edited shaders;
   the metallib linked 36-byte and 32-byte objects side by side and
   the frame went near-black ("no shadow found" in the catcher, fake
   fast bench). Fixed in CMake (METAL_SHARED_HEADERS on all 12 rules)
   — the header-edit variant of the stale-shader class.
2. **Thermal drift breaks sequential A/B at retina.** The identical
   build measured 20.7 and 26.0 ms in one session (5+ consecutive
   retina benches). B,A,B interleave caught it; sequential pairs would
   have minted a fake 2 ms win. Future rounds: alternate short pairs
   or accept only mechanism-backed claims when the band exceeds the
   effect.
3. A dead struct field is not free: it taxes every pass that touches
   the buffer. Audit struct layouts against LIVE readers after
   retiring a consumer.

---

### G2 — fp16 SSDO storage (2026-07-24) — SHIPPED

**Hypothesis:** the SSDO block (trace + 3x 25-tap A-trous denoise, the
largest item in the attributed retina waterfall) is bandwidth-bound;
half4 storage (8 B/px, was 16) halves its I/O with epsilon-class
output cost.

**Method:** `SSDO_HALF_PRECISION` mirrored between optimization_flags.h
and gbuffer_types.metal; `ssdo_pack/unpack` helpers; stamped A/B
(per-stage date + metallib md5), alternating thermal pairs; five
oracles (catcher + 4 MLP phases) through the epsilon quantifier.

**Measurements:** retina 20.8/20.7 -> 19.9/19.7 ms (both pairs agree,
-1.0 ms, ~48 -> ~50.5 FPS); 1600 flat (SSDO scales with pixels).
Epsilon: every differing channel exactly +/-1/255, worst oracle
0.0039% of channels — an order cleaner than the accepted item-C
epsilon. User verdict 2026-07-24 after Eden retina session ("really
good progress"): shipped, default flipped.

**Learnings:**
1. **Metal's constant address space misreads half4 indexing.** The
   buffer held correct data (proven byte-for-byte with the new
   `read_ssdo_debug` inspector) while the apply kernel read zeros
   through `constant half4*`; `device const` reads correctly. Use
   device address space for any fp16-element buffer.
2. **fp16 has a usable floor, and real signals live below it.** Raw
   bounce values (~1e-4, floor 6e-5) flushed to zero and killed the
   glow. Power-of-two range shifts (x1024 store, /1024 load) are
   EXACT in binary FP — they move the representable window without
   adding rounding. Check signal ranges before narrowing storage.
3. **The oracle earned its keep twice:** both failures were invisible
   to the bench (faster either way) and to 3 of 4 MLP phases; only
   the 1-light phase exposed the dead glow. Diverse oracle scenes
   catch what averages hide.
4. **Fire-and-forget background A/B runs produced one self-
   contradictory result set** (stage timings physically too short,
   an epsilon profile matching a build that was no longer on disk).
   Cause not fully proven; mitigation is procedural and permanent:
   campaign scripts run synchronously and every stage logs a
   timestamp + metallib md5, so a poisoned run is detectable on
   sight instead of by archaeology.
5. New permanent instruments: `read_ssdo_debug` (pipeline inspector,
   decodes the pack) and the MLP_SSDO_PROBE env hook (rect scan of
   SSDO channel ranges per phase).

---

### G3 — Penumbra compact-id stream (2026-07-24) — SHIPPED

**Hypothesis:** the penumbra V-blur (largest sub-stage once the chain
was actually instrumented: blurV 4.9 ms vs blurH 1.7) is bound by its
per-tap `gbuffer[nidx].particle_id` read — 4 useful bytes through a
32-byte struct stride, column-wise, up to 65 rows per pixel.

**Method:** `PENUMBRA_COMPACT_IDS`: the H-blur emits a packed uint id
buffer as a free second output (it already loads the id); the V-blur
taps that instead of the G-buffer. Same ids, same comparisons, same
arithmetic order — bit-exact by construction. Found by finally
instrumenting the chain per sub-stage: the old "Penumbra JFA+blur"
timestamp hung on the LAST command buffer only (blurV), hiding seed +
6 JFA passes + blurH in the unattributed gap.

**Measurements:** A-B-A sandwich at retina: 19.8 -> 17.1 -> 19.7 ms
(**-2.7 ms**, 50.7 -> 58.5 FPS — the lane's biggest single win).
Oracles: catcher + all 4 MLP phases BYTE-IDENTICAL. Late-run bench
stages (B2, restore) again thermally contaminated and discarded; the
sandwich carried the verdict.

**Learnings:**
1. A timestamp on the last command buffer of a chain is not a
   timestamp of the chain. Instrument sub-stages before ranking
   blocks — the "penumbra 10.4" figure was blurV alone.
2. Struct-of-everything buffers tax narrow readers: a 4-byte field
   read through a 32-byte stride wastes 8x the bandwidth, and
   column-wise walks multiply it by the working-set height. Emitting
   a packed side-stream from a pass that already holds the value is
   nearly free.
3. The A-B-A sandwich beats paired runs under thermal drift: two
   matching A endpoints bracket the B measurement.

---

### G4 — SSDO halton constant-fold (2026-07-24) — NEGATIVE, discarded

**Hypothesis:** the SSDO trace computes two halton radical inverses
(divide-loops) per sample, 32x per pixel, 215M evaluations/frame at
retina — pure functions of the loop index; a compile-time sample count
lets the compiler unroll and fold them to constants.

**Method:** `SSDO_FOLD_SAMPLES` metal define; A-B-A sandwich +
five oracles.

**Measurements:** oracles all BYTE-IDENTICAL (the fold is exact).
Bench: A1 18.1 -> B 18.2 -> A2 18.5 — B inside the A endpoints, zero
measurable win. Discarded on the metrics.

**Learning:** ALU under a memory-latency-bound kernel is free. The
trace's cost is its 16 SCATTERED G-buffer samples per pixel (cache-
hostile by design of SSAO), and the divide-loops rode entirely under
that latency. Attribute a kernel's cost to memory vs ALU before
optimizing either; a folded constant that saves nothing is still code
to maintain.

---

### G5 — Oracle frame-skip nondeterminism: RCA + two falsifications (2026-07-24)

**Trigger:** the raster-bbox A/B showed the fp16 glow-loss signature in
an experiment that never touches SSDO. Same-build controls confirmed:
the SHIPPED binary rendered two different stable phase-1 states across
runs (run1 != run2 == run3 == run4, byte-level).

**Root cause:** the non-blocking GPU frame sync (shipped for windowed
play) makes `engine.render()` DROP frames when the GPU is behind.
Which animation frames actually render is timing-dependent; the MLP
scene's lights move per frame, so runs lock into divergent render
trajectories (proven by two-frame dumps: no cross-frame match — the
content diverges, it is not a dump-off-by-one). Any build that changes
GPU frame time changes the skip pattern, which masquerades as a pixel
diff of the change under test.

**Fix:** headless MLP serializes every frame (wait_for_completion per
frame). Four-run control: byte-identical across all phases. The
per-phase stats now measure full GPU frame time — the honest scaling
number.

**Falsified by the fixed oracle (both G2 "fixes" were coincidence —
the recompile just changed the skip trajectory):**
1. ~~"Metal's constant address space misreads half4"~~ — `constant`
   and `device const` render byte-identically. Binding reverted to
   `constant`.
2. ~~"bounce underflows fp16 min normal; x1024 range shift needed"~~ —
   shift vs no-shift differs by ONE +/-1 channel across four oracle
   frames. Shift removed (minimum code).

**Re-verified with the trustworthy oracle:** the shipped fp16 epsilon
claim STANDS — +/-1/255 on <=0.0039% of channels (66-224 per frame),
matching the accepted numbers.

**Learnings:**
1. A pipelined renderer is a nondeterminism source for any
   fixed-frame-count consumer. Oracles must serialize frames; benches
   should not (throughput is the pipelined number).
2. When two experiments produce the SAME diff histogram, suspect the
   harness, not the changes. The glow signature repeating across
   unrelated flags was the tell.
3. Same-build controls (A-vs-A, run x4) are the only defense against
   accepting a coincidence as a mechanism — both falsified "fixes"
   had plausible theories and passing verifications.

---

### H1 — Raster reject-path bbox stream (2026-07-24) — SHIPPED

**Hypothesis:** the per-pixel raster copies the FULL 176-byte
TriangleLit and recomputes the screen bbox (6-way min/max) per
candidate triangle BEFORE the reject; a precomputed 16-byte int4 bbox
stream (built in the binning prep from the same floats with the same
casts) should carry the reject path.

**Method:** `RASTER_BBOX_STREAM` mirrored pair; bboxes computed once
per triangle CPU-side; kernel loads the full struct only on bbox pass.
A-B-A sandwich + five serialized oracles.

**Measurements:** retina A1 17.0 -> B 16.7 -> A2 17.3 (-0.4 ms);
oracles all BYTE-IDENTICAL. Post-flip confirmation run: **16.6 ms /
60.3 FPS — the retina-native 60 FPS target, reached.** The win is
smaller than the load-volume theory predicted (the tile's triangle
data is largely cache-resident across its 4096 pixels); the bbox
precompute removes redundant per-pixel min/max work.

---

### H2 — Binning tile-size sweep (2026-07-24) — NEGATIVE ON BALANCE, 64 kept

**Hypothesis:** smaller tiles shorten each pixel's triangle list in the
raster walk; 32px tiles should cut Pass 1.

**Method:** unified the duplicated constant first (the kernel hardcoded
64 beside a "must match" comment — a C++-side flip alone would have
made pixels walk the WRONG tile's list; plausibly the origin of the
ancient "(32 caused issues)" note, which no longer reproduces).
Mirrored define; A-B-A sandwich + serialized oracles + CPU binning
telemetry + 1600 cross-check.

**Measurements:** retina 16.7 -> 16.3 -> 16.6 (-0.35 ms GPU,
byte-identical oracles, binning never disabled). BUT: CPU binning
0.75 -> 2.3-2.6 ms (3x), and 1600 slightly worse (5.5 -> 5.7 mean).
Verdict: kept 64 — buying 0.35 ms GPU with 1.6 ms CPU is the wrong
trade with live play CPU-bound (the next lane's target). The constant
unification ships (hazard fix, zero cost).

**Learnings:**
1. A GPU-side win that migrates cost to the CPU is not a win in a
   pipelined engine whose live bottleneck is the CPU side. Bench the
   resource you are ABOUT to be bound on, not just the one you are
   optimizing.
2. Mirrored-constant hygiene again: the sweep was impossible to run
   safely until the kernel stopped hardcoding its own copy.

---

### I1 — Live-stall RCA: a 50 ms sleep on the deletion path (2026-07-29) — FIXED

**Symptom (user-visible):** live retina Eden ran 50-56 FPS with periodic
hard hitches. Headless never reproduced them across 4,800 frames.

**Method:** stall-frame attribution (phase split printed at the stall
site) + a new physics-spike detector, then a live session.

**Measurement:** 17 stalls in ~1,000 live frames. Correlation was
total: **8 delete-flushes, 8 stalls**, and the pre-existing
[STALL-BREAKDOWN] probe already split it — GPU wait 61-83 ms, workers
0.00, delete 0.5. Deleting 750 particles cost the same as 75: a fixed
cost, not per-particle work.

**Root cause:** `GPURasterizer::wait_for_completion()` carried a
hardcoded `usleep(50000)` labelled "waiting for drawable pool to
drain" — a guard for `CAMetalLayer.drawableSize` changes. No caller of
that function changes the drawable: the resolution path holds
`acquire_all_slots()` and calls `force_drawable_resize()`, which owns
its own stabilization wait. The sleep taxed only the OTHER callers —
deletion flush (every chunk unload), shutdown, scene reset, and every
serialized headless oracle frame.

**Fix + result:** sleep removed; per-call checkpoint logging in that
path verbose-gated. Drain primitive 51.04 -> 0.00 ms (guarded by
`test_gpu_wait_no_fixed_sleep`, written red-first: the 51 ms samples
were dead flat, the signature of a fixed sleep). Live flush stall
61-83 -> ~19 ms, which is one honest in-flight retina frame.

**Learnings:**
1. **A safety wait belongs at the site it protects, not in the shared
   primitive underneath it.** This one guarded a resize, sat in a
   generic drain, and was paid by chunk streaming forever after.
2. **Dead-flat timings are a fingerprint.** Five samples at 51.0 ms
   is not GPU work; variance would show. Read the distribution, not
   just the mean.
3. Headless could not see it because headless barely unloads chunks —
   "not reproducible headless" was a property of the workload, not
   evidence against the bug. Reproduce on the workload that hurts.
4. Collateral: the serialized MLP oracle paid this sleep per frame,
   so its per-phase timings were mostly measuring the sleep. They are
   honest now.

**Still open (not this fix):** the residual ~19 ms drain is real work
(waiting for in-flight frames before mutating particle indices).
Whether it can be dropped depends on proving in-flight GPU work never
references CPU particle memory — architectural, needs its own pass.

---

### J — Measurement kit replaces ad-hoc probing (2026-07-29/30)

Not an optimization item: the instrument the campaign should have had.
Full design, methodology and study journal live in
`docs/PERFORMANCE_RESEARCH.md` and `docs/todo_plans/PERF_RESEARCH_KIT.md`;
this entry exists so the ledger points at them.

**Why.** Nearly every error in this campaign was a MEASUREMENT failure:
a stale-metallib mirage produced a fake 15 ms win; two fp16 "fixes" were
falsified once oracle nondeterminism was found; thermal drift repeatedly
exceeded the effect under test; GPU stages were sampled 1-in-60 (n = 9
per run), which is the noise that made the SSDO denoise experiment
unreadable.

**What landed.**
- `logosphere::telemetry` — three tiers (counters / phase timers / deep
  probes). Replaces `MetricsCollector`, which hashed a `std::string` per
  phase per frame and ran regardless of the profiling flag.
- `docs/PERFORMANCE_RESEARCH.md` — the methodology every profiling
  decision here has cited since the initial commit but which was never
  tracked in git. Being uncitable is why probes drifted ad-hoc anyway.
- Metrics sink (`LOGOSPHERE_METRICS=path.jsonl`), one record per frame.
- `scripts/bench_sweep.py` / `bench_report.py` — parametric sweeps with
  interleaved ordering, cooldowns, provenance manifests, and the OVERLAP
  FACTOR reported explicitly.
- GPU stages now recorded EVERY frame: per-stage n goes 9 -> ~200.

**What it found (studies S1-S9, all detail in the kit journal).** Lights
are nearly free to at least 24. Particle cost is CPU render prep, not GPU
(GPU is ~11% of the frame at 4.5k particles). The CPU path is LINEAR in
SURFACES, not particles: an early "superlinear engine" reading was
falsified by counters and traced to sphere subdivision inflating
surfaces-per-particle. Two CPU optimizations followed (allocation removal,
geometry cache); both are correct and oracle-clean, neither bought
measurable frame time.

**Learnings:**
1. **An instrument needs provenance more than precision.** The first
   sweep silently produced ZERO GPU data because a binary was built
   before the instrumentation was wired. The metallib hash could not
   catch it; the manifest now hashes the binaries too.
2. **Instrumentation that only works in one run mode is a trap.** The
   frame boundary was first finalized in `present()`, which headless
   never calls, so the sink wrote an empty file: a headless measurement
   kit that could not measure headless.
3. **Timings cannot separate a superlinear algorithm from a superlinear
   workload.** Only counters can. Two studies drew an algorithmic
   conclusion the timing data did not support.
4. A vehicle's own diagnostics can invalidate its numbers: the
   multi-light vehicle serializes frames for oracle determinism, so it
   is valid for scaling SHAPE and invalid for absolute cost or overlap.
   Stated in the journal so nobody quotes its overlap factor.

---

## Cross-cutting learnings (campaign infrastructure)

1. **Pixel oracles demand proven determinism.** The 2000-particle
   multi-light config differs from ITSELF run-to-run (depth-tie races
   on the atomic depth buffer where densely packed faces project to
   equal depths). An A-vs-A control run is mandatory before
   attributing any diff to a change; oracle configs are the verified
   144-particle grid and the staircase-catcher scene. Engine property
   worth remembering: dense equal-depth geometry renders with
   pixel-level nondeterminism.
2. **A/B scripts restore flags by value-flip, never `git checkout`**
   over files carrying uncommitted flag definitions — the checkout
   deletes the definition the code still references and quietly breaks
   both build trees (bitten twice: items A and B scripts).
3. **Wrapper exit codes lie.** A script's exit status reflects its
   last command, not the campaign's health; every stage must be judged
   by its own output markers (BUILD_OK, suite lines, oracle verdicts).
   Also bitten by `docker info` succeeding while the daemon socket was
   dead — verify the operation, not the probe.
4. **Stale shader hazard** (from the audit, repeated here because it
   shapes every A/B): the runtime loads `build/default.metallib`
   relative to CWD, so Release binaries can run debug-tree shaders; a
   partial rebuild silently serves stale kernels. Rebuild
   `metal_shaders` in BOTH trees when kernels change, and treat
   too-good-to-be-true results as stale-shader suspects first.
5. **Negative results are campaign wins when they're cheap.** Two
   flags, two A/B sessions, two avenues permanently closed with
   numbers attached — nobody re-litigates Shared-vs-Private buffers or
   clear elimination without new evidence.
6. **A stage win is not a frame win, but NOT for the reason recorded
   here until 2026-08-01.** Four changes have cut real work and bought
   zero frame time (G4, the pow-to-squarings experiment, kit S8, kit
   S9). The explanation on this line used to be "stages overlap, so
   their costs are not real". Serializing every pass changed per-stage
   cost by under 1% (kit S13); the passes have data dependencies and
   already ran effectively serially, and stage overlap measures 1.01x.
   The stage numbers were right.

   The mechanism is CPU/GPU BALANCE (kit S12): retina runs CPU 19.74 ms
   against GPU 18.92 ms, so cutting either side gains at most 0.81 ms
   before the other becomes the wall. Windowed has 6.59 ms of GPU
   headroom and CPU wins pay in full. Measure both sides first; the
   only uncapped lever is work that cuts both at once.

7. **CORRECTED 2026-08-01: per-command-buffer GPU times ARE work. The
   sum was the problem.** This entry used to read "residency, not work",
   on the evidence that `GPUEndTime - GPUStartTime` summed to 21.4 ms
   inside a 16.0 ms frame. The impossibility was real; the diagnosis was
   not. A frame's GPU window routinely extends past that frame's own
   period, because the GPU runs about a frame behind the CPU, so
   consecutive windows OVERLAP and adding them counts the shared region
   twice. Summed that way the metric reported 122% of wall clock (kit
   S12).

   Occupancy is the UNION of the intervals, not their sum: 95.8% at
   retina, 52.0% windowed. `telemetry::GpuWindow` now publishes
   `start_s` / `end_s` so any consumer can compute it, and
   `tests/test_gpu_occupancy_sanity.cpp` fails if any occupancy figure
   exceeds 100%. Per-pass figures in this ledger are sound as ISOLATED
   costs (confirmed by serialization, S13); it is only their sum that
   was never GPU busy time.

   Still true: `ioreg` "Device Utilization %" is not GPU busy time. It
   read 100% while the hardware was at 5%.

8. **Command-buffer count is not a lever on this hardware.** Merging the
   JFA seed and its 6 propagation steps took a frame from 18 command
   buffers to 12 and moved the GPU span by +0.16 ms. Boundary cost is
   near zero. Do not retry submission-overhead theories without new
   evidence.

9. **The biggest win of the campaign was not an algorithm.** It was
   constant data rebuilt inside a per-element loop: ~5 million heap
   allocations per frame regenerating a unit icosphere that never
   changes (kit study S10, CPU render -35%). Before theorising about
   architecture, count what a single element allocates and ask what part
   of the work is identical for every element.

11. **Price it with the counters before building it.** Two candidate levers
    were closed in minutes with no code (kit S15). The shadow-to-render
    triangle ratio looked like 1.9x of free money; it is required input for a
    per-ray cull that already runs on the GPU. Analytic sphere shadows looked
    obviously worth doing; 196,596 shadow triangles is exactly 16,383 x 12, an
    exact multiple of a box, so every Eden shadow caster is a box and the change
    would buy zero there. A divisibility check settled a question that would
    otherwise have cost a full implementation to answer. Ask what the recorded
    counters already imply before writing anything.

12. **Read the code's own comments before re-investigating.** The shadow
    triangle ratio was already investigated, answered and documented at
    `render_pipeline.cpp:380`, including why build-time back-face culling is the
    wrong frame of reference and where the correct per-ray cull lives. A
    previous session had also already deleted the dead cross products left from
    the abandoned attempt. Grepping the area first would have skipped the whole
    exercise. This codebase writes down its RCAs at the site; use them.

10. **Look for locks before looking at arithmetic.** `prep_shadow_tris`
    is named after geometry and was 68% mutex: a `getEntityByRenderIndex()`
    call per particle, locking a `recursive_mutex` from 14 worker threads,
    at 275 ns a call against ~25 uncontended (kit study S11, frame -18%).
    Any per-element lock inside a parallel region is a serialization
    point, and a phase name tells you where code lives, not what it costs.

11. **Size the prize by deletion before designing the fix.** Replacing
    the suspect call with a constant took one build and one bench and
    said the ceiling was 4.5 ms. Only then was the real fix worth
    writing. Cheap, decisive, and it protects against spending a day on
    something worth 0.04 ms (which the same audit also found: the second
    back-face cull in `convert_surface_to_lit_triangles`).

12. **A clean A/B moves ONE phase.** If phases unrelated to the change
    also move, the experiment is confounded and the number is not yours
    to claim.
