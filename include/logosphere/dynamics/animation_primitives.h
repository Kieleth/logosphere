// Animation Primitives - Simple, chainable animation building blocks
//
// Philosophy: Build complex animations from simple, verified primitives.
// Each primitive moves one joint through one motion.
//
// Anatomical terms used:
//   - Abduction: moving limb AWAY from body midline (lateral raise)
//   - Adduction: moving limb TOWARD body midline
//   - Flexion: decreasing angle between bones (bending)
//   - Extension: increasing angle between bones (straightening)

#ifndef LOGOSPHERE_ANIMATION_PRIMITIVES_H
#define LOGOSPHERE_ANIMATION_PRIMITIVES_H

#include "logosphere/dynamics/animation_types.h"
#include <cmath>
#include <string>

// ============================================================================
// BODY MAP — Decouples animation generators from joint naming conventions
// ============================================================================
// Same generator works for any humanoid-shaped creature regardless of which
// side is active. Maps joint "roles" (hip, knee, shoulder...) to name strings.

enum class Side { RIGHT, LEFT };

inline Side opposite(Side s) { return s == Side::RIGHT ? Side::LEFT : Side::RIGHT; }

struct BodyMap {
    std::string hip, knee, ankle, toe;
    std::string shoulder, elbow, wrist;
    // Axis mirror sign: left-side joint definitions have inverted flex/abduct
    // axes (e.g. shoulder_left flex_axis = -X vs shoulder_right = +X).
    // Multiply flex/abduct angles by this to get the same world-space motion.
    // Exception: knees use the same axis on both sides — do NOT apply to knee flex.
    float flex_sign;
};

inline const BodyMap& body(Side s) {
    static const BodyMap R = {"right_hip", "right_knee", "right_ankle", "right_toe",
                              "right_shoulder", "right_elbow", "right_wrist", 1.0f};
    static const BodyMap L = {"left_hip", "left_knee", "left_ankle", "left_toe",
                              "left_shoulder", "left_elbow", "left_wrist", -1.0f};
    return s == Side::RIGHT ? R : L;
}

// ============================================================================
// SHOULDER ABDUCTION (Lateral Arm Raise)
// ============================================================================
// Motion: Raise arm laterally away from body, like bird wings or T-pose
// Result: Upper arm horizontal, forearm hangs down (or parallel if elbow bent)
//
// At rest:
//   - Upper arm hangs at side (Z ≈ shoulder height - 0.2m)
//   - Forearm hangs below upper arm
//   - Hand hangs below forearm
//
// At raised position:
//   - Upper arm horizontal (Z ≈ shoulder height)
//   - Upper arm extended laterally (+X)
//   - Forearm and hand follow the chain
//
inline AnimationClip create_shoulder_abduction_right(
    unsigned int upper_arm_id,
    unsigned int forearm_id,
    unsigned int hand_id)
{
    AnimationClip clip;
    clip.name = "shoulder_abduction_right";
    clip.loops = false;

    // Keyframe 0: Rest (0ms) - arm hanging at side
    {
        AnimationPose pose;
        pose.add_target(upper_arm_id, 0.0f, 0.0f, 0.0f);
        pose.add_target(forearm_id, 0.0f, 0.0f, 0.0f);
        pose.add_target(hand_id, 0.0f, 0.0f, 0.0f);
        clip.add_keyframe(0.0f, pose);
    }

    // Keyframe 1: Raised (300ms) - T-POSE (entire arm horizontal)
    //
    // From DIAG at rest (LOCAL coordinates relative to hips):
    //   Shoulder LOCAL_Z = 0.638 (this is our target height)
    //   Upper_arm LOCAL_Z = 0.513 (needs +0.125 to reach shoulder)
    //   Forearm LOCAL_Z = 0.221 (needs +0.42 to reach shoulder)
    //   Hand LOCAL_Z = 0.024 (needs +0.62 to reach shoulder)
    //
    // For proper T-pose: ALL arm segments at SAME Z (shoulder height)
    // X offsets: realistic arm ROTATION, not rubber-band stretching
    {
        AnimationPose pose;

        // Upper arm: ROTATE up to shoulder height
        // Must rise 0.125 to reach shoulder Z, modest lateral extension
        float ua_x = 0.12f;   // arm rotates outward slightly
        float ua_y = 0.0f;
        float ua_z = 0.13f;   // rise to shoulder height!

        // Forearm: continue arc, rise to shoulder height
        // Must rise 0.42 to reach shoulder Z
        float fa_x = 0.28f;   // continues outward (rotation arc)
        float fa_y = 0.0f;
        float fa_z = 0.42f;   // rise to shoulder height

        // Hand: complete arc, rise to shoulder height
        // Must rise 0.62 to reach shoulder Z
        float h_x = 0.40f;    // full extension (realistic arm reach)
        float h_y = 0.0f;
        float h_z = 0.62f;    // rise to shoulder height

        pose.add_target(upper_arm_id, ua_x, ua_y, ua_z);
        pose.add_target(forearm_id, fa_x, fa_y, fa_z);
        pose.add_target(hand_id, h_x, h_y, h_z);
        clip.add_keyframe(300.0f, pose);
    }

    // Keyframe 2: Return to rest (600ms)
    {
        AnimationPose pose;
        pose.add_target(upper_arm_id, 0.0f, 0.0f, 0.0f);
        pose.add_target(forearm_id, 0.0f, 0.0f, 0.0f);
        pose.add_target(hand_id, 0.0f, 0.0f, 0.0f);
        clip.add_keyframe(600.0f, pose);
    }

    return clip;
}

// ============================================================================
// FK-BASED SHOULDER ABDUCTION (Rotation targets, not position offsets)
// ============================================================================
// Uses joint angles instead of particle positions. FK computes positions.
// Segment distances stay constant - no stretching.
//
// Shoulder abduction: rotate upper arm upward around shoulder joint.
// At rest: arm hangs down (-Z), angle = -π/2 from horizontal
// At raised: arm horizontal (+X), angle = 0

inline FKAnimationClip create_fk_shoulder_abduction_right()
{
    FKAnimationClip clip;
    clip.name = "fk_shoulder_abduction_right";
    clip.loops = false;

    // Shoulder abduction: upper arm raises to horizontal
    // Elbow and wrist RELAX (hang down due to gravity)
    //
    // At rest: entire arm hangs down
    // At raised: upper arm horizontal, forearm/hand hang down from elbow/wrist

    // Keyframe 0: Rest (0ms) - arm hanging at side
    {
        RotationPose pose;
        pose.drive("right_shoulder", 0.0f);
        pose.relax("right_elbow");   // Hang down
        pose.relax("right_wrist");   // Hang down
        clip.add_keyframe(0.0f, pose);
    }

    // Keyframe 1: Raised (300ms) - T-pose with hanging forearm
    // Shoulder rotates -π/2 (arm horizontal)
    // Elbow/wrist stay RELAXED (hanging down in world space)
    {
        RotationPose pose;
        pose.drive("right_shoulder", static_cast<float>(-M_PI / 2.0));  // Arm horizontal
        pose.relax("right_elbow");   // Hang down (compensates for shoulder rotation)
        pose.relax("right_wrist");   // Hang down
        clip.add_keyframe(300.0f, pose);
    }

    // Keyframe 2: Return to rest (600ms)
    {
        RotationPose pose;
        pose.drive("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(600.0f, pose);
    }

    return clip;
}

// ============================================================================
// FK-BASED HIP FLEXION (Knee Raise)
// ============================================================================
// Motion: Raise knee forward (like marching or kicking prep)
// Uses X-axis rotation (pitch) to rotate thigh forward.
// Shin and foot follow via FK chain propagation (relaxed, hanging).
//
// At rest: leg hangs straight down, angle = 0
// At raised: thigh horizontal forward, knee at hip height

inline FKAnimationClip create_fk_hip_flexion_right()
{
    FKAnimationClip clip;
    clip.name = "fk_hip_flexion_right";
    clip.loops = false;

    // Hip flexion: thigh raises to horizontal (like knee raise)
    // Knee and ankle RELAX (hang down due to gravity)
    //
    // At rest: leg straight down
    // At flexed: thigh horizontal, shin/foot hang down from knee/ankle

    // Keyframe 0: Rest (0ms) - leg straight down
    {
        RotationPose pose;
        pose.flex("right_hip", 0.0f);
        pose.relax("right_knee");    // Hang down
        pose.relax("right_ankle");   // Hang down
        clip.add_keyframe(0.0f, pose);
    }

    // Keyframe 1: Knee raised (300ms)
    // Hip rotates +π/2 (thigh horizontal)
    // Knee/ankle stay RELAXED (shin/foot hang down)
    {
        RotationPose pose;
        pose.flex("right_hip", static_cast<float>(M_PI / 2.0));  // Thigh horizontal
        pose.relax("right_knee");    // Shin hangs down
        pose.relax("right_ankle");   // Foot hangs down
        clip.add_keyframe(300.0f, pose);
    }

    // Keyframe 2: Return to rest (600ms)
    {
        RotationPose pose;
        pose.flex("right_hip", 0.0f);
        pose.relax("right_knee");
        pose.relax("right_ankle");
        clip.add_keyframe(600.0f, pose);
    }

    return clip;
}

// ============================================================================
// FK-BASED ELBOW FLEXION (Bend Elbow Backward)
// ============================================================================
// Motion: Bend elbow to bring forearm toward back (behind upper arm)
// Prerequisite: Works best AFTER shoulder_abduction (arm in T-pose)
//
// When arm is horizontal (T-pose):
//   - Forearm hangs down at rest (relaxed)
//   - Elbow flexion rotates forearm around X-axis
//   - Positive angle = forearm moves forward (toward front of body)
//
// At rest: forearm hangs down, angle = 0
// At flexed: forearm horizontal forward, angle = +π/2

inline FKAnimationClip create_fk_elbow_flexion_right()
{
    FKAnimationClip clip;
    clip.name = "fk_elbow_flexion_right";
    clip.loops = false;

    // Elbow flexion: forearm bends forward
    // Shoulder stays at rest, wrist relaxes (hand hangs from forearm)

    // Keyframe 0: Rest (0ms) - forearm hanging/relaxed
    {
        RotationPose pose;
        pose.drive("right_shoulder", 0.0f);    // Shoulder at rest
        pose.relax("right_elbow");             // Forearm hangs
        pose.relax("right_wrist");             // Hand hangs
        clip.add_keyframe(0.0f, pose);
    }

    // Keyframe 1: Flexed (300ms) - forearm bent forward
    {
        RotationPose pose;
        pose.drive("right_shoulder", 0.0f);    // Shoulder at rest
        pose.drive("right_elbow", static_cast<float>(M_PI / 2.0));   // Bend forward
        pose.relax("right_wrist");             // Hand hangs from bent forearm
        clip.add_keyframe(300.0f, pose);
    }

    // Keyframe 2: Return to rest (600ms)
    {
        RotationPose pose;
        pose.drive("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(600.0f, pose);
    }

    return clip;
}

// ============================================================================
// CHAINED: SHOULDER ABDUCTION + ELBOW FLEXION
// ============================================================================
// Demonstrates primitive chaining - foundation for complex animations.
//
// Sequence:
//   0-300ms:   Shoulder abducts (arm rises to T-pose)
//   300-600ms: Elbow flexes (forearm bends forward)
//   600-900ms: Elbow returns to rest
//   900-1200ms: Shoulder returns to rest
//
// CRITICAL: Uses DRIVEN mode throughout to avoid type transitions.
// Type transitions (PHYSICS↔DRIVEN, DIRECTION↔DRIVEN) cause position
// discontinuities because each mode computes geometry differently.

inline FKAnimationClip create_fk_shoulder_then_elbow_right()
{
    FKAnimationClip clip;
    clip.name = "fk_shoulder_then_elbow_right";
    clip.loops = false;

    // Chained animation: shoulder abduction → elbow flex → return
    //
    // ALL joints use DRIVEN mode with explicit angles.
    // Elbow angle=0 means "extended" (forearm continues arm line).
    // The visual effect may differ from "hanging" but transitions are smooth.

    // Phase 1: Arm at rest (0ms)
    // Shoulder at rest, elbow extended (angle=0)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_elbow", 0.0f);       // Extended
        pose.flex("right_wrist", 0.0f);       // Extended
        clip.add_keyframe(0.0f, pose);
    }

    // Phase 2: Shoulder abducted to T-pose, elbow still extended (300ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));  // T-pose
        pose.flex("right_elbow", 0.0f);       // Extended (arm straight)
        pose.flex("right_wrist", 0.0f);       // Extended
        clip.add_keyframe(300.0f, pose);
    }

    // Phase 3: Shoulder held, elbow flexed forward (600ms)
    // Semantic flex() maps to X-axis via JointDefinition, avoiding Y-cancellation
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));  // Hold T-pose
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));        // Bend forward 90°
        pose.flex("right_wrist", 0.0f);       // Extended from forearm
        clip.add_keyframe(600.0f, pose);
    }

    // Phase 4: Shoulder held, elbow returns to extended (900ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));  // Hold T-pose
        pose.flex("right_elbow", 0.0f);       // Back to extended
        pose.flex("right_wrist", 0.0f);
        clip.add_keyframe(900.0f, pose);
    }

    // Phase 5: Return to rest (1200ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_elbow", 0.0f);
        pose.flex("right_wrist", 0.0f);
        clip.add_keyframe(1200.0f, pose);
    }

    return clip;
}

// ============================================================================
// SHOULDER HORIZONTAL EXTENSION (Pull Upper Arm Back)
// ============================================================================
// Motion: Rotate upper arm backward around body's vertical (Z) axis
// Used for: Wind-up phase of cross punch
//
// With arm in T-pose (shoulder abducted):
//   - Upper arm points to the side (horizontal)
//   - Z-axis rotation swings arm in horizontal plane
//   - Negative angle = arm moves backward (toward back)
//
// At T-pose: arm to side, shoulder Z angle = 0
// At retracted: arm back, shoulder Z angle = -π/2 (or less)

