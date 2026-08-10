#ifndef PHYSICS_SYSTEM_H
#define PHYSICS_SYSTEM_H

// ============================================================================
// ⚠️  WARNING: IMPLEMENTATION IS IN physics_system_v4.cpp, NOT physics_system.cpp
// ============================================================================
// This header defines the PhysicsSystem class interface.
// The ACTUAL implementation is in physics_system_v4.cpp (Sequential Impulse solver).
// physics_system.cpp exists but is NOT COMPILED (dead code, do not use).
//
// When adding/modifying physics features:
//   1. Add declarations here (physics_system.h)
//   2. Add implementations in physics_system_v4.cpp
//   3. NEVER modify physics_system.cpp
// ============================================================================

#include "particle.h"
#include "core/particle_system.h"
#include "core/force.h"
#include "math_types.h"
#include "math/quat.h"                           // logosphere::Quat for 3-axis gluon targets
#include "logosphere/physics/bvh.h"  // For AABB struct (Swept AABB collision detection)
#include "logosphere/physics/physics_solver.h"  // For ContactKey, ContactKeyHash (warm starting)
#include "core/entity_telemetry.h" // Physics-level telemetry (per-particle contacts)
#include "logosphere/interaction/particle_interaction_system.h"  // broad-phase filter policy
#include <memory>
#include <vector>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <cmath>

namespace logosphere { class EventBus; }

// Forward declarations
class Engine;

// ============================================================================
// PHYSICS v2 - Clean Physics System
// ============================================================================
// See docs/PHYSICS_v2.md for design philosophy and implementation plan
//
// MAXIMA DIRECTIVA: Newtonian classical physics only (F=ma, v=at, x=½at²)
//
// This is a MINIMAL SHELL for rebuilding physics from first principles.
// All old physics code has been deleted. We start with test-driven development.

// Physics configuration
struct PhysicsConfig {
    bool enable_collision = true;
    bool debug_collision = false;
    float restitution = 0.5f;  // Coefficient of restitution (0=inelastic, 1=elastic)
};

// Collision info between two particles (internal use)
struct CollisionInfo {
    size_t particle1_idx;
    size_t particle2_idx;
    Vec3 normal;           // Collision normal (from particle1 toward particle2)
    Vec3 contact_point;    // Contact point (world space)
};

// Collision event for game systems (combat detection, step climbing, etc.)
// Populated each frame during physics update
struct CollisionEvent {
    size_t particle_a;     // First particle index
    size_t particle_b;     // Second particle index
    float penetration;     // Overlap depth
    // Collision geometry (added for step climbing, combat knockback, etc.)
    // Unit normal from A toward B. Oriented at the emission site: the
    // solver's manifold normal points the other way, and consumers were
    // written against this contract. Guarded by test_knockback_scene.
    float normal_x, normal_y, normal_z;
    float contact_x, contact_y, contact_z; // World-space contact point
    // Dynamics system info
    // Approach speed along the reported normal (m/s). NEGATIVE while
    // approaching, positive while separating. Computed with the oriented
    // normal, so this sign is the documented one.
    float relative_velocity;
    // TODO(#36 D4): these two are a STOPGAP rename, not the fix. The old
    // names said "dynamics" while the value written is
    // `solver_mode == ParticleSolverMode::KINEMATIC`
    // (physics_system_v4.cpp, the push_back in the contact recording
    // block). The honest fix is to carry ParticleSolverMode itself and
    // drop the flattened bools; deferred so the rename could land with
    // zero readers rather than after a consumer bakes the lie in.
    bool a_is_kinematic;      // particle_a's solver_mode == KINEMATIC
    bool b_is_kinematic;      // particle_b's solver_mode == KINEMATIC
    bool is_corner_contact;   // V4.12 corner detection (center-to-center normal, not axis-aligned)
};

// ImpactEvent was declared here for years, never pushed to, never read:
// three references in the whole tree, all declarations (#36). Deleted.
// Impact speed lives on CollisionEvent.relative_velocity, which is
// computed, sign-correct, and consumed.

// REMOVED: ParticleConstraint struct - old spring constraint system was never used
// Only gluon_v2 constraints are active in the codebase

