/**
 * ============================================================================
 * PHYSICS SYSTEM V4 - Sequential Impulse Constraint Solver
 * ============================================================================
 *
 * ONE SENTENCE: Iteratively nudge particles apart until nothing penetrates.
 *
 * THE ALGORITHM:
 *
 *   ┌─────────────────────────────────────────────────┐
 *   │ PHASE 1: Apply Gravity                          │
 *   │   For each particle: vz += -9.8 × dt            │
 *   └─────────────────────────────────────────────────┘
 *                           ↓
 *   ┌─────────────────────────────────────────────────┐
 *   │ PHASE 2: Predict Positions                      │
 *   │   For each particle: z_predicted = z + vz × dt  │
 *   └─────────────────────────────────────────────────┘
 *                           ↓
 *   ┌─────────────────────────────────────────────────┐
 *   │ PHASE 3: Find Overlaps                          │
 *   │   For each pair: if overlap → create constraint │
 *   └─────────────────────────────────────────────────┘
 *                           ↓
 *   ┌─────────────────────────────────────────────────┐
 *   │ PHASE 4: Solve Constraints (THE CORE)           │
 *   │                                                 │
 *   │   FOR iteration = 1 to 8:                       │
 *   │       FOR each constraint:                      │
 *   │           v_rel = vz[a] - vz[b]                 │
 *   │           if v_rel < 0:  # approaching          │
 *   │               impulse = -v_rel × effective_mass │
 *   │               apply_impulse(impulse)            │
 *   │                                                 │
 *   └─────────────────────────────────────────────────┘
 *                           ↓
 *   ┌─────────────────────────────────────────────────┐
 *   │ PHASE 5: Commit                                 │
 *   │   For each particle: z = z + vz × dt            │
 *   └─────────────────────────────────────────────────┘
 *
 * WHY THIS WORKS:
 *   V3 tried to solve everything in one pass with special ordering.
 *   That fails when gluons and contacts interact (Heavy-on-Light case).
 *
 *   V4 just iterates. Each pass improves the solution. After 8 iterations,
 *   even complex gluon-contact chains reach equilibrium.
 *
 * EXAMPLE (Heavy-on-Light, the V3 failure case):
 *
 *       [Heavy] 100kg
 *          |
 *        gluon
 *          |
 *       [Light] 2kg
 *          |
 *       contact
 *          |
 *       [Floor]
 *
 *   Iteration 1:
 *     - Light-Floor contact: Light stops (vz=0)
 *     - Heavy-Light gluon: Heavy stops, pulls Light down
 *
 *   Iteration 2:
 *     - Light-Floor contact: Push Light back up
 *     - Heavy-Light gluon: Already synchronized
 *
 *   Result: Equilibrium. Light doesn't fall through floor.
 *
 * REFERENCE: docs/PHYSICS_ITERATIVE_RESOLVER_V4.md
 * ============================================================================
 */

// ╔══════════════════════════════════════════════════════════════════════════╗
// ║                        ⚠️  CRITICAL WARNING  ⚠️                           ║
// ╠══════════════════════════════════════════════════════════════════════════╣
// ║  THE ONLY IMMOVABLE THING IS THE TURTLE BOUNDARY (world edge at z=0).    ║
// ║                                                                          ║
// ║  ALL particles can move:                                                 ║
// ║  - HEAVY_STATIC material = high density (10000 kg/m³), NOT immovable     ║
// ║  - is_at_rest flag = performance optimization, NOT immobility            ║
// ║  - solver_mode == KINEMATIC = external writer owns position, still in   ║
// ║    the contact pipeline; physics just skips integration + impulses      ║
// ║                                                                          ║
// ║  DO NOT add special cases to prevent particle movement based on:         ║
// ║  - Material type                                                         ║
// ║  - Mass thresholds                                                       ║
// ║  - Custom "anchored" flags                                               ║
// ║                                                                          ║
// ║  If something needs to be immovable, it must be handled by:              ║
// ║  - Turtle boundary enforcement (enforce_turtle_boundary)                 ║
// ║  - External constraints (gluons with anchor points)                      ║
// ║  - Kinematic solver mode (solver_mode = KINEMATIC)                      ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#include "logosphere/physics/physics_system.h"
#include "physics_trace.h"
#include "explosion_detector.h"
#include "logosphere/physics/physics_solver.h"
#include "logosphere/physics/narrow_phase.h"
#include "engine.h"
#include "logosphere/events/event_bus.h"
#include "../materials.h"
#include "../optimization_flags.h"
#include <iostream>
#include <vector>
#include <cmath>
#include "../math/quat.h"
#include <queue>
#include <climits>
#include <algorithm>
#include <chrono>
#include <unordered_set>

// ============================================================================
// THE ENERGY LEDGER (ENERGY_LEDGER=1)
// ============================================================================
// Energy is conserved; it is transformed, not created. So the invariant a
// physics engine must satisfy on an unforced system is not "kinetic energy
// must not rise" — a released spring is SUPPOSED to raise it, and so is a
// falling body. It is that the TOTAL may fall and never rise:
//
//     KE  +  gravitational PE  +  elastic strain stored in bonds
//
// with dissipation accounting for the fall.
//
// A sequential-impulse solver has no energy. It computes impulses that satisfy
// constraints and never asks what they cost, so a gluon stores a target
// distance and a stiffness rather than a joule. That is why every injection
// defect this session had to be found one scene at a time, backwards, by
// inference: nothing in the engine checked a conservation law. Four of those
// attributions were wrong.
//
// This measures the buckets between phase boundaries, so a rise is attributed
// to the phase that produced it rather than guessed at.
struct EnergyBuckets {
    double kinetic = 0.0, gravity = 0.0, strain = 0.0;
    double total() const { return kinetic + gravity + strain; }
};

static EnergyBuckets measure_energy(
        ParticleSystem::WriteView& particles,
        const std::deque<std::unique_ptr<GluonConstraintBase>>& gluons) {
    EnergyBuckets e;
    constexpr double G = 9.81;
    for (size_t i = 0; i < particles.size(); ++i) {
        const Particle& p = particles[i];
        const double m = p.GetMass();
        if (m <= 0.0) continue;
        if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;
        e.kinetic += 0.5 * m * (double(p.vx)*p.vx + double(p.vy)*p.vy + double(p.vz)*p.vz);
        e.gravity += m * G * double(p.z);
    }
    // Elastic strain: 1/2 k x^2, x measured where the bond actually acts —
    // between its ATTACHMENT points, not between centres. Getting that wrong
    // is what cost four wrong fixes on the tree (ledger entry G1c).
    for (const auto& g : gluons) {
        if (!g) continue;
        if (g->particle_a >= particles.size() || g->particle_b >= particles.size()) continue;
        const Particle& pa = particles[g->particle_a];
        const Particle& pb = particles[g->particle_b];
        auto attach = [](const Particle& p, const Vec3& o, bool rot,
                         float& wx, float& wy, float& wz) {
            if (!rot) { wx = p.x + o.x; wy = p.y + o.y; wz = p.z + o.z; return; }
            const float cx = cosf(p.rotation_x), sx = sinf(p.rotation_x);
            const float cy = cosf(p.rotation_y), sy = sinf(p.rotation_y);
            const float cz = cosf(p.rotation_z), sz = sinf(p.rotation_z);
            const float y1 = o.y * cx - o.z * sx;
            const float z1 = o.y * sx + o.z * cx;
            const float x2 = o.x * cy + z1 * sy;
            const float z2 = -o.x * sy + z1 * cy;
            wx = p.x + x2 * cz - y1 * sz;
            wy = p.y + x2 * sz + y1 * cz;
            wz = p.z + z2;
        };
        float ax, ay, az, bx, by, bz;
        attach(pa, g->offset_a, g->rotate_offsets, ax, ay, az);
        attach(pb, g->offset_b, g->rotate_offsets, bx, by, bz);
        const double dx = bx - ax, dy = by - ay, dz = bz - az;
        const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        const double x = dist - double(g->target_distance);
        e.strain += 0.5 * double(g->stiffness) * x * x;
    }
    return e;
}

// PER-ROW-TYPE ENERGY ATTRIBUTION. Applying impulse J along the jacobian to a
// pair changes their kinetic energy by exactly
//     dKE = J * v_rel + J^2 / (2 * m_eff)
// (expand 1/2 m (v+J/m)^2 for both bodies; the cross terms give J*v_rel and
// the squares give J^2/2 * (1/ma + 1/mb) = J^2/(2*m_eff)). Both quantities are
// already in scope where the impulse is applied, so this costs two multiplies
// per row and tells us WHICH KIND of constraint is pumping — the same split
// that turned d_integrate into d_positions and d_turtle and named the turtle
// in one run.
struct RowEnergy { double contact = 0.0, turtle = 0.0, gluon = 0.0; };
static RowEnergy g_row_energy;

static bool energy_ledger_on() {
    static const bool v = std::getenv("ENERGY_LEDGER") != nullptr;
    return v;
}


using namespace PhysicsV4;

// Forward declaration for wake propagation diagnostics
void log_wake_propagation_stats();

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

PhysicsSystem::PhysicsSystem()
    : engine_(nullptr)
    , particle_system_(nullptr)
    , is_initialized_(false)
    , next_gluon_id_(0)
{
}

PhysicsSystem::~PhysicsSystem() {
    shutdown();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool PhysicsSystem::initialize(ParticleSystem& particle_system, const PhysicsConfig& config) {
    particle_system_ = &particle_system;
    config_ = config;

    std::cout << "=== PHYSICS V4.1: Sequential Impulse Solver ===" << std::endl;
    std::cout << "Iterations: " << SOLVER_ITERATIONS << std::endl;
    std::cout << "Slop: " << SLOP << "m" << std::endl;
    std::cout << "Beta: " << BETA << std::endl;
    std::cout << "Friction: " << FRICTION_COEFFICIENT << std::endl;
    std::cout << "===============================================" << std::endl;

    is_initialized_ = true;
    return true;
}

bool PhysicsSystem::initialize(Engine* engine, const PhysicsConfig& config) {
    if (!engine) {
        std::cerr << "[PhysicsV4] ERROR: Null engine pointer" << std::endl;
        return false;
    }
    engine_ = engine;
    return initialize(engine->get_particle_system(), config);
}

void PhysicsSystem::shutdown() {
    if (!is_initialized_) return;

    std::cout << "[PhysicsV4] Shutting down..." << std::endl;

    engine_ = nullptr;
    particle_system_ = nullptr;
    is_initialized_ = false;
}

// ============================================================================
// MAIN UPDATE LOOP
// ============================================================================

void PhysicsSystem::update(double delta_time, int input_target_id) {
    if (!is_initialized_) return;

    float dt = static_cast<float>(delta_time);

    // Get exclusive write access to particles
    auto particles = particle_system_->lock_particles_for_write();

    // ==========================================================================
    // CENTRALIZED GLUON VALIDATION
    // ==========================================================================
    // Remove gluons with stale particle indices BEFORE any physics processing.
    // This logs diagnostic info to help find the root cause of stale indices.
    prune_invalid_gluons(particles.size());

    // ==========================================================================
    // SAVE PRE-CONSTRAINT OMEGA_Z
    // ==========================================================================
    // Angular constraints propagate omega_z to all connected particles.
    // To identify which particle is being externally controlled (e.g., by look-at
    // or walking), we save omega_z BEFORE the solver runs.
    // External control sets omega_z directly; solver then propagates it.
    // In position projection, the particle with higher pre-constraint omega_z
    // is the one being controlled → it becomes the anchor.
    pre_constraint_omega_z_.resize(particles.size());
    for (size_t i = 0; i < particles.size(); ++i) {
        pre_constraint_omega_z_[i] = particles[i].omega_z;
    }

    // ==========================================================================
    // V4.2 PHYSICS PIPELINE - Integrated Linear + Angular Solving
    // ==========================================================================
    //
    // KEY INSIGHT: Angular and linear constraints must be coupled because:
    //   - Position projection uses rotation_z to compute rotated attachment points
    //   - If rotation changes AFTER position projection, positions become stale
    //   - This causes "door hinge" effect where particles rotate around edge
    //
    // SOLUTION: Integrate angular velocity BEFORE position projection so
    // rotation_z is current when computing attachment point positions.
    //
    // ORDER (per substep):
    //   1. Solve velocity constraints (linear + angular together)
    //   2. Integrate angular: omega_z → rotation_z (rotation is now current)
    //   3. Project angular limits (hard bounds on rotation)
    //   4. Project gluon positions (uses updated rotation_z)
    //   5. Integrate linear: velocity → position
    //   6. Enforce turtle boundary
    //
    // V4.11 (2026-05): Outer substep loop. Pipeline runs N substeps
    // per frame with dt/N each. Position-bias gluon corrections
    // accumulate across substeps, giving the solver enough authority
    // to keep up with 33 mm/frame hip motion without cranking BETA
    // (April rehab probes proved BETA can't be tuned past 1.0
    // without chain blowup). At-rest state update runs once at the
    // end after every substep has fed back into final velocities.
    // ==========================================================================

    constexpr int N_SUBSTEPS = PhysicsV4::SOLVER_SUBSTEPS;
    const float sub_dt = dt / static_cast<float>(N_SUBSTEPS);

    // Interaction model: filtered broad-phase overlaps accumulate across
    // substeps and are consumed once per frame (episode events, volume
    // forces) — clear at frame entry, not per substep.
    filtered_overlaps_.clear();

    for (int substep = 0; substep < N_SUBSTEPS; ++substep) {
        ::logosphere::phystrace::set_substep(substep);
        // PHASE 1-4: Apply gravity, predict, detect, solve constraints
        // V4.2: Angular constraints now solved alongside linear in same iteration loop
        EnergyBuckets e_before, e_after_solve, e_after_angular, e_after_integrate;
        const bool ledger = energy_ledger_on();
        if (ledger) { e_before = measure_energy(particles, gluon_constraints_v2_);
                      g_row_energy = RowEnergy{}; }

        {
            ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsForces);
            apply_all_forces(particles, sub_dt);
        }
        if (ledger) e_after_solve = measure_energy(particles, gluon_constraints_v2_);

        // PHASE 4.5: Integrate angular velocity → rotation
        // This MUST happen BEFORE position projection so rotation_z is current
        {
            ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsAngularVel);
            integrate_angular_velocities(particles, sub_dt);
        }
        if (ledger) e_after_angular = measure_energy(particles, gluon_constraints_v2_);

        // PHASE 4.6: Project angular limits (hard bounds)
        // Multiple iterations to propagate through gluon chains
        {
            ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsAngularLim);
            for (int iter = 0; iter < 4; ++iter) {
                project_angular_limits(particles);
            }
        }

        // PHASE 4.7: Project gluon positions (OLD METHOD - disabled by default)
        // Now uses CURRENT rotation_z (updated in 4.5) for attachment point calculation
        // This fixes the "door hinge" problem where rotation changed after position
        //
        // 2025-12-17: Disabled in favor of unified Jacobian solver with position bias.
        // Position projection fights with contact solver → oscillation.
        // New: gluon constraints have position bias, solved together with contacts.
        if constexpr (PhysicsV4::ENABLE_GLUON_POSITION_PROJECTION) {
            project_gluon_positions(particles);
        }

        // PHASE 5: Integrate linear velocity → position
        {
            ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsIntegrate);
            integrate_positions(particles, sub_dt);
        }
        EnergyBuckets e_after_positions;
        if (ledger) e_after_positions = measure_energy(particles, gluon_constraints_v2_);

        // PHASE 6: Enforce turtle boundary (absolute - nothing goes below z=0)
        {
            ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsBoundary);
            enforce_turtle_boundary(particles);
        }

        if (ledger) {
            e_after_integrate = measure_energy(particles, gluon_constraints_v2_);
            static int led_n = 0;
            // Gravity does real work during integration, so that phase is
            // EXPECTED to trade PE for KE and its total should still not rise.
            const double d_solve = e_after_solve.total()     - e_before.total();
            const double d_ang   = e_after_angular.total()   - e_after_solve.total();
            // Split the integrate window: moving bodies by their velocity is
            // one thing, and pushing them out of the floor is another. Both
            // live between the same two probes, and only one of them can
            // create energy without paying for it.
            const double d_pos   = e_after_positions.total()  - e_after_angular.total();
            const double d_turtle= e_after_integrate.total()  - e_after_positions.total();
            const double d_int   = e_after_integrate.total() - e_after_angular.total();
            const double d_all   = e_after_integrate.total() - e_before.total();
            if ((led_n++ % 8) == 0 || d_all > 1.0) {
                std::cout << "[ENERGY] sub=" << substep
                          << " KE=" << e_after_integrate.kinetic
                          << " PE=" << e_after_integrate.gravity
                          << " strain=" << e_after_integrate.strain
                          << " | d_solve=" << d_solve
                          << " d_angular=" << d_ang
                          << " d_positions=" << d_pos
                          << " d_turtle=" << d_turtle
                          << " | rows: contact=" << g_row_energy.contact
                          << " turtleRow=" << g_row_energy.turtle
                          << " gluon=" << g_row_energy.gluon
                          << " | d_TOTAL=" << d_all
                          << (d_all > 1.0 ? "   *** ENERGY CREATED ***" : "")
                          << std::endl;
            }
        }
    }

    // PHASE 7: Update at-rest state once per frame, after all substeps
    // have settled into the final velocities.
    {
        ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsRestState);
        update_rest_state(particles);
    }

    // Cleanup broken gluons (and wake what they freed)
    remove_marked_gluons(&particles);

    // PHASE 8: EXPLOSION DETECTOR (issue #42). One pass over final velocities,
    // always on. Two world-scale explosions were found by a human looking at a
    // screen while every number needed to raise the alarm sat in this array;
    // one of them no longer reproduces on demand, which is exactly the kind of
    // intermittent a permanent tripwire exists for. Cost is ~tens of
    // microseconds at 19k bodies, below the kit's measured noise floor.
    if (::logosphere::expdet::enabled()) {
        ::logosphere::expdet::begin_frame();
        for (size_t i = 0; i < particles.size(); ++i) {
            const Particle& p = particles[i];
            if (p.is_light_source) continue;   // lights float by design
            ::logosphere::expdet::sample((int)i, p.vx, p.vy, p.vz,
                                         p.GetMass(), p.x, p.y, p.z);
        }
        ::logosphere::expdet::end_frame();
    }
}

// ============================================================================
// PHASE 1: APPLY GRAVITY
// ============================================================================
/**
 * Apply gravity to all dynamic particles.
 *
 * EXAMPLE:
 *   Particle: mass = 10kg
 *   vz before: 0 m/s
 *   dt: 0.0167s
 *
 *   vz after = 0 + (-9.8) × 0.0167 = -0.1634 m/s
 */
void PhysicsSystem::apply_all_forces(ParticleSystem::WriteView& particles, float dt) {
    const size_t count = particles.size();

    // Phase 1: Apply gravity to dynamic, non-resting particles
    // V4.4: Skip at-rest particles. Contact constraints are also skipped for at-rest
    // particles, so gravity must be skipped too or particle will wake every frame.
    // When a particle wakes (external collision), it will get gravity again.
    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];
        // Skip massless particles (lights, celestial) - they float
        if (p.GetMass() == 0.0f) continue;
        // Skip DYNAMICS-owned - dynamics system applies entity-level gravity
        if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;
        // Skip at-rest particles - they're in equilibrium
        if (p.is_at_rest) continue;
        // ANIMATION-owned quaternion-driven particles are the animation
        // layer's gluon PD bones: gravity on them would droop a 7-hop
        // chain visibly, and the drive is the animation system's "this
        // joint holds its commanded pose" statement — let it hold.
        // PHYSICS-owned quat-driven bodies (organic chains, debris) are
        // real bodies and MUST weigh: the blanket exemption made a freed
        // segment float forever on its last velocity — no bond, no
        // contact, no gravity (the rung-4 ghost on 1656 mm of air).
        // Only PHYSICS-owned quat bodies weigh (organic blades, debris).
        // ANIMATION- and DYNAMICS-owned bones are the animation layer's:
        // per-bone gravity makes the drive fight it on every joint (the
        // original exemption's rationale) — and giving DYNAMICS bones
        // weight put visible churn into the human's limbs.
        if (p.is_quat_driven && p.owner != ParticleOwner::PHYSICS) continue;
        // Apply gravity: v += g × dt
        p.vz += -GRAVITY * dt;
    }

    // Phase 2-4: Detect contacts, build constraints, solve
    solve_contacts_v3(particles, dt);
}

// IMPULSE_MEMORY_OFF=1 disables the bond integral term (warm_ix/iy/iz) at both
// its apply and its store-back. The other two suspects in the light-body
// ringing hunt (anchor torque, sleep law) already had env levers and both
// measured identical to baseline; this one had none, which is the only reason
// it stayed a hypothesis instead of an answer. Diagnostic only, default OFF.
static bool impulse_memory_off() {
    static const bool v = std::getenv("IMPULSE_MEMORY_OFF") != nullptr;
    return v;
}

// ============================================================================
// PHASE 2-4: CONTACT DETECTION AND CONSTRAINT SOLVING
// ============================================================================
/**
 * Detect overlapping particles, create constraints, solve iteratively.
 *
 * This is the CORE of V4. Instead of special ordering (V3), we iterate.
 */
