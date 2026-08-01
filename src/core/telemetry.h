#pragma once

// =============================================================================
// TELEMETRY — tiered performance instrumentation
// =============================================================================
// Methodology and rationale: docs/PERFORMANCE_RESEARCH.md
// Harness, sweeps, study journal: the performance research notes
//
// THREE TIERS, each stating what it can and cannot answer:
//
//   Tier 1  COUNTERS      — increment into a fixed array, no clock, no
//                           allocation, no string keys. Hot paths welcome.
//   Tier 2  PHASE TIMERS  — two clock reads per frame-level phase. Tens per
//                           frame, never per-ray or per-pixel.
//   Tier 3  DEEP PROBES   — compile-time gated, OFF by default. Sampling
//                           inside hot paths, serialized GPU passes. Alters
//                           what it measures, by design.
//
// Tiers 1 and 2 stay COMPILED IN and are runtime-toggleable (default off).
// That is a deliberate amendment to the old "never profile in shipping
// builds" rule: if enabling measurement needs a rebuild, you can never
// measure the binary you ship, and build provenance has already produced
// false results in this project. The disabled cost is one relaxed atomic
// load, predicted not-taken — and it is verified, not assumed.
//
// WHY NOT MetricsCollector: it keyed phases by std::string into a hash map,
// so every phase of every frame paid a hash and a possible allocation, and
// its call sites ran regardless of the profiling flag. This replaces it.
//
// THREADING: counters accumulate per-thread and merge at frame end, so hot
// paths never touch a shared cache line. GPU stage timings arrive later,
// on Metal completion handlers, and are correlated BY FRAME INDEX rather
// than by arrival order.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <cstddef>

