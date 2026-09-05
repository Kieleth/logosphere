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
#include "logosphere/physics/creation_door.h"
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
#include <map>
#include <limits>
#include <tuple>
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
    for (size_t i = 0; i < particles.size(); ++i) {
        const Particle& p = particles[i];
        const double m = p.GetMass();
        if (m <= 0.0) continue;
        if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;
        e.kinetic += 0.5 * m * (double(p.vx)*p.vx + double(p.vy)*p.vy + double(p.vz)*p.vz);
        e.gravity += m * PhysicsV4::ENERGY_LEDGER_G * double(p.z);
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

// WHERE THE ENERGY WENT (INV-19's commitment).
//
// The invariant says damping exists only where a real dissipation
// process is being modelled — a material turning motion into heat —
// and that the conversion is BOOKED. Until 2026-08-15 the ledger
// tracked kinetic, potential and strain, and every joule that left the
// world left silently: a reader could see energy vanish and had no way
// to ask which mechanism took it, or whether that mechanism had any
// right to.
//
// Each bucket names a process, not a fudge:
//   friction  Coulomb friction at contacts. Real: surfaces heat.
//   material  gluon material damping, c = eta*sqrt(k*mu). Real:
//             internal friction of the bonded material.
//   drag      quadratic air drag during integration. Real: the
//             particle stirs the medium.
//   sleep     residue the sleep cache absorbs below its quietness
//             bound (INV-31's resolver). NOT a physical process — it
//             is the cache being a cache, and it is booked separately
//             precisely so it can never hide inside the honest ones.
struct Dissipation {
    double friction = 0.0, material = 0.0, drag = 0.0, sleep = 0.0;
    double total() const { return friction + material + drag + sleep; }
};
static Dissipation g_dissipation;

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
    // The decision tracer's env contract (LOGOSPHERE_PHYS_TRACE...)
    // must hold wherever physics runs. Engine init also calls this;
    // headless standalone tests construct no Engine, and a tracer
    // that ignores its documented switches is a lying contract.
    // init_from_env is idempotent.
    logosphere::phystrace::init_from_env();
    // Quaternion truth is the DEFAULT (owner ruling 2026-08-19, ledger).
    // LOGOSPHERE_QUAT_TRUTH=0 is the kill switch for A/B and bisection;
    // the setter exists for in-process baselines (orientation O2).
    if (const char* qt = std::getenv("LOGOSPHERE_QUAT_TRUTH"))
        quat_truth_ = !(qt[0] == '0' && qt[1] == '\0');
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
    // Headless harnesses drive physics directly; the engine's
    // frame_begin never runs and every trace line said f0. The
    // physics frame IS the trace frame here; engine-driven runs
    // overwrite this with the telemetry index, same value stream.
    { static uint64_t phys_trace_frame = 0;
      logosphere::phystrace::frame_begin(phys_trace_frame++); }
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

    // A sleeper declared from outside the law (a chunk restored from the
    // store, a tile born at rest) is judged before anything else sees it
    // (G-72): asleep in the air is not a state, it is a photograph of a fall.
    derive_kinematic_motion(particles, dt);   // INV-39, behind KINEMATIC_LEDGER
    admit_declared_sleepers(particles);

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

    ++telemetry_frame_;
    for (int substep = 0; substep < N_SUBSTEPS; ++substep) {
        ::logosphere::phystrace::set_substep(substep);
        telemetry_last_substep_ = (substep == N_SUBSTEPS - 1);
        // Fresh dissatisfaction slate BEFORE anyone marks (G-48): the
        // contact build marks first, the gluon build marks later, the
        // sleep gate reads the union at frame end.
        constraint_dissatisfied_.assign(particles.size(), 0);
        if (::logosphere::phystrace::level() >= 2) { dissat_line_.assign(particles.size(), 0); dissat_depth_.assign(particles.size(), 0.0f); dissat_partner_.assign(particles.size(), -1); }
        // PHASE 1-4: Apply gravity, predict, detect, solve constraints
        // V4.2: Angular constraints now solved alongside linear in same iteration loop
        EnergyBuckets e_before, e_after_solve, e_after_angular, e_after_integrate;
        const bool ledger = energy_ledger_on();
        if (ledger) { e_before = measure_energy(particles, gluon_constraints_v2_);
                      g_row_energy = RowEnergy{};
                      g_dissipation = Dissipation{}; }

        auto omega_probe = [&](const char* site) {
            if (!::logosphere::phystrace::at(::logosphere::phystrace::Solve))
                return;
            for (size_t qi = 0; qi < particles.size(); ++qi) {
                if (!::logosphere::phystrace::focused((int)qi, (int)qi)) continue;
                PHYS_TRACE_F(::logosphere::phystrace::Constraint, "omega_probe",  // per body per substep: level 5, not 2
                             (int)qi, (int)qi, site,
                             particles[qi].omega_x, particles[qi].omega_y,
                             particles[qi].omega_z);
            }
        };
        {
            ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsForces);
            apply_all_forces(particles, sub_dt);
        }
        omega_probe("after_forces_and_solve");
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
        omega_probe("after_angular_integrate_and_limits");

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
        omega_probe("after_integrate_positions");
        EnergyBuckets e_after_positions;
        if (ledger) e_after_positions = measure_energy(particles, gluon_constraints_v2_);

        // PHASE 6: Enforce turtle boundary (absolute - nothing goes below z=0)
        {
            ::logosphere::telemetry::ScopedPhase _p(::logosphere::telemetry::Phase::PhysicsBoundary);
            enforce_turtle_boundary(particles);
        }
        omega_probe("after_turtle_boundary");

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
                          << " | dissipated: friction=" << g_dissipation.friction
                          << " material=" << g_dissipation.material
                          << " drag=" << g_dissipation.drag
                          << " sleepCache=" << g_dissipation.sleep
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
    // WHO holds whom awake (G-67's census, level >= 2): per body, which
    // setter marked it dissatisfied (by source line) this frame.
    if (::logosphere::phystrace::level() >= 2 && dissat_line_.size() == particles.size()) {
        std::vector<std::pair<int,int>> held;   // (line, count)
        std::map<int,int> by_line;
        int quiet_held = 0;
        for (size_t i = 0; i < particles.size(); ++i) {
            const Particle& p = particles[i];
            if (p.is_at_rest || dissat_line_[i] == 0) continue;
            if (p.rest_quiet_sq >= REST_VELOCITY_THRESHOLD * REST_VELOCITY_THRESHOLD) continue;
            ++quiet_held; by_line[dissat_line_[i]]++;
        }
        for (auto& kv : by_line)
            PHYS_TRACE(::logosphere::phystrace::Frame, "dissat_by_line", kv.first, kv.second,
                       "setter_line_count", (double)quiet_held, 0.0);
        // the ten deepest quiet-held bodies, by their recorded depth
        std::vector<std::pair<float,int>> deep;
        for (size_t i = 0; i < particles.size(); ++i) {
            const Particle& p = particles[i];
            if (p.is_at_rest || dissat_line_[i] == 0) continue;
            if (p.rest_quiet_sq >= REST_VELOCITY_THRESHOLD * REST_VELOCITY_THRESHOLD) continue;
            deep.push_back({dissat_depth_[i], (int)i});
        }
        // The gluon-held bodies carry no depth: name the first ten by line.
        {
            int shown = 0;
            for (size_t i = 0; i < particles.size() && shown < 10; ++i) {
                const Particle& p = particles[i];
                if (p.is_at_rest || dissat_line_[i] == 0 || dissat_depth_[i] > 0.0f) continue;
                if (p.rest_quiet_sq >= REST_VELOCITY_THRESHOLD * REST_VELOCITY_THRESHOLD) continue;
                char why[200];
                std::snprintf(why, sizeof why,
                    "P%zu %s mat=%d m=%.4g dims=(%.3g,%.3g,%.3g) z=%.3g bonds=%zu line=%d",
                    i, p.shape == ParticleShape::BOX ? "BOX" : "SPH", (int)p.material_type,
                    p.GetMass(), p.width, p.height, p.thickness, p.z,
                    get_gluons_for_particle((int)i).size(), dissat_line_[i]);
                PHYS_TRACE(::logosphere::phystrace::Frame, "dissat_gluon_who", shown, 0, why, 0.0, 0.0);
                ++shown;
            }
        }
        std::sort(deep.begin(), deep.end(), [](auto& x, auto& y){ return x.first > y.first; });
        for (size_t k = 0; k < deep.size() && k < 10; ++k) {
            const Particle& p = particles[deep[k].second];
            char why[200];
            std::snprintf(why, sizeof why,
                "P%d %s mat=%d m=%.4g dims=(%.3g,%.3g,%.3g) at(%.1f,%.1f,%.3f) bonds=%zu line=%d partner=P%d",
                deep[k].second, p.shape == ParticleShape::BOX ? "BOX" : "SPH", (int)p.material_type,
                p.GetMass(), p.width, p.height, p.thickness, p.x, p.y, p.z,
                get_gluons_for_particle(deep[k].second).size(), dissat_line_[deep[k].second],
                dissat_partner_[deep[k].second]);
            PHYS_TRACE(::logosphere::phystrace::Frame, "dissat_who", (int)k, 0, why,
                       (double)deep[k].first, 0.0);
        }
    }
    // Level 1 FRAME record, as the trace header promises: bodies, how many
    // sleep, how many are awake yet under the rest speed, and how many of
    // those the dissatisfaction gate holds (G-67's census).
    if (::logosphere::phystrace::level() >= 1) {
        int asleep = 0, awake_quiet = 0, awake_dissatisfied = 0;
        for (size_t i = 0; i < particles.size(); ++i) {
            const Particle& p = particles[i];
            if (p.is_at_rest) { ++asleep; continue; }
            if (p.GetMass() == 0.0f) continue;
            if (p.rest_quiet_sq < REST_VELOCITY_THRESHOLD * REST_VELOCITY_THRESHOLD)
                ++awake_quiet;
            if (i < constraint_dissatisfied_.size() && constraint_dissatisfied_[i])
                ++awake_dissatisfied;
        }
        PHYS_TRACE(::logosphere::phystrace::Frame, "frame", (int)particles.size(),
                   asleep, "asleep_quiet_dissatisfied", (double)awake_quiet,
                   (double)awake_dissatisfied);
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
// THE SLEEPER'S SUPPORT (G-72, INV-31)
// ============================================================================
// A body may be DECLARED asleep from outside the sleep law - a chunk
// restored from the store, a scene's born-at-rest tile, a game's rock - but
// it may only STAY asleep if it rests on something. Eden's store births its
// tiles 2-7 mm above their supports and asleep; two sleepers never pair in
// the broad phase, so nothing ever finds the gap, and a stone dropped on
// one lands on a tile held by nothing (test_jammed_sleep, case G).
//
// Judged ONCE, at the first update after the declaration. A declared
// sleeper is a body asleep without the law's own count: frames_at_rest is
// below REST_FRAMES_REQUIRED, because the law reaches that count as it puts
// a body to sleep and every wake site zeroes it. Supported means: a bond
// holds it (its strain is the bond law's business, and it wakes through the
// dissatisfaction gate), or the turtle or a live body lies within SLOP along
// the direction it would fall - the solver's gravity, so nothing falls in
// zero-g. The measure is the creation door's own (ParticleSystem::
// overlap_depth): one index, one narrow phase, no second geometry. A
// sleeper with nothing under it is woken, loudly, and falls to its support
// like any other body; a supported one is booked as earned and never
// judged again.
// ============================================================================
// INV-39: A BODY MOVED FROM OUTSIDE CARRIES ITS MOTION (registered 2026-09-05,
// G-77 the moving platform, G-78 the turning post; born red, measured: a slab
// slid 1.254 m under a cube that never moved, a post turned 1.5 rad above an
// arm that never turned).
//
// A KINEMATIC body is a body of infinite mass with PRESCRIBED motion: its
// writer owns its position and orientation, and that motion is state the
// solver must know when it prices relative velocity. Nothing wrote the
// velocity ledger of such a body, so every row against it read zero: a
// contact held a cube against a floor that was sliding away; a nail damped
// an arm against a post that was turning; a drive's rows welded an arm to a
// forearm read as still (G-75). One missing state, many symptoms.
//
// The derivation, in one place, before any row exists: v = dx/dt, omega from
// the shortest-arc axis-angle of q * prev_q^-1 over dt (the integrator's own
// convention: q_new = from_axis_angle(w_hat, |w| dt) * q_old). The momentum
// door is untouched (INV-7: KINEMATIC takes no impulse); the integrators skip
// KINEMATIC bodies, so the ledger is read by the rows and never spent on the
// body. A writer's jump is a velocity the law makes visible (INV-11).
//
// INV-18, the coherence contract of sleep: a support that moves invalidates
// the equilibrium of what rests on it or hangs from it. A moving KINEMATIC
// body therefore wakes its bonded partners and every sleeper it touches
// (bounding spheres within SLOP), through the one wake path.
//
// Lever KINEMATIC_LEDGER: default OFF, byte-identical to the old world, until
// the owner's QA. No new constant: SLOP and ANGULAR_SLOP are the engine's own
// bars for 'error, not motion'.
// ============================================================================
void PhysicsSystem::derive_kinematic_motion(ParticleSystem::WriteView& particles, float dt) {
    static const bool ledger_on = std::getenv("KINEMATIC_LEDGER") != nullptr;
    if (!ledger_on || dt <= 0.0f) return;
    const size_t count = particles.size();
    for (size_t i = 0; i < count; ++i) {
        Particle& k = particles[i];
        if (k.solver_mode != ParticleSolverMode::KINEMATIC) continue;
        bool moved = false;
        if (k.prev_valid) {
            k.vx = (k.x - k.prev_x) / dt;
            k.vy = (k.y - k.prev_y) / dt;
            k.vz = (k.z - k.prev_z) / dt;
            const logosphere::Quat dq = (k.rotation_q * k.prev_q.conjugate()).normalized();
            float ax = 0.0f, ay = 0.0f, az = 1.0f, th = 0.0f;
            dq.to_axis_angle(ax, ay, az, th);
            if (th > static_cast<float>(M_PI)) th -= 2.0f * static_cast<float>(M_PI);   // the shortest arc
            const float rate = th / dt;
            k.omega_x = ax * rate; k.omega_y = ay * rate; k.omega_z = az * rate;
            const float dx = k.x - k.prev_x, dy = k.y - k.prev_y, dz = k.z - k.prev_z;
            moved = (dx*dx + dy*dy + dz*dz > PhysicsV4::SLOP * PhysicsV4::SLOP) ||
                    (std::fabs(th) > PhysicsV4::ANGULAR_SLOP);
        }
        k.prev_x = k.x; k.prev_y = k.y; k.prev_z = k.z;
        k.prev_q = k.rotation_q;
        k.prev_valid = true;
        if (!moved) continue;
        // What it holds: the other end of every bond.
        for (const GluonConstraintBase* g : get_gluons_for_particle(i)) {
            if (!g) continue;
            const size_t other = (g->particle_a == i) ? g->particle_b : g->particle_a;
            if (other < count && particles[other].is_at_rest &&
                particles[other].solver_mode != ParticleSolverMode::KINEMATIC)
                wake_particle_with_propagation(other, particles, 0.0f);
        }
        // What it carries: every sleeper within SLOP of its bounding sphere.
        const float rk = 0.5f * std::sqrt(k.width*k.width + k.height*k.height + k.thickness*k.thickness);
        for (size_t j = 0; j < count; ++j) {
            if (j == i) continue;
            const Particle& s = particles[j];
            if (!s.is_at_rest || s.solver_mode == ParticleSolverMode::KINEMATIC) continue;
            const float rs = 0.5f * std::sqrt(s.width*s.width + s.height*s.height + s.thickness*s.thickness);
            const float ddx = s.x - k.x, ddy = s.y - k.y, ddz = s.z - k.z;
            const float reach = rk + rs + PhysicsV4::SLOP;
            if (ddx*ddx + ddy*ddy + ddz*ddz <= reach * reach) {
                wake_particle_with_propagation(j, particles, 0.0f);
                static const bool dbg = std::getenv("KINEMATIC_LEDGER_DEBUG") != nullptr;
                if (dbg) std::printf("[KIN LEDGER] P%zu (v %.3f,%.3f,%.3f) woke sleeper P%zu; at_rest now %d\n",
                                     i, k.vx, k.vy, k.vz, j, (int)particles[j].is_at_rest);
            }
        }
    }
}

void PhysicsSystem::admit_declared_sleepers(ParticleSystem::WriteView& particles) {
    // The kill switch (INV-36's pattern): SLEEPER_JUDGE=0 restores the old
    // behaviour - a declared sleeper is taken at its word - for A/B only.
    static const bool judge_off = [] {
        const char* v = std::getenv("SLEEPER_JUDGE");
        return v && v[0] == '0';
    }();
    if (judge_off) return;
    const size_t count = particles.size();
    std::vector<size_t> declared;
    for (size_t i = 0; i < count; ++i) {
        const Particle& p = particles[i];
        if (!p.is_at_rest || p.frames_at_rest >= REST_FRAMES_REQUIRED) continue;
        if (p.solver_mode == ParticleSolverMode::KINEMATIC || p.GetMass() == 0.0f) continue;
        if (p.is_light_source) continue;   // a light is not a body (the door's rule)
        declared.push_back(i);
    }
    if (declared.empty()) return;

    std::vector<uint8_t> bonded(count, 0);
    for (const auto& g : gluon_constraints_v2_) {
        if (g->particle_a < count) bonded[g->particle_a] = 1;
        if (g->particle_b < count) bonded[g->particle_b] = 1;
    }
    const Vec3 g = get_solver_gravity();
    const float g_len = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);

    for (size_t i : declared) {
        Particle& p = particles[i];
        bool supported = bonded[i] || g_len == 0.0f;
        if (!supported) {
            Particle probe = p;   // the body, one SLOP further along its fall
            probe.x += g.x / g_len * SLOP;
            probe.y += g.y / g_len * SLOP;
            probe.z += g.z / g_len * SLOP;
            supported = logosphere::describe_creation_body(-1, probe).world_min_z < TURTLE_Z ||
                        particle_system_->overlap_depth(probe, static_cast<int>(i)) > 0.0f;
        }
        if (supported) {
            p.frames_at_rest = REST_FRAMES_REQUIRED;   // earned by geometry, judged once
            continue;
        }
        p.is_at_rest = false;
        p.frames_at_rest = 0;
        ++woken_at_birth_;
        const logosphere::CreationBody b = logosphere::describe_creation_body(static_cast<int>(i), p);
        std::fprintf(stderr,
                     "[PHYSICS WOKEN] body %zu declared asleep with nothing under it within "
                     "%.4f m: %s half %.4fx%.4fx%.4f m at (%.3f, %.3f, %.3f), bottom z %.4f "
                     "- a sleeper rests on its support (G-72, INV-31)\n",
                     i, SLOP, b.shape, b.half[0], b.half[1], b.half[2],
                     p.x, p.y, p.z, b.world_min_z);
        PHYS_TRACE_F(::logosphere::phystrace::Pair, "woken_at_birth", (int)i, -1,
                     "unsupported", b.world_min_z, 0.0f, 0.0f);
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
// ============================================================================
// THE ONE DOOR FOR MOMENTUM
// ============================================================================
// One predicate answers "can this body receive momentum?", and one function
// applies every pairwise impulse. Before this existed the predicate was
// re-derived inline at 8 division sites with three different formulas
// (KINEMATIC-only at the gluon row build, at_rest||KINEMATIC at the impulse
// applies, both plus a mass guard at the position projection), and the V4.9
// damping blend had a fourth opinion: none. A sleeping body was infinite
// mass to gravity and to impulses but movable to that blend, which is how a
// 111.661 kg branch sank 6.74 m while asleep (see the damping site).
//
// The law: massless, sleeping and KINEMATIC bodies do not receive momentum.
// Sleep is an optimisation about momentum only. Geometric repair answers a
// different question with a different predicate (inv_mass_positional in the
// split-impulse position pass), on purpose: a sleeping body's overlap is as
// real as an awake one's.
// INV-31 (owner decree 2026-08-14): WAKE IS SOLVED PHYSICS. With the
// resolver on, a sleeping body stops claiming immovability — INV-1 says
// the turtle is the only immovable thing, and a 45 kg crate that has
// been still for a second is not the turtle. It is priced at its TRUE
// mass, the solver computes the interaction honestly, and the wake
// decision is the RESULT: above the quietness bound that admitted it to
// sleep it wakes and keeps what physics gave it; below, the cache
// absorbs the residue (resolve_sleep_wakes, end of this file's solve).
// Gravity is skipped for sleepers at its own site, on its own reason:
// a resting body's weight is balanced by its support, so skipping the
// pair is exact rather than an immovability claim.
//
// Default OFF until measured: WAKE_RESOLVER=1.
static bool wake_resolver_on() {
    static const bool on = std::getenv("WAKE_RESOLVER") != nullptr;
    return on;
}

// An axis-aligned box as an OBB: the merged surface of a tile family handed
// to the oriented narrow phase (night 2026-09-04, journal 8).
static inline OBB obb_of_aabb(const AABB6& b) {
    OBB o;
    o.c[0] = 0.5f * (b.min_x + b.max_x);
    o.c[1] = 0.5f * (b.min_y + b.max_y);
    o.c[2] = 0.5f * (b.min_z + b.max_z);
    o.axis[0][0] = 1.0f; o.axis[0][1] = 0.0f; o.axis[0][2] = 0.0f;
    o.axis[1][0] = 0.0f; o.axis[1][1] = 1.0f; o.axis[1][2] = 0.0f;
    o.axis[2][0] = 0.0f; o.axis[2][1] = 0.0f; o.axis[2][2] = 1.0f;
    o.half[0] = 0.5f * (b.max_x - b.min_x);
    o.half[1] = 0.5f * (b.max_y - b.min_y);
    o.half[2] = 0.5f * (b.max_z - b.min_z);
    return o;
}

static inline float inv_mass_momentum(const Particle& p) {
    if (p.solver_mode == ParticleSolverMode::KINEMATIC) return 0.0f;
    if (p.is_at_rest && !wake_resolver_on()) return 0.0f;
    const float m = p.GetMass();
    return (m > 0.0f) ? (1.0f / m) : 0.0f;
}

// Equal and opposite by construction: +J to a, -J to b, each side through
// the predicate. A body the predicate calls immovable receives J * 0, so
// "forgot the guard" is not a writable bug at a door call site.
static inline void apply_pair_impulse(Particle& a, Particle& b,
                                      float jx, float jy, float jz) {
    const float ia = inv_mass_momentum(a);
    const float ib = inv_mass_momentum(b);
    a.vx += jx * ia;  a.vy += jy * ia;  a.vz += jz * ia;
    b.vx -= jx * ib;  b.vy -= jy * ib;  b.vz -= jz * ib;
}

// THE SLEEP-AWAKE RESOLVER (INV-31). Runs once per substep, after the
// solve, before integration — so a body that stays asleep never
// integrates a velocity it was not allowed to keep.
//
// Three outcomes per sleeping body, and the solved velocity decides:
//   |v| > REST_VELOCITY_THRESHOLD  -> WAKE. The interaction was real;
//        the body keeps exactly what the solver gave it and rejoins
//        the world (gravity, integration, its own contacts).
//   |v| <= bound, |v| > 0          -> the cache ABSORBS it. The residue
//        is below the quietness that defines sleep, so zeroing it is
//        the cache being a cache. Booked as dissipation (task #44).
//   |v| == 0                       -> nothing happened. The common case:
//        resting stacks whose rows see no approach cost nothing here.
//
// What this replaces: a pre-solve guess
// ((m_a/(m_a+m_s))*v_a >= WAKE_TRANSFER_SPEED) that priced the sleeper
// as immovable whenever it said no — which annihilated 343 kg*m/s
// against a sleeping equal-mass crate in the Rube Goldberg machine
// (probe f271) and made a grain of sand and a castle wall the same
// question.
static void resolve_sleep_wakes(ParticleSystem::WriteView& particles) {
    const size_t count = particles.size();
    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];
        if (!p.is_at_rest) continue;
        if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;
        const float v_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;
        const float w_sq = p.omega_x * p.omega_x + p.omega_y * p.omega_y +
                           p.omega_z * p.omega_z;
        if (v_sq == 0.0f && w_sq == 0.0f) continue;
        if (w_sq > 0.0f && v_sq <= REST_VELOCITY_THRESHOLD *
                                   REST_VELOCITY_THRESHOLD) {
            // Spin the cache cannot hold: below the linear bound but
            // turning. Absorb it with the rest of the residue rather than
            // leaving a sleeper rotating.
            p.omega_x = p.omega_y = p.omega_z = 0.0f;
        }
        if (v_sq > REST_VELOCITY_THRESHOLD * REST_VELOCITY_THRESHOLD) {
            p.is_at_rest = false;
            p.low_velocity_frames = 0;
            PHYS_TRACE_F(::logosphere::phystrace::Pair, "resolver_wake",
                         (int)i, -1, "solved_impulse",
                         std::sqrt(v_sq), 0.0f, 0.0f);
        } else {
            // The cache absorbs it. Book the kinetic energy destroyed
            // here, separately from the modelled processes: this is
            // bookkeeping about an optimisation, not physics, and INV-19
            // only sanctions the latter.
            if (energy_ledger_on()) {
                const double m = p.GetMass();
                if (m > 0.0) g_dissipation.sleep += 0.5 * m * double(v_sq);
            }
            p.vx = p.vy = p.vz = 0.0f;
        }
    }
}

void PhysicsSystem::record_refused_impulse(size_t particle_id,
                                           float jx, float jy, float jz) {
    auto& e = refused_impulses_[particle_id];
    e[0] += jx; e[1] += jy; e[2] += jz;
}

bool PhysicsSystem::take_refused_impulse(size_t particle_id,
                                         float& jx, float& jy, float& jz) {
    auto it = refused_impulses_.find(particle_id);
    if (it == refused_impulses_.end()) return false;
    jx = it->second[0]; jy = it->second[1]; jz = it->second[2];
    refused_impulses_.erase(it);
    return true;
}

void PhysicsSystem::apply_all_forces(ParticleSystem::WriteView& particles, float dt) {
    const size_t count = particles.size();

    // Phase 1: Apply gravity to dynamic, non-resting particles
    // V4.4: Skip at-rest particles. Contact constraints are also skipped for at-rest
    // particles, so gravity must be skipped too or particle will wake every frame.
    // When a particle wakes (external collision), it will get gravity again.
    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];
        // Gravity is momentum input, and who may receive momentum is the
        // door's one question: massless, KINEMATIC and sleeping bodies no.
        if (inv_mass_momentum(p) == 0.0f) continue;
        // THE SLEEP CACHE, stated: a resting body's weight is carried by
        // its support, and skipping both sides of that balanced pair is
        // exact. This is why sleep is cheap — not because the body is
        // immovable (INV-1: only the turtle is).
        if (p.is_at_rest) continue;
        if (const char* cenv = std::getenv("CANARY_PID"); cenv && (int)i == std::atoi(cenv)) {
            std::cout << "[CANARY GRAVITY] P" << i << " receives gravity, vz_pre=" << p.vz
                      << " at_rest=" << (int)p.is_at_rest << std::endl;
        }
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

    // Phase 5: THE SLEEP-AWAKE RESOLVER (INV-31). Every sleeping body
    // that the solve actually moved is judged here, on the solved
    // result, against the same bound that admitted it to sleep.
    if (wake_resolver_on()) resolve_sleep_wakes(particles);
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
    // THE SINGLE LAW OF CONTACT (G-63, owner ruling 2026-08-31,
    // default off). A contact is one unilateral constraint: the
    // impulse is whatever complementarity demands, bounded by nothing
    // but the material. Under the lever the velocity-priced capture
    // cap is REMOVED (every contact cap site below) and each contact
    // manifold's normal rows are solved as a BLOCK per iteration
    // (pattern from feat/joint-block-solver), so passivity is a
    // property of convergence, not a clamp. The jury: INV-17, the
    // energy ledger, the refused-momentum ledger, the explosion
    // detector.
    // THE FLIP (owner ruling 2026-09-01, INV-36): the single law is the
    // shipped default; SINGLE_LAW=0 is the kill switch for A/B, never a
    // shipping mode. Same for FRICTION_TWIST, WARM_LEARN, MANIFOLD_SPAN.
    static const bool single_law =
        []{ const char* e = std::getenv("SINGLE_LAW"); return !(e && e[0] == '0' && e[1] == '\0'); }();
    // RCA sub-switches (2026-09-01, the flip's bond-world moles): the
    // plateau door's residual guard and the position gate are the two
    // single-law refinements that touch EVERY row, gluon rows included.
    // =0 isolates each. Diagnostic; not shipping modes.
    static const bool single_law_door =
        []{ const char* e = std::getenv("SINGLE_LAW_DOOR"); return !(e && e[0] == '0' && e[1] == '\0'); }();
    static const bool single_law_gate =
        []{ const char* e = std::getenv("SINGLE_LAW_GATE"); return !(e && e[0] == '0' && e[1] == '\0'); }();
    // Two more RCA sub-switches: SINGLE_LAW_CAPS=0 keeps the legacy
    // capture caps under the single law; SINGLE_LAW_BLOCKS=0 skips the
    // manifold blocks (sequential rows). Diagnostic only.
    static const bool single_law_uncapped =
        []{ const char* e = std::getenv("SINGLE_LAW_CAPS"); return !(e && e[0] == '0' && e[1] == '\0'); }();
    static const bool single_law_blocks =
        []{ const char* e = std::getenv("SINGLE_LAW_BLOCKS"); return !(e && e[0] == '0' && e[1] == '\0'); }();
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
        // CANARY_AT=x,y,z: pick the body nearest that point (ids follow the
        // chunk streaming order and differ run to run; a place does not).
        if (const char* at = std::getenv("CANARY_AT")) {
            float ax = 0, ay = 0, az = 0;
            if (std::sscanf(at, "%f,%f,%f", &ax, &ay, &az) == 3) {
                float best = 1e30f;
                for (size_t i = 0; i < count; ++i) {
                    const Particle& q = particles[i];
                    const float d = (q.x-ax)*(q.x-ax) + (q.y-ay)*(q.y-ay) + (q.z-az)*(q.z-az);
                    if (d < best) { best = d; canary_pid = (int)i; }
                }
                std::cout << "[CANARY AT] (" << ax << "," << ay << "," << az << ") -> P"
                          << canary_pid << std::endl;
            }
        }
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

    // ==========================================================================
    // BONDED-STRUCTURE COMPONENTS (union-find over the gluon graph)
    // ==========================================================================
    // The direct-pair skip below states the principle: gluons already
    // constrain the pair, and a contact on top is a conflicting constraint.
    // A bonded STRUCTURE is that statement transitively: a tree's crown is
    // built with branches and foliage boxes deliberately crossing each other
    // (test_foliage_stays_attached's own header: perfect separation at birth
    // costs the complexity that is the point of the tree). Those crossings
    // are the structure's internal geometry, held by its bonds; contact rows
    // inside one structure fight the bonds — measured when the narrow phase
    // became rotation-aware and first SAW the crossings: 0.80 m
    // "penetrations" between a trunk and its own crown's foliage boxes,
    // leaves ejected at 6.77 m/s against a 2.0 gate.
    //
    // Connectivity runs through DYNAMIC bodies only. An immovable anchor is
    // ground, not structure: two plants rooted to the same KINEMATIC tile
    // are two plants, and their blades still collide with each other.
    // Rebuilt every step from the live gluon list (~one union per gluon),
    // so torn bonds dissolve the component with no stale state.
    std::vector<uint32_t> bond_root(count);
    for (size_t i = 0; i < count; ++i) bond_root[i] = (uint32_t)i;
    {
        auto find_root = [&bond_root](uint32_t x) {
            while (bond_root[x] != x) {
                bond_root[x] = bond_root[bond_root[x]];   // path halving
                x = bond_root[x];
            }
            return x;
        };
        for (const auto& gluon : gluon_constraints_v2_) {
            const size_t a = gluon->particle_a, b = gluon->particle_b;
            if (a >= count || b >= count) continue;
            if (particles[a].solver_mode == ParticleSolverMode::KINEMATIC ||
                particles[b].solver_mode == ParticleSolverMode::KINEMATIC)
                continue;   // ground link, not structure
            const uint32_t ra = find_root((uint32_t)a);
            const uint32_t rb = find_root((uint32_t)b);
            if (ra != rb) bond_root[ra] = rb;
        }
        // Flatten so the pair loop's comparison is one array read each.
        for (size_t i = 0; i < count; ++i)
            bond_root[i] = find_root((uint32_t)i);
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

        // Calculate particle bottom at predicted position. A rotated BOX's
        // down-reach comes from its oriented bounds (the turtle is the z=0
        // plane; its normal is +Z by definition — plane geometry, not a
        // gravity assumption). Unrotated boxes and other shapes keep the
        // raw thickness extent, which for them is exact. Must stay in
        // lock-step with enforce_turtle_boundary below.
        //
        // Cost guard: no orientation can reach further than the half
        // diagonal, so a body with more clearance than that needs no exact
        // extent (quat + matrix) — and no extent choice could fire the
        // threshold test anyway. Measured on Eden headless 1600px: without
        // this, two full-particle turtle loops per substep re-derived
        // orientations for thousands of resting canopy boxes.
        // V4.3 FIX: Create constraint for particles NEAR turtle, not just penetrating
        // This prevents "contact bouncing" where particles leave contact between frames.
        // Particles within 3mm of turtle surface maintain contact (prevents oscillation).

        float half_zi = pi.thickness * 0.5f;
        if (pi.shape == ParticleShape::BOX && box_particle_is_rotated(pi)) {
            const float hx = pi.width * 0.5f, hy = pi.height * 0.5f,
                        hz = pi.thickness * 0.5f;
            const float reach_sq = hx * hx + hy * hy + hz * hz;
            const float clearance =
                z_predicted[i] - (TURTLE_Z + TURTLE_CONTACT_THRESHOLD);
            if (clearance <= 0.0f || clearance * clearance < reach_sq) {
                const AABB6 w =
                    aabb_of_obb(obb_of_box_particle(pi, z_predicted[i]));
                half_zi = (w.max_z - w.min_z) * 0.5f;
            }
        }
        float particle_bottom = z_predicted[i] - half_zi;

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

            // TURTLE CONTACT PATCH (G-39 slice C, corrected; same
            // CONTACT_TORQUE lever). The first version used ONE support
            // vertex — a step function in orientation. A tumbling cube
            // landing near-flat struck one invented corner: the lane
            // trace showed y = -1.200 exact for the entire 135-frame
            // descent, then -1.20 -> -2.07 beginning at turtle
            // touchdown, with no lateral force in the scene. The honest
            // contact of a near-flat landing is a PATCH: every corner
            // within SUPPORT_PATCH_BAND of the lowest carries a row,
            // load shared, each with its own lever arm — the same
            // answer box-box already gives through manifold clipping.
            // Rows are emitted at the push site below; here the corners
            // are collected. Unrotated boxes keep the legacy single
            // row: a flat face's torques cancel by symmetry.
            int   patch_n = 0;
            float patch_r[4][3];
            float patch_pen[4];
            {
                static const bool contact_torque_on2 =
                    []{ const char* e = std::getenv("CONTACT_TORQUE"); return !(e && e[0] == '0' && e[1] == '\0'); }()  /* INV-32: torque is default physics; =0 is the kill switch */;
                if (contact_torque_on2 &&
                    pi.solver_mode == ParticleSolverMode::DYNAMIC &&
                    pi.shape == ParticleShape::BOX &&
                    box_particle_is_rotated(pi)) {
                    const OBB o = obb_of_box_particle(pi, z_predicted[i]);
                    float cz[8], cx[8], cy[8];
                    float zmin = 1e30f;
                    int k = 0;
                    for (int s0 = -1; s0 <= 1; s0 += 2)
                    for (int s1 = -1; s1 <= 1; s1 += 2)
                    for (int s2 = -1; s2 <= 1; s2 += 2) {
                        cx[k] = s0*o.half[0]*o.axis[0][0] + s1*o.half[1]*o.axis[1][0]
                              + s2*o.half[2]*o.axis[2][0];
                        cy[k] = s0*o.half[0]*o.axis[0][1] + s1*o.half[1]*o.axis[1][1]
                              + s2*o.half[2]*o.axis[2][1];
                        cz[k] = s0*o.half[0]*o.axis[0][2] + s1*o.half[1]*o.axis[1][2]
                              + s2*o.half[2]*o.axis[2][2];
                        if (cz[k] < zmin) zmin = cz[k];
                        ++k;
                    }
                    for (int q = 0; q < 8 && patch_n < 4; ++q) {
                        if (cz[q] - zmin <= SUPPORT_PATCH_BAND) {
                            patch_r[patch_n][0] = cx[q];
                            patch_r[patch_n][1] = cy[q];
                            patch_r[patch_n][2] = cz[q];
                            patch_pen[patch_n] =
                                TURTLE_Z - (z_predicted[i] + cz[q]);
                            ++patch_n;
                        }
                    }
                }
            }

            // Effective mass: only particle's mass (Turtle has infinite mass)
            // effective_mass = 1 / (1/ma + 0) = ma
            //
            // This was `mass > 0 ? mass : 1.0f` — the THIRD copy of the
            // eff-mass phantom (S22 killed it at the contact build, the
            // gluon build killed it again; inventory B1). Massless is
            // skipped at the top of this loop, so the invented 1.0f was an
            // unreachable arm waiting for that guard to change. If mass is
            // not positive here, particle data is broken: refuse loud.
            {
                const float m_turtle = pi.GetMass();
                if (!(m_turtle > 0.0f)) {
                    std::fprintf(stderr,
                        "[PHYSICS REFUSED] turtle row for P%zu with mass %g "
                        "(massless bodies are skipped above; a non-positive "
                        "mass reaching the row build is corrupted particle "
                        "data).\n", i, m_turtle);
                    if (!std::getenv("PHYSICS_LENIENT")) std::abort();
                    continue;   // lenient: drop the row, never invent a mass
                }
                c.effective_mass = m_turtle;
            }

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
                c.build_approach = approach;   // G-63: the store law's source
                // G-63: under the single law the cap is gone, not resized.
                c.max_impulse = (single_law && single_law_uncapped)
                    ? std::numeric_limits<float>::max()
                    : c.effective_mass *
                      (approach + PhysicsV4::CONTACT_CAPTURE_CUSHION);
            }
            c.accumulated_impulse = 0.0f;

            // Mark as contact for friction processing
            c.is_contact = true;
            c.friction_impulse_t1 = 0.0f;
            c.friction_impulse_t2 = 0.0f;

            if (patch_n > 0) {
                // The patch: one row per support corner, load shared,
                // each priced with its own arm (INV-20). The base row
                // above carries the shared bias/caps machinery; each
                // clone re-derives what depends on ITS corner.
                // PENETRATION-WEIGHTED SHARES (rotation item 3). Equal
                // shares made the patch a STABILIZER of the 45-degree
                // balance: at that angle two corners tie inside the 5 mm
                // band, the symmetric row pair produces zero net torque
                // with a restoring bias, and honest friction then parks
                // the cube on a knife-edge widened into a ~2-degree trap
                // (measured: every wild 3-D tumble ended at z = 0.346,
                // edge height, omega 0). Real contact pressure loads the
                // deeper corner harder; weighting each row's share by its
                // positive penetration restores the knife-edge to a
                // knife-edge, with the equal split kept as the fallback
                // when nothing penetrates yet (speculative band).
                float wsum = 0.0f;
                for (int q = 0; q < patch_n; ++q)
                    wsum += (patch_pen[q] > 0.0f) ? patch_pen[q] : 0.0f;
                const float I = pi.GetMomentOfInertia();
                const float m_i = pi.GetMass();
                // G-55 (lever FRICTION_TWIST=1): a >=3-corner turtle
                // patch is a FACE contact; its rotational braking
                // belongs to the twist carrier (the deepest corner,
                // whose share reconstruction of N_total is stable),
                // its tangents go linear-only.
                static const bool friction_twist_t =
                    []{ const char* e = std::getenv("FRICTION_TWIST"); return !(e && e[0] == '0' && e[1] == '\0'); }();  // INV-36 default; =0 kills
                const bool patch_face = friction_twist_t && patch_n >= 3;
                int q_deep = 0;
                float patch_r2_max = 0.0f;
                if (patch_face) {
                    for (int q = 0; q < patch_n; ++q) {
                        if (patch_pen[q] > patch_pen[q_deep]) q_deep = q;
                        const float r2 = patch_r[q][0]*patch_r[q][0]
                                       + patch_r[q][1]*patch_r[q][1];
                        patch_r2_max = std::fmax(patch_r2_max, r2);
                    }
                }
                for (int q = 0; q < patch_n; ++q) {
                    const float share = (wsum > 1e-6f)
                        ? std::max(patch_pen[q], 0.0f) / wsum
                        : 1.0f / (float)patch_n;
                    Constraint ck = c;
                    ck.apply_anchor_torque = true;
                    ck.anchor_rax = patch_r[q][0];
                    ck.anchor_ray = patch_r[q][1];
                    ck.anchor_raz = patch_r[q][2];
                    ck.anchor_rbx = ck.anchor_rby = ck.anchor_rbz = 0.0f;
                    if (patch_face) {
                        ck.face_friction = true;
                        if (q == q_deep) {
                            ck.twist_carrier = true;
                            ck.twist_r = PhysicsV4::TWIST_RADIUS_FACTOR *
                                         std::sqrt(2.0f * patch_r2_max);
                        }
                    }
                    const float pen_q = patch_pen[q];
                    ck.bias = (pen_q > SLOP)
                        ? std::min(BETA * (pen_q - SLOP) / dt,
                                   MAX_BIAS_VELOCITY)
                        : 0.0f;
                    ck.penetration = pen_q;
                    float k_row = (m_i > 0.0f) ? 1.0f / m_i : 0.0f;
                    if (I > 0.0f) {
                        const float rx = ck.anchor_ray;   // r x (0,0,1)
                        const float ry = -ck.anchor_rax;
                        k_row += (rx*rx + ry*ry) / I;
                    }
                    ck.effective_mass =
                        (k_row > 0.0f) ? (1.0f / k_row) * share : 0.0f;
                    ck.eff_mass_share = share;
                    {
                        const float approach = std::fabs(pi.vz);
                        ck.build_approach = approach;
                        // G-63: no cap under the single law.
                        ck.max_impulse = (single_law && single_law_uncapped)
                            ? std::numeric_limits<float>::max()
                            : ck.effective_mass *
                              (approach + PhysicsV4::CONTACT_CAPTURE_CUSHION);
                    }
                    PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                                 (int)ck.body_a, (int)ck.body_b,
                                 "turtle_patch", ck.bias, ck.effective_mass,
                                 ck.jz);
                    constraints.push_back(ck);
                }
            } else {
                PHYS_TRACE_F(::logosphere::phystrace::Pair, "row_created",
                             (int)c.body_a, (int)c.body_b, "turtle",
                             c.bias, c.effective_mass, c.jz);
                constraints.push_back(c);
            }

            // THE SLEEP LAW HEARS CONTACTS TOO (G-48): real penetration
            // beyond the tolerance marks the body dissatisfied, so sleep
            // cannot freeze it mid-repair. Speculative gaps do not mark.
            if (penetration > SLEEP_PEN_TOLERANCE &&
                i < constraint_dissatisfied_.size()) {
                constraint_dissatisfied_[i] = 1;
                dissat_note(i, __LINE__, penetration, -1);
            }

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
        // Particle dimensions: width=X, height=Y, thickness=Z.
        // A ROTATED box's raw extents under-cover its world span (a tilted
        // blade's length leaves Z and enters Y), so its bounds come from the
        // oriented box instead — exact for a box, never loose. Unrotated
        // particles keep the raw arithmetic to the bit.
        const bool obb_i = (pi.shape == ParticleShape::BOX) &&
                           box_particle_is_rotated(pi);
        OBB obb_i_box;   // valid only when obb_i
        float min_xi, max_xi, min_yi, max_yi, min_zi, max_zi;
        if (obb_i) {
            obb_i_box = obb_of_box_particle(pi, z_predicted[i]);
            const AABB6 w = aabb_of_obb(obb_i_box);
            min_xi = w.min_x; max_xi = w.max_x;
            min_yi = w.min_y; max_yi = w.max_y;
            min_zi = w.min_z; max_zi = w.max_z;
        } else {
            const float half_xi = pi.width * 0.5f;
            const float half_yi = pi.height * 0.5f;
            const float half_zi = pi.thickness * 0.5f;
            min_xi = pi.x - half_xi;
            max_xi = pi.x + half_xi;
            min_yi = pi.y - half_yi;
            max_yi = pi.y + half_yi;
            min_zi = z_predicted[i] - half_zi;
            max_zi = z_predicted[i] + half_zi;
        }

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

            // Particle j AABB (using predicted Z) — same oriented-bounds
            // rule as particle i above.
            const bool obb_j = (pj.shape == ParticleShape::BOX) &&
                               box_particle_is_rotated(pj);
            OBB obb_j_box;   // valid only when obb_j
            float min_xj, max_xj, min_yj, max_yj, min_zj, max_zj;
            if (obb_j) {
                obb_j_box = obb_of_box_particle(pj, z_predicted[j]);
                const AABB6 w = aabb_of_obb(obb_j_box);
                min_xj = w.min_x; max_xj = w.max_x;
                min_yj = w.min_y; max_yj = w.max_y;
                min_zj = w.min_z; max_zj = w.max_z;
            } else {
                const float half_xj = pj.width * 0.5f;
                const float half_yj = pj.height * 0.5f;
                const float half_zj = pj.thickness * 0.5f;
                min_xj = pj.x - half_xj;
                max_xj = pj.x + half_xj;
                min_yj = pj.y - half_yj;
                max_yj = pj.y + half_yj;
                min_zj = z_predicted[j] - half_zj;
                max_zj = z_predicted[j] + half_zj;
            }

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
                // Same principle, transitively: two bodies of ONE bonded
                // structure (connected through dynamic bodies in the gluon
                // graph) do not contact each other. Their overlap is the
                // structure's internal geometry; the bonds own it.
                // (KINEMATIC bodies are never unioned, so their components
                // are singletons and this test cannot fire for them.)
                if (bond_root[i] == bond_root[j]) {
                    PHYS_TRACE_F(::logosphere::phystrace::Pair, "pair_rejected",
                                 (int)i, (int)j, "bonded_structure_internal");
                    continue;
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
                // A KINEMATIC body wakes a sleeper on APPROACH, not on raw
                // speed. The old 100 m/s raw-speed gate protected gliding
                // foot-plant pin anchors (fast but parallel to the floor,
                // zero closing speed) — but it also made every sleeper
                // immovable to a kinematic pusher: rung 3's 1 m/s finger
                // passed through a sleeping chain untouched, 32 contacts,
                // zero displacement. Closing speed keeps both cases honest:
                // anchors glide (closing ~0, silent), pushers close in.

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

                bool both_box = (pi.shape == ParticleShape::BOX) &&
                                (pj.shape == ParticleShape::BOX);
                // Rotation-aware pairs bypass the AABB SAT below: a rotated
                // box collided as its world-axis bounds produces axis-locked
                // normals ((0,-1,0)/(0,0,-1) only on the grass-walk canary)
                // and a tipped plate resting on the air where its unrotated
                // slab would be.
                const bool oriented_pair = both_box && (obb_i || obb_j);

                // Surface continuity: if j is static, merge its AABB with
                // adjacent coplanar static neighbors. Two adjacent floor tiles
                // aren't two surfaces - they're one surface represented as
                // two particles. Merging removes the data-structure artifact
                // that makes SAT pick the phantom boundary axis.
                // Axis-aligned seam fix; the oriented path doesn't read aabb_j.
                //
                // THE MERGE'S PREMISE, STATED RIGHT (night 2026-09-04, journal 6).
                // The widened box is the surface j's coplanar sleeping neighbours
                // form with it; it is the right box for a body ON that surface
                // and the wrong box for a body that is PART of it. Three tests:
                //  (a) the body's bottom is at j's top plane, within the contact
                //      margin - resting, landing, or embedded by error;
                //  (b) the body is not a coplanar sibling of j (its z-extent
                //      differs: case H's centre dirt tile beside its siblings);
                //  (c) the body's footprint overlaps the MERGED footprint by more
                //      than ADJ_EPS - it is somewhere on that surface. Footprint
                //      overlap with j ALONE (2026-09-02) lost the foot crossing a
                //      seam 4.6 mm before the next tile's edge (a 0.9 mm wall,
                //      test_tile_sticking f260) and G's diagonal supports.
                // The merged box is built first, then judged.
                // SEAM_MERGE_PRECONDITION=0 restores the unconditional merge for A/B.
                static const bool merge_precondition_off = [] {
                    const char* v = std::getenv("SEAM_MERGE_PRECONDITION");
                    return v && v[0] == '0';
                }();
                // THE SURFACE REACHES THE ORIENTED PATH (night 2026-09-04, journal
                // 8): a walking foot is a rotated box, so its pairs take the
                // oriented narrow phase, and until now the merge lived only in the
                // axis-aligned branch - a foot 0.9 mm into one tile met the next
                // tile's side as a 0.9 mm wall (test_body_coherence, 1.94 N s).
                // The tile under a foot is unrotated: its family's surface is
                // built and judged here for any i, and the oriented SAT below
                // receives it as an axis-aligned OBB in place of the tile's own.
                // THE STRIP REACHES THE ORIENTED PATH (night 2026-09-04, journals 9
                // and 12). A walking foot is a rotated box; its pairs take the
                // oriented SAT, which saw only the tile's own box and so met every
                // seam as a wall. Handed the family's strip as an axis-aligned OBB
                // it reads one surface. Refuted once (journal 9) when the family
                // excluded the awake tile under the foot and the strip ended at the
                // seam; with the family by coplanarity (journal 11) both walkers'
                // force lines read 0.00000 N s. RE-PARKED (journal 12c): a 16-cube
                // stack of 4096 tiles, slightly rotated by settling, never sleeps
                // when each tile over a sleeping tile is handed an 8 m strip
                // (test_physics_minimal_v2: at rest by frame 19 without it, never
                // with it). Default OFF until the stack under strips is understood;
                // SEAM_MERGE_ORIENTED=1 enables.
                static const bool merge_oriented = [] {
                    const char* v = std::getenv("SEAM_MERGE_ORIENTED");
                    return v && v[0] == '1';
                }();
                if (pj.is_at_rest && !obb_j) {
                    // A STRIP, NEVER AN L'S BOUNDING BOX (night 2026-09-04,
                    // journal 8): widened along one axis at a time, the merged
                    // box is a union of tiles; the bounding box of an L covers
                    // the corner cell where a lower tile may sit, and lifts
                    // whatever stands there (case H's centre tile, 5.17 mm rows
                    // from four diagonal tiles it is not over).
                    AABB6 strip_x = aabb_j, strip_y = aabb_j, merged = aabb_j;
                    for (int cand_k : candidates) {
                        size_t k = static_cast<size_t>(cand_k);
                        if (k == i || k == j) continue;
                        const Particle& pk = particles[k];
                        // PARKED (night 2026-09-04, journal 11, revised): including
                        // awake coplanar neighbours in the family removed the walker's
                        // seam wall - together with the parked oriented reach - and cost
                        // Eden 410 ms per steady frame (705 -> 1115) with a less quiet
                        // end state: the merge widens per PAIR, so a body over a family
                        // gets one full-face row per member, and awake members are the
                        // tiles under moving bodies. SEAM_FAMILY_AWAKE=1 includes them.
                        static const bool family_awake = [] {
                            const char* v = std::getenv("SEAM_FAMILY_AWAKE");
                            return v && v[0] == '1';
                        }();
                        if (!family_awake && !pk.is_at_rest) continue;
                        float half_xk = pk.width * 0.5f, half_yk = pk.height * 0.5f, half_zk = pk.thickness * 0.5f;
                        float kx_min = pk.x - half_xk, kx_max = pk.x + half_xk;
                        float ky_min = pk.y - half_yk, ky_max = pk.y + half_yk;
                        float kz_min = pk.z - half_zk, kz_max = pk.z + half_zk;
                        // Coplanar: same Z extent (same thin axis position)
                        if (std::abs(kz_min - min_zj) > COPLANAR_EPS ||
                            std::abs(kz_max - max_zj) > COPLANAR_EPS) continue;
                        // Adjacent: touching on X or Y (within epsilon)
                        bool y_touch = std::abs(ky_min - max_yj) < ADJ_EPS || std::abs(ky_max - min_yj) < ADJ_EPS;
                        bool x_touch = std::abs(kx_min - max_xj) < ADJ_EPS || std::abs(kx_max - min_xj) < ADJ_EPS;
                        // Same extent on the non-touching axis (forms a continuous surface)
                        bool x_aligned = std::abs(kx_min - min_xj) < ADJ_EPS && std::abs(kx_max - max_xj) < ADJ_EPS;
                        bool y_aligned = std::abs(ky_min - min_yj) < ADJ_EPS && std::abs(ky_max - max_yj) < ADJ_EPS;
                        if (x_touch && y_aligned) {
                            strip_x.min_x = std::min(strip_x.min_x, kx_min);
                            strip_x.max_x = std::max(strip_x.max_x, kx_max);
                        }
                        if (y_touch && x_aligned) {
                            strip_y.min_y = std::min(strip_y.min_y, ky_min);
                            strip_y.max_y = std::max(strip_y.max_y, ky_max);
                        }
                        if ((y_touch && x_aligned) || (x_touch && y_aligned)) {
                            merged.min_x = std::min(merged.min_x, kx_min);
                            merged.max_x = std::max(merged.max_x, kx_max);
                            merged.min_y = std::min(merged.min_y, ky_min);
                            merged.max_y = std::max(merged.max_y, ky_max);
                        }
                    }
                    const bool on_top   = std::fabs(min_zi - max_zj) <= CONTACT_MARGIN;
                    const bool sibling  = std::fabs(min_zi - min_zj) <= COPLANAR_EPS &&
                                          std::fabs(max_zi - max_zj) <= COPLANAR_EPS;
                    // On the x-strip: overlapping it along x and j's own y-range
                    // (every member shares that range), so a member is under i.
                    const float ox_j = std::fmin(max_xi, max_xj) - std::fmax(min_xi, min_xj);
                    const float oy_j = std::fmin(max_yi, max_yj) - std::fmax(min_yi, min_yj);
                    const float ox_s = std::fmin(max_xi, strip_x.max_x) - std::fmax(min_xi, strip_x.min_x);
                    const float oy_s = std::fmin(max_yi, strip_y.max_y) - std::fmax(min_yi, strip_y.min_y);
                    const bool on_strip_x = ox_s > ADJ_EPS && oy_j > ADJ_EPS;
                    const bool on_strip_y = oy_s > ADJ_EPS && ox_j > ADJ_EPS;
                    if (merge_precondition_off) {
                        aabb_j = merged;                       // the old union, for A/B
                    } else if (on_top && !sibling && (on_strip_x || on_strip_y)) {
                        // The strip the body overlaps more is the seam it straddles.
                        aabb_j = (on_strip_x && (!on_strip_y || ox_s >= oy_s)) ? strip_x : strip_y;
                    }
                }

                ContactManifold manifold;
                bool have_contact;
                if (oriented_pair) {
                    // Box-box with at least one rotated body: SAT over the
                    // oriented boxes. The unrotated side degenerates to
                    // identity axes, so mixed pairs are exact too.
                    have_contact = narrow_phase_obb(
                        obb_i ? obb_i_box : obb_of_box_particle(pi, z_predicted[i]),
                        obb_j ? obb_j_box
                              : (merge_oriented ? obb_of_aabb(aabb_j)     // the family's strip when merged
                                                : obb_of_box_particle(pj, z_predicted[j])),
                        i, j, CONTACT_MARGIN, manifold);
                } else if (both_box) {
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
                // THE ONE PREDICATE (INV-7). These two lines carried
                // their own opinion until 2026-08-14 — no KINEMATIC
                // check, and a sleep test that contradicted the door
                // once the resolver priced sleepers honestly (the
                // striker stopped dead against a crate the door was
                // simultaneously moving: priced in one model, spent in
                // another, which is INV-20 exactly).
                // Routing these through the door's predicate also fixes a
                // real pricing/spending mismatch: a KINEMATIC body that is
                // not at_rest was priced MOVABLE here while the apply site
                // refused to move it. Both corrections ride the resolver
                // lever together, because the KINEMATIC half shifts the
                // effective mass of every kinematic contact and the
                // ringing ladder + grass yield tests measure exactly that.
                // Default path stays bit-identical until the flip.
                float inv_ma, inv_mb;
                if (wake_resolver_on()) {
                    inv_ma = inv_mass_momentum(pi);
                    inv_mb = inv_mass_momentum(pj);
                } else {
                    inv_ma = pi.is_at_rest ? 0.0f : (1.0f / pi.GetMass());
                    inv_mb = pj.is_at_rest ? 0.0f : (1.0f / pj.GetMass());
                }
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
                    static const bool split_off = std::getenv("SPLIT_OFF") != nullptr;
                    c.eff_mass_share = split_off ? 1.0f : 1.0f / (float)manifold.num_points;
                    c.effective_mass = effective_mass * c.eff_mass_share;

                    // CONTACT TORQUE (G-39, lever CONTACT_TORQUE=1, default
                    // off). The manifold point was computed, carried here,
                    // and used only to fill a CollisionEvent; with the
                    // lever on it becomes the row's lever arm, so an
                    // off-centre contact can rotate a DYNAMIC body: the
                    // tilted cube rights itself instead of freezing on its
                    // edge (G-35). INV-20: the row's effective mass gains
                    // (r x J)^2/I for every body the torque may spin, or
                    // the impulse over-corrects. Gate is solver_mode ==
                    // DYNAMIC alone: KINEMATIC bones are excluded without
                    // reading owner (INV-15), and inv_m = 0 bodies cannot
                    // spin regardless.
                    static const bool contact_torque_on =
                        []{ const char* e = std::getenv("CONTACT_TORQUE"); return !(e && e[0] == '0' && e[1] == '\0'); }()  /* INV-32: torque is default physics; =0 is the kill switch */;
                    if (contact_torque_on) {
                        c.apply_anchor_torque = true;
                        c.anchor_rax = manifold.points[cp].px - pi.x;
                        c.anchor_ray = manifold.points[cp].py - pi.y;
                        c.anchor_raz = manifold.points[cp].pz - pi.z;
                        c.anchor_rbx = manifold.points[cp].px - pj.x;
                        c.anchor_rby = manifold.points[cp].py - pj.y;
                        c.anchor_rbz = manifold.points[cp].pz - pj.z;
                        float k = inv_mass_sum;
                        if (pi.solver_mode == ParticleSolverMode::DYNAMIC &&
                            inv_ma > 0.0f) {
                            const float I = pi.GetMomentOfInertia();
                            if (I > 0.0f) {
                                const float cx = c.anchor_ray*c.jz - c.anchor_raz*c.jy;
                                const float cy = c.anchor_raz*c.jx - c.anchor_rax*c.jz;
                                const float cz = c.anchor_rax*c.jy - c.anchor_ray*c.jx;
                                k += (cx*cx + cy*cy + cz*cz) / I;
                            }
                        }
                        if (pj.solver_mode == ParticleSolverMode::DYNAMIC &&
                            inv_mb > 0.0f) {
                            const float I = pj.GetMomentOfInertia();
                            if (I > 0.0f) {
                                const float cx = c.anchor_rby*c.jz - c.anchor_rbz*c.jy;
                                const float cy = c.anchor_rbz*c.jx - c.anchor_rbx*c.jz;
                                const float cz = c.anchor_rbx*c.jy - c.anchor_rby*c.jx;
                                k += (cx*cx + cy*cy + cz*cz) / I;
                            }
                        }
                        if (k > 0.0f)
                            c.effective_mass = (1.0f / k) * c.eff_mass_share;
                    }

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
                        c.build_approach = approach;
                        // G-63: no cap under the single law.
                        c.max_impulse = (single_law && single_law_uncapped)
                            ? std::numeric_limits<float>::max()
                            : c.effective_mass *
                              (approach + PhysicsV4::CONTACT_CAPTURE_CUSHION);
                    }
                    c.accumulated_impulse = 0.0f;
                    c.is_contact = true;
                    c.friction_impulse_t1 = 0.0f;
                    c.friction_impulse_t2 = 0.0f;

                    // G-55 (lever FRICTION_TWIST=1): a FACE manifold's
                    // rotational braking belongs to a twist row with the
                    // face-integral limit, not to corner tangents at
                    // 0.707*L. Flags set here; the friction block reads
                    // them. Corner/edge contacts keep corner physics.
                    static const bool friction_twist =
                        []{ const char* e = std::getenv("FRICTION_TWIST"); return !(e && e[0] == '0' && e[1] == '\0'); }();  // INV-36 default; =0 kills
                    // A face TREATMENT needs a face PATCH: >= 3 points.
                    // A sphere's contact flags is_face_contact with one
                    // point, and linear-only tangents there took away
                    // the r x t torque that rolls it (measured: the
                    // ramp sphere stopped turning).
                    if (contact_torque_on && friction_twist &&
                        manifold.is_face_contact &&
                        manifold.num_points >= 3) {
                        c.face_friction = true;
                        if (cp == 0) {
                            float r2_max = 0.0f;
                            for (int q = 0; q < manifold.num_points; q++) {
                                const float dx = manifold.points[q].px - pi.x;
                                const float dy = manifold.points[q].py - pi.y;
                                const float dz = manifold.points[q].pz - pi.z;
                                const float along = dx*c.jx + dy*c.jy
                                                  + dz*c.jz;
                                const float r2 = dx*dx + dy*dy + dz*dz
                                               - along*along;
                                r2_max = std::fmax(r2_max, r2);
                            }
                            // patch side ~ r_max*sqrt(2) (square patch;
                            // an over-estimate for partial overlap,
                            // INV-21-sanctioned)
                            c.twist_carrier = true;
                            c.twist_r = PhysicsV4::TWIST_RADIUS_FACTOR *
                                        std::sqrt(2.0f * r2_max);
                        }
                    }

                    // THE SLEEP LAW HEARS CONTACTS TOO (G-48): touching
                    // rows with real penetration beyond the tolerance mark
                    // both bodies dissatisfied; sleep cannot freeze a pair
                    // mid-repair (stacked boxes slept 1-2 cm interlocked
                    // because only gluon strain wrote the carrier).
                    // Speculative rows (bias < 0, gap open) do not mark.
                    if (c.bias >= 0.0f &&
                        c.penetration > SLEEP_PEN_TOLERANCE) {
                        if (c.body_a < constraint_dissatisfied_.size()) {
                            constraint_dissatisfied_[c.body_a] = 1;
                            dissat_note(c.body_a, __LINE__, c.penetration, (int)c.body_b);
                        }
                        if (c.body_b < constraint_dissatisfied_.size()) {
                            constraint_dissatisfied_[c.body_b] = 1;
                            dissat_note(c.body_b, __LINE__, c.penetration, (int)c.body_a);
                        }
                    }

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
                    float worst_pen = manifold.points[0].penetration;
                    for (int cp = 1; cp < manifold.num_points; cp++)
                        worst_pen = std::fmax(worst_pen, manifold.points[cp].penetration);
                    std::cout << "[CANARY F" << phys_frame << " CONTACT] P" << i << "<->P" << j
                              << " n=(" << manifold.normal_x << "," << manifold.normal_y << "," << manifold.normal_z << ")"
                              << " pen=" << (penetration*1000) << "mm"
                              << " worst_pen=" << (worst_pen*1000) << "mm"
                              << " points=" << manifold.num_points << " corner=" << is_corner_contact
                              << " zi=" << pi.z << " zj=" << pj.z
                              << (oriented_pair ? " OBB" : " AABB") << std::endl;
                    std::cout << "         Pi dims=(" << pi.width << "," << pi.height << "," << pi.thickness
                              << ") rot=(" << pi.rotation_x << "," << pi.rotation_y << "," << pi.rotation_z
                              << ") | Pj dims=(" << pj.width << "," << pj.height << "," << pj.thickness
                              << ") rot=(" << pj.rotation_x << "," << pj.rotation_y << "," << pj.rotation_z
                              << ")" << std::endl;
                    // The point SET is the G-48/G-50 question: does the
                    // manifold rebuilt from predicted poses keep its
                    // anchors between substeps, or flip them?
                    for (int cp = 0; cp < manifold.num_points; cp++)
                        std::cout << "         pt" << cp << "=("
                                  << manifold.points[cp].px << ","
                                  << manifold.points[cp].py << ","
                                  << manifold.points[cp].pz << ") pen="
                                  << (manifold.points[cp].penetration*1000)
                                  << "mm" << std::endl;
                }

                // Record collision event for game systems (combat, step climbing, etc.)
                // Every narrow-phase handler returns true only with
                // num_points >= 1 (their contract), so the old
                // invented-from-normal else-arm here was a dead fallback
                // (inventory B6). Read the manifold directly.
                float contact_x = manifold.points[0].px;
                float contact_y = manifold.points[0].py;
                float contact_z = manifold.points[0].pz;
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
                            float inv_ma_spec, inv_mb_spec;
                            if (wake_resolver_on()) {
                                inv_ma_spec = inv_mass_momentum(pi);
                                inv_mb_spec = inv_mass_momentum(pj);
                            } else {
                                inv_ma_spec = pi.is_at_rest ? 0.0f : (1.0f / pi.GetMass());
                                inv_mb_spec = pj.is_at_rest ? 0.0f : (1.0f / pj.GetMass());
                            }
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
                                c.build_approach = approach;
                                // G-63: no cap under the single law.
                                c.max_impulse = (single_law && single_law_uncapped)
                                    ? std::numeric_limits<float>::max()
                                    : c.effective_mass *
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

    // PHYSICS TELEMETRY moved after the solve (night 2026-09-04): it records
    // the solved rows, not the penetration events. See the block after the
    // solve loop.

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

    // The dissatisfaction carrier is cleared at SUBSTEP START (before
    // contact detection), not here: this assign used to run after the
    // contact build and wiped the contact marks (G-48) before the sleep
    // gate ever read them. Gluon strain marks land after contacts; both
    // survive to update_rest_state.
    if (constraint_dissatisfied_.size() != particles.size())
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

        // Effective mass (reduced mass) - same for all 3 constraints.
        // Through the one predicate: this build used KINEMATIC-only while the
        // apply used at_rest||KINEMATIC, so rows sized impulses for moving a
        // sleeping body the apply then refused to move.
        float inv_ma = inv_mass_momentum(pa);
        float inv_mb = inv_mass_momentum(pb);
        float inv_mass_sum = inv_ma + inv_mb;
        // No movable endpoint -> effective mass ZERO, not an arbitrary 1.0f.
        // The contact build learned this in S22 (see the phantom-impulse
        // comment there): a fake eff mass makes a row compute the same
        // nonzero impulse forever, applied times zero, pinning the global
        // convergence max. With sleep now immovable at this build too, a
        // strained sleeping bond would otherwise poison warm start and
        // impulse memory with impulses sized for a 1 kg system on
        // 0.0002 kg blades, slamming the body the moment it wakes.
        float eff_mass = (inv_mass_sum > 0.0f) ? (1.0f / inv_mass_sum) : 0.0f;

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
                        { constraint_dissatisfied_[body_a] = 1; dissat_note(body_a, __LINE__, 0.0f, -1); }
                    if (body_b < constraint_dissatisfied_.size())
                        { constraint_dissatisfied_[body_b] = 1; dissat_note(body_b, __LINE__, 0.0f, -1); }
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

        // A bond between two immovable bodies builds no rows. The wake-on-
        // strain check above already ran, so a strained sleeping bond woke
        // its endpoints this substep and does not take this exit. What
        // remains is the sleeping, satisfied majority of the world: their
        // rows are guaranteed zeros (eff mass 0 on every axis), and Eden
        // was visiting 58k of them 15+ iterations x 4 substeps a frame for
        // nothing. Identity-safe: consumers resolve via gluon_slot, same as
        // the both-KINEMATIC skip above.
        if (inv_mass_momentum(pa) == 0.0f && inv_mass_momentum(pb) == 0.0f) {
            continue;
        }

        // V4.9 material damping, re-expressed as the reduced-mass impulse
        // it always was: J = mu * d * (v_b - v_a), mu = 1/(inv_a + inv_b).
        // For two dynamic bodies this IS the old COM blend, algebraically
        // (dv_a = (m_b/M) * d * (v_b - v_a), momentum conserved exactly).
        // As one side becomes immovable, mu -> m_dyn and the formula
        // reproduces the old one-kinematic branch with the immovable side
        // untouched (dv = J * 0). The old code hand-expanded those limits
        // into three branches, and the case it never wrote, at_rest, moved
        // sleeping bodies: a 111.661 kg branch asleep under ten 0.107 kg
        // leaves gained 1.02316e-3 m/s per frame (0.6% of g, under the
        // 0.2 m/s wake threshold) and sank 6.74 m in 900 frames, at_rest=1
        // throughout. One formula through the one door; no branch to forget.
        if (damping_factor > 0.0f) {
            const float inv_da = inv_mass_momentum(pa);
            const float inv_db = inv_mass_momentum(pb);
            const float inv_sum_d = inv_da + inv_db;
            if (inv_sum_d > 0.0f) {
                const float sd = damping_factor / inv_sum_d;   // mu * d
                apply_pair_impulse(particles[body_a], particles[body_b],
                    sd * (particles[body_b].vx - particles[body_a].vx),
                    sd * (particles[body_b].vy - particles[body_a].vy),
                    sd * (particles[body_b].vz - particles[body_a].vz));
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
            if (canary_active && ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                std::cout << "[CANARY ROWBUILD] P" << c.body_a << "<->P" << c.body_b
                          << " j=(" << c.jx << "," << c.jy << "," << c.jz << ")"
                          << " eff=" << c.effective_mass << " bias=" << c.bias
                          << " budget=[" << c.min_impulse << "," << c.max_impulse << "]"
                          << " breaking_per_axis=" << breaking_per_axis
                          << " err=" << bond_err_mag
                          << " str_a=" << pa.material_strength
                          << " str_b=" << pb.material_strength
                          << std::endl;
            }
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
            apply_pair_impulse(wa, wb,
                               gluon->warm_ix, gluon->warm_iy, gluon->warm_iz);
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
                // Was a silent `continue // shouldn't happen` (inventory B2):
                // catch-and-continue on a state this code called impossible.
                // Both-KINEMATIC pairs exited above, so reaching here means a
                // dynamic endpoint with I <= 0 — broken particle data whose
                // joint would silently never couple. Refuse loud.
                std::fprintf(stderr,
                    "[PHYSICS REFUSED] angular row P%zu<->P%zu with no "
                    "positive inertia (I_a=%g mode_a=%d, I_b=%g mode_b=%d). "
                    "A dynamic endpoint with I<=0 is corrupted particle data "
                    "(mass or dimensions).\n",
                    body_a, body_b, I_a, (int)pa.solver_mode,
                    I_b, (int)pb.solver_mode);
                if (!std::getenv("PHYSICS_LENIENT")) std::abort();
                continue;   // lenient: drop the row, never invent an inertia
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

            // ANGULAR_BETA: INV-29 registry (PositionCorrection group).

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
                // ============================================================
                // GRAVITY FEED-FORWARD: the holding torque is DERIVED
                // ============================================================
                // A drive row is a proportional controller, so under a
                // sustained load it holds a standing error proportional to
                // that load (both drive tests: distal joints green, the
                // loaded shoulder red at 0.1368 rad against 0.0627). Two
                // integral-term attempts (linear, then angular) both pumped
                // the joint's own oscillation and were retired; see the
                // FAILED experiment commit. The lawful cure is statics, not
                // integration: the torque a joint must hold is the moment of
                // its child subtree's weight about the joint anchor,
                //   tau = -(COM_sub - anchor) x (M_sub * g),
                // every term already in the data. Robotics calls this the
                // G(q) compensation term. Applied as +tau*dt to the child
                // and the reaction to the parent, each side through the
                // sleep/KINEMATIC guards. FEEDFORWARD_OFF=1 for A/B.
                // DEFAULT OFF: two attempts measured worse than no
                // compensation at all (integral: shoulder 0.1368 -> 0.3394;
                // this feed-forward: -> 1.93, because the drive-gluon a/b
                // direction is not a reliable parent->child convention and
                // the subtree sweep climbs into the torso: P370->P348 read
                // 29.186 kg while its sibling chain P370->P349 read 0.213).
                // Doing this right needs grounded-side detection (cut the
                // joint, take the component without the kinematic root) and
                // a sign proof against the row's q_err convention.
                // FEEDFORWARD_ON=1 to experiment.
                static const bool ff_on = std::getenv("FEEDFORWARD_ON") != nullptr;
                if (ff_on) {
                    float m_sub = 0.0f, cx = 0.0f, cy = 0.0f, cz = 0.0f;
                    {
                        // Subtree = the child and everything hanging off it
                        // distally (each joint compensates its OWN load, so
                        // nesting does not double-count).
                        std::vector<size_t> stack{body_b};
                        std::unordered_set<size_t> seen{body_a, body_b};
                        while (!stack.empty()) {
                            const size_t pid = stack.back(); stack.pop_back();
                            const Particle& sp = particles[pid];
                            const float m = sp.GetMass();
                            m_sub += m;
                            cx += m * sp.x; cy += m * sp.y; cz += m * sp.z;
                            for (const GluonConstraintBase* cg :
                                 get_gluons_for_particle(pid)) {
                                if (!cg) continue;
                                const size_t other =
                                    (cg->particle_a == pid) ? cg->particle_b
                                                            : cg->particle_a;
                                // Descend the JOINT tree only: drive gluons,
                                // parent->child. Descending every bond type
                                // swept structural bonds and pins across the
                                // whole body graph (measured: a "subtree" of
                                // 32.275 kg on a rig whose arm weighs a few
                                // kilos, and the near-massless neck holding
                                // 30 kg of phantom load, 7x worse than no
                                // feed-forward at all).
                                if (other < particles.size() &&
                                    cg->angular_drive_enabled &&
                                    cg->use_quat_target &&
                                    cg->particle_a == pid &&
                                    seen.insert(other).second) {
                                    stack.push_back(other);
                                }
                            }
                        }
                    }
                    if (std::getenv("FF_DEBUG")) {
                        static int ffn = 0;
                        if ((ffn++ % 240) == 0)
                            printf("[FF] joint P%zu->P%zu m_sub=%.3f kg\n",
                                   body_a, body_b, m_sub);
                    }
                    if (m_sub > 0.0f) {
                        cx /= m_sub; cy /= m_sub; cz /= m_sub;
                        // Anchor = the joint pivot: body a's attachment
                        // point, its local offset rotated by a's orientation
                        // (same Euler composition as the linear build).
                        float anchor_x, anchor_y;
                        {
                            const float ox = gluon->offset_a.x;
                            const float oy = gluon->offset_a.y;
                            const float oz = gluon->offset_a.z;
                            const float cxr = cosf(pa.rotation_x), sxr = sinf(pa.rotation_x);
                            const float cyr = cosf(pa.rotation_y), syr = sinf(pa.rotation_y);
                            const float czr = cosf(pa.rotation_z), szr = sinf(pa.rotation_z);
                            const float y1 = oy * cxr - oz * sxr;
                            const float z1 = oy * sxr + oz * cxr;
                            const float x2 = ox * cyr + z1 * syr;
                            anchor_x = pa.x + x2 * czr - y1 * szr;
                            anchor_y = pa.y + x2 * szr + y1 * czr;
                        }
                        // Only the horizontal lever matters against vertical
                        // gravity: tau = r x (0,0,-Mg) has no z component.
                        const float rx = cx - anchor_x;
                        const float ry = cy - anchor_y;
                        // tau = -r x (M*g), g = (0,0,-GRAVITY):
                        // r x (0,0,-Mg) = (-ry*Mg, rx*Mg, 0); negated below.
                        const float Mg = m_sub * GRAVITY;
                        const float tx = -ry * Mg;
                        const float ty =  rx * Mg;
                        // tau_motor = -(r x Mg vec):
                        const float ffx = -tx, ffy = -ty;
                        const float fmag = std::sqrt(ffx * ffx + ffy * ffy);
                        if (fmag > 1e-9f) {
                            const float axf = ffx / fmag, ayf = ffy / fmag;
                            const float J = fmag * dt;   // torque -> angular impulse
                            Particle& fb = particles[body_b];
                            const float Ifb = fb.GetInertiaAboutAxis(axf, ayf, 0.0f);
                            if (Ifb > 0.0f && !fb.is_at_rest &&
                                fb.solver_mode != ParticleSolverMode::KINEMATIC) {
                                const float inv = J / Ifb;
                                fb.omega_x += axf * inv;
                                fb.omega_y += ayf * inv;
                            }
                            Particle& fa = particles[body_a];
                            const float Ifa = fa.GetInertiaAboutAxis(axf, ayf, 0.0f);
                            if (Ifa > 0.0f && !fa.is_at_rest &&
                                fa.solver_mode != ParticleSolverMode::KINEMATIC) {
                                const float inv = J / Ifa;
                                fa.omega_x -= axf * inv;
                                fa.omega_y -= ayf * inv;
                            }
                        }
                    }
                }

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
                        { constraint_dissatisfied_[body_a] = 1; dissat_note(body_a, __LINE__, 0.0f, -1); }
                    if (body_b < constraint_dissatisfied_.size())
                        { constraint_dissatisfied_[body_b] = 1; dissat_note(body_b, __LINE__, 0.0f, -1); }
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
                // DRIVE_ROWS=3 (night 2026-09-04, journal 16; default 1 keeps
                // the one row along e, byte-identical): the drive damps and
                // corrects the WHOLE relative angular velocity - three
                // world-axis rows, each carrying its component of the
                // Rodrigues error as bias and sized by the inertia about its
                // own axis - which is what the comment above this branch
                // describes. With one row along e, any relative spin
                // perpendicular to e is touched by nothing: measured on the
                // 3-axis drive's post-impact cantilever, B spinning at 1 rad/s
                // for 300 frames while the angle was held by position repair.
                static const int drive_rows = [] {
                    const char* e = std::getenv("DRIVE_ROWS");
                    return (e && e[0] == '3' && e[1] == '\0') ? 3 : 1;
                }();
                // DRIVE_ROWS_DIAG_SKIP_KIN_B=1: diagnostic only (journal 16e).
                // A joint whose body_b is KINEMATIC falls back to the one row.
                // a/b is no parent-child convention; this is a scalpel for one
                // measurement: is the elbow's three-row weld to the FK forearm
                // what locks the physics-driven shoulder?
                static const bool diag_skip_kin_b =
                    std::getenv("DRIVE_ROWS_DIAG_SKIP_KIN_B") != nullptr;
                // THE SCOPE (journal 16f): a joint row damps only what the
                // solver can see. A KINEMATIC endpoint is FK-owned; its spin
                // is not in the ledger (G-38), so a complete three-row drive
                // against it welds the DYNAMIC side to a ghost at rest - the
                // elbow's rest-pose drive undid the shoulder's impulses to the
                // last digit. Three rows only when both endpoints are DYNAMIC;
                // a joint with an FK side keeps the one row it always had.
                const bool both_dynamic =
                    pa.solver_mode == ParticleSolverMode::DYNAMIC &&
                    pb.solver_mode == ParticleSolverMode::DYNAMIC;
                if (drive_rows == 3 && both_dynamic &&
                    !(diag_skip_kin_b && pb.solver_mode == ParticleSolverMode::KINEMATIC)) {
                    const float ecomp[3] = {ex, ey, ez};
                    const float bias_mag_raw = ANGULAR_BETA * e_mag / dt;
                    const bool  bias_capped  = bias_mag_raw > PhysicsV4::MAX_ANGULAR_BIAS_VELOCITY;
                    const float bias_scale   = bias_capped
                        ? PhysicsV4::MAX_ANGULAR_BIAS_VELOCITY / bias_mag_raw : 1.0f;
                    const float wcomp[3] = {pa.omega_x - pb.omega_x,
                                            pa.omega_y - pb.omega_y,
                                            pa.omega_z - pb.omega_z};
                    for (int k = 0; k < 3; ++k) {
                        Constraint c_k = c_ang;
                        c_k.angular_axis_x = (k == 0) ? 1.0f : 0.0f;
                        c_k.angular_axis_y = (k == 1) ? 1.0f : 0.0f;
                        c_k.angular_axis_z = (k == 2) ? 1.0f : 0.0f;
                        c_k.angular_axis_vec_len = 1.0f;
                        const float Ia3 = (pa.solver_mode == ParticleSolverMode::KINEMATIC)
                            ? 0.0f : pa.GetInertiaAboutAxis(c_k.angular_axis_x, c_k.angular_axis_y, c_k.angular_axis_z);
                        const float Ib3 = (pb.solver_mode == ParticleSolverMode::KINEMATIC)
                            ? 0.0f : pb.GetInertiaAboutAxis(c_k.angular_axis_x, c_k.angular_axis_y, c_k.angular_axis_z);
                        float inv3 = 0.0f;
                        if (Ia3 > 0.0f) inv3 += 1.0f / Ia3;
                        if (Ib3 > 0.0f) inv3 += 1.0f / Ib3;
                        if (inv3 > 0.0f) c_k.effective_inertia = 1.0f / inv3;
                        // Cap the bias VECTOR's magnitude, never its components:
                        // a per-axis cap gave the smallest error component the
                        // same 4 rad/s as the largest and turned a 0.05 rad twist
                        // into a third of the commanded motion, into the torso's
                        // contacts, which refused all of it (journal 16d).
                        c_k.angular_bias = ANGULAR_BETA * ecomp[k] / dt * bias_scale;
                        c_k.angular_bias_saturated = bias_capped;
                        if (gluon->force_bounded()) {
                            const float tb = (gluon->angular_stiffness * std::fabs(ecomp[k]) +
                                              gluon->angular_damping * std::fabs(wcomp[k])) * dt;
                            c_k.min_angular_impulse = -tb;
                            c_k.max_angular_impulse = tb;
                        } else {
                            c_k.min_angular_impulse = -std::numeric_limits<float>::infinity();
                            c_k.max_angular_impulse = std::numeric_limits<float>::infinity();
                        }
                        c_k.accumulated_angular_impulse = 0.0f;
                        constraints.push_back(c_k);
                    }
                    continue;  // quaternion path handled, three rows
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
                    // CANARY_DEBUG: quaternion angular row build — axis,
                    // error, bias and budget, mirroring [CANARY ROWBUILD].
                    if (canary_active && ((int)c_axis.body_a == canary_pid ||
                                          (int)c_axis.body_b == canary_pid)) {
                        std::cout << "[CANARY F" << phys_frame << " ANGROW-Q] P"
                                  << c_axis.body_a << "<->P" << c_axis.body_b
                                  << " axis=(" << c_axis.angular_axis_x << ","
                                  << c_axis.angular_axis_y << ","
                                  << c_axis.angular_axis_z << ")"
                                  << " e_mag=" << e_mag
                                  << " bias=" << c_axis.angular_bias
                                  << " effI=" << c_axis.effective_inertia
                                  << " budget=[" << c_axis.min_angular_impulse
                                  << "," << c_axis.max_angular_impulse << "]"
                                  << " drive=" << (int)gluon->angular_drive_enabled
                                  << " qa=(" << q_a.w << "," << q_a.x << ","
                                  << q_a.y << "," << q_a.z << ")"
                                  << " eul_a=(" << pa.rotation_x << ","
                                  << pa.rotation_y << "," << pa.rotation_z << ")"
                                  << std::endl;
                    }
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
            // CANARY_DEBUG: scalar-Z angular row build.
            if (canary_active && ((int)c_ang.body_a == canary_pid ||
                                  (int)c_ang.body_b == canary_pid)) {
                std::cout << "[CANARY F" << phys_frame << " ANGROW-Z] P"
                          << c_ang.body_a << "<->P" << c_ang.body_b
                          << " angle_diff=" << angle_diff
                          << " bias=" << c_ang.angular_bias
                          << " effI=" << c_ang.effective_inertia
                          << " budget=[" << c_ang.min_angular_impulse
                          << "," << c_ang.max_angular_impulse << "]"
                          << " drive=" << (int)gluon->angular_drive_enabled
                          << " limit=" << limit
                          << std::endl;
            }
            constraints.push_back(c_ang);
        }
    }

    auto t_after_gluons = std::chrono::high_resolution_clock::now();
    // (authority report emitted after the solve, below)

    if (constraints.empty()) {
        // A frame with no rows is an observation: the tracked bodies are in
        // contact with nothing. Record it (night 2026-09-04).
        record_row_telemetry(particles, constraints, phys_frame);
        return;
    }

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
    static int g_maxdv_body_a=-1, g_maxdv_body_b=-1, g_maxdv_type=-1;
    float prev_max_impulse = 1e10f;
    int low_improvement_count = 0;
    // MIN_IMPROVEMENT_RATE, MIN_ITERATIONS, PLATEAU_CONFIRM,
    // VELOCITY_PLATEAU_FLOOR: INV-29 registry (ConvergenceExits group).

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

    // ========================================================================
    // ROW MASS REFRESH: never larger than the live predicate allows
    // ========================================================================
    // Contact rows are built BEFORE the gluon pass, and the gluon pass wakes
    // bodies (wake-on-strain). A row built against a sleeping body froze
    // eff = m_other (inv 0); the impulses then land on the woken body through
    // the live predicate. Measured on the walk gate, detector frame 270: a
    // 0.03 kg blade behind a row sized for a 1.77 kg part of the walker took
    // 59x its intended dv per correction, the four-point manifold pumped the
    // error geometrically (impulses 0.21 -> 1.30 -> 2.32 -> 4.00 across
    // iterations), and the blade left at 245 m/s in one substep with the
    // capture cap 59x too generous to stop it.
    //
    // The refresh SHRINKS oversized rows to the live value and does nothing
    // else. The first version also zeroed the rows of still-sleeping bodies;
    // that killed their warm-start continuity across wake and the 500x/1044x
    // ringing ladder tore where it used to ring down (A/B: refresh-off green,
    // split-off still red). A sleeping body's row already applies impulses
    // times zero; it needs no help, and its cache is the support that must
    // be warm the frame it wakes.
    for (Constraint& c : constraints) {
        if (c.is_angular) continue;   // sized by inertia through KINEMATIC-only doors
        if (!c.is_contact && !c.is_turtle_contact) {
            // GLUON rows get the same live refresh, for the same disease one
            // phase later: wake-on-strain fires DURING the gluon build, so a
            // gluon built early in the loop is sized with an endpoint the
            // strain of a LATER gluon then wakes. At heel strike the replant
            // strains the stance pin, the wake sweeps the leg chain, and the
            // early-built rows (eff = m for one-movable, true response 2x
            // that) overcorrect x1.96 per sweep — a geometric divergence
            // that exhausts all 32 iterations and lands +-100 m/s on the one
            // drive child in the chain for exactly one substep per heel
            // strike (walk gate: detector events 2, worst 92.3 m/s, period
            // ~130 substeps = the gait half-cycle; CANARY P718 F376: v_rel
            // flipping sign at equal magnitude every sweep, impulses +-937
            // clamped by the +-833 breaking budget). Shrink to the live
            // value, never grow; budgets stay — a gluon's cap is breaking
            // FORCE, not a mass-derived cushion.
            const float ginv_a = inv_mass_momentum(particles[c.body_a]);
            const float ginv_b = inv_mass_momentum(particles[c.body_b]);
            const float ginv_sum = ginv_a + ginv_b;
            if (ginv_sum <= 0.0f) continue;            // sleeping row: leave it
            const float geff_live = 1.0f / ginv_sum;
            if (geff_live < c.effective_mass) c.effective_mass = geff_live;
            continue;
        }
        const float inv_a = inv_mass_momentum(particles[c.body_a]);
        const float inv_b = c.is_turtle_contact
                            ? 0.0f : inv_mass_momentum(particles[c.body_b]);
        const float inv_sum = inv_a + inv_b;
        if (inv_sum <= 0.0f) continue;                 // sleeping row: leave it
        const float eff_live = c.eff_mass_share / inv_sum;
        if (eff_live < c.effective_mass) {
            // The cap is eff * (approach + cushion); keep that meaning.
            c.max_impulse *= eff_live / c.effective_mass;
            c.effective_mass = eff_live;
        }
    }

    if (ENABLE_WARM_STARTING) {
        // WARM START = ITERATION ZERO, THROUGH THE FULL JACOBIAN (G-43).
        //
        // The old apply pushed the cached support through the LINEAR
        // channel only, on the first row of each key (a hash dedup
        // skipped the rest). Support without its lever arms is support
        // without torque: the turtle patch row never saw an approach
        // again, never fired, and a cube stood on its corner forever
        // (R8) while the identical pose fell through box-box rows whose
        // cache missed (R7). A warm start with different physics than
        // the iterations is not a warm start, it is a second solver.
        //
        // Now: contact rows GROUP by their full ContactKey (comparing
        // full keys also retires the documented hash-collision defect
        // that silently dropped one pair's warm start), the cached
        // impulse distributes across the group by eff_mass_share, and
        // each row applies its share through linear AND angular halves
        // with the same gates the solve uses. accumulated_impulse
        // starts at the share, so the iterations correct residuals
        // instead of rediscovering equilibrium.
        std::map<std::tuple<size_t, size_t, int>, std::vector<size_t>> key_groups;
        for (size_t ci = 0; ci < constraints.size(); ++ci) {
            const Constraint& c = constraints[ci];
            if (c.is_angular) continue;
            if (!c.is_contact && !c.is_turtle_contact) continue;
            if (c.is_turtle_contact) {
                key_groups[{c.body_a, c.body_a, 0}].push_back(ci);
            } else {
                const bool swapped = c.body_a > c.body_b;
                const float jx_can = swapped ? -c.jx : c.jx;
                const float jy_can = swapped ? -c.jy : c.jy;
                const float jz_can = swapped ? -c.jz : c.jz;
                int type;
                if (std::abs(jz_can) > 0.5f)      type = (jz_can > 0) ? 5 : 6;
                else if (std::abs(jx_can) > 0.5f) type = (jx_can > 0) ? 1 : 2;
                else                              type = (jy_can > 0) ? 3 : 4;
                key_groups[{std::min(c.body_a, c.body_b),
                            std::max(c.body_a, c.body_b), type}].push_back(ci);
            }
        }
        for (const auto& kv : key_groups) {
            ContactKey key;
            key.particle_a = std::get<0>(kv.first);
            key.particle_b = std::get<1>(kv.first);
            key.contact_type = std::get<2>(kv.first);
            const bool turtle_group = (key.contact_type == 0 &&
                                       key.particle_a == key.particle_b);
            auto it = cached_impulses_.find(key);
            if (it == cached_impulses_.end()) {
                warm_misses++;
                if (turtle_group) turtle_misses++;
                continue;
            }
            warm_hits++;
            if (turtle_group) turtle_hits++;
            const float cached = it->second.first;
            total_cached_applied += std::abs(cached);
            float share_sum = 0.0f;
            for (size_t ci : kv.second) share_sum += constraints[ci].eff_mass_share;
            if (!(share_sum > 0.0f)) share_sum = (float)kv.second.size();
            for (size_t ci : kv.second) {
                Constraint& c = constraints[ci];
                // A SPECULATIVE row (gap still open, bias < 0 — the
                // row's own classification; predicted-pose penetration
                // is positive even with the gap open, so it cannot
                // discriminate) is an approach limit, not a support: it
                // has no equilibrium to restore and receives no warm
                // impulse.
                if (c.is_contact && c.bias < 0.0f) continue;
                if (c.is_turtle_contact && !(c.penetration > 0.0f)) continue;
                const float w = cached * (c.eff_mass_share / share_sum);
                Particle& pa = particles[c.body_a];
                const float inv_ma = inv_mass_momentum(pa);
                pa.vx += c.jx * w * inv_ma;
                pa.vy += c.jy * w * inv_ma;
                pa.vz += c.jz * w * inv_ma;
                if (c.apply_anchor_torque &&
                    pa.solver_mode == ParticleSolverMode::DYNAMIC &&
                    inv_ma > 0.0f) {
                    const float I = pa.GetMomentOfInertia();
                    if (I > 0.0f) {
                        const float fx = c.jx * w, fy = c.jy * w, fz = c.jz * w;
                        const float inv = 1.0f / I;
                        pa.omega_x += (c.anchor_ray * fz - c.anchor_raz * fy) * inv;
                        pa.omega_y += (c.anchor_raz * fx - c.anchor_rax * fz) * inv;
                        pa.omega_z += (c.anchor_rax * fy - c.anchor_ray * fx) * inv;
                    }
                }
                if (!c.is_turtle_contact) {
                    Particle& pb = particles[c.body_b];
                    const float inv_mb = inv_mass_momentum(pb);
                    pb.vx -= c.jx * w * inv_mb;
                    pb.vy -= c.jy * w * inv_mb;
                    pb.vz -= c.jz * w * inv_mb;
                    if (c.apply_anchor_torque &&
                        pb.solver_mode == ParticleSolverMode::DYNAMIC &&
                        inv_mb > 0.0f) {
                        const float I = pb.GetMomentOfInertia();
                        if (I > 0.0f) {
                            const float fx = c.jx * w, fy = c.jy * w, fz = c.jz * w;
                            const float inv = 1.0f / I;
                            pb.omega_x -= (c.anchor_rby * fz - c.anchor_rbz * fy) * inv;
                            pb.omega_y -= (c.anchor_rbz * fx - c.anchor_rbx * fz) * inv;
                            pb.omega_z -= (c.anchor_rbx * fy - c.anchor_rby * fx) * inv;
                        }
                    }
                    // BOOK WHAT THIS SPENDS (unchanged law): the warm
                    // start is momentum like any other.
                    if (inv_mb == 0.0f && w != 0.0f) {
                        record_refused_impulse(c.body_b,
                                               -c.jx * w, -c.jy * w, -c.jz * w);
                    }
                }
                if (!c.is_turtle_contact && inv_ma == 0.0f && w != 0.0f) {
                    record_refused_impulse(c.body_a,
                                           c.jx * w, c.jy * w, c.jz * w);
                }
                if (canary_active && ((int)c.body_a == canary_pid ||
                                      (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " WARM] P"
                              << c.body_a << "<->P" << c.body_b
                              << " share_impulse=" << w
                              << " of=" << cached
                              << " ra=(" << c.anchor_rax << ","
                              << c.anchor_ray << "," << c.anchor_raz << ")"
                              << " vz_after=" << particles[canary_pid].vz
                              << (c.is_turtle_contact ? " TURTLE" : " BOX")
                              << std::endl;
                }
                PHYS_TRACE_F(::logosphere::phystrace::Constraint,
                             "warm_apply", (int)c.body_a, (int)c.body_b,
                             c.is_turtle_contact ? "turtle" : "contact",
                             w, cached, c.penetration);
                warm_started_impulses[ci] = w;
                c.accumulated_impulse = w;
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

    // CONTACT-COUPLED BODIES resolve orientation error in the VELOCITY
    // phase. The angular split's position repair rotates a body with no
    // momentum cost — and no negotiation: contacts carry no angular
    // Jacobian, so a repair that rotates a trampled grass blade back
    // upright THROUGH the foot standing on it manufactures fresh
    // penetration every substep, which the contact rows then eject at
    // full budget (measured on the walk-through-grass gate: worst blade
    // 27.80 m from home at 19.2 m/s; with the repair kept in momentum,
    // 2.58 m at 2.7 m/s). The linear position pass never had this hole:
    // its contact rows sit in the same Gauss-Seidel pass and answer
    // back. Until angular repairs can be negotiated with contacts, a
    // body touching anything keeps its bias in the velocity solve.
    std::vector<uint8_t> body_in_contact(count, 0);
    for (const Constraint& cc : constraints) {
        if (!cc.is_contact && !cc.is_turtle_contact) continue;
        if (cc.body_a < count) body_in_contact[cc.body_a] = 1;
        if (!cc.is_turtle_contact && cc.body_b < count) body_in_contact[cc.body_b] = 1;
    }

    // Rows entering the solve, counted ONCE. iterations x rows is the true
    // solver work unit; counting rows per iteration would conflate the two.
    LOGO_COUNT_N(::logosphere::telemetry::Counter::PhysSolverRows, constraints.size());

    // ========================================================================
    // THE CONTACT MANIFOLD BLOCK (G-63, single-law lever)
    // ========================================================================
    // A manifold's normal rows are not independent facts: pushing one
    // corner tilts the body, which changes every other corner's
    // measured velocity - the off-diagonal of A[i][j] below IS that
    // spillover, and solved one row at a time it is the rocking/
    // starvation the campaign measured. Pattern ported from the joint
    // block solver (feat/joint-block-solver): build
    // A[i][j] = J_i M^-1 J_j^T for the group's <= 4 rows, solve by
    // Gaussian elimination, clamp each row's ACCUMULATED impulse to
    // its own limits, apply the deltas. Complementarity is enforced
    // by iterate-and-clamp exactly as the sequential rows do it; the
    // sequential pass skips these rows' normal solve (friction still
    // runs there, on the block-updated accumulated impulse).
    struct ContactGroup { size_t first; int n; };
    std::vector<ContactGroup> contact_groups;
    std::vector<uint8_t> in_contact_block;
    if (single_law && single_law_blocks) {
        in_contact_block.assign(constraints.size(), 0);
        size_t i = 0;
        while (i < constraints.size()) {
            const Constraint& c = constraints[i];
            if ((c.is_contact || c.is_turtle_contact) && !c.is_angular) {
                size_t j = i + 1;
                while (j < constraints.size()) {
                    const Constraint& d = constraints[j];
                    if (d.is_angular) break;
                    if (!(d.is_contact || d.is_turtle_contact)) break;
                    if (d.body_a != c.body_a || d.body_b != c.body_b) break;
                    if (d.is_turtle_contact != c.is_turtle_contact) break;
                    ++j;
                }
                const int n = (int)(j - i);
                if (n >= 2 && n <= 4) {
                    contact_groups.push_back({i, n});
                    for (size_t k = i; k < j; ++k) in_contact_block[k] = 1;
                }
                i = j;
            } else {
                ++i;
            }
        }
    }

    // SHOCK PROPAGATION (G-64, owner ruling 2026-09-01: "shock now").
    // A heavy-through-light chain converges at ~m_light/m_heavy per
    // sequential sweep (measured: 32/32 iterations, the leaves still at
    // -0.106 m/s under the gold). Two moves, both inside the single law
    // (ordering and mass model, never an impulse bound):
    //   1. ORDER the blocks by contact-graph distance from the immovable
    //      roots - INV-7's one predicate (bodies that cannot take
    //      momentum) at depth 0, turtle-touching bodies at depth 1 - and
    //      solve bottom-up every sweep. Never world-up (INV-6).
    //   2. After the loop, ONE SHOCK SWEEP re-solves each block with its
    //      lower body held immovable, so support propagates up a stack
    //      in one pass. This is the known, bounded non-physicality of
    //      shock propagation: momentum exchange in that sweep favours
    //      stability. The refused-momentum and energy ledgers are the
    //      jury; no refusal is booked for a body held only for the sweep.
    std::vector<int> contact_depth;
    if (single_law && !contact_groups.empty()) {
        const int UNREACHED = std::numeric_limits<int>::max();
        contact_depth.assign(count, UNREACHED);
        std::vector<size_t> frontier;
        for (size_t b = 0; b < count; ++b)
            if (inv_mass_momentum(particles[b]) == 0.0f) {
                contact_depth[b] = 0; frontier.push_back(b);
            }
        for (const Constraint& c : constraints)
            if (c.is_turtle_contact && c.body_a < count &&
                contact_depth[c.body_a] == UNREACHED) {
                contact_depth[c.body_a] = 1; frontier.push_back(c.body_a);
            }
        // BFS over box-box contact rows.
        while (!frontier.empty()) {
            std::vector<size_t> next;
            for (const Constraint& c : constraints) {
                if (!c.is_contact || c.is_turtle_contact || c.is_angular) continue;
                if (c.body_a >= count || c.body_b >= count) continue;
                for (int side = 0; side < 2; ++side) {
                    const size_t from = side ? c.body_b : c.body_a;
                    const size_t to   = side ? c.body_a : c.body_b;
                    if (contact_depth[from] == UNREACHED) continue;
                    if (contact_depth[to] > contact_depth[from] + 1) {
                        contact_depth[to] = contact_depth[from] + 1;
                        next.push_back(to);
                    }
                }
            }
            frontier.swap(next);
        }
        auto group_depth = [&](const ContactGroup& g) {
            const Constraint& c = constraints[g.first];
            const int da = contact_depth[c.body_a];
            const int db = c.is_turtle_contact ? 0 : contact_depth[c.body_b];
            return std::min(da, db);
        };
        std::stable_sort(contact_groups.begin(), contact_groups.end(),
                         [&](const ContactGroup& x, const ContactGroup& y) {
                             return group_depth(x) < group_depth(y);
                         });
    }

    // One manifold's rows, solved simultaneously. Mirrors the
    // sequential contact row EXACTLY: same measure gates (G-39:
    // measure-gate == apply-gate, DYNAMIC for contacts), same split-
    // impulse bias rule (only speculative negatives stay), same
    // clamped-accumulated application, same refused-momentum and
    // energy-ledger bookkeeping. Returns the largest |delta|.
    // shock: 0 = normal; 1 = hold body_a immovable; 2 = hold body_b.
    auto solve_contact_block = [&](const ContactGroup& g, int iter,
                                   float& max_dv, int shock) -> float {
        Constraint& c0 = constraints[g.first];
        Particle& pa = particles[c0.body_a];
        Particle& pb = particles[c0.body_b];
        const bool turtle = c0.is_turtle_contact;
        const float inv_ma = shock == 1 ? 0.0f : inv_mass_momentum(pa);
        const float inv_mb = (turtle || shock == 2) ? 0.0f
                                                    : inv_mass_momentum(pb);
        if (inv_ma + inv_mb <= 0.0f) return 0.0f;   // sleeping pair
        // COHERENCE (joint-block lesson): one opinion per body. A body
        // spins in this block only if it is DYNAMIC, movable, and has
        // inertia - the same predicate for measure and apply.
        const float Ia = pa.GetMomentOfInertia();
        const float Ib = pb.GetMomentOfInertia();
        const bool a_rot = pa.solver_mode == ParticleSolverMode::DYNAMIC &&
                           inv_ma > 0.0f && Ia > 0.0f;
        const bool b_rot = !turtle &&
                           pb.solver_mode == ParticleSolverMode::DYNAMIC &&
                           inv_mb > 0.0f && Ib > 0.0f;
        const float inv_Ia = a_rot ? 1.0f / Ia : 0.0f;
        const float inv_Ib = b_rot ? 1.0f / Ib : 0.0f;

        const int n = g.n;
        float L[4][3] = {}, Ga[4][3] = {}, Hb[4][3] = {};
        double A[4][4] = {}, b[4] = {};
        for (int i = 0; i < n; ++i) {
            const Constraint& c = constraints[g.first + (size_t)i];
            L[i][0] = c.jx; L[i][1] = c.jy; L[i][2] = c.jz;
            if (c.apply_anchor_torque && a_rot) {
                Ga[i][0] = c.anchor_ray * c.jz - c.anchor_raz * c.jy;
                Ga[i][1] = c.anchor_raz * c.jx - c.anchor_rax * c.jz;
                Ga[i][2] = c.anchor_rax * c.jy - c.anchor_ray * c.jx;
            }
            if (c.apply_anchor_torque && b_rot) {
                Hb[i][0] = -(c.anchor_rby * c.jz - c.anchor_rbz * c.jy);
                Hb[i][1] = -(c.anchor_rbz * c.jx - c.anchor_rbx * c.jz);
                Hb[i][2] = -(c.anchor_rbx * c.jy - c.anchor_rby * c.jx);
            }
            float err = turtle
                ? (c.jx * pa.vx + c.jy * pa.vy + c.jz * pa.vz)
                : (c.jx * (pa.vx - pb.vx) + c.jy * (pa.vy - pb.vy) +
                   c.jz * (pa.vz - pb.vz));
            err += Ga[i][0] * pa.omega_x + Ga[i][1] * pa.omega_y +
                   Ga[i][2] * pa.omega_z;
            if (!turtle)
                err += Hb[i][0] * pb.omega_x + Hb[i][1] * pb.omega_y +
                       Hb[i][2] * pb.omega_z;
            // Split impulse: only a speculative approach limit (a
            // contact with a GAP, bias < 0) keeps its bias here.
            float effective_bias = c.bias;
            if (ENABLE_SPLIT_IMPULSE && !(c.is_contact && c.bias < 0.0f))
                effective_bias = 0.0f;
            b[i] = (double)effective_bias - (double)err;
            for (int j2 = 0; j2 <= i; ++j2) {
                const Constraint& cj = constraints[g.first + (size_t)j2];
                const double ll = (double)L[i][0]*cj.jx + (double)L[i][1]*cj.jy
                                + (double)L[i][2]*cj.jz;
                double gg = 0.0, hh = 0.0;
                // Ga/Hb for row j2 were filled on its own visit (j2<=i).
                gg = (double)Ga[i][0]*Ga[j2][0] + (double)Ga[i][1]*Ga[j2][1]
                   + (double)Ga[i][2]*Ga[j2][2];
                hh = (double)Hb[i][0]*Hb[j2][0] + (double)Hb[i][1]*Hb[j2][1]
                   + (double)Hb[i][2]*Hb[j2][2];
                A[i][j2] = A[j2][i] =
                    ll * (double)(inv_ma + inv_mb) + gg * (double)inv_Ia
                    + hh * (double)inv_Ib;
            }
            A[i][i] += 1e-9;
        }

        // THE BLOCK IS A SMALL LCP, NOT A LINEAR SOLVE (G-63 correction,
        // 2026-09-01, measured detonation). Contacts are UNILATERAL:
        // solving the coupled system unconstrained and clamping per row
        // afterward turns a near-singular manifold (two almost-parallel
        // rows) into a huge (+L, -L) pair whose negative half the clamp
        // discards - a one-sided mega-impulse, energy from arithmetic
        // (the tile raft detonated to 99 m/s). The joint block never
        // met this: gluon rows are bidirectional. Here the exact
        // active-set enumeration solves the true LCP: for each subset
        // of free rows solve the subsystem with clamped rows at their
        // lower bound (-accumulated: total impulse >= 0), accept the
        // subset whose free deltas respect the bound and whose clamped
        // rows' residual velocity is separating. n <= 4: at most 16
        // subsets, unique solution for a PSD matrix, no iteration.
        double lam[4] = {};
        {
            double lo[4];
            for (int i = 0; i < n; ++i)
                lo[i] = -(double)constraints[g.first + (size_t)i]
                             .accumulated_impulse;
            bool solved = false;
            // Try subsets from all-free downward: the common case first.
            for (int mask = (1 << n) - 1; mask >= 0 && !solved; --mask) {
                int f_idx[4]; int nf = 0;
                for (int i = 0; i < n; ++i)
                    if (mask & (1 << i)) f_idx[nf++] = i;
                // Right-hand side for the free subsystem: b_F - A_FC * lo_C.
                double M[4][5];
                for (int r = 0; r < nf; ++r) {
                    const int i = f_idx[r];
                    double rhs = b[i];
                    for (int j2 = 0; j2 < n; ++j2)
                        if (!(mask & (1 << j2))) rhs -= A[i][j2] * lo[j2];
                    for (int c2 = 0; c2 < nf; ++c2)
                        M[r][c2] = A[i][f_idx[c2]];
                    M[r][nf] = rhs;
                }
                // Gaussian elimination with partial pivoting (nf <= 4).
                bool singular = false;
                for (int col = 0; col < nf && !singular; ++col) {
                    int piv = col;
                    for (int r = col + 1; r < nf; ++r)
                        if (std::fabs(M[r][col]) > std::fabs(M[piv][col]))
                            piv = r;
                    if (std::fabs(M[piv][col]) < 1e-20) { singular = true; break; }
                    if (piv != col)
                        for (int j2 = col; j2 <= nf; ++j2)
                            std::swap(M[col][j2], M[piv][j2]);
                    for (int r = col + 1; r < nf; ++r) {
                        const double f = M[r][col] / M[col][col];
                        for (int j2 = col; j2 <= nf; ++j2)
                            M[r][j2] -= f * M[col][j2];
                    }
                }
                if (singular) continue;
                double lf[4] = {};
                for (int i = nf - 1; i >= 0; --i) {
                    double acc2 = M[i][nf];
                    for (int j2 = i + 1; j2 < nf; ++j2)
                        acc2 -= M[i][j2] * lf[j2];
                    lf[i] = acc2 / M[i][i];
                }
                double cand[4];
                bool ok = true;
                for (int i = 0; i < n; ++i) cand[i] = lo[i];
                for (int r = 0; r < nf; ++r) {
                    cand[f_idx[r]] = lf[r];
                    if (lf[r] < lo[f_idx[r]] - 1e-9) { ok = false; break; }
                }
                if (ok) {
                    // Clamped rows must be legal: residual velocity
                    // separating (pushing them would need pulling).
                    for (int i = 0; i < n && ok; ++i) {
                        if (mask & (1 << i)) continue;
                        double r2 = -b[i];
                        for (int j2 = 0; j2 < n; ++j2)
                            r2 += A[i][j2] * cand[j2];
                        if (r2 < -1e-6) ok = false;
                    }
                }
                if (ok) {
                    for (int i = 0; i < n; ++i) lam[i] = cand[i];
                    solved = true;
                }
            }
            if (!solved) return 0.0f;   // degenerate beyond the subsets: leave
                                        // the group to the sequential rows
        }

        float worst = 0.0f;
        for (int i = 0; i < n; ++i) {
            Constraint& c = constraints[g.first + (size_t)i];
            const float old_acc = c.accumulated_impulse;
            float acc = old_acc + (float)lam[i];
            acc = std::max(c.min_impulse, std::min(c.max_impulse, acc));
            const float d = acc - old_acc;
            c.accumulated_impulse = acc;
            worst = std::max(worst, std::fabs(d));
            if (d == 0.0f) continue;
            pa.vx += L[i][0] * d * inv_ma;
            pa.vy += L[i][1] * d * inv_ma;
            pa.vz += L[i][2] * d * inv_ma;
            if (!turtle) {
                pb.vx -= L[i][0] * d * inv_mb;
                pb.vy -= L[i][1] * d * inv_mb;
                pb.vz -= L[i][2] * d * inv_mb;
                if (inv_mb == 0.0f && shock != 2)
                    record_refused_impulse(c.body_b, -L[i][0] * d,
                                           -L[i][1] * d, -L[i][2] * d);
            }
            if (!turtle && inv_ma == 0.0f && shock != 1)
                record_refused_impulse(c.body_a, L[i][0] * d,
                                       L[i][1] * d, L[i][2] * d);
            pa.omega_x += Ga[i][0] * d * inv_Ia;
            pa.omega_y += Ga[i][1] * d * inv_Ia;
            pa.omega_z += Ga[i][2] * d * inv_Ia;
            if (!turtle) {
                pb.omega_x += Hb[i][0] * d * inv_Ib;
                pb.omega_y += Hb[i][1] * d * inv_Ib;
                pb.omega_z += Hb[i][2] * d * inv_Ib;
            }
            const float dv_here = std::fabs(d) * std::max(inv_ma, inv_mb);
            if (dv_here > max_dv) max_dv = dv_here;
            if (energy_ledger_on() && c.effective_mass > 0.0f) {
                const double dke = double(d) * (double)(-(b[i]))
                                 + double(d) * double(d)
                                   / (2.0 * double(c.effective_mass));
                if (turtle) g_row_energy.turtle  += dke;
                else        g_row_energy.contact += dke;
            }
            // Every nonzero delta prints (d == 0 already continued
            // above); a noise threshold here would be a new magic
            // number (INV-29 gate).
            if (canary_active &&
                ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                std::cout << "[CANARY F" << phys_frame << " BLOCK I" << iter
                          << "] P" << c.body_a << "<->P" << c.body_b
                          << " d=" << d << " acc=" << c.accumulated_impulse
                          << " cap=[" << c.min_impulse << ","
                          << c.max_impulse << "]"
                          << (turtle ? " TURTLE" : " BOX") << std::endl;
            }
        }
        return worst;
    };

    // Level 2 ROW CENSUS: what the budget is spent on. a=turtle rows,
    // b=body-body contact rows, v=(gluon linear rows, angular rows).
    if (::logosphere::phystrace::level() >= 2) {
        int n_turtle = 0, n_contact = 0, n_gluon = 0, n_ang = 0;
        for (const Constraint& c : constraints) {
            if (c.is_angular) ++n_ang;
            else if (c.is_turtle_contact) ++n_turtle;
            else if (c.is_contact) ++n_contact;
            else ++n_gluon;
        }
        PHYS_TRACE(::logosphere::phystrace::Solve, "rows", n_turtle, n_contact,
                   "gluon_angular", (double)n_gluon, (double)n_ang);
    }
    for (int iter = 0; iter < SOLVER_ITERATIONS; ++iter) {
        ::logosphere::phystrace::set_iteration(iter);
        float max_impulse_this_iter = 0.0f;
        max_dv_this_iter = 0.0f;
        // Angular twin of max_dv: the largest angular-velocity change any
        // angular row imparted this sweep. See ANGULAR_RESIDUAL_FLOOR for
        // why raw angular impulse cannot gate an exit door.
        float max_domega_this_iter = 0.0f;
        actual_iterations = iter + 1;

        // G-63: the manifold blocks solve first each sweep; their rows'
        // sequential normal solve is skipped below (friction still runs).
        for (const ContactGroup& g : contact_groups) {
            const float w = solve_contact_block(g, iter, max_dv_this_iter, 0);
            max_impulse_this_iter = std::max(max_impulse_this_iter, w);
        }

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

                // SPLIT IMPULSE, angular half. The linear rows stopped feeding
                // their Baumgarte term into momentum (see the linear block
                // below); angular rows kept theirs, and it is the same disease
                // one DOF over: an orientation error that cannot close adds
                // bias-velocity to REAL omega every substep. Measured on the
                // humanoid drive rig (P948<->P910<->P914): the 2.3e-5 kg*m^2
                // shoulder-bridge bone carried a sustained |omega| ~ 3 rad/s
                // limit cycle — its two rows' biases (1.3 + 2.0 rad/s)
                // pumping a precession that re-rotated both error axes
                // 30-45 deg per substep and held the joint error at ~0.04 rad
                // forever. With the bias out of the velocity phase, angular
                // rows match omegas (the joint's damping statement) and
                // orientation error is geometry, repaired by the angular
                // position pass whose pseudo-omega is discarded.
                // ANGULAR_SPLIT_OFF=1: diagnostic lever, restores the old
                // bias-into-momentum behavior for A/B isolation.
                static const bool ang_split_off = std::getenv("ANGULAR_SPLIT_OFF") != nullptr;
                // CONSTRAINTS split, SPRINGS don't. A row with a finite
                // torque budget is a force-bounded bond: its bias IS its
                // spring force, already bounded by (k*e + c*w)*dt, and
                // moving it to the position pass hands a 2-gram blade
                // segment budget/I of pseudo-rotation per substep with no
                // momentum cost — measured on the walk-through-grass gate
                // as a blade catapulted 27.80 m at 19.2 m/s (baseline
                // 3.19 m). An unbounded row is a constraint: its bias is
                // geometry, and geometry goes to the position pass (same
                // law as the linear split above).
                const bool spring_row = std::isfinite(c.min_angular_impulse) ||
                                        std::isfinite(c.max_angular_impulse);
                // Contact-coupled AND saturated: see the body_in_contact
                // comment above. A row whose bias sits at the cap is
                // declaring its error is MOTION, not repair (that is what
                // the cap means: larger errors resolve over frames), and
                // motion against live contacts must be negotiated through
                // momentum. Unsaturated repair on a contact-coupled body
                // is SLOP-scale geometry the contacts can absorb — keeping
                // those rows in momentum instead left the two-joints arm
                // in a permanent velocity-phase tug (grazing bone contacts
                // never clear on a humanoid) that crept the shoulder
                // 0.018 -> 0.034 rad across the hold window.
                const bool contact_coupled =
                    body_in_contact[c.body_a] || body_in_contact[c.body_b];
                const bool saturated = c.angular_bias_saturated ||
                    std::fabs(c.angular_bias) >=
                    PhysicsV4::MAX_ANGULAR_BIAS_VELOCITY * 0.999f;
                float effective_angular_bias = c.angular_bias;
                if (ENABLE_SPLIT_IMPULSE && !ang_split_off &&
                    !spring_row && !(contact_coupled && saturated))
                    effective_angular_bias = 0.0f;
                // Compute angular impulse to satisfy constraint
                // Target: omega_rel_new = effective bias
                float angular_impulse = -(omega_rel - effective_angular_bias) * c.effective_inertia;

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

                float applied_inv_inertia = 0.0f;
                if (I_a > 0.0f && pa.solver_mode != ParticleSolverMode::KINEMATIC) {
                    float inv = 1.0f / I_a;
                    pa.omega_x += angular_impulse * ax_x * inv;
                    pa.omega_y += angular_impulse * ax_y * inv;
                    pa.omega_z += angular_impulse * ax_z * inv;
                    applied_inv_inertia = inv;
                }
                if (!c.is_turtle_contact && I_b > 0.0f && pb.solver_mode != ParticleSolverMode::KINEMATIC) {
                    float inv = 1.0f / I_b;
                    pb.omega_x -= angular_impulse * ax_x * inv;
                    pb.omega_y -= angular_impulse * ax_y * inv;
                    pb.omega_z -= angular_impulse * ax_z * inv;
                    applied_inv_inertia = std::max(applied_inv_inertia, inv);
                }

                // WHAT THAT ANGULAR IMPULSE ACTUALLY DOES — the rad/s it
                // imparts to the faster-responding endpoint. Same physical
                // quantity at every inertia; the raw impulse is not.
                {
                    const float dw = std::abs(angular_impulse) * applied_inv_inertia;
                    if (dw > max_domega_this_iter) max_domega_this_iter = dw;
                }

                // CANARY_DEBUG: angular impulse applied to canary.
                if (canary_active && std::abs(angular_impulse) > 1e-5f &&
                    ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " ANGSOLVE I" << iter
                              << "] P" << c.body_a << "<->P" << c.body_b
                              << " axis=(" << ax_x << "," << ax_y << "," << ax_z << ")"
                              << " w_rel=" << omega_rel
                              << " bias=" << c.angular_bias
                              << " dL=" << angular_impulse
                              << " acc=" << c.accumulated_angular_impulse
                              << " Ia=" << I_a << " Ib=" << I_b
                              << " inv=" << applied_inv_inertia
                              << " mode=" << (int)pa.solver_mode << "/" << (int)pb.solver_mode
                              << " wa=(" << pa.omega_x << "," << pa.omega_y << "," << pa.omega_z << ")"
                              << " wb=(" << pb.omega_x << "," << pb.omega_y << "," << pb.omega_z << ")"
                              << std::endl;
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
                // Anchor rows measure velocity AT THE ANCHOR here too
                // (G-43). This term lived only in the box-box branch, so
                // turtle rows were blind to rotation: after warm start
                // zeroed the linear approach they read v_rel ~ 0 forever,
                // fired nothing, and could neither pump a topple nor
                // brace against one. Support without rotation measurement
                // made the corner stand eternal on the turtle (R8) while
                // the identical pose fell through box-box rows (R7).
                // Gate mirrors the apply site (G-39: contacts gate on
                // solver_mode == DYNAMIC alone). A row that APPLIES torque
                // it never MEASURES over-corrects — measure and apply must
                // share one predicate.
                if (c.apply_anchor_torque &&
                    pa.solver_mode == ParticleSolverMode::DYNAMIC) {
                    v_rel += c.jx * (pa.omega_y * c.anchor_raz - pa.omega_z * c.anchor_ray)
                           + c.jy * (pa.omega_z * c.anchor_rax - pa.omega_x * c.anchor_raz)
                           + c.jz * (pa.omega_x * c.anchor_ray - pa.omega_y * c.anchor_rax);
                }
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
                    // Measure-gate == apply-gate (G-39): contact rows on
                    // solver_mode == DYNAMIC; non-contact anchor rows keep
                    // the quat-driven predicate. is_quat_driven belongs to
                    // humanoid/animation bodies — gating a plain DYNAMIC
                    // cube's contact on it left every ordinary body's
                    // rotation unmeasured while torque was still applied.
                    const bool a_meas = c.is_contact
                        ? (pa.solver_mode == ParticleSolverMode::DYNAMIC)
                        : (pa.is_quat_driven &&
                           pa.owner == ParticleOwner::PHYSICS);
                    const bool b_meas = c.is_contact
                        ? (pb.solver_mode == ParticleSolverMode::DYNAMIC)
                        : (pb.is_quat_driven &&
                           pb.owner == ParticleOwner::PHYSICS);
                    if (a_meas) {
                        v_rel += c.jx * (pa.omega_y * c.anchor_raz - pa.omega_z * c.anchor_ray)
                               + c.jy * (pa.omega_z * c.anchor_rax - pa.omega_x * c.anchor_raz)
                               + c.jz * (pa.omega_x * c.anchor_ray - pa.omega_y * c.anchor_rax);
                    }
                    if (b_meas) {
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
            // G-63: this row's normal solve already ran in its manifold
            // block this sweep; zero here makes every apply below a
            // no-op while friction (further down) still runs against
            // the block-updated accumulated impulse.
            if (!in_contact_block.empty() && in_contact_block[ci])
                impulse = 0.0f;

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
            float inv_ma = inv_mass_momentum(pa);
            float inv_mb = c.is_turtle_contact ? 0.0f : inv_mass_momentum(pb);

            // WHAT THAT IMPULSE ACTUALLY DOES. An impulse in N*s means nothing
            // on its own: 0.01 N*s is a shrug to a 112 kg branch and a shove to
            // a 0.107 kg leaf. Track the VELOCITY it imparts to the lighter
            // endpoint, which is the same physical quantity whatever the mass.
            {
                const float dv_here = std::abs(impulse) * std::max(inv_ma, inv_mb);
                if (dv_here > max_dv_this_iter) {
                    max_dv_this_iter = dv_here;
                    g_maxdv_body_a = (int)c.body_a;
                    g_maxdv_body_b = (int)c.body_b;
                    g_maxdv_type = c.is_turtle_contact ? 2 : (c.is_contact ? 1 : 0);
                }
            }

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

            // THE DOOR DECIDES, NOT THE CALL SITE (INV-7). This guard
            // used to re-ask the immovability question inline
            // (`!pb.is_at_rest && !KINEMATIC`) on top of inv_mb, which
            // is exactly what the predicate already answered. Harmless
            // while both agreed; the moment the resolver priced
            // sleepers honestly it made body A pay an impulse body B
            // never received — momentum destroyed at the door itself.
            // inv_mb is 0 for anyone immovable, so multiplying is the
            // whole guard.
            if (!c.is_turtle_contact) {
                pb.vx -= c.jx * impulse * inv_mb;
                pb.vy -= c.jy * impulse * inv_mb;
                pb.vz -= c.jz * impulse * inv_mb;
                // Book the half that could not be delivered. inv_mb == 0
                // means an external writer owns this body; the momentum
                // is real and belongs to that writer, not to the void.
                // ANY refusal, not just KINEMATIC ones. inv_mb is 0
                // whenever this body cannot take momentum — held by an
                // external writer, or asleep. A resting body books
                // nothing (no approach, no impulse); a STRUCK one books
                // exactly what it refused, which is the whole point.
                if (inv_mb == 0.0f && impulse != 0.0f) {
                    record_refused_impulse(c.body_b,
                                           -c.jx * impulse,
                                           -c.jy * impulse,
                                           -c.jz * impulse);
                }
            }
            // Turtle rows are excluded on BOTH sides: the turtle is the
            // world boundary (INV-1), not a body delivering a shove, and
            // its support force fires every frame under every resting
            // body. Booking it swamped the ledger with -14.23 N*s of
            // "someone pushed you" on a humanoid that was merely standing.
            if (!c.is_turtle_contact && inv_ma == 0.0f && impulse != 0.0f) {
                record_refused_impulse(c.body_a,
                                       c.jx * impulse,
                                       c.jy * impulse,
                                       c.jz * impulse);
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
                // Contact rows gate on solver_mode == DYNAMIC alone
                // (G-39): after the flip a DYNAMIC body's rotation is
                // visible and collidable the moment it is written, and
                // KINEMATIC excludes every animation-driven bone without
                // reading owner. Gluon rows keep their historical gate
                // untouched in this slice.
                const bool a_may_spin = c.is_contact
                    ? (pa.solver_mode == ParticleSolverMode::DYNAMIC &&
                       inv_ma > 0.0f)
                    : (pa.is_quat_driven &&
                       pa.owner == ParticleOwner::PHYSICS && inv_ma > 0.0f);
                if (a_may_spin) {
                    const float I = pa.GetMomentOfInertia();
                    if (I > 0.0f) {
                        const float inv = 1.0f / I;
                        pa.omega_x += (c.anchor_ray * fz - c.anchor_raz * fy) * inv;
                        pa.omega_y += (c.anchor_raz * fx - c.anchor_rax * fz) * inv;
                        pa.omega_z += (c.anchor_rax * fy - c.anchor_ray * fx) * inv;
                    }
                }
                const bool b_may_spin = c.is_contact
                    ? (pb.solver_mode == ParticleSolverMode::DYNAMIC &&
                       inv_mb > 0.0f)
                    : (!c.is_turtle_contact && pb.is_quat_driven &&
                       pb.owner == ParticleOwner::PHYSICS && inv_mb > 0.0f);
                if (b_may_spin) {
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
                          << " acc=" << c.accumulated_impulse
                          << " cap=[" << c.min_impulse << "," << c.max_impulse << "]"
                          << " bias=" << c.bias << " eff=" << c.effective_mass
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
                // FRICTION ACTS ONLY THROUGH A TOUCHING CONTACT (G-36's
                // real mechanism, found 2026-08-20). A NEGATIVE bias is
                // the row's own statement that the gap is still open
                // (split-impulse law above): its normal impulse is
                // numerical capture, not transmitted force, and mu times
                // a capture impulse is fiction. Measured: a cube spinning
                // 3 rad/s lost ALL of it in the last 3 cm of fall, 52.1
                // N*s of phantom friction across four corners, three
                // frames before the surfaces met. A real die keeps its
                // spin until the face touches.
                // G-79 (2026-09-05): TOUCHING IS GEOMETRIC, NOT A SIGN. Under the
                // single law a seated body sits at penetration under SLOP with
                // a negative bias by construction (bias = BETA (pen - SLOP) / dt),
                // so the sign gate below denies friction to every body resting
                // on a box: measured on G-77's platform (normal 1.633 N s,
                // v_rel 0.333, limit 0) and behind G-46's red. Under the lever
                // the gate is the gap itself: within SLOP the normal impulse is
                // transmitted force and carries friction; wider, it is capture.
                static const bool touch_geometric = std::getenv("FRICTION_TOUCH_GEOMETRIC") != nullptr;
                const bool capture_only = touch_geometric
                    ? (c.penetration < -PhysicsV4::SLOP)
                    : (c.bias < 0.0f);
                if (c.is_contact && !c.is_turtle_contact && capture_only)
                    friction_limit = 0.0f;

                // Determine tangent directions from normal (Jacobian)
                // If normal is Z-axis dominant, tangents are X and Y
                // If normal is X-axis dominant, tangents are Y and Z
                // If normal is Y-axis dominant, tangents are X and Z
                float t1x, t1y, t1z;  // Tangent 1
                float t2x, t2y, t2z;  // Tangent 2

                static const bool contact_torque_basis =
                    []{ const char* e = std::getenv("CONTACT_TORQUE"); return !(e && e[0] == '0' && e[1] == '\0'); }()  /* INV-32: torque is default physics; =0 is the kill switch */;
                if (contact_torque_basis) {
                    // THE TANGENTS LIVE IN THE CONTACT PLANE (G-40, same
                    // CONTACT_TORQUE lever). The axis-dominant picks below
                    // leave t1 with a sin(40) = 0.64 component ALONG a
                    // 40-degree ramp's normal: two thirds of a unit of
                    // every t1 impulse acted off-plane, a normal impulse
                    // in disguise, bypassing the unilateral clamp and the
                    // mu cap. Harmless-looking without torque; with it,
                    // r x t1 of an off-plane tangent has components no
                    // symmetry cancels, and the tumbling ramp cube walked
                    // out of its lane (y -1.20 -> -2.05, no lateral force
                    // in the scene) — which is how study question 6.4
                    // answered itself. Basis: the world axis LEAST aligned
                    // with the normal (deterministic tie-break, and
                    // least-aligned means no world-up preference, INV-6),
                    // then t1 = normalize(a x n), t2 = n x t1. For the
                    // ramp this yields t1 exactly downhill, t2 exactly
                    // lateral.
                    const float ax_ = std::abs(c.jx), ay_ = std::abs(c.jy),
                                az_ = std::abs(c.jz);
                    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
                    if (ax_ <= ay_ && ax_ <= az_)      rx = 1.0f;
                    else if (ay_ <= az_)               ry = 1.0f;
                    else                               rz = 1.0f;
                    t1x = ry * c.jz - rz * c.jy;
                    t1y = rz * c.jx - rx * c.jz;
                    t1z = rx * c.jy - ry * c.jx;
                    const float len = std::sqrt(t1x*t1x + t1y*t1y + t1z*t1z);
                    t1x /= len; t1y /= len; t1z /= len;   // |a x n| > 0 by choice of a
                    t2x = c.jy * t1z - c.jz * t1y;
                    t2y = c.jz * t1x - c.jx * t1z;
                    t2z = c.jx * t1y - c.jy * t1x;
                } else if (std::abs(c.jz) > 0.5f) {
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

                // FRICTION TORQUE (G-39 slice B, same CONTACT_TORQUE
                // lever; active only when the build stage set anchors on
                // this row). Friction acts AT THE CONTACT POINT, so its
                // truth is the contact-point relative velocity — v plus
                // omega x r per body — and its price is the tangent's own
                // K = 1/m + (r x t)^2/I (INV-20; the normal row's
                // effective mass carries the NORMAL's angular term, which
                // is the wrong Jacobian for a tangent). Its consequence
                // is the twist that makes a sphere roll instead of skate:
                // friction brakes the contact-point slip, the torque
                // spins the body, and rolling-without-slipping emerges
                // when omega x r cancels v. Gate per G-39: measurement
                // includes every body's omega (measuring is not
                // applying); application spins DYNAMIC bodies only.
                struct TangentTorque {
                    float rax, ray, raz, rbx, rby, rbz;   // r x t per body
                    float eff;                            // tangent's own K
                    bool  spin_a, spin_b;
                    float inv_Ia, inv_Ib;
                };
                auto tangent_terms = [&](float tx, float ty, float tz,
                                         float& v_rel) -> TangentTorque {
                    TangentTorque tt{};
                    tt.eff = c.effective_mass;   // lever off: unchanged
                    if (!(c.is_contact && c.apply_anchor_torque)) return tt;
                    // G-55: a FACE row's tangents are LINEAR-ONLY — no
                    // omega x r in the measurement, no r x t applied.
                    // Rotational braking belongs to the twist row with
                    // the face-integral limit; corner tangents at
                    // 0.707*L overbrake a spinning face 1.85x. The eff
                    // mass is the linear one (the normal row's K
                    // carries an angular term a linear tangent must
                    // not inherit).
                    if (c.face_friction) {
                        const float k_lin = inv_ma + inv_mb;
                        if (k_lin > 0.0f)
                            tt.eff = (1.0f / k_lin) * c.eff_mass_share;
                        return tt;
                    }
                    v_rel += tx*(pa.omega_y*c.anchor_raz - pa.omega_z*c.anchor_ray)
                           + ty*(pa.omega_z*c.anchor_rax - pa.omega_x*c.anchor_raz)
                           + tz*(pa.omega_x*c.anchor_ray - pa.omega_y*c.anchor_rax);
                    if (!c.is_turtle_contact) {
                        v_rel -= tx*(pb.omega_y*c.anchor_rbz - pb.omega_z*c.anchor_rby)
                               + ty*(pb.omega_z*c.anchor_rbx - pb.omega_x*c.anchor_rbz)
                               + tz*(pb.omega_x*c.anchor_rby - pb.omega_y*c.anchor_rbx);
                    }
                    tt.rax = c.anchor_ray*tz - c.anchor_raz*ty;
                    tt.ray = c.anchor_raz*tx - c.anchor_rax*tz;
                    tt.raz = c.anchor_rax*ty - c.anchor_ray*tx;
                    tt.rbx = c.anchor_rby*tz - c.anchor_rbz*ty;
                    tt.rby = c.anchor_rbz*tx - c.anchor_rbx*tz;
                    tt.rbz = c.anchor_rbx*ty - c.anchor_rby*tx;
                    float k = inv_ma + inv_mb;
                    tt.spin_a = pa.solver_mode == ParticleSolverMode::DYNAMIC &&
                                inv_ma > 0.0f;
                    tt.spin_b = !c.is_turtle_contact &&
                                pb.solver_mode == ParticleSolverMode::DYNAMIC &&
                                inv_mb > 0.0f;
                    if (tt.spin_a) {
                        const float I = pa.GetMomentOfInertia();
                        if (I > 0.0f) {
                            tt.inv_Ia = 1.0f / I;
                            k += (tt.rax*tt.rax + tt.ray*tt.ray + tt.raz*tt.raz)
                                 * tt.inv_Ia;
                        } else tt.spin_a = false;
                    }
                    if (tt.spin_b) {
                        const float I = pb.GetMomentOfInertia();
                        if (I > 0.0f) {
                            tt.inv_Ib = 1.0f / I;
                            k += (tt.rbx*tt.rbx + tt.rby*tt.rby + tt.rbz*tt.rbz)
                                 * tt.inv_Ib;
                        } else tt.spin_b = false;
                    }
                    if (k > 0.0f) tt.eff = (1.0f / k) * c.eff_mass_share;
                    return tt;
                };

                // Friction constraint 1 (tangent 1)
                // Start with pa's velocity; subtract pb's only if pb is moving
                // MEASURE THE TRUTH, always. Skipping a sleeper's
                // velocity here was harmless only because rest-entry
                // zeroes it; under the resolver a sleeping body carries
                // real velocity mid-substep, and a relative velocity that
                // ignores one side is not a relative velocity. Only the
                // turtle is exempt: it is a boundary, not a body, and has
                // no velocity to subtract.
                float v_rel_t1 = t1x * pa.vx + t1y * pa.vy + t1z * pa.vz;
                if (!c.is_turtle_contact) {
                    v_rel_t1 -= t1x * pb.vx + t1y * pb.vy + t1z * pb.vz;
                }
                const TangentTorque tt1 = tangent_terms(t1x, t1y, t1z, v_rel_t1);
                float friction_impulse_1 = -v_rel_t1 * tt1.eff;
                float old_f1 = c.friction_impulse_t1;
                c.friction_impulse_t1 = std::max(-friction_limit,
                                        std::min(friction_limit, old_f1 + friction_impulse_1));
                friction_impulse_1 = c.friction_impulse_t1 - old_f1;
                // CANARY: the friction row as priced and spent (INV-39's G-77 read).
                if (canary_active &&
                    ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " FRIC1 I" << iter << "] P" << c.body_a << "<->P" << c.body_b
                              << " t1=(" << t1x << "," << t1y << "," << t1z << ")"
                              << " v_rel=" << v_rel_t1 << " eff=" << tt1.eff
                              << " limit=" << friction_limit << " dJ=" << friction_impulse_1
                              << " acc=" << c.friction_impulse_t1
                              << " inv_ma=" << inv_ma << " inv_mb=" << inv_mb
                              << " va=(" << pa.vx << "," << pa.vy << "," << pa.vz << ")"
                              << " vb=(" << pb.vx << "," << pb.vy << "," << pb.vz << ")" << std::endl;
                }

                if (energy_ledger_on() && friction_impulse_1 != 0.0f) {
                    // Work removed along this tangent: J * v_rel plus the
                    // J^2/2m the impulse itself carries, same shape as the
                    // normal row's accounting above.
                    const double inv_sum = double(inv_ma) + double(inv_mb);
                    if (inv_sum > 0.0) {
                        const double j = friction_impulse_1;
                        g_dissipation.friction -= j * double(v_rel_t1)
                                                + j * j * inv_sum * 0.5;
                    }
                }
                PHYS_TRACE_F(::logosphere::phystrace::Constraint,
                             "friction_impulse", (int)c.body_a, (int)c.body_b,
                             "t1", friction_impulse_1, friction_limit,
                             v_rel_t1);
                pa.vx += t1x * friction_impulse_1 * inv_ma;
                pa.vy += t1y * friction_impulse_1 * inv_ma;
                pa.vz += t1z * friction_impulse_1 * inv_ma;
                if (tt1.spin_a && friction_impulse_1 != 0.0f) {
                    pa.omega_x += tt1.rax * friction_impulse_1 * tt1.inv_Ia;
                    pa.omega_y += tt1.ray * friction_impulse_1 * tt1.inv_Ia;
                    pa.omega_z += tt1.raz * friction_impulse_1 * tt1.inv_Ia;
                }

                // DEBUG: Log turtle friction (once per frame)
                if (c.is_turtle_contact && canary_active && (int)c.body_a == canary_pid && iter == 0) {
                    std::cout << "[TURTLE_FRIC F" << phys_frame << "] vx_after=" << pa.vx
                              << " v_rel=" << v_rel_t1 << " f_imp=" << friction_impulse_1
                              << " limit=" << friction_limit << " accum=" << c.accumulated_impulse
                              << std::endl;
                }

                // THE DOOR DECIDES (INV-7). inv_mb is already 0 for
                // anyone immovable; re-asking the question here made body
                // A pay friction body B never received the moment the two
                // disagreed. Same cure as the normal-impulse apply.
                if (!c.is_turtle_contact) {
                    pb.vx -= t1x * friction_impulse_1 * inv_mb;
                    pb.vy -= t1y * friction_impulse_1 * inv_mb;
                    pb.vz -= t1z * friction_impulse_1 * inv_mb;
                    if (tt1.spin_b && friction_impulse_1 != 0.0f) {
                        pb.omega_x -= tt1.rbx * friction_impulse_1 * tt1.inv_Ib;
                        pb.omega_y -= tt1.rby * friction_impulse_1 * tt1.inv_Ib;
                        pb.omega_z -= tt1.rbz * friction_impulse_1 * tt1.inv_Ib;
                    }
                    // Friction is momentum too. The whole block booked
                    // nothing until 2026-08-14, so a body braced against
                    // a shove absorbed all the tangential load silently.
                    if (inv_mb == 0.0f && friction_impulse_1 != 0.0f) {
                        record_refused_impulse(c.body_b,
                                               -t1x * friction_impulse_1,
                                               -t1y * friction_impulse_1,
                                               -t1z * friction_impulse_1);
                    }
                }
                if (!c.is_turtle_contact && inv_ma == 0.0f &&
                    friction_impulse_1 != 0.0f) {
                    record_refused_impulse(c.body_a,
                                           t1x * friction_impulse_1,
                                           t1y * friction_impulse_1,
                                           t1z * friction_impulse_1);
                }

                // Friction constraint 2 (tangent 2)
                // Start with pa's velocity; subtract pb's only if pb is moving
                float v_rel_t2 = t2x * pa.vx + t2y * pa.vy + t2z * pa.vz;
                if (!c.is_turtle_contact) {
                    v_rel_t2 -= t2x * pb.vx + t2y * pb.vy + t2z * pb.vz;
                }
                const TangentTorque tt2 = tangent_terms(t2x, t2y, t2z, v_rel_t2);
                float friction_impulse_2 = -v_rel_t2 * tt2.eff;
                float old_f2 = c.friction_impulse_t2;
                // G-55: face rows close the friction cone as a VECTOR.
                // Two independent clamps let the tangent pair reach
                // sqrt(2)*mu*N on the diagonal, and a spinning contact
                // samples the diagonal continuously.
                float limit_t2 = friction_limit;
                if (c.face_friction) {
                    const float f1 = c.friction_impulse_t1;
                    const float rem2 = friction_limit * friction_limit
                                     - f1 * f1;
                    limit_t2 = (rem2 > 0.0f) ? std::sqrt(rem2) : 0.0f;
                }
                c.friction_impulse_t2 = std::max(-limit_t2,
                                        std::min(limit_t2, old_f2 + friction_impulse_2));
                friction_impulse_2 = c.friction_impulse_t2 - old_f2;
                if (canary_active &&
                    ((int)c.body_a == canary_pid || (int)c.body_b == canary_pid)) {
                    std::cout << "[CANARY F" << phys_frame << " FRIC2 I" << iter << "] P" << c.body_a << "<->P" << c.body_b
                              << " t2=(" << t2x << "," << t2y << "," << t2z << ")"
                              << " v_rel=" << v_rel_t2 << " eff=" << tt2.eff
                              << " limit=" << friction_limit << " dJ=" << friction_impulse_2
                              << " acc=" << c.friction_impulse_t2 << " normal_acc=" << c.accumulated_impulse
                              << " vb=(" << pb.vx << "," << pb.vy << "," << pb.vz << ")" << std::endl;
                }

                if (energy_ledger_on() && friction_impulse_2 != 0.0f) {
                    const double inv_sum = double(inv_ma) + double(inv_mb);
                    if (inv_sum > 0.0) {
                        const double j = friction_impulse_2;
                        g_dissipation.friction -= j * double(v_rel_t2)
                                                + j * j * inv_sum * 0.5;
                    }
                }
                PHYS_TRACE_F(::logosphere::phystrace::Constraint,
                             "friction_impulse", (int)c.body_a, (int)c.body_b,
                             "t2", friction_impulse_2, friction_limit,
                             v_rel_t2);
                pa.vx += t2x * friction_impulse_2 * inv_ma;
                pa.vy += t2y * friction_impulse_2 * inv_ma;
                pa.vz += t2z * friction_impulse_2 * inv_ma;
                if (tt2.spin_a && friction_impulse_2 != 0.0f) {
                    pa.omega_x += tt2.rax * friction_impulse_2 * tt2.inv_Ia;
                    pa.omega_y += tt2.ray * friction_impulse_2 * tt2.inv_Ia;
                    pa.omega_z += tt2.raz * friction_impulse_2 * tt2.inv_Ia;
                }

                if (!c.is_turtle_contact && inv_ma == 0.0f &&
                    friction_impulse_2 != 0.0f) {
                    record_refused_impulse(c.body_a,
                                           t2x * friction_impulse_2,
                                           t2y * friction_impulse_2,
                                           t2z * friction_impulse_2);
                }
                if (!c.is_turtle_contact) {
                    pb.vx -= t2x * friction_impulse_2 * inv_mb;
                    pb.vy -= t2y * friction_impulse_2 * inv_mb;
                    pb.vz -= t2z * friction_impulse_2 * inv_mb;
                    if (tt2.spin_b && friction_impulse_2 != 0.0f) {
                        pb.omega_x -= tt2.rbx * friction_impulse_2 * tt2.inv_Ib;
                        pb.omega_y -= tt2.rby * friction_impulse_2 * tt2.inv_Ib;
                        pb.omega_z -= tt2.rbz * friction_impulse_2 * tt2.inv_Ib;
                    }
                    if (inv_mb == 0.0f && friction_impulse_2 != 0.0f) {
                        record_refused_impulse(c.body_b,
                                               -t2x * friction_impulse_2,
                                               -t2y * friction_impulse_2,
                                               -t2z * friction_impulse_2);
                    }
                }

                // ========================================================
                // THE TWIST ROW (G-55, face contacts, FRICTION_TWIST=1).
                // Torsional Coulomb friction about the contact normal:
                // a spinning face brakes at mu * N_total * <r>, the face
                // integral, not at corner radii. One carrier row per
                // manifold. Gates per G-39: measure every body's omega,
                // apply to DYNAMIC only (the turtle has none to measure).
                // ========================================================
                // G-45 binds here too: a speculative row (gap open)
                // transmits no torsional friction either — its capture
                // impulses are not contact forces, and a twist across
                // an open gap killed airborne spin (ladder R2/R3, the
                // same disease the linear gate cured).
                // G-63 refinement (2026-09-01, measured: ladder R2/R4
                // spin never braked): the exact LCP block routinely
                // satisfies a micro-tilted manifold with the CARRIER
                // row at zero while the group carries the full load.
                // The twist's law is mu * N_TOTAL * <r> (summed below),
                // so the carrier's OWN impulse must not gate the group.
                // The default world keeps the old gate byte-identically.
                // ...and the single-law form of the same gate is
                // G-45 stated directly: torsion transmits only through
                // REAL contact. bias < 0 IS the engine's own speculative
                // marker (see the split-impulse comment at the solve),
                // and a predicted-pose face manifold can carry
                // twist_carrier with the true gap 76 mm open (measured:
                // the ladder hero's spin died 51 mm above the slab, F78
                // canary pen = -75.9 mm). The capture rows may stop the
                // approach; they transmit no torsion.
                // The single-law world RESTS slightly separated (the
                // speculative persistent rows hold bodies ~0.2 mm off),
                // so bias >= 0 excluded rest itself and the landed cube
                // spun forever. SLOP is the engine's own statement of
                // which gap is not real: within SLOP is touching, beyond
                // it is G-45's open gap.
                if (c.twist_carrier && c.twist_r > 0.0f &&
                    (single_law ? c.penetration > -PhysicsV4::SLOP
                                : friction_limit > 0.0f) &&
                    (!c.is_turtle_contact || c.penetration > 0.0f)) {
                    const float nx = c.jx, ny = c.jy, nz = c.jz;
                    float w_rel = pa.omega_x*nx + pa.omega_y*ny
                                + pa.omega_z*nz;
                    if (!c.is_turtle_contact)
                        w_rel -= pb.omega_x*nx + pb.omega_y*ny
                               + pb.omega_z*nz;
                    float invIa = 0.0f, invIb = 0.0f;
                    if (pa.solver_mode == ParticleSolverMode::DYNAMIC &&
                        inv_ma > 0.0f) {
                        const float Ia = pa.GetMomentOfInertia();
                        if (Ia > 0.0f) invIa = 1.0f / Ia;
                    }
                    if (!c.is_turtle_contact &&
                        pb.solver_mode == ParticleSolverMode::DYNAMIC &&
                        inv_mb > 0.0f) {
                        const float Ib = pb.GetMomentOfInertia();
                        if (Ib > 0.0f) invIb = 1.0f / Ib;
                    }
                    const float k_tw = invIa + invIb;
                    if (k_tw > 0.0f) {
                        // N_total: the manifold's whole normal impulse,
                        // summed EXACTLY over the pair's rows, which
                        // their builders push contiguously. A share
                        // reconstruction from one row over-estimated
                        // 1.6x on the pen-weighted turtle patch
                        // (measured limits 48-59 vs the derived 31).
                        auto same_group = [&](const Constraint& g) {
                            return g.body_a == c.body_a &&
                                   g.body_b == c.body_b &&
                                   g.is_turtle_contact ==
                                       c.is_turtle_contact &&
                                   !g.is_angular;
                        };
                        // Under the single law, speculative rows
                        // (bias < 0) contribute NO normal force to the
                        // twist limit - capture is not contact (G-45).
                        auto n_of = [&](const Constraint& g) {
                            if (single_law &&
                                g.penetration <= -PhysicsV4::SLOP)
                                return 0.0f;   // open gap: capture, not contact
                            return std::fmax(0.0f, g.accumulated_impulse);
                        };
                        float n_total = 0.0f;
                        for (size_t gj = ci;; --gj) {
                            if (!same_group(constraints[gj])) break;
                            n_total += n_of(constraints[gj]);
                            if (gj == 0) break;
                        }
                        for (size_t gj = ci + 1;
                             gj < constraints.size() &&
                             same_group(constraints[gj]); ++gj)
                            n_total += n_of(constraints[gj]);
                        const float t_limit =
                            combined_friction * n_total * c.twist_r;
                        float t_imp = -w_rel / k_tw;
                        const float old_t = c.twist_impulse;
                        c.twist_impulse = std::max(-t_limit,
                                          std::min(t_limit, old_t + t_imp));
                        t_imp = c.twist_impulse - old_t;
                        if (energy_ledger_on() && t_imp != 0.0f) {
                            const double j = t_imp;
                            g_dissipation.friction -=
                                j * double(w_rel)
                                + j * j * double(k_tw) * 0.5;
                        }
                        PHYS_TRACE_F(::logosphere::phystrace::Constraint,
                                     "twist_impulse",
                                     (int)c.body_a, (int)c.body_b, "twist",
                                     t_imp, t_limit, w_rel);
                        if (t_imp != 0.0f) {
                            pa.omega_x += nx * t_imp * invIa;
                            pa.omega_y += ny * t_imp * invIa;
                            pa.omega_z += nz * t_imp * invIa;
                            if (!c.is_turtle_contact) {
                                pb.omega_x -= nx * t_imp * invIb;
                                pb.omega_y -= ny * t_imp * invIb;
                                pb.omega_z -= nz * t_imp * invIb;
                            }
                        }
                    }
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
        // CONVERGED = the next iteration would change no body's velocity by
        // more than VELOCITY_THRESHOLD. Velocity is the only quantity that
        // makes the same physical statement at every mass. The impulse
        // AND-term this replaces (0.01 N*s) was the wrong dimension, and
        // with derived material stiffness it became unreachable: heavy rows
        // hold impulses above a threshold that means 3e-5 m/s on a strata
        // tile, so 58k-row Eden substeps burned all 32 iterations every
        // time (2.5 FPS, 360 ms of physics a frame) while the light-body
        // residuals this velocity term watches were long gone.
        // ...and the velocity-only variant that replaced it was unreachable
        // from the other side: 1 mm/s on a 0.1 g grass segment is 1e-7 N*s,
        // solver noise floor, so 58k-row Eden still burned all 32 iterations
        // (measured: maxdv argmax hopping between adjacent grass-segment
        // bonds, a different pair every substep). The light-body residual
        // this term chased is cured at the mechanism now: the impulse-memory
        // cap is in momentum units and the force law is derived, and the
        // foliage gate is the regression tripwire. Impulse threshold alone.
        // ABSOLUTE_THRESHOLD: INV-29 registry (ConvergenceExits group).
        // Angular AND-term: the impulse threshold is momentum-dimensioned and
        // blind to angular rows on light bodies (a 1.5e-4 N*m*s drive impulse
        // is 6.5 rad/s on a 2.3e-5 kg*m^2 bone — measured exiting at iters=1
        // on every humanoid drive substep, shoulder converging at 1/65th of
        // its commanded rate). Converged means the last sweep also stopped
        // spinning anything.
        // ANGULAR_EXIT_OFF=1: diagnostic lever, restores the angular-blind
        // exit doors for A/B isolation.
        static const bool ang_exit_off = std::getenv("ANGULAR_EXIT_OFF") != nullptr;
        if (max_impulse_this_iter < ABSOLUTE_THRESHOLD &&
            (ang_exit_off ||
             max_domega_this_iter < PhysicsV4::ANGULAR_RESIDUAL_FLOOR)) {
            LOGO_COUNT(::logosphere::telemetry::Counter::PhysSolveConverged);
            solve_exit_recorded = true;
            last_solve_ = SolveStats{(int)constraints.size(), actual_iterations, "impulse_under_absolute_threshold"};
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
        // The plateau door carries the same angular guard: a drive row
        // shoving a light link 6 rad/s per sweep with ~1% improvement per
        // iteration is a HARD PROBLEM mid-solve (Gauss-Seidel propagating
        // torque through a 65x inertia mismatch), not a converged one.
        if (iter >= MIN_ITERATIONS && prev_max_impulse > 0.0f &&
            (ang_exit_off ||
             max_domega_this_iter < PhysicsV4::ANGULAR_RESIDUAL_FLOOR) &&
            // G-63: the registry (ConvergenceExits group) has always
            // DOCUMENTED a linear residual guard on this door -
            // VELOCITY_PLATEAU_FLOOR - and the code never enforced it.
            // A chain ping-ponging at constant amplitude reads as a
            // plateau to the rate test while its light middle body
            // still exchanges 44 mm/s per sweep (measured, the 38.6:1
            // anvil, canary F1000: exit at iter 9 of 32 with the wood
            // at -0.164 m/s). Under the single law the door honors its
            // own registry; the default keeps the drift, booked on the
            // board.
            (!single_law || !single_law_door ||
             max_dv_this_iter < PhysicsV4::VELOCITY_PLATEAU_FLOOR)) {
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
                    last_solve_ = SolveStats{(int)constraints.size(), actual_iterations, "improvement_below_min_rate"};
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

    // THE SHOCK SWEEP (G-64): bottom-up once, each block's lower body
    // held immovable. Blocks whose bodies sit at equal depth, or with a
    // body the graph never reached, are solved with both sides movable.
    if (single_law && !contact_depth.empty()) {
        float shock_dv = 0.0f;
        for (const ContactGroup& g : contact_groups) {
            const Constraint& c = constraints[g.first];
            int shock = 0;
            if (!c.is_turtle_contact) {
                const int da = contact_depth[c.body_a];
                const int db = contact_depth[c.body_b];
                if (da < db) shock = 1; else if (db < da) shock = 2;
            }
            solve_contact_block(g, SOLVER_ITERATIONS, shock_dv, shock);
        }
    }

    // Neither door taken: the full budget ran without converging or plateauing.
    if (!solve_exit_recorded) {
        LOGO_COUNT(::logosphere::telemetry::Counter::PhysSolveExhausted);
        last_solve_ = SolveStats{(int)constraints.size(), actual_iterations, "iteration_budget_exhausted"};
        PHYS_TRACE(::logosphere::phystrace::Solve, "solve_exit", (int)constraints.size(),
                   actual_iterations, "iteration_budget_exhausted",
                   (double)SOLVER_ITERATIONS);
    }
    record_row_telemetry(particles, constraints, phys_frame);
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
        std::cout << " contacts=" << canary_contacts << " turtle=" << canary_turtle
                  << " omega=(" << cp.omega_x << "," << cp.omega_y << "," << cp.omega_z << ")"
                  << " q=(" << cp.rotation_q.w << "," << cp.rotation_q.x << ","
                  << cp.rotation_q.y << "," << cp.rotation_q.z << ")" << std::endl;
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
    if (ENABLE_WARM_STARTING) {
        // Store side of the group warm start: the cache holds the
        // GROUP TOTAL per key (it always claimed to; the old loop
        // stored only the first row's value, undercounting every
        // multi-point contact). Per row the equilibrium-freeze rule is
        // kept: a warm-started row contributes its warm share, not the
        // share plus solver delta (V4.6: corrections repair residuals,
        // they are not equilibrium).
        std::map<std::tuple<size_t, size_t, int>, float> key_totals;
        for (size_t ci = 0; ci < constraints.size(); ++ci) {
            const Constraint& c = constraints[ci];
            if (c.is_angular) continue;
            if (!c.is_contact && !c.is_turtle_contact) continue;
            std::tuple<size_t, size_t, int> kt;
            if (c.is_turtle_contact) {
                kt = {c.body_a, c.body_a, 0};
            } else {
                const bool swapped = c.body_a > c.body_b;
                const float jx_can = swapped ? -c.jx : c.jx;
                const float jy_can = swapped ? -c.jy : c.jy;
                const float jz_can = swapped ? -c.jz : c.jz;
                int type;
                if (std::abs(jz_can) > 0.5f)      type = (jz_can > 0) ? 5 : 6;
                else if (std::abs(jx_can) > 0.5f) type = (jx_can > 0) ? 1 : 2;
                else                              type = (jy_can > 0) ? 3 : 4;
                kt = {std::min(c.body_a, c.body_b),
                      std::max(c.body_a, c.body_b), type};
            }
            float velocity_impulse;
            if (ENABLE_SPLIT_IMPULSE) {
                velocity_impulse = c.accumulated_impulse;
            } else {
                float bias_impulse = c.bias * c.effective_mass;
                velocity_impulse = c.accumulated_impulse - bias_impulse;
                if (velocity_impulse < 0) velocity_impulse = 0;
            }
            auto it = warm_started_impulses.find(ci);
            // MEASUREMENT LEVER (G-44 at the warm cache, default off):
            // the equilibrium-freeze stores a warm-started row's own warm
            // share back, so a cached key can never learn a new fixed
            // point. Under a standing load the iterations rebuild the true
            // support every substep (measured: 290 N*s found, 86.9 stored,
            // need 306) and the freeze discards it. WARM_LEARN=1 stores
            // the true accumulated total instead. The freeze's own reason
            // (V4.6: bias-contaminated totals ratcheting) predates split
            // impulse, under which the accumulated impulse is bias-free.
            static const bool warm_learn =
                []{ const char* e = std::getenv("WARM_LEARN"); return !(e && e[0] == '0' && e[1] == '\0'); }();  // INV-36 default; =0 kills
            const bool frozen = !warm_learn &&
                                it != warm_started_impulses.end();
            float learned = velocity_impulse;
            // G-63 CORRECTION (2026-09-01, measured detonations): the
            // first single-law cut stored the TRUE TOTAL when the cap
            // was infinite - removing exactly the approach-subtraction
            // G-52 exists for. Yesterday's capture transient re-applied
            // onto moved geometry with NO cap to bound it detonated
            // every contact-turnover scene (tiles at 99 m/s, the ramp
            // cube flying 204 m). G-52's law holds in BOTH worlds; the
            // capless world reads the approach from the row itself.
            if (warm_learn && c.effective_mass > 0.0f) {
                // G-52: cache what SUSTAINS, forget what CAPTURES. The
                // accumulated impulse holds two physically different
                // parts: the approach cancellation (a transient that
                // exists only while something arrives) and the sustained
                // support (the fixed point a resting load needs every
                // substep). Caching the capture re-applies yesterday's
                // strike to today's moved geometry (measured: the ramp's
                // tumbling cube gains 5.8% travel). The row's build-time
                // approach is recoverable from its own capture bound
                // (max_impulse = eff * (approach + cushion), every
                // contact and turtle row site).
                const float approach = std::isfinite(c.max_impulse)
                    ? c.max_impulse / c.effective_mass
                      - PhysicsV4::CONTACT_CAPTURE_CUSHION
                    : c.build_approach;
                if (approach > 0.0f)
                    learned = std::max(
                        0.0f, velocity_impulse - c.effective_mass * approach);
            }
            const float stored = frozen ? it->second : learned;
            key_totals[kt] += stored;
            if (canary_active && ((int)c.body_a == canary_pid ||
                                  (int)c.body_b == canary_pid)) {
                std::cout << "[CANARY F" << phys_frame << " STORE] P"
                          << c.body_a << "<->P" << c.body_b
                          << " accumulated=" << c.accumulated_impulse
                          << " stored=" << stored
                          << (frozen ? " FROZEN-AT-WARM" : " TRUE-TOTAL")
                          << std::endl;
            }
        }
        for (const auto& kv : key_totals) {
            ContactKey key;
            key.particle_a = std::get<0>(kv.first);
            key.particle_b = std::get<1>(kv.first);
            key.contact_type = std::get<2>(kv.first);
            cached_impulses_[key] = {kv.second,
                                     static_cast<int>(warm_debug_frame)};
        }

        // Aged cleanup - only delete entries unused for 60 frames (1 second)
        // This allows brief oscillations where contact lifts off and reforms
        for (auto it = cached_impulses_.begin(); it != cached_impulses_.end(); ) {
            int last_used = it->second.second;  // (impulse, last_frame) pair
            int age = static_cast<int>(warm_debug_frame) - last_used;
            if (age > WARM_CACHE_AGE_LIMIT) {
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
        // Pseudo ANGULAR velocity: the angular rows' Baumgarte terms were
        // held out of the velocity solve (same split the linear rows get),
        // so their orientation error is repaired here — pseudo-omega spent
        // on rotation_q below, then discarded. Allocated lazily: most
        // worlds have no biased angular row in a given substep.
        std::vector<float> pwx, pwy, pwz;
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
        // (The momentum-side predicate is inv_mass_momentum; the two differ
        // on sleep, deliberately.)
        auto inv_mass_positional = [](const Particle& p) -> float {
            if (p.solver_mode == ParticleSolverMode::KINEMATIC) return 0.0f;
            const float m = p.GetMass();
            return (m > 0.0f) ? (1.0f / m) : 0.0f;
        };

        // THE BOUNDARY PAYS LIKE EVERYONE ELSE (G-50, lever
        // TURTLE_PRICED=1): turtle rows join this pass instead of being
        // repaired by the free clamp. Self-referenced body_b is the
        // turtle: infinite positional mass, a-side apply only.
        static const bool turtle_priced =
            std::getenv("TURTLE_PRICED") != nullptr;
        for (int iter = 0; iter < PhysicsV4::POSITION_ITERATIONS; ++iter) {
            for (Constraint& c : constraints) {
                if (c.is_turtle_contact && !turtle_priced) continue;
                // ---- Angular rows: repair orientation error via pseudo-omega.
                if (c.is_angular) {
                    static const bool ang_split_off2 = std::getenv("ANGULAR_SPLIT_OFF") != nullptr;
                    if (ang_split_off2) continue;           // lever: bias stayed in the velocity solve
                    // Springs and saturated contact-coupled rows kept their
                    // bias in the velocity solve (see the spring_row and
                    // body_in_contact comments there); repairing them here
                    // too would double-apply the force.
                    if (std::isfinite(c.min_angular_impulse) ||
                        std::isfinite(c.max_angular_impulse)) continue;
                    if ((body_in_contact[c.body_a] || body_in_contact[c.body_b]) &&
                        (c.angular_bias_saturated ||
                         std::fabs(c.angular_bias) >=
                            PhysicsV4::MAX_ANGULAR_BIAS_VELOCITY * 0.999f)) continue;
                    if (c.angular_bias == 0.0f) continue;   // dead-zone rows couple velocity only
                    // Tolerance, same law as SLOP: bias*dt is the rotation this
                    // pass wants; under the floor it is float residue.
                    if (std::fabs(c.angular_bias) * dt < PhysicsV4::ANGULAR_SLOP) continue;
                    if (c.body_a >= count || c.body_b >= count) continue;

                    float ax_x, ax_y, ax_z;
                    if (c.angular_axis_vec_len > 0.0f) {
                        ax_x = c.angular_axis_x; ax_y = c.angular_axis_y; ax_z = c.angular_axis_z;
                    } else {
                        ax_x = (c.angular_axis_idx == 0) ? 1.0f : 0.0f;
                        ax_y = (c.angular_axis_idx == 1) ? 1.0f : 0.0f;
                        ax_z = (c.angular_axis_idx == 2) ? 1.0f : 0.0f;
                    }

                    // Positional inertia door: only KINEMATIC is immovable here
                    // (sleep is not immobility), matching inv_mass_positional.
                    const Particle& ra = particles[c.body_a];
                    const Particle& rb = particles[c.body_b];
                    const float I_a = (ra.solver_mode == ParticleSolverMode::KINEMATIC)
                        ? 0.0f : ra.GetInertiaAboutAxis(ax_x, ax_y, ax_z);
                    const float I_b = (rb.solver_mode == ParticleSolverMode::KINEMATIC)
                        ? 0.0f : rb.GetInertiaAboutAxis(ax_x, ax_y, ax_z);
                    if (I_a <= 0.0f && I_b <= 0.0f) continue;

                    if (pwx.empty()) {
                        pwx.assign(count, 0.0f);
                        pwy.assign(count, 0.0f);
                        pwz.assign(count, 0.0f);
                    }
                    const float pw_rel =
                        (pwx[c.body_a] - pwx[c.body_b]) * ax_x +
                        (pwy[c.body_a] - pwy[c.body_b]) * ax_y +
                        (pwz[c.body_a] - pwz[c.body_b]) * ax_z;

                    float aimp = -(pw_rel - c.angular_bias) * c.effective_inertia;

                    // Same clamp the velocity rows obey: a force-bounded bond
                    // repairs its bend within its torque budget, never past it.
                    const float aold = c.accumulated_pseudo_impulse;
                    c.accumulated_pseudo_impulse =
                        std::max(c.min_angular_impulse,
                                 std::min(c.max_angular_impulse, aold + aimp));
                    aimp = c.accumulated_pseudo_impulse - aold;

                    if (I_a > 0.0f) {
                        const float inv = 1.0f / I_a;
                        pwx[c.body_a] += aimp * ax_x * inv;
                        pwy[c.body_a] += aimp * ax_y * inv;
                        pwz[c.body_a] += aimp * ax_z * inv;
                    }
                    if (I_b > 0.0f) {
                        const float inv = 1.0f / I_b;
                        pwx[c.body_b] -= aimp * ax_x * inv;
                        pwy[c.body_b] -= aimp * ax_y * inv;
                        pwz[c.body_b] -= aimp * ax_z * inv;
                    }
                    continue;
                }
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
                // G-63 (2026-09-01, measured): bias_as_error is the
                // BETA-scaled correction, BETA * (pen - SLOP), so this gate
                // gives up once pen < SLOP * (1 + 1/BETA) = 3.5 mm - a
                // dead zone 3.5x INV-2's own 1 mm (the footprint and the
                // 38.6:1 anvil both stood at exactly 3.5-4.5 mm). The
                // tolerance the comment above means is on the ERROR:
                // stop when the remaining error is under SLOP, i.e. the
                // correction is under BETA * SLOP. Under the single law
                // the gate says what it meant; the default keeps its
                // arithmetic byte-identically.
                const float gate = (single_law && single_law_gate)
                                       ? PhysicsV4::BETA * PhysicsV4::SLOP
                                       : PhysicsV4::SLOP;
                if (bias_as_error < gate) continue;
                // Speculative approach limits stayed in the velocity solve;
                // they are not geometry to repair. Skipping them explicitly
                // rather than relying on the unilateral clamp to zero them.
                if (c.is_contact && c.bias < 0.0f) continue;
                if (c.body_a >= count || c.body_b >= count) continue;

                const float inv_ma = inv_mass_positional(particles[c.body_a]);
                // Turtle rows self-reference body_b as a sentinel: the
                // boundary has infinite positional mass, and reading b
                // through the sentinel would cancel a's own pseudo
                // velocity (G-50).
                const float inv_mb = c.is_turtle_contact
                    ? 0.0f : inv_mass_positional(particles[c.body_b]);
                if (inv_ma == 0.0f && inv_mb == 0.0f) continue;

                const float pv_rel = c.is_turtle_contact
                    ? (c.jx * pvx[c.body_a] + c.jy * pvy[c.body_a] +
                       c.jz * pvz[c.body_a])
                    : (c.jx * (pvx[c.body_a] - pvx[c.body_b]) +
                       c.jy * (pvy[c.body_a] - pvy[c.body_b]) +
                       c.jz * (pvz[c.body_a] - pvz[c.body_b]));

                // THE IMPULSE MUST BE PRICED IN THE MASSES IT IS SPENT ON.
                // c.effective_mass came from the VELOCITY mass model, where
                // sleep = infinite mass. This pass spends through the
                // POSITIONAL model above, where sleep weighs its real mass.
                // Pricing with one and spending with the other turned a
                // foot-vs-sleeping-blade row (pair effective mass 1.25 kg,
                // capture budget 6.2 N·s) into a 13.9 m position teleport
                // of a 1.6 g blade in ONE substep — measured on the
                // walk-through-grass canary, frame 934: displacement exactly
                // along -normal, velocity (0,0,0) before and after, detector
                // silent because nothing ever moved at velocity level.
                // Recomputed against the same inverse masses the correction
                // is applied with, that row moves the blade ~1 mm.
                const float eff_pos = c.eff_mass_share / (inv_ma + inv_mb);
                float imp = -(pv_rel - c.bias) * eff_pos;

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
                if (!c.is_turtle_contact) {
                    pvx[c.body_b] -= c.jx * imp * inv_mb;
                    pvy[c.body_b] -= c.jy * imp * inv_mb;
                    pvz[c.body_b] -= c.jz * imp * inv_mb;
                }
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

        // Spend the pseudo ANGULAR velocity on orientations, then discard.
        // Same law as the integrator: quat-driven particles rotate rotation_q
        // through the axis-angle exponential and republish their Euler triple;
        // Euler-owned bones take only the Z component (physics owns nothing
        // else of their Euler state — FK does).
        if (!pwx.empty()) {
            for (size_t i = 0; i < count; ++i) {
                if (pwx[i] == 0.0f && pwy[i] == 0.0f && pwz[i] == 0.0f) continue;
                Particle& p = particles[i];
                if (p.solver_mode == ParticleSolverMode::KINEMATIC) continue;
                if (p.is_quat_driven) {
                    const float wx = pwx[i], wy = pwy[i], wz = pwz[i];
                    const float wmag = std::sqrt(wx*wx + wy*wy + wz*wz);
                    if (wmag > 1e-9f) {
                        const float theta = wmag * dt;
                        const float inv = 1.0f / wmag;
                        logosphere::Quat dq = logosphere::Quat::from_axis_angle(
                            wx * inv, wy * inv, wz * inv, theta);
                        p.rotation_q = (dq * p.rotation_q).normalized();
                        p.rotation_q.to_euler_zyx(p.rotation_x, p.rotation_y, p.rotation_z);
                    }
                } else {
                    p.rotation_z += pwz[i] * dt;
                }
            }
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
                      << " iters=" << actual_iterations
                      << " maxdv_pair=P" << g_maxdv_body_a << "<->P" << g_maxdv_body_b
                      << " type=" << (g_maxdv_type==0?"gluon":g_maxdv_type==1?"contact":"turtle")
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
            // IMPULSE_MEMORY_DECAY / _REVERSED (anti-windup): INV-29
            // registry (WarmStartMemory group).
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
                const float d = (warm_old * velocity_part < 0.0f) ? IMPULSE_MEMORY_DECAY_REVERSED
                                                                  : IMPULSE_MEMORY_DECAY;
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
                    std::cout << "[TEAR f" << phys_frame << "] P" << gluon->particle_a
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

// INV-2's instrument (night 2026-09-04): every tracked body records, every
// frame, the solved contact ROWS it is part of - gap or overlap - with their
// accumulated impulses; a frame with no rows is recorded empty. Called from
// both exits of solve_contacts_v3.
void PhysicsSystem::record_row_telemetry(ParticleSystem::WriteView& particles,
                                         const std::vector<PhysicsV4::Constraint>& constraints,
                                         int phys_frame) {
    // ==========================================================================
    // PHYSICS TELEMETRY: the solved ROWS of every tracked particle (INV-2's
    // instrument). Until 2026-09-04 this copied collision_events_, which only
    // carries manifolds that penetrate: a cube landing in the speculative
    // cushion settled 75 um ABOVE its floor and recorded nothing for 300
    // frames (test_falling_cube read 'Contacts recorded (0)' while the canary
    // saw one manifold per frame from f142). A row is the contact the solver
    // priced, gap or overlap, and its accumulated impulse is the force it
    // carried; a seam question ('horizontal normals on a flat floor') is a
    // question about that force, which no event could answer.
    // Normal convention as before: from A toward B. A row's jacobian pushes A
    // along +j (off B), so the telemetry normal is -j.
    // ==========================================================================
    if (!telemetry_.is_enabled() || !telemetry_last_substep_) return;
    const size_t count = particles.size();
    {
        for (unsigned int pid : telemetry_.tracked_particles()) {
            if (pid >= count) continue;
            const Particle& p = particles[pid];
            Logosphere::ParticleFrameSnapshot snap;
            snap.frame_number = (uint32_t)telemetry_frame_;   // the update, not the substep
            (void)phys_frame;
            snap.x = p.x; snap.y = p.y; snap.z = p.z;
            snap.vx = p.vx; snap.vy = p.vy; snap.vz = p.vz;
            snap.width = p.width; snap.height = p.height; snap.thickness = p.thickness;
            snap.is_at_rest = p.is_at_rest;
            for (const Constraint& c : constraints) {
                if (!c.is_contact || c.is_angular) continue;
                if (c.body_a != pid && c.body_b != pid) continue;
                Logosphere::ContactSnapshot cs;
                cs.particle_a = (unsigned)c.body_a;
                cs.particle_b = (unsigned)c.body_b;
                cs.is_turtle = c.is_turtle_contact;
                cs.normal_x = -c.jx; cs.normal_y = -c.jy; cs.normal_z = -c.jz;
                cs.penetration = c.penetration;
                // A row carries no manifold point; its anchor lever arm (0
                // when none) is the nearest thing to one.
                const Particle& pa = particles[c.body_a];
                cs.contact_x = pa.x + c.anchor_rax;
                cs.contact_y = pa.y + c.anchor_ray;
                cs.contact_z = pa.z + c.anchor_raz;
                cs.is_corner_contact = false;
                cs.is_horizontal = (std::abs(cs.normal_x) > 0.1f || std::abs(cs.normal_y) > 0.1f);
                cs.normal_impulse = c.accumulated_impulse;
                cs.tangent_impulse = std::sqrt(c.friction_impulse_t1 * c.friction_impulse_t1 +
                                               c.friction_impulse_t2 * c.friction_impulse_t2);
                snap.contacts.push_back(cs);
                snap.total_contact_count++;
                if (cs.is_horizontal) {
                    snap.horizontal_contact_count++;
                    snap.horizontal_impulse += std::fabs(cs.normal_impulse);
                }
            }
            telemetry_.get_buffer(pid).record(std::move(snap));
        }
    }
}

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
            // MAX_VELOCITY: INV-29 registry. Derived square kept local.
            constexpr float MAX_VEL_SQ = MAX_VELOCITY * MAX_VELOCITY;
            float clamp_vel_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;
            if (clamp_vel_sq > MAX_VEL_SQ) {
                float scale = MAX_VELOCITY / std::sqrt(clamp_vel_sq);
                p.vx *= scale;
                p.vy *= scale;
                p.vz *= scale;
            }
            // A NaN/Inf velocity is a solver defect, and zeroing it silently
            // was how detonation-class bugs stayed invisible (inventory B4 —
            // the old comment here admitted it: "Root cause should still be
            // investigated"). Refuse loud, naming the body. PHYSICS_LENIENT
            // keeps the zero-reset so inventory sweeps can complete.
            if (!std::isfinite(p.vx) || !std::isfinite(p.vy) ||
                !std::isfinite(p.vz)) {
                std::fprintf(stderr,
                    "[PHYSICS REFUSED] P%zu velocity is not finite: "
                    "(%g, %g, %g) at pos (%g, %g, %g), mass %g. A NaN "
                    "velocity is a solver defect upstream of this line.\n",
                    i, p.vx, p.vy, p.vz, p.x, p.y, p.z, p.GetMass());
                if (!std::getenv("PHYSICS_LENIENT")) std::abort();
                if (!std::isfinite(p.vx)) p.vx = 0.0f;
                if (!std::isfinite(p.vy)) p.vy = 0.0f;
                if (!std::isfinite(p.vz)) p.vz = 0.0f;
            }
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

            if (speed_sq > MIN_DRAG_SPEED_SQ) {  // Only apply if moving (> 1cm/s)
                float speed = std::sqrt(speed_sq);

                // 1. QUADRATIC DRAG (air resistance) - applies to ALL particles
                float cross_section = p.width * p.height;
                // RHO_AIR, DRAG_CD: INV-29 registry (GravityAndDrag group).
                float drag_coeff = 0.5f * RHO_AIR * DRAG_CD * cross_section / p.GetMass();

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

                if (energy_ledger_on()) {
                    // The medium takes what the factor removes.
                    const double m = p.GetMass();
                    const double v2 = double(p.vx)*p.vx + double(p.vy)*p.vy
                                    + double(p.vz)*p.vz;
                    const double f2 = double(damping_factor) * damping_factor;
                    g_dissipation.drag += 0.5 * m * v2 * (1.0 - f2);
                }
                p.vx *= damping_factor;
                p.vy *= damping_factor;
                p.vz *= damping_factor;
            }
        }

        // ====================================================================
        // FRAME-GATED DAMPING: ERADICATED (2026-08-14, owner decree)
        // ====================================================================
        // A speed-gated *0.90/tick velocity tax lived here from 2025-12-11.
        // It was written to kill numerical oscillation and could not tell
        // oscillation from coasting, because it looked at SPEED — and
        // oscillation is a signature, not a speed. Measured consequence
        // (sleep-wake resolver ladder R1 + machine S3 RCA): any body below
        // 0.8 m/s with a saturated low_velocity_frames counter lost 10% of
        // its velocity per tick — 72 kg*m/s ground to 11 in 17 frames on a
        // mu=0.02 floor, with contacts ferrying neighbours' momentum into
        // the sink. The engine forbade slow coasting outright.
        //
        // Owner ruling (LEDGER.md 2026-08-14): "a dampening is a dampening
        // if it's a dampening; anything else is energy transference between
        // materials and consequences of that — physics." Rest belongs to
        // the sleep law (INV-18/24); dissipation belongs to modeled
        // processes (INV-19). Nothing else may touch velocity.
        //
        // The low_velocity_frames counter still ticks (wake sites reset
        // it); its only remaining reader is diagnostics.
        {
            float vel_sq_track = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;
            constexpr float DAMPING_RESET_THRESHOLD_SQ =
                DAMPING_VELOCITY_THRESHOLD_SQ * 4.0f;
            if (vel_sq_track < DAMPING_VELOCITY_THRESHOLD_SQ) {
                if (p.low_velocity_frames < 65535) p.low_velocity_frames++;
            } else if (vel_sq_track > DAMPING_RESET_THRESHOLD_SQ) {
                p.low_velocity_frames = 0;
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
    // THE BOUNDARY PAYS LIKE EVERYONE ELSE (G-50): under TURTLE_PRICED
    // the free clamp retires — support and repair go through the rows
    // and the split-impulse position pass, priced and booked. The
    // energy ledger measured this clamp injecting +44 J per substep
    // into a RESTING four-box column, the power supply of the G-48
    // stack pump; the door work measured it lifting tilted grass
    // blades free every substep.
    static const bool turtle_priced = std::getenv("TURTLE_PRICED") != nullptr;
    if (turtle_priced) return;
    const size_t count = particles.size();

    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];

        // Skip massless particles (lights float)
        if (p.GetMass() == 0.0f) continue;

        // Calculate particle bottom. Rotated boxes reach down by their
        // ORIENTED extent, not their raw thickness — a quarter-turned
        // plate's down-reach is half its height. Kept in lock-step with
        // the turtle contact build above; disagreeing extents would clamp
        // a body one pass and contact it at another height the next.
        // Same half-diagonal cost guard as the contact build: bodies that
        // cannot reach the turtle at any orientation skip the exact extent.
        float half_thickness = p.thickness * 0.5f;
        if (p.shape == ParticleShape::BOX && box_particle_is_rotated(p)) {
            const float hx = p.width * 0.5f, hy = p.height * 0.5f,
                        hz = p.thickness * 0.5f;
            const float reach_sq = hx * hx + hy * hy + hz * hz;
            const float clearance = p.z - (TURTLE_Z - SLOP);
            if (clearance <= 0.0f || clearance * clearance < reach_sq) {
                const AABB6 w = aabb_of_obb(obb_of_box_particle(p, p.z));
                half_thickness = (w.max_z - w.min_z) * 0.5f;
            }
        }
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
    // REST_VELOCITY_THRESHOLD, WAKE_VELOCITY_THRESHOLD,
    // REST_FRAMES_REQUIRED: INV-29 registry (RestSleepDamping group).

    const size_t count = particles.size();

    for (size_t i = 0; i < count; ++i) {
        Particle& p = particles[i];

        // Skip massless particles
        if (p.GetMass() == 0.0f) continue;

        float vel_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;

        // G-44: QUIETNESS IS ONE CURRENCY AND IT MUST NOT BE GROWING.
        // (1) A slow-COM fast-spin body is not quiet: price the angular
        //     half as extremity speed, omega times the body's half-
        //     extent, in the same m/s the threshold already speaks.
        //     (First appearance of the disease: sleeping spinners kept
        //     their spin. Second: R7's corner stand LOST its budding
        //     topple, confiscated at exactly REST_FRAMES_REQUIRED.)
        // (2) A body whose quietness GROWS is not settling, it is an
        //     instability passing through low speed on its way up (an
        //     inverted pendulum's launch). The counter accumulates only
        //     while quietness does not grow beyond jitter tolerance.
        //     No gravity term, no world-up: zero-g safe.
        float r_ext_sq;
        if (p.shape == ParticleShape::BOX) {
            const float hw = 0.5f * p.width, hh = 0.5f * p.height,
                        ht = 0.5f * p.thickness;
            r_ext_sq = hw * hw + hh * hh + ht * ht;
        } else {
            const float hr = 0.5f * p.size;
            r_ext_sq = hr * hr;
        }
        const float omega_sq = p.omega_x * p.omega_x +
                               p.omega_y * p.omega_y +
                               p.omega_z * p.omega_z;
        const float q_sq = vel_sq + omega_sq * r_ext_sq;
        const float prev_q_sq = p.rest_quiet_sq;
        p.rest_quiet_sq = q_sq;
        const bool grew_this_frame =
            q_sq > prev_q_sq * (REST_GROWTH_TOLERANCE * REST_GROWTH_TOLERANCE)
                 + REST_GROWTH_FLOOR * REST_GROWTH_FLOOR;
        // G-44 refined (test_tree_wiggly): a topple grows MONOTONICALLY,
        // solver jitter ALTERNATES. Only a sustained run of growing
        // frames is an instability doing its work; alternating residue
        // is exactly what the sleep cache exists to absorb.
        if (grew_this_frame) {
            if (p.quiet_growth_run < 255) p.quiet_growth_run++;
        } else {
            p.quiet_growth_run = 0;
        }
        const bool quiet_growing = p.quiet_growth_run >= REST_GROWTH_RUN;

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
        } else if (q_sq < REST_VELOCITY_THRESHOLD * REST_VELOCITY_THRESHOLD) {
            // Quietness below rest threshold AND not growing (G-44) -
            // accumulate rest frames; growth resets the count.
            if (quiet_growing) {
                p.frames_at_rest = 0;
            } else
            if (p.frames_at_rest < 255) p.frames_at_rest++;
            if (p.frames_at_rest >= REST_FRAMES_REQUIRED) {
                p.is_at_rest = true;
                // Zero velocity when entering rest to prevent drift.
                // BOTH halves: a body that kept its angular velocity went
                // on spinning while asleep forever — enter-rest zeroed
                // only the linear part, angular integration gates only on
                // KINEMATIC, and nothing downstream judged spin. Sleep is
                // a cache over dynamics (INV-18) and a cache that keeps
                // one field live is a leak.
                p.vx = p.vy = p.vz = 0.0f;
                p.omega_x = p.omega_y = p.omega_z = 0.0f;
            }
        } else if (q_sq > WAKE_VELOCITY_THRESHOLD * WAKE_VELOCITY_THRESHOLD) {
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
        // A sleeping body does not spin. The linear half of this has been
        // true since the sleep law existed; the angular half was simply
        // never written, so a sleeper kept whatever omega it fell asleep
        // with (INV-18: sleep hides nothing).
        if (p.is_at_rest) continue;

        // Get moment of inertia (calculate if not cached)
        //
        // Was `if (I <= 0) I = 0.01f` (inventory B3): an INVENTED inertia
        // integrating real torque on a body whose inertia is unknown. Split
        // honestly: a massless body carries no angular momentum — same door
        // predicate as inv_mass_momentum on the linear side — so it skips
        // angular integration entirely. A MASSIVE body with I <= 0 is
        // corrupted particle data (degenerate dimensions): refuse loud.
        float I = p.GetMomentOfInertia();
        if (I <= 0.0f) {
            if (p.GetMass() <= 0.0f) continue;   // massless: no angular dynamics
            std::fprintf(stderr,
                "[PHYSICS REFUSED] P%zu has mass %g but inertia %g — "
                "corrupted particle data (dimensions %g x %g x %g). No "
                "inertia will be invented for it.\n",
                i, p.GetMass(), I, p.width, p.height, p.thickness);
            if (!std::getenv("PHYSICS_LENIENT")) std::abort();
            continue;   // lenient: skip integration, never invent an inertia
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
        // MAX_OMEGA: INV-29 registry (GravityAndDrag group).
        if (p.omega_z > MAX_OMEGA) p.omega_z = MAX_OMEGA;
        if (p.omega_z < -MAX_OMEGA) p.omega_z = -MAX_OMEGA;

        // ANGULAR_DRAG is dead (owner order, G-42). Air resistance for
        // rotation is now DERIVED below, from the same ambient constants
        // the linear law uses, acting on the body's real extents. A
        // constant that stole 18.5% of every spin per frame was five
        // orders of magnitude off the derivation for a stone cube, and
        // it is why every spin experiment was dead air.

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
        p.torque_x = 0.0f;
        p.torque_y = 0.0f;

        // ================================================================
        // ROTATIONAL AIR DRAG, DERIVED (G-42; replaces ANGULAR_DRAG).
        // Quadratic pressure drag integrated over the box's side faces:
        // about body axis i, tau = -(RHO_AIR * DRAG_CD * L_i *
        // (L_j^4 + L_k^4) / 32) * w|w|. The 32 is the integral of |y|^3
        // across a face — a derived coefficient like the 12 in a box
        // inertia, not a tunable. Same ambient constants as the linear
        // law, so a declarable medium (D8) will feed both at once.
        // Spheres take their bounding extents: a sanctioned
        // over-estimate (INV-21); honest sphere spin drag is skin
        // friction and is boarded, not faked.
        // Applied in the BODY frame (rotation_q is the one truth) with
        // the unconditionally stable form w /= 1 + (c|w|/I) dt, which
        // can never overshoot or flip sign. For a 166 kg stone cube at
        // 4 rad/s this is ~0.1% per second — a flywheel, which is
        // physical. Spins die at contacts, not in thin air.
        // ================================================================
        {
            const float w2 = p.omega_x*p.omega_x + p.omega_y*p.omega_y
                           + p.omega_z*p.omega_z;
            if (w2 > 1e-10f) {
                const float I = p.GetMomentOfInertia();
                if (I > 0.0f) {
                    const float Lx = p.width, Ly = p.height, Lz = p.thickness;
                    const float k  = RHO_AIR * DRAG_CD / ROT_DRAG_FACE_INTEGRAL;
                    const float cx = k * Lx * (Ly*Ly*Ly*Ly + Lz*Lz*Lz*Lz);
                    const float cy = k * Ly * (Lx*Lx*Lx*Lx + Lz*Lz*Lz*Lz);
                    const float cz = k * Lz * (Lx*Lx*Lx*Lx + Ly*Ly*Ly*Ly);
                    // world -> body
                    logosphere::Quat qc = p.rotation_q.conjugate();
                    float m[9];
                    qc.to_matrix3x3(m);
                    float bx = m[0]*p.omega_x + m[1]*p.omega_y + m[2]*p.omega_z;
                    float by = m[3]*p.omega_x + m[4]*p.omega_y + m[5]*p.omega_z;
                    float bz = m[6]*p.omega_x + m[7]*p.omega_y + m[8]*p.omega_z;
                    bx /= 1.0f + (cx * std::fabs(bx) / I) * dt;
                    by /= 1.0f + (cy * std::fabs(by) / I) * dt;
                    bz /= 1.0f + (cz * std::fabs(bz) / I) * dt;
                    // body -> world (transpose)
                    p.omega_x = m[0]*bx + m[3]*by + m[6]*bz;
                    p.omega_y = m[1]*bx + m[4]*by + m[7]*bz;
                    p.omega_z = m[2]*bx + m[5]*by + m[8]*bz;
                }
            }
        }

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
        // Under the quat-truth lever every DYNAMIC body publishes, so a
        // body has one orientation whichever field a consumer reads
        // (G-23's red, the frozen twin). KINEMATIC stays with its
        // external writer. Default ON (owner ruling 2026-08-19);
        // LOGOSPHERE_QUAT_TRUTH=0 is the kill switch, the old gate.
        if (p.is_quat_driven ||
            (quat_truth_ && p.solver_mode == ParticleSolverMode::DYNAMIC)) {
            // + 0.0f canonicalizes IEEE negative zero: to_euler_zyx of the
            // identity emits -0.0 on Y and Z (each is a negated atan2 of
            // zero), numerically equal to +0.0 and bitwise different. The
            // publish must be a bitwise NO-OP for a body that never
            // rotated, or G-19's bit-identical baseline can never hold.
            float ex, ey, ez;
            p.rotation_q.to_euler_zyx(ex, ey, ez);
            p.rotation_x = ex + 0.0f;
            p.rotation_y = ey + 0.0f;
            p.rotation_z = ez + 0.0f;
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
    // THE REGISTRY IS NOT APPLIED (inventory B13). apply_registry_forces is
    // an empty stub: the V4 solver hardcodes gravity (get_solver_gravity)
    // and never integrates registered forces. Callers registering a force
    // and expecting it to act were silently wrong. State the contract loud,
    // once: the registry is direction/query DATA (get_gravity_vector), not
    // a force input. Full cure (implement or delete) is an owner decision —
    // see the inventory.
    static bool warned = false;
    if (!warned) {
        warned = true;
        std::fprintf(stderr,
            "[PHYSICS CONTRACT] add_force: the V4 solver does NOT apply "
            "registry forces (apply_registry_forces is a stub; gravity is "
            "PhysicsV4::GRAVITY). Registered forces are query data only "
            "(get_gravity_vector).\n");
    }
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

// ============================================================================
// PHASE C: THE FORCE LAW IS DERIVED, NOT DECLARED (axial + damping)
// ============================================================================
// k = A / (L_a/E_a + L_b/E_b): a bond is two material spans in series
// between its attachment points, each span the offset that reaches the
// joint from its body's centre. c = eta * sqrt(k * mu): the loss factor
// working against the reduced mass the bond actually drives. Every input
// already exists: contact_area is geometry the generators compute, E and
// eta live in the materials table, masses are on the particles.
//
// This exists because of G6: four creators declared three different
// stiffnesses and a fourth declared none, and the undeclared one stood a
// canopy on nothing. A derived law cannot be forgotten, and deleting the
// declarations (C-116) means there is no second opinion to disagree with.
static void derive_organic_force_law(OrganicGluon& g,
                                     const Particle& pa, const Particle& pb) {
    const float A  = g.contact_area;
    const float Ea = Materials::GetYoungsModulus(pa.material_type);
    const float Eb = Materials::GetYoungsModulus(pb.material_type);
    if (!(A > 0.0f) || !(Ea > 0.0f) || !(Eb > 0.0f)) return;  // guard refuses
    const float La = std::sqrt(g.offset_a.x * g.offset_a.x +
                               g.offset_a.y * g.offset_a.y +
                               g.offset_a.z * g.offset_a.z);
    const float Lb = std::sqrt(g.offset_b.x * g.offset_b.x +
                               g.offset_b.y * g.offset_b.y +
                               g.offset_b.z * g.offset_b.z);
    // Two coincident attachment points still join real material: floor the
    // total span at 1 cm, split evenly, so k stays finite and material-true.
    float la = La, lb = Lb;
    const float total = la + lb;
    if (total < 0.01f) { const float pad = (0.01f - total) * 0.5f; la += pad; lb += pad; }
    g.stiffness = A / (la / Ea + lb / Eb);
    const float ma = pa.GetMass(), mb = pb.GetMass();
    const float mu = (ma > 0.0f && mb > 0.0f) ? (ma * mb) / (ma + mb)
                                              : std::fmax(ma, mb);
    const float eta = 0.5f * (Materials::GetLossFactor(pa.material_type) +
                              Materials::GetLossFactor(pb.material_type));
    g.damping = eta * std::sqrt(g.stiffness * std::fmax(mu, 1e-6f));

    // Bending (Phase C angular): the same material spans resisting rotation.
    // Second moment of a round section from the area the bond already
    // carries, I = A^2/(4*pi), so K = I / (L_a/E_a + L_b/E_b) in N*m/rad.
    // A grass blade derives ~8e-4 (bends underfoot instead of tearing), an
    // oak branch ~6e5 (holds its canopy). Angular damping is the loss
    // factor against the pair's reduced moment of inertia. Joint semantics
    // (enable_angular_constraint, rotation limits) stay declared: they are
    // policy, not material.
    const float I_sec = (A * A) / (4.0f * (float)M_PI);
    g.angular_stiffness = I_sec / (la / Ea + lb / Eb);
    const float Ia = pa.GetMomentOfInertia(), Ib = pb.GetMomentOfInertia();
    const float mu_rot = (Ia > 0.0f && Ib > 0.0f) ? (Ia * Ib) / (Ia + Ib)
                                                  : std::fmax(Ia, Ib);
    g.angular_damping =
        eta * std::sqrt(g.angular_stiffness * std::fmax(mu_rot, 1e-12f));
}

// ============================================================================
// REFUSE-TO-BUILD: a bond's force law must exist before the bond does
// ============================================================================
// G6: stiffness/damping had no initializer and the tree generator's branch
// gluon never set them. Under force-bounded budgeting the per-substep
// impulse budget is (k*err + d*vrel)*dt, so an unset k is not a default,
// it is a bond that exerts ZERO force at any error: measured 3 m of
// stretch, 80 kN of breaking budget available, allowed impulse [-0,0],
// an entire canopy standing on nothing.
//
// The law (C-121): missing required data crashes loud at the door.
// Zero damping is legal (bonds may ring); zero stiffness never is.
// GLUON_LENIENT=1 downgrades to a printed violation for inventory sweeps.
//
// EXTENDED 2026-08-13 (fallback destruction, inventory A1/A2/A5):
//  - The angular law joins the linear one. GluonConstraintBase used to
//    default angular_stiffness/damping to 100/10 — numbers nobody chose,
//    silently inherited by every bond whose creator forgot (the red
//    test_humanoid_tuning_coverage finding). The defaults are now the
//    UNDECLARED sentinel (0/0); a force-bounded bond that consumes its
//    angular law (angular constraint enabled) must have one, derived by
//    Phase C or declared.
//  - EVERY bond must have a positive breaking force. calculate_breaking_
//    force() <= 0 is the G6 hollow-bond disease through the other factor:
//    for organics the budget is min(breaking, spring force), so breaking 0
//    is a zero-force bond however stiff; for welds it is an indeterminate
//    contract (NailGluon::breaking_force was previously uninitialized).
static void refuse_undeclared_bond(const GluonConstraintBase& g,
                                   const Particle& pa, const Particle& pb,
                                   size_t a, size_t b) {
    const float breaking = g.calculate_breaking_force(pa, pb);
    if (!(breaking > 0.0f)) {
        std::fprintf(stderr,
            "[GLUON REFUSED] bond P%zu<->P%zu born with breaking force %g "
            "(must be > 0). A weld: declare breaking_force at the creation "
            "site. An organic: its breaking is contact_area * avg material "
            "strength — contact_area=%s, strength_a=%g strength_b=%g; declare "
            "the missing one.\n",
            a, b, breaking,
            g.force_bounded() ? "(see organic creator)" : "n/a",
            pa.material_strength, pb.material_strength);
        if (!std::getenv("GLUON_LENIENT")) std::abort();
    }
    if (!g.force_bounded()) return;   // force law unused on rigid welds
    const bool bad_k = !(g.stiffness > 0.0f);
    const bool bad_d = g.damping < 0.0f;
    const bool bad_ak = g.enable_angular_constraint && !(g.angular_stiffness > 0.0f);
    const bool bad_ad = g.enable_angular_constraint && g.angular_damping < 0.0f;
    if (!bad_k && !bad_d && !bad_ak && !bad_ad) return;
    std::fprintf(stderr,
        "[GLUON REFUSED] force-bounded bond P%zu<->P%zu born without a force "
        "law: stiffness=%g damping=%g angular_stiffness=%g angular_damping=%g "
        "(angular constraint %s). k<=0 means zero force at any error (ledger "
        "G6); an enabled angular constraint with K<=0 is a joint with no "
        "torque behind it. Declare it or provide the materials Phase C "
        "derives from (contact_area, Young's modulus, loss factor).\n",
        a, b, g.stiffness, g.damping, g.angular_stiffness, g.angular_damping,
        g.enable_angular_constraint ? "enabled" : "disabled");
    if (!std::getenv("GLUON_LENIENT")) std::abort();
}

size_t PhysicsSystem::add_particle_with_gluon_to(
    size_t particle_a_id,
    const Particle& particle_b_config,
    std::unique_ptr<GluonConstraintBase> gluon,
    bool use_config_position)
{
    if (!is_initialized_ || !gluon) {
        // A requested bond must not silently vanish (inventory B12): the
        // caller's world model now differs from the engine's. PHYSICS_LENIENT
        // keeps the old error-code return for inventory sweeps.
        std::fprintf(stderr,
            "[PHYSICS REFUSED] add_particle_with_gluon_to(P%zu, ...): %s — the "
            "bond was NOT created. Initialize physics before creating bonds / "
            "pass a non-null gluon.\n",
            particle_a_id, !is_initialized_ ? "physics not initialized"
                                            : "null gluon");
        if (!std::getenv("PHYSICS_LENIENT")) std::abort();
        return static_cast<size_t>(-1);
    }

    // A CHILD OF A BODY THAT WAS NEVER BORN IS NOT PLACED AT THE ORIGIN.
    // The placement law is pb = pa + offset_a - offset_b, and
    // get_particle_copy answers Particle{} for an index it does not have —
    // so a part whose parent the creation door refused used to be built at
    // (0,0,0) and then reported by the TURTLE door, which named the wrong
    // defect and aborted the world one generation downstream of the real
    // one. Refuse here, name the parent, and let the generator's own
    // `id < 0` guards unwind the limb.
    if (particle_a_id >= particle_system_->count()) {
        std::fprintf(stderr,
            "[PHYSICS REFUSED] add_particle_with_gluon_to(P%zu, ...): there is "
            "no such body to bond to (%zu live) — its own creation was refused "
            "or removed. The part is NOT created; fix the parent's placement.\n",
            particle_a_id, particle_system_->count());
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
    const int added_b = particle_system_->add_particle(pb);

    // THE CREATION DOOR may have refused it (INV-37): the placement law
    // pb = pa + offset_a - offset_b puts a bonded part wherever the offsets
    // say, and "the generator drew it that way" is not a licence to be born
    // inside something. A refused body gets NO BOND — a gluon whose B is
    // (size_t)-1 would index past the array on its first solve, and a bond to
    // a body that does not exist is a lie about the structure.
    if (added_b < 0) {
        std::fprintf(stderr,
            "[PHYSICS REFUSED] add_particle_with_gluon_to(P%zu, ...): the "
            "creation door refused body B (INV-37, see the line above) — the "
            "bond was NOT created. Fix the part's placement at its generator.\n",
            particle_a_id);
        return static_cast<size_t>(-1);
    }
    size_t particle_b_id = static_cast<size_t>(added_b);

    // Setup gluon
    gluon->particle_a = particle_a_id;
    gluon->particle_b = particle_b_id;
    gluon->id = next_gluon_id_++;

    {
        const Particle sa = particle_system_->get_particle_copy(particle_a_id);
        const Particle sb = particle_system_->get_particle_copy(particle_b_id);
        if (auto* og = dynamic_cast<OrganicGluon*>(gluon.get())) {
            derive_organic_force_law(*og, sa, sb);
        }
        refuse_undeclared_bond(*gluon, sa, sb, particle_a_id, particle_b_id);
    }

    if (!gluon->force_bounded()) {
        // V4.4 Mass-scaled damping: heavier connections = more damping.
        // Derived organic damping is already mass-true (c = eta*sqrt(k*mu));
        // scaling it again would double-count the masses.
        gluon->damping *= std::sqrt(pa.GetMass() * pb.GetMass());
    }

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
        // Same door as add_particle_with_gluon_to (inventory B12): a
        // requested bond must not silently vanish.
        std::fprintf(stderr,
            "[PHYSICS REFUSED] add_gluon_between(P%zu, P%zu): %s — the bond "
            "was NOT created. Initialize physics before creating bonds / pass "
            "a non-null gluon.\n",
            particle_a_id, particle_b_id,
            !is_initialized_ ? "physics not initialized" : "null gluon");
        if (!std::getenv("PHYSICS_LENIENT")) std::abort();
        return;
    }

    // Setup gluon between existing particles
    // A BOND NAMES TWO BODIES (night 2026-09-04, journal 19). A refused
    // birth returns -1; as size_t that is a stranger the solver skips forever,
    // and only a second such bond used to trip INV-22's 'second live bond'
    // line - the wrong refusal. The range is the PROMISE range (live +
    // pending), because generators bond by predicted indices before the
    // flush. Refused like the door refuses: named, counted, not created.
    {
        const size_t promised = particle_system_->promised_count();
        if (particle_a_id >= promised || particle_b_id >= promised) {
            std::fprintf(stderr,
                "[PHYSICS REFUSED] bond P%zu<->P%zu: an endpoint lies beyond the "
                "promise range (live + pending = %zu) - a refused or unborn body "
                "cannot be bonded. The bond was NOT created.\n",
                particle_a_id, particle_b_id, promised);
            ++bonds_refused_;
            return;
        }
    }
    gluon->particle_a = particle_a_id;
    gluon->particle_b = particle_b_id;
    gluon->id = next_gluon_id_++;

    Particle pa = particle_system_->get_particle_copy(particle_a_id);
    Particle pb = particle_system_->get_particle_copy(particle_b_id);
    if (auto* og = dynamic_cast<OrganicGluon*>(gluon.get())) {
        derive_organic_force_law(*og, pa, pb);
    }
    refuse_undeclared_bond(*gluon, pa, pb, particle_a_id, particle_b_id);

    if (!gluon->force_bounded()) {
        // V4.4 Mass-scaled damping: heavier connections = more damping.
        // Derived organic damping is already mass-true (c = eta*sqrt(k*mu)).
        gluon->damping *= std::sqrt(pa.GetMass() * pb.GetMass());
    }

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
    // TWO LIVE BONDS FOR ONE PAIR (inventory B10, INV-22): the deque solves
    // BOTH (double dose) while queries see only the newer one. This used to
    // be a cout WARNING scrolling past. Refuse loud; GLUON_LENIENT keeps the
    // overwrite for inventory sweeps. Known suspect flow: pin-gluon respawn
    // indexing the replacement while the old bond awaits deferred removal.
    auto existing = gluon_pair_index_.find(key);
    if (existing != gluon_pair_index_.end()) {
        std::fprintf(stderr,
            "[GLUON REFUSED] second live bond for pair P%zu<->P%zu "
            "(old offset_a.z=%g, new offset_a.z=%g). One mechanism owns a "
            "pair (INV-22): remove the old bond before creating its "
            "replacement, or the pair is double-solved.\n",
            gluon->particle_a, gluon->particle_b,
            existing->second->offset_a.z, gluon->offset_a.z);
        if (!std::getenv("GLUON_LENIENT")) std::abort();
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
        // Repair-and-continue on corrupted bookkeeping was a silent fallback
        // (inventory B11): this function's own header says finding anything
        // is a bug in swap/removal. The diagnostics above stay; the run does
        // not, unless GLUON_LENIENT for inventory sweeps.
        if (!std::getenv("GLUON_LENIENT")) std::abort();
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
//
// WAKE_PROPAGATION_THRESHOLD and WAKE_ENERGY_DAMPING live in the INV-29
// registry (schema/physics.yaml -> generated/physics_constants.h).
// ============================================================================

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

