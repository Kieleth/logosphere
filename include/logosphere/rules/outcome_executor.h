#ifndef LOGOSPHERE_RULES_OUTCOME_EXECUTOR_H
#define LOGOSPHERE_RULES_OUTCOME_EXECUTOR_H

// Typed rulebook outcome execution.
//
// Concrete handlers read rule data through a const KG view and append an
// OutcomePlan. They cannot mutate world state. The executor resolves
// sequences and choices, keeps dice private, applies the complete KG-op batch
// atomically, then publishes typed procedure results.

#include "logosphere/kg/kg_ops.h"
#include "logosphere/rules/planned_world.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kg { class KGModule; }
namespace logosphere::dice { class DiceService; }

namespace logosphere::rules {

enum class OutcomeStatus { APPLIED, PENDING_CHOICE, FAILED };

// Which of several rolls counts, when a rule rolls a table more than
// once. LOWEST and HIGHEST mean the lowest or highest dice TOTAL, not
// the better or worse result: on Cepheus's Injury table 1 is the worst
// outcome and 6 the mildest, so which end is kind is a fact about the
// table rather than about the rule.
enum class TableRollSelection { EACH, LOWEST, HIGHEST };

struct TableRollRequest {
    kg::EntityID table = kg::INVALID_ENTITY;
    int roll_count = 0;
    // Absent in the data means EACH: a bare "roll on the table" says
    // nothing about picking between results.
    TableRollSelection selection = TableRollSelection::EACH;
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

// A change the schema would not allow in full. Cepheus reduces
// characteristics and the schema floors them at 0, so "reduce three
// physical characteristics by 2" applied to someone already at 1 asks
// for -1. Refusing the whole rule is wrong (the book has an answer for
// exactly this: the aging crisis), and writing -1 is not allowed, so
// the change lands at the floor and says it did.
//
// The engine stops there. What hitting the floor MEANS, dying unless
// you can pay for medical care in Cepheus's case, is the game's rule.
struct AttributeLimited {
    kg::EntityID target = kg::INVALID_ENTITY;
    std::string attribute;
    int64_t requested = 0;   // what the rule asked for
    int64_t applied = 0;     // what the schema allowed
};

struct OutcomePlan {
    std::vector<kg::KGOp> ops;
    std::vector<TableRollRequest> table_roll_requests;
    std::vector<ProcedureSignal> procedure_signals;
    std::vector<uint64_t> roll_ids;
    std::vector<AttributeLimited> attributes_limited;
};

struct OutcomeHandlerContext {
    kg::EntityID outcome = kg::INVALID_ENTITY;
    kg::EntityID target = kg::INVALID_ENTITY;
    std::string outcome_type;
    const kg::KGModule& kg;
    logosphere::dice::DiceService& dice;
    const std::string& dice_stream;
    const std::string& purpose;
    // The world as this plan will leave it. A handler that reads a
    // value, changes it and writes it back MUST read it here: `kg` is
    // the committed graph and still reads as it did before the rule
    // started, so a second step of the same rule that read from `kg`
    // would silently clobber the first. Valid only for this call.
    const PlannedWorld& planned;
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
    std::vector<AttributeLimited> attributes_limited;
    std::optional<PendingChoice> pending_choice;
};

// A ModifyAttributesInGroup change whose count is smaller than its
// group: the book fixes how many attributes move and by how much, and
// leaves WHICH to whoever applies the rule. Cepheus aging prints
// "reduce two physical characteristics by 2" over a group of three.
//
// The engine will not invent that answer. It supplies the eligible
// set, the count and the resolved delta; a game installs whatever
// policy it wants, a prompt, a roll, a narrator. When the count covers
// the whole group there is nothing to choose and no selector is
// consulted.
struct AttributeSelectionRequest {
    kg::EntityID target = kg::INVALID_ENTITY;
    kg::EntityID outcome = kg::INVALID_ENTITY;
    kg::EntityID group = kg::INVALID_ENTITY;
    std::vector<std::string> eligible;
    int count = 0;
    int64_t delta = 0;

    // What this outcome has ALREADY done, which the graph cannot yet
    // be asked about. A rule like aging's "reduce two physical
    // characteristics by 2, reduce one physical characteristic by 1"
    // is two changes over one group, and the whole plan commits at
    // once at the end, so a selector querying the KG between them
    // would read the values as they were before any of it. These carry
    // the pending state instead.
    //
    // `current` is the planned value of each entry in `eligible`, in
    // the same order: what the attribute will be if nothing else
    // touches it. `already_taken` names the eligible attributes this
    // same outcome has changed already.
    //
    // Neither is enforced. A rule may legitimately hit one attribute
    // twice, so the engine reports and the chooser decides.
    std::vector<int64_t> current;
    std::vector<std::string> already_taken;
};

// Fills chosen with exactly count distinct names drawn from eligible.
// Anything else the executor refuses, so an unreliable source of
// answers cannot widen a rule.
using AttributeSelector = std::function<bool(
    const AttributeSelectionRequest&, std::vector<std::string>& chosen,
    std::string& error)>;

// Answers a branch the book prints but does not decide. The sibling of
// AttributeSelector, for the same reason: the engine knows the options
// and validates the answer, the game knows who may give one.
//
// Set `option` to an index into request.options. Returning false
// refuses the outcome. There is deliberately no default. A rule with
// nobody to answer it stops the run, because picking the first option
// silently collapses every fork the book prints into one branch and
// nothing downstream can tell that a question was skipped.
//
// The two-phase form is still there - apply() hands back
// PENDING_CHOICE and the caller re-applies with selections - and suits
// a UI that wants to suspend on the question. A resolver suits a
// caller that can answer in place, an auto-player or an LLM referee
// among them.
using ChoiceResolver = std::function<bool(
    const PendingChoice& request, int& option, std::string& error)>;

class OutcomeExecutor {
public:
    OutcomeExecutor(kg::KGModule& kg,
                    logosphere::dice::DiceService& dice);

    // Registered handlers capture this executor, so it stays put.
    OutcomeExecutor(const OutcomeExecutor&) = delete;
    OutcomeExecutor& operator=(const OutcomeExecutor&) = delete;
    OutcomeExecutor(OutcomeExecutor&&) = delete;
    OutcomeExecutor& operator=(OutcomeExecutor&&) = delete;

    bool register_handler(const std::string& concrete_type,
                          OutcomeHandler handler, std::string& error);

    void set_attribute_selector(AttributeSelector selector) {
        attribute_selector_ = std::move(selector);
    }

    // Consulted only when apply() was not already given a selection
    // for the choice in question, so an explicit answer always wins.
    void set_choice_resolver(ChoiceResolver resolver) {
        choice_resolver_ = std::move(resolver);
    }

    OutcomeResult apply(
        kg::EntityID root, const OutcomeContext& context,
        const std::vector<OutcomeSelection>& selections = {});

private:
    struct Planner;

    kg::KGModule& kg_;
    logosphere::dice::DiceService& dice_;
    std::unordered_map<std::string, OutcomeHandler> handlers_;
    AttributeSelector attribute_selector_;
    ChoiceResolver choice_resolver_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_OUTCOME_EXECUTOR_H
