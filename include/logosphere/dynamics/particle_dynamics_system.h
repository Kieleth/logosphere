#ifndef PARTICLE_DYNAMICS_SYSTEM_H
#define PARTICLE_DYNAMICS_SYSTEM_H

#include "logosphere/kg/kg_types.h"
#include "logosphere/dynamics/animation_controller.h"
#include "core/particle_system.h"  // Need full definition for ParticleSystem::WriteView
#include "math/transform.h"        // Quaternion-based transforms for FK
#include "core/joint_types.h"      // Anatomical joint definitions
#include "logosphere/capability/capability_profile.h"  // Engine capability aggregation
#include "logosphere/capability/dynamics_params.h"     // Game-overridable dynamics parameters
#include "core/entity_telemetry.h" // Per-entity per-frame state recording
#include "logosphere/dynamics/kinematic_root.h"      // Skeleton anchor (stance foot / hips / thorax / ...)
#include "logosphere/animation/joint_hierarchy.h"    // Joint, JointHierarchy, JointMode (refactor B3 home)
#include "logosphere/animation/humanoid_locomotion.h" // HumanoidParts (refactor B4 home)
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <functional>
#include <memory>

// Forward declarations
class Engine;
class CoordinateTransformer;
class InputSystem;
namespace kg { class KGModule; }
struct CollisionEvent;  // From physics_system.h

// EDUCATIONAL NOTE: Particle Dynamics System
//
// This system handles dynamic behaviors for particles and entities:
// 1. Look-At behaviors - entities track targets (mouse, other entities)
// 2. Procedural animations - idle movements, breathing, etc.
// 3. Hierarchical transformations - parent-child rotation/position
//
// Separates behavior logic from:
// - Generation (WorldGen creates, Dynamics animates)
// - Physics (Physics handles forces, Dynamics handles rotations/poses)
// - Input (Input reads, Dynamics applies)

// Configuration for dynamics system
struct DynamicsConfig {
    bool enable_look_at = true;           // Enable look-at behaviors
    bool debug_rotations = false;         // Log rotation calculations

    // Rotation constraints (radians)
    float head_max_rotation = 1.57f;      // ±90° (π/2)
    float torso_max_rotation = 0.785f;    // ±45° (π/4)
};

// Performance metrics
struct DynamicsMetrics {
    int tracked_entities = 0;             // Entities with behaviors
    int look_at_updates = 0;              // Look-at calculations this frame
    double update_time = 0.0;             // Time spent updating (ms)
};

// Particle Dynamics System - handles entity behaviors and animations
class ParticleDynamicsSystem {
    // Refactor B5+: HumanoidLocomotion is the eventual home for all
    // humanoid policy. While clusters migrate, it needs read/write
    // access to humanoid_look_at_entities_ (still owned here for the
    // moment). The friend declaration is removed in B-cleanup once
    // the container itself relocates.
    friend class logosphere::animation::HumanoidLocomotion;

public:
    // FK debug: when > 0, prints per-joint trace for right arm chain
    // Decremented each frame. Set from test code to enable for N frames.
    static int fk_arm_debug_frames;

    ParticleDynamicsSystem();
    ~ParticleDynamicsSystem();

    // Initialize with engine reference (production path).
    bool initialize(Engine* engine, const DynamicsConfig& config = DynamicsConfig());

    // Initialize without an Engine, for headless test harnesses. Just enough
    // wiring to host the humanoid_look_at_entities_ container + utility
    // helpers (normalize_angle, get_friction). The pre / post-physics
    // loops live in HumanoidLocomotion now, so dynamics in headless mode
    // is essentially a state holder.
    bool initialize_headless(ParticleSystem& particle_system,
                             const DynamicsConfig& config = DynamicsConfig());

    void shutdown();

    // Telemetry forwarded to physics system (physics-level, all particles)
    Logosphere::PhysicsTelemetry& get_telemetry();
    const Logosphere::PhysicsTelemetry& get_telemetry() const;

    // Main update - processes all entity behaviors (runs BEFORE physics)
    void update(double delta_time);