void PhysicsSystem::solve_contacts_v3(ParticleSystem::WriteView& particles, float dt) {
    const size_t count = particles.size();
    // ========================================================================
    // V4.3 FIX: Changed from "count < 2" to "count < 1"
    // ========================================================================
    // The original check "count < 2" was an optimization for particle-particle
    // collision (need 2+ particles to collide). BUT Turtle collision is
    // particle-vs-world boundary, not particle-vs-particle.
    //
    // A single particle CAN collide with the Turtle! The early return was
    // causing single-particle scenarios to skip Turtle detection entirely,
    // resulting in particles falling through the world floor.
    //
    // Discovered via test_turtle_single_particle test.
    // ========================================================================
    if (count < 1) return;

    // Per-phase timing for RCA
    static int phys_frame = 0;
    phys_frame++;
    auto t_start = std::chrono::high_resolution_clock::now();

    // ========================================================================
    // CANARY_DEBUG: Track a single particle through all phases to find root cause
    // Run with: CANARY_PID=677 CANARY_FRAME_MIN=0 CANARY_FRAME_MAX=50 to track particle 677 from frame 0-50
    // ========================================================================
    static int canary_pid = -1;
    static int canary_frame_min = 0;
    static int canary_frame_max = 15;
    static bool canary_init = false;
    if (!canary_init) {
        canary_init = true;
        const char* env = std::getenv("CANARY_PID");
        if (env) canary_pid = std::atoi(env);
        const char* env_min = std::getenv("CANARY_FRAME_MIN");
        if (env_min) canary_frame_min = std::atoi(env_min);
        const char* env_max = std::getenv("CANARY_FRAME_MAX");
        if (env_max) canary_frame_max = std::atoi(env_max);
    }
    bool canary_active = (canary_pid >= 0 && canary_pid < (int)count &&
                          phys_frame >= canary_frame_min && phys_frame <= canary_frame_max);

    // Log canary state at start of frame (BEFORE any physics)
    if (canary_active) {
        const Particle& cp = particles[canary_pid];
        std::cout << "\n[CANARY F" << phys_frame << " START] P" << canary_pid
                  << " pos=(" << cp.x << "," << cp.y << "," << cp.z << ")"
                  << " vel=(" << cp.vx << "," << cp.vy << "," << cp.vz << ")"
                  << " at_rest=" << cp.is_at_rest << std::endl;
    }

    // Granular timing variables
    size_t debug_active_particles = 0;
    size_t debug_bvh_queries = 0;
    size_t debug_total_candidates = 0;

    // Clear active contacts from previous frame
    active_contacts_.clear();

    // Clear collision events from previous frame (for game systems)
    collision_events_.clear();

    // ========================================================================
    // PHASE 2: PREDICT POSITIONS
    // ========================================================================
    std::vector<float> z_predicted(count);
    for (size_t i = 0; i < count; ++i) {
        const Particle& p = particles[i];
        z_predicted[i] = p.z + p.vz * dt;
    }

    // ========================================================================
    // PHASE 3: DETECT CONTACTS AND BUILD CONSTRAINTS
    // ========================================================================
    /**
     * 3D AABB Collision Detection
     *
     * Two boxes overlap if they overlap on ALL THREE axes:
     *   X: [x - width/2, x + width/2]       (width = X span)
     *   Y: [y - height/2, y + height/2]     (height = Y span)
     *   Z: [z - thickness/2, z + thickness/2] (thickness = Z span)
     *
     * EXAMPLE:
     *   Trunk: center=(0,0,1.4), dims=(1.2, 1.2, 2.4)
     *     X range: [-0.6, 0.6]
     *     Y range: [-0.6, 0.6]
     *     Z range: [0.2, 2.6]
     *
     *   Floor tile: center=(0,0,0.1), dims=(2,2,0.2)
     *     X range: [-1, 1]
     *     Y range: [-1, 1]
     *     Z range: [0, 0.2]
     *
     *   Overlap check:
     *     X: [-0.6,0.6] ∩ [-1,1] = yes
     *     Y: [-0.6,0.6] ∩ [-1,1] = yes
     *     Z: [0.2,2.6] ∩ [0,0.2] = yes (touching at 0.2)
     *   → COLLISION
     */
    std::vector<Constraint> constraints;

    // ==========================================================================
    // FIX B: Build gluon pair set for O(1) lookup instead of O(g) scan
    // ==========================================================================
    // Hash function for pair<size_t, size_t> - order-independent
    auto pair_hash = [](const std::pair<size_t, size_t>& p) {
        size_t a = std::min(p.first, p.second);
        size_t b = std::max(p.first, p.second);
        return std::hash<size_t>()(a) ^ (std::hash<size_t>()(b) << 1);
    };
    auto pair_equal = [](const std::pair<size_t, size_t>& lhs, const std::pair<size_t, size_t>& rhs) {
        size_t la = std::min(lhs.first, lhs.second), lb = std::max(lhs.first, lhs.second);
        size_t ra = std::min(rhs.first, rhs.second), rb = std::max(rhs.first, rhs.second);
        return la == ra && lb == rb;
    };
    std::unordered_set<std::pair<size_t, size_t>, decltype(pair_hash), decltype(pair_equal)>
        gluon_pairs(gluon_constraints_v2_.size(), pair_hash, pair_equal);
    for (const auto& gluon : gluon_constraints_v2_) {
        gluon_pairs.insert({gluon->particle_a, gluon->particle_b});
    }
    auto t_after_gluon_hash = std::chrono::high_resolution_clock::now();

    // Use BVH broad-phase to reduce O(n²) to O(n log n)
    const BVH* bvh = particle_system_->get_shadow_bvh();
    std::vector<int> candidates;  // Reuse across iterations

    // ========================================================================
    // TURTLE COLLISION DETECTION (V4.3)
    // ========================================================================
    // The Turtle is an infinite-mass collision plane at z = TURTLE_Z.
    // Any particle whose bottom falls below TURTLE_Z gets a contact constraint.
    // This is the ONE exception to "everything is particles" - the world boundary.
    //
    // NOTE: No kinematic check - Turtle supports ALL particles including
    // what used to be kinematic floor tiles. We're removing kinematic entirely.
    // ========================================================================
    for (size_t i = 0; i < count; ++i) {
        const Particle& pi = particles[i];

        // Skip massless particles (lights, celestial) - they float
        if (pi.GetMass() == 0.0f) continue;

        // Skip particles at rest (optimization)
        if (pi.is_at_rest) continue;

        // Calculate particle bottom at predicted position
        // WARNING: This uses thickness as WORLD Z-extent, IGNORING rotation_x/y/z!
        // Particles must set width/height/thickness to match WORLD axes.
        // See physics_tree_generator.cpp "CRITICAL WARNING" for full explanation.
        float half_zi = pi.thickness * 0.5f;
        float particle_bottom = z_predicted[i] - half_zi;

        // V4.3 FIX: Create constraint for particles NEAR turtle, not just penetrating
        // This prevents "contact bouncing" where particles leave contact between frames.
        // Particles within 3mm of turtle surface maintain contact (prevents oscillation).
        constexpr float TURTLE_CONTACT_THRESHOLD = 0.003f;  // 3mm
        if (particle_bottom < TURTLE_Z + TURTLE_CONTACT_THRESHOLD) {
            // Particle penetrating the Turtle!
            float penetration = TURTLE_Z - particle_bottom;

            // Create Turtle contact constraint
            Constraint c;
            c.body_a = i;
            c.body_b = i;  // Turtle has no particle - self-reference as sentinel
            c.is_turtle_contact = true;  // Flag to skip body_b impulse

            // Jacobian: normal pointing UP from Turtle
            c.jx = 0.0f;
            c.jy = 0.0f;
            c.jz = 1.0f;

            // Effective mass: only particle's mass (Turtle has infinite mass)
            // effective_mass = 1 / (1/ma + 0) = ma
            c.effective_mass = pi.GetMass() > 0.0f ? pi.GetMass() : 1.0f;

            // Bias for position correction (Baumgarte)
            if (penetration > SLOP) {
                c.bias = BETA * (penetration - SLOP) / dt;
            } else {
                c.bias = 0.0f;
            }
            c.penetration = penetration;   // measurement, see the field's comment

            // Contact can only push up (non-negative impulse), and MECHANISM A
            // bounds it by capture: stop the approach plus the support cushion,
            // never more (see CONTACT_CAPTURE_CUSHION).
            c.min_impulse = 0.0f;
            {
                const float approach = std::fabs(pi.vz);
                c.max_impulse = c.effective_mass *
                                (approach + PhysicsV4::CONTACT_CAPTURE_CUSHION);
            }
            c.accumulated_impulse = 0.0f;

            // Mark as contact for friction processing
            c.is_contact = true;
            c.friction_impulse_t1 = 0.0f;
            c.friction_impulse_t2 = 0.0f;

            PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                         (int)c.body_a, (int)c.body_b, "turtle",
                         c.bias, c.effective_mass, c.jz);
            constraints.push_back(c);

            // Turtle contacts provide equilibrium for BFS distance calculation
            active_contacts_.push_back(std::make_pair(i, i));
        }
    }
    auto t_after_turtle = std::chrono::high_resolution_clock::now();

    // Track wake events: count particles at rest before contact detection
    size_t at_rest_before = 0;
    size_t contact_wake_events = 0;  // DEBUG: track wake events from contacts
    size_t threshold_blocked = 0;    // V4.7: wakes blocked by velocity threshold
    for (size_t i = 0; i < count; ++i) {
        if (particles[i].is_at_rest) at_rest_before++;
    }
    auto t_after_at_rest_count = std::chrono::high_resolution_clock::now();

    // V4.4: Track processed contact pairs to prevent duplicates
    // Without this, pair (i,j) and (j,i) both create constraints → double impulse → explosions
    std::unordered_set<size_t> processed_contact_pairs;

    // Check particles using BVH broad-phase
    for (size_t i = 0; i < count; ++i) {
        const Particle& pi = particles[i];

        // Skip massless particles (lights, celestial) - they float
        if (pi.GetMass() == 0.0f) continue;

        // V4.14: Skip at-rest particles for BVH QUERY (not for being found as candidates)
        // At-rest particles are still in BVH, so awake particles will find them.
        // This is safe because: if A is at-rest and B is awake above A,
        // B queries BVH → finds A → creates contact B-A → A supports B.
        // A doesn't need to query because its neighbors are also at-rest.
        //
        // Exception: KINEMATIC particles always query BVH. Their position
        // is externally driven and can change step-to-step (animation
        // clips, scripted movement, input-driven entities); the at-rest
        // short-circuit would cause them to skip contact detection even
        // when they've just been teleported into another body. Staying
        // in the BVH query loop keeps contact generation correct for any
        // kinematic actor regardless of whose driver owns it.
        if (pi.is_at_rest && pi.solver_mode != ParticleSolverMode::KINEMATIC) continue;

        debug_active_particles++;  // Count particles actually processed
        LOGO_COUNT(::logosphere::telemetry::Counter::PhysActiveBodies);

        // Particle i AABB (using predicted Z)
        // Particle dimensions: width=X, height=Y, thickness=Z
        float half_xi = pi.width * 0.5f;
        float half_yi = pi.height * 0.5f;
        float half_zi = pi.thickness * 0.5f;

        float min_xi = pi.x - half_xi;
        float max_xi = pi.x + half_xi;
        float min_yi = pi.y - half_yi;
        float max_yi = pi.y + half_yi;
        float min_zi = z_predicted[i] - half_zi;
        float max_zi = z_predicted[i] + half_zi;

        // Query BVH for nearby candidates - O(log n) instead of O(n)
        // Expand query box by CONTACT_MARGIN in Z to catch speculative contacts
        candidates.clear();
        if (bvh && bvh->is_ready()) {
            AABB query_box(min_xi, min_yi, min_zi - CONTACT_MARGIN,
                          max_xi, max_yi, max_zi + CONTACT_MARGIN);
            bvh->query_aabb(query_box, particles.get_particles(), candidates);
        }
        debug_bvh_queries++;
        debug_total_candidates += candidates.size();
        LOGO_COUNT_N(::logosphere::telemetry::Counter::PhysBvhCandidates, candidates.size());

        for (int candidate : candidates) {
            size_t j = static_cast<size_t>(candidate);
            if (j == i) continue;  // Skip self-collision

            // V4.4: Skip duplicate pairs using canonical key (min,max)
            size_t pair_key = (std::min(i,j) << 16) | std::max(i,j);
            if (processed_contact_pairs.count(pair_key)) continue;

            const Particle& pj = particles[j];

            // Particle j AABB (using predicted Z)
            // Particle dimensions: width=X, height=Y, thickness=Z
            float half_xj = pj.width * 0.5f;
            float half_yj = pj.height * 0.5f;
            float half_zj = pj.thickness * 0.5f;

            float min_xj = pj.x - half_xj;
            float max_xj = pj.x + half_xj;
            float min_yj = pj.y - half_yj;
            float max_yj = pj.y + half_yj;
            float min_zj = z_predicted[j] - half_zj;
            float max_zj = z_predicted[j] + half_zj;

            // Check 3D AABB overlap (must overlap on ALL axes)
            // V4.13: Added CONTACT_MARGIN_XY to X/Y to prevent edge contact jitter
            // Without XY margin, edge-touching boxes lose contact after 0.1mm movement
            bool x_overlap = (min_xi - CONTACT_MARGIN_XY <= max_xj) && (max_xi + CONTACT_MARGIN_XY >= min_xj);
            bool y_overlap = (min_yi - CONTACT_MARGIN_XY <= max_yj) && (max_yi + CONTACT_MARGIN_XY >= min_yj);
            bool z_overlap = (min_zi - CONTACT_MARGIN <= max_zj) && (max_zi + CONTACT_MARGIN >= min_zj);

            if (x_overlap && y_overlap && z_overlap) {
                // Pair-level decisions, with the REASON. Level 4 is where
                // wasted work becomes visible: S20 found ~26 constraint rows
                // per KINEMATIC floor tile, generated against other KINEMATIC
                // tiles that can neither move nor respond. A counter says the
                // rows exist. Only the reason says they are pointless.
                {
                    const Particle& pj_t = particles[j];
                    const bool kin_i = pi.solver_mode == ParticleSolverMode::KINEMATIC;
                    const bool kin_j = pj_t.solver_mode == ParticleSolverMode::KINEMATIC;
                    if (kin_i && kin_j) {
                        PHYS_TRACE_F(::logosphere::phystrace::Pair, "pair_overlap",
                                     (int)i, (int)j, "both_kinematic_neither_can_respond");
                    } else {
                        PHYS_TRACE_F(::logosphere::phystrace::Pair, "pair_overlap",
                                     (int)i, (int)j, "accepted");
                    }
                }

                // =============================================================
                // SKIP CONTACTS BETWEEN GLUONED PARTICLES
                // =============================================================
                // Gluons already constrain the pair. Adding a contact creates
                // conflicting constraints (gluon pulls together, contact pushes apart).
                // The contact's Baumgarte bias can push particles in wrong direction.
                // FIX B: O(1) hash lookup instead of O(g) scan
                // =============================================================
                if (gluon_pairs.count({i, j})) {
                    PHYS_TRACE_F(::logosphere::phystrace::Pair, "pair_rejected",
                                 (int)i, (int)j, "gluon_owns_this_pair");
                    continue;  // Skip - gluon handles this pair
                }

                // =============================================================
                // INTERACTION-PROFILE FILTER (particle_interaction_model.md)
                // =============================================================
                // Declared-passable pairs skip narrow phase. Not decoration:
                // the overlap is recorded and the interaction system applies
                // the declared medium forces + emits episode events outside
                // the solver. Null system / default profiles short-circuit
                // to today's behavior.
                if (interaction_ &&
                    !interaction_->should_contact(pi.interaction_profile_id,
                                                  pj.interaction_profile_id)) {
                    filtered_overlaps_.push_back(
                        {static_cast<uint32_t>(i), static_cast<uint32_t>(j),
                         pi.interaction_profile_id, pj.interaction_profile_id});
                    continue;
                }

                // =============================================================
                // V4.13: REMOVED at-rest skip optimization
                // =============================================================
                // Previously skipped contacts when both particles at_rest.
                // BUG: This broke the support chain. When one particle wakes,
                // it has no contacts with resting neighbors and falls through.
                // Contacts must exist to maintain structural integrity.
                // =============================================================

                // =============================================================
                // V4.14 MOMENTUM-BASED WAKE (the V4.7 TODO, implemented)
                // =============================================================
                // When an active DYNAMIC particle collides with an at-rest
                // particle, wake the sleeper if the speed it would inherit
                // from an inelastic transfer exceeds a small threshold:
                //     v_transfer = m_active / (m_active + m_sleeper) * v_active
                // A 10-tonne boulder at 19 m/s wakes a 500 kg dirt tile
                // (transfer ~18 m/s); a 1 kg pebble cannot wake a 45-tonne
                // bedrock slab (transfer ~0). Same-mass tiles settling at
                // ~1 m/s transfer ~0.5 m/s and stay below threshold, so
                // sequential strata settling does not re-wake lower layers.
                // The old V4.7 flat 50 m/s speed gate made sleeping tiles
                // effectively immovable to every real impact — is_at_rest
                // is a solver optimization, NOT immobility.
                // KINEMATIC actors keep their own (effectively disabled)
                // gate: locomotion must not wake the floor underfoot.
                constexpr float WAKE_TRANSFER_SPEED       = 1.0f;   // m/s inherited
                // A KINEMATIC body wakes a sleeper on APPROACH, not on raw
                // speed. The old 100 m/s raw-speed gate protected gliding
                // foot-plant pin anchors (fast but parallel to the floor,
                // zero closing speed) — but it also made every sleeper
                // immovable to a kinematic pusher: rung 3's 1 m/s finger
                // passed through a sleeping chain untouched, 32 contacts,
                // zero displacement. Closing speed keeps both cases honest:
                // anchors glide (closing ~0, silent), pushers close in.
                constexpr float WAKE_KINEMATIC_APPROACH   = 0.5f;   // m/s closing

                auto should_wake = [](const Particle& active,
                                      const Particle& sleeper,
                                      float active_speed) {
                    if (active.solver_mode == ParticleSolverMode::KINEMATIC) {
                        // Closing speed along the CONTACT NORMAL (the
                        // minimum-overlap axis), not the centre line: a wide
                        // bar's centre can be metres off-axis while its face
                        // plows straight in (rung 4's finger woke nobody),
                        // and a gliding pin anchor overlaps the floor while
                        // approaching on no axis at all.
                        const float ox = std::fmin(active.x + active.width * 0.5f,
                                                   sleeper.x + sleeper.width * 0.5f) -
                                         std::fmax(active.x - active.width * 0.5f,
                                                   sleeper.x - sleeper.width * 0.5f);
                        const float oy = std::fmin(active.y + active.height * 0.5f,
                                                   sleeper.y + sleeper.height * 0.5f) -
                                         std::fmax(active.y - active.height * 0.5f,
                                                   sleeper.y - sleeper.height * 0.5f);
                        const float oz = std::fmin(active.z + active.thickness * 0.5f,
                                                   sleeper.z + sleeper.thickness * 0.5f) -
                                         std::fmax(active.z - active.thickness * 0.5f,
                                                   sleeper.z - sleeper.thickness * 0.5f);
                        int axis = 0; float om = ox;
                        if (oy < om) { om = oy; axis = 1; }
                        if (oz < om) { om = oz; axis = 2; }
                        const float rv[3] = {active.vx - sleeper.vx,
                                             active.vy - sleeper.vy,
                                             active.vz - sleeper.vz};
                        const float toward = (axis == 0)
                            ? ((sleeper.x >= active.x) ? rv[0] : -rv[0])
                            : (axis == 1)
                            ? ((sleeper.y >= active.y) ? rv[1] : -rv[1])
                            : ((sleeper.z >= active.z) ? rv[2] : -rv[2]);
                        return toward >= WAKE_KINEMATIC_APPROACH;
                    }
                    float ma = active.GetMass();
                    float ms = sleeper.GetMass();
                    if (ma <= 0.0f) return false;
                    if (ms <= 0.0f) return active_speed >= WAKE_TRANSFER_SPEED;
                    return (ma / (ma + ms)) * active_speed >= WAKE_TRANSFER_SPEED;
                };

                // SUPPORT-LOSS WAKE: a sleeper whose supporter is leaving
                // must wake, or it stands on air asleep forever (at_rest
                // skips gravity). Support = the awake body's top at the
                // sleeper's bottom; leaving = receding at walking pace.
                auto support_leaving = [](const Particle& sleeper,
                                          const Particle& mover) {
                    const float mover_top = mover.z + mover.thickness * 0.5f;
                    const float sleeper_bottom = sleeper.z - sleeper.thickness * 0.5f;
                    if (std::fabs(mover_top - sleeper_bottom) > 0.05f) return false;
                    const float dx = sleeper.x - mover.x;
                    const float dy = sleeper.y - mover.y;
                    const float dz = sleeper.z - mover.z;
                    const float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                    if (d < 1e-6f) return false;
                    const float receding = -((mover.vx - sleeper.vx) * dx +
                                             (mover.vy - sleeper.vy) * dy +
                                             (mover.vz - sleeper.vz) * dz) / d;
                    return receding >= 0.3f;
                };

                if (pi.is_at_rest && !pj.is_at_rest) {
                    float pj_vel = std::sqrt(pj.vx*pj.vx + pj.vy*pj.vy + pj.vz*pj.vz);
                    if (support_leaving(pi, pj)) {
                        wake_particle_with_propagation(i, particles, pj_vel);
                        contact_wake_events++;
                    } else if (should_wake(pj, pi, pj_vel)) {
                        wake_particle_with_propagation(i, particles, pj_vel);
                        contact_wake_events++;
                    } else {
                        threshold_blocked++;
                    }
                } else if (pj.is_at_rest && !pi.is_at_rest) {
                    float pi_vel = std::sqrt(pi.vx*pi.vx + pi.vy*pi.vy + pi.vz*pi.vz);
                    if (support_leaving(pj, pi)) {
                        wake_particle_with_propagation(j, particles, pi_vel);
                        contact_wake_events++;
                    } else if (should_wake(pi, pj, pi_vel)) {
                        wake_particle_with_propagation(j, particles, pi_vel);
                        contact_wake_events++;
                    } else {
                        threshold_blocked++;
                    }
                }

                // =============================================================
                // GENERAL CONTACT CONSTRAINT
                // =============================================================
                // NARROW PHASE: SAT + Sutherland-Hodgman Face Clipping
                // =============================================================
                // Replaces the old dist_to_exit heuristic with containment-aware
                // SAT axis selection + face clipping for 1-4 contact points.
                // =============================================================
                AABB6 aabb_i = {min_xi, max_xi, min_yi, max_yi, min_zi, max_zi};
                AABB6 aabb_j = {min_xj, max_xj, min_yj, max_yj, min_zj, max_zj};

                // Surface continuity: if j is static, merge its AABB with
                // adjacent coplanar static neighbors. Two adjacent floor tiles
                // aren't two surfaces - they're one surface represented as
                // two particles. Merging removes the data-structure artifact
                // that makes SAT pick the phantom boundary axis.
                if (pj.is_at_rest) {
                    for (int cand_k : candidates) {
                        size_t k = static_cast<size_t>(cand_k);
                        if (k == i || k == j) continue;
                        const Particle& pk = particles[k];
                        if (!pk.is_at_rest) continue;
                        float half_xk = pk.width * 0.5f, half_yk = pk.height * 0.5f, half_zk = pk.thickness * 0.5f;
                        float kx_min = pk.x - half_xk, kx_max = pk.x + half_xk;
                        float ky_min = pk.y - half_yk, ky_max = pk.y + half_yk;
                        float kz_min = pk.z - half_zk, kz_max = pk.z + half_zk;
                        // Coplanar: same Z extent (same thin axis position)
                        constexpr float COPLANAR_EPS = 0.005f;
                        if (std::abs(kz_min - min_zj) > COPLANAR_EPS ||
                            std::abs(kz_max - max_zj) > COPLANAR_EPS) continue;
                        // Adjacent: touching on X or Y (within epsilon)
                        constexpr float ADJ_EPS = 0.01f;
                        bool y_touch = std::abs(ky_min - max_yj) < ADJ_EPS || std::abs(ky_max - min_yj) < ADJ_EPS;
                        bool x_touch = std::abs(kx_min - max_xj) < ADJ_EPS || std::abs(kx_max - min_xj) < ADJ_EPS;
                        // Same extent on the non-touching axis (forms a continuous surface)
                        bool x_aligned = std::abs(kx_min - min_xj) < ADJ_EPS && std::abs(kx_max - max_xj) < ADJ_EPS;
                        bool y_aligned = std::abs(ky_min - min_yj) < ADJ_EPS && std::abs(ky_max - max_yj) < ADJ_EPS;
                        if ((y_touch && x_aligned) || (x_touch && y_aligned)) {
                            // Merge: expand j's effective AABB to include k
                            aabb_j.min_x = std::min(aabb_j.min_x, kx_min);
                            aabb_j.max_x = std::max(aabb_j.max_x, kx_max);
                            aabb_j.min_y = std::min(aabb_j.min_y, ky_min);
                            aabb_j.max_y = std::max(aabb_j.max_y, ky_max);
                        }
                    }
                }

                ContactManifold manifold;
                bool both_box = (pi.shape == ParticleShape::BOX) &&
                                (pj.shape == ParticleShape::BOX);
                bool have_contact;
                if (both_box) {
                    have_contact = narrow_phase_aabb(aabb_i, aabb_j, i, j,
                                                     CONTACT_MARGIN, manifold);
                } else {
                    // At least one side is SPHERE or ELLIPSOID — dispatch
                    // through the shape-aware handler. Surface-merging above
                    // is box-specific and skipped in this path.
                    have_contact = narrow_phase_particle_pair(pi, pj, i, j,
                                                              CONTACT_MARGIN, manifold);
                }
                if (!have_contact) {
                    continue;
                }

                // Effective mass (shared across all contact points in manifold)
                float inv_ma = pi.is_at_rest ? 0.0f : (1.0f / pi.GetMass());
                float inv_mb = pj.is_at_rest ? 0.0f : (1.0f / pj.GetMass());
                float inv_mass_sum = inv_ma + inv_mb;
                // inv_mass_sum == 0 means BOTH bodies are immovable (KINEMATIC,
                // at rest, or the turtle). Their effective mass is infinite, so
                // no finite impulse changes anything and the row's correct
                // contribution is ZERO.
                //
                // This used to fall back to 1.0f, an arbitrary value chosen only
                // to avoid dividing by zero, and it was physically wrong. It
                // produced a non-zero impulse that was then multiplied by zero
                // inverse mass on application: it moved nothing, and recomputed
                // identically on every iteration. Because the solver's stopping
                // test is a GLOBAL max over all rows, one such row pinned the
                // whole world above the convergence threshold permanently.
                //
                // Measured: a 2x2 floor of immovable tiles never converged, at
                // any scale, because its two DIAGONAL tiles produced exactly
                // this row (impulse 4, effective_mass 1, bias 4, unchanged
                // forever). A 1x3 line, which has no diagonal pair, converged
                // fine. See kit study S22 and
                // tests/test_immovable_pair_phantom_impulse.cpp.
                float effective_mass = (inv_mass_sum > 0.0f) ? (1.0f / inv_mass_sum) : 0.0f;

                // V4.9: Material-based contact damping
                float contact_damping = Materials::GetCombinedDamping(pi.material_type, pj.material_type);

                // Corner/edge detection from manifold
                bool is_corner_contact = !manifold.is_face_contact;

                // Use first contact point's penetration for collision event
                float penetration = manifold.points[0].penetration;

                // Create one constraint per contact point in the manifold
                for (int cp = 0; cp < manifold.num_points; cp++) {
                    float cp_pen = manifold.points[cp].penetration;
                    bool is_speculative = (cp_pen <= SLOP);

                    Constraint c;
                    c.body_a = i;
                    c.body_b = j;
                    c.jx = manifold.normal_x;
                    c.jy = manifold.normal_y;
                    c.jz = manifold.normal_z;
                    c.effective_mass = effective_mass;

                    // Bias = target relative velocity along the contact normal.
                    // Active penetration (pen > SLOP): positive bias pushes bodies
                    // apart (Baumgarte). Speculative (pen < 0, gap within margin):
                    // negative bias allows controlled approach up to closing the
                    // gap without overshoot. Resting slop band: zero bias (quiet).
                    // Unilateral clamp (min_impulse=0) ensures we only push, never
                    // pull, so separating contacts get no impulse in either case.
                    if (cp_pen > SLOP) {
                        // V4.14: cap the push-out speed. Baumgarte is a
                        // POSITION correction expressed at velocity level;
                        // uncapped, a deep overlap (SAT axis flip on
                        // rotated boxes, a tunneling fast body) becomes a
                        // ballistic launch — BETA*pen/dt turned a wedged
                        // boulder's ~3 m false penetration into a 72 m/s
                        // ejection (strata impact AT). Deep overlaps now
                        // resolve across frames at a bounded speed.
                        c.bias = std::min(BETA * (cp_pen - SLOP) / dt,
                                          MAX_BIAS_VELOCITY);
                    } else if (cp_pen < 0.0f) {
                        c.bias = BETA * cp_pen / dt;
                    } else {
                        c.bias = 0.0f;
                    }
                    c.penetration = cp_pen;   // measurement, see the field's comment

                    // Material damping: reduce bounce for heavy/damped materials
                    if (contact_damping > 0.0f) {
                        float v_rel_normal = (pi.vx - pj.vx) * c.jx
                                           + (pi.vy - pj.vy) * c.jy
                                           + (pi.vz - pj.vz) * c.jz;
                        if (v_rel_normal < 0.0f) {
                            c.bias *= (1.0f - contact_damping);
                        }
                    }

                    c.min_impulse = 0.0f;
                    // MECHANISM A: capture-bounded, stop the approach plus the
                    // support cushion, never more (CONTACT_CAPTURE_CUSHION).
                    {
                        const float approach = std::fabs(
                            (pi.vx - pj.vx) * c.jx + (pi.vy - pj.vy) * c.jy +
                            (pi.vz - pj.vz) * c.jz);
                        c.max_impulse = c.effective_mass *
                                        (approach + PhysicsV4::CONTACT_CAPTURE_CUSHION);
                    }
                    c.accumulated_impulse = 0.0f;
                    c.is_contact = true;
                    c.friction_impulse_t1 = 0.0f;
                    c.friction_impulse_t2 = 0.0f;

                    PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                                 (int)c.body_a, (int)c.body_b, "sat_manifold",
                                 c.bias, c.effective_mass, c.jz);
                    constraints.push_back(c);
                }

                // V4.4: Mark pair as processed to prevent duplicates
                processed_contact_pairs.insert(pair_key);

                // V4.11: Track contact for persistence
                recent_contact_frames_[pair_key] = phys_frame;

                // CANARY_DEBUG: Log manifold involving canary particle
                if (canary_active && ((int)i == canary_pid || (int)j == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " CONTACT] P" << i << "<->P" << j
                              << " n=(" << manifold.normal_x << "," << manifold.normal_y << "," << manifold.normal_z << ")"
                              << " pen=" << (penetration*1000) << "mm"
                              << " points=" << manifold.num_points << " corner=" << is_corner_contact
                              << " zi=" << pi.z << " zj=" << pj.z << std::endl;
                }

                // Record collision event for game systems (combat, step climbing, etc.)
                // Use manifold's first contact point position, or compute from normal
                float contact_x = (manifold.num_points > 0) ? manifold.points[0].px
                                : pi.x - manifold.normal_x * (pi.width / 2.0f);
                float contact_y = (manifold.num_points > 0) ? manifold.points[0].py
                                : pi.y - manifold.normal_y * (pi.height / 2.0f);
                float contact_z = (manifold.num_points > 0) ? manifold.points[0].pz
                                : z_predicted[i] - manifold.normal_z * (pi.thickness / 2.0f);
                // ORIENT THE REPORTED NORMAL: from A toward B, which is
                // what CollisionEvent has always documented and what every
                // consumer was written against.
                //
                // The solver's manifold normal points the OTHER way, back
                // toward A, despite contact_manifold.h:26 claiming
                // otherwise. The constraint jacobians are built for that
                // sign, so the manifold is left exactly as it is; only the
                // copy leaving physics is oriented.
                //
                // Measured, not assumed (tests/test_knockback_scene, the
                // "which way does the normal point" case): with A left of B
                // the raw manifold gives -x, with A right of B it gives +x.
                // It flips with the approach, so it is a convention, and it
                // is consistently back toward A.
                //
                // Two things were wrong because of this, and both are fixed
                // by the same negation:
                //   - humanoid_locomotion.cpp:5594 pushes a humanoid away
                //     from an obstacle using `is_a ? -normal : normal`,
                //     which with the inverted input drove it INTO the
                //     obstacle instead.
                //   - relative_velocity is documented "negative =
                //     approaching" and was POSITIVE while closing, so any
                //     consumer thresholding on approach speed could never
                //     fire on a real contact.
                //
                // The composed-corner path below (search: "Normal should
                // point from particle_a to particle_b") already emitted
                // A toward B, so this also makes the two writers agree.
                const float evt_nx = -manifold.normal_x;
                const float evt_ny = -manifold.normal_y;
                const float evt_nz = -manifold.normal_z;
                float v_rel_normal = evt_nx * (pj.vx - pi.vx) +
                                     evt_ny * (pj.vy - pi.vy) +
                                     evt_nz * (pj.vz - pi.vz);
                collision_events_.push_back({
                    i, j, penetration,
                    evt_nx, evt_ny, evt_nz,
                    contact_x, contact_y, contact_z,
                    v_rel_normal,
                    pi.solver_mode == ParticleSolverMode::KINEMATIC,
                    pj.solver_mode == ParticleSolverMode::KINEMATIC,
                    is_corner_contact
                });

                // NOTE: Kinematic removed - BFS anchoring now comes from Turtle contacts only
                // (Turtle contacts are added in TURTLE COLLISION DETECTION section above)
            } else {
                // =============================================================
                // V4.11: PERSISTENT CONTACTS - Check if recent contact exists
                // =============================================================
                // When AABB doesn't overlap but pair had contact within N frames,
                // create a speculative contact to prevent jitter-sink pattern.
                // This keeps contacts "warm" during small separations.
                // =============================================================
                auto it = recent_contact_frames_.find(pair_key);
                if (it != recent_contact_frames_.end()) {
                    int frames_since_contact = phys_frame - it->second;
                    if (frames_since_contact <= CONTACT_PERSISTENCE_FRAMES) {
                        // Check if particles are close enough for speculative contact
                        // (within 2x the persistence threshold)
                        float gap_z = std::min(max_zi, max_zj) - std::max(min_zi, min_zj);
                        float gap_threshold = 0.15f;  // 150mm max gap for persistent contact

                        if (gap_z > -gap_threshold) {  // Negative gap = separation
                            // Create speculative contact (Z axis, like stacking)
                            // Normal points from i toward j (so push = -normal pushes i away from j)
                            float center_i = z_predicted[i];
                            float center_j = z_predicted[j];
                            float normal_sign = (center_j > center_i) ? 1.0f : -1.0f;

                            Constraint c;
                            c.body_a = i;
                            c.body_b = j;
                            c.jx = 0.0f;
                            c.jy = 0.0f;
                            c.jz = normal_sign;

                            // at_rest particles act as infinite mass (don't move until woken)
                            float inv_ma_spec = pi.is_at_rest ? 0.0f : (1.0f / pi.GetMass());
                            float inv_mb_spec = pj.is_at_rest ? 0.0f : (1.0f / pj.GetMass());
                            float inv_mass_sum = inv_ma_spec + inv_mb_spec;
                            // 0, not 1: both bodies immovable means infinite
                            // effective mass, so this row's correct
                            // contribution is nothing. Same wrong constant as
                            // the box-box path carried, found 2026-08-02 while
                            // instrumenting. Latent there rather than active
                            // (bias is 0 here, so the phantom impulse was 0
                            // too), but one wrong copy left behind is how the
                            // bug comes back.
                            c.effective_mass = (inv_mass_sum > 0.0f) ? (1.0f / inv_mass_sum) : 0.0f;
                            c.bias = 0.0f;  // Speculative: no position correction
                            // gap_z is min(maxes) - max(mins): positive is already
                            // overlap, negative is separation, same sign as
                            // penetration everywhere else. Z-axis only on this
                            // path, which is all it computes.
                            c.penetration = gap_z;
                            c.min_impulse = 0.0f;
                            // MECHANISM A: capture-bounded (Z-only row, so the
                            // approach speed is the relative vz along jz).
                            {
                                const float approach = std::fabs(
                                    (pi.vz - pj.vz) * c.jz);
                                c.max_impulse = c.effective_mass *
                                                (approach + PhysicsV4::CONTACT_CAPTURE_CUSHION);
                            }
                            c.accumulated_impulse = 0.0f;
                            c.is_contact = true;
                            c.is_turtle_contact = false;
                            c.friction_impulse_t1 = 0.0f;
                            c.friction_impulse_t2 = 0.0f;

                            PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                                         (int)c.body_a, (int)c.body_b, "speculative",
                                         c.bias, c.effective_mass, c.jz);
                            constraints.push_back(c);
                            processed_contact_pairs.insert(pair_key);
                        }
                    }
                }
            }
        }
    }

    // V4.11: Clean up old entries from recent_contact_frames_
    for (auto it = recent_contact_frames_.begin(); it != recent_contact_frames_.end(); ) {
        if (phys_frame - it->second > CONTACT_PERSISTENCE_FRAMES * 2) {
            it = recent_contact_frames_.erase(it);
        } else {
            ++it;
        }
    }

    auto t_after_contacts = std::chrono::high_resolution_clock::now();

    // ==========================================================================
    // PHYSICS TELEMETRY: Record contacts for tracked particles
    // ==========================================================================
    if (telemetry_.is_enabled()) {
        for (unsigned int pid : telemetry_.tracked_particles()) {
            if (pid >= count) continue;
            const Particle& p = particles[pid];

            Logosphere::ParticleFrameSnapshot snap;
            snap.frame_number = phys_frame;
            snap.x = p.x; snap.y = p.y; snap.z = p.z;
            snap.vx = p.vx; snap.vy = p.vy; snap.vz = p.vz;
            snap.width = p.width; snap.height = p.height; snap.thickness = p.thickness;
            snap.is_at_rest = p.is_at_rest;

            for (const auto& evt : collision_events_) {
                if (evt.particle_a != pid && evt.particle_b != pid) continue;

                Logosphere::ContactSnapshot cs;
                cs.particle_a = (unsigned)evt.particle_a;
                cs.particle_b = (unsigned)evt.particle_b;
                cs.normal_x = evt.normal_x;
                cs.normal_y = evt.normal_y;
                cs.normal_z = evt.normal_z;
                cs.penetration = evt.penetration;
                cs.contact_x = evt.contact_x;
                cs.contact_y = evt.contact_y;
                cs.contact_z = evt.contact_z;
                cs.is_corner_contact = evt.is_corner_contact;
                cs.is_horizontal = (std::abs(evt.normal_x) > 0.1f || std::abs(evt.normal_y) > 0.1f);

                snap.contacts.push_back(cs);
                snap.total_contact_count++;
                if (cs.is_horizontal) snap.horizontal_contact_count++;
                if (cs.is_corner_contact) snap.corner_contact_count++;
            }

            telemetry_.get_buffer(pid).record(std::move(snap));
        }
    }

    // ==========================================================================
    // ADD GLUON CONSTRAINTS (3 per gluon: X, Y, Z)
    // ==========================================================================
    /**
     * A gluon says: "These two particles should have the SAME velocity."
     * That's THREE constraints - one for each axis:
     *   v_a.x = v_b.x  →  Constraint with jacobian (1, 0, 0)
     *   v_a.y = v_b.y  →  Constraint with jacobian (0, 1, 0)
     *   v_a.z = v_b.z  →  Constraint with jacobian (0, 0, 1)
     *
     * Breaking force is split equally among the 3 constraints.
     * Total force = sqrt(fx² + fy² + fz²) checked after solving.
     *
     * FIX A: Store constraint indices for O(1) lookup in breaking check
     */
    struct GluonConstraintIndices {
        // IDENTITY, not position. The build loop skips gluons (both endpoints
        // KINEMATIC, zero effective mass), so this vector COMPACTS while
        // gluon_constraints_v2_ does not. Walking both with one counter made
        // every gluon after the first skip get another bond's rows: torn on
        // another bond's force, warm-loaded with another bond's memory.
        // Measured firing thousands of times per run in exactly the two
        // scenes that were red, and never in the one that was green.
        size_t gluon_slot = SIZE_MAX;   // index into gluon_constraints_v2_
        size_t x_idx, y_idx, z_idx;
    };
    std::vector<GluonConstraintIndices> gluon_constraint_indices;
    gluon_constraint_indices.reserve(gluon_constraints_v2_.size());

    constraint_dissatisfied_.assign(particles.size(), 0);
    size_t gluon_slot_counter = 0;
    for (const auto& gluon : gluon_constraints_v2_) {
        const size_t this_gluon_slot = gluon_slot_counter++;
        size_t body_a = gluon->particle_a;
        size_t body_b = gluon->particle_b;

        const Particle& pa = particles[body_a];
        const Particle& pb = particles[body_b];

        // Skip gluon constraints for DYNAMICS-owned particles
        // Dynamics system maintains entity shape via position anchoring, not gluons
        if (pa.solver_mode == ParticleSolverMode::KINEMATIC && pb.solver_mode == ParticleSolverMode::KINEMATIC) {
            // FALSIFICATION CHECK (env GLUON_SKIP_DEBUG): this `continue`
            // skips the row build WITHOUT pushing an index entry, so
            // gluon_constraint_indices compacts while gluon_constraints_v2_
            // does not. Four later loops walk both with one counter, so
            // after the first skip every subsequent gluon is torn,
            // warm-loaded and repaired against ANOTHER bond's rows. Counted
            // here to establish whether it fires in real scenes at all.
            static const bool skip_dbg = std::getenv("GLUON_SKIP_DEBUG") != nullptr;
            if (skip_dbg) {
                static size_t skips = 0, frames = 0;
                if (++skips % 500 == 1)
                    printf("[GLUON_SKIP] both-KINEMATIC skip #%zu at gluon pair "
                           "(%zu,%zu); every later gluon this substep is now "
                           "misassociated\n", skips, body_a, body_b);
                (void)frames;
            }
            continue;
        }

        // Effective mass (reduced mass) - same for all 3 constraints
        // DYNAMICS particles act as infinite mass (their velocity is controlled by dynamics)
        float inv_ma = (pa.solver_mode == ParticleSolverMode::KINEMATIC) ? 0.0f : (pa.GetMass() > 0.0f ? 1.0f / pa.GetMass() : 0.0f);
        float inv_mb = (pb.solver_mode == ParticleSolverMode::KINEMATIC) ? 0.0f : (pb.GetMass() > 0.0f ? 1.0f / pb.GetMass() : 0.0f);
        float inv_mass_sum = inv_ma + inv_mb;
        float eff_mass = (inv_mass_sum > 0.0f) ? (1.0f / inv_mass_sum) : 1.0f;

        // Breaking force per axis (total force checked after solve)
        float breaking = gluon->calculate_breaking_force(pa, pb);
        float breaking_per_axis = breaking * dt;  // Convert to impulse

        // =====================================================================
        // V4.9: MATERIAL-BASED GLUON DAMPING
        // =====================================================================
        // Damping based on BOTH particles' materials (combined).
        // Wood-to-wood: high damping (fibers absorb energy)
        // Stone-to-stone: low damping (rigid)
        // Wood-to-stone: medium (average of both)
        //
        // Damping factor = fraction of relative velocity dissipated per frame.
        // target_v_rel = current_v_rel * (1 - damping_factor)
        // =====================================================================
        float damping_factor = Materials::GetCombinedDamping(pa.material_type, pb.material_type);
        // Force-bounded bonds own their damping character: the gluon's
        // declared damping maps to the per-substep dissipated fraction,
        // c*dt/m of the lighter endpoint. Without this the material table
        // (wood absorbs hard) overrode every declaration and no bond could
        // ring, however low its damping (rung 4: BOUNCY swung zero times).
        if (gluon->force_bounded()) {
            const float m_light = std::fmin(pa.GetMass(), pb.GetMass());
            damping_factor = (m_light > 0.0f)
                ? std::clamp(gluon->damping * dt / m_light, 0.0f, 0.95f)
                : damping_factor;
        }

        // Compute relative velocities for damping bias
        float v_rel[3] = {
            pa.vx - pb.vx,
            pa.vy - pb.vy,
            pa.vz - pb.vz
        };

        // Create 3 constraints: X, Y, Z and store indices (FIX A)
        float jacobians[3][3] = {
            {1.0f, 0.0f, 0.0f},  // X axis
            {0.0f, 1.0f, 0.0f},  // Y axis
            {0.0f, 0.0f, 1.0f}   // Z axis
        };

        // =====================================================================
        // V4.10: UNIFIED SOLVER - Gluon position bias in Jacobian
        // =====================================================================
        // Instead of separate position projection phase, add gentle position
        // correction bias to gluon constraints. This lets contacts and gluons
        // find equilibrium together in the same solver iteration.
        //
        // Separation = attachment_a - attachment_b (what we need to close)
        // bias = GLUON_POSITION_BETA * separation[axis]  (no /dt to prevent explosion)
        //
        // V4.10: ALWAYS compute position bias - removed at-rest skip.
        // When gravity applies to all particles, the skip condition was causing
        // gluon constraints to apply zero bias, allowing connected particles to
        // fall apart even though they should be held together.
        // =====================================================================
        float separation[3] = {0.0f, 0.0f, 0.0f};
        float bond_err_mag = 0.0f;   // |anchor distance - rest|, for force-bounded budgets
        float lever_a[3] = {0,0,0}, lever_b[3] = {0,0,0};   // anchor - centre, world
        bool skip_position_bias = false;  // V4.10: Never skip - always compute
        if constexpr (PhysicsV4::ENABLE_GLUON_JACOBIAN_POSITION_BIAS) {
            // V4.10: Removed skip conditions - gluons always need position bias
            // to resist drift caused by external forces (gravity)
        }
        if constexpr (PhysicsV4::ENABLE_GLUON_JACOBIAN_POSITION_BIAS) {
            if (!skip_position_bias) {
            // Calculate attachment points (same logic as project_gluon_positions)
            float attach_a_x, attach_a_y, attach_a_z;
            float attach_b_x, attach_b_y, attach_b_z;

            if (gluon->rotate_offsets) {
                // Rotate offsets by the particle's FULL orientation, Euler
                // applied X then Y then Z (the same composition generation
                // uses to point a segment's local +Z along its grown
                // direction). Reduces exactly to the old rotation_z-only
                // math when rotation_x = rotation_y = 0, which is every
                // pre-existing offset user (humanoid pins, nails). Rung 1
                // of the rotation ladder: a parent turning about X must
                // carry its anchor with it.
                auto rotate_full = [](const Particle& p, float ox, float oy,
                                      float oz, float& wx, float& wy, float& wz) {
                    const float cx = cosf(p.rotation_x), sx = sinf(p.rotation_x);
                    const float cy = cosf(p.rotation_y), sy = sinf(p.rotation_y);
                    const float cz = cosf(p.rotation_z), sz = sinf(p.rotation_z);
                    // X: (ox, oy*cx - oz*sx, oy*sx + oz*cx)
                    const float y1 = oy * cx - oz * sx;
                    const float z1 = oy * sx + oz * cx;
                    // Y: (ox*cy + z1*sy, y1, -ox*sy + z1*cy)
                    const float x2 = ox * cy + z1 * sy;
                    const float z2 = -ox * sy + z1 * cy;
                    // Z: same form the site always used
                    wx = p.x + x2 * cz - y1 * sz;
                    wy = p.y + x2 * sz + y1 * cz;
                    wz = p.z + z2;
                };
                rotate_full(pa, gluon->offset_a.x, gluon->offset_a.y,
                            gluon->offset_a.z, attach_a_x, attach_a_y, attach_a_z);
                rotate_full(pb, gluon->offset_b.x, gluon->offset_b.y,
                            gluon->offset_b.z, attach_b_x, attach_b_y, attach_b_z);
            } else {
                // World-space offsets (no rotation)
                attach_a_x = pa.x + gluon->offset_a.x;
                attach_a_y = pa.y + gluon->offset_a.y;
                attach_a_z = pa.z + gluon->offset_a.z;

                attach_b_x = pb.x + gluon->offset_b.x;
                attach_b_y = pb.y + gluon->offset_b.y;
                attach_b_z = pb.z + gluon->offset_b.z;
            }

            lever_a[0] = attach_a_x - pa.x; lever_a[1] = attach_a_y - pa.y;
            lever_a[2] = attach_a_z - pa.z;
            lever_b[0] = attach_b_x - pb.x; lever_b[1] = attach_b_y - pb.y;
            lever_b[2] = attach_b_z - pb.z;

            // Separation vector (a - b)
            float sep_x = attach_a_x - attach_b_x;
            float sep_y = attach_a_y - attach_b_y;
            float sep_z = attach_a_z - attach_b_z;

            // CRITICAL: Account for target_distance
            // Gluons want to maintain target_distance, not zero distance
            // Error = current_distance - target_distance
            // Only correct the error, not the full separation
            float current_dist = std::sqrt(sep_x*sep_x + sep_y*sep_y + sep_z*sep_z);
            float target_dist = gluon->target_distance;

            if (current_dist > 0.0001f) {  // Avoid division by zero
                float error = current_dist - target_dist;
                bond_err_mag = std::fabs(error);
                // Wake-on-strain (see GLUON_WAKE_STRAIN): a bond under
                // geometric strain wakes its bodies, whatever moved the
                // anchor (kinematic rotation, teleport, mass change).
                if (std::fabs(error) > PhysicsV4::GLUON_WAKE_STRAIN) {
                    if (body_a < constraint_dissatisfied_.size())
                        constraint_dissatisfied_[body_a] = 1;
                    if (body_b < constraint_dissatisfied_.size())
                        constraint_dissatisfied_[body_b] = 1;
                    // Clearing is_at_rest alone is the hysteresis trap: the
                    // rest counter stays high, the damper crushes the first
                    // small correction, and sleep re-latches within the
                    // frame (the lying-blade probe saw rest=1 every sample
                    // while this wake fired every frame). Freedom resets
                    // the counter, same as wake-on-break.
                    if (pa.solver_mode != ParticleSolverMode::KINEMATIC) {
                        particles[body_a].is_at_rest = false;
                        particles[body_a].low_velocity_frames = 0;
                    }
                    if (pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                        particles[body_b].is_at_rest = false;
                        particles[body_b].low_velocity_frames = 0;
                    }
                }
                // Project error along separation direction to get per-axis correction
                float scale = error / current_dist;
                separation[0] = sep_x * scale;
                separation[1] = sep_y * scale;
                separation[2] = sep_z * scale;
            } else {
                // Particles coincident - no correction needed
                separation[0] = separation[1] = separation[2] = 0.0f;
            }
            }  // if (!skip_position_bias)
        }

        // V4.9: Apply material-based damping to velocities BEFORE constraint solve
        // This reduces relative motion energy based on material properties
        // Wood-to-wood: high damping (fibers absorb), stone-to-stone: low damping (rigid)
        //
        // KINEMATIC endpoints are infinite mass (2026-07-10 fix): the COM
        // velocity of an infinite+finite mass pair IS the kinematic
        // endpoint's velocity, so only the dynamic endpoint damps toward
        // it and the kinematic side is NEVER written. Without this, a
        // pinned foot-plant anchor was dragged to walking speed by its
        // pin gluon, never rested, and leaked anchors accumulated into
        // an eternally-awake contact horde (contract a4 in
        // tests/test_position_authority.cpp).
        // AT-REST IS INFINITE MASS HERE TOO. Every other site in this solver
        // gives a sleeping body inv_mass = 0 (the impulse pass computes
        // `pa.is_at_rest || KINEMATIC ? 0.0f : 1/m`), but this blend wrote
        // both endpoints unconditionally. A sleeping 111.661 kg branch
        // carrying ten 0.107 kg leaves was handed com_vz * damping_factor
        // once per leaf per substep — 1.02e-3 m/s per frame, 0.6% of gravity,
        // on a body that skips gravity and never woke because the ramp stayed
        // under the 0.2 m/s wake threshold. Linear velocity ramp integrates to
        // quadratic drift: 6.91 m predicted over 900 frames, 6.74 m measured.
        // That was the foliage canopy "detaching" — the branches were sinking
        // out from under leaves that stayed where they were put.
        if (damping_factor > 0.0f) {
            static const bool at_rest_damp_old = std::getenv("AT_REST_DAMP_OLD") != nullptr;
            bool a_immovable = (pa.solver_mode == ParticleSolverMode::KINEMATIC) ||
                               (pa.is_at_rest && !at_rest_damp_old);
            bool b_immovable = (pb.solver_mode == ParticleSolverMode::KINEMATIC) ||
                               (pb.is_at_rest && !at_rest_damp_old);
            float keep = 1.0f - damping_factor;

            if (a_immovable && b_immovable) {
                // Both infinite mass — nothing to damp.
            } else if (a_immovable || b_immovable) {
                size_t dyn_id = a_immovable ? body_b : body_a;
                size_t kin_id = a_immovable ? body_a : body_b;
                const Particle& kin_p = particles[kin_id];
                Particle& dyn_p = particles[dyn_id];
                dyn_p.vx = kin_p.vx + (dyn_p.vx - kin_p.vx) * keep;
                dyn_p.vy = kin_p.vy + (dyn_p.vy - kin_p.vy) * keep;
                dyn_p.vz = kin_p.vz + (dyn_p.vz - kin_p.vz) * keep;
            } else {
                // Get current velocities
                float va_x = particles[body_a].vx, va_y = particles[body_a].vy, va_z = particles[body_a].vz;
                float vb_x = particles[body_b].vx, vb_y = particles[body_b].vy, vb_z = particles[body_b].vz;

                // Compute center-of-mass velocity (conserved)
                float mass_a = pa.GetMass(), mass_b = pb.GetMass();
                float total_mass = mass_a + mass_b;
                float com_vx = (mass_a * va_x + mass_b * vb_x) / total_mass;
                float com_vy = (mass_a * va_y + mass_b * vb_y) / total_mass;
                float com_vz = (mass_a * va_z + mass_b * vb_z) / total_mass;

                // Relative velocities from COM
                float rel_a_x = va_x - com_vx, rel_a_y = va_y - com_vy, rel_a_z = va_z - com_vz;
                float rel_b_x = vb_x - com_vx, rel_b_y = vb_y - com_vy, rel_b_z = vb_z - com_vz;

                // Damp relative velocities (reduce by damping_factor)
                particles[body_a].vx = com_vx + rel_a_x * keep;
                particles[body_a].vy = com_vy + rel_a_y * keep;
                particles[body_a].vz = com_vz + rel_a_z * keep;
                particles[body_b].vx = com_vx + rel_b_x * keep;
                particles[body_b].vy = com_vy + rel_b_y * keep;
                particles[body_b].vz = com_vz + rel_b_z * keep;
            }
        }

        GluonConstraintIndices indices;
        for (int axis = 0; axis < 3; ++axis) {
            Constraint c;
            c.body_a = body_a;
            c.body_b = body_b;
            c.jx = jacobians[axis][0];
            c.jy = jacobians[axis][1];
            c.jz = jacobians[axis][2];
            c.effective_mass = eff_mass;

            // V4.10: Position bias for unified solver
            // Gentle correction: bias = BETA * separation (no /dt to prevent explosion)
            // Negative bias because we want to CLOSE the gap (reduce separation)
            if constexpr (PhysicsV4::ENABLE_GLUON_JACOBIAN_POSITION_BIAS) {
                // ONE CORRECTION LAW for every constraint: a position error
                // is corrected at BETA * error / dt, bounded by a velocity
                // cap. Gluons alone were missing the /dt ("no /dt to prevent
                // explosion"), which made them 30x weaker than contacts at
                // the same error: a 0.3 m gap earned 0.12 m/s of closing
                // speed against a 1 m/s drag, so bonds could never hold
                // under a sustained push (measured 124-650 mm gaps, blades
                // stretched past their tear limit for many frames). The
                // explosion that removal avoided is what the cap now
                // prevents by construction, so the law can be uniform.
                c.bias = std::clamp(
                    -PhysicsV4::GLUON_POSITION_BETA * separation[axis] / dt,
                    -PhysicsV4::GLUON_MAX_BIAS_VELOCITY,
                     PhysicsV4::GLUON_MAX_BIAS_VELOCITY);
            } else {
                c.bias = 0.0f;  // Old: Gluons match velocities only
            }

            // Gluons can push or pull (bidirectional). Force-bounded bonds
            // (organics) can exert at most their spring-damper force at the
            // current stretch; rigid welds keep the full breaking budget.
            float budget = breaking_per_axis;
            if (gluon->force_bounded()) {
                const float vrel_ax = std::fabs(v_rel[axis]);
                budget = std::min(budget,
                                  (gluon->stiffness * bond_err_mag +
                                   gluon->damping * vrel_ax) * dt);
            }
            c.min_impulse = -budget;
            c.max_impulse = budget;
            c.accumulated_impulse = 0.0f;

            // FIX A: Store constraint index for this axis
            size_t constraint_idx = constraints.size();
            if (axis == 0) indices.x_idx = constraint_idx;
            else if (axis == 1) indices.y_idx = constraint_idx;
            else indices.z_idx = constraint_idx;

            // Anchor lever arms for the force-to-rotation transducer.
            // RCA lever: ANCHOR_TORQUE_OFF=1 removes the reciprocal half of
            // the pivot problem. If the perfectly-opposed row pair (drive
            // +3.3 rad/s vs anchor -3.3) is what stalls the solve, removing
            // one side must drop the improvement_below_min_rate exits.
            static const bool at_off = std::getenv("ANCHOR_TORQUE_OFF") != nullptr;
            c.apply_anchor_torque = !at_off;
            c.anchor_rax = lever_a[0]; c.anchor_ray = lever_a[1]; c.anchor_raz = lever_a[2];
            c.anchor_rbx = lever_b[0]; c.anchor_rby = lever_b[1]; c.anchor_rbz = lever_b[2];
            // The row's effective mass must include the angular response,
            // K = 1/m_a + 1/m_b + (r_a x J)^2/I_a + (r_b x J)^2/I_b, or every
            // impulse over-corrects the anchor it now spins.
            {
                float k = inv_ma + inv_mb;
                const float jx = c.jx, jy = c.jy, jz = c.jz;
                if (pa.is_quat_driven && pa.owner == ParticleOwner::PHYSICS &&
                    pa.solver_mode != ParticleSolverMode::KINEMATIC) {
                    const float I = pa.GetMomentOfInertia();
                    if (I > 0.0f) {
                        const float cx = lever_a[1]*jz - lever_a[2]*jy;
                        const float cy = lever_a[2]*jx - lever_a[0]*jz;
                        const float cz = lever_a[0]*jy - lever_a[1]*jx;
                        k += (cx*cx + cy*cy + cz*cz) / I;
                    }
                }
                if (pb.is_quat_driven && pb.owner == ParticleOwner::PHYSICS &&
                    pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                    const float I = pb.GetMomentOfInertia();
                    if (I > 0.0f) {
                        const float cx = lever_b[1]*jz - lever_b[2]*jy;
                        const float cy = lever_b[2]*jx - lever_b[0]*jz;
                        const float cz = lever_b[0]*jy - lever_b[1]*jx;
                        k += (cx*cx + cy*cy + cz*cz) / I;
                    }
                }
                if (k > 0.0f) c.effective_mass = 1.0f / k;
            }

            PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                         (int)c.body_a, (int)c.body_b, "gluon_axis",
                         c.bias, c.effective_mass, c.jz);
            constraints.push_back(c);
        }
        indices.gluon_slot = this_gluon_slot;
        gluon_constraint_indices.push_back(indices);

        // IMPULSE MEMORY apply: last frame's carried load, up front, so the
        // rows correct residuals instead of rebuilding the force from the
        // error (see the field's comment). ORGANIC bonds only: warm-loading
        // humanoid drive gluons fought the commanded pose drive (shoulder
        // held a 0.64 rad standing error, never converging) and fed the
        // spawn-time arm spikes (walk gate 21 -> 2 events with this gate).
        if (gluon->force_bounded() && !impulse_memory_off()) {
            Particle& wa = particles[body_a];
            Particle& wb = particles[body_b];
            const float winv_a = (wa.is_at_rest ||
                                  wa.solver_mode == ParticleSolverMode::KINEMATIC)
                                 ? 0.0f : (1.0f / wa.GetMass());
            const float winv_b = (wb.is_at_rest ||
                                  wb.solver_mode == ParticleSolverMode::KINEMATIC)
                                 ? 0.0f : (1.0f / wb.GetMass());
            wa.vx += gluon->warm_ix * winv_a;
            wa.vy += gluon->warm_iy * winv_a;
            wa.vz += gluon->warm_iz * winv_a;
            wb.vx -= gluon->warm_ix * winv_b;
            wb.vy -= gluon->warm_iy * winv_b;
            wb.vz -= gluon->warm_iz * winv_b;
        }

        // =================================================================
        // ANGULAR CONSTRAINT (V4.2) - Rotation coupling
        // =================================================================
        // If gluon has angular constraint enabled, add angular constraint
        // that couples rotation_z between connected particles.
        //
        // DEAD ZONE: Within ±limit, free rotation (no impulse).
        // Beyond limit, Baumgarte bias pushes back to boundary.
        // =================================================================
        if (gluon->enable_angular_constraint) {
            Constraint c_ang;
            c_ang.body_a = body_a;
            c_ang.body_b = body_b;
            c_ang.is_angular = true;
            c_ang.jx = c_ang.jy = c_ang.jz = 0.0f;  // No linear Jacobian for angular

            // Effective inertia (reduced inertia) - same as reduced mass for rotation
            // DYNAMICS particles = infinite inertia (inv_inertia = 0), matching
            // the linear solver convention (inv_mass = 0 for DYNAMICS).
            // The row's inertia must be about the axis it actually turns, not
            // a single scalar built from two of the three extents (see
            // Particle::GetInertiaAboutAxis). The scalar path turns about Z;
            // the quaternion path recomputes below, once its axis is known.
            float I_a = pa.GetInertiaAboutAxis(0.0f, 0.0f, 1.0f);
            float I_b = pb.GetInertiaAboutAxis(0.0f, 0.0f, 1.0f);
            float inv_inertia_sum = 0.0f;
            if (I_a > 0.0f && pa.solver_mode != ParticleSolverMode::KINEMATIC) inv_inertia_sum += 1.0f / I_a;
            if (I_b > 0.0f && pb.solver_mode != ParticleSolverMode::KINEMATIC) inv_inertia_sum += 1.0f / I_b;

            if (inv_inertia_sum > 0.0f) {
                c_ang.effective_inertia = 1.0f / inv_inertia_sum;
            } else {
                continue;  // Zero inertia (shouldn't happen), skip
            }

            // Angular limit (dead zone)
            c_ang.angular_limit = gluon->max_relative_rotation;

            // TEMP QUATPATH_DEBUG: the lying-blade probe (peg bond 67<->68)
            static const bool qdbg = std::getenv("QUATPATH_DEBUG") != nullptr;
            const bool qhit = qdbg && ((body_a == 67 && body_b == 68) ||
                                       (body_a == 68 && body_b == 67));
            if (qhit) {
                static int qn = 0;
                if ((qn++ % 240) == 0)
                    printf("[QDBG] peg bond reached: drive=%d quat=%d ang_k=%.1f "
                           "a_rest=%d b_rest=%d\n",
                           (int)gluon->angular_drive_enabled,
                           (int)gluon->use_quat_target, gluon->angular_stiffness,
                           (int)pa.is_at_rest, (int)pb.is_at_rest);
            }

            // Shared Baumgarte gain for all angular bias calculations
            // (both the scalar Z path below and the 3-axis path above).
            const float ANGULAR_BETA = 0.4f;

            // =============================================================
            // 3-AXIS QUATERNION DRIVE (rotational-DOF upgrade Stage 2)
            // =============================================================
            // When the gluon carries a quaternion target, emit three
            // independent angular rows (one per world axis). Each row
            // drives the corresponding component of the Rodrigues error
            // vector to zero. The scalar Z-axis path below continues to
            // service every gluon that hasn't opted in to quaternion
            // mode, so existing callers are untouched.
            //
            // Error formulation: we want q_b = target_relative_q * q_a
            // (i.e., child's orientation = parent's orientation composed
            // with the relative target). The error quaternion is
            //   q_err = q_b * (target * q_a)^(-1)
            //         = q_b * q_a^(-1) * target^(-1)
            // When the solver drives q_err -> identity the pose is met.
            // Decompose q_err to axis-angle (axis, theta); the 3-element
            // Rodrigues error e = theta * axis is what each world-axis
            // constraint row tries to null out.
            if (gluon->angular_drive_enabled && gluon->use_quat_target) {
                if (qhit) {
                    static int qn2 = 0;
                    if ((qn2++ % 240) == 0) printf("[QDBG] quat path entered\n");
                }
                namespace lm = logosphere;
                // Parent orientation source: if the parent is itself
                // quat-driven (nested joint), its rotation_q is kept
                // current by the solver. Otherwise it's FK-owned; sync
                // from its Euler triple on-the-fly so the error term
                // reflects the current world orientation.
                lm::Quat q_a = pa.is_quat_driven
                    ? pa.rotation_q
                    : lm::Quat::from_euler(pa.rotation_x, pa.rotation_y, pa.rotation_z);
                lm::Quat q_b = pb.is_quat_driven
                    ? pb.rotation_q
                    : lm::Quat::from_euler(pb.rotation_x, pb.rotation_y, pb.rotation_z);
                lm::Quat q_err = q_b * q_a.conjugate() * gluon->target_relative_q.conjugate();

                // Rodrigues error vector, computed directly from the
                // quaternion components to avoid the axis/sin_half
                // division that to_axis_angle does. That division is
                // stable for large rotations but at small theta it
                // amplifies floating-point noise on the "zero" axes
                // into full unit magnitudes — a pure-X rotation's
                // residual near-identity q_err would emerge with a
                // phantom Y or Z axis pointing somewhere meaningless,
                // and the constraint would then apply impulse along
                // that noisy direction, pumping cross-axis omega on
                // every physics-drive particle and ringing through
                // chains.
                //
                // For a unit quaternion q = (w, x, y, z) with w >= 0,
                // the Rodrigues vector is 2*(x, y, z) / sinc(theta/2).
                // At small theta, sinc(theta/2) -> 1 so e ~ 2*(x,y,z).
                // Using this directly means the error vector decays
                // smoothly to zero with no division blowup, and the
                // constraint row's magnitude equals |e| = |theta|.
                // Sign of w handles the "short way" (if w<0, negate
                // so we correct the -180..+180 wrap correctly).
                float ex = 2.0f * q_err.x;
                float ey = 2.0f * q_err.y;
                float ez = 2.0f * q_err.z;
                if (q_err.w < 0.0f) { ex = -ex; ey = -ey; ez = -ez; }
                float e_mag = std::sqrt(ex*ex + ey*ey + ez*ez);

                // Wake-on-angular-strain: a joint bent away from its target
                // is mid-recovery; sleep would freeze it bent (rung 3: the
                // chain 'STAYED BENT' at 0.70 m because nothing wakes on
                // angle error — the linear anchors were satisfied).
                if (qhit) {
                    static int qn3 = 0;
                    if ((qn3++ % 240) == 0)
                        printf("[QDBG] e_mag=%.3f rad (wake at >0.1)\n", e_mag);
                }
                if (e_mag > 0.1f) {
                    if (body_a < constraint_dissatisfied_.size())
                        constraint_dissatisfied_[body_a] = 1;
                    if (body_b < constraint_dissatisfied_.size())
                        constraint_dissatisfied_[body_b] = 1;
                    if (pa.solver_mode != ParticleSolverMode::KINEMATIC) {
                        particles[body_a].is_at_rest = false;
                        particles[body_a].low_velocity_frames = 0;
                    }
                    if (pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                        particles[body_b].is_at_rest = false;
                        particles[body_b].low_velocity_frames = 0;
                    }
                }
                // PLASTIC YIELD: absorb deformation beyond the yield angle
                // into the rest pose. rel = q_b * q_a^-1; the new target
                // keeps exactly 'yield' of elastic error along the current
                // axis: target_new = FA(axis, -yield) * rel.
                if (std::isfinite(gluon->plastic_yield_angle) &&
                    e_mag > gluon->plastic_yield_angle && e_mag > 1e-6f) {
                    const float inv_m = 1.0f / e_mag;
                    lm::Quat rel = q_b * q_a.conjugate();
                    gluon->target_relative_q = lm::Quat::from_axis_angle(
                        ex * inv_m, ey * inv_m, ez * inv_m,
                        -gluon->plastic_yield_angle) * rel;
                    // Recompute the error at the (now clamped) yield.
                    lm::Quat q2 = rel * gluon->target_relative_q.conjugate();
                    ex = 2.0f * q2.x; ey = 2.0f * q2.y; ez = 2.0f * q2.z;
                    if (q2.w < 0.0f) { ex = -ex; ey = -ey; ez = -ez; }
                    e_mag = std::sqrt(ex*ex + ey*ey + ez*ez);
                }
                if (e_mag > 1e-6f) {
                    float inv_mag = 1.0f / e_mag;
                    Constraint c_axis = c_ang;
                    c_axis.angular_axis_x = ex * inv_mag;
                    c_axis.angular_axis_y = ey * inv_mag;
                    c_axis.angular_axis_z = ez * inv_mag;
                    c_axis.angular_axis_vec_len = 1.0f;  // unit axis

                    // Bias drives omega along the axis so the Rodrigues
                    // error is zeroed out after BETA/dt frames. e_mag
                    // is equivalent to theta in the small-angle regime
                    // and to 2*sin(theta/2) for larger angles — slightly
                    // gentler than theta itself for big rotations, which
                    // happens to curb the initial overshoot that ran the
                    // chain into saturation.
                    {   // size this row by the inertia about ITS axis
                        const float axx = c_axis.angular_axis_x;
                        const float axy = c_axis.angular_axis_y;
                        const float axz = c_axis.angular_axis_z;
                        const float Ia2 = (pa.solver_mode == ParticleSolverMode::KINEMATIC)
                            ? 0.0f : pa.GetInertiaAboutAxis(axx, axy, axz);
                        const float Ib2 = (pb.solver_mode == ParticleSolverMode::KINEMATIC)
                            ? 0.0f : pb.GetInertiaAboutAxis(axx, axy, axz);
                        float inv2 = 0.0f;
                        if (Ia2 > 0.0f) inv2 += 1.0f / Ia2;
                        if (Ib2 > 0.0f) inv2 += 1.0f / Ib2;
                        if (inv2 > 0.0f) c_axis.effective_inertia = 1.0f / inv2;
                    }
                    c_axis.angular_bias = std::min(
                        ANGULAR_BETA * e_mag / dt,
                        PhysicsV4::MAX_ANGULAR_BIAS_VELOCITY);
                    // Force-bounded bonds bend at their own angular
                    // stiffness: torque budget = (k_ang * angle error +
                    // c_ang * |relative spin|) * dt. Rigid joints keep
                    // infinite authority (their semantic). Without this the
                    // drive held segments upright with unbounded torque and
                    // a pushed chain could only shear (rung 3: rot 0.0 deg,
                    // joints gaping 717 mm).
                    if (gluon->force_bounded()) {
                        const float wrel = std::fabs(
                            (pa.omega_x - pb.omega_x) * c_axis.angular_axis_x +
                            (pa.omega_y - pb.omega_y) * c_axis.angular_axis_y +
                            (pa.omega_z - pb.omega_z) * c_axis.angular_axis_z);
                        const float tb = (gluon->angular_stiffness * e_mag +
                                          gluon->angular_damping * wrel) * dt;
                        c_axis.min_angular_impulse = -tb;
                        c_axis.max_angular_impulse = tb;
                    } else {
                        c_axis.min_angular_impulse = -std::numeric_limits<float>::infinity();
                        c_axis.max_angular_impulse = std::numeric_limits<float>::infinity();
                    }
                    c_axis.accumulated_angular_impulse = 0.0f;
                    PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                                 (int)c_axis.body_a, (int)c_axis.body_b, "gluon_angular",
                                 c_axis.bias, c_axis.effective_mass, c_axis.jz);
                    constraints.push_back(c_axis);
                }
                continue;  // quaternion path handled; skip the scalar branch below
            }

            // Calculate relative rotation and bias (scalar Z-axis path)
            float angle_diff = pb.rotation_z - pa.rotation_z - gluon->target_relative_rotation;
            // Normalize to [-π, π]
            angle_diff = std::fmod(angle_diff + M_PI, 2.0f * M_PI);
            if (angle_diff < 0) angle_diff += 2.0f * M_PI;
            angle_diff -= M_PI;

            // VELOCITY COUPLING WITH POSITION LIMITS
            // Always couple angular velocities (particles rotate together)
            // Position limits prevent excessive relative rotation
            //
            // Inside limits = velocity coupling only (bias=0, match omegas)
            // At/past limits = velocity coupling + position correction
            // Drive enabled  = bias always pulls toward target_relative_rotation
            //                  (Option C: animation commands via target field)
            float limit = gluon->max_relative_rotation;

            float error = 0.0f;
            bool at_positive_limit = (angle_diff >= limit);
            bool at_negative_limit = (angle_diff <= -limit);

            if (gluon->angular_drive_enabled) {
                // PD-style drive: always pull toward target. angle_diff is
                // already relative to target, so it IS the error. Solver
                // will apply impulses in whichever direction reduces it.
                c_ang.angular_bias = ANGULAR_BETA * angle_diff / dt;
                c_ang.min_angular_impulse = -std::numeric_limits<float>::infinity();
                c_ang.max_angular_impulse = std::numeric_limits<float>::infinity();
            } else if (at_positive_limit) {
                // Past positive limit - position correction + velocity coupling
                error = angle_diff - limit;
                c_ang.angular_bias = ANGULAR_BETA * error / dt;
                // One-way: only allow impulses that reduce angle_diff
                c_ang.min_angular_impulse = 0.0f;
                c_ang.max_angular_impulse = std::numeric_limits<float>::infinity();
            } else if (at_negative_limit) {
                // Past negative limit - position correction + velocity coupling
                error = angle_diff + limit;
                c_ang.angular_bias = ANGULAR_BETA * error / dt;
                // One-way: only allow impulses that increase angle_diff
                c_ang.min_angular_impulse = -std::numeric_limits<float>::infinity();
                c_ang.max_angular_impulse = 0.0f;
            } else {
                // Inside dead zone - velocity coupling only (no position correction)
                // This makes connected particles rotate TOGETHER
                c_ang.angular_bias = 0.0f;
                c_ang.min_angular_impulse = -std::numeric_limits<float>::infinity();
                c_ang.max_angular_impulse = std::numeric_limits<float>::infinity();
            }

            // Note: min/max_angular_impulse already set above for one-way limit
            // B1 angular: bounded correction rate (MAX_ANGULAR_BIAS_VELOCITY).
            c_ang.angular_bias = std::clamp(c_ang.angular_bias,
                                            -PhysicsV4::MAX_ANGULAR_BIAS_VELOCITY,
                                             PhysicsV4::MAX_ANGULAR_BIAS_VELOCITY);
            c_ang.accumulated_angular_impulse = 0.0f;

            PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                         (int)c_ang.body_a, (int)c_ang.body_b, "angular",
                         c_ang.bias, c_ang.effective_mass, c_ang.jz);
            constraints.push_back(c_ang);
        }
    }

    auto t_after_gluons = std::chrono::high_resolution_clock::now();
    // (authority report emitted after the solve, below)

    if (constraints.empty()) return;

    // ========================================================================
    // PHASE 4: ITERATIVE CONSTRAINT SOLVING (THE CORE)
    // ========================================================================
    /**
     * VEC3 JACOBIAN CONSTRAINT SOLVING
     *
     * Each constraint has a Jacobian (jx, jy, jz) - the direction of the
     * constraint force. We want particles to not approach along this direction.
     *
     * v_rel = dot(jacobian, v_a - v_b)
     *       = jx*(va.x - vb.x) + jy*(va.y - vb.y) + jz*(va.z - vb.z)
     *
     * If v_rel < 0: particles approaching, apply impulse to separate
     * Impulse pushes body_a in +jacobian direction, body_b in -jacobian
     *
     * APPLYING IMPULSE (Vec3):
     *   v_a += jacobian * (impulse / mass_a)
     *   v_b -= jacobian * (impulse / mass_b)
     */

    // ========================================================================
    // SMART EARLY STOPPING
    // ========================================================================
    // Track convergence RATE, not just absolute impulse magnitude.
    // Stop when: (1) impulses tiny OR (2) not improving meaningfully anymore.
    // ========================================================================
    int actual_iterations = 0;
    float max_dv_this_iter = 0.0f;
    float prev_max_impulse = 1e10f;
    int low_improvement_count = 0;
    constexpr float MIN_IMPROVEMENT_RATE = 0.05f;  // Need 5% improvement per iteration
    constexpr int MIN_ITERATIONS = 6;              // At least 6 iterations before considering early stop
    constexpr int PLATEAU_CONFIRM = 3;             // Need 3 consecutive low-improvement iters
    // The residual velocity below which a plateau really is convergence. Same
    // physical quantity whatever the body's mass, unlike an impulse or a rate.
    constexpr float VELOCITY_PLATEAU_FLOOR = 0.001f;  // m/s

    // NOTE ON PLACEMENT: this runs BEFORE warm starting, and it must.
    // `warm_started_impulses` below is keyed by constraint INDEX and read back
    // after the solve to update the impulse cache. Permuting between those two
    // points files one pair's equilibrium impulse under another pair's key,
    // which corrupts warm starting across frames. The first version of this
    // lever sat just above the iteration loop and did exactly that, so the
    // experiment was measuring "shuffle plus a broken cache" and its numbers
    // were confounded. Shuffle first, then let every index-keyed structure be
    // built on the order that will actually be solved.
    // ====================================================================
    // EXPERIMENT LEVER: permute constraint order (LOGOSPHERE_PHYS_SHUFFLE)
    // ====================================================================
    // Sequential impulse is Gauss-Seidel: every row is applied against
    // velocities the rows before it already changed, so ORDER is part of the
    // computation. Whether it is part of the ANSWER is the open question S19
    // tried and failed to settle, having closed parallelism, islands, SoA and
    // sorting on the strength of it.
    //
    // The only honest way to ask is to change nothing but the order and see
    // whether the residual moves. Deterministic on the seed, so a run repeats
    // exactly and an A-vs-A control is possible; seed 0 (the default) does not
    // shuffle and does not touch the array.
    //
    // NOT AN OPTIMISATION AND NOT A FIX. This exists to be measured with and
    // then turned off. Reordering as a shipped change would need the answer
    // this lever is for.
    if (const uint32_t seed = ::logosphere::telemetry::constraint_shuffle_seed()) {
        // xorshift32, inline and self-contained: the experiment must not depend
        // on a library RNG whose sequence could change under us.
        uint32_t s = seed;
        auto next = [&s]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
        for (size_t i = constraints.size(); i > 1; --i) {
            std::swap(constraints[i - 1], constraints[next() % i]);
        }
        PHYS_TRACE(::logosphere::phystrace::Solve, "shuffle", (int)seed,
                   (int)constraints.size(), "constraint_order_permuted",
                   (double)seed, (double)constraints.size());
    }

    // ========================================================================
    // V4.3: WARM STARTING - Apply cached impulses from previous frame
    //
    // Resting contacts have stable equilibrium impulse. Start there instead
    // of rediscovering from scratch (which causes overshoot → oscillation).
    // ========================================================================
    static int warm_debug_frame = 0;
    warm_debug_frame++;
    int warm_hits = 0, warm_misses = 0;
    int turtle_hits = 0, turtle_misses = 0;
    float total_cached_applied = 0.0f;

    // Track warm-started impulses for cache storage later
    // Key: constraint index, Value: impulse applied to velocity
    std::unordered_map<size_t, float> warm_started_impulses;

    // Track which normalized keys we've already applied this frame (avoid double-apply)
    // RCA (owner: prove it, no guesses): this set holds HASHES, and the
    // dedup below compares hashes, not keys. Two DIFFERENT contact pairs
    // that collide in the hash therefore silently lose one constraint —
    // and which pairs collide depends on particle INDICES, which is
    // exactly what adding one unrelated body shifts. Instrumented to
    // count true duplicates against false ones.
    std::unordered_set<size_t> applied_keys;
    std::unordered_map<size_t, PhysicsV4::ContactKey> applied_key_of;
    static const bool dedup_dbg = std::getenv("DEDUP_DEBUG") != nullptr;
    size_t dup_true = 0, dup_false = 0;

    if (ENABLE_WARM_STARTING) {
        for (size_t ci = 0; ci < constraints.size(); ++ci) {
            Constraint& c = constraints[ci];

            // Only warm start CONTACTS (turtle and box-box contacts)
            // Gluons are persistent constraints - they don't need warm starting
            if (c.is_angular) {
                // CANARY_DEBUG: Log skipped angular
                if (canary_active && ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " SKIP_ANG] P" << c.body_a << "<->P" << c.body_b << std::endl;
                }
                continue;
            }
            if (!c.is_contact && !c.is_turtle_contact) {
                // CANARY_DEBUG: Log skipped gluon
                if (canary_active && ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " SKIP_GLUON] P" << c.body_a << "<->P" << c.body_b << std::endl;
                }
                continue;  // Skip gluons
            }

            // Build normalized ContactKey (smaller ID first)
            ContactKey key;
            if (c.is_turtle_contact) {
                key.particle_a = c.body_a;
                key.particle_b = c.body_a;  // Turtle contact uses same ID for both
                key.contact_type = 0;
            } else {
                // Normalize: smaller ID first, canonicalize Jacobian sign
                bool swapped = c.body_a > c.body_b;
                key.particle_a = std::min(c.body_a, c.body_b);
                key.particle_b = std::max(c.body_a, c.body_b);

                // Canonical Jacobian (flip if swapped)
                float jx_can = swapped ? -c.jx : c.jx;
                float jy_can = swapped ? -c.jy : c.jy;
                float jz_can = swapped ? -c.jz : c.jz;

                // Contact type from canonical Jacobian axis AND sign
                // 0=Turtle, 1=+X, 2=-X, 3=+Y, 4=-Y, 5=+Z, 6=-Z
                if (std::abs(jz_can) > 0.5f) {
                    key.contact_type = (jz_can > 0) ? 5 : 6;
                } else if (std::abs(jx_can) > 0.5f) {
                    key.contact_type = (jx_can > 0) ? 1 : 2;
                } else {
                    key.contact_type = (jy_can > 0) ? 3 : 4;
                }
            }

            // Check if we already applied this key (avoid double-apply for mirror constraints)
            size_t key_hash = PhysicsV4::ContactKeyHash{}(key);

            if (dedup_dbg && applied_keys.count(key_hash) > 0) {
                const auto& prev = applied_key_of[key_hash];
                const bool same = prev.particle_a == key.particle_a &&
                                  prev.particle_b == key.particle_b &&
                                  prev.contact_type == key.contact_type;
                if (same) dup_true++; else dup_false++;
            }
            if (applied_keys.count(key_hash) > 0) {
                // CANARY_DEBUG: Log skipped due to duplicate key
                if (canary_active && ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " SKIP_DUP] P" << c.body_a << "<->P" << c.body_b
                              << " key=(" << key.particle_a << "," << key.particle_b << "," << key.contact_type << ")"
                              << " hash=" << key_hash << std::endl;
                }
                continue;
            }
            applied_keys.insert(key_hash);
            if (dedup_dbg) applied_key_of[key_hash] = key;

            // Look up cached impulse
            auto it = cached_impulses_.find(key);
            if (it != cached_impulses_.end()) {
                warm_hits++;
                if (c.is_turtle_contact) turtle_hits++;
                float cached = it->second.first;  // (impulse, last_frame) pair
                total_cached_applied += std::abs(cached);

                // V4.3 CORRECT: Apply cached impulse to velocity (with capping)
                //
                // This changes v_rel so solver sees the constraint nearly satisfied.
                // Cap to prevent over-correction when particle is moving fast.
                //
                // Example (particle at rest):
                //   vz = -0.163 (from gravity)
                //   cached = 7.35 N·s (equilibrium)
                //   Apply 7.35 → vz becomes ~0
                //   Solver sees v_rel ≈ 0, applies tiny delta
                //   Store: 7.35 + tiny ≈ 7.35 (stable)

                Particle& pa = particles[c.body_a];
                // V4.13: Check is_at_rest like solver does (at_rest = infinite mass)
                // Also skip DYNAMICS-owned particles - dynamics controls their velocity
                float inv_ma = (pa.is_at_rest || pa.solver_mode == ParticleSolverMode::KINEMATIC) ? 0.0f : (pa.GetMass() > 0.0f ? 1.0f / pa.GetMass() : 0.0f);
                float mass_a = pa.GetMass();

                // Compute relative velocity along Jacobian
                float rel_vel = pa.vx * c.jx + pa.vy * c.jy + pa.vz * c.jz;
                if (!c.is_turtle_contact) {
                    Particle& pb = particles[c.body_b];
                    rel_vel -= (pb.vx * c.jx + pb.vy * c.jy + pb.vz * c.jz);
                }

                // NO CAPPING - apply full cached impulse
                // Multiple contacts balance each other:
                //   Turtle pushes UP (supports total weight above)
                //   Box pushes DOWN (transfers weight from particle above)
                // These are meant to cancel. Capping prevents that!
                float warm_impulse = cached;

                // Apply warm impulse to velocity
                pa.vx += c.jx * warm_impulse * inv_ma;
                pa.vy += c.jy * warm_impulse * inv_ma;
                pa.vz += c.jz * warm_impulse * inv_ma;

                if (!c.is_turtle_contact) {
                    Particle& pb = particles[c.body_b];
                    // V4.13: Check is_at_rest like solver does (at_rest = infinite mass)
                    // Also skip DYNAMICS-owned particles - dynamics controls their velocity
                    float inv_mb = (pb.is_at_rest || pb.solver_mode == ParticleSolverMode::KINEMATIC) ? 0.0f : (pb.GetMass() > 0.0f ? 1.0f / pb.GetMass() : 0.0f);
                    pb.vx -= c.jx * warm_impulse * inv_mb;
                    pb.vy -= c.jy * warm_impulse * inv_mb;
                    pb.vz -= c.jz * warm_impulse * inv_mb;
                }

                // CANARY_DEBUG: Log warm start impulse applied to canary
                if (canary_active && ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    float dvz_a = c.jz * warm_impulse * inv_ma;
                    float dvz_b = c.is_turtle_contact ? 0.0f : (-c.jz * warm_impulse * (particles[c.body_b].is_at_rest ? 0.0f : (1.0f / particles[c.body_b].GetMass())));
                    std::cout << "[CANARY F" << phys_frame << " WARM] P" << c.body_a << "<->P" << c.body_b
                              << " impulse=" << warm_impulse
                              << " j=(" << c.jx << "," << c.jy << "," << c.jz << ")"
                              << " dvz_a=" << dvz_a << " dvz_b=" << dvz_b
                              << " vz_after=" << particles[canary_pid].vz
                              << (c.is_turtle_contact ? " TURTLE" : " BOX") << std::endl;
                }

                // Track for cache storage later
                warm_started_impulses[ci] = warm_impulse;

                // Set accumulated_impulse so friction sees the normal force
                c.accumulated_impulse = warm_impulse;
            } else {
                warm_misses++;
                if (c.is_turtle_contact) turtle_misses++;

                // CANARY_DEBUG: Log cache miss for canary particle
                if (canary_active && ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " MISS] P" << c.body_a << "<->P" << c.body_b
                              << " key=(" << key.particle_a << "," << key.particle_b << "," << key.contact_type << ")"
                              << " j=(" << c.jx << "," << c.jy << "," << c.jz << ")"
                              << (c.is_turtle_contact ? " TURTLE" : " BOX") << std::endl;
                }
            }
        }
    }

    // ========================================================================
    // CONTACT COMPOSITION (internal edge resolution)
    // ========================================================================
    // When a dynamic particle contacts multiple adjacent static bodies
    // (e.g., foot straddling two floor tiles), the individual SAT normals
    // can disagree (one picks +Y, another picks -Y). This is a data-structure
    // artifact: the boundary between adjacent coplanar statics doesn't exist
    // physically. Compose the normals: sum the contact-to-particle vectors,
    // normalize, and use that as the effective normal for all contacts in
    // the group. Pure geometry, gravity-independent.
    // ========================================================================
    {
        // Group constraint indices by dynamic particle
        std::unordered_map<size_t, std::vector<size_t>> dyn_to_constraints;
        for (size_t ci = 0; ci < constraints.size(); ++ci) {
            const Constraint& c = constraints[ci];
            if (!c.is_contact || c.is_turtle_contact) continue;
            const Particle& pa = particles[c.body_a];
            const Particle& pb = particles[c.body_b];
            // Identify the dynamic particle (the one NOT at rest)
            size_t dyn_id;
            if (!pa.is_at_rest && pb.is_at_rest) dyn_id = c.body_a;
            else if (pa.is_at_rest && !pb.is_at_rest) dyn_id = c.body_b;
            else continue;  // Both dynamic or both static, no composition
            dyn_to_constraints[dyn_id].push_back(ci);
        }

        for (auto& [dyn_id, cidx_list] : dyn_to_constraints) {
            if (cidx_list.size() < 2) continue;  // Need 2+ contacts to compose

            const Particle& pd = particles[dyn_id];

            // Check if static bodies in this group are adjacent (AABBs touch).
            // Collect static body IDs and their AABBs.
            std::vector<size_t> static_ids;
            for (size_t ci : cidx_list) {
                const Constraint& c = constraints[ci];
                size_t static_id = (c.body_a == dyn_id) ? c.body_b : c.body_a;
                static_ids.push_back(static_id);
            }

            // Adjacency check: for any two statics, their AABBs must touch
            // (overlap on at least one axis, touch on another).
            bool has_adjacent_pair = false;
            for (size_t i = 0; i < static_ids.size() && !has_adjacent_pair; ++i) {
                const Particle& pi = particles[static_ids[i]];
                float ia_min_x = pi.x - pi.width * 0.5f, ia_max_x = pi.x + pi.width * 0.5f;
                float ia_min_y = pi.y - pi.height * 0.5f, ia_max_y = pi.y + pi.height * 0.5f;
                float ia_min_z = pi.z - pi.thickness * 0.5f, ia_max_z = pi.z + pi.thickness * 0.5f;
                for (size_t j = i + 1; j < static_ids.size(); ++j) {
                    const Particle& pj = particles[static_ids[j]];
                    float jb_min_x = pj.x - pj.width * 0.5f, jb_max_x = pj.x + pj.width * 0.5f;
                    float jb_min_y = pj.y - pj.height * 0.5f, jb_max_y = pj.y + pj.height * 0.5f;
                    float jb_min_z = pj.z - pj.thickness * 0.5f, jb_max_z = pj.z + pj.thickness * 0.5f;
                    // Adjacent if AABBs touch or overlap (with small epsilon)
                    constexpr float ADJ_EPS = 0.01f;  // 10mm tolerance
                    bool x_touch = (ia_max_x >= jb_min_x - ADJ_EPS) && (jb_max_x >= ia_min_x - ADJ_EPS);
                    bool y_touch = (ia_max_y >= jb_min_y - ADJ_EPS) && (jb_max_y >= ia_min_y - ADJ_EPS);
                    bool z_touch = (ia_max_z >= jb_min_z - ADJ_EPS) && (jb_max_z >= ia_min_z - ADJ_EPS);
                    if (x_touch && y_touch && z_touch) {
                        has_adjacent_pair = true;
                        break;
                    }
                }
            }

            if (!has_adjacent_pair) continue;

            // Gather normalized normals pointing from static to dynamic.
            // Only compose if there's actual opposition (two normals with
            // conflicting signs on the same axis). Otherwise the SAT picked
            // consistently and no composition is needed.
            std::vector<std::array<float, 3>> normals_out;
            normals_out.reserve(cidx_list.size());
            for (size_t ci : cidx_list) {
                const Constraint& c = constraints[ci];
                const Particle& pa = particles[c.body_a];
                float sign = (pa.is_at_rest) ? 1.0f : -1.0f;
                normals_out.push_back({c.jx * sign, c.jy * sign, c.jz * sign});
            }

            // Detect opposition: on any axis, are there both positive and
            // negative signed components?
            bool has_opposition = false;
            for (int axis = 0; axis < 3; axis++) {
                bool has_pos = false, has_neg = false;
                for (const auto& n : normals_out) {
                    if (n[axis] > 0.3f) has_pos = true;
                    if (n[axis] < -0.3f) has_neg = true;
                }
                if (has_pos && has_neg) {
                    has_opposition = true;
                    break;
                }
            }

            if (!has_opposition) continue;  // No conflict, leave normals alone

            float sum_x = 0, sum_y = 0, sum_z = 0;
            for (const auto& n : normals_out) {
                sum_x += n[0];
                sum_y += n[1];
                sum_z += n[2];
            }
            float len = std::sqrt(sum_x*sum_x + sum_y*sum_y + sum_z*sum_z);
            if (len < 0.001f) continue;  // Degenerate, skip

            float nx = sum_x / len;
            float ny = sum_y / len;
            float nz = sum_z / len;

            // Replace each contact's normal with the composed direction.
            for (size_t ci : cidx_list) {
                Constraint& c = constraints[ci];
                size_t static_id = (c.body_a == dyn_id) ? c.body_b : c.body_a;
                if (c.body_a == dyn_id) {
                    c.jx = -nx;
                    c.jy = -ny;
                    c.jz = -nz;
                } else {
                    c.jx = nx;
                    c.jy = ny;
                    c.jz = nz;
                }
                // Also update collision_events_ so telemetry reflects actual
                // solver-used normal (composed, not pre-composition SAT).
                for (auto& evt : collision_events_) {
                    if ((evt.particle_a == dyn_id && evt.particle_b == static_id) ||
                        (evt.particle_b == dyn_id && evt.particle_a == static_id)) {
                        // Normal should point from particle_a to particle_b
                        float sign = (evt.particle_a == dyn_id) ? -1.0f : 1.0f;
                        evt.normal_x = nx * sign;
                        evt.normal_y = ny * sign;
                        evt.normal_z = nz * sign;
                        break;
                    }
                }
            }
        }
    }

    // AUTHORITY MEASUREMENT (env AUTHORITY_DEBUG=<particle id>): who owns
    // this body's orientation? Sum the angular impulse delivered by ANGULAR
    // DRIVE rows against the torque impulse delivered by LINEAR rows via
    // their anchor lever arms (r x J). Same units, same substep, same body.
    static const char* auth_env = std::getenv("AUTHORITY_DEBUG");
    static const int auth_id = auth_env ? std::atoi(auth_env) : -1;
    double auth_angular = 0.0, auth_linear = 0.0;

    bool solve_exit_recorded = false;   // which door the iteration loop took

    // Rows entering the solve, counted ONCE. iterations x rows is the true
    // solver work unit; counting rows per iteration would conflate the two.
    LOGO_COUNT_N(::logosphere::telemetry::Counter::PhysSolverRows, constraints.size());

    for (int iter = 0; iter < SOLVER_ITERATIONS; ++iter) {
        ::logosphere::phystrace::set_iteration(iter);
        float max_impulse_this_iter = 0.0f;
        max_dv_this_iter = 0.0f;
        actual_iterations = iter + 1;

        for (size_t ci = 0; ci < constraints.size(); ++ci) {
            Constraint& c = constraints[ci];

            Particle& pa = particles[c.body_a];
            Particle& pb = particles[c.body_b];

            // =================================================================
            // V4.2: ANGULAR CONSTRAINT SOLVING
            // =================================================================
            // Angular constraints couple omega_z (angular velocity) between bodies.
            // Solved same way as linear: compute relative velocity, apply impulse.
            //
            // ω_rel = ω_a - ω_b (matching linear convention: pa - pb)
            // impulse = -(ω_rel - bias) × effective_inertia
            // ω_a += impulse / I_a, ω_b -= impulse / I_b
            // =================================================================
            if (c.is_angular) {
                // Vector-axis path (rotational-DOF Stage 4): a single
                // constraint row whose Jacobian is the unit axis of the
                // Rodrigues error. omega_rel along the axis = scalar;
                // impulse projects back onto the axis. Cleanly handles
                // composed rotations without the small-angle approximation
                // that the axis_idx path's three independent rows accrue.
                bool vec_path = (c.angular_axis_vec_len > 0.0f);
                float ax_x = 0.0f, ax_y = 0.0f, ax_z = 0.0f;
                uint8_t ax_idx = c.angular_axis_idx;
                if (vec_path) {
                    ax_x = c.angular_axis_x;
                    ax_y = c.angular_axis_y;
                    ax_z = c.angular_axis_z;
                } else {
                    // axis_idx legacy path: unit vector aligned with the
                    // indexed world axis. Makes the math below unified.
                    ax_x = (ax_idx == 0) ? 1.0f : 0.0f;
                    ax_y = (ax_idx == 1) ? 1.0f : 0.0f;
                    ax_z = (ax_idx == 2) ? 1.0f : 0.0f;
                }

                // Relative angular velocity projected onto the axis.
                float omega_rel = (pa.omega_x - pb.omega_x) * ax_x
                                + (pa.omega_y - pb.omega_y) * ax_y
                                + (pa.omega_z - pb.omega_z) * ax_z;

                // Compute angular impulse to satisfy constraint
                // Target: omega_rel_new = angular_bias (pushes back to limit boundary)
                float angular_impulse = -(omega_rel - c.angular_bias) * c.effective_inertia;

                // Clamp accumulated angular impulse
                float old_ang = c.accumulated_angular_impulse;
                const bool auth_hit_ang = auth_id >= 0 &&
                    ((int)c.body_a == auth_id || (int)c.body_b == auth_id);
                c.accumulated_angular_impulse = std::max(c.min_angular_impulse,
                                        std::min(c.max_angular_impulse, old_ang + angular_impulse));
                angular_impulse = c.accumulated_angular_impulse - old_ang;
                if (auth_hit_ang) {
                    const float Iq = particles[(size_t)auth_id].GetMomentOfInertia();
                    const float sgn = ((int)c.body_a == auth_id) ? 1.0f : -1.0f;
                    if (Iq > 0.0f) auth_angular += sgn * angular_impulse * ax_x / Iq;
                }
                max_impulse_this_iter = std::max(max_impulse_this_iter, std::abs(angular_impulse));

                // Apply angular impulse as a vector along the axis.
                // KINEMATIC-mode particles: external writer controls
                // rotation, so they absorb no impulse (same principle as
                // inv_mass=0 for KINEMATIC on the linear side).
                // THE PIVOT LAW is DIAGNOSED but not yet shippable here: see
                // the pivot_inertia_* comment in physics_solver.h. Applying
                // it through independent scalar rows (this formulation) broke
                // the cancellation but went unstable (blade tore, 99.6 m/s):
                // the reciprocal couplings — force-at-anchor makes torque,
                // torque-at-anchor makes motion — need ONE row carrying a
                // full Jacobian, not two rows correcting each other.
                // Apply with the SAME inertia the row was sized with, about
                // the same axis. Sizing and applying with different numbers
                // is how a row over- or under-corrects by a constant factor.
                float I_a = pa.GetInertiaAboutAxis(ax_x, ax_y, ax_z);
                float I_b = pb.GetInertiaAboutAxis(ax_x, ax_y, ax_z);

                if (I_a > 0.0f && pa.solver_mode != ParticleSolverMode::KINEMATIC) {
                    float inv = 1.0f / I_a;
                    pa.omega_x += angular_impulse * ax_x * inv;
                    pa.omega_y += angular_impulse * ax_y * inv;
                    pa.omega_z += angular_impulse * ax_z * inv;
                }
                if (!c.is_turtle_contact && I_b > 0.0f && pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                    float inv = 1.0f / I_b;
                    pb.omega_x -= angular_impulse * ax_x * inv;
                    pb.omega_y -= angular_impulse * ax_y * inv;
                    pb.omega_z -= angular_impulse * ax_z * inv;
                }

                continue;  // Angular constraints don't have linear/friction components
            }

            // =================================================================
            // LINEAR CONSTRAINT SOLVING (existing V4.1 code)
            // =================================================================

            // ================================================================
            // V4.3 FIX: Correct v_rel for Turtle contacts
            // ================================================================
            // For Turtle contacts, body_b = body_a (self-reference sentinel).
            // This caused v_rel = pa.vz - pb.vz = vz - vz = 0 ALWAYS!
            //
            // The bug: With v_rel=0, impulse = -(0 - bias) × mass = bias × mass
            // Each of 16 solver iterations added bias to vz, causing massive
            // overcorrection and growing oscillation instead of convergence.
            //
            // The fix: Turtle is stationary (vb = 0), so for Turtle contacts:
            //   v_rel = pa.vz - 0 = pa.vz (particle velocity vs static floor)
            //
            // This allows the solver to correctly detect:
            //   - Approaching (v_rel < 0): Apply upward impulse to stop
            //   - Separating (v_rel > 0): No impulse needed
            //   - At rest (v_rel ≈ 0): Minimal correction
            //
            // Discovered via test_turtle_single_particle test showing
            // growing oscillation from z=0.25 to z=0.63 over 60 frames.
            // ================================================================
            float v_rel;
            if (c.is_turtle_contact) {
                // Turtle is stationary - compare particle velocity to zero
                v_rel = c.jx * pa.vx + c.jy * pa.vy + c.jz * pa.vz;
            } else {
                // Normal particle-particle: relative velocity
                v_rel = c.jx * (pa.vx - pb.vx)
                      + c.jy * (pa.vy - pb.vy)
                      + c.jz * (pa.vz - pb.vz);
                // Anchor rows measure velocity AT THE ANCHOR: add omega x r.
                // Without this the row corrects centre velocity while torque
                // spins the anchor ever faster — the 15 m fling of rung 3's
                // first torque attempt.
                if (c.apply_anchor_torque) {
                    if (pa.is_quat_driven && pa.owner == ParticleOwner::PHYSICS) {
                        v_rel += c.jx * (pa.omega_y * c.anchor_raz - pa.omega_z * c.anchor_ray)
                               + c.jy * (pa.omega_z * c.anchor_rax - pa.omega_x * c.anchor_raz)
                               + c.jz * (pa.omega_x * c.anchor_ray - pa.omega_y * c.anchor_rax);
                    }
                    if (pb.is_quat_driven && pb.owner == ParticleOwner::PHYSICS) {
                        v_rel -= c.jx * (pb.omega_y * c.anchor_rbz - pb.omega_z * c.anchor_rby)
                               + c.jy * (pb.omega_z * c.anchor_rbx - pb.omega_x * c.anchor_rbz)
                               + c.jz * (pb.omega_x * c.anchor_rby - pb.omega_y * c.anchor_rbx);
                    }
                }
            }

            // Compute impulse to satisfy constraint
            // SPLIT IMPULSE (V4.4): For TURTLE contacts, use pure velocity (no bias)
            // Position correction happens via enforce_turtle_boundary()
            // This prevents Baumgarte energy injection that causes oscillation
            // NOTE: Box contacts still use bias - no separate position correction for them yet
            // SPLIT IMPULSE, universal. NO row carries its position bias into
            // the velocity solve. Position error is geometry and is repaired by
            // the position pass below, whose velocity is discarded.
            //
            // This used to read `&& c.is_turtle_contact`, so gluon and box rows
            // injected their Baumgarte term straight into momentum. Measured on
            // Eden P3946: at iteration 0 the row fired dvz=+3.90 with v_rel
            // already at -0.01 — satisfied at the velocity level, impulsed
            // anyway — then converged to v_rel = -4.00, which is
            // GLUON_MAX_BIAS_VELOCITY exactly. Velocity carries across frames,
            // so an error that cannot close adds the cap every frame forever:
            // vz 3.73 -> 6.62 -> 12.17 -> 20.07 -> 30.98 -> 44.94 -> 60.75.
            // Capping the bias bounds the per-frame dose, not the total.
            float effective_bias = c.bias;
            if (ENABLE_SPLIT_IMPULSE) {
                // NOT every bias is position repair, and the difference is the
                // whole subtlety here.
                //
                // A contact row with a NEGATIVE bias is a speculative contact:
                // the bodies have a GAP, and the bias is the approach speed
                // they are allowed to close it at. That is a velocity
                // constraint, not geometry, and it must stay in the velocity
                // solve. Zeroing it turns the row into a hard stop at whatever
                // gap it was built with — measured as an 80 mm cushion under a
                // dropped box in test_physics_battery scenario 2, which rested
                // at 0.680 m instead of 0.600 with PEAK pen 0.00000, never
                // having touched anything.
                //
                // A contact row with a POSITIVE bias is penetration being
                // pushed out: real position repair, and it goes to the
                // position pass. A gluon row is position repair in either
                // direction (a bond can be stretched or compressed), so all of
                // it goes.
                const bool is_approach_limit = c.is_contact && c.bias < 0.0f;
                if (!is_approach_limit) effective_bias = 0.0f;
            }
            float impulse = -(v_rel - effective_bias) * c.effective_mass;

            // Clamp accumulated impulse
            float old_impulse = c.accumulated_impulse;
            c.accumulated_impulse = std::max(c.min_impulse,
                                    std::min(c.max_impulse, old_impulse + impulse));
            impulse = c.accumulated_impulse - old_impulse;
            max_impulse_this_iter = std::max(max_impulse_this_iter, std::abs(impulse));

            // LEVEL 5: which constraint is holding the solver's stopping test
            // hostage. The exit test is a global max over every row, so ONE row
            // that never shrinks pins the whole world. This prints the rows big
            // enough to be that one, with the terms that produced them.
            if (::logosphere::phystrace::at(::logosphere::phystrace::Constraint) &&
                std::abs(impulse) > 0.01f) {
                const Particle& ta = particles[c.body_a];
                const Particle& tb = particles[c.body_b];
                const bool kin_a = ta.solver_mode == ParticleSolverMode::KINEMATIC || ta.is_at_rest;
                const bool kin_b = c.is_turtle_contact ||
                                   tb.solver_mode == ParticleSolverMode::KINEMATIC || tb.is_at_rest;
                ::logosphere::phystrace::emit(
                    ::logosphere::phystrace::Constraint, "row_impulse",
                    (int)c.body_a, (int)c.body_b,
                    (kin_a && kin_b) ? "BOTH_IMMOVABLE"
                                     : (c.is_turtle_contact ? "turtle"
                                                            : (c.is_contact ? "contact" : "gluon")),
                    impulse, c.effective_mass, c.bias);
            }

            // Apply impulse along Jacobian direction (Vec3)
            // body_a pushed in +jacobian, body_b in -jacobian
            // Turtle contacts: inv_mb = 0 (infinite mass)
            // at_rest particles: treated as infinite mass (don't move until woken)
            // DYNAMICS particles: inv_mass = 0 (dynamics system handles collision response)
            float inv_ma = (pa.is_at_rest || pa.solver_mode == ParticleSolverMode::KINEMATIC) ? 0.0f : (pa.GetMass() > 0.0f ? 1.0f / pa.GetMass() : 0.0f);
            float inv_mb = c.is_turtle_contact ? 0.0f :
                           ((pb.is_at_rest || pb.solver_mode == ParticleSolverMode::KINEMATIC) ? 0.0f : (pb.GetMass() > 0.0f ? 1.0f / pb.GetMass() : 0.0f));

            // WHAT THAT IMPULSE ACTUALLY DOES. An impulse in N*s means nothing
            // on its own: 0.01 N*s is a shrug to a 112 kg branch and a shove to
            // a 0.107 kg leaf. Track the VELOCITY it imparts to the lighter
            // endpoint, which is the same physical quantity whatever the mass.
            max_dv_this_iter = std::max(max_dv_this_iter,
                                        std::abs(impulse) * std::max(inv_ma, inv_mb));

            if (energy_ledger_on() && c.effective_mass > 0.0f) {
                const double dke = double(impulse) * double(v_rel)
                                 + double(impulse) * double(impulse)
                                   / (2.0 * double(c.effective_mass));
                if (c.is_turtle_contact)   g_row_energy.turtle  += dke;
                else if (c.is_contact)     g_row_energy.contact += dke;
                else                       g_row_energy.gluon   += dke;
            }

            pa.vx += c.jx * impulse * inv_ma;
            pa.vy += c.jy * impulse * inv_ma;
            pa.vz += c.jz * impulse * inv_ma;

            if (!c.is_turtle_contact && !pb.is_at_rest && pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                pb.vx -= c.jx * impulse * inv_mb;
                pb.vy -= c.jy * impulse * inv_mb;
                pb.vz -= c.jz * impulse * inv_mb;
            }

            // ANCHOR TORQUE: the same impulse, applied at the anchor, spins
            // the body: omega += (r x J*impulse) / I. Opt-in per body via
            // is_quat_driven so nothing outside the rotational-DOF path
            // changes behavior.
            if (c.apply_anchor_torque && std::fabs(impulse) > 1e-12f) {
                const float fx = c.jx * impulse, fy = c.jy * impulse, fz = c.jz * impulse;
                if (auth_id >= 0) {
                    const float Iq = particles[(size_t)auth_id].GetMomentOfInertia();
                    if (Iq > 0.0f && (int)c.body_a == auth_id) {
                        auth_linear += (c.anchor_ray * fz - c.anchor_raz * fy) / Iq;
                    } else if (Iq > 0.0f && (int)c.body_b == auth_id) {
                        auth_linear -= (c.anchor_rby * fz - c.anchor_rbz * fy) / Iq;
                    }
                }
                // Rotation-from-physics is for PHYSICS-owNED bodies only:
                // animation's quat-driven bones belong to FK, and torquing
                // them spun 20 of the human's 30 particles at 10 rad/s
                // (owner: 'VERY WEIRD STUFF'). Same discriminator as the
                // gravity exemption.
                if (pa.is_quat_driven && pa.owner == ParticleOwner::PHYSICS &&
                    inv_ma > 0.0f) {
                    const float I = pa.GetMomentOfInertia();
                    if (I > 0.0f) {
                        const float inv = 1.0f / I;
                        pa.omega_x += (c.anchor_ray * fz - c.anchor_raz * fy) * inv;
                        pa.omega_y += (c.anchor_raz * fx - c.anchor_rax * fz) * inv;
                        pa.omega_z += (c.anchor_rax * fy - c.anchor_ray * fx) * inv;
                    }
                }
                if (!c.is_turtle_contact && pb.is_quat_driven &&
                    pb.owner == ParticleOwner::PHYSICS && inv_mb > 0.0f) {
                    const float I = pb.GetMomentOfInertia();
                    if (I > 0.0f) {
                        const float inv = 1.0f / I;
                        pb.omega_x -= (c.anchor_rby * fz - c.anchor_rbz * fy) * inv;
                        pb.omega_y -= (c.anchor_rbz * fx - c.anchor_rbx * fz) * inv;
                        pb.omega_z -= (c.anchor_rbx * fy - c.anchor_rby * fx) * inv;
                    }
                }
            }

            // CANARY_DEBUG: Log solver impulse applied to canary
            if (canary_active && std::abs(impulse) > 0.0001f &&
                ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                float dvz_canary = ((int)c.body_a == canary_pid) ? (c.jz * impulse * inv_ma) : (-c.jz * impulse * inv_mb);
                std::cout << "[CANARY F" << phys_frame << " SOLVE I" << iter << "] P" << c.body_a << "<->P" << c.body_b
                          << " v_rel=" << v_rel << " impulse=" << impulse
                          << " j=(" << c.jx << "," << c.jy << "," << c.jz << ")"
                          << " dvz=" << dvz_canary << " vz_now=" << particles[canary_pid].vz
                          << (c.is_turtle_contact ? " TURTLE" : " BOX") << std::endl;
            }

            // =================================================================
            // V4.1: FRICTION CONSTRAINTS (Coulomb model)
            // =================================================================
            // Only for contacts (not gluons). Tangents are perpendicular to normal.
            // Friction impulse clamped by: |f| <= mu * normal_impulse
            //
            // Per-particle friction: use minimum of both particles' coefficients.
            // If either particle has friction=0 (volitional entity), no friction.
            // See particle.h for when to set friction=0.
            // =================================================================
            float combined_friction = std::min(pa.friction, pb.friction);

            // DEBUG: Check why turtle friction might not apply
            if (c.is_turtle_contact && canary_active && (int)c.body_a == canary_pid && iter == 0) {
                std::cout << "[TURTLE_CHECK F" << phys_frame << "] accum=" << c.accumulated_impulse
                          << " friction=" << combined_friction << std::endl;
            }

            if ((c.is_contact || c.is_turtle_contact) && c.accumulated_impulse > 0.0f && combined_friction > 0.0f) {
                float friction_limit = combined_friction * c.accumulated_impulse;

                // Determine tangent directions from normal (Jacobian)
                // If normal is Z-axis dominant, tangents are X and Y
                // If normal is X-axis dominant, tangents are Y and Z
                // If normal is Y-axis dominant, tangents are X and Z
                float t1x, t1y, t1z;  // Tangent 1
                float t2x, t2y, t2z;  // Tangent 2

                if (std::abs(c.jz) > 0.5f) {
                    // Normal is mostly Z, tangents are X and Y
                    t1x = 1.0f; t1y = 0.0f; t1z = 0.0f;
                    t2x = 0.0f; t2y = 1.0f; t2z = 0.0f;
                } else if (std::abs(c.jx) > 0.5f) {
                    // Normal is mostly X, tangents are Y and Z
                    t1x = 0.0f; t1y = 1.0f; t1z = 0.0f;
                    t2x = 0.0f; t2y = 0.0f; t2z = 1.0f;
                } else {
                    // Normal is mostly Y, tangents are X and Z
                    t1x = 1.0f; t1y = 0.0f; t1z = 0.0f;
                    t2x = 0.0f; t2y = 0.0f; t2z = 1.0f;
                }

                // Friction constraint 1 (tangent 1)
                // Start with pa's velocity; subtract pb's only if pb is moving
                float v_rel_t1 = t1x * pa.vx + t1y * pa.vy + t1z * pa.vz;
                if (!c.is_turtle_contact && !pb.is_at_rest) {
                    v_rel_t1 -= t1x * pb.vx + t1y * pb.vy + t1z * pb.vz;
                }
                float friction_impulse_1 = -v_rel_t1 * c.effective_mass;
                float old_f1 = c.friction_impulse_t1;
                c.friction_impulse_t1 = std::max(-friction_limit,
                                        std::min(friction_limit, old_f1 + friction_impulse_1));
                friction_impulse_1 = c.friction_impulse_t1 - old_f1;

                pa.vx += t1x * friction_impulse_1 * inv_ma;
                pa.vy += t1y * friction_impulse_1 * inv_ma;
                pa.vz += t1z * friction_impulse_1 * inv_ma;

                // DEBUG: Log turtle friction (once per frame)
                if (c.is_turtle_contact && canary_active && (int)c.body_a == canary_pid && iter == 0) {
                    std::cout << "[TURTLE_FRIC F" << phys_frame << "] vx_after=" << pa.vx
                              << " v_rel=" << v_rel_t1 << " f_imp=" << friction_impulse_1
                              << " limit=" << friction_limit << " accum=" << c.accumulated_impulse
                              << std::endl;
                }

                if (!c.is_turtle_contact && !pb.is_at_rest && pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                    pb.vx -= t1x * friction_impulse_1 * inv_mb;
                    pb.vy -= t1y * friction_impulse_1 * inv_mb;
                    pb.vz -= t1z * friction_impulse_1 * inv_mb;
                }

                // Friction constraint 2 (tangent 2)
                // Start with pa's velocity; subtract pb's only if pb is moving
                float v_rel_t2 = t2x * pa.vx + t2y * pa.vy + t2z * pa.vz;
                if (!c.is_turtle_contact && !pb.is_at_rest) {
                    v_rel_t2 -= t2x * pb.vx + t2y * pb.vy + t2z * pb.vz;
                }
                float friction_impulse_2 = -v_rel_t2 * c.effective_mass;
                float old_f2 = c.friction_impulse_t2;
                c.friction_impulse_t2 = std::max(-friction_limit,
                                        std::min(friction_limit, old_f2 + friction_impulse_2));
                friction_impulse_2 = c.friction_impulse_t2 - old_f2;

                pa.vx += t2x * friction_impulse_2 * inv_ma;
                pa.vy += t2y * friction_impulse_2 * inv_ma;
                pa.vz += t2z * friction_impulse_2 * inv_ma;

                if (!c.is_turtle_contact && !pb.is_at_rest && pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                    pb.vx -= t2x * friction_impulse_2 * inv_mb;
                    pb.vy -= t2y * friction_impulse_2 * inv_mb;
                    pb.vz -= t2z * friction_impulse_2 * inv_mb;
                }
            }
        }

        // ================================================================
        // SMART EARLY STOPPING CHECK
        // ================================================================
        // 1. Absolute threshold: impulses are negligible
        // CONVERGED MEANS THE SAME THING FOR A LEAF AND A TRUNK.
        //
        // This exited on the impulse alone, at 0.01 N*s. That is an ABSOLUTE
        // impulse applied to every body in the world regardless of mass, and
        // what it permits scales as threshold/mass:
        //     0.01 / 0.107 kg leaf   = 0.093 m/s left unresolved
        //     0.01 / 111.7 kg branch = 0.00009 m/s     — 1046x stricter
        // Gravity per frame is 0.0817 m/s, so on a leaf the convergence
        // threshold was LARGER than the entire error it was asked to fix. The
        // solver exited at 3-9 iterations of 32 believing it was done, leaving
        // 1.2% of gravity every frame. Measured on P140: end-of-frame velocity
        // climbing -0.001, -0.002, -0.003, -0.004, -0.005, -0.006 in perfect
        // steps until the leaf had sunk metres and its bond tore at 3.9x.
        //
        // Now both must be small: the impulse AND the velocity it imparts.
        // Strictly tighter than before for every body — heavy ones keep the
        // solve they already got, light ones stop being abandoned early.
        constexpr float ABSOLUTE_THRESHOLD = 0.01f;   // N*s
        constexpr float VELOCITY_THRESHOLD = 0.001f;  // m/s, mass-independent
        if (max_impulse_this_iter < ABSOLUTE_THRESHOLD &&
            (std::getenv("PLATEAU_OLD") || max_dv_this_iter < VELOCITY_THRESHOLD)) {
            LOGO_COUNT(::logosphere::telemetry::Counter::PhysSolveConverged);
            solve_exit_recorded = true;
            PHYS_TRACE(::logosphere::phystrace::Solve, "solve_exit", (int)constraints.size(),
                       actual_iterations, "impulse_under_absolute_threshold",
                       max_impulse_this_iter, ABSOLUTE_THRESHOLD);
            break;
        }

        // 2. Convergence rate check (after minimum iterations)
        // A PLATEAU IS ONLY CONVERGENCE IF THE REMAINING ERROR IS SMALL.
        //
        // This stopped on the improvement RATE alone: under 5% per iteration
        // for 3 consecutive iterations and the solve ends, however much error
        // is left. A rate is dimensionless, so it says nothing about whether
        // the residual matters — and on a 0.107 kg leaf it exited at iteration
        // 9 of 32 leaving 1.2% of gravity unresolved EVERY frame. Measured on
        // P140: end-of-frame velocity climbing -0.001, -0.002, -0.003, -0.004
        // in perfect steps until the leaf sank metres and its bond tore.
        //
        // Slow progress on a large error is not a plateau, it is a hard
        // problem. Only stop when the remaining velocity error is negligible
        // in absolute terms, which is the same quantity for any mass.
        if (iter >= MIN_ITERATIONS && prev_max_impulse > 0.0f &&
            (std::getenv("PLATEAU_OLD") || max_dv_this_iter < VELOCITY_PLATEAU_FLOOR)) {
            float improvement = (prev_max_impulse - max_impulse_this_iter) / prev_max_impulse;
            if (improvement < MIN_IMPROVEMENT_RATE) {
                low_improvement_count++;
                if (low_improvement_count >= PLATEAU_CONFIRM) {
                    LOGO_COUNT(::logosphere::telemetry::Counter::PhysSolvePlateaued);
                    solve_exit_recorded = true;
                    // Records the improvement RATE, not just the fact of the
                    // exit. S20: low improvement at a steady state is what a
                    // CONVERGED solve looks like, so the number is the whole
                    // story and the label is misleading on its own.
                    PHYS_TRACE(::logosphere::phystrace::Solve, "solve_exit", (int)constraints.size(),
                               actual_iterations, "improvement_below_min_rate",
                               improvement, MIN_IMPROVEMENT_RATE, max_impulse_this_iter);
                    break;  // Plateaued - more iterations won't help
                }
            } else {
                low_improvement_count = 0;  // Reset on good progress
            }
        }
        prev_max_impulse = max_impulse_this_iter;
    }

    // Neither door taken: the full budget ran without converging or plateauing.
    if (!solve_exit_recorded) {
        LOGO_COUNT(::logosphere::telemetry::Counter::PhysSolveExhausted);
        PHYS_TRACE(::logosphere::phystrace::Solve, "solve_exit", (int)constraints.size(),
                   actual_iterations, "iteration_budget_exhausted",
                   (double)SOLVER_ITERATIONS);
    }
    LOGO_COUNT_N(::logosphere::telemetry::Counter::PhysSolverIterations,
                 (uint64_t)actual_iterations);

    // ====================================================================
    // CONSTRAINT RESIDUAL: is the answer good, not did the loop stop?
    // ====================================================================
    // The exit doors above measure per-iteration DELTA impulse, which says how
    // much the last sweep still changed things. A solve can stop changing while
    // still being wrong: the convergence ladder reports converged on iteration
    // 1 at every rung while a stack of 8 rests 1.3 mm lower than a stack of 1.
    // This pass asks the other question, by re-deriving each row's violation
    // from the velocities the solve actually left behind.
    //
    // Read-only. Costs about one iteration, so it is off unless asked for.
    if (::logosphere::telemetry::residual_enabled()) {
        ::logosphere::telemetry::SolveResidual r{0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0, 0};
        double sum_sq = 0.0, sum_pen_sq = 0.0;

        for (const Constraint& c : constraints) {
            // Angular rows are rad/s; mixing them into a m/s max would produce
            // a number in no units at all.
            if (c.is_angular) continue;

            // effective_mass == 0 means both bodies are immovable (S22). No
            // finite impulse changes anything, so this row cannot be satisfied
            // and its violation is not the solver's failure. Counted, never
            // folded into the max: doing that is what hid S22.
            if (c.effective_mass <= 0.0f) { r.rows_unsolvable++; continue; }

            const Particle& pa = particles[c.body_a];

            // Identical to the solve's own v_rel, deliberately. A residual
            // derived from a different formula measures a different constraint.
            float v_rel;
            if (c.is_turtle_contact) {
                v_rel = c.jx * pa.vx + c.jy * pa.vy + c.jz * pa.vz;
            } else {
                const Particle& pb = particles[c.body_b];
                v_rel = c.jx * (pa.vx - pb.vx)
                      + c.jy * (pa.vy - pb.vy)
                      + c.jz * (pa.vz - pb.vz);
            }
            float effective_bias = c.bias;
            if (ENABLE_SPLIT_IMPULSE && c.is_turtle_contact) effective_bias = 0.0f;

            // A contact can only push (min_impulse == 0), so a pair moving
            // apart faster than the bias asks is SATISFIED, not violated. A
            // gluon is an equality and is violated in both directions.
            const bool unilateral = (c.min_impulse == 0.0f);
            const double violation =
                unilateral ? std::max(0.0f, effective_bias - v_rel)
                           : std::fabs(v_rel - effective_bias);

            r.rows++;
            sum_sq += violation * violation;
            if (violation > ::logosphere::telemetry::kViolationEps) r.rows_violating++;
            if (violation > r.max_violation) r.max_violation = violation;
            if (std::fabs(effective_bias) > r.max_bias) r.max_bias = std::fabs(effective_bias);

            // The position-level violation: what a person would actually see.
            // Only overlap counts; a gap is not a violated contact.
            const double pen = (double)c.penetration;
            if (pen > 0.0) {
                sum_pen_sq += pen * pen;
                if (pen > ::logosphere::telemetry::kPenetrationEps) r.rows_penetrating++;
                // The worst PAIR tracks penetration, not velocity, because that
                // is the number this instrument exists to explain. Naming the
                // element is the whole S22 lesson: a max with no owner is what
                // hid one immovable corner pair pinning 14,000 bodies.
                if (pen > r.max_penetration) {
                    r.max_penetration = pen;
                    r.worst_a = (uint32_t)c.body_a;
                    r.worst_b = c.is_turtle_contact ? UINT32_MAX : (uint32_t)c.body_b;
                }
            }
        }

        if (r.rows > 0) {
            r.rms_violation   = std::sqrt(sum_sq / (double)r.rows);
            r.rms_penetration = std::sqrt(sum_pen_sq / (double)r.rows);
        }
        ::logosphere::telemetry::record_solve_residual(r);

        PHYS_TRACE(::logosphere::phystrace::Solve, "residual",
                   (int)r.worst_a, (int)r.worst_b, "post_solve_penetration",
                   r.max_penetration, r.max_violation, (double)r.rows_penetrating);
    }

    // CANARY_DEBUG: Summary after solver
    if (canary_active) {
        const Particle& cp = particles[canary_pid];
        std::cout << "[CANARY F" << phys_frame << " END] P" << canary_pid
                  << " vel=(" << cp.vx << "," << cp.vy << "," << cp.vz << ")"
                  << " iters=" << actual_iterations;
        // Count canary constraints
        int canary_contacts = 0, canary_turtle = 0;
        for (const Constraint& c : constraints) {
            if ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid) {
                if (c.is_turtle_contact) canary_turtle++;
                else if (c.is_contact) canary_contacts++;
            }
        }
        std::cout << " contacts=" << canary_contacts << " turtle=" << canary_turtle << std::endl;
    }

    // V4.13 DEBUG: Find particle with max upward velocity in early frames
    if (phys_frame <= 10) {
        float max_up_vz = 0.0f;
        int max_up_idx = -1;
        float max_vel = 0.0f;
        int max_vel_idx = -1;
        for (size_t i = 1; i < particles.size(); ++i) {
            const Particle& p = particles[i];
            if (p.vz > max_up_vz) {
                max_up_vz = p.vz;
                max_up_idx = i;
            }
            float vel = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            if (vel > max_vel) {
                max_vel = vel;
                max_vel_idx = i;
            }
        }
        if (max_up_vz > 0.001f) {
            const Particle& p = particles[max_up_idx];
            std::cout << "[MAX_UP F" << phys_frame << "] P" << max_up_idx
                      << " vz=" << max_up_vz << " z=" << p.z
                      << " at_rest=" << p.is_at_rest << std::endl;
        }
    }

    // ========================================================================
    // V4.3: WARM STARTING - Cache impulses for next frame
    // ========================================================================
    // Store accumulated impulses so next frame starts at equilibrium.
    // Also cleanup stale entries (contacts that no longer exist).
    // ========================================================================
    std::unordered_set<size_t> stored_keys;  // Avoid storing same key twice (second overwrites with 0)

    if (ENABLE_WARM_STARTING) {
        for (size_t ci = 0; ci < constraints.size(); ++ci) {
            const Constraint& c = constraints[ci];

            // Only cache CONTACTS - gluons don't need caching (they're persistent)
            if (c.is_angular) continue;
            if (!c.is_contact && !c.is_turtle_contact) continue;  // Skip gluons

            // Build normalized ContactKey (MUST match warm start apply exactly)
            ContactKey key;
            if (c.is_turtle_contact) {
                key.particle_a = c.body_a;
                key.particle_b = c.body_a;  // Turtle contact uses same ID for both
                key.contact_type = 0;
            } else {
                // Normalize: smaller ID first, canonicalize Jacobian sign
                bool swapped = c.body_a > c.body_b;
                key.particle_a = std::min(c.body_a, c.body_b);
                key.particle_b = std::max(c.body_a, c.body_b);

                // Canonical Jacobian (flip if swapped)
                float jx_can = swapped ? -c.jx : c.jx;
                float jy_can = swapped ? -c.jy : c.jy;
                float jz_can = swapped ? -c.jz : c.jz;

                // Contact type from canonical Jacobian axis AND sign
                // 0=Turtle, 1=+X, 2=-X, 3=+Y, 4=-Y, 5=+Z, 6=-Z
                if (std::abs(jz_can) > 0.5f) {
                    key.contact_type = (jz_can > 0) ? 5 : 6;
                } else if (std::abs(jx_can) > 0.5f) {
                    key.contact_type = (jx_can > 0) ? 1 : 2;
                } else {
                    key.contact_type = (jy_can > 0) ? 3 : 4;
                }
            }

            // Skip if we already stored this key (avoid second constraint overwriting with 0)
            size_t key_hash = PhysicsV4::ContactKeyHash{}(key);
            if (stored_keys.count(key_hash) > 0) continue;
            stored_keys.insert(key_hash);

            // SPLIT IMPULSE (V4.4): With split impulse, accumulated_impulse is already
            // pure velocity impulse (no bias component). No need to subtract.
            // Without split impulse: subtract bias to avoid caching position correction.
            // NOTE: Only turtle contacts use split impulse - box contacts still have bias
            // Split impulse is now universal, so accumulated_impulse holds pure
            // velocity impulse for every row and there is no bias to subtract.
            float velocity_impulse;
            if (ENABLE_SPLIT_IMPULSE) {
                velocity_impulse = c.accumulated_impulse;
            } else {
                float bias_impulse = c.bias * c.effective_mass;
                velocity_impulse = c.accumulated_impulse - bias_impulse;
                if (velocity_impulse < 0) velocity_impulse = 0;
            }

            // V4.6 FIX: Don't accumulate solver corrections into cache
            // The warm_part IS the equilibrium impulse. Solver corrections fix
            // small residuals each frame but shouldn't accumulate in cache.
            // See: Feynman analysis of +0.02 m/s drift in Frame 3
            float total_impulse;
            float warm_part = 0.0f;
            auto it = warm_started_impulses.find(ci);
            if (it != warm_started_impulses.end()) {
                warm_part = it->second;
                // Keep equilibrium value, don't add solver delta
                total_impulse = warm_part;
            } else {
                // First frame or cache miss - use solver's value
                total_impulse = velocity_impulse;
            }

            cached_impulses_[key] = {total_impulse, static_cast<int>(warm_debug_frame)};

        }

        // Aged cleanup - only delete entries unused for 60 frames (1 second)
        // This allows brief oscillations where contact lifts off and reforms
        constexpr int CACHE_AGE_LIMIT = 60;
        for (auto it = cached_impulses_.begin(); it != cached_impulses_.end(); ) {
            int last_used = it->second.second;  // (impulse, last_frame) pair
            int age = static_cast<int>(warm_debug_frame) - last_used;
            if (age > CACHE_AGE_LIMIT) {
                it = cached_impulses_.erase(it);
            } else {
                ++it;
            }
        }
    }


    // ========================================================================
    // POSITION PASS (split impulse)
    // ========================================================================
    // The velocity solve above ran with every bias zeroed, so the bodies now
    // carry only real momentum. The position error is still there, and this is
    // where it is repaired: the same rows are solved again against their bias,
    // but the resulting velocity goes into a PSEUDO velocity that moves
    // positions and is then thrown away. Nothing here reaches p.vx/vy/vz, so a
    // constraint that can never close its error can never accumulate energy.
    //
    // Turtle rows are skipped: enforce_turtle_boundary() already repairs those
    // geometrically, and correcting them twice would double the push.
    if (PhysicsV4::ENABLE_SPLIT_IMPULSE && !constraints.empty()) {
        std::vector<float> pvx(count, 0.0f), pvy(count, 0.0f), pvz(count, 0.0f);
        for (Constraint& c : constraints) c.accumulated_pseudo_impulse = 0.0f;

        // SLEEP IS NOT IMMOBILITY. Only KINEMATIC is immovable here.
        //
        // The velocity solve treats an at_rest body as infinite mass, and that
        // is right: sleep is an optimisation about MOMENTUM. Position repair is
        // geometry, and a sleeping body's overlap or stretched bond is just as
        // real as an awake one's. CLAUDE.md states the invariant directly —
        // "is_at_rest is a solver optimization, NOT immobility".
        //
        // Measured with SPLIT_DEBUG on the ratchet SLING scenario: the body
        // fell to the world floor, slept, and the position pass fired exactly
        // 4 times in 300 frames (each moving it the capped 0.0333 m) before
        // inverse mass went to zero and repair stopped for good. It finished
        // holding a 2.35 m bond error, 117x the 0.02 m wake threshold.
        auto inv_mass_of = [](const Particle& p) -> float {
            if (p.solver_mode == ParticleSolverMode::KINEMATIC) return 0.0f;
            const float m = p.GetMass();
            return (m > 0.0f) ? (1.0f / m) : 0.0f;
        };

        for (int iter = 0; iter < PhysicsV4::POSITION_ITERATIONS; ++iter) {
            for (Constraint& c : constraints) {
                if (c.is_turtle_contact) continue;
                if (c.bias == 0.0f) continue;
                // A CORRECTION WITH NO TOLERANCE NEVER TERMINATES.
                //
                // This pass corrected ANY non-zero position error, which
                // sounds rigorous and is the bug: below a certain size the
                // "error" is not geometry, it is the last bits of a float. It
                // then lifts the body a few hundred nanometres, recomputes a
                // very slightly smaller error, and lifts again — converging
                // toward zero asymptotically and never arriving.
                //
                // Measured on test_settling_wiggle, a scene AT REST with zero
                // kinetic energy and no constraint row firing: the pass fired
                // 1955 times, moving the same body 290 nm every substep, while
                // potential energy climbed 29424.7 -> 29424.9. Each lift is
                // real work against gravity that nothing pays for. That is the
                // Baumgarte ratchet I removed from velocity this morning,
                // reinstalled in position by me.
                //
                // SLOP is the engine's existing statement that this much
                // geometric error is not real — contact rows have always used
                // it, and the turtle boundary got it this afternoon for the
                // identical defect. Third place that needed the same number.
                const float bias_as_error = std::fabs(c.bias) * dt;
                if (bias_as_error < PhysicsV4::SLOP) continue;
                // Speculative approach limits stayed in the velocity solve;
                // they are not geometry to repair. Skipping them explicitly
                // rather than relying on the unilateral clamp to zero them.
                if (c.is_contact && c.bias < 0.0f) continue;
                if (c.body_a >= count || c.body_b >= count) continue;

                const float inv_ma = inv_mass_of(particles[c.body_a]);
                const float inv_mb = inv_mass_of(particles[c.body_b]);
                if (inv_ma == 0.0f && inv_mb == 0.0f) continue;

                const float pv_rel =
                    c.jx * (pvx[c.body_a] - pvx[c.body_b]) +
                    c.jy * (pvy[c.body_a] - pvy[c.body_b]) +
                    c.jz * (pvz[c.body_a] - pvz[c.body_b]);

                float imp = -(pv_rel - c.bias) * c.effective_mass;

                // Same clamp the velocity rows obey, so a non-penetration row
                // still cannot pull and a bond still cannot exceed its break
                // force while repairing.
                const float old = c.accumulated_pseudo_impulse;
                c.accumulated_pseudo_impulse =
                    std::max(c.min_impulse, std::min(c.max_impulse, old + imp));
                imp = c.accumulated_pseudo_impulse - old;

                pvx[c.body_a] += c.jx * imp * inv_ma;
                pvy[c.body_a] += c.jy * imp * inv_ma;
                pvz[c.body_a] += c.jz * imp * inv_ma;
                pvx[c.body_b] -= c.jx * imp * inv_mb;
                pvy[c.body_b] -= c.jy * imp * inv_mb;
                pvz[c.body_b] -= c.jz * imp * inv_mb;
            }
        }

        // Spend the pseudo velocity on positions, then drop it on the floor.
        // SPLIT_DEBUG=1 reports what the pass actually moved, per substep:
        // how many rows it corrected, the largest single displacement and who
        // received it. Position repair is invisible by construction (it leaves
        // no velocity behind), so without this there is no way to tell a pass
        // that did nothing from one that moved a body 80 mm.
        static const bool split_dbg = std::getenv("SPLIT_DEBUG") != nullptr;
        size_t moved = 0; float worst_dz = 0.0f; int worst_id = -1;
        for (size_t i = 0; i < count; ++i) {
            if (pvx[i] == 0.0f && pvy[i] == 0.0f && pvz[i] == 0.0f) continue;
            Particle& p = particles[i];
            if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;
            p.x += pvx[i] * dt;
            p.y += pvy[i] * dt;
            p.z += pvz[i] * dt;
            if (split_dbg) {
                moved++;
                if (std::fabs(pvz[i] * dt) > std::fabs(worst_dz)) {
                    worst_dz = pvz[i] * dt; worst_id = (int)i;
                }
            }
        }
        if (split_dbg && moved > 0) {
            size_t biased_rows = 0;
            for (const Constraint& c : constraints)
                if (!c.is_turtle_contact && c.bias != 0.0f) biased_rows++;
            std::cout << "[SPLIT F" << phys_frame << "] biased_rows=" << biased_rows
                      << " moved=" << moved
                      << " worst_dz=" << worst_dz << "m on P" << worst_id
                      << std::endl;
        }
    }

    auto t_after_solver = std::chrono::high_resolution_clock::now();

    // Slow-substep diagnostic (permanent, env-gated, zero cost unless
    // PHYS_SLOW_MS is set): phase breakdown + constraint/active counts
    // for any substep slower than the given milliseconds. This is the
    // probe that found the 2026-07-10 awake-anchor horde.
    {
        static float slow_ms_gate = -1.0f;
        if (slow_ms_gate < 0.0f) {
            const char* env = std::getenv("PHYS_SLOW_MS");
            slow_ms_gate = env ? (float)std::atof(env) : 1e9f;
        }
        auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        double total = ms(t_start, t_after_solver);
        if (total > slow_ms_gate) {
            std::cerr << "[PHYS_SLOW] total=" << total
                      << " hash=" << ms(t_start, t_after_gluon_hash)
                      << " turtle=" << ms(t_after_gluon_hash, t_after_turtle)
                      << " restcnt=" << ms(t_after_turtle, t_after_at_rest_count)
                      << " contacts=" << ms(t_after_at_rest_count, t_after_contacts)
                      << " gluons=" << ms(t_after_contacts, t_after_gluons)
                      << " solver=" << ms(t_after_gluons, t_after_solver)
                      << " nconstraints=" << constraints.size()
                      << " active=" << debug_active_particles
                      << std::endl;
        }
    }

    // ========================================================================
    // STATE TRACE: omega + orientation at the two moments that matter.
    if (auth_id >= 0 && auth_id < (int)particles.size()) {
        static int st_n = 0;
        if ((st_n++ % 240) == 0) {
            const Particle& t = particles[auth_id];
            printf("[STATE P%d post-solve]  omega(%.3f,%.3f,%.3f)  "
                   "euler(%.2f,%.2f,%.2f)  q(%.3f,%.3f,%.3f,%.3f) qd%d rest%d\n",
                   auth_id, t.omega_x, t.omega_y, t.omega_z,
                   t.rotation_x, t.rotation_y, t.rotation_z,
                   t.rotation_q.w, t.rotation_q.x, t.rotation_q.y, t.rotation_q.z,
                   (int)t.is_quat_driven, (int)t.is_at_rest);
        }
    }

    if (dedup_dbg && (dup_true || dup_false)) {
        static size_t tot_true = 0, tot_false = 0;
        tot_true += dup_true; tot_false += dup_false;
        static int dn = 0;
        if ((dn++ % 600) == 0)
            printf("[DEDUP] warm-start skips: %zu true duplicates, %zu FALSE "
                   "(different pair, same hash -> constraint silently dropped)\n",
                   tot_true, tot_false);
    }

    // AUTHORITY REPORT: the measurement that settles who owns orientation.
    if (auth_id >= 0 && (auth_angular > 0.0 || auth_linear > 0.0)) {
        static int auth_n = 0;
        if ((auth_n++ % 240) == 0) {
            const double tot = auth_angular + auth_linear;
            printf("[AUTHORITY P%d] SIGNED d(omega_x): angular-drive %+.4f  "
                   "linear-anchor %+.4f  NET %+.4f rad/s per solve  (I=%.2e)\n",
                   auth_id, auth_angular, auth_linear, auth_angular + auth_linear,
                   particles[(size_t)auth_id].GetMomentOfInertia());
            (void)tot;
        }
    }

    // GLUON BREAKING CHECK
    // ========================================================================
    /**
     * Each gluon has 3 constraints (X, Y, Z). Total force is:
     *   F_total = sqrt(Fx² + Fy² + Fz²)
     *
     * FIX A: Use stored constraint indices for O(1) lookup instead of O(c) scan
     * This reduces complexity from O(g×c) to O(g)
     */
    for (size_t gi_i = 0; gi_i < gluon_constraint_indices.size(); ++gi_i) {
        const auto& idx = gluon_constraint_indices[gi_i];
        // Look the gluon up by the slot it recorded for itself. The old code
        // indexed gluon_constraints_v2_ by POSITION IN THIS VECTOR, which is
        // only the same thing when nothing was ever skipped.
        if (idx.gluon_slot >= gluon_constraints_v2_.size()) continue;
        const auto& gluon = gluon_constraints_v2_[idx.gluon_slot];
        float impulse_x = constraints[idx.x_idx].accumulated_impulse;
        float impulse_y = constraints[idx.y_idx].accumulated_impulse;
        float impulse_z = constraints[idx.z_idx].accumulated_impulse;

        // IMPULSE MEMORY store-back (organic only, matching the apply).
        if (gluon->force_bounded() && !impulse_memory_off()) {
            const float DECAY = 0.98f;
            // Anti-windup (owner QA: the chain rang 5-8 swings): when the
            // solver's fresh impulse OPPOSES the carried load, the spring
            // has crossed rest and the memory is now driving the next
            // swing — bleed it fast instead of carrying it through.
            const float DECAY_REVERSED = 0.55f;
            // THE INTEGRAL'S AUTHORITY IS A MOMENTUM, NOT A FORCE.
            //
            // This cap used to be breaking_force * dt, which is a property of
            // how hard the bond is to SNAP and says nothing about what the
            // bodies it joins can absorb. On a 2 g grass blade that is roughly
            // 8 kN·s of stored impulse against 0.00238 kg of body: applying a
            // fraction of it launches the blade. Measured consequence, 3-body
            // chain at 15x mass ratio: a 1.2 m/s push came back as 7.4 m/s,
            // and 4 of 7 ratios either grew without bound or tore.
            //
            // An accumulated impulse is momentum. Bound it by the momentum the
            // LIGHTER endpoint can carry at the same speed the engine already
            // says a constraint may drive a correction. Then the integral can
            // never impart more than that speed to the body least able to take
            // it, whatever the bond's breaking force happens to be.
            //
            //   2 g blade   -> 0.0095 N·s, at most 4 m/s imparted
            //   5 kg arm    -> 20 N·s, ample authority for a driven joint
            //
            // One law, scaling with mass. No mass-ratio branch: the charter
            // rejects those, and this does not need one — the physics of
            // momentum already carries the scaling.
            const Particle& cap_a = particles[gluon->particle_a];
            const Particle& cap_b = particles[gluon->particle_b];
            const float m_a = cap_a.GetMass(), m_b = cap_b.GetMass();
            const float m_light = (m_a > 0.0f && m_b > 0.0f)
                                    ? std::fmin(m_a, m_b)
                                    : std::fmax(m_a, m_b);
            const float cap = m_light * PhysicsV4::GLUON_MAX_BIAS_VELOCITY;
            auto upd = [&](float warm_old, size_t row_idx, float acc) {
                const Constraint& rc = constraints[row_idx];
                // Under split impulse the row's accumulator is already pure
                // velocity impulse; subtracting the bias term here would
                // remove something that was never added.
                const float velocity_part =
                    PhysicsV4::ENABLE_SPLIT_IMPULSE
                        ? acc
                        : (acc - rc.bias * rc.effective_mass);
                const float d = (warm_old * velocity_part < 0.0f) ? DECAY_REVERSED : DECAY;
                return std::clamp(d * (warm_old + velocity_part), -cap, cap);
            };
            gluon->warm_ix = upd(gluon->warm_ix, idx.x_idx, impulse_x);
            gluon->warm_iy = upd(gluon->warm_iy, idx.y_idx, impulse_y);
            gluon->warm_iz = upd(gluon->warm_iz, idx.z_idx, impulse_z);
        }

        // Total force magnitude
        float total_impulse = std::sqrt(impulse_x * impulse_x
                                      + impulse_y * impulse_y
                                      + impulse_z * impulse_z);
        float force = total_impulse / dt;

        const Particle& pa = particles[gluon->particle_a];
        const Particle& pb = particles[gluon->particle_b];

        // Fiber tears from TENSION, not from viscous drag. For force-bounded
        // bonds the row impulse is dominated by the damper under any
        // sustained rub (grass: c=100 x 1 m/s = 100 N of drag against 58 N
        // of strength — every brush tore every bond, 3-frame fatigue only
        // delayed it). Structural load = stiffness x stretch. Welds (nails)
        // keep impulse-based breaking: that is their contract.
        float breaking = gluon->calculate_breaking_force(pa, pb);

        // Fiber has ONE tear law: elongation (the strain check below).
        // Impulse- or tension-derived force checks double-declare the same
        // physics at inconsistent thresholds (grass: 11.6 mm by tension vs
        // 100+ mm by strain) and the stricter one always fires under drag.
        // Welds (nails) keep impulse-based breaking: that is their contract.
        if (!gluon->force_bounded() && force >= breaking * 0.99f) {
            if (++gluon->force_over_frames >= 12) {
                mark_gluon_for_removal(gluon->id);
                continue;
            }
        } else {
            gluon->force_over_frames = 0;
        }

        // MECHANISM B2 (issue #47): tear by STRAIN. See max_strain_ratio().
        const float max_strain = gluon->max_strain_ratio();
        if (std::isfinite(max_strain)) {
            const float ddx = pb.x - pa.x, ddy = pb.y - pa.y, ddz = pb.z - pa.z;
            const float dist = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
            // Rest geometry: organic bonds carry it in target_distance
            // (center to center); rigid offset-anchored gluons (nails)
            // carry it in the attachment offsets with target_distance 0.
            // Take the larger, floored at 1 cm, or a nail's centers read
            // as a torn fiber and scenario 8 of the battery loses its box.
            // REST GEOMETRY MUST BE MEASURED IN THE SAME FRAME THE BOND IS.
            //
            // get_segment_length() subtracts offset_a and offset_b as raw
            // vectors, which silently assumes both bodies share an
            // orientation. They rarely do: a branch hangs off a trunk at an
            // angle, a grass blade leans. The offsets are stored in each
            // body's OWN local frame, so subtracting them unrotated measures
            // a quantity that exists in no frame at all.
            //
            // Measured on test_foliage_stays_attached, the very first tear:
            //   off_a=(0,0,1.29965)  thick_a=2.59929  -> exactly thick_a/2, correct
            //   off_b=(0.093,-0.799,-0.069) thick_b=1.61406 -> |off_b| = 0.80704,
            //                                exactly thick_b/2, also correct
            //   get_segment_length() = 1.587   true centre separation = 3.177
            // Both offsets are right. The subtraction is wrong, by a factor of
            // 2.0017, which lands a bond sitting exactly where it was built a
            // hair over the 2.0x tear ratio. 51 bonds tore that way with both
            // bodies stationary, and the canopy left: mean drift 27.5 m.
            //
            // Rotate each offset by its OWN body before differencing, the same
            // composition the solver uses to build attachment points a few
            // hundred lines up. Rest is then a function of current orientation,
            // which is correct: as a branch swings, the centre-to-centre
            // distance at zero strain genuinely changes.
            auto rot_local = [](const Particle& p, const Vec3& o,
                                float& wx, float& wy, float& wz) {
                const float cx = cosf(p.rotation_x), sx = sinf(p.rotation_x);
                const float cy = cosf(p.rotation_y), sy = sinf(p.rotation_y);
                const float cz = cosf(p.rotation_z), sz = sinf(p.rotation_z);
                const float y1 = o.y * cx - o.z * sx;
                const float z1 = o.y * sx + o.z * cx;
                const float x2 = o.x * cy + z1 * sy;
                const float z2 = -o.x * sy + z1 * cy;
                wx = x2 * cz - y1 * sz;
                wy = x2 * sz + y1 * cz;
                wz = z2;
            };
            float seg_len;
            if (gluon->rotate_offsets) {
                float ax, ay, az, bx, by, bz;
                rot_local(pa, gluon->offset_a, ax, ay, az);
                rot_local(pb, gluon->offset_b, bx, by, bz);
                const float sx = ax - bx, sy2 = ay - by, sz2 = az - bz;
                seg_len = std::sqrt(sx*sx + sy2*sy2 + sz2*sz2);
            } else {
                seg_len = gluon->get_segment_length();   // already world-space
            }
            const float rest = std::max({gluon->target_distance,
                                         seg_len, 0.01f});
            if (dist > max_strain * rest) {
                // TEAR_DEBUG=1: name every bond that snaps and the state that
                // snapped it. A tear is silent by construction — the bond is
                // simply gone next frame — so without this the only evidence is
                // a count going down and a blade 2.5 m from home.
                static const bool tear_dbg = std::getenv("TEAR_DEBUG") != nullptr;
                if (tear_dbg) {
                    std::cout << "[TEAR] P" << gluon->particle_a
                              << "<->P" << gluon->particle_b
                              << " dist=" << dist << " rest=" << rest
                              << " strain=" << (dist / rest) << "x (limit "
                              << max_strain << "x)"
                              // The rest for an offset-anchored bond is
                              // |offset_a - offset_b|, which SHOULD equal the
                              // centre-to-centre distance at rest. When it does
                              // not, the bond was born strained and the tear is
                              // a generation bug, not a dynamics one. Print both
                              // offsets and the bodies' Z extents so the
                              // arithmetic can be checked against the geometry.
                              << " | off_a=(" << gluon->offset_a.x << ","
                              << gluon->offset_a.y << "," << gluon->offset_a.z << ")"
                              << " off_b=(" << gluon->offset_b.x << ","
                              << gluon->offset_b.y << "," << gluon->offset_b.z << ")"
                              << " seglen=" << gluon->get_segment_length()
                              << " tgt=" << gluon->target_distance
                              << " thick_a=" << pa.thickness
                              << " thick_b=" << pb.thickness
                              << " | a: pos=(" << pa.x << "," << pa.y << "," << pa.z
                              << ") vel=(" << pa.vx << "," << pa.vy << "," << pa.vz
                              << ") m=" << pa.GetMass() << " mode=" << (int)pa.solver_mode
                              << " | b: pos=(" << pb.x << "," << pb.y << "," << pb.z
                              << ") vel=(" << pb.vx << "," << pb.vy << "," << pb.vz
                              << ") m=" << pb.GetMass() << " mode=" << (int)pb.solver_mode
                              << std::endl;
                }
                mark_gluon_for_removal(gluon->id);
            }
        }
    }

}

