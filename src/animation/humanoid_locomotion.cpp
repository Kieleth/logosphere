// Humanoid locomotion subsystem — see header for the design intent.
//
// B0 (this file): minimal scaffolding only. Compiles, links, runs as a
// no-op. The actual move of HumanoidParts + locomotion logic out of
// ParticleDynamicsSystem starts in B1.

#include "logosphere/animation/humanoid_locomotion.h"

#include "core/engine.h"
#include "core/joint_types.h"
#include "core/particle_system.h"
#include "core/particle_tracer.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "logosphere/dynamics/animation_types.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/dynamics/two_bone_ik.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/physics/bvh.h"
#include "logosphere/physics/narrow_phase.h"
#include "logosphere/physics/physics_solver.h"
#include "logosphere/physics/physics_system.h"
#include "math/quat.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>

namespace logosphere::animation {

// Forward decls — definitions follow below. register_humanoid_direct
// calls both to default-on the physics-drive path:
//   legs       — Phase 5 (kinematic-root + pin-gluon foot plant + IK→gluon publish)
//   upper body — Phase E (gluon angular drive on arms / spine / neck / head)
static void apply_physics_drive_legs_init(
    HumanoidParts& parts,
    ParticleSystem& ps,
    PhysicsSystem& physics);
static void apply_physics_drive_upper_body_init(
    HumanoidParts& parts,
    ParticleSystem& ps,
    PhysicsSystem& physics);

// Step climbing constants — duplicated from particle_dynamics_system.cpp.
// Used by anticipate_step_climbing().
namespace {
constexpr float MAX_STEP_HEIGHT = 0.4f;
constexpr float MIN_STEP_HEIGHT = 0.05f;
constexpr float MAX_STEP_BOOST = 4.0f;
constexpr float LOOKAHEAD_DIST = 0.8f;
constexpr float LOOKAHEAD_WIDTH = 0.8f;
constexpr float MIN_MOVEMENT_SPEED = 0.1f;
}  // namespace

struct HumanoidLocomotion::Impl {
    Engine* engine = nullptr;     // null in headless mode
    bool initialized = false;

    // Direct subsystem refs — populated by either initialize variant.
    // Engine path fetches from engine->get_X(). Headless path takes them
    // explicitly. All getters below assume the corresponding ptr is live.
    ParticleSystem* ps = nullptr;
    PhysicsSystem* physics = nullptr;
    kg::KGModule* kg = nullptr;
    ParticleDynamicsSystem* dyn = nullptr;
    ParticleTracer* tracer = nullptr;

    // Monotonic frame counter advanced once per update_post_physics.
    // Used to stamp queue_particle_deletion for the rare anchor
    // teardown in unregister_humanoid. We don't require alignment with
    // the renderer's frame number — only monotonicity, so deletions
    // land >0 frames after the queue (the safe-deletion delay).
    int frame_counter = 0;

    ParticleSystem& get_particle_system()         { return *ps; }
    PhysicsSystem&  get_physics_system()          { return *physics; }
    kg::KGModule&   get_kg()                      { return *kg; }
    ParticleDynamicsSystem& get_dynamics_system() { return *dyn; }
    ParticleTracer& get_particle_tracer()         { return *tracer; }
};

HumanoidLocomotion::HumanoidLocomotion()
    : impl_(std::make_unique<Impl>()) {}

HumanoidLocomotion::~HumanoidLocomotion() = default;

bool HumanoidLocomotion::initialize_headless(ParticleSystem& particle_system,
                                              PhysicsSystem& physics_system,
                                              kg::KGModule& kg_module,
                                              ParticleDynamicsSystem& dynamics_system,
                                              ParticleTracer& tracer) {
    impl_->engine = nullptr;
    impl_->ps = &particle_system;
    impl_->physics = &physics_system;
    impl_->kg = &kg_module;
    impl_->dyn = &dynamics_system;
    impl_->tracer = &tracer;
    impl_->initialized = true;
    std::cout << "[HumanoidLocomotion] initialized (headless mode)\n";
    return true;
}

bool HumanoidLocomotion::initialize(Engine* engine) {
    if (!engine) {
        std::cerr << "[HumanoidLocomotion] initialize: null engine pointer\n";
        return false;
    }
    impl_->engine = engine;
    impl_->ps = &engine->get_particle_system();
    impl_->physics = &engine->get_physics_system();
    impl_->kg = &engine->get_kg();
    impl_->dyn = &engine->get_dynamics_system();
    impl_->tracer = &engine->get_particle_tracer();
    impl_->initialized = true;

    // Subscribe to body-part health-state changes for capability /
    // dynamics recomputation. Was in dynamics::initialize before B21
    // since the iteration target lives in dynamics's friend-accessible
    // container.
    engine->get_event_bus().state_changes().subscribe(
        [this](const logosphere::ontology::WorldEvent& evt) {
            if (!evt.target_entity_id) return;
            if (!impl_->engine) return;
            kg::EntityID part_id = static_cast<kg::EntityID>(
                std::stoul(*evt.target_entity_id));
            auto& dyn = impl_->get_dynamics_system();
            auto& kg_mod = impl_->get_kg();
            for (auto& parts : dyn.humanoid_look_at_entities_) {
                auto children = kg_mod.getRelated(parts.entity_id, "HAS_PART");
                for (auto child : children) {
                    if (child == part_id) {
                        recompute_capability(parts.entity_id);
                        return;
                    }
                }
            }
        }
    );

    std::cout << "[HumanoidLocomotion] initialized\n";
    return true;
}

void HumanoidLocomotion::shutdown() {
    if (!impl_->initialized) return;
    impl_->engine = nullptr;
    impl_->initialized = false;
    std::cout << "[HumanoidLocomotion] shutdown\n";
}

void HumanoidLocomotion::update_pre_physics(double delta_time) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    if (dyn.humanoid_look_at_entities_.empty()) return;

    static int look_at_check_debug = 0;
    if (look_at_check_debug++ % 120 == 0) {
        std::cout << "[LOOK_AT_CHECK] enable=" << dyn.config_.enable_look_at
                  << " entities=" << dyn.humanoid_look_at_entities_.size() << std::endl;
    }
    if (dyn.config_.enable_look_at) {
        for (auto& parts : dyn.humanoid_look_at_entities_) {
            if (!parts.has_custom_target) {
                static int no_target_debug = 0;
                if (no_target_debug++ % 120 == 0) {
                    std::cout << "[LOOK_AT_SKIP] No custom target set for entity "
                              << parts.entity_id << std::endl;
                }
                update_walk_cycle(parts, delta_time);
                dyn.metrics_.look_at_updates++;
                continue;
            }

            float target_x = parts.custom_target_x;
            float target_y = parts.custom_target_y;

            if (parts.joint_hierarchy.get_joint("head") != nullptr) {
                set_spine_look_at(parts.hips, target_x, target_y);
            } else {
                update_humanoid_look_at(parts, target_x, target_y, delta_time);
            }
            update_yaw_cascade_state(parts, target_x, target_y, delta_time);
            update_walk_cycle(parts, delta_time);
            dyn.metrics_.look_at_updates++;
        }
    }

    // Pre-physics gravity + animation velocities (write-locked).
    auto pre_view = impl_->get_particle_system().lock_particles_for_write();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        apply_entity_gravity(parts, static_cast<float>(delta_time), pre_view);
        set_animation_velocities_pre_physics(parts, delta_time, pre_view);
    }
}