inline FKAnimationClip create_fk_shoulder_horizontal_extension_right()
{
    FKAnimationClip clip;
    clip.name = "fk_shoulder_horizontal_extension_right";
    clip.loops = false;

    // Shoulder horizontal extension: pull arm backward (Z-axis rotation)
    // Elbow and wrist relax (hang down)

    // Keyframe 0: Rest (0ms)
    {
        RotationPose pose;
        pose.drive("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(0.0f, pose);
    }

    // Keyframe 1: Retracted backward (300ms)
    {
        RotationPose pose;
        pose.drive("right_shoulder", static_cast<float>(-M_PI / 3.0));  // ~60° back
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(300.0f, pose);
    }

    // Keyframe 2: Return (600ms)
    {
        RotationPose pose;
        pose.drive("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(600.0f, pose);
    }

    return clip;
}

// ============================================================================
// CROSS PUNCH WINDUP: T-pose → Elbow flex (hold) → Pull arm back
// ============================================================================
// Full windup sequence for a cross punch:
//   0-300ms:   Shoulder abducts (T-pose)
//   300-600ms: Elbow flexes (forearm horizontal, HOLD)
//   600-900ms: Upper arm rotates backward (Z-axis, windup) - STAYS HERE
//
// The elbow stays flexed while the upper arm pulls back - classic boxing windup.
// Animation ENDS at windup position (does NOT return to rest) for chaining with strike.

inline FKAnimationClip create_fk_cross_punch_windup_right()
{
    FKAnimationClip clip;
    clip.name = "fk_cross_punch_windup_right";
    clip.loops = false;

    // Cross punch windup: T-pose → elbow flex → pull back
    //
    // Phase 1: Rest - arm hangs, elbow/wrist relaxed
    // Phase 2: T-pose - shoulder drives, elbow/wrist relax (hang down)
    // Phase 3: Windup - shoulder_z drives (pull back), elbow drives (bent), wrist relaxes

    // Phase 1: Arm at rest (0ms)
    // Semantic API: abduct for lateral raise, flex for forward/back
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);  // Lateral raise
        pose.flex("right_shoulder", 0.0f);    // Forward/back rotation
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(0.0f, pose);
    }

    // Phase 2: Shoulder abducted to T-pose (300ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));  // T-pose
        pose.flex("right_shoulder", 0.0f);    // No forward/back yet
        pose.relax("right_elbow");   // Forearm hangs down
        pose.relax("right_wrist");   // Hand hangs down
        clip.add_keyframe(300.0f, pose);
    }

    // Phase 3: Elbow flexed + shoulder pulls arm back (600ms)
    // KEY FIX: flex("right_elbow") maps to X-axis, not Y-axis
    // This avoids cancellation with shoulder's Y-axis abduction
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));  // Hold T-pose
        pose.flex("right_shoulder", static_cast<float>(M_PI / 4.0));     // Pull back 45° (positive = back with new rotation order)
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));        // Forearm bent 90°
        pose.relax("right_wrist");   // Hand hangs from bent forearm
        clip.add_keyframe(600.0f, pose);
    }

    clip.body_region = BodyRegion::UPPER_BODY;
    clip.blend_in_ms = 80.0f;
    clip.blend_out_ms = 100.0f;
    return clip;
}

// ============================================================================
// CROSS PUNCH STRIKE: From windup → punch forward → return
// ============================================================================
// Continues from windup position (does NOT relax first).
//
// Mechanics:
//   - Starts at windup: arm in T-pose, elbow bent, shoulder_z pulled back
//   - Strike:
//     1. Shoulder_z swings forward (inverse of windup rotation)
//     2. Shoulder Y comes down from T-pose (arm drops from horizontal)
//     3. Elbow extends fully (forearm straightens forward)
//   - Result: fist punches straight forward
//   - Return to rest
//
// Timing:
//   0-300ms:     Windup position (hold) - arm cocked back
//   300-450ms:   STRIKE - explosive forward punch (FAST)
//   450-600ms:   Hold extended position
//   600-900ms:   Return to rest

inline FKAnimationClip create_fk_cross_punch_strike_right()
{
    FKAnimationClip clip;
    clip.name = "fk_cross_punch_strike_right";
    clip.loops = false;

    // Cross punch strike: from windup → punch forward → return
    //
    // Wrist is RIGID during strike (fist aligned with forearm for impact)
    // Only relaxes when returning to rest

    // Phase 1: Start at WINDUP position (0ms) - must match end of windup animation!
    // Windup ends with: shoulder abduct=-π/2 (T-pose), shoulder flex=-π/4 (pullback), elbow flex=π/2 (bent)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));    // T-pose
        pose.flex("right_shoulder", static_cast<float>(M_PI / 4.0));       // Pullback (positive = back with new rotation order)
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));          // Elbow bent
        pose.rigid("right_wrist");   // Fist aligned with forearm
        clip.add_keyframe(0.0f, pose);
    }

    // Phase 2: STRIKE (300ms) - swing forward, extend elbow
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 4.0));    // Bring arm down from T-pose
        pose.flex("right_shoulder", static_cast<float>(-M_PI / 4.0));      // Swing forward (negative = forward with new rotation order)
        pose.flex("right_elbow", 0.0f);                                     // Extend elbow
        pose.rigid("right_wrist");   // Fist punches forward
        clip.add_keyframe(300.0f, pose);
    }

    // Phase 3: Hold extended (450ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 4.0));
        pose.flex("right_shoulder", static_cast<float>(-M_PI / 4.0));
        pose.flex("right_elbow", 0.0f);
        pose.rigid("right_wrist");
        clip.add_keyframe(450.0f, pose);
    }

    // Phase 4: Return arm up (600ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);   // Arm back to side
        pose.flex("right_shoulder", 0.0f);      // No rotation
        pose.relax("right_elbow");              // Arm hangs again
        pose.relax("right_wrist");              // Hand hangs
        clip.add_keyframe(600.0f, pose);
    }

    // Phase 5: Full rest (900ms) - hold at rest
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(900.0f, pose);
    }

    clip.body_region = BodyRegion::UPPER_BODY;
    clip.blend_in_ms = 80.0f;
    clip.blend_out_ms = 100.0f;
    return clip;
}

// ============================================================================
// CROSS PUNCH FULL: Windup + Strike in one continuous clip
// ============================================================================
// Combines windup (TC3) and strike (TC4) into a single clip.
//
// Timing:
//   0-300ms:     Rest → T-pose (shoulder abducts)
//   300-600ms:   T-pose → Windup (arm pulls back, elbow cocks)
//   600-900ms:   Windup → Strike peak (swing forward, extend elbow)
//   900-1200ms:  Hold extended

inline FKAnimationClip create_fk_cross_punch_full_right()
{
    FKAnimationClip clip;
    clip.name = "fk_cross_punch_full_right";
    clip.loops = false;

    // Flex sign convention with south-facing parent (rot_z=π):
    //   Parent flips local Y → world -Y, so:
    //   +flex (local +Y) → world -Y → FORWARD for south-facing
    //   -flex (local -Y) → world +Y → BEHIND for south-facing

    // 0ms: Rest — arm hanging
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(0.0f, pose);
    }

    // 300ms: T-pose — arm horizontal, elbow extended
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", 0.0f);
        pose.flex("right_elbow", 0.0f);
        pose.flex("right_wrist", 0.0f);
        clip.add_keyframe(300.0f, pose);
    }

    // 600ms: Windup — arm pulls BACK, elbow cocked
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", static_cast<float>(-M_PI / 4.0));  // -π/4 = behind (south-facing)
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));     // Elbow cocked
        pose.rigid("right_wrist");
        clip.add_keyframe(600.0f, pose);
    }

    // 900ms: Strike — full forward extension at shoulder height
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", static_cast<float>(M_PI / 2.0));  // +π/2 = forward (south-facing)
        pose.flex("right_elbow", 0.0f);   // Arm straight
        pose.rigid("right_wrist");
        clip.add_keyframe(900.0f, pose);
    }

    // 1200ms: Hold at max extension
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", static_cast<float>(M_PI / 2.0));  // Hold forward
        pose.flex("right_elbow", 0.0f);
        pose.rigid("right_wrist");
        clip.add_keyframe(1200.0f, pose);
    }

    clip.body_region = BodyRegion::UPPER_BODY;
    clip.blend_in_ms = 80.0f;
    clip.blend_out_ms = 100.0f;
    return clip;
}

// ============================================================================
// CROSS PUNCH FULL WITH RECOVERY: Windup + Strike + Return to Rest
// ============================================================================
// Same poses as create_fk_cross_punch_full_right() but with realistic timing:
//   0-400ms:     Rest → T-pose          (400ms — deliberate setup)
//   400-700ms:   T-pose → Windup        (300ms — preparation)
//   700-800ms:   Windup → Strike        (100ms — explosive, 3x faster)
//   800-900ms:   Hold at max extension  (100ms — brief)
//   900-1200ms:  Recovery to rest       (300ms — controlled return)
//   1200-1500ms: Hold at rest           (300ms)

inline FKAnimationClip create_fk_cross_punch_full_recovery_right()
{
    FKAnimationClip clip;
    clip.name = "fk_cross_punch_full_recovery_right";
    clip.loops = false;

    // 0ms: Rest — arm hanging
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(0.0f, pose);
    }

    // 400ms: T-pose — arm horizontal, elbow extended (deliberate)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", 0.0f);
        pose.flex("right_elbow", 0.0f);
        pose.flex("right_wrist", 0.0f);
        clip.add_keyframe(400.0f, pose);
    }

    // 700ms: Windup — arm pulls BACK, elbow cocked
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", static_cast<float>(-M_PI / 4.0));
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));
        pose.rigid("right_wrist");
        clip.add_keyframe(700.0f, pose);
    }

    // 800ms: Strike — full forward extension (100ms, explosive)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", static_cast<float>(M_PI / 2.0));
        pose.flex("right_elbow", 0.0f);
        pose.rigid("right_wrist");
        clip.add_keyframe(800.0f, pose);
    }

    // 900ms: Hold at max extension (brief)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.0));
        pose.flex("right_shoulder", static_cast<float>(M_PI / 2.0));
        pose.flex("right_elbow", 0.0f);
        pose.rigid("right_wrist");
        clip.add_keyframe(900.0f, pose);
    }

    // 1200ms: Recovery — arm returns to rest
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(1200.0f, pose);
    }

    // 1500ms: Hold at rest
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_shoulder", 0.0f);
        pose.relax("right_elbow");
        pose.relax("right_wrist");
        clip.add_keyframe(1500.0f, pose);
    }

    clip.body_region = BodyRegion::UPPER_BODY;
    clip.blend_in_ms = 80.0f;
    clip.blend_out_ms = 100.0f;
    return clip;
}

// ============================================================================
// SEMANTIC API DEMO: T-pose with elbow flex
// ============================================================================
// Demonstrates the new semantic animation API where motions are anatomically
// defined regardless of parent joint orientation.
//
// Uses flex/abduct/twist instead of drive_x/y/z:
//   - abduct("right_shoulder", angle) always raises arm laterally
//   - flex("right_elbow", angle) always bends forearm toward body
//
// The FK system maps these semantic commands to correct local axes via
// JointDefinition, solving the "axis meaning changes with parent rotation" bug.

inline FKAnimationClip create_semantic_tpose_elbow_flex_right()
{
    FKAnimationClip clip;
    clip.name = "semantic_tpose_elbow_flex_right";
    clip.loops = false;

    // Phase 1: Rest (0ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);  // Arm hanging
        pose.flex("right_elbow", 0.0f);       // Forearm hanging
        pose.flex("right_wrist", 0.0f);       // Hand hanging
        clip.add_keyframe(0.0f, pose);
    }

    // Phase 2: T-pose (300ms)
    // Shoulder abducts 90 degrees, arm goes horizontal
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(M_PI / 2.0));  // T-pose
        pose.flex("right_elbow", 0.0f);       // Forearm extended
        pose.flex("right_wrist", 0.0f);       // Hand extended
        clip.add_keyframe(300.0f, pose);
    }

    // Phase 3: Elbow flexed while in T-pose (600ms)
    // This is where the semantic API shines:
    // flex("elbow") always bends forearm toward body, even with arm horizontal
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(M_PI / 2.0));  // Hold T-pose
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));       // Bend 90 degrees
        pose.flex("right_wrist", 0.0f);
        clip.add_keyframe(600.0f, pose);
    }

    // Phase 4: Return to rest (900ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", 0.0f);
        pose.flex("right_elbow", 0.0f);
        pose.flex("right_wrist", 0.0f);
        clip.add_keyframe(900.0f, pose);
    }

    return clip;
}

// ============================================================================
// SEMANTIC API DEMO: Hip flexion with knee bend
// ============================================================================
// Demonstrates semantic leg animation:
//   - flex("right_hip", angle) brings thigh forward
//   - flex("right_knee", angle) bends shin back

inline FKAnimationClip create_semantic_kick_prep_right()
{
    FKAnimationClip clip;
    clip.name = "semantic_kick_prep_right";
    clip.loops = false;

    // Phase 1: Standing (0ms)
    {
        RotationPose pose;
        pose.flex("right_hip", 0.0f);
        pose.flex("right_knee", 0.0f);
        pose.flex("right_ankle", 0.0f);
        clip.add_keyframe(0.0f, pose);
    }

    // Phase 2: Knee raise (300ms) - thigh forward, shin hangs
    {
        RotationPose pose;
        pose.flex("right_hip", static_cast<float>(M_PI / 3.0));  // 60 degrees forward
        pose.flex("right_knee", static_cast<float>(M_PI / 2.0)); // Shin bent back 90 degrees
        pose.flex("right_ankle", 0.0f);
        clip.add_keyframe(300.0f, pose);
    }

    // Phase 3: Kick extension (450ms) - knee snaps forward
    {
        RotationPose pose;
        pose.flex("right_hip", static_cast<float>(M_PI / 3.0));  // Hold hip
        pose.flex("right_knee", 0.0f);  // Extend knee
        pose.flex("right_ankle", -0.3f); // Point toe
        clip.add_keyframe(450.0f, pose);
    }

    // Phase 4: Return (700ms)
    {
        RotationPose pose;
        pose.flex("right_hip", 0.0f);
        pose.flex("right_knee", 0.0f);
        pose.flex("right_ankle", 0.0f);
        clip.add_keyframe(700.0f, pose);
    }

    return clip;
}

// ============================================================================
// GUARD POSE (Boxing Stance)
// ============================================================================
// Trained fighter's default ready position. Hands up, elbows in, fists at chin.
// Single keyframe — use as start/end frame for fighting animations.
//
// This replaces "arms at sides" as the neutral pose for trained fighters.
// Novice punch starts/ends at rest (arms hanging). Trained punch starts/ends
// here (hands up, protecting head).

