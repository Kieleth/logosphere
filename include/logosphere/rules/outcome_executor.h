#ifndef LOGOSPHERE_RULES_OUTCOME_EXECUTOR_H
#define LOGOSPHERE_RULES_OUTCOME_EXECUTOR_H

// Typed rulebook outcome execution.
//
// Concrete handlers read rule data through a const KG view and append an
// OutcomePlan. They cannot mutate world state. The executor resolves
// sequences and choices, keeps dice private, applies the complete KG-op batch
// atomically, then publishes typed procedure results.

#include "logosphere/kg/kg_ops.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kg { class KGModule; }
namespace logosphere::dice { class DiceService; }

namespace logosphere::rules {

enum class OutcomeStatus { APPLIED, PENDING_CHOICE, FAILED };

struct TableRollRequest {
    kg::EntityID table = kg::INVALID_ENTITY;
    int roll_count = 0;
};

struct ProcedureSignal {
    std::string outcome_type;
    kg::EntityID outcome = kg::INVALID_ENTITY;
    kg::EntityID target = kg::INVALID_ENTITY;
};

struct ChoiceOption {
    int option_index = 0;
    std::string label;
    kg::EntityID outcome = kg::INVALID_ENTITY;
};

struct PendingChoice {
    kg::EntityID choice = kg::INVALID_ENTITY;
    std::string authority;
    std::vector<ChoiceOption> options;
};

struct OutcomeSelection {
    kg::EntityID choice = kg::INVALID_ENTITY;
    int option_index = 0;
};

struct OutcomeContext {
    kg::EntityID target = kg::INVALID_ENTITY;
    std::string dice_stream;
    std::string purpose;
};

struct OutcomePlan {
    std::vector<kg::KGOp> ops;
    std::vector<TableRollRequest> table_roll_requests;
    std::vector<ProcedureSignal> procedure_signals;
    std::vector<uint64_t> roll_ids;
};

struct OutcomeHandlerContext {
    kg::EntityID outcome = kg::INVALID_ENTITY;
    kg::EntityID target = kg::INVALID_ENTITY;
    std::string outcome_type;
    const kg::KGModule& kg;
    logosphere::dice::DiceService& dice;
    const std::string& dice_stream;
    const std::string& purpose;
};

using OutcomeHandler = std::function<bool(
    const OutcomeHandlerContext&, OutcomePlan&, std::string&)>;

struct OutcomeResult {
    OutcomeStatus status = OutcomeStatus::FAILED;
    std::string error;
    size_t ops_applied = 0;
    std::vector<TableRollRequest> table_roll_requests;
    std::vector<ProcedureSignal> procedure_signals;
    std::vector<uint64_t> roll_ids;
    std::optional<PendingChoice> pending_choice;
};

class OutcomeExecutor {
public:
    OutcomeExecutor(kg::KGModule& kg,
                    logosphere::dice::DiceService& dice);

    bool register_handler(const std::string& concrete_type,
                          OutcomeHandler handler, std::string& error);

    OutcomeResult apply(
        kg::EntityID root, const OutcomeContext& context,
        const std::vector<OutcomeSelection>& selections = {});

private:
    struct Planner;

    kg::KGModule& kg_;
    logosphere::dice::DiceService& dice_;
    std::unordered_map<std::string, OutcomeHandler> handlers_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_OUTCOME_EXECUTOR_H
