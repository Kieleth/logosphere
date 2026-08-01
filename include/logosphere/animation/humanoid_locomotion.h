#ifndef LOGOSPHERE_ANIMATION_HUMANOID_LOCOMOTION_H
#define LOGOSPHERE_ANIMATION_HUMANOID_LOCOMOTION_H

// Humanoid locomotion subsystem.
//
// Owns all humanoid-specific animation policy: HumanoidParts struct,
// look-at + yaw cascade, walk / strafe / turn / idle clips, FK
// transforms, foot-planting IK, semantic joint API
// (set_joint_flex/abduct/twist), physics-drive routing into gluons.
//
// Lives outside src/core/ on purpose: dynamics is generic-mechanism
// only (integration hooks, particle-swap fanout, telemetry forwarding),
// and game-shaped policy belongs here. See the rehaul plan in
// the kinematic-root design (docs/ARCHITECTURE.md).
//
// Module shape mirrors src/animation/humanoid_locomotion.cpp;
// instantiated by Engine alongside ParticleDynamicsSystem. Subscribers
// register against ParticleDynamicsSystem callbacks (creature, particle
// swap) so dynamics holds no back-reference to this module.
//
// B0 scaffolding: header + cpp pair + CMake wiring + Engine
// instantiation. No behavior moved yet — that comes in B1+.
//
// ============================================================================
// Reference design
// ============================================================================
// The Phase 4b mechanism (pin-gluon foot plant, IK→gluon publish) was
// re-ported here after the refactor. It used to gate behind
// `use_physics_drive_legs`; Phase 5 made it the default for every
// humanoid (V4.11 substepping closed the bias-stability gap that
// kept the original opt-in alive). Original work parked on
// archive/physics-drive-2026-04.

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/particle_system.h"                     // ParticleSystem::WriteView
#include "logosphere/animation/joint_hierarchy.h"     // Joint, JointHierarchy
#include "logosphere/capability/capability_profile.h" // CapabilityProfile
#include "logosphere/capability/dynamics_params.h"
#include "logosphere/dynamics/animation_controller.h" // AnimationController
#include "logosphere/dynamics/animation_types.h"      // FKAnimationClip
#include "logosphere/dynamics/kinematic_root.h"       // logosphere::KinematicRoot
#include "logosphere/kg/kg_types.h"

class Engine;
class ParticleSystem;
class ParticleDynamicsSystem;
class PhysicsSystem;
class ParticleTracer;
namespace kg { class KGModule; }
namespace logosphere { struct Quat; }

namespace logosphere::animation {

// Humanoid body parts (parsed from KG).
//
// Owns per-entity state for one humanoid: body-part particle ids, yaw
// cascade phase, walk / strafe / turn / idle clip state, foot-planting
// IK pose, semantic joint hierarchy, capability + dynamics params,
// kinematic root anchor.
//
// Currently allocated and iterated by ParticleDynamicsSystem; the
// rehaul will move state ownership into HumanoidLocomotion::Impl in a
// later commit. Here we only relocate the type definition so dynamics
// and animation modules can both name it.
struct HumanoidParts {
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    // True after register_humanoid_direct (or parse_humanoid_parts)
    // populated this struct. Replaces the historical
    // `parts.hips == 0` / `parts.head == 0` "is this entry valid?"
    // sentinel, which collided with legitimate particle index 0.
    // Per-frame guards check `!registered` to skip uninitialized
    // entries.
    bool registered = false;
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

    // Phase 4b — pin-gluon foot plant lifecycle. Each foot owns ONE
    // persistent KINEMATIC anchor particle, created lazily on that
    // foot's first plant and MOVED to plant_target on every later
    // plant. (Per-strike create/delete fed the deferred-deletion queue
    // every 2-3 frames at low FPS; each flush costs a synchronous GPU
    // wait plus a full BVH rebuild — Eden RCA 2026-07-13.) Anchors are
    // invisible (tiny, transparent, is_light_source so they skip mass
    // validation and the BVH) and serve only as the far end of a
    // zero-length pin gluon to the stance foot. The engaged pin is the
    // gluon's EXISTENCE — the V4 solver builds gluon rows hard and
    // ignores stiffness — so plant/release = add/remove the gluon,
    // never the anchor particle.
    int left_plant_anchor_id  = -1;     // persistent, lazily created
    int right_plant_anchor_id = -1;     // persistent, lazily created
    int plant_anchor_particle_id = -1;  // engaged anchor (-1 = no plant)

