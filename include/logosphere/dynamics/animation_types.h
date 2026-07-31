// Animation Types - Physics-Driven Animation via Motor Forces
//
// Minimal structures for skeletal animation using PD controller motor forces.
// Animation provides TARGET positions, physics provides ACTUAL motion.
//
// Key concept: PD controller calculates force: F = Kp*(target-pos) - Kd*vel
// This integrates with existing particle forces (fx, fy, fz on Particle struct).

#ifndef LOGOSPHERE_ANIMATION_TYPES_H
#define LOGOSPHERE_ANIMATION_TYPES_H

#include <vector>
#include <string>
#include <unordered_map>
#include <tuple>
#include <cmath>

// Target position for a single particle
// Offsets are relative to rest position (not absolute world coords)
struct ParticleTarget {
    unsigned int particle_id = 0;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float offset_z = 0.0f;
};

// A pose is a set of target positions for multiple particles
struct AnimationPose {
    std::vector<ParticleTarget> targets;

    void add_target(unsigned int particle_id, float ox, float oy, float oz);
};

// A keyframe is a pose at a specific time
struct AnimationKeyframe {
    float time_ms = 0.0f;
    AnimationPose pose;
};

// An animation clip is a sequence of keyframes
struct AnimationClip {
    std::string name;
    std::vector<AnimationKeyframe> keyframes;
    float duration_ms = 0.0f;
    bool loops = false;

    void add_keyframe(float time_ms, const AnimationPose& pose);
    bool get_pose_at_time(float time_ms, AnimationPose& out_pose) const;
};

// Animation state for a single playing animation
struct AnimationState {
    const AnimationClip* clip = nullptr;
    float current_time_ms = 0.0f;
    bool is_playing = false;
    std::unordered_map<unsigned int, std::tuple<float, float, float>> rest_positions;

    void start(const AnimationClip* animation_clip);
    void stop();
    bool update(float dt_ms);
    bool get_current_pose(AnimationPose& out_pose) const;
};

// =============================================================================
// FK-BASED ANIMATION (Rotation targets instead of position offsets)
// =============================================================================
// Animation sets joint angles, FK computes positions from gluon segment lengths.
// Segment distances stay constant - no stretching.
//
// KEY CONCEPT: Joints can be controlled in different "modes":
//   - DRIVEN:    Explicit angle in local space (parent-relative)
//   - DIRECTION: Point toward a world-space direction (e.g., down for "relaxed")
//   - INHERIT:   Follow parent rotation (rigid attachment, angle=0)
//
// This allows semantic primitives like "relax" (hang down) without manual
// angle computation to compensate for parent rotations.

// How a joint target is interpreted by FK
enum class JointTargetType {
    DRIVEN,      // Explicit local-space angle (current behavior)
    DIRECTION,   // Point in world-space direction (FK computes compensation)
    INHERIT,     // Follow parent (local angle = 0, rigid attachment)
    PHYSICS,     // Let physics/gluon dynamics control (skip FK entirely)
    SEMANTIC     // Anatomical target - FK resolves via JointDefinition
};

// Simple 3D vector for direction (avoids dependency on math headers)
struct Direction3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    static Direction3 down()    { return {0.0f, 0.0f, -1.0f}; }
    static Direction3 up()      { return {0.0f, 0.0f,  1.0f}; }
    static Direction3 forward() { return {0.0f, 1.0f,  0.0f}; }
    static Direction3 back()    { return {0.0f, -1.0f, 0.0f}; }
    static Direction3 right()   { return {1.0f, 0.0f,  0.0f}; }
    static Direction3 left()    { return {-1.0f, 0.0f, 0.0f}; }
};

// Which rotation axis to target (for compound rotation)
enum class RotationAxis {
    Y,  // Default: abduction/adduction (most common for shoulders/hips)
    X,  // Flexion/extension (elbows, knees)
    Z   // Twist/horizontal rotation
};