void HumanoidLocomotion::update_post_physics(double delta_time) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();

    ++impl_->frame_counter;

    // Inner scope: write lock lives until the closing brace below.
    // After it releases, flush_pending_pin_gluon_ops can take its
    // own lock to spawn anchor particles + wire pin gluons without
    // self-deadlocking.
    {
    // CRITICAL: Acquire write lock for locomotion velocity updates
    auto particles_view = impl_->get_particle_system().lock_particles_for_write();
    // (Debug-only particles_write_lock_depth counter removed in the
    //  physics-dynamics rehaul; the member no longer exists on
    //  ParticleDynamicsSystem.)

    // Get collision events from physics for step climbing detection
    const auto& collisions = impl_->get_physics_system().get_collision_events();

    for (auto& parts : dyn.humanoid_look_at_entities_) {
        // Skip lying down entities - pose is static, no dynamics processing
        if (parts.is_lying_down) continue;

        // ====================================================================
        // STEP CLIMBING: Anticipate and boost over low obstacles
        // ====================================================================
        // Uses BVH lookahead AHEAD of movement direction to detect steps
        // BEFORE collision occurs. This mirrors human behavior - we see steps
        // and lift our legs, rather than bumping into them.
        // ====================================================================
        if (parts.is_volitional && parts.is_moving) {
            anticipate_step_climbing(parts, particles_view);
        }

        // ====================================================================
        // DYNAMICS-CONTROLLED ENTITY: Position Integration + Shape
        // ====================================================================
        // MUST come FIRST! Physics skips DYNAMICS particles, so we:
        // 1. Integrate hips position from velocity
        // 2. Snap all other particles to rest offsets relative to hips
        // 3. Integrated ground support (snaps to floor if needed)
        // After this, all particles are at correct world positions on ground.
        // ====================================================================

        // DIAG: Track punch-while-walking teleport bug
        static int diag_punch_frame = -1;
        static bool diag_was_playing = false;
        if (parts.fk_playing && !diag_was_playing) {
            diag_punch_frame = 0;  // punch just started
        }
        diag_was_playing = parts.fk_playing;
        if (diag_punch_frame >= 0 && diag_punch_frame < 15) {
            auto& hp = particles_view[parts.hips];
            std::cout << "[DIAG:PRE_SHAPE] f=" << diag_punch_frame
                      << " hips=(" << hp.x << "," << hp.y << "," << hp.z << ")"
                      << " vel=(" << hp.vx << "," << hp.vy << ")"
                      << " owner=" << static_cast<int>(hp.owner)
                      << " fk_time=" << parts.fk_time_ms
                      << std::endl;
        }

        maintain_entity_shape(parts, particles_view, static_cast<float>(delta_time));

        if (diag_punch_frame >= 0 && diag_punch_frame < 15) {
            auto& hp = particles_view[parts.hips];
            std::cout << "[DIAG:POST_SHAPE] f=" << diag_punch_frame
                      << " hips=(" << hp.x << "," << hp.y << "," << hp.z << ")"
                      << std::endl;
        }

        // ====================================================================
        // ANIMATION LAYERING: Compute one-shot + locomotion, merge by region
        // ====================================================================
        // Two-layer system:
        //   Layer 1 (base): Locomotion (walk/run/strafe/turn/idle)
        //   Layer 2 (overlay): One-shot clips (punch, kick, guard)
        //
        // For UPPER_BODY overlays, locomotion drives legs while one-shot
        // drives arms+spine. For FULL_BODY overlays (kicks), one-shot
        // takes everything (backward compatible with pre-layering behavior).
        // ====================================================================

        // --- Step 1: Advance one-shot clip, sample overlay pose ---
        RotationPose overlay_pose;
        bool have_overlay = false;
        BodyRegion overlay_region = BodyRegion::FULL_BODY;

        if (parts.fk_playing && parts.fk_active_clip) {
            float dt_ms = static_cast<float>(delta_time) * 1000.0f;
            parts.fk_time_ms += dt_ms;

            if (parts.fk_time_ms >= parts.fk_active_clip->duration_ms) {
                // One-shot complete — reset
                parts.fk_playing = false;
                parts.fk_active_clip = nullptr;
                parts.fk_time_ms = 0.0f;
                // Zero joints (safe fallback if no locomotion layer takes over)
                for (auto& j : parts.joint_hierarchy.joints) {
                    j.rotation_x = 0.0f;
                    j.rotation_y = 0.0f;
                    j.rotation_z = 0.0f;
                    j.clear_semantic_targets();
                    j.mode = JointMode::DRIVEN;
                }
                // Return ownership to DYNAMICS
                for (unsigned int pid : parts.all_particle_indices) {
                    if (particles_view[pid].owner == ParticleOwner::ANIMATION) {
                        particles_view[pid].owner = ParticleOwner::DYNAMICS;
                        particles_view[pid].solver_mode = ParticleSolverMode::KINEMATIC;
                    }
                }
            } else {
                if (parts.fk_active_clip->get_pose_at_time(parts.fk_time_ms, overlay_pose)) {
                    have_overlay = true;
                    overlay_region = parts.fk_active_clip->body_region;
                }
            }
        }

        // --- Step 2: Compute locomotion pose (always, regardless of overlay) ---
        // Locomotion runs even during one-shot playback so that legs keep
        // moving when an UPPER_BODY clip (e.g. punch) is active.
        {
            bool have_locomotion = false;
            RotationPose locomotion_pose;

            // --- MODE 1: Turn-in-place ---
            if (parts.fk_turn_enabled && parts.is_turning_in_place) {
                float phase_in_half = fmodf(parts.turn_phase, static_cast<float>(M_PI));
                float clip_time = (phase_in_half / static_cast<float>(M_PI)) * parts.fk_turn_step_duration_ms;

                // Turn direction determines which clip to use
                float turn_dir_sign = dyn.normalize_angle(
                    parts.base_rotation - parts.prev_base_rotation);
                const FKAnimationClip& turn_clip = (turn_dir_sign >= 0)
                    ? parts.fk_turn_clip_r : parts.fk_turn_clip_l;

                RotationPose turn_pose;
                if (turn_clip.get_pose_at_time(clip_time, turn_pose)) {
                    locomotion_pose = turn_pose;
                    have_locomotion = true;
                }
            }
            // --- MODE 2: Walk/Run with strafe blending ---
            else if (parts.fk_walk_enabled && parts.is_volitional) {
                float phase_in_half = fmodf(parts.walk_phase, static_cast<float>(M_PI));
                // C1: Ease timing curve — non-linear phase-to-clip-time mapping.
                // ease(t) = t + A * sin(2πt)
                // Effect: boundaries (toe-off, heel-strike) are snappy,
                //         mid-swing lingers at peak flexion.
                float t_norm = phase_in_half / static_cast<float>(M_PI);
                float A = parts.fk_walk_ease_amount;
                float t_eased = t_norm + A * sinf(2.0f * static_cast<float>(M_PI) * t_norm);
                t_eased = std::max(0.0f, std::min(1.0f, t_eased));  // safety clamp
                float walk_clip_time = t_eased * parts.fk_walk_step_duration_ms;

                // Get forward walk pose
                const FKAnimationClip& walk_clip = parts.fk_walk_side_right
                    ? parts.fk_walk_clip_r : parts.fk_walk_clip_l;

                RotationPose walk_pose;
                bool have_walk = walk_clip.get_pose_at_time(walk_clip_time, walk_pose);

                // Compute strafe blend ratio from body-relative inputs
                float abs_forward = std::abs(parts.local_forward);
                float abs_right = std::abs(parts.local_right);
                float strafe_ratio = 0.0f;
                if (parts.fk_strafe_enabled && (abs_forward + abs_right) > 0.01f) {
                    strafe_ratio = abs_right / (abs_forward + abs_right);
                }

                if (strafe_ratio > 0.01f && parts.fk_strafe_enabled) {
                    // Blend with strafe clip (same ease curve)
                    float strafe_clip_time = t_eased * parts.fk_strafe_step_duration_ms;

                    const FKAnimationClip& strafe_clip = parts.fk_walk_side_right
                        ? parts.fk_strafe_clip_r : parts.fk_strafe_clip_l;

                    RotationPose strafe_pose;
                    bool have_strafe = strafe_clip.get_pose_at_time(strafe_clip_time, strafe_pose);

                    if (have_walk && have_strafe) {
                        locomotion_pose = blend_rotation_poses(walk_pose, strafe_pose, strafe_ratio);
                        have_locomotion = true;
                    } else if (have_walk) {
                        locomotion_pose = walk_pose;
                        have_locomotion = true;
                    }
                } else if (have_walk) {
                    // Pure forward walk
                    locomotion_pose = walk_pose;
                    have_locomotion = true;
                }

                // C2: Walk forward lean — subtle forward tilt during walking.
                // Biomechanics: walkers naturally lean ~2-3 degrees forward.
                // Applied across spine chain with decreasing scale upward.
                if (have_locomotion) {
                    float walk_lean = -0.05f;  // ~2.9 degrees forward (negative flex = forward)
                    struct WalkLeanTarget { const char* joint; float scale; };
                    WalkLeanTarget walk_lean_targets[] = {
                        {"lower_spine", 1.0f},
                        {"upper_spine", 0.5f},
                    };
                    for (auto& lt : walk_lean_targets) {
                        float target_lean = walk_lean * lt.scale;
                        bool found = false;
                        for (auto& t : locomotion_pose.targets) {
                            if (t.type == JointTargetType::SEMANTIC &&
                                t.semantic == SemanticChannel::FLEX &&
                                t.joint_name == lt.joint) {
                                t.angle += target_lean;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            JointTarget jt;
                            jt.joint_name = lt.joint;
                            jt.type = JointTargetType::SEMANTIC;
                            jt.semantic = SemanticChannel::FLEX;
                            jt.angle = target_lean;
                            locomotion_pose.targets.push_back(jt);
                        }
                    }
                }

                // --- Walk/Run blending ---
                // When speed exceeds walk threshold, blend walk pose with run pose
                // run_blend: 0 = pure walk, 1 = pure run
                if (have_locomotion && parts.fk_run_enabled) {
                    // Compute current movement speed from position delta
                    float dx_move = particles_view[parts.hips].x - parts.prev_world_x;
                    float dy_move = particles_view[parts.hips].y - parts.prev_world_y;
                    float move_dist = sqrtf(dx_move * dx_move + dy_move * dy_move);
                    float move_speed = (delta_time > 0.0001) ? move_dist / static_cast<float>(delta_time) : 0.0f;

                    float walk_threshold = parts.dynamics.max_walk_speed * 0.8f;
                    float run_threshold = parts.dynamics.max_walk_speed * 1.2f;
                    float target_run_blend = 0.0f;

                    if (move_speed > run_threshold) {
                        target_run_blend = 1.0f;
                    } else if (move_speed > walk_threshold) {
                        target_run_blend = (move_speed - walk_threshold) / (run_threshold - walk_threshold);
                    }

                    // C4: Temporal smoothing — prevents jitter from frame-to-frame speed fluctuation.
                    // Quick to enter run (10/s rise), slower to return to walk (5/s fall).
                    // Gives momentum feel: easy to accelerate, reluctant to decelerate.
                    float smooth_rate = (target_run_blend > parts.current_run_blend) ? parts.dynamics.run_blend_rise_rate : parts.dynamics.run_blend_fall_rate;
                    float blend_delta = (target_run_blend - parts.current_run_blend);
                    float max_change = smooth_rate * static_cast<float>(delta_time);
                    if (std::abs(blend_delta) > max_change) {
                        parts.current_run_blend += (blend_delta > 0 ? max_change : -max_change);
                    } else {
                        parts.current_run_blend = target_run_blend;
                    }
                    parts.current_run_blend = std::max(0.0f, std::min(1.0f, parts.current_run_blend));

                    // C4: Smoothstep curve on top of temporal smoothing.
                    // Makes the blend organic (slow start/end, fast middle).
                    float rb = parts.current_run_blend;
                    float run_blend = rb * rb * (3.0f - 2.0f * rb);  // Hermite smoothstep

                    if (run_blend > 0.01f) {
                        // Sample run clip at same eased phase but with run step duration
                        float run_clip_time = t_eased * parts.fk_run_step_duration_ms;
                        const FKAnimationClip& run_clip = parts.fk_walk_side_right
                            ? parts.fk_run_clip_r : parts.fk_run_clip_l;

                        RotationPose run_pose;
                        if (run_clip.get_pose_at_time(run_clip_time, run_pose)) {
                            locomotion_pose = blend_rotation_poses(locomotion_pose, run_pose, run_blend);
                        }

                        // Forward torso lean proportional to run speed.
                        // Biomechanics: runners lean forward 5-15 degrees.
                        // The lean distributes across the full spine chain:
                        //   lower_spine: full lean (pelvis tilt)
                        //   upper_spine: 60% (thorax follows)
                        //   neck: 30% (cervical follows, less than thorax)
                        //   head: 10% (vestibular reflex keeps gaze near-level)
                        // Without neck/head, the hair particles get left behind.
                        float lean_angle = run_blend * -0.20f;  // ~11 degrees forward (negative flex = forward lean)

                        struct LeanTarget { const char* joint; float scale; };
                        LeanTarget lean_targets[] = {
                            {"lower_spine", 1.0f},
                            {"upper_spine", 0.6f},
                            {"neck",        0.3f},
                            {"head",        0.1f},
                        };

                        for (auto& lt : lean_targets) {
                            float target_lean = lean_angle * lt.scale;
                            bool found = false;
                            for (auto& t : locomotion_pose.targets) {
                                if (t.type == JointTargetType::SEMANTIC &&
                                    t.semantic == SemanticChannel::FLEX &&
                                    t.joint_name == lt.joint) {
                                    t.angle += target_lean;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                JointTarget jt;
                                jt.joint_name = lt.joint;
                                jt.type = JointTargetType::SEMANTIC;
                                jt.semantic = SemanticChannel::FLEX;
                                jt.angle = target_lean;
                                locomotion_pose.targets.push_back(jt);
                            }
                        }
                    }
                }
            }
            // --- MODE 3: Idle animation (lowest priority) ---
            // Plays when stationary, not turning, and walk phase settled
            else if (parts.fk_idle_enabled && !parts.is_volitional &&
                     !parts.is_turning_in_place) {
                // Check if walk phase has settled to neutral (0 or π, within tolerance)
                float pi_f = static_cast<float>(M_PI);
                float phase_mod = fmodf(parts.walk_phase, pi_f);
                bool phase_settled = (phase_mod < 0.1f || phase_mod > (pi_f - 0.1f));

                if (phase_settled) {
                    // Sample idle clip at current idle_phase time
                    float idle_time = (parts.idle_phase / (2.0f * pi_f)) * parts.fk_idle_cycle_ms;

                    RotationPose idle_pose;
                    if (parts.fk_idle_clip.get_pose_at_time(idle_time, idle_pose)) {
                        locomotion_pose = idle_pose;
                        have_locomotion = true;

                    }
                }
            }

            // C4: Decay current_run_blend when not in MODE 2 (walk/run).
            // Without this, run_blend stays stuck at its last value when
            // transitioning to idle or turn-in-place mode.
            if (!parts.is_volitional && parts.current_run_blend > 0.001f) {
                float decay_rate = parts.dynamics.run_blend_fall_rate;
                float max_decay = decay_rate * static_cast<float>(delta_time);
                parts.current_run_blend = std::max(0.0f, parts.current_run_blend - max_decay);
            }

            // --- Step 3: Merge layers with crossfade ---
            RotationPose final_pose;
            bool should_apply_fk = false;

            // Compute overlay weight for crossfade (blend-in / blend-out)
            float overlay_weight = 1.0f;
            if (have_overlay && parts.fk_active_clip) {
                float t = parts.fk_time_ms;
                float dur = parts.fk_active_clip->duration_ms;
                float bi = parts.fk_active_clip->blend_in_ms;
                float bo = parts.fk_active_clip->blend_out_ms;
                float remaining = dur - t;

                if (bi > 0.0f && t < bi) {
                    overlay_weight = t / bi;
                } else if (bo > 0.0f && remaining < bo) {
                    overlay_weight = remaining / bo;
                }
                overlay_weight = std::max(0.0f, std::min(1.0f, overlay_weight));
            }

            // DIAG: Log merge state during punch
            if (diag_punch_frame >= 0 && diag_punch_frame < 15) {
                std::cout << "[DIAG:MERGE] f=" << diag_punch_frame
                          << " have_overlay=" << have_overlay
                          << " have_loco=" << have_locomotion
                          << " region=" << static_cast<int>(overlay_region)
                          << " weight=" << overlay_weight
                          << " fk_time=" << parts.fk_time_ms
                          << std::endl;
            }

            if (have_overlay && have_locomotion) {
                // Both layers active — merge with crossfade weight
                final_pose = merge_layered_poses(locomotion_pose, overlay_pose,
                                                  overlay_region, overlay_weight);
                should_apply_fk = true;
            } else if (have_overlay) {
                // Only one-shot — blend with neutral (weight still applies)
                if (overlay_weight < 0.999f) {
                    // During blend-in/out without locomotion, scale angles directly
                    final_pose = overlay_pose;
                    for (auto& t : final_pose.targets) {
                        t.angle *= overlay_weight;
                    }
                } else {
                    final_pose = overlay_pose;
                }
                should_apply_fk = true;
            } else if (have_locomotion) {
                // No one-shot — pure locomotion
                final_pose = locomotion_pose;
                should_apply_fk = true;
            }

            // --- Step 4: Apply merged pose ---
            if (should_apply_fk) {
                // Save look-at twist targets (set by set_spine_look_at in pre-physics)
                struct SavedTwist { float angle; };
                SavedTwist saved_twist[4] = {};
                bool has_saved[4] = {};
                const char* spine_names[4] = {"head", "neck", "upper_spine", "lower_spine"};
                for (int i = 0; i < 4; ++i) {
                    auto* j = parts.joint_hierarchy.get_joint(spine_names[i]);
                    if (j && j->has_twist_target) {
                        saved_twist[i].angle = j->twist_angle;
                        has_saved[i] = true;
                    }
                }

                // Clear and apply clip
                for (auto& j : parts.joint_hierarchy.joints) {
                    j.clear_semantic_targets();
                }
                apply_fk_pose_targets(parts, final_pose);

                // Additive blend: add look-at twist on top of animation twist
                for (int i = 0; i < 4; ++i) {
                    if (has_saved[i]) {
                        auto* j = parts.joint_hierarchy.get_joint(spine_names[i]);
                        if (j) {
                            j->twist_angle += saved_twist[i].angle;
                            j->has_twist_target = true;
                        }
                    }
                }
            }
        }

        // ====================================================================
        // FK ANIMATION: Apply joint angles to position particles
        // ====================================================================
        // Runs AFTER maintain_entity_shape() so FK can override positions.
        // FK sets particle.owner = ANIMATION to prevent future shape snapping.
        // Only runs if any joint has non-zero rotation (optimization).
        // ====================================================================
        bool has_active_fk = false;
        for (const auto& joint : parts.joint_hierarchy.joints) {
            if (std::abs(joint.rotation_x) > 0.001f ||
                std::abs(joint.rotation_y) > 0.001f ||
                std::abs(joint.rotation_z) > 0.001f ||
                joint.has_flex_target ||
                joint.has_abduct_target ||
                joint.has_twist_target) {
                has_active_fk = true;
                break;
            }
        }
        if (has_active_fk) {
            // DEBUG: Check joint right before FK
            static int pre_fk_frame = 0;
            pre_fk_frame++;
            for (auto& j : parts.joint_hierarchy.joints) {
                if (j.name == "right_shoulder" && pre_fk_frame <= 5) {
                    std::cout << "[PRE_FK] frame=" << pre_fk_frame
                              << " pivot=(" << j.pivot_offset.x << "," << j.pivot_offset.y << "," << j.pivot_offset.z << ")"
                              << " addr=" << (void*)&j
                              << std::endl;
                    break;
                }
            }
            // Capture pre-FK positions for velocity (combat needs velocity > 1.0 m/s)
            struct OldPos { unsigned int pid; float x, y, z; };
            std::vector<OldPos> old_positions;
            if (parts.fk_playing) {
                for (unsigned int pid : parts.right_arm_particles) {
                    auto& p = particles_view[pid];
                    old_positions.push_back({pid, p.x, p.y, p.z});
                }
            }

            // DIAG: Log hips before FK
            if (diag_punch_frame >= 0 && diag_punch_frame < 15) {
                auto& hp = particles_view[parts.hips];
                std::cout << "[DIAG:PRE_FK] f=" << diag_punch_frame
                          << " hips=(" << hp.x << "," << hp.y << "," << hp.z << ")"
                          << std::endl;
            }

            // Use quaternion-based FK for consistent behavior
            apply_fk_transforms(parts, particles_view);

            // Post-FK chain projection (Option B). Restores chain
            // integrity broken by per-joint collide-and-slide. Runs on
            // the whole hierarchy except the stance leg, which
            // foot_planting_IK (below) rebuilds from plant_target.
            project_chain_geometry(parts, particles_view);

            // Yaw cascade rotation write — must run after FK so the
            // biomechanical head/torso/hips ordering isn't clobbered by
            // FK's per-joint rotation output. State was advanced pre-FK
            // in update_yaw_cascade_state; this just publishes the
            // resulting rotation_z values onto every spine + limb
            // particle.
            apply_yaw_cascade_rotations(parts, particles_view);

            // DIAG: Log hips after FK
            if (diag_punch_frame >= 0 && diag_punch_frame < 15) {
                auto& hp = particles_view[parts.hips];
                std::cout << "[DIAG:POST_FK] f=" << diag_punch_frame
                          << " hips=(" << hp.x << "," << hp.y << "," << hp.z << ")"
                          << std::endl;
            }

            // Position cosmetic particles (hair, ears) relative to head after FK.
            // maintain_entity_shape positions these relative to HIPS (rest offsets),
            // but FK moves the head via joint chain rotations (lean, etc.).
            // During forward lean, the head's FK position diverges from its rest
            // offset, leaving hair behind. Fix: snap hair to head + canonical offset.
            if (!parts.head_child_particles.empty() &&
                parts.head_child_3d_offsets.size() == parts.head_child_particles.size()) {
                const auto& head = particles_view[parts.head];
                // Rotate XY offsets by the HEAD's yaw (post-cascade,
                // written by apply_yaw_cascade_rotations above). The
                // cascade makes the head LEAD the hips by design, so
                // rotating face features by the hips' yaw glued eyes,
                // ears, and nose to the body axis while the face
                // turned away underneath them (Eden playtest find;
                // repro: test_face_tracks_head).
                float cos_r = std::cos(head.rotation_z);
                float sin_r = std::sin(head.rotation_z);

                for (size_t i = 0; i < parts.head_child_particles.size(); i++) {
                    const auto& off = parts.head_child_3d_offsets[i];
                    float rot_x = off.x * cos_r + off.y * sin_r;
                    float rot_y = -off.x * sin_r + off.y * cos_r;
                    particles_view[parts.head_child_particles[i]].x = head.x + rot_x;
                    particles_view[parts.head_child_particles[i]].y = head.y + rot_y;
                    particles_view[parts.head_child_particles[i]].z = head.z + off.z;
                }
            }

            // ================================================================
            // FOOT PLANTING: Lock stance foot via 2-bone IK
            // After FK positions all limbs, we override the stance leg so
            // the planted foot stays at its world-space anchor. The swing
            // leg keeps its FK animation unchanged.
            //
            // Architecture:
            //   1. Detect heel-strike (walk_phase half-cycle transition)
            //   2. Compute plant target (stride ahead of hips in facing dir)
            //   3. 2-bone IK: solve hip→knee→ankle to reach plant target
            //   4. Override thigh/shin/foot particle positions with IK result
            // ================================================================
            // --- Walk-to-idle transition: release the stance plant ---
            // When velocity stops, the current plant_target was set at a
            // heel-strike half a stride behind the current hips. Without
            // releasing, the foot stays pinned at that stale plant and
            // the leg dangles behind the body. On the transition edge,
            // clear the plant so FK re-seats both feet under the hips.
            // Fresh twist-step triggers (if the hips are still yawing)
            // will re-commit a new plant next frame.
            if (parts.was_moving && !parts.is_moving) {
                // Phase 4b: release the live pin gluon on walk→idle
                // (the anchor particle persists). Without this, the
                // foot stays glued to the last heel-strike location
                // half a stride behind the (now stopped) hips.
                if (parts.plant_anchor_particle_id >= 0) {
                    HumanoidParts::PinGluonOp op;
                    op.kind = HumanoidParts::PinGluonOp::DISENGAGE;
                    op.release_anchor_id = parts.plant_anchor_particle_id;
                    parts.pending_pin_ops.push_back(op);
                    parts.plant_anchor_particle_id = -1;
                }
                parts.has_planted_foot = false;
                parts.plant_blend = 0.0f;
                // Snap walk_phase to neutral on the walk→idle edge.
                // Without this, the phase drifts back over ~0.3 s via
                // the return-to-neutral logic, which means MODE 3 (idle
                // clip) waits for `phase_settled`. In the meantime MODE
                // 2 doesn't fire (not volitional) and nothing applies a
                // pose — the last walk-clip pose persists on the
                // particles, and whichever leg was mid-swing stays
                // stretched forward. Forcing phase to 0 lets idle pick
                // up on the very next frame.
                parts.walk_phase = 0.0f;
                parts.prev_walk_phase_half = 0.0f;
            }
            parts.was_moving = parts.is_moving;

            // --- Twist-step trigger (step when turning in place) ---
            // Real bipeds can't spin feet-glued in place. Once the twist
            // between hips and committed foot orientation exceeds a comfort
            // threshold (~π/4 = 45°), the nervous system commits to a new
            // foot placement. When idle (is_moving == false), that trigger
            // is the only source of a heel-strike.
            bool twist_step = false;
            const float YAW_STEP_THRESHOLD = static_cast<float>(M_PI) / 4.0f;
            if (parts.yaw_cascade_inited && parts.feet_yaw_inited) {
                float d = parts.hips_yaw_world - parts.feet_yaw_world;
                while (d >  static_cast<float>(M_PI))  d -= 2.0f * static_cast<float>(M_PI);
                while (d <= -static_cast<float>(M_PI)) d += 2.0f * static_cast<float>(M_PI);
                if (std::abs(d) > YAW_STEP_THRESHOLD) twist_step = true;
            }

            // Gate: run the foot-planting block whenever there's something
            // for it to do — walking, committing a twist-step, or holding
            // an existing plant. The last case is what keeps the stance
            // foot pinned between twist-steps during an idle turn; without
            // it, the plant gets snapped out one frame after twist-step
            // plants it, and the foot teleports back to the FK position
            // the next frame.
            if (parts.foot_planting_enabled && parts.fk_walk_enabled
                && (parts.is_moving || twist_step || parts.has_planted_foot)) {
                namespace lm = logosphere;

                // --- Phase detection: which half-cycle are we in? ---
                float phase_in_half = fmodf(parts.walk_phase, static_cast<float>(M_PI));
                float half_frac = phase_in_half / static_cast<float>(M_PI);  // [0, 1)
                int current_half = static_cast<int>(parts.walk_phase / static_cast<float>(M_PI));

                // The PASSIVE (stance) foot is the opposite of the active (swinging) side.
                // For a twist-step (idle turn), force a stance swap so the
                // previously swinging foot becomes the new anchor.
                if (twist_step && !parts.is_moving) {
                    parts.fk_walk_side_right = !parts.fk_walk_side_right;
                }
                bool stance_is_right = !parts.fk_walk_side_right;

                // Detect half-cycle transition (new step = new plant)
                bool new_step = false;
                if (parts.prev_walk_phase_half < 0.0f) {
                    // First frame — initialize
                    parts.prev_walk_phase_half = static_cast<float>(current_half);
                } else if (static_cast<float>(current_half) != parts.prev_walk_phase_half) {
                    new_step = true;
                }
                parts.prev_walk_phase_half = static_cast<float>(current_half);
                if (twist_step && !parts.is_moving) new_step = true;

                // --- Plant target computation ---
                // At heel-strike: compute where the foot LANDS in world space.
                // The foot plants stride/2 AHEAD of the hips in the facing
                // direction, offset laterally by the hip pivot. Z is set at
                // ground-level ankle height (foot sole on floor).
                //
                // During stance the hips advance:
                //   0%  heel-strike: foot is stride/2 ahead of hip
                //   50% mid-stance:  hip directly above foot
                //   100% toe-off:   foot is stride/2 behind hip
                if (new_step || !parts.has_planted_foot) {
                    const float STRIDE_LENGTH = parts.dynamics.walk_stride_length;
                    const float HALF_STRIDE = STRIDE_LENGTH / 2.0f;

                    unsigned int foot_id = stance_is_right ?
                        parts.right_leg_particles[0] : parts.left_leg_particles[0];

                    // Hip pivot offset for this side (in body frame)
                    const char* s = stance_is_right ? "right" : "left";
                    auto* hip_jt = parts.joint_hierarchy.get_joint(std::string(s) + "_hip");
                    auto* ankle_jt = parts.joint_hierarchy.get_joint(std::string(s) + "_ankle");

                    auto& hips_p = particles_view[parts.hips];
                    float facing = hips_p.rotation_z;
                    float cos_f = std::cos(facing), sin_f = std::sin(facing);

                    // Stride-ahead direction = MOTION direction (not facing).
                    // When strafing or walking diagonally, the foot should
                    // plant in the direction of travel, not the direction the
                    // body is facing. Otherwise Eva "crab walks" with feet
                    // pointed forward while the body slides sideways.
                    // Fallback to facing when motion is tiny (standing, or
                    // first frame before update_locomotion sets target_v*).
                    //
                    // Twist-step exception: when stepping because the hips
                    // out-rotated the committed foot yaw (idle turn), use
                    // the TARGET hips orientation — hips_yaw_world — as the
                    // step-ahead direction. target_v is zero; facing has
                    // already lagged to hips_yaw_world via the cascade, so
                    // using hips_yaw_world directly keeps intent-aligned.
                    float motion_len = std::sqrt(parts.target_vx * parts.target_vx
                                               + parts.target_vy * parts.target_vy);
                    float ahead_dir_x, ahead_dir_y;
                    if (twist_step && !parts.is_moving) {
                        ahead_dir_x = std::sin(parts.hips_yaw_world);
                        ahead_dir_y = std::cos(parts.hips_yaw_world);
                    } else if (motion_len > 0.01f) {
                        ahead_dir_x = parts.target_vx / motion_len;
                        ahead_dir_y = parts.target_vy / motion_len;
                    } else {
                        ahead_dir_x = sin_f;
                        ahead_dir_y = cos_f;
                    }
                    // Scale the stride-ahead bias by how much of the motion
                    // is along facing (forward/backward). For pure strafe
                    // (motion perpendicular to facing), stride-ahead goes
                    // to zero and feet plant at hips + body-lateral only —
                    // the sidestep pattern. For a twist-step in place
                    // (idle rotation, motion_len ≈ 0), we also want zero
                    // stride-ahead: the foot should PIVOT to the new body-
                    // lateral position, not step half a stride forward.
                    // A forward bias here plants the foot far ahead of the
                    // stationary hips and the leg stretches into a visible
                    // lunge — the "twisted legs just standing" pose.
                    float stride_scale = 1.0f;
                    if (twist_step && !parts.is_moving) {
                        stride_scale = 0.0f;
                    } else if (motion_len > 0.01f) {
                        float fwd_proj_signed = parts.target_vx * sin_f + parts.target_vy * cos_f;
                        stride_scale = std::abs(fwd_proj_signed) / motion_len;  // [0, 1]
                    }
                    float ahead_x = ahead_dir_x * HALF_STRIDE * stride_scale;
                    float ahead_y = ahead_dir_y * HALF_STRIDE * stride_scale;

                    // Lateral hip offset still rotated by facing (body-frame
                    // shoulder-width), not motion — the hip joint is
                    // attached to the body, not to the motion vector.
                    // Twist-step: use the TARGET hips orientation for the
                    // lateral offset too, otherwise the first twist-step
                    // uses stale FK-era facing and the foot lands sideways
                    // of where the rotated body needs it.
                    float lat_cos = cos_f, lat_sin = sin_f;
                    if (twist_step && !parts.is_moving) {
                        lat_cos = std::cos(parts.hips_yaw_world);
                        lat_sin = std::sin(parts.hips_yaw_world);
                    }
                    // Body → world for a point (bx, by) in the body frame,
                    // given engine convention body_forward = (sin, cos),
                    // body_right = (cos, -sin):
                    //   world = bx * body_right + by * body_forward
                    //         = (bx*cos + by*sin, -bx*sin + by*cos)
                    // The previous formula (bx*cos − by*sin, bx*sin + by*cos)
                    // is a standard CCW rotation, which puts body-right on
                    // the wrong world side once hips yaw. Invisible when
                    // facing is +Y (rot=0) because both formulas agree; at
                    // Eden's rot=1.66 the planted foot lands on the wrong
                    // body side and the IK pulls the leg into a splay.
                    float hip_lat_x = 0.0f, hip_lat_y = 0.0f;
                    if (hip_jt) {
                        hip_lat_x =  lat_cos * hip_jt->pivot_offset.x + lat_sin * hip_jt->pivot_offset.y;
                        hip_lat_y = -lat_sin * hip_jt->pivot_offset.x + lat_cos * hip_jt->pivot_offset.y;
                    }

                    // Plant target XY: hips + stride ahead + lateral offset.
                    parts.plant_target_x = hips_p.x + ahead_x + hip_lat_x;
                    parts.plant_target_y = hips_p.y + ahead_y + hip_lat_y;

                    // Midline-crossing clamp. In body-right world axis,
                    // L foot must stay ≤ R foot's current position and R
                    // must stay ≥ L's. Without this clamp, when hips
                    // outrun the stance foot during a long stance phase
                    // (common during strafe because motion is perpendicular
                    // to the hip-to-hip axis), the new plant can leap past
                    // the other foot's pinned position and the visible
                    // result is feet crossing each other. Clamping in
                    // body-right space preserves the L-left-of-R ordering
                    // regardless of motion direction.
                    {
                        unsigned int other_foot_id = stance_is_right
                            ? parts.left_leg_particles[0]
                            : parts.right_leg_particles[0];
                        float ox = particles_view[other_foot_id].x;
                        float oy = particles_view[other_foot_id].y;
                        // body_right world axis = (cos_f, -sin_f). For
                        // twist-step use hips_yaw_world's frame.
                        float br_x = lat_cos;
                        float br_y = -lat_sin;
                        float other_along_br = ox * br_x + oy * br_y;
                        float plant_along_br = parts.plant_target_x * br_x
                                             + parts.plant_target_y * br_y;
                        // Minimum lateral separation (half a hip-width).
                        const float MIN_SEP = 0.05f;
                        float desired_along_br = plant_along_br;
                        if (stance_is_right) {
                            // R plant must be ≥ L_other_along_br + MIN_SEP.
                            if (desired_along_br < other_along_br + MIN_SEP) {
                                desired_along_br = other_along_br + MIN_SEP;
                            }
                        } else {
                            // L plant must be ≤ R_other_along_br − MIN_SEP.
                            if (desired_along_br > other_along_br - MIN_SEP) {
                                desired_along_br = other_along_br - MIN_SEP;
                            }
                        }
                        float correction = desired_along_br - plant_along_br;
                        parts.plant_target_x += correction * br_x;
                        parts.plant_target_y += correction * br_y;
                    }

                    // Plant target Z: the foot's ACTUAL current world Z. No
                    // synthesized offset. The previous `foot.z +
                    // ankle_child_offset.z` formula added half-a-foot-thickness
                    // of height each step, and when combined with
                    // ground_correct lifting the sunken swing leg, produced
                    // the spider-Eva ratchet. The foot IS where it is; its
                    // world position at heel-strike IS the plant.
                    parts.plant_target_z = particles_view[foot_id].z;

                    // Kinematic root transfer. The planted foot is now the
                    // skeleton's world anchor; hips will be derived from it.
                    // previous_particle_id captured for continuity diagnostics.
                    //
                    // Anchor is the intended plant (= plant_target_*), not the
                    // FK-placed foot position at this instant. The walk animation
                    // clip's reach and dynamics.walk_stride_length are different
                    // data paths; sourcing the anchor from `foot.xy` captures the
                    // clip's reach instead of the stride target, and the post-FK
                    // shift then translates the body by that constant mismatch
                    // every stance frame, cancelling velocity-driven forward motion.
                    // Using plant_target_* as the single source of truth makes the
                    // shift a no-op in the common case (IK reaches).
                    auto& tracer = impl_->get_particle_tracer();
                    parts.root.previous_particle_id = parts.root.particle_id;
                    parts.root.particle_id = foot_id;
                    parts.root.anchor_world = {
                        parts.plant_target_x,
                        parts.plant_target_y,
                        parts.plant_target_z
                    };
                    parts.root.mode = logosphere::RootMode::FIXED_WORLD;
                    // Trace the transfer (writes .z as a marker; the pair
                    // previous→new is readable via the note field). Frame
                    // stamp comes from begin_frame() at update() entry.
                    if (tracer.is_active() && tracer.is_traced(static_cast<int>(foot_id))) {
                        tracer.record(static_cast<int>(foot_id),
                                      "kinematic_root.transfer", "anchor_z",
                                      0.0f, parts.root.anchor_world.z,
                                      stance_is_right ? "R foot anchored" : "L foot anchored");
                    }

                    // Phase 4b: queue the pin handover — release the
                    // previous stance pin (if any) and re-pin this
                    // foot to its persistent anchor, moved to the new
                    // plant_target. The actual mutations happen in
                    // flush_pending_pin_gluon_ops after the write
                    // lock is released.
                    HumanoidParts::PinGluonOp eop;
                    eop.kind = HumanoidParts::PinGluonOp::ENGAGE;
                    eop.release_anchor_id = parts.plant_anchor_particle_id;
                    eop.foot_is_right = stance_is_right;
                    eop.foot_id = foot_id;
                    eop.tx = parts.plant_target_x;
                    eop.ty = parts.plant_target_y;
                    eop.tz = parts.plant_target_z;
                    parts.pending_pin_ops.push_back(eop);
                    parts.plant_anchor_particle_id = -1;

                    // Lock foot rotation at heel-strike
                    parts.plant_foot_rx = particles_view[foot_id].rotation_x;
                    parts.plant_foot_ry = particles_view[foot_id].rotation_y;
                    parts.plant_foot_rz = particles_view[foot_id].rotation_z;

                    parts.planted_foot_is_right = stance_is_right;
                    parts.has_planted_foot = true;
                    // Start blend at 0 so IK picks up smoothly from
                    // wherever FK left the foot; snapping to 1 produces
                    // a single-frame 0.3–0.4 m teleport (the "shooting
                    // leg" visual). The blend ramp below lifts blend
                    // toward 1 over ~0.12 s.
                    parts.plant_blend = 0.0f;
                    parts.plant_step_count++;
                    // Commit the committed-foot yaw so subsequent frames
                    // don't immediately re-trigger a twist-step.
                    parts.feet_yaw_world = parts.hips_yaw_world;
                    parts.feet_yaw_inited = true;
                }

                // --- Blend ramp: smoothly engage/disengage IK ---
                // While walking, the walk_phase half-cycle drives a
                // ramp-up / hold / ramp-down profile so the foot pin
                // lifts during toe-off and re-engages at heel-strike.
                // While idle with a plant alive (post-twist-step or just
                // standing), ramp up to 1 and stay there — the foot is
                // pinned continuously.
                if (parts.is_moving) {
                    const float BLEND_IN_END = 0.15f;   // Blend in during first 15%
                    const float BLEND_OUT_START = 0.85f; // Blend out during last 15%
                    float target_blend = 1.0f;
                    if (half_frac < BLEND_IN_END) {
                        target_blend = half_frac / BLEND_IN_END;
                    } else if (half_frac > BLEND_OUT_START) {
                        target_blend = (1.0f - half_frac) / (1.0f - BLEND_OUT_START);
                    }
                    float blend_speed = 8.0f * static_cast<float>(delta_time);
                    if (target_blend > parts.plant_blend) {
                        parts.plant_blend = std::min(target_blend, parts.plant_blend + blend_speed);
                    } else {
                        parts.plant_blend = std::max(target_blend, parts.plant_blend - blend_speed);
                    }
                } else if (parts.has_planted_foot) {
                    // Idle plant: ramp blend up to 1 and hold. 8/s is the
                    // same rate used while walking, so a twist-step
                    // transition takes ~0.12 s to fully engage — fast
                    // enough to look responsive, slow enough that the
                    // foot doesn't jump visibly in a single frame.
                    float blend_speed = 8.0f * static_cast<float>(delta_time);
                    parts.plant_blend = std::min(1.0f, parts.plant_blend + blend_speed);
                }

                // Phase 4b — scale pin-gluon stiffness by plant_blend.
                // NOTE (2026-06-12 RCA): the V4 solver builds gluon
                // position constraints as hard Jacobian rows and never
                // reads linear `stiffness`, so this scaling has no
                // mechanical effect — the pin is hard for its whole
                // lifetime (heel-strike to opposite heel-strike), which
                // matches the stance window. The writes are kept as
                // documentation of intent; the [PIN_LOST] guard below is
                // the part that matters.
                if (parts.plant_anchor_particle_id >= 0) {
                    const float BASE_PIN_STIFFNESS = 50000.0f;
                    const float BASE_PIN_DAMPING   = 500.0f;
                    auto& physics = impl_->get_physics_system();
                    unsigned int foot_id_for_pin =
                        parts.planted_foot_is_right
                            ? parts.right_leg_particles[0]
                            : parts.left_leg_particles[0];
                    GluonConstraintBase* pin = physics.get_gluon_mut(
                        static_cast<size_t>(foot_id_for_pin),
                        static_cast<size_t>(parts.plant_anchor_particle_id));
                    if (pin) {
                        pin->stiffness = BASE_PIN_STIFFNESS * parts.plant_blend;
                        pin->damping   = BASE_PIN_DAMPING   * parts.plant_blend;
                    } else {
                        // A live plant whose pin gluon can't be found is a
                        // bug (stale anchor id, lost gluon) — the foot will
                        // drift off its plant. Crash loud, not silent.
                        std::cerr << "[PIN_LOST] foot=" << foot_id_for_pin
                                  << " anchor=" << parts.plant_anchor_particle_id
                                  << " blend=" << parts.plant_blend << std::endl;
                    }
                }

                // --- Foot planting: 2-bone IK + chain reconstruction ---
                //
                // The stance foot must stay locked at plant_target while
                // the hips advance. 2-bone IK solves for the knee position
                // from the hip pivot to the ankle target. Chain is rebuilt
                // from blended FK↔IK rotations, guaranteeing connectivity.
                //
                // Historical note (Stage 3 retirement): the previous
                // "hip dip" compass-gait correction lowered all non-stance
                // body particles by up to 8 cm to let the leg reach over-
                // stride targets. With `anchor_world = plant_target` and
                // the post-FK shift pinning the stance foot, hip dip was
                // patching a problem that no longer exists — over-stride
                // now just lets the leg extend toward plant_target and the
                // shift keeps the foot at anchor. Dip retired.
                if (parts.has_planted_foot && parts.plant_blend > 0.01f) {
                    auto& leg_ids = parts.planted_foot_is_right ?
                        parts.right_leg_particles : parts.left_leg_particles;
                    unsigned int thigh_id = leg_ids[2];
                    unsigned int shin_id = leg_ids[1];
                    unsigned int foot_id = leg_ids[0];
                    unsigned int toe_id = leg_ids[3];

                    Particle& thigh_p = particles_view[thigh_id];
                    Particle& shin_p = particles_view[shin_id];
                    Particle& foot_p = particles_view[foot_id];
                    Particle& toe_p = particles_view[toe_id];
                    Particle& hips_p = particles_view[parts.hips];

                    float b = parts.plant_blend;

                    // Get joint data for the stance leg
                    const char* side_str = parts.planted_foot_is_right ? "right" : "left";
                    auto* hip_j   = parts.joint_hierarchy.get_joint(std::string(side_str) + "_hip");
                    auto* knee_j  = parts.joint_hierarchy.get_joint(std::string(side_str) + "_knee");
                    auto* ankle_j = parts.joint_hierarchy.get_joint(std::string(side_str) + "_ankle");
                    auto* toe_j   = parts.joint_hierarchy.get_joint(std::string(side_str) + "_toe");

                    if (hip_j && knee_j && ankle_j && toe_j) {
                        // Bundle joint offsets
                        lm::LegJointOffsets offsets = {
                            hip_j->child_offset,
                            knee_j->pivot_offset,
                            knee_j->child_offset,
                            ankle_j->pivot_offset,
                            ankle_j->child_offset,
                            toe_j->pivot_offset,
                            toe_j->child_offset
                        };

                        // 1. Bone lengths (constant)
                        float upper_len = lm::compute_bone_length(offsets.hip_child, offsets.knee_pivot);
                        float lower_len = lm::compute_bone_length(offsets.knee_child, offsets.ankle_pivot);
                        float max_reach = upper_len + lower_len;

                        // 2. Current hip pivot (before dip)
                        lm::Quat hips_rot = lm::Quat::from_euler(
                            hips_p.rotation_x, hips_p.rotation_y, hips_p.rotation_z);
                        lm::Vec3 hips_pos = {hips_p.x, hips_p.y, hips_p.z};
                        lm::Vec3 hip_pivot = hips_pos + lm::quat_rotate(hips_rot, hip_j->pivot_offset);

                        // plant_target is where the FOOT CENTER should land in
                        // world space (=anchor_world). IK solves for the ANKLE
                        // PIVOT, which sits above the foot center by
                        // rotate(foot_rot, ankle_child). Aim the ankle at
                        // plant_target + that offset so the chain rebuild
                        // places foot_center exactly at plant_target in steady
                        // state (b=1). Otherwise foot_center drifts relative to
                        // anchor by the offset, and the post-FK shift yanks
                        // the body by that delta every frame.
                        lm::Vec3 plant_target_center = {parts.plant_target_x, parts.plant_target_y, parts.plant_target_z};
                        lm::Quat plant_foot_rot_for_ik = lm::Quat::from_euler(
                            parts.plant_foot_rx, parts.plant_foot_ry, parts.plant_foot_rz);
                        lm::Vec3 ankle_target = plant_target_center
                            + lm::quat_rotate(plant_foot_rot_for_ik, offsets.ankle_child);

                        auto& ik_tracer = impl_->get_particle_tracer();
                        (void)max_reach;  // retained for possible future unreachable-target handling

                        // 4. FK rotations (current particle state after FK ran)
                        lm::Quat fk_thigh = lm::Quat::from_euler(thigh_p.rotation_x, thigh_p.rotation_y, thigh_p.rotation_z);
                        lm::Quat fk_shin  = lm::Quat::from_euler(shin_p.rotation_x, shin_p.rotation_y, shin_p.rotation_z);
                        lm::Quat fk_foot  = lm::Quat::from_euler(foot_p.rotation_x, foot_p.rotation_y, foot_p.rotation_z);
                        lm::Quat fk_toe   = lm::Quat::from_euler(toe_p.rotation_x, toe_p.rotation_y, toe_p.rotation_z);

                        // 5. IK solve from (dipped) hip_pivot to ankle_target
                        lm::Quat plant_foot_rot = lm::Quat::from_euler(
                            parts.plant_foot_rx, parts.plant_foot_ry, parts.plant_foot_rz);
                        float facing = hips_p.rotation_z;
                        lm::Vec3 pole = {std::sin(facing), std::cos(facing), 0};
                        auto ik = lm::solve_two_bone_ik(hip_pivot, ankle_target, upper_len, lower_len, pole);

                        // 6. IK rotations from bone directions, IN THE
                        // HIPS' YAW FRAME. The no-twist variant blended
                        // against yaw-carrying FK quats sent slerp down
                        // the short path sideways, and the relative
                        // gluon target (blended * hips.conjugate())
                        // commanded counter-yaw — legs spun whenever
                        // the body rotated (Eden playtest find; repro:
                        // test_leg_spin_on_rotation).
                        lm::Quat ik_thigh = lm::bone_rotation_from_pivots_yawed(
                            hip_pivot, ik.mid, facing);
                        lm::Quat ik_shin  = lm::bone_rotation_from_pivots_yawed(
                            ik.mid, ankle_target, facing);

                        // 7. Blend FK → IK rotations via slerp
                        lm::Quat blended_thigh = lm::Quat::slerp(fk_thigh, ik_thigh, b);
                        lm::Quat blended_shin  = lm::Quat::slerp(fk_shin, ik_shin, b);

                        // 8. Blend foot rotation: FK→locked plant rotation
                        lm::Quat blended_foot = lm::Quat::slerp(fk_foot, plant_foot_rot, b);
                        lm::Quat blended_toe = fk_toe; // toe follows FK

                        // Phase 5b: only the IK→gluon publish path remains.
                        // Publish parent-relative quats to the stance-side
                        // hip + knee gluons. The XPBD angular drive rotates
                        // thigh + shin to match; the pin gluon (C2) holds
                        // the foot at plant_target. No particle position or
                        // Euler writes — solver owns final pose. Swing leg
                        // stays clip-driven via publish_physics_drive_targets.
                        //
                        // target = q_child * q_parent.conjugate() in world
                        // frame. If the gluon stores the pair as
                        // (child → parent) we conjugate, mirroring
                        // set_joint_physics_drive_q's convention.
                        auto& physics = impl_->get_physics_system();
                        lm::Quat hip_target  = blended_thigh * hips_rot.conjugate();
                        lm::Quat knee_target = blended_shin  * blended_thigh.conjugate();

                        auto publish = [&](const Joint* jt, const lm::Quat& target) {
                            if (!jt) return;
                            GluonConstraintBase* gluon = physics.get_gluon_mut(
                                jt->parent_particle, jt->child_particle);
                            if (!gluon) return;
                            lm::Quat tgt = target;
                            if (gluon->particle_a == jt->child_particle &&
                                gluon->particle_b == jt->parent_particle) {
                                tgt = target.conjugate();
                            }
                            gluon->target_relative_q = tgt;
                            gluon->use_quat_target = true;
                            gluon->angular_drive_enabled = true;
                            gluon->enable_angular_constraint = true;
                        };
                        publish(hip_j,  hip_target);
                        publish(knee_j, knee_target);
                    }

                    static int plant_log_count = 0;
                    if (plant_log_count < 20) {
                        // Log ankle position (not foot center) for accurate delta
                        float dx = parts.plant_target_x - foot_p.x;
                        float dy = parts.plant_target_y - foot_p.y;
                        float dz = parts.plant_target_z - foot_p.z;
                        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                        printf("[FOOT_PLANT] step=%d side=%s blend=%.2f "
                               "target=(%.3f,%.3f,%.3f) foot=(%.3f,%.3f,%.3f) "
                               "delta=%.4f\n",
                               parts.plant_step_count,
                               parts.planted_foot_is_right ? "R" : "L",
                               b,
                               parts.plant_target_x, parts.plant_target_y, parts.plant_target_z,
                               foot_p.x, foot_p.y, foot_p.z,
                               dist);
                        plant_log_count++;
                    }
                }
            }
            // Note: we do NOT clear has_planted_foot here when idle. The
            // stance foot must stay pinned between steps (walk-cycle or
            // twist-step) so IK can keep it in place while the cascade
            // rotates the body. Clearing at idle produces a two-frame
            // teleport: frame N plants, frame N+1 releases → FK places
            // the foot somewhere else, frame N+2 replants after the next
            // twist-step fires. Keeping the plant alive lets the foot sit
            // quietly until there's a reason to move it.

            // Set velocity from FK position delta (for collision damage detection)
            if (parts.fk_playing && delta_time > 0.0001) {
                float dt = static_cast<float>(delta_time);
                for (const auto& old : old_positions) {
                    auto& p = particles_view[old.pid];
                    p.vx = (p.x - old.x) / dt;
                    p.vy = (p.y - old.y) / dt;
                    p.vz = (p.z - old.z) / dt;
                    p.is_sleeping = false;
                }
            }
        }

        // ================================================================
        // KINEMATIC ROOT CONSTRAINT — post-FK anchor shift
        // ================================================================
        // If root is FIXED_WORLD (stance phase), shift the whole body so
        // the root particle (stance foot) lands exactly at anchor_world.
        // All particles shift by the same delta, so inter-particle distances
        // are preserved; what changes is where the body is in world space.
        //
        // Stride progression happens via the walk cycle rotating joints:
        // as the thigh tilts through the stride, FK+shift resolves the
        // hip to a new position that satisfies "foot at anchor + current
        // chain rotations". That's how real bipedal locomotion works — the
        // planted foot is the pivot; the body vaults over it.
        //
        // This runs on top of the existing hip_dip / ground_correct / IK
        // patches. With the anchor pinning the foot exactly, those patches
        // become near-no-ops for the stance leg but stay in place as
        // safety rails for the swing leg + transitions. Retiring them is
        // the next step, done carefully one at a time.
        if (parts.root.mode == logosphere::RootMode::FIXED_WORLD &&
            parts.root.particle_id != 0)
        {
            const Particle& root_p = particles_view[parts.root.particle_id];
            // Shift authority tracks IK authority via plant_blend. At
            // heel-strike plant_blend == 0 so the shift is a no-op and
            // the body doesn't teleport; IK gradually pulls the foot
            // toward plant_target over the 5-frame blend-in ramp. Mid-
            // stance plant_blend == 1 and the residual dx is already
            // ≈ 0 because IK pinned the foot at plant_target, so the
            // full shift is a near no-op. Blend-out mirrors blend-in.
            // Kills the ~150–360 mm per-heel-strike teleport that
            // looked like "nervous-system tics" visually.
            // Post-FK anchor shift is DISABLED (2026-04-17). With Option B's
            // post-FK chain projection maintaining chain integrity and the
            // IK chain rebuild pinning the stance foot near plant_target,
            // the shift was cancelling 40–60 % of forward velocity because
            // a small per-frame chain-math drift (~2 mm/frame in Y, ~3.5 mm
            // in Z) caused the shift to pull the body backward every frame.
            // Disabling it: walk speed went from 43 % → 100.5 % of intent,
            // no tests regressed (body_coherence, spider_eva_shin_crush,
            // stance_foot_invariance, joint_hierarchy_swap_integrity all
            // still green). The kinematic_root infrastructure (anchor,
            // particle_id, heel-strike transfer) is retained because it
            // will be the substrate for Option C (physics-driven skeleton)
            // in future work — see the kinematic-root design (docs/ARCHITECTURE.md).
            float dx = 0.0f, dy = 0.0f, dz = 0.0f;
            if (dx*dx + dy*dy + dz*dz > 1e-10f) {
                auto& root_tracer = impl_->get_particle_tracer();
                for (unsigned int pid : parts.all_particle_indices) {
                    float ox = particles_view[pid].x;
                    float oy = particles_view[pid].y;
                    float oz = particles_view[pid].z;
                    particles_view[pid].x += dx;
                    particles_view[pid].y += dy;
                    particles_view[pid].z += dz;
                    TRACE_POS_WRITE(root_tracer, static_cast<int>(pid),
                                    "kinematic_root.shift_to_anchor",
                                    ox, oy, oz,
                                    particles_view[pid].x,
                                    particles_view[pid].y,
                                    particles_view[pid].z);
                }
            }
        }

        // DIAG: Log hips after entire post-physics pass
        if (diag_punch_frame >= 0 && diag_punch_frame < 15) {
            auto& hp = particles_view[parts.hips];
            std::cout << "[DIAG:POST_ALL] f=" << diag_punch_frame
                      << " hips=(" << hp.x << "," << hp.y << "," << hp.z << ")"
                      << std::endl;
            diag_punch_frame++;
        }

        // ====================================================================
        // LOCOMOTION: Apply velocity to all particles
        // ====================================================================
        // Now that hips has moved, set consistent velocity on all particles.
        // Velocity ramps up/down based on target velocity and acceleration.
        // This is the single locomotion call (removed redundant PRE_PHYSICS).
        // ====================================================================
        update_locomotion(parts, delta_time, particles_view, "POST_PHYSICS");

        // Apply motor forces for walk animation (modifies positions relative to hips)
        apply_motor_forces(parts, delta_time, particles_view);

        // Diagnostic: log PHYSICS-joint child positions after motor forces
        {
            static int post_motor_count = 0;
            if (post_motor_count < 20) {
                for (auto& j : parts.joint_hierarchy.joints) {
                    if (j.mode == JointMode::PHYSICS) {
                        auto& p = particles_view[j.child_particle];
                        printf("[POST_MOTOR_POS] joint=%s child=%d pos=(%.4f,%.4f,%.4f)\n",
                               j.name.c_str(), j.child_particle, p.x, p.y, p.z);
                    }
                }
                post_motor_count++;
            }
        }

        // Handle collision events - push entire entity uniformly
        handle_collision_events(parts, particles_view);

        // Telemetry now lives in PhysicsSystem (physics-level, all particles)
    }
    }  // close inner scope — write lock released here

    // Phase 4b: drain any pending pin-gluon CREATE / DESTROY ops.
    // Safe to take fresh locks now that the loop's view is gone.
    flush_pending_pin_gluon_ops();
}

// ----------------------------------------------------------------------
// Registration API — B1 forward delegators
// ----------------------------------------------------------------------
//
// Each method forwards to ParticleDynamicsSystem. The motivation is
// to give callers a forward-compatible surface to migrate to without
// the state ownership move (which is structurally larger — see B2 in
// the plan). When B2 lands, dynamics's methods become the delegators
// to these.

void HumanoidLocomotion::register_humanoid_direct(
    int hips_id,
    const std::vector<int>& left_leg_ids,
    const std::vector<int>& right_leg_ids,
    const std::vector<int>& left_arm_ids,
    const std::vector<int>& right_arm_ids,
    const std::vector<int>& torso_ids,
    float reflexes_ms,
    float grit_W,
    kg::EntityID entity_id,
    const DynamicsParams* custom_dynamics)
{
    if (!impl_->initialized) {
        std::cerr << "[HumanoidLocomotion] Cannot register: not initialized" << std::endl;
        return;
    }
    auto& dyn = impl_->get_dynamics_system();
    auto& kg_mod = impl_->get_kg();
    auto& ps = impl_->get_particle_system();

    HumanoidParts parts;
    parts.entity_id = entity_id;
    parts.registered = true;
    parts.is_physics_based = false;
    parts.hips = static_cast<unsigned int>(hips_id);

    if (torso_ids.size() >= 5) {
        parts.hips = static_cast<unsigned int>(torso_ids[0]);
        parts.abdomen = static_cast<unsigned int>(torso_ids[1]);
        parts.torso = static_cast<unsigned int>(torso_ids[2]);
        parts.neck = static_cast<unsigned int>(torso_ids[3]);
        parts.head = static_cast<unsigned int>(torso_ids[4]);
        parts.has_look_at_capability = true;

        for (size_t i = 5; i < torso_ids.size(); i++) {
            parts.head_child_particles.push_back(static_cast<unsigned int>(torso_ids[i]));
        }

        std::cout << "[HumanoidLocomotion] Torso parts set: head=" << parts.head
                  << " neck=" << parts.neck << " torso=" << parts.torso
                  << " head_children=" << parts.head_child_particles.size() << std::endl;
    }

    for (int id : left_leg_ids) {
        parts.left_leg_particles.push_back(static_cast<unsigned int>(id));
    }
    for (int id : right_leg_ids) {
        parts.right_leg_particles.push_back(static_cast<unsigned int>(id));
    }
    for (int id : left_arm_ids) {
        parts.left_arm_particles.push_back(static_cast<unsigned int>(id));
    }
    for (int id : right_arm_ids) {
        parts.right_arm_particles.push_back(static_cast<unsigned int>(id));
    }

    parts.all_particle_indices.push_back(static_cast<unsigned int>(hips_id));
    for (int id : left_leg_ids) parts.all_particle_indices.push_back(static_cast<unsigned int>(id));
    for (int id : right_leg_ids) parts.all_particle_indices.push_back(static_cast<unsigned int>(id));
    for (int id : left_arm_ids) parts.all_particle_indices.push_back(static_cast<unsigned int>(id));
    for (int id : right_arm_ids) parts.all_particle_indices.push_back(static_cast<unsigned int>(id));
    for (int id : torso_ids) {
        if (id != hips_id) {
            parts.all_particle_indices.push_back(static_cast<unsigned int>(id));
        }
    }

    {
        auto particles_read = ps.lock_particles_for_read();
        parts.mass = 0.0f;
        for (unsigned int pid : parts.all_particle_indices) {
            if (pid < particles_read.size()) {
                parts.mass += particles_read[pid].GetMass();
            }
        }
    }
    if (parts.mass < 0.01f) parts.mass = 75.0f;

    if (entity_id != kg::INVALID_ENTITY) {
        parts.cap = CapabilityProfile::compute_from_kg(
            kg_mod, entity_id, parts.mass, 0.9f, 1.8f);
    } else {
        parts.cap = CapabilityProfile::compute(reflexes_ms, grit_W, parts.mass, 0.9f, 1.8f);
    }
    parts.dynamics = custom_dynamics
        ? *custom_dynamics
        : DynamicsParams::from_capability(parts.cap);
    parts.speed_modifier = 1.0f;

    {
        auto particles = ps.lock_particles_for_read();
        if (static_cast<size_t>(hips_id) < particles.size()) {
            parts.world_x = particles[hips_id].x;
            parts.world_y = particles[hips_id].y;
            parts.world_z = particles[hips_id].z;
            parts.prev_world_x = parts.world_x;
            parts.prev_world_y = parts.world_y;
        }
    }
    {
        auto particles_write = ps.lock_particles_for_write();
        const Particle& hips_p = particles_write[hips_id];
        for (unsigned int pid : parts.all_particle_indices) {
            particles_write[pid].owner = ParticleOwner::DYNAMICS;
            particles_write[pid].solver_mode = ParticleSolverMode::KINEMATIC;
            const Particle& p = particles_write[pid];
            parts.rest_offsets.push_back({
                p.x - hips_p.x,
                p.y - hips_p.y,
                p.z - hips_p.z,
                p.rotation_z - hips_p.rotation_z
            });
        }

        if (parts.head != 0 && parts.head < particles_write.size() &&
            !parts.head_child_particles.empty()) {
            const Particle& head_p = particles_write[parts.head];
            for (unsigned int child_pid : parts.head_child_particles) {
                if (child_pid < particles_write.size()) {
                    const Particle& child = particles_write[child_pid];
                    parts.head_child_3d_offsets.push_back({
                        child.x - head_p.x,
                        child.y - head_p.y,
                        child.z - head_p.z
                    });
                }
            }
        }
    }

    dyn.humanoid_look_at_entities_.push_back(parts);
    std::cout << "[HumanoidLocomotion] Registered humanoid (direct) for animations"
              << (torso_ids.size() >= 5 ? " + look-at" : "") << std::endl;

    auto& reg = dyn.humanoid_look_at_entities_.back();
    int joint_count = 0;

    auto add_joint = [&](const std::string& name, unsigned int parent, unsigned int child,
                         const logosphere::JointDefinition* def) {
        Joint j;
        j.name = name;
        j.parent_particle = parent;
        j.child_particle = child;
        j.definition = def;
        auto& physics = impl_->get_physics_system();
        const auto* gluon = physics.get_gluon(j.parent_particle, j.child_particle);
        if (gluon) {
            ::Vec3 oa = gluon->offset_a;
            ::Vec3 ob = gluon->offset_b;
            if (gluon->particle_a != j.parent_particle) std::swap(oa, ob);
            j.pivot_offset = logosphere::Vec3{oa.x, oa.y, oa.z};
            j.child_offset = logosphere::Vec3{ob.x, ob.y, ob.z};
            j.rest_local.position = logosphere::Vec3{oa.x - ob.x, oa.y - ob.y, oa.z - ob.z};
            j.rest_local.rotation = logosphere::Quat::identity();
        }
        reg.joint_hierarchy.add_joint(j);
        joint_count++;
    };

    auto u = [](int id) { return static_cast<unsigned int>(id); };
    if (torso_ids.size() >= 2) add_joint("lower_spine", u(torso_ids[0]), u(torso_ids[1]), &logosphere::LOWER_SPINE);
    if (torso_ids.size() >= 3) add_joint("upper_spine", u(torso_ids[1]), u(torso_ids[2]), &logosphere::UPPER_SPINE);
    if (torso_ids.size() >= 4) add_joint("neck", u(torso_ids[2]), u(torso_ids[3]), &logosphere::NECK_JOINT);
    if (torso_ids.size() >= 5) add_joint("head", u(torso_ids[3]), u(torso_ids[4]), &logosphere::HEAD_JOINT);

    if (torso_ids.size() >= 3 && right_arm_ids.size() >= 1)
        add_joint("right_shoulder_bridge", u(torso_ids[2]), u(right_arm_ids[0]), &logosphere::FIXED_JOINT);
    if (torso_ids.size() >= 3 && left_arm_ids.size() >= 1)
        add_joint("left_shoulder_bridge", u(torso_ids[2]), u(left_arm_ids[0]), &logosphere::FIXED_JOINT);

    if (right_arm_ids.size() >= 2) add_joint("right_shoulder", u(right_arm_ids[0]), u(right_arm_ids[1]), &logosphere::SHOULDER_RIGHT);
    if (right_arm_ids.size() >= 3) add_joint("right_elbow", u(right_arm_ids[1]), u(right_arm_ids[2]), &logosphere::ELBOW_RIGHT);
    if (right_arm_ids.size() >= 4) add_joint("right_wrist", u(right_arm_ids[2]), u(right_arm_ids[3]), &logosphere::WRIST_RIGHT);

    if (left_arm_ids.size() >= 2) add_joint("left_shoulder", u(left_arm_ids[0]), u(left_arm_ids[1]), &logosphere::SHOULDER_LEFT);
    if (left_arm_ids.size() >= 3) add_joint("left_elbow", u(left_arm_ids[1]), u(left_arm_ids[2]), &logosphere::ELBOW_LEFT);
    if (left_arm_ids.size() >= 4) add_joint("left_wrist", u(left_arm_ids[2]), u(left_arm_ids[3]), &logosphere::WRIST_LEFT);

    if (right_leg_ids.size() >= 3)
        add_joint("right_hip", u(torso_ids[0]), u(right_leg_ids[2]), &logosphere::HIP_RIGHT);
    if (right_leg_ids.size() >= 3)
        add_joint("right_knee", u(right_leg_ids[2]), u(right_leg_ids[1]), &logosphere::KNEE_RIGHT);
    if (right_leg_ids.size() >= 2)
        add_joint("right_ankle", u(right_leg_ids[1]), u(right_leg_ids[0]), &logosphere::ANKLE_RIGHT);
    if (right_leg_ids.size() >= 4)
        add_joint("right_toe", u(right_leg_ids[0]), u(right_leg_ids[3]), &logosphere::TOE_RIGHT);

    if (left_leg_ids.size() >= 3)
        add_joint("left_hip", u(torso_ids[0]), u(left_leg_ids[2]), &logosphere::HIP_LEFT);
    if (left_leg_ids.size() >= 3)
        add_joint("left_knee", u(left_leg_ids[2]), u(left_leg_ids[1]), &logosphere::KNEE_LEFT);
    if (left_leg_ids.size() >= 2)
        add_joint("left_ankle", u(left_leg_ids[1]), u(left_leg_ids[0]), &logosphere::ANKLE_LEFT);
    if (left_leg_ids.size() >= 4)
        add_joint("left_toe", u(left_leg_ids[0]), u(left_leg_ids[3]), &logosphere::TOE_LEFT);

    std::cout << "[HumanoidLocomotion] Auto-registered " << joint_count << " joints" << std::endl;

    reg.foot_planting_enabled = true;
    std::cout << "[HumanoidLocomotion] Foot planting enabled" << std::endl;

    register_walk_clips(hips_id,
        create_fk_walk_step(Side::RIGHT), create_fk_walk_step(Side::LEFT));
    register_strafe_clips(hips_id,
        create_fk_strafe_step(Side::RIGHT, 1.0f), create_fk_strafe_step(Side::LEFT, -1.0f));
    register_turn_clips(hips_id,
        create_fk_turn_step(Side::RIGHT, 1.0f), create_fk_turn_step(Side::LEFT, -1.0f));
    register_idle_clip(hips_id, create_fk_idle_clip());
    register_fk_animation(hips_id, "punch_r", create_fk_cross_punch(0.7f, Side::RIGHT));
    register_fk_animation(hips_id, "punch_l", create_fk_cross_punch(0.7f, Side::LEFT));
    register_fk_animation(hips_id, "kick_r", create_fk_front_kick(0.7f, Side::RIGHT));
    register_fk_animation(hips_id, "kick_l", create_fk_front_kick(0.7f, Side::LEFT));
    std::cout << "[HumanoidLocomotion] Auto-registered walk/strafe/turn/idle/punch/kick clips" << std::endl;

    // Phase 5: physics-drive legs is the default path. Wire the
    // leg-particle quat-driven flip + leg gluon angular profile +
    // joint-children registration so heel-strikes spawn pin gluons
    // and the IK→gluon publish runs.
    apply_physics_drive_legs_init(
        dyn.humanoid_look_at_entities_.back(),
        impl_->get_particle_system(),
        impl_->get_physics_system());

    // Phase E: upper body (arms, spine, neck, head) on physics-drive
    // by default. Same gluon angular profile as legs; the per-frame
    // publisher routes joint.semantic_rotation() (FK clip rotation)
    // to gluon target_relative_q each frame, so the body still
    // follows walk/idle/turn animations — but via solver, not
    // direct particle position writes (apply_fk_transforms skips
    // joints whose child is in physics_drive_children).
    apply_physics_drive_upper_body_init(
        dyn.humanoid_look_at_entities_.back(),
        impl_->get_particle_system(),
        impl_->get_physics_system());
}

void HumanoidLocomotion::register_humanoid_look_at(kg::EntityID entity_id) {
    if (!impl_->initialized) {
        std::cerr << "[HumanoidLocomotion] Cannot register: not initialized" << std::endl;
        return;
    }

    HumanoidParts parts;
    if (!parse_humanoid_parts(entity_id, parts)) {
        std::cerr << "[HumanoidLocomotion] Failed to parse humanoid parts for entity " << entity_id << std::endl;
        return;
    }

    auto& dyn = impl_->get_dynamics_system();
    dyn.humanoid_look_at_entities_.push_back(parts);
    std::cout << "[HumanoidLocomotion] Registered humanoid " << entity_id << " for look-at" << std::endl;
}

bool HumanoidLocomotion::parse_humanoid_parts(kg::EntityID entity_id, HumanoidParts& out_parts) {
    if (!impl_->engine) return false;
    auto& kg_mod = impl_->get_kg();
    auto& ps = impl_->get_particle_system();

    out_parts.entity_id = entity_id;

    auto head_str = kg_mod.getProperty(entity_id, "head_particle");
    auto neck_str = kg_mod.getProperty(entity_id, "neck_particle");
    auto torso_str = kg_mod.getProperty(entity_id, "torso_particle");
    auto abdomen_str = kg_mod.getProperty(entity_id, "abdomen_particle");
    auto hips_str = kg_mod.getProperty(entity_id, "hips_particle");
    auto pos_x_str = kg_mod.getProperty(entity_id, "position_x");
    auto pos_y_str = kg_mod.getProperty(entity_id, "position_y");
    auto pos_z_str = kg_mod.getProperty(entity_id, "position_z");

    auto leg_spacing_str = kg_mod.getProperty(entity_id, "leg_spacing");
    auto shoulder_offset_str = kg_mod.getProperty(entity_id, "shoulder_offset");
    auto ear_offset_str = kg_mod.getProperty(entity_id, "ear_offset");

    auto left_leg_str = kg_mod.getProperty(entity_id, "left_leg_particles");
    auto right_leg_str = kg_mod.getProperty(entity_id, "right_leg_particles");
    auto left_arm_str = kg_mod.getProperty(entity_id, "left_arm_particles");
    auto right_arm_str = kg_mod.getProperty(entity_id, "right_arm_particles");

    if (!head_str.empty()) out_parts.head = std::stoi(head_str);
    if (!neck_str.empty()) out_parts.neck = std::stoi(neck_str);
    if (!torso_str.empty()) out_parts.torso = std::stoi(torso_str);
    if (!abdomen_str.empty()) out_parts.abdomen = std::stoi(abdomen_str);
    if (!hips_str.empty()) out_parts.hips = std::stoi(hips_str);

    if (!pos_x_str.empty()) out_parts.world_x = std::stof(pos_x_str);
    if (!pos_y_str.empty()) out_parts.world_y = std::stof(pos_y_str);
    if (!pos_z_str.empty()) out_parts.world_z = std::stof(pos_z_str);

    out_parts.prev_world_x = out_parts.world_x;
    out_parts.prev_world_y = out_parts.world_y;

    if (!leg_spacing_str.empty()) out_parts.leg_spacing = std::stof(leg_spacing_str);
    if (!shoulder_offset_str.empty()) out_parts.shoulder_offset = std::stof(shoulder_offset_str);
    if (!ear_offset_str.empty()) out_parts.ear_offset = std::stof(ear_offset_str);

    auto parse_ids = [](const std::string& str, std::vector<unsigned int>& vec) {
        if (str.empty()) return;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                vec.push_back(std::stoi(token));
            }
        }
    };

    parse_ids(left_leg_str, out_parts.left_leg_particles);
    parse_ids(right_leg_str, out_parts.right_leg_particles);
    parse_ids(left_arm_str, out_parts.left_arm_particles);
    parse_ids(right_arm_str, out_parts.right_arm_particles);

    auto head_children = kg_mod.getRelated(entity_id, "HAS_PART");
    for (auto child_entity : head_children) {
        std::string child_type = kg_mod.getType(child_entity);
        if (child_type == "Head") {
            auto head_kg_particles = kg_mod.getEntityKGParticles(child_entity);
            for (auto kg_id : head_kg_particles) {
                unsigned int render_idx = kg_mod.getRenderIndex(kg_id);
                if (render_idx != 0 && render_idx != out_parts.head) {
                    out_parts.head_child_particles.push_back(render_idx);
                }
            }
            std::cout << "[HumanoidLocomotion] Cached " << out_parts.head_child_particles.size()
                      << " head child particles (hair, ears)" << std::endl;

            auto particles_view = ps.lock_particles_for_read();
            const auto& particles = particles_view.get();

            if (out_parts.head != 0 && out_parts.head < particles.size()) {
                const Particle& head_particle = particles[out_parts.head];
                float head_x = head_particle.x;
                float head_y = head_particle.y;

                out_parts.head_child_offsets.clear();
                out_parts.head_child_offsets.reserve(out_parts.head_child_particles.size());

                float head_z = head_particle.z;

                for (unsigned int child_idx : out_parts.head_child_particles) {
                    if (child_idx < particles.size()) {
                        const Particle& child = particles[child_idx];
                        float offset_x = child.x - head_x;
                        float offset_y = child.y - head_y;
                        float offset_z = child.z - head_z;
                        out_parts.head_child_offsets.push_back({offset_x, offset_y});
                        out_parts.head_child_3d_offsets.push_back({offset_x, offset_y, offset_z});
                    }
                }

                std::cout << "[HumanoidLocomotion] Stored " << out_parts.head_child_offsets.size()
                          << " canonical offsets for head children (2D+3D)" << std::endl;
            }

            break;
        }
    }

    auto kg_particles = kg_mod.getEntityKGParticlesRecursive(entity_id);
    out_parts.all_particle_indices.clear();
    out_parts.all_particle_indices.reserve(kg_particles.size());

    for (auto kg_id : kg_particles) {
        unsigned int render_index = kg_mod.getRenderIndex(kg_id);
        if (render_index != 0) {
            out_parts.all_particle_indices.push_back(render_index);
        }
    }

    if (out_parts.head == 0) {
        std::cerr << "[HumanoidLocomotion] No head particle found for entity " << entity_id << std::endl;
        return false;
    }

    out_parts.mass = 0.0f;
    auto particles_view = ps.lock_particles_for_read();
    for (unsigned int particle_id : out_parts.all_particle_indices) {
        const Particle& p = particles_view[particle_id];
        out_parts.mass += p.GetMass();
    }
    std::cout << "[HumanoidLocomotion] Entity " << entity_id
              << " total mass: " << out_parts.mass << " kg ("
              << out_parts.all_particle_indices.size() << " particles)" << std::endl;

    out_parts.registered = true;
    return true;
}