    // Deferred pin-gluon ops. Gluon add/removal and anchor spawn both
    // take locks that update_post_physics already holds for the whole
    // humanoid loop. We accumulate ops here, then
    // HumanoidLocomotion::flush_pending_pin_gluon_ops drains them after
    // the lock is released — always within the same update, so the raw
    // ids below cannot go stale (no particle removal happens between
    // emit and drain).
    struct PinGluonOp {
        enum Kind { ENGAGE, DISENGAGE };
        Kind kind = DISENGAGE;
        int release_anchor_id = -1;     // engaged anchor whose pin gluon to release (-1 = none)
        bool foot_is_right = false;     // ENGAGE: which foot plants
        unsigned int foot_id = 0;       // ENGAGE: foot particle the anchor pins
        float tx = 0.0f;                // ENGAGE: plant_target world pos
        float ty = 0.0f;
        float tz = 0.0f;
    };
    std::vector<PinGluonOp> pending_pin_ops;

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
    bool is_volitional = false;        // Is movement intentional (game must set via set_volitional())
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

// HumanoidLocomotion is the destination for humanoid policy currently
// living inside ParticleDynamicsSystem. Until the rehaul completes
// (see the kinematic-root design (docs/ARCHITECTURE.md)), the implementations here delegate
// back to dynamics so callers can adopt the new API surface ahead of
// state ownership migrating.
//
// During B1 (this commit): the public registration API is exposed
// on HumanoidLocomotion; impls delegate to dynamics. New code should
// prefer this API. Existing test/example call sites stay on the old
// surface and will migrate during B8.
class HumanoidLocomotion {
public:
    HumanoidLocomotion();
    ~HumanoidLocomotion();

    HumanoidLocomotion(const HumanoidLocomotion&) = delete;
    HumanoidLocomotion& operator=(const HumanoidLocomotion&) = delete;

    // Production initialize — pulls all subsystem refs out of Engine.
    bool initialize(Engine* engine);

    // Headless initialize — for test harnesses that exercise locomotion
    // without spinning up the full Engine (no GLFW / Metal). Pass each
    // subsystem ref directly. EventBus omitted in headless: no body-part
    // health subscription is wired up; the test drives recompute_capability
    // explicitly if needed.
    bool initialize_headless(ParticleSystem& particle_system,
                             PhysicsSystem& physics_system,
                             kg::KGModule& kg_module,
                             ParticleDynamicsSystem& dynamics_system,
                             ParticleTracer& tracer);

    void shutdown();
    void update_pre_physics(double delta_time);
    void update_post_physics(double delta_time);

    // ------------------------------------------------------------------
    // Registration API (B1 surface). Mirrors ParticleDynamicsSystem's
    // humanoid registration. Currently delegates; B2 will host the
    // real impl here and dynamics will become the delegator.
    // ------------------------------------------------------------------

    // Register humanoid for animations using particle IDs directly.
    // reflexes_ms and grit_W are physical inputs used to compute
    // default dynamics. Pass custom_dynamics to override the default
    // derivation entirely.
    void register_humanoid_direct(
        int hips_id,
        const std::vector<int>& left_leg_ids,
        const std::vector<int>& right_leg_ids,
        const std::vector<int>& left_arm_ids,
        const std::vector<int>& right_arm_ids,
        const std::vector<int>& torso_ids = {},
        float reflexes_ms = 250.0f,
        float grit_W = 500.0f,
        kg::EntityID entity_id = kg::INVALID_ENTITY,
        const DynamicsParams* custom_dynamics = nullptr);

    // Register entity for KG-driven look-at behaviour.
    void register_humanoid_look_at(kg::EntityID entity_id);

    // Unregister humanoid by hips particle id (for physics-based
    // humanoids without a KG entity).
    void unregister_humanoid(int hips_id);