inline FKAnimationClip create_fk_guard_pose_right()
{
    FKAnimationClip clip;
    clip.name = "fk_guard_pose_right";
    clip.loops = false;

    // Guard position: hand up near chin, elbow close to body
    // - Slight lateral lift (abduct -π/6 ≈ -30°)
    // - Hands forward (flex +π/6 ≈ 30°)
    // - Forearm folded up (elbow flex π/2 = 90°)
    // - Fist formed (wrist rigid)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 6.0));  // Slight lateral lift
        pose.flex("right_shoulder", static_cast<float>(M_PI / 6.0));     // Hands forward
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));        // Forearm folded up
        pose.rigid("right_wrist");                                         // Fist formed
        clip.add_keyframe(0.0f, pose);
    }

    clip.body_region = BodyRegion::UPPER_BODY;
    clip.blend_in_ms = 100.0f;
    clip.blend_out_ms = 80.0f;
    return clip;
}

// ============================================================================
// SWORD SWING (Windup -> Strike -> Hold -> Recovery, right-hand diagonal)
// ============================================================================
// A single committed sword swing: windup raises the blade back and up with
// the spine winding away from the target, strike snaps the shoulder
// forward with the elbow extending (the blade's reach) and the spine
// unwinding into the target, then recovery settles back to the guard pose
// (create_fk_guard_pose_right's exact angles, so looping swings don't pop).
// skill (0..1) scales windup/strike timing -- low skill telegraphs a
// slower swing, high skill is tighter and faster. This is a single fixed
// weapon technique, not a tunable unarmed strike, so it doesn't need cross
// punch's full interpolated-biomechanics-profile machinery -- just the
// same windup/strike/recovery keyframe shape.
inline FKAnimationClip create_fk_sword_swing_right(float skill = 0.5f) {
    if (skill < 0.0f) skill = 0.0f;
    if (skill > 1.0f) skill = 1.0f;

    FKAnimationClip clip;
    clip.name = "fk_sword_swing_right";
    clip.loops = false;

    const float windup_ms  = 260.0f - 80.0f * skill;  // 260ms novice -> 180ms skilled
    const float strike_ms  = 90.0f  - 30.0f * skill;   // 90ms novice -> 60ms skilled
    const float hold_ms    = 70.0f;
    const float recover_ms = 260.0f;

    auto guard_pose = [] {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 6.0));
        pose.flex("right_shoulder", static_cast<float>(M_PI / 6.0));
        pose.twist("right_shoulder", 0.0f);
        pose.flex("right_elbow", static_cast<float>(M_PI / 2.0));
        pose.rigid("right_wrist");
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        return pose;
    };

    float t = 0.0f;
    clip.add_keyframe(t, guard_pose());

    // --- Windup: blade raised behind the shoulder, spine winds away ---
    t += windup_ms;
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 2.2));
        pose.flex("right_shoulder", static_cast<float>(-M_PI / 8.0));
        pose.twist("right_shoulder", static_cast<float>(-M_PI / 6.0));
        pose.flex("right_elbow", static_cast<float>(2.0 * M_PI / 3.0));
        pose.rigid("right_wrist");
        pose.twist("lower_spine", static_cast<float>(-M_PI / 10.0));
        pose.twist("upper_spine", static_cast<float>(-M_PI / 8.0));
        clip.add_keyframe(t, pose);
    }

    // --- Strike: shoulder snaps forward, elbow extends, spine unwinds into it ---
    t += strike_ms;
    RotationPose strike_pose;
    {
        strike_pose.abduct("right_shoulder", static_cast<float>(-M_PI / 5.0));
        strike_pose.flex("right_shoulder", static_cast<float>(M_PI / 2.4));
        strike_pose.twist("right_shoulder", static_cast<float>(M_PI / 5.0));
        strike_pose.flex("right_elbow", static_cast<float>(M_PI / 8.0));  // near-extended
        strike_pose.rigid("right_wrist");
        strike_pose.twist("lower_spine", static_cast<float>(M_PI / 10.0));
        strike_pose.twist("upper_spine", static_cast<float>(M_PI / 8.0));
        clip.add_keyframe(t, strike_pose);
    }

    // --- Hold briefly at full extension (the "hit" window) ---
    t += hold_ms;
    clip.add_keyframe(t, strike_pose);

    // --- Recovery: back to guard ---
    t += recover_ms;
    clip.add_keyframe(t, guard_pose());

    clip.body_region = BodyRegion::UPPER_BODY;
    clip.blend_in_ms = 120.0f;
    clip.blend_out_ms = 120.0f;
    return clip;
}

// ============================================================================
// TRAINED CROSS PUNCH (Guard → Load → Strike → Guard)
// ============================================================================
// Biomechanically correct punch with spine torsion. Key differences vs novice:
//   - Starts and ends in guard (not arms-at-sides)
//   - Compact path (max abduction -π/6, not -π/2 T-pose)
//   - Spine torsion (lower_spine + upper_spine wind/unwind)
//   - Shoulder pronation (internal→external twist)
//   - Faster: 150ms windup, 60ms strike (vs novice 300ms/100ms)
//   - Total ~1000ms (vs novice 1500ms)
//
// Timeline:
//   0-100ms:   Guard pose (settled)
//   100-250ms: Compact load + torso wind (150ms)
//   250-310ms: Strike peak (60ms — explosive)
//   310-410ms: Hold at extension
//   410-610ms: Guard recovery (200ms)
//   610-810ms: Hold guard

inline FKAnimationClip create_fk_cross_punch_trained_right()
{
    FKAnimationClip clip;
    clip.name = "fk_cross_punch_trained_right";
    clip.loops = false;

    // Guard pose values (reused at start, hold, and recovery)
    const float guard_shoulder_abduct = static_cast<float>(-M_PI / 6.0);  // -30°
    const float guard_shoulder_flex   = static_cast<float>(M_PI / 6.0);   // +30°
    const float guard_elbow_flex      = static_cast<float>(M_PI / 2.0);   // 90°

    // 0ms: Guard pose
    {
        RotationPose pose;
        pose.abduct("right_shoulder", guard_shoulder_abduct);
        pose.flex("right_shoulder", guard_shoulder_flex);
        pose.flex("right_elbow", guard_elbow_flex);
        pose.rigid("right_wrist");
        clip.add_keyframe(0.0f, pose);
    }

    // 100ms: Hold guard (settled)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", guard_shoulder_abduct);
        pose.flex("right_shoulder", guard_shoulder_flex);
        pose.flex("right_elbow", guard_elbow_flex);
        pose.rigid("right_wrist");
        clip.add_keyframe(100.0f, pose);
    }

    // 250ms: Compact load + torso wind (150ms)
    // Shoulder pulls back from guard, stays compact (no T-pose flare)
    // Spine winds: lower_spine -π/8, upper_spine -π/12 (rear hip loads)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", guard_shoulder_abduct);                     // Stay compact
        pose.flex("right_shoulder", static_cast<float>(-M_PI / 6.0));             // Pull back from guard
        pose.twist("right_shoulder", static_cast<float>(-M_PI / 6.0));            // Internal rotation
        pose.flex("right_elbow", guard_elbow_flex);                                // Arm stays bent
        pose.rigid("right_wrist");
        // Spine torsion — rear hip loads
        pose.twist("lower_spine", static_cast<float>(-M_PI / 8.0));
        pose.twist("upper_spine", static_cast<float>(-M_PI / 12.0));
        clip.add_keyframe(250.0f, pose);
    }

    // 310ms: Strike peak (60ms — explosive)
    // Full forward drive, hip unwinds into punch
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 4.0));           // Arm into centerline
        pose.flex("right_shoulder", static_cast<float>(M_PI / 2.0));              // Full forward drive
        pose.twist("right_shoulder", static_cast<float>(M_PI / 6.0));             // Pronation (knuckle alignment)
        pose.flex("right_elbow", 0.1f);                                            // Near full extension
        pose.rigid("right_wrist");
        // Spine unwinds — hip drives through
        pose.twist("lower_spine", static_cast<float>(M_PI / 8.0));
        pose.twist("upper_spine", static_cast<float>(M_PI / 12.0));
        clip.add_keyframe(310.0f, pose);
    }

    // 410ms: Hold at extension (100ms)
    {
        RotationPose pose;
        pose.abduct("right_shoulder", static_cast<float>(-M_PI / 4.0));
        pose.flex("right_shoulder", static_cast<float>(M_PI / 2.0));
        pose.twist("right_shoulder", static_cast<float>(M_PI / 6.0));
        pose.flex("right_elbow", 0.1f);
        pose.rigid("right_wrist");
        pose.twist("lower_spine", static_cast<float>(M_PI / 8.0));
        pose.twist("upper_spine", static_cast<float>(M_PI / 12.0));
        clip.add_keyframe(410.0f, pose);
    }

    // 610ms: Guard recovery (200ms) — return to guard, spine neutral
    {
        RotationPose pose;
        pose.abduct("right_shoulder", guard_shoulder_abduct);
        pose.flex("right_shoulder", guard_shoulder_flex);
        pose.twist("right_shoulder", 0.0f);
        pose.flex("right_elbow", guard_elbow_flex);
        pose.rigid("right_wrist");
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(610.0f, pose);
    }

    // 810ms: Hold guard
    {
        RotationPose pose;
        pose.abduct("right_shoulder", guard_shoulder_abduct);
        pose.flex("right_shoulder", guard_shoulder_flex);
        pose.twist("right_shoulder", 0.0f);
        pose.flex("right_elbow", guard_elbow_flex);
        pose.rigid("right_wrist");
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(810.0f, pose);
    }

    clip.body_region = BodyRegion::UPPER_BODY;
    clip.blend_in_ms = 80.0f;
    clip.blend_out_ms = 100.0f;
    return clip;
}

// ============================================================================
// CONTINUOUS SKILL-DRIVEN CROSS PUNCH
// ============================================================================
// Parameterized cross punch where a continuous skill float (0.0-1.0) controls
// all biomechanical parameters. Four reference profiles (control points) at
// skill 0.0, 0.3, 0.7, 1.0 are interpolated linearly.
//
// Based on:
//   - Fitts & Posner motor learning model (cognitive → associative → autonomous)
//   - Published boxing biomechanics (Filimonov 1985, PMC9798280, PMC7739747)
//   - Joint limits from joint_types.cpp
//
// API:
//   create_fk_cross_punch_right(0.0f, false)  // == legacy novice
//   create_fk_cross_punch_right(0.5f, true)   // interpolated, from guard
//   create_fk_cross_punch_right(1.0f, true)   // == legacy trained

struct CrossPunchProfile {
    float skill_level;

    // Guard pose
    float guard_shoulder_abduct;
    float guard_shoulder_flex;
    float guard_elbow_flex;

    // Load phase
    float load_shoulder_abduct;
    float load_shoulder_flex;
    float load_shoulder_twist;

    // Spine engagement
    float spine_lower_twist;
    float spine_upper_twist;

    // Strike phase
    float strike_shoulder_abduct;
    float strike_shoulder_flex;
    float strike_shoulder_twist;
    float strike_elbow_flex;

    // Timing (ms)
    float guard_settle_ms;
    float load_ms;
    float hip_arm_gap_ms;
    float strike_ms;
    float hold_ms;
    float recovery_ms;
    float guard_hold_ms;

    // Recovery arc
    float recovery_abduct_flare;
};

// Reference profiles: 4 control points along the skill continuum
// Values derived from biomechanics research (see GAME_DESIGN_ANIMATION_SKILL_PROFILES.md)
inline const CrossPunchProfile& get_reference_profile(int index) {
    static const CrossPunchProfile profiles[4] = {
        // 0.0 - Cognitive: never punched, arm-only, T-pose windup
        {
            0.0f,
            // Guard: untrained "hands up" — wide, low, sloppy
            static_cast<float>(-M_PI / 10.0),
            static_cast<float>(M_PI / 4.0),
            static_cast<float>(M_PI / 4.0),
            // Load: full T-pose flare, big pullback, no twist
            static_cast<float>(-M_PI / 2.0),
            static_cast<float>(-M_PI / 4.0),
            0.0f,
            // Spine: no engagement
            0.0f, 0.0f,
            // Strike: T-pose level, full forward, no pronation, full extension
            static_cast<float>(-M_PI / 2.0),
            static_cast<float>(M_PI / 2.0),
            0.0f,
            0.0f,
            // Timing: slow
            0.0f, 700.0f, 0.0f, 100.0f, 100.0f, 300.0f, 300.0f,
            // Recovery: straight retract
            0.0f
        },
        // 0.3 - Early Associative: basic pattern, loose guard, some hip
        {
            0.3f,
            // Guard: loose (wide elbows, hands rising)
            static_cast<float>(-M_PI / 7.0),
            static_cast<float>(M_PI / 3.0),
            static_cast<float>(M_PI / 3.0),
            // Load: moderate flare, moderate pullback, some twist
            static_cast<float>(-M_PI / 4.0),
            static_cast<float>(-M_PI / 5.0),
            static_cast<float>(-M_PI / 10.0),
            // Spine: beginning engagement
            static_cast<float>(M_PI / 12.0),
            static_cast<float>(M_PI / 16.0),
            // Strike: narrower path, some pronation, slight residual flex
            static_cast<float>(-M_PI / 3.0),
            static_cast<float>(M_PI / 2.0),
            static_cast<float>(M_PI / 10.0),
            0.2f,
            // Timing: moderate (guard_settle 100 — still finding guard)
            100.0f, 200.0f, 30.0f, 80.0f, 100.0f, 240.0f, 200.0f,
            // Recovery: wide arc (intermediate error)
            static_cast<float>(-M_PI / 3.5)
        },
        // 0.7 - Late Associative: refined technique, tight guard, near-automatic
        {
            0.7f,
            // Guard: refined (elbows tucking, hands near chin)
            static_cast<float>(-M_PI / 8.0),
            static_cast<float>(M_PI / 2.5),
            static_cast<float>(5.0 * M_PI / 12.0),
            // Load: compact, controlled pullback, more twist
            static_cast<float>(-M_PI / 5.0),
            static_cast<float>(-M_PI / 5.5),
            static_cast<float>(-M_PI / 7.0),
            // Spine: strong engagement
            static_cast<float>(M_PI / 9.0),
            static_cast<float>(M_PI / 13.0),
            // Strike: converging on centerline, more pronation
            static_cast<float>(-M_PI / 3.5),
            static_cast<float>(M_PI / 2.0),
            static_cast<float>(M_PI / 7.0),
            0.15f,
            // Timing: faster (guard_settle 40 — near-automatic)
            40.0f, 175.0f, 15.0f, 70.0f, 100.0f, 210.0f, 200.0f,
            // Recovery: narrowing arc
            static_cast<float>(-M_PI / 5.0)
        },
        // 1.0 - Autonomous: professional, compact, explosive
        {
            1.0f,
            // Guard: professional (compact, hands at chin, elbows in)
            static_cast<float>(-M_PI / 8.0),
            static_cast<float>(M_PI / 2.5),
            static_cast<float>(M_PI / 2.0),
            // Load: minimal displacement from guard
            static_cast<float>(-M_PI / 6.0),
            static_cast<float>(-M_PI / 6.0),
            static_cast<float>(-M_PI / 6.0),
            // Spine: full engagement
            static_cast<float>(M_PI / 8.0),
            static_cast<float>(M_PI / 12.0),
            // Strike: centerline, full pronation, near-full extension
            static_cast<float>(-M_PI / 4.0),
            static_cast<float>(M_PI / 2.0),
            static_cast<float>(M_PI / 6.0),
            0.1f,
            // Timing: fast (guard_settle 0 — pro, instant transition)
            0.0f, 150.0f, 0.0f, 60.0f, 100.0f, 200.0f, 200.0f,
            // Recovery: straight retract (efficient)
            0.0f
        }
    };
    return profiles[index];
}

