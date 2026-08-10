// Character generation: the book's own procedure, run against the KG.
//
// Design record: docs/RPG_MODULE.md. This is the first executable
// slice, and it is deliberately GAME-side code (examples/logovger),
// not engine code. The engine's rule executor does not exist yet;
// deriving its primitive vocabulary from a path that actually runs
// beats inventing one in advance. Every place this file wants a
// mechanism the engine does not have is marked ENGINE GAP.
//
// What it proves, or fails to:
//   - The rules come out of the KG. Targets, characteristics and
//     dice are read from seeded entities, never from constants in
//     this file. Change the seed, change the life.
//   - The dice come from the engine. Every roll is seeded, journaled
//     and citable by id; this code cannot assert a result.
//   - The result goes back into the KG through entity writes, so a
//     referee reading the graph sees the same life the player did.
//
// Basic on purpose: one career, the book's qualification / survival /
// advancement checks, skills from the service table, ageing, and a
// reenlistment decision. No commissions, no mishaps table, no
// benefits, no ageing crisis. Those are the next slices.

#ifndef LOGOVGER_CHARGEN_H
#define LOGOVGER_CHARGEN_H

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_module.h"

#include <string>
#include <vector>

namespace logovger {

// One thing that happened, in the order it happened. The timeline is
// the watchable artifact for a headless slice: a life you can read.
struct LifeEvent {
    int         term = 0;        // 0 = before service
    std::string what;            // "qualified", "survived", "gained Athletics"
    std::string detail;          // the book's terms: "Soc 6+", "2D6+1 = 8"
    uint64_t    roll_id = 0;     // 0 when nothing was rolled
};

struct CharacterSheet {
    kg::EntityID id = kg::INVALID_ENTITY;
    std::string  upp;
    int          strength = 0, dexterity = 0, endurance = 0;
    int          intelligence = 0, education = 0, social_standing = 0;
    int          age_years = 18;
    int          terms_served = 0;
    std::string  career;             // name of the career entity
    bool         qualified = false;
    std::vector<std::string> skills;  // names, in the order gained
    std::vector<LifeEvent>   life;
};

// What the runner needs from the world. The career is named, not
// passed as data: the runner looks it up in the KG, so a career the
// seed did not define cannot be played.
struct ChargenRequest {
    std::string career_name;
    uint64_t    seed = 0;          // the dice stream seed: same seed, same life
    int         max_terms = 4;     // the book allows 7; the slice stops earlier
};

// Runs the procedure. Returns false with `error` set when the world
// is missing something the book needs (a career, a check, a table) -
// missing rule data is a loud failure, never a default.
bool run_chargen(const ChargenRequest& request,
                 kg::KGModule& kg,
                 logosphere::dice::DiceService& dice,
                 CharacterSheet& out,
                 std::string& error);

// The life as a timeline, one line per event, with roll citations.
std::string format_life(const CharacterSheet& sheet);

}  // namespace logovger

#endif  // LOGOVGER_CHARGEN_H
