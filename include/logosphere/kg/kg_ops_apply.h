#ifndef LOGOSPHERE_KG_KG_OPS_APPLY_H
#define LOGOSPHERE_KG_KG_OPS_APPLY_H

// Apply a validated KGOp to the KG. Engine-side helper. Generic
// — every game's KGOp apply path goes through here, no game-
// specific knowledge required.
//
// Contract: the caller has already (a) resolved any symbolic
// EntityRefs to numeric ids and (b) run the op through
// validate_kg_op. apply_kg_op trusts both. If validation was
// skipped, apply may produce KG state the ontology rejects on
// the next read; KGCore's own write checks will warn but the op
// will have already happened. Always validate first.

#include "logosphere/kg/kg_ops.h"

#include <string>

namespace kg {

class KGModule;

struct ApplyResult {
    bool        ok = true;
    std::string reason;        // empty when ok
    EntityID    created_id = INVALID_ENTITY;  // for create_entity
};

ApplyResult apply_kg_op(const KGOp& op, KGModule& kg);

}  // namespace kg

#endif  // LOGOSPHERE_KG_KG_OPS_APPLY_H