    // Post-physics update - applies kinematic animations AFTER gluon solver
    // Must run after physics to prevent gluon constraints from undoing animation
    void update_post_physics(double delta_time);

    // Notify that a particle was swapped from old_idx to new_idx.
    // Called by ParticleSystem::remove_particle when swap-removing a particle.
    // Updates cached particle IDs in all tracked entity structs so subsequent
    // updates write to the correct particle indices.
    void notify_particle_swap(size_t old_idx, size_t new_idx);

    // Unregister entity (cleanup)
    void unregister_entity(kg::EntityID entity_id);

    // Creature callback registration
    // Autonomous creatures register a callback to be invoked during dynamics update
    // The callback receives dt, particle system, and dynamics system references
    using CreatureCallback = std::function<void(float dt, ParticleSystem&, ParticleDynamicsSystem&)>;
    void register_creature_callback(CreatureCallback callback);
    void clear_creature_callbacks();

    // Configuration and metrics
    const DynamicsConfig& get_config() const { return config_; }
    const DynamicsMetrics& get_metrics() const { return metrics_; }
    void reset_metrics() { metrics_ = DynamicsMetrics(); }

    // Engine accessor — needed by callers that hold a dynamics handle but
    // need to reach a sibling subsystem (e.g. EntitySystem unregistering a
    // humanoid via HumanoidLocomotion). Was implicitly available before B8
    // through the public delegators; now it's the explicit hop.
    Engine* get_engine() const { return engine_; }

    // =========================================================================
    // GENERIC JOINT SYSTEM (FK Animation)
    // =========================================================================
    // Joints are named rotation points in a kinematic chain.
    // Animation sets joint angles, FK computes particle positions.
    // Gluons provide segment lengths and rotation limits.
    //
    // See: docs/dynamics_physics_integration.md - "Gluons as Source of Truth"
    // =========================================================================

    // Joint, JointHierarchy, JointMode are defined in
    // include/logosphere/animation/joint_hierarchy.h. The aliases
    // below keep `ParticleDynamicsSystem::Joint` etc. working for
    // existing callers; future commits in the rehaul (see plan
    // the kinematic-root design (docs/ARCHITECTURE.md)) will
    // migrate consumers to the canonical
    // `logosphere::animation::Joint` and drop these aliases.
    using JointMode      = logosphere::animation::JointMode;
    using Joint          = logosphere::animation::Joint;
    using JointHierarchy = logosphere::animation::JointHierarchy;

    // (Inline Joint / JointHierarchy definitions previously here
    // moved to include/logosphere/animation/joint_hierarchy.h in
    // the rehaul B3 commit.)
#if 0
    struct Joint {
        std::string name;               // "right_shoulder", "right_elbow", etc.
        unsigned int parent_particle;   // Particle closer to root (e.g., shoulder)
        unsigned int child_particle;    // Particle farther from root (e.g., upper_arm)

        // === COMPOUND ROTATION (NEW) ===
        // Multiple rotation channels composed into single quaternion BEFORE FK.
        // This replaces the old single-axis (axis + current_angle) approach.
        float rotation_x = 0.0f;        // Twist/roll (around bone axis)
        float rotation_y = 0.0f;        // Abduction (Y-axis, e.g., shoulder raise)
        float rotation_z = 0.0f;        // Flexion (Z-axis, e.g., arm forward/back)

        // Compute compound rotation as quaternion (extrinsic XYZ order)
        logosphere::Quat compound_rotation() const {
            // Extrinsic XYZ order: all rotations around world axes
            // X first, Y second (abduction), Z third (horizontal swing)
            // Quaternion math: Q2 * Q1 applies Q1 first, then Q2
            // So qz * qy * qx = X applied first, Y second, Z third (all in world frame)
            logosphere::Quat qx = logosphere::Quat::from_axis_angle(1, 0, 0, rotation_x);
            logosphere::Quat qy = logosphere::Quat::from_axis_angle(0, 1, 0, rotation_y);
            logosphere::Quat qz = logosphere::Quat::from_axis_angle(0, 0, 1, rotation_z);
            return qz * qy * qx;
        }

