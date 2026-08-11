#include "logosphere/rules/rollable_table_runner.h"
#include "rule_entity_reader.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
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
        // A band the book writes open-ended ("1+", "8+") carries no
        // roll_max at all, because the source prints no upper figure.
        // roll_max_unbounded says so explicitly rather than letting a
        // missing property mean two different things.
        const bool unbounded =
            world.getProperty(part, "roll_max_unbounded") == "true";
        if (!detail::parse_required_integer(
                world.getProperty(part, "roll_min"), "roll_min", row.low,
                error)) {
            error = "TableEntry " + std::to_string(part) + ": " + error;
            return false;
        }
        if (unbounded) {
            row.high = std::numeric_limits<int64_t>::max();
        } else if (!detail::parse_required_integer(
                       world.getProperty(part, "roll_max"), "roll_max",
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
        if (!detail::read_entity_reference(
                world, part, "outcome", "Outcome", row.outcome, error)) {
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
    const std::string& purpose, int dice_modifier) const {
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
    if (!detail::read_dice_expression(
            kg_, table, "dice", expression, error)) {
        return failure(std::move(error));
    }
    // The caller's modifier folds into the expression, so the recorded
    // dice fact carries the DM that produced the row rather than the
    // bare table dice. Coverage is then validated against the totals
    // this roll can actually reach, not the unmodified ones: aging is
    // "2D6 minus total terms", and at seven terms the reachable band
    // is -5..5, which is a different question from 2..12.
    const int64_t combined =
        static_cast<int64_t>(expression.modifier) + dice_modifier;
    if (combined < std::numeric_limits<int>::min() ||
        combined > std::numeric_limits<int>::max()) {
        return failure("dice modifier overflows the table's expression");
    }
    expression.modifier = static_cast<int>(combined);

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