void HumanoidLocomotion::unregister_humanoid(int hips_id) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    // Persistent pin anchors are engine-spawned mechanism particles;
    // tear them down with their humanoid or they leak.
    auto& ps = impl_->get_particle_system();
    auto& physics = impl_->get_physics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        const int anchors[] = { parts.left_plant_anchor_id,
                                parts.right_plant_anchor_id };
        for (int anchor : anchors) {
            if (anchor < 0) continue;
            physics.remove_gluons_for_particle(static_cast<size_t>(anchor));
            ps.queue_particle_deletion(static_cast<size_t>(anchor),
                                       impl_->frame_counter);
        }
        break;
    }
    auto it = std::remove_if(
        dyn.humanoid_look_at_entities_.begin(),
        dyn.humanoid_look_at_entities_.end(),
        [hips_id](const HumanoidParts& parts) {
            return static_cast<int>(parts.hips) == hips_id;
        }
    );
    dyn.humanoid_look_at_entities_.erase(it, dyn.humanoid_look_at_entities_.end());
}

void HumanoidLocomotion::reset_humanoid_position(int hips_id) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    auto particles = ps.lock_particles_for_read();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            if (static_cast<size_t>(hips_id) < particles.size()) {
                parts.prev_world_x = particles[hips_id].x;
                parts.prev_world_y = particles[hips_id].y;
                parts.world_x = parts.prev_world_x;
                parts.world_y = parts.prev_world_y;
            }
            return;
        }
    }
}

// Phase 4b — full leg physics-drive init. Flips the leg particles
// to is_quat_driven (skips gravity, solver owns rotation_q), seeds
// rotation_q from current Euler triple, registers joint children
// in physics_drive_children so the per-frame publisher routes
// targets, and bumps each leg gluon's angular profile (stiffness +
// damping + ±π range) to the same shape Phase 3 arms use.
//
// The Euler triple is re-derived each frame via the Stage 3 bridge,
// but seeding from the current pose at enable() time keeps the
// first frame's quat error from snapping the chain.
//
// Idempotent: calling twice is a no-op.
static void apply_physics_drive_legs_init(
    HumanoidParts& parts,
    ParticleSystem& ps,
    PhysicsSystem& physics)
{
    {
        auto view = ps.lock_particles_for_write();
        auto flip = [&](unsigned int pid) {
            if (pid == 0 || static_cast<size_t>(pid) >= view.size()) return;
            Particle& p = view[pid];
            p.solver_mode = ParticleSolverMode::DYNAMIC;
            p.is_at_rest = false;
            p.frames_at_rest = 0;
            p.is_quat_driven = true;
            p.owner = ParticleOwner::DYNAMICS;
            p.rotation_q = logosphere::Quat::from_euler(
                p.rotation_x, p.rotation_y, p.rotation_z);
        };
        for (unsigned int pid : parts.left_leg_particles)  flip(pid);
        for (unsigned int pid : parts.right_leg_particles) flip(pid);
    }

    // Bump leg-joint gluons to the Phase 3 angular profile. ±π
    // relative rotation lets abduction reach the strafe peak
    // without the rest-based ±10° clamp shutting down the swing.
    const char* leg_joints[] = {
        "right_hip", "right_knee", "right_ankle", "right_toe",
        "left_hip",  "left_knee",  "left_ankle",  "left_toe",
    };
    const float LEG_ANG_STIFFNESS = 2000.0f;
    const float LEG_ANG_DAMPING   = 60.0f;
    for (const char* name : leg_joints) {
        Joint* j = parts.joint_hierarchy.get_joint(name);
        if (!j) continue;
        parts.physics_drive_children.insert(j->child_particle);
        GluonConstraintBase* gluon = physics.get_gluon_mut(
            j->parent_particle, j->child_particle);
        if (!gluon) continue;
        gluon->angular_stiffness = LEG_ANG_STIFFNESS;
        gluon->angular_damping   = LEG_ANG_DAMPING;
        gluon->use_quat_target = true;
        gluon->angular_drive_enabled = true;
        gluon->enable_angular_constraint = true;
        gluon->max_relative_rotation = 3.14159f;
    }
}

// Phase E — upper-body physics-drive init. Mirrors apply_physics_drive_
// legs_init for the rest of the body: arms, neck, head, spine. Each
// non-leg joint child is flipped to is_quat_driven, registered in
// physics_drive_children (so apply_fk_transforms skips its position
// write and the per-frame publisher routes joint.semantic_rotation()
// to the gluon's target_relative_q), and the gluon's angular profile
// is bumped to the same 2000 N·m/rad / 60 N·s damping / ±π shape the
// per-test setup in test_physics_drive_full_idle uses.
//
// Static-target insertion is deliberately skipped — clip-driven
// upper body wants the publisher to refresh the target every frame
// from the current FK rotation, not freeze at identity.
//
// Idempotent.
static void apply_physics_drive_upper_body_init(
    HumanoidParts& parts,
    ParticleSystem& ps,
    PhysicsSystem& physics)
{
    const char* upper_joints[] = {
        "lower_spine", "upper_spine", "neck", "head",
        // Bridges included: the shoulder BONES must live in the same
        // physics regime as their neighbors. Left out, they were the
        // only FK-regime particles between the physical chest and the
        // physical upper arm — four writers fought over them (FK
        // position+Euler, yaw cascade, both gluons), producing 0.3 m
        // single-frame snaps during yaw transients ("shooting").
        // FIXED_JOINT supports no semantic channel, so the publisher
        // holds the bridge target at identity: bones ride the chest.
        "right_shoulder_bridge", "left_shoulder_bridge",
        "right_shoulder", "right_elbow", "right_wrist",
        "left_shoulder",  "left_elbow",  "left_wrist",
    };
    const float UPPER_ANG_STIFFNESS = 2000.0f;
    // NOTE: the 3-axis quat drive in the solver is a velocity-level
    // constraint with Baumgarte bias; it reads neither linear stiffness
    // nor angular_damping (verified 2026-06-12 — changing this value
    // does not alter the drive response). Kept for the legacy scalar-Z
    // drive path, which does read angular_stiffness.
    const float UPPER_ANG_DAMPING   = 60.0f;

    auto view = ps.lock_particles_for_write();
    for (const char* name : upper_joints) {
        Joint* j = parts.joint_hierarchy.get_joint(name);
        if (!j) continue;
        unsigned int pid = j->child_particle;
        if (pid != 0 && static_cast<size_t>(pid) < view.size()) {
            Particle& p = view[pid];
            p.solver_mode = ParticleSolverMode::DYNAMIC;
            p.is_at_rest = false;
            p.frames_at_rest = 0;
            p.is_quat_driven = true;
            p.owner = ParticleOwner::DYNAMICS;
            p.rotation_q = logosphere::Quat::from_euler(
                p.rotation_x, p.rotation_y, p.rotation_z);
        }

        parts.physics_drive_children.insert(j->child_particle);
        // Intentionally NOT inserting into physics_drive_static_targets
        // — let publish_physics_drive_targets refresh the gluon target
        // each frame from joint.semantic_rotation() so the body
        // follows walk/idle/turn clips.

        GluonConstraintBase* gluon = physics.get_gluon_mut(
            j->parent_particle, j->child_particle);
        if (!gluon) continue;
        gluon->angular_stiffness = UPPER_ANG_STIFFNESS;
        gluon->angular_damping   = UPPER_ANG_DAMPING;
        gluon->use_quat_target = true;
        gluon->angular_drive_enabled = true;
        gluon->enable_angular_constraint = true;
        gluon->max_relative_rotation = 3.14159f;
    }
}

int HumanoidLocomotion::get_plant_anchor_particle_id(int hips_id) const {
    if (!impl_->initialized) return -1;
    auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        return parts.plant_anchor_particle_id;
    }
    return -1;
}

// Phase 4b — drain pending_pin_ops. The post-physics loop accumulates
// ENGAGE / DISENGAGE ops while the ParticleSystem write lock is held;
// this runs after that lock is released and is safe to take its own.
// Anchor particles are persistent (one per foot, lazily created);
// engage/release only adds/removes the pin gluon and moves the anchor,
// so steady-state walking changes the particle count by zero
// (tests/test_pin_anchor_persistence.cpp).
void HumanoidLocomotion::flush_pending_pin_gluon_ops() {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    auto& physics = impl_->get_physics_system();

    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.pending_pin_ops.empty()) continue;

        for (auto& op : parts.pending_pin_ops) {
            // Both kinds release the previously engaged pin. The V4
            // solver builds gluon rows hard (stiffness is not read),
            // so gluon existence IS the engage switch.
            if (op.release_anchor_id >= 0) {
                physics.remove_gluons_for_particle(
                    static_cast<size_t>(op.release_anchor_id));
            }
            if (op.kind == HumanoidParts::PinGluonOp::DISENGAGE) continue;

            // ENGAGE: pin the stance foot to this foot's persistent
            // anchor, creating it on the foot's first-ever plant.
            int& anchor_id = op.foot_is_right ? parts.right_plant_anchor_id
                                              : parts.left_plant_anchor_id;
            if (anchor_id < 0) {
                Particle anchor{};
                anchor.width = 0.01f;
                anchor.height = 0.01f;
                anchor.thickness = 0.01f;
                // is_light_source bypasses mass calculation — we borrow
                // the massless-particle allowance. solver_mode is set
                // to KINEMATIC below, which is the physics-layer truth.
                anchor.is_light_source = true;
                anchor.a = 0.0f;
                anchor.x = op.tx;
                anchor.y = op.ty;
                anchor.z = op.tz;
                int spawned = ps.queue_particle_addition(anchor);
                ps.flush_pending_particles();
                if (spawned < 0) continue;
                anchor_id = spawned;
            }
            {
                // Move the anchor to the new plant target. Anchors are
                // is_light_source and therefore BVH-excluded: the
                // position write needs no BVH marking.
                auto view = ps.lock_particles_for_write();
                if (static_cast<size_t>(anchor_id) < view.size()) {
                    Particle& a = view[anchor_id];
                    a.x = op.tx;
                    a.y = op.ty;
                    a.z = op.tz;
                    a.vx = 0.0f; a.vy = 0.0f; a.vz = 0.0f;
                    a.solver_mode = ParticleSolverMode::KINEMATIC;
                    a.owner = ParticleOwner::DYNAMICS;
                    a.is_at_rest = false;
                }
            }

            auto g = std::make_unique<NailGluon>();
            g->offset_a.x = 0.0f; g->offset_a.y = 0.0f; g->offset_a.z = 0.0f;
            g->offset_b.x = 0.0f; g->offset_b.y = 0.0f; g->offset_b.z = 0.0f;
            g->target_distance = 0.0f;
            // Stiffness scales with plant_blend each frame in C3 (the
            // IK→gluon publish path). Start at zero so heel-strike
            // doesn't yank the foot at plant_blend = 0.
            g->stiffness = 0.0f;
            g->damping = 0.0f;
            g->breaking_force = 1e9f;
            // Position-only: no rotation coupling; angular constraints
            // would fight the hip + knee PD published in C3.
            g->enable_angular_constraint = false;
            g->angular_stiffness = 0.0f;
            g->angular_damping = 0.0f;
            g->rotate_offsets = false;
            physics.add_gluon_between(
                static_cast<size_t>(op.foot_id),
                static_cast<size_t>(anchor_id),
                std::move(g));

            parts.plant_anchor_particle_id = anchor_id;
        }
        parts.pending_pin_ops.clear();
    }
}

void HumanoidLocomotion::reset_animation_owners(kg::EntityID entity_id) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();

    HumanoidParts* parts = nullptr;
    for (auto& p : dyn.humanoid_look_at_entities_) {
        if (p.entity_id == entity_id) {
            parts = &p;
            break;
        }
    }
    if (!parts) return;

    auto particles = ps.lock_particles_for_write();
    for (unsigned int pid : parts->all_particle_indices) {
        if (particles[pid].owner == ParticleOwner::ANIMATION) {
            particles[pid].owner = ParticleOwner::DYNAMICS;
            particles[pid].solver_mode = ParticleSolverMode::KINEMATIC;
        }
    }
}

// ----------------------------------------------------------------------
// Direct rotation channel API + mode-based legacy API.
// ----------------------------------------------------------------------

bool HumanoidLocomotion::set_joint_angle(kg::EntityID entity_id,
                                         const std::string& joint_name, float angle) {
    return set_joint_rotation_y(entity_id, joint_name, angle);
}

bool HumanoidLocomotion::set_joint_rotation_x(kg::EntityID entity_id,
                                              const std::string& joint_name, float angle) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
            if (joint) {
                joint->mode = JointMode::DRIVEN;
                joint->rotation_x = angle;
                joint->clear_semantic_targets();
                return true;
            }
            return false;
        }
    }
    return false;
}

bool HumanoidLocomotion::set_joint_rotation_y(kg::EntityID entity_id,
                                              const std::string& joint_name, float angle) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
            if (joint) {
                joint->mode = JointMode::DRIVEN;
                joint->rotation_y = angle;
                joint->clear_semantic_targets();
                return true;
            }
            return false;
        }
    }
    return false;
}

bool HumanoidLocomotion::set_joint_rotation_z(kg::EntityID entity_id,
                                              const std::string& joint_name, float angle) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
            if (joint) {
                joint->mode = JointMode::DRIVEN;
                joint->rotation_z = angle;
                joint->clear_semantic_targets();
                return true;
            }
            return false;
        }
    }
    return false;
}

bool HumanoidLocomotion::set_joint_rotation(kg::EntityID entity_id,
                                            const std::string& joint_name,
                                            float rotation_x, float rotation_y, float rotation_z) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
            if (joint) {
                joint->mode = JointMode::DRIVEN;
                joint->rotation_x = rotation_x;
                joint->rotation_y = rotation_y;
                joint->rotation_z = rotation_z;
                return true;
            }
            return false;
        }
    }
    return false;
}

bool HumanoidLocomotion::set_joint_target(kg::EntityID entity_id,
                                          const std::string& joint_name,
                                          JointMode mode, float angle,
                                          const logosphere::Vec3& direction) {
    (void)mode;
    (void)direction;
    return set_joint_rotation_y(entity_id, joint_name, angle);
}

