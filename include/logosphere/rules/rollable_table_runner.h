#ifndef LOGOSPHERE_RULES_ROLLABLE_TABLE_RUNNER_H
#define LOGOSPHERE_RULES_ROLLABLE_TABLE_RUNNER_H

// Two-phase RollableTable execution.
//
// select() validates the complete table, records one dice fact, and returns
// the exact row and typed outcome selected by that fact. It deliberately does
// not apply the outcome. Callers may pass the returned outcome to
// OutcomeExecutor, suspend it for a choice, or retry it without rerolling.

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_types.h"

#include <optional>
#include <string>
#include <utility>

namespace kg { class KGModule; }

namespace logosphere::rules {

class RollableTableSelection {
public:
    RollableTableSelection(const RollableTableSelection&) = default;
    RollableTableSelection(RollableTableSelection&&) = default;
    RollableTableSelection& operator=(const RollableTableSelection&) = default;
    RollableTableSelection& operator=(RollableTableSelection&&) = default;

    kg::EntityID table() const { return table_; }
    kg::EntityID row() const { return row_; }
    kg::EntityID outcome() const { return outcome_; }
    const logosphere::dice::DiceRoll& roll() const { return roll_; }

private:
    friend class RollableTableRunner;
    RollableTableSelection(kg::EntityID selected_table,
                           kg::EntityID selected_row,
                           kg::EntityID selected_outcome,
                           logosphere::dice::DiceRoll selected_roll)
        : table_(selected_table),
          row_(selected_row),
          outcome_(selected_outcome),
          roll_(std::move(selected_roll)) {}

    kg::EntityID table_ = kg::INVALID_ENTITY;
    kg::EntityID row_ = kg::INVALID_ENTITY;
    kg::EntityID outcome_ = kg::INVALID_ENTITY;
    logosphere::dice::DiceRoll roll_;
};

struct RollableTableResult {
    std::optional<RollableTableSelection> selection;
    std::string error;

    bool ok() const { return selection.has_value() && error.empty(); }
};

class RollableTableRunner {
public:
    RollableTableRunner(const kg::KGModule& kg,
                        logosphere::dice::DiceService& dice)
        : kg_(kg), dice_(dice) {}

    RollableTableResult select(kg::EntityID table,
                               const std::string& dice_stream,
                               const std::string& purpose) const;

private:
    const kg::KGModule& kg_;
    logosphere::dice::DiceService& dice_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_ROLLABLE_TABLE_RUNNER_H
