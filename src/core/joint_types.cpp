// Joint Types - Predefined Joint Templates
//
// Anatomical joint definitions based on human biomechanics.
// Values derived from kinesiology literature and simplified for game use.
//
// References:
//   - Human joint ranges: Kapandji, "Physiology of the Joints"
//   - Simplified for gameplay (not medical accuracy)

#include "joint_types.h"
#include <cstring>  // For strcmp

namespace logosphere {

// ============================================================================
// ARM JOINTS
// ============================================================================

// RIGHT ELBOW - Hinge joint, single-axis flexion
// At rest: forearm hangs down
// Flex: forearm moves toward upper arm (bending arm)
// Axis: X (perpendicular to bone, allows forward/back bend)
//
// Human elbow: 0 to ~150 degrees flexion, ~10 degrees hyperextension
const JointDefinition ELBOW_RIGHT = {
    .type = JointType::HINGE,
    .flexion_axis = {1.0f, 0.0f, 0.0f},   // X-axis: flex/extend
    .abduction_axis = {0.0f, 0.0f, 0.0f}, // Not used (hinge)
    .twist_axis = {0.0f, 0.0f, 0.0f},     // Not used (hinge)
    .flex_limit = {-0.17f, 2.62f},        // -10 to +150 degrees
    .abduct_limit = {0.0f, 0.0f},         // N/A
    .twist_limit = {0.0f, 0.0f},          // N/A
    .name = "elbow_right"
};

// LEFT ELBOW - Mirror of right
// Flexion axis is -X to maintain symmetry
const JointDefinition ELBOW_LEFT = {
    .type = JointType::HINGE,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},  // -X-axis (mirrored)
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-2.62f, 0.17f},        // Mirrored range (negated angles with -X axis)
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "elbow_left"
};

// RIGHT SHOULDER - Ball-socket, 3 DOF
// Complex joint with wide range of motion
//
// Axes (in shoulder-local frame at rest, arm hanging along -Z):
//   - Flexion (X): arm forward/back in sagittal plane (lateral axis)
//   - Abduction (Y): arm raise laterally (anteroposterior axis)
//   - Twist (Z): internal/external rotation along arm's long axis
//
// Human shoulder ranges:
//   - Flexion: -60 to +180 degrees (back to overhead)
//   - Abduction: -30 to +180 degrees (side to overhead)
//   - Rotation: -90 to +90 degrees
const JointDefinition SHOULDER_RIGHT = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {1.0f, 0.0f, 0.0f},   // X-axis: forward/back (matches anatomy & defaults)
    .abduction_axis = {0.0f, 1.0f, 0.0f}, // Y-axis: lateral raise
    .twist_axis = {0.0f, 0.0f, 1.0f},     // Z-axis: rotation along arm length
    .flex_limit = {-1.05f, 3.14f},        // -60 to +180 degrees
    .abduct_limit = {-3.14f, 3.14f},      // Full range (negative = T-pose direction for right arm)
    .twist_limit = {-1.57f, 1.57f},       // -90 to +90 degrees
    .name = "shoulder_right"
};

// LEFT SHOULDER - Mirror of right
// Flexion and abduction axes mirrored, twist same (along bone axis)
const JointDefinition SHOULDER_LEFT = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},  // -X (mirrored)
    .abduction_axis = {0.0f, -1.0f, 0.0f}, // -Y (mirrored)
    .twist_axis = {0.0f, 0.0f, 1.0f},      // Z (same for both sides — along bone)
    .flex_limit = {-3.14f, 1.05f},         // Mirrored range (negated angles with -X axis)
    .abduct_limit = {-3.14f, 3.14f},       // Full range (symmetric, no change needed)
    .twist_limit = {-1.57f, 1.57f},
    .name = "shoulder_left"
};

// RIGHT WRIST - Simplified as hinge (real wrist is more complex)
// For game purposes, primarily flexion (bending hand up/down)
const JointDefinition WRIST_RIGHT = {
    .type = JointType::HINGE,
    .flexion_axis = {1.0f, 0.0f, 0.0f},   // X-axis: flex/extend
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-1.22f, 1.22f},        // -70 to +70 degrees
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "wrist_right"
};

const JointDefinition WRIST_LEFT = {
    .type = JointType::HINGE,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-1.22f, 1.22f},
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "wrist_left"
};

