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

namespace kg {

class KGModule;
class OntologyRegistry;

struct ValidationResult {
    bool        ok = true;
    std::string reason;   // human-readable; empty when ok
};

ValidationResult validate_kg_op(const KGOp& op,
                                const KGModule& kg,
                                const OntologyRegistry& ont);

// The property-write check itself, shared by every door that writes a
// property: the KGOp path above (set_property + create_entity's
// per-property checks) and the engine-side gate in KGCore::setProperty.
// One implementation, so the LLM loop and the engine cannot drift.
//
// Checks: property is declared on entity_type or an ancestor; the value
// coerces to the declared value_type (string/integer/float/boolean —
// enum and entity_ref pass through); numeric values respect schema
// min/max. The caller supplies the entity type; type resolution is the
// caller's problem (KGCore has it in hand, the KGOp path resolves refs).
ValidationResult validate_property_write(const OntologyRegistry& ont,
                                         const std::string& entity_type,
                                         const std::string& property,
                                         const std::string& value);

}  // namespace kg

#endif  // LOGOSPHERE_KG_ONTOLOGY_VALIDATOR_H