// Semantic rotation channel (anatomical meaning)
// Used with SEMANTIC target type - FK resolves to correct axis via JointDefinition
enum class SemanticChannel {
    FLEX,    // Flexion/extension (decreasing/increasing joint angle)
    ABDUCT,  // Abduction/adduction (away from/toward body midline)
    TWIST    // Internal/external rotation (along bone axis)
};

// Target rotation for a named joint
struct JointTarget {
    std::string joint_name;                          // e.g., "right_shoulder", "right_elbow"
    JointTargetType type = JointTargetType::DRIVEN;  // How to interpret this target
    float angle = 0.0f;                              // For DRIVEN/SEMANTIC: rotation in radians
    RotationAxis axis = RotationAxis::Y;             // Which axis to rotate around (DRIVEN mode)
    Direction3 direction = Direction3::down();       // For DIRECTION: world-space target
    SemanticChannel semantic = SemanticChannel::FLEX; // For SEMANTIC: which anatomical motion
};

// A rotation pose is a set of joint targets
struct RotationPose {
    std::vector<JointTarget> targets;

    // === SEMANTIC HELPERS ===
    // These provide clear intent and encapsulate the target type

    // DRIVEN: Explicit angle in local space (parent-relative)
    // Use for animated movements: flex, extend, abduct, etc.
    // Default axis is Y (most common: shoulder/hip abduction)
    void drive(const std::string& joint_name, float angle) {
        targets.push_back({joint_name, JointTargetType::DRIVEN, angle, RotationAxis::Y, {}});
    }

    // Axis-specific rotation (for compound rotation joints)
    // Use these when a joint needs multiple axes controlled
    void drive_x(const std::string& joint_name, float angle) {
        targets.push_back({joint_name, JointTargetType::DRIVEN, angle, RotationAxis::X, {}});
    }
    void drive_y(const std::string& joint_name, float angle) {
        targets.push_back({joint_name, JointTargetType::DRIVEN, angle, RotationAxis::Y, {}});
    }
    void drive_z(const std::string& joint_name, float angle) {
        targets.push_back({joint_name, JointTargetType::DRIVEN, angle, RotationAxis::Z, {}});
    }

    // DIRECTION: Point joint in world-space direction
    // FK computes the local angle needed to achieve this world orientation
    // Use sparingly - only works when joint axis can reach target direction
    void point(const std::string& joint_name, const Direction3& world_dir) {
        targets.push_back({joint_name, JointTargetType::DIRECTION, 0.0f, RotationAxis::Y, world_dir});
    }

    // RELAX: Let physics/gluon dynamics control this joint naturally
    // Maps to PHYSICS - FK skips this joint, physics determines position
    // Use for natural hanging (gravity + gluon constraints)
    void relax(const std::string& joint_name) {
        targets.push_back({joint_name, JointTargetType::PHYSICS, 0.0f, RotationAxis::Y, {}});
    }

    // INHERIT: Follow parent rotation (rigid attachment)
    // Local angle = 0, child rotates with parent
    // Alias: same as relax() - both mean "no active joint rotation"
    void rigid(const std::string& joint_name) {
        targets.push_back({joint_name, JointTargetType::INHERIT, 0.0f, RotationAxis::Y, {}});
    }

    // Legacy: add_target maps to drive() for backward compatibility
    void add_target(const std::string& joint_name, float angle) {
        drive(joint_name, angle);
    }

    // =========================================================================
    // SEMANTIC API - Anatomical motion commands
    // =========================================================================
    // These use joint definitions to map semantic commands to correct local axes.
    // The meaning of "flex" is consistent regardless of parent orientation:
    //   - flex("right_elbow", angle) always bends forearm toward body
    //   - abduct("right_shoulder", angle) always raises arm laterally
    //
    // FK resolves these via JointDefinition at runtime.

    // FLEX: Flexion/extension motion
    // For hinges (elbow, knee): the primary/only motion
    // For ball-sockets (shoulder, hip): forward/back motion
    void flex(const std::string& joint_name, float angle) {
        JointTarget target;
        target.joint_name = joint_name;
        target.type = JointTargetType::SEMANTIC;
        target.angle = angle;
        target.semantic = SemanticChannel::FLEX;
        targets.push_back(target);
    }

