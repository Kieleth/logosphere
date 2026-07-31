#include "logosphere/dynamics/particle_dynamics_system.h"
#include "engine.h"
#include "particle_system.h"
#include "input_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/physics/physics_solver.h"  // For GRAVITY constant
#include "logosphere/physics/physics_system.h"  // For CollisionEvent
#include "logosphere/physics/narrow_phase.h"  // For swept_aabb_vs_aabb (CCD for FK)
#include "logosphere/physics/bvh.h"          // For BVH spatial queries (step climbing lookahead)
#include "logosphere/dynamics/two_bone_ik.h"  // For foot planting IK
#include "logosphere/dynamics/animation_primitives.h"  // For auto-registered default clips
#include <cmath>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <unordered_set>
#include <queue>
#include <set>
#include <cassert>

// Static debug flag for FK arm diagnostics
int ParticleDynamicsSystem::fk_arm_debug_frames = 0;

// Debug guard: detect nested write lock acquisition (causes silent deadlock).
// std::shared_mutex is NOT recursive — same-thread re-acquire freezes forever.
#ifndef NDEBUG
thread_local int particles_write_lock_depth = 0;
#endif

// =============================================================================
// STEP CLIMBING CONSTANTS (BVH Lookahead Approach)
// =============================================================================
// Anticipate steps BEFORE collision by querying BVH ahead of movement.
// Real humans see steps and lift their legs - they don't bump and climb.
// =============================================================================
constexpr float MAX_STEP_HEIGHT = 0.4f;          // 40cm - max climbable step
constexpr float MIN_STEP_HEIGHT = 0.05f;         // 5cm - ignore tiny bumps
constexpr float MAX_STEP_BOOST = 4.0f;           // Cap boost velocity (m/s) - increased for reliability
constexpr float LOOKAHEAD_DIST = 0.8f;           // How far ahead to look (m) - increased for early detection
constexpr float LOOKAHEAD_WIDTH = 0.8f;          // Query box width (m) - wider for reliability
constexpr float MIN_MOVEMENT_SPEED = 0.1f;       // Don't check if barely moving

ParticleDynamicsSystem::ParticleDynamicsSystem()
    : engine_(nullptr)
    , particle_system_(nullptr)
    , coord_transformer_(nullptr)
    , input_system_(nullptr)
    , kg_(nullptr)
    , is_initialized_(false)
{
}

ParticleDynamicsSystem::~ParticleDynamicsSystem() {
    shutdown();
}

bool ParticleDynamicsSystem::initialize_headless(ParticleSystem& particle_system,
                                                 const DynamicsConfig& config) {
    if (is_initialized_) {
        std::cerr << "[ParticleDynamicsSystem] Already initialized" << std::endl;
        return false;
    }
    engine_ = nullptr;
    particle_system_ = &particle_system;
    coord_transformer_ = nullptr;
    input_system_ = nullptr;
    kg_ = nullptr;
    config_ = config;
    is_initialized_ = true;
    std::cout << "[ParticleDynamicsSystem] Initialized (headless mode)" << std::endl;
    return true;
}

bool ParticleDynamicsSystem::initialize(Engine* engine, const DynamicsConfig& config) {
    if (is_initialized_) {
        std::cerr << "[ParticleDynamicsSystem] Already initialized" << std::endl;
        return false;
    }

    if (!engine) {
        std::cerr << "[ParticleDynamicsSystem] Invalid engine pointer" << std::endl;
        return false;
    }

    engine_ = engine;
    particle_system_ = &engine->get_particle_system();
    coord_transformer_ = &engine->get_coord_transformer();
    input_system_ = &engine->get_input_system();
    kg_ = &engine->get_kg();
    config_ = config;
    is_initialized_ = true;

    // Refactor B21: humanoid body-part health subscription moved to
    // HumanoidLocomotion::initialize.

    std::cout << "[ParticleDynamicsSystem] Initialized successfully" << std::endl;
    return true;
}

Logosphere::PhysicsTelemetry& ParticleDynamicsSystem::get_telemetry() {
    return engine_->get_physics_system().get_telemetry();
}

const Logosphere::PhysicsTelemetry& ParticleDynamicsSystem::get_telemetry() const {
    return engine_->get_physics_system().get_telemetry();
}

