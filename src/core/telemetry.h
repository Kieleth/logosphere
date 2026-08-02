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
    // Physics work counters. Added 2026-08-01: physics is O(n^1.38) and the
    // sole scaling limit (S18), and there was not one physics counter in the
    // engine. The render side's two biggest wins both came from COUNTING work
    // rather than timing it.
    PhysActiveBodies,     // bodies that reached contact detection: the denominator
    PhysBvhCandidates,    // broad-phase candidates returned, summed over bodies.
                          //   / PhysActiveBodies = mean neighbours. This is what
                          //   separates "more bodies" from "denser pile".
    PhysSolverRows,       // constraints entering the solve
    PhysSolverIterations, // iterations actually run (early stop can cut it short)
    // HOW THE SOLVE ENDED, and this is the decisive one. A sequential-impulse
    // (Gauss-Seidel) solver applies each constraint against velocities already
    // changed by the ones before it, so ORDER shapes the answer whenever the
    // solve stops short. If it exits converged, remaining impulses are
    // negligible and order is mostly a bit-pattern detail, which leaves
    // reordering, islands, SoA and parallelism on the table. If it exits
    // plateaued, the order is baked into the PHYSICS and every one of those is
    // closed. Counting the exits answers that without a reordering experiment,
    // which a settling pile would confound anyway: chaos amplifies any
    // perturbation, so positions diverge in both worlds and prove nothing.
    PhysSolveConverged,   // exited under ABSOLUTE_THRESHOLD: impulses negligible
    PhysSolvePlateaued,   // exited on "more iterations won't help"
    PhysSolveExhausted,   // ran the full iteration budget without either
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
    HandoffSurfaces,    //   of which: the surface deque copy
    HandoffParticles,   //   of which: the particle vector copy
    RenderSlotWait,     //   of which: blocked on the triple-buffer semaphore
    RenderSubmit,       // buffer upload and GPU dispatch
    // prepare_gpu_data and its steps. ON THE MAIN THREAD TODAY, and therefore
    // on the critical path: Optimizations::USE_ASYNC_GPU_PREP has been false
    // since 2026-04-04, when it was set false "(for testing)" during an
    // Eva-shadow investigation and never restored. While it stays false the
    // three phases above are always 0.00 because their code path never
    // executes, which is NOT evidence that the pipeline never stalls.
    RenderPrep,         // prepare_gpu_data as a whole
    PrepTriangles,      //   step 1: surfaces -> GPU lit triangles
    PrepShadowTris,     //   step 2: shadow triangles from particles
    PrepBVH,            //   step 3: shadow BVH build/refit (+ entity BVH)
    PrepBvhTriangle,    //     of which: the shadow TriangleBVH build/refit
    PrepBvhEntity,      //     of which: entity grouping + EntityBVH build/refit
    PrepLights,         //   step 4: light packing
    PrepTransforms,     //   steps 5-6: emissive map + particle transforms
    // NOT a step of RenderPrep, despite the name and the old indentation.
    // Binning runs later in render_particles, outside prepare_gpu_data
    // (153-956), so it is a SIBLING of RenderPrep. Adding it to the prep steps
    // overstates them by about 30% and makes the children exceed the parent,
    // which reads as an instrumentation bug and is not one.
    PrepBinning,        // tile binning: SIBLING of RenderPrep, not a step
    // Sub-phases inside PhysicsSystem::update. Added 2026-08-01 because the
    // falling-bodies ramp showed a physics STEP going 1.3 ms to 66 ms across
    // 16x the bodies (~O(n^1.4)) while render stayed flat at 2.7 us/body, and
    // `physics` as one number cannot say which stage is superlinear. Same
    // position `render` was in before it was split.
    //
    // These run inside the substep loop, so each accumulates N_SUBSTEPS times
    // per frame. Compare their RATIO, and read the per-step figures alongside
    // the step count: the per-frame physics number FALLS past the knee only
    // because fewer steps fit in a frame, not because physics got cheaper.
    PhysicsForces,      // apply_all_forces: gravity, broad phase, contact solve
    PhysicsAngularVel,  // integrate_angular_velocities
    PhysicsAngularLim,  // project_angular_limits (4 iterations)
    PhysicsIntegrate,   // integrate_positions
    PhysicsBoundary,    // enforce_turtle_boundary
    PhysicsRestState,   // update_rest_state, once per frame not per substep
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
// Solver residual: how far the constraint set is from SATISFIED.
// -----------------------------------------------------------------------------
// WHY THIS EXISTS, and why the existing counters could not answer it.
//
// `PhysSolveConverged` / `Plateaued` / `Exhausted` say how the iteration LOOP
// ended. The test behind them is a per-iteration DELTA impulse
// (physics_system_v4.cpp:2025): how much the last sweep still changed the
// answer. That is a measure of iteration PROGRESS, and it is the right measure
// for deciding when to stop iterating.
//
// It is the wrong measure for "is the answer any good", and the two come apart.
// The convergence ladder reports 120/120 converged on iteration 1 while a stack
// of 8 boxes rests 1.3 mm lower than a stack of 1: a real, steady, unreported
// constraint violation. A solve can stop changing while still being wrong.
//
// So this measures the CONSTRAINT itself, after the loop, in a read-only pass:
// for every row, how badly is it still violated?
//
//   contacts (unilateral, min_impulse == 0): violation = max(0, bias - v_rel)
//     the row can only push, so a separating pair is satisfied, not violated
//   gluons   (bilateral):                    violation = |v_rel - bias|
//     an equality constraint is violated in both directions
//
// Units are m/s: `v_rel` is a relative velocity and the Baumgarte `bias` is a
// penetration re-expressed as one. `max_bias` is carried alongside as the
// position-error proxy, since penetration is not stored on the row.
//
// WORST_A / WORST_B ARE THE POINT. `max_violation` is a max over rows, and this
// campaign has now twice been misled by a max whose owning element was never
// identified: one immovable corner pair pinned 14,000 bodies above the stopping
// threshold, and no aggregate could show it. When an aggregate is a max, the
// question is always "which element", so the element ships with the number.
//
// OFF BY DEFAULT. The extra pass costs about one solver iteration, which is not
// free now that a settled scene converges in one. Enable with
// LOGOSPHERE_PHYS_RESIDUAL=1 or set_residual_enabled(true) from a test.
// UNSOLVABLE ROWS ARE COUNTED, NOT SKIPPED. A row whose effective_mass is 0 has
// two immovable bodies: infinite effective mass, no finite impulse changes
// anything, so it can never be satisfied and its violation is not a solver
// failure. Folding those into a global max is what hid the S22 bug for a week,
// so they get their own count. Not hidden, not mixed. `rows_unsolvable` staying
// high is itself the finding: those rows are built and solved every substep for
// nothing (~26 per floor tile).
//
// Angular rows are excluded from `max_violation`: their v_rel is in rad/s and
// averaging it with m/s would produce a number in no units at all.
// TWO RESIDUALS, BECAUSE THERE ARE TWO CONSTRAINTS AND THEY DISAGREE.
//
// Velocity: how far v_rel is from the target the row asked for. This is what
// the solver iterates on and it is usually ZERO, including for bodies visibly
// buried in each other. That is not a bug in the measurement. Baumgarte turns a
// position error into a velocity target, so hitting the target exactly is what
// success looks like at the velocity level regardless of how much overlap
// remains. Measured: two boxes spawned 40% overlapped, velocity residual 0.
//
// Penetration: how far the bodies actually overlap, in metres. This is the one
// a person can see, and the only one that answers "did the solve leave the
// world in a legal state". Sourced from the row's stored `penetration`, never
// inverted from bias, which is clamped and slop-zeroed.
//
// Report both. The velocity residual answers "did the solver do its job"; the
// penetration residual answers "was its job the right job".
struct SolveResidual {
    double   max_violation;   // worst unsatisfied SOLVABLE row, m/s
    double   rms_violation;   // root mean square across solvable rows
    double   max_penetration; // deepest overlap left standing, metres
    double   rms_penetration; // root mean square across overlapping rows
    double   max_bias;        // largest Baumgarte bias, m/s
    uint32_t rows;            // solvable linear rows measured
    uint32_t rows_violating;  // of those, how many over kViolationEps
    uint32_t rows_penetrating;// of those, how many over kPenetrationEps
    uint32_t rows_unsolvable; // effective_mass == 0: both bodies immovable
    uint32_t worst_a;         // the pair that produced max_penetration
    uint32_t worst_b;
};

// 1 mm/s. Below this a row is doing nothing a body could be seen to do.
constexpr double kViolationEps = 1e-3;
// 1 mm of overlap. SLOP is 1 mm, so anything at or under this is inside the
// band the solver deliberately ignores and is not a violation.
constexpr double kPenetrationEps = 1e-3;

void          set_residual_enabled(bool on);
bool          residual_enabled();
void          record_solve_residual(const SolveResidual& r);
SolveResidual solve_residual();   // most recent solve

// EXPERIMENT LEVER, not a setting. Non-zero permutes constraint order before
// each solve, deterministically on the seed, so the same seed reproduces
// exactly and an A-vs-A control is possible. 0 (default) does not shuffle and
// does not touch the array.
//
// It exists to answer one question: sequential impulse applies each row against
// velocities the earlier rows already changed, so order is part of the
// COMPUTATION, but is it part of the ANSWER? S19 assumed yes and closed
// parallelism, islands, SoA and sorting on it. Pair this with the residual and
// measure instead of assuming. LOGOSPHERE_PHYS_SHUFFLE=<seed>.
void     set_constraint_shuffle_seed(uint32_t seed);
uint32_t constraint_shuffle_seed();

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
