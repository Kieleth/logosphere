#pragma once

#include "logosphere/kg/kg_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kg {

class KGModule;

struct IngestionLedgerReport {
    bool ok = false;
    std::string error;
    std::size_t coverage_count = 0;
    std::size_t claim_count = 0;
    std::size_t decision_count = 0;
};

// Reconcile one complete mechanical enumeration against its enduring
// coverage and claim records. Current outcomes are derived from contiguous
// append-only decision histories. The first violation fails loudly.
IngestionLedgerReport reconcile_ingestion_ledger(
    const KGModule& world,
    const std::vector<EntityID>& enumerated_targets);

}  // namespace kg
