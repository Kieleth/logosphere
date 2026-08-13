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

// What a book can say about a throw that the throw's own row cannot
// carry. Cepheus prints each career's survival number in a table cell
// ("End 5+") and states separately, in prose, that "A natural 2 is
// always a failure" - one sentence governing 24 cells. A citation
// proves one entity against one quote, so the sentence cannot be
// pinned onto the cells; it becomes a RuleConstant the game reads and
// hands to the runner here. The engine supplies the mechanism, the
// book supplies the number.
struct TaskCheckOptions {
    // The raw dice sum, BEFORE any modifier, at or below which the
    // throw fails whatever the total came to. Unset means the throw is
    // decided by its total alone.
    std::optional<int64_t> natural_failure_at_or_below;

    // A DM the SITUATION supplies rather than the check. Cepheus: "You
    // suffer a DM-2 to qualification rolls for each previous career you
    // have entered" - a modifier that belongs to the character's
    // history, not to the Agent qualification row, and so cannot live
    // on the check the way its characteristic DM does. Added on top of
    // the check's own modifier, and reported separately on the
    // execution so a timeline can show both.
    int64_t situational_modifier = 0;
};

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
    // The DM from the check's own characteristic lookup.
    int64_t modifier() const { return modifier_; }
    // The DM the caller supplied for this throw's circumstances, kept
    // apart so "2D6 = 7 +1 DM -2 for prior careers" can be shown as the
    // book states it rather than as one merged number.
    int64_t situational_modifier() const { return situational_modifier_; }
    int64_t target_number() const { return target_number_; }
    const logosphere::dice::DiceRoll& roll() const { return roll_; }
    int64_t total() const { return total_; }
    // The dice as they landed, before the DM. What "a natural 2" means.
    int64_t natural_total() const { return natural_total_; }
    // True when the throw failed on the dice alone: the modified total
    // would have made the target, and a natural result took it away.
    // Kept separate from passed() so a timeline can say WHY.
    bool failed_on_natural() const { return failed_on_natural_; }
    bool passed() const { return passed_; }

private:
    friend class TaskCheckRunner;
    TaskCheckExecution(kg::EntityID executed_check,
                       kg::EntityID executed_target,
                       std::string attribute, int64_t attribute_value,
                       std::optional<LookupTableSelection> lookup,
                       std::string modifier_property, int64_t modifier,
                       int64_t situational_modifier,
                       int64_t target_number,
                       logosphere::dice::DiceRoll roll, int64_t total,
                       int64_t natural_total, bool failed_on_natural)
        : check_(executed_check),
          target_(executed_target),
          attribute_(std::move(attribute)),
          attribute_value_(attribute_value),
          lookup_(std::move(lookup)),
          modifier_property_(std::move(modifier_property)),
          modifier_(modifier),
          situational_modifier_(situational_modifier),
          target_number_(target_number),
          roll_(std::move(roll)),
          total_(total),
          natural_total_(natural_total),
          failed_on_natural_(failed_on_natural),
          passed_(total >= target_number && !failed_on_natural) {}

    kg::EntityID check_ = kg::INVALID_ENTITY;
    kg::EntityID target_ = kg::INVALID_ENTITY;
    std::string attribute_;
    int64_t attribute_value_ = 0;
    std::optional<LookupTableSelection> lookup_;
    std::string modifier_property_;
    int64_t modifier_ = 0;
    int64_t situational_modifier_ = 0;
    int64_t target_number_ = 0;
    logosphere::dice::DiceRoll roll_;
    int64_t total_ = 0;
    int64_t natural_total_ = 0;
    bool failed_on_natural_ = false;
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
                        const std::string& purpose,
                        const TaskCheckOptions& options = {}) const;

private:
    const kg::KGModule& kg_;
    logosphere::dice::DiceService& dice_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_TASK_CHECK_RUNNER_H
