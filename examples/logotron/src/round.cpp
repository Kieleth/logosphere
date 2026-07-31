#include "round.h"

#include "arena.h"
#include "cycle.h"

namespace logotron {

const char* round_status_name(RoundStatus s) {
    switch (s) {
        case RoundStatus::IN_PROGRESS: return "IN_PROGRESS";
        case RoundStatus::PLAYER_WON:  return "PLAYER_WON";
        case RoundStatus::AI_WON:      return "AI_WON";
        case RoundStatus::DRAW:        return "DRAW";
    }
    return "?";
}

RoundStatus check_round_status(const kg::KGModule& kg,
                               kg::EntityID player_entity,
                               kg::EntityID ai_entity) {
    Cycle p = read_cycle(kg, player_entity);
    Cycle a = read_cycle(kg, ai_entity);
    bool player_alive = (p.state == CycleState::RIDING);
    bool ai_alive     = (a.state == CycleState::RIDING);
    if (player_alive && ai_alive)   return RoundStatus::IN_PROGRESS;
    if (player_alive && !ai_alive)  return RoundStatus::PLAYER_WON;
    if (!player_alive && ai_alive)  return RoundStatus::AI_WON;
    return RoundStatus::DRAW;
}

} // namespace logotron