    // Reset humanoid position tracking (call after teleporting /
    // resetting position) — updates prev_world_x/y so the walk cycle
    // doesn't think the entity teleported.
    void reset_humanoid_position(int hips_id);

    // Return all animated particles to DYNAMICS ownership +
    // KINEMATIC solver mode after an animation completes. Acquires
    // a write lock — callers must not be inside one already.
    void reset_animation_owners(kg::EntityID entity_id);

    // ------------------------------------------------------------------
    // Semantic joint API (B2 surface). Anatomical motion commands —
    // FK resolves these to local axes via the joint's JointDefinition.
    // Returns false if joint not found or motion type unsupported.
    // ------------------------------------------------------------------

    // Direct rotation channel API (DRIVEN mode). rotation_y is the
    // legacy single-axis convention; _x / _z handle compound poses.
    bool set_joint_angle(kg::EntityID entity_id,
                         const std::string& joint_name, float angle);
    bool set_joint_rotation_x(kg::EntityID entity_id,
                              const std::string& joint_name, float angle);
    bool set_joint_rotation_y(kg::EntityID entity_id,
                              const std::string& joint_name, float angle);
    bool set_joint_rotation_z(kg::EntityID entity_id,
                              const std::string& joint_name, float angle);
    bool set_joint_rotation(kg::EntityID entity_id,
                            const std::string& joint_name,
                            float rotation_x, float rotation_y, float rotation_z);

    // Mode-based legacy API. set_joint_target is deprecated, kept for
    // existing callers. set_joint_relax flips the joint into PHYSICS
    // mode; set_joint_rigid flips it to INHERIT.
    bool set_joint_target(kg::EntityID entity_id,
                          const std::string& joint_name,
                          JointMode mode, float angle,
                          const logosphere::Vec3& direction);
    bool set_joint_relax(kg::EntityID entity_id,
                         const std::string& joint_name);
    bool set_joint_rigid(kg::EntityID entity_id,
                         const std::string& joint_name);
    JointMode get_joint_mode(kg::EntityID entity_id,
                             const std::string& joint_name) const;

    bool set_joint_flex(kg::EntityID entity_id,
                        const std::string& joint_name, float angle);
    bool set_joint_abduct(kg::EntityID entity_id,
                          const std::string& joint_name, float angle);
    bool set_joint_twist(kg::EntityID entity_id,
                         const std::string& joint_name, float angle);
    bool clear_joint_semantic_targets(kg::EntityID entity_id,
                                      const std::string& joint_name);
    bool get_joint_twist(int hips_id, const char* joint_name,
                         float& angle_out) const;

    // Single joint physics-drive (Z-axis target). Flips the gluon
    // into PD-controller mode pulling rotation_z toward the target.
    bool set_joint_physics_drive(kg::EntityID entity_id,
                                 const std::string& joint_name,
                                 float target_z_rotation,
                                 float stiffness = 200.0f,
                                 float damping = 12.0f);

    // 3-axis quaternion variant for ball-socket targets.
    bool set_joint_physics_drive_q(kg::EntityID entity_id,
                                   const std::string& joint_name,
                                   const logosphere::Quat& target_q,
                                   float stiffness = 200.0f,
                                   float damping = 12.0f);

    // ------------------------------------------------------------------
    // Joint hierarchy + FK invocation (B10). register_joint installs a
    // joint into the entity's hierarchy and caches its gluon offsets;
    // derive_joints_from_gluons builds a BFS-ordered hierarchy from the
    // physics topology; apply_entity_fk runs FK end-to-end for one
    // entity (used by tests + tools that drive a frame outside the
    // standard update loop).
    // ------------------------------------------------------------------

    void register_joint(kg::EntityID entity_id, const Joint& joint);

    std::vector<Joint> derive_joints_from_gluons(
        const std::vector<unsigned int>& entity_particles,
        unsigned int root_particle);

    void apply_entity_fk(kg::EntityID entity_id);

    // ------------------------------------------------------------------
    // Look-at API (B7). Custom NPC look-at target overrides the default
    // (mouse-driven) input; FK spine-distributed look-at writes joint
    // twist angles for head/neck/upper_spine/lower_spine.
    // ------------------------------------------------------------------

