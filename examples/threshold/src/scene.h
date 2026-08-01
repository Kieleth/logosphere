// Shared scene construction for the threshold level -- what the windowed
// app (threshold_app.cpp) builds. tools/verify.cpp does NOT use this: it
// needs to stay Engine/GLFW/Metal-free (see its own file comment for why),
// so it builds an equivalent minimal scene directly against the raw
// ParticleSystem/PhysicsSystem/HumanoidLocomotion headless APIs instead.
// This file is the one source of truth for the real level's geometry.
#pragma once

#include "logosphere/kg/kg_types.h"

#include <cstdint>

class Engine;

namespace threshold {

// Builds the strata-floor level (ground, the gap, the exit region).
void build_level(Engine& engine);

// Spawns the loose floor tile at level.h's kLooseTileX as a single
// create_floor_grid() tile, is_at_rest (static) until woken. Returns its
// particle id -- callers wake it (PhysicsSystem::wake_particle) once the
// Prince gets close enough, see threshold_app.cpp's update_game.
int spawn_loose_tile(Engine& engine);

// Spawns a sensor-only spike hazard spanning the gap (level.h's kGapMinX
// to kGapMaxX) at the bottom of the pit, and registers the
// ParticleInteractionProfile that makes it one: it never rigid-contacts
// anything (collides_with = 0, so the Prince falls straight through it
// rather than landing on it) but declares a (negligible) medium, which is
// what makes the engine treat overlaps with it as a volume-trigger episode
// instead of silently dropping the filtered pair. Returns the profile id
// -- callers match it against VolumeEvent::medium_profile to know a
// filtered overlap was specifically the spikes (see threshold_app.cpp's
// update_hazards).
uint32_t spawn_spikes(Engine& engine);

struct SpawnedHumanoid {
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    int hips_id = -1;
    // Right arm, distal joints only -- what a sword prop needs to track
    // the hand and orient along the forearm (see spawn_sword). -1 if the
    // generated body has no right arm.
    int right_hand_id = -1;
    int right_forearm_id = -1;
};

// Spawns a physics-driven humanoid at (x, kFixedDepthY) and registers it
// with HumanoidLocomotion. reflexes_ms/grit_w follow the same physical
// parameterization examples/pop/src/world.cpp uses for its KG bodies.
SpawnedHumanoid spawn_humanoid(Engine& engine, float x,
                               float reflexes_ms = 210.0f, float grit_w = 520.0f);

// Spawns a single long thin box particle to render as the guard's sword.
// It's KINEMATIC (physics never moves it -- an external writer, here
// threshold_app.cpp's update_guard, owns its position/orientation every
// frame) and carries its own sensor-only, medium-free interaction profile
// so it can never rigid-contact or volume-trigger against anything else
// in the scene -- purely decorative, tracks the hand, nothing more.
// Local geometry: the blade's length runs along local +X: local point
// (blade_length/2, 0, 0) is the tip. Returns the particle id.
int spawn_sword(Engine& engine);

} // namespace threshold