bool HumanoidLocomotion::set_joint_relax(kg::EntityID entity_id,
                                         const std::string& joint_name) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
            if (joint) {
                joint->mode = JointMode::PHYSICS;
                joint->rotation_x = 0.0f;
                joint->rotation_y = 0.0f;
                joint->rotation_z = 0.0f;
                joint->clear_semantic_targets();
                return true;
            }
            return false;
        }
    }
    return false;
}

bool HumanoidLocomotion::set_joint_rigid(kg::EntityID entity_id,
                                         const std::string& joint_name) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
            if (joint) {
                joint->mode = JointMode::INHERIT;
                joint->rotation_x = 0.0f;
                joint->rotation_y = 0.0f;
                joint->rotation_z = 0.0f;
                joint->clear_semantic_targets();
                return true;
            }
            return false;
        }
    }
    return false;
}

JointMode HumanoidLocomotion::get_joint_mode(kg::EntityID entity_id,
                                             const std::string& joint_name) const {
    if (!impl_->initialized) return JointMode::DRIVEN;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            const Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
            if (joint) return joint->mode;
            return JointMode::DRIVEN;
        }
    }
    return JointMode::DRIVEN;
}

// ----------------------------------------------------------------------
// Semantic joint API — anatomical motion commands.
// ----------------------------------------------------------------------

bool HumanoidLocomotion::set_joint_flex(kg::EntityID entity_id,
                                        const std::string& joint_name,
                                        float angle) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id != entity_id) continue;
        Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
        if (!joint) return false;
        if (joint->definition && !joint->definition->supports_flex()) {
            std::cerr << "[SEMANTIC] Joint " << joint_name
                      << " does not support flexion" << std::endl;
            return false;
        }
        joint->flex_angle = angle;
        joint->has_flex_target = true;
        joint->mode = JointMode::DRIVEN;
        return true;
    }
    return false;
}

bool HumanoidLocomotion::set_joint_abduct(kg::EntityID entity_id,
                                          const std::string& joint_name,
                                          float angle) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id != entity_id) continue;
        Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
        if (!joint) return false;
        if (joint->definition && !joint->definition->supports_abduct()) {
            std::cerr << "[SEMANTIC] Joint " << joint_name
                      << " does not support abduction" << std::endl;
            return false;
        }
        joint->abduct_angle = angle;
        joint->has_abduct_target = true;
        joint->mode = JointMode::DRIVEN;
        return true;
    }
    return false;
}

bool HumanoidLocomotion::set_joint_twist(kg::EntityID entity_id,
                                         const std::string& joint_name,
                                         float angle) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id != entity_id) continue;
        Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
        if (!joint) return false;
        if (joint->definition && !joint->definition->supports_twist()) {
            std::cerr << "[SEMANTIC] Joint " << joint_name
                      << " does not support twist" << std::endl;
            return false;
        }
        joint->twist_angle = angle;
        joint->has_twist_target = true;
        joint->mode = JointMode::DRIVEN;
        return true;
    }
    return false;
}

bool HumanoidLocomotion::clear_joint_semantic_targets(
        kg::EntityID entity_id, const std::string& joint_name) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id != entity_id) continue;
        Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
        if (!joint) return false;
        joint->clear_semantic_targets();
        return true;
    }
    return false;
}

bool HumanoidLocomotion::get_joint_twist(int hips_id, const char* joint_name,
                                         float& angle_out) const {
    if (!impl_->initialized) return false;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        const auto* j = parts.joint_hierarchy.get_joint(joint_name);
        if (j && j->has_twist_target) {
            angle_out = j->twist_angle;
            return true;
        }
        return false;
    }
    return false;
}

bool HumanoidLocomotion::set_joint_physics_drive(
        kg::EntityID entity_id, const std::string& joint_name,
        float target_z_rotation, float stiffness, float damping) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    auto& physics = impl_->get_physics_system();
    auto& ps = impl_->get_particle_system();

    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id != entity_id) continue;

        Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
        if (!joint) {
            std::cerr << "[PHYSICS_DRIVE] Joint '" << joint_name
                      << "' not found on entity " << entity_id << std::endl;
            return false;
        }

        GluonConstraintBase* gluon = physics.get_gluon_mut(joint->parent_particle,
                                                           joint->child_particle);
        if (!gluon) {
            std::cerr << "[PHYSICS_DRIVE] No gluon between particles "
                      << joint->parent_particle << " and "
                      << joint->child_particle << std::endl;
            return false;
        }

        // Direction matters: the solver computes (pb - pa) where pa/pb
        // are the gluon's stored particle_a / particle_b. If the joint's
        // parent maps to the gluon's particle_b, we must flip sign on
        // the target so the PD still pulls the child to the intended
        // world-frame angle relative to the parent.
        float sign = 1.0f;
        if (gluon->particle_a == joint->child_particle &&
            gluon->particle_b == joint->parent_particle) {
            sign = -1.0f;
        }
        gluon->target_relative_rotation = sign * target_z_rotation;
        gluon->angular_drive_enabled = true;
        gluon->enable_angular_constraint = true;
        gluon->angular_stiffness = stiffness;
        gluon->angular_damping = damping;

        parts.physics_drive_children.insert(joint->child_particle);
        parts.physics_drive_static_targets.insert(joint->child_particle);

        // Ensure the solver actually integrates omega_z on the child.
        // Humanoid particles register as KINEMATIC so dynamics owns
        // their position updates today; physics-drive needs the solver
        // to own angular state, so put the child back on DYNAMIC and
        // wake it.
        auto view = ps.lock_particles_for_write();
        if (joint->child_particle < view.size()) {
            view[joint->child_particle].solver_mode = ParticleSolverMode::DYNAMIC;
            view[joint->child_particle].is_at_rest = false;
            view[joint->child_particle].frames_at_rest = 0;
            // Reset owner: FK will no longer write this particle
            // (physics_drive_children is gated), so leaving owner
            // at ANIMATION would cause shape_snap_to_hips to skip
            // it too — the particle would have nobody writing its
            // position and drift away whenever the humanoid walks.
            view[joint->child_particle].owner = ParticleOwner::DYNAMICS;
        }
        return true;
    }
    std::cerr << "[PHYSICS_DRIVE] Entity " << entity_id
              << " not registered in humanoid look-at set" << std::endl;
    return false;
}

bool HumanoidLocomotion::set_joint_physics_drive_q(
        kg::EntityID entity_id, const std::string& joint_name,
        const logosphere::Quat& target_q, float stiffness, float damping) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    auto& physics = impl_->get_physics_system();
    auto& ps = impl_->get_particle_system();

    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id != entity_id) continue;

        Joint* joint = parts.joint_hierarchy.get_joint(joint_name);
        if (!joint) {
            std::cerr << "[PHYSICS_DRIVE_Q] Joint '" << joint_name
                      << "' not found on entity " << entity_id << std::endl;
            return false;
        }

        GluonConstraintBase* gluon = physics.get_gluon_mut(joint->parent_particle,
                                                           joint->child_particle);
        if (!gluon) {
            std::cerr << "[PHYSICS_DRIVE_Q] No gluon between particles "
                      << joint->parent_particle << " and "
                      << joint->child_particle << std::endl;
            return false;
        }

        // Direction matters the same way as the scalar path. If the
        // joint's parent is the gluon's particle_b, invert the target
        // so the PD drives b relative to a with the intended pose.
        logosphere::Quat tgt = target_q;
        if (gluon->particle_a == joint->child_particle &&
            gluon->particle_b == joint->parent_particle) {
            tgt = target_q.conjugate();
        }
        gluon->target_relative_q = tgt;
        gluon->use_quat_target = true;
        gluon->angular_drive_enabled = true;
        gluon->enable_angular_constraint = true;
        gluon->angular_stiffness = stiffness;
        gluon->angular_damping = damping;

        parts.physics_drive_children.insert(joint->child_particle);
        parts.physics_drive_static_targets.insert(joint->child_particle);

        auto view = ps.lock_particles_for_write();
        if (joint->child_particle < view.size()) {
            Particle& p = view[joint->child_particle];
            p.solver_mode = ParticleSolverMode::DYNAMIC;
            p.is_at_rest = false;
            p.frames_at_rest = 0;
            p.is_quat_driven = true;
            // Same owner-reset as the scalar set_joint_physics_drive
            // path: without it shape_snap_to_hips skips this
            // particle while FK does too, and walking leaves it
            // behind.
            p.owner = ParticleOwner::DYNAMICS;
            // Seed rotation_q from the current Euler triple so the
            // first frame's error computation starts from the
            // particle's actual orientation, not identity.
            p.rotation_q = logosphere::Quat::from_euler(
                p.rotation_x, p.rotation_y, p.rotation_z);
        }
        return true;
    }
    std::cerr << "[PHYSICS_DRIVE_Q] Entity " << entity_id
              << " not registered in humanoid look-at set" << std::endl;
    return false;
}

// ----------------------------------------------------------------------
// Joint hierarchy + FK invocation — B10
// ----------------------------------------------------------------------

void HumanoidLocomotion::register_joint(kg::EntityID entity_id, const Joint& joint) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& physics = impl_->get_physics_system();

    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id != entity_id) continue;

        Joint j = joint;

        // Cache gluon offsets immediately, before any render frame can
        // reshuffle gluon storage.
        const auto* gluon = physics.get_gluon(j.parent_particle, j.child_particle);
        if (gluon) {
            ::Vec3 offset_a = gluon->offset_a;
            ::Vec3 offset_b = gluon->offset_b;

            bool reversed = (gluon->particle_a != j.parent_particle);
            if (reversed) std::swap(offset_a, offset_b);

            j.pivot_offset = logosphere::Vec3{offset_a.x, offset_a.y, offset_a.z};
            j.child_offset = logosphere::Vec3{offset_b.x, offset_b.y, offset_b.z};
            j.rest_local.position = logosphere::Vec3{
                offset_a.x - offset_b.x,
                offset_a.y - offset_b.y,
                offset_a.z - offset_b.z
            };
            j.rest_local.rotation = logosphere::Quat::identity();

            std::cout << "[JOINT_CACHE] " << j.name
                      << " pivot=(" << j.pivot_offset.x << "," << j.pivot_offset.y << "," << j.pivot_offset.z << ")"
                      << " child=(" << j.child_offset.x << "," << j.child_offset.y << "," << j.child_offset.z << ")"
                      << std::endl;
        }

        parts.joint_hierarchy.add_joint(j);

        if (j.name.find("shoulder") != std::string::npos) {
            Joint* stored = parts.joint_hierarchy.get_joint(j.name);
            if (stored) {
                std::cout << "[JOINT_VERIFY] " << j.name
                          << " stored pivot=(" << stored->pivot_offset.x << "," << stored->pivot_offset.y << "," << stored->pivot_offset.z << ")"
                          << " stored child=(" << stored->child_offset.x << "," << stored->child_offset.y << "," << stored->child_offset.z << ")"
                          << " vec_addr=" << (void*)parts.joint_hierarchy.joints.data()
                          << std::endl;
            }
        }
        return;
    }
    std::cerr << "[Dynamics] register_joint: entity " << entity_id << " not found" << std::endl;
}

std::vector<Joint> HumanoidLocomotion::derive_joints_from_gluons(
        const std::vector<unsigned int>& entity_particles,
        unsigned int root_particle) {
    std::vector<Joint> joints;
    if (!impl_->initialized) return joints;
    auto& physics = impl_->get_physics_system();

    std::set<unsigned int> visited;
    std::queue<unsigned int> to_visit;

    to_visit.push(root_particle);
    visited.insert(root_particle);

    while (!to_visit.empty()) {
        unsigned int parent = to_visit.front();
        to_visit.pop();

        auto gluons = physics.get_gluons_for_particle(parent);

        for (const auto* gluon : gluons) {
            unsigned int child = (gluon->particle_a == parent)
                ? static_cast<unsigned int>(gluon->particle_b)
                : static_cast<unsigned int>(gluon->particle_a);

            if (visited.count(child)) continue;

            bool in_entity = std::find(entity_particles.begin(), entity_particles.end(), child)
                != entity_particles.end();
            if (!in_entity) continue;

            Joint j;
            j.name = "joint_" + std::to_string(parent) + "_" + std::to_string(child);
            j.parent_particle = parent;
            j.child_particle = child;

            ::Vec3 offset_a = gluon->offset_a;
            ::Vec3 offset_b = gluon->offset_b;

            if (gluon->particle_a != parent) {
                std::swap(offset_a, offset_b);
            }

            j.pivot_offset = logosphere::Vec3{offset_a.x, offset_a.y, offset_a.z};
            j.child_offset = logosphere::Vec3{offset_b.x, offset_b.y, offset_b.z};
            j.rest_local.position = logosphere::Vec3{
                offset_a.x - offset_b.x,
                offset_a.y - offset_b.y,
                offset_a.z - offset_b.z
            };
            j.rest_local.rotation = logosphere::Quat::identity();

            joints.push_back(j);
            visited.insert(child);
            to_visit.push(child);
        }
    }

    std::cout << "[derive_joints] Derived " << joints.size() << " joints from gluon topology"
              << " (root=" << root_particle << ", particles=" << entity_particles.size() << ")"
              << std::endl;

    return joints;
}

void HumanoidLocomotion::apply_entity_fk(kg::EntityID entity_id) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();

    HumanoidParts* parts = nullptr;
    for (auto& p : dyn.humanoid_look_at_entities_) {
        if (p.entity_id == entity_id) {
            parts = &p;
            break;
        }
    }
    if (!parts) {
        std::cerr << "[FK] Entity " << entity_id << " not found in humanoid_look_at_entities_" << std::endl;
        return;
    }

    auto particles = ps.lock_particles_for_write();

    static int external_fk_call = 0;
    external_fk_call++;

    unsigned int upper_arm_dbg = (parts->right_arm_particles.size() >= 2) ? parts->right_arm_particles[1] : 0;
    float before_x = (upper_arm_dbg > 0) ? particles[upper_arm_dbg].x : 0;
    float before_z = (upper_arm_dbg > 0) ? particles[upper_arm_dbg].z : 0;
    printf("[EXTERNAL_FK_CALL] call=%d entity=%d particles_ptr=%p upper_arm=%d BEFORE=(%.6f,_,%.6f)\n",
           external_fk_call, entity_id, (void*)&particles[0], upper_arm_dbg, before_x, before_z);

    // FK workhorse still lives on dynamics — friend access reaches it.
    apply_fk_transforms(*parts, particles);

    if (parts->right_arm_particles.size() >= 2) {
        unsigned int upper_arm = parts->right_arm_particles[1];
        printf("[EXTERNAL_FK_AFTER] call=%d upper_arm=%d pos=(%.6f,%.6f,%.6f)\n",
               external_fk_call, upper_arm,
               particles[upper_arm].x, particles[upper_arm].y, particles[upper_arm].z);
    }
    if (parts->right_arm_particles.size() >= 3) {
        unsigned int forearm = parts->right_arm_particles[2];
        printf("[EXTERNAL_FK_FOREARM] call=%d forearm=%d pos=(%.4f,%.4f,%.4f)\n",
               external_fk_call, forearm,
               particles[forearm].x, particles[forearm].y, particles[forearm].z);
    }
}

// ----------------------------------------------------------------------
// Look-at API — B7
// ----------------------------------------------------------------------

void HumanoidLocomotion::set_look_at_target(int hips_id, float target_x, float target_y) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.has_custom_target = true;
            parts.custom_target_x = target_x;
            parts.custom_target_y = target_y;
            return;
        }
    }
}

void HumanoidLocomotion::clear_look_at_target(int hips_id) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.has_custom_target = false;
            return;
        }
    }
}

void HumanoidLocomotion::set_spine_look_at(unsigned int hips_id,
                                           float target_world_x,
                                           float target_world_y) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();

    HumanoidParts* parts_ptr = nullptr;
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.hips == hips_id) {
            parts_ptr = &parts;
            break;
        }
    }
    if (!parts_ptr) return;

    // Check if spine FK joints exist
    auto* head_joint = parts_ptr->joint_hierarchy.get_joint("head");
    if (!head_joint) return;  // No spine FK — caller should use legacy path

    // Skip during FK animation playback (animation owns spine joints)
    if (parts_ptr->fk_playing) return;

    auto particles_view = ps.lock_particles_for_read();
    const Particle& hips = particles_view[parts_ptr->hips];

    // Calculate angle from hips to target.
    float dx = target_world_x - hips.x;
    float dy = target_world_y - hips.y;
    float target_angle = atan2f(dx, dy);  // 0 = +Y (North), π/2 = +X (East)

    // Angle relative to body facing direction.
    float angle_to_target = target_angle - parts_ptr->base_rotation;
    while (angle_to_target > static_cast<float>(M_PI)) angle_to_target -= 2.0f * static_cast<float>(M_PI);
    while (angle_to_target < -static_cast<float>(M_PI)) angle_to_target += 2.0f * static_cast<float>(M_PI);

    // Distribute across spine joints: head takes first, excess cascades down.
    float remaining = angle_to_target;

    // Head: takes up to ±10° (small fine adjustment via atlas/axis).
    auto* hj = parts_ptr->joint_hierarchy.get_joint("head");
    if (hj && hj->definition) {
        float head_twist = hj->definition->clamp_twist(remaining);
        hj->twist_angle = head_twist;
        hj->has_twist_target = true;
        remaining -= head_twist;
    }

    // Neck: absorbs up to ±50° (most mobile segment).
    auto* nj = parts_ptr->joint_hierarchy.get_joint("neck");
    if (nj && nj->definition) {
        float neck_twist = nj->definition->clamp_twist(remaining);
        nj->twist_angle = neck_twist;
        nj->has_twist_target = true;
        remaining -= neck_twist;
    }

    // Upper spine: takes 70% of remainder, up to ±35°.
    auto* usj = parts_ptr->joint_hierarchy.get_joint("upper_spine");
    if (usj && usj->definition) {
        float upper_twist = usj->definition->clamp_twist(remaining * 0.7f);
        usj->twist_angle = upper_twist;
        usj->has_twist_target = true;
        remaining -= upper_twist;
    }

    // Lower spine: takes 50% of remainder, up to ±30°.
    auto* lsj = parts_ptr->joint_hierarchy.get_joint("lower_spine");
    if (lsj && lsj->definition) {
        float lower_twist = lsj->definition->clamp_twist(remaining * 0.5f);
        lsj->twist_angle = lower_twist;
        lsj->has_twist_target = true;
    }
}

// ----------------------------------------------------------------------
// Locomotion controls — B8
// ----------------------------------------------------------------------

void HumanoidLocomotion::set_volitional(int hips_id, bool volitional) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            // Grappled entities cannot move volitionally.
            if (parts.is_grappled && volitional) return;
            parts.is_volitional = volitional;
            return;
        }
    }
}

void HumanoidLocomotion::set_facing_direction(int hips_id, float rotation_z) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.base_rotation = rotation_z;
            // Instant re-orientation must also re-seed the yaw-cascade
            // state. Without this the cascade reverts the new facing on
            // its next update, and mid-frame consumers (shape snap) see
            // a one-frame flip-flop between raw facing and cascade yaw —
            // at large disagreement that rotationally teleports every
            // snapped particle around the hips (measured 0.33 m on a
            // forearm at ~3.7 rad disagreement).
            if (parts.yaw_cascade_inited) {
                parts.head_yaw_world  = rotation_z;
                parts.torso_yaw_world = rotation_z;
                parts.hips_yaw_world  = rotation_z;
            }
            if (parts.feet_yaw_inited) {
                parts.feet_yaw_world = rotation_z;
            }
            auto particles = ps.lock_particles_for_write();
            for (unsigned int pid : parts.all_particle_indices) {
                if (pid < particles.size()) {
                    particles[pid].rotation_z = rotation_z;
                }
            }
            return;
        }
    }
}

void HumanoidLocomotion::set_grappled(int hips_id, bool grappled) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.is_grappled = grappled;
            if (grappled) {
                // Immediately disable volitional movement and stop.
                parts.is_volitional = false;
                parts.target_vx = 0.0f;
                parts.target_vy = 0.0f;
            }
            return;
        }
    }
}

bool HumanoidLocomotion::is_grappled(int hips_id) const {
    if (!impl_->initialized) return false;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.is_grappled;
    }
    return false;
}

void HumanoidLocomotion::set_speed_modifier(int hips_id, float modifier) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.speed_modifier = std::max(0.0f, std::min(1.0f, modifier));
            return;
        }
    }
}

void HumanoidLocomotion::recompute_capability(kg::EntityID entity_id) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& kg = impl_->get_kg();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (parts.entity_id == entity_id) {
            parts.cap = CapabilityProfile::compute_from_kg(
                kg, entity_id, parts.mass,
                parts.cap.leg_length, parts.cap.total_height);
            parts.dynamics = DynamicsParams::from_capability(parts.cap);
            return;
        }
    }
}

float HumanoidLocomotion::get_max_walk_speed(int hips_id) const {
    if (!impl_->initialized) return 2.0f;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.dynamics.max_walk_speed;
    }
    return 2.0f;
}

float HumanoidLocomotion::get_max_run_speed(int hips_id) const {
    if (!impl_->initialized) return 4.0f;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.dynamics.max_run_speed;
    }
    return 4.0f;
}

float HumanoidLocomotion::get_effective_max_speed(int hips_id) const {
    if (!impl_->initialized) return 4.0f;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            return parts.dynamics.max_run_speed * parts.speed_modifier;
        }
    }
    return 4.0f;
}

float HumanoidLocomotion::get_entity_mass(int hips_id) const {
    if (!impl_->initialized) return 0.0f;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.mass;
    }
    return 0.0f;
}

void HumanoidLocomotion::set_lying_down(int hips_id, bool lying) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        parts.is_lying_down = lying;

        auto particles_view = ps.lock_particles_for_write();

        // Get hips position as reference point.
        float base_x = particles_view[parts.hips].x;
        float base_y = particles_view[parts.hips].y;

        if (lying) {
            // Block movement while lying.
            parts.is_grappled = true;
            parts.target_vx = 0;
            parts.target_vy = 0;

            // Teleport to lying pose: body stretched along Y axis, flat on ground.
            // Floor tiles have top at z=0.3 (thickness=0.3, center=0.15), so lie at z=0.35.
            float z_ground = 0.35f;

            auto flatten = [&](unsigned int id, float y_offset, float x_offset = 0.0f) {
                particles_view[id].x = base_x + x_offset;
                particles_view[id].y = base_y + y_offset;
                particles_view[id].z = z_ground;
                particles_view[id].vx = particles_view[id].vy = particles_view[id].vz = 0;
            };

            // Torso chain: hips(0) → abdomen → torso → neck → head.
            flatten(parts.hips, 0.0f);
            flatten(parts.abdomen, 0.15f);
            flatten(parts.torso, 0.30f);
            flatten(parts.neck, 0.45f);
            flatten(parts.head, 0.60f);

            // Legs: extend backward (negative Y) — foot, shin, thigh order.
            for (size_t i = 0; i < parts.left_leg_particles.size(); i++) {
                flatten(parts.left_leg_particles[i], -0.25f * (i + 1), -parts.leg_spacing);
            }
            for (size_t i = 0; i < parts.right_leg_particles.size(); i++) {
                flatten(parts.right_leg_particles[i], -0.25f * (i + 1), parts.leg_spacing);
            }

            // Arms: alongside body — upper, forearm, hand order.
            for (size_t i = 0; i < parts.left_arm_particles.size(); i++) {
                flatten(parts.left_arm_particles[i], 0.15f + 0.15f * i, -parts.shoulder_offset * 0.5f);
            }
            for (size_t i = 0; i < parts.right_arm_particles.size(); i++) {
                flatten(parts.right_arm_particles[i], 0.15f + 0.15f * i, parts.shoulder_offset * 0.5f);
            }

            // Head children (hair, ears) — keep near head.
            for (size_t i = 0; i < parts.head_child_particles.size(); i++) {
                flatten(parts.head_child_particles[i], 0.65f);
            }
        } else {
            // Stand up — calculate proper hips height from rest_offsets.
            parts.is_grappled = false;

            float min_rest_z = 0.0f;
            for (const auto& offset : parts.rest_offsets) {
                min_rest_z = std::min(min_rest_z, offset.z);
            }

            // Floor tiles have top at z=0.3, add margin of 0.05.
            // hips.z + min_rest_z = floor_top + margin → hips.z = floor_top + margin − min_rest_z.
            float floor_surface = 0.35f;
            float standing_hips_z = floor_surface - min_rest_z;

            particles_view[parts.hips].x = base_x;
            particles_view[parts.hips].y = base_y;
            particles_view[parts.hips].z = standing_hips_z;
            particles_view[parts.hips].vx = 0;
            particles_view[parts.hips].vy = 0;
            particles_view[parts.hips].vz = 0;

            // All other particles will be snapped by maintain_entity_shape next frame.
        }
        return;
    }
}

bool HumanoidLocomotion::is_lying_down(int hips_id) const {
    if (!impl_->initialized) return false;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.is_lying_down;
    }
    return false;
}

void HumanoidLocomotion::set_target_velocity(int hips_id, float target_vx, float target_vy) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        if (parts.is_grappled) return;
        parts.target_vx = target_vx;
        parts.target_vy = target_vy;
        parts.use_body_relative = false;
        float target_speed = std::sqrt(target_vx * target_vx + target_vy * target_vy);
        parts.is_volitional = (target_speed > 0.01f);
        return;
    }
    // No registered humanoid has this hips id. The classic cause is a
    // STALE id after chunk-swap remapping — the caller must drive the
    // swap-remapped cache, not the spawn-time id. Silent no-ops here
    // cost a debugging session (2026-07-30); be loud, but only once
    // per offending id per second-ish.
    static int stale_warns = 0;
    if (stale_warns < 20 && ++stale_warns) {
        std::cerr << "[HumanoidLocomotion] set_target_velocity: no humanoid "
                     "with hips id " << hips_id
                  << " (stale id after particle swaps?)" << std::endl;
    }
}

void HumanoidLocomotion::set_body_relative_velocity(int hips_id, float forward, float right) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        if (parts.is_grappled) return;
        parts.local_forward = forward;
        parts.local_right = right;
        parts.use_body_relative = true;
        float input_speed = std::sqrt(forward * forward + right * right);
        parts.is_volitional = (input_speed > 0.01f);
        return;
    }
}

void HumanoidLocomotion::zero_all_velocities(int hips_id) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        auto particles_view = ps.lock_particles_for_write();
        for (unsigned int pid : parts.all_particle_indices) {
            particles_view[pid].vx = 0.0f;
            particles_view[pid].vy = 0.0f;
            particles_view[pid].vz = 0.0f;
        }
        parts.velocity_x = 0.0f;
        parts.velocity_y = 0.0f;
        parts.velocity_z = 0.0f;
        return;
    }
}

void HumanoidLocomotion::apply_impulse(int hips_id, float velocity_x, float velocity_y) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        auto particles_view = ps.lock_particles_for_write();
        for (unsigned int pid : parts.all_particle_indices) {
            particles_view[pid].vx += velocity_x;
            particles_view[pid].vy += velocity_y;
        }
        parts.velocity_x += velocity_x;
        parts.velocity_y += velocity_y;
        return;
    }
}

float HumanoidLocomotion::get_base_rotation(int hips_id) const {
    if (!impl_->initialized) return 0.0f;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.base_rotation;
    }
    return 0.0f;
}

float HumanoidLocomotion::get_walk_phase(int hips_id) const {
    if (!impl_->initialized) return -1.0f;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.walk_phase;
    }
    return -1.0f;
}

float HumanoidLocomotion::get_current_run_blend(int hips_id) const {
    if (!impl_->initialized) return -1.0f;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.current_run_blend;
    }
    return -1.0f;
}

// ----------------------------------------------------------------------
// Animation playback — B9
// ----------------------------------------------------------------------

bool HumanoidLocomotion::register_animation(int hips_id, const std::string& name,
                                            const AnimationClip& clip) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.animation_controller.register_clip(name, clip);
            return true;
        }
    }
    std::cerr << "[Dynamics] No humanoid found with hips_id=" << hips_id << std::endl;
    return false;
}

bool HumanoidLocomotion::play_animation(int hips_id, const std::string& name) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            // Mutual exclusion: stop FK animation if playing.
            parts.fk_playing = false;
            parts.fk_active_clip = nullptr;
            return parts.animation_controller.play(name);
        }
    }
    std::cerr << "[Dynamics] No humanoid found with hips_id=" << hips_id << std::endl;
    return false;
}

bool HumanoidLocomotion::register_fk_animation(int hips_id, const std::string& name,
                                               const FKAnimationClip& clip) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.fk_clips[name] = clip;
            return true;
        }
    }
    std::cerr << "[Dynamics] register_fk_animation: no humanoid with hips_id=" << hips_id << std::endl;
    return false;
}

bool HumanoidLocomotion::play_fk_animation(int hips_id, const std::string& name) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            auto it = parts.fk_clips.find(name);
            if (it == parts.fk_clips.end()) {
                std::cerr << "[Dynamics] play_fk_animation: clip '" << name << "' not registered" << std::endl;
                return false;
            }
            // Mutual exclusion: stop position-based animation.
            parts.animation_controller.stop();
            parts.fk_active_clip = &it->second;
            parts.fk_time_ms = 0.0f;
            parts.fk_playing = true;
            return true;
        }
    }
    std::cerr << "[Dynamics] play_fk_animation: no humanoid with hips_id=" << hips_id << std::endl;
    return false;
}

bool HumanoidLocomotion::is_fk_animation_playing(int hips_id) const {
    if (!impl_->initialized) return false;
    const auto& dyn = impl_->get_dynamics_system();
    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) return parts.fk_playing;
    }
    return false;
}

void HumanoidLocomotion::register_walk_clips(int hips_id,
                                              const FKAnimationClip& right_step,
                                              const FKAnimationClip& left_step) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.fk_walk_clip_r = right_step;
            parts.fk_walk_clip_l = left_step;
            parts.fk_walk_step_duration_ms = right_step.duration_ms;
            parts.fk_walk_enabled = true;
            std::cout << "[Dynamics] Walk clips registered for hips=" << hips_id
                      << " step_duration=" << parts.fk_walk_step_duration_ms << "ms" << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] register_walk_clips: no humanoid with hips_id=" << hips_id << std::endl;
}

void HumanoidLocomotion::register_strafe_clips(int hips_id,
                                                const FKAnimationClip& right_step,
                                                const FKAnimationClip& left_step) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.fk_strafe_clip_r = right_step;
            parts.fk_strafe_clip_l = left_step;
            parts.fk_strafe_step_duration_ms = right_step.duration_ms;
            parts.fk_strafe_enabled = true;
            std::cout << "[Dynamics] Strafe clips registered for hips=" << hips_id
                      << " step_duration=" << parts.fk_strafe_step_duration_ms << "ms" << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] register_strafe_clips: no humanoid with hips_id=" << hips_id << std::endl;
}

void HumanoidLocomotion::register_turn_clips(int hips_id,
                                              const FKAnimationClip& right_step,
                                              const FKAnimationClip& left_step) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.fk_turn_clip_r = right_step;
            parts.fk_turn_clip_l = left_step;
            parts.fk_turn_step_duration_ms = right_step.duration_ms;
            parts.fk_turn_enabled = true;
            parts.prev_base_rotation = parts.base_rotation;
            std::cout << "[Dynamics] Turn clips registered for hips=" << hips_id
                      << " step_duration=" << parts.fk_turn_step_duration_ms << "ms" << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] register_turn_clips: no humanoid with hips_id=" << hips_id << std::endl;
}

void HumanoidLocomotion::register_idle_clip(int hips_id, const FKAnimationClip& clip) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.fk_idle_clip = clip;
            parts.fk_idle_cycle_ms = clip.duration_ms;
            parts.fk_idle_enabled = true;
            parts.idle_phase = 0.0f;
            std::cout << "[Dynamics] Idle clip registered for hips=" << hips_id
                      << " cycle=" << parts.fk_idle_cycle_ms << "ms" << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] register_idle_clip: no humanoid with hips_id=" << hips_id << std::endl;
}

void HumanoidLocomotion::register_run_clips(int hips_id,
                                             const FKAnimationClip& right_step,
                                             const FKAnimationClip& left_step) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.fk_run_clip_r = right_step;
            parts.fk_run_clip_l = left_step;
            parts.fk_run_step_duration_ms = right_step.duration_ms;
            parts.fk_run_enabled = true;
            std::cout << "[Dynamics] Run clips registered for hips=" << hips_id
                      << " step_duration=" << parts.fk_run_step_duration_ms << "ms"
                      << " stride=" << parts.fk_run_stride_length << "m" << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] register_run_clips: no humanoid with hips_id=" << hips_id << std::endl;
}

