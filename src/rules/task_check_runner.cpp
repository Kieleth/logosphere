#include "logosphere/rules/task_check_runner.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "rule_entity_reader.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace logosphere::rules {
namespace {

using kg::EntityID;

TaskCheckResult failure(std::string error) {
    TaskCheckResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

TaskCheckResult TaskCheckRunner::run(EntityID check, EntityID target,
                                     const std::string& dice_stream,
                                     const std::string& purpose,
                                     const TaskCheckOptions& options) const {
    if (dice_stream.empty()) {
        return failure("TaskCheck execution requires a dice stream");
    }
    if (purpose.empty()) {
        return failure("TaskCheck execution requires a roll purpose");
    }
    if (!kg_.exists(check)) {
        return failure("TaskCheck entity does not exist: " +
                       std::to_string(check));
    }
    const std::string check_type = kg_.getType(check);
    if (!kg_.getRegistry().isSubtypeOf(check_type, "TaskCheck")) {
        return failure("entity " + std::to_string(check) + " has type " +
                       check_type + ", not TaskCheck");
    }
    if (!kg_.exists(target)) {
        return failure("TaskCheck target entity does not exist: " +
                       std::to_string(target));
    }

    // A throw need not be modified by anything. Cepheus prints
    // re-enlistment as a bare "6+": roll and compare, with no
    // characteristic and therefore no lookup. Attribute and modifier
    // table travel together, so an unmodified throw simply has a
    // modifier of zero.
    const std::string attribute = kg_.getProperty(check, "attribute_ref");
    const bool modified = !attribute.empty();
    std::string error;
    int64_t attribute_value = 0;
    if (modified) {
        const std::string target_type = kg_.getType(target);
        const auto* attribute_def =
            kg_.getRegistry().findProperty(target_type, attribute);
        if (!attribute_def) {
            return failure("TaskCheck attribute_ref '" + attribute +
                           "' is not declared on target type '" +
                           target_type + "'");
        }
        if (attribute_def->value_kind != kg::PropertyValueKind::Integer) {
            return failure("TaskCheck attribute_ref '" + attribute +
                           "' is not an integer property");
        }
        if (!detail::parse_required_integer(
                kg_.getProperty(target, attribute), attribute,
                attribute_value, error)) {
            return failure("TaskCheck target: " + error);
        }
    }
    int64_t target_number = 0;
    if (!detail::parse_required_integer(
            kg_.getProperty(check, "target_number"), "target_number",
            target_number, error)) {
        return failure(std::move(error));
    }

    logosphere::dice::DiceExpression expression;
    if (!detail::read_dice_expression(
            kg_, check, "dice", expression, error)) {
        return failure(std::move(error));
    }

    EntityID modifier_table = kg::INVALID_ENTITY;
    std::string modifier_property;
    if (modified) {
        if (!detail::read_entity_reference(
                kg_, check, "modifier_table", "LookupTable", modifier_table,
                error)) {
            return failure(std::move(error));
        }
        modifier_property = kg_.getProperty(check, "modifier_property");
        if (modifier_property.empty()) {
            return failure("TaskCheck names an attribute but no "
                           "modifier_property to read its DM from");
        }
    }

    int64_t modifier = 0;
    std::optional<LookupTableSelection> lookup;
    if (modified) {
        LookupTableSelector selector(kg_);
        auto selected = selector.select(modifier_table, attribute_value);
        if (!selected.ok()) {
            return failure("TaskCheck modifier lookup failed: " +
                           selected.error);
        }
        const std::string entry_type =
            kg_.getProperty(modifier_table, "entry_type");
        const auto* modifier_def =
            kg_.getRegistry().findProperty(entry_type, modifier_property);
        if (!modifier_def) {
            return failure("TaskCheck modifier_property '" +
                           modifier_property + "' is not declared on lookup "
                           "entry type '" + entry_type + "'");
        }
        if (modifier_def->value_kind != kg::PropertyValueKind::Integer) {
            return failure("TaskCheck modifier_property '" +
                           modifier_property + "' is not an integer "
                           "property");
        }
        if (!detail::parse_required_integer(
                kg_.getProperty(selected.selection->row(),
                                modifier_property),
                modifier_property, modifier, error)) {
            return failure("selected LookupEntry: " + error);
        }
        lookup = std::move(*selected.selection);
    }

    auto dice_transaction = dice_.begin_transaction();
    auto roll = dice_.roll(expression, dice_stream, purpose,
                           static_cast<uint64_t>(check));
    if (roll.id == 0) {
        return failure("DiceService rejected the validated DiceExpression");
    }
    if ((modifier > 0 &&
         static_cast<int64_t>(roll.total) >
             std::numeric_limits<int64_t>::max() - modifier) ||
        (modifier < 0 &&
         static_cast<int64_t>(roll.total) <
             std::numeric_limits<int64_t>::min() - modifier)) {
        return failure("TaskCheck total exceeds signed integer range");
    }
    const int64_t total = static_cast<int64_t>(roll.total) + modifier;

    // "A natural 2 is always a failure." The natural result is the dice
    // as they landed, so it is summed from the faces rather than taken
    // off a total that already carries the DM and the multiplier.
    int64_t natural_total = 0;
    for (const int face : roll.values) natural_total += face;
    const bool failed_on_natural =
        options.natural_failure_at_or_below.has_value() &&
        natural_total <= *options.natural_failure_at_or_below;

    TaskCheckResult result;
    result.execution = TaskCheckExecution(
        check, target, attribute, attribute_value,
        std::move(lookup), modifier_property, modifier,
        target_number, std::move(roll), total, natural_total,
        failed_on_natural);
    dice_transaction.commit();
    return result;
}

}  // namespace logosphere::rules