// ============================================================================
// PHASE 5: INTEGRATE POSITIONS
// ============================================================================
/**
 * Move particles based on their (now corrected) velocities.
 *
 * EXAMPLE:
 *   Particle after constraint solving: vz = 0 m/s (was stopped by floor)
 *   z before: 0.1m
 *   dt: 0.0167s
 *
 *   z after = 0.1 + 0 × 0.0167 = 0.1m (stays in place)
 */
void PhysicsSystem::integrate_positions(ParticleSystem::WriteView& particles, float dt) {
    const size_t count = particles.size();
    static int position_debug_frame = 0;
    position_debug_frame++;

    // V4.11: Build set of particles with gluon connections
    // Only these get structural damping. Free-falling particles get air drag only.
    std::unordered_set<size_t> particles_with_gluons;
    for (const auto& gluon : gluon_constraints_v2_) {
        particles_with_gluons.insert(gluon->particle_a);
        particles_with_gluons.insert(gluon->particle_b);
    }

    // V4.15: structural damping must not brake a structure's SHARED
    // motion — only internal oscillation (deviation from the cluster
    // mean). V4.11 exempted gluon-FREE particles, but bonded bodies
    // still had their absolute velocity damped: a gluon-bonded
    // boulder hit ~1 m/s terminal velocity in free fall while a loose
    // stone fell at 9.8 (the falling-boulder AT). Union-find the
    // gluon graph, record each cluster's mass-weighted mean velocity,
    // and damp only the deviation from it.
    std::unordered_map<size_t, size_t> uf_parent;
    auto uf_find = [&uf_parent](size_t a) {
        size_t root = a;
        while (true) {
            auto it = uf_parent.find(root);
            if (it == uf_parent.end() || it->second == root) break;
            root = it->second;
        }
        while (a != root) {   // path compression
            size_t next = uf_parent[a];
            uf_parent[a] = root;
            a = next;
        }
        return root;
    };
    for (const auto& gluon : gluon_constraints_v2_) {
        size_t ra = uf_find(gluon->particle_a);
        size_t rb = uf_find(gluon->particle_b);
        if (ra != rb) uf_parent[ra] = rb;
    }
    struct ClusterVel { float mvx = 0, mvy = 0, mvz = 0, m = 0; };
    std::unordered_map<size_t, ClusterVel> cluster_vel;
    for (size_t idx : particles_with_gluons) {
        if (idx >= count) continue;
        const auto& q = particles[idx];
        float m = q.GetMass();
        if (m <= 0.0f) continue;
        auto& c = cluster_vel[uf_find(idx)];
        c.mvx += m * q.vx;
        c.mvy += m * q.vy;
        c.mvz += m * q.vz;
        c.m += m;
    }

    // Writer-marks-BVH: this loop is the position authority for
    // solver-DYNAMIC particles, so it owns the BVH dirty flag for their
    // motion (the deleted ParticleSystem::update loop marked it as a
    // side effect, x/y only; humanoid locomotion self-marks for its
    // DYNAMICS-owned bodies). The shadow BVH doubles as the physics
    // broadphase and the locomotion ground probe.
    bool any_moved = false;

    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];

        // Skip DYNAMICS-owned particles - dynamics system handles their positions
        if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;

        // Debug: Log position/velocity for first 5 particles (disabled - enable for debugging)
        // if (position_debug_frame % 60 == 0 && i < 5) {
        //     std::cout << "[POS_DEBUG] P" << i
        //               << " pos=(" << p.x << "," << p.y << "," << p.z << ")"
        //               << " vel=(" << p.vx << "," << p.vy << "," << p.vz << ")"
        //               << std::endl;
        // }

        // ====================================================================
        // V4.14: VELOCITY CLAMP (Safety net for numerical stability)
        // ====================================================================
        // Clamp velocity to physically reasonable maximum (100 m/s ≈ 360 km/h)
        // Prevents numerical instabilities from cascading into explosions
        // Root cause should still be investigated, but this prevents NaN propagation
        {
            constexpr float MAX_VELOCITY = 100.0f;
            constexpr float MAX_VEL_SQ = MAX_VELOCITY * MAX_VELOCITY;
            float clamp_vel_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;
            if (clamp_vel_sq > MAX_VEL_SQ) {
                float scale = MAX_VELOCITY / std::sqrt(clamp_vel_sq);
                p.vx *= scale;
                p.vy *= scale;
                p.vz *= scale;
            }
            // Also check for NaN/Inf and reset to zero
            if (!std::isfinite(p.vx)) p.vx = 0.0f;
            if (!std::isfinite(p.vy)) p.vy = 0.0f;
            if (!std::isfinite(p.vz)) p.vz = 0.0f;
        }

        if (p.vx != 0.0f || p.vy != 0.0f || p.vz != 0.0f) {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.z += p.vz * dt;
            any_moved = true;
        }

        // ====================================================================
        // V4.11: SELECTIVE DAMPING MODEL
        // ====================================================================
        // Two damping mechanisms:
        //
        // 1. QUADRATIC DRAG (air resistance): F ∝ v² - applies to ALL particles
        //    Dominates at high velocities (turbulent regime)
        //    F_drag = 0.5 * ρ * v² * Cd * A
        //
        // 2. LINEAR DAMPING (structural/viscous): F ∝ v - ONLY for gluon-connected
        //    Dominates at low velocities and for connected structures
        //    Models internal friction, material damping in connected systems
        //    F_visc = -b * m * v  (b = damping coefficient in 1/s)
        //
        // V4.11 FIX: Free-falling particles (no gluons) get only air drag.
        // Before: All particles got structural damping → terminal velocity ~2 m/s
        // After: Free-falling particles accelerate naturally under gravity
        // ====================================================================
        if (p.GetMass() > 0.0f) {
            float speed_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;

            if (speed_sq > 0.0001f) {  // Only apply if moving (> 1cm/s)
                float speed = std::sqrt(speed_sq);

                // 1. QUADRATIC DRAG (air resistance) - applies to ALL particles
                float cross_section = p.width * p.height;
                constexpr float RHO_AIR = 1.225f;  // kg/m³
                constexpr float CD = 1.5f;         // Drag coefficient (blunt objects)
                float drag_coeff = 0.5f * RHO_AIR * CD * cross_section / p.GetMass();

                float damping_factor;
                bool has_gluons = particles_with_gluons.count(i) > 0;

                if (has_gluons) {
                    // 2. LINEAR DAMPING (structural) - ONLY for gluon-connected
                    // particles, and V4.15: only on the DEVIATION from the
                    // cluster's shared velocity — internal oscillation dies,
                    // the structure's free fall stays free.
                    float linear_damping = Materials::GetDamping(p.material_type);
                    float b = std::max(5.0f, linear_damping * 24.0f);
                    float ks = 1.0f - b * dt;
                    if (ks < 0.5f) ks = 0.5f;
                    float cvx = 0.0f, cvy = 0.0f, cvz = 0.0f;
                    auto cit = cluster_vel.find(uf_find(i));
                    if (cit != cluster_vel.end() && cit->second.m > 0.0f) {
                        cvx = cit->second.mvx / cit->second.m;
                        cvy = cit->second.mvy / cit->second.m;
                        cvz = cit->second.mvz / cit->second.m;
                    }
                    p.vx = cvx + (p.vx - cvx) * ks;
                    p.vy = cvy + (p.vy - cvy) * ks;
                    p.vz = cvz + (p.vz - cvz) * ks;

                    // Air drag still applies to the full velocity.
                    damping_factor = 1.0f - drag_coeff * speed * dt;
                } else {
                    // Free-falling: air drag only
                    // For typical game particles, this is very small
                    damping_factor = 1.0f - drag_coeff * speed * dt;
                }

                if (damping_factor < 0.5f) damping_factor = 0.5f;  // Clamp to prevent reversal

                p.vx *= damping_factor;
                p.vy *= damping_factor;
                p.vz *= damping_factor;
            }
        }

        // ====================================================================
        // FRAME-GATED DAMPING (2025-12-11)
        // ====================================================================
        // Track consecutive frames at low velocity, then apply damping.
        // This preserves real physics reactions (rock hits tree - velocity varies)
        // while eventually stopping numerical oscillation (steady 0.1-0.3 m/s).
        //
        // WHY: Hard clamping caused instability - clamping one gluon-connected
        // particle while neighbor was above threshold created large v_rel,
        // causing solver to apply destabilizing impulses (max vel grew to 0.84 m/s).
        //
        // RESULTS: 98% particles at rest (was 10%), max velocity 0.016 m/s (stable)
        //
        // Constants from physics_solver.h (see that file for full documentation)
        // THE SLEEP LAW's corollary: a constraint-dissatisfied body's
        // velocity is the correction in progress — the rest damper must
        // not crush it.
        if (i < constraint_dissatisfied_.size() && constraint_dissatisfied_[i]) {
            p.low_velocity_frames = 0;
        }
        float vel_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;

        // Track low-velocity frames with hysteresis (V4.7)
        // - Increment counter when below threshold
        // - Only RESET counter if velocity is 2x above threshold (real motion, not oscillation)
        // - Velocities in between don't change counter (hysteresis band)
        constexpr float DAMPING_RESET_THRESHOLD_SQ = DAMPING_VELOCITY_THRESHOLD_SQ * 4.0f;  // 2x velocity = 4x squared

        if (vel_sq < DAMPING_VELOCITY_THRESHOLD_SQ) {
            if (p.low_velocity_frames < 65535) p.low_velocity_frames++;
        } else if (vel_sq > DAMPING_RESET_THRESHOLD_SQ) {
            p.low_velocity_frames = 0;  // Real motion - reset counter
        }
        // Velocity between threshold and 2x threshold: don't change counter (hysteresis)

        // Apply damping only after sustained low velocity
        if (p.low_velocity_frames >= DAMPING_FRAMES_REQUIRED) {
            if (vel_sq < ZERO_VELOCITY_SQ) {
                // Below noise floor: hard clamp to exactly zero
                p.vx = p.vy = p.vz = 0.0f;
                vel_sq = 0.0f;
            } else {
                // Apply gradual damping
                p.vx *= DAMPING_FACTOR;
                p.vy *= DAMPING_FACTOR;
                p.vz *= DAMPING_FACTOR;
                vel_sq *= DAMPING_FACTOR * DAMPING_FACTOR;
            }
        }

        // NOTE: At-rest detection moved to update_rest_state() - called AFTER enforce_turtle_boundary
        // so it sees final velocities (turtle boundary zeros vz for particles on ground)
    }

    if (any_moved && particle_system_) {
        particle_system_->mark_bvh_dirty();
    }
}

