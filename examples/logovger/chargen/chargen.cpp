#include "chargen/chargen.h"
#include "chargen/procedure_catalog.h"

#include "logosphere/rules/outcome_executor.h"
#include "logosphere/rules/lookup_table_selector.h"
#include "logosphere/rules/rollable_table_runner.h"
#include "logosphere/rules/task_check_runner.h"
#include "rules/ehex.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace logovger {
namespace {

// --- reading the rules out of the graph ----------------------------
//
// Everything below LOOKS UP what the book said. Nothing here decides
// what a target is; when the seed did not say, the run fails loudly.

kg::EntityID find_named(kg::KGModule& kg, const std::string& type,
                        const std::string& name) {
    for (auto id : kg.findByType(type))
        if (kg.getProperty(id, "name") == name) return id;
    return kg::INVALID_ENTITY;
}

bool entity_reference(kg::KGModule& kg, kg::EntityID owner,
                      const std::string& property,
                      const std::string& expected_type,
                      kg::EntityID& referenced, std::string& error) {
    const std::string value = kg.getProperty(owner, property);
    uint64_t parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (value.empty() || result.ec != std::errc{} ||
        result.ptr != value.data() + value.size() ||
        parsed > std::numeric_limits<kg::EntityID>::max()) {
        error = kg.getType(owner) + " has invalid or missing required " +
                property + " entity reference";
        return false;
    }
    referenced = static_cast<kg::EntityID>(parsed);
    if (!kg.exists(referenced) ||
        !kg.getRegistry().isSubtypeOf(kg.getType(referenced),
                                      expected_type)) {
        error = kg.getType(owner) + "." + property +
                " does not reference a " + expected_type;
        return false;
    }
    return true;
}

struct TaskCheckReference {
    kg::EntityID id = kg::INVALID_ENTITY;
    std::string attribute;
    int64_t target = 0;
};

bool task_check_reference(kg::KGModule& kg, kg::EntityID career,
                          const std::string& property,
                          TaskCheckReference& check, std::string& error) {
    if (!entity_reference(kg, career, property, "TaskCheck", check.id,
                          error)) {
        return false;
    }
    check.attribute = kg.getProperty(check.id, "attribute_ref");
    const std::string target = kg.getProperty(check.id, "target_number");
    const auto result = std::from_chars(
        target.data(), target.data() + target.size(), check.target);
    if (check.attribute.empty() || target.empty() ||
        result.ec != std::errc{} ||
        result.ptr != target.data() + target.size()) {
        error = "TaskCheck has invalid or missing required display data";
        return false;
    }
    return true;
}

std::string check_detail(
    const logosphere::rules::TaskCheckExecution& execution) {
    std::ostringstream detail;
    detail << execution.roll().expression.to_string() << " = "
           << execution.roll().total
           << (execution.modifier() >= 0 ? " +" : " ")
           << execution.modifier() << " DM -> " << execution.total()
           << " vs " << execution.target_number() << "+";
    // A throw the dice killed says so, because the numbers alone read
    // as a pass and a reader would think the engine had miscounted.
    if (execution.failed_on_natural()) {
        detail << " (natural " << execution.natural_total()
               << ": always a failure)";
    }
    return detail.str();
}

// The six characteristics are written ONCE, when they are rolled.
// After that the graph owns them: Personal Development raises them,
// aging will lower them, and every one of those changes goes through
// the outcome executor and lands on the entity. Writing the session's
// cached copies back afterwards silently undid all of it, so a "+1
// Str" was applied and then overwritten at the end of the term.
void write_characteristics(kg::KGModule& kg, const CharacterSheet& s) {
    kg.setProperty(s.id, "strength",        std::to_string(s.strength));
    kg.setProperty(s.id, "dexterity",       std::to_string(s.dexterity));
    kg.setProperty(s.id, "endurance",       std::to_string(s.endurance));
    kg.setProperty(s.id, "intelligence",    std::to_string(s.intelligence));
    kg.setProperty(s.id, "education",       std::to_string(s.education));
    kg.setProperty(s.id, "social_standing", std::to_string(s.social_standing));
    kg.setProperty(s.id, "upp",             s.upp);
}

// What the SESSION owns: how long this has taken.
void write_sheet(kg::KGModule& kg, const CharacterSheet& s) {
    kg.setProperty(s.id, "age_years",       std::to_string(s.age_years));
    kg.setProperty(s.id, "terms_served",    std::to_string(s.terms_served));
}

// Read the characteristics back out of the graph, where they may have
// been changed by an outcome, and recompute the UPP from them. Call it
// after anything that could have touched them.
// The money and the goods are not the sheet's to hold: they live in
// the character's CurrencyBalance and PossessionHolding parts, and the
// sheet only mirrors them. Re-derived from those parts every time the
// sheet is refreshed, because ANY rule can move money, not only
// mustering out. Deriving it in one place is the whole point: the
// mirror was re-read at muster-out alone, so a mishap's fine left the
// sheet Cr10000 richer than the character, and the aging crisis then
// quoted a purse that was not there.
void read_holdings(const kg::KGModule& kg, CharacterSheet& s) {
    s.credits = 0;
    s.possessions.clear();
    for (const auto part : kg.getRelated(s.id, "HAS_PART")) {
        const std::string type = kg.getType(part);
        if (type == "CurrencyBalance") {
            const auto amount = kg.getProperty(part, "balance_amount");
            if (!amount.empty()) s.credits += std::stoll(amount);
        } else if (type == "PossessionHolding") {
            const auto ref = kg.getProperty(part, "possession");
            const auto count = kg.getProperty(part, "possession_count");
            if (ref.empty()) continue;
            const auto name = kg.getProperty(
                static_cast<kg::EntityID>(std::stoul(ref)), "name");
            if (name.empty()) continue;
            s.possessions.push_back(
                count.empty() || count == "1" ? name : count + "x " + name);
        }
    }
}

void read_characteristics(const kg::KGModule& kg, CharacterSheet& s) {
    read_holdings(kg, s);
    const auto value = [&](const char* slot, int& into) {
        const std::string text = kg.getProperty(s.id, slot);
        if (!text.empty()) into = std::stoi(text);
    };
    value("strength", s.strength);
    value("dexterity", s.dexterity);
    value("endurance", s.endurance);
    value("intelligence", s.intelligence);
    value("education", s.education);
    value("social_standing", s.social_standing);
    // Age is the graph's too. Mishap 5 is "Injured... and imprisoned
    // for 4 years", and that ModifyAttribute lands on the entity like
    // any other outcome - then the session wrote its own stale copy
    // back over it, so four years of prison cost nothing and the
    // character came out at 20 instead of 24.
    value("age_years", s.age_years);
    s.upp = upp(s.strength, s.dexterity, s.endurance, s.intelligence,
                s.education, s.social_standing);
    kg.getProperty(s.id, "upp") == s.upp
        ? void()
        : const_cast<kg::KGModule&>(kg).setProperty(s.id, "upp", s.upp);
}

// A step consults a table the SCHEMA names, and that table's rows say
// which subject each is about. Nothing here matches a table by name:
// the step carries subject_table, the row carries subject, and this
// walks from one to the other.
kg::EntityID subject_row(const kg::KGModule& kg, kg::EntityID step,
                         kg::EntityID subject, const char* result_slot,
                         std::string& error) {
    const std::string table_ref = kg.getProperty(step, "subject_table");
    if (table_ref.empty()) {
        error = "this step names no subject_table";
        return kg::INVALID_ENTITY;
    }
    kg::EntityID table = kg::INVALID_ENTITY;
    try {
        table = static_cast<kg::EntityID>(std::stoul(table_ref));
    } catch (...) {
        error = "step's subject_table is not an entity reference";
        return kg::INVALID_ENTITY;
    }
    for (auto row : kg.getRelated(table, "HAS_PART")) {
        if (kg.getProperty(row, "subject") != std::to_string(subject)) {
            continue;
        }
        const std::string result = kg.getProperty(row, result_slot);
        if (result.empty()) {
            error = "the row for this subject carries no " +
                    std::string(result_slot);
            return kg::INVALID_ENTITY;
        }
        try {
            return static_cast<kg::EntityID>(std::stoul(result));
        } catch (...) {
            error = "the row's " + std::string(result_slot) +
                    " is not an entity reference";
            return kg::INVALID_ENTITY;
        }
    }
    error = "no row in this step's table is about " +
            kg.getProperty(subject, "name");
    return kg::INVALID_ENTITY;
}

// Every row of the step's table that is about this subject. The
// training rule offers four tables per career, so one subject has
// several rows, unlike the throw rules where it has exactly one.
std::vector<kg::EntityID> subject_rows(const kg::KGModule& kg,
                                       kg::EntityID step,
                                       kg::EntityID subject,
                                       const char* result_slot,
                                       std::string& error) {
    std::vector<kg::EntityID> out;
    const std::string table_ref = kg.getProperty(step, "subject_table");
    if (table_ref.empty()) {
        error = "this step names no subject_table";
        return out;
    }
    kg::EntityID table = kg::INVALID_ENTITY;
    try {
        table = static_cast<kg::EntityID>(std::stoul(table_ref));
    } catch (...) {
        error = "step's subject_table is not an entity reference";
        return out;
    }
    for (auto row : kg.getRelated(table, "HAS_PART")) {
        if (kg.getProperty(row, "subject") != std::to_string(subject)) {
            continue;
        }
        const std::string result = kg.getProperty(row, result_slot);
        if (result.empty()) continue;
        try {
            out.push_back(static_cast<kg::EntityID>(std::stoul(result)));
        } catch (...) {
            error = "a row's " + std::string(result_slot) +
                    " is not an entity reference";
            return {};
        }
    }
    if (out.empty()) {
        error = "no row in this step's table is about " +
                kg.getProperty(subject, "name");
    }
    return out;
}

// Select a career training row, then apply its structured outcome. These are
// separate engine operations: the recorded selection remains a fact if
// outcome application fails. There is no default skill level in procedure
// code.
// An outcome that did not apply has two very different reasons, and
// only one of them fills in `error`. A PENDING_CHOICE is the executor
// saying "someone has to decide first", and it carries no error text,
// so reporting it through the failure path produced a message that
// stopped dead at the colon. Say which it is.
std::string outcome_failure(const logosphere::rules::OutcomeResult& applied) {
    if (applied.status == logosphere::rules::OutcomeStatus::PENDING_CHOICE) {
        return "the rule stops for a choice that chargen cannot yet "
               "present, so it cannot be applied here";
    }
    return applied.error.empty() ? "no reason given" : applied.error;
}

bool apply_training_table(kg::KGModule& kg, kg::EntityID table,
                          kg::EntityID character,
                          logosphere::dice::DiceService& dice,
                          logosphere::rules::OutcomeExecutor& executor,
                          uint64_t& roll_id_out, std::string& gained,
                          std::string& error, bool* was_skill = nullptr) {
    gained.clear();
    error.clear();

    logosphere::rules::RollableTableRunner table_runner(kg, dice);
    const auto selected = table_runner.select(
        table, "chargen", "skills and training");
    if (!selected.ok()) {
        error = "skills and training selection failed: " + selected.error;
        return false;
    }
    roll_id_out = selected.selection->roll().id;

    // A training row is not always a skill. Personal Development
    // grants characteristics ("+1 Dex"), and the executor applies
    // whatever the row carries; only the REPORT differs, because a
    // skill is worth naming with its new level and a characteristic
    // is not.
    const auto outcome = selected.selection->outcome();
    const bool grants_skill =
        kg.getRegistry().isSubtypeOf(kg.getType(outcome), "AdvanceSkill");
    if (was_skill) *was_skill = grants_skill;
    kg::EntityID skill = kg::INVALID_ENTITY;
    std::string skill_ref;
    std::string skill_name;
    if (grants_skill) {
        if (!entity_reference(kg, outcome, "skill", "Skill", skill, error)) {
            error = "skills and training outcome: " + error;
            return false;
        }
        skill_ref = kg.getProperty(outcome, "skill");
        skill_name = kg.getProperty(skill, "name");
        if (skill_name.empty()) {
            error = "skills and training outcome points to an unnamed skill";
            return false;
        }
    }

    const auto applied = executor.apply(
        outcome, {character, "chargen", "skills and training"});
    if (applied.status != logosphere::rules::OutcomeStatus::APPLIED) {
        error = "skills and training outcome failed: " +
                outcome_failure(applied);
        return false;
    }

    if (!grants_skill) {
        gained = kg.getProperty(outcome, "name");
        const auto cut = gained.rfind(": ");
        if (cut != std::string::npos) gained = gained.substr(cut + 2);
        return true;
    }

    int matches = 0;
    for (auto part : kg.getRelated(character, "HAS_PART")) {
        if (kg.getProperty(part, "skill") != skill_ref) continue;
        ++matches;
        const auto level = kg.getProperty(part, "skill_level");
        if (level.empty()) {
            error = "executed skill outcome created a rating without "
                    "skill_level";
            return false;
        }
        gained = skill_name + "-" + level;
    }
    if (matches != 1) {
        error = "executed skill outcome left " +
                std::to_string(matches) +
                " ratings for the selected skill";
        return false;
    }
    return true;
}

std::string lowercase(std::string value) {
    for (auto& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

const Choice* find_choice(const std::vector<Choice>& choices,
                          const std::string& answer) {
    const std::string normalized = lowercase(answer);
    for (const auto& choice : choices) {
        if (normalized == lowercase(choice.key) ||
            normalized == lowercase(choice.label)) {
            return &choice;
        }
    }
    return nullptr;
}

}  // namespace

// --- the session: a life, one decision at a time -------------------

ChargenSession::ChargenSession(kg::KGModule& kg,
                               logosphere::dice::DiceService& dice)
    : kg_(kg), dice_(dice),
      primitives_(make_chargen_procedure_registry()),
      runner_(kg_, primitives_),
      executor_(kg_, dice_) {
    bind_primitives();
    register_outcome_handlers();
}

// What a career ending, or benefits being forfeited, MEANS is game
// policy: the engine only dispatches the typed Outcome. Both are
// reported rather than applied, because neither writes to the graph.
// They raise a fact the procedure step above reads once the whole
// outcome has committed.
//
// Registering them is not decoration. An OutcomeSequence plans every
// child and commits once, so an unhandled EndCareer at step 0 fails
// the whole row and discards the money and the injury after it. Every
// mishap row but one carries one of these, which is why the mishap
// table could not run at all before now.
void ChargenSession::register_outcome_handlers() {
    std::string error;
    const auto report = [&](const char* type) {
        if (!executor_.register_handler(
                type,
                [](const logosphere::rules::OutcomeHandlerContext& context,
                   logosphere::rules::OutcomePlan& plan, std::string&) {
                    plan.procedure_signals.push_back(
                        {context.outcome_type, context.outcome,
                         context.target});
                    return true;
                },
                error)) {
            // A registry that refuses a handler is a broken build, not
            // a condition to survive at runtime.
            throw std::runtime_error(
                std::string("chargen could not register the ") + type +
                " handler: " + error);
        }
    };
    report("EndCareer");
    report("ForfeitBenefits");
}

void ChargenSession::bind_primitives() {
    std::string error;
    // Every primitive is wrapped, so a step that DECLARES an outcome
    // has it applied without the primitive knowing. "Increase your age
    // by 4 years" is the whole of what its checklist step does, and it
    // was `age_years += 4` inside the primitive: a rule the graph could
    // hold, written where no reader of the procedure could find it.
    // Applied on first entry only, so a step that suspends for an
    // answer does not charge for it twice.
    const auto bind = [&](const std::string& name,
                          logosphere::rules::ProcedurePrimitive primitive) {
        auto wrapped =
            [this, inner = std::move(primitive)](
                const PrimitiveContext& context) -> PrimitiveResult {
            if (!context.input) {
                const std::string declared =
                    kg_.getProperty(context.step, "outcome");
                if (!declared.empty()) {
                    kg::EntityID outcome = kg::INVALID_ENTITY;
                    try {
                        outcome =
                            static_cast<kg::EntityID>(std::stoul(declared));
                    } catch (...) {
                        return PrimitiveResult::failed(
                            "this step's outcome is not an entity "
                            "reference");
                    }
                    const auto applied = executor_.apply(
                        outcome, {sheet_.id, "chargen", "step"});
                    if (applied.status !=
                        logosphere::rules::OutcomeStatus::APPLIED) {
                        return PrimitiveResult::failed(
                            "step outcome: " + outcome_failure(applied));
                    }
                    read_characteristics(kg_, sheet_);
                }
            }
            return inner(context);
        };
        if (!primitives_.bind_primitive(name, std::move(wrapped), error)) {
            throw std::logic_error(error);
        }
    };
    bind("generate_characteristics", [this](const PrimitiveContext& context) {
        return generate_characteristics(context);
    });
    bind("choose_career", [this](const PrimitiveContext& context) {
        return choose_career(context);
    });
    bind("roll_qualification", [this](const PrimitiveContext& context) {
        return roll_qualification(context);
    });
    bind("draft_or_drifter", [this](const PrimitiveContext& context) {
        return draft_or_drifter(context);
    });
    bind("roll_survival", [this](const PrimitiveContext& context) {
        return roll_survival(context);
    });
    bind("roll_training", [this](const PrimitiveContext& context) {
        return roll_training(context);
    });
    bind("advance_term", [this](const PrimitiveContext& context) {
        return advance_term(context);
    });

    bind("roll_aging", [this](const PrimitiveContext& context) {
        return roll_aging(context);
    });
    bind("survival_mishap", [this](const PrimitiveContext& context) {
        return survival_mishap(context);
    });
    bind("muster_out", [this](const PrimitiveContext& context) {
        return muster_out(context);
    });
    bind("basic_training", [this](const PrimitiveContext& context) {
        return basic_training(context);
    });
    bind("roll_commission", [this](const PrimitiveContext& context) {
        return roll_promotion(context, true);
    });
    bind("roll_advancement", [this](const PrimitiveContext& context) {
        return roll_promotion(context, false);
    });
    bind("roll_reenlistment", [this](const PrimitiveContext& context) {
        return roll_reenlistment(context);
    });
    bind("choose_term_end", [this](const PrimitiveContext& context) {
        return choose_term_end(context);
    });
    bind("finish_character", [this](const PrimitiveContext& context) {
        return finish_character(context);
    });
}

// A number the book fixes, read from the graph rather than typed here.
// Missing, duplicate, or malformed rule data is an error, never a default.
bool ChargenSession::constant(const char* name, int& value,
                              std::string& error) const {
    kg::EntityID match = kg::INVALID_ENTITY;
    for (auto id : kg_.findByType("RuleConstant")) {
        if (kg_.getProperty(id, "name") != name) continue;
        if (match != kg::INVALID_ENTITY) {
            error = "multiple RuleConstant entities named '" +
                    std::string(name) + "'";
            return false;
        }
        match = id;
    }
    if (match == kg::INVALID_ENTITY) {
        error = "missing required RuleConstant '" + std::string(name) + "'";
        return false;
    }
    const std::string encoded = kg_.getProperty(match, "constant_value");
    const auto parsed = std::from_chars(
        encoded.data(), encoded.data() + encoded.size(), value);
    if (encoded.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != encoded.data() + encoded.size()) {
        error = "RuleConstant '" + std::string(name) +
                "' has invalid constant_value '" + encoded + "'";
        return false;
    }
    return true;
}

bool ChargenSession::begin(uint64_t seed, std::string& error) {
    error.clear();
    sheet_ = CharacterSheet{};
    finished_ = false;
    career_ = kg::INVALID_ENTITY;
    procedure_ = kg::INVALID_ENTITY;
    cursor_ = {};
    choices_.clear();
    prompt_.clear();
    finish_reason_.clear();
    drained_ = 0;
    dice_.seed_stream("chargen", seed);

    procedure_ = find_named(kg_, "Procedure", "basic_chargen");
    if (procedure_ == kg::INVALID_ENTITY) {
        error = "the world has no Procedure named 'basic_chargen'; load "
                "the basic chargen procedure seed";
        return false;
    }
    if (!runner_.validate(procedure_, error)) return false;

    sheet_.id = kg_.createEntity("Character");
    if (sheet_.id == kg::INVALID_ENTITY) {
        error = "the world does not know what a Character is; load the "
                "cepheus character-creation pack";
        return false;
    }
    return accept(runner_.start(procedure_, sheet_.id), error);
}

bool ChargenSession::accept(logosphere::rules::ProcedureResult result,
                            std::string& error) {
    if (result.status == logosphere::rules::ProcedureStatus::FAILED) {
        error = result.error;
        return false;
    }
    if (result.status == logosphere::rules::ProcedureStatus::PENDING) {
        cursor_ = result.cursor;
        prompt_ = std::move(result.prompt);
        choices_ = std::move(result.choices);
        return true;
    }
    finished_ = true;
    choices_.clear();
    return true;
}

ChargenSession::PrimitiveResult ChargenSession::generate_characteristics(
    const PrimitiveContext& context) {
    if (context.target != sheet_.id) {
        return PrimitiveResult::failed(
            "generate_characteristics received the wrong target");
    }
    // Six characteristics, 2D6 each, in the book's order.
    int* scores[] = {&sheet_.strength, &sheet_.dexterity, &sheet_.endurance,
                     &sheet_.intelligence, &sheet_.education,
                     &sheet_.social_standing};
    const char* names[] = {"Str", "Dex", "End", "Int", "Edu", "Soc"};
    logosphere::dice::DiceExpression two_d6;
    two_d6.count = 2;
    two_d6.sides = 6;
    for (int i = 0; i < 6; ++i) {
        const auto r = dice_.roll(two_d6, "chargen",
                                  std::string("characteristic ") + names[i]);
        *scores[i] = r.total;
        sheet_.life.push_back({0, std::string("rolled ") + names[i],
                               std::to_string(r.total), r.id});
    }
    sheet_.upp = upp(sheet_.strength, sheet_.dexterity, sheet_.endurance,
                     sheet_.intelligence, sheet_.education,
                     sheet_.social_standing);
    sheet_.life.push_back({0, "UPP", sheet_.upp, 0});
    write_characteristics(kg_, sheet_);
    write_sheet(kg_, sheet_);
    return PrimitiveResult::advance();
}

bool ChargenSession::offer_careers(std::string& error) {
    choices_.clear();
    // Numbered, not lettered: the book offers 24 careers and a single
    // letter runs out at 16, which silently made eight of them
    // unchoosable.
    size_t n = 0;
    for (auto id : kg_.findByType("Career")) {
        const auto name = kg_.getProperty(id, "name");
        if (name.empty()) continue;
        // A career already left cannot be chosen again. Draft outcomes may
        // still assign one; Drifter is always available.
        if (name != "Drifter" &&
            std::find(sheet_.careers_served.begin(),
                      sheet_.careers_served.end(),
                      name) != sheet_.careers_served.end()) {
            continue;
        }
        TaskCheckReference qualification;
        TaskCheckReference survival;
        if (!task_check_reference(kg_, id, "qualification_check",
                                  qualification, error) ||
            !task_check_reference(kg_, id, "survival_check", survival,
                                  error)) {
            error = "career '" + name + "': " + error;
            return false;
        }
        choices_.push_back({std::to_string(n + 1), name,
                            "qualify on " + qualification.attribute + " " +
                                std::to_string(qualification.target) +
                                "+, survive on " + survival.attribute + " " +
                                std::to_string(survival.target) + "+"});
        ++n;
    }
    prompt_ = "Which career do you try for?";
    return true;
}

void ChargenSession::finish(const std::string& why) {
    finished_ = true;
    choices_.clear();
    prompt_ = why;
    write_sheet(kg_, sheet_);
}

ChargenSession::PrimitiveResult ChargenSession::choose_career(
    const PrimitiveContext& context) {
    if (!context.input) {
        std::string error;
        int cap = 0;
        if (!constant("max_terms", cap, error)) {
            return PrimitiveResult::failed(error);
        }
        if (cap <= 0) {
            return PrimitiveResult::failed(
                "RuleConstant 'max_terms' must be positive");
        }
        if (sheet_.terms_served >= cap) {
            finish_reason_ = std::to_string(cap) +
                             " terms served: the book allows no more.";
            return PrimitiveResult::advance("finish");
        }
        if (!offer_careers(error)) return PrimitiveResult::failed(error);
        if (choices_.empty()) {
            return PrimitiveResult::failed(
                "no Career in the knowledge graph; load a careers seed");
        }
        // "...or to step 12 if you wish to finish your character."
        // Only once something has been lived: there is no finishing
        // a character who has not started.
        if (!sheet_.careers_served.empty()) {
            choices_.push_back({"finish", "Finish the character",
                                "stop here and keep what you have"});
            prompt_ = "Out at age " + std::to_string(sheet_.age_years) +
                      " after " + std::to_string(sheet_.terms_served) +
                      " term(s). Another career, or finish?";
        }
        return PrimitiveResult::pending(prompt_, choices_);
    }
    const Choice* picked = find_choice(choices_, *context.input);
    if (!picked) {
        return PrimitiveResult::failed("'" + *context.input +
                                       "' is not one of the options");
    }
    if (picked->key == "finish") {
        choices_.clear();
        finish_reason_ =
            "Finished at age " + std::to_string(sheet_.age_years) +
            " after " + std::to_string(sheet_.terms_served) +
            " term(s) across " +
            std::to_string(sheet_.careers_served.size()) + " career(s).";
        return PrimitiveResult::advance("finish");
    }
    career_ = find_named(kg_, "Career", picked->label);
    if (career_ == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed("no Career named '" + picked->label +
                                       "'");
    }
    sheet_.career = picked->label;
    sheet_.careers_served.push_back(picked->label);
    enter_career();
    choices_.clear();
    return PrimitiveResult::advance();
}

ChargenSession::PrimitiveResult ChargenSession::roll_qualification(
    const PrimitiveContext&) {
    if (career_ == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed(
            "roll_qualification has no selected career");
    }
    // "The character automatically fails any Qualification checks from
    // now on." A crisis survivor does not get to roll: the book says
    // the check fails, not that it is made at a penalty, so no die is
    // spent on it and none is cited.
    if (kg_.getProperty(sheet_.id, "qualification_barred") == "true") {
        sheet_.qualified = false;
        sheet_.life.push_back(
            {sheet_.terms_served, "turned away by " + sheet_.career,
             "no service takes them after the crisis", 0});
        return PrimitiveResult::advance("failed");
    }
    TaskCheckReference check;
    std::string error;
    if (!task_check_reference(kg_, career_, "qualification_check", check,
                              error)) {
        return PrimitiveResult::failed("career '" + sheet_.career +
                                       "': " + error);
    }
    logosphere::rules::TaskCheckRunner runner(kg_, dice_);
    const auto qualification = runner.run(
        check.id, sheet_.id, "chargen", "qualification");
    if (!qualification.ok()) {
        return PrimitiveResult::failed(qualification.error);
    }
    const auto& execution = *qualification.execution;
    sheet_.qualified = execution.passed();
    sheet_.life.push_back(
        {0, execution.passed() ? "qualified" : "failed to qualify",
         check_detail(execution), execution.roll().id});
    if (execution.passed()) return PrimitiveResult::advance("passed");

    // "You must either submit to the Draft or take the Drifter career
    // for this term." Drifter is always open. The next procedure step
    // offers that choice and executes the seeded Draft table if selected.
    sheet_.life.push_back(
        {sheet_.terms_served, "turned away by the " + sheet_.career, "",
         0});
    turned_away_from_ = sheet_.career;
    career_ = kg::INVALID_ENTITY;
    sheet_.career.clear();
    if (!sheet_.careers_served.empty()) sheet_.careers_served.pop_back();
    return PrimitiveResult::advance("failed");
}

// "You must either submit to the Draft or take the Drifter career for
// this term." Not the whole list again: the term is already spent, and
// these are the two ways to spend it.
ChargenSession::PrimitiveResult ChargenSession::draft_or_drifter(
    const PrimitiveContext& context) {
    if (!context.input) {
        choices_ = {
            {"1", "Take the Drifter career",
             "always open, whatever else has refused you"},
            {"2", "Submit to the Draft",
             "1D6 decides which service takes you"},
        };
        prompt_ = "The " + turned_away_from_ +
                  " will not have you this term. The book gives you two "
                  "ways to spend it.";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    const Choice* picked = find_choice(choices_, *context.input);
    if (!picked) {
        return PrimitiveResult::failed("'" + *context.input +
                                       "' is not one of the options");
    }
    choices_.clear();

    std::string career_name = "Drifter";
    kg::EntityID selected_career = kg::INVALID_ENTITY;
    if (picked->key == "2") {
        // The Draft is a table in the book, so it is a table in the
        // graph: 1D6 across six services, rolled by the engine, and
        // the row's typed outcome names the career you are taken by.
        // You do not choose, which is the whole point of a draft.
        const auto table = find_named(kg_, "RollableTable", "Draft Career");
        if (table == kg::INVALID_ENTITY) {
            return PrimitiveResult::failed(
                "the Draft table is not in the graph; load the careers "
                "seed");
        }
        logosphere::rules::RollableTableRunner runner(kg_, dice_);
        const auto drafted = runner.select(table, "chargen", "the Draft");
        if (!drafted.ok()) {
            return PrimitiveResult::failed("the Draft failed: " +
                                           drafted.error);
        }
        std::string error;
        if (!entity_reference(kg_, drafted.selection->outcome(),
                              "drafted_career", "Career",
                              selected_career, error)) {
            return PrimitiveResult::failed("Draft outcome: " + error);
        }
        career_name = kg_.getProperty(selected_career, "name");
        if (career_name.empty()) {
            return PrimitiveResult::failed(
                "Draft outcome references an unnamed Career");
        }
        sheet_.life.push_back(
            {sheet_.terms_served, "drafted into the " + career_name,
             "1D6 = " + std::to_string(drafted.selection->roll().total),
             drafted.selection->roll().id});
    }
    if (picked->key != "2") {
        selected_career = find_named(kg_, "Career", career_name);
        if (selected_career == kg::INVALID_ENTITY) {
            return PrimitiveResult::failed(
                "the Drifter career is not in the graph, and the book says "
                "it is always open");
        }
    }
    career_ = selected_career;
    sheet_.career = career_name;
    sheet_.careers_served.push_back(career_name);
    enter_career();
    if (picked->key != "2")
        sheet_.life.push_back({sheet_.terms_served, "became a Drifter",
                               "the career that is always open", 0});
    return PrimitiveResult::advance("took_it");
}

ChargenSession::PrimitiveResult ChargenSession::roll_survival(
    const PrimitiveContext&) {
    const int term = sheet_.terms_served + 1;
    TaskCheckReference check;
    std::string error;
    if (!task_check_reference(kg_, career_, "survival_check", check,
                              error)) {
        return PrimitiveResult::failed("career '" + sheet_.career +
                                       "': " + error);
    }
    // "Each career has a survival roll. If you fail this roll, your
    // character is dead... A natural 2 is always a failure." The number
    // lives in the graph rather than here, because it is the book's and
    // a Referee may change it. Missing is a hard stop: a survival throw
    // that quietly lost its floor kills nobody it should.
    int natural_failure = 0;
    if (!constant("survival_natural_failure", natural_failure, error)) {
        return PrimitiveResult::failed(error);
    }
    logosphere::rules::TaskCheckOptions options;
    options.natural_failure_at_or_below = natural_failure;
    logosphere::rules::TaskCheckRunner runner(kg_, dice_);
    const auto survival = runner.run(
        check.id, sheet_.id, "chargen", "survival", options);
    if (!survival.ok()) return PrimitiveResult::failed(survival.error);
    const auto& execution = *survival.execution;
    sheet_.life.push_back(
        {term, execution.passed() ? "survived" : "did not survive",
         check_detail(execution), execution.roll().id});
    if (!execution.passed()) {
        finish_reason_ =
            "The career ended here: a failed survival roll is death, "
            "unless the Referee allows the mishap table instead.";
        return PrimitiveResult::advance("failed");
    }
    return PrimitiveResult::advance("passed");
}

// Step 7. "Choose one of the Skills and Training tables for this
// career and roll on it." The choice is the player's and the book
// gives four: Personal Development, Service Skills, Specialist and
// Adv Education. Rolling the service table automatically, as this did
// before, skipped a decision the book makes every single term.
// Multivalued strings are stored "a; b; c", the separator this
// ontology documents on every list-valued slot.
std::vector<std::string> split_refs(const std::string& value) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= value.size()) {
        const size_t cut = value.find(';', at);
        const size_t end = cut == std::string::npos ? value.size() : cut;
        size_t first = at, last = end;
        while (first < last &&
               std::isspace(static_cast<unsigned char>(value[first]))) ++first;
        while (last > first &&
               std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
        if (last > first) out.push_back(value.substr(first, last - first));
        if (cut == std::string::npos) break;
        at = cut + 1;
    }
    return out;
}

// The characteristics, as the book groups them, read from the graph.
// "If any characteristic is reduced to 0" and "will bring any
// characteristics back up to 1" are rules about the WHOLE set, and
// naming the six in C++ wrote that set out four separate times: a
// seventh characteristic, or a book that has five, would have needed
// every one of them edited.
std::vector<std::string> ChargenSession::characteristic_slots(
    std::string& error) const {
    for (const auto id : kg_.findByType("AttributeGroup")) {
        if (kg_.getProperty(id, "name") != "characteristic characteristics") {
            continue;
        }
        auto slots = split_refs(kg_.getProperty(id, "attribute_refs"));
        if (slots.empty()) {
            error = "the characteristic AttributeGroup names no attributes";
        }
        return slots;
    }
    error = "no AttributeGroup for the characteristics; load the shared "
            "tables seed";
    return {};
}

// The tables this character may roll on RIGHT NOW. Recomputed every
// time it is asked, because a training roll can change the answer: a
// table may ask something of whoever rolls on it ("You may only roll
// on the Advanced Education table if your character has Education
// 8+"), and "+1 Edu" can meet that requirement mid-term. The
// requirement lives on the table, so this reads it rather than knowing
// which table is which.
std::vector<kg::EntityID> ChargenSession::eligible_training_tables(
    kg::EntityID step, std::string& error) {
    auto tables = subject_rows(kg_, step, career_, "rollable_table", error);
    tables.erase(
        std::remove_if(tables.begin(), tables.end(),
            [this](kg::EntityID table) {
                const std::string attribute =
                    kg_.getProperty(table, "requires_attribute");
                const std::string minimum =
                    kg_.getProperty(table, "requires_minimum");
                if (attribute.empty() || minimum.empty()) return false;
                const std::string have =
                    kg_.getProperty(sheet_.id, attribute);
                if (have.empty()) return true;   // cannot show it: refuse
                try {
                    return std::stoll(have) < std::stoll(minimum);
                } catch (...) {
                    return true;
                }
            }),
        tables.end());
    if (tables.empty() && error.empty()) {
        error = "every table this career offers is out of reach for this "
                "character";
    }
    return tables;
}

ChargenSession::PrimitiveResult ChargenSession::roll_training(
    const PrimitiveContext& context) {
    const int term = sheet_.terms_served + 1;
    std::string table_error;
    // The term's OWN roll, added once when the step is first entered.
    // It used to be granted only when nothing else was owed, so a
    // promotion's extra roll REPLACED it: "roll on the skills tables
    // for an extra skill" (checklist 6.2) is on top of the term's roll
    // (7.1), and every promoted character was a skill short.
    if (!context.input) training_rolls_owed_ += 1;
    const auto options = eligible_training_tables(context.step, table_error);
    if (options.empty()) {
        return PrimitiveResult::failed("skills and training: " +
                                       table_error);
    }
    if (!context.input) {
        choices_.clear();
        for (size_t i = 0; i < options.size(); ++i) {
            choices_.push_back(
                {std::to_string(i + 1),
                 kg_.getProperty(options[i], "name"),
                 "roll 1D6 on it", options[i]});
        }
        prompt_ = "Term " + std::to_string(term) +
                  ": which table do you train on? (click one to read "
                  "what it can give you)";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    const Choice* picked = find_choice(choices_, *context.input);
    if (!picked) {
        return PrimitiveResult::failed("'" + *context.input +
                                       "' is not one of the tables");
    }
    // The answer names a TABLE, not a position in a list. Resolving it
    // by index into a list recomputed on resume let the answer land on
    // a different table than the one offered: take Personal
    // Development, roll "+1 Edu" from 7 to 8, and Advanced Education
    // joins the list for the follow-up roll and shifts everything after
    // it. The offer already carries the entity it means, so use that.
    const kg::EntityID chosen = picked->subject;
    if (std::find(options.begin(), options.end(), chosen) == options.end()) {
        return PrimitiveResult::failed(
            "'" + picked->label + "' is not a table this character may "
            "roll on");
    }
    choices_.clear();
    uint64_t skill_roll = 0;
    std::string skill;
    std::string error;
    bool granted_a_skill = true;
    if (!apply_training_table(kg_, chosen, sheet_.id, dice_, executor_,
                              skill_roll,
                              skill, error, &granted_a_skill)) {
        return PrimitiveResult::failed(error);
    }
    // A characteristic gain lands on the entity, not on the skill
    // list, so the sheet has to read it back or it never shows.
    if (granted_a_skill) sheet_.skills.push_back(skill);
    else read_characteristics(kg_, sheet_);
    sheet_.life.push_back({term, "gained " + skill,
                           kg_.getProperty(chosen, "name"), skill_roll});
    // A promotion buys another roll, and so does a career with no
    // hierarchy to climb. Stay on this step until they are spent, and
    // ASK again rather than re-reading the answer already given.
    if (--training_rolls_owed_ > 0) {
        // Offered fresh, because the roll just taken can change what
        // the character may roll on next: "+1 Edu" from 7 to 8 opens
        // Advanced Education, and reusing the list from before the
        // gain would keep offering the old four.
        const auto next = eligible_training_tables(context.step,
                                                   table_error);
        if (next.empty()) {
            return PrimitiveResult::failed("skills and training: " +
                                           table_error);
        }
        choices_.clear();
        for (size_t i = 0; i < next.size(); ++i) {
            choices_.push_back({std::to_string(i + 1),
                                kg_.getProperty(next[i], "name"),
                                "roll 1D6 on it", next[i]});
        }
        prompt_ = "Term " + std::to_string(term) + ": " +
                  std::to_string(training_rolls_owed_) +
                  " more training roll(s). Which table?";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    return PrimitiveResult::advance();
}

// The service skills a career teaches, in table order. Only rows that
// grant a skill count: a service table is all skills, but reading the
// outcome rather than assuming keeps that true if a book disagrees.
std::vector<std::pair<kg::EntityID, std::string>> service_skills(
    const kg::KGModule& kg, kg::EntityID table) {
    std::vector<std::pair<kg::EntityID, std::string>> out;
    for (auto row : kg.getRelated(table, "HAS_PART")) {
        const std::string outcome_ref = kg.getProperty(row, "outcome");
        if (outcome_ref.empty()) continue;
        kg::EntityID outcome = kg::INVALID_ENTITY;
        try {
            outcome = static_cast<kg::EntityID>(std::stoul(outcome_ref));
        } catch (...) { continue; }
        if (!kg.getRegistry().isSubtypeOf(kg.getType(outcome),
                                          "AdvanceSkill")) {
            continue;
        }
        const std::string skill_ref = kg.getProperty(outcome, "skill");
        if (skill_ref.empty()) continue;
        kg::EntityID skill = kg::INVALID_ENTITY;
        try {
            skill = static_cast<kg::EntityID>(std::stoul(skill_ref));
        } catch (...) { continue; }
        const std::string name = kg.getProperty(skill, "name");
        if (name.empty()) continue;
        bool seen = false;
        for (const auto& had : out) seen = seen || had.first == skill;
        if (!seen) out.emplace_back(skill, name);
    }
    return out;
}

// Give a skill at the level basic training grants, if it is not held
// at all. The level is the book's number and arrives from the graph;
// this function does not know what it is.
bool know_at_level_zero(kg::KGModule& kg, kg::EntityID character,
                        kg::EntityID skill, int level) {
    for (auto part : kg.getRelated(character, "HAS_PART")) {
        if (kg.getProperty(part, "skill") == std::to_string(skill)) {
            return false;
        }
    }
    const auto rating = kg.createEntity("SkillRating");
    if (rating == kg::INVALID_ENTITY) return false;
    kg.setProperty(rating, "skill", std::to_string(skill));
    kg.setProperty(rating, "skill_level", std::to_string(level));
    kg.createRelation(character, "HAS_PART", rating);
    return true;
}

// Step 4. "For your first term in your first career, you get every
// skill in the service skills table at level 0. For your first term in
// subsequent careers, you may pick any one skill from the service
// skills table at level 0." The step runs once on entering a career,
// which is exactly when the book applies it.
ChargenSession::PrimitiveResult ChargenSession::basic_training(
    const PrimitiveContext& context) {
    std::string error;
    const auto tables = subject_rows(kg_, context.step, career_,
                                     "rollable_table", error);
    if (tables.empty()) {
        return PrimitiveResult::failed("basic training: " + error);
    }
    kg::EntityID service = kg::INVALID_ENTITY;
    for (auto table : tables) {
        const std::string name = kg_.getProperty(table, "name");
        if (name.size() > 14 &&
            name.compare(name.size() - 14, 14, "Service Skills") == 0) {
            service = table;
        }
    }
    if (service == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed(
            "basic training: this career offers no Service Skills table");
    }
    const auto skills = service_skills(kg_, service);
    if (skills.empty()) {
        return PrimitiveResult::failed(
            "basic training: the service table grants no skills");
    }

    // "you get every skill in the service skills table at level 0"
    int basic_level = 0;
    std::string level_error;
    if (!constant("basic_training_level", basic_level, level_error)) {
        return PrimitiveResult::failed(level_error);
    }

    const bool first_career = sheet_.careers_served.size() <= 1;
    if (first_career) {
        int granted = 0;
        for (const auto& [id, name] : skills) {
            if (know_at_level_zero(kg_, sheet_.id, id, basic_level)) {
                ++granted;
            }
        }
        sheet_.life.push_back(
            {sheet_.terms_served, "basic training",
             "every service skill at level 0 (" +
                 std::to_string(granted) + " new)", 0});
        return PrimitiveResult::advance();
    }

    if (!context.input) {
        choices_.clear();
        for (size_t i = 0; i < skills.size(); ++i) {
            // A skill already held gains nothing at level 0, and the
            // book still allows picking it, so say so rather than
            // hiding the option or letting it look like a real one.
            std::string held;
            for (auto part : kg_.getRelated(sheet_.id, "HAS_PART")) {
                if (kg_.getProperty(part, "skill") !=
                    std::to_string(skills[i].first)) {
                    continue;
                }
                const std::string level = kg_.getProperty(part, "skill_level");
                held = "you already have this at level " +
                       (level.empty() ? "0" : level) + ", so it gains nothing";
            }
            choices_.push_back({std::to_string(i + 1), skills[i].second,
                                held.empty() ? "learn it at level 0" : held,
                                skills[i].first});
        }
        prompt_ = "Joining the " + sheet_.career +
                  ": which service skill do they start you on? (one, at "
                  "level 0)";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    const Choice* picked = find_choice(choices_, *context.input);
    if (!picked) {
        return PrimitiveResult::failed("'" + *context.input +
                                       "' is not one of the skills");
    }
    const size_t index = static_cast<size_t>(std::stoul(picked->key)) - 1;
    choices_.clear();
    if (index >= skills.size()) {
        return PrimitiveResult::failed("no such skill");
    }
    know_at_level_zero(kg_, sheet_.id, skills[index].first, basic_level);
    sheet_.life.push_back({sheet_.terms_served, "basic training",
                           skills[index].second + " at level 0", 0});
    return PrimitiveResult::advance();
}

// Step 10. "Characters who end their careers receive one benefit per
// term served in which they did not lose benefits. An additional
// benefit is gained if the character held rank O4, and two for rank
// O5. A character with rank O6 gains three extra benefits." And: "Up
// to 3 benefit rolls can be taken on the Cash table. All others must
// be taken in material benefits."
ChargenSession::PrimitiveResult ChargenSession::muster_out(
    const PrimitiveContext& context) {
    std::string error;
    const auto tables = subject_rows(kg_, context.step, career_,
                                     "rollable_table", error);
    if (tables.empty()) {
        return PrimitiveResult::failed("benefits: " + error);
    }
    // Which of the two is the cash table is read from its name. That
    // is weaker than a typed slot and worth replacing if a third kind
    // of benefit table ever appears; today the book prints two.
    const auto is_cash = [this](kg::EntityID table) {
        const std::string name = kg_.getProperty(table, "name");
        return name.find("Cash Benefits") != std::string::npos ||
               name.find("Cost Benefits") != std::string::npos;
    };

    // "Lose all benefits." Checked BEFORE the lazy initialisation
    // below, which would otherwise refill the very count it is meant
    // to empty: muster_out reads 0 as "not worked out yet" rather than
    // "none owed", so zeroing the counter would achieve nothing.
    if (benefits_forfeited_) {
        sheet_.life.push_back({sheet_.terms_served, "no benefits",
                               "forfeited on leaving the service", 0});
        benefits_forfeited_ = false;
        return PrimitiveResult::advance("continue");
    }
    if (benefit_rolls_owed_ == 0) {
        // "A character gets one Benefit Roll for every full term
        // served IN THAT CAREER." Counting every term of the whole
        // life instead paid a third career for the years spent in the
        // first two, so seven terms across three careers paid seven
        // rolls on leaving each of them.
        benefit_rolls_owed_ = sheet_.terms_in_career;
        if (sheet_.rank >= 6)      benefit_rolls_owed_ += 3;
        else if (sheet_.rank == 5) benefit_rolls_owed_ += 2;
        else if (sheet_.rank == 4) benefit_rolls_owed_ += 1;
        // "Up to 3 benefit rolls can be taken on the Cash table."
        if (!constant("cash_benefit_roll_max", cash_rolls_left_, error)) {
            return PrimitiveResult::failed(error);
        }
        if (benefit_rolls_owed_ <= 0) {
            sheet_.life.push_back({sheet_.terms_served, "no benefits",
                                   "no term served in this career", 0});
            return PrimitiveResult::advance("continue");
        }
    }

    if (!context.input) {
        choices_.clear();
        for (size_t i = 0; i < tables.size(); ++i) {
            if (is_cash(tables[i]) && cash_rolls_left_ <= 0) continue;
            choices_.push_back(
                {std::to_string(i + 1), kg_.getProperty(tables[i], "name"),
                 is_cash(tables[i])
                     ? std::to_string(cash_rolls_left_) + " cash rolls left"
                     : "goods, passages, ship shares",
                 tables[i]});
        }
        prompt_ = std::to_string(benefit_rolls_owed_) +
                  " benefit roll(s) left: which table? (click one to "
                  "read what is on it)";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    const Choice* picked = find_choice(choices_, *context.input);
    if (!picked) {
        return PrimitiveResult::failed("'" + *context.input +
                                       "' is not one of the tables");
    }
    // The answer names a TABLE, not a position, for the same reason
    // the training answer does: this list is rebuilt between offers as
    // cash rolls run out, so an index resolves against a list that is
    // no longer the one the player was shown.
    const kg::EntityID chosen = picked->subject;
    choices_.clear();
    if (std::find(tables.begin(), tables.end(), chosen) == tables.end()) {
        return PrimitiveResult::failed(
            "'" + picked->label + "' is not a benefit table this career "
            "offers");
    }
    if (is_cash(chosen) && cash_rolls_left_ <= 0) {
        return PrimitiveResult::failed(
            "the book allows at most three cash benefit rolls");
    }

    logosphere::rules::RollableTableRunner runner(kg_, dice_);
    const auto selected = runner.select(chosen, "chargen", "benefits");
    if (!selected.ok()) {
        return PrimitiveResult::failed("benefit selection failed: " +
                                       selected.error);
    }
    const auto outcome = selected.selection->outcome();
    auto& executor = executor_;
    const auto applied = executor.apply(
        outcome, {sheet_.id, "chargen", "benefits"});
    if (applied.status != logosphere::rules::OutcomeStatus::APPLIED) {
        return PrimitiveResult::failed("benefit failed: " +
                                       outcome_failure(applied));
    }
    std::string got = kg_.getProperty(outcome, "name");
    const auto cut = got.rfind(": ");
    if (cut != std::string::npos) got = got.substr(cut + 2);
    sheet_.life.push_back({sheet_.terms_served, "mustering out: " + got,
                           kg_.getProperty(chosen, "name"),
                           selected.selection->roll().id});
    if (is_cash(chosen)) --cash_rolls_left_;
    if (--benefit_rolls_owed_ > 0) {
        choices_.clear();
        for (size_t i = 0; i < tables.size(); ++i) {
            if (is_cash(tables[i]) && cash_rolls_left_ <= 0) continue;
            choices_.push_back(
                {std::to_string(i + 1), kg_.getProperty(tables[i], "name"),
                 is_cash(tables[i])
                     ? std::to_string(cash_rolls_left_) + " cash rolls left"
                     : "goods, passages, ship shares",
                 tables[i]});
        }
        prompt_ = std::to_string(benefit_rolls_owed_) +
                  " benefit roll(s) left: which table? (click one to "
                  "read what is on it)";
        return PrimitiveResult::pending(prompt_, choices_);
    }

    // What the character walks away with, read back from the graph
    // rather than accumulated alongside it. Benefits can raise a
    // characteristic too, so those come back as well.
    read_characteristics(kg_, sheet_);
    // Explicitly routed. Falling through by index from here lands on
    // finish_character, which ended a character who had only left one
    // career and still had terms in front of them.
    return PrimitiveResult::advance("continue");
}

// Step 6. Commission takes a Rank 0 character into the officer ranks;
// Advancement moves a Rank 1 or higher character up one. Both are
// optional and both are once per term, and a career that offers
// neither gives a second training roll instead, which is why the
// count of rolls owed lives on the session rather than in the step.
ChargenSession::PrimitiveResult ChargenSession::roll_promotion(
    const PrimitiveContext& context, bool commission) {
    const char* what = commission ? "commission" : "advancement";
    const int term = sheet_.terms_served + 1;
    std::string error;
    const auto check = subject_row(kg_, context.step, career_,
                                   "throw_check", error);
    // No row means this career does not offer the throw at all, which
    // the book states for seven of them. Not an error: a skip.
    if (check == kg::INVALID_ENTITY) {
        // "Because the Athlete, Barbarian, Belter, Drifter,
        // Entertainer, Hunter and Scout careers do not have commission
        // or advancement checks, characters get to make two rolls for
        // skills instead of one every term." Granted at the commission
        // step only: a career with no commission has no advancement
        // either, and the book gives two rolls, not three.
        if (commission) ++training_rolls_owed_;
        return PrimitiveResult::advance();
    }
    const bool eligible = commission ? sheet_.rank == 0 : sheet_.rank >= 1;
    if (!eligible) return PrimitiveResult::advance();

    if (!context.input) {
        // Say what is being risked and what is being reached for. A
        // player asked to gamble should be told the odds, the prize
        // and the cost, and the cost here is nothing, which is worth
        // knowing too.
        const std::string throw_text =
            kg_.getProperty(check, "attribute_ref") + " " +
            kg_.getProperty(check, "target_number") + "+";
        const int next_rank = commission ? 1 : sheet_.rank + 1;
        std::string prize = "rank " + std::to_string(next_rank);
        std::string rung_error;
        const auto track = subject_row(kg_, context.step, career_, "track",
                                       rung_error);
        if (track != kg::INVALID_ENTITY) {
            for (auto rung : kg_.getRelated(track, "HAS_PART")) {
                if (kg_.getProperty(rung, "step_index") !=
                    std::to_string(next_rank)) {
                    continue;
                }
                const std::string title =
                    kg_.getProperty(rung, "step_title");
                if (!title.empty()) prize = title;
                if (!kg_.getProperty(rung, "grants").empty()) {
                    prize += " and its skill";
                }
                break;
            }
        }
        choices_ = {{"1", std::string("Try for ") + what,
                     "throw " + throw_text + " -> " + prize +
                         ", plus a training roll. Fail and nothing "
                         "changes.",
                     check},
                    {"2", "Do not try",
                     "stay rank " + std::to_string(sheet_.rank) +
                         " and keep this term's single training roll"}};
        prompt_ = std::string("Term ") + std::to_string(term) + ": try for " +
                  what + "? (click it to read the throw)";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    const Choice* picked = find_choice(choices_, *context.input);
    if (!picked) {
        return PrimitiveResult::failed("'" + *context.input +
                                       "' is not one of the options");
    }
    const bool attempt = picked->key == "1";
    choices_.clear();
    if (!attempt) {
        sheet_.life.push_back({term, std::string("declined ") + what,
                               "reaching for rank is optional", 0});
        return PrimitiveResult::advance();
    }

    logosphere::rules::TaskCheckRunner runner(kg_, dice_);
    const auto thrown = runner.run(check, sheet_.id, "chargen", what);
    if (!thrown.ok()) return PrimitiveResult::failed(thrown.error);
    const auto& execution = *thrown.execution;
    if (!execution.passed()) {
        sheet_.life.push_back({term, std::string("no ") + what,
                               check_detail(execution),
                               execution.roll().id});
        return PrimitiveResult::advance();
    }

    read_characteristics(kg_, sheet_);
    sheet_.rank = commission ? 1 : sheet_.rank + 1;
    // "You also get any benefits listed for your new rank", and an
    // extra roll on any Skills and Training table.
    ++training_rolls_owed_;
    std::string rank_error;
    const auto track = subject_row(kg_, context.step, career_, "track",
                                   rank_error);
    if (track != kg::INVALID_ENTITY) {
        for (auto rung : kg_.getRelated(track, "HAS_PART")) {
            if (kg_.getProperty(rung, "step_index") !=
                std::to_string(sheet_.rank)) {
                continue;
            }
            sheet_.rank_title = kg_.getProperty(rung, "step_title");
            const std::string grant = kg_.getProperty(rung, "grants");
            if (grant.empty()) break;
            auto& executor = executor_;
            const auto applied = executor.apply(
                static_cast<kg::EntityID>(std::stoul(grant)),
                {sheet_.id, "chargen", "rank benefit"});
            if (applied.status !=
                logosphere::rules::OutcomeStatus::APPLIED) {
                return PrimitiveResult::failed("rank benefit: " +
                                               applied.error);
            }
            break;
        }
    }
    sheet_.life.push_back(
        {term,
         std::string(commission ? "commissioned" : "promoted") +
             (sheet_.rank_title.empty() ? "" : ": " + sheet_.rank_title),
         check_detail(execution), execution.roll().id});
    return PrimitiveResult::advance();
}

// Step 9. The book does not let you simply decide to stay: "If
// continuation is desired, the character must make a successful
// Reenlistment check as listed for their current profession or
// service." Two results are not the player's to choose. A natural 12
// means "they cannot leave their current career and must continue for
// another term", and it outranks the seven-term cap, which the book
// says explicitly: a character at seven or more "must retire and
// cannot undertake any more prior experience, unless they roll a
// natural 12 during Reenlistment and must serve another term".
ChargenSession::PrimitiveResult ChargenSession::roll_reenlistment(
    const PrimitiveContext& context) {
    const int term = sheet_.terms_served;
    std::string error;
    const auto check = subject_row(kg_, context.step, career_,
                                   "throw_check", error);
    if (check == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed("re-enlistment: " + error);
    }
    logosphere::rules::TaskCheckRunner runner(kg_, dice_);
    const auto thrown = runner.run(check, sheet_.id, "chargen",
                                   "re-enlistment");
    if (!thrown.ok()) return PrimitiveResult::failed(thrown.error);
    const auto& execution = *thrown.execution;

    const auto& roll = execution.roll();
    // "If the character rolls a natural 12, they cannot leave their
    // current career and must continue for another term." The 12 is
    // the book's, so it is in the graph; the identical case is already
    // done this way for the survival throw's natural failure.
    int forced_natural = 0;
    if (!constant("reenlistment_forced_natural", forced_natural, error)) {
        return PrimitiveResult::failed(error);
    }
    const bool natural_twelve = execution.natural_total() == forced_natural;
    if (natural_twelve) {
        sheet_.life.push_back(
            {term, "cannot leave", "natural 12 on re-enlistment",
             roll.id});
        return PrimitiveResult::advance("forced");
    }
    if (!execution.passed()) {
        sheet_.life.push_back({term, "not permitted to re-enlist",
                               check_detail(execution), roll.id});
        return PrimitiveResult::advance("must_leave");
    }
    sheet_.life.push_back({term, "may re-enlist",
                           check_detail(execution), roll.id});
    return PrimitiveResult::advance("may_choose");
}

// "You begin as a Rank 0 character", every time you begin. Rank, its
// title and the terms served belong to the career, not to the person:
// carrying them into the next one gave a Drifter a Navy commission and
// paid them for years served somewhere else.
void ChargenSession::enter_career() {
    sheet_.rank = 0;
    sheet_.rank_title.clear();
    sheet_.terms_in_career = 0;
    benefit_rolls_owed_ = 0;
    cash_rolls_left_ = 0;
}

ChargenSession::PrimitiveResult ChargenSession::advance_term(
    const PrimitiveContext&) {
    // "Increase your age by 4 years" is the step's declared outcome and
    // has already been applied by the time this runs; the sheet was
    // re-read from the graph with it. What is left here is the counting
    // the book does not state as an effect on the character.
    const int term = sheet_.terms_served + 1;
    sheet_.terms_served = term;
    // Benefits and the seven-term cap count different things: one
    // counts this career, the other counts the whole life.
    sheet_.terms_in_career += 1;
    sheet_.life.push_back({term, "term ends",
                           "age " + std::to_string(sheet_.age_years), 0});
    write_sheet(kg_, sheet_);
    return PrimitiveResult::advance();
}


// "If any characteristic is reduced to 0 by aging, then the character
// suffers an aging crisis. The character dies unless he can pay
// 1D6x10,000 Credits for medical care, which will bring any
// characteristics back up to 1. The character automatically fails any
// Qualification checks from now on."
//
// The book prints this rule twice, once under Aging and once under
// Injuries, word for word apart from the cause. It is written once
// here for the same reason.
//
// The price is rolled BEFORE the question, because the book prices the
// care and then asks whether you can meet it. Someone who cannot pay
// is not offered the choice: the book does not let you decline a bill
// you could not have settled.
ChargenSession::PrimitiveResult ChargenSession::begin_crisis(
    const char* cause) {
    // Pay-or-die turns on what the character HOLDS, so the purse is
    // read from the parts here rather than trusted from the last time
    // something happened to look.
    read_holdings(kg_, sheet_);
    logosphere::dice::DiceExpression cost;
    cost.count = 1;
    cost.sides = 6;
    cost.multiplier = 10000;
    const auto price = dice_.roll(cost, "chargen", "medical care");
    if (price.id == 0) {
        return PrimitiveResult::failed("the crisis could not be priced");
    }
    crisis_price_ = price.total;
    crisis_roll_ = price.id;
    crisis_cause_ = cause;
    crisis_open_ = true;

    std::string slots_error;
    const auto slots = characteristic_slots(slots_error);
    if (slots.empty()) return PrimitiveResult::failed(slots_error);
    std::vector<std::string> at_zero;
    for (const auto& slot : slots) {
        if (kg_.getProperty(sheet_.id, slot) != "0") continue;
        std::string label = slot;
        label[0] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(label[0])));
        for (size_t i = 1; i < label.size(); ++i) {
            if (label[i - 1] != '_') continue;
            label[i] = static_cast<char>(std::toupper(
                static_cast<unsigned char>(label[i])));
        }
        std::replace(label.begin(), label.end(), '_', ' ');
        at_zero.push_back(label);
    }
    std::string ruined;
    for (size_t i = 0; i < at_zero.size(); ++i) {
        ruined += (i ? ", " : "") + at_zero[i];
    }

    sheet_.life.push_back(
        {sheet_.terms_served, std::string(cause) + " crisis: " + ruined +
                                  " gone",
         "medical care costs Cr" + std::to_string(crisis_price_), price.id});

    choices_.clear();
    if (sheet_.credits >= crisis_price_) {
        choices_.push_back({"1",
                            "pay Cr" + std::to_string(crisis_price_) +
                                " for medical care",
                            "restored to 1, and you will never qualify "
                            "for a new career again",
                            kg::INVALID_ENTITY});
    }
    choices_.push_back({"2", "refuse the care", "the character dies here",
                        kg::INVALID_ENTITY});
    prompt_ = std::string(cause) + " has taken " + ruined +
              " to nothing. Care costs Cr" + std::to_string(crisis_price_) +
              "; you hold Cr" + std::to_string(sheet_.credits) +
              (sheet_.credits >= crisis_price_
                   ? "."
                   : ", which is not enough.");
    return PrimitiveResult::pending(prompt_, choices_);
}

ChargenSession::PrimitiveResult ChargenSession::resolve_crisis(
    const std::string& answer) {
    crisis_open_ = false;
    if (answer != "1") {
        finish_reason_ = crisis_cause_ +
                         " took a characteristic to nothing, and the "
                         "care went unpaid.";
        sheet_.life.push_back({sheet_.terms_served, "died of the " +
                                                        crisis_cause_,
                               "care unpaid", crisis_roll_});
        return PrimitiveResult::advance("died");
    }

    // The purse is the CurrencyBalance parts. sheet_.credits is a cache
    // of them, re-derived from scratch whenever anything looks
    // (muster_out does exactly that), so money taken out of the cache
    // alone is handed straight back the next time it is recomputed.
    // Take it out of the money itself, which is the thing that owns it.
    long long owed = crisis_price_;
    for (const auto part : kg_.getRelated(sheet_.id, "HAS_PART")) {
        if (kg_.getType(part) != "CurrencyBalance") continue;
        const std::string held = kg_.getProperty(part, "balance_amount");
        if (held.empty()) continue;
        long long balance = std::stoll(held);
        const long long taken = balance < owed ? balance : owed;
        if (taken > 0) {
            balance -= taken;
            owed -= taken;
            kg_.setProperty(part, "balance_amount", std::to_string(balance));
        }
    }
    // The sheet's field and the Character's credits property are both
    // mirrors of the purse, so both are re-derived FROM it rather than
    // decremented alongside it. Two independent subtractions of one
    // number is how they came to disagree in the first place.
    read_holdings(kg_, sheet_);
    kg_.setProperty(sheet_.id, "credits", std::to_string(sheet_.credits));
    // "which will bring any characteristics back up to 1"
    int restore_to = 0;
    std::string restore_error;
    if (!constant("crisis_restore_value", restore_to, restore_error)) {
        return PrimitiveResult::failed(restore_error);
    }
    std::string slots_error;
    const auto slots = characteristic_slots(slots_error);
    if (slots.empty()) return PrimitiveResult::failed(slots_error);
    for (const auto& slot : slots) {
        if (kg_.getProperty(sheet_.id, slot) != "0") continue;
        kg_.setProperty(sheet_.id, slot, std::to_string(restore_to));
    }
    // The graph is what was written, so the sheet is re-read from it
    // rather than kept in step by hand.
    read_characteristics(kg_, sheet_);

    // Permanent, so it is a mark on the character rather than a
    // modifier on one throw.
    kg_.setProperty(sheet_.id, "qualification_barred", "true");
    sheet_.life.push_back(
        {sheet_.terms_served, "bought back from the " + crisis_cause_,
         "Cr" + std::to_string(crisis_price_) +
             " spent; no new career will ever take them",
         crisis_roll_});
    return PrimitiveResult::advance();
}

// What a table row DID, in the book's own words. Absorbed outcomes
// carry the cell they came from, so the timeline can read "Medically
// discharged from the service" instead of the alias the seed happened
// to give the entity. Falls back to the name when a rule carries no
// quote, which is state rather than absorbed text.
std::string row_text(const kg::KGModule& kg, kg::EntityID outcome) {
    std::string text = kg.getProperty(outcome, "source_quote");
    if (text.empty()) {
        text = kg.getProperty(outcome, "name");
        const auto cut = text.rfind(": ");
        if (cut != std::string::npos) text = text.substr(cut + 2);
        return text;
    }
    // One clause is enough for a timeline; the citation carries the
    // rest and the roll id points at it.
    const auto stop = text.find(". ");
    if (stop != std::string::npos) text = text.substr(0, stop + 1);
    return text;
}

// A rule can say "roll on the Injury table" as part of its outcome.
// The executor hands that back as a request rather than performing it,
// so the roll happens here, above apply(), and the loop is visible
// instead of hidden inside a handler recursing on itself.
bool ChargenSession::run_granted_rolls(
    const std::vector<logosphere::rules::TableRollRequest>& requests,
    const char* purpose, std::string& error) {
    logosphere::rules::RollableTableRunner runner(kg_, dice_);
    for (const auto& request : requests) {
        // "Roll twice and take the lower result": the rule says how
        // many rolls and which of them counts, so both come from the
        // request and neither is decided here.
        std::vector<logosphere::rules::RollableTableSelection> rolled;
        for (int i = 0; i < request.roll_count; ++i) {
            const auto selected =
                runner.select(request.table, "chargen", purpose);
            if (!selected.ok()) { error = selected.error; return false; }
            rolled.push_back(*selected.selection);
        }
        if (rolled.empty()) continue;
        size_t chosen = 0;
        for (size_t i = 1; i < rolled.size(); ++i) {
            const bool lower = rolled[i].roll().total < rolled[chosen].roll().total;
            switch (request.selection) {
                case logosphere::rules::TableRollSelection::LOWEST:
                    if (lower) chosen = i;
                    break;
                case logosphere::rules::TableRollSelection::HIGHEST:
                    if (!lower) chosen = i;
                    break;
                case logosphere::rules::TableRollSelection::EACH:
                    break;
            }
        }
        const std::string table_name = kg_.getProperty(request.table, "name");
        for (size_t i = 0; i < rolled.size(); ++i) {
            if (request.selection !=
                    logosphere::rules::TableRollSelection::EACH &&
                i != chosen) {
                // Recorded, not applied: the roll happened and is
                // citable even though the rule discarded it.
                sheet_.life.push_back(
                    {sheet_.terms_served, table_name + ": set aside",
                     "rolled " + std::to_string(rolled[i].roll().total),
                     rolled[i].roll().id});
                continue;
            }
            const auto applied = executor_.apply(
                rolled[i].outcome(), {sheet_.id, "chargen", purpose});
            if (applied.status !=
                logosphere::rules::OutcomeStatus::APPLIED) {
                error = table_name + ": " + outcome_failure(applied);
                return false;
            }
            sheet_.life.push_back(
                {sheet_.terms_served,
                 table_name + ": " + row_text(kg_, rolled[i].outcome()),
                 "rolled " + std::to_string(rolled[i].roll().total),
                 rolled[i].roll().id});
        }
    }
    return true;
}

// Step 5, the way out of a failed survival throw.
//
// "With the Referee's approval, you can keep the character that fails
// a survival roll and roll on the Survival Mishaps table instead. This
// mishap is always enough to force you to leave the service after half
// a term, or two years of service. You lose the benefit roll for the
// current term only."
//
// The book's default is death, and this is printed as an optional
// rule. Both are therefore offered: in a solo generation the player
// holds the referee's seat, and the choice is made where the book puts
// it, at the moment the throw is missed.
ChargenSession::PrimitiveResult ChargenSession::survival_mishap(
    const PrimitiveContext& context) {
    // This step asks two different questions. The mishap can ruin a
    // characteristic, and the crisis it raises suspends the SAME step,
    // so an arriving answer has to be routed by which question is
    // open. Without this, "pay Cr50000 for care" was read as "take the
    // mishap": a second mishap was rolled, two more years added, no
    // money taken, and the characteristic left at 0.
    if (context.input && crisis_open_) return resolve_crisis(*context.input);
    if (!context.input) {
        choices_.clear();
        choices_.push_back({"1", "take the mishap instead",
                            "the career ends, but the character lives",
                            kg::INVALID_ENTITY});
        choices_.push_back({"2", "let the character die",
                            "the book's own default", kg::INVALID_ENTITY});
        prompt_ = sheet_.career +
                  " should have killed them. The Referee may allow a "
                  "mishap instead.";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    if (*context.input != "1") {
        finish_reason_ = "The career ended here. A failed survival throw "
                         "is death, and the mishap rule was not taken.";
        return PrimitiveResult::advance("died");
    }

    const auto table = find_named(kg_, "RollableTable", "Survival Mishaps");
    if (table == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed(
            "the Survival Mishaps table is not in the graph; load the "
            "shared tables seed");
    }
    logosphere::rules::RollableTableRunner runner(kg_, dice_);
    const auto selected = runner.select(table, "chargen", "mishap");
    if (!selected.ok()) {
        return PrimitiveResult::failed("mishap: " + selected.error);
    }
    const auto& roll = selected.selection->roll();
    const auto applied = executor_.apply(
        selected.selection->outcome(), {sheet_.id, "chargen", "mishap"});
    if (applied.status != logosphere::rules::OutcomeStatus::APPLIED) {
        return PrimitiveResult::failed("mishap: " + outcome_failure(applied));
    }

    sheet_.life.push_back(
        {sheet_.terms_served,
         "mishap: " + row_text(kg_, selected.selection->outcome()),
         "1D6 = " + std::to_string(roll.total), roll.id});

    // What the rule MEANT, as opposed to what it wrote to the graph.
    for (const auto& signal : applied.procedure_signals) {
        if (signal.outcome_type == "ForfeitBenefits") {
            benefits_forfeited_ = true;
        }
    }
    // "Roll on the Injury table", where two of the six rows send you.
    std::string error;
    if (!run_granted_rolls(applied.table_roll_requests, "injury", error)) {
        return PrimitiveResult::failed("mishap: " + error);
    }
    read_characteristics(kg_, sheet_);

    // "after half a term, or two years of service". This path skips
    // advance_term, so the term neither completes nor pays: the two
    // years are added here and terms_in_career is left alone, which is
    // what makes the current term's benefit roll disappear.
    int mishap_years = 0;
    std::string mishap_years_error;
    if (!constant("mishap_years", mishap_years, mishap_years_error)) {
        return PrimitiveResult::failed(mishap_years_error);
    }
    sheet_.age_years += mishap_years;
    write_sheet(kg_, sheet_);
    sheet_.life.push_back({sheet_.terms_served, "left the service",
                           "two years into the term, age " +
                               std::to_string(sheet_.age_years), 0});

    // The same rule aging has: a characteristic at 0 is a crisis.
    // "If any characteristic is reduced to 0", asked of the group the
    // book names rather than of six fields written out here.
    std::string ruined_error;
    const auto ruined_slots = characteristic_slots(ruined_error);
    if (ruined_slots.empty()) return PrimitiveResult::failed(ruined_error);
    bool ruined = false;
    for (const auto& slot : ruined_slots) {
        if (kg_.getProperty(sheet_.id, slot) == "0") ruined = true;
    }
    if (ruined) return begin_crisis("injury");
    return PrimitiveResult::advance();
}

// Step 8. "The effects of aging begin when a character reaches 34
// years of age. At the end of the fourth term, and at the end of every
// term thereafter, the character must roll 2D6 on the Aging Table.
// Apply the character's total number of terms as a negative Dice
// Modifier on this table."
//
// The book states the trigger twice, by age and by term count, and the
// two agree for anyone who has only served: 18 + 4x4 = 34. The term
// count is the mechanical one, and it is also what the modifier is
// made of, so it is what gates here. If a rule ever adds years without
// adding terms (mishap 5's prison sentence does), the two part company
// and this needs revisiting rather than quietly picking one.
//
// TOTAL terms, not terms in this career. The two counters are kept
// apart deliberately: benefits are paid per career, this is paid by a
// whole life.
ChargenSession::PrimitiveResult ChargenSession::roll_aging(
    const PrimitiveContext& context) {
    // The only question this step asks is the crisis, so an answer
    // arriving here is an answer to that. Routed on the same flag as
    // survival_mishap rather than on "there was input", because one
    // step asking two questions is what went wrong there.
    if (context.input && crisis_open_) return resolve_crisis(*context.input);
    // "The effects of aging begin when a character reaches 34 years of
    // age", and the checklist repeats it as "If your character is 34 or
    // older, roll for aging." Gating on the term count instead agreed
    // only for a life made purely of terms: a mishap adds two years
    // without a term and its prison row adds four, so a character
    // could pass 34 and never roll. Age is what the book asks about.
    int aging_start_age = 0;
    std::string age_error;
    if (!constant("aging_start_age", aging_start_age, age_error)) {
        return PrimitiveResult::failed(age_error);
    }
    if (sheet_.age_years < aging_start_age) {
        return PrimitiveResult::advance();
    }

    const auto table = find_named(kg_, "RollableTable", "Effects of Aging");
    if (table == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed(
            "the Aging table is not in the graph; load the shared "
            "tables seed");
    }
    logosphere::rules::RollableTableRunner runner(kg_, dice_);
    const auto selected = runner.select(table, "chargen", "aging",
                                        -sheet_.terms_served);
    if (!selected.ok()) {
        return PrimitiveResult::failed("aging: " + selected.error);
    }
    const auto& roll = selected.selection->roll();
    const auto applied = executor_.apply(
        selected.selection->outcome(), {sheet_.id, "chargen", "aging"});
    if (applied.status != logosphere::rules::OutcomeStatus::APPLIED) {
        return PrimitiveResult::failed("aging: " + outcome_failure(applied));
    }
    // The executor wrote the reduced values into the graph. The
    // session's copy is stale until it reads them back, and the UPP is
    // recomputed from them.
    const CharacterSheet before = sheet_;
    read_characteristics(kg_, sheet_);

    std::ostringstream detail;
    detail << roll.expression.to_string() << " = " << roll.total
           << " (" << sheet_.terms_served << " terms as a negative DM)";
    const auto lost = [&](const char* name, int was, int now) {
        return was == now ? std::string()
                          : std::string(name) + " " + std::to_string(was) +
                                "->" + std::to_string(now) + " ";
    };
    std::string toll =
        lost("Str", before.strength, sheet_.strength) +
        lost("Dex", before.dexterity, sheet_.dexterity) +
        lost("End", before.endurance, sheet_.endurance) +
        lost("Int", before.intelligence, sheet_.intelligence) +
        lost("Edu", before.education, sheet_.education) +
        lost("Soc", before.social_standing, sheet_.social_standing);
    if (!toll.empty() && toll.back() == ' ') toll.pop_back();
    sheet_.life.push_back({sheet_.terms_served,
                           toll.empty() ? "the years pass, unmarked"
                                        : "the years take " + toll,
                           detail.str(), roll.id});

    // "If any characteristic is reduced to 0 by aging..."
    // "If any characteristic is reduced to 0", asked of the group the
    // book names rather than of six fields written out here.
    std::string ruined_error;
    const auto ruined_slots = characteristic_slots(ruined_error);
    if (ruined_slots.empty()) return PrimitiveResult::failed(ruined_error);
    bool ruined = false;
    for (const auto& slot : ruined_slots) {
        if (kg_.getProperty(sheet_.id, slot) == "0") ruined = true;
    }
    if (ruined) return begin_crisis("aging");
    return PrimitiveResult::advance();
}

ChargenSession::PrimitiveResult ChargenSession::choose_term_end(
    const PrimitiveContext& context) {
    if (!context.input) {
        // "The maximum number of terms spent in character creation" is
        // seven, cited in the graph. At the cap there is nothing left
        // to decide: the character is made.
        int cap = 0;
        std::string error;
        if (!constant("max_terms", cap, error)) {
            return PrimitiveResult::failed(error);
        }
        if (cap <= 0) {
            return PrimitiveResult::failed(
                "RuleConstant 'max_terms' must be positive");
        }
        if (sheet_.terms_served >= cap) {
            finish_reason_ =
                std::to_string(cap) + " terms served. Mustered out at age " +
                std::to_string(sheet_.age_years) + ".";
            sheet_.life.push_back(
                {sheet_.terms_served, "out of terms",
                 "the book allows " + std::to_string(cap) +
                 " and no more", 0});
            return PrimitiveResult::advance("muster_out");
        }
        choices_ = {{"1", "Serve another term",
                     "four more years in the " + sheet_.career},
                    {"2", "Muster out", "leave with what you have"}};
        prompt_ = "Term " + std::to_string(sheet_.terms_served) +
                  " is over. What now?";
        return PrimitiveResult::pending(prompt_, choices_);
    }
    const Choice* picked = find_choice(choices_, *context.input);
    if (!picked) {
        return PrimitiveResult::failed("'" + *context.input +
                                       "' is not one of the options");
    }
    const std::string key = picked->key;
    choices_.clear();
    if (key == "1") return PrimitiveResult::advance("continue");

    finish_reason_ = "Mustered out at age " +
                     std::to_string(sheet_.age_years) + " after " +
                     std::to_string(sheet_.terms_served) + " term(s).";
    return PrimitiveResult::advance("muster_out");
}

ChargenSession::PrimitiveResult ChargenSession::finish_character(
    const PrimitiveContext&) {
    if (finish_reason_.empty()) {
        finish_reason_ = "Character generation completed by the procedure.";
    }
    finish(finish_reason_);
    return PrimitiveResult::complete();
}

bool ChargenSession::choose(const std::string& answer, std::string& error) {
    error.clear();
    if (finished_) {
        error = "this life is finished";
        return false;
    }
    return accept(runner_.resume(cursor_, answer), error);
}

std::vector<LifeEvent> ChargenSession::drain() {
    std::vector<LifeEvent> out;
    for (size_t i = drained_; i < sheet_.life.size(); ++i)
        out.push_back(sheet_.life[i]);
    drained_ = sheet_.life.size();
    return out;
}

// --- auto-play -----------------------------------------------------

// The key of the first cash table on offer, or of the first table at
// all when the book will not allow another cash roll. Matched on the
// label because that is all a caller of the session can see; the
// session itself reaches the table through the graph.
std::string cash_first(const std::vector<Choice>& choices) {
    for (const auto& choice : choices) {
        if (choice.label.find("Cash Benefits") != std::string::npos ||
            choice.label.find("Cost Benefits") != std::string::npos) {
            return choice.key;
        }
    }
    return choices.front().key;
}

bool run_chargen(const ChargenRequest& request,
                 kg::KGModule& kg,
                 logosphere::dice::DiceService& dice,
                 CharacterSheet& out,
                 std::string& error) {
    ChargenSession session(kg, dice);
    if (request.attribute_selector) {
        session.set_attribute_selector(request.attribute_selector);
    }
    // An auto-player has no taste, and the alternative to answering is
    // that mishap 1 and injury 3 abort the run - which they did, for
    // 8% of every sweep, while the sweeps reported the survivors.
    // Taking the first option is deterministic and, like every other
    // auto-played answer here, exists to carry a life to its end.
    session.set_choice_resolver(
        request.choice_resolver
            ? request.choice_resolver
            : logosphere::rules::ChoiceResolver(
                  [](const logosphere::rules::PendingChoice& ask,
                     int& option, std::string& error) {
                      if (ask.options.empty()) {
                          error = "an OutcomeChoice with no options";
                          return false;
                      }
                      option = 0;
                      return true;
                  }));
    if (!session.begin(request.seed, error)) return false;

    bool offered = false;
    for (const auto& c : session.choices())
        if (c.label == request.career_name) offered = true;
    if (!offered) {
        error = "no Career named '" + request.career_name + "' in the graph";
        return false;
    }
    if (!session.choose(request.career_name, error)) return false;

    // An auto-player takes the named career, serves out, and then stops.
    // It only answers prompts containing that career or "Serve another
    // term". Draft versus Drifter is an authority choice it cannot make.
    // The bound is "stop volunteering for terms", not "stop playing".
    // A natural 12 on re-enlistment forces a term the character did not
    // ask for, and the book lets that outrank even its own seven-term
    // cap, so terms_served can pass max_terms. Leaving the loop at that
    // point abandoned the session mid-question: measured at 5 lives in
    // 3000, each stranded on "Term 8: try for advancement?".
    int guard = 0;
    while (!session.finished()) {
        const auto& choices = session.choices();
        const bool career_offered = std::any_of(
            choices.begin(), choices.end(), [&](const Choice& choice) {
                return choice.label == request.career_name;
            });
        if (career_offered) {
            if (++guard > 1) break;
            if (!session.choose(request.career_name, error)) return false;
            continue;
        }
        // Where max_terms is actually spent: the auto-player volunteers
        // for another term until it has had its fill, then declines.
        // Being forced to serve on is not this question, and does not
        // consult this bound.
        if (!choices.empty() &&
            choices.front().label == "Serve another term") {
            const bool willing =
                session.sheet().terms_served < request.max_terms;
            if (!session.choose(willing ? "1" : "2", error)) return false;
            continue;
        }
        // Which table to train on is a real choice the book gives every
        // term, and an auto-player has no taste. It takes Service
        // Skills, which is the table this harness rolled before the
        // choice existed, so what it measures is unchanged.
        // Benefits: the auto-player takes cash while the book allows
        // it, then goods. A human weighs a weapon against money; this
        // one just needs to be deterministic and to spend them all.
        if (session.prompt().find("benefit roll(s) left") !=
            std::string::npos) {
            // Cash FIRST, which is what the comment above always
            // claimed and the code never did: the extractor emits the
            // material table before the cash one, and taking the front
            // of the list meant every auto-played character finished
            // with Cr0. Two rules had therefore never run in any test
            // - the three-cash-roll cap, and the crisis pay path that
            // needs money in hand.
            if (!session.choose(cash_first(choices), error)) return false;
            continue;
        }
        // A failed survival throw is death unless the Referee allows
        // the mishap table instead. The auto-player allows it, for the
        // same reason it pays for care: it exists to carry a life to
        // its end, and the alternative stops it dead. A human referee
        // weighs the story; this one has no story to weigh.
        if (session.prompt().find("should have killed them") !=
            std::string::npos) {
            if (!session.choose("1", error)) return false;
            continue;
        }
        // A crisis is pay-or-die, and the auto-player pays whenever
        // the money is there: it exists to carry a life to its end, and
        // refusing care ends it. Where the money is short the book
        // offers no choice at all and only "refuse" is on the table,
        // so taking the last option is right either way.
        if (session.prompt().find("to nothing. Care costs") !=
            std::string::npos) {
            if (!session.choose(choices.front().key, error)) return false;
            continue;
        }
        // Reaching for rank costs nothing but the throw, so the
        // auto-player always tries. A human decides; this one has no
        // reason not to.
        if (session.prompt().find("try for commission") !=
                std::string::npos ||
            session.prompt().find("try for advancement") !=
                std::string::npos) {
            if (!session.choose("1", error)) return false;
            continue;
        }
        // Both forms of the question: the term's own roll, and the
        // extra one a promotion buys. Matching only the first left
        // every promoted character stuck at a prompt the auto-player
        // could not answer, which is most of a long life.
        if (session.prompt().find("which table do you train on") !=
                std::string::npos ||
            session.prompt().find("more training roll(s). Which table?") !=
                std::string::npos) {
            const auto service = std::find_if(
                choices.begin(), choices.end(), [](const Choice& choice) {
                    return choice.label.size() > 14 &&
                           choice.label.compare(choice.label.size() - 14, 14,
                                                "Service Skills") == 0;
                });
            if (service == choices.end()) {
                error = "no Service Skills table offered for this career";
                return false;
            }
            if (!session.choose(service->key, error)) return false;
            continue;
        }
        break;
    }
    if (!session.finished() && !session.choices().empty() &&
        session.choices().front().label == "Serve another term") {
        if (!session.choose("2", error)) return false;
    }
    // Leaving a career pays out, and that happens after the term loop
    // has stopped counting terms. Spend every roll the book grants.
    int benefit_guard = 0;
    while (!session.finished() && !session.choices().empty() &&
           session.prompt().find("benefit roll(s) left") !=
               std::string::npos) {
        if (++benefit_guard > 32) break;
        if (!session.choose(session.choices().front().key, error)) {
            return false;
        }
    }
    if (!session.finished()) {
        const bool finish_offered = std::any_of(
            session.choices().begin(), session.choices().end(),
            [](const Choice& choice) { return choice.key == "finish"; });
        if (finish_offered && !session.choose("finish", error)) return false;
    }
    out = session.sheet();
    if (session.finished()) return true;

    const bool draft_choice = std::any_of(
        session.choices().begin(), session.choices().end(),
        [](const Choice& choice) {
            return choice.label == "Take the Drifter career" ||
                   choice.label == "Submit to the Draft";
        });
    error = draft_choice
                ? "auto-play requires an explicit Draft or Drifter choice"
                : "auto-play stopped at unresolved choice: " +
                      session.prompt();
    return false;
}

std::string format_life(const CharacterSheet& s) {
    std::ostringstream o;
    o << "  " << s.career << " " << s.upp << ", age " << s.age_years
      << ", " << s.terms_served << " term(s)\n";
    int last_term = -1;
    for (const auto& e : s.life) {
        if (e.term != last_term) {
            o << "  --- " << (e.term == 0 ? std::string("before service")
                                          : "term " + std::to_string(e.term))
              << " ---\n";
            last_term = e.term;
        }
        o << "    " << e.what;
        if (!e.detail.empty()) o << ": " << e.detail;
        if (e.roll_id) o << "   [roll #" << e.roll_id << "]";
        o << "\n";
    }
    if (!s.skills.empty()) {
        o << "  skills:";
        for (const auto& sk : s.skills) o << " " << sk;
        o << "\n";
    }
    return o.str();
}

bool format_character_facts(const CharacterSheet& s,
                            const kg::KGModule& kg,
                            std::string& out,
                            std::string& error) {
    out.clear();
    error.clear();

    kg::EntityID table = kg::INVALID_ENTITY;
    for (const auto id : kg.findByType("LookupTable")) {
        if (kg.getProperty(id, "name") != "characteristic_modifiers") {
            continue;
        }
        if (table != kg::INVALID_ENTITY) {
            error = "multiple LookupTable entities named "
                    "'characteristic_modifiers'";
            return false;
        }
        table = id;
    }
    if (table == kg::INVALID_ENTITY) {
        error = "missing required LookupTable 'characteristic_modifiers'";
        return false;
    }

    const struct {
        const char* label;
        int value;
    } characteristics[] = {
        {"Str", s.strength},
        {"Dex", s.dexterity},
        {"End", s.endurance},
        {"Int", s.intelligence},
        {"Edu", s.education},
        {"Soc", s.social_standing},
    };

    logosphere::rules::LookupTableSelector selector(kg);
    std::ostringstream summary;
    bool first = true;
    for (const auto& characteristic : characteristics) {
        const auto selected = selector.select(table, characteristic.value);
        if (!selected.ok()) {
            error = "characteristic modifier lookup failed for " +
                    std::string(characteristic.label) + ": " +
                    selected.error;
            return false;
        }
        const std::string encoded = kg.getProperty(
            selected.selection->row(), "characteristic_modifier");
        int64_t modifier = 0;
        const auto parsed = std::from_chars(
            encoded.data(), encoded.data() + encoded.size(), modifier);
        if (encoded.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != encoded.data() + encoded.size()) {
            error = "selected CharacteristicModifierEntry has invalid or "
                    "missing characteristic_modifier";
            return false;
        }
        if (!first) summary << ", ";
        first = false;
        summary << characteristic.label << " " << characteristic.value
                << " (DM " << (modifier >= 0 ? "+" : "") << modifier << ")";
    }
    summary << "\nUPP " << s.upp << ", age " << s.age_years << ", "
            << s.terms_served << " term(s)";
    if (!s.career.empty()) summary << ", currently " << s.career;
    if (!s.careers_served.empty()) {
        summary << "\nCareers so far:";
        for (const auto& career : s.careers_served) summary << " " << career;
    }
    if (!s.skills.empty()) {
        summary << "\nSkills:";
        for (const auto& skill : s.skills) summary << " " << skill;
    }
    // Rank, money and belongings are facts about the person, and the
    // narrator writes from facts. Without them a commissioned officer
    // reads the same as a rating, and mustering out changes nothing
    // about how the character is described.
    if (s.rank > 0) {
        summary << "\nRank " << s.rank;
        if (!s.rank_title.empty()) summary << ", " << s.rank_title;
    }
    if (s.credits != 0) summary << "\nCredits: " << s.credits;
    if (!s.possessions.empty()) {
        summary << "\nHolds:";
        for (const auto& held : s.possessions) summary << " " << held << ";";
    }
    out = summary.str();
    return true;
}

}  // namespace logovger