void ParticleDynamicsSystem::shutdown() {
    if (!is_initialized_) return;

    humanoid_look_at_entities_.clear();
    is_initialized_ = false;

    std::cout << "[ParticleDynamicsSystem] Shutdown" << std::endl;
}

void ParticleDynamicsSystem::update(double delta_time) {
    if (!is_initialized_) return;

    reset_metrics();
    metrics_.tracked_entities = static_cast<int>(
        humanoid_look_at_entities_.size()
        + (engine_ ? engine_->get_serpent_locomotion().entity_count() : 0)
        + (engine_ ? engine_->get_butterfly_flight().entity_count() : 0));

    // Reset the tracer's frame counter once per dynamics step. Write-site
    // records captured during this and the subsequent post_physics pass all
    // stamp with the same frame number, preserving cross-system ordering.
    if (engine_) {
        int fnum = engine_->get_renderer().current_frame_number();
        engine_->get_particle_tracer().begin_frame(fnum);
    }

    // Invoke creature callbacks (autonomous NPCs run their AI here)
    // This happens BEFORE physics so creatures can set velocities for integration
    for (auto& callback : creature_callbacks_) {
        callback(static_cast<float>(delta_time), *particle_system_, *this);
    }

    // Get current input target entity (for serpent movement)
    kg::EntityID input_target = kg::INVALID_ENTITY;
    if (engine_) {
        input_target = engine_->get_input_target_entity();
    }

    // Refactor B15: humanoid pre-physics work (look-at + walk_cycle +
    // yaw cascade state + gravity + animation velocities) lives in
    // logosphere::animation::HumanoidLocomotion::update_pre_physics.
    // Engine calls it right after this update().

    // Refactor B19/B20: serpent + butterfly per-frame work lives in their
    // own modules (SerpentLocomotion, ButterflyFlight). Engine drives both
    // right after this update().
    (void)delta_time;
}

void ParticleDynamicsSystem::update_post_physics(double /*delta_time*/) {
    // Refactor B14: humanoid post-physics loop now lives in
    // logosphere::animation::HumanoidLocomotion::update_post_physics.
    // Engine calls both for now; this body is intentionally empty.
}

void ParticleDynamicsSystem::notify_particle_swap(size_t old_idx, size_t new_idx) {
    // When ParticleSystem swap-removes a particle, the particle that was at
    // old_idx has moved to new_idx. Any cached particle IDs in our entity
    // tracking structs need to be updated so subsequent updates write to the
    // correct particles.
    auto fix = [&](unsigned int& id) {
        if (id == (unsigned int)old_idx) id = (unsigned int)new_idx;
    };
    auto fix_int = [&](int& id) {
        if (id == (int)old_idx) id = (int)new_idx;
    };
    auto fix_vec = [&](std::vector<unsigned int>& v) {
        for (auto& id : v) fix(id);
    };
    auto fix_vec_int = [&](std::vector<int>& v) {
        for (auto& id : v) fix_int(id);
    };

    for (auto& parts : humanoid_look_at_entities_) {
        fix(parts.head); fix(parts.neck); fix(parts.torso);
        fix(parts.abdomen); fix(parts.hips);
        fix_vec(parts.left_leg_particles);
        fix_vec(parts.right_leg_particles);
        fix_vec(parts.left_arm_particles);
        fix_vec(parts.right_arm_particles);
        fix_vec(parts.head_child_particles);
        fix_vec(parts.all_particle_indices);
        // KinematicRoot caches a particle id (the stance foot). Chunk
        // streaming shuffles indices; without this fix the id silently
        // aliases to whatever particle ended up at the old slot, and any
        // downstream "stance foot at anchor" computation reads wrong
        // coordinates.
        fix(parts.root.particle_id);
        fix(parts.root.previous_particle_id);
        // Foot-plant pin anchors: persistent per-foot particles plus the
        // engaged-anchor id. A stale id makes the per-frame pin lookup
        // miss ([PIN_LOST]) and the stance foot drifts.
        fix_int(parts.plant_anchor_particle_id);
        fix_int(parts.left_plant_anchor_id);
        fix_int(parts.right_plant_anchor_id);
        // Ground-probe cache: a stale id makes step anticipation read
        // support height from whatever particle landed in the old slot.
        fix_int(parts.cached_ground_particle_id);
        // Joint hierarchy caches particle IDs for FK parent/child lookup.
        // Without this fix, after a chunk swap apply_fk_transforms walks
        // the hierarchy using stale IDs, reads parent pose from whatever
        // random particle landed at the old slot, and writes the child
        // pose to another random particle — while Eva's actual upper-body
        // particles retain their stale ANIMATION ownership (set by FK
        // before the swap), so snap_to_hips keeps skipping them. Net:
        // chest / neck / shoulders stop tracking hips and drift behind
        // by velocity*dt per frame until the deep-probe pair-separation
        // threshold fires. See tests/test_joint_hierarchy_swap_integrity.cpp:
        // without this fix the test sees 6–10 m drift; with it, 3 cm wiggle.
        for (auto& joint : parts.joint_hierarchy.joints) {
            fix(joint.parent_particle);
            fix(joint.child_particle);
        }
    }

    if (engine_) {
        engine_->get_serpent_locomotion().notify_particle_swap(old_idx, new_idx);
        engine_->get_butterfly_flight().notify_particle_swap(old_idx, new_idx);
    }
}