    void set_look_at_target(int hips_id, float target_x, float target_y);
    void clear_look_at_target(int hips_id);
    void set_spine_look_at(unsigned int hips_id, float target_world_x, float target_world_y);

    // ------------------------------------------------------------------
    // Locomotion controls (B8). Movement state, velocity targets,
    // facing, grapple / lying-down lifecycle, capability queries.
    // All identify the humanoid by hips particle id.
    // ------------------------------------------------------------------

    // Phase 4b introspection: live anchor particle id for the humanoid
    // whose hips particle is hips_id. -1 when no plant is active.
    // Tests use this to assert pin-gluon lifecycle transitions.
    int get_plant_anchor_particle_id(int hips_id) const;

    // Volitional movement state. When true: friction disabled for
    // smooth walking. When false: passive drift, friction applies.
    void set_volitional(int hips_id, bool volitional);

    // Facing rotation around Z. Sets base_rotation and rotates
    // every particle in the humanoid to match.
    void set_facing_direction(int hips_id, float rotation_z);

    // Grapple lifecycle. While grappled, volitional movement is
    // blocked and target velocity is zeroed.
    void set_grappled(int hips_id, bool grappled);
    bool is_grappled(int hips_id) const;

    // Speed modifier in [0, 1] for status effects (damage, low HP).
    void set_speed_modifier(int hips_id, float modifier);

    // Recompute capability profile from KG body graph and
    // re-derive dynamics params.
    void recompute_capability(kg::EntityID entity_id);

    // Capability + dynamics queries.
    float get_max_walk_speed(int hips_id) const;
    float get_max_run_speed(int hips_id) const;
    float get_effective_max_speed(int hips_id) const;  // run × speed_modifier
    float get_entity_mass(int hips_id) const;

    // Lying-down pose. Lying = teleport into a flat-on-ground spread,
    // grapple-locked. Standing back up = restore hips height from
    // rest_offsets and snap_to_hips re-fluffs the body next frame.
    void set_lying_down(int hips_id, bool lying);
    bool is_lying_down(int hips_id) const;

    // World-space target velocity (m/s, X+Y world axes).
    // update_locomotion ramps current velocity toward this each frame.
    void set_target_velocity(int hips_id, float target_vx, float target_vy);

    // Body-relative target velocity (m/s, forward + right axes).
    // The walk cycle resolves world-space target each frame from
    // base_rotation + (forward, right).
    void set_body_relative_velocity(int hips_id, float forward, float right);

    // Hard reset: zero every particle's velocity + internal tracking.
    void zero_all_velocities(int hips_id);

    // Instantaneous velocity delta to every particle in the humanoid.
    void apply_impulse(int hips_id, float velocity_x, float velocity_y);

    // Player-triggered vertical (Z) impulse, gated by on-ground state --
    // the command-driven counterpart to anticipate_step_climbing's
    // automatic obstacle-clearing boost, reusing the same "boost hips +
    // legs, let gravity + the FK chain carry the rest of the body" shape
    // and the same v = sqrt(2*g*h) arc physics. jump_height is the desired
    // apex in meters; the resulting velocity is capped (see .cpp) so this
    // stays a deliberate hop, not a launch. No-op (returns false) if the
    // humanoid isn't currently grounded.
    bool try_jump(int hips_id, float jump_height = 1.0f);

    // On-ground test: is there stable support directly beneath this
    // humanoid's feet right now? Duplicates (rather than shares) the
    // support-query logic anticipate_step_climbing/apply_entity_gravity
    // use internally, so callers get an honest independent answer without
    // this becoming another caller of those functions' internals.
    bool is_grounded(int hips_id) const;

    // Read accessors.
    float get_base_rotation(int hips_id) const;
    float get_walk_phase(int hips_id) const;
    float get_current_run_blend(int hips_id) const;

    // ------------------------------------------------------------------
    // Animation playback (B9). Position-based AnimationClip controller
    // and the parallel FK-clip system (walk / strafe / turn / idle /
    // run + ad-hoc named clips). The two paths are mutually exclusive
    // per humanoid: starting one stops the other.
    // ------------------------------------------------------------------