    // ABDUCT: Abduction/adduction motion
    // Moving limb away from (positive) or toward (negative) body midline
    // Only valid for ball-socket joints (shoulder, hip)
    void abduct(const std::string& joint_name, float angle) {
        JointTarget target;
        target.joint_name = joint_name;
        target.type = JointTargetType::SEMANTIC;
        target.angle = angle;
        target.semantic = SemanticChannel::ABDUCT;
        targets.push_back(target);
    }

    // TWIST: Internal/external rotation
    // Rotation along the bone's long axis
    // Only valid for ball-socket or pivot joints
    void twist(const std::string& joint_name, float angle) {
        JointTarget target;
        target.joint_name = joint_name;
        target.type = JointTargetType::SEMANTIC;
        target.angle = angle;
        target.semantic = SemanticChannel::TWIST;
        targets.push_back(target);
    }
};

// A rotation keyframe is a pose at a specific time
struct RotationKeyframe {
    float time_ms = 0.0f;
    RotationPose pose;
};

// Body region for animation layering.
// Determines which joints a one-shot clip controls when overlaying locomotion.
//   FULL_BODY:  overrides all joints (default, backward compatible)
//   UPPER_BODY: shoulders, elbows, wrists, spine, neck, head
//   LOWER_BODY: hips, knees, ankles, toes
enum class BodyRegion {
    FULL_BODY,
    UPPER_BODY,
    LOWER_BODY,
};

// FK animation clip - stores rotation keyframes
struct FKAnimationClip {
    std::string name;
    std::vector<RotationKeyframe> keyframes;
    float duration_ms = 0.0f;
    bool loops = false;
    BodyRegion body_region = BodyRegion::FULL_BODY;

    // Crossfade: smooth blend-in/out when overlaying locomotion.
    // During blend_in_ms, overlay weight ramps 0→1 (locomotion → clip).
    // During blend_out_ms, overlay weight ramps 1→0 (clip → locomotion).
    // Default 0 = instant snap (backward compatible).
    float blend_in_ms = 0.0f;
    float blend_out_ms = 0.0f;

    void add_keyframe(float time_ms, const RotationPose& pose) {
        keyframes.push_back({time_ms, pose});
        if (time_ms > duration_ms) duration_ms = time_ms;
    }

