// Round status for a 1-vs-1 light cycle match.
//
// Status is a pure function of the two cycles' KG state. The game
// loop calls this after each tick to decide whether to keep going.
// Headless-testable: no engine, just KG read.

#pragma once

#include "logosphere/kg/kg_module.h"

namespace logotron {

enum class RoundStatus {
    IN_PROGRESS,  // both cycles still RIDING
    PLAYER_WON,   // AI crashed, player still riding
    AI_WON,       // player crashed, AI still riding
    DRAW,         // both crashed (e.g., same tick, head-on, or
                  // player and AI both ran into walls together)
};

const char* round_status_name(RoundStatus s);

RoundStatus check_round_status(const kg::KGModule& kg,
                               kg::EntityID player_entity,
                               kg::EntityID ai_entity);

} // namespace logotron