        // Transform-based FK
        logosphere::Transform rest_local;      // Bind pose (from gluon offsets)
        logosphere::Transform local_transform; // Current (animated)
        logosphere::Transform world_transform; // Computed by FK

        // Gluon offsets (cached from lazy init for FK calculation)
        // pivot_offset: offset_a (parent center → pivot point)
        // child_offset: offset_b (child center → pivot point)
        logosphere::Vec3 pivot_offset = {0.0f, 0.0f, 0.0f};
        logosphere::Vec3 child_offset = {0.0f, 0.0f, 0.0f};

        // Current control mode (set by animation)
        // DRIVEN: use rotation_x/y/z angles (default)
        // PHYSICS: skip FK, let physics/gravity control position
        // DIRECTION: point in world-space direction
        // INHERIT: follow parent rotation (angle=0)
        JointMode mode = JointMode::DRIVEN;

        // === ANATOMICAL JOINT DEFINITION ===
        // Defines what "flex", "abduct", "twist" mean for this joint.
        // Shared template - do not delete (owned by joint_types.cpp globals).
        // nullptr = no semantic support, use raw rotation_x/y/z only.
        const logosphere::JointDefinition* definition = nullptr;

        // === SEMANTIC ROTATION TARGETS ===
        // Set by animation via flex()/abduct()/twist() API.
        // FK reads these and maps to correct axis via definition.
        // Raw rotation_x/y/z are used when definition is nullptr or for legacy API.
        float flex_angle = 0.0f;     // Target angle for flexion/extension
        float abduct_angle = 0.0f;   // Target angle for abduction/adduction
        float twist_angle = 0.0f;    // Target angle for internal/external rotation
        bool has_flex_target = false;
        bool has_abduct_target = false;
        bool has_twist_target = false;

        // Clear semantic targets (called at start of each animation frame)
        void clear_semantic_targets() {
            flex_angle = 0.0f;
            abduct_angle = 0.0f;
            twist_angle = 0.0f;
            has_flex_target = false;
            has_abduct_target = false;
            has_twist_target = false;
        }

        // Compute rotation quaternion from semantic targets using joint definition
        // Falls back to compound_rotation() if no definition or no semantic targets
        logosphere::Quat semantic_rotation() const {
            if (!definition) {
                return compound_rotation();  // No definition, use raw angles
            }

            // Start with identity
            logosphere::Quat result = logosphere::Quat::identity();

            // Apply semantic rotations: twist, then flex, then abduct
            // Intrinsic order: flex in body plane first, then abduct to raise
            // (innermost first in quaternion multiplication)
            if (has_twist_target && definition->supports_twist()) {
                float angle = definition->clamp_twist(twist_angle);
                const auto& axis = definition->twist_axis;
                result = logosphere::Quat::from_axis_angle(axis.x, axis.y, axis.z, angle) * result;
            }
            if (has_flex_target && definition->supports_flex()) {
                float angle = definition->clamp_flex(flex_angle);
                const auto& axis = definition->flexion_axis;
                result = logosphere::Quat::from_axis_angle(axis.x, axis.y, axis.z, angle) * result;
            }
            if (has_abduct_target && definition->supports_abduct()) {
                float angle = definition->clamp_abduct(abduct_angle);
                const auto& axis = definition->abduction_axis;
                result = logosphere::Quat::from_axis_angle(axis.x, axis.y, axis.z, angle) * result;
            }

            return result;
        }
    };

    // Per-entity joint hierarchy (ordered for FK: parents before children)
    struct JointHierarchy {
        std::vector<Joint> joints;
        std::unordered_map<std::string, size_t> name_to_index;

        // Add a joint (call in parent-first order for correct FK)
        void add_joint(const Joint& joint) {
            name_to_index[joint.name] = joints.size();
            joints.push_back(joint);
        }

        // Get joint by name (nullptr if not found)
        Joint* get_joint(const std::string& name) {
            auto it = name_to_index.find(name);
            return (it != name_to_index.end()) ? &joints[it->second] : nullptr;
        }

