#ifndef LOGOSPHERE_RULES_ROLL_RULES_H
#define LOGOSPHERE_RULES_ROLL_RULES_H

// What a book says about a ROLL rather than about a row, read off the
// thing being rolled.
//
// A RollableTable or a TaskCheck carries RollRule entities as HAS_PART.
// Each rule may add a dice modifier, may scale that modifier by an
// attribute of whoever is rolling, and may impose a natural-result
// floor. Each may carry RollCondition children; any one holding is
// enough, and a rule with none always applies.
//
// The point of reading them here is that no runner and no procedure
// learns WHICH table or check it is holding. Before this, "characters
// with Gambling skill or who have retired gain +1 on Cash Benefit
// rolls" was a C++ conditional that matched the table by the substring
// "Cash Benefits" in its name and the skill by the literal "Gambling",
// which put the book's assertions somewhere a reader of the graph
// could not see them and a Referee could not change them.
//
// Every predicate reads the target as the GRAPH holds it. A rule can
// only ask what the game has actually written there, which is what
// stops a condition becoming a private arrangement between two pieces
// of code.

#include "logosphere/kg/kg_module.h"
#include "rule_entity_reader.h"

#include <cstdint>
#include <string>

namespace logosphere::rules::detail {

struct RollRuleEffect {
    int64_t dice_modifier = 0;
    bool has_natural_failure = false;
    int64_t natural_failure_at_or_below = 0;
};

// True when the target holds a SkillRating for `skill` at any level.
inline bool holds_skill(const kg::KGModule& world, kg::EntityID target,
                        kg::EntityID skill) {
    for (const kg::EntityID part : world.getRelated(target, "HAS_PART")) {
        if (!world.getRegistry().isSubtypeOf(world.getType(part),
                                             "SkillRating")) {
            continue;
        }
        if (world.getProperty(part, "skill") == std::to_string(skill)) {
            return true;
        }
    }
    return false;
}

// One condition. `holds` is only meaningful when this returns true.
inline bool evaluate_condition(const kg::KGModule& world,
                               kg::EntityID condition, kg::EntityID target,
                               bool& holds, std::string& error) {
    const std::string attribute =
        world.getProperty(condition, "requires_attribute");
    const std::string minimum =
        world.getProperty(condition, "requires_minimum");
    const std::string skill = world.getProperty(condition, "requires_skill");

    if (attribute.empty() && minimum.empty() && skill.empty()) {
        error = "RollCondition " + std::to_string(condition) +
                " tests nothing; a rule that always applies carries no "
                "conditions at all";
        return false;
    }
    // The pair travels together, exactly as it does on a gate: one
    // without the other is malformed rather than permissive.
    if (attribute.empty() != minimum.empty()) {
        error = "RollCondition " + std::to_string(condition) +
                " carries requires_attribute or requires_minimum without "
                "the other";
        return false;
    }

    holds = false;
    if (!skill.empty()) {
        int64_t skill_id = 0;
        if (!parse_required_integer(skill, "requires_skill", skill_id,
                                    error)) {
            error = "RollCondition " + std::to_string(condition) + ": " +
                    error;
            return false;
        }
        const auto id = static_cast<kg::EntityID>(skill_id);
        if (!world.exists(id)) {
            error = "RollCondition " + std::to_string(condition) +
                    " requires a skill that is not in the graph";
            return false;
        }
        if (holds_skill(world, target, id)) holds = true;
    }
    if (!holds && !attribute.empty()) {
        const std::string have = world.getProperty(target, attribute);
        // An attribute the target does not carry is a refusal, not a
        // failed test: a condition that quietly never holds is a rule
        // that stopped applying without saying so.
        if (have.empty()) {
            error = "RollCondition " + std::to_string(condition) +
                    " asks for '" + attribute + "', which the target does " +
                    "not carry in the graph";
            return false;
        }
        int64_t value = 0;
        int64_t least = 0;
        if (!parse_required_integer(have, attribute, value, error) ||
            !parse_required_integer(minimum, "requires_minimum", least,
                                    error)) {
            error = "RollCondition " + std::to_string(condition) + ": " +
                    error;
            return false;
        }
        if (value >= least) holds = true;
    }
    return true;
}

// Sums every applicable rule carried by `carrier` for this `target`.
inline bool evaluate_roll_rules(const kg::KGModule& world,
                                kg::EntityID carrier, kg::EntityID target,
                                RollRuleEffect& effect, std::string& error) {
    effect = RollRuleEffect{};
    for (const kg::EntityID rule : world.getRelated(carrier, "HAS_PART")) {
        if (!world.getRegistry().isSubtypeOf(world.getType(rule),
                                             "RollRule")) {
            continue;
        }
        // Conditions are alternatives: "with Gambling skill OR who have
        // retired" is one +1, not two. No conditions means always.
        bool applies = true;
        bool saw_condition = false;
        for (const kg::EntityID child : world.getRelated(rule, "HAS_PART")) {
            if (!world.getRegistry().isSubtypeOf(world.getType(child),
                                                 "RollCondition")) {
                continue;
            }
            if (!saw_condition) {
                saw_condition = true;
                applies = false;
            }
            bool holds = false;
            if (!evaluate_condition(world, child, target, holds, error)) {
                return false;
            }
            if (holds) applies = true;
        }
        if (!applies) continue;

        const std::string modifier = world.getProperty(rule, "dice_modifier");
        if (!modifier.empty()) {
            int64_t value = 0;
            if (!parse_required_integer(modifier, "dice_modifier", value,
                                        error)) {
                error = "RollRule " + std::to_string(rule) + ": " + error;
                return false;
            }
            const std::string scale =
                world.getProperty(rule, "scales_with_attribute");
            if (!scale.empty()) {
                const std::string have = world.getProperty(target, scale);
                if (have.empty()) {
                    error = "RollRule " + std::to_string(rule) +
                            " scales with '" + scale + "', which the target "
                            "does not carry in the graph";
                    return false;
                }
                int64_t count = 0;
                if (!parse_required_integer(have, scale, count, error)) {
                    error = "RollRule " + std::to_string(rule) + ": " + error;
                    return false;
                }
                value *= count;
            }
            effect.dice_modifier += value;
        }

        const std::string floor =
            world.getProperty(rule, "natural_failure_at_or_below");
        if (!floor.empty()) {
            int64_t value = 0;
            if (!parse_required_integer(
                    floor, "natural_failure_at_or_below", value, error)) {
                error = "RollRule " + std::to_string(rule) + ": " + error;
                return false;
            }
            // The strictest floor wins when a check carries several.
            if (!effect.has_natural_failure ||
                value > effect.natural_failure_at_or_below) {
                effect.natural_failure_at_or_below = value;
            }
            effect.has_natural_failure = true;
        }
    }
    return true;
}

}  // namespace logosphere::rules::detail

#endif  // LOGOSPHERE_RULES_ROLL_RULES_H
