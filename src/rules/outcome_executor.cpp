#include "logosphere/rules/outcome_executor.h"

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace logosphere::rules {
namespace {

using kg::EntityID;

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
    const std::string value = world.getProperty(owner, field);
    int64_t parsed = 0;
    if (!parse_integer(value, field, parsed, error) || parsed < 0 ||
        static_cast<uint64_t>(parsed) >
            std::numeric_limits<EntityID>::max()) {
        if (error.empty()) error = "invalid entity reference '" + field + "'";
        return false;
    }
    out = static_cast<EntityID>(parsed);
    if (!world.exists(out)) {
        error = "entity reference '" + field + "' points to missing entity " +
                std::to_string(out);
        return false;
    }
    const std::string actual = world.getType(out);
    if (!expected_type.empty() &&
        !world.getRegistry().isSubtypeOf(actual, expected_type)) {
        error = "entity reference '" + field + "' points to " + actual +
                ", not " + expected_type;
        return false;
    }
    return true;
}

bool checked_add(int64_t left, int64_t right, int64_t& result) {
    if ((right > 0 && left > std::numeric_limits<int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<int64_t>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}

bool same_ref(const kg::EntityRef& left, const kg::EntityRef& right) {
    return left.id == right.id && left.symbolic == right.symbolic;
}

// One implementation of "the world as this plan will leave it", in
// PlannedWorld. This was hand-rolled here three separate times before
// it had a name, which is how it earned one.
std::string planned_property(const kg::KGModule& world,
                             const OutcomePlan& plan,
                             const kg::EntityRef& target,
                             const std::string& property) {
    return PlannedWorld(world, plan).property(target, property);
}

std::vector<kg::EntityRef> matching_parts(
    const kg::KGModule& world, const OutcomePlan& plan, EntityID target,
    const std::string& type, const std::string& ref_property,
    EntityID referenced) {
    std::vector<kg::EntityRef> matches;
    for (const EntityID part : world.getRelated(target, "HAS_PART")) {
        if (!world.getRegistry().isSubtypeOf(world.getType(part), type)) {
            continue;
        }
        const kg::EntityRef ref{part, ""};
        if (planned_property(world, plan, ref, ref_property) ==
            std::to_string(referenced)) {
            matches.push_back(ref);
        }
    }
    for (const auto& op : plan.ops) {
        const auto* relation = std::get_if<kg::KGOpSetRelation>(&op);
        if (!relation || relation->from.id != target ||
            relation->from.is_symbolic() || relation->relation != "HAS_PART" ||
            !relation->to.is_symbolic()) {
            continue;
        }
        const kg::EntityRef ref{kg::INVALID_ENTITY, relation->to.symbolic};
        for (const auto& candidate : plan.ops) {
            const auto* create = std::get_if<kg::KGOpCreateEntity>(&candidate);
            if (!create || create->as != ref.symbolic ||
                !world.getRegistry().isSubtypeOf(create->type, type)) {
                continue;
            }
            if (planned_property(world, plan, ref, ref_property) ==
                std::to_string(referenced)) {
                matches.push_back(ref);
            }
            break;
        }
    }
    return matches;
}

// Multivalued strings are stored "a; b; c", the separator this
// ontology documents on every list-valued slot.
std::vector<std::string> split_list(const std::string& value) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= value.size()) {
        const size_t cut = value.find(';', at);
        const size_t end = cut == std::string::npos ? value.size() : cut;
        size_t first = at;
        size_t last = end;
        while (first < last && std::isspace(
                   static_cast<unsigned char>(value[first]))) ++first;
        while (last > first && std::isspace(
                   static_cast<unsigned char>(value[last - 1]))) --last;
        if (last > first) out.push_back(value.substr(first, last - first));
        if (cut == std::string::npos) break;
        at = cut + 1;
    }
    return out;
}

bool read_dice(const kg::KGModule& world, EntityID outcome,
               logosphere::dice::DiceExpression& expression,
               std::string& error,
               const char* slot = "amount_dice") {
    EntityID dice_id = kg::INVALID_ENTITY;
    if (!parse_entity(world, outcome, slot, "DiceExpression",
                      dice_id, error)) {
        return false;
    }
    int64_t count = 0;
    int64_t sides = 0;
    if (!parse_integer(world.getProperty(dice_id, "dice_count"),
                       "dice_count", count, error) ||
        !parse_integer(world.getProperty(dice_id, "dice_sides"),
                       "dice_sides", sides, error)) {
        return false;
    }
    int64_t modifier = 0;
    int64_t multiplier = 1;
    const std::string modifier_value =
        world.getProperty(dice_id, "dice_modifier");
    const std::string multiplier_value =
        world.getProperty(dice_id, "dice_multiplier");
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
        error = "DiceExpression field exceeds executor integer range";
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

bool plan_skill(const OutcomeHandlerContext& context, OutcomePlan& plan,
                bool ensure, std::string& error) {
    EntityID skill = kg::INVALID_ENTITY;
    if (!parse_entity(context.kg, context.outcome, "skill", "Entity", skill,
                      error)) {
        return false;
    }
    const auto ratings = matching_parts(context.kg, plan, context.target,
                                        "SkillRating", "skill", skill);
    if (ratings.size() > 1) {
        error = "target has duplicate SkillRating entities for skill " +
                std::to_string(skill);
        return false;
    }

    int64_t initial = 0;
    int64_t delta = 0;
    if (ensure) {
        if (!parse_integer(context.kg.getProperty(context.outcome,
                                                  "skill_level"),
                           "skill_level", initial, error)) {
            return false;
        }
    } else {
        if (!parse_integer(context.kg.getProperty(context.outcome,
                                                  "initial_skill_level"),
                           "initial_skill_level", initial, error) ||
            !parse_integer(context.kg.getProperty(context.outcome,
                                                  "existing_skill_delta"),
                           "existing_skill_delta", delta, error)) {
            return false;
        }
    }
    if (initial < 0) {
        error = "initial skill level cannot be negative";
        return false;
    }

    if (ratings.empty()) {
        const std::string alias =
            "outcome_skill_rating_" + std::to_string(context.outcome);
        plan.ops.emplace_back(kg::KGOpCreateEntity{
            "SkillRating",
            {{"skill", std::to_string(skill)},
             {"skill_level", std::to_string(initial)}},
            alias});
        plan.ops.emplace_back(kg::KGOpSetRelation{
            {context.target, ""}, "HAS_PART", {kg::INVALID_ENTITY, alias}});
        return true;
    }

    int64_t current = 0;
    if (!parse_integer(planned_property(context.kg, plan, ratings.front(),
                                        "skill_level"),
                       "SkillRating.skill_level", current, error)) {
        return false;
    }
    int64_t next = current;
    if (ensure) {
        next = std::max(current, initial);
    } else if (!checked_add(current, delta, next)) {
        error = "skill level arithmetic overflow";
        return false;
    }
    if (next < 0) {
        error = "skill level would become negative";
        return false;
    }
    if (next != current) {
        plan.ops.emplace_back(kg::KGOpSetProperty{
            ratings.front(), "skill_level", std::to_string(next)});
    }
    return true;
}

bool plan_money(const OutcomeHandlerContext& context, OutcomePlan& plan,
                bool rolled, std::string& error) {
    EntityID currency = kg::INVALID_ENTITY;
    if (!parse_entity(context.kg, context.outcome, "currency", "Entity",
                      currency, error)) {
        return false;
    }
    int64_t amount = 0;
    if (rolled) {
        logosphere::dice::DiceExpression expression;
        if (!read_dice(context.kg, context.outcome, expression, error)) {
            return false;
        }
        const auto result = context.dice.roll(expression, context.dice_stream,
                                              context.purpose);
        if (result.id == 0) {
            error = "DiceService rejected the outcome DiceExpression";
            return false;
        }
        amount = result.total;
        plan.roll_ids.push_back(result.id);
    } else if (!parse_integer(
                   context.kg.getProperty(context.outcome, "amount"),
                   "amount", amount, error)) {
        return false;
    }

    const auto balances = matching_parts(context.kg, plan, context.target,
                                         "CurrencyBalance", "currency",
                                         currency);
    if (balances.size() > 1) {
        error = "target has duplicate CurrencyBalance entities for currency " +
                std::to_string(currency);
        return false;
    }
    if (balances.empty()) {
        const std::string alias =
            "outcome_currency_balance_" + std::to_string(context.outcome);
        plan.ops.emplace_back(kg::KGOpCreateEntity{
            "CurrencyBalance",
            {{"currency", std::to_string(currency)},
             {"balance_amount", std::to_string(amount)}},
            alias});
        plan.ops.emplace_back(kg::KGOpSetRelation{
            {context.target, ""}, "HAS_PART", {kg::INVALID_ENTITY, alias}});
        return true;
    }

    int64_t current = 0;
    if (!parse_integer(planned_property(context.kg, plan, balances.front(),
                                        "balance_amount"),
                       "CurrencyBalance.balance_amount", current, error)) {
        return false;
    }
    int64_t next = 0;
    if (!checked_add(current, amount, next)) {
        error = "currency balance arithmetic overflow";
        return false;
    }
    plan.ops.emplace_back(kg::KGOpSetProperty{
        balances.front(), "balance_amount", std::to_string(next)});
    return true;
}

// What a book hands out that is neither money nor a skill: a passage,
// a weapon, ship shares. Held in a count, because "1D6 Ship Shares" is
// a thing the material benefits tables actually print, and an absent
// count means one.
bool plan_possession(const OutcomeHandlerContext& context, OutcomePlan& plan,
                     std::string& error) {
    EntityID possession = kg::INVALID_ENTITY;
    if (!parse_entity(context.kg, context.outcome, "possession", "Entity",
                      possession, error)) {
        return false;
    }
    int64_t count = 1;
    const std::string dice_ref =
        context.kg.getProperty(context.outcome, "possession_count_dice");
    const std::string fixed =
        context.kg.getProperty(context.outcome, "possession_count");
    if (!dice_ref.empty()) {
        logosphere::dice::DiceExpression expression;
        if (!read_dice(context.kg, context.outcome, expression, error,
                       "possession_count_dice")) {
            return false;
        }
        const auto result = context.dice.roll(expression, context.dice_stream,
                                              context.purpose);
        if (result.id == 0) {
            error = "DiceService rejected the outcome DiceExpression";
            return false;
        }
        count = result.total;
        plan.roll_ids.push_back(result.id);
    } else if (!fixed.empty() &&
               !parse_integer(fixed, "possession_count", count, error)) {
        return false;
    }

    const auto holdings = matching_parts(context.kg, plan, context.target,
                                         "PossessionHolding", "possession",
                                         possession);
    if (holdings.size() > 1) {
        error = "target has duplicate PossessionHolding entities for "
                "possession " + std::to_string(possession);
        return false;
    }
    if (holdings.empty()) {
        const std::string alias =
            "outcome_possession_" + std::to_string(context.outcome);
        plan.ops.emplace_back(kg::KGOpCreateEntity{
            "PossessionHolding",
            {{"possession", std::to_string(possession)},
             {"possession_count", std::to_string(count)}},
            alias});
        plan.ops.emplace_back(kg::KGOpSetRelation{
            {context.target, ""}, "HAS_PART", {kg::INVALID_ENTITY, alias}});
        return true;
    }
    int64_t current = 0;
    if (!parse_integer(planned_property(context.kg, plan, holdings.front(),
                                        "possession_count"),
                       "PossessionHolding.possession_count", current,
                       error)) {
        return false;
    }
    int64_t next = 0;
    if (!checked_add(current, count, next)) {
        error = "possession count arithmetic overflow";
        return false;
    }
    plan.ops.emplace_back(kg::KGOpSetProperty{
        holdings.front(), "possession_count", std::to_string(next)});
    return true;
}

// "Reduce two physical characteristics by 2": the book fixes the count
// and the delta, and leaves which ones to whoever applies the rule.
// A change the schema will not take in full lands at the boundary and
// says so. Refusing the whole rule would be wrong: Cepheus floors
// characteristics at 0 and then has a rule for reaching 0, so a
// reduction past the floor is expected play, not malformed data.
//
// What it MEANS is the game's business, which is why this reports and
// stops.
int64_t clamp_to_schema(const OutcomeHandlerContext& context,
                        OutcomePlan& plan, const std::string& property,
                        int64_t wanted) {
    const kg::PropertyDef* def = context.kg.getRegistry().findProperty(
        context.kg.getType(context.target), property);
    if (!def) return wanted;
    int64_t applied = wanted;
    if (def->has_min && static_cast<double>(applied) < def->min_value) {
        applied = static_cast<int64_t>(def->min_value);
    }
    if (def->has_max && static_cast<double>(applied) > def->max_value) {
        applied = static_cast<int64_t>(def->max_value);
    }
    if (applied != wanted) {
        plan.attributes_limited.push_back(
            {context.target, property, wanted, applied});
    }
    return applied;
}

bool plan_attributes_in_group(const OutcomeHandlerContext& context,
                              OutcomePlan& plan,
                              const AttributeSelector& selector,
                              std::string& error) {
    EntityID group = kg::INVALID_ENTITY;
    if (!parse_entity(context.kg, context.outcome, "attribute_group",
                      "AttributeGroup", group, error)) {
        return false;
    }
    const std::vector<std::string> eligible =
        split_list(context.kg.getProperty(group, "attribute_refs"));
    if (eligible.empty()) {
        error = "AttributeGroup names no attributes in attribute_refs";
        return false;
    }
    int64_t count = 0;
    if (!parse_integer(context.kg.getProperty(context.outcome,
                                              "affected_count"),
                       "affected_count", count, error)) {
        return false;
    }
    if (count < 1 || static_cast<size_t>(count) > eligible.size()) {
        error = "affected_count " + std::to_string(count) +
                " is outside the group's " +
                std::to_string(eligible.size()) + " attributes";
        return false;
    }

    // A fixed delta or a rolled one, never both: the book uses one or
    // the other, and a rule carrying both is ambiguous rather than
    // generous.
    const std::string fixed =
        context.kg.getProperty(context.outcome, "attribute_delta");
    const bool rolled =
        !context.kg.getProperty(context.outcome,
                                "attribute_delta_dice").empty();
    int64_t delta = 0;
    if (!fixed.empty() && rolled) {
        error = "ModifyAttributesInGroup has both attribute_delta and "
                "attribute_delta_dice";
        return false;
    }
    if (!fixed.empty()) {
        if (!parse_integer(fixed, "attribute_delta", delta, error)) {
            return false;
        }
    } else if (rolled) {
        logosphere::dice::DiceExpression expression;
        if (!read_dice(context.kg, context.outcome, expression, error,
                       "attribute_delta_dice")) {
            return false;
        }
        const auto roll = context.dice.roll(expression, context.dice_stream,
                                            context.purpose);
        if (roll.id == 0) {
            error = "DiceService rejected attribute_delta_dice";
            return false;
        }
        plan.roll_ids.push_back(roll.id);
        // Dice say how much, never which way. "Reduce one physical
        // characteristic by 1D6" prints no minus sign, and neither does
        // a rule that rolls a gain, so the direction has to be stated
        // rather than assumed from the one table we happened to absorb
        // first. A rule that rolls without saying is refused.
        const std::string direction =
            context.kg.getProperty(context.outcome, "attribute_delta_reduces");
        if (direction != "true" && direction != "false") {
            error = "ModifyAttributesInGroup rolls attribute_delta_dice "
                    "without saying which way it goes: set "
                    "attribute_delta_reduces";
            return false;
        }
        delta = direction == "true" ? -roll.total : roll.total;
    } else {
        error = "ModifyAttributesInGroup needs attribute_delta or "
                "attribute_delta_dice";
        return false;
    }

    std::vector<std::string> chosen;
    if (static_cast<size_t>(count) == eligible.size()) {
        chosen = eligible;  // the whole group: nothing to choose
    } else if (!selector) {
        error = "ModifyAttributesInGroup affects " + std::to_string(count) +
                " of " + std::to_string(eligible.size()) +
                " attributes and no attribute selector is installed";
        return false;
    } else {
        AttributeSelectionRequest request;
        request.target = context.target;
        request.outcome = context.outcome;
        request.group = group;
        request.eligible = eligible;
        request.count = static_cast<int>(count);
        request.delta = delta;
        // What the plan has already done to this group. The graph
        // cannot answer for it: every op commits together at the end,
        // so a chooser asking the KG between two steps of one rule
        // would read the values as they stood before either.
        const kg::EntityRef planned_target{context.target, ""};
        for (const auto& name : eligible) {
            const std::string value =
                planned_property(context.kg, plan, planned_target, name);
            int64_t parsed = 0;
            std::string ignored;
            request.current.push_back(
                parse_integer(value, name, parsed, ignored) ? parsed : 0);
            for (const auto& op : plan.ops) {
                const auto* set = std::get_if<kg::KGOpSetProperty>(&op);
                if (set && same_ref(set->target, planned_target) &&
                    set->property == name) {
                    request.already_taken.push_back(name);
                    break;
                }
            }
        }
        if (!selector(request, chosen, error)) {
            if (error.empty()) error = "attribute selector refused";
            return false;
        }
        // The selector is not trusted. It may be a prompt, a roll or a
        // narrator, and none of those may widen the rule the book set.
        if (chosen.size() != static_cast<size_t>(count)) {
            error = "attribute selector returned " +
                    std::to_string(chosen.size()) + " attributes, not " +
                    std::to_string(count);
            return false;
        }
        for (size_t i = 0; i < chosen.size(); ++i) {
            if (std::find(eligible.begin(), eligible.end(), chosen[i]) ==
                eligible.end()) {
                error = "attribute selector chose '" + chosen[i] +
                        "', which is not in the group";
                return false;
            }
            if (std::find(chosen.begin(), chosen.begin() + i, chosen[i]) !=
                chosen.begin() + i) {
                error = "attribute selector chose '" + chosen[i] + "' twice";
                return false;
            }
        }
    }

    const std::string target_type = context.kg.getType(context.target);
    const kg::EntityRef target{context.target, ""};
    for (const auto& property : chosen) {
        const kg::PropertyDef* definition =
            context.kg.getRegistry().findProperty(target_type, property);
        if (!definition ||
            definition->value_kind != kg::PropertyValueKind::Integer) {
            error = "target type '" + target_type +
                    "' has no integer property '" + property + "'";
            return false;
        }
        const std::string current_value =
            planned_property(context.kg, plan, target, property);
        if (current_value.empty() &&
            !context.kg.hasProperty(context.target, property)) {
            error = "target has no current value for property '" +
                    property + "'";
            return false;
        }
        int64_t current = 0;
        int64_t next = 0;
        if (!parse_integer(current_value, property, current, error)) {
            return false;
        }
        if (!checked_add(current, delta, next)) {
            error = "attribute arithmetic overflow";
            return false;
        }
        next = clamp_to_schema(context, plan, property, next);
        plan.ops.emplace_back(kg::KGOpSetProperty{
            target, property, std::to_string(next)});
    }
    return true;
}

}  // namespace

struct OutcomeExecutor::Planner {
    OutcomeExecutor& executor;
    const OutcomeContext& context;
    const std::unordered_map<EntityID, int>& selections;
    std::unordered_set<EntityID> used_selections;
    std::unordered_set<EntityID> stack;
    OutcomePlan plan;
    std::optional<PendingChoice> pending;
    std::string error;

    bool ordered_parts(EntityID owner, const std::string& part_type,
                       const std::string& index_property,
                       std::vector<EntityID>& ordered) {
        const auto parts = executor.kg_.getRelated(owner, "HAS_PART");
        if (parts.empty()) {
            error = executor.kg_.getType(owner) + " has no " + part_type +
                    " parts";
            return false;
        }
        std::vector<std::pair<int, EntityID>> indexed;
        for (const EntityID part : parts) {
            const std::string actual = executor.kg_.getType(part);
            if (!executor.kg_.getRegistry().isSubtypeOf(actual, part_type)) {
                error = executor.kg_.getType(owner) + " contains " + actual +
                        ", not " + part_type;
                return false;
            }
            int64_t index = 0;
            if (!parse_integer(executor.kg_.getProperty(part, index_property),
                               index_property, index, error) ||
                index < 0 || index > std::numeric_limits<int>::max()) {
                return false;
            }
            indexed.emplace_back(static_cast<int>(index), part);
        }
        std::sort(indexed.begin(), indexed.end());
        for (size_t index = 0; index < indexed.size(); ++index) {
            if (indexed[index].first != static_cast<int>(index)) {
                error = executor.kg_.getType(owner) + " " + index_property +
                        " values must be contiguous from 0";
                return false;
            }
            ordered.push_back(indexed[index].second);
        }
        return true;
    }

    bool node(EntityID outcome) {
        if (!executor.kg_.exists(outcome)) {
            error = "outcome entity " + std::to_string(outcome) +
                    " does not exist";
            return false;
        }
        const std::string type = executor.kg_.getType(outcome);
        if (!executor.kg_.getRegistry().isSubtypeOf(type, "Outcome")) {
            error = "entity " + std::to_string(outcome) + " is a " + type +
                    ", not an Outcome";
            return false;
        }
        if (!stack.insert(outcome).second) {
            error = "cyclic outcome graph at entity " +
                    std::to_string(outcome);
            return false;
        }
        // On the way in, so a sequence, its steps and the choice above
        // them all count as reached rather than only the leaf that
        // produced ops. This is planning: apply() decides whether any
        // of it happened.
        plan.rules_reached.push_back(outcome);

        bool ok = true;
        if (type == "OutcomeSequence") {
            std::vector<EntityID> steps;
            ok = ordered_parts(outcome, "OutcomeStep", "step_index", steps);
            for (size_t index = 0; ok && index < steps.size(); ++index) {
                EntityID child = kg::INVALID_ENTITY;
                ok = parse_entity(executor.kg_, steps[index], "outcome",
                                  "Outcome", child, error) && node(child);
                if (pending) break;
            }
        } else if (type == "OutcomeChoice") {
            const std::string authority =
                executor.kg_.getProperty(outcome, "choice_authority");
            if (authority != "player" && authority != "referee" &&
                authority != "procedure") {
                error = "OutcomeChoice has unknown choice_authority '" +
                        authority + "'";
                ok = false;
            }
            std::vector<EntityID> options;
            if (ok) {
                ok = ordered_parts(outcome, "OutcomeOption", "option_index",
                                   options);
            }
            std::vector<ChoiceOption> choices;
            for (size_t index = 0; ok && index < options.size(); ++index) {
                const std::string label =
                    executor.kg_.getProperty(options[index], "option_label");
                if (label.empty()) {
                    error = "OutcomeOption has empty option_label";
                    ok = false;
                    break;
                }
                EntityID child = kg::INVALID_ENTITY;
                if (!parse_entity(executor.kg_, options[index], "outcome",
                                  "Outcome", child, error)) {
                    ok = false;
                    break;
                }
                choices.push_back(
                    {static_cast<int>(index), label, child});
            }
            if (ok) {
                const auto selected = selections.find(outcome);
                // An explicit selection wins. Failing that, a resolver
                // answers in place; failing that, the question goes
                // back to the caller. The order matters: a caller that
                // already answered must never be second-guessed by a
                // resolver that would answer differently.
                if (selected == selections.end() && executor.choice_resolver_) {
                    const PendingChoice ask{outcome, authority, choices};
                    int option = -1;
                    if (!executor.choice_resolver_(ask, option, error)) {
                        if (error.empty()) {
                            error = "the choice resolver refused "
                                    "OutcomeChoice " +
                                    std::to_string(outcome) +
                                    " without a reason";
                        }
                        ok = false;
                    } else if (option < 0 ||
                               static_cast<size_t>(option) >= choices.size()) {
                        error = "the choice resolver answered "
                                "OutcomeChoice " + std::to_string(outcome) +
                                " with option_index " +
                                std::to_string(option) + ", which is not "
                                "one of its " +
                                std::to_string(choices.size()) + " options";
                        ok = false;
                    } else {
                        ok = node(choices[static_cast<size_t>(option)].outcome);
                    }
                } else if (selected == selections.end()) {
                    pending = PendingChoice{outcome, authority,
                                            std::move(choices)};
                } else if (selected->second < 0 ||
                           static_cast<size_t>(selected->second) >=
                               choices.size()) {
                    error = "OutcomeChoice " + std::to_string(outcome) +
                            " has no option_index " +
                            std::to_string(selected->second);
                    ok = false;
                } else {
                    used_selections.insert(outcome);
                    ok = node(choices[static_cast<size_t>(selected->second)]
                                  .outcome);
                }
            }
        } else {
            const auto handler = executor.handlers_.find(type);
            if (handler == executor.handlers_.end()) {
                error = "no handler for concrete outcome type '" + type +
                        "'";
                ok = false;
            } else {
                // Built fresh per call: it reads the plan as it stands
                // now, and it must not outlive this handler.
                const PlannedWorld planned(executor.kg_, plan);
                OutcomeHandlerContext handler_context{
                    outcome, context.target, type, executor.kg_,
                    executor.dice_, context.dice_stream, context.purpose,
                    planned};
                ok = handler->second(handler_context, plan, error);
                if (!ok && error.empty()) {
                    error = "outcome handler '" + type +
                            "' failed without a reason";
                }
            }
        }
        stack.erase(outcome);
        return ok;
    }
};

OutcomeExecutor::OutcomeExecutor(kg::KGModule& kg,
                                 logosphere::dice::DiceService& dice)
    : kg_(kg), dice_(dice) {
    std::string error;
    register_handler(
        "NoEffect",
        [](const OutcomeHandlerContext&, OutcomePlan&, std::string&) {
            return true;
        }, error);
    register_handler(
        "ModifyAttribute",
        [](const OutcomeHandlerContext& context, OutcomePlan& plan,
           std::string& error) {
            const std::string property =
                context.kg.getProperty(context.outcome, "attribute_ref");
            if (property.empty()) {
                error = "missing required attribute_ref";
                return false;
            }
            const std::string target_type = context.kg.getType(context.target);
            const kg::PropertyDef* definition =
                context.kg.getRegistry().findProperty(target_type, property);
            if (!definition ||
                definition->value_kind != kg::PropertyValueKind::Integer) {
                error = "target type '" + target_type +
                        "' has no integer property '" + property + "'";
                return false;
            }
            const kg::EntityRef target{context.target, ""};
            const std::string current_value =
                planned_property(context.kg, plan, target, property);
            if (current_value.empty() &&
                !context.kg.hasProperty(context.target, property)) {
                error = "target has no current value for property '" +
                        property + "'";
                return false;
            }
            int64_t current = 0;
            int64_t delta = 0;
            if (!parse_integer(current_value, property, current, error) ||
                !parse_integer(context.kg.getProperty(context.outcome,
                                                      "attribute_delta"),
                               "attribute_delta", delta, error)) {
                return false;
            }
            int64_t next = 0;
            if (!checked_add(current, delta, next)) {
                error = "attribute arithmetic overflow";
                return false;
            }
            next = clamp_to_schema(context, plan, property, next);
            plan.ops.emplace_back(kg::KGOpSetProperty{
                target, property, std::to_string(next)});
            return true;
        }, error);
    register_handler(
        "ModifyAttributesInGroup",
        [this](const OutcomeHandlerContext& context, OutcomePlan& plan,
               std::string& error) {
            return plan_attributes_in_group(
                context, plan, attribute_selector_, error);
        }, error);
    register_handler(
        "EnsureSkillLevel",
        [](const OutcomeHandlerContext& context, OutcomePlan& plan,
           std::string& error) {
            return plan_skill(context, plan, true, error);
        }, error);
    register_handler(
        "AdvanceSkill",
        [](const OutcomeHandlerContext& context, OutcomePlan& plan,
           std::string& error) {
            return plan_skill(context, plan, false, error);
        }, error);
    register_handler(
        "GainFixedMoney",
        [](const OutcomeHandlerContext& context, OutcomePlan& plan,
           std::string& error) {
            return plan_money(context, plan, false, error);
        }, error);
    register_handler(
        "GainRolledMoney",
        [](const OutcomeHandlerContext& context, OutcomePlan& plan,
           std::string& error) {
            return plan_money(context, plan, true, error);
        }, error);
    register_handler(
        "GainPossession",
        [](const OutcomeHandlerContext& context, OutcomePlan& plan,
           std::string& error) {
            return plan_possession(context, plan, error);
        }, error);
    register_handler(
        "GrantTableRoll",
        [](const OutcomeHandlerContext& context, OutcomePlan& plan,
           std::string& error) {
            EntityID table = kg::INVALID_ENTITY;
            if (!parse_entity(context.kg, context.outcome, "table",
                              "RollableTable", table, error)) {
                return false;
            }
            // An absent roll_count means one. The book writes a bare
            // "Roll on the Injury table" with no number, so the seed
            // stores no number: a 1 there would be a figure the source
            // never printed. A roll_count that IS present is still held
            // to being a positive integer.
            const std::string raw =
                context.kg.getProperty(context.outcome, "roll_count");
            int64_t count = 1;
            if (!raw.empty() &&
                (!parse_integer(raw, "roll_count", count, error) ||
                 count < 1 || count > std::numeric_limits<int>::max())) {
                if (error.empty()) error = "roll_count must be positive";
                return false;
            }
            // "Roll twice on the Injury table and take the lower
            // result." Which roll counts is part of the request, not
            // something the caller invents; an absent value means
            // every roll counts, because a bare instruction to roll
            // says nothing about choosing between results.
            TableRollSelection selection = TableRollSelection::EACH;
            const std::string wanted =
                context.kg.getProperty(context.outcome, "roll_selection");
            if (!wanted.empty()) {
                if (wanted == "EACH") {
                    selection = TableRollSelection::EACH;
                } else if (wanted == "LOWEST") {
                    selection = TableRollSelection::LOWEST;
                } else if (wanted == "HIGHEST") {
                    selection = TableRollSelection::HIGHEST;
                } else {
                    error = "GrantTableRoll has unknown roll_selection '" +
                            wanted + "'";
                    return false;
                }
            }
            if (selection != TableRollSelection::EACH && count < 2) {
                error = "roll_selection '" + wanted +
                        "' needs more than one roll to choose between";
                return false;
            }
            plan.table_roll_requests.push_back(
                {table, static_cast<int>(count), selection});
            return true;
        }, error);
}

bool OutcomeExecutor::register_handler(const std::string& concrete_type,
                                       OutcomeHandler handler,
                                       std::string& error) {
    error.clear();
    if (concrete_type == "OutcomeSequence" ||
        concrete_type == "OutcomeChoice") {
        error = "structural outcome '" + concrete_type +
                "' is managed by the executor";
        return false;
    }
    const auto& ontology = kg_.getRegistry();
    if (!ontology.hasEntityType(concrete_type)) {
        error = "unknown outcome handler type '" + concrete_type + "'";
        return false;
    }
    if (!ontology.isSubtypeOf(concrete_type, "Outcome") ||
        ontology.isAbstract(concrete_type)) {
        error = "handler type '" + concrete_type +
                "' must be a concrete Outcome";
        return false;
    }
    if (!handler) {
        error = "handler for '" + concrete_type + "' is empty";
        return false;
    }
    if (handlers_.count(concrete_type)) {
        error = "handler for '" + concrete_type + "' is already registered";
        return false;
    }
    handlers_.emplace(concrete_type, std::move(handler));
    return true;
}

OutcomeResult OutcomeExecutor::apply(
    EntityID root, const OutcomeContext& context,
    const std::vector<OutcomeSelection>& selection_list) {
    OutcomeResult result;
    if (!kg_.exists(context.target)) {
        result.error = "outcome target does not exist";
        return result;
    }

    std::unordered_map<EntityID, int> selections;
    for (const auto& selection : selection_list) {
        if (!selections.emplace(selection.choice, selection.option_index)
                 .second) {
            result.error = "duplicate selection for OutcomeChoice " +
                           std::to_string(selection.choice);
            return result;
        }
    }

    auto dice_transaction = dice_.begin_transaction();
    Planner planner{*this, context, selections};
    if (!planner.node(root)) {
        result.error = planner.error;
        return result;
    }
    if (planner.pending) {
        result.status = OutcomeStatus::PENDING_CHOICE;
        result.pending_choice = std::move(planner.pending);
        return result;
    }
    if (planner.used_selections.size() != selections.size()) {
        result.error = "selection does not belong to the executed outcome "
                       "graph";
        return result;
    }

    kg::KGOpBatchReport batch;
    if (!kg::apply_kg_ops_atomically(planner.plan.ops, kg_, batch)) {
        result.error = batch.error;
        return result;
    }
    dice_transaction.commit();

    // Past the batch, so nothing here is a rule that merely planned to
    // act. A pending choice or a refused batch returns above.
    rules_reached_.insert(planner.plan.rules_reached.begin(),
                          planner.plan.rules_reached.end());

    result.status = OutcomeStatus::APPLIED;
    result.ops_applied = batch.ops_applied;
    result.table_roll_requests =
        std::move(planner.plan.table_roll_requests);
    result.procedure_signals = std::move(planner.plan.procedure_signals);
    result.roll_ids = std::move(planner.plan.roll_ids);
    result.attributes_limited =
        std::move(planner.plan.attributes_limited);
    return result;
}

}  // namespace logosphere::rules