        const Joint* get_joint(const std::string& name) const {
            auto it = name_to_index.find(name);
            return (it != name_to_index.end()) ? &joints[it->second] : nullptr;
        }
    };
#endif  // dead inline Joint / JointHierarchy bodies — moved to animation/joint_hierarchy.h

private:
    // HumanoidParts is defined in
    // include/logosphere/animation/humanoid_locomotion.h. The alias
    // below keeps `ParticleDynamicsSystem::HumanoidParts` working for
    // existing internal references; future commits in the rehaul (see
    // plan the kinematic-root design (docs/ARCHITECTURE.md)) will
    // migrate state ownership into HumanoidLocomotion::Impl and drop
    // this alias.
    using HumanoidParts = logosphere::animation::HumanoidParts;

    // (Inline HumanoidParts struct previously defined here moved to
    // humanoid_locomotion.h in the rehaul B4 commit.)
#if 0
    struct HumanoidParts {
        kg::EntityID entity_id = kg::INVALID_ENTITY;
        bool is_physics_based = false;  // True = gluon-connected, use velocity rotation
        unsigned int head = 0;
        unsigned int neck = 0;
        unsigned int torso = 0;
        unsigned int abdomen = 0;
        unsigned int hips = 0;

        // Body parts with position offsets
        std::vector<unsigned int> left_leg_particles;
        std::vector<unsigned int> right_leg_particles;
        std::vector<unsigned int> left_arm_particles;
        std::vector<unsigned int> right_arm_particles;
        std::vector<unsigned int> head_child_particles;  // Hair, ears (from "Head" child entity)
        std::vector<std::pair<float, float>> head_child_offsets;  // XY offsets from head center for pivoting
        struct Vec3Offset { float x, y, z; };
        std::vector<Vec3Offset> head_child_3d_offsets;  // 3D offsets from head (for FK coherence)

        // Offsets from body center
        float leg_spacing = 0.0f;       // X offset for legs (±leg_spacing)
        float shoulder_offset = 0.0f;   // X offset for shoulders/arms (±shoulder_offset)
        float ear_offset = 0.0f;        // X offset for ears (±ear_offset from head center)

        float world_x = 0, world_y = 0, world_z = 0;  // Base position
        float base_rotation = 0.0f;                   // Current body rotation

        // Yaw cascade (biomechanical head-leads model).
        // Each segment follows its upstream (target→head→torso→hips) with an
        // exponential time constant, producing the "head leads by ~100 ms,
        // hips catch up last by ~1 s" behaviour documented in locomotor-
        // steering research. head_yaw_world / torso_yaw_world /
        // hips_yaw_world are world-frame Euler-Z values; the legacy
        // head_rotation / torso_rotation fields are derived as the relative
        // offsets between adjacent segments and written onto particle
        // rotations by update_humanoid_look_at (docs/ARCHITECTURE.md,
        // "Biomechanical rotation + sidestep").
        float head_yaw_world  = 0.0f;
        float torso_yaw_world = 0.0f;
        float hips_yaw_world  = 0.0f;
        bool  yaw_cascade_inited = false;

        // World-frame facing at which feet were last committed (via heel-
        // strike or idle twist-step). When |hips_yaw_world − feet_yaw_world|
        // exceeds YAW_STEP_THRESHOLD (~45°) while idle, the non-stance foot
        // replants under the new hips orientation — real bipeds don't spin
        // feet-glued in place. See plan "Biomechanical rotation + sidestep"
        // foot-step trigger. Initialized at first plant.
        float feet_yaw_world = 0.0f;
        bool  feet_yaw_inited = false;

        // One-frame memory of `is_moving` so we can detect the
        // walk-to-idle transition. On that edge the plant must release —
        // otherwise the stance foot stays pinned at the last heel-strike
        // plant_target, which is up to half a stride behind the hips
        // when the walk stops, leaving one leg dangling behind.
        bool was_moving = false;

        // Physics-drive mode. When true, FK does NOT write bone particle
        // positions; instead it publishes per-joint target rotations onto
        // the owning gluons, and the XPBD solver reconciles chain
        // integrity + contacts. See plan "Physics-Driven Skeleton
        // (Option C)". Default false keeps the legacy position-write
        // path until Phase 5 retires it. Flip per-entity in tests to
        // opt in while the old path still exists.
        bool use_physics_drive = false;

