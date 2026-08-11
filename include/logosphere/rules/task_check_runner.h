#ifndef LOGOSPHERE_RULES_TASK_CHECK_RUNNER_H
#define LOGOSPHERE_RULES_TASK_CHECK_RUNNER_H

// Declarative TaskCheck execution. The runner resolves every dependency and
// validates the complete modifier lookup before one dice fact is committed.

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_types.h"
#include "logosphere/rules/lookup_table_selector.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace kg { class KGModule; }

namespace logosphere::rules {

class TaskCheckExecution {
public:
    TaskCheckExecution(const TaskCheckExecution&) = default;
    TaskCheckExecution(TaskCheckExecution&&) = default;
    TaskCheckExecution& operator=(const TaskCheckExecution&) = default;
    TaskCheckExecution& operator=(TaskCheckExecution&&) = default;

    kg::EntityID check() const { return check_; }
    kg::EntityID target() const { return target_; }
    const std::string& attribute() const { return attribute_; }
    int64_t attribute_value() const { return attribute_value_; }
    // Null when the throw had no characteristic to modify it, which
    // the book does print: Cepheus re-enlistment is a bare "6+".
    const LookupTableSelection* lookup() const {
        return lookup_ ? &*lookup_ : nullptr;
    }
    bool modified() const { return lookup_.has_value(); }
    const std::string& modifier_property() const {
        return modifier_property_;
    }
    int64_t modifier() const { return modifier_; }
    int64_t target_number() const { return target_number_; }
    const logosphere::dice::DiceRoll& roll() const { return roll_; }
    int64_t total() const { return total_; }
    bool passed() const { return passed_; }

private:
    friend class TaskCheckRunner;
    TaskCheckExecution(kg::EntityID executed_check,
                       kg::EntityID executed_target,
                       std::string attribute, int64_t attribute_value,
                       std::optional<LookupTableSelection> lookup,
                       std::string modifier_property, int64_t modifier,
                       int64_t target_number,
                       logosphere::dice::DiceRoll roll, int64_t total)
        : check_(executed_check),
          target_(executed_target),
          attribute_(std::move(attribute)),
          attribute_value_(attribute_value),
          lookup_(std::move(lookup)),
          modifier_property_(std::move(modifier_property)),
          modifier_(modifier),
          target_number_(target_number),
          roll_(std::move(roll)),
          total_(total),
          passed_(total >= target_number) {}

    kg::EntityID check_ = kg::INVALID_ENTITY;
    kg::EntityID target_ = kg::INVALID_ENTITY;
    std::string attribute_;
    int64_t attribute_value_ = 0;
    std::optional<LookupTableSelection> lookup_;
    std::string modifier_property_;
    int64_t modifier_ = 0;
    int64_t target_number_ = 0;
    logosphere::dice::DiceRoll roll_;
    int64_t total_ = 0;
    bool passed_ = false;
};

struct TaskCheckResult {
    std::optional<TaskCheckExecution> execution;
    std::string error;

    bool ok() const { return execution.has_value() && error.empty(); }
};

class TaskCheckRunner {
public:
    TaskCheckRunner(const kg::KGModule& kg,
                    logosphere::dice::DiceService& dice)
        : kg_(kg), dice_(dice) {}

    TaskCheckResult run(kg::EntityID check, kg::EntityID target,
                        const std::string& dice_stream,
                        const std::string& purpose) const;

private:
    const kg::KGModule& kg_;
    logosphere::dice::DiceService& dice_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_TASK_CHECK_RUNNER_H