// ============================================================================
// PHASE 6: TURTLE BOUNDARY ENFORCEMENT
// ============================================================================
/**
 * Enforce the Turtle as an ABSOLUTE boundary - nothing goes below z=0.
 *
 * This is a post-solver safety net. The constraint solver tries to respect
 * the Turtle through collision contacts, but strong gluon forces can sometimes
 * push particles through. This function clamps them back.
 *
 * BEHAVIOR:
 *   - Any particle whose bottom penetrates Turtle (z < half_thickness) gets clamped
 *   - Downward velocity is zeroed (prevents continued penetration)
 *   - Horizontal velocity preserved (allows sliding)
 *   - No bounce (prevents oscillation from repeated corrections)
 *
 * WHY NO BOUNCE:
 *   If we add upward velocity, the particle might overshoot, then next frame
 *   gravity pulls it down, it penetrates again, we bounce again → oscillation.
 *   Zero velocity = immediate rest on boundary.
 */
void PhysicsSystem::enforce_turtle_boundary(ParticleSystem::WriteView& particles) {
    const size_t count = particles.size();

    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];

        // Skip massless particles (lights float)
        if (p.GetMass() == 0.0f) continue;

        // Calculate particle bottom
        float half_thickness = p.thickness * 0.5f;
        float particle_bottom = p.z - half_thickness;

        // THE BOUNDARY HAS THE SAME SLOP EVERY OTHER CONTACT HAS.
        //
        // This fired on `bottom < TURTLE_Z` with no tolerance, and exact
        // geometric equality is not achievable in floating point. A body
        // computed to sit exactly on the floor lands a few hundred nanometres
        // under it and gets lifted — every substep, forever, as a free
        // position correction. Measured on grass: blades placed at
        // z = 0.0199997 against a half-thickness of 0.0200000, under by 250
        // NANOMETRES, on hundreds of blades.
        //
        // SLOP is the engine's existing statement of "this much geometric
        // error is not real", already used by every contact row. Reusing it
        // rather than inventing a second tolerance, so there is one number
        // that means one thing.
        if (particle_bottom < TURTLE_Z - SLOP) {
            // Clamp position so bottom is exactly at turtle
            float correction = TURTLE_Z - particle_bottom;
            p.z += correction;

            // Zero out downward velocity (no bounce to prevent oscillation)
            if (p.vz < 0.0f) {
                p.vz = 0.0f;
            }

            // Debug: track turtle corrections (disabled)
            // std::cout << "[TURTLE_ENFORCE] P" << i << " z=" << (p.z - correction) << "→" << p.z
            //           << " correction=" << correction << " material=" << static_cast<int>(p.material_type) << "\n";
        }
    }
}

