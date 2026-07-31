#ifndef KINEMATIC_ROOT_H
#define KINEMATIC_ROOT_H

// =============================================================================
// KinematicRoot — the particle whose world position anchors a skeleton.
// =============================================================================
// During any given frame, one particle on an animated entity is authoritative
// for its world position; the rest are derived from it through the joint
// chain. That anchor is the kinematic root.
//
// Examples:
//   - Humanoid walking, stance phase: root is the planted foot (FIXED_WORLD).
//     Hips derive from the foot via reverse-FK up the stance leg.
//   - Humanoid swing / air / idle: root is the hips (FOLLOW_VELOCITY).
//     Hips integrate from vx/vy/vz; everything else snaps relative.
//   - Serpent slithering: root is the head (FOLLOW_VELOCITY). Body segments
//     follow in a chain.
//   - Butterfly in flight: root is the thorax (FOLLOW_VELOCITY).
//   - A tree: root is the trunk base particle (FIXED_WORLD).
//
// Root transitions (e.g. heel-strike swapping stance feet) snap the new
// anchor to the root particle's current world position — preserving body
// pose across the transfer.
//
// This header is deliberately thin. The dynamics path consults `mode` and
// `particle_id` per frame; transitions are driven by game-logic / animation
// state machines. Engine provides the mechanism (root-aware integration +
// reverse-FK); games provide the policy (who roots when).
// =============================================================================

#include "math/mat4.h"  // Vec3

namespace logosphere {

enum class RootMode {
    // Root particle integrates its world position from its velocity each
    // frame. Everything else snaps relative. Matches the pre-refactor
    // hip-rooted default for humanoids (and head-rooted / thorax-rooted
    // default for serpents / butterflies).
    FOLLOW_VELOCITY,

    // Root particle is pinned at anchor_world. Its world position is held
    // constant across frames; the rest of the skeleton derives from it
    // via the joint chain (reverse-FK through the leg for humanoid stance).
    FIXED_WORLD,

    // Root follows an externally-driven path (ropes, rails, leader entity).
    // Not used yet — reserved for future attachment/constraint systems.
    FOLLOW_PATH,
};

struct KinematicRoot {
    unsigned int particle_id = 0;
    RootMode     mode        = RootMode::FOLLOW_VELOCITY;
    Vec3         anchor_world{0.0f, 0.0f, 0.0f};
    unsigned int previous_particle_id = 0;  // for transition continuity
};

} // namespace logosphere

#endif // KINEMATIC_ROOT_H