        // Child particles whose rotation_z is being driven by a gluon
        // PD controller instead of FK / cascade. Populated by
        // set_joint_physics_drive. FK position writes, the yaw cascade,
        // and shape-snap's rotation propagation skip any particle in
        // this set so the physics solver has uncontested ownership.
        std::unordered_set<unsigned int> physics_drive_children;

        // Subset of physics_drive_children whose target was set once
        // via set_joint_physics_drive[_q] and is NOT to be overwritten
        // by per-frame semantic-target publishing. Tests and external
        // controllers that want a static pose land here. Clip-driven
        // joints (walk cycle, idle breathing, look-at) omit this flag
        // so the Phase 4 publisher refreshes their target every frame.
        std::unordered_set<unsigned int> physics_drive_static_targets;

        // TODO[DYNAMICS-003]: Use generic particle list instead of hardcoded body parts (see docs/entity_dynamics.md "Discovery 3")
        // All particles belonging to this entity (from KG recursive query)
        // Populated from kg_->getEntityKGParticlesRecursive() at registration
        // Handles entity mutation (severed limbs, attached items) automatically
        std::vector<unsigned int> all_particle_indices;

        // Rest offsets from hips for shape maintenance (populated at registration)
        // Each entry corresponds to the particle in all_particle_indices at same index
        // Used by maintain_entity_shape() to restore relative positions
        struct RestOffset { float x, y, z, rotation_z; };  // rotation_z = particle's rest rotation relative to hips
        std::vector<RestOffset> rest_offsets;

        // NOTE: Particle ownership is tracked directly on Particle.owner (see particle.h)
        // At registration: all particles set to ParticleOwner::DYNAMICS
        // During FK animation: animated particles set to ParticleOwner::ANIMATION
        // After animation completes: reset back to ParticleOwner::DYNAMICS

        // Physics state (in-memory, not synced to KG per-frame)
        float velocity_x = 0.0f;
        float velocity_y = 0.0f;
        float velocity_z = 0.0f;

        // Ground state cache (set by enforce_ground_support, read by telemetry)
        float cached_ground_z = -1e9f;
        float cached_gap_to_ground = 1e9f;
        int cached_ground_particle_id = -1;

        // Animation state (in-memory, not synced to KG per-frame)
        float walk_phase = 0.0f;           // Walk cycle phase [0, 2π]
        bool is_moving = false;            // Is entity position changing this frame (auto-detected)
                                           // Friction disabled only when BOTH is_moving AND is_volitional
        bool is_grappled = false;          // Is entity grabbed by another? Blocks volitional movement
        bool is_lying_down = false;        // Is entity lying down (resting)? Pose is horizontal
        bool has_look_at_capability = false;  // Has torso parts (head, neck, etc.) for look-at behavior
        bool diagnostics_enabled = false;  // When true, log arm particle LOCAL coordinates each frame
        float prev_world_x = 0.0f;         // Previous position (to detect movement)
        float prev_world_y = 0.0f;

        // (prev_offset fields removed - velocity-based animation is stateless)
        // See docs/PHYSICS_AND_ANIMATIONS.md

        // Per-particle mass implementation (see docs/entity_dynamics.md "Discovery 1")
        // Mass is calculated by summing all particle masses (volume × material_density)
        // Updated during parse_humanoid_parts() from KG particle query
        float mass = 0.0f;         // Total entity mass in kg (calculated from particles)

        // Material-based friction (see docs/entity_dynamics.md "Discovery 2")
        // Lookup infrastructure ready: get_friction(particle_material, ground_material)
        // Usage: Detect ground material → lookup coefficient from table
        float friction = 0.8f;     // STUB: Not yet used in physics integration

        // Capability profile (engine): aggregated capability factors from KG body graph.
        CapabilityProfile cap;
        // Dynamics params (game-overridable): speeds, turn rates, gaze zones, etc.
        // Derived from cap via DynamicsParams::from_capability() at registration.
        DynamicsParams dynamics;

