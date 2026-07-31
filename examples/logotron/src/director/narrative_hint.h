#ifndef LOGOTRON_DIRECTOR_NARRATIVE_HINT_H
#define LOGOTRON_DIRECTOR_NARRATIVE_HINT_H

// Causation hint pipe — turn the KG-stamped crash metadata into
// a one-line narrative the LLM can reason about. Wires the dead
// `GameState.narrative_hint` slot to live data.
//
// Logotron-side (logotron's collision causes are game-specific).
// arena.cpp already stamps `crash_cause`, `crash_x`, `crash_y`,
// `crash_hit_entity`, `crash_hit_age` on the AI entity when it
// crashes; this helper reads them back and formats.
//
// Format examples (one liners):
//   "AI hit own sealed trail at (10.7, 23.6); trail was 12.3s old"
//   "AI hit player's sealed trail at (4.5, 8.0); trail was 5.1s old"
//   "AI rammed director wall at (3.5, 12.0)"
//   "AI rammed player head-on at (15.2, 18.7)"
//   "AI ran out of arena at (40.0, 12.5)"
//   ""  (empty when AI is alive or no crash data is present)
//
// Also marks director-origin walls explicitly (the LLM gains
// awareness that "AI died on a wall I spawned last round" so it
// can credit / build on its own moves).

#include "logosphere/kg/kg_types.h"

#include <string>

namespace kg { class KGModule; }

namespace logotron::director {

// Build the narrative hint for the AI's most recent death. Returns
// empty string if the AI entity has no crash metadata stamped (e.g.
// before the first death, or after the metadata has been wiped on
// respawn).
std::string make_narrative_hint(const kg::KGModule& kg,
                                kg::EntityID ai_entity);

}  // namespace logotron::director

#endif  // LOGOTRON_DIRECTOR_NARRATIVE_HINT_H
