#ifndef LOGOTRON_DIRECTOR_SYMBOLIC_REFS_H
#define LOGOTRON_DIRECTOR_SYMBOLIC_REFS_H

// Symbolic refs — short stable names for the singletons the
// Director cares about (the player bike, the AI bike, the arena
// entity). The LLM sees these in its prompt and can use them in
// its responses without having to know the live numeric entity
// id (which changes every round when bikes respawn).
//
// Logotron-side. Each game picks its own symbolic vocabulary.
//
// Usage:
//   SymbolicRefs refs = build_symbolic_refs(player_e, {p1, p2}, arena_e);
//   prompt += refs.as_prompt_text();
//   ...
//   // Parsing a response that contains "@player_cycle":
//   auto eid = refs.resolve("@player_cycle");
//
// Phase B keeps the v0.9 menu mutation format, so the response
// parser doesn't yet consume `@aliases`. The Director's `thoughts`
// string starts referencing them once they're in the prompt
// (cheap win on its own). Phase C wires them through the new
// KG-op vocabulary.

#include <vector>
#include "logosphere/kg/kg_types.h"

#include <string>
#include <unordered_map>

namespace logotron::director {

struct SymbolicRefs {
    std::unordered_map<std::string, kg::EntityID> name_to_id;
    std::unordered_map<kg::EntityID, std::string> id_to_name;

    // Returns kg::INVALID_ENTITY if the name is unknown. Accepts
    // either "@player_cycle" or "player_cycle" (the @ is canonical
    // but optional).
    kg::EntityID resolve(const std::string& name) const;

    // Returns the alias for an id, or "" if no alias exists.
    // Useful when serialising responses back for the ledger HUD.
    std::string alias_for(kg::EntityID id) const;

    // One-line prompt-friendly summary, e.g.
    //   "Symbols: @player_cycle=7 @ai_cycle=11 @arena=3"
    // Empty when no aliases are defined.
    std::string as_prompt_text() const;
};

// Build the canonical Logotron symbol set. Skips entities that
// equal kg::INVALID_ENTITY so the map stays clean across the
// brief windows where the AI is between deaths.
SymbolicRefs build_symbolic_refs(kg::EntityID player_cycle,
                                 const std::vector<kg::EntityID>& programs,
                                 kg::EntityID arena,
                                 const std::vector<kg::EntityID>& allies = {});

}  // namespace logotron::director

#endif  // LOGOTRON_DIRECTOR_SYMBOLIC_REFS_H
