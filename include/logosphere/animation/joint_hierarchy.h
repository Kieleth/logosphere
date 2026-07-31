#ifndef LOGOSPHERE_ANIMATION_JOINT_HIERARCHY_H
#define LOGOSPHERE_ANIMATION_JOINT_HIERARCHY_H

// Joint + JointHierarchy — anatomical FK joints used by humanoid /
// serpent / butterfly locomotion modules.
//
// Lives in include/logosphere/animation/ on purpose: these types are
// animation-layer policy, not generic dynamics. Particle-dynamics-
// system carries them today (B3 of the rehaul, see plan
// the kinematic-root design (docs/ARCHITECTURE.md)) but during the
// migration ParticleDynamicsSystem keeps a `using` alias so existing
// callers compile unchanged.
//
// Each anatomical joint connects two particles with a named rotation.
// One joint per anatomical joint (shoulder, elbow, wrist) — multiple
// rotation channels (flex / abduct / twist) compose into a single
// quaternion before FK. Gluons hold segment length + rotation limits;
// joints hold the current rotation state.

#include "core/joint_types.h"      // logosphere::JointDefinition
#include "math/mat4.h"             // logosphere::Vec3
#include "math/quat.h"             // logosphere::Quat
#include "math/transform.h"        // logosphere::Transform

#include <string>
#include <unordered_map>
#include <vector>

namespace logosphere::animation {

// How a joint target is interpreted by FK.
// (Mirrors JointTargetType from animation_types.h for decoupling.)
enum class JointMode {
    DRIVEN,      // Explicit local-space angle (default)
    DIRECTION,   // Point in world-space direction (FK computes compensation)
    INHERIT,     // Follow parent (local angle = 0)
    PHYSICS      // Let physics/gluon dynamics control (skip FK for this joint)
};

// A joint connects two particles with a named rotation.
//
// ARCHITECTURE: ONE joint per anatomical joint (shoulder, elbow, wrist).
// Each joint has COMPOUND rotation (rotation_x/y/z) composed into a
// single quaternion BEFORE FK. This avoids the bug where multiple
// joints targeting the same child_particle overwrite each other.
//
// Gluons define segment length and rotation limits. Joints store
// current rotation state for FK animation.
struct Joint {
    std::string name;               // "right_shoulder", "right_elbow", etc.
    unsigned int parent_particle;   // Particle closer to root (e.g., shoulder)
    unsigned int child_particle;    // Particle farther from root (e.g., upper_arm)

    // === COMPOUND ROTATION ===
    // Multiple rotation channels composed into a single quaternion
    // BEFORE FK. Replaces the old single-axis approach.
    float rotation_x = 0.0f;        // Twist/roll (around bone axis)
    float rotation_y = 0.0f;        // Abduction (Y-axis, e.g., shoulder raise)
    float rotation_z = 0.0f;        // Flexion (Z-axis, e.g., arm forward/back)

    // Compute compound rotation as quaternion (extrinsic XYZ order).
    // X first, Y second (abduction), Z third (horizontal swing).
    // Quaternion math: Q2 * Q1 applies Q1 first, then Q2 — so
    // qz * qy * qx = X applied first, Y second, Z third (all in
    // world frame).
    logosphere::Quat compound_rotation() const {
        logosphere::Quat qx = logosphere::Quat::from_axis_angle(1, 0, 0, rotation_x);
        logosphere::Quat qy = logosphere::Quat::from_axis_angle(0, 1, 0, rotation_y);
        logosphere::Quat qz = logosphere::Quat::from_axis_angle(0, 0, 1, rotation_z);
        return qz * qy * qx;
    }

    // Transform-based FK
    logosphere::Transform rest_local;      // Bind pose (from gluon offsets)
    logosphere::Transform local_transform; // Current (animated)
    logosphere::Transform world_transform; // Computed by FK

    // Gluon offsets cached from lazy init for FK calculation:
    //   pivot_offset: offset_a (parent center → pivot point)
    //   child_offset: offset_b (child center → pivot point)
    logosphere::Vec3 pivot_offset = {0.0f, 0.0f, 0.0f};
    logosphere::Vec3 child_offset = {0.0f, 0.0f, 0.0f};

    // Current control mode (set by animation):
    //   DRIVEN:    use rotation_x/y/z angles (default)
    //   PHYSICS:   skip FK, let physics/gravity control position
    //   DIRECTION: point in world-space direction
    //   INHERIT:   follow parent rotation (angle = 0)
    JointMode mode = JointMode::DRIVEN;

    // === ANATOMICAL JOINT DEFINITION ===
    // Defines what "flex", "abduct", "twist" mean for this joint.
    // Shared template — owned by joint_types.cpp globals; do not delete.
    // nullptr = no semantic support, use raw rotation_x/y/z only.
    const logosphere::JointDefinition* definition = nullptr;

    // === SEMANTIC ROTATION TARGETS ===
    // Set by animation via flex() / abduct() / twist() API.
    // FK reads these and maps to the correct axis via definition.
    // Raw rotation_x/y/z are used when definition is nullptr or for
    // legacy API.
    float flex_angle = 0.0f;
    float abduct_angle = 0.0f;
    float twist_angle = 0.0f;
    bool has_flex_target = false;
    bool has_abduct_target = false;
    bool has_twist_target = false;

    // Clear semantic targets (called at the start of each animation
    // frame).
    void clear_semantic_targets() {
        flex_angle = 0.0f;
        abduct_angle = 0.0f;
        twist_angle = 0.0f;
        has_flex_target = false;
        has_abduct_target = false;
        has_twist_target = false;
    }

    // Compute rotation quaternion from semantic targets using the
    // joint definition. Falls back to compound_rotation() if no
    // definition or no semantic targets.
    logosphere::Quat semantic_rotation() const {
        if (!definition) {
            return compound_rotation();
        }

        logosphere::Quat result = logosphere::Quat::identity();

        // Apply semantic rotations: twist, then flex, then abduct.
        // Intrinsic order: flex in body plane first, then abduct to
        // raise (innermost first in quaternion multiplication).
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

// Per-entity joint hierarchy (ordered for FK: parents before children).
struct JointHierarchy {
    std::vector<Joint> joints;
    std::unordered_map<std::string, size_t> name_to_index;

    // Add a joint (call in parent-first order for correct FK).
    void add_joint(const Joint& joint) {
        name_to_index[joint.name] = joints.size();
        joints.push_back(joint);
    }

    // Get joint by name (nullptr if not found).
    Joint* get_joint(const std::string& name) {
        auto it = name_to_index.find(name);
        return (it != name_to_index.end()) ? &joints[it->second] : nullptr;
    }

    const Joint* get_joint(const std::string& name) const {
        auto it = name_to_index.find(name);
        return (it != name_to_index.end()) ? &joints[it->second] : nullptr;
    }
};

}  // namespace logosphere::animation

#endif  // LOGOSPHERE_ANIMATION_JOINT_HIERARCHY_H
