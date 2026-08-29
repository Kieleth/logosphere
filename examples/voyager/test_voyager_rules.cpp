// The rules, held to what this game says about them.
//
// It loads the shipped seeds through the engine's ingestion verifier,
// which is the thing that string-matches every citation into the
// vendored book and refuses a number the quoted text does not contain.
// A seed that has drifted, a quote that no longer resolves, a career
// whose throw cites the wrong cell: all of those fail here rather than
// in front of a player.
//
// It then asks the graph the questions the game asks it, because
// verification proves FIDELITY and says nothing about CONSEQUENCE. A
// citation can be perfect on a career the game can never read.
//
// And it closes one of the four leaks by measurement rather than by
// intention: EVERY number the book fixes is seeded as a RuleConstant
// AND is named by code that reads it. A constant nothing reads is a
// rule that is cited, verified, counted, and executed by nothing.

#undef NDEBUG

#include "procedure_catalog.h"
#include "rule_loader.h"
#include "session.h"
#include "sheet.h"
#include "test_support.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/rules/procedure_runner.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/voyager_chargen_ontology_registry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                       \
    do {                                                                \
        if (condition) {                                                \
            ++passed;                                                   \
        } else {                                                        \
            ++failed;                                                   \
            std::cout << "FAIL: " << message << '\n';                   \
        }                                                               \
    } while (false)

kg::OntologyRegistry game_registry() {
    auto registry = logosphere::ontology::registry();
    registry.extend(rulebook::ontology::registry());
    registry.extend(voyager_chargen::ontology::registry());
    return registry;
}