// ============================================================================
// PHASE 7: AT-REST STATE UPDATE
// ============================================================================
/**
 * Update at-rest state for all particles AFTER turtle boundary enforcement.
 *
 * WHY HERE (not in integrate_positions):
 *   Layer 0 particles (on Turtle) have vz=-0.48 m/s from gravity BEFORE
 *   enforce_turtle_boundary zeros it. If we check rest state before turtle
 *   enforcement, we see high velocity and reset frames_at_rest every frame.
 *   By checking AFTER, we see the true final velocity (vz=0 for ground particles).
 *
 * HYSTERESIS:
 *   - Enter rest: velocity < 0.1 m/s for 10 consecutive frames
 *   - Wake: velocity > 0.2 m/s (higher threshold prevents oscillation)
 *   - In-between: no state change (hysteresis band)
 */
void PhysicsSystem::update_rest_state(ParticleSystem::WriteView& particles) {
    constexpr float REST_VELOCITY_THRESHOLD = 0.1f;    // m/s - enter rest
    constexpr float WAKE_VELOCITY_THRESHOLD = 0.2f;    // m/s - exit rest (2x for hysteresis)
    constexpr uint8_t REST_FRAMES_REQUIRED = 10;       // frames at rest before resting

    const size_t count = particles.size();

    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];

        // Skip massless particles
        if (p.GetMass() == 0.0f) continue;

        float vel_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;

        // THE SLEEP LAW: a body may rest only while its constraint set is
        // satisfied. Low speed with screaming constraints is a body mid-
        // correction, not a body in equilibrium (the lying blade's
        // wake/re-sleep limit cycle recovered at 0.1 mm/frame).
        static const bool law_off = std::getenv("SLEEP_LAW_OFF") != nullptr;
        const bool satisfied = law_off ||
                               i >= constraint_dissatisfied_.size() ||
                               constraint_dissatisfied_[i] == 0;
        if (!satisfied) {
            p.frames_at_rest = 0;
            p.is_at_rest = false;
            p.low_velocity_frames = 0;
        } else if (vel_sq < REST_VELOCITY_THRESHOLD * REST_VELOCITY_THRESHOLD) {
            // Velocity below rest threshold - accumulate rest frames
            if (p.frames_at_rest < 255) p.frames_at_rest++;
            if (p.frames_at_rest >= REST_FRAMES_REQUIRED) {
                p.is_at_rest = true;
                // Zero velocity when entering rest to prevent drift
                p.vx = p.vy = p.vz = 0.0f;
            }
        } else if (vel_sq > WAKE_VELOCITY_THRESHOLD * WAKE_VELOCITY_THRESHOLD) {
            // Velocity above wake threshold - actually moving
            p.frames_at_rest = 0;
            p.is_at_rest = false;
        }
        // Velocity between 0.1-0.2 m/s: no state change (hysteresis band)
    }
}

