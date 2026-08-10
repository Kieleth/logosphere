#include "logosphere/rules/rollable_table_runner.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace logosphere::rules {
namespace {

using kg::EntityID;

struct Row {
    EntityID id = kg::INVALID_ENTITY;
    EntityID outcome = kg::INVALID_ENTITY;
    int64_t low = 0;
    int64_t high = 0;
};

RollableTableResult failure(std::string error) {
    RollableTableResult result;
    result.error = std::move(error);
    return result;
}

bool parse_integer(const std::string& value, const std::string& field,
                   int64_t& out, std::string& error) {
    if (value.empty()) {
        error = "missing required integer '" + field + "'";
        return false;
    }
    try {
        size_t end = 0;
        const long long parsed = std::stoll(value, &end);
        if (end != value.size()) throw std::invalid_argument("trailing");
        out = static_cast<int64_t>(parsed);
        return true;
    } catch (...) {
        error = "invalid integer '" + field + "': '" + value + "'";
        return false;
    }
}

bool parse_entity(const kg::KGModule& world, EntityID owner,
                  const std::string& field, const std::string& expected_type,
                  EntityID& out, std::string& error) {
    int64_t parsed = 0;
    if (!parse_integer(world.getProperty(owner, field), field, parsed,
                       error)) {
        return false;
    }
    if (parsed <= 0 || static_cast<uint64_t>(parsed) >
                           std::numeric_limits<EntityID>::max()) {
        error = "invalid entity reference '" + field + "': " +
                std::to_string(parsed);
        return false;
    }
    out = static_cast<EntityID>(parsed);
    if (!world.exists(out)) {
        error = "entity reference '" + field + "' points to missing " +
                expected_type + " entity " + std::to_string(out);
        return false;
    }
    const std::string actual = world.getType(out);
    if (!world.getRegistry().isSubtypeOf(actual, expected_type)) {
        error = "entity reference '" + field + "' points to " + actual +
                ", not " + expected_type;
        return false;
    }
    return true;
}

bool read_expression(const kg::KGModule& world, EntityID table,
                     logosphere::dice::DiceExpression& expression,
                     std::string& error) {
    EntityID dice = kg::INVALID_ENTITY;
    if (!parse_entity(world, table, "dice", "DiceExpression", dice, error)) {
        return false;
    }

    int64_t count = 0;
    int64_t sides = 0;
    int64_t modifier = 0;
    int64_t multiplier = 1;
    if (!parse_integer(world.getProperty(dice, "dice_count"), "dice_count",
                       count, error) ||
        !parse_integer(world.getProperty(dice, "dice_sides"), "dice_sides",
                       sides, error)) {
        return false;
    }
    const std::string modifier_value =
        world.getProperty(dice, "dice_modifier");
    const std::string multiplier_value =
        world.getProperty(dice, "dice_multiplier");
    if ((!modifier_value.empty() &&
         !parse_integer(modifier_value, "dice_modifier", modifier, error)) ||
        (!multiplier_value.empty() &&
         !parse_integer(multiplier_value, "dice_multiplier", multiplier,
                        error))) {
        return false;
    }

    const auto in_int = [](int64_t value) {
        return value >= std::numeric_limits<int>::min() &&
               value <= std::numeric_limits<int>::max();
    };
    if (!in_int(count) || !in_int(sides) || !in_int(modifier) ||
        !in_int(multiplier)) {
        error = "DiceExpression field exceeds runner integer range";
        return false;
    }
    expression = {static_cast<int>(count), static_cast<int>(sides),
                  static_cast<int>(modifier),
                  static_cast<int>(multiplier)};
    if (!expression.is_valid()) {
        error = "referenced DiceExpression is invalid";
        return false;
    }
    return true;
}

bool read_rows(const kg::KGModule& world, EntityID table,
               std::vector<Row>& rows, std::string& error) {
    const auto parts = world.getRelated(table, "HAS_PART");
    if (parts.empty()) {
        error = "RollableTable has no TableEntry rows";
        return false;
    }

    rows.reserve(parts.size());
    for (const EntityID part : parts) {
        if (!world.exists(part)) {
            error = "RollableTable contains missing HAS_PART entity " +
                    std::to_string(part);
            return false;
        }
        const std::string type = world.getType(part);
        if (!world.getRegistry().isSubtypeOf(type, "TableEntry")) {
            error = "RollableTable part " + std::to_string(part) +
                    " has type " + type + ", not TableEntry";
            return false;
        }

        Row row;
        row.id = part;
        if (!parse_integer(world.getProperty(part, "roll_min"), "roll_min",
                           row.low, error) ||
            !parse_integer(world.getProperty(part, "roll_max"), "roll_max",
                           row.high, error)) {
            error = "TableEntry " + std::to_string(part) + ": " + error;
            return false;
        }
        if (row.low > row.high) {
            error = "TableEntry " + std::to_string(part) +
                    " has malformed band [" + std::to_string(row.low) +
                    ", " + std::to_string(row.high) + "]";
            return false;
        }
        if (!parse_entity(world, part, "outcome", "Outcome", row.outcome,
                          error)) {
            error = "TableEntry " + std::to_string(part) + ": " + error;
            return false;
        }
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
        if (left.low != right.low) return left.low < right.low;
        if (left.high != right.high) return left.high < right.high;
        return left.id < right.id;
    });
    for (size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].low <= rows[i - 1].high) {
            error = "RollableTable row bands overlap at " +
                    std::to_string(rows[i].low);
            return false;
        }
    }
    return true;
}

