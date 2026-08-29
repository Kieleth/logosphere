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
    CHECK(session.finished(), "the slice did not end at the first door");

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
        CHECK(!voyager_test::names_word(code, value),
              "the value of '" << name << "' (" << value << ") appears as a "
              "literal in shipping code, which is the leak the constant "
              "exists to close");
    }

    std::cout << (failed ? "FAILED " : "OK ") << passed << " passed, "
              << failed << " failed\n";
    return failed ? 1 : 0;
}