    bool register_animation(int hips_id, const std::string& name,
                            const AnimationClip& clip);
    bool play_animation(int hips_id, const std::string& name);

    bool register_fk_animation(int hips_id, const std::string& name,
                               const FKAnimationClip& clip);
    bool play_fk_animation(int hips_id, const std::string& name);
    bool is_fk_animation_playing(int hips_id) const;

    void register_walk_clips(int hips_id,
                             const FKAnimationClip& right_step,
                             const FKAnimationClip& left_step);
    void register_strafe_clips(int hips_id,
                               const FKAnimationClip& right_step,
                               const FKAnimationClip& left_step);
    void register_turn_clips(int hips_id,
                             const FKAnimationClip& right_step,
                             const FKAnimationClip& left_step);
    void register_idle_clip(int hips_id, const FKAnimationClip& clip);
    void register_run_clips(int hips_id,
                            const FKAnimationClip& right_step,
                            const FKAnimationClip& left_step);

    void set_foot_planting(int hips_id, bool enabled);

    // Set rest position for a particle (rest pose animation reference).
    // Converts the world-space target to body-local coordinates.
    void set_rest_position(int hips_id, unsigned int particle_id,
                           float x, float y, float z);

    // Per-frame LOCAL coordinate logging on arm particles for arm-pose
    // verification.
    void enable_humanoid_diagnostics(int hips_id, bool enable);

private:
    // Phase 4b — drain pending_pin_ops on every HumanoidParts. Spawns
    // KINEMATIC anchor particles + zero-length pin gluons for CREATE
    // ops, removes gluons + queues anchor deletion for DESTROY ops.
    // Must be called AFTER the ParticleSystem write lock from
    // update_post_physics is released (it takes its own locks).
    void flush_pending_pin_gluon_ops();

    // Parse body-part particle ids + offsets from KG entity properties.
    // Returns false when the head particle is missing — humanoid won't
    // get registered and the caller logs.
    bool parse_humanoid_parts(kg::EntityID entity_id, HumanoidParts& out_parts);

    // Per-frame helpers (called from update_pre_physics / update_post_physics).
    // Each grabs an `auto& dyn = ...` ref once for friend access to dynamics
    // state still living there (config_, metrics_, normalize_angle, the
    // humanoid_look_at_entities_ container).
    void update_yaw_cascade_state(HumanoidParts& parts,
                                  float target_world_x, float target_world_y,
                                  double delta_time);
    void apply_yaw_cascade_rotations(HumanoidParts& parts,
                                     ParticleSystem::WriteView& particles);
    void update_humanoid_look_at(HumanoidParts& parts,
                                 float target_world_x, float target_world_y,
                                 double delta_time);
    void update_walk_cycle(HumanoidParts& parts, double delta_time);
    void update_locomotion(HumanoidParts& parts, double delta_time,
                           ParticleSystem::WriteView& particles_view,
                           const char* phase);
    void apply_motor_forces(HumanoidParts& parts, double delta_time,
                            ParticleSystem::WriteView& particles_view);
    void set_animation_velocities_pre_physics(HumanoidParts& parts,
                                              double delta_time,
                                              ParticleSystem::WriteView& particles_view);
    void apply_fk_pose_targets(HumanoidParts& parts, const RotationPose& pose);
    void publish_physics_drive_targets(HumanoidParts& parts);
    void anticipate_step_climbing(HumanoidParts& parts,
                                  ParticleSystem::WriteView& particles);
    void apply_entity_gravity(HumanoidParts& parts, float dt,
                              ParticleSystem::WriteView& particles);
    void maintain_entity_shape(HumanoidParts& parts,
                               ParticleSystem::WriteView& particles, float dt);
    void handle_collision_events(HumanoidParts& parts,
                                 ParticleSystem::WriteView& particles);
    void apply_fk_transforms(HumanoidParts& parts,
                             ParticleSystem::WriteView& particles);
    void project_chain_geometry(HumanoidParts& parts,
                                ParticleSystem::WriteView& particles);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace logosphere::animation

#endif  // LOGOSPHERE_ANIMATION_HUMANOID_LOCOMOTION_H
