#ifndef LOGOSPHERE_KG_KG_OPS_TRANSACTION_H
#define LOGOSPHERE_KG_KG_OPS_TRANSACTION_H

// Atomic application for a complete, additive KG-op plan.
//
// Symbolic aliases bound by create_entity operations resolve in later
// operations. Every operation validates against the world's registry. A
// failure restores properties, relations, and created entities, publishes no
// mutation events, and exposes no partial report state. Destruction and
// cinematics are rejected because this transaction has no reversible form for
// them yet.

#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/ontology_validator.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace kg {

class KGModule;

struct KGOpBatchReport {
    bool ok = true;
    int failed_op = -1;
    std::string error;
    size_t ops_applied = 0;
    std::unordered_map<std::string, EntityID> bindings;
    std::vector<EntityID> created_ids;
};

bool apply_kg_ops_atomically(const std::vector<KGOp>& ops, KGModule& kg,
                             KGOpBatchReport& report,
                             MutationAuthority authority =
                                 MutationAuthority::Runtime);

}  // namespace kg

#endif  // LOGOSPHERE_KG_KG_OPS_TRANSACTION_H