    // Interpolate joint targets at given time
    // Handles DRIVEN (angle lerp), DIRECTION (direction lerp), and INHERIT (passthrough)
    bool get_pose_at_time(float time_ms, RotationPose& out_pose) const {
        if (keyframes.empty()) return false;

        // Clamp time
        if (time_ms <= keyframes.front().time_ms) {
            out_pose = keyframes.front().pose;
            return true;
        }
        if (time_ms >= keyframes.back().time_ms) {
            out_pose = keyframes.back().pose;
            return true;
        }

        // Find surrounding keyframes
        for (size_t i = 0; i < keyframes.size() - 1; ++i) {
            if (time_ms >= keyframes[i].time_ms && time_ms <= keyframes[i + 1].time_ms) {
                float t = (time_ms - keyframes[i].time_ms) /
                          (keyframes[i + 1].time_ms - keyframes[i].time_ms);

                const auto& from = keyframes[i].pose;
                const auto& to = keyframes[i + 1].pose;

                out_pose.targets.clear();
                for (const auto& from_target : from.targets) {
                    JointTarget interp_target;
                    interp_target.joint_name = from_target.joint_name;
                    interp_target.axis = from_target.axis;  // Default, may be overwritten below

                    // ================================================================
                    // MATCHING LOGIC: Find corresponding target in TO keyframe
                    // ================================================================
                    //
                    // WHY MATCHING IS NEEDED:
                    // Each keyframe is a bag of targets. To interpolate, we must pair
                    // each FROM target with its corresponding TO target.
                    //
                    // MATCHING RULES:
                    // 1. SEMANTIC: Match by joint_name + semantic channel
                    // 2. DRIVEN: Match by joint_name + axis
                    //    - One joint can have multiple DRIVEN targets (Y + Z for shoulder)
                    //    - Each axis interpolates independently
                    // 3. PHYSICS/INHERIT: Match by joint_name only
                    //    - These modes have no axis concept (physics controls position)
                    //    - When transitioning to DRIVEN, we match to find the target axis
                    //
                    const JointTarget* to_target_ptr = nullptr;
                    for (const auto& to_t : to.targets) {
                        if (to_t.joint_name != from_target.joint_name) continue;
                        if (from_target.type == JointTargetType::SEMANTIC) {
                            if (to_t.semantic == from_target.semantic) {
                                to_target_ptr = &to_t;
                                break;
                            }
                        } else if (from_target.type == JointTargetType::DRIVEN) {
                            if (to_t.axis == from_target.axis) {
                                to_target_ptr = &to_t;
                                break;
                            }
                        } else {
                            to_target_ptr = &to_t;
                            break;
                        }
                    }

                    // ================================================================
                    // AXIS RESOLUTION: Always use TO's axis when match found
                    // ================================================================
                    //
                    // WHY: The FROM axis may be a meaningless default (e.g., PHYSICS
                    // targets default to Y but don't actually use it). The TO axis
                    // is what we're animating toward.
                    //
                    // This is OUTSIDE the transition block because it applies to ALL
                    // matched pairs, not just type transitions.
                    //
                    if (to_target_ptr) {
                        interp_target.axis = to_target_ptr->axis;
                    }

                    // ================================================================
                    // TYPE TRANSITIONS: Handle mode changes (PHYSICS ↔ angle-controlled)
                    // ================================================================
                    JointTargetType from_type = from_target.type;
                    JointTargetType to_type = to_target_ptr ? to_target_ptr->type : from_type;

                    // Both DRIVEN and SEMANTIC are angle-controlled (local-space joint angles)
                    auto is_angle_controlled = [](JointTargetType t) {
                        return t == JointTargetType::DRIVEN || t == JointTargetType::SEMANTIC;
                    };
                    bool transitioning_to_angle = (is_angle_controlled(to_type) && !is_angle_controlled(from_type));
                    bool transitioning_from_angle = (is_angle_controlled(from_type) && !is_angle_controlled(to_type));

                    if (transitioning_to_angle || transitioning_from_angle) {
                        // PHYSICS ↔ DRIVEN/SEMANTIC TRANSITION
                        //
                        // PROBLEM: PHYSICS mode uses world-space "hang down" position.
                        //          DRIVEN/SEMANTIC mode uses local-space joint angles.
                        //          These are fundamentally different coordinate systems!
                        //
                        // CURRENT SOLUTION:
                        //   - Keep PHYSICS for 80% of transition (joint hangs naturally)
                        //   - Blend to target type in final 20% (assumes angle≈0 at rest)
                        //
                        if (transitioning_to_angle) {
                            // Transition TO angle-controlled: keep PHYSICS for first 80%
                            if (t < 0.8f) {
                                interp_target.type = JointTargetType::PHYSICS;
                            } else {
                                // Final 20%: blend to target type
                                interp_target.type = to_type;
                                if (to_type == JointTargetType::SEMANTIC) {
                                    interp_target.semantic = to_target_ptr->semantic;
                                }
                                float blend = (t - 0.8f) / 0.2f;  // 0→1 over last 20%
                                float to_angle = to_target_ptr->angle;
                                interp_target.angle = blend * to_angle;
                            }
                        } else {
                            // Transition FROM angle-controlled: scale angle down to 0
                            interp_target.type = from_type;
                            if (from_type == JointTargetType::SEMANTIC) {
                                interp_target.semantic = from_target.semantic;
                            }
                            float from_angle = from_target.angle;
                            interp_target.angle = from_angle * (1.0f - t);
                        }
                    }
                    else {
                        // Same type throughout - use standard interpolation
                        interp_target.type = from_type;

                        switch (from_type) {
                            case JointTargetType::DRIVEN: {
                                float from_angle = from_target.angle;
                                float to_angle = to_target_ptr ? to_target_ptr->angle : from_angle;
                                interp_target.angle = from_angle + t * (to_angle - from_angle);
                                break;
                            }
                            case JointTargetType::DIRECTION: {
                                // Lerp direction vectors (normalize after for safety)
                                Direction3 from_dir = from_target.direction;
                                Direction3 to_dir = to_target_ptr ? to_target_ptr->direction : from_dir;
                                interp_target.direction.x = from_dir.x + t * (to_dir.x - from_dir.x);
                                interp_target.direction.y = from_dir.y + t * (to_dir.y - from_dir.y);
                                interp_target.direction.z = from_dir.z + t * (to_dir.z - from_dir.z);
                                // Normalize (avoid div by zero)
                                float len = std::sqrt(interp_target.direction.x * interp_target.direction.x +
                                                      interp_target.direction.y * interp_target.direction.y +
                                                      interp_target.direction.z * interp_target.direction.z);
                                if (len > 0.0001f) {
                                    interp_target.direction.x /= len;
                                    interp_target.direction.y /= len;
                                    interp_target.direction.z /= len;
                                }
                                break;
                            }
                            case JointTargetType::INHERIT:
                                // No interpolation needed - always follow parent
                                break;
                            case JointTargetType::PHYSICS:
                                // No interpolation needed - physics controls this joint
                                break;
                            case JointTargetType::SEMANTIC: {
                                // Semantic targets interpolate angle and preserve semantic channel
                                interp_target.semantic = from_target.semantic;
                                float from_angle = from_target.angle;
                                float to_angle = to_target_ptr ? to_target_ptr->angle : from_angle;
                                interp_target.angle = from_angle + t * (to_angle - from_angle);
                                break;
                            }
                        }
                    }

                    out_pose.targets.push_back(interp_target);
                }
                return true;
            }
        }
        return false;
    }
};