        float speed_modifier = 1.0f;       // 0-1 multiplier from status effects

        // Locomotion state (runtime, not FORGE-derived)
        float target_vx = 0.0f;
        float target_vy = 0.0f;
        float local_forward = 0.0f;
        float local_right = 0.0f;
        bool use_body_relative = false;

        // Sustained gaze tracking (runtime state)
        float head_offset_timer = 0.0f;
        float head_offset_angle = 0.0f;

        // Custom look-at target (for NPC AI - overrides mouse input)
        bool has_custom_target = false;
        float custom_target_x = 0.0f;
        float custom_target_y = 0.0f;

        // Animation controller for motor-force driven animations (punch, etc.)
        AnimationController animation_controller;

        // Joint hierarchy for FK-based animation
        // Joints are registered at humanoid creation, angles set by animation
        JointHierarchy joint_hierarchy;

        // FK animation playback (joint-angle driven, parallel to AnimationController)
        std::unordered_map<std::string, FKAnimationClip> fk_clips;
        const FKAnimationClip* fk_active_clip = nullptr;
        float fk_time_ms = 0.0f;
        bool fk_playing = false;

        // Phase-driven FK walk (replaces sinusoidal velocity impulses)
        FKAnimationClip fk_walk_clip_r;       // Pre-registered right step clip
        FKAnimationClip fk_walk_clip_l;       // Pre-registered left step clip
        bool fk_walk_enabled = false;         // Walk clips registered
        bool fk_walk_side_right = true;       // Current step is right leg
        float fk_walk_step_duration_ms = 600.0f;  // Clip duration (for phase→time mapping)

        // Strafe side-step clips (for blending with forward walk)
        FKAnimationClip fk_strafe_clip_r;     // Right step when strafing rightward
        FKAnimationClip fk_strafe_clip_l;     // Left step when strafing rightward
        bool fk_strafe_enabled = false;
        float fk_strafe_step_duration_ms = 600.0f;

        // Turn-in-place clips
        FKAnimationClip fk_turn_clip_r;       // Right leg swings (turning right)
        FKAnimationClip fk_turn_clip_l;       // Left leg swings (turning left)
        bool fk_turn_enabled = false;
        float fk_turn_step_duration_ms = 500.0f;
        bool is_turning_in_place = false;     // Detected: rotating without translating
        float turn_phase = 0.0f;              // Phase for turn animation [0, 2π]
        float prev_base_rotation = 0.0f;      // For detecting rotation delta

        // Foot planting — locks stance foot in world space via 2-bone IK
        // During stance phase, the foot stays fixed at plant_target while
        // the hips advance. IK solves the hip→knee→ankle chain to reach.
        bool foot_planting_enabled = false;   // Master switch
        bool has_planted_foot = false;        // Currently tracking a plant
        bool planted_foot_is_right = false;   // Which foot is planted
        float plant_target_x = 0.0f;         // World-space anchor position
        float plant_target_y = 0.0f;
        float plant_target_z = 0.0f;
        float plant_foot_rx = 0.0f;          // Foot rotation at heel-strike (Euler)
        float plant_foot_ry = 0.0f;          // Locked so ankle_target doesn't drift
        float plant_foot_rz = 0.0f;
        float plant_blend = 0.0f;            // 0=FK, 1=IK (for smooth transitions)
        int plant_step_count = 0;            // Steps since planting enabled (for calibration)
        float prev_walk_phase_half = -1.0f;  // For detecting half-cycle transitions

        // Idle animation — subtle breathing + weight shift when stationary
        FKAnimationClip fk_idle_clip;         // Looping idle clip (~4s cycle)
        bool fk_idle_enabled = false;         // Idle clip registered
        float idle_phase = 0.0f;             // [0, 2π] phase for idle cycle
        float fk_idle_cycle_ms = 4000.0f;    // Full cycle duration

