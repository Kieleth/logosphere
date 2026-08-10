#include "chargen/chargen.h"

#include "rules/characteristics.h"
#include "rules/ehex.h"

#include <cctype>
#include <sstream>

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

// One throw of the book's basic check: 2D6, plus the DM of the named
// characteristic, against a target. The DM comes from the tested
// primitive, which agrees with the book's published table row for row.
struct Throw {
    bool        passed = false;
    uint64_t    roll_id = 0;
    std::string detail;
};

Throw throw_check(int target, const std::string& attribute,
                  const CharacterSheet& sheet,
                  logosphere::dice::DiceService& dice,
                  const std::string& purpose) {
    Throw t;
    logosphere::dice::DiceExpression two_d6;
    two_d6.count = 2;
    two_d6.sides = 6;

    const auto roll = dice.roll(two_d6, "chargen", purpose);
    const int dm    = characteristic_dm(characteristic_of(sheet, attribute));
    const int total = roll.total + dm;
    t.roll_id = roll.id;
    t.passed  = total >= target;

    std::ostringstream d;
    d << "2D6 = " << roll.total << (dm >= 0 ? " +" : " ") << dm
      << " DM -> " << total << " vs " << target << "+";
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

// Roll on a career's skill table and apply the row's outcome. The
// whole chain is graph data: career HAS_PART table, table HAS_PART
// rows, row -> outcome -> skill. Returns the skill's name, empty when
// the career has no table (the book gives every career one, so an
// empty result means the seed is thin, not that the rule is optional).
std::string roll_for_skill(kg::KGModule& kg, kg::EntityID career,
                           kg::EntityID character,
                           logosphere::dice::DiceService& dice,
                           uint64_t& roll_id_out) {
    kg::EntityID table = kg::INVALID_ENTITY;
    for (auto part : kg.getRelated(career, "HAS_PART")) {
        for (auto t : kg.findByType("RollableTable"))
            if (t == part) { table = part; break; }
        if (table != kg::INVALID_ENTITY) break;
    }
    if (table == kg::INVALID_ENTITY) return "";

    // The table names its own dice; a d6 table and a d3 table are
    // both legal and this code does not care which it got.
    const auto dice_ref = kg.getProperty(table, "dice");
    if (dice_ref.empty()) return "";
    const auto dice_id = static_cast<kg::EntityID>(std::stoul(dice_ref));
    logosphere::dice::DiceExpression expr;
    bool ok = true;
    expr.count = property_int(kg, dice_id, "dice_count", ok);
    expr.sides = property_int(kg, dice_id, "dice_sides", ok);
    if (!ok) return "";

    const auto roll = dice.roll(expr, "chargen", "skills and training");
    roll_id_out = roll.id;

    // The row whose band contains the result.
    for (auto row : kg.getRelated(table, "HAS_PART")) {
        bool have = true;
        const int lo = property_int(kg, row, "roll_min", have);
        const int hi = property_int(kg, row, "roll_max", have);
        if (!have || roll.total < lo || roll.total > hi) continue;

        const auto outcome_ref = kg.getProperty(row, "outcome");
        if (outcome_ref.empty()) return "";
        const auto outcome =
            static_cast<kg::EntityID>(std::stoul(outcome_ref));
        const auto skill_ref = kg.getProperty(outcome, "skill");
        if (skill_ref.empty()) return "";   // not a GrantSkill outcome
        const auto skill = static_cast<kg::EntityID>(std::stoul(skill_ref));

        auto level = kg.getProperty(outcome, "skill_level");
        if (level.empty()) level = "1";

        // "If you gain a skill as a result and you do not already have
        // levels in that skill, take it at level 1. If you already have
        // the skill, increase your skill by one level." One rating per
        // skill, raised on a repeat, never a second rating.
        for (auto part : kg.getRelated(character, "HAS_PART")) {
            if (kg.getProperty(part, "skill") != skill_ref) continue;
            const auto had = kg.getProperty(part, "skill_level");
            const int next = (had.empty() ? 0 : std::stoi(had)) + 1;
            kg.setProperty(part, "skill_level", std::to_string(next));
            return kg.getProperty(skill, "name") + "-" +
                   std::to_string(next);
        }

        auto rating = kg.createEntity("SkillRating");
        kg.setProperty(rating, "skill", skill_ref);
        kg.setProperty(rating, "skill_level", level);
        kg.createRelation(character, "HAS_PART", rating);
        return kg.getProperty(skill, "name") + "-" + level;
    }
    return "";
}

}  // namespace

// --- the session: a life, one decision at a time -------------------

