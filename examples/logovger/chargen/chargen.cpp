#include "chargen/chargen.h"
#include "chargen/procedure_catalog.h"

#include "logosphere/rules/outcome_executor.h"
#include "logosphere/rules/rollable_table_runner.h"
#include "rules/characteristics.h"
#include "rules/ehex.h"

#include <algorithm>
#include <cctype>
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

int property_int(kg::KGModule& kg, kg::EntityID id, const std::string& key,
                 bool& ok) {
    const auto v = kg.getProperty(id, key);
    if (v.empty()) { ok = false; return 0; }
    return std::stoi(v);
}

int characteristic_of(const CharacterSheet& s, const std::string& slot) {
    if (slot == "strength")        return s.strength;
    if (slot == "dexterity")       return s.dexterity;
    if (slot == "endurance")       return s.endurance;
    if (slot == "intelligence")    return s.intelligence;
    if (slot == "education")       return s.education;
    if (slot == "social_standing") return s.social_standing;
    return -1;   // caller treats a negative as "no such characteristic"
}


// A TaskCheck as the book printed it: a target, the characteristic
// whose DM applies, and the dice to throw. The dice come from the
// referenced DiceExpression, not from an assumption that every throw
// is 2D6.
struct Check {
    int         target = 0;
    std::string attribute;
    int         count = 2, sides = 6, modifier = 0;
    bool        valid = false;
};

Check read_check(kg::KGModule& kg, const std::string& ref) {
    Check c;
    if (ref.empty()) return c;
    const auto id = static_cast<kg::EntityID>(std::stoul(ref));
    const auto target = kg.getProperty(id, "target_number");
    c.attribute = kg.getProperty(id, "attribute_ref");
    if (target.empty() || c.attribute.empty()) return c;
    c.target = std::stoi(target);
    const auto dice_ref = kg.getProperty(id, "dice");
    if (dice_ref.empty()) return c;
    const auto d = static_cast<kg::EntityID>(std::stoul(dice_ref));
    bool ok = true;
    c.count = property_int(kg, d, "dice_count", ok);
    c.sides = property_int(kg, d, "dice_sides", ok);
    const auto m = kg.getProperty(d, "dice_modifier");
    if (!m.empty()) c.modifier = std::stoi(m);
    c.valid = ok;
    return c;
}

// One throw of the book's basic check: 2D6, plus the DM of the named
// characteristic, against a target. The DM comes from the tested
// primitive, which agrees with the book's published table row for row.
struct Throw {
    bool        passed = false;
    uint64_t    roll_id = 0;
    std::string detail;
};

Throw throw_check(const Check& c, const CharacterSheet& sheet,
                  logosphere::dice::DiceService& dice,
                  const std::string& purpose) {
    Throw t;
    logosphere::dice::DiceExpression expr;
    expr.count    = c.count;
    expr.sides    = c.sides;
    expr.modifier = c.modifier;

    const auto roll = dice.roll(expr, "chargen", purpose);
    const int dm    = characteristic_dm(characteristic_of(sheet, c.attribute));
    const int total = roll.total + dm;
    t.roll_id = roll.id;
    t.passed  = total >= c.target;

    std::ostringstream d;
    d << expr.to_string() << " = " << roll.total
      << (dm >= 0 ? " +" : " ") << dm << " DM -> " << total
      << " vs " << c.target << "+";
    t.detail = d.str();
    return t;
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
    const auto skill_ref = kg.getProperty(outcome, "skill");
    if (skill_ref.empty()) {
        error = "skills and training outcome has no skill reference";
        return false;
    }
    kg::EntityID skill = kg::INVALID_ENTITY;
    try {
        const auto parsed = std::stoull(skill_ref);
        if (parsed > std::numeric_limits<kg::EntityID>::max()) {
            throw std::out_of_range("entity id");
        }
        skill = static_cast<kg::EntityID>(parsed);
    } catch (...) {
        error = "skills and training outcome has invalid skill reference '" +
                skill_ref + "'";
        return false;
    }
    if (!kg.exists(skill) ||
        !kg.getRegistry().isSubtypeOf(kg.getType(skill), "Skill")) {
        error = "skills and training outcome skill reference does not point "
                "to a Skill";
        return false;
    }
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
    bind("choose_term_end", [this](const PrimitiveContext& context) {
        return choose_term_end(context);
    });
    bind("finish_character", [this](const PrimitiveContext& context) {
        return finish_character(context);
    });
}