// ============================================================================
// GLUON SYSTEM v2 - Polymorphic Force-Based Constraints
// ============================================================================
// See docs/ARCHITECTURE.md "GLUON SYSTEM v2" for design philosophy
//
// Core principle: Each gluon type knows how to calculate its own breaking force.
// No switch statements - polymorphism handles type-specific behavior.

// Base class for all gluon constraints (abstract interface)
class GluonConstraintBase {
public:
    size_t particle_a;          // First particle index
    size_t particle_b;          // Second particle index

    Vec3 offset_a;              // Attachment point on A (local coordinates)
    Vec3 offset_b;              // Attachment point on B (local coordinates)

    float target_distance;      // Usually 0.0 for rigid constraints
    float compliance = 0.0f;    // Inverse stiffness for solver. 0 = rigid (structural).
                                // >0 = soft (animation-driven, solver can violate to satisfy contacts).
                                // Set by animation system per frame for driven gluons.

    size_t id;                  // For deferred removal

    // Physical properties (polymorphic per gluon type)
    float stiffness;            // N/m (how rigid: nail=100000, elastic=10000)
    float damping;              // Ns/m (energy absorption: nail=0, elastic=1000)
    float last_force_magnitude; // Stored for breaking check after convergence

    // OFFSET ROTATION CONTROL:
    // - true (default): Offsets rotate with particle's rotation_z. All skeleton gluons
    //   use body-local coordinates, so attachment points rotate with the parent particle.
    //   This is required for angular physics to work correctly.
    // - false: Offsets are in world-space. Only use for special cases where you want
    //   the attachment point fixed in world coordinates regardless of particle rotation.
    bool rotate_offsets = true;

    // ANGULAR CONSTRAINT PROPERTIES:
    // Couples rotation_z between connected particles using spring-damper model.
    // This enables rotation to propagate through gluon chains like real joints.
    //
    // Example: Head rotates → torque propagates → neck follows → torso follows
    //
    // Different joints need different stiffness:
    //   - Neck: 50 N·m/rad (loose - head turns independently)
    //   - Knee: 500 N·m/rad (rigid - knee doesn't twist)
    //   - Hair: 0 (disable - hair swings freely via position constraint only)
    float angular_stiffness = 100.0f;       // N·m/rad - torque per radian of rotation difference
    float angular_damping = 10.0f;          // N·m·s/rad - damping of relative angular velocity
    float target_relative_rotation = 0.0f;  // Desired rotation difference (radians, usually 0)
    bool enable_angular_constraint = true;  // true=skeleton (rotation couples), false=hair (swings free)

    // ACTIVE ROTATION DRIVE (physics-driven skeleton, Option C).
    // When true, the Jacobian angular constraint always pulls toward
    // a commanded target rotation, regardless of the limit dead-zone.
    //
    // Two target modes:
    //   - Scalar (default): uses target_relative_rotation (Z-only).
    //     Phase 1/2 of the physics-drive refactor run on this path.
    //   - Quaternion: uses target_relative_q (full 3D). The solver
    //     decomposes the error into an axis-angle Rodrigues vector
    //     and applies impulses on all three world axes. Activated by
    //     setting use_quat_target=true alongside a non-identity
    //     target_relative_q. Stage 2+ of the rotational-DOF upgrade.
    bool angular_drive_enabled = false;

    // 3-axis quaternion target. Represents the desired rotation of
    // particle_b relative to particle_a. Identity = child matches
    // parent. Only read when use_quat_target is true; otherwise the
    // solver falls back to target_relative_rotation.
    logosphere::Quat target_relative_q = logosphere::Quat::identity();
    bool use_quat_target = false;

    // ANGULAR LIMIT (dead zone):
    // Joint can rotate freely within ±max_relative_rotation.
    // Beyond this limit, strong restoring torque kicks in.
    // This creates natural joint limits like real anatomy:
    //   - Neck: ±45° (π/4) - head turns independently until limit
    //   - Spine: ±20° - torso follows only when head hits neck limit
    //   - Hips: ±10° - hips rotate only when whole spine is at limit
    // Default M_PI = no limit (180° = effectively unlimited)
    float max_relative_rotation = 3.14159f;  // radians (default: unlimited)

    virtual ~GluonConstraintBase() = default;

    // Each gluon type calculates its own breaking force (polymorphic)
    virtual float calculate_breaking_force(const Particle& p_a, const Particle& p_b) const = 0;