bool ChargenSession::begin(uint64_t seed, std::string& error) {
    error.clear();
    sheet_ = CharacterSheet{};
    finished_ = false;
    drained_ = 0;
    dice_.seed_stream("chargen", seed);

    sheet_.id = kg_.createEntity("Character");
    if (sheet_.id == kg::INVALID_ENTITY) {
        error = "the world does not know what a Character is; load the "
                "cepheus character-creation pack";
        return false;
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

    offer_careers();
    if (choices_.empty()) {
        error = "no Career in the knowledge graph; load a careers seed";
        return false;
    }
    return true;
}

void ChargenSession::offer_careers() {
    choices_.clear();
    const char* keys = "abcdefghijklmnop";
    size_t n = 0;
    for (auto id : kg_.findByType("Career")) {
        const auto name = kg_.getProperty(id, "name");
        if (name.empty() || n >= 16) continue;
        const auto qt = kg_.getProperty(id, "qualification_target");
        const auto qa = kg_.getProperty(id, "qualification_dm_characteristic");
        const auto st = kg_.getProperty(id, "survival_target");
        const auto sa = kg_.getProperty(id, "survival_dm_characteristic");
        choices_.push_back({std::string(1, keys[n]), name,
                            "qualify on " + qa + " " + qt +
                                "+, survive on " + sa + " " + st + "+"});
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

void ChargenSession::run_term() {
    const int term = sheet_.terms_served + 1;
    bool have = true;
    const int surv_target = property_int(kg_, career_, "survival_target",
                                         have);
    const auto surv_attr = kg_.getProperty(career_,
                                           "survival_dm_characteristic");

    const auto s = throw_check(surv_target, surv_attr, sheet_, dice_,
                               "survival");
    sheet_.life.push_back({term, s.passed ? "survived" : "did not survive",
                           s.detail, s.roll_id});
    if (!s.passed) {
        // "If you fail this roll, your character is dead." The mishap
        // table is an optional rule and a later slice.
        finish("The career ended here. (Cepheus: a failed survival roll "
               "is death; the mishap table is an optional rule we have "
               "not absorbed yet.)");
        return;
    }

    uint64_t skill_roll = 0;
    const auto skill = roll_for_skill(kg_, career_, sheet_.id, dice_,
                                      skill_roll);
    if (!skill.empty()) {
        sheet_.skills.push_back(skill);
        sheet_.life.push_back({term, "gained " + skill,
                               "skills and training", skill_roll});
    }

    sheet_.age_years   += 4;
    sheet_.terms_served = term;
    sheet_.life.push_back({term, "term ends",
                           "age " + std::to_string(sheet_.age_years), 0});
    write_sheet(kg_, sheet_);

    choices_ = {{"a", "Serve another term",
                 "four more years in the " + sheet_.career},
                {"b", "Muster out", "leave with what you have"}};
    prompt_ = "Term " + std::to_string(term) + " is over. What now?";
}

bool ChargenSession::choose(const std::string& answer, std::string& error) {
    error.clear();
    if (finished_) { error = "this life is finished"; return false; }

    std::string a = answer;
    for (auto& ch : a) ch = static_cast<char>(std::tolower(ch));
    const Choice* picked = nullptr;
    for (const auto& c : choices_) {
        std::string label = c.label;
        for (auto& ch : label) ch = static_cast<char>(std::tolower(ch));
        if (a == c.key || a == label) { picked = &c; break; }
    }
    if (!picked) {
        error = "'" + answer + "' is not one of the options";
        return false;
    }

    // Career chosen: qualify, then live the first term.
    if (career_ == kg::INVALID_ENTITY) {
        career_ = find_named(kg_, "Career", picked->label);
        if (career_ == kg::INVALID_ENTITY) {
            error = "no Career named '" + picked->label + "'";
            return false;
        }
        sheet_.career = picked->label;
        bool have = true;
        const int qt = property_int(kg_, career_, "qualification_target",
                                    have);
        const auto qa = kg_.getProperty(career_,
                                        "qualification_dm_characteristic");
        if (!have || qa.empty()) {
            error = "career '" + picked->label +
                    "' has no qualification throw";
            return false;
        }
        const auto q = throw_check(qt, qa, sheet_, dice_, "qualification");
        sheet_.qualified = q.passed;
        sheet_.life.push_back({0, q.passed ? "qualified"
                                           : "failed to qualify",
                               q.detail, q.roll_id});
        if (!q.passed) {
            finish("Not accepted. (A life that never joined is still a "
                   "life; the book would send you to the draft, which is "
                   "a later slice.)");
            return true;
        }
        run_term();
        return true;
    }

    if (picked->key == "a") { run_term(); return true; }
    finish("Mustered out at age " + std::to_string(sheet_.age_years) +
           " after " + std::to_string(sheet_.terms_served) + " term(s).");
    return true;
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

    while (!session.finished() && session.sheet().terms_served <
                                      request.max_terms) {
        if (!session.choose("a", error)) return false;
    }
    if (!session.finished()) session.choose("b", error);
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