bool as_int(const std::string& text, long long& out) {
    try {
        size_t end = 0;
        out = std::stoll(text, &end);
        return end == text.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

int main() {
    kg::KGModule world(game_registry());
    world.setMode(kg::KGMode::MINIMAL);

    const auto primitives = voyager::make_procedure_registry();
    std::string error;
    if (!voyager::load_rules(world, VOYAGER_GAME_DIR, VOYAGER_CORPUS_DIR,
                             VOYAGER_BOOK_CORPUS_DIR,
                             primitives, error)) {
        std::cout << "FAIL: the rules did not verify or load: " << error
                  << '\n';
        return 1;
    }
    std::cout << "rules verified and loaded, edition "
              << voyager::rules_edition(world) << '\n';
    CHECK(!voyager::rules_edition(world).empty(),
          "the world names no rulebook edition, so a tape cannot say what "
          "it was played against");

    // ---- what the book prints, present and readable ----------------
    const auto careers = world.findByType("Career");
    CHECK(careers.size() == 24,
          "the chapter details twenty-four careers and the graph holds "
          << careers.size());

    // Every career must answer the two questions the game asks it. A
    // citation that resolves on a career the game cannot read is
    // fidelity without consequence.
    size_t with_throws = 0;
    for (const kg::EntityID career : careers) {
        long long qualification = 0;
        long long survival = 0;
        const bool ok =
            as_int(world.getProperty(career, "qualification_check"),
                   qualification) &&
            as_int(world.getProperty(career, "survival_check"), survival) &&
            world.exists(static_cast<kg::EntityID>(qualification)) &&
            world.exists(static_cast<kg::EntityID>(survival));
        CHECK(ok, "career '" << world.getProperty(career, "name")
                             << "' has no readable pair of throws");
        if (!ok) continue;
        ++with_throws;
        for (const long long check : {qualification, survival}) {
            const auto id = static_cast<kg::EntityID>(check);
            const std::string attribute =
                world.getProperty(id, "attribute_ref");
            const std::string target =
                world.getProperty(id, "target_number");
            CHECK(!attribute.empty() && !target.empty(),
                  "throw '" << world.getProperty(id, "name")
                            << "' is missing a characteristic or a target");
            CHECK(world.getRegistry().hasProperty("Character", attribute),
                  "throw '" << world.getProperty(id, "name")
                            << "' names attribute '" << attribute
                            << "', which Character does not declare");
        }
    }
    CHECK(with_throws == careers.size(),
          "only " << with_throws << " of " << careers.size()
                  << " careers carry both throws");

    // ---- the sheet's own inputs ------------------------------------
    std::vector<std::pair<kg::EntityID, std::string>> characteristics;
    CHECK(voyager::characteristics_in_order(world, characteristics, error),
          "the characteristics do not read back: " << error);
    CHECK(characteristics.size() == 6,
          "character creation rolls six characteristics and the graph "
          "holds " << characteristics.size());

    kg::EntityID modifiers = kg::INVALID_ENTITY;
    CHECK(voyager::characteristic_modifier_table(world, modifiers, error),
          "no single score-to-modifier lookup: " << error);
    if (modifiers != kg::INVALID_ENTITY) {
        CHECK(world.getRelated(modifiers, "HAS_PART").size() == 12,
              "the modifier table has "
              << world.getRelated(modifiers, "HAS_PART").size()
              << " rows and the book prints twelve");
    }

    // ---- the procedure is data, and the data is walkable -----------
    kg::EntityID procedure = kg::INVALID_ENTITY;
    for (const kg::EntityID id : world.findByType("Procedure")) {
        if (world.getProperty(id, "name") == voyager::kProcedureName) {
            procedure = id;
        }
    }
    CHECK(procedure != kg::INVALID_ENTITY,
          "the world holds no Procedure named '"
          << voyager::kProcedureName << "'");
    if (procedure != kg::INVALID_ENTITY) {
        CHECK(world.getRelated(procedure, "HAS_PART").size() == 3,
              "the procedure has "
              << world.getRelated(procedure, "HAS_PART").size()
              << " steps and this slice declares three");
    }

    // ---- one whole character, with a scripted referee --------------
    //
    // No model is called here and none ever will be: the referee is a
    // seam, and a test holds it exactly as a tape does. What this
    // measures is CONSEQUENCE — that the rules just verified can be
    // walked end to end and produce a character the graph can answer
    // questions about.
    logosphere::dice::DiceService dice;
    dice.seed_stream("chargen", 20260821);
    voyager::Session session(world, dice);
    session.set_arbiter("test_voyager_rules");
    std::string offered_first;
    session.set_referee(
        [&](const voyager::RefereeQuestion& question, std::string& answer,
            std::string& why) {
            if (question.site == "background") {
                answer = "Raised on a tender in a shipping lane, and left "
                         "it the first year anybody would take them.";
                return true;
            }
            if (question.site == "careers") {
                CHECK(question.allowed.size() == careers.size(),
                      "the referee was offered " << question.allowed.size()
                      << " careers and the rules hold " << careers.size());
                // Narrowing, from the legal set and nowhere else.
                offered_first = question.allowed.front();
                answer = offered_first + " | the door that opened first\n" +
                         question.allowed.back() + " | the stranger one";
                return true;
            }
            if (question.site == "moment") {
                CHECK(question.allowed.size() ==
                          world.findByType("MomentKind").size(),
                      "the referee was offered "
                          << question.allowed.size()
                          << " kinds and the graph holds "
                          << world.findByType("MomentKind").size());
                CHECK(!question.low.empty() && !question.high.empty(),
                      "the moment ask carries no bounds, so the referee "
                      "cannot know the book's limits");
                // The FLOOR itself: the book says between, inclusive,
                // and answering exactly at the bound proves it.
                answer = question.allowed.front() + " | " + question.low +
                         "\nA situation arrives, as situations do.";
                return true;
            }
            if (question.site == "moment.aftermath") {
                answer = "It passed, and left a mark that can be read.";
                return true;
            }
            why = "the test referee was asked '" + question.site + "'";
            return false;
        });

    CHECK(session.begin(error), "the character did not begin: " << error);
    CHECK(session.choices().size() == 2,
          "the referee offered two doors and the session shows "
          << session.choices().size());
    CHECK(!session.finished(),
          "the session finished before anybody chose a career");
    if (session.choices().size() == 2) {
        CHECK(session.choices().front().key == offered_first,
              "the first door is '" << session.choices().front().key
                                    << "', the referee offered '"
                                    << offered_first << "'");
        CHECK(!session.choices().front().detail.empty(),
              "a door states no odds, so the player is watching rather "
              "than deciding");
        std::string refused;
        CHECK(!session.choose("Not A Career", refused),
              "an answer nobody offered was accepted");
        CHECK(session.choose(offered_first, error),
              "the offered door was refused: " << error);
    }

    // The life continues past the career door: a season to spend, its
    // ways read from the game's own book, then a moment that breaks it.
    CHECK(!session.finished(),
          "the game ended at the career door; the life procedure never "
          "started");
    const size_t ways = world.findByType("SeasonMode").size();
    CHECK(session.choices().size() == ways,
          "the book fixes " << ways << " ways to spend a season and the "
          "session offers " << session.choices().size());
    if (!session.choices().empty()) {
        std::string refused;
        CHECK(!session.choose("a fourth way", refused),
              "a way the book never fixed was accepted");
        CHECK(session.choose(session.choices().front().key, error),
              "the season choice was refused: " << error);
    }
    CHECK(session.finished(),
          "the life did not reach the end of what is written");

    voyager::Sheet sheet;
    CHECK(voyager::read_sheet(world, session.character(), sheet, error),
          "the sheet could not be read: " << error);
    CHECK(sheet.lines.size() == characteristics.size(),
          "the sheet shows " << sheet.lines.size() << " characteristics "
          "and the graph holds " << characteristics.size());
    size_t scored = 0;
    for (const auto& line : sheet.lines) {
        if (!line.value.empty() && !line.modifier.empty()) ++scored;
    }
    CHECK(scored == characteristics.size(),
          "only " << scored << " characteristics were rolled and stated");
    CHECK(sheet.career == offered_first,
          "the sheet says the career is '" << sheet.career << "'");
    CHECK(!sheet.background.empty(),
          "the character carries no background, so the screen cannot be "
          "rebuilt from the graph");
    CHECK(!sheet.age.empty(), "the character has no age");

    // The season cost exactly what the book fixes, derived through the
    // constants and never through a literal here: majority plus one
    // season, both read from the graph.
    {
        long long majority = 0;
        long long season = 0;
        for (const kg::EntityID id : world.findByType("RuleConstant")) {
            const std::string name = world.getProperty(id, "name");
            long long value = 0;
            if (!as_int(world.getProperty(id, "constant_value"), value)) {
                continue;
            }
            if (name == "age_of_majority") majority = value;
            if (name == "season_standard_years") season = value;
        }
        CHECK(majority > 0 && season > 0,
              "the constants this check derives from did not read back");
        CHECK(sheet.age == std::to_string(majority + season),
              "one season from majority should age to "
                  << (majority + season) << " and the sheet says "
                  << sheet.age);
    }

    // Stage is a QUERY: one moment faced, one row on the record,
    // counted from what happened rather than stored anywhere.
    CHECK(sheet.record.size() == 1,
          "one moment was faced and the record shows "
              << sheet.record.size() << " kinds lived");
    if (!sheet.record.empty()) {
        CHECK(sheet.record.front().count == "x1",
              "the one kind lived counts '" << sheet.record.front().count
                                            << "', not x1");
    }

    // The book's bounds bite: a chance outside them is refused, and
    // the run stops rather than playing a moment the rules forbid.
    {
        logosphere::dice::DiceService loose_dice;
        loose_dice.seed_stream("chargen", 3);
        voyager::Session loose(world, loose_dice);
        loose.set_arbiter("test_voyager_rules");
        loose.set_referee(
            [&](const voyager::RefereeQuestion& question,
                std::string& answer, std::string& why) {
                (void)why;
                if (question.site == "background") {
                    answer = "Someone.";
                } else if (question.site == "careers") {
                    answer = question.allowed.front() + " | a door";
                } else if (question.site == "moment") {
                    // No probability ceiling admits one and a half.
                    answer = question.allowed.front() +
                             " | 1.5\nA situation.";
                } else {
                    answer = "It ended.";
                }
                return true;
            });
        std::string why;
        CHECK(loose.begin(why), "the loose session did not begin: " << why);
        CHECK(loose.choose(loose.choices().front().key, why),
              "the loose career choice was refused: " << why);
        CHECK(!loose.choose(loose.choices().front().key, why),
              "a chance outside the book's bounds was accepted");
        CHECK(why.find("outside the book's bounds") != std::string::npos,
              "the refusal does not name the bounds: " << why);
    }

    // And a kind the book never defined is refused the same way.
    {
        logosphere::dice::DiceService alien_dice;
        alien_dice.seed_stream("chargen", 4);
        voyager::Session alien(world, alien_dice);
        alien.set_arbiter("test_voyager_rules");
        alien.set_referee(
            [&](const voyager::RefereeQuestion& question,
                std::string& answer, std::string& why) {
                (void)why;
                if (question.site == "background") {
                    answer = "Someone.";
                } else if (question.site == "careers") {
                    answer = question.allowed.front() + " | a door";
                } else if (question.site == "moment") {
                    answer = "weather | 0.5\nA storm arrives.";
                } else {
                    answer = "It ended.";
                }
                return true;
            });
        std::string why;
        CHECK(alien.begin(why), "the alien session did not begin: " << why);
        CHECK(alien.choose(alien.choices().front().key, why),
              "the alien career choice was refused: " << why);
        CHECK(!alien.choose(alien.choices().front().key, why),
              "a kind the book never defined was accepted");
        CHECK(why.find("not a kind the book defines") != std::string::npos,
              "the refusal does not name the kind: " << why);
    }

    // The refusals, which are the part that has to be loud.
    {
        logosphere::dice::DiceService second_dice;
        second_dice.seed_stream("chargen", 1);
        voyager::Session refuses(world, second_dice);
        std::string why;
        CHECK(!refuses.begin(why),
              "a session with no referee started anyway");
        CHECK(why.find("no referee") != std::string::npos,
              "a session with no referee did not say so: " << why);
    }
    {
        logosphere::dice::DiceService third_dice;
        third_dice.seed_stream("chargen", 2);
        voyager::Session invents(world, third_dice);
        invents.set_referee(
            [](const voyager::RefereeQuestion& question, std::string& answer,
               std::string&) {
                answer = question.site == "background"
                             ? "somewhere"
                             : "Time Traveller | not a career";
                return true;
            });
        std::string why;
        CHECK(!invents.begin(why),
              "the referee offered a career the rules never issued and the "
              "run carried on");
        CHECK(why.find("did not issue") != std::string::npos,
              "the refusal does not say what was wrong: " << why);
    }
    {
        logosphere::dice::DiceService fourth_dice;
        fourth_dice.seed_stream("chargen", 3);
        voyager::Session empty(world, fourth_dice);
        empty.set_referee(
            [](const voyager::RefereeQuestion& question, std::string& answer,
               std::string&) {
                answer = question.site == "background" ? "somewhere" : "";
                return true;
            });
        std::string why;
        CHECK(!empty.begin(why),
              "the referee offered nothing and the run carried on");
        CHECK(why.find("offered nothing") != std::string::npos,
              "the refusal does not say what was wrong: " << why);
    }
    {
        logosphere::dice::DiceService fifth_dice;
        fifth_dice.seed_stream("chargen", 4);
        voyager::Session broken(world, fifth_dice);
        broken.set_referee(
            [](const voyager::RefereeQuestion&, std::string&,
               std::string& why) {
                why = "the model is down";
                return false;
            });
        std::string why;
        CHECK(!broken.begin(why),
              "the referee failed and the run fell back to something");
        CHECK(why.find("the model is down") != std::string::npos,
              "the failure was swallowed: " << why);
    }

    // ---- every seeded constant has a reader ------------------------
    //
    // The gate, not a test naming the constants it happens to know. A
    // number the book fixes is put in the graph so a primitive can read
    // it instead of spelling it; one that no primitive names is a rule
    // in the graph that changes nothing, and it reads as working.
    size_t generated_skipped = 0;
    const std::string code =
        voyager_test::shipping_text(VOYAGER_GAME_DIR,
                                    generated_skipped);
    CHECK(!code.empty(), "no shipping source was read, so this proved "
                         "nothing");
    CHECK(generated_skipped >= 1,
          "the generated-file exclusion excluded nothing: either "
          "src/generated moved or the path match broke, and a broken "
          "match reports the schema's own generated output as leaks");
    const auto constants = world.findByType("RuleConstant");
    CHECK(!constants.empty(), "the rules fix no constants at all");
    for (const kg::EntityID id : constants) {
        const std::string name = world.getProperty(id, "name");
        const std::string value = world.getProperty(id, "constant_value");
        CHECK(code.find("\"" + name + "\"") != std::string::npos,
              "the rule constant '" << name << "' is seeded and no shipping "
              "source names it, so nothing reads it");
        // A single-character value cannot be discriminated by a text
        // scan: every loop writes a bare 1 and every array a bare 0,
        // so scanning for them would flag arithmetic, not leaks. The
        // reader requirement above still binds such a constant; only
        // this literal hunt stands down, and says so.
        if (value.size() < 2) continue;
        CHECK(!voyager_test::names_word(code, value),
              "the value of '" << name << "' (" << value << ") appears as a "
              "literal in shipping code, which is the leak the constant "
              "exists to close");
    }

    std::cout << (failed ? "FAILED " : "OK ") << passed << " passed, "
              << failed << " failed\n";
    return failed ? 1 : 0;
}