void HumanoidLocomotion::set_foot_planting(int hips_id, bool enabled) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.foot_planting_enabled = enabled;
            // Release a live pin: leaving the gluon wired while plant
            // state is reset would orphan a hard constraint on the foot.
            if (parts.plant_anchor_particle_id >= 0) {
                HumanoidParts::PinGluonOp op;
                op.kind = HumanoidParts::PinGluonOp::DISENGAGE;
                op.release_anchor_id = parts.plant_anchor_particle_id;
                parts.pending_pin_ops.push_back(op);
                parts.plant_anchor_particle_id = -1;
            }
            parts.has_planted_foot = false;
            parts.plant_blend = 0.0f;
            parts.plant_step_count = 0;
            parts.prev_walk_phase_half = -1.0f;
            std::cout << "[Dynamics] Foot planting " << (enabled ? "enabled" : "disabled")
                      << " for hips=" << hips_id << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] set_foot_planting: no humanoid with hips_id=" << hips_id << std::endl;
}

void HumanoidLocomotion::set_rest_position(int hips_id, unsigned int particle_id,
                                            float x, float y, float z) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            // Get current hips position to convert absolute to relative offset.
            auto particles = ps.lock_particles_for_read();
            const auto& hips_particle = particles[parts.hips];

            // Calculate WORLD offset from hips.
            float world_offset_x = x - hips_particle.x;
            float world_offset_y = y - hips_particle.y;
            float offset_z = z - hips_particle.z;

            // Convert to LOCAL body space (inverse of forward transformation).
            // Forward: world_x = local_x * cos + local_y * sin
            //          world_y = -local_x * sin + local_y * cos
            // Inverse: local_x = world_x * cos - world_y * sin
            //          local_y = world_x * sin + world_y * cos
            float facing = hips_particle.rotation_z;
            float cos_f = std::cos(facing);
            float sin_f = std::sin(facing);
            float local_offset_x = world_offset_x * cos_f - world_offset_y * sin_f;
            float local_offset_y = world_offset_x * sin_f + world_offset_y * cos_f;

            parts.animation_controller.set_rest_position(
                particle_id, local_offset_x, local_offset_y, offset_z);
            std::cout << "[REST_POS] particle=" << particle_id
                      << " world=(" << world_offset_x << "," << world_offset_y << ")"
                      << " facing=" << facing
                      << " local=(" << local_offset_x << "," << local_offset_y << ")" << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] No humanoid found with hips_id=" << hips_id << std::endl;
}

void HumanoidLocomotion::enable_humanoid_diagnostics(int hips_id, bool enable) {
    if (!impl_->initialized) return;
    auto& dyn = impl_->get_dynamics_system();
    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) == hips_id) {
            parts.diagnostics_enabled = enable;
            std::cout << "[DIAG] Humanoid diagnostics " << (enable ? "ENABLED" : "DISABLED")
                      << " for hips_id=" << hips_id << std::endl;
            return;
        }
    }
    std::cerr << "[Dynamics] No humanoid found with hips_id=" << hips_id << std::endl;
}


// ====================================================================
// Humanoid per-frame helpers — relocated from ParticleDynamicsSystem.
// Each acquires `dyn` once at top to reach friend-accessible state
// (config_, metrics_, normalize_angle, humanoid_look_at_entities_).
// ====================================================================

void HumanoidLocomotion::update_yaw_cascade_state(
    HumanoidParts& parts,
    float target_world_x, float target_world_y,
    double delta_time)
{
    auto& dyn = impl_->get_dynamics_system();
    if (!impl_->engine) return;
    if (parts.fk_playing) return;  // one-shot FK clip owns joint rotations

    auto particles = impl_->get_particle_system().lock_particles_for_read();
    size_t n = particles.size();
    if (parts.hips >= n) return;
    const Particle& hips_p = particles[parts.hips];

    // Target: world-space angle from hips to the look target.
    float dx = target_world_x - hips_p.x;
    float dy = target_world_y - hips_p.y;
    float target_angle = std::atan2(dx, dy);  // 0 = +Y, π/2 = +X (engine convention)

    auto norm = [](float a) {
        while (a > static_cast<float>(M_PI))  a -= 2.0f * static_cast<float>(M_PI);
        while (a <= -static_cast<float>(M_PI)) a += 2.0f * static_cast<float>(M_PI);
        return a;
    };

    if (!parts.yaw_cascade_inited) {
        parts.head_yaw_world  = parts.base_rotation;
        parts.torso_yaw_world = parts.base_rotation;
        parts.hips_yaw_world  = parts.base_rotation;
        parts.yaw_cascade_inited = true;
        // Seed the committed-foot yaw so twist-step can detect deviation
        // without waiting for a walk-cycle heel-strike to establish it.
        parts.feet_yaw_world  = parts.base_rotation;
        parts.feet_yaw_inited = true;
    }

    const float TAU_HEAD  = 0.08f;   //  80 ms
    const float TAU_TORSO = 0.18f;   // 180 ms
    const float TAU_HIPS  = 0.35f;   // 350 ms
    float dt_s = static_cast<float>(delta_time);
    float a_head  = 1.0f - std::exp(-dt_s / TAU_HEAD);
    float a_torso = 1.0f - std::exp(-dt_s / TAU_TORSO);
    float a_hips  = 1.0f - std::exp(-dt_s / TAU_HIPS);

    parts.head_yaw_world  = norm(parts.head_yaw_world
        + norm(target_angle - parts.head_yaw_world) * a_head);
    parts.torso_yaw_world = norm(parts.torso_yaw_world
        + norm(parts.head_yaw_world - parts.torso_yaw_world) * a_torso);
    parts.hips_yaw_world  = norm(parts.hips_yaw_world
        + norm(parts.torso_yaw_world - parts.hips_yaw_world) * a_hips);

    // Comfort clamps on the relative twists — prevents gaze lock-in from
    // ripping head off torso when a target is physically unreachable.
    const float HEAD_MAX  = dyn.config_.head_max_rotation;   // ±45°
    const float TORSO_MAX = dyn.config_.torso_max_rotation;  // ±45°
    float head_rel  = norm(parts.head_yaw_world  - parts.torso_yaw_world);
    float torso_rel = norm(parts.torso_yaw_world - parts.hips_yaw_world);
    if (head_rel >  HEAD_MAX)  parts.head_yaw_world  = norm(parts.torso_yaw_world + HEAD_MAX);
    if (head_rel < -HEAD_MAX)  parts.head_yaw_world  = norm(parts.torso_yaw_world - HEAD_MAX);
    if (torso_rel >  TORSO_MAX) parts.torso_yaw_world = norm(parts.hips_yaw_world + TORSO_MAX);
    if (torso_rel < -TORSO_MAX) parts.torso_yaw_world = norm(parts.hips_yaw_world - TORSO_MAX);

    // Publish hips yaw as the base rotation so walk-cycle / body-relative-
    // velocity / snap_to_hips all see it.
    parts.base_rotation = parts.hips_yaw_world;
}

void HumanoidLocomotion::apply_yaw_cascade_rotations(
    HumanoidParts& parts, ParticleSystem::WriteView& particles)
{
    auto& dyn = impl_->get_dynamics_system();
    if (!parts.yaw_cascade_inited) return;
    if (parts.fk_playing) return;

    size_t n = particles.size();
    const auto& drive_set = parts.physics_drive_children;
    auto set_rot = [&](unsigned int pid, float yaw) {
        if (pid == 0 || pid >= n) return;
        if (drive_set.count(pid)) return;  // physics owns rotation_z on this particle
        particles[pid].rotation_z = yaw;
    };

    // Spine: head/neck at head_yaw, chest/torso/abdomen at torso_yaw,
    // hips at hips_yaw. The differential produces a visible spine twist.
    set_rot(parts.head,    parts.head_yaw_world);
    set_rot(parts.neck,    parts.head_yaw_world);
    set_rot(parts.torso,   parts.torso_yaw_world);
    set_rot(parts.abdomen, parts.torso_yaw_world);
    set_rot(parts.hips,    parts.hips_yaw_world);

    // Limbs: legs go with hips (walk clip adds swing on top via
    // snap_to_hips). Arms go with torso, so shoulder counter-rotation vs
    // hips appears automatically when torso lags hips on a gait cycle.
    for (unsigned int pid : parts.left_leg_particles)   set_rot(pid, parts.hips_yaw_world);
    for (unsigned int pid : parts.right_leg_particles)  set_rot(pid, parts.hips_yaw_world);
    for (unsigned int pid : parts.left_arm_particles)   set_rot(pid, parts.torso_yaw_world);
    for (unsigned int pid : parts.right_arm_particles)  set_rot(pid, parts.torso_yaw_world);
    for (unsigned int pid : parts.head_child_particles) set_rot(pid, parts.head_yaw_world);
}

void HumanoidLocomotion::update_humanoid_look_at(HumanoidParts& parts, float mouse_world_x, float mouse_world_y, double delta_time) {
    auto& dyn = impl_->get_dynamics_system();
    if (!parts.registered) return;
    if (!impl_->engine) return;  // No particle system

    // THREAD SAFETY: Use WriteView for automatic lock management
    auto particles_view = impl_->get_particle_system().lock_particles_for_write();

    // Validate particle indices
    size_t num_particles = particles_view.size();
    if (parts.hips >= num_particles || parts.head >= num_particles) {
        std::cerr << "[LOOK_AT] Invalid particle indices: hips=" << parts.hips
                  << " head=" << parts.head << " max=" << num_particles << std::endl;
        return;
    }

    // Get current body center position (hips) for pivot calculations
    Particle& hips = particles_view[parts.hips];
    float body_center_x = hips.x;
    float body_center_y = hips.y;

    // ================================================================
    // LOOK-AT PRIMITIVE: Calculate angle from "eyes" to target
    // ================================================================
    Particle& head = particles_view[parts.head];
    float head_x = head.x;
    float head_y = head.y;

    // Calculate angle from head to mouse (eyes follow target)
    // Using atan2(dx, dy) so 0° = +Y (North), matching docs/ARCHITECTURE.md convention
    // This directly gives the correct rotation_z value
    float dx = mouse_world_x - head_x;
    float dy = mouse_world_y - head_y;
    float target_angle = atan2f(dx, dy);

    // No offset needed - atan2(dx, dy) gives:
    // North (dx=0, dy>0) → 0°, East (dx>0, dy=0) → 90°
    // South (dx=0, dy<0) → 180°, West (dx<0, dy=0) → -90°
    const float VISUAL_ORIENTATION_OFFSET = 0.0f;

    // ========================================================================
    // PHYSICS-BASED LOOK-AT: Apply torque to head, let angular constraints propagate
    // ========================================================================
    // Instead of directly setting rotation_z on all particles, we apply a
    // "look-at torque" to the head. Angular constraints with limits naturally
    // propagate rotation through the skeleton:
    //
    //   Head (torque applied) → neck gluon → Neck → spine gluon → Torso → ...
    //
    // Each joint has max_relative_rotation limit (dead zone). Head rotates freely
    // until neck limit, then torque propagates to torso, then hips, then legs.
    // ========================================================================
    if (parts.is_physics_based) {
        // TORQUE-BASED CONTROL: Apply torque to head toward target
        // The angular constraint solver will propagate rotation through joints

        float desired_rotation = target_angle + VISUAL_ORIENTATION_OFFSET;
        float current_rotation = head.rotation_z;

        // Calculate angle difference (normalized to [-π, π])
        float angle_error = desired_rotation - current_rotation;
        angle_error = dyn.normalize_angle(angle_error);

        // ================================================================
        // CONTEXTUAL STIFFNESS MODIFIERS
        // ================================================================
        // Two factors affect how quickly body rotates:
        // 1. Distance to target: closer = faster rotation (urgency)
        // 2. Movement speed: standing = fastest, sprint = slowest
        // ================================================================

        // Distance factor: close targets need quick rotation
        float target_distance = std::sqrt(dx * dx + dy * dy);
        float distance_factor = 1.0f;
        if (target_distance < 5.0f) {
            // Close target (< 5m): boost rotation (1.0 → 1.4 as distance → 0)
            distance_factor = 1.0f + 0.4f * (1.0f - target_distance / 5.0f);
        } else if (target_distance > 15.0f) {
            // Far target (> 15m): reduce rotation (1.0 → 0.7 at 30m+)
            float far_t = std::min((target_distance - 15.0f) / 15.0f, 1.0f);
            distance_factor = 1.0f - 0.3f * far_t;
        }

        // Speed factor: harder to rotate while moving fast
        float current_speed = std::sqrt(parts.velocity_x * parts.velocity_x +
                                        parts.velocity_y * parts.velocity_y);
        float speed_factor = 1.0f;
        const float WALK_SPEED = parts.dynamics.max_walk_speed;
        const float SPRINT_SPEED = parts.dynamics.max_run_speed;

        if (current_speed < 0.3f) {
            // Standing still: boost rotation (1.4x)
            speed_factor = 1.4f;
        } else if (current_speed < WALK_SPEED) {
            // Slow walk / stopping: interpolate 1.4 → 1.0
            float t = current_speed / WALK_SPEED;
            speed_factor = 1.4f - 0.4f * t;
        } else if (current_speed > SPRINT_SPEED) {
            // Sprinting: reduce rotation (0.6x)
            speed_factor = 0.6f;
        } else if (current_speed > WALK_SPEED * 1.5f) {
            // Fast walk / jog: interpolate 1.0 → 0.6
            float t = (current_speed - WALK_SPEED * 1.5f) / (SPRINT_SPEED - WALK_SPEED * 1.5f);
            speed_factor = 1.0f - 0.4f * t;
        }
        // Walking speed (1.5-2.25 m/s): speed_factor stays 1.0

        // Combined modifier affects body rotation (not head - head always tracks)
        float body_stiffness_modifier = distance_factor * speed_factor;

        // ================================================================
        // EYE-ONLY DEAD ZONE: Head stays still, only eyes track mouse
        // ================================================================
        // Human eyes can rotate ~±25° in their sockets without head movement.
        // Within this zone, the foveal focus shader shows where eyes are looking.
        // Only when gaze exceeds this zone does head start rotating to follow.
        //
        // This creates the natural "eyes lead, head follows" hierarchy:
        //   Mouse → Eyes (instant, shader) → Head (delayed) → Body (more delayed)
        // ================================================================
        const float EYE_DEAD_ZONE = parts.dynamics.eye_dead_zone;
        const float HEAD_ENGAGE_ZONE = parts.dynamics.head_engage_zone;

        float abs_error = std::abs(angle_error);
        float head_engagement = 0.0f;

        if (abs_error <= EYE_DEAD_ZONE) {
            // Within eye-only zone: no head rotation, eyes track via foveal focus shader
            head_engagement = 0.0f;
        } else if (abs_error <= HEAD_ENGAGE_ZONE) {
            // Transition zone: head gradually engages
            float t = (abs_error - EYE_DEAD_ZONE) / (HEAD_ENGAGE_ZONE - EYE_DEAD_ZONE);
            head_engagement = t * t;  // Quadratic ramp for smooth onset
        } else {
            // Full head tracking
            head_engagement = 1.0f;
        }

        // Spring-damper look-at control (scaled by head engagement)
        // τ = k_p * error - k_d * ω
        // Stiffness/damping derived from FORGE attributes (see docs/FORGE_DYNAMICS_PROPOSAL.md)
        // Hunter (180ms, 800W) → stiffness ~56, Scholar (200ms, 300W) → stiffness ~31

        float look_at_torque = head_engagement * (parts.dynamics.look_at_stiffness * angle_error - parts.dynamics.look_at_damping * head.omega_z);
        head.torque_z += look_at_torque;

        // DEBUG: Verify torque is being applied
        static int torque_debug_frame = 0;
        torque_debug_frame++;
        if (torque_debug_frame % 60 == 0) {
            std::cout << "[TORQUE_DEBUG] engagement=" << head_engagement
                      << " stiffness=" << parts.dynamics.look_at_stiffness
                      << " angle_error=" << angle_error
                      << " torque=" << look_at_torque
                      << " total_torque_z=" << head.torque_z << std::endl;
        }

        // ================================================================
        // CASCADING VELOCITY COUPLING (Prince of Persia fluid motion)
        // ================================================================
        // When head exceeds dead zone relative to body, proactively couple
        // angular velocity down the chain. This makes body "commit to turning"
        // rather than waiting for physics to slowly propagate torque.
        //
        // Dead zones (degrees):
        //   - Head can rotate ~45° before neck needs to follow
        //   - Neck can rotate ~30° before torso needs to follow
        //   - Torso can rotate ~20° before hips need to follow
        //
        // Coupling factors: how much of parent's omega_z passes to child
        //   - 0.8 = 80% inheritance (fluid following)
        //   - Decreases down chain for some independence
        // ================================================================

        const float NECK_DEAD_ZONE = parts.dynamics.neck_dead_zone;
        const float TORSO_DEAD_ZONE = parts.dynamics.torso_dead_zone;
        const float HIPS_DEAD_ZONE = parts.dynamics.hips_dead_zone;

        // Only apply coupling if we have body parts
        if (parts.neck != 0 && parts.torso != 0 && parts.hips != 0) {
            Particle& neck = particles_view[parts.neck];
            Particle& torso = particles_view[parts.torso];
            Particle& abdomen = particles_view[parts.abdomen];
            Particle& hips_p = particles_view[parts.hips];

            // Calculate angle differences (head to body)
            float head_vs_neck = dyn.normalize_angle(head.rotation_z - neck.rotation_z);
            float neck_vs_torso = dyn.normalize_angle(neck.rotation_z - torso.rotation_z);
            float torso_vs_hips = dyn.normalize_angle(torso.rotation_z - hips_p.rotation_z);

            // ================================================================
            // VOLITIONAL TORQUE: Each segment actively rotates toward target
            // ================================================================
            // Instead of passively copying omega_z from parent to child,
            // each body segment applies its OWN torque toward the target.
            // This creates natural, coordinated movement.
            //
            // Key insight: humanoids rotate body parts volitionally - each
            // segment anticipates when upstream approaches its joint limit.
            // See: docs/entity_dynamics_volition.md
            // ================================================================

            const float VOLITION_THRESHOLD = 0.7f;  // Start anticipating at 70% of limit

            // Cascade 1: Head approaching neck limit → torso applies volitional torque
            if (std::abs(head_vs_neck) > NECK_DEAD_ZONE * VOLITION_THRESHOLD) {
                // Calculate urgency: 0 at threshold, 1.0 at full limit
                float urgency = (std::abs(head_vs_neck) - NECK_DEAD_ZONE * VOLITION_THRESHOLD)
                              / (NECK_DEAD_ZONE * (1.0f - VOLITION_THRESHOLD));
                urgency = std::min(1.0f, urgency);

                // Torso applies its own volitional torque toward target
                // Stiffness modified by distance/speed context
                float torso_to_target = dyn.normalize_angle(desired_rotation - torso.rotation_z);
                float torso_stiffness = parts.dynamics.look_at_stiffness * 0.7f * body_stiffness_modifier;
                // NOTE: All particles are now dynamic (is_kinematic removed)
                torso.torque_z += urgency * torso_stiffness * torso_to_target
                               - urgency * parts.dynamics.look_at_damping * torso.omega_z;

                // Abdomen follows torso pattern (intermediate segment)
                float abdomen_to_target = dyn.normalize_angle(desired_rotation - abdomen.rotation_z);
                float abdomen_stiffness = parts.dynamics.look_at_stiffness * 0.6f * body_stiffness_modifier;
                abdomen.torque_z += urgency * abdomen_stiffness * abdomen_to_target
                                 - urgency * parts.dynamics.look_at_damping * abdomen.omega_z;
            }

            // Cascade 2: Torso approaching hips limit → hips apply volitional torque
            if (std::abs(torso_vs_hips) > TORSO_DEAD_ZONE * VOLITION_THRESHOLD) {
                float urgency = (std::abs(torso_vs_hips) - TORSO_DEAD_ZONE * VOLITION_THRESHOLD)
                              / (TORSO_DEAD_ZONE * (1.0f - VOLITION_THRESHOLD));
                urgency = std::min(1.0f, urgency);

                // Hips stiffness also modified by context
                float hips_to_target = dyn.normalize_angle(desired_rotation - hips_p.rotation_z);
                float hips_stiffness = parts.dynamics.look_at_stiffness * 0.5f * body_stiffness_modifier;
                hips_p.torque_z += urgency * hips_stiffness * hips_to_target
                                - urgency * parts.dynamics.look_at_damping * hips_p.omega_z;
            }

            // ================================================================
            // SUSTAINED GAZE → BODY ROTATION
            // ================================================================
            // If head stays rotated away from body for >1 second, gradually
            // rotate hips to face head direction. This creates natural
            // "commit to looking" behavior without affecting quick glances.

            // Calculate head-vs-hips angle (how far head is rotated from body forward)
            float head_vs_body = dyn.normalize_angle(head.rotation_z - hips_p.rotation_z);
            float offset_magnitude = std::abs(head_vs_body);

            // Only track when head is significantly rotated
            if (offset_magnitude > parts.dynamics.gaze_offset_threshold) {
                // Head is rotated - accumulate timer
                parts.head_offset_timer += static_cast<float>(delta_time);
                parts.head_offset_angle = head_vs_body;  // Remember which direction

                // After sustained gaze, apply torque to hips
                if (parts.head_offset_timer > parts.dynamics.gaze_hold_threshold) {
                    // Calculate how much to rotate body
                    float gaze_duration = parts.head_offset_timer - parts.dynamics.gaze_hold_threshold;
                    float turn_strength = std::min(gaze_duration, 1.0f);  // Ramp up over 1s to full strength

                    // Apply torque to hips toward head direction
                    float body_torque = parts.head_offset_angle * turn_strength * parts.dynamics.body_turn_rate;
                    hips_p.torque_z += body_torque;
                }
            } else {
                // Head returned to forward - reset timer
                parts.head_offset_timer = 0.0f;
            }
        }

        // DEBUG: Print tracking info (once per second)
        static int debug_counter = 0;
        if (debug_counter++ % 60 == 0) {
            std::cout << "\n[TORQUE-BASED LOOK-AT]" << std::endl;
            std::cout << "  Target: " << (target_angle * 180.0f / M_PI) << "°" << std::endl;
            std::cout << "  Current: " << (current_rotation * 180.0f / M_PI) << "°" << std::endl;
            std::cout << "  Error: " << (angle_error * 180.0f / M_PI) << "°" << std::endl;
            std::cout << "  Torque applied: " << look_at_torque << " N·m" << std::endl;
            std::cout << "  omega_z: " << head.omega_z << " rad/s" << std::endl;
        }

        // DON'T set rotation_z directly - physics handles it via torque integration
        // DON'T reposition limbs - gluons handle positions

        // Mark BVH as dirty since physics will change orientations
        impl_->get_particle_system().mark_bvh_dirty();
        return;  // Exit early for physics-based mode
    }

    // ========================================================================
    // NON-PHYSICS LOOK-AT: Direct rotation (original behavior)
    // ========================================================================
    // For non-physics humanoids, keep the manual hierarchical rotation approach

    // DEBUG: Print tracking info (first 3 only)
    static int debug_counter = 0;
    if (debug_counter++ < 3) {
        std::cout << "\n[HEAD TRACKING DEBUG]" << std::endl;
        std::cout << "  Head position: (" << head_x << ", " << head_y << ")" << std::endl;
        std::cout << "  Mouse position: (" << mouse_world_x << ", " << mouse_world_y << ")" << std::endl;
        std::cout << "  Delta: (" << dx << ", " << dy << ")" << std::endl;
        std::cout << "  Target angle: " << target_angle << " rad (" << (target_angle * 180.0f / M_PI) << "°)" << std::endl;
        std::cout << "  Base rotation: " << parts.base_rotation << " rad (" << (parts.base_rotation * 180.0f / M_PI) << "°)" << std::endl;
    }

    // ========================================================================
    // FLUID CASCADING ROTATION: Natural head-first with gradual follow
    // ========================================================================
    // Head rotates freely up to 20°. Beyond 20°, torso gradually helps.
    // Beyond 15° torso rotation, hips gradually help.
    // Result: Smooth, natural looking rotation chain.
    // ========================================================================

    const float HEAD_COMFORT = 0.35f;   // 20° - torso starts helping beyond this
    const float TORSO_COMFORT = 0.26f;  // 15° - hips start helping beyond this
    const float HEAD_MAX = dyn.config_.head_max_rotation;   // ±45° (0.785 rad)
    const float TORSO_MAX = dyn.config_.torso_max_rotation; // ±45° (0.785 rad)

    float angle_to_target = dyn.normalize_angle(target_angle - parts.base_rotation);

    // Head takes what it needs (clamped to max)
    float head_rotation = std::clamp(angle_to_target, -HEAD_MAX, HEAD_MAX);
    float torso_rotation = 0.0f;
    float body_rotation = parts.base_rotation;

    // If head exceeds comfort zone, torso gradually absorbs the strain
    float head_strain = fabsf(head_rotation) - HEAD_COMFORT;
    if (head_strain > 0) {
        // Torso takes 70% of the excess, smoothly
        float torso_help = head_strain * 0.7f;
        torso_help = std::min(torso_help, TORSO_MAX);

        if (head_rotation > 0) {
            torso_rotation = torso_help;
            head_rotation -= torso_help;
        } else {
            torso_rotation = -torso_help;
            head_rotation += torso_help;
        }
    }

    // If torso exceeds its comfort zone, hips gradually rotate
    float torso_strain = fabsf(torso_rotation) - TORSO_COMFORT;
    if (torso_strain > 0) {
        // Hips take 60% of torso's excess strain
        float hips_help = torso_strain * 0.6f;

        if (torso_rotation > 0) {
            body_rotation = dyn.normalize_angle(parts.base_rotation + hips_help);
            torso_rotation -= hips_help;
        } else {
            body_rotation = dyn.normalize_angle(parts.base_rotation - hips_help);
            torso_rotation += hips_help;
        }
    }

    // ========================================================================
    // WALKING TURN: When moving, hips gradually catch up to look direction
    // ========================================================================
    // If walking and looking sideways, feet naturally follow eyes over 2-4 sec.
    // This makes movement feel natural - you turn toward where you're going.
    // ========================================================================
    if (parts.is_moving && parts.is_volitional) {
        // How far are hips from look direction?
        float hips_to_look = dyn.normalize_angle(target_angle - body_rotation);

        // Only apply if there's meaningful difference (>10°)
        const float WALK_TURN_THRESHOLD = parts.dynamics.walk_turn_threshold;
        if (fabsf(hips_to_look) > WALK_TURN_THRESHOLD) {
            float max_turn = parts.dynamics.walk_turn_rate * static_cast<float>(delta_time);

            float turn_amount = std::clamp(hips_to_look, -max_turn, max_turn);
            body_rotation = dyn.normalize_angle(body_rotation + turn_amount);

            // Reduce head/torso strain since hips are catching up
            float relief = turn_amount;
            if (fabsf(torso_rotation) > 0.01f) {
                float torso_relief = std::clamp(relief, -fabsf(torso_rotation), fabsf(torso_rotation));
                torso_rotation -= torso_relief;
                relief -= torso_relief;
            }
            if (fabsf(head_rotation) > HEAD_COMFORT && fabsf(relief) > 0.01f) {
                head_rotation -= relief;
                head_rotation = std::clamp(head_rotation, -HEAD_MAX, HEAD_MAX);
            }
        }
    }

    // Compensate for any accumulated error to ensure head faces target
    float achieved = dyn.normalize_angle(body_rotation + torso_rotation + head_rotation);
    float error = dyn.normalize_angle(target_angle - achieved);
    head_rotation = std::clamp(head_rotation + error, -HEAD_MAX, HEAD_MAX);

    // DEBUG: Print rotation breakdown (first 3 only, matches counter above)
    if (debug_counter <= 3) {
        std::cout << "  Angle to target: " << angle_to_target << " rad (" << (angle_to_target * 180.0f / M_PI) << "°)" << std::endl;
        std::cout << "  Applied rotations:" << std::endl;
        std::cout << "    Head: " << head_rotation << " rad (" << (head_rotation * 180.0f / M_PI) << "°)" << std::endl;
        std::cout << "    Torso: " << torso_rotation << " rad (" << (torso_rotation * 180.0f / M_PI) << "°)" << std::endl;
        std::cout << "    Body: " << body_rotation << " rad (" << (body_rotation * 180.0f / M_PI) << "°)" << std::endl;
        std::cout << "    Total head rotation_z: " << (body_rotation + torso_rotation + head_rotation)
                  << " rad (" << ((body_rotation + torso_rotation + head_rotation) * 180.0f / M_PI) << "°)" << std::endl;
    }

    if (dyn.config_.debug_rotations) {
        std::cout << "[LookAt] target=" << target_angle
                  << " angle_to_target=" << angle_to_target
                  << " head=" << head_rotation
                  << " torso=" << torso_rotation
                  << " body=" << body_rotation << std::endl;
    }

    // Head children (hair, ears) inherit head rotation
    // Positioning handled by maintain_entity_shape
    float head_pivot_angle = body_rotation + torso_rotation + head_rotation;
    for (unsigned int child_id : parts.head_child_particles) {
        if (child_id < num_particles) {
            particles_view[child_id].rotation_z = head_pivot_angle;
        }
    }

    // ========================================================================
    // DIRECT HIERARCHICAL ROTATION (non-physics mode)
    // ========================================================================
    // Only set rotation_z values here. Positioning is handled by
    // maintain_entity_shape() which uses hips.rotation_z with rest offsets.
    //
    // Hierarchy: mouse → head → neck → torso → abdomen → hips
    // Each level has rotation limits, excess propagates down.
    // ========================================================================

    // Spine particles: hierarchical rotation
    if (parts.head != 0 && parts.head < num_particles) {
        particles_view[parts.head].rotation_z = body_rotation + torso_rotation + head_rotation;
    }
    if (parts.neck != 0 && parts.neck < num_particles) {
        particles_view[parts.neck].rotation_z = body_rotation + torso_rotation + head_rotation;
    }
    if (parts.torso != 0 && parts.torso < num_particles) {
        particles_view[parts.torso].rotation_z = body_rotation + torso_rotation;
    }
    if (parts.abdomen != 0 && parts.abdomen < num_particles) {
        particles_view[parts.abdomen].rotation_z = body_rotation + torso_rotation;
    }
    if (parts.hips != 0 && parts.hips < num_particles) {
        particles_view[parts.hips].rotation_z = body_rotation;
    }

    // Limbs: inherit body rotation (positioning handled by maintain_entity_shape)
    for (unsigned int leg_id : parts.left_leg_particles) {
        if (leg_id < num_particles) {
            particles_view[leg_id].rotation_z = body_rotation;
        }
    }
    for (unsigned int leg_id : parts.right_leg_particles) {
        if (leg_id < num_particles) {
            particles_view[leg_id].rotation_z = body_rotation;
        }
    }

    // Arms follow torso rotation
    float arm_rotation = body_rotation + torso_rotation;
    for (unsigned int arm_id : parts.left_arm_particles) {
        if (arm_id < num_particles) {
            particles_view[arm_id].rotation_z = arm_rotation;
        }
    }
    for (unsigned int arm_id : parts.right_arm_particles) {
        if (arm_id < num_particles) {
            particles_view[arm_id].rotation_z = arm_rotation;
        }
    }

    // Track base rotation for next frame's angle_diff calculation
    parts.base_rotation = body_rotation;
}

