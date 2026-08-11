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
    return detail.str();
}

void write_sheet(kg::KGModule& kg, const CharacterSheet& s) {
    kg.setProperty(s.id, "strength",        std::to_string(s.strength));
    kg.setProperty(s.id, "dexterity",       std::to_string(s.dexterity));
    kg.setProperty(s.id, "endurance",       std::to_string(s.endurance));
    kg.setProperty(s.id, "intelligence",    std::to_string(s.intelligence));
    kg.setProperty(s.id, "education",       std::to_string(s.education));
    kg.setProperty(s.id, "social_standing", std::to_string(s.social_standing));
    kg.setProperty(s.id, "upp",             s.upp);
    kg.setProperty(s.id, "age_years",       std::to_string(s.age_years));
    kg.setProperty(s.id, "terms_served",    std::to_string(s.terms_served));
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

// Select a career training row, then apply its structured outcome. These are
// separate engine operations: the recorded selection remains a fact if
// outcome application fails. There is no default skill level in procedure
// code.
bool select_and_apply_training(kg::KGModule& kg, kg::EntityID career,
                               kg::EntityID character,
                               logosphere::dice::DiceService& dice,
                               uint64_t& roll_id_out, std::string& gained,
                               std::string& error) {
    gained.clear();
    error.clear();
    kg::EntityID table = kg::INVALID_ENTITY;
    for (auto part : kg.getRelated(career, "HAS_PART")) {
        if (kg.getRegistry().isSubtypeOf(kg.getType(part),
                                         "RollableTable")) {
            table = part;
        }
        if (table != kg::INVALID_ENTITY) break;
    }
    if (table == kg::INVALID_ENTITY) {
        error = "career has no RollableTable for skills and training";
        return false;
    }

    logosphere::rules::RollableTableRunner table_runner(kg, dice);
    const auto selected = table_runner.select(
        table, "chargen", "skills and training");
    if (!selected.ok()) {
        error = "skills and training selection failed: " + selected.error;
        return false;
    }
    roll_id_out = selected.selection->roll().id;

    const auto outcome = selected.selection->outcome();
    kg::EntityID skill = kg::INVALID_ENTITY;
    if (!entity_reference(kg, outcome, "skill", "Skill", skill, error)) {
        error = "skills and training outcome: " + error;
        return false;
    }
    const auto skill_ref = kg.getProperty(outcome, "skill");
    const std::string skill_name = kg.getProperty(skill, "name");
    if (skill_name.empty()) {
        error = "skills and training outcome points to an unnamed skill";
        return false;
    }

    logosphere::rules::OutcomeExecutor executor(kg, dice);
    const auto applied = executor.apply(
        outcome, {character, "chargen", "skills and training"});
    if (applied.status != logosphere::rules::OutcomeStatus::APPLIED) {
        error = "skills and training outcome failed: " + applied.error;
        return false;
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
      runner_(kg_, primitives_) {
    bind_primitives();
}

void ChargenSession::bind_primitives() {
    std::string error;
    const auto bind = [&](const std::string& name,
                          logosphere::rules::ProcedurePrimitive primitive) {
        if (!primitives_.bind_primitive(name, std::move(primitive), error)) {
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
    choices_.clear();
    return PrimitiveResult::advance();
}

ChargenSession::PrimitiveResult ChargenSession::roll_qualification(
    const PrimitiveContext&) {
    if (career_ == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed(
            "roll_qualification has no selected career");
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
    logosphere::rules::TaskCheckRunner runner(kg_, dice_);
    const auto survival = runner.run(
        check.id, sheet_.id, "chargen", "survival");
    if (!survival.ok()) return PrimitiveResult::failed(survival.error);
    const auto& execution = *survival.execution;
    sheet_.life.push_back(
        {term, execution.passed() ? "survived" : "did not survive",
         check_detail(execution), execution.roll().id});
    if (!execution.passed()) {
        finish_reason_ =
            "The career ended here. (Cepheus: a failed survival roll is "
            "death; the mishap table is an optional rule we have not "
            "absorbed yet.)";
        return PrimitiveResult::advance("failed");
    }
    return PrimitiveResult::advance("passed");
}

ChargenSession::PrimitiveResult ChargenSession::roll_training(
    const PrimitiveContext&) {
    const int term = sheet_.terms_served + 1;
    uint64_t skill_roll = 0;
    std::string skill;
    std::string error;
    if (!select_and_apply_training(kg_, career_, sheet_.id, dice_, skill_roll,
                                   skill, error)) {
        return PrimitiveResult::failed(error);
    }
    sheet_.skills.push_back(skill);
    sheet_.life.push_back({term, "gained " + skill,
                           "skills and training", skill_roll});
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
    const bool natural_twelve = roll.total == 12;
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

ChargenSession::PrimitiveResult ChargenSession::advance_term(
    const PrimitiveContext&) {
    const int term = sheet_.terms_served + 1;
    sheet_.age_years   += 4;
    sheet_.terms_served = term;
    sheet_.life.push_back({term, "term ends",
                           "age " + std::to_string(sheet_.age_years), 0});
    write_sheet(kg_, sheet_);
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

bool run_chargen(const ChargenRequest& request,
                 kg::KGModule& kg,
                 logosphere::dice::DiceService& dice,
                 CharacterSheet& out,
                 std::string& error) {
    ChargenSession session(kg, dice);
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
    int guard = 0;
    while (!session.finished() &&
           session.sheet().terms_served < request.max_terms) {
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
        if (!choices.empty() &&
            choices.front().label == "Serve another term") {
            if (!session.choose("1", error)) return false;
            continue;
        }
        break;
    }
    if (!session.finished() && !session.choices().empty() &&
        session.choices().front().label == "Serve another term") {
        if (!session.choose("2", error)) return false;
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
    out = summary.str();
    return true;
}

}  // namespace logovger