const Row* row_for_total(const std::vector<Row>& rows, int64_t total) {
    const auto found = std::lower_bound(
        rows.begin(), rows.end(), total,
        [](const Row& row, int64_t value) { return row.high < value; });
    if (found == rows.end() || total < found->low) return nullptr;
    return &*found;
}

bool covers_every_reachable_total(
    const std::vector<Row>& rows,
    const logosphere::dice::DiceExpression& expression,
    std::string& error) {
    const int64_t lowest_sum = expression.count;
    const int64_t highest_sum =
        static_cast<int64_t>(expression.count) * expression.sides;
    for (int64_t sum = lowest_sum; sum <= highest_sum; ++sum) {
        const int64_t total =
            (sum + expression.modifier) * expression.multiplier;
        if (!row_for_total(rows, total)) {
            error = "RollableTable has no row for reachable total " +
                    std::to_string(total);
            return false;
        }
    }
    return true;
}

}  // namespace

RollableTableResult RollableTableRunner::select(
    EntityID table, const std::string& dice_stream,
    const std::string& purpose) const {
    if (dice_stream.empty()) {
        return failure("RollableTable selection requires a dice stream");
    }
    if (purpose.empty()) {
        return failure("RollableTable selection requires a roll purpose");
    }
    if (!kg_.exists(table)) {
        return failure("RollableTable entity does not exist: " +
                       std::to_string(table));
    }
    const std::string type = kg_.getType(table);
    if (!kg_.getRegistry().isSubtypeOf(type, "RollableTable")) {
        return failure("entity " + std::to_string(table) + " has type " +
                       type + ", not RollableTable");
    }

    logosphere::dice::DiceExpression expression;
    std::string error;
    if (!read_expression(kg_, table, expression, error)) {
        return failure(std::move(error));
    }
    std::vector<Row> rows;
    if (!read_rows(kg_, table, rows, error) ||
        !covers_every_reachable_total(rows, expression, error)) {
        return failure(std::move(error));
    }

    auto dice_transaction = dice_.begin_transaction();
    auto roll = dice_.roll(expression, dice_stream, purpose);
    if (roll.id == 0) {
        return failure("DiceService rejected the validated DiceExpression");
    }
    const Row* row = row_for_total(rows, roll.total);
    if (!row) {
        return failure("recorded table roll has no validated TableEntry");
    }

    RollableTableResult result;
    result.selection = RollableTableSelection(
        table, row->id, row->outcome, std::move(roll));
    dice_transaction.commit();
    return result;
}

}  // namespace logosphere::rules