        // Run animation — biomechanically distinct from walk
        FKAnimationClip fk_run_clip_r;        // Right step run clip
        FKAnimationClip fk_run_clip_l;        // Left step run clip
        bool fk_run_enabled = false;          // Run clips registered
        float fk_run_step_duration_ms = 400.0f;  // Shorter than walk (300+100ms)
        float fk_run_stride_length = 0.85f;   // Longer than walk (0.65m)

        // C1: Ease timing curve — non-linear phase-to-clip-time mapping
        // Makes toe-off/heel-strike snappy, mid-swing floaty.
        // ease(t) = t + A * sin(2πt), where A controls intensity.
        // At A=0.08: boundaries 50% faster, mid-swing 50% slower.
        float fk_walk_ease_amount = 0.08f;

        // C4: Walk-to-run transition smoothing
        // Temporal filter on run_blend prevents jitter from frame-to-frame speed fluctuation.
        // Smoothstep applied on top for organic feel.
        float current_run_blend = 0.0f;       // Smoothed run blend [0,1]

        // Kinematic root — which particle anchors this skeleton's world
        // position. Default: hips, FOLLOW_VELOCITY (pre-refactor behavior).
        // Stance-phase walking will flip this to the planted foot with
        // mode=FIXED_WORLD; hips are then derived via reverse-FK.
        logosphere::KinematicRoot root;
    };
#endif  // dead inline HumanoidParts body — moved to animation/humanoid_locomotion.h

    // Serpent parts (parsed from KG)
    //
    // SERPENT LOCOMOTION DESIGN:
    // ==========================
    // Real serpentine movement requires:
    // 1. Head turns gradually left/right (not instant rotation)
    // 2. Head always moves forward in current heading direction
    // 3. Turn switches every ~35cm traveled (distance-based, not time-based)
    // 4. Random turn angles (30-60°) create natural variation
    // 5. Body segments follow head via simple follow-the-leader chain
    //    - Each segment maintains segment_spacing behind predecessor
    //    - Smooth interpolation at 3.0 m/s prevents teleporting/compression
    //    - The lag naturally creates the S-curve wave propagating down body
    //
    // WHAT DIDN'T WORK:
    // - Trail-based following (segments following head's position history)
    //   → Complex distance measurement along trail caused compression/elongation
    // - Artificial lateral oscillation added to segments
    //   → Caused compression and unnatural movement
    // - Simultaneous forward + lateral movement
    //   → Felt like earthworm, not serpent
    // - Time-based turn switching
    //   → Inconsistent at different speeds
    //
    // FINAL APPROACH (distance-based turning + simple follow-the-leader):
    // - Head turns toward random target angle while moving forward
    // - Every 35cm, pick new random angle (30-60°) and switch left/right
    // - Body segments follow via simple chain (segment → predecessor)
    // - Wave emerges from lag between segments following curved path
    //
    // SerpentParts moved to logosphere::animation::SerpentParts in B19
    // (see include/logosphere/animation/serpent_locomotion.h).

    // ButterflyParts moved to logosphere::animation::ButterflyParts in B20
    // (see include/logosphere/animation/butterfly_flight.h).

    // ========================================================================
    // REUSABLE BEHAVIOR PRIMITIVES
    // ========================================================================
    // These helper functions implement common animation patterns that can be
    // reused across different entity types (humanoid, serpent, butterfly, etc.)

    // Primitive: Sinusoidal offset calculation
    // Returns: amplitude * sin(phase + phase_offset)
    // Usage: Wing beats, limb swings, tail waves, breathing
    float calculate_sinusoidal_offset(float phase, float amplitude, float phase_offset = 0.0f) const;

    // Primitive: Random probability check (for state transitions)
    // Returns: true with probability (chance_per_second * delta_time)
    // Usage: Gliding transitions, idle animations, behavior switches
    bool check_random_probability(float chance_per_second, double delta_time) const;

    // Primitive: Smooth direction turning
    // Gradually adjusts current_direction toward target_direction
    // Returns: new direction (radians)
    // Usage: Autonomous flight, wandering, smooth rotation
    float smooth_turn_toward(float current_direction, float target_direction, float max_turn_rate, double delta_time) const;

