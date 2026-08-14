#ifndef LOGOSPHERE_RULES_RULE_ENTITY_READER_H
#define LOGOSPHERE_RULES_RULE_ENTITY_READER_H

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace logosphere::rules::detail {

inline bool parse_required_integer(const std::string& value,
                                   const std::string& field,
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

inline bool read_entity_reference(const kg::KGModule& world,
                                  kg::EntityID owner,
                                  const std::string& field,
                                  const std::string& expected_type,
                                  kg::EntityID& out, std::string& error) {
    int64_t parsed = 0;
    if (!parse_required_integer(world.getProperty(owner, field), field,
                                parsed, error)) {
        return false;
    }
    if (parsed <= 0 || static_cast<uint64_t>(parsed) >
                           std::numeric_limits<kg::EntityID>::max()) {
        error = "invalid entity reference '" + field + "': " +
                std::to_string(parsed);
        return false;
    }
    out = static_cast<kg::EntityID>(parsed);
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

inline bool read_dice_expression(const kg::KGModule& world,
                                 kg::EntityID owner,
                                 const std::string& field,
                                 logosphere::dice::DiceExpression& expression,
                                 std::string& error) {
    kg::EntityID dice = kg::INVALID_ENTITY;
    if (!read_entity_reference(world, owner, field, "DiceExpression", dice,
                               error)) {
        return false;
    }

    int64_t count = 0;
    int64_t sides = 0;
    int64_t modifier = 0;
    int64_t multiplier = 1;
    if (!parse_required_integer(world.getProperty(dice, "dice_count"),
                                "dice_count", count, error) ||
        !parse_required_integer(world.getProperty(dice, "dice_sides"),
                                "dice_sides", sides, error)) {
        return false;
    }
    const std::string modifier_value =
        world.getProperty(dice, "dice_modifier");
    const std::string multiplier_value =
        world.getProperty(dice, "dice_multiplier");
    if ((!modifier_value.empty() &&
         !parse_required_integer(modifier_value, "dice_modifier", modifier,
                                 error)) ||
        (!multiplier_value.empty() &&
         !parse_required_integer(multiplier_value, "dice_multiplier",
                                 multiplier, error))) {
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

}  // namespace logosphere::rules::detail

#endif  // LOGOSPHERE_RULES_RULE_ENTITY_READER_H