// ============================================================================
// POSE BLENDING UTILITY
// ============================================================================
// Blends two RotationPoses by interpolating joint target angles.
// Matching is by joint_name + semantic channel. If a joint appears in only
// one pose, it's included at scaled weight from that pose.
//
// blend_t: 0.0 = 100% pose_a, 1.0 = 100% pose_b
// ============================================================================

inline RotationPose blend_rotation_poses(const RotationPose& pose_a, const RotationPose& pose_b, float blend_t) {
    blend_t = std::max(0.0f, std::min(1.0f, blend_t));

    // Build lookup: joint_name + semantic -> blend entry
    struct BlendEntry {
        JointTarget target;
        bool has_a = false;
        bool has_b = false;
        float angle_a = 0.0f;
        float angle_b = 0.0f;
    };

    auto make_key = [](const JointTarget& t) -> std::string {
        char ch = 'D';
        if (t.type == JointTargetType::SEMANTIC) {
            switch (t.semantic) {
                case SemanticChannel::FLEX: ch = 'F'; break;
                case SemanticChannel::ABDUCT: ch = 'A'; break;
                case SemanticChannel::TWIST: ch = 'T'; break;
                default: ch = 'D'; break;
            }
        }
        return t.joint_name + ":" + ch;
    };

    std::unordered_map<std::string, BlendEntry> entries;

    for (const auto& t : pose_a.targets) {
        auto key = make_key(t);
        auto& e = entries[key];
        e.target = t;
        e.has_a = true;
        e.angle_a = t.angle;
    }

    for (const auto& t : pose_b.targets) {
        auto key = make_key(t);
        auto& e = entries[key];
        if (!e.has_a) e.target = t;
        e.has_b = true;
        e.angle_b = t.angle;
    }

    RotationPose result;
    for (auto& [key, e] : entries) {
        JointTarget blended = e.target;
        if (e.has_a && e.has_b) {
            blended.angle = e.angle_a * (1.0f - blend_t) + e.angle_b * blend_t;
        } else if (e.has_a) {
            blended.angle = e.angle_a * (1.0f - blend_t);
        } else {
            blended.angle = e.angle_b * blend_t;
        }
        result.targets.push_back(blended);
    }

    return result;
}