    // IMPULSE MEMORY (the integral term). A bond under SUSTAINED load must
    // not re-earn its force from the error every frame: the bias term is a
    // proportional controller, so under a 1 m/s drag a joint equilibrates
    // at drag/BETA ~= 0.35 m of standing error no matter the stiffness
    // (measured: linear stiffness x10 changed the sweep gap by zero).
    // Last frame's per-axis impulse, applied at warm start, stored back
    // with decay; capped at breaking force * dt. A/B'd on the sweep.
    float warm_ix = 0.0f, warm_iy = 0.0f, warm_iz = 0.0f;

    // PLASTIC YIELD (rung 4, owner: 'a bent grass would stay bent if
    // damage is done'). Finite = plastic: bent past this angle, the bond's
    // rest orientation CREEPS to keep the elastic error at the yield —
    // deformation beyond yield is absorbed into the rest pose and the bond
    // remembers its damage. Infinity = purely elastic.
    float plastic_yield_angle = std::numeric_limits<float>::infinity();

    // FORCE-BOUNDED bonds (rung 3). A row's impulse budget per substep is
    // the force the bond's spring could actually exert at its current
    // stretch, (stiffness * |error| + damping * |v_rel|) * dt, capped by
    // breaking. Without this, 'stiffness' was decorative: every bond was an
    // infinitely strong constraint with a 4 m/s rate limit, so a walking
    // push could never bend a blade (the bond restored as fast as the
    // capped bias allowed, whatever force that took). Nails and rigid
    // welds stay unbounded: that is their semantic.
    virtual bool force_bounded() const { return false; }

    // FATIGUE (grass-natures red): a single-substep force spike must not
    // tear fiber — grass declares damping whose transient at a 1 m/s brush
    // (~100 N) exceeds its own strength (~58 N), so every first touch
    // ripped every bond (all four natures at f113, detonation downstream).
    // Force-based tearing now requires the load to be SUSTAINED; strain
    // tearing stays instant because stretched IS torn.
    uint16_t force_over_frames = 0;

    // Tear-at-elongation limit, as a ratio of rest length. Infinity = never
    // tears by strain (rigid joints, nails). Needed because force-based
    // breaking cannot fire on light bodies: a 2-gram blade segment's rows
    // can only ever accumulate ~2 N through its own inertia (measured,
    // single-blade study), so a 60 N tear threshold is unreachable no
    // matter how far the bond is dragged. Fibers tear by STRAIN.
    virtual float max_strain_ratio() const {
        return std::numeric_limits<float>::infinity();
    }

    // ========================================================================
    // FK ANIMATION QUERY INTERFACE
    // ========================================================================
    // Gluons are the single source of truth for joint properties.
    // Both Physics (force-based) and Dynamics/Animation (kinematic) query these.
    //
    // Animation workflow:
    //   1. Query gluon for segment length (target_distance)
    //   2. Query gluon for rotation limits (max_relative_rotation)
    //   3. Animation sets rotation angle
    //   4. FK computes child positions using gluon-defined segment lengths
    //
    // See: docs/dynamics_physics_integration.md - "Gluons as Source of Truth"
    // ========================================================================

    // Get segment length (rest distance between particle centers)
    // This is the "bone length" for FK animation
    // Computed from attachment offsets: |offset_a - offset_b|
    float get_segment_length() const {
        float dx = offset_a.x - offset_b.x;
        float dy = offset_a.y - offset_b.y;
        float dz = offset_a.z - offset_b.z;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }

    // Get maximum rotation from rest angle (range of motion limit)
    // Animation should clamp rotations to ±this value
    float get_max_rotation() const { return max_relative_rotation; }

    // Get rest angle (neutral joint position)
    // Animation offsets are relative to this angle
    float get_rest_angle() const { return target_relative_rotation; }

    // Get angular stiffness (resistance to rotation)
    // Higher = stiffer joint (knee), Lower = loose joint (neck)
    float get_angular_stiffness() const { return angular_stiffness; }

    // Check if angular constraints are enabled for this gluon
    // false = free-swinging (hair), true = coupled rotation (skeleton)
    bool is_angular_enabled() const { return enable_angular_constraint; }
};

// Nail: Rigid constraint with fixed breaking threshold
class NailGluon : public GluonConstraintBase {
public:
    float breaking_force;       // Fixed threshold (e.g., 5000 N)