void HumanoidLocomotion::update_walk_cycle(HumanoidParts& parts, double delta_time) {
    auto& dyn = impl_->get_dynamics_system();
    if (!parts.registered) return;

    // THREAD SAFETY: Use WriteView for automatic lock management
    auto particles_view = impl_->get_particle_system().lock_particles_for_write();

    // NOTE: update_locomotion() is called from update_post_physics()
    // to run AFTER gluon corrections, preventing drift

    // Get current hips position to detect movement
    Particle& hips = particles_view[parts.hips];
    float current_x = hips.x;
    float current_y = hips.y;

    // Detect movement by comparing to previous position
    float dx = current_x - parts.prev_world_x;
    float dy = current_y - parts.prev_world_y;
    float measured_distance = sqrtf(dx * dx + dy * dy);
    const float MOVEMENT_THRESHOLD = 0.001f;  // Minimum distance to trigger walk

    // Walk-cycle drive source. With the kinematic-root refactor, the stance
    // hip is a dependent variable — derived via reverse-FK from the planted
    // foot. If the walk cycle were driven purely by measured hip movement,
    // we'd get a circular stall: hip pinned by reverse-FK → no measured
    // delta → walk cycle doesn't advance → joint rotations don't change →
    // reverse-FK keeps hip pinned. So we drive the cycle from INTENT
    // (target_vx/vy) when volitional. The animation advances, joints rotate,
    // and the reverse-FK chain pivots the hip forward over the stance ankle
    // — which is how real bipedal locomotion actually works.
    float target_speed_sq = parts.target_vx * parts.target_vx
                          + parts.target_vy * parts.target_vy;
    float target_speed    = sqrtf(target_speed_sq);
    // Pace intent at the FORGE cap: update_locomotion clamps the actual
    // velocity to effective_max, so the gait cadence must not outrun the
    // body it drives (an over-commanded entity would otherwise cycle its
    // legs at the raw commanded speed while translating at the cap).
    {
        float effective_max = parts.dynamics.max_run_speed * parts.speed_modifier;
        if (effective_max > 0.001f && target_speed > effective_max) {
            target_speed = effective_max;
        }
    }
    float movement_distance = (parts.is_volitional && target_speed > 0.01f)
        ? target_speed * static_cast<float>(delta_time)
        : measured_distance;

    parts.is_moving = (movement_distance > MOVEMENT_THRESHOLD);

    // =========================================================================
    // VOLITIONAL FRICTION CONTROL
    // =========================================================================
    // SEPARATION OF CONCERNS (Dynamics vs Physics):
    //
    //   Dynamics (this system):
    //   - Entity-aware: knows humanoid structure, volitional state (is_moving)
    //   - Sets friction VALUE based on behavioral state
    //   - Policy: WHEN and WHERE to apply friction
    //
    //   Physics (physics_system_v4.cpp):
    //   - Particle-aware: no concept of "entity" or "Humanoid"
    //   - Uses friction value during contact constraint solving
    //   - Mechanism: HOW friction is applied (Coulomb model, impulse clamping)
    //
    // HOW FRICTION PROPAGATES THROUGH GLUONS:
    //   The physics solver is iterative (16 iterations). Each iteration:
    //   1. Gluon constraints propagate velocity changes through body chain
    //   2. Contact constraints apply friction at floor contact points
    //   3. Friction slows feet → gluons slow connected body → torso slows
    //   After 16 iterations, friction effect has propagated through entire body.
    //
    // WHY FEET ONLY:
    //   Friction coefficient affects particle-particle contacts. Setting high
    //   friction on feet affects feet↔floor contacts. The iterative solver
    //   then propagates this anchoring effect up through the gluon chain.
    //   Setting friction on torso would affect torso↔other collisions, not
    //   the ground anchoring we want.
    // =========================================================================
    const float MOVING_FRICTION = parts.dynamics.moving_friction;
    const float STATIONARY_FRICTION = parts.dynamics.stationary_friction;
    // Friction disabled when volitional (intent to move) - allows starting from rest
    // Drift handling: deceleration in update_locomotion() brakes unwanted movement
    bool wants_frictionless = parts.is_volitional;
    float target_friction = wants_frictionless ? MOVING_FRICTION : STATIONARY_FRICTION;

    // Apply friction to feet (first particle in each leg chain = foot)
    if (!parts.left_leg_particles.empty()) {
        particles_view[parts.left_leg_particles[0]].friction = target_friction;
    }
    if (!parts.right_leg_particles.empty()) {
        particles_view[parts.right_leg_particles[0]].friction = target_friction;
    }

    // Gradual body turn toward look direction during FK walk
    if (parts.fk_walk_enabled && parts.is_volitional && parts.has_custom_target) {
        float dx_look = parts.custom_target_x - current_x;
        float dy_look = parts.custom_target_y - current_y;
        float target_angle = atan2f(dx_look, dy_look);
        float hips_to_look = dyn.normalize_angle(target_angle - parts.base_rotation);

        const float WALK_TURN_THRESHOLD = parts.dynamics.walk_turn_threshold;
        if (fabsf(hips_to_look) > WALK_TURN_THRESHOLD) {
            float max_turn = parts.dynamics.walk_turn_rate * static_cast<float>(delta_time);
            float turn_amount = std::clamp(hips_to_look, -max_turn, max_turn);
            parts.base_rotation = dyn.normalize_angle(parts.base_rotation + turn_amount);
            hips.rotation_z = parts.base_rotation;

            // Walk direction follows body rotation — velocity rotates with facing
            // Convention: base_rotation 0 = North (+Y), π/2 = East (+X) — CW from North
            // Skip this override in body-relative mode: velocity is recomputed below
            if (!parts.use_body_relative) {
                float target_speed = std::sqrt(parts.target_vx * parts.target_vx +
                                               parts.target_vy * parts.target_vy);
                if (target_speed > 0.001f) {
                    parts.target_vx = target_speed * std::sin(parts.base_rotation);
                    parts.target_vy = target_speed * std::cos(parts.base_rotation);
                }
            }
        }
    }

    // Body-relative velocity: compute world-space target from base_rotation + local inputs
    // Convention: base_rotation 0 = North (+Y), π/2 = East (+X) — CW from North
    // Forward along facing: (sin(rot), cos(rot))
    // Right (90° CW):       (cos(rot), -sin(rot))
    if (parts.use_body_relative) {
        float sin_rot = std::sin(parts.base_rotation);
        float cos_rot = std::cos(parts.base_rotation);
        parts.target_vx = parts.local_forward * sin_rot + parts.local_right * cos_rot;
        parts.target_vy = parts.local_forward * cos_rot - parts.local_right * sin_rot;
    }

    // =========================================================================
    // TURN-IN-PLACE: Rotate toward look target when standing still
    // =========================================================================
    // Fires when NOT moving but look-at target diverges from facing.
    // Drives turn_phase from rotation delta for turn-in-place animation.
    //
    // NOTE: the yaw cascade (head → torso → hips exponential follow) now
    // owns rotation when it's been initialised on the entity. When
    // cascade is active, this legacy path is dead code: it would write
    // base_rotation in parallel with the cascade and set
    // is_turning_in_place = true, which triggers MODE 1 (turn clip) and
    // poses the legs in a stride while Eva is just standing. Gate it
    // off so the idle clip can take over.
    // =========================================================================
    // Cascade owns rotation when active — force is_turning_in_place OFF
    // so the FK pose selector doesn't fall into MODE 1 (turn clip). If we
    // don't explicitly clear, a single frame where legacy turn-in-place
    // fired (before the cascade inited, or during a transient) can latch
    // the flag at true and keep the turn clip running forever — that's
    // the "twisted legs just standing" in Eden.
    if (parts.yaw_cascade_inited) {
        parts.is_turning_in_place = false;
    }

    if (!parts.yaw_cascade_inited
        && parts.fk_turn_enabled && !parts.is_volitional && parts.has_custom_target) {
        float dx_look = parts.custom_target_x - current_x;
        float dy_look = parts.custom_target_y - current_y;
        float target_angle = atan2f(dx_look, dy_look);
        float hips_to_look = dyn.normalize_angle(target_angle - parts.base_rotation);

        const float TURN_THRESHOLD = parts.dynamics.walk_turn_threshold;
        if (fabsf(hips_to_look) > TURN_THRESHOLD) {
            parts.is_turning_in_place = true;

            float max_turn = parts.dynamics.stand_turn_rate * static_cast<float>(delta_time);
            float turn_amount = std::clamp(hips_to_look, -max_turn, max_turn);
            parts.base_rotation = dyn.normalize_angle(parts.base_rotation + turn_amount);
            hips.rotation_z = parts.base_rotation;

            // Drive turn phase proportionally to rotation speed
            // ~0.15 rad of turn per half-cycle (one step)
            float rotation_speed = fabsf(turn_amount) / static_cast<float>(delta_time);
            const float TURN_STRIDE = 0.15f;  // radians of rotation per step
            float turn_freq = rotation_speed / TURN_STRIDE;
            turn_freq = std::max(0.5f, std::min(4.0f, turn_freq));

            float prev_turn_phase = parts.turn_phase;
            parts.turn_phase += static_cast<float>(M_PI) * turn_freq * static_cast<float>(delta_time);
            parts.turn_phase = fmodf(parts.turn_phase, 2.0f * static_cast<float>(M_PI));

            // Detect half-cycle boundary for step alternation
            int prev_half = static_cast<int>(prev_turn_phase / static_cast<float>(M_PI));
            int curr_half = static_cast<int>(parts.turn_phase / static_cast<float>(M_PI));
            if (curr_half != prev_half) {
                parts.fk_walk_side_right = !parts.fk_walk_side_right;
            }
        } else {
            parts.is_turning_in_place = false;
        }
    } else {
        parts.is_turning_in_place = false;
    }

    // Decay turn phase to neutral when not turning
    if (!parts.is_turning_in_place && parts.turn_phase > 0.001f) {
        const float TURN_RETURN_RATE = 6.0f;
        float pi_f = static_cast<float>(M_PI);
        float target_phase = 0.0f;
        if (parts.turn_phase > pi_f * 0.5f && parts.turn_phase < pi_f * 1.5f) {
            target_phase = pi_f;
        }
        float diff = target_phase - parts.turn_phase;
        if (fabsf(diff) < 0.05f) {
            parts.turn_phase = target_phase;
        } else {
            float step = TURN_RETURN_RATE * static_cast<float>(delta_time);
            parts.turn_phase += (diff > 0 ? 1.0f : -1.0f) * std::min(step, fabsf(diff));
        }
        if (parts.turn_phase < 0.0f) parts.turn_phase += 2.0f * pi_f;
        parts.turn_phase = fmodf(parts.turn_phase, 2.0f * pi_f);
    }

    // Track rotation for next frame
    parts.prev_base_rotation = parts.base_rotation;

    // Advance walk phase if moving
    if (parts.is_moving) {
        // Reset idle phase when movement starts
        parts.idle_phase = 0.0f;

        // ANIMATION-DRIVEN SPEED: Calculate animation frequency from actual movement speed
        // This makes legs swing faster when running, slower when walking
        //
        // STRIDE_LENGTH is per-step (half-stride). Full cycle (2π) = 2 steps.
        // Phase formula: d_phase = (speed / stride) * π * dt
        // π (not 2π) because stride_length covers one half-cycle.
        //
        // When running (speed > walk threshold), use run stride length (0.85m)
        // which produces a lower frequency for the same speed → longer, loping strides
        // Walk stride = 0.65m, Run stride = 0.85m
        float movement_speed = movement_distance / static_cast<float>(delta_time);

        // Blend stride length based on speed relative to FORGE-derived walk/run thresholds
        float stride_length = parts.dynamics.walk_stride_length;
        if (parts.fk_run_enabled) {
            float walk_threshold = parts.dynamics.max_walk_speed * 0.8f;
            float run_threshold = parts.dynamics.max_walk_speed * 1.2f;
            if (movement_speed > run_threshold) {
                stride_length = parts.dynamics.run_stride_length;
            } else if (movement_speed > walk_threshold) {
                float t = (movement_speed - walk_threshold) / (run_threshold - walk_threshold);
                stride_length = parts.dynamics.walk_stride_length + t * (parts.dynamics.run_stride_length - parts.dynamics.walk_stride_length);
            }
        }

        float animation_frequency = movement_speed / stride_length;

        // Clamp frequency to reasonable range (0.5-5.0 Hz)
        animation_frequency = std::max(0.5f, std::min(5.0f, animation_frequency));

        float prev_phase = parts.walk_phase;
        parts.walk_phase += static_cast<float>(M_PI) * animation_frequency * delta_time;
        parts.walk_phase = fmodf(parts.walk_phase, 2.0f * static_cast<float>(M_PI));  // Keep in [0, 2π]

        // Detect half-cycle boundary (step transition R↔L)
        if (parts.fk_walk_enabled && parts.is_volitional) {
            int prev_half = static_cast<int>(prev_phase / static_cast<float>(M_PI));
            int curr_half = static_cast<int>(parts.walk_phase / static_cast<float>(M_PI));
            if (curr_half != prev_half) {
                parts.fk_walk_side_right = !parts.fk_walk_side_right;
            }
        }
    } else {
        // Drive phase linearly to nearest neutral (0 or π). ~0.3s return.
        const float RETURN_RATE = 6.0f;  // rad/s
        float target_phase = 0.0f;
        float pi_f = static_cast<float>(M_PI);
        if (parts.walk_phase > pi_f * 0.5f && parts.walk_phase < pi_f * 1.5f) {
            target_phase = pi_f;  // closer to π
        }
        float diff = target_phase - parts.walk_phase;
        if (fabsf(diff) < 0.05f) {
            parts.walk_phase = target_phase;
        } else {
            float step = RETURN_RATE * static_cast<float>(delta_time);
            parts.walk_phase += (diff > 0 ? 1.0f : -1.0f) * std::min(step, fabsf(diff));
        }
        if (parts.walk_phase < 0.0f) parts.walk_phase += 2.0f * pi_f;
        parts.walk_phase = fmodf(parts.walk_phase, 2.0f * pi_f);

        // Advance idle phase when stationary (for breathing/weight-shift animation)
        // Phase wraps at 2π, rate = 2π / cycle_ms → ~0.25 Hz at 4000ms cycle
        if (parts.fk_idle_enabled) {
            float idle_rate = (2.0f * pi_f) / (parts.fk_idle_cycle_ms * 0.001f);  // rad/s
            parts.idle_phase += idle_rate * static_cast<float>(delta_time);
            parts.idle_phase = fmodf(parts.idle_phase, 2.0f * pi_f);
        }
    }

    // FK walk replaces sinusoidal impulses. Phase + side stored above for
    // post-physics FK application. Fallback for humanoids without FK walk clips.
    if (!parts.fk_walk_enabled) {
        // Legacy sinusoidal velocity impulses (for humanoids without FK walk clips)
        const float LEG_SWING_STRENGTH = 1.5f;
        const float ARM_SWING_STRENGTH = 1.0f;
        float cos_phase = cosf(parts.walk_phase);

        float left_leg_vel_y = LEG_SWING_STRENGTH * cos_phase;
        float right_leg_vel_y = -left_leg_vel_y;
        float left_arm_vel_y = -ARM_SWING_STRENGTH * cos_phase;
        float right_arm_vel_y = -left_arm_vel_y;

        if (hips.vy < 0) {
            left_leg_vel_y = -left_leg_vel_y;
            right_leg_vel_y = -right_leg_vel_y;
            left_arm_vel_y = -left_arm_vel_y;
            right_arm_vel_y = -right_arm_vel_y;
        }

        bool wants_leg_animation = parts.is_moving && parts.is_volitional;
        if (wants_leg_animation) {
            if (!parts.left_leg_particles.empty())
                particles_view[parts.left_leg_particles[0]].vy += left_leg_vel_y * delta_time;
            if (!parts.right_leg_particles.empty())
                particles_view[parts.right_leg_particles[0]].vy += right_leg_vel_y * delta_time;
            if (!parts.left_arm_particles.empty())
                particles_view[parts.left_arm_particles.back()].vy += left_arm_vel_y * delta_time;
            if (!parts.right_arm_particles.empty())
                particles_view[parts.right_arm_particles.back()].vy += right_arm_vel_y * delta_time;
        }
    }

    // Update previous position for next frame
    parts.prev_world_x = current_x;
    parts.prev_world_y = current_y;

    // Mark BVH as dirty since we changed particle positions
    impl_->get_particle_system().mark_bvh_dirty();
}

void HumanoidLocomotion::update_locomotion(HumanoidParts& parts, double delta_time, ParticleSystem::WriteView& particles_view, const char* phase) {
    auto& dyn = impl_->get_dynamics_system();
    if (parts.all_particle_indices.empty()) return;

    // Suppress locomotion during FULL_BODY one-shot animations (e.g., kicks).
    // The character must plant and execute — can't slide forward mid-kick.
    // UPPER_BODY overlays (punches) keep locomotion active so legs walk.
    if (parts.fk_playing && parts.fk_active_clip &&
        parts.fk_active_clip->body_region == BodyRegion::FULL_BODY) {
        for (unsigned int id : parts.all_particle_indices) {
            particles_view[id].vx = 0.0f;
            particles_view[id].vy = 0.0f;
        }
        return;
    }

    // Reference particle (hips) for current velocity
    Particle& hips = particles_view[parts.hips];

    // 1. Get current velocity from hips
    float vx = hips.vx;
    float vy = hips.vy;
    float current_speed = std::sqrt(vx * vx + vy * vy);


    // 1b. Clamp target velocity to effective max speed (FORGE-derived cap)
    float effective_max = parts.dynamics.max_run_speed * parts.speed_modifier;
    float target_speed_raw = std::sqrt(parts.target_vx * parts.target_vx +
                                       parts.target_vy * parts.target_vy);
    float clamped_tvx = parts.target_vx;
    float clamped_tvy = parts.target_vy;
    if (target_speed_raw > effective_max && target_speed_raw > 0.001f) {
        float scale = effective_max / target_speed_raw;
        clamped_tvx *= scale;
        clamped_tvy *= scale;
    }

    // 2. Calculate delta to target velocity
    float dvx = clamped_tvx - vx;
    float dvy = clamped_tvy - vy;
    float dv_mag = std::sqrt(dvx * dvx + dvy * dvy);

    // 3. Determine if accelerating or decelerating (use clamped target)
    float target_speed = std::sqrt(clamped_tvx * clamped_tvx + clamped_tvy * clamped_tvy);
    float dot = vx * clamped_tvx + vy * clamped_tvy;
    bool is_decelerating = (target_speed < current_speed) || (dot < 0);

    // 4. Ensure max_deceleration is set (calculate from friction × g if 0)
    if (parts.dynamics.max_deceleration <= 0.0f) {
        parts.dynamics.max_deceleration = parts.friction * PhysicsV4::GRAVITY;
    }

    // 5. Choose acceleration limit
    float max_dv_per_sec = is_decelerating ? parts.dynamics.max_deceleration : parts.dynamics.max_acceleration;
    float max_dv = max_dv_per_sec * static_cast<float>(delta_time);

    // 6. Clamp velocity change
    if (dv_mag > max_dv && dv_mag > 0.001f) {
        float scale = max_dv / dv_mag;
        dvx *= scale;
        dvy *= scale;
    }

    // 7. Calculate new velocity
    float new_vx = vx + dvx;
    float new_vy = vy + dvy;

    // 8. Apply to ALL entity particles (key insight!)
    for (unsigned int id : parts.all_particle_indices) {
        particles_view[id].vx = new_vx;
        particles_view[id].vy = new_vy;
    }

    // DEBUG: Check for body part separation (decomposition)
    static int decomp_check = 0;
    if (decomp_check++ % 60 == 0 && parts.all_particle_indices.size() > 1) {
        float hips_x = particles_view[parts.hips].x;
        float hips_y = particles_view[parts.hips].y;
        float hips_z = particles_view[parts.hips].z;
        float max_dist = 0.0f;
        unsigned int worst_id = 0;
        for (unsigned int id : parts.all_particle_indices) {
            float dx = particles_view[id].x - hips_x;
            float dy = particles_view[id].y - hips_y;
            float dz = particles_view[id].z - hips_z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist > max_dist) {
                max_dist = dist;
                worst_id = id;
            }
        }
        if (max_dist > 3.0f) {  // More than 3m from hips = decomposing!
            std::cout << "[DECOMP_ALERT] Particle " << worst_id << " is " << max_dist
                      << "m from hips! pos=(" << particles_view[worst_id].x << ","
                      << particles_view[worst_id].y << "," << particles_view[worst_id].z << ")" << std::endl;
        }
    }

    // 9. Update movement state for friction control in update_walk_cycle
    float new_speed = std::sqrt(new_vx * new_vx + new_vy * new_vy);
    parts.is_moving = (new_speed > 0.01f);
}

void HumanoidLocomotion::apply_motor_forces(HumanoidParts& parts, double delta_time, ParticleSystem::WriteView& particles_view) {
    auto& dyn = impl_->get_dynamics_system();
    auto& controller = parts.animation_controller;

    // Update animation state
    if (!controller.update(static_cast<float>(delta_time))) {
        return;  // Animation not playing or finished
    }

    // Get current pose
    AnimationPose pose;
    if (!controller.get_current_pose(pose)) {
        return;  // No pose available
    }

    // Note: particles_view is passed from caller - no lock acquisition here
    // This prevents deadlock since caller already holds the write lock
    const MotorGains& gains = controller.gains();

    // Apply motor forces for each target in pose
    for (const auto& target : pose.targets) {
        unsigned int pid = target.particle_id;
        if (pid >= particles_view.size()) continue;

        Particle& p = particles_view[pid];

        // Get facing angle from hips particle (humanoid's facing direction)
        // NOTE: Use rotation_z not facing_angle - physics updates rotation_z via torque
        const Particle& hips_p = particles_view[parts.hips];
        float facing_angle = hips_p.rotation_z;

        // Get target offset from hips (rest_offset + animation_offset, rotated by facing)
        float offset_x, offset_y, offset_z;
        if (!controller.get_world_target(pid, pose, facing_angle, offset_x, offset_y, offset_z)) {
            continue;  // No rest position registered
        }

        // Convert to world position by adding current hips position
        // This makes animation targets move with the body
        float target_x = hips_p.x + offset_x;
        float target_y = hips_p.y + offset_y;
        float target_z = hips_p.z + offset_z;

        // MVP: Direct position blending (kinematic animation)
        // Force-based doesn't work because gluon constraint solver (16 iterations)
        // directly modifies positions AFTER force integration, undoing our work.
        // This approach: blend particle toward target each frame.
        // NOTE: For snappy punch animation, use high blend speed (60 = instant at 60fps)
        float blend_speed = 60.0f;  // Instant reach to target for responsive animation
        float dt = static_cast<float>(delta_time);
        float blend = std::min(1.0f, blend_speed * dt);  // Clamp to 1.0 max

        // Calculate how far we're about to move
        float move_x = blend * (target_x - p.x);
        float move_y = blend * (target_y - p.y);
        float move_z = blend * (target_z - p.z);

        // Set velocity BEFORE moving - this IS our actual velocity (distance/time)
        // This ensures impact detection sees the hand moving, not stationary
        if (dt > 0.0001f) {
            p.vx = move_x / dt;
            p.vy = move_y / dt;
            p.vz = move_z / dt;
        }

        // Apply position change
        p.x += move_x;
        p.y += move_y;
        p.z += move_z;

        // Wake particle - animation movement must participate in collision detection
        // Without this, sleeping particles get skipped in physics collision loop
        p.is_sleeping = false;
    }

    // =========================================================================
    // HUMANOID DIAGNOSTICS: Show ALL right arm particles in LOCAL coordinates
    // =========================================================================
    // LOCAL +X = humanoid's right side, LOCAL +Y = forward, LOCAL +Z = up
    // This shows where the arm IS vs where it SHOULD BE in body-relative terms.
    // =========================================================================
    if (parts.diagnostics_enabled && !parts.right_arm_particles.empty()) {
        const Particle& hips_p = particles_view[parts.hips];
        float facing = hips_p.rotation_z;
        float cos_f = std::cos(-facing);  // Inverse rotation to get LOCAL
        float sin_f = std::sin(-facing);

        static int diag_frame = 0;
        if (diag_frame++ % 6 == 0) {  // Every 6 frames (~10Hz at 60fps)
            std::cout << "\n[DIAG] RIGHT ARM (facing=" << (facing * 180.0f / 3.14159f) << "°)" << std::endl;
            std::cout << "[DIAG]    Particle     LOCAL_X  LOCAL_Y  LOCAL_Z  |  WORLD_X  WORLD_Y  WORLD_Z" << std::endl;

            const char* names[] = {"R_SHOULDER", "R_UPPER_ARM", "R_FOREARM", "R_HAND"};
            for (size_t i = 0; i < parts.right_arm_particles.size() && i < 4; ++i) {
                unsigned int pid = parts.right_arm_particles[i];
                if (pid >= particles_view.size()) continue;

                const Particle& p = particles_view[pid];

                // Convert world position to LOCAL (relative to hips, rotated into body frame)
                float world_dx = p.x - hips_p.x;
                float world_dy = p.y - hips_p.y;
                float world_dz = p.z - hips_p.z;

                // Inverse rotation: local = world rotated by -facing
                float local_x = world_dx * cos_f - world_dy * sin_f;
                float local_y = world_dx * sin_f + world_dy * cos_f;
                float local_z = world_dz;

                printf("[DIAG]    %-12s %+7.3f  %+7.3f  %+7.3f  |  %+7.3f  %+7.3f  %+7.3f\n",
                       names[i], local_x, local_y, local_z, p.x, p.y, p.z);
            }

            // =====================================================================
            // CHAIN DISTANCES: Show segment lengths (should be CONSTANT if proper IK)
            // =====================================================================
            // If these values change during animation, the arm is "stretching" - WRONG!
            // Proper kinematic chain: only shoulder rotates, rest follows via constraint.
            // =====================================================================
            if (parts.right_arm_particles.size() >= 4) {
                const char* seg_names[] = {"shoulder→upper", "upper→forearm", "forearm→hand"};
                std::cout << "[DIAG]    Chain distances (should be constant):" << std::endl;
                for (size_t i = 0; i < 3; ++i) {
                    unsigned int pid_a = parts.right_arm_particles[i];
                    unsigned int pid_b = parts.right_arm_particles[i + 1];
                    if (pid_a >= particles_view.size() || pid_b >= particles_view.size()) continue;

                    const Particle& pa = particles_view[pid_a];
                    const Particle& pb = particles_view[pid_b];
                    float dx = pb.x - pa.x;
                    float dy = pb.y - pa.y;
                    float dz = pb.z - pa.z;
                    float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                    printf("[DIAG]      %-16s = %.4f m\n", seg_names[i], dist);
                }
            }
        }
    }

    // Mark BVH dirty since forces will cause movement
    impl_->get_particle_system().mark_bvh_dirty();
}

void HumanoidLocomotion::set_animation_velocities_pre_physics(HumanoidParts& parts, double delta_time, ParticleSystem::WriteView& particles_view) {
    auto& dyn = impl_->get_dynamics_system();
    auto& controller = parts.animation_controller;

    // Check if animation is playing (but don't advance it - that's Phase 2's job)
    if (!controller.is_playing()) {
        return;  // No animation playing
    }

    // Get current pose (without advancing animation)
    AnimationPose pose;
    if (!controller.get_current_pose(pose)) {
        return;  // No pose available
    }

    float dt = static_cast<float>(delta_time);
    if (dt < 0.0001f) return;  // Avoid division by zero

    // Calculate blend parameters (same as apply_motor_forces)
    float blend_speed = 60.0f;  // Must match apply_motor_forces
    float blend = std::min(1.0f, blend_speed * dt);

    // Set velocity for each target particle based on expected movement
    for (const auto& target : pose.targets) {
        unsigned int pid = target.particle_id;
        if (pid >= particles_view.size()) continue;

        Particle& p = particles_view[pid];

        // Get facing angle from hips particle
        // NOTE: Use rotation_z not facing_angle - physics updates rotation_z via torque
        const Particle& hips_p = particles_view[parts.hips];
        float facing_angle = hips_p.rotation_z;

        // Get target position (same calculation as apply_motor_forces)
        float offset_x, offset_y, offset_z;
        if (!controller.get_world_target(pid, pose, facing_angle, offset_x, offset_y, offset_z)) {
            continue;
        }

        float target_x = hips_p.x + offset_x;
        float target_y = hips_p.y + offset_y;
        float target_z = hips_p.z + offset_z;

        // Calculate expected movement
        float move_x = blend * (target_x - p.x);
        float move_y = blend * (target_y - p.y);
        float move_z = blend * (target_z - p.z);

        // Set velocity based on expected movement
        p.vx = move_x / dt;
        p.vy = move_y / dt;
        p.vz = move_z / dt;

        // CRITICAL FIX: Apply position update PRE-PHYSICS for collision detection
        // Without this, physics collision detection sees the hand at REST position,
        // not the animated/extended position. The post-physics apply_motor_forces
        // will compute remaining delta and correct for any gluon modifications.
        p.x += move_x;
        p.y += move_y;
        p.z += move_z;

        // Wake particle for collision detection
        p.is_sleeping = false;

        // DEBUG: Log velocity being set
        float vel_mag = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
        if (vel_mag > 0.5f) {
            std::cout << "[ANIM_VEL_PRE] p" << pid << " vel=" << vel_mag
                      << " m/s (pre-physics)" << std::endl;
        }
    }
}

void HumanoidLocomotion::apply_fk_pose_targets(HumanoidParts& parts, const RotationPose& pose) {
    auto& dyn = impl_->get_dynamics_system();
    for (const auto& target : pose.targets) {
        switch (target.type) {
            case JointTargetType::SEMANTIC:
                switch (target.semantic) {
                    case SemanticChannel::FLEX:
                        set_joint_flex(parts.entity_id, target.joint_name, target.angle);
                        break;
                    case SemanticChannel::ABDUCT:
                        set_joint_abduct(parts.entity_id, target.joint_name, target.angle);
                        break;
                    case SemanticChannel::TWIST:
                        set_joint_twist(parts.entity_id, target.joint_name, target.angle);
                        break;
                }
                break;
            case JointTargetType::DRIVEN:
                switch (target.axis) {
                    case RotationAxis::X:
                        set_joint_rotation_x(parts.entity_id, target.joint_name, target.angle);
                        break;
                    case RotationAxis::Y:
                        set_joint_rotation_y(parts.entity_id, target.joint_name, target.angle);
                        break;
                    case RotationAxis::Z:
                        set_joint_rotation_z(parts.entity_id, target.joint_name, target.angle);
                        break;
                }
                break;
            case JointTargetType::PHYSICS:
                set_joint_relax(parts.entity_id, target.joint_name);
                break;
            case JointTargetType::INHERIT:
                set_joint_rigid(parts.entity_id, target.joint_name);
                break;
            case JointTargetType::DIRECTION:
                set_joint_target(parts.entity_id, target.joint_name,
                    JointMode::DIRECTION, 0.0f,
                    {target.direction.x, target.direction.y, target.direction.z});
                break;
        }
    }
    publish_physics_drive_targets(parts);
}

