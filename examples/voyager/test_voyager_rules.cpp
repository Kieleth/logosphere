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
            if (question.site == "arrival") {
                CHECK(question.allowed.size() ==
                          world.findByType("MomentKind").size(),
                      "the director was asked to rate "
                          << question.allowed.size()
                          << " kinds and the graph holds "
                          << world.findByType("MomentKind").size());
                CHECK(!question.low.empty() && !question.high.empty(),
                      "the arrival ask carries no bounds");
                // The CEILING itself, for every kind: the book says
                // between, inclusive, and a life this stormy reaches a
                // moment within a season or two under any seed.
                for (const auto& kind : question.allowed) {
                    answer += kind + " | " + question.high + "\n";
                }
                return true;
            }
            if (question.site == "doors") {
                CHECK(!question.vocab.at("doors").empty() &&
                          !question.vocab.at("weights").empty(),
                      "the doors ask carries no vocabulary");
                // The rung that permits the most without being
                // unbounded, read off the graph, so the two effects
                // per list below are inside its cap.
                std::string rung;
                long long best = -1;
                for (const kg::EntityID id : world.findByType("Weight")) {
                    long long limit = 0;
                    if (as_int(world.getProperty(id, "weight_effect_limit"),
                               limit) && limit > best) {
                        best = limit;
                        rung = world.getProperty(id, "weight_key");
                    }
                }
                std::string standing;
                for (const kg::EntityID id :
                     world.findByType("StandingKind")) {
                    standing = world.getProperty(id, "standing_key");
                    break;
                }
                answer = "weight | " + rung + "\nsituation\n"
                         "A situation arrives, as situations do.\n";
                for (const auto& door : question.vocab.at("doors")) {
                    answer += "door | " + door + " | " + question.low +
                              " | the " + door + " way\n"
                              "risk | leave_mark | risked " + door + "\n"
                              "risk | bind_standing | " + standing +
                              " | Person | The Spotter\n"
                              "reach | leave_mark | reached " + door + "\n"
                              "reach | bind_standing | " + standing +
                              " | Person | The Spotter\n";
                }
                return true;
            }
            if (question.site == "price") {
                answer = "chance | " + question.low +
                         "\nrisk | leave_mark | risked by plan\n"
                         "reach | leave_mark | reached by plan\n";
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

    // The life continues past the career door: seasons to spend, their
    // ways read from the game's own book plus the player's way out,
    // until a season breaks and the doors are on the table.
    CHECK(!session.finished(),
          "the game ended at the career door; the life procedure never "
          "started");
    const size_t ways = world.findByType("SeasonMode").size() + 1;
    CHECK(session.choices().size() == ways,
          "the book fixes " << (ways - 1) << " ways to spend a season, "
          "plus ending the making, and the session offers "
          << session.choices().size());
    {
        std::string refused;
        CHECK(!session.choose("a fifth way", refused),
              "a way the book never fixed was accepted");
    }
    const size_t doors = world.findByType("Door").size();
    // A season on the table and a moment on the table both offer four
    // choices (three ways plus ending the making; three doors plus the
    // player's own), so they are told apart by the first way the book
    // fixes, read off the first season prompt.
    const std::string first_way = session.choices().front().key;
    const auto at_season = [&](const voyager::Session& s) {
        return !s.choices().empty() && s.choices().front().key == first_way;
    };
    size_t seasons = 0;
    while (!session.finished() && at_season(session) && seasons < 12) {
        ++seasons;
        CHECK(session.choose(session.choices().front().key, error),
              "the season choice was refused: " << error);
    }
    CHECK(!at_season(session) && session.choices().size() == doors,
          "after " << seasons << " season(s) at the ceiling rate the "
          "session offers " << session.choices().size()
          << " choices, and a broken season offers " << doors
          << " doors");
    if (session.choices().size() == doors) {
        // A kind never faced shows its doors unpriced.
        CHECK(session.choices().front().detail.find("unpriced") !=
                  std::string::npos,
              "a rookie's door came priced: '"
                  << session.choices().front().detail << "'");
        CHECK(session.choose(session.choices().front().key, error),
              "the door was refused: " << error);
    }
    // The life goes on after the moment; the player ends it.
    CHECK(!session.finished() && at_season(session),
          "after the moment the session should offer the next season");
    CHECK(session.choose("enough", error),
          "ending the making was refused: " << error);
    CHECK(session.finished(),
          "the life did not end when the player said so");

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
        long long lived = 0;
        for (const kg::EntityID e :
             world.getRelated(session.character(), "LIVED")) {
            if (world.getType(e) == "SeasonLived") ++lived;
        }
        CHECK(sheet.age == std::to_string(majority + season * lived),
              lived << " season(s) from majority should age to "
                    << (majority + season * lived) << " and the sheet says "
                    << sheet.age);
    }

    // Stage is a QUERY: every kind that landed is one row counted
    // from the MomentFaced records, and nothing stores it.
    size_t kinds_lived = 0;
    size_t moments = 0;
    for (const kg::EntityID e : world.getRelated(session.character(), "LIVED")) {
        if (world.getType(e) == "MomentFaced") ++moments;
    }
    for (const auto& row : sheet.record) {
        if (row.count == "x1") ++kinds_lived;
    }
    CHECK(moments >= 1 && kinds_lived == moments,
          moments << " moment record(s) and " << kinds_lived
                  << " kind row(s) counting x1");

    // Teeth: exactly what the taken door's list said lands, and
    // nothing else. One mark whose words follow the draw, one standing
    // toward a person the director named, who now exists in the
    // playing's own scope.
    {
        size_t marks = 0;
        size_t standings = 0;
        bool against = false;
        std::string mark;
        for (const kg::EntityID e :
             world.getRelated(session.character(), "LIVED")) {
            const std::string type = world.getType(e);
            if (type == "MarkLeft") {
                ++marks;
                mark = world.getProperty(e, "mark_text");
            } else if (type == "StandingHeld") {
                ++standings;
            } else if (type == "MomentFaced") {
                against = world.getProperty(e, "moment_went_against") == "true";
            }
        }
        CHECK(marks == 1, "one mark was in the list that landed and "
                          << marks << " were left");
        CHECK(mark.rfind(against ? "risked" : "reached", 0) == 0,
              "the draw " << (against ? "went against" : "spared")
                          << " them and the mark says '" << mark << "'");
        CHECK(standings == 1, standings << " standing(s) held, expected 1");
        size_t spotters = 0;
        for (const kg::EntityID id : world.findByType("Person")) {
            if (world.getProperty(id, "name") == "The Spotter") ++spotters;
        }
        CHECK(spotters == 1, "the director named one person and the world "
                             "holds " << spotters);
        bool on_record = false;
        for (const auto& row : sheet.record) {
            if (row.label == "The Spotter") on_record = true;
        }
        CHECK(on_record, "the standing does not show on the sheet");
    }

    // The player's own door: the words go through the price, and the
    // record says which door it was.
    {
        logosphere::dice::DiceService own_dice;
        own_dice.seed_stream("chargen", 9);
        voyager::Session own(world, own_dice);
        own.set_arbiter("test_voyager_rules");
        own.set_referee(
            [&](const voyager::RefereeQuestion& question,
                std::string& answer, std::string& why) {
                (void)why;
                if (question.site == "background") {
                    answer = "Someone.";
                } else if (question.site == "careers") {
                    answer = question.allowed.front() + " | a door";
                } else if (question.site == "arrival") {
                    for (const auto& kind : question.allowed) {
                        answer += kind + " | " + question.high + "\n";
                    }
                } else if (question.site == "doors") {
                    answer = "weight | " + question.vocab.at("weights").front() +
                             "\nsituation\nA storm.\n";
                    for (const auto& door : question.vocab.at("doors")) {
                        answer += "door | " + door + " | " + question.low +
                                  " | a way\n";
                    }
                } else if (question.site == "price") {
                    answer = "chance | " + question.low + "\n";
                } else {
                    answer = "It ended.";
                }
                return true;
            });
        std::string why;
        CHECK(own.begin(why), "the own-door session did not begin: " << why);
        CHECK(own.choose(own.choices().front().key, why),
              "the own-door career choice was refused: " << why);
        for (int i = 0; i < 12 && at_season(own); ++i) {
            CHECK(own.choose(own.choices().front().key, why),
                  "a season was refused: " << why);
        }
        const std::string players = own.players_door_key();
        CHECK(!players.empty(), "the graph names no door as the player's");
        CHECK(!at_season(own) && own.choices().size() == doors &&
                  own.choose(players, why),
              "the player's door was refused: " << why);
        CHECK(own.awaiting_plan(), "the player's door did not ask for words");
        std::string refused;
        CHECK(!own.choose("   ", refused), "empty words were accepted");
        CHECK(own.choose("I talk my way past the spotter.", why),
              "the plan was refused: " << why);
        bool taken_own = false;
        for (const kg::EntityID e : world.getRelated(own.character(), "LIVED")) {
            if (world.getType(e) == "MomentFaced" &&
                world.getProperty(e, "moment_door") == players) {
                taken_own = true;
            }
        }
        CHECK(taken_own, "the record does not say the player's door was taken");
    }

    // Refusals at the doors. Each scripted director breaks one rule of
    // the book, and each run must stop naming that rule. `run` plays a
    // life to the doors with the given doors reply and reports whether
    // the doors were refused, and why.
    const size_t doors_count = doors;
    const auto run = [&](const std::function<std::string(
                             const voyager::RefereeQuestion&)>& doors_reply,
                         const std::string& arrival_chance,
                         const std::function<void(voyager::Session&)>& before,
                         std::string& why) {
        logosphere::dice::DiceService d;
        d.seed_stream("chargen", 5);
        voyager::Session s(world, d);
        s.set_arbiter("test_voyager_rules");
        s.set_referee([&](const voyager::RefereeQuestion& q, std::string& a,
                          std::string& e) {
            (void)e;
            if (q.site == "background") {
                a = "Someone.";
            } else if (q.site == "careers") {
                a = q.allowed.front() + " | a door";
            } else if (q.site == "arrival") {
                for (const auto& kind : q.allowed) {
                    a += kind + " | " +
                         (arrival_chance.empty() ? q.high : arrival_chance) +
                         "\n";
                }
            } else if (q.site == "doors") {
                a = doors_reply(q);
            } else {
                a = "It ended.";
            }
            return true;
        });
        if (!s.begin(why)) return false;
        if (!s.choose(s.choices().front().key, why)) return false;
        if (before) before(s);
        for (int i = 0; i < 12; ++i) {
            if (!s.choose(s.choices().front().key, why)) return true;
            if (!at_season(s) && s.choices().size() == doors_count) {
                why = "the doors were accepted";
                return false;
            }
        }
        why = "no season broke in twelve tries";
        return false;
    };
    // A characteristic already at the schema's floor, so a move down
    // has nowhere to go. Set through the validated path, like any write.
    const auto floor_first = [&](voyager::Session& s) {
        std::string slot;
        for (const kg::EntityID id : world.findByType("Characteristic")) {
            slot = world.getProperty(id, "attribute_ref");
            break;
        }
        const auto* def = world.getRegistry().findProperty("Character", slot);
        const long long minimum =
            def && def->has_min ? static_cast<long long>(def->min_value) : 0;
        kg::KGOpSetProperty op;
        op.target.id = s.character();
        op.property = slot;
        op.value = std::to_string(minimum);
        kg::KGOpBatchReport report;
        kg::apply_kg_ops_atomically({kg::KGOp{op}}, world, report);
    };
    const auto doors_base = [](const voyager::RefereeQuestion& q,
                               const std::string& rung) {
        std::string a = "weight | " + rung + "\nsituation\nA storm.\n";
        for (const auto& door : q.vocab.at("doors")) {
            a += "door | " + door + " | " + q.low + " | a way\n";
        }
        return a;
    };
    std::string rung_none, rung_capped_no_turn, first_label, first_turn;
    for (const kg::EntityID id : world.findByType("Weight")) {
        const std::string limit = world.getProperty(id, "weight_effect_limit");
        if (limit == "0") rung_none = world.getProperty(id, "weight_key");
        if (!limit.empty() && limit != "0" &&
            world.getProperty(id, "weight_may_turn") == "false") {
            rung_capped_no_turn = world.getProperty(id, "weight_key");
        }
    }
    for (const kg::EntityID id : world.findByType("Characteristic")) {
        first_label = world.getProperty(id, "characteristic_abbreviation");
        break;
    }
    for (const kg::EntityID id : world.findByType("Turn")) {
        first_turn = world.getProperty(id, "turn_key");
        break;
    }
    CHECK(!rung_none.empty() && !rung_capped_no_turn.empty() &&
              !first_label.empty() && !first_turn.empty(),
          "the graph did not supply the rungs, label and turn these "
          "refusals lean on");
    struct Refusal {
        const char* claim;
        const char* names;
        std::function<std::string(const voyager::RefereeQuestion&)> reply;
        std::string arrival;
        std::function<void(voyager::Session&)> before;
    };
    const std::vector<Refusal> refusals = {
        {"an effect on the rung that permits none was accepted",
         "may do 0 thing",
         [&](const voyager::RefereeQuestion& q) {
             return doors_base(q, rung_none) + "risk | leave_mark | x\n";
         }, "", nullptr},
        {"a move below the schema's floor was accepted", "cannot fall below",
         [&](const voyager::RefereeQuestion& q) {
             return doors_base(q, rung_capped_no_turn) +
                    "risk | move_characteristic | " + first_label + " | -1\n";
         }, "", floor_first},
        {"a move of two was accepted", "never more",
         [&](const voyager::RefereeQuestion& q) {
             return doors_base(q, rung_capped_no_turn) +
                    "risk | move_characteristic | " + first_label + " | +2\n";
         }, ""},
        {"two opposite pulls were accepted", "opposite directions",
         [&](const voyager::RefereeQuestion& q) {
             std::string rung;
             for (const kg::EntityID id : world.findByType("Weight")) {
                 if (world.getProperty(id, "weight_unbounded") == "true") {
                     rung = world.getProperty(id, "weight_key");
                 }
             }
             return doors_base(q, rung) +
                    "risk | move_characteristic | " + first_label + " | +1\n"
                    "risk | move_characteristic | " + first_label + " | -1\n";
         }, ""},
        {"a turn on a rung that may not turn was accepted",
         "may not turn the life",
         [&](const voyager::RefereeQuestion& q) {
             return doors_base(q, rung_capped_no_turn) +
                    "risk | turn_life | " + first_turn + "\n";
         }, ""},
        {"an effect the book never allowed was accepted",
         "not a thing a moment may do",
         [&](const voyager::RefereeQuestion& q) {
             return doors_base(q, rung_capped_no_turn) +
                    "risk | sprout_wings | now\n";
         }, ""},
        {"a missing door was accepted", "written once",
         [&](const voyager::RefereeQuestion& q) {
             std::string a = "weight | " + rung_none + "\nsituation\nA storm.\n";
             a += "door | " + q.vocab.at("doors").front() + " | " + q.low +
                  " | a way\n";
             return a;
         }, ""},
        {"a chance outside the arrival bounds was accepted",
         "outside the book's bounds",
         [&](const voyager::RefereeQuestion& q) {
             return doors_base(q, rung_none);
         }, "1.5"},
    };
    for (const auto& refusal : refusals) {
        std::string why;
        const bool refused =
            run(refusal.reply, refusal.arrival, refusal.before, why);
        CHECK(refused, refusal.claim << ": " << why);
        CHECK(why.find(refusal.names) != std::string::npos,
              "the refusal does not name the rule '" << refusal.names
                                                       << "': " << why);
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