// A number the book fixes, read from the graph rather than typed here.
// Absent means the seed is thin, and the fallback is stated out loud
// rather than hidden.
int ChargenSession::constant(const char* name, int fallback) const {
    for (auto id : kg_.findByType("RuleConstant")) {
        if (kg_.getProperty(id, "name") != name) continue;
        const auto v = kg_.getProperty(id, "constant_value");
        if (!v.empty()) return std::stoi(v);
    }
    return fallback;
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

void ChargenSession::offer_careers() {
    choices_.clear();
    // Numbered, not lettered: the book offers 24 careers and a single
    // letter runs out at 16, which silently made eight of them
    // unchoosable.
    size_t n = 0;
    for (auto id : kg_.findByType("Career")) {
        const auto name = kg_.getProperty(id, "name");
        if (name.empty()) continue;
        // "Once you leave a career you cannot return to it. The Draft
        // and the Drifter career are exceptions - the Drifter career
        // is always open."
        if (name != "Drifter" &&
            std::find(sheet_.careers_served.begin(),
                      sheet_.careers_served.end(),
                      name) != sheet_.careers_served.end())
            continue;
        const auto q = read_check(kg_, kg_.getProperty(id, "qualification_check"));
        const auto v = read_check(kg_, kg_.getProperty(id, "survival_check"));
        if (!q.valid || !v.valid) continue;   // a career that cannot be
                                              // thrown for is not offered
        choices_.push_back({std::to_string(n + 1), name,
                            "qualify on " + q.attribute + " " +
                                std::to_string(q.target) + "+, survive on " +
                                v.attribute + " " +
                                std::to_string(v.target) + "+"});
        ++n;
    }
    prompt_ = "Which career do you try for?";
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
        // The book grants a new career only to someone with terms
        // left: "If you're leaving your current career and your total
        // number of terms in character creation is less than seven,
        // you may go to step 3 to choose a new career". At seven the
        // permission is gone, so there is nothing to choose and the
        // character is finished rather than being offered a list the
        // rules will not honour.
        const int cap = constant("max_terms", 7);
        if (sheet_.terms_served >= cap) {
            finish_reason_ = std::to_string(cap) +
                             " terms served: the book allows no more.";
            return PrimitiveResult::advance("finish");
        }
        offer_careers();
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
    const auto check =
        read_check(kg_, kg_.getProperty(career_, "qualification_check"));
    if (!check.valid) {
        return PrimitiveResult::failed(
            "career '" + sheet_.career +
            "' has no qualification throw in the graph");
    }
    // "If this is not your first career, you suffer a -2 DM for every
    // previous career in which you have served." The number is a cited
    // RuleConstant in the graph, not a literal here; the careers
    // already served are on the sheet.
    auto penalised = check;
    const int prior = static_cast<int>(sheet_.careers_served.size()) - 1;
    if (prior > 0)
        penalised.modifier += constant("prior_career_dm", -2) * prior;
    const auto qualification =
        throw_check(penalised, sheet_, dice_, "qualification");
    sheet_.qualified = qualification.passed;
    sheet_.life.push_back(
        {0, qualification.passed ? "qualified" : "failed to qualify",
         qualification.detail, qualification.roll_id});
    if (qualification.passed) return PrimitiveResult::advance("passed");

    // "You must either submit to the Draft or take the Drifter career
    // for this term." Drifter is always open, so a refusal sends you
    // back to the list rather than ending the character. The Draft's
    // own 1D6 table is not absorbed yet, and the line says so.
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
        const auto ref = kg_.getProperty(drafted.selection->outcome(),
                                         "drafted_career");
        if (ref.empty()) {
            return PrimitiveResult::failed(
                "a Draft row names no career");
        }
        const auto drafted_career =
            static_cast<kg::EntityID>(std::stoul(ref));
        career_name = kg_.getProperty(drafted_career, "name");
        sheet_.life.push_back(
            {sheet_.terms_served, "drafted into the " + career_name,
             "1D6 = " + std::to_string(drafted.selection->roll().total),
             drafted.selection->roll().id});
    }
    career_ = find_named(kg_, "Career", career_name);
    if (career_ == kg::INVALID_ENTITY) {
        return PrimitiveResult::failed(
            "the Drifter career is not in the graph, and the book says "
            "it is always open");
    }
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
    const auto check = read_check(kg_,
                                  kg_.getProperty(career_, "survival_check"));
    if (!check.valid) {
        return PrimitiveResult::failed(
            "career '" + sheet_.career +
            "' has no survival throw in the graph");
    }
    const auto s = throw_check(check, sheet_, dice_, "survival");
    sheet_.life.push_back({term, s.passed ? "survived" : "did not survive",
                           s.detail, s.roll_id});
    if (!s.passed) {
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
        const int cap = constant("max_terms", 7);
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

    // An auto-player takes the named career, serves out, and then
    // stops. It must also cope with being TURNED AWAY: the book sends
    // a refusal back to the list, so a career whose throw cannot be
    // made would loop forever. One refusal is the answer; a human
    // would pick Drifter or the Draft, and this player has no opinion.
    int guard = 0;
    while (!session.finished() &&
           session.sheet().terms_served < request.max_terms) {
        const auto& choices = session.choices();
        const bool at_career_menu =
            !choices.empty() && choices.front().label != "Serve another term";
        if (at_career_menu) {
            if (!session.sheet().career.empty() || ++guard > 1) break;
            if (!session.choose(request.career_name, error)) return false;
            continue;
        }
        if (!session.choose("1", error)) return false;
    }
    if (!session.finished() && !session.choices().empty() &&
        session.choices().front().label == "Serve another term")
        session.choose("2", error);
    out = session.sheet();
    return true;
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

}  // namespace logovger