void ParticleDynamicsSystem::register_creature_callback(CreatureCallback callback) {
    creature_callbacks_.push_back(std::move(callback));
}

void ParticleDynamicsSystem::clear_creature_callbacks() {
    creature_callbacks_.clear();
}

void ParticleDynamicsSystem::unregister_entity(kg::EntityID entity_id) {
    auto it = std::remove_if(
        humanoid_look_at_entities_.begin(),
        humanoid_look_at_entities_.end(),
        [entity_id](const HumanoidParts& parts) {
            return parts.entity_id == entity_id;
        }
    );
    humanoid_look_at_entities_.erase(it, humanoid_look_at_entities_.end());

    if (engine_) {
        engine_->get_serpent_locomotion().unregister_entity(entity_id);
        engine_->get_butterfly_flight().unregister_entity(entity_id);
    }
}





// ============================================================================
// LOCOMOTION: All-Particle Velocity Ramping
// ============================================================================
// See docs/physics_locomotion.md for design rationale
//
// Key insight: Apply velocity to ALL entity particles simultaneously.
// Gluons see all velocities equal → nothing to correct → no drift.
//
// Velocity ramping creates momentum feel:
// - Never set velocity instantly
// - Accelerate/decelerate with limits from FORGE attributes
// - Changing direction counts as deceleration
// ============================================================================



float ParticleDynamicsSystem::normalize_angle(float angle) const {
    while (angle > M_PI) angle -= 2.0f * M_PI;
    while (angle < -M_PI) angle += 2.0f * M_PI;
    return angle;
}

// ============================================================================
// MATERIAL FRICTION LOOKUP
// ============================================================================

bool ParticleDynamicsSystem::MaterialPair::operator==(const MaterialPair& other) const {
    // Friction is symmetric: (A, B) == (B, A)
    return (mat_a == other.mat_a && mat_b == other.mat_b) ||
           (mat_a == other.mat_b && mat_b == other.mat_a);
}

size_t ParticleDynamicsSystem::MaterialPairHash::operator()(const MaterialPair& pair) const {
    // Symmetric hash: hash(A, B) == hash(B, A)
    // Use XOR so order doesn't matter
    std::hash<std::string> hasher;
    return hasher(pair.mat_a) ^ hasher(pair.mat_b);
}

float ParticleDynamicsSystem::get_friction(const std::string& particle_material, const std::string& ground_material) const {
    // Material-based friction lookup (see docs/entity_dynamics.md "Discovery 2")
    // Architecture:
    // 1. Define materials in KG: kg_->createEntity("material_skin"), etc.
    // 2. Store friction coefficients as relations:
    //    kg_->createRelation(skin_mat, "FRICTION_WITH", ice_mat)
    //    kg_->setProperty(relation, "coefficient", "0.05")
    // 3. Load friction_table_ at init from KG (not yet implemented)
    // 4. Detect ground material at contact point (raycast or heightmap)
    // 5. Lookup from table: O(1), no KG queries per frame

    MaterialPair pair = {particle_material, ground_material};
    auto it = friction_table_.find(pair);

    if (it != friction_table_.end()) {
        return it->second;  // Found in table
    }

    // Default friction for unknown material pairs
    return 0.8f;  // Reasonable default (concrete on rubber)
}