// ============================================================================
// ANGULAR PHYSICS: CONSTRAINT APPLICATION (DEPRECATED - V4.2)
// ============================================================================
/**
 * DEPRECATED: This function is no longer called in V4.2.
 * Angular constraints are now solved in solve_contacts_v3() alongside linear
 * constraints using the same Sequential Impulse iteration loop.
 *
 * This function remains for reference and potential fallback.
 *
 * OLD BEHAVIOR (pre-V4.2):
 * Apply angular constraints from gluons using spring-damper torques.
 * Ran AFTER the Jacobian solver, causing "door hinge" effect because
 * position projection used stale rotation_z values.
 *
 * V4.2 FIX: Angular constraints now create Constraint objects in
 * solve_contacts_v3() and are solved in the same iteration loop as
 * linear constraints. This couples angular and linear properly.
 */
void PhysicsSystem::apply_angular_constraints(ParticleSystem::WriteView& particles, float dt) {
    for (const auto& gluon : gluon_constraints_v2_) {
        if (!gluon->enable_angular_constraint) continue;

        Particle& pa = particles[gluon->particle_a];
        Particle& pb = particles[gluon->particle_b];

        // NOTE: is_kinematic removed (Turtle architecture) - all particles are dynamic

        // Relative rotation (how much B differs from A, accounting for target)
        float angle_diff = pb.rotation_z - pa.rotation_z - gluon->target_relative_rotation;

        // Skip if NaN or infinity (prevents infinite loop in normalization)
        if (!std::isfinite(angle_diff)) continue;

        // Normalize to [-π, π] using fmod (safer than while loops)
        angle_diff = std::fmod(angle_diff + M_PI, 2.0f * M_PI);
        if (angle_diff < 0) angle_diff += 2.0f * M_PI;
        angle_diff -= M_PI;

        // Relative angular velocity
        float omega_rel = pb.omega_z - pa.omega_z;

        // DEAD ZONE LOGIC: No restoring torque within joint limits
        // This allows free rotation within limits, but resists beyond limits
        float limit = gluon->max_relative_rotation;
        float effective_diff = 0.0f;

        if (angle_diff > limit) {
            // Exceeded positive limit - push back to limit
            effective_diff = angle_diff - limit;
        } else if (angle_diff < -limit) {
            // Exceeded negative limit - push back to limit
            effective_diff = angle_diff + limit;
        }
        // else: within limits, effective_diff = 0 (free rotation)

        // Spring-damper torque: τ = -k * θ_effective - c * ω
        // Torque only applied when exceeding limits (effective_diff != 0)
        // Damping always applied to prevent oscillation at limits
        float torque = -gluon->angular_stiffness * effective_diff
                      - gluon->angular_damping * omega_rel;

        // Apply equal and opposite torques (Newton's third law)
        pa.torque_z -= torque;
        pb.torque_z += torque;
    }
}