// ============================================================================
// BODY REGION CLASSIFICATION
// ============================================================================
// Classifies a joint name as upper or lower body for animation layering.
// Upper body: shoulder, elbow, wrist, spine, neck, head
// Lower body: hip, knee, ankle, toe
// ============================================================================

inline bool is_upper_body_joint(const std::string& name) {
    if (name.find("shoulder") != std::string::npos) return true;
    if (name.find("elbow") != std::string::npos) return true;
    if (name.find("wrist") != std::string::npos) return true;
    if (name.find("spine") != std::string::npos) return true;
    if (name == "neck" || name == "head") return true;
    return false;
}

// ============================================================================
// LAYERED POSE MERGE
// ============================================================================
// Merges a base (locomotion) pose with an overlay (one-shot) pose.
// The overlay's body_region determines which joints it controls:
//   FULL_BODY:  overlay replaces base entirely
//   UPPER_BODY: overlay controls upper body, base controls lower body
//   LOWER_BODY: overlay controls lower body, base controls upper body
//
// overlay_weight (0.0-1.0) controls crossfade:
//   0.0 = pure base (overlay has no effect)
//   1.0 = full overlay (hard region split, default)
//   0.0-1.0 = blend between base and overlay for the overlay's joints
// ============================================================================

inline RotationPose merge_layered_poses(
    const RotationPose& base_pose,
    const RotationPose& overlay_pose,
    BodyRegion overlay_region,
    float overlay_weight = 1.0f)
{
    // Weight 0 = pure base
    if (overlay_weight <= 0.001f) {
        return base_pose;
    }

    // FULL_BODY at full weight = pure overlay (fast path)
    if (overlay_region == BodyRegion::FULL_BODY && overlay_weight >= 0.999f) {
        return overlay_pose;
    }

    // FULL_BODY with partial weight = blend all joints
    if (overlay_region == BodyRegion::FULL_BODY) {
        return blend_rotation_poses(base_pose, overlay_pose, overlay_weight);
    }

    // Partial body region: split by region, blend the overlay's joints
    RotationPose result;

    // Take base targets that are NOT in the overlay's region (unmodified)
    for (const auto& t : base_pose.targets) {
        bool is_upper = is_upper_body_joint(t.joint_name);
        bool overlay_owns = (overlay_region == BodyRegion::UPPER_BODY) ? is_upper : !is_upper;
        if (!overlay_owns) {
            result.targets.push_back(t);
        }
    }

    if (overlay_weight >= 0.999f) {
        // Full weight: hard split (original behavior)
        for (const auto& t : overlay_pose.targets) {
            bool is_upper = is_upper_body_joint(t.joint_name);
            bool overlay_owns = (overlay_region == BodyRegion::UPPER_BODY) ? is_upper : !is_upper;
            if (overlay_owns) {
                result.targets.push_back(t);
            }
        }
    } else {
        // Partial weight: blend overlay's joints with base's joints.
        // Extract region-specific sub-poses, blend, then add to result.
        RotationPose base_region, overlay_region_pose;
        for (const auto& t : base_pose.targets) {
            bool is_upper = is_upper_body_joint(t.joint_name);
            bool overlay_owns = (overlay_region == BodyRegion::UPPER_BODY) ? is_upper : !is_upper;
            if (overlay_owns) {
                base_region.targets.push_back(t);
            }
        }
        for (const auto& t : overlay_pose.targets) {
            bool is_upper = is_upper_body_joint(t.joint_name);
            bool overlay_owns = (overlay_region == BodyRegion::UPPER_BODY) ? is_upper : !is_upper;
            if (overlay_owns) {
                overlay_region_pose.targets.push_back(t);
            }
        }
        RotationPose blended = blend_rotation_poses(base_region, overlay_region_pose, overlay_weight);
        for (const auto& t : blended.targets) {
            result.targets.push_back(t);
        }
    }

    return result;
}

#endif // LOGOSPHERE_ANIMATION_TYPES_H