    float calculate_breaking_force(const Particle& p_a, const Particle& p_b) const override {
        return breaking_force;
    }
};

// Organic: Breaking force from contact area × material strength (trees, bones)
class OrganicGluon : public GluonConstraintBase {
public:
    float contact_area;         // m² (larger particles = larger area)

    float calculate_breaking_force(const Particle& p_a, const Particle& p_b) const override {
        // Average strength of both particles (oak vs pine, etc.)
        float strength = (p_a.material_strength + p_b.material_strength) * 0.5f;
        return contact_area * strength;
    }

    bool force_bounded() const override { return true; }

    // Tear-at-elongation, per bond. Default: plant fiber tears at roughly
    // double its rest length — generous vs real fiber's few percent,
    // deliberately, so bends and brushing survive. PLASTIC bonds declare
    // higher: yielding absorbs what tearing would otherwise take (rung 4:
    // the BENT totem tore during the finger fold at the hardcoded 2x).
    float max_strain = 2.0f;
    float max_strain_ratio() const override { return max_strain; }
};

// Elastic: Actual spring behavior (future - knees, tendons)
class ElasticGluon : public GluonConstraintBase {
public:
    float breaking_force;       // Based on max strain
    float stiffness;
    float max_strain;

    float calculate_breaking_force(const Particle& p_a, const Particle& p_b) const override {
        return breaking_force;
    }
};

// OLD STRUCTURE REMOVED - Migrated to polymorphic GluonConstraintBase (see above)
// Use add_particle_with_gluon_to() for new code


