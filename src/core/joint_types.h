// Joint Types - Anatomical Joint Constraint Definitions
//
// Defines semantic rotation axes per joint type. Animation authors use
// anatomical terms (flex, abduct, twist) that map to correct local axes
// regardless of parent orientation.
//
// Problem solved:
//   FK applies rotations in parent-local space. When parent rotates,
//   X/Y/Z axes change meaning. This makes animation authoring error-prone.
//
// Solution:
//   Define anatomical axes per joint. "flex" always means the same motion:
//   - Elbow flex = forearm toward body (regardless of shoulder orientation)
//   - Shoulder abduct = arm laterally (regardless of torso facing)
//
// Usage:
//   pose.flex("right_elbow", M_PI/2);    // Semantic: always bends forearm
//   // FK resolves to correct local axis based on joint definition

#ifndef LOGOSPHERE_JOINT_TYPES_H
#define LOGOSPHERE_JOINT_TYPES_H

#include "../math/transform.h"  // For Vec3
#include <cmath>

namespace logosphere {

// ============================================================================
// JOINT TYPE - Degrees of Freedom
// ============================================================================
// Real joints have limited DOF. This affects which semantic axes are valid.
//
// BALL_SOCKET: 3 DOF (shoulder, hip) - flex, abduct, twist all valid
// HINGE: 1 DOF (elbow, knee) - only flex valid
// PIVOT: 1 DOF (forearm rotation) - only twist valid
// FIXED: 0 DOF (rigid attachment) - no rotation

enum class JointType {
    BALL_SOCKET,  // 3 DOF: shoulder, hip (flex + abduct + twist)
    HINGE,        // 1 DOF: elbow, knee (flex only)
    PIVOT,        // 1 DOF: forearm, ankle twist (twist only)
    FIXED         // 0 DOF: rigid attachment
};

// ============================================================================
// AXIS LIMIT - Per-axis range of motion
// ============================================================================
// Anatomical limits in radians. FK and physics both enforce these.
//
// Examples (real human values):
//   Elbow flexion: 0 to +150 degrees (cannot hyperextend much)
//   Shoulder abduction: -10 to +180 degrees
//   Hip flexion: -10 to +120 degrees

struct AxisLimit {
    float min_angle = -static_cast<float>(M_PI);  // Minimum rotation (radians)
    float max_angle = static_cast<float>(M_PI);   // Maximum rotation (radians)

    // Check if angle is within limits
    bool contains(float angle) const {
        return angle >= min_angle && angle <= max_angle;
    }

    // Clamp angle to limits
    float clamp(float angle) const {
        if (angle < min_angle) return min_angle;
        if (angle > max_angle) return max_angle;
        return angle;
    }
};

// ============================================================================
// JOINT DEFINITION - Anatomical axes and limits
// ============================================================================
// Defines the "meaning" of flex/abduct/twist for a specific joint type.
//
// Axes are in JOINT-LOCAL REST FRAME (before any parent rotation).
// When animation says "flex", FK rotates around flexion_axis.
//
// Example: Right elbow (hinge joint)
//   - flexion_axis = (1, 0, 0) - X-axis in joint-local frame
//   - When forearm "flexes", it rotates around local X
//   - Result: forearm always bends toward body, regardless of shoulder pose
//
// Human anatomy conventions:
//   - Flexion: decreasing angle between bones (bending)
//   - Extension: increasing angle (straightening, sometimes hyperextension)
//   - Abduction: moving away from body midline
//   - Adduction: moving toward body midline
//   - Twist (rotation): rotation along bone's long axis

struct JointDefinition {
    JointType type = JointType::FIXED;

    // Anatomical axes in joint's REST local frame
    // These define what "flex", "abduct", "twist" mean for this joint
    Vec3 flexion_axis = {1.0f, 0.0f, 0.0f};    // Axis for flex/extend
    Vec3 abduction_axis = {0.0f, 1.0f, 0.0f};  // Axis for abduct/adduct
    Vec3 twist_axis = {0.0f, 0.0f, 1.0f};      // Axis for internal/external rotation

    // Per-axis limits (radians)
    AxisLimit flex_limit;
    AxisLimit abduct_limit;
    AxisLimit twist_limit;

    // Human-readable name for debugging
    const char* name = "unnamed";

    // ========================================================================
    // QUERY METHODS
    // ========================================================================

    // Check if this joint type supports the given semantic axis
    bool supports_flex() const {
        return type == JointType::BALL_SOCKET || type == JointType::HINGE;
    }

    bool supports_abduct() const {
        return type == JointType::BALL_SOCKET;
    }

    bool supports_twist() const {
        return type == JointType::BALL_SOCKET || type == JointType::PIVOT;
    }

    // Get the rotation axis for a semantic channel
    // Returns zero vector if channel not supported
    Vec3 get_axis_for_flex() const {
        return supports_flex() ? flexion_axis : Vec3{0, 0, 0};
    }

    Vec3 get_axis_for_abduct() const {
        return supports_abduct() ? abduction_axis : Vec3{0, 0, 0};
    }

    Vec3 get_axis_for_twist() const {
        return supports_twist() ? twist_axis : Vec3{0, 0, 0};
    }

    // Clamp a semantic angle to joint limits
    float clamp_flex(float angle) const { return flex_limit.clamp(angle); }
    float clamp_abduct(float angle) const { return abduct_limit.clamp(angle); }
    float clamp_twist(float angle) const { return twist_limit.clamp(angle); }
};

// ============================================================================
// PREDEFINED JOINT TEMPLATES
// ============================================================================
// Shared definitions for common joint types. Multiple joints can reference
// the same template (e.g., left and right elbows use same ELBOW definition).
//
// Values based on human anatomy:
//   - Elbow: hinge, flex 0-150 degrees
//   - Shoulder: ball-socket, wide range of motion
//   - Knee: hinge, flex 0-130 degrees
//   - Hip: ball-socket, more limited than shoulder

// Declared here, defined in joint_types.cpp
extern const JointDefinition ELBOW_RIGHT;
extern const JointDefinition ELBOW_LEFT;
extern const JointDefinition SHOULDER_RIGHT;
extern const JointDefinition SHOULDER_LEFT;
extern const JointDefinition WRIST_RIGHT;
extern const JointDefinition WRIST_LEFT;

extern const JointDefinition KNEE_RIGHT;
extern const JointDefinition KNEE_LEFT;
extern const JointDefinition HIP_RIGHT;
extern const JointDefinition HIP_LEFT;
extern const JointDefinition ANKLE_RIGHT;
extern const JointDefinition ANKLE_LEFT;
extern const JointDefinition TOE_RIGHT;
extern const JointDefinition TOE_LEFT;

// Spine joints (hips → abdomen → chest → neck → head)
extern const JointDefinition LOWER_SPINE;   // hips → abdomen
extern const JointDefinition UPPER_SPINE;   // abdomen → chest
extern const JointDefinition NECK_JOINT;    // chest → neck
extern const JointDefinition HEAD_JOINT;    // neck → head

// Utility joint
extern const JointDefinition FIXED_JOINT;   // 0-DOF bridge (e.g., chest → shoulder)

// Utility: Get joint definition by name
// Returns nullptr if not found
const JointDefinition* get_joint_definition_by_name(const char* joint_name);

} // namespace logosphere

#endif // LOGOSPHERE_JOINT_TYPES_H