inline CrossPunchProfile lerp_profile(const CrossPunchProfile& a, const CrossPunchProfile& b, float t) {
    CrossPunchProfile r;
    r.skill_level             = a.skill_level + t * (b.skill_level - a.skill_level);
    r.guard_shoulder_abduct   = a.guard_shoulder_abduct + t * (b.guard_shoulder_abduct - a.guard_shoulder_abduct);
    r.guard_shoulder_flex     = a.guard_shoulder_flex + t * (b.guard_shoulder_flex - a.guard_shoulder_flex);
    r.guard_elbow_flex        = a.guard_elbow_flex + t * (b.guard_elbow_flex - a.guard_elbow_flex);
    r.load_shoulder_abduct    = a.load_shoulder_abduct + t * (b.load_shoulder_abduct - a.load_shoulder_abduct);
    r.load_shoulder_flex      = a.load_shoulder_flex + t * (b.load_shoulder_flex - a.load_shoulder_flex);
    r.load_shoulder_twist     = a.load_shoulder_twist + t * (b.load_shoulder_twist - a.load_shoulder_twist);
    r.spine_lower_twist       = a.spine_lower_twist + t * (b.spine_lower_twist - a.spine_lower_twist);
    r.spine_upper_twist       = a.spine_upper_twist + t * (b.spine_upper_twist - a.spine_upper_twist);
    r.strike_shoulder_abduct  = a.strike_shoulder_abduct + t * (b.strike_shoulder_abduct - a.strike_shoulder_abduct);
    r.strike_shoulder_flex    = a.strike_shoulder_flex + t * (b.strike_shoulder_flex - a.strike_shoulder_flex);
    r.strike_shoulder_twist   = a.strike_shoulder_twist + t * (b.strike_shoulder_twist - a.strike_shoulder_twist);
    r.strike_elbow_flex       = a.strike_elbow_flex + t * (b.strike_elbow_flex - a.strike_elbow_flex);
    r.guard_settle_ms         = a.guard_settle_ms + t * (b.guard_settle_ms - a.guard_settle_ms);
    r.load_ms                 = a.load_ms + t * (b.load_ms - a.load_ms);
    r.hip_arm_gap_ms          = a.hip_arm_gap_ms + t * (b.hip_arm_gap_ms - a.hip_arm_gap_ms);
    r.strike_ms               = a.strike_ms + t * (b.strike_ms - a.strike_ms);
    r.hold_ms                 = a.hold_ms + t * (b.hold_ms - a.hold_ms);
    r.recovery_ms             = a.recovery_ms + t * (b.recovery_ms - a.recovery_ms);
    r.guard_hold_ms           = a.guard_hold_ms + t * (b.guard_hold_ms - a.guard_hold_ms);
    r.recovery_abduct_flare   = a.recovery_abduct_flare + t * (b.recovery_abduct_flare - a.recovery_abduct_flare);
    return r;
}

// ============================================================================
// GENERIC PROFILE INTERPOLATION (shared by punch and kick)
// ============================================================================
// Bracket search + lerp across an array of profiles sorted by skill_level.
// Each profile type must have a .skill_level field.
template<typename T>
T interpolate_skill_profiles(
    float skill,
    const T* profiles,
    size_t count,
    T (*lerp_fn)(const T&, const T&, float))
{
    if (count == 0) return T{};
    if (skill <= profiles[0].skill_level) return profiles[0];
    if (skill >= profiles[count - 1].skill_level) return profiles[count - 1];
    for (size_t i = 0; i < count - 1; ++i) {
        if (skill <= profiles[i + 1].skill_level) {
            float t = (skill - profiles[i].skill_level) /
                      (profiles[i + 1].skill_level - profiles[i].skill_level);
            return lerp_fn(profiles[i], profiles[i + 1], t);
        }
    }
    return profiles[count - 1];
}

inline CrossPunchProfile get_interpolated_profile(float skill) {
    static const CrossPunchProfile profiles[4] = {
        get_reference_profile(0),
        get_reference_profile(1),
        get_reference_profile(2),
        get_reference_profile(3)
    };
    return interpolate_skill_profiles<CrossPunchProfile>(skill, profiles, 4, lerp_profile);
}