// ============================================================================
// ANGULAR PHYSICS: VELOCITY INTEGRATION (V4.2)
// ============================================================================
/**
 * Integrate angular velocities and update rotation angles.
 *
 * V4.2 ROLE:
 * Angular velocity (omega_z) is modified by TWO sources:
 *   1. External torques (torque_z) - e.g., ParticleDynamicsSystem look-at control
 *   2. Jacobian solver impulses - gluon angular constraints apply impulses directly
 *
 * This function:
 *   1. Integrates external torques: ω += (τ/I) × dt
 *   2. Applies damping to prevent infinite spin
 *   3. Integrates velocity to angle: θ += ω × dt
 *   4. Clears torque accumulator
 *
 * IMPORTANT: This runs BEFORE position projection in V4.2 so that rotation_z
 * is current when computing gluon attachment point positions. This fixes the
 * "door hinge" effect from V4.1.
 */
void PhysicsSystem::integrate_angular_velocities(ParticleSystem::WriteView& particles, float dt) {
    for (size_t i = 0; i < particles.size(); ++i) {
        Particle& p = particles[i];

        // DYNAMICS particles are controlled by ParticleDynamicsSystem (humanoids, etc.)
        // Skip angular integration here - dynamics handles their rotation
        if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;

        // Get moment of inertia (calculate if not cached)
        float I = p.GetMomentOfInertia();
        if (I <= 0.0f) {
            // Fallback for invalid inertia
            I = 0.01f;
        }

        // Angular acceleration = torque / moment_of_inertia
        float angular_accel = p.torque_z / I;

        // Integrate angular velocity
        p.omega_z += angular_accel * dt;

        // Clamp angular velocity to keep torque integration stable.
        // 2π rad/s = 360°/s. Matches the dynamics angular integrator's
        // cap so the two paths agree on the maximum angular speed a
        // particle can carry; also leaves physics-drive PD gluons
        // enough headroom to swing a humanoid limb (1–3 rad/s target
        // peaks) without being velocity-starved into a steady-state
        // offset. Raise via config if future actors need faster
        // rotation.
        const float MAX_OMEGA = 6.28f;
        if (p.omega_z > MAX_OMEGA) p.omega_z = MAX_OMEGA;
        if (p.omega_z < -MAX_OMEGA) p.omega_z = -MAX_OMEGA;

        // Apply angular damping (global damping to prevent infinite spin)
        // This is separate from gluon damping - it's general air resistance for rotation
        const float ANGULAR_DRAG = 0.95f;  // 5% velocity loss per frame
        p.omega_z *= ANGULAR_DRAG;

        // Integrate rotation angle
        p.rotation_z += p.omega_z * dt;

        // Clear torque accumulator for next frame
        p.torque_z = 0.0f;

        // -------------------------------------------------------------
        // 3-axis angular integration (rotational-DOF upgrade Stage 2)
        // -------------------------------------------------------------
        // omega_x/y accumulate from torque_x/y the same way Z does
        // above, then rotation_q integrates the full 3-vector angular
        // velocity via the axis-angle exponential map. The legacy Z
        // scalar path above is left intact so callers that only read
        // rotation_z keep working; later stages unify.
        p.omega_x += (p.torque_x / I) * dt;
        p.omega_y += (p.torque_y / I) * dt;
        if (p.omega_x >  MAX_OMEGA) p.omega_x =  MAX_OMEGA;
        if (p.omega_x < -MAX_OMEGA) p.omega_x = -MAX_OMEGA;
        if (p.omega_y >  MAX_OMEGA) p.omega_y =  MAX_OMEGA;
        if (p.omega_y < -MAX_OMEGA) p.omega_y = -MAX_OMEGA;
        p.omega_x *= ANGULAR_DRAG;
        p.omega_y *= ANGULAR_DRAG;
        p.torque_x = 0.0f;
        p.torque_y = 0.0f;

        // Integrate rotation_q from the full omega vector. Axis-angle
        // exponential: dq = axis_angle(omega / |omega|, |omega| * dt).
        // World-frame omega, so dq applies AFTER the current q.
        float wx = p.omega_x, wy = p.omega_y, wz = p.omega_z;
        float omega_mag = std::sqrt(wx*wx + wy*wy + wz*wz);
        if (omega_mag > 1e-6f) {
            float theta = omega_mag * dt;
            float inv = 1.0f / omega_mag;
            logosphere::Quat dq = logosphere::Quat::from_axis_angle(
                wx * inv, wy * inv, wz * inv, theta);
            p.rotation_q = (dq * p.rotation_q).normalized();
        }

        // Quaternion-driven particles publish their Euler triple from
        // rotation_q so rendering, FK cascade reads, and shape-snap
        // stay coherent with the live orientation. Particles that
        // aren't quat-driven keep their Euler-owned path; rotation_q
        // on those particles is a stale by-product that the 3-axis
        // constraint build re-syncs from Euler when it needs a
        // quaternion view.
        if (p.is_quat_driven) {
            p.rotation_q.to_euler_zyx(p.rotation_x, p.rotation_y, p.rotation_z);
        }

        static const char* st_env = std::getenv("AUTHORITY_DEBUG");
        static const int st_id = st_env ? std::atoi(st_env) : -1;
        if ((int)i == st_id) {
            static int sn = 0;
            if ((sn++ % 240) == 0)
                printf("[STATE P%d post-integ] omega(%.3f,%.3f,%.3f)  "
                       "euler(%.2f,%.2f,%.2f)  q(%.3f,%.3f,%.3f,%.3f)\n",
                       st_id, p.omega_x, p.omega_y, p.omega_z,
                       p.rotation_x, p.rotation_y, p.rotation_z,
                       p.rotation_q.w, p.rotation_q.x, p.rotation_q.y, p.rotation_q.z);
        }
    }
}

// ============================================================================
// PHASE 4.8: ANGULAR LIMIT PROJECTION (HARD CONSTRAINTS)
// ============================================================================
/**
 * Project rotations to satisfy gluon angular limits.
 *
 * This is the angular equivalent of project_gluon_positions().
 * After integrating angular velocities, rotations may exceed limits.
 * This function projects them back to satisfy constraints.
 *
 * EXAMPLE:
 *   Head at 60°, Neck at 0°, limit = 45°
 *   angle_diff = 60° - 0° = 60°
 *   error = 60° - 45° = 15° (exceeded by 15°)
 *
 *   Split correction by inertia:
 *   - Head moves back: -15° × ratio_b
 *   - Neck moves forward: +15° × ratio_a
 *
 *   After projection: angle_diff = 45° (at limit)
 */
void PhysicsSystem::project_angular_limits(ParticleSystem::WriteView& particles) {
    for (const auto& gluon : gluon_constraints_v2_) {
        if (!gluon->enable_angular_constraint) continue;

        Particle& pa = particles[gluon->particle_a];
        Particle& pb = particles[gluon->particle_b];

        // Calculate relative rotation
        float angle_diff = pb.rotation_z - pa.rotation_z - gluon->target_relative_rotation;

        // Normalize to [-π, π]
        angle_diff = std::fmod(angle_diff + M_PI, 2.0f * M_PI);
        if (angle_diff < 0) angle_diff += 2.0f * M_PI;
        angle_diff -= M_PI;

        float limit = gluon->max_relative_rotation;
        float error = 0.0f;

        if (angle_diff > limit) {
            error = angle_diff - limit;  // How much past the positive limit
        } else if (angle_diff < -limit) {
            error = angle_diff + limit;  // How much past the negative limit (negative)
        }

        if (std::abs(error) < 0.001f) continue;  // Within limits, no correction needed

        // Project rotations to satisfy limit
        // Split correction based on inertia (like position projection splits by mass)
        // Higher inertia = harder to rotate = moves less
        float I_a = pa.GetMomentOfInertia();
        float I_b = pb.GetMomentOfInertia();
        float total_I = I_a + I_b;

        if (total_I > 0.0001f) {
            // ratio_a uses I_b: if B has more inertia, A moves more
            float ratio_a = I_b / total_I;
            float ratio_b = I_a / total_I;

            // Skip DYNAMICS-owned particles — dynamics system controls
            // their rotation. Same principle as linear position projection.
            if (pa.solver_mode != ParticleSolverMode::KINEMATIC) {
                pa.rotation_z += error * ratio_a;
            }
            if (pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                pb.rotation_z -= error * ratio_b;
            }
        }
    }
}

// ============================================================================
// HELPER METHODS (from interface)
// ============================================================================

void PhysicsSystem::load_constraints_from_kg() {
    // Stub - will be implemented when KG integration is needed
}

void PhysicsSystem::add_force(std::unique_ptr<Force> force) {
    if (!force) return;
    forces_.push_back(std::move(force));
}

void PhysicsSystem::clear_forces() {
    forces_.clear();
}

Force* PhysicsSystem::get_force(size_t index) {
    if (index >= forces_.size()) return nullptr;
    return forces_[index].get();
}

// ============================================================================
// GLUON MANAGEMENT
// ============================================================================

size_t PhysicsSystem::add_particle_with_gluon_to(
    size_t particle_a_id,
    const Particle& particle_b_config,
    std::unique_ptr<GluonConstraintBase> gluon,
    bool use_config_position)
{
    if (!is_initialized_ || !gluon) {
        return static_cast<size_t>(-1);
    }

    // Get particle A's position (thread-safe copy)
    Particle pa = particle_system_->get_particle_copy(particle_a_id);

    // Position B relative to A using offsets, UNLESS the caller has already
    // placed it. See the header for why that distinction matters: overwriting a
    // deliberate placement is issue #38, where a non-overlapping search ran for
    // every leaf and its answer was discarded one call later.
    Particle pb = particle_b_config;
    if (!use_config_position) {
        pb.x = pa.x + gluon->offset_a.x - gluon->offset_b.x;
        pb.y = pa.y + gluon->offset_a.y - gluon->offset_b.y;
        pb.z = pa.z + gluon->offset_a.z - gluon->offset_b.z;
    }

    // Add particle B
    size_t particle_b_id = particle_system_->add_particle(pb);

    // Setup gluon
    gluon->particle_a = particle_a_id;
    gluon->particle_b = particle_b_id;
    gluon->id = next_gluon_id_++;

    // V4.4 Mass-scaled damping: heavier connections = more damping
    gluon->damping *= std::sqrt(pa.GetMass() * pb.GetMass());

    gluon_constraints_v2_.push_back(std::move(gluon));

    // Index for O(1) lookup
    auto* indexed_gluon = gluon_constraints_v2_.back().get();
    index_gluon(indexed_gluon);

    // V4.10 wake logic REMOVED (V4.15)
    // The original reasoning was:
    //   1. Gravity applies to A (at-rest particles skip gravity)
    //   2. Gluon constraints aren't skipped (skip when both at rest)
    //
    // Why it's wrong:
    //   1. Floor tiles don't need gravity - they rest on turtle boundary
    //   2. Gluon skip was removed in V4.10 itself - constraints always run
    //   3. At_rest particles have inv_mass=0 in solver - they act as anchors
    //
    // Waking floor tiles caused the "explosion bug": gluon forces moved floor
    // tiles at 3+ m/s, cascading wake events to 38,000+ particles.

    return particle_b_id;
}

void PhysicsSystem::add_gluon_between(
    size_t particle_a_id,
    size_t particle_b_id,
    std::unique_ptr<GluonConstraintBase> gluon)
{
    if (!is_initialized_ || !gluon) {
        return;
    }

    // Setup gluon between existing particles
    gluon->particle_a = particle_a_id;
    gluon->particle_b = particle_b_id;
    gluon->id = next_gluon_id_++;

    // V4.4 Mass-scaled damping: heavier connections = more damping
    Particle pa = particle_system_->get_particle_copy(particle_a_id);
    Particle pb = particle_system_->get_particle_copy(particle_b_id);
    gluon->damping *= std::sqrt(pa.GetMass() * pb.GetMass());

    gluon_constraints_v2_.push_back(std::move(gluon));

    // Index for O(1) lookup
    index_gluon(gluon_constraints_v2_.back().get());

    // V4.10 wake logic REMOVED (V4.15) - see add_particle_with_gluon_to for explanation
}

// ============================================================================
// GLUON INDEX MAINTENANCE
// ============================================================================

void PhysicsSystem::index_gluon(GluonConstraintBase* gluon) {
    size_t key = make_gluon_pair_key(gluon->particle_a, gluon->particle_b);
    // Debug: check if we're overwriting an existing entry
    auto existing = gluon_pair_index_.find(key);
    if (existing != gluon_pair_index_.end()) {
        std::cout << "[GLUON_INDEX] WARNING: Overwriting existing gluon for pair ("
                  << gluon->particle_a << "," << gluon->particle_b << ")"
                  << " old_offset_a.z=" << existing->second->offset_a.z
                  << " new_offset_a.z=" << gluon->offset_a.z << std::endl;
    }
    gluon_pair_index_[key] = gluon;
}

void PhysicsSystem::unindex_gluon(GluonConstraintBase* gluon) {
    size_t key = make_gluon_pair_key(gluon->particle_a, gluon->particle_b);
    // Only erase if the entry still points at THIS gluon — a newer gluon
    // for the same pair may have been indexed since (pin gluon respawn).
    auto it = gluon_pair_index_.find(key);
    if (it != gluon_pair_index_.end() && it->second == gluon) {
        gluon_pair_index_.erase(it);
    }
}

// ============================================================================
// GLUON QUERY INTERFACE (for Animation/Dynamics FK)
// ============================================================================

const GluonConstraintBase* PhysicsSystem::get_gluon(size_t particle_a, size_t particle_b) const {
    size_t key = make_gluon_pair_key(particle_a, particle_b);
    auto it = gluon_pair_index_.find(key);
    return (it != gluon_pair_index_.end()) ? it->second : nullptr;
}

GluonConstraintBase* PhysicsSystem::get_gluon_mut(size_t particle_a, size_t particle_b) {
    size_t key = make_gluon_pair_key(particle_a, particle_b);
    auto it = gluon_pair_index_.find(key);
    return (it != gluon_pair_index_.end()) ? it->second : nullptr;
}

std::vector<const GluonConstraintBase*> PhysicsSystem::get_gluons_for_particle(size_t particle_id) const {
    std::vector<const GluonConstraintBase*> result;
    for (const auto& gluon : gluon_constraints_v2_) {
        if (gluon->particle_a == particle_id || gluon->particle_b == particle_id) {
            result.push_back(gluon.get());
        }
    }
    return result;
}

void PhysicsSystem::mark_gluon_for_removal(size_t gluon_id) {
    gluons_to_remove_.push_back(gluon_id);
}

void PhysicsSystem::remove_marked_gluons(ParticleSystem::WriteView* particles) {
    if (gluons_to_remove_.empty()) return;

    for (size_t id : gluons_to_remove_) {
        // WAKE-ON-BREAK: a snapping bond frees its bodies, and freedom is
        // an event — a sleeping segment whose bond tears must FALL, not
        // hover where the bond left it (rung 4, owner QA: the brittle
        // chain's upper segment stood on air after the snap).
        if (particles) {
            for (const auto& g : gluon_constraints_v2_) {
                if (g->id != id) continue;
                // Clearing is_at_rest alone is not enough: gravity grows
                // velocity too slowly (0.16 m/s in the first frame) to
                // escape the rest counter's hysteresis band, so the damper
                // crushes it and the particle re-latches at_rest while
                // FLOATING. Freedom resets the counter too.
                if (g->particle_a < particles->size()) {
                    (*particles)[g->particle_a].is_at_rest = false;
                    (*particles)[g->particle_a].low_velocity_frames = 0;
                }
                if (g->particle_b < particles->size()) {
                    (*particles)[g->particle_b].is_at_rest = false;
                    (*particles)[g->particle_b].low_velocity_frames = 0;
                }
            }
        }
        // Unindex BEFORE the unique_ptr frees the gluon. Without this,
        // gluon_pair_index_ keeps a dangling pointer and get_gluon(_mut)
        // serves freed memory to every later caller for that pair —
        // publish_physics_drive_targets then writes target_relative_q
        // into reused heap (found 2026-06-12 corrupting Metal encoders).
        for (const auto& g : gluon_constraints_v2_) {
            if (g->id == id) unindex_gluon(g.get());
        }
        auto it = std::remove_if(gluon_constraints_v2_.begin(), gluon_constraints_v2_.end(),
            [id](const std::unique_ptr<GluonConstraintBase>& g) {
                return g->id == id;
            });
        gluon_constraints_v2_.erase(it, gluon_constraints_v2_.end());
    }

    gluons_to_remove_.clear();
}

