#ifndef LOGOSPHERE_KG_ONTOLOGY_VALIDATOR_H
#define LOGOSPHERE_KG_ONTOLOGY_VALIDATOR_H

// Ontology validator — gate every LLM-proposed KG mutation against
// the ontology schema BEFORE it touches the KG. The ontology is
// the safety envelope: anything the schema declares is legal,
// anything it doesn't is rejected. Adding a new entity type or
// property to a game's YAML automatically expands the LLM's
// authorial range with zero validator change.
//
// Engine-side helper. Generic.
//
// Symbolic refs (@aliases) MUST be resolved by the caller before
// validation — the validator can't reach a game-specific symbol
// table. Unresolved symbolic refs are reported as a validation
// failure so the caller knows to expand them upstream.
//
// What's checked today (Phase C.2):
//   CreateEntity:  type exists in the registry; type is concrete.
//   DestroyEntity: target exists in the KG.
//   SetProperty:   target exists; property is declared on the
//                  target's type (or any ancestor).
//   SetRelation:   from/to exist; relation type exists; the
//                  relation accepts the from/to types.
//
// Coming in C.3:
//   Range / type-coercion checks for SetProperty values once
//   PropertyDef carries min/max/step + tighter value-type kinds.

#include "logosphere/kg/kg_ops.h"

#include <string>
#include <unordered_set>

namespace kg {

class KGModule;
class OntologyRegistry;

struct ValidationResult {
    bool        ok = true;
    std::string reason;   // human-readable; empty when ok
};

enum class MutationAuthority {
    Runtime,
    SeedIngestion,
};

struct ValidationOptions {
    MutationAuthority authority = MutationAuthority::Runtime;
    // A trusted seed may finish assembling entities it created in the same
    // atomic batch. It may never rewrite sealed entities from an earlier load.
    std::unordered_set<EntityID> constructing;
};

ValidationResult validate_kg_op(const KGOp& op,
                                const KGModule& kg,
                                const OntologyRegistry& ont,
                                const ValidationOptions& options = {});

}  // namespace kg

#endif  // LOGOSPHERE_KG_ONTOLOGY_VALIDATOR_H