void HumanoidLocomotion::anticipate_step_climbing(
    HumanoidParts& parts,
    ParticleSystem::WriteView& particles)
{
    auto& dyn = impl_->get_dynamics_system();
    // Get BVH for spatial queries
    const BVH* bvh = impl_->get_particle_system().get_shadow_bvh();
    if (!bvh || !bvh->is_ready()) return;

    // Use HIPS position for lookahead (not feet, since feet can lag behind)
    // The hips represent where the body is actually trying to go
    const auto& hips = particles[parts.hips];
    float look_x = hips.x;
    float look_y = hips.y;

    // But use FEET height for step detection (we need to know ground clearance)
    float foot_z = 1000.0f;  // foot_z starts high to find min
    if (!parts.left_leg_particles.empty()) {
        unsigned int lfoot_id = parts.left_leg_particles[0];
        const auto& left_foot = particles[lfoot_id];
        foot_z = std::min(foot_z, left_foot.z - left_foot.thickness / 2.0f);
        // DEBUG: Show actual foot particle values
        static int foot_debug_count = 0;
        if (foot_debug_count++ % 60 == 0) {
            std::cout << "[FOOT_DEBUG] lfoot_id=" << lfoot_id
                      << " lfoot.z=" << left_foot.z
                      << " thick=" << left_foot.thickness
                      << " foot_z=" << foot_z << std::endl;
        }
    }
    if (!parts.right_leg_particles.empty()) {
        const auto& right_foot = particles[parts.right_leg_particles[0]];
        foot_z = std::min(foot_z, right_foot.z - right_foot.thickness / 2.0f);
    }
    if (foot_z > 500.0f) foot_z = hips.z - 1.0f;  // Fallback if no feet

    // Get movement direction from target velocity
    float speed = std::sqrt(parts.target_vx * parts.target_vx + parts.target_vy * parts.target_vy);
    if (speed < MIN_MOVEMENT_SPEED) return;  // Not moving enough to check

    float dir_x = parts.target_vx / speed;
    float dir_y = parts.target_vy / speed;

    // Construct lookahead AABB ahead of HIPS in movement direction
    // The box starts slightly ahead and extends in the movement direction
    float start_offset = 0.1f;  // Start 10cm ahead
    float center_x = look_x + dir_x * (start_offset + LOOKAHEAD_DIST / 2.0f);
    float center_y = look_y + dir_y * (start_offset + LOOKAHEAD_DIST / 2.0f);

    // FIRST: Find current support surface height (what we're standing on)
    // A "step" is only valid if it's higher than the current support surface
    AABB support_box;
    support_box.min_x = look_x - 0.3f;  // Area beneath humanoid
    support_box.max_x = look_x + 0.3f;
    support_box.min_y = look_y - 0.3f;
    support_box.max_y = look_y + 0.3f;
    support_box.min_z = -10.0f;         // Always reach ground (arbitrary large depth)
    support_box.max_z = foot_z + 0.1f;  // Up to foot level

    std::vector<int> support_candidates;
    bvh->query_aabb(support_box, particles.get_particles(), support_candidates);

    float support_top = -1e9f;  // Height of surface we're standing on
    int support_id = -1;  // Track which particle is support
    for (int cand_id : support_candidates) {
        // Skip self particles
        bool is_self = false;
        for (unsigned int pid : parts.all_particle_indices) {
            if (static_cast<int>(pid) == cand_id) { is_self = true; break; }
        }
        if (is_self) continue;

        const auto& support = particles[cand_id];
        if (support.owner == ParticleOwner::DYNAMICS) continue;  // Skip other humanoids

        // STABILITY CHECK: Only use stable surfaces as ground
        // A stable surface has low velocity - moving/falling objects aren't ground
        float vel_sq = support.vx * support.vx + support.vy * support.vy + support.vz * support.vz;
        constexpr float MAX_STABLE_VEL_SQ = 0.25f;  // 0.5 m/s threshold
        if (vel_sq > MAX_STABLE_VEL_SQ) continue;

        // SIZE CHECK: Real ground surfaces are large enough to stand on
        constexpr float MIN_GROUND_SIZE = 0.25f;
        if (support.width < MIN_GROUND_SIZE || support.height < MIN_GROUND_SIZE) continue;

        float top = support.z + support.thickness * 0.5f;
        if (top <= foot_z + 0.15f) {  // Surface must be at or below foot level
            if (top > support_top) {
                support_top = top;
                support_id = cand_id;
            }
        }
    }

    // If no support found, the humanoid is in the air - disable step climbing
    if (support_top < -1e8f) {
        // In air - no ground to step from
        return;
    }

    // Additional check: if foot is more than 0.5m above support, we're airborne
    // This prevents step climbing while jumping/falling
    if (foot_z - support_top > 0.5f) {
        return;  // Airborne - step climbing disabled
    }

    // SECOND: Query for obstacles AHEAD in movement direction
    AABB query_box;
    query_box.min_x = center_x - LOOKAHEAD_WIDTH / 2.0f;
    query_box.max_x = center_x + LOOKAHEAD_WIDTH / 2.0f;
    query_box.min_y = center_y - LOOKAHEAD_WIDTH / 2.0f;
    query_box.max_y = center_y + LOOKAHEAD_WIDTH / 2.0f;
    query_box.min_z = foot_z - 0.1f;                    // Slightly below foot level
    query_box.max_z = foot_z + MAX_STEP_HEIGHT + 0.1f;  // Up to max step height

    // Query BVH for obstacles in the lookahead box
    std::vector<int> candidates;
    bvh->query_aabb(query_box, particles.get_particles(), candidates);

    // DEBUG: Step lookahead logging (disabled)
    constexpr bool should_debug = false;

    // Find the tallest climbable step (must be higher than current support surface)
    float max_step_height = 0.0f;
    int step_obs_id = -1;  // Track which particle is the step

    for (int obs_id : candidates) {
        // Skip if obstacle is part of this entity
        bool is_internal = false;
        for (unsigned int pid : parts.all_particle_indices) {
            if (static_cast<int>(pid) == obs_id) {
                is_internal = true;
                break;
            }
        }
        if (is_internal) continue;

        const auto& obs = particles[obs_id];

        // STABILITY CHECK: Don't try to step over moving objects
        float vel_sq = obs.vx * obs.vx + obs.vy * obs.vy + obs.vz * obs.vz;
        constexpr float MAX_STABLE_VEL_SQ = 0.25f;  // 0.5 m/s threshold
        if (vel_sq > MAX_STABLE_VEL_SQ) continue;

        // SIZE CHECK: Only consider stepping over real obstacles, not tiny debris/body parts
        constexpr float MIN_STEP_OBS_SIZE = 0.2f;
        if (obs.width < MIN_STEP_OBS_SIZE || obs.height < MIN_STEP_OBS_SIZE) continue;

        // Calculate step height relative to SUPPORT SURFACE (not foot)
        // This is the key general fix: a "step" must be higher than where we're standing
        float obs_top_z = obs.z + obs.thickness / 2.0f;
        float step_height = obs_top_z - support_top;  // Height above current ground

        // Debug: show all candidates
        if (should_debug) {
            std::cout << "  [CAND] obs_id=" << obs_id << " obs_z=" << obs.z
                      << " obs_top=" << obs_top_z << " support_top=" << support_top
                      << " step_height=" << step_height
                      << (step_height > MIN_STEP_HEIGHT && step_height < MAX_STEP_HEIGHT ? " CLIMBABLE" : " NOT_CLIMBABLE")
                      << std::endl;
        }

        // Check if it's a climbable step (not too small, not too tall)
        // Walls (step_height >= MAX_STEP_HEIGHT) are handled by collision system, not here
        if (step_height > MIN_STEP_HEIGHT && step_height < MAX_STEP_HEIGHT) {
            if (step_height > max_step_height) {
                max_step_height = step_height;
                step_obs_id = obs_id;
            }
        }
    }

    // Apply sustained upward velocity while approaching a climbable step
    if (max_step_height > 0.0f) {
        // Don't boost if already moving upward significantly (prevents runaway climbing)
        float hips_vz = particles[parts.hips].vz;
        if (hips_vz > 1.0f) {
            return;  // Already climbing/jumping - let it complete
        }

        // Calculate required upward velocity to clear the step
        // v = sqrt(2gh) gives velocity needed to reach height h
        // Add 50% margin for safety
        float target_vz = std::sqrt(2.0f * PhysicsV4::GRAVITY * max_step_height) * 1.5f;
        target_vz = std::min(target_vz, MAX_STEP_BOOST);

        // DEBUG: Log when applying step climb velocity
        const auto& step_obs = particles[step_obs_id];
        std::cout << "[STEP_CLIMB] obs_id=" << step_obs_id
                  << " obs_top=" << (step_obs.z + step_obs.thickness/2)
                  << " support_top=" << support_top
                  << " step_height=" << max_step_height
                  << " vz=" << target_vz << std::endl;

        // Boost vz ONLY on hips + leg particles. The upper body (abdomen,
        // chest, neck, head, arms, accessories) does not need a separate
        // climb impulse — once the hips rise, the FK chain and gluons carry
        // the torso up. Boosting above the hips caused "spider Eva": DYNAMICS
        // entities skip gravity while on_ground, so any stray vz on head-chain
        // particles never decays. Use the anatomical part lists instead of a
        // z-threshold — during a squat/step the feet can be above the hips,
        // breaking a z-based filter.
        auto& tracer = impl_->get_particle_tracer();
        auto boost = [&](unsigned int pid) {
            float old_vz = particles[pid].vz;
            if (old_vz < target_vz) {
                particles[pid].vz = target_vz;
                TRACE_WRITE(tracer, static_cast<int>(pid),
                            "step_climb.boost", "vz", old_vz, target_vz);
            }
        };
        boost(parts.hips);
        for (unsigned int pid : parts.left_leg_particles) boost(pid);
        for (unsigned int pid : parts.right_leg_particles) boost(pid);
    }
}

namespace {
// Is there stable support directly beneath these feet right now? Same
// support-query shape anticipate_step_climbing uses for its own gating
// (support_box under the hips, foot height from the leg particles,
// filtered by owner/velocity/size, nearest surface at-or-below foot
// level) -- duplicated rather than factored out of that function or
// apply_entity_gravity, so this addition doesn't touch either.
bool compute_on_ground(const HumanoidParts& parts, ParticleSystem::WriteView& particles,
                       const BVH* bvh) {
    if (!bvh || !bvh->is_ready()) return false;

    const auto& hips = particles[parts.hips];

    // A jump sets vz before the next physics step has moved anything, so
    // for one or more frames the feet are still positionally touching the
    // old support surface even though the humanoid is now rising -- a
    // pure position check alone would report "still grounded" right after
    // take-off (this is exactly what caught it: try_jump immediately
    // followed by a second try_jump must refuse, and didn't until this
    // check existed). 1.0 m/s matches anticipate_step_climbing's own
    // "already climbing/jumping, don't re-trigger" threshold just above,
    // rather than the tighter 0.5 m/s support-candidate stability check
    // (which would risk flickering false during ordinary bouncy gait).
    if (hips.vz > 1.0f) return false;

    float foot_z = 1000.0f;
    if (!parts.left_leg_particles.empty()) {
        const auto& lfoot = particles[parts.left_leg_particles[0]];
        foot_z = std::min(foot_z, lfoot.z - lfoot.thickness / 2.0f);
    }
    if (!parts.right_leg_particles.empty()) {
        const auto& rfoot = particles[parts.right_leg_particles[0]];
        foot_z = std::min(foot_z, rfoot.z - rfoot.thickness / 2.0f);
    }
    if (foot_z > 500.0f) foot_z = hips.z - 1.0f;

    AABB support_box;
    support_box.min_x = hips.x - 0.3f;
    support_box.max_x = hips.x + 0.3f;
    support_box.min_y = hips.y - 0.3f;
    support_box.max_y = hips.y + 0.3f;
    support_box.min_z = -10.0f;
    support_box.max_z = foot_z + 0.1f;

    std::vector<int> candidates;
    bvh->query_aabb(support_box, particles.get_particles(), candidates);

    float support_top = -1e9f;
    for (int cand_id : candidates) {
        bool is_self = false;
        for (unsigned int pid : parts.all_particle_indices) {
            if (static_cast<int>(pid) == cand_id) { is_self = true; break; }
        }
        if (is_self) continue;

        const auto& support = particles[cand_id];
        if (support.owner == ParticleOwner::DYNAMICS) continue;

        float vel_sq = support.vx * support.vx + support.vy * support.vy + support.vz * support.vz;
        if (vel_sq > 0.25f) continue;
        if (support.width < 0.25f || support.height < 0.25f) continue;

        float top = support.z + support.thickness * 0.5f;
        if (top <= foot_z + 0.15f && top > support_top) {
            support_top = top;
        }
    }

    if (support_top < -1e8f) return false;       // nothing beneath at all
    return (foot_z - support_top) <= 0.5f;        // same airborne threshold anticipate_step_climbing uses
}
} // namespace

bool HumanoidLocomotion::try_jump(int hips_id, float jump_height) {
    if (!impl_->initialized) return false;
    auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    const BVH* bvh = ps.get_shadow_bvh();

    for (auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;

        auto particles_view = ps.lock_particles_for_write();
        if (!compute_on_ground(parts, particles_view, bvh)) return false;

        // Release the stance-foot pin before launching. Without this,
        // maintain_entity_shape's kinematic-root constraint (stance foot
        // pinned at anchor_world, applied every frame in
        // update_post_physics) shifts the whole body back down each tick
        // so the planted foot returns to its pre-jump anchor -- silently
        // cancelling the vz boost below before any real height change
        // shows up (this is exactly what the height-gain assertion in
        // threshold_verify caught: 0.0m of measured rise with this step
        // missing). Same release the walk->idle transition already does
        // a few hundred lines up, just triggered by jumping instead of
        // stopping.
        if (parts.plant_anchor_particle_id >= 0) {
            HumanoidParts::PinGluonOp op;
            op.kind = HumanoidParts::PinGluonOp::DISENGAGE;
            op.release_anchor_id = parts.plant_anchor_particle_id;
            parts.pending_pin_ops.push_back(op);
            parts.plant_anchor_particle_id = -1;
        }
        parts.has_planted_foot = false;
        parts.plant_blend = 0.0f;

        // v = sqrt(2*g*h): the same arc-physics formula
        // anticipate_step_climbing uses, capped independently of
        // MAX_STEP_BOOST (that constant is tuned for clearing a small
        // obstacle, not a deliberate jump -- 1.2m of jump height alone
        // needs ~4.85 m/s, already past MAX_STEP_BOOST's 4.0).
        constexpr float kMaxJumpBoost = 6.0f;
        float target_vz = std::sqrt(2.0f * PhysicsV4::GRAVITY * jump_height);
        target_vz = std::min(target_vz, kMaxJumpBoost);

        auto& tracer = impl_->get_particle_tracer();
        auto boost = [&](unsigned int pid) {
            float old_vz = particles_view[pid].vz;
            particles_view[pid].vz = target_vz;
            TRACE_WRITE(tracer, static_cast<int>(pid), "jump.boost", "vz", old_vz, target_vz);
        };
        boost(parts.hips);
        for (unsigned int pid : parts.left_leg_particles) boost(pid);
        for (unsigned int pid : parts.right_leg_particles) boost(pid);
        return true;
    }
    return false;
}

bool HumanoidLocomotion::is_grounded(int hips_id) const {
    if (!impl_->initialized) return false;
    const auto& dyn = impl_->get_dynamics_system();
    auto& ps = impl_->get_particle_system();
    const BVH* bvh = ps.get_shadow_bvh();

    for (const auto& parts : dyn.humanoid_look_at_entities_) {
        if (static_cast<int>(parts.hips) != hips_id) continue;
        auto particles_view = ps.lock_particles_for_write();
        return compute_on_ground(parts, particles_view, bvh);
    }
    return false;
}

void HumanoidLocomotion::apply_entity_gravity(
    HumanoidParts& parts,
    float dt,
    ParticleSystem::WriteView& particles)
{
    auto& dyn = impl_->get_dynamics_system();
    // ========================================================================
    // FOOTPRINT-BASED GROUND SUPPORT (BVH Query)
    // ========================================================================
    // Instead of just checking if feet are below z=0.1, we query BVH for
    // actual supporting particles beneath a "footprint" rectangle between feet.
    // This prevents falling through gaps between floor tiles.
    // ========================================================================

    const BVH* bvh = impl_->get_particle_system().get_shadow_bvh();
    bool on_ground = false;

    // Get foot positions and calculate footprint bounds
    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;
    float foot_bottom_z = 1e9f;

    // Left foot
    if (!parts.left_leg_particles.empty()) {
        const Particle& foot = particles[parts.left_leg_particles.front()];
        float half_size = foot.size * 0.5f;
        min_x = std::min(min_x, foot.x - half_size);
        max_x = std::max(max_x, foot.x + half_size);
        min_y = std::min(min_y, foot.y - half_size);
        max_y = std::max(max_y, foot.y + half_size);
        foot_bottom_z = std::min(foot_bottom_z, foot.z - foot.thickness * 0.5f);
    }

    // Right foot
    if (!parts.right_leg_particles.empty()) {
        const Particle& foot = particles[parts.right_leg_particles.front()];
        float half_size = foot.size * 0.5f;
        min_x = std::min(min_x, foot.x - half_size);
        max_x = std::max(max_x, foot.x + half_size);
        min_y = std::min(min_y, foot.y - half_size);
        max_y = std::max(max_y, foot.y + half_size);
        foot_bottom_z = std::min(foot_bottom_z, foot.z - foot.thickness * 0.5f);
    }

    // Expand footprint slightly to catch nearby support
    const float FOOTPRINT_MARGIN = 0.1f;  // 10cm margin
    min_x -= FOOTPRINT_MARGIN;
    max_x += FOOTPRINT_MARGIN;
    min_y -= FOOTPRINT_MARGIN;
    max_y += FOOTPRINT_MARGIN;

    // Query BVH for particles below the footprint
    // Look from below ground up to a reasonable height to find ANY supporting surface
    const float GROUND_LEVEL = -1.0f;          // Search from below ground
    const float MAX_SUPPORT_HEIGHT = 0.5f;     // Floor tiles are at z~0.2, always check up to here
    const float SUPPORT_THRESHOLD = 0.2f;      // Support if top within 20cm of foot

    // DEBUG: Footprint detection logging (disabled)
    constexpr bool should_debug = false;
    static int ground_debug_frame = 0;
    ground_debug_frame++;

    if (bvh && bvh->is_ready() && foot_bottom_z < 1e8f) {
        AABB query_box;
        query_box.min_x = min_x;
        query_box.max_x = max_x;
        query_box.min_y = min_y;
        query_box.max_y = max_y;
        // Search from below ground up to at least floor level
        query_box.min_z = GROUND_LEVEL;
        query_box.max_z = std::max(foot_bottom_z + SUPPORT_THRESHOLD, MAX_SUPPORT_HEIGHT);

        std::vector<int> candidates;
        bvh->query_aabb(query_box, particles.get_particles(), candidates);

        if (should_debug) {
            // Get hips position for context
            const Particle& hips = particles[parts.hips];
            std::cout << "[GROUND_DEBUG] frame=" << ground_debug_frame
                      << " hips=(" << hips.x << "," << hips.y << "," << hips.z << ")"
                      << " foot_bottom=" << foot_bottom_z
                      << " footprint=[" << min_x << "," << max_x << "]x[" << min_y << "," << max_y << "]"
                      << " candidates=" << candidates.size() << std::endl;
        }

        // Check if any candidate provides support
        int self_count = 0;
        int checked_count = 0;
        for (int cand_id : candidates) {
            // Skip self (entity's own particles)
            bool is_self = false;
            for (unsigned int pid : parts.all_particle_indices) {
                if (static_cast<int>(pid) == cand_id) {
                    is_self = true;
                    break;
                }
            }
            if (is_self) {
                self_count++;
                continue;
            }
            checked_count++;

            // Check if this particle's top surface supports the entity
            const Particle& support = particles[cand_id];
            float support_top = support.z + support.thickness * 0.5f;

            if (should_debug) {
                std::cout << "  [SUPPORT_CAND " << cand_id << "] pos=(" << support.x << "," << support.y << "," << support.z << ")"
                          << " size=" << support.size << " thick=" << support.thickness
                          << " top=" << support_top << " vs foot=" << foot_bottom_z
                          << (support_top >= foot_bottom_z - SUPPORT_THRESHOLD ? " SUPPORTS" : " NO_SUPPORT")
                          << std::endl;
            }

            // Support if top surface is close to foot bottom
            // Extended range: -0.35 to 0.3 (handles 30cm gap between turtle and floor tiles)
            float gap = foot_bottom_z - support_top;  // Positive = foot above floor
            if (gap < 0.3f && gap > -0.35f) {  // MUST match maintain_entity_shape thresholds!
                on_ground = true;
                if (should_debug) {
                    std::cout << "  [GROUND_FOUND] support_id=" << cand_id << " gap=" << gap << " ON GROUND" << std::endl;
                }
                // Stop downward velocity to prevent gravity from being applied.
                // Position correction happens in maintain_entity_shape() after integration.
                auto& grav_tracer = impl_->get_particle_tracer();
                for (unsigned int pid : parts.all_particle_indices) {
                    if (particles[pid].vz < 0) {
                        float old_vz = particles[pid].vz;
                        particles[pid].vz = 0;
                        TRACE_WRITE_N(grav_tracer, static_cast<int>(pid),
                                      "gravity.on_ground_clamp", "vz", old_vz, 0.0f,
                                      "negative vz zeroed");
                    }
                }
                break;
            } else if (should_debug) {
                std::cout << "  [GAP_REJECT] gap=" << gap << " (need -0.35 < gap < 0.3)" << std::endl;
            }
        }

        if (should_debug && !on_ground) {
            std::cout << "  [GROUND_DEBUG] NO SUPPORT - self=" << self_count << " checked=" << checked_count << std::endl;
        }
    } else if (should_debug) {
        std::cout << "[GROUND_DEBUG] BVH not ready or no feet" << std::endl;
    }

    // Apply gravity uniformly to ALL particles if not on ground
    if (!on_ground) {
        const float GRAVITY = 9.8f;
        float gravity_dv = -GRAVITY * dt;
        auto& grav_tracer = impl_->get_particle_tracer();
        for (unsigned int pid : parts.all_particle_indices) {
            float old_vz = particles[pid].vz;
            particles[pid].vz += gravity_dv;
            TRACE_WRITE_N(grav_tracer, static_cast<int>(pid),
                          "gravity.apply", "vz", old_vz, particles[pid].vz,
                          "airborne");
        }
    }
}

void HumanoidLocomotion::maintain_entity_shape(
    HumanoidParts& parts,
    ParticleSystem::WriteView& particles,
    float dt)
{
    auto& dyn = impl_->get_dynamics_system();
    // Skip if lying down - we set the pose explicitly, don't restore to standing
    if (parts.is_lying_down) return;

    // Skip if no rest offsets stored (shouldn't happen but safety check)
    if (parts.rest_offsets.empty() || parts.rest_offsets.size() != parts.all_particle_indices.size()) {
        return;
    }

    // ========================================================================
    // DYNAMICS POSITION INTEGRATION
    // ========================================================================
    // Since physics skips DYNAMICS particles, we integrate hips position here.
    // Then snap all other particles to hips using rest offsets.
    // ========================================================================
    Particle& hips = particles[parts.hips];
    auto& shape_tracer = impl_->get_particle_tracer();
    float shape_hips_ox = hips.x, shape_hips_oy = hips.y, shape_hips_oz = hips.z;

    // Hips integrate from velocity every frame. The kinematic-root
    // constraint (stance foot pinned at anchor_world) is enforced AFTER
    // FK runs, by shifting the whole body so the stance foot lands at the
    // anchor. See the root_constraint_shift block in update_post_physics().
    hips.x += hips.vx * dt;
    hips.y += hips.vy * dt;
    hips.z += hips.vz * dt;
    TRACE_POS_WRITE(shape_tracer, static_cast<int>(parts.hips),
                    "shape.hips_integrate",
                    shape_hips_ox, shape_hips_oy, shape_hips_oz,
                    hips.x, hips.y, hips.z);

    // Rigid-body translate all entity particles by the same per-frame
    // delta so the physics step (which runs BEFORE FK this tick) sees
    // gluon distances preserved. Without this, only hips advances pre-
    // physics; gluon solver then sees the hips<>thigh gluon stretched
    // by v·dt and pulls hips backward to satisfy the constraint,
    // cancelling ~50% of forward motion. snap_to_hips runs immediately
    // after this (and re-places non-ANIMATION particles to their rest
    // offsets from hips), and FK runs post-physics to re-place
    // ANIMATION particles from joint rotations — so any per-particle
    // pose state we need is restored downstream. This step exists
    // purely to keep the pre-physics pose gluon-consistent.
    float entity_dx = hips.x - shape_hips_ox;
    float entity_dy = hips.y - shape_hips_oy;
    float entity_dz = hips.z - shape_hips_oz;
    if (entity_dx*entity_dx + entity_dy*entity_dy + entity_dz*entity_dz > 1e-10f) {
        for (unsigned int pid : parts.all_particle_indices) {
            if (pid == parts.hips) continue;  // already integrated
            float ox = particles[pid].x;
            float oy = particles[pid].y;
            float oz = particles[pid].z;
            particles[pid].x += entity_dx;
            particles[pid].y += entity_dy;
            particles[pid].z += entity_dz;
            TRACE_POS_WRITE(shape_tracer, static_cast<int>(pid),
                            "shape.entity_translate",
                            ox, oy, oz,
                            particles[pid].x, particles[pid].y, particles[pid].z);
        }
    }

    // ========================================================================
    // DYNAMICS ANGULAR INTEGRATION
    // ========================================================================
    // Physics skips DYNAMICS particles, so we integrate angular physics here.
    // This converts torques applied by update_humanoid_look_at() into rotation.
    // ========================================================================
    unsigned int angular_particles[] = {
        parts.head, parts.neck, parts.torso, parts.abdomen, parts.hips
    };

    for (unsigned int pid : angular_particles) {
        if (pid == 0) continue;  // Skip if not set
        Particle& p = particles[pid];

        // Skip ANIMATION-owned particles - FK controls their rotation
        if (p.owner == ParticleOwner::ANIMATION) continue;

        // Calculate rotational inertia (simplified: sphere approximation)
        // I = 0.4 * m * r^2 for solid sphere
        float radius = p.size * 0.5f;
        float inertia = 0.4f * p.GetMass() * radius * radius;
        if (inertia < 0.001f) inertia = 0.001f;  // Prevent division by zero

        // Angular acceleration from torque: α = τ / I
        float angular_accel = p.torque_z / inertia;

        // Integrate angular velocity: ω += α * dt
        p.omega_z += angular_accel * dt;

        // Clamp angular velocity to prevent instability
        // Higher than physics (1.57) to allow quick head turns
        const float MAX_OMEGA = 6.28f;  // 2π rad/s = 360°/sec max rotation speed
        if (p.omega_z > MAX_OMEGA) p.omega_z = MAX_OMEGA;
        if (p.omega_z < -MAX_OMEGA) p.omega_z = -MAX_OMEGA;

        // Apply angular damping (prevents infinite oscillation)
        // Note: look-at system already has derivative damping in torque calculation
        // This is additional drag to ensure stability
        const float ANGULAR_DRAG = 0.98f;  // 2% velocity loss per frame
        p.omega_z *= ANGULAR_DRAG;

        // Integrate rotation: θ += ω * dt
        p.rotation_z += p.omega_z * dt;

        // Clear torque accumulator for next frame
        p.torque_z = 0.0f;
    }

    // Rotate offsets by hips rotation (Z-axis rotation for isometric view)
    // Uses CLOCKWISE rotation to match:
    //   - particle_geometry_v2.cpp visual rotation
    //   - movement forward direction (sin(θ), cos(θ))
    // Convention: rotation_z = 0 → facing North (+Y)
    float cos_r = std::cos(hips.rotation_z);
    float sin_r = std::sin(hips.rotation_z);

    // Calculate walk animation offsets
    bool wants_walk_anim = parts.is_moving && parts.is_volitional;
    float walk_swing_amplitude = wants_walk_anim ? 0.15f : 0.0f;  // meters of leg swing
    float left_leg_swing = walk_swing_amplitude * std::sin(parts.walk_phase);
    float right_leg_swing = walk_swing_amplitude * std::sin(parts.walk_phase + M_PI);  // opposite phase

    // Get animated particles to skip (animation system handles their positions)
    std::unordered_set<unsigned int> animated_particles;
    if (parts.animation_controller.is_playing()) {
        AnimationPose pose;
        if (parts.animation_controller.get_current_pose(pose)) {
            for (const auto& target : pose.targets) {
                animated_particles.insert(target.particle_id);
            }
        }
    }

    // Also skip FK-animated particles (joints with non-zero rotations)
    // AND their downstream chain (elbow follows shoulder even if elbow rotation=0)
    std::unordered_set<unsigned int> fk_parents;
    for (const auto& joint : parts.joint_hierarchy.joints) {
        if (std::abs(joint.rotation_x) > 0.001f ||
            std::abs(joint.rotation_y) > 0.001f ||
            std::abs(joint.rotation_z) > 0.001f) {
            animated_particles.insert(joint.child_particle);
            fk_parents.insert(joint.child_particle);
        }
    }
    // Second pass: add children of FK-positioned particles (chain propagation)
    for (const auto& joint : parts.joint_hierarchy.joints) {
        if (fk_parents.count(joint.parent_particle)) {
            animated_particles.insert(joint.child_particle);
            fk_parents.insert(joint.child_particle);  // For deeper chains
        }
    }
    // Third pass for 3-deep chains (shoulder → elbow → wrist)
    for (const auto& joint : parts.joint_hierarchy.joints) {
        if (fk_parents.count(joint.parent_particle)) {
            animated_particles.insert(joint.child_particle);
        }
    }

    // NOTE (2026-06-12 RCA): the planted stance foot is dragged off its
    // pin anchor by this snap (measured: foot-to-anchor delta grows
    // 0.04 → 0.46 m at plant_blend = 1) — the pin's position bias
    // (GLUON_POSITION_BETA) closes ~1/60 of the gap per frame while the
    // snap re-opens all of it. Excluding the stance leg here was tried
    // and falsified: the chain still rides the hips via velocity-level
    // constraints and the stance handoff then teleports the leg 0.33 m.
    // Holding the foot at the anchor needs a position-level authority
    // (e.g. KINEMATIC stance foot) — tracked as a separate decision.

    // Swing direction = normalized motion direction, fallback to facing
    // when stationary (same precedence the plant-target code uses).
    // Body-forward at yaw θ is (sin θ, cos θ); applying the swing along
    // raw world-Y only matches gait when facing north — at other
    // headings it turns lateral and the feet scissor across the body.
    float swing_dir_x = sin_r, swing_dir_y = cos_r;
    {
        float motion_len = std::sqrt(parts.target_vx * parts.target_vx
                                   + parts.target_vy * parts.target_vy);
        if (motion_len > 0.01f) {
            swing_dir_x = parts.target_vx / motion_len;
            swing_dir_y = parts.target_vy / motion_len;
        }
    }
    // Leg length = deepest rest offset below the hips (foot/toe level),
    // used to scale swing as a pendulum about the hip pivot.
    float leg_len = 0.0f;
    for (size_t li = 0; li < parts.all_particle_indices.size(); ++li) {
        if (-parts.rest_offsets[li].z > leg_len) leg_len = -parts.rest_offsets[li].z;
    }

    // Apply rest offsets to maintain shape - snap all other particles to hips
    for (size_t i = 0; i < parts.all_particle_indices.size(); ++i) {
        unsigned int pid = parts.all_particle_indices[i];
        if (pid == parts.hips) continue;  // Hips already integrated above

        // Skip particles owned by ANIMATION - FK controls their position
        // This is the authoritative ownership check (not the animated_particles set)
        if (particles[pid].owner == ParticleOwner::ANIMATION) {
            // Emit a note-only trace record so the causal log shows who's
            // owning this particle (no write happens here).
            if (shape_tracer.is_active() && shape_tracer.is_traced(static_cast<int>(pid))) {
                shape_tracer.record(static_cast<int>(pid), "shape.snap_to_hips",
                                    "skipped", 0.0f, 0.0f, "ANIMATION-owned");
            }
            continue;
        }

        // Legacy: also skip based on animated_particles set (motor force animations)
        if (animated_particles.count(pid)) {
            if (shape_tracer.is_active() && shape_tracer.is_traced(static_cast<int>(pid))) {
                shape_tracer.record(static_cast<int>(pid), "shape.snap_to_hips",
                                    "skipped", 0.0f, 0.0f, "animated_particles set");
            }
            continue;
        }

        // Head children (eyes, ears, nose, hair) ride the HEAD, not the
        // hips: the post-FK head-snap positions them in the head's yaw
        // frame and the cascade owns their rotation_z. This hips snap
        // used to overwrite both every frame — with the head leading
        // the hips by design, the face stayed glued to the body axis
        // while the skull turned underneath it (Eden playtest find;
        // repro: test_face_tracks_head).
        if (std::find(parts.head_child_particles.begin(),
                      parts.head_child_particles.end(), pid) !=
            parts.head_child_particles.end()) {
            if (shape_tracer.is_active() && shape_tracer.is_traced(static_cast<int>(pid))) {
                shape_tracer.record(static_cast<int>(pid), "shape.snap_to_hips",
                                    "skipped", 0.0f, 0.0f, "head-child (rides head)");
            }
            continue;
        }

        // Physics-drive children still get snap_to_hips's XY/Z
        // position write so they follow hips each frame — without it,
        // walking leaves them behind at a rate equal to inter-
        // maintain_entity_shape hips motion. The ROTATION write below
        // is what would fight the PD; it's gated. For rotated-target
        // joints (shoulder flex, etc.) the gluon distance constraint
        // corrects whatever offset the snap leaves off-mark.
        const auto& offset = parts.rest_offsets[i];

        // Rotate XY offset by hips rotation, Z stays unchanged
        // CLOCKWISE rotation: (x, y) → (x*cos + y*sin, -x*sin + y*cos)
        float rotated_x = offset.x * cos_r + offset.y * sin_r;
        float rotated_y = -offset.x * sin_r + offset.y * cos_r;

        // Add walk animation swing for legs, along the motion direction.
        // The leg is a pendulum about the hip pivot: a particle's swing
        // displacement scales with its distance below the hips, so the
        // foot swings the full amplitude while the thigh barely moves.
        // (Translating the whole leg rigidly shoves the thigh into step
        // faces during climbs — the snap has no collide-and-slide.)
        float anim = 0.0f;
        for (unsigned int leg_pid : parts.left_leg_particles) {
            if (leg_pid == pid) { anim = left_leg_swing; break; }
        }
        for (unsigned int leg_pid : parts.right_leg_particles) {
            if (leg_pid == pid) { anim = right_leg_swing; break; }
        }
        if (anim != 0.0f && leg_len > 0.001f) {
            float leg_reach = -offset.z;             // distance below hips
            anim *= std::max(0.0f, leg_reach) / leg_len;
        }

        // Target position = hips + rotated offset + animation
        // Ground correction is applied AFTER snap (see below)
        float target_x = hips.x + rotated_x + swing_dir_x * anim;
        float target_y = hips.y + rotated_y + swing_dir_y * anim;
        float target_z = hips.z + offset.z;

        // Hard position correction (no spring - immediate snap)
        float shape_old_x = particles[pid].x;
        float shape_old_y = particles[pid].y;
        float shape_old_z = particles[pid].z;
        particles[pid].x = target_x;
        particles[pid].y = target_y;
        particles[pid].z = target_z;
        TRACE_POS_WRITE(shape_tracer, static_cast<int>(pid),
                        "shape.snap_to_hips",
                        shape_old_x, shape_old_y, shape_old_z,
                        target_x, target_y, target_z);

        // Propagate rotation: particle.rotation_z = hips.rotation_z + rest_rotation_offset
        // Skip if physics-drive owns this particle's rotation_z.
        if (!parts.physics_drive_children.count(pid)) {
            particles[pid].rotation_z = hips.rotation_z + offset.rotation_z;
        }
    }

    // ========================================================================
    // INTEGRATED GROUND SUPPORT
    // ========================================================================
    // After snapping to rest positions, check if feet are below ground and
    // correct the skeleton by adjusting upper body + hips + swing leg.
    // The stance leg is EXCLUDED — its foot is pinned at anchor_world by
    // the post-FK shift (kinematic-root). Lifting the stance leg here and
    // then pulling it back down via the shift would create a per-frame
    // tug-of-war and slow walking. Ground correction's remaining job is
    // the swing leg + body clearance over changing terrain height.
    //
    // This correction is unconditional on gap alone -- it doesn't look at
    // vz -- so it actively re-snaps the skeleton back to floor+5mm every
    // frame the feet are still within its (-0.35, 0.3) range, which is
    // most of a jump's initial ascent. hips.z += hips.vz*dt happens
    // earlier this same function; without this guard the very next line
    // below undoes it every single frame, so try_jump()'s vz boost never
    // produces any visible height change (caught by threshold_verify's
    // jump_rises_and_lands: 0.0m measured rise). 1.0 m/s matches
    // try_jump/is_grounded's own "rising with intent" threshold.
    // ========================================================================
    if (hips.vz > 1.0f) return;
    const BVH* bvh = impl_->get_particle_system().get_shadow_bvh();
    if (!bvh || !bvh->is_ready()) return;

    // Find lowest foot position
    float foot_bottom_z = 1e9f;
    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;

    if (!parts.left_leg_particles.empty()) {
        const Particle& foot = particles[parts.left_leg_particles.front()];
        float half_size = foot.size * 0.5f;
        min_x = std::min(min_x, foot.x - half_size);
        max_x = std::max(max_x, foot.x + half_size);
        min_y = std::min(min_y, foot.y - half_size);
        max_y = std::max(max_y, foot.y + half_size);
        foot_bottom_z = std::min(foot_bottom_z, foot.z - foot.thickness * 0.5f);
    }
    if (!parts.right_leg_particles.empty()) {
        const Particle& foot = particles[parts.right_leg_particles.front()];
        float half_size = foot.size * 0.5f;
        min_x = std::min(min_x, foot.x - half_size);
        max_x = std::max(max_x, foot.x + half_size);
        min_y = std::min(min_y, foot.y - half_size);
        max_y = std::max(max_y, foot.y + half_size);
        foot_bottom_z = std::min(foot_bottom_z, foot.z - foot.thickness * 0.5f);
    }

    if (foot_bottom_z > 1e8f) return;  // No feet

    // Expand footprint
    const float MARGIN = 0.15f;
    min_x -= MARGIN; max_x += MARGIN;
    min_y -= MARGIN; max_y += MARGIN;

    // Query BVH for floor beneath footprint
    AABB query_box;
    query_box.min_x = min_x;
    query_box.max_x = max_x;
    query_box.min_y = min_y;
    query_box.max_y = max_y;
    query_box.min_z = -1.0f;
    query_box.max_z = foot_bottom_z + 0.5f;

    std::vector<int> candidates;
    bvh->query_aabb(query_box, particles.get_particles(), candidates);

    // Find highest floor tile beneath footprint
    float best_support_top = -1e9f;
    int best_support_id = -1;
    for (int cand_id : candidates) {
        // Skip self
        bool is_self = false;
        for (unsigned int pid : parts.all_particle_indices) {
            if (static_cast<int>(pid) == cand_id) { is_self = true; break; }
        }
        if (is_self) continue;

        const Particle& support = particles[cand_id];

        // Skip other humanoids - prevents rocket launch when two humanoids overlap
        // (each would see the other as floor and push up in feedback loop)
        if (support.owner == ParticleOwner::DYNAMICS) continue;

        // STABILITY CHECK: Only use stable surfaces as ground
        // A stable surface has low velocity - moving/falling objects aren't ground
        float vel_sq = support.vx * support.vx + support.vy * support.vy + support.vz * support.vz;
        constexpr float MAX_STABLE_VEL_SQ = 0.25f;  // 0.5 m/s threshold
        if (vel_sq > MAX_STABLE_VEL_SQ) {
            // DEBUG: Log skipped unstable support
            static int unstable_skip_frame = 0;
            if (unstable_skip_frame++ % 60 == 0) {
                float vel = std::sqrt(vel_sq);
                std::cout << "[GROUND_SKIP] pid=" << cand_id << " vel=" << vel
                          << " m/s - unstable, skipping" << std::endl;
            }
            continue;
        }

        // SIZE CHECK: Real ground surfaces are large enough to stand on
        // Floor tiles: width ~0.5m+, body parts: ~0.1-0.2m
        // This prevents using small objects (body parts, debris) as ground
        constexpr float MIN_GROUND_SIZE = 0.25f;  // Minimum width/height to be ground
        if (support.width < MIN_GROUND_SIZE || support.height < MIN_GROUND_SIZE) {
            static int size_skip_frame = 0;
            if (size_skip_frame++ % 60 == 0) {
                std::cout << "[GROUND_SKIP] pid=" << cand_id << " size=" << support.width
                          << "x" << support.height << " - too small, skipping" << std::endl;
            }
            continue;
        }

        float support_top = support.z + support.thickness * 0.5f;
        if (support_top <= foot_bottom_z + 0.3f) {
            best_support_top = std::max(best_support_top, support_top);
            best_support_id = cand_id;
        }
    }

    // DEBUG: Shape ground logging
    static int shape_ground_frame = 0;
    shape_ground_frame++;
    bool should_debug_shape = (shape_ground_frame <= 5);

    // If floor found, correct ALL particles (including hips) to snap to ground
    if (best_support_top > -1e8f) {
        float gap = foot_bottom_z - best_support_top;

        if (should_debug_shape) {
            const Particle& sup = particles[best_support_id];
            std::cout << "[SHAPE_GROUND] foot=" << foot_bottom_z << " floor=" << best_support_top
                      << " gap=" << gap << " support_id=" << best_support_id
                      << " sup_z=" << sup.z << " sup_thick=" << sup.thickness << std::endl;
        }

        // Only correct if feet are in correction range
        // Extended range: -0.35 to 0.3 (handles 30cm gap between turtle and floor tiles)
        // Floor tiles: z=0.15 center, z=0.30 top (thickness 0.3)
        // Humanoid spawn at z=0: feet at z≈0.04, gap to floor = -0.26
        if (gap > -0.35f && gap < 0.3f) {
            // Target: 5mm above floor
            float target_gap = 0.005f;
            float correction = target_gap - gap;  // Positive = move up, negative = move down

            // Dead zone: don't correct if already within 3mm of target (prevents oscillation)
            if (std::abs(correction) > 0.003f) {
                if (should_debug_shape) {
                    std::cout << "  [SHAPE_ADJUST] gap=" << gap << " correction=" << correction << std::endl;
                }

                // Apply correction to all body particles EXCEPT the stance
                // leg (kinematic-root anchors its foot; lifting it here
                // fights the post-FK shift). Swing leg + hips + upper body
                // get the correction as before.
                unsigned int stance_skip[4] = {0, 0, 0, 0};
                if (parts.has_planted_foot) {
                    const auto& leg = parts.planted_foot_is_right
                        ? parts.right_leg_particles
                        : parts.left_leg_particles;
                    if (leg.size() >= 4) {
                        stance_skip[0] = leg[0];  // foot
                        stance_skip[1] = leg[1];  // shin
                        stance_skip[2] = leg[2];  // thigh
                        stance_skip[3] = leg[3];  // toe
                    }
                }
                for (unsigned int pid : parts.all_particle_indices) {
                    bool is_stance = false;
                    for (int k = 0; k < 4; k++) {
                        if (stance_skip[k] != 0 && pid == stance_skip[k]) {
                            is_stance = true; break;
                        }
                    }
                    if (is_stance) continue;
                    float old_z = particles[pid].z;
                    particles[pid].z += correction;
                    TRACE_WRITE(shape_tracer, static_cast<int>(pid),
                                "shape.ground_correct", "z", old_z, particles[pid].z);
                }
            }

            // Always stop downward velocity when on ground (even in dead zone)
            for (unsigned int pid : parts.all_particle_indices) {
                if (particles[pid].vz < 0) particles[pid].vz = 0;
            }
        } else if (shape_ground_frame % 30 == 0 || gap < -1.0f) {
            // DIAG: gap out of correction range — body in free-fall
            std::cout << "[GROUND_GAP_OOR] frame=" << shape_ground_frame
                      << " gap=" << gap << " foot_bottom=" << foot_bottom_z
                      << " floor_top=" << best_support_top
                      << " hips_z=" << hips.z << " vz=" << hips.vz << std::endl;
        }
    } else {
        if (should_debug_shape || shape_ground_frame % 30 == 0) {
            std::cout << "[SHAPE_GROUND] NO FLOOR FOUND! frame=" << shape_ground_frame
                      << " candidates=" << candidates.size()
                      << " foot_bottom=" << foot_bottom_z
                      << " hips_z=" << hips.z
                      << " hips_xy=(" << hips.x << "," << hips.y << ")" << std::endl;
        }
    }
}