// ============================================================================
// LEG JOINTS
// ============================================================================

// RIGHT KNEE - Hinge joint
// At rest: shin hangs down
// Flex: shin moves back (bending knee)
// Human knee: 0 to ~130 degrees flexion, minimal hyperextension
const JointDefinition KNEE_RIGHT = {
    .type = JointType::HINGE,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},  // -X-axis: flex bends shin backward (posterior)
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-0.09f, 2.27f},        // -5 to +130 degrees
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "knee_right"
};

const JointDefinition KNEE_LEFT = {
    .type = JointType::HINGE,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-0.09f, 2.27f},
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "knee_left"
};

// RIGHT HIP - Ball-socket, 3 DOF
// More limited than shoulder due to pelvis structure
//
// Axes:
//   - Flexion (X): thigh forward/back (kicking forward)
//   - Abduction (Y): thigh outward (side kick)
//   - Twist (Z): leg rotation (turning foot in/out)
//
// Human hip ranges:
//   - Flexion: -10 to +120 degrees
//   - Abduction: -30 to +45 degrees
//   - Rotation: -45 to +45 degrees
const JointDefinition HIP_RIGHT = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {1.0f, 0.0f, 0.0f},   // X-axis: forward/back
    .abduction_axis = {0.0f, 1.0f, 0.0f}, // Y-axis: outward/inward
    .twist_axis = {0.0f, 0.0f, 1.0f},     // Z-axis: rotation
    .flex_limit = {-0.17f, 2.09f},        // -10 to +120 degrees
    .abduct_limit = {-0.52f, 0.79f},      // -30 to +45 degrees
    .twist_limit = {-0.79f, 0.79f},       // -45 to +45 degrees
    .name = "hip_right"
};

const JointDefinition HIP_LEFT = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},  // -X (mirrored)
    .abduction_axis = {0.0f, -1.0f, 0.0f}, // -Y (mirrored)
    .twist_axis = {0.0f, 0.0f, 1.0f},
    .flex_limit = {-2.09f, 0.17f},         // Mirrored range (negated angles with -X axis)
    .abduct_limit = {-0.52f, 0.79f},       // Same as right (abduct angles not negated for mirror)
    .twist_limit = {-0.79f, 0.79f},
    .name = "hip_left"
};

// RIGHT ANKLE - Simplified as hinge (dorsiflexion/plantarflexion)
// Real ankle also has some inversion/eversion, simplified here
const JointDefinition ANKLE_RIGHT = {
    .type = JointType::HINGE,
    .flexion_axis = {1.0f, 0.0f, 0.0f},   // X-axis: point toe up/down
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-0.70f, 0.52f},        // -40 to +30 degrees (point/flex)
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "ankle_right"
};

const JointDefinition ANKLE_LEFT = {
    .type = JointType::HINGE,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-0.52f, 0.70f},        // Mirrored range (negated angles with -X axis)
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "ankle_left"
};

// RIGHT TOE - Hinge joint at metatarsophalangeal (ball of foot)
// Plantarflexion (toe curl down) at push-off, dorsiflexion (toe up) during swing
const JointDefinition TOE_RIGHT = {
    .type = JointType::HINGE,
    .flexion_axis = {1.0f, 0.0f, 0.0f},   // X-axis: curl/lift
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-0.52f, 0.79f},        // -30 to +45 degrees (curl to lift)
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "toe_right"
};

const JointDefinition TOE_LEFT = {
    .type = JointType::HINGE,
    .flexion_axis = {-1.0f, 0.0f, 0.0f},  // -X (mirrored)
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {-0.79f, 0.52f},        // Mirrored range
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "toe_left"
};

// ============================================================================
// SPINE JOINTS
// ============================================================================
// Spine chain: hips → abdomen → chest → neck → head
// Each segment has progressively more mobility toward the head.
// Axes: flex=X (nod), abduct=Y (tilt), twist=Z (turn)

// LOWER SPINE - hips → abdomen
// Largest twist range — hip rotation drives torso torsion (punch power)
const JointDefinition LOWER_SPINE = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {1.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 1.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 1.0f},
    .flex_limit = {-0.349f, 0.349f},      // ±20° (π/9)
    .abduct_limit = {-0.175f, 0.175f},    // ±10° (π/18)
    .twist_limit = {-0.524f, 0.524f},     // ±30° (π/6)
    .name = "lower_spine"
};