    // ========================================================================
    // ENTITY-SPECIFIC BEHAVIOR FUNCTIONS
    // ========================================================================

    // Behavior update functions
    // Per-frame physics-drive target publisher (Phase 4). After the
    // frame's semantic targets are set on joints, pushes each joint's
    // composed rotation into its gluon's target_relative_q iff the
    // child particle is in physics_drive_children. Cheap no-op when
    // no joints are driven.

    // ========================================================================
    // DYNAMICS-CONTROLLED ENTITY FUNCTIONS
    // ========================================================================
    // These functions manage entities where physics is disabled (owner == DYNAMICS)

    // Apply gravity uniformly to all particles in entity (entity-level, not per-particle)
    // Enforce ground support after position integration (runs post-physics)
    // Integrate hips position and maintain entity shape via rest offsets
    // Transform-based FK using quaternion/matrix composition
    // Cleaner implementation that avoids rotation order bugs
    // Yaw cascade: head→torso→hips with time constants (80/180/350 ms).
    // Split into two phases because FK would otherwise overwrite the
    // rotation_z values the cascade writes:
    //   - _state: advances head_yaw_world / torso_yaw_world / hips_yaw_world
    //             each frame, updates parts.base_rotation. Runs pre-FK so
    //             walk-cycle + body-relative velocity see the new hips yaw.
    //   - _apply: writes particle.rotation_z for head / neck / chest /
    //             torso / abdomen / hips / limbs. Runs post-FK so FK's own
    //             joint-rotation outputs don't clobber the cascade.
    // See the kinematic-root design (docs/ARCHITECTURE.md) "Biomechanical rotation + sidestep".
    // Post-FK chain projection: re-thread every animated particle through
    // its joint's pure-FK world_transform.position, overwriting any per-
    // joint collide-and-slide residues. Restores chain integrity
    // (|parent-child| == segment_length) in a single forward walk through
    // the joint hierarchy. Idempotent and gravity-free; operates only on
    // position, leaves rotations alone (FK owns those).
    //
    // See the kinematic-root design (docs/ARCHITECTURE.md) "Option B" and
    // andreasaristidou.com/FABRIK for the surrounding design context.
    // update_serpent_movement / parse_serpent_parts moved to
    // logosphere::animation::SerpentLocomotion in B19.
    // update_butterfly_flight / parse_butterfly_parts moved to
    // logosphere::animation::ButterflyFlight in B20.

    // Helper: Normalize angle to [-PI, PI]
    float normalize_angle(float angle) const;

    // System references
    Engine* engine_;
    ParticleSystem* particle_system_;
    CoordinateTransformer* coord_transformer_;
    InputSystem* input_system_;
    kg::KGModule* kg_;

    // Configuration
    DynamicsConfig config_;
    bool is_initialized_;

    // Active entities with behaviors
    std::vector<HumanoidParts> humanoid_look_at_entities_;
    // serpent_entities_ moved to SerpentLocomotion::Impl in B19.
    // butterfly_entities_ moved to ButterflyFlight::Impl in B20.

    // Telemetry lives in PhysicsSystem now (forwarded via get_telemetry())

    // Creature callbacks (autonomous NPCs)
    std::vector<CreatureCallback> creature_callbacks_;

    // Metrics
    DynamicsMetrics metrics_;

    // Material friction lookup (see docs/entity_dynamics.md "Discovery 2")
    // Maps (material_a, material_b) → friction coefficient
    // Will be populated from KG at init: kg_->findByRelationType("FRICTION_WITH")
    // Hot-path lookup: O(1) hash table, no KG queries per frame
    struct MaterialPair {
        std::string mat_a;
        std::string mat_b;
        bool operator==(const MaterialPair& other) const;
    };
    struct MaterialPairHash {
        size_t operator()(const MaterialPair& pair) const;
    };
    std::unordered_map<MaterialPair, float, MaterialPairHash> friction_table_;

    // Get friction coefficient between two materials
    // Returns default if pair not found in table
    float get_friction(const std::string& particle_material, const std::string& ground_material) const;
};

#endif // PARTICLE_DYNAMICS_SYSTEM_H