void HumanoidLocomotion::handle_collision_events(
    HumanoidParts& parts,
    ParticleSystem::WriteView& particles)
{
    auto& dyn = impl_->get_dynamics_system();
    // Get collision events from physics system
    if (!impl_->engine) return;
    const auto& events = impl_->get_physics_system().get_collision_events();

    // Build set of our particle IDs for fast lookup
    std::unordered_set<size_t> our_particles;
    for (unsigned int pid : parts.all_particle_indices) {
        our_particles.insert(static_cast<size_t>(pid));
    }

    // Process each collision event
    for (const auto& evt : events) {
        bool is_a = our_particles.count(evt.particle_a) > 0;
        bool is_b = our_particles.count(evt.particle_b) > 0;

        if (!is_a && !is_b) continue;  // Not our collision
        if (is_a && is_b) continue;    // Internal collision, skip

        // Identify the obstacle particle
        size_t other_idx = is_a ? evt.particle_b : evt.particle_a;
        const auto& obstacle = particles[other_idx];

        // Filter B: skip non-solid materials (vegetation, lights)
        if (obstacle.material_type == Materials::Type::LEAVES ||
            obstacle.material_type == Materials::Type::LIGHT) {
            continue;
        }

        // Filter A: skip tiny obstacles (grass, debris) regardless of material
        constexpr float MIN_COLLISION_SIZE = 0.2f;
        if (obstacle.width < MIN_COLLISION_SIZE &&
            obstacle.height < MIN_COLLISION_SIZE) {
            continue;
        }

        // Push entity away from obstacle uniformly
        // Use full penetration as correction to stop walking through walls
        float push = evt.penetration;

        // Direction: if we're particle A, push in -normal direction (away from B)
        // if we're particle B, push in +normal direction (away from A)
        float normal_x = is_a ? -evt.normal_x : evt.normal_x;
        float normal_y = is_a ? -evt.normal_y : evt.normal_y;
        float dx = normal_x * push;
        float dy = normal_y * push;

        // Apply uniform push to all entity particles
        // Also zero velocity component toward obstacle to prevent walking through
        for (unsigned int pid : parts.all_particle_indices) {
            particles[pid].x += dx;
            particles[pid].y += dy;

            // Zero velocity component toward obstacle
            // If normal points away from obstacle, dot(velocity, -normal) is velocity toward obstacle
            float v_toward = particles[pid].vx * (-normal_x) + particles[pid].vy * (-normal_y);
            if (v_toward > 0) {  // Moving toward obstacle
                particles[pid].vx += normal_x * v_toward;
                particles[pid].vy += normal_y * v_toward;
            }
        }
    }
}

void HumanoidLocomotion::apply_fk_transforms(HumanoidParts& parts_ref, ParticleSystem::WriteView& particles) {
    auto& dyn = impl_->get_dynamics_system();
    // Use explicit namespace to avoid Vec3 ambiguity
    namespace lm = logosphere;

    HumanoidParts* parts = &parts_ref;
    auto& physics = impl_->get_physics_system();

    // Get entity root transform (hips position + facing)
    Particle& hips = particles[parts->hips];
    float facing = hips.rotation_z;

    lm::Transform entity_transform = {
        lm::Vec3{hips.x, hips.y, hips.z},
        lm::Quat::from_axis_angle(0, 0, 1, -facing)
    };

    // Map particle ID to its world transform (for parent lookup)
    std::unordered_map<unsigned int, lm::Transform> world_transforms;
    world_transforms[parts->hips] = entity_transform;

    // Initialize rest_local for joints that haven't been set up yet
    // (lazy initialization from gluon offsets)
    for (auto& joint : parts->joint_hierarchy.joints) {
        // Check if pivot_offset needs initialization (all zeros = uninitialized)
        // Only init once - cached values are immune to gluon corruption
        bool needs_init = (joint.pivot_offset.x == 0.0f &&
                          joint.pivot_offset.y == 0.0f &&
                          joint.pivot_offset.z == 0.0f &&
                          joint.child_offset.x == 0.0f &&
                          joint.child_offset.y == 0.0f &&
                          joint.child_offset.z == 0.0f);

        if (needs_init) {
            // Query gluon for segment offsets
            const auto* gluon = physics.get_gluon(joint.parent_particle, joint.child_particle);
            if (!gluon) continue;

            // Access gluon members directly (not via getters)
            ::Vec3 offset_a = gluon->offset_a;  // Use global Vec3 from physics
            ::Vec3 offset_b = gluon->offset_b;

            bool reversed = (gluon->particle_a != joint.parent_particle);
            if (reversed) std::swap(offset_a, offset_b);

            // Store offset_a for pivot calculation and -offset_b for child offset
            // Gluon FK formula: child = pivot - rotated(offset_b)
            // Where: pivot = parent + rotated(offset_a)
            //
            // For transform composition, we need:
            //   rest_local.position = where child center is relative to parent center (at rest)
            //   = offset_a - offset_b (both in local frame at rest)
            joint.rest_local.position = lm::Vec3{
                offset_a.x - offset_b.x,
                offset_a.y - offset_b.y,
                offset_a.z - offset_b.z
            };
            joint.rest_local.rotation = lm::Quat::identity();

            // Store offset_b for per-joint pivot offset calculation
            joint.pivot_offset = lm::Vec3{offset_a.x, offset_a.y, offset_a.z};
            joint.child_offset = lm::Vec3{offset_b.x, offset_b.y, offset_b.z};

            // compound_rotation() now handles axis composition
        }
    }

    // Process joints in order (parents before children - guaranteed by hierarchy)
    for (auto& joint : parts->joint_hierarchy.joints) {
        // Step 1: Get parent's world transform
        // If parent was FK-positioned, use that. Otherwise, use parent particle's actual position/rotation.
        lm::Transform parent_world;
        auto parent_it = world_transforms.find(joint.parent_particle);
        if (parent_it != world_transforms.end()) {
            parent_world = parent_it->second;
        } else {
            // Parent not FK-positioned - build transform from particle's actual state
            Particle& parent_p = particles[joint.parent_particle];
            parent_world.position = lm::Vec3{parent_p.x, parent_p.y, parent_p.z};
            // Build rotation from particle's Euler angles
            // ZYX order: Rz * Ry * Rx
            // Negate rotation_z: particle stores CW convention (from to_euler),
            // but axis-angle uses standard CCW
            lm::Quat qz = lm::Quat::from_axis_angle(0, 0, 1, -parent_p.rotation_z);
            lm::Quat qy = lm::Quat::from_axis_angle(0, 1, 0, parent_p.rotation_y);
            lm::Quat qx = lm::Quat::from_axis_angle(1, 0, 0, parent_p.rotation_x);
            parent_world.rotation = qz * qy * qx;
        }

        // Step 2: Compute pivot point in world space
        // pivot = parent_pos + parent_rot.rotate(pivot_offset)
        lm::Vec3 pivot = parent_world.transform_point(joint.pivot_offset);

        // Step 3: Compute joint rotation
        // PHYSICS mode: bone hangs straight down (gravity) from FK-updated pivot.
        // Semantic mode: semantic_rotation() maps anatomical commands to local axes.
        // Legacy mode: compound_rotation() for raw angle API.
        lm::Quat joint_rotation;
        if (joint.mode == JointMode::PHYSICS) {
            // PHYSICS mode: compute local rotation that makes bone point in
            // world -Z direction, then follow normal FK path.
            // This keeps the FK chain intact for downstream joints.

            // Rest-pose bone direction in parent's local frame: -child_offset (normalized)
            float bx = -joint.child_offset.x;
            float by = -joint.child_offset.y;
            float bz = -joint.child_offset.z;

            // World gravity direction (-Z) transformed to parent's local frame
            lm::Quat parent_inv = parent_world.rotation.conjugate();
            float gx, gy, gz;
            parent_inv.rotate_vector(0.0f, 0.0f, -1.0f, gx, gy, gz);

            // Local rotation: rotate rest bone direction to gravity direction
            joint_rotation = lm::Quat::from_two_vectors(bx, by, bz, gx, gy, gz);

        } else if (joint.has_flex_target || joint.has_abduct_target || joint.has_twist_target) {
            joint_rotation = joint.semantic_rotation();
        } else {
            joint_rotation = joint.compound_rotation();
        }

        // Step 4: Compute world rotation for child
        // child_world_rot = parent_world_rot * joint_rotation
        lm::Quat child_world_rot = parent_world.rotation * joint_rotation;

        // Step 5: Compute child position
        // child_pos = pivot - child_world_rot.rotate(child_offset)
        // (child_offset points FROM child center TO pivot, so we subtract)
        lm::Vec3 rotated_child_offset;
        child_world_rot.rotate_vector(
            joint.child_offset.x, joint.child_offset.y, joint.child_offset.z,
            rotated_child_offset.x, rotated_child_offset.y, rotated_child_offset.z
        );

        lm::Vec3 child_pos = {
            pivot.x - rotated_child_offset.x,
            pivot.y - rotated_child_offset.y,
            pivot.z - rotated_child_offset.z
        };

        // Store computed transforms
        joint.world_transform.position = child_pos;
        joint.world_transform.rotation = child_world_rot;
        joint.local_transform.rotation = joint_rotation;

        // FK ARM DEBUG: trace right arm chain positions and lateral spread
        if (dyn.fk_arm_debug_frames > 0 &&
            (joint.name == "right_chest_shoulder" || joint.name == "right_shoulder" ||
             joint.name == "right_elbow" || joint.name == "right_wrist" ||
             joint.name == "left_chest_shoulder" || joint.name == "left_shoulder" ||
             joint.name == "left_elbow" || joint.name == "left_wrist")) {
            // Compute body-local right axis from hips facing
            float cos_f = std::cos(-facing);
            float sin_f = std::sin(-facing);
            // body_right = Rz(-facing) * (1,0,0) = (cos_f, sin_f, 0)
            float body_right_x = cos_f;
            float body_right_y = sin_f;
            // Lateral = dot(child_pos - hips_pos, body_right)
            float dx = child_pos.x - hips.x;
            float dy = child_pos.y - hips.y;
            float lateral = dx * body_right_x + dy * body_right_y;
            // Joint rotation components for debugging
            float jr_w = joint_rotation.w, jr_x = joint_rotation.x, jr_y = joint_rotation.y, jr_z = joint_rotation.z;
            std::printf("[FK_TRACE] %-24s child_id=%u pos=(%.3f,%.3f,%.3f) lateral=%.4f "
                        "pivot=(%.3f,%.3f,%.3f) pivot_off=(%.3f,%.3f,%.3f) child_off=(%.3f,%.3f,%.3f) "
                        "jrot=(%.3f,%.3f,%.3f,%.3f) flex=%.3f abduct=%.3f mode=%d\n",
                        joint.name.c_str(), joint.child_particle,
                        child_pos.x, child_pos.y, child_pos.z, lateral,
                        pivot.x, pivot.y, pivot.z,
                        joint.pivot_offset.x, joint.pivot_offset.y, joint.pivot_offset.z,
                        joint.child_offset.x, joint.child_offset.y, joint.child_offset.z,
                        jr_w, jr_x, jr_y, jr_z,
                        joint.flex_angle, joint.abduct_angle,
                        static_cast<int>(joint.mode));
        }

        // Store world transform for downstream joints
        world_transforms[joint.child_particle] = joint.world_transform;

        // Step 3: Collide-and-slide from current position to FK target.
        // Quake-style algorithm: sweep AABB along motion; on hit, clamp to
        // contact and project remaining motion onto the contact plane so
        // the particle slides along the surface. Iterate (max 3) to handle
        // sliding into new surfaces (corners). Gravity-independent.
        Particle& child = particles[joint.child_particle];
        float px = child.x, py = child.y, pz = child.z;  // Current position
        float tx = joint.world_transform.position.x;
        float ty = joint.world_transform.position.y;
        float tz = joint.world_transform.position.z;
        float mx = tx - px, my = ty - py, mz = tz - pz;  // Remaining motion

        const BVH* bvh = impl_->get_particle_system().get_shadow_bvh();
        float hx = child.width * 0.5f, hy = child.height * 0.5f, hz = child.thickness * 0.5f;

        for (int slide_iter = 0; slide_iter < 3; slide_iter++) {
            float motion_len2 = mx*mx + my*my + mz*mz;
            if (motion_len2 < 1e-10f) break;  // No more motion

            float sx = px + mx, sy = py + my, sz = pz + mz;
            AABB6 start_aabb = {px - hx, px + hx, py - hy, py + hy, pz - hz, pz + hz};
            AABB6 end_aabb = {sx - hx, sx + hx, sy - hy, sy + hy, sz - hz, sz + hz};
            AABB sweep_query(
                std::min(start_aabb.min_x, end_aabb.min_x),
                std::min(start_aabb.min_y, end_aabb.min_y),
                std::min(start_aabb.min_z, end_aabb.min_z),
                std::max(start_aabb.max_x, end_aabb.max_x),
                std::max(start_aabb.max_y, end_aabb.max_y),
                std::max(start_aabb.max_z, end_aabb.max_z));

            std::vector<int> sweep_candidates;
            if (bvh && bvh->is_ready()) {
                bvh->query_aabb(sweep_query, particles.get_particles(), sweep_candidates);
            }

            float earliest_t = 1.0f;
            float nx = 0, ny = 0, nz = 0;
            bool had_hit = false;
            for (int cand : sweep_candidates) {
                size_t k = (size_t)cand;
                if (k == (size_t)joint.child_particle || k == (size_t)joint.parent_particle) continue;
                const Particle& pk = particles[k];
                if (!pk.is_at_rest) continue;
                float khx = pk.width * 0.5f, khy = pk.height * 0.5f, khz = pk.thickness * 0.5f;
                AABB6 target_aabb = {pk.x - khx, pk.x + khx, pk.y - khy, pk.y + khy, pk.z - khz, pk.z + khz};
                float t_hit, hit_nx, hit_ny, hit_nz;
                if (swept_aabb_vs_aabb(start_aabb, end_aabb, target_aabb, t_hit, hit_nx, hit_ny, hit_nz)) {
                    if (t_hit < earliest_t) {
                        earliest_t = t_hit;
                        nx = hit_nx; ny = hit_ny; nz = hit_nz;
                        had_hit = true;
                    }
                }
            }

            if (!had_hit) {
                // Complete the remaining motion
                px += mx; py += my; pz += mz;
                break;
            }

            // Move to contact point with small backoff
            constexpr float BACKOFF = 0.001f;
            float safe_t = std::max(0.0f, earliest_t - BACKOFF);
            px += mx * safe_t;
            py += my * safe_t;
            pz += mz * safe_t;

            // Project remaining motion onto contact plane: slide = remaining - (remaining·n)*n
            float remaining = 1.0f - safe_t;
            float rmx = mx * remaining, rmy = my * remaining, rmz = mz * remaining;
            float rdotn = rmx * nx + rmy * ny + rmz * nz;
            mx = rmx - rdotn * nx;
            my = rmy - rdotn * ny;
            mz = rmz - rdotn * nz;
        }

        // Physics-drive: skip the position + rotation write on this
        // particle. The solver owns rotation_z via the joint's gluon
        // PD, and the gluon's distance constraint keeps the pivot
        // aligned so position follows from the parent automatically.
        if (parts->physics_drive_children.count(joint.child_particle)) {
            continue;
        }

        float fk_old_x = child.x, fk_old_y = child.y, fk_old_z = child.z;
        child.x = px;
        child.y = py;
        child.z = pz;
        {
            auto& fk_tracer = impl_->get_particle_tracer();
            TRACE_POS_WRITE(fk_tracer, static_cast<int>(joint.child_particle),
                            "FK.apply_fk_transforms",
                            fk_old_x, fk_old_y, fk_old_z,
                            child.x, child.y, child.z);
        }

        // Extract Euler angles for rendering compatibility
        float euler_x, euler_y, euler_z;
        joint.world_transform.to_euler(euler_x, euler_y, euler_z);
        child.rotation_x = euler_x;
        child.rotation_y = euler_y;
        child.rotation_z = euler_z;

        // Store FK joint angles (for animation debugging/assertions)
        child.fk_rotation_x = joint.rotation_x;
        child.fk_rotation_y = joint.rotation_y;
        child.fk_rotation_z = joint.rotation_z;

        // Mark as animation-owned
        child.owner = ParticleOwner::ANIMATION;
    }

    // Decrement FK debug counter
    if (dyn.fk_arm_debug_frames > 0) {
        dyn.fk_arm_debug_frames--;
    }

    // POST-FK TORSO COLLISION: enforce minimum lateral spread for arm particles.
    // When the torso is twisted (spine rotation during walking/turning), arm flex
    // rotations compound through the chain and can drift the arm medially. In real
    // humans, the ribcage physically blocks the arm from crossing the body midline.
    // This post-FK pass models that collision by clamping arm particles to a minimum
    // lateral distance from the body's sagittal plane.
    {
        const float min_lateral = 0.05f;  // Minimum lateral distance (meters) — arm can't pass through ribcage

        // Body-right vector from facing
        float cos_f = std::cos(-facing);
        float sin_f = std::sin(-facing);
        float body_right_x = cos_f;
        float body_right_y = sin_f;

        // Clamp function for one arm chain
        auto clamp_arm_lateral = [&](const std::vector<unsigned int>& arm_particles, float sign) {
            // sign: +1.0 for right arm (should be positive lateral), -1.0 for left arm
            for (unsigned int pid : arm_particles) {
                // Physics-drive owns this particle's position — ribcage
                // blocking is handled by particle contacts, not this clamp.
                if (parts->physics_drive_children.count(pid)) continue;
                Particle& p = particles[pid];
                float dx = p.x - hips.x;
                float dy = p.y - hips.y;
                float lateral = dx * body_right_x + dy * body_right_y;

                // Right arm: lateral should be >= min_lateral
                // Left arm:  lateral should be <= -min_lateral (so sign*lateral >= min_lateral)
                float signed_lateral = sign * lateral;
                if (signed_lateral < min_lateral) {
                    // Push particle outward along body-right axis
                    float deficit = min_lateral - signed_lateral;
                    float clamp_ox = p.x, clamp_oy = p.y, clamp_oz = p.z;
                    p.x += sign * body_right_x * deficit;
                    p.y += sign * body_right_y * deficit;
                    auto& clamp_tracer = impl_->get_particle_tracer();
                    TRACE_POS_WRITE(clamp_tracer, static_cast<int>(pid),
                                    "FK.arm_lateral_clamp",
                                    clamp_ox, clamp_oy, clamp_oz,
                                    p.x, p.y, p.z);
                }
            }
        };

        clamp_arm_lateral(parts->right_arm_particles, 1.0f);
        clamp_arm_lateral(parts->left_arm_particles, -1.0f);
    }
}

void HumanoidLocomotion::project_chain_geometry(
    HumanoidParts& parts, ParticleSystem::WriteView& particles)
{
    auto& dyn = impl_->get_dynamics_system();
    // Project ALL joints. foot_planting_IK runs AFTER this and will
    // override stance-leg particles with its anchor-based rebuild; no
    // need to skip here. Earlier this did skip stance, but the FK chain
    // at mid-frame (before heel-strike transfer) is also broken for the
    // just-exited stance leg, and not projecting it leaves the drift
    // locked in until that side becomes stance again. Project first,
    // let IK have the final say.
    auto& tracer = impl_->get_particle_tracer();

    for (auto& joint : parts.joint_hierarchy.joints) {
        unsigned int pid = joint.child_particle;
        if (pid >= particles.size()) continue;

        // Physics-drive owns this particle — its position comes from
        // the gluon distance constraint, not the FK joint transform.
        if (parts.physics_drive_children.count(pid)) continue;

        Particle& p = particles[pid];
        float ox = p.x, oy = p.y, oz = p.z;
        p.x = joint.world_transform.position.x;
        p.y = joint.world_transform.position.y;
        p.z = joint.world_transform.position.z;

        TRACE_POS_WRITE(tracer, static_cast<int>(pid),
                        "chain.project",
                        ox, oy, oz,
                        p.x, p.y, p.z);
    }
}


void HumanoidLocomotion::publish_physics_drive_targets(HumanoidParts& parts) {
    auto& dyn = impl_->get_dynamics_system();
    if (parts.physics_drive_children.empty()) return;
    if (!impl_->engine) return;
    auto& physics = impl_->get_physics_system();

    // Yaw cascade through drive targets. The legacy cascade wrote
    // rotation_z directly onto spine/neck/head particles; those are
    // drive children now, so the writes were skipped and the
    // eyes→head→torso→hips lag chain died with Phase E. Re-express it
    // here: distribute (torso − hips) yaw across the two spine joints
    // and (head − torso) across neck + head, composed onto the clip
    // pose. Steady state: chest tracks torso_yaw_world, head tracks
    // head_yaw_world — the cascade semantics, through the solver.
    // The solver enforces q_child = target_relative_q · q_parent
    // (world frame), so extra yaw composes as a left-multiplied
    // Z-axis quat.
    auto norm = [](float a) {
        while (a > static_cast<float>(M_PI))  a -= 2.0f * static_cast<float>(M_PI);
        while (a <= -static_cast<float>(M_PI)) a += 2.0f * static_cast<float>(M_PI);
        return a;
    };
    float spine_delta = 0.0f, neck_delta = 0.0f;
    if (parts.yaw_cascade_inited) {
        spine_delta = 0.5f * norm(parts.torso_yaw_world - parts.hips_yaw_world);
        neck_delta  = 0.5f * norm(parts.head_yaw_world  - parts.torso_yaw_world);
    }

    for (const auto& joint : parts.joint_hierarchy.joints) {
        if (!parts.physics_drive_children.count(joint.child_particle)) continue;

        // Static-target joints (set via set_joint_physics_drive[_q])
        // keep whatever gluon target the caller wrote; the publisher
        // is for clip-driven joints only.
        if (parts.physics_drive_static_targets.count(joint.child_particle)) continue;

        GluonConstraintBase* gluon = physics.get_gluon_mut(joint.parent_particle,
                                                           joint.child_particle);
        if (!gluon) continue;

        logosphere::Quat target = joint.semantic_rotation();

        float cascade_dz = 0.0f;
        if (joint.name == "lower_spine" || joint.name == "upper_spine") {
            cascade_dz = spine_delta;
        } else if (joint.name == "neck" || joint.name == "head") {
            cascade_dz = neck_delta;
        }
        if (cascade_dz != 0.0f) {
            // Negative: the solver's relative-target convention runs
            // opposite to the engine's CW-positive rotation_z for a
            // left-composed Z quat (arbitrated by
            // test_rotation_cascade_yaw — positive sign turns the head
            // away from the look target).
            target = logosphere::Quat::from_axis_angle(0.0f, 0.0f, 1.0f, -cascade_dz)
                     * target;
        }

        // Sign flip if the gluon stores the pair in reverse order.
        // Mirrors the bookkeeping in set_joint_physics_drive_q.
        if (gluon->particle_a == joint.child_particle &&
            gluon->particle_b == joint.parent_particle) {
            target = target.conjugate();
        }
        gluon->target_relative_q = target;
    }
}

}  // namespace logosphere::animation