// ============================================================================
// REUSABLE BEHAVIOR PRIMITIVES
// ============================================================================

float ParticleDynamicsSystem::calculate_sinusoidal_offset(float phase, float amplitude, float phase_offset) const {
    // Primitive: Sinusoidal offset calculation
    // Formula: amplitude × sin(phase + phase_offset)
    //
    // Usage examples:
    // - Wing beats: calculate_sinusoidal_offset(wing_phase, 0.08, 0.0) for left wing
    // - Wing beats (opposite): calculate_sinusoidal_offset(wing_phase, 0.08, M_PI) for right wing
    // - Limb swings: calculate_sinusoidal_offset(walk_phase, 0.15, 0.0) for legs
    // - Breathing: calculate_sinusoidal_offset(breath_phase, 0.01, 0.0) for chest expansion
    return amplitude * sinf(phase + phase_offset);
}

bool ParticleDynamicsSystem::check_random_probability(float chance_per_second, double delta_time) const {
    // Primitive: Random probability check for state transitions
    // Returns true with probability = (chance_per_second × delta_time)
    //
    // Example: chance_per_second=0.1 (10% per second), delta_time=0.016 (60 FPS)
    // → probability = 0.1 × 0.016 = 0.0016 (0.16% per frame)
    // → Over 1 second (60 frames): 1 - (1 - 0.0016)^60 ≈ 9.4% (close to 10%)
    //
    // Usage: if (check_random_probability(0.1, dt)) start_gliding();
    float probability = chance_per_second * static_cast<float>(delta_time);
    float random_value = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return random_value < probability;
}

float ParticleDynamicsSystem::smooth_turn_toward(float current_direction, float target_direction, float max_turn_rate, double delta_time) const {
    // Primitive: Smooth direction turning
    // Gradually adjusts current_direction toward target_direction
    // max_turn_rate in radians per second
    //
    // Usage: new_dir = smooth_turn_toward(current_dir, target_dir, M_PI, dt);  // 180°/sec max turn

    // Calculate shortest angular distance
    float angle_diff = target_direction - current_direction;
    angle_diff = normalize_angle(angle_diff);

    // Clamp to max turn rate
    float max_turn = max_turn_rate * static_cast<float>(delta_time);
    if (fabsf(angle_diff) <= max_turn) {
        // Can reach target this frame
        return target_direction;
    } else {
        // Turn toward target at max rate
        return current_direction + (angle_diff > 0 ? max_turn : -max_turn);
    }
}


// =============================================================================
// Transform-Based FK (Quaternion/Matrix System)
// =============================================================================
// Clean FK using transform composition: world = parent * local
// Matrix multiplication handles rotation composition automatically.
// No more rotation order bugs (Y-X-Z vs X-Y-Z).
// =============================================================================


// ============================================================================
// POST-FK CHAIN PROJECTION — Option B from the kinematic-root design (docs/ARCHITECTURE.md)
// ============================================================================
// apply_fk_transforms writes each joint's clean FK target into
// joint.world_transform.position, THEN runs a per-joint collide-and-slide
// that mutates the particle independently. For a kinematic chain, sliding
// segment N without sliding N+1 breaks the |parent-child| invariant —
// proven via tracer dumps to produce 0.5 m+ drift on swing-leg foot↔toe
// during heel-strike on floor tiles.
//
// This projection restores chain integrity by writing each animated
// particle to its pre-slide world_transform.position, in parent-first
// order (joints are stored that way). Equivalent in effect to disabling
// collide-and-slide, but keeps the slide running as a *diagnostic* (tracer
// still records what would have slid), which future C-path work can
// reintroduce as a chain-aware contact solver.
//
// Leaves the stance leg alone — foot_planting_IK runs after this and has
// its own chain rebuild targeting plant_target.
// ============================================================================