namespace logosphere {
namespace telemetry {

// -----------------------------------------------------------------------------
// Compile-time IDs. Array indices, never strings — adding one is free at
// runtime. Keep COUNT last.
// -----------------------------------------------------------------------------

enum class Counter : uint16_t {
    ShadowRay,           // shadow rays traced (CPU path)
    ShadowRayBlocked,
    ShadowRayClear,
    BvhNode,             // BVH nodes visited
    BvhRay,
    LightingCall,
    ParticlesAdded,
    ParticlesDeleted,
    ChunkLoad,
    ChunkUnload,
    DeletionFlush,
    FrameSkip,
    // Render-path work counters. Added 2026-07-30 to settle whether CPU
    // render is superlinear because the WORK grows superlinearly, or because
    // linear work is getting slower per item (cache / allocation). Counting
    // is the only way to tell those apart; timings cannot.
    ParticlesVisible,     // survived broad-phase (grid/entity) culling
    SurfacesCollected,    // surfaces emitted by collection
    SurfacesCulled,       // rejected by frustum/back-face culling
    SurfacesDrawn,        // survived to rasterization
    TrianglesBuilt,       // GPU lit triangles produced
    ShadowTrianglesBuilt,
    SurfaceCacheHit,      // particle geometry reused (did not move)
    COUNT
};

enum class Phase : uint16_t {
    Frame,               // whole frame (update + render + present)
    Update,
    Poll,
    Movement,
    Input,
    Physics,
    Dynamics,
    Worldgen,
    UI,
    Render,
    RenderClear,
    RenderParticles,
    // Sub-phases inside the CPU render path. Added 2026-07-30 because the
    // ramp studies showed CPU render growing ~n^1.7 with particle count and
    // "render" as one number cannot say which part is superlinear.
    RenderCollect,      // surface collection
    RenderCull,         // frustum / entity culling
    RenderDistances,    // per-surface camera distance
    RenderSort,         // depth sort
    // Main-thread work that had no name until 2026-07-31, when the Eden
    // split showed 2.6 of 4.5 ms of render_particles unaccounted for.
    RenderPrepWait,     // main thread blocked on the async prep worker
    RenderHandoff,      // surface + particle copies handed to that worker
    RenderSlotWait,     //   of which: blocked on the triple-buffer semaphore
    RenderSubmit,       // buffer upload and GPU dispatch
    RenderPrep,         // prepare_gpu_data as a whole (ASYNC THREAD)
    PrepTriangles,      //   step 1: surfaces -> GPU lit triangles
    PrepShadowTris,     //   step 2: shadow triangles from particles
    PrepBVH,            //   step 3: shadow BVH build/refit (+ entity BVH)
    PrepBvhTriangle,    //     of which: the shadow TriangleBVH build/refit
    PrepBvhEntity,      //     of which: entity grouping + EntityBVH build/refit
    PrepLights,         //   step 4: light packing
    PrepTransforms,     //   steps 5-6: emissive map + particle transforms
    PrepBinning,        //   tile binning
    Present,
    COUNT
};

// GPU stages are NOT phases: they are reported asynchronously from Metal
// completion handlers, they overlap each other, and their sum exceeds the
// frame. Never read a stage sum as a frame budget — see principle 5.
enum class GpuStage : uint16_t {
    Pass1GBuffer,
    Pass2ShadowRT,
    Pass25JfaSeed,
    Pass25JfaPropagate,
    Pass25PenumbraBlurH,
    Pass25PenumbraBlurV,
    Pass25bDdgiTrace,
    Pass25cDdgiUpdate,
    Pass27Ssdo,
    Pass28SsdoDenoise,
    Pass3Apply,
    AccelBuild,
    AccelRefit,
    COUNT
};

constexpr size_t kCounterCount  = static_cast<size_t>(Counter::COUNT);
constexpr size_t kPhaseCount    = static_cast<size_t>(Phase::COUNT);
constexpr size_t kGpuStageCount = static_cast<size_t>(GpuStage::COUNT);

const char* name_of(Counter c);
const char* name_of(Phase p);
const char* name_of(GpuStage s);

// -----------------------------------------------------------------------------
// Runtime gate. One relaxed atomic load on the hot path when compiled in.
// -----------------------------------------------------------------------------

namespace detail {
extern std::atomic<bool> g_enabled;
}

inline bool enabled() {
    return detail::g_enabled.load(std::memory_order_relaxed);
}

void set_enabled(bool on);

// -----------------------------------------------------------------------------
// Tier 1 — counters. Thread-local; merged at frame end.
// -----------------------------------------------------------------------------

void add_counter(Counter c, uint64_t n);
inline void count(Counter c) { if (enabled()) add_counter(c, 1); }
inline void count_n(Counter c, uint64_t n) { if (enabled()) add_counter(c, n); }

// -----------------------------------------------------------------------------
// Tier 2 — phase timers. Frame granularity only.
// -----------------------------------------------------------------------------

void phase_begin(Phase p);
void phase_end(Phase p);

// RAII. Cheaper to read than paired begin/end and cannot leak on early return.
class ScopedPhase {
public:
    explicit ScopedPhase(Phase p) : phase_(p), active_(enabled()) {
        if (active_) phase_begin(phase_);
    }
    ~ScopedPhase() { if (active_) phase_end(phase_); }
    ScopedPhase(const ScopedPhase&) = delete;
    ScopedPhase& operator=(const ScopedPhase&) = delete;
private:
    Phase phase_;
    bool  active_;
};

// -----------------------------------------------------------------------------
// GPU stages — recorded from completion handlers, correlated by frame index.
// -----------------------------------------------------------------------------

void record_gpu_stage(GpuStage stage, double ms, uint64_t frame_index);

// Every command buffer's GPU execution window, in seconds on Metal's clock,
// from its completion handler. Named stages cover only some buffers and their
// durations are NOT additive, so a stage sum is not GPU busy time. This is:
//   busy = sum(end - start)        real GPU work
//   span = max(end) - min(start)   wall time from first start to last end
//   span - busy                    GPU idle BETWEEN this frame's passes
// Recorded for whichever frame the buffer was created in; published two frames
// later, once every handler for that frame has certainly fired.
//
// THESE ARE PER-FRAME AND MUST NOT BE SUMMED ACROSS FRAMES to get occupancy.
// A frame's window routinely extends past that frame's own period: the GPU runs
// roughly a frame behind the CPU, so consecutive windows OVERLAP, and adding
// them counts the shared region twice. Doing it anyway produced 111% of wall
// clock on Eden at retina (2026-08-01), which reads as "GPU-bound" and is not a
// fact, only an artefact of the sum.
//
// For occupancy take the UNION of [start_s, end_s] across frames and divide by
// wall time. That is why the absolute bounds are published and not only the
// durations: without them the double counting is invisible to every consumer.
void record_gpu_window(uint64_t frame_index, double gpu_start_s, double gpu_end_s);
struct GpuWindow { double busy_ms, span_ms, start_s, end_s; uint32_t buffers; };
GpuWindow gpu_window();   // last frame with a complete set of handlers
void publish_gpu_window(uint64_t current_frame);  // called from frame_end

// -----------------------------------------------------------------------------
// Frame lifecycle + published snapshot
// -----------------------------------------------------------------------------

void frame_begin(uint64_t frame_index);
void frame_end();

uint64_t frame_index();

// Read-back for consumers (debug overlay, tests, the metrics sink).
double   phase_ms(Phase p);          // last completed frame
uint64_t counter_value(Counter c);   // last completed frame
double   gpu_stage_ms(GpuStage s);   // most recent sample for that stage

// Scene facts the sink records alongside timings.
// Each set by whichever system actually knows the number, rather than
// forcing one caller to invent values it cannot see.
void set_particle_count(uint32_t n);
void set_light_count(uint32_t n);
void set_triangle_count(uint32_t n);
void get_scene_counts(uint32_t& particles, uint32_t& lights, uint32_t& triangles);

// -----------------------------------------------------------------------------
// Metrics sink — one JSON record per frame, off the hot path.
// Opened from LOGOSPHERE_METRICS=<path.jsonl>; sampling from
// LOGOSPHERE_METRICS_EVERY (default 1). Absent env = no sink, no cost.
// -----------------------------------------------------------------------------

bool open_sink(const char* path, uint32_t every_n_frames);
void close_sink();
bool sink_is_open();

// Reads LOGOSPHERE_METRICS / LOGOSPHERE_METRICS_EVERY and opens the sink if
// requested. Enables tiers 1-2 automatically when a sink is opened.
void init_from_env();

}  // namespace telemetry
}  // namespace logosphere

// -----------------------------------------------------------------------------
// Macros. Tier 3 compiles to nothing unless LOGOSPHERE_DEEP_PROBES is defined,
// so deep probes cost literally zero in a normal build.
// -----------------------------------------------------------------------------

#define LOGO_COUNT(c)        ::logosphere::telemetry::count(c)
#define LOGO_COUNT_N(c, n)   ::logosphere::telemetry::count_n((c), (n))
#define LOGO_PHASE(p)        ::logosphere::telemetry::ScopedPhase \
                                 _logo_phase_##__LINE__(p)

#ifdef LOGOSPHERE_DEEP_PROBES
  #define LOGO_DEEP_PHASE(p) ::logosphere::telemetry::ScopedPhase \
                                 _logo_deep_##__LINE__(p)
  #define LOGO_DEEP_COUNT(c) ::logosphere::telemetry::count(c)
#else
  #define LOGO_DEEP_PHASE(p) ((void)0)
  #define LOGO_DEEP_COUNT(c) ((void)0)
#endif