// ============================================================================
// PARTICLE DELETION SUPPORT - Clean up gluons when particles are removed
// ============================================================================

void PhysicsSystem::prune_invalid_gluons(size_t particle_count) {
    // DIAGNOSTIC: Find and report stale gluons to help identify root cause
    // This should ideally find nothing - if it finds stale gluons, there's a bug
    // in notify_particle_swap or particle removal logic.

    for (const auto& g : gluon_constraints_v2_) {
        bool a_invalid = g->particle_a >= particle_count;
        bool b_invalid = g->particle_b >= particle_count;
        if (a_invalid || b_invalid) {
            std::cerr << "[GLUON STALE] gluon_id=" << g->id
                      << " particle_a=" << g->particle_a << (a_invalid ? " INVALID" : "")
                      << " particle_b=" << g->particle_b << (b_invalid ? " INVALID" : "")
                      << " particle_count=" << particle_count
                      << " rotate_offsets=" << g->rotate_offsets
                      << std::endl;
            // Unindex stale gluon
            unindex_gluon(g.get());
        }
    }

    // Now remove them to prevent crash
    auto it = std::remove_if(gluon_constraints_v2_.begin(), gluon_constraints_v2_.end(),
        [particle_count](const std::unique_ptr<GluonConstraintBase>& g) {
            return g->particle_a >= particle_count || g->particle_b >= particle_count;
        });

    if (it != gluon_constraints_v2_.end()) {
        size_t removed = std::distance(it, gluon_constraints_v2_.end());
        std::cerr << "[GLUON] Pruned " << removed << " stale gluons - INVESTIGATE ROOT CAUSE!" << std::endl;
        gluon_constraints_v2_.erase(it, gluon_constraints_v2_.end());
    }
}

void PhysicsSystem::remove_gluons_for_particle(size_t particle_id) {
    // Remove all gluons that reference this particle
    // Called by ParticleSystem BEFORE swap-and-pop deletion
    size_t initial_count = gluon_constraints_v2_.size();

    // First unindex affected gluons (must happen before removal)
    for (const auto& g : gluon_constraints_v2_) {
        if (g->particle_a == particle_id || g->particle_b == particle_id) {
            unindex_gluon(g.get());
        }
    }

    // Then remove from vector
    auto it = std::remove_if(gluon_constraints_v2_.begin(), gluon_constraints_v2_.end(),
        [particle_id](const std::unique_ptr<GluonConstraintBase>& g) {
            return g->particle_a == particle_id || g->particle_b == particle_id;
        });

    gluon_constraints_v2_.erase(it, gluon_constraints_v2_.end());

    size_t removed = initial_count - gluon_constraints_v2_.size();
    if (removed > 0) {
        std::cout << "[GLUON] Removed " << removed << " gluons for deleted particle " << particle_id << std::endl;
    }

    // Collision events carry raw particle indices and live until the next
    // physics update. Consumers (handle_collision_events, step climbing)
    // index particles[] with them, so an event referencing the deleted
    // slot is a read past the live end of the particle vector. Same
    // lifecycle rule as gluons: drop on delete.
    auto evt_it = std::remove_if(collision_events_.begin(), collision_events_.end(),
        [particle_id](const CollisionEvent& e) {
            return e.particle_a == particle_id || e.particle_b == particle_id;
        });
    collision_events_.erase(evt_it, collision_events_.end());
}

void PhysicsSystem::notify_particle_swap(size_t old_index, size_t new_index) {
    // Update gluon indices after swap-and-pop
    // Called by ParticleSystem AFTER swap (last particle moved to old_index position)
    for (auto& gluon : gluon_constraints_v2_) {
        bool needs_reindex = (gluon->particle_a == old_index || gluon->particle_b == old_index);

        if (needs_reindex) {
            // Remove old index entry
            unindex_gluon(gluon.get());

            // Update particle references
            if (gluon->particle_a == old_index) {
                gluon->particle_a = new_index;
            }
            if (gluon->particle_b == old_index) {
                gluon->particle_b = new_index;
            }

            // Add new index entry
            index_gluon(gluon.get());
        }
    }

    // Same lifecycle rule as gluons: collision events referencing the
    // moved particle follow it to its new slot.
    for (auto& evt : collision_events_) {
        if (evt.particle_a == old_index) evt.particle_a = new_index;
        if (evt.particle_b == old_index) evt.particle_b = new_index;
    }
}

// ============================================================================
// WAKE SYSTEM - Propagate wakeups through gluon connections
// ============================================================================
/**
 * Wake a particle and all connected particles via gluons (BFS).
 *
 * WHY BFS PROPAGATION:
 *   When a tree is cut, only the cut gluon is directly affected.
 *   But the whole tree section should wake up - trunk, branches, leaves.
 *   BFS traversal ensures the entire connected component wakes.
 *
 * EXAMPLE:
 *   Tree structure: [Ground] - gluon - [Trunk] - gluon - [Branch] - gluon - [Leaf]
 *   Player cuts Trunk-Ground gluon.
 *   Trunk wakes → BFS → Branch wakes → Leaf wakes.
 *   Result: Falling tree section is fully awake.
 */
// ============================================================================
// V4.6: THRESHOLD-BASED WAKE PROPAGATION
// ============================================================================
// Physical principle: Small disturbances shouldn't cascade through entire structures.
// A leaf settling on a tree shouldn't wake the whole tree.
//
// Threshold logic (similar to adaptive damping):
// - impact_velocity < WAKE_PROPAGATION_THRESHOLD: wake ONLY the hit particle
// - impact_velocity >= WAKE_PROPAGATION_THRESHOLD: propagate wake through gluons
//
// Default: FLT_MAX = always propagate (for external API like wake_particle())
// ============================================================================
constexpr float WAKE_PROPAGATION_THRESHOLD = 0.5f;  // m/s - small bumps don't cascade
constexpr float WAKE_ENERGY_DAMPING = 0.3f;  // Energy loss per gluon hop (70% loss)

// Static counters for wake propagation diagnostics
static size_t s_wake_propagation_total = 0;
static size_t s_wake_propagation_calls = 0;
static size_t s_wake_propagation_max_single = 0;

void PhysicsSystem::wake_particle_with_propagation(size_t particle_id, ParticleSystem::WriteView& particles,
                                                    float impact_velocity) {
    if (particle_id >= particles.size()) return;

    // Always wake the initial particle
    Particle& initial = particles[particle_id];
    if (initial.is_at_rest) {
        initial.is_at_rest = false;
        initial.frames_at_rest = 0;
    }

    // V4.8: Energy-aware wake propagation
    // Only propagate if impact has enough momentum to move connected particles
    // transferred_velocity = impact_velocity × m_curr / m_neighbor
    // Only wake neighbor if transferred_velocity > threshold
    //
    // Physics: A 1 m/s impact on a 10kg branch can't move a 1000kg trunk
    // This prevents cascading wakes through heavy structures (trees, rocks)
    if (impact_velocity < WAKE_PROPAGATION_THRESHOLD) {
        return;  // Wake only the hit particle, no propagation
    }

    // Build gluon adjacency list
    const size_t count = particles.size();
    std::vector<std::vector<size_t>> adj(count);
    for (const auto& gluon : gluon_constraints_v2_) {
        size_t a = gluon->particle_a;
        size_t b = gluon->particle_b;
        if (a < count && b < count) {
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
    }

    // BFS with momentum tracking - propagate wake only if enough energy
    std::queue<std::pair<size_t, float>> queue;  // (particle_id, incoming_velocity)
    std::vector<bool> visited(count, false);

    queue.push({particle_id, impact_velocity});
    visited[particle_id] = true;

    size_t woken_this_call = 1;  // Count initial particle

    while (!queue.empty()) {
        auto [curr, curr_velocity] = queue.front();
        queue.pop();

        Particle& p = particles[curr];

        // Wake this particle
        if (p.is_at_rest) {
            p.is_at_rest = false;
            p.frames_at_rest = 0;
        }

        // Propagate to neighbors with momentum transfer
        float curr_mass = p.GetMass();
        if (curr_mass <= 0.0f) continue;  // Skip massless (lights)

        for (size_t neighbor : adj[curr]) {
            if (visited[neighbor]) continue;

            const Particle& pn = particles[neighbor];
            float neighbor_mass = pn.GetMass();
            if (neighbor_mass <= 0.0f) continue;  // Skip massless

            // Momentum transfer with damping: v_neighbor = v_curr × m_curr / m_neighbor × damping
            // Damping represents energy lost at each gluon joint (friction, deformation)
            float transferred_velocity = curr_velocity * curr_mass / neighbor_mass * WAKE_ENERGY_DAMPING;

            // Only propagate if transferred velocity exceeds threshold
            if (transferred_velocity >= WAKE_PROPAGATION_THRESHOLD) {
                visited[neighbor] = true;
                queue.push({neighbor, transferred_velocity});
                woken_this_call++;
            }
        }
    }

    // Update diagnostics
    s_wake_propagation_total += woken_this_call;
    s_wake_propagation_calls++;
    if (woken_this_call > s_wake_propagation_max_single) {
        s_wake_propagation_max_single = woken_this_call;
    }
}

// Call this from physics update to reset diagnostics (logging disabled)
void log_wake_propagation_stats() {
    s_wake_propagation_total = 0;
    s_wake_propagation_calls = 0;
    s_wake_propagation_max_single = 0;
}

// ============================================================================
// V4.5: TURTLE-SUPPORTED LOAD-BEARING OPTIMIZATION
// ============================================================================
/**
 * Check if a particle has continuous gluon support down to the Turtle.
 *
 * Returns true if BFS finds a path through downward gluons to a particle
 * in contact with the Turtle (z ≈ TURTLE_Z). This indicates the particle
 * is supported by the world and doesn't need to wake.
 *
 * Key insight: Only downward gluons count for support (pb.z < pa.z).
 * Lateral gluons (same z level) provide stability but not support.
 *
 * Example (supported):
 *   Floor1 (z=0.2) ← Floor2 (z=0.1) ← Floor3 (z=0.05, contacts Turtle)
 *   BFS finds Floor3 at z≈0 → returns true
 *
 * Example (unsupported, floating):
 *   Floor1 (z=10) ← Floor2 (z=9) ← no more downward gluons
 *   BFS terminates → returns false
 */
bool PhysicsSystem::check_turtle_support_chain(size_t particle_id, ParticleSystem::WriteView& particles) {
    if (particle_id >= particles.size()) return false;

    const size_t count = particles.size();

    // Build downward-only adjacency list (only gluons going down)
    std::vector<std::vector<size_t>> downward_adj(count);
    for (const auto& gluon : gluon_constraints_v2_) {
        size_t a = gluon->particle_a;
        size_t b = gluon->particle_b;
        if (a < count && b < count) {
            const Particle& pa = particles[a];
            const Particle& pb = particles[b];

            // Only add downward edges (b is below a in Z)
            if (pb.z < pa.z) {
                downward_adj[a].push_back(b);
            } else if (pa.z < pb.z) {
                downward_adj[b].push_back(a);
            }
            // Same z level (lateral): not added to downward adjacency
        }
    }

    // BFS downward from starting particle
    std::queue<size_t> queue;
    std::vector<bool> visited(count, false);

    queue.push(particle_id);
    visited[particle_id] = true;

    while (!queue.empty()) {
        size_t curr = queue.front();
        queue.pop();
        const Particle& p = particles[curr];

        // Check if this particle contacts Turtle (z_bottom ≈ TURTLE_Z)
        float bottom_z = p.z - (p.thickness * 0.5f);
        if (bottom_z <= PhysicsV4::TURTLE_Z + 0.01f) {
            // Found turtle contact - this particle is supported
            return true;
        }

        // Add unvisited downward neighbors
        for (size_t neighbor : downward_adj[curr]) {
            if (!visited[neighbor]) {
                visited[neighbor] = true;
                queue.push(neighbor);
            }
        }
    }

    // No path to turtle found
    return false;
}

/**
 * Public API: Wake a particle (locks particles internally).
 * Call this when applying external force to a particle at rest.
 */
void PhysicsSystem::wake_particle(size_t particle_id) {
    if (!is_initialized_ || !particle_system_) return;

    auto particles = particle_system_->lock_particles_for_write();
    wake_particle_with_propagation(particle_id, particles);
}

// ============================================================================
// PHASE 4.5: POSITION PROJECTION FOR GLUONS (V3.5 Fix)
// ============================================================================
/**
 * BFS distance from kinematic anchors.
 *
 * Returns distance[i] = shortest gluon-hop count from particle i to any kinematic particle.
 * Kinematic particles have distance 0. Disconnected particles have INT_MAX.
 *
 * EXAMPLE (Tree structure):
 *   [Ground] (kinematic, id=0)
 *       |
 *     gluon
 *       |
 *   [Trunk] (id=1)      → distance = 1
 *       |
 *     gluon
 *       |
 *   [Branch] (id=2)     → distance = 2
 */
std::vector<int> PhysicsSystem::compute_bfs_distance(ParticleSystem::WriteView& particles) {
    const size_t count = particles.size();
    std::vector<int> distance(count, INT_MAX);

    // Build adjacency list from gluons
    std::vector<std::vector<size_t>> adj(count);
    for (const auto& gluon : gluon_constraints_v2_) {
        size_t a = gluon->particle_a;
        size_t b = gluon->particle_b;
        adj[a].push_back(b);
        adj[b].push_back(a);
    }

    // Add contacts to adjacency list
    // This allows mobile bodies (like Eva) to get equilibrium from floor contacts
    // Without this, bodies not gluoned to kinematic anchors have distance=INT_MAX
    // and position projection is skipped, causing drift.
    for (const auto& contact : active_contacts_) {
        size_t a = contact.first;
        size_t b = contact.second;
        adj[a].push_back(b);
        adj[b].push_back(a);
    }

    // Pure mass-weighted constraint solving (no anchor heuristics)
    // All particles start with distance=INT_MAX, BFS propagates from gluon connections
    // Heavy particles naturally resist movement in position projection
    std::queue<size_t> queue;
    // Seed with all particles that have gluon connections
    for (const auto& gluon : gluon_constraints_v2_) {
        if (distance[gluon->particle_a] == INT_MAX) {
            distance[gluon->particle_a] = 0;
            queue.push(gluon->particle_a);
        }
        if (distance[gluon->particle_b] == INT_MAX) {
            distance[gluon->particle_b] = 0;
            queue.push(gluon->particle_b);
        }
    }

    while (!queue.empty()) {
        size_t curr = queue.front();
        queue.pop();

        for (size_t neighbor : adj[curr]) {
            if (distance[neighbor] == INT_MAX) {
                distance[neighbor] = distance[curr] + 1;
                queue.push(neighbor);
            }
        }
    }

    return distance;
}

/**
 * Project gluon positions to satisfy distance constraints.
 *
 * ALGORITHM:
 *   1. Compute BFS distance from kinematic anchors
 *   2. Sort gluons by distance (near-to-far from anchor)
 *   3. For each gluon: move the particle farther from anchor to close the gap
 *   4. Match velocities to prevent drift reopening
 *
 * WHY THIS WORKS:
 *   Velocity constraints (Phase 4) don't correct existing position errors.
 *   This phase directly fixes position drift by snapping attachment points.
 *   Processing near-to-far ensures corrections propagate from stable anchors.
 *
 * EXAMPLE:
 *   Trunk at (0, 0, 7), attachment point offset (0, 0, 0.5) = (0, 0, 7.5)
 *   Branch drifted to (0.5, 0.3, 7), attachment offset (0, 0, -0.5) = (0.5, 0.3, 6.5)
 *   Separation: (0, 0, 7.5) - (0.5, 0.3, 6.5) = (-0.5, -0.3, 1.0)
 *   Move branch by separation: (0.5, 0.3, 7) + (-0.5, -0.3, 1.0) = (0, 0, 8)
 *   Now attachment points are coincident.
 */
void PhysicsSystem::project_gluon_positions(ParticleSystem::WriteView& particles) {
    if (gluon_constraints_v2_.empty()) return;

    const size_t count = particles.size();

    // Step 1: Build distance map (includes contacts for position anchor selection)
    std::vector<int> distance = compute_bfs_distance(particles);

    // Step 2: (REMOVED - velocity averaging disabled)
    // Previously tracked has_gluon_velocity for velocity averaging hack.
    // See comment in gluon projection loop below for why this was removed.

    // Step 3: Sort gluons by distance (ascending = near-to-far)
    std::vector<size_t> gluon_order;
    for (size_t i = 0; i < gluon_constraints_v2_.size(); ++i) {
        gluon_order.push_back(i);
    }
    std::sort(gluon_order.begin(), gluon_order.end(), [&](size_t a, size_t b) {
        auto& ga = gluon_constraints_v2_[a];
        auto& gb = gluon_constraints_v2_[b];
        int dist_a = std::max(distance[ga->particle_a], distance[ga->particle_b]);
        int dist_b = std::max(distance[gb->particle_a], distance[gb->particle_b]);
        return dist_a < dist_b;  // Near first
    });

    // Step 4: Project each gluon
    for (size_t idx : gluon_order) {
        auto& gluon = gluon_constraints_v2_[idx];
        size_t a = gluon->particle_a;
        size_t b = gluon->particle_b;

        Particle& pa = particles[a];
        Particle& pb = particles[b];

        // V4.10: Removed at-rest skip - gluon position projection must always run
        // to resist drift from external forces (gravity).
        // Previous skip conditions caused gluon-connected particles to fall apart
        // when both were marked at-rest but external forces were still acting.

        // Skip if both disconnected from equilibrium
        if (distance[a] == INT_MAX && distance[b] == INT_MAX) continue;

        // Calculate attachment points
        // If rotate_offsets is true: offsets rotate with particle's rotation_z
        //   - Used for attached items (hair, accessories) that orbit when parent rotates
        // If rotate_offsets is false (default): offsets are world-space
        //   - Used for skeleton gluons (rigid body structure)
        float attach_a_x, attach_a_y, attach_a_z;
        float attach_b_x, attach_b_y, attach_b_z;

        if (gluon->rotate_offsets) {
            // Rotate offsets by particle's rotation_z
            float cos_a = cosf(pa.rotation_z);
            float sin_a = sinf(pa.rotation_z);
            float rotated_offset_a_x = gluon->offset_a.x * cos_a - gluon->offset_a.y * sin_a;
            float rotated_offset_a_y = gluon->offset_a.x * sin_a + gluon->offset_a.y * cos_a;

            float cos_b = cosf(pb.rotation_z);
            float sin_b = sinf(pb.rotation_z);
            float rotated_offset_b_x = gluon->offset_b.x * cos_b - gluon->offset_b.y * sin_b;
            float rotated_offset_b_y = gluon->offset_b.x * sin_b + gluon->offset_b.y * cos_b;

            attach_a_x = pa.x + rotated_offset_a_x;
            attach_a_y = pa.y + rotated_offset_a_y;
            attach_a_z = pa.z + gluon->offset_a.z;

            attach_b_x = pb.x + rotated_offset_b_x;
            attach_b_y = pb.y + rotated_offset_b_y;
            attach_b_z = pb.z + gluon->offset_b.z;
        } else {
            // World-space offsets (no rotation) - default for skeleton
            attach_a_x = pa.x + gluon->offset_a.x;
            attach_a_y = pa.y + gluon->offset_a.y;
            attach_a_z = pa.z + gluon->offset_a.z;

            attach_b_x = pb.x + gluon->offset_b.x;
            attach_b_y = pb.y + gluon->offset_b.y;
            attach_b_z = pb.z + gluon->offset_b.z;
        }

        // Separation (what we need to close)
        // When rotate_offsets=true, attachment points rotate with particle
        // This naturally creates orbital motion through position projection
        float sep_x = attach_a_x - attach_b_x;
        float sep_y = attach_a_y - attach_b_y;
        float sep_z = attach_a_z - attach_b_z;

        // Determine which particle is anchor
        //
        // For skeleton gluons with rotate_offsets=true:
        //   Rotation propagates FROM the control source (head/torso) TO extremities (feet).
        //   The control source is FURTHER from kinematic contacts (ground).
        //   Therefore: particle FURTHER from ground = anchor, closer = moves to follow.
        //
        // For regular gluons (rotate_offsets=false):
        //   Use standard BFS: closer to ground = anchor (stable base).
        //
        // MASS-WEIGHTED CONSTRAINT SOLVING
        // Pure physics: lighter particles move more, heavier particles move less
        // Particles treated as infinite mass (don't move):
        //   - DYNAMICS-owned: controlled by dynamics system
        //   - is_at_rest: performance optimization, acts as anchor until woken
        float inv_mass_a = (pa.solver_mode == ParticleSolverMode::KINEMATIC || pa.is_at_rest) ? 0.0f : (pa.GetMass() > 0.0f ? 1.0f / pa.GetMass() : 0.0f);
        float inv_mass_b = (pb.solver_mode == ParticleSolverMode::KINEMATIC || pb.is_at_rest) ? 0.0f : (pb.GetMass() > 0.0f ? 1.0f / pb.GetMass() : 0.0f);
        float total_inv_mass = inv_mass_a + inv_mass_b;

        if (total_inv_mass > 0.0f) {
            float ratio_a = inv_mass_a / total_inv_mass;
            float ratio_b = inv_mass_b / total_inv_mass;

            // Move each particle proportionally to close the gap
            pa.x -= sep_x * ratio_a;
            pa.y -= sep_y * ratio_a;
            pa.z -= sep_z * ratio_a;

            pb.x += sep_x * ratio_b;
            pb.y += sep_y * ratio_b;
            pb.z += sep_z * ratio_b;

            // DISABLED: Velocity averaging hack (2024-12-14)
            //
            // This code explicitly averaged velocities between gluon-connected particles,
            // which is NOT proper physics. In a correct Sequential Impulse solver:
            // - Position constraints create impulses
            // - Impulses naturally transfer momentum through solver iterations
            // - Connected bodies move together through iterative constraint solving
            //
            // The velocity averaging was a hack that caused cascade wakes:
            // - If particle A wakes and connects to at-rest particle B
            // - B gets A's velocity through averaging (bypassing wake system)
            // - B's velocity > 0.2 m/s triggers velocity-based wake
            // - B's neighbors get averaged velocities, cascade spreads
            // - Entire gluon graph wakes without depth limit
            //
            // Position projection alone should handle rigid body behavior.
            // If structures don't move together properly, fix the solver iterations
            // or constraint formulation instead of bypassing physics.
        }
    }
}

// ============================================================================
// UNUSED INTERFACE METHODS (kept for compatibility)
// ============================================================================

void PhysicsSystem::integrate_velocities(ParticleSystem::WriteView& particles, float dt) {
    // Not used in V4 - velocities updated in constraint solving
}

void PhysicsSystem::apply_registry_forces(ParticleSystem::WriteView& particles, float dt) {
    // Forces from registry - can be added later if needed
    for (const auto& force : forces_) {
        // Apply each force
    }
}

void PhysicsSystem::apply_gluon_forces(ParticleSystem::WriteView& particles, float dt) {
    // Gluons are now handled as constraints in solve_contacts_v3
}

Vec3 PhysicsSystem::get_gravity_vector() const {
    // Search for GravityForce in force registry
    for (const auto& force : forces_) {
        if (auto* gravity = dynamic_cast<GravityForce*>(force.get())) {
            return Vec3{gravity->get_gx(), gravity->get_gy(), gravity->get_gz()};
        }
    }
    // No gravity force registered
    return Vec3{0.0f, 0.0f, 0.0f};
}

// ============================================================================
// TARGETED GLUON CONSTRAINT (for Animation PHYSICS mode)
// ============================================================================
// Apply gluon distance constraint for a specific particle pair.
// Parent is the anchor (already FK-positioned), child moves to satisfy constraint.
//
// Algorithm:
//   1. Find gluon between parent and child
//   2. Compute attachment point on parent (center + rotated offset)
//   3. Get gravity direction from physics
//   4. Child hangs from attachment point in gravity direction at segment length
//
// This respects actual gravity direction - works with reversed/sideways gravity.
// ============================================================================
void PhysicsSystem::apply_gluon_constraint_for_pair(size_t parent_id, size_t child_id,
                                                     ParticleSystem::WriteView& particles) {
    // Find the gluon connecting these particles
    const GluonConstraintBase* gluon = get_gluon(parent_id, child_id);
    if (!gluon) {
        return;  // No gluon between these particles
    }

    Particle& parent = particles[parent_id];
    Particle& child = particles[child_id];

    // Determine which particle is A and which is B in the gluon
    bool parent_is_a = (gluon->particle_a == parent_id);
    Vec3 parent_offset = parent_is_a ? gluon->offset_a : gluon->offset_b;
    Vec3 child_offset = parent_is_a ? gluon->offset_b : gluon->offset_a;

    // Compute attachment point on parent (in world space)
    // Use full 3D rotation via quaternion (not just rotation_z)
    float attach_x, attach_y, attach_z;
    if (gluon->rotate_offsets) {
        // Build quaternion from particle's Euler angles (ZYX order)
        // Negate rotation_z: particle stores CW convention (from to_euler),
        // but axis-angle uses standard CCW
        logosphere::Quat qx = logosphere::Quat::from_axis_angle(1, 0, 0, parent.rotation_x);
        logosphere::Quat qy = logosphere::Quat::from_axis_angle(0, 1, 0, parent.rotation_y);
        logosphere::Quat qz = logosphere::Quat::from_axis_angle(0, 0, 1, -parent.rotation_z);
        logosphere::Quat parent_rot = qz * qy * qx;

        // Rotate offset by quaternion
        float rotated_x, rotated_y, rotated_z;
        parent_rot.rotate_vector(parent_offset.x, parent_offset.y, parent_offset.z,
                                  rotated_x, rotated_y, rotated_z);

        attach_x = parent.x + rotated_x;
        attach_y = parent.y + rotated_y;
        attach_z = parent.z + rotated_z;
    } else {
        attach_x = parent.x + parent_offset.x;
        attach_y = parent.y + parent_offset.y;
        attach_z = parent.z + parent_offset.z;
    }

    // Get segment length from child offset
    float segment_length = std::sqrt(
        child_offset.x * child_offset.x +
        child_offset.y * child_offset.y +
        child_offset.z * child_offset.z
    );

    // Get gravity direction (normalized)
    Vec3 gravity = get_gravity_vector();
    float gravity_mag = std::sqrt(gravity.x * gravity.x + gravity.y * gravity.y + gravity.z * gravity.z);

    Vec3 hang_dir;
    if (gravity_mag > 0.001f) {
        // Normalize gravity to get hanging direction
        hang_dir = {gravity.x / gravity_mag, gravity.y / gravity_mag, gravity.z / gravity_mag};
    } else {
        // Zero gravity: maintain current relative position (or default to -Z)
        // Use current child position relative to attachment as hang direction
        float dx = child.x - attach_x;
        float dy = child.y - attach_y;
        float dz = child.z - attach_z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 0.001f) {
            hang_dir = {dx / dist, dy / dist, dz / dist};
        } else {
            hang_dir = {0.0f, 0.0f, -1.0f};  // Default: hang down
        }
    }

    // Child position: attachment point + hang direction * segment length
    child.x = attach_x + hang_dir.x * segment_length;
    child.y = attach_y + hang_dir.y * segment_length;
    child.z = attach_z + hang_dir.z * segment_length;

    // DEBUG: Log constraint application
    static int debug_count = 0;
    if (debug_count++ < 10) {
        printf("[GLUON_PHYSICS] parent=%zu child=%zu attach=(%.3f,%.3f,%.3f) "
               "gravity_dir=(%.2f,%.2f,%.2f) segment=%.3f child_pos=(%.3f,%.3f,%.3f)\n",
               parent_id, child_id, attach_x, attach_y, attach_z,
               hang_dir.x, hang_dir.y, hang_dir.z, segment_length,
               child.x, child.y, child.z);
    }
}

void PhysicsSystem::update_velocity(Particle& p, float dt) {
    // Not used - velocity integration is inline
}

bool PhysicsSystem::detect_aabb_collision(const Particle& a, const Particle& b,
                                          const Vec3& pos_a, const Vec3& pos_b,
                                          CollisionInfo& out) {
    // Simplified - full AABB in solve_contacts_v3
    return false;
}

AABB PhysicsSystem::build_particle_aabb(const Particle& p, const Vec3& pos) {
    AABB aabb;
    // Convention: width→X, height→Y, thickness→Z
    // For a flat tile: width/height are horizontal, thickness is vertical
    float half_w = p.width * 0.5f;
    float half_h = p.height * 0.5f;
    float half_t = p.thickness * 0.5f;

    aabb.min_x = pos.x - half_w;
    aabb.min_y = pos.y - half_h;
    aabb.min_z = pos.z - half_t;
    aabb.max_x = pos.x + half_w;
    aabb.max_y = pos.y + half_h;
    aabb.max_z = pos.z + half_t;

    return aabb;
}

bool PhysicsSystem::aabb_intersects(const AABB& a, const AABB& b) {
    return (a.min_x <= b.max_x && a.max_x >= b.min_x) &&
           (a.min_y <= b.max_y && a.max_y >= b.min_y) &&
           (a.min_z <= b.max_z && a.max_z >= b.min_z);
}