// Generate an FKAnimationClip from a CrossPunchProfile and stance context.
//
// Keyframe structure:
//   [start_pose] → [guard_settle] → [load] → [hip_peak?] → [strike] →
//   [hold] → [recovery_arc?] → [recovery] → [end_hold]
//
// Optional keyframes appear when hip_arm_gap_ms > 0 or recovery_abduct_flare != 0.
inline FKAnimationClip generate_clip_from_profile(
    const CrossPunchProfile& p, bool from_guard,
    const BodyMap& active, const BodyMap& passive, float spine_sign)
{
    FKAnimationClip clip;
    clip.name = "fk_cross_punch_skill_" + std::to_string(static_cast<int>(p.skill_level * 100));
    clip.loops = false;

    // Axis mirror for left-side joints (derived from M * R * M reflection):
    //   Flex (X-axis):  invariant under X-reflection → negate angle (axis inverted)
    //   Abduct (Y-axis): inverts under X-reflection → DON'T negate (axis inversion suffices)
    //   Twist (Z-axis):  inverts under X-reflection → negate angle (axis NOT inverted)
    const float s = active.flex_sign;  // +1 right, -1 left (flex & twist sign)

    // Start/end pose depends on stance context
    float start_abduct = from_guard ? p.guard_shoulder_abduct : 0.0f;
    float start_flex   = from_guard ? s * p.guard_shoulder_flex   : 0.0f;
    float start_elbow  = from_guard ? s * p.guard_elbow_flex      : 0.0f;

    // Low skill + guard penalty: harder to coordinate guard→punch
    float effective_load_ms = p.load_ms;
    if (from_guard && p.skill_level < 0.3f) {
        effective_load_ms *= 1.2f;
    }

    // For no-guard skill 0.0: add T-pose setup phase (matches legacy novice)
    float tpose_setup_ms = 0.0f;
    if (!from_guard && p.skill_level < 0.01f) {
        tpose_setup_ms = 400.0f;  // Deliberate T-pose raise
    }

    // Helper: stabilize both legs at rest to prevent drift
    auto stabilize_legs = [&](RotationPose& pose) {
        pose.flex(active.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(active.knee, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.flex(passive.toe, 0.0f);
    };

    float t = 0.0f;

    // --- Keyframe 1: Start pose ---
    {
        RotationPose pose;
        if (from_guard || p.skill_level >= 0.01f) {
            pose.abduct(active.shoulder, start_abduct);
            pose.flex(active.shoulder, start_flex);
            pose.flex(active.elbow, start_elbow);
            pose.rigid(active.wrist);
        } else {
            pose.abduct(active.shoulder, 0.0f);
            pose.flex(active.shoulder, 0.0f);
            pose.relax(active.elbow);
            pose.relax(active.wrist);
        }
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    // --- Keyframe 2: Guard settle / T-pose setup ---
    if (tpose_setup_ms > 0.0f) {
        t += tpose_setup_ms;
        RotationPose pose;
        pose.abduct(active.shoulder, static_cast<float>(-M_PI / 2.0));
        pose.flex(active.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.flex(active.wrist, 0.0f);
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    } else if (p.guard_settle_ms > 0.0f) {
        t += p.guard_settle_ms;
        RotationPose pose;
        pose.abduct(active.shoulder, start_abduct);
        pose.flex(active.shoulder, start_flex);
        pose.flex(active.elbow, start_elbow);
        pose.rigid(active.wrist);
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    // --- Keyframe 3: Load (windup) ---
    t += effective_load_ms;
    {
        RotationPose pose;
        pose.abduct(active.shoulder, p.load_shoulder_abduct);
        pose.flex(active.shoulder, s * p.load_shoulder_flex);
        if (p.load_shoulder_twist != 0.0f) {
            pose.twist(active.shoulder, s * p.load_shoulder_twist);
        }
        pose.flex(active.elbow, s * (start_elbow != 0.0f ? (start_elbow / s) : static_cast<float>(M_PI / 2.0)));
        pose.rigid(active.wrist);
        // Spine loads (spine_sign controls wind direction)
        if (p.spine_lower_twist > 0.0f) {
            pose.twist("lower_spine", spine_sign * p.spine_lower_twist);
            pose.twist("upper_spine", spine_sign * p.spine_upper_twist);
        }
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    // --- Keyframe 3b: Hip peak (when gap > 0, hip fires before arm) ---
    if (p.hip_arm_gap_ms > 0.5f) {
        t += p.hip_arm_gap_ms;
        RotationPose pose;
        pose.abduct(active.shoulder, p.load_shoulder_abduct);
        pose.flex(active.shoulder, s * p.load_shoulder_flex);
        if (p.load_shoulder_twist != 0.0f) {
            pose.twist(active.shoulder, s * p.load_shoulder_twist);
        }
        pose.flex(active.elbow, s * (start_elbow != 0.0f ? (start_elbow / s) : static_cast<float>(M_PI / 2.0)));
        pose.rigid(active.wrist);
        // Spine has ALREADY unwound (hip fires first)
        pose.twist("lower_spine", -spine_sign * p.spine_lower_twist);
        pose.twist("upper_spine", -spine_sign * p.spine_upper_twist);
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    // --- Keyframe 4: Strike peak ---
    t += p.strike_ms;
    {
        RotationPose pose;
        pose.abduct(active.shoulder, p.strike_shoulder_abduct);
        pose.flex(active.shoulder, s * p.strike_shoulder_flex);
        if (p.strike_shoulder_twist != 0.0f) {
            pose.twist(active.shoulder, s * p.strike_shoulder_twist);
        }
        pose.flex(active.elbow, s * p.strike_elbow_flex);
        pose.rigid(active.wrist);
        // Spine fully unwound through strike
        if (p.spine_lower_twist > 0.0f) {
            pose.twist("lower_spine", -spine_sign * p.spine_lower_twist);
            pose.twist("upper_spine", -spine_sign * p.spine_upper_twist);
        }
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    // --- Keyframe 5: Hold at extension ---
    t += p.hold_ms;
    {
        RotationPose pose;
        pose.abduct(active.shoulder, p.strike_shoulder_abduct);
        pose.flex(active.shoulder, s * p.strike_shoulder_flex);
        if (p.strike_shoulder_twist != 0.0f) {
            pose.twist(active.shoulder, s * p.strike_shoulder_twist);
        }
        pose.flex(active.elbow, s * p.strike_elbow_flex);
        pose.rigid(active.wrist);
        if (p.spine_lower_twist > 0.0f) {
            pose.twist("lower_spine", -spine_sign * p.spine_lower_twist);
            pose.twist("upper_spine", -spine_sign * p.spine_upper_twist);
        }
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    // --- Keyframe 5b: Recovery arc (when flare != 0) ---
    if (std::abs(p.recovery_abduct_flare) > 0.01f) {
        t += p.recovery_ms * 0.4f;
        RotationPose pose;
        float mid_abduct = start_abduct + p.recovery_abduct_flare;
        pose.abduct(active.shoulder, mid_abduct);
        pose.flex(active.shoulder, start_flex);
        pose.twist(active.shoulder, 0.0f);
        pose.flex(active.elbow, start_elbow != 0.0f ? start_elbow : 0.0f);
        pose.rigid(active.wrist);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);

        t += p.recovery_ms * 0.6f;
    } else {
        t += p.recovery_ms;
    }

    // --- Keyframe 6: Recovery to start pose ---
    {
        RotationPose pose;
        if (from_guard || p.skill_level >= 0.01f) {
            pose.abduct(active.shoulder, start_abduct);
            pose.flex(active.shoulder, start_flex);
            pose.twist(active.shoulder, 0.0f);
            pose.flex(active.elbow, start_elbow);
            pose.rigid(active.wrist);
        } else {
            pose.abduct(active.shoulder, 0.0f);
            pose.flex(active.shoulder, 0.0f);
            pose.relax(active.elbow);
            pose.relax(active.wrist);
        }
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    // --- Keyframe 7: Hold at end ---
    t += p.guard_hold_ms;
    {
        RotationPose pose;
        if (from_guard || p.skill_level >= 0.01f) {
            pose.abduct(active.shoulder, start_abduct);
            pose.flex(active.shoulder, start_flex);
            pose.twist(active.shoulder, 0.0f);
            pose.flex(active.elbow, start_elbow);
            pose.rigid(active.wrist);
        } else {
            pose.abduct(active.shoulder, 0.0f);
            pose.flex(active.shoulder, 0.0f);
            pose.relax(active.elbow);
            pose.relax(active.wrist);
        }
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        stabilize_legs(pose);
        clip.add_keyframe(t, pose);
    }

    clip.body_region = BodyRegion::UPPER_BODY;
    // Blend-in depends on starting context:
    //   from_guard: arms are already raised in guard pose — bigger gap from
    //               walk swing, needs longer crossfade to avoid visible pop.
    //   no guard:   arms start at neutral (near walk rest), smaller gap.
    // The guard_settle phase absorbs the blend-in (arm holds during ramp).
    clip.blend_in_ms = from_guard ? 180.0f : 100.0f;
    clip.blend_out_ms = 100.0f;
    return clip;
}

// Public API: Side-generic cross punch
inline FKAnimationClip create_fk_cross_punch(float skill, Side side, bool from_guard = true) {
    CrossPunchProfile profile = get_interpolated_profile(skill);
    float spine_sign = (side == Side::RIGHT) ? -1.0f : 1.0f;
    return generate_clip_from_profile(profile, from_guard, body(side), body(opposite(side)), spine_sign);
}

// Backward compatible: right-side cross punch
inline FKAnimationClip create_fk_cross_punch_right(float skill, bool from_guard = true) {
    return create_fk_cross_punch(skill, Side::RIGHT, from_guard);
}

// ============================================================================
// CONTINUOUS SKILL-DRIVEN FRONT KICK (Ap Chagi)
// ============================================================================
// Parameterized front snap kick where a continuous skill float (0.0-1.0)
// controls all biomechanical parameters. Same architecture as CrossPunchProfile.
//
// Based on:
//   - Fitts & Posner motor learning model (cognitive → associative → autonomous)
//   - Taekwondo ap chagi biomechanics
//   - Joint limits from joint_types.cpp
//
// API:
//   create_fk_front_kick_right(0.0f, false)  // novice, from neutral
//   create_fk_front_kick_right(0.5f, true)   // interpolated, from stance
//   create_fk_front_kick_right(1.0f, true)   // black belt, from stance

struct FrontKickProfile {
    float skill_level;

    // Stance pose (fighting stance hip/knee bend)
    float stance_hip_flex;
    float stance_knee_flex;

    // Chamber (knee raise + fold)
    float chamber_hip_flex;
    float chamber_knee_flex;
    float chamber_hip_abduct;       // lateral drift (novice: wide)

    // Spine engagement (kinetic chain)
    float spine_lower_twist;        // hip rotation driving kick
    float spine_upper_twist;        // counter-rotation for balance

    // Snap (knee extension at impact)
    float snap_hip_flex;            // hip angle at full extension
    float snap_knee_flex;           // residual knee bend (0=straight)
    float snap_ankle_flex;          // dorsiflexion (negative=toe back)

    // Timing (ms)
    float stance_settle_ms;
    float chamber_ms;
    float hip_leg_gap_ms;           // spine fires before leg snaps
    float snap_ms;                  // explosive extension
    float hold_ms;                  // hold at impact
    float recoil_ms;                // rechamber before recovery
    float recovery_ms;
    float stance_hold_ms;

    // Recovery
    float recovery_hip_abduct;      // lateral flare during recovery
};

// Reference profiles: 4 control points along the skill continuum
inline const FrontKickProfile& get_kick_reference_profile(int index) {
    static const FrontKickProfile profiles[4] = {
        // 0.0 - Cognitive: never kicked, low chamber, can't extend, no chain
        {
            0.0f,
            // Stance
            0.05f, 0.10f,
            // Chamber: low (60°), loose fold (60°), foot drifts wide
            static_cast<float>(M_PI / 3.0),        // 1.047
            static_cast<float>(M_PI / 3.0),        // 1.047
            0.15f,
            // Spine: no engagement
            0.0f, 0.0f,
            // Snap: same hip as chamber, can't extend (0.5 residual), no dorsiflexion
            static_cast<float>(M_PI / 3.0),        // 1.047
            0.50f,
            0.0f,
            // Timing: slow (total ~1430ms)
            0.0f, 500.0f, 0.0f, 150.0f, 80.0f, 0.0f, 400.0f, 300.0f,
            // Recovery: wide flare
            0.20f
        },
        // 0.3 - Early Associative: basic pattern, some hip rotation, beginning rechamber
        {
            0.3f,
            // Stance
            0.08f, 0.15f,
            // Chamber: better (75°), tighter fold (90°), less drift
            static_cast<float>(5.0 * M_PI / 12.0), // 1.309
            static_cast<float>(M_PI / 2.0),         // 1.571
            0.10f,
            // Spine: beginning engagement
            static_cast<float>(M_PI / 12.0),       // 0.262
            static_cast<float>(M_PI / 16.0),       // 0.196
            // Snap: better extension (0.3 residual), developing dorsiflexion
            static_cast<float>(5.0 * M_PI / 12.0), // 1.309
            0.30f,
            -0.20f,
            // Timing: moderate (total ~1235ms)
            80.0f, 350.0f, 25.0f, 100.0f, 80.0f, 100.0f, 300.0f, 200.0f,
            // Recovery: moderate flare
            0.15f
        },
        // 0.7 - Late Associative: high chamber, tight fold, good chain, clean rechamber
        {
            0.7f,
            // Stance
            0.10f, 0.20f,
            // Chamber: high (90°), tight fold (120°), minimal drift
            static_cast<float>(M_PI / 2.0),        // 1.571
            static_cast<float>(2.0 * M_PI / 3.0),  // 2.094
            0.05f,
            // Spine: strong engagement
            static_cast<float>(M_PI / 9.0),        // 0.349
            static_cast<float>(M_PI / 13.0),       // 0.242
            // Snap: near-full extension (0.12 residual), good dorsiflexion
            static_cast<float>(M_PI / 2.0),        // 1.571
            0.12f,
            -0.40f,
            // Timing: fast (total ~1015ms)
            80.0f, 250.0f, 15.0f, 70.0f, 80.0f, 120.0f, 200.0f, 200.0f,
            // Recovery: slight flare
            0.08f
        },
        // 1.0 - Autonomous: max chamber (90°), heel-to-glute fold, full chain, instant rechamber
        {
            1.0f,
            // Stance
            0.12f, 0.25f,
            // Chamber: maximum (120° = high kick), heel-to-glute (126°), zero drift
            static_cast<float>(2.0 * M_PI / 3.0),  // 2.094
            2.20f,                                   // near max knee flex
            0.0f,
            // Spine: full engagement
            static_cast<float>(M_PI / 8.0),        // 0.393
            static_cast<float>(M_PI / 12.0),       // 0.262
            // Snap: full extension (0.05 residual), ball-of-foot strike (max dorsiflexion)
            static_cast<float>(2.0 * M_PI / 3.0),  // 2.094
            0.05f,
            -0.50f,
            // Timing: explosive (total ~820ms)
            80.0f, 180.0f, 0.0f, 50.0f, 60.0f, 100.0f, 150.0f, 200.0f,
            // Recovery: zero lateral drift
            0.0f
        }
    };
    return profiles[index];
}

inline FrontKickProfile lerp_kick_profile(const FrontKickProfile& a, const FrontKickProfile& b, float t) {
    FrontKickProfile r;
    r.skill_level          = a.skill_level + t * (b.skill_level - a.skill_level);
    r.stance_hip_flex      = a.stance_hip_flex + t * (b.stance_hip_flex - a.stance_hip_flex);
    r.stance_knee_flex     = a.stance_knee_flex + t * (b.stance_knee_flex - a.stance_knee_flex);
    r.chamber_hip_flex     = a.chamber_hip_flex + t * (b.chamber_hip_flex - a.chamber_hip_flex);
    r.chamber_knee_flex    = a.chamber_knee_flex + t * (b.chamber_knee_flex - a.chamber_knee_flex);
    r.chamber_hip_abduct   = a.chamber_hip_abduct + t * (b.chamber_hip_abduct - a.chamber_hip_abduct);
    r.spine_lower_twist    = a.spine_lower_twist + t * (b.spine_lower_twist - a.spine_lower_twist);
    r.spine_upper_twist    = a.spine_upper_twist + t * (b.spine_upper_twist - a.spine_upper_twist);
    r.snap_hip_flex        = a.snap_hip_flex + t * (b.snap_hip_flex - a.snap_hip_flex);
    r.snap_knee_flex       = a.snap_knee_flex + t * (b.snap_knee_flex - a.snap_knee_flex);
    r.snap_ankle_flex      = a.snap_ankle_flex + t * (b.snap_ankle_flex - a.snap_ankle_flex);
    r.stance_settle_ms     = a.stance_settle_ms + t * (b.stance_settle_ms - a.stance_settle_ms);
    r.chamber_ms           = a.chamber_ms + t * (b.chamber_ms - a.chamber_ms);
    r.hip_leg_gap_ms       = a.hip_leg_gap_ms + t * (b.hip_leg_gap_ms - a.hip_leg_gap_ms);
    r.snap_ms              = a.snap_ms + t * (b.snap_ms - a.snap_ms);
    r.hold_ms              = a.hold_ms + t * (b.hold_ms - a.hold_ms);
    r.recoil_ms            = a.recoil_ms + t * (b.recoil_ms - a.recoil_ms);
    r.recovery_ms          = a.recovery_ms + t * (b.recovery_ms - a.recovery_ms);
    r.stance_hold_ms       = a.stance_hold_ms + t * (b.stance_hold_ms - a.stance_hold_ms);
    r.recovery_hip_abduct  = a.recovery_hip_abduct + t * (b.recovery_hip_abduct - a.recovery_hip_abduct);
    return r;
}

inline FrontKickProfile get_interpolated_kick_profile(float skill) {
    static const FrontKickProfile profiles[4] = {
        get_kick_reference_profile(0),
        get_kick_reference_profile(1),
        get_kick_reference_profile(2),
        get_kick_reference_profile(3)
    };
    return interpolate_skill_profiles<FrontKickProfile>(skill, profiles, 4, lerp_kick_profile);
}

// Generate an FKAnimationClip from a FrontKickProfile and stance context.
//
// Keyframe structure:
//   [start_pose] → [stance_settle?] → [chamber] → [hip_peak?] → [snap] →
//   [hold] → [recoil?] → [recovery_arc?] → [recovery] → [stance_hold]
//
// from_stance: true = starts/ends in fighting stance, false = neutral standing
inline FKAnimationClip generate_kick_clip_from_profile(
    const FrontKickProfile& p, bool from_stance,
    const BodyMap& active, const BodyMap& passive, float spine_sign)
{
    FKAnimationClip clip;
    clip.name = "fk_front_kick_skill_" + std::to_string(static_cast<int>(p.skill_level * 100));
    clip.loops = false;

    // Axis mirror: left-side joints have inverted flex/abduct axes.
    // Hip, ankle: multiply by s. Knee: same axis both sides, no sign needed.
    const float s = active.flex_sign;

    // Start/end hip and knee angles depend on stance context
    float start_hip_flex  = from_stance ? s * p.stance_hip_flex  : 0.0f;
    float start_knee_flex = from_stance ? p.stance_knee_flex : 0.0f;  // knee: no sign

    // Low skill + no stance penalty: harder to coordinate
    float effective_chamber_ms = p.chamber_ms;
    if (!from_stance && p.skill_level < 0.3f) {
        effective_chamber_ms *= 1.2f;
    }

    // Helper: stabilize non-kicking limbs (opposite leg + both shoulders + toes)
    auto add_stabilization = [&](RotationPose& pose) {
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(passive.toe, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(passive.shoulder, 0.0f);
    };

    float t = 0.0f;

    // --- KF1: Start pose ---
    {
        RotationPose pose;
        pose.flex(active.hip, start_hip_flex);
        pose.flex(active.knee, start_knee_flex);
        pose.flex(active.ankle, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF2: Stance settle (conditional) ---
    if (p.stance_settle_ms > 0.0f) {
        t += p.stance_settle_ms;
        RotationPose pose;
        pose.flex(active.hip, start_hip_flex);
        pose.flex(active.knee, start_knee_flex);
        pose.flex(active.ankle, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF3: Chamber (knee raise + fold + spine load) ---
    t += effective_chamber_ms;
    {
        RotationPose pose;
        pose.flex(active.hip, s * p.chamber_hip_flex);
        pose.flex(active.knee, p.chamber_knee_flex);          // knee: no sign
        pose.flex(active.ankle, 0.0f);
        if (std::abs(p.chamber_hip_abduct) > 0.01f) {
            pose.abduct(active.hip, p.chamber_hip_abduct);
        }
        if (p.spine_lower_twist > 0.0f) {
            pose.twist("lower_spine", spine_sign * p.spine_lower_twist);
            pose.twist("upper_spine", spine_sign * p.spine_upper_twist);
        } else {
            pose.twist("lower_spine", 0.0f);
            pose.twist("upper_spine", 0.0f);
        }
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF3b: Hip peak (spine fires before leg, when gap > 0) ---
    if (p.hip_leg_gap_ms > 0.5f) {
        t += p.hip_leg_gap_ms;
        RotationPose pose;
        pose.flex(active.hip, s * p.chamber_hip_flex);
        pose.flex(active.knee, p.chamber_knee_flex);          // knee: no sign
        pose.flex(active.ankle, 0.0f);
        if (std::abs(p.chamber_hip_abduct) > 0.01f) {
            pose.abduct(active.hip, p.chamber_hip_abduct);
        }
        pose.twist("lower_spine", -spine_sign * p.spine_lower_twist);
        pose.twist("upper_spine", -spine_sign * p.spine_upper_twist);
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF4: Snap (knee extension + dorsiflexion + spine through) ---
    t += p.snap_ms;
    {
        RotationPose pose;
        pose.flex(active.hip, s * p.snap_hip_flex);
        pose.flex(active.knee, p.snap_knee_flex);             // knee: no sign
        pose.flex(active.ankle, s * p.snap_ankle_flex);
        if (std::abs(p.chamber_hip_abduct) > 0.01f) {
            pose.abduct(active.hip, 0.0f);
        }
        if (p.spine_lower_twist > 0.0f) {
            pose.twist("lower_spine", -spine_sign * p.spine_lower_twist);
            pose.twist("upper_spine", -spine_sign * p.spine_upper_twist);
        } else {
            pose.twist("lower_spine", 0.0f);
            pose.twist("upper_spine", 0.0f);
        }
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF5: Hold at impact ---
    t += p.hold_ms;
    {
        RotationPose pose;
        pose.flex(active.hip, s * p.snap_hip_flex);
        pose.flex(active.knee, p.snap_knee_flex);             // knee: no sign
        pose.flex(active.ankle, s * p.snap_ankle_flex);
        if (p.spine_lower_twist > 0.0f) {
            pose.twist("lower_spine", -spine_sign * p.spine_lower_twist);
            pose.twist("upper_spine", -spine_sign * p.spine_upper_twist);
        } else {
            pose.twist("lower_spine", 0.0f);
            pose.twist("upper_spine", 0.0f);
        }
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF6: Recoil to chamber (conditional, when recoil_ms > 0) ---
    if (p.recoil_ms > 0.5f) {
        t += p.recoil_ms;
        RotationPose pose;
        pose.flex(active.hip, s * p.chamber_hip_flex);
        pose.flex(active.knee, p.chamber_knee_flex);          // knee: no sign
        pose.flex(active.ankle, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF7: Recovery arc (conditional, when recovery_hip_abduct > threshold) ---
    if (std::abs(p.recovery_hip_abduct) > 0.01f) {
        t += p.recovery_ms * 0.4f;
        RotationPose pose;
        float mid_hip_flex = start_hip_flex + s * (p.chamber_hip_flex - p.stance_hip_flex) * 0.3f;
        pose.flex(active.hip, mid_hip_flex);
        pose.abduct(active.hip, p.recovery_hip_abduct);
        pose.flex(active.knee, start_knee_flex);              // knee: no sign
        pose.flex(active.ankle, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        add_stabilization(pose);
        clip.add_keyframe(t, pose);

        t += p.recovery_ms * 0.6f;
    } else {
        t += p.recovery_ms;
    }

    // --- KF7b: Recovery to stance ---
    {
        RotationPose pose;
        pose.flex(active.hip, start_hip_flex);
        pose.abduct(active.hip, 0.0f);
        pose.flex(active.knee, start_knee_flex);              // knee: no sign
        pose.flex(active.ankle, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    // --- KF8: Stance hold ---
    t += p.stance_hold_ms;
    {
        RotationPose pose;
        pose.flex(active.hip, start_hip_flex);
        pose.abduct(active.hip, 0.0f);
        pose.flex(active.knee, start_knee_flex);              // knee: no sign
        pose.flex(active.ankle, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        add_stabilization(pose);
        clip.add_keyframe(t, pose);
    }

    clip.blend_in_ms = 100.0f;
    clip.blend_out_ms = 150.0f;
    return clip;
}

// Public API: Side-generic front kick
inline FKAnimationClip create_fk_front_kick(float skill, Side side, bool from_stance = true) {
    FrontKickProfile profile = get_interpolated_kick_profile(skill);
    float spine_sign = (side == Side::RIGHT) ? -1.0f : 1.0f;
    return generate_kick_clip_from_profile(profile, from_stance, body(side), body(opposite(side)), spine_sign);
}

// Backward compatible: right-side front kick
inline FKAnimationClip create_fk_front_kick_right(float skill, bool from_stance = true) {
    return create_fk_front_kick(skill, Side::RIGHT, from_stance);
}

// ============================================================================
// WALK STEP PROFILE (same architecture as CrossPunchProfile / FrontKickProfile)
// ============================================================================
// A single walking step: one leg swings forward while the other supports.
// Clips chain at matching terminal poses: KF_last of step_R == KF_first of step_L.
//
// Gait cycle reference (Perry & Burnfield, "Gait Analysis"):
//   Stance phase:  ~60% of gait cycle (foot on ground)
//   Swing phase:   ~40% of gait cycle (foot in air)
//   Double support: ~20% total (split at start/end of stance)
//
// A single "step" clip covers one swing phase + weight acceptance:
//   KF0: Double-support (both feet planted)
//   KF1: Toe-off (active foot lifts, push from stance ankle)
//   KF2: Mid-swing (active leg at peak flexion, passing stance leg)
//   KF3: Heel-strike (active foot forward, knee extending)
//   KF4: Weight-accept (new double-support, mirror of KF0)

struct WalkStepProfile {
    float skill_level;

    // Active leg (swing phase)
    float hip_flex_peak;          // Peak hip flexion during swing (~0.4 rad walk)
    float knee_flex_peak;         // Peak knee flexion for clearance (~0.7 rad walk)
    float ankle_dorsi_swing;      // Ankle dorsiflexion during swing (~0.15 rad)
    float ankle_push_off;         // Ankle plantarflexion at toe-off (~-0.2 rad)
    float toe_push_off;           // Toe plantarflexion at push-off (~-0.35 rad, curl down)
    float toe_dorsi_swing;        // Toe dorsiflexion during swing (~0.20 rad, lift up)

    // Passive leg (stance phase)
    float stance_hip_extend;      // Hip extension as body passes over (~-0.15 rad)
    float stance_knee_flex;       // Stance knee bend for shock absorption (~0.1 rad)

    // Arms (contralateral: opposite arm swings with active leg)
    float arm_shoulder_flex;      // Opposite arm forward swing (~0.3 rad)
    float arm_elbow_flex;         // Slight elbow bend (~0.2 rad)
    float arm_spread_compensation; // Outward abduction proportional to flex (~0.4)
                                   // Counteracts medial drift from spine twist during turns

    // Spine
    float spine_twist;            // Counter-rotation toward active leg (~0.05 rad)

    // Hip sway & bounce (pelvic motion)
    float pelvic_tilt;            // Lateral spine abduction toward swing leg (~0.07 rad / ~4°)
    float pelvic_bounce;          // Spine flexion oscillation for vertical CoM bounce (~0.04 rad)

    // Timing (ms)
    float swing_ms;               // Swing phase duration
    float contact_ms;             // Double-support / weight acceptance phase

    // Stride
    float stride_length;          // Meters per step (for future velocity derivation)

    // C3: Head stabilization (vestibular-ocular reflex)
    // Head counter-rotates against pelvic tilt/bounce to stay visually stable.
    // 0.0 = no stabilization, 1.0 = full counter-motion (robotic).
    // 0.3-0.5 is natural range (partial compensation).
    float head_stabilize_factor = 0.4f;
};

// Reference walk profile — tuned for natural adult walking (~1.2 m/s)
// Angles from biomechanics literature (Perry & Burnfield), visual tuning expected.
//
// C2 tuning: Increased pelvic motion and arm swing for more weight/momentum feel.
// Increased pelvic_tilt 0.07→0.09 (more hip sway, visible lateral weight shift).
// Increased pelvic_bounce 0.04→0.06 (more vertical bounce, reads as heavier).
// Increased arm_shoulder_flex 0.30→0.35 (more expressive arm swing).
// Increased arm_elbow_flex 0.20→0.25 (arms held slightly higher, more engaged).
inline const WalkStepProfile& get_walk_reference_profile() {
    static const WalkStepProfile profile = {
        1.0f,  // skill_level (only one for now; add continuum later if needed)

        // Active leg
        0.40f,   // hip_flex_peak: ~23 degrees forward
        0.70f,   // knee_flex_peak: ~40 degrees for foot clearance
        0.15f,   // ankle_dorsi_swing: toe up during swing
        -0.20f,  // ankle_push_off: plantarflex at toe-off (negative = toe down)
        -0.35f,  // toe_push_off: curl down at push-off
        0.20f,   // toe_dorsi_swing: lift up for clearance during swing

        // Passive leg
        -0.15f,  // stance_hip_extend: slight extension as body passes
        0.10f,   // stance_knee_flex: shock absorption bend

        // Arms — C2: increased from 0.30/0.20 for more expressive swing
        0.35f,   // arm_shoulder_flex: contralateral forward swing (was 0.30)
        0.25f,   // arm_elbow_flex: slight elbow bend during swing (was 0.20)
        0.00f,   // arm_spread_compensation: outward abduction ratio vs flex (DEBUG: disabled)

        // Spine
        0.05f,   // spine_twist: subtle counter-rotation

        // Hip sway & bounce — C2: increased for weight/momentum feel
        0.09f,   // pelvic_tilt: lateral dip toward swing leg (~5 degrees, was 0.07)
        0.06f,   // pelvic_bounce: spine flex oscillation for vertical CoM (~3.4 degrees, was 0.04)

        // Timing
        400.0f,  // swing_ms: swing phase
        200.0f,  // contact_ms: weight acceptance

        // Stride
        0.65f,   // stride_length: meters per step (~1.3m full stride)

        // C3: Head stabilization
        0.4f     // head_stabilize_factor: 40% counter-motion vs pelvic oscillation
    };
    return profile;
}

// Generate a single walk step FK clip.
//
// Parameters follow the punch/kick convention:
//   active  = side of the STEPPING (swing) leg
//   passive = side of the STANCE (support) leg
//   spine_sign = controls spine twist direction (-1 for right step, +1 for left)
//
// Keyframe structure (5 keyframes):
//   KF0 (0ms):                      Double-support rest
//   KF1 (swing_ms * 0.3):           Toe-off
//   KF2 (swing_ms * 0.7):           Mid-swing (peak flexion)
//   KF3 (swing_ms):                 Heel-strike
//   KF4 (swing_ms + contact_ms):    Weight-accept (new double-support)
//
// CHAINING: KF4 of step_R is the mirror of KF0 of step_L.
// Both are double-support poses with swapped roles.
inline FKAnimationClip generate_walk_step_clip(
    const WalkStepProfile& p,
    const BodyMap& active, const BodyMap& passive, float spine_sign)
{
    FKAnimationClip clip;
    clip.name = "fk_walk_step";
    clip.loops = false;

    const float s = active.flex_sign;  // +1 right, -1 left

    float t = 0.0f;

    // C3: Head stabilization factor — counters pelvic tilt/bounce to keep head level
    const float hs = p.head_stabilize_factor;

    // --- KF0: Double-support rest (0ms) ---
    // Both feet planted, all joints near neutral
    {
        RotationPose pose;
        // Active leg: neutral
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, 0.0f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        // Passive leg: neutral
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: neutral
        pose.flex(active.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.flex(passive.shoulder, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        // Spine: neutral
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        // Hip sway & bounce: double-support — pelvis neutral, CoM at lowest
        pose.abduct("lower_spine", 0.0f);
        pose.flex("lower_spine", p.pelvic_bounce * 0.5f);  // slight forward flex (low point)
        // C3: Head counter-motion (counters pelvic bounce at KF0)
        pose.flex("neck", -p.pelvic_bounce * 0.5f * hs * 0.6f);
        pose.flex("head", -p.pelvic_bounce * 0.5f * hs * 0.4f);
        pose.abduct("neck", 0.0f);
        pose.abduct("head", 0.0f);
        clip.add_keyframe(t, pose);
    }

    // --- KF1: Toe-off (swing_ms * 0.3) ---
    // Active foot lifts off, stance ankle pushes
    t = p.swing_ms * 0.3f;
    {
        RotationPose pose;
        // Active leg: beginning to lift
        pose.flex(active.hip, s * p.hip_flex_peak * 0.3f);
        pose.flex(active.knee, p.knee_flex_peak * 0.4f);
        pose.flex(active.ankle, s * p.ankle_push_off);  // plantarflex (push off)
        pose.flex(active.toe, s * p.toe_push_off);      // curl down at push-off
        // Passive leg: accepting weight
        pose.flex(passive.hip, -s * p.stance_hip_extend * 0.3f);
        pose.flex(passive.knee, p.stance_knee_flex * 0.5f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: beginning contralateral swing
        // Opposite arm (passive side) swings forward with active leg
        pose.flex(passive.shoulder, -s * p.arm_shoulder_flex * 0.3f);
        pose.flex(passive.elbow, passive.flex_sign * p.arm_elbow_flex * 0.3f);
        pose.abduct(passive.shoulder, p.arm_shoulder_flex * 0.3f * p.arm_spread_compensation);
        // Same-side arm swings back (opposite sign from passive)
        pose.flex(active.shoulder, s * p.arm_shoulder_flex * 0.2f * -1.0f);
        pose.flex(active.elbow, active.flex_sign * p.arm_elbow_flex * 0.2f);
        pose.abduct(active.shoulder, p.arm_shoulder_flex * 0.2f * p.arm_spread_compensation);
        // Spine: beginning twist
        pose.twist("lower_spine", spine_sign * p.spine_twist * 0.3f);
        pose.twist("upper_spine", spine_sign * p.spine_twist * 0.2f);
        // Hip sway & bounce: pelvis beginning to tilt toward swing side, CoM rising
        pose.abduct("lower_spine", spine_sign * p.pelvic_tilt * 0.4f);
        pose.flex("lower_spine", 0.0f);  // transition toward extension
        // C3: Head counter-motion (counters pelvic tilt at KF1)
        pose.flex("neck", 0.0f);
        pose.flex("head", 0.0f);
        pose.abduct("neck", -spine_sign * p.pelvic_tilt * 0.4f * hs * 0.6f);
        pose.abduct("head", -spine_sign * p.pelvic_tilt * 0.4f * hs * 0.4f);
        clip.add_keyframe(t, pose);
    }

    // --- KF2: Mid-swing (swing_ms * 0.7) ---
    // Active leg at peak flexion, passing stance leg
    t = p.swing_ms * 0.7f;
    {
        RotationPose pose;
        // Active leg: peak flexion
        pose.flex(active.hip, s * p.hip_flex_peak);
        pose.flex(active.knee, p.knee_flex_peak);
        pose.flex(active.ankle, s * p.ankle_dorsi_swing);  // toe up for clearance
        pose.flex(active.toe, s * p.toe_dorsi_swing);     // toe up for clearance
        // Passive leg: mid-stance extension
        pose.flex(passive.hip, -s * p.stance_hip_extend);
        pose.flex(passive.knee, p.stance_knee_flex);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: peak contralateral swing
        pose.flex(passive.shoulder, -s * p.arm_shoulder_flex);
        pose.flex(passive.elbow, passive.flex_sign * p.arm_elbow_flex);
        pose.abduct(passive.shoulder, p.arm_shoulder_flex * p.arm_spread_compensation);
        // Same-side arm at peak backward swing (opposite sign from passive)
        pose.flex(active.shoulder, s * p.arm_shoulder_flex * 0.6f * -1.0f);
        pose.flex(active.elbow, active.flex_sign * p.arm_elbow_flex * 0.4f);
        pose.abduct(active.shoulder, p.arm_shoulder_flex * 0.6f * p.arm_spread_compensation);
        // Spine: peak twist
        pose.twist("lower_spine", spine_sign * p.spine_twist);
        pose.twist("upper_spine", spine_sign * p.spine_twist * 0.7f);
        // Hip sway & bounce: peak tilt toward swing side, CoM at highest (spine extended)
        pose.abduct("lower_spine", spine_sign * p.pelvic_tilt);
        pose.flex("lower_spine", -p.pelvic_bounce * 0.5f);  // slight extension (high point)
        // C3: Head counter-motion — peak stabilization (counters both tilt and bounce)
        pose.flex("neck", p.pelvic_bounce * 0.5f * hs * 0.6f);    // counter spine extension
        pose.flex("head", p.pelvic_bounce * 0.5f * hs * 0.4f);
        pose.abduct("neck", -spine_sign * p.pelvic_tilt * hs * 0.6f);   // counter lateral tilt
        pose.abduct("head", -spine_sign * p.pelvic_tilt * hs * 0.4f);
        clip.add_keyframe(t, pose);
    }

    // --- KF3: Heel-strike (swing_ms) ---
    // Active foot forward, knee extending for ground contact
    t = p.swing_ms;
    {
        RotationPose pose;
        // Active leg: forward, extending for contact
        pose.flex(active.hip, s * p.hip_flex_peak * 0.5f);
        pose.flex(active.knee, p.knee_flex_peak * 0.1f);  // nearly straight
        pose.flex(active.ankle, s * p.ankle_dorsi_swing * 0.5f);  // slight dorsiflex
        pose.flex(active.toe, s * p.toe_dorsi_swing * 0.3f);    // slight dorsi at heel-strike
        // Passive leg: terminal stance
        pose.flex(passive.hip, -s * p.stance_hip_extend * 0.5f);
        pose.flex(passive.knee, p.stance_knee_flex * 0.3f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: decelerating
        pose.flex(passive.shoulder, -s * p.arm_shoulder_flex * 0.5f);
        pose.flex(passive.elbow, passive.flex_sign * p.arm_elbow_flex * 0.5f);
        pose.abduct(passive.shoulder, p.arm_shoulder_flex * 0.5f * p.arm_spread_compensation);
        // Same-side arm decelerating from backward swing
        pose.flex(active.shoulder, s * p.arm_shoulder_flex * 0.3f * -1.0f);
        pose.flex(active.elbow, active.flex_sign * p.arm_elbow_flex * 0.2f);
        pose.abduct(active.shoulder, p.arm_shoulder_flex * 0.3f * p.arm_spread_compensation);
        // Spine: unwinding
        pose.twist("lower_spine", spine_sign * p.spine_twist * 0.3f);
        pose.twist("upper_spine", spine_sign * p.spine_twist * 0.2f);
        // Hip sway & bounce: returning from tilt, CoM descending
        pose.abduct("lower_spine", spine_sign * p.pelvic_tilt * 0.3f);
        pose.flex("lower_spine", p.pelvic_bounce * 0.3f);  // flexing back down
        // C3: Head counter-motion (returning)
        pose.flex("neck", -p.pelvic_bounce * 0.3f * hs * 0.6f);
        pose.flex("head", -p.pelvic_bounce * 0.3f * hs * 0.4f);
        pose.abduct("neck", -spine_sign * p.pelvic_tilt * 0.3f * hs * 0.6f);
        pose.abduct("head", -spine_sign * p.pelvic_tilt * 0.3f * hs * 0.4f);
        clip.add_keyframe(t, pose);
    }

    // --- KF4: Weight-accept / new double-support (swing_ms + contact_ms) ---
    // Return to neutral. This pose == KF0 for the OPPOSITE step.
    t = p.swing_ms + p.contact_ms;
    {
        RotationPose pose;
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, 0.0f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.flex(passive.shoulder, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        // Hip sway & bounce: back to neutral (mirrors KF0)
        pose.abduct("lower_spine", 0.0f);
        pose.flex("lower_spine", p.pelvic_bounce * 0.5f);  // low point again
        // C3: Head counter-motion (mirrors KF0)
        pose.flex("neck", -p.pelvic_bounce * 0.5f * hs * 0.6f);
        pose.flex("head", -p.pelvic_bounce * 0.5f * hs * 0.4f);
        pose.abduct("neck", 0.0f);
        pose.abduct("head", 0.0f);
        clip.add_keyframe(t, pose);
    }

    return clip;
}

// Public API: Side-generic walk step
inline FKAnimationClip create_fk_walk_step(Side side) {
    const WalkStepProfile& profile = get_walk_reference_profile();
    float spine_sign = (side == Side::RIGHT) ? -1.0f : 1.0f;
    return generate_walk_step_clip(profile, body(side), body(opposite(side)), spine_sign);
}

// Backward compatible: right-side walk step
inline FKAnimationClip create_fk_walk_step_right() {
    return create_fk_walk_step(Side::RIGHT);
}

// ============================================================================
// STRAFE SIDE-STEP CLIP
// ============================================================================
// Lateral stepping motion for strafing. Biomechanically different from walk:
// - Primary motion is hip ABDUCTION (lateral) instead of FLEXION (sagittal)
// - Smaller knee flex (lateral step needs less clearance)
// - Arms provide lateral balance instead of sagittal swing
// - Spine leans laterally toward movement direction
//
// strafe_dir: +1 = stepping rightward, -1 = stepping leftward
// The active leg alternates (R then L then R...) just like walk.
// Leading leg (active on strafe side) gets full abduction amplitude.
// Trailing leg (active on opposite side) gets reduced amplitude (closing gap).
//
// Sign convention: strafe_dir * active.flex_sign > 0 means leading step.
//
// Keyframes (5, same timing as walk):
//   KF0: Neutral double-support
//   KF1: Active leg lifting laterally
//   KF2: Peak lateral reach
//   KF3: Active foot landing laterally
//   KF4: Weight acceptance (back to neutral)
// ============================================================================

struct StrafeStepProfile {
    float hip_abduct_peak;        // Peak hip abduction for lateral step (~0.25 rad)
    float knee_flex_peak;         // Knee flex for ground clearance (~0.35 rad, less than walk)
    float ankle_dorsi;            // Ankle dorsiflexion during lift (~0.1 rad)
    float arm_lateral_balance;    // Lateral arm balance (~0.15 rad shoulder abduction)
    float spine_lateral_lean;     // Spine lateral lean toward movement (~0.05 rad)
    float spine_flex_bounce;      // Vertical bounce via spine flex (~0.03 rad)
    float trailing_scale;         // Scale for trailing leg amplitude (~0.5)
    float swing_ms;               // Swing duration
    float contact_ms;             // Contact/weight-accept duration
};

inline const StrafeStepProfile& get_strafe_reference_profile() {
    static const StrafeStepProfile profile = {
        0.25f,   // hip_abduct_peak: lateral reach
        0.35f,   // knee_flex_peak: ground clearance (less than walk's 0.7)
        0.10f,   // ankle_dorsi: slight toe-up during lift
        0.15f,   // arm_lateral_balance: arms help balance
        0.05f,   // spine_lateral_lean: body leans into strafe
        0.03f,   // spine_flex_bounce: subtle vertical bounce
        0.5f,    // trailing_scale: trailing step is half the amplitude
        400.0f,  // swing_ms (same timing as walk)
        200.0f,  // contact_ms
    };
    return profile;
}

inline FKAnimationClip generate_strafe_step_clip(
    const StrafeStepProfile& p,
    const BodyMap& active, const BodyMap& passive,
    float strafe_dir)  // +1 = rightward, -1 = leftward
{
    FKAnimationClip clip;
    clip.name = "fk_strafe_step";
    clip.loops = false;

    const float s = active.flex_sign;  // +1 right, -1 left

    // Leading leg gets full amplitude, trailing gets reduced
    bool is_leading = (strafe_dir * s > 0);
    float amplitude = is_leading ? 1.0f : p.trailing_scale;

    // Abduction direction: strafe_dir * s makes both sides step in strafe direction
    // (see sign convention docs in WalkStepProfile)
    float abduct_sign = strafe_dir;

    float t = 0.0f;

    // --- KF0: Neutral double-support ---
    {
        RotationPose pose;
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, 0.0f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.abduct(active.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        pose.abduct(passive.hip, 0.0f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(passive.shoulder, 0.0f);
        pose.abduct(active.shoulder, 0.0f);
        pose.abduct(passive.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        pose.abduct("lower_spine", 0.0f);
        pose.flex("lower_spine", p.spine_flex_bounce * 0.5f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(t, pose);
    }

    // --- KF1: Active leg beginning to lift laterally ---
    t = p.swing_ms * 0.3f;
    {
        RotationPose pose;
        // Active leg: lifting, beginning abduction
        pose.abduct(active.hip, s * abduct_sign * p.hip_abduct_peak * 0.3f * amplitude);
        pose.flex(active.hip, 0.0f);  // minimal sagittal (lateral step)
        pose.flex(active.knee, p.knee_flex_peak * 0.4f * amplitude);
        pose.flex(active.ankle, s * p.ankle_dorsi * 0.5f);
        pose.flex(active.toe, 0.0f);
        // Passive leg: weight-bearing, slight stabilization
        pose.abduct(passive.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.05f);  // slight bend for stability
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: beginning lateral balance
        pose.abduct(active.shoulder, s * abduct_sign * p.arm_lateral_balance * 0.3f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.abduct(passive.shoulder, -s * abduct_sign * p.arm_lateral_balance * 0.2f);
        pose.flex(passive.shoulder, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        // Spine: beginning lean toward movement
        pose.abduct("lower_spine", abduct_sign * p.spine_lateral_lean * 0.3f);
        pose.flex("lower_spine", 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(t, pose);
    }

    // --- KF2: Peak lateral reach ---
    t = p.swing_ms * 0.7f;
    {
        RotationPose pose;
        // Active leg: peak abduction
        pose.abduct(active.hip, s * abduct_sign * p.hip_abduct_peak * amplitude);
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, p.knee_flex_peak * amplitude);
        pose.flex(active.ankle, s * p.ankle_dorsi);
        pose.flex(active.toe, 0.0f);
        // Passive leg: stance
        pose.abduct(passive.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.08f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: peak lateral balance
        pose.abduct(active.shoulder, s * abduct_sign * p.arm_lateral_balance);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(active.elbow, 0.1f);
        pose.abduct(passive.shoulder, -s * abduct_sign * p.arm_lateral_balance * 0.5f);
        pose.flex(passive.shoulder, 0.0f);
        pose.flex(passive.elbow, 0.05f);
        // Spine: peak lean + bounce (CoM high at mid-step)
        pose.abduct("lower_spine", abduct_sign * p.spine_lateral_lean);
        pose.flex("lower_spine", -p.spine_flex_bounce * 0.5f);  // extension = high
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(t, pose);
    }

    // --- KF3: Active foot landing ---
    t = p.swing_ms;
    {
        RotationPose pose;
        // Active leg: extending for contact
        pose.abduct(active.hip, s * abduct_sign * p.hip_abduct_peak * 0.4f * amplitude);
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, p.knee_flex_peak * 0.1f * amplitude);
        pose.flex(active.ankle, s * p.ankle_dorsi * 0.3f);
        pose.flex(active.toe, 0.0f);
        // Passive leg: releasing
        pose.abduct(passive.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.03f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: decelerating
        pose.abduct(active.shoulder, s * abduct_sign * p.arm_lateral_balance * 0.4f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(active.elbow, 0.05f);
        pose.abduct(passive.shoulder, -s * abduct_sign * p.arm_lateral_balance * 0.2f);
        pose.flex(passive.shoulder, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        // Spine: returning from lean
        pose.abduct("lower_spine", abduct_sign * p.spine_lateral_lean * 0.3f);
        pose.flex("lower_spine", p.spine_flex_bounce * 0.3f);  // flexing = low
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(t, pose);
    }

    // --- KF4: Weight acceptance (back to neutral) ---
    t = p.swing_ms + p.contact_ms;
    {
        RotationPose pose;
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, 0.0f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.abduct(active.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        pose.abduct(passive.hip, 0.0f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(passive.shoulder, 0.0f);
        pose.abduct(active.shoulder, 0.0f);
        pose.abduct(passive.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        pose.abduct("lower_spine", 0.0f);
        pose.flex("lower_spine", p.spine_flex_bounce * 0.5f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(t, pose);
    }

    return clip;
}

// Public API: Side-generic strafe step (for strafing rightward)
inline FKAnimationClip create_fk_strafe_step(Side active_side, float strafe_dir) {
    const StrafeStepProfile& profile = get_strafe_reference_profile();
    return generate_strafe_step_clip(profile, body(active_side), body(opposite(active_side)), strafe_dir);
}

// ============================================================================
// TURN-IN-PLACE CLIP
// ============================================================================
// Rotational stepping when body turns without translational movement.
// One foot is the pivot (plant), the other lifts and arcs to a new position.
//
// Biomechanics:
// - Plant foot stays grounded, bears full weight
// - Swing foot lifts slightly, arcs around pivot via hip twist + abduction
// - Spine leads the turn with slight twist
// - Arms provide rotational balance
//
// turn_dir: +1 = turning right (CW), -1 = turning left (CCW)
// active leg = the swing leg (lifts and repositions)
// passive leg = the pivot leg (stays planted)
//
// Keyframes (5):
//   KF0: Neutral stance
//   KF1: Weight shift to pivot leg, swing leg lifting
//   KF2: Peak lift, swing leg mid-arc
//   KF3: Swing leg landing in new position
//   KF4: Weight acceptance (back to neutral)
// ============================================================================

struct TurnStepProfile {
    float hip_twist_peak;         // Hip internal/external rotation for arc (~0.15 rad)
    float hip_abduct_peak;        // Hip abduction during swing for clearance (~0.10 rad)
    float hip_flex_peak;          // Small forward flex to lift foot (~0.10 rad)
    float knee_flex_peak;         // Knee flex for ground clearance (~0.25 rad)
    float spine_twist;            // Spine leads the turn (~0.08 rad)
    float arm_balance;            // Arms counter-rotate for balance (~0.10 rad)
    float swing_ms;               // Swing duration
    float contact_ms;             // Contact/weight-accept duration
};

inline const TurnStepProfile& get_turn_reference_profile() {
    static const TurnStepProfile profile = {
        0.15f,   // hip_twist_peak: arc motion via internal rotation
        0.10f,   // hip_abduct_peak: slight lateral lift
        0.10f,   // hip_flex_peak: small forward flex for clearance
        0.25f,   // knee_flex_peak: less than walk (small step)
        0.08f,   // spine_twist: leads the turn
        0.10f,   // arm_balance: counter-rotation
        350.0f,  // swing_ms (slightly faster than walk)
        150.0f,  // contact_ms
    };
    return profile;
}

inline FKAnimationClip generate_turn_step_clip(
    const TurnStepProfile& p,
    const BodyMap& active, const BodyMap& passive,
    float turn_dir)  // +1 = CW (turning right), -1 = CCW (turning left)
{
    FKAnimationClip clip;
    clip.name = "fk_turn_step";
    clip.loops = false;

    const float s = active.flex_sign;

    float t = 0.0f;

    // --- KF0: Neutral stance ---
    {
        RotationPose pose;
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, 0.0f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.twist(active.hip, 0.0f);
        pose.abduct(active.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(passive.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(t, pose);
    }

    // --- KF1: Weight shift, swing leg beginning to lift ---
    t = p.swing_ms * 0.3f;
    {
        RotationPose pose;
        // Active leg (swing): lifting, beginning arc
        pose.flex(active.hip, s * p.hip_flex_peak * 0.3f);
        pose.flex(active.knee, p.knee_flex_peak * 0.4f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.twist(active.hip, s * turn_dir * p.hip_twist_peak * 0.2f);
        pose.abduct(active.hip, s * p.hip_abduct_peak * 0.3f);
        // Passive leg (pivot): planted, slight flex for stability
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.05f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: beginning counter-rotation
        pose.flex(active.shoulder, s * turn_dir * p.arm_balance * 0.3f);
        pose.flex(passive.shoulder, -s * turn_dir * p.arm_balance * 0.2f);
        pose.flex(active.elbow, 0.05f);
        pose.flex(passive.elbow, 0.03f);
        // Spine: leading the turn
        pose.twist("lower_spine", turn_dir * p.spine_twist * 0.4f);
        pose.twist("upper_spine", turn_dir * p.spine_twist * 0.3f);
        clip.add_keyframe(t, pose);
    }

    // --- KF2: Peak lift, swing leg mid-arc ---
    t = p.swing_ms * 0.7f;
    {
        RotationPose pose;
        // Active leg: peak of arc
        pose.flex(active.hip, s * p.hip_flex_peak);
        pose.flex(active.knee, p.knee_flex_peak);
        pose.flex(active.ankle, s * 0.05f);  // slight dorsiflex
        pose.flex(active.toe, 0.0f);
        pose.twist(active.hip, s * turn_dir * p.hip_twist_peak);
        pose.abduct(active.hip, s * p.hip_abduct_peak);
        // Passive leg: planted, stable
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.08f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: peak counter-rotation
        pose.flex(active.shoulder, s * turn_dir * p.arm_balance);
        pose.flex(passive.shoulder, -s * turn_dir * p.arm_balance * 0.6f);
        pose.flex(active.elbow, 0.1f);
        pose.flex(passive.elbow, 0.05f);
        // Spine: peak twist
        pose.twist("lower_spine", turn_dir * p.spine_twist);
        pose.twist("upper_spine", turn_dir * p.spine_twist * 0.7f);
        clip.add_keyframe(t, pose);
    }

    // --- KF3: Swing foot landing ---
    t = p.swing_ms;
    {
        RotationPose pose;
        // Active leg: extending for contact in new position
        pose.flex(active.hip, s * p.hip_flex_peak * 0.3f);
        pose.flex(active.knee, p.knee_flex_peak * 0.1f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.twist(active.hip, s * turn_dir * p.hip_twist_peak * 0.4f);
        pose.abduct(active.hip, s * p.hip_abduct_peak * 0.3f);
        // Passive leg: relaxing
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.03f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        // Arms: decelerating
        pose.flex(active.shoulder, s * turn_dir * p.arm_balance * 0.3f);
        pose.flex(passive.shoulder, -s * turn_dir * p.arm_balance * 0.2f);
        pose.flex(active.elbow, 0.05f);
        pose.flex(passive.elbow, 0.02f);
        // Spine: unwinding
        pose.twist("lower_spine", turn_dir * p.spine_twist * 0.3f);
        pose.twist("upper_spine", turn_dir * p.spine_twist * 0.2f);
        clip.add_keyframe(t, pose);
    }

    // --- KF4: Weight acceptance (back to neutral) ---
    t = p.swing_ms + p.contact_ms;
    {
        RotationPose pose;
        pose.flex(active.hip, 0.0f);
        pose.flex(active.knee, 0.0f);
        pose.flex(active.ankle, 0.0f);
        pose.flex(active.toe, 0.0f);
        pose.twist(active.hip, 0.0f);
        pose.abduct(active.hip, 0.0f);
        pose.flex(passive.hip, 0.0f);
        pose.flex(passive.knee, 0.0f);
        pose.flex(passive.ankle, 0.0f);
        pose.flex(passive.toe, 0.0f);
        pose.flex(active.shoulder, 0.0f);
        pose.flex(passive.shoulder, 0.0f);
        pose.flex(active.elbow, 0.0f);
        pose.flex(passive.elbow, 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(t, pose);
    }

    return clip;
}

// Public API: Side-generic turn step
inline FKAnimationClip create_fk_turn_step(Side active_side, float turn_dir) {
    const TurnStepProfile& profile = get_turn_reference_profile();
    return generate_turn_step_clip(profile, body(active_side), body(opposite(active_side)), turn_dir);
}

// ============================================================================
// IDLE ANIMATION CLIP
// ============================================================================
// Subtle breathing and weight-shift loop for when the humanoid stands still.
// Real humans are never perfectly static — there's always breathing, micro-
// adjustments, and slight postural sway.
//
// Cycle: ~4 seconds (4000ms), loops continuously
// Keyframes (4, smooth sinusoidal feel):
//   KF0 (0ms):     Neutral exhale — spine slightly flexed, weight centered
//   KF1 (1000ms):  Inhale peak — spine extends slightly, chest rises
//   KF2 (2000ms):  Exhale — spine flexes back, weight shifts slightly right
//   KF3 (3000ms):  Second inhale — weight shifts slightly left
//   KF4 (4000ms):  Return to KF0 (loop point)
//
// Amplitudes are deliberately very small (0.01-0.03 rad) to read as
// "alive" without looking twitchy.
// ============================================================================

struct IdleProfile {
    float breath_flex;            // Spine flex oscillation for breathing (~0.02 rad)
    float weight_shift;           // Lateral pelvic sway (~0.015 rad)
    float shoulder_breathe;       // Shoulder rise with inhale (~0.01 rad)
    float knee_slight_bend;       // Standing knee not locked (~0.03 rad)
    float cycle_ms;               // Full breathing cycle (~4000 ms)
};

inline const IdleProfile& get_idle_reference_profile() {
    static const IdleProfile profile = {
        0.050f,    // breath_flex: spine flex/extend for breathing (visible oscillation)
        0.020f,    // weight_shift: gentle lateral sway
        0.015f,    // shoulder_breathe: shoulder rise on inhale
        0.035f,    // knee_slight_bend: standing knees slightly flexed (natural)
        4000.0f,   // cycle_ms: 4 second breathing cycle
    };
    return profile;
}

inline FKAnimationClip generate_idle_clip(const IdleProfile& p) {
    FKAnimationClip clip;
    clip.name = "fk_idle";
    clip.loops = true;

    float quarter = p.cycle_ms * 0.25f;

    // --- KF0 (0ms): Neutral exhale — slight forward flex, weight centered ---
    {
        RotationPose pose;
        // Legs: slight knee bend (standing, not locked)
        pose.flex("right_knee", p.knee_slight_bend);
        pose.flex("left_knee", p.knee_slight_bend);
        pose.flex("right_hip", 0.0f);
        pose.flex("left_hip", 0.0f);
        pose.flex("right_ankle", 0.0f);
        pose.flex("left_ankle", 0.0f);
        // Arms: relaxed at sides
        pose.flex("right_shoulder", 0.0f);
        pose.flex("left_shoulder", 0.0f);
        pose.flex("right_elbow", 0.05f);  // slight natural elbow bend
        pose.flex("left_elbow", -0.05f); // negated: left elbow axis is (-X)
        // Spine: slight exhale flex (both segments for visible chest rise)
        pose.flex("lower_spine", p.breath_flex * 0.5f);
        pose.flex("upper_spine", p.breath_flex * 0.5f);
        pose.abduct("lower_spine", 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(0.0f, pose);
    }

    // --- KF1 (1000ms): Inhale peak — spine extends, chest rises ---
    {
        RotationPose pose;
        pose.flex("right_knee", p.knee_slight_bend);
        pose.flex("left_knee", p.knee_slight_bend);
        pose.flex("right_hip", 0.0f);
        pose.flex("left_hip", 0.0f);
        pose.flex("right_ankle", 0.0f);
        pose.flex("left_ankle", 0.0f);
        // Arms: very slight shoulder lift with inhale
        pose.flex("right_shoulder", -p.shoulder_breathe);
        pose.flex("left_shoulder", -p.shoulder_breathe);
        pose.flex("right_elbow", 0.05f);
        pose.flex("left_elbow", -0.05f);
        // Spine: extension (inhale lifts chest)
        pose.flex("lower_spine", -p.breath_flex * 0.5f);
        pose.flex("upper_spine", -p.breath_flex * 0.5f);
        pose.abduct("lower_spine", p.weight_shift * 0.3f);  // slight rightward
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(quarter, pose);
    }

    // --- KF2 (2000ms): Exhale — spine flexes, weight shifts right ---
    {
        RotationPose pose;
        pose.flex("right_knee", p.knee_slight_bend * 0.8f);  // slight asymmetry
        pose.flex("left_knee", p.knee_slight_bend * 1.2f);
        pose.flex("right_hip", 0.0f);
        pose.flex("left_hip", 0.0f);
        pose.flex("right_ankle", 0.0f);
        pose.flex("left_ankle", 0.0f);
        pose.flex("right_shoulder", 0.0f);
        pose.flex("left_shoulder", 0.0f);
        pose.flex("right_elbow", 0.05f);
        pose.flex("left_elbow", -0.05f);
        // Spine: exhale flex + weight shifted right
        pose.flex("lower_spine", p.breath_flex * 0.5f);
        pose.flex("upper_spine", p.breath_flex * 0.5f);
        pose.abduct("lower_spine", p.weight_shift);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(quarter * 2.0f, pose);
    }

    // --- KF3 (3000ms): Second inhale — weight shifts left ---
    {
        RotationPose pose;
        pose.flex("right_knee", p.knee_slight_bend * 1.2f);
        pose.flex("left_knee", p.knee_slight_bend * 0.8f);
        pose.flex("right_hip", 0.0f);
        pose.flex("left_hip", 0.0f);
        pose.flex("right_ankle", 0.0f);
        pose.flex("left_ankle", 0.0f);
        pose.flex("right_shoulder", -p.shoulder_breathe);
        pose.flex("left_shoulder", -p.shoulder_breathe);
        pose.flex("right_elbow", 0.05f);
        pose.flex("left_elbow", -0.05f);
        // Spine: extension + weight shifted left
        pose.flex("lower_spine", -p.breath_flex * 0.5f);
        pose.flex("upper_spine", -p.breath_flex * 0.5f);
        pose.abduct("lower_spine", -p.weight_shift);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(quarter * 3.0f, pose);
    }

    // --- KF4 (4000ms): Return to KF0 (loop point) ---
    {
        RotationPose pose;
        pose.flex("right_knee", p.knee_slight_bend);
        pose.flex("left_knee", p.knee_slight_bend);
        pose.flex("right_hip", 0.0f);
        pose.flex("left_hip", 0.0f);
        pose.flex("right_ankle", 0.0f);
        pose.flex("left_ankle", 0.0f);
        pose.flex("right_shoulder", 0.0f);
        pose.flex("left_shoulder", 0.0f);
        pose.flex("right_elbow", 0.05f);
        pose.flex("left_elbow", -0.05f);
        pose.flex("lower_spine", p.breath_flex * 0.5f);
        pose.flex("upper_spine", p.breath_flex * 0.5f);
        pose.abduct("lower_spine", 0.0f);
        pose.twist("lower_spine", 0.0f);
        pose.twist("upper_spine", 0.0f);
        clip.add_keyframe(p.cycle_ms, pose);
    }

    return clip;
}

// Public API: Create idle breathing/weight-shift clip
inline FKAnimationClip create_fk_idle_clip() {
    const IdleProfile& profile = get_idle_reference_profile();
    return generate_idle_clip(profile);
}

// ============================================================================
// RUN STEP CLIP
// ============================================================================
// Biomechanically distinct from walk: running has a flight phase (both feet
// off ground), higher knee drive, more arm pump, forward lean, longer stride.
//
// Key differences from walk:
//   - No double-support phase (walk has both feet on ground)
//   - Higher hip flexion (0.60 vs 0.40 rad)
//   - More knee drive (0.85 vs 0.70 rad)
//   - Bigger arm pump (0.50 vs 0.30 rad shoulder flex)
//   - More elbow flexion (0.45 vs 0.20 rad — arms held higher)
//   - Forward lean (0.06 rad constant spine flex)
//   - More vertical bounce (0.06 vs 0.04 rad)
//   - Shorter step timing (300+100ms vs 400+200ms)
//   - Longer stride (0.85m vs 0.65m)
//
// Uses the same WalkStepProfile struct — just different parameter values.
// The generate_walk_step_clip() function works for both walk and run
// since it's parameterized by the profile.
// ============================================================================

inline const WalkStepProfile& get_run_reference_profile() {
    static const WalkStepProfile profile = {
        1.0f,  // skill_level

        // Active leg — higher amplitude than walk
        0.60f,   // hip_flex_peak: ~34 degrees (vs walk 23)
        0.85f,   // knee_flex_peak: ~49 degrees (vs walk 40) — more drive
        0.20f,   // ankle_dorsi_swing: more toe clearance
        -0.30f,  // ankle_push_off: stronger push-off (vs walk -0.20)
        -0.40f,  // toe_push_off: more forceful (vs walk -0.35)
        0.25f,   // toe_dorsi_swing: more clearance (vs walk 0.20)

        // Passive leg — more extension in stance
        -0.25f,  // stance_hip_extend: bigger extension (vs walk -0.15)
        0.15f,   // stance_knee_flex: more shock absorption (vs walk 0.10)

        // Arms — pumping action, held higher
        0.50f,   // arm_shoulder_flex: vigorous arm pump (vs walk 0.30)
        0.45f,   // arm_elbow_flex: arms bent ~90 degrees (vs walk 0.20)
        0.00f,   // arm_spread_compensation: disabled (same as walk)

        // Spine — forward lean + twist
        0.08f,   // spine_twist: more counter-rotation (vs walk 0.05)

        // Hip sway & bounce — C2: increased for more pronounced running feel
        0.12f,   // pelvic_tilt: more lateral motion (was 0.10, vs walk 0.09)
        0.08f,   // pelvic_bounce: more vertical oscillation (was 0.06, vs walk 0.06)

        // Timing — faster cadence
        300.0f,  // swing_ms: shorter swing phase (vs walk 400)
        100.0f,  // contact_ms: brief ground contact (vs walk 200)

        // Stride — longer
        0.85f,   // stride_length: bigger steps (vs walk 0.65m)

        // C3: Head stabilization — less than walk (runners bob more)
        0.3f     // head_stabilize_factor: 30% counter-motion (vs walk 40%)
    };
    return profile;
}

// Public API: Side-generic run step
inline FKAnimationClip create_fk_run_step(Side side) {
    const WalkStepProfile& profile = get_run_reference_profile();
    float spine_sign = (side == Side::RIGHT) ? -1.0f : 1.0f;
    return generate_walk_step_clip(profile, body(side), body(opposite(side)), spine_sign);
}

// blend_rotation_poses() is defined in animation_types.h (engine layer)
// and available through the #include at the top of this file.

#endif // LOGOSPHERE_ANIMATION_PRIMITIVES_H