// UPPER SPINE - abdomen → chest
// Thoracic region, slightly more twist than lower
const JointDefinition UPPER_SPINE = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {1.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 1.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 1.0f},
    .flex_limit = {-0.436f, 0.436f},      // ±25° (π/7.2)
    .abduct_limit = {-0.175f, 0.175f},    // ±10° (π/18)
    .twist_limit = {-0.611f, 0.611f},     // ±35° (π/5.1)
    .name = "upper_spine"
};

// NECK - chest → neck
// Most mobile segment — head tilt and nod
const JointDefinition NECK_JOINT = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {1.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 1.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 1.0f},
    .flex_limit = {-0.698f, 0.698f},      // ±40° (π/4.5)
    .abduct_limit = {-0.524f, 0.524f},    // ±30° (π/6)
    .twist_limit = {-0.873f, 0.873f},     // ±50° (5π/18)
    .name = "neck"
};

// HEAD - neck → head
// Fine adjustment (atlas/axis joint)
const JointDefinition HEAD_JOINT = {
    .type = JointType::BALL_SOCKET,
    .flexion_axis = {1.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 1.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 1.0f},
    .flex_limit = {-0.349f, 0.349f},      // ±20° (π/9)
    .abduct_limit = {-0.262f, 0.262f},    // ±15° (π/12)
    .twist_limit = {-0.175f, 0.175f},     // ±10° (π/18)
    .name = "Head"
};

// FIXED - 0-DOF rigid attachment (chest → shoulder bridge)
// Pure transform propagation, no rotation
const JointDefinition FIXED_JOINT = {
    .type = JointType::FIXED,
    .flexion_axis = {0.0f, 0.0f, 0.0f},
    .abduction_axis = {0.0f, 0.0f, 0.0f},
    .twist_axis = {0.0f, 0.0f, 0.0f},
    .flex_limit = {0.0f, 0.0f},
    .abduct_limit = {0.0f, 0.0f},
    .twist_limit = {0.0f, 0.0f},
    .name = "fixed"
};

// ============================================================================
// LOOKUP FUNCTION
// ============================================================================
// Maps joint names used in animation to their definitions.
// Animation uses names like "right_elbow", FK looks up the definition.

const JointDefinition* get_joint_definition_by_name(const char* joint_name) {
    if (!joint_name) return nullptr;

    // Arm joints
    if (std::strcmp(joint_name, "right_shoulder") == 0) return &SHOULDER_RIGHT;
    if (std::strcmp(joint_name, "left_shoulder") == 0) return &SHOULDER_LEFT;
    if (std::strcmp(joint_name, "right_elbow") == 0) return &ELBOW_RIGHT;
    if (std::strcmp(joint_name, "left_elbow") == 0) return &ELBOW_LEFT;
    if (std::strcmp(joint_name, "right_wrist") == 0) return &WRIST_RIGHT;
    if (std::strcmp(joint_name, "left_wrist") == 0) return &WRIST_LEFT;

    // Leg joints
    if (std::strcmp(joint_name, "right_hip") == 0) return &HIP_RIGHT;
    if (std::strcmp(joint_name, "left_hip") == 0) return &HIP_LEFT;
    if (std::strcmp(joint_name, "right_knee") == 0) return &KNEE_RIGHT;
    if (std::strcmp(joint_name, "left_knee") == 0) return &KNEE_LEFT;
    if (std::strcmp(joint_name, "right_ankle") == 0) return &ANKLE_RIGHT;
    if (std::strcmp(joint_name, "left_ankle") == 0) return &ANKLE_LEFT;
    if (std::strcmp(joint_name, "right_toe") == 0) return &TOE_RIGHT;
    if (std::strcmp(joint_name, "left_toe") == 0) return &TOE_LEFT;

    // Spine joints
    if (std::strcmp(joint_name, "lower_spine") == 0) return &LOWER_SPINE;
    if (std::strcmp(joint_name, "upper_spine") == 0) return &UPPER_SPINE;
    if (std::strcmp(joint_name, "neck") == 0) return &NECK_JOINT;
    if (std::strcmp(joint_name, "Head") == 0) return &HEAD_JOINT;
    if (std::strcmp(joint_name, "fixed") == 0) return &FIXED_JOINT;

    return nullptr;
}

} // namespace logosphere