class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    // Initialize with direct subsystem ref. Decouples physics from Engine
    // so it can run in a headless test harness without GLFW / Metal.
    bool initialize(ParticleSystem& particle_system, const PhysicsConfig& config = PhysicsConfig());

    // Engine-aware overload kept as a thin shim for callers that already
    // hold an Engine reference. Forwards to the ParticleSystem& variant.
    bool initialize(Engine* engine, const PhysicsConfig& config = PhysicsConfig());

    void shutdown();

    // Main physics update - called at fixed 60 Hz by TimeSystem
    // dt is ALWAYS 0.0167s (fixed timestep)
    void update(double delta_time, int input_target_id = -1);

    // Constraint loading stub (called by Engine after entities created)
    void load_constraints_from_kg();

    // Force registry management
    // Add a force to the system (takes ownership)
    void add_force(std::unique_ptr<Force> force);

    // Clear all forces (e.g., zero-gravity level)
    void clear_forces();

    // Clear all gluon constraints (for test reset)
    // Also clears warm start cache and pair index to prevent stale values when particle IDs are reused
    void clear_gluons() {
        gluon_constraints_v2_.clear();
        gluon_pair_index_.clear();
        cached_impulses_.clear();
    }

    // Total gluon count (useful for worldgen cross-checks and instrumentation)
    size_t get_total_gluon_count() const { return gluon_constraints_v2_.size(); }

    // Centralized gluon validation - removes gluons with stale particle indices
    // Called once at physics update start. Logs when pruning happens for diagnostics.
    void prune_invalid_gluons(size_t particle_count);

    // Particle deletion support - called by ParticleSystem during particle removal
    // Removes all gluons that reference this particle (must be called BEFORE swap-and-pop)
    void remove_gluons_for_particle(size_t particle_id);
    // Updates gluon indices after swap-and-pop (must be called AFTER swap)
    void notify_particle_swap(size_t old_index, size_t new_index);

    // Wake a sleeping particle (and propagate through gluon connections)
    // Call this when applying external force to a particle
    void wake_particle(size_t particle_id);

    // Get force by index (for runtime modification, e.g., change gravity)
    Force* get_force(size_t index);
    size_t get_force_count() const { return forces_.size(); }

    // Get gravity vector (searches for GravityForce in force registry)
    // Returns (0,0,0) if no gravity force is registered.
    // NOTE: the registry is NOT what the solver integrates — see
    // get_solver_gravity() for the gravity particles actually feel.
    Vec3 get_gravity_vector() const;

    // The gravity the solver actually applies each substep
    // (apply_all_forces hardcodes PhysicsV4::GRAVITY along -z; the
    // force registry is vestigial — apply_registry_forces is an empty
    // stub). Consumers that must oppose or scale real gravity
    // (buoyancy, flight) read this, not the registry.
    Vec3 get_solver_gravity() const { return Vec3{0.0f, 0.0f, -PhysicsV4::GRAVITY}; }

    // Get collision events from this frame (for game systems like combat)
    const std::vector<CollisionEvent>& get_collision_events() const { return collision_events_; }

    // Particle interaction model: nullable policy consulted at contact
    // broad phase (null = no filtering, today's behavior). The solver
    // only ever READS the policy; filtered overlaps are recorded below
    // and consumed outside the solver (episode events, volume forces).
    // See the particle-interaction design notes.
    void set_interaction_system(const logosphere::interaction::ParticleInteractionSystem* sys) {
        interaction_ = sys;
    }
    // Broad-phase overlaps skipped by profile filtering this frame
    // (cleared at solve entry; may contain per-substep duplicates —
    // consumers dedup).
    const std::vector<logosphere::interaction::FilteredOverlap>& get_filtered_overlaps() const {
        return filtered_overlaps_;
    }

    // Physics telemetry: per-particle contact recording for ANY particle
    Logosphere::PhysicsTelemetry& get_telemetry() { return telemetry_; }
    const Logosphere::PhysicsTelemetry& get_telemetry() const { return telemetry_; }

    // REMOVED: Old spring constraint methods - never used
    // Only gluon_v2 system is active

    // ========================================================================
    // GLUON v2 - Polymorphic Primitive API
    // ========================================================================
    // Create particle with gluon attachment (positions particle correctly)
    // Returns particle B's index
    //
    // Example - Nail:
    //   auto nail = std::make_unique<NailGluon>();
    //   nail->offset_a = Vec3(0, 0, -0.6f);    // Bottom of beam
    //   nail->offset_b = Vec3(0, 0, 0.1f);     // Top of hanging particle
    //   nail->breaking_force = 5000.0f;
    //   nail->target_distance = 0.0f;
    //   size_t hanging_id = physics->add_particle_with_gluon_to(beam_id, hanging_particle, std::move(nail));
    //
    // `use_config_position`: keep particle_b_config's OWN x/y/z instead of
    // deriving a position from the gluon offsets.
    //
    // The default (false) places B so the two anchor points coincide:
    // B = A + offset_a - offset_b. That is right when the caller has no opinion
    // about where B goes and wants the bond to decide it.
    //
    // It is WRONG, silently and destructively, when the caller has already
    // worked out a position, because this overwrites it. Issue #38: the tree
    // generator ran a 30-attempt non-overlapping placement search for every
    // leaf, and this function then discarded the result and put every leaf on a
    // branch at the SAME point (offset_a = branch tip, offset_b = 0). Branches
    // too: their offset_a runs along world +Z, so an angled parent's child
    // landed nowhere near the tip the generator had computed. 381 of a single
    // tree's body pairs were created at the same position, and the solver then
    // ejected them at over 3 m/s.
    //
    // If you have placed the particle yourself, pass true.
    size_t add_particle_with_gluon_to(size_t particle_a_id,
                                       const Particle& particle_b_config,
                                       std::unique_ptr<GluonConstraintBase> gluon,
                                       bool use_config_position = false);

    // Add gluon between two EXISTING particles
    // Use when both particles already exist (e.g., connecting right leg to hips)
    void add_gluon_between(size_t particle_a_id, size_t particle_b_id,
                           std::unique_ptr<GluonConstraintBase> gluon);

    // ========================================================================
    // GLUON QUERY INTERFACE (for Animation/Dynamics FK)
    // ========================================================================
    // Query gluon properties for Forward Kinematics animation.
    // Animation sets rotation angles, FK computes positions using gluon segment lengths.
    //
    // See: docs/dynamics_physics_integration.md - "Gluons as Source of Truth"
    // ========================================================================

    // Get gluon connecting two particles (order-independent)
    // Returns nullptr if no gluon exists between them
    // Use for querying segment length, rotation limits, etc.
    const GluonConstraintBase* get_gluon(size_t particle_a, size_t particle_b) const;
    GluonConstraintBase* get_gluon_mut(size_t particle_a, size_t particle_b);

    // Get all gluons connected to a particle
    // Useful for FK chain traversal (shoulder → upper_arm → forearm → hand)
    std::vector<const GluonConstraintBase*> get_gluons_for_particle(size_t particle_id) const;

    // ========================================================================
    // TARGETED GLUON CONSTRAINT (for Animation PHYSICS mode)
    // ========================================================================
    // Apply gluon distance constraint for a specific particle pair.
    // Used by animation system for PHYSICS-mode joints where FK skips the joint
    // and physics should position the child based on gluon distance from parent.
    //
    // parent_id: The particle that was FK-positioned (anchor)
    // child_id: The particle to be positioned by physics (moves to satisfy constraint)
    //
    // The child is moved to maintain gluon distance from parent.
    // This runs AFTER FK so the parent position is already updated.
    // ========================================================================
    void apply_gluon_constraint_for_pair(size_t parent_id, size_t child_id,
                                         ParticleSystem::WriteView& particles);

