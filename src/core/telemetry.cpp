#include "telemetry.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace logosphere {
namespace telemetry {

namespace {

using Clock = std::chrono::high_resolution_clock;

// Per-thread counter accumulation: hot paths never touch a shared line.
// Registered on first use so frame_end can sum across threads.
struct ThreadCounters {
    uint64_t values[kCounterCount] = {};
    ThreadCounters();
    ~ThreadCounters();
};

std::mutex                  g_threads_mutex;
std::vector<ThreadCounters*> g_threads;
// Counts from threads that exited before the frame boundary. Worker pools
// here spawn per frame (collect_surfaces uses std::thread), so a thread's
// thread_local counters are destroyed BEFORE frame_end() can merge them.
// Without this, every counter incremented off the main thread silently read
// zero — which is exactly how the surface-cache study first looked like a
// 0% hit rate.
uint64_t g_orphaned[kCounterCount] = {};

ThreadCounters::ThreadCounters() {
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    g_threads.push_back(this);
}
ThreadCounters::~ThreadCounters() {
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    for (size_t i = 0; i < kCounterCount; ++i) g_orphaned[i] += values[i];
    for (size_t i = 0; i < g_threads.size(); ++i) {
        if (g_threads[i] == this) { g_threads.erase(g_threads.begin() + i); break; }
    }
}

thread_local ThreadCounters t_counters;

// Frame state. Phases are main-thread only (frame granularity by contract).
Clock::time_point g_phase_start[kPhaseCount];
double            g_phase_ms[kPhaseCount]       = {};
double            g_phase_ms_published[kPhaseCount] = {};
uint64_t          g_counters_published[kCounterCount] = {};

// GPU stages land from Metal completion handlers on other threads.
std::mutex g_gpu_mutex;
double     g_gpu_ms[kGpuStageCount] = {};
uint64_t   g_gpu_frame[kGpuStageCount] = {};

uint64_t g_frame_index = 0;
uint32_t g_particles = 0, g_lights = 0, g_triangles = 0;

// Sink
FILE*    g_sink = nullptr;
uint32_t g_sink_every = 1;
std::mutex g_sink_mutex;

}  // namespace

namespace detail {
std::atomic<bool> g_enabled{false};
}

void set_enabled(bool on) {
    detail::g_enabled.store(on, std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------

const char* name_of(Counter c) {
    switch (c) {
        case Counter::ShadowRay:        return "shadow_ray";
        case Counter::ShadowRayBlocked: return "shadow_ray_blocked";
        case Counter::ShadowRayClear:   return "shadow_ray_clear";
        case Counter::BvhNode:          return "bvh_node";
        case Counter::BvhRay:           return "bvh_ray";
        case Counter::LightingCall:     return "lighting_call";
        case Counter::ParticlesAdded:   return "particles_added";
        case Counter::ParticlesDeleted: return "particles_deleted";
        case Counter::ChunkLoad:        return "chunk_load";
        case Counter::ChunkUnload:      return "chunk_unload";
        case Counter::DeletionFlush:    return "deletion_flush";
        case Counter::FrameSkip:        return "frame_skip";
        case Counter::ParticlesVisible:      return "particles_visible";
        case Counter::SurfacesCollected:     return "surfaces_collected";
        case Counter::SurfacesCulled:        return "surfaces_culled";
        case Counter::SurfacesDrawn:         return "surfaces_drawn";
        case Counter::TrianglesBuilt:        return "triangles_built";
        case Counter::ShadowTrianglesBuilt:  return "shadow_triangles_built";
        case Counter::SurfaceCacheHit:       return "surface_cache_hit";
        default:                        return "?";
    }
}

const char* name_of(Phase p) {
    switch (p) {
        case Phase::Frame:      return "frame";
        case Phase::Update:     return "update";
        case Phase::Poll:       return "poll";
        case Phase::Movement:   return "movement";
        case Phase::Input:      return "input";
        case Phase::Physics:    return "physics";
        case Phase::Dynamics:   return "dynamics";
        case Phase::Worldgen:   return "worldgen";
        case Phase::UI:         return "ui";
        case Phase::Render:     return "render";
        case Phase::RenderClear: return "render_clear";
        case Phase::RenderParticles: return "render_particles";
        case Phase::RenderCollect:   return "render_collect";
        case Phase::RenderCull:      return "render_cull";
        case Phase::RenderDistances: return "render_distances";
        case Phase::RenderSort:      return "render_sort";
        case Phase::RenderPrepWait:  return "render_prep_wait";
        case Phase::RenderHandoff:   return "render_handoff";
        case Phase::HandoffSurfaces: return "handoff_surfaces";
        case Phase::HandoffParticles:return "handoff_particles";
        case Phase::RenderSlotWait:  return "render_slot_wait";
        case Phase::RenderSubmit:    return "render_submit";
        case Phase::RenderPrep:      return "render_prep";
        case Phase::PrepTriangles:   return "prep_triangles";
        case Phase::PrepShadowTris:  return "prep_shadow_tris";
        case Phase::PrepBVH:         return "prep_bvh";
        case Phase::PrepBvhTriangle: return "prep_bvh_triangle";
        case Phase::PrepBvhEntity:   return "prep_bvh_entity";
        case Phase::PrepLights:      return "prep_lights";
        case Phase::PrepTransforms:  return "prep_transforms";
        case Phase::PrepBinning:     return "prep_binning";
        case Phase::PhysicsForces:    return "phys_forces";
        case Phase::PhysicsAngularVel:return "phys_angular_vel";
        case Phase::PhysicsAngularLim:return "phys_angular_lim";
        case Phase::PhysicsIntegrate: return "phys_integrate";
        case Phase::PhysicsBoundary:  return "phys_boundary";
        case Phase::PhysicsRestState: return "phys_rest_state";
        case Phase::Present:    return "present";
        default:                return "?";
    }
}

const char* name_of(GpuStage s) {
    switch (s) {
        case GpuStage::Pass1GBuffer:        return "pass1_gbuffer";
        case GpuStage::Pass2ShadowRT:       return "pass2_shadow_rt";
        case GpuStage::Pass25JfaSeed:       return "pass25_jfa_seed";
        case GpuStage::Pass25JfaPropagate:  return "pass25_jfa_propagate";
        case GpuStage::Pass25PenumbraBlurH: return "pass25_blur_h";
        case GpuStage::Pass25PenumbraBlurV: return "pass25_blur_v";
        case GpuStage::Pass25bDdgiTrace:    return "pass25b_ddgi_trace";
        case GpuStage::Pass25cDdgiUpdate:   return "pass25c_ddgi_update";
        case GpuStage::Pass27Ssdo:          return "pass27_ssdo";
        case GpuStage::Pass28SsdoDenoise:   return "pass28_ssdo_denoise";
        case GpuStage::Pass3Apply:          return "pass3_apply";
        case GpuStage::AccelBuild:          return "accel_build";
        case GpuStage::AccelRefit:          return "accel_refit";
        default:                            return "?";
    }
}

// -----------------------------------------------------------------------------

void add_counter(Counter c, uint64_t n) {
    t_counters.values[static_cast<size_t>(c)] += n;
}

void phase_begin(Phase p) {
    g_phase_start[static_cast<size_t>(p)] = Clock::now();
}

void phase_end(Phase p) {
    const size_t i = static_cast<size_t>(p);
    g_phase_ms[i] += std::chrono::duration<double, std::milli>(
        Clock::now() - g_phase_start[i]).count();
}

namespace {
// Ring of in-flight frames. Handlers land out of order and up to a couple of
// frames late, so a slot is only read once two newer frames have opened.
constexpr size_t kWinSlots = 8;
struct WinSlot {
    uint64_t frame = UINT64_MAX;
    double   min_start = 0.0, max_end = 0.0, busy_s = 0.0;
    uint32_t buffers = 0;
};
WinSlot   g_win[kWinSlots];
GpuWindow g_win_published{0.0, 0.0, 0};
}  // namespace

void record_gpu_window(uint64_t frame_idx, double start_s, double end_s) {
    if (end_s <= start_s) return;   // Metal reports 0 for a buffer that never ran
    std::lock_guard<std::mutex> lock(g_gpu_mutex);
    WinSlot& s = g_win[frame_idx % kWinSlots];
    if (s.frame != frame_idx) { s = WinSlot{}; s.frame = frame_idx;
                                s.min_start = start_s; s.max_end = end_s; }
    if (start_s < s.min_start) s.min_start = start_s;
    if (end_s   > s.max_end)   s.max_end   = end_s;
    s.busy_s += (end_s - start_s);
    s.buffers++;
}

// Publish the frame that is two behind: every handler for it has fired.
void publish_gpu_window(uint64_t current_frame) {
    if (current_frame < 2) return;
    std::lock_guard<std::mutex> lock(g_gpu_mutex);
    const WinSlot& s = g_win[(current_frame - 2) % kWinSlots];
    if (s.frame != current_frame - 2 || s.buffers == 0) return;
    g_win_published = { s.busy_s * 1000.0,
                        (s.max_end - s.min_start) * 1000.0,
                        s.min_start, s.max_end,
                        s.buffers };
}

GpuWindow gpu_window() {
    std::lock_guard<std::mutex> lock(g_gpu_mutex);
    return g_win_published;
}

void record_gpu_stage(GpuStage stage, double ms, uint64_t frame_idx) {
    // Completion handlers fire on Metal's threads, after the frame's CPU
    // record. Correlate by frame index, never by arrival order.
    std::lock_guard<std::mutex> lock(g_gpu_mutex);
    const size_t i = static_cast<size_t>(stage);
    // Accumulate within a frame: multi-dispatch stages (JFA propagation, the
    // SSDO denoise loop) submit several command buffers per frame and the
    // stage cost is their sum, not the last one. Single-dispatch stages are
    // unaffected because their frame index never repeats.
    if (g_gpu_frame[i] == frame_idx) {
        g_gpu_ms[i] += ms;
    } else {
        g_gpu_ms[i]    = ms;
        g_gpu_frame[i] = frame_idx;
    }
}

void set_particle_count(uint32_t n) { g_particles = n; }
void set_light_count(uint32_t n)    { g_lights = n; }
void set_triangle_count(uint32_t n) { g_triangles = n; }

void get_scene_counts(uint32_t& particles, uint32_t& lights, uint32_t& triangles) {
    particles = g_particles; lights = g_lights; triangles = g_triangles;
}

uint64_t frame_index() { return g_frame_index; }

double phase_ms(Phase p) { return g_phase_ms_published[static_cast<size_t>(p)]; }
uint64_t counter_value(Counter c) { return g_counters_published[static_cast<size_t>(c)]; }

double gpu_stage_ms(GpuStage s) {
    std::lock_guard<std::mutex> lock(g_gpu_mutex);
    return g_gpu_ms[static_cast<size_t>(s)];
}

// -----------------------------------------------------------------------------

void frame_begin(uint64_t frame_idx) {
    g_frame_index = frame_idx;
    for (size_t i = 0; i < kPhaseCount; ++i) g_phase_ms[i] = 0.0;
}

namespace {

void write_sink_record() {
    // Buffered stdio, one line, off the hot path (frame end). Never called
    // with the render thread holding a GPU lock.
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    if (!g_sink) return;

    fprintf(g_sink, "{\"frame\":%llu", (unsigned long long)g_frame_index);
    fprintf(g_sink, ",\"particles\":%u,\"lights\":%u,\"triangles\":%u",
            g_particles, g_lights, g_triangles);

    fprintf(g_sink, ",\"phase_ms\":{");
    for (size_t i = 0; i < kPhaseCount; ++i) {
        fprintf(g_sink, "%s\"%s\":%.4f", i ? "," : "",
                name_of(static_cast<Phase>(i)), g_phase_ms_published[i]);
    }
    fprintf(g_sink, "}");

    fprintf(g_sink, ",\"counters\":{");
    for (size_t i = 0; i < kCounterCount; ++i) {
        fprintf(g_sink, "%s\"%s\":%llu", i ? "," : "",
                name_of(static_cast<Counter>(i)),
                (unsigned long long)g_counters_published[i]);
    }
    fprintf(g_sink, "}");

    {
        std::lock_guard<std::mutex> glock(g_gpu_mutex);
        fprintf(g_sink, ",\"gpu_ms\":{");
        bool first = true;
        for (size_t i = 0; i < kGpuStageCount; ++i) {
            // Only emit stages with a sample; include the frame they came
            // from so async arrival is auditable rather than assumed.
            if (g_gpu_frame[i] == 0 && g_gpu_ms[i] == 0.0) continue;
            fprintf(g_sink, "%s\"%s\":{\"ms\":%.4f,\"frame\":%llu}",
                    first ? "" : ",", name_of(static_cast<GpuStage>(i)),
                    g_gpu_ms[i], (unsigned long long)g_gpu_frame[i]);
            first = false;
        }
        fprintf(g_sink, "}");
    }

    {
        // GPU busy vs span for THIS frame. The stage durations above are not
        // additive; these two are, but only WITHIN a frame. Across frames they
        // are not: consecutive windows overlap because the GPU runs about a
        // frame behind, so summing them exceeds wall clock (measured 111% at
        // retina). start_s/end_s are emitted so a consumer can union the
        // intervals and get real occupancy instead.
        GpuWindow w = g_win_published;
        fprintf(g_sink, ",\"gpu_window\":{\"busy_ms\":%.4f,\"span_ms\":%.4f,"
                        "\"start_s\":%.6f,\"end_s\":%.6f,\"buffers\":%u}",
                w.busy_ms, w.span_ms, w.start_s, w.end_s, w.buffers);
    }

    fprintf(g_sink, "}\n");
}

}  // namespace

void frame_end() {
    // Publish phases.
    for (size_t i = 0; i < kPhaseCount; ++i) g_phase_ms_published[i] = g_phase_ms[i];

    publish_gpu_window(g_frame_index);

    // Merge and reset per-thread counters.
    for (size_t i = 0; i < kCounterCount; ++i) g_counters_published[i] = 0;
    {
        std::lock_guard<std::mutex> lock(g_threads_mutex);
        for (ThreadCounters* tc : g_threads) {
            for (size_t i = 0; i < kCounterCount; ++i) {
                g_counters_published[i] += tc->values[i];
                tc->values[i] = 0;
            }
        }
        // Threads that finished mid-frame left their counts here.
        for (size_t i = 0; i < kCounterCount; ++i) {
            g_counters_published[i] += g_orphaned[i];
            g_orphaned[i] = 0;
        }
    }

    if (g_sink && (g_sink_every <= 1 || (g_frame_index % g_sink_every) == 0)) {
        write_sink_record();
    }
}

// -----------------------------------------------------------------------------

bool open_sink(const char* path, uint32_t every_n_frames) {
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    if (g_sink) { fclose(g_sink); g_sink = nullptr; }
    g_sink = fopen(path, "wb");
    if (!g_sink) return false;
    setvbuf(g_sink, nullptr, _IOFBF, 1 << 16);
    g_sink_every = every_n_frames ? every_n_frames : 1;
    return true;
}

void close_sink() {
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    if (g_sink) { fflush(g_sink); fclose(g_sink); g_sink = nullptr; }
}

bool sink_is_open() {
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    return g_sink != nullptr;
}

void init_from_env() {
    const char* path = std::getenv("LOGOSPHERE_METRICS");
    if (!path || !*path) return;   // no sink requested: no cost, no behavior change

    uint32_t every = 1;
    if (const char* e = std::getenv("LOGOSPHERE_METRICS_EVERY")) {
        long v = strtol(e, nullptr, 10);
        if (v > 0) every = (uint32_t)v;
    }
    if (open_sink(path, every)) {
        set_enabled(true);   // a sink implies tiers 1-2 on
        fprintf(stderr, "[TELEMETRY] sink=%s every=%u\n", path, every);
    } else {
        fprintf(stderr, "[TELEMETRY] failed to open sink '%s'\n", path);
    }
}

}  // namespace telemetry
}  // namespace logosphere
