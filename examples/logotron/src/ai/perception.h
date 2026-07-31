// perception.h — the ONE file allowed to read the KG on behalf of
// the AI driver. See GAME_DESIGN.md §16 for the contract.
//
// All tactic files in ai/tactics/ must NOT include kg_module.h;
// they operate exclusively on PerceivedWorld.

#pragma once

#include <vector>
#include "ai/perceived_world.h"
#include "logosphere/kg/kg_module.h"

namespace logotron::ai {

// Build a snapshot of what the given cycle's rider can perceive,
// given its head state, FOV (radians), range (meters), and the
// arena bounds.
//
// Arena bounds are passed in (not pulled from KG) because the
// arena is a static game-layer fact; no reason to go through KG
// for them.
// rival_types: which cycle types register as the "opponent" in the
// perceived world (nearest visible wins). Enemy Programs hunt
// everyone (default); an ALLY passes {"AICycle"} so it never targets
// the User it rides for.
PerceivedWorld build_perceived_world(
    const kg::KGModule& kg,
    kg::EntityID self_cycle,
    const HeadState& head,
    float cone_fov_rad,
    float cone_range,
    float arena_w,
    float arena_h,
    const std::vector<const char*>& rival_types =
        {"PlayerCycle", "AllyCycle", "AICycle", "Cycle"});

} // namespace logotron::ai