private:
    // Refactored physics phases (extracted for modularity)
    void apply_all_forces(ParticleSystem::WriteView& particles, float dt);     // Phase 1: Force application
    void integrate_velocities(ParticleSystem::WriteView& particles, float dt); // Phase 2: Velocity integration
    void integrate_positions(ParticleSystem::WriteView& particles, float dt);  // Phase 3: Position integration
    void enforce_gluon_constraints(ParticleSystem::WriteView& particles);     // Phase 4: PBD position correction for gluons

    // Angular physics phase - integrates angular velocities and applies torques from angular constraints
    void apply_angular_constraints(ParticleSystem::WriteView& particles, float dt);    // Apply gluon angular torques
    void integrate_angular_velocities(ParticleSystem::WriteView& particles, float dt); // Integrate omega_z → rotation_z

    // Force application submethods (Phase 1 breakdown)
    void apply_registry_forces(ParticleSystem::WriteView& particles, float dt);
    void apply_gluon_forces(ParticleSystem::WriteView& particles, float dt);
    void solve_contacts_v3(ParticleSystem::WriteView& particles, float dt);  // V3 predictive integration

    // Physics integration (split for 6-phase collision system)
    void update_velocity(Particle& p, float dt);     // Phase 2: v += a×dt
    // Phase 6 now uses inline hard contact constraint - no helper function

    // Collision detection (AABB - axis-aligned bounding boxes)
    // Returns true if collision detected
    bool detect_aabb_collision(const Particle& a, const Particle& b,
                               const Vec3& pos_a, const Vec3& pos_b,
                               CollisionInfo& out);

    // Swept AABB helpers (Phase 1: Continuous Collision Detection)
    AABB build_particle_aabb(const Particle& p, const Vec3& pos);
    bool aabb_intersects(const AABB& a, const AABB& b);

    // REMOVED: resolve_collision_impulse() - pure force-based physics only

    Engine* engine_;
    ParticleSystem* particle_system_;
    PhysicsConfig config_;
    bool is_initialized_;

    // Force registry (modular force system)
    std::vector<std::unique_ptr<Force>> forces_;

    // Gluon v2 - Polymorphic gluons (the only constraint system in use)
    // Use deque instead of vector: deque doesn't invalidate pointers on push_back,
    // which is critical for gluon_pair_index_ (stores raw pointers to elements)
    std::deque<std::unique_ptr<GluonConstraintBase>> gluon_constraints_v2_;
    // THE SLEEP LAW: a body may be at rest ONLY while its constraint set is
    // satisfied. Rebuilt each solve from gluon strain and angular error;
    // consulted by rest-entry and the rest damper. This is a derived fact,
    // not a heuristic: it replaces per-cause wake triggers, holds in any
    // gravity, and reads no ownership. (The lying blade was a wake/re-sleep
    // limit cycle: woken mid-build every frame, re-captured by frame end,
    // recovering at ~0.1 mm/frame while probes read rest=1, v=0.)
    std::vector<uint8_t> constraint_dissatisfied_;
    size_t next_gluon_id_;  // For gluon ID assignment
    std::vector<size_t> gluons_to_remove_;  // Deferred removal (can't modify during iteration)

    // Gluon index by particle pair - O(1) lookup for FK animation queries
    // Key: canonical pair (min << 32 | max), Value: pointer to gluon
    // Updated when gluons are added/removed/swapped
    std::unordered_map<size_t, GluonConstraintBase*> gluon_pair_index_;

    // Helper: create canonical pair key (order-independent)
    static size_t make_gluon_pair_key(size_t a, size_t b) {
        size_t min_id = std::min(a, b);
        size_t max_id = std::max(a, b);
        return (min_id << 32) | max_id;
    }

    // Index maintenance helpers
    void index_gluon(GluonConstraintBase* gluon);
    void unindex_gluon(GluonConstraintBase* gluon);

    // Active contacts for current frame (used by BFS for position projection)
    // Contacts with kinematic surfaces provide equilibrium source for mobile bodies
    std::vector<std::pair<size_t, size_t>> active_contacts_;

    // V4.2 Step 1: Support-Loss Wake Detection
    // Track which particles have support contacts (Turtle or vertical particle contact)
    // When a particle loses support, wake it so gravity applies and it falls to re-establish contact
    std::unordered_set<size_t> supported_particles_prev_;  // From last frame
    std::unordered_set<size_t> supported_particles_curr_;  // This frame

    // V4.3: Warm Starting - Cache impulses across frames
    // Key: particle pair + contact type
    // Value: (impulse, last_used_frame) - keep entries for 60 frames after last use
    // Resting contacts retain equilibrium impulse, preventing oscillation
    std::unordered_map<PhysicsV4::ContactKey, std::pair<float, int>, PhysicsV4::ContactKeyHash> cached_impulses_;

    // V4.11: Persistent contacts - track last frame each pair had a contact
    // Key: canonical pair key (min,max packed into size_t)
    // Value: last frame number when contact existed
    // Used to keep contacts alive for N frames after separation to prevent jitter-sink
    std::unordered_map<size_t, int> recent_contact_frames_;

    // Collision events for game systems (combat, etc.) - populated each frame
    std::vector<CollisionEvent> collision_events_;

    // Particle interaction model (nullable policy + this frame's
    // filtered broad-phase overlaps; cleared at solve entry)
    const logosphere::interaction::ParticleInteractionSystem* interaction_ = nullptr;
    std::vector<logosphere::interaction::FilteredOverlap> filtered_overlaps_;

    // Physics telemetry: per-particle contact recording
    Logosphere::PhysicsTelemetry telemetry_;

    // Pre-constraint omega_z values - saved BEFORE angular constraints propagate omega_z
    // Used by position projection to identify which particle is being externally controlled
    // (external control sets omega_z before solver, solver propagates to connected particles)
    std::vector<float> pre_constraint_omega_z_;

    // Gluon removal (deferred to avoid modifying container during iteration)
    void mark_gluon_for_removal(size_t gluon_id);
    void remove_marked_gluons(ParticleSystem::WriteView* particles = nullptr);

    // Wake propagation through gluon graph (BFS)
    // V4.6: Threshold-based propagation - only propagate if impact_velocity exceeds threshold
    // Default: FLT_MAX = always propagate (for external API calls like wake_particle())
    // Contact collisions pass the colliding particle's velocity
    void wake_particle_with_propagation(size_t particle_id, ParticleSystem::WriteView& particles,
                                        float impact_velocity = std::numeric_limits<float>::max());

    // V4.5: Turtle-Support Chain Checking (Optimization)
    // Returns true if particle has continuous gluon support path down to Turtle
    // Used to prevent cascading wake events on terrain
    bool check_turtle_support_chain(size_t particle_id, ParticleSystem::WriteView& particles);

    // Phase 4.5: Position Projection for Gluons (V3.5 Fix)
    // See docs/PHYSICS_ITERATIVE_RESOLVER_V4.md Part 17
    std::vector<int> compute_bfs_distance(ParticleSystem::WriteView& particles);
    void project_gluon_positions(ParticleSystem::WriteView& particles);

    // Phase 4.8: Angular Limit Projection (hard constraints)
    // Projects rotations back to satisfy gluon angular limits after integration
    void project_angular_limits(ParticleSystem::WriteView& particles);

    // Phase 6: Turtle Boundary Enforcement (absolute constraint)
    // Post-solver safety net - clamps any particle that penetrated turtle back above z=0
    void enforce_turtle_boundary(ParticleSystem::WriteView& particles);

    // Phase 7: At-rest state update (MUST be after turtle boundary)
    // Checks final velocities after all corrections to determine rest state
    void update_rest_state(ParticleSystem::WriteView& particles);
};

#endif // PHYSICS_SYSTEM_H
