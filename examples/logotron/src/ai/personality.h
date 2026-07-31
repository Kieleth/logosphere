// Personality — a weighted blend of DRIVE tactics, a weighted
// blend of LOOK tactics, and a head-swivel rate. Phase 1 ships
// one default personality (a tuned blend that merely stays
// alive); Phase 2 will realize the four AIPersonality enum
// variants. Phase 3's Weirden Director writes personalities from
// LLM output.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/tactic.h"

namespace logotron::ai {

struct WeightedTactic {
    std::unique_ptr<Tactic> tactic;
    float weight = 1.0f;
};

struct WeightedLookTactic {
    std::unique_ptr<LookTactic> look;
    float weight = 1.0f;
};

struct Personality {
    std::string name;                            // human-readable
    std::vector<WeightedTactic>     drive;
    std::vector<WeightedLookTactic> look;
    float head_max_rate = 4.0f;                  // rad/s
};

// Default personality — moderate "try to stay alive and
// occasionally chase" blend. The four named variants below
// realize the AIPersonality enum from logotron.yaml; each tunes
// the same drive + look tactic blend differently.
Personality default_personality();

// Reckless. Heavy chase, lighter survival. Engages the User even
// at the cost of risky turns.
Personality aggressive_personality();

// Wall-hugging survivor. Dominant avoid-crash + avoid-opponent.
// Won't chase; tries to outlast the User.
Personality defensive_personality();

// Erratic. Negative MaintainDirection weight makes it prefer
// turning over committing; chaotic-fast head swivel.
Personality chaotic_personality();

// Player-tracker. Even heavier chase than aggressive. Will
// sacrifice survival probability to close the gap.
Personality pursuing_personality();

// Map an AIPersonality enum string (from the KG ai_personality
// slot — values "AGGRESSIVE" / "DEFENSIVE" / "CHAOTIC" / "PURSUING"
// per logotron.yaml) to its factory output. Unknown names fall
// back to default_personality(). Case-sensitive on purpose; the
// schema validator emits canonical UPPER form. The Weirden
// Director writes one of these via set_property mid-game.
Personality personality_from_name(const std::string& name);

} // namespace logotron::ai
