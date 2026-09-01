// Voyager with no window: one character, made by whoever is asked.
//
// The same session the windowed game runs, driven by an InputSource
// instead of a person. EVERY decision the rules leave open goes through
// one seam — the referee's two and the player's one — so the run can
// be:
//
//   --random N     answers invented from a seed. No model, no key.
//                  Exercises the rules and is reproducible.
//   --record FILE  answers taken live from a model, written down.
//   --replay FILE  answers read back. The same character, exactly,
//                  offline, with no model call at all.
//   --forks FILE   what else that character could have been.
//   --fork FILE --at N --instead KEY   take one of those roads.
//   --book         what the graph holds of the game's own book, in
//                  the words it cites. The endpoint the writing loop
//                  iterates against: write, regenerate, read back.
//
// WHERE THE NARRATION WENT. The background is written by a model and
// cannot be derived from a seed, so a replay that did not have it could
// not reproduce the screen. It is taped like any other answer, at a
// free-form site: `referee.background` holds the prose verbatim and
// `referee.careers` holds the option set the model authored. The
// PLAYER's question that follows carries those keys in `Ask.offered`,
// which is what makes a recorded character forkable at the one decision
// this slice has.
//
// Usage:
//   ./build/voyager-headless --random 7
//   ./build/voyager-headless --record /tmp/life.tape
//   ./build/voyager-headless --replay /tmp/life.tape

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_query.h"
#include "logosphere/replay/run_recorder.h"
#include "logosphere/replay/run_tape.h"
#include "logosphere/telemetry/session.h"

#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/voyager_chargen_ontology_registry.h"

#include "procedure_catalog.h"
#include "rule_loader.h"
#include "session.h"
#include "sheet.h"

#ifdef VOYAGER_WITH_LLM
#include "model_referee.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace replay = logosphere::replay;

#ifndef VOYAGER_GAME_DIR
#error "VOYAGER_GAME_DIR undefined: the game cannot find its own rules"
#endif
#ifndef VOYAGER_CORPUS_DIR
#error "VOYAGER_CORPUS_DIR undefined: declare the corpus this game reads"
#endif

std::string game_path(const std::string& relative) {
    return std::string(VOYAGER_GAME_DIR) + "/" + relative;
}

kg::OntologyRegistry game_registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());
    out.extend(voyager_chargen::ontology::registry());
    return out;
}

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n\"'.");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n\"'.");
    return text.substr(first, last - first + 1);
}

// What a synthetic answer to a FREE-FORM site may draw from, handed
// over by the session at the moment it asks. The engine deliberately
// answers a free-form ask with an empty string, because it has no idea
// what a plausible answer looks like in someone's game; this is the
// game saying. It is a file-scope value because RandomInput's hook
// takes the Ask and the Ask carries no such set — an ask whose answer
// is a LIST cannot put its keys in `offered`, which is validated
// against a single answer.
//
// It exists only for --random. Nothing on the live path reads it, so a
// referee that fails still stops the run.
std::vector<std::string> g_allowed;

// The numeric bounds a free-form answer must respect, as the graph
// states them, handed over the same way as g_allowed and for the same
// reason: the generator must answer inside the rules without this
// file spelling a rule value.
std::string g_low;
std::string g_high;
// The named lists a structured answer composes from, as the graph
// holds them. The generator spells none of them.
std::map<std::string, std::vector<std::string>> g_vocab;

// A chance inside the graph's bounds, picked by the seed.
std::string chance_from(uint64_t roll) {
    double low = 0.0, high = 0.0;
    try {
        low = std::stod(g_low);
        high = std::stod(g_high);
    } catch (...) {
        return {};
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << low + (high - low) * (static_cast<double>(roll % 101) / 100.0);
    return out.str();
}

std::string synthetic(const replay::Ask& ask, uint64_t roll) {
    if (ask.site == "referee.background") {
        // NOT a stand-in narration. It says what it is, on screen, so a
        // run with no referee cannot be mistaken for one with a bad
        // referee.
        return "(no referee: this life was played by the seeded "
               "generator, which invents no prose)";
    }
    if (ask.site == "referee.careers") {
        if (g_allowed.empty()) return {};
        // A deterministic slice of the legal set: enough doors to be a
        // decision, chosen by the seed rather than by taste.
        std::ostringstream out;
        const size_t count = 3 + (roll % 3);
        for (size_t i = 0; i < count && i < g_allowed.size(); ++i) {
            const size_t at =
                (roll / (i + 1) + i * 7) % g_allowed.size();
            out << g_allowed[at] << " | offered by the generator\n";
        }
        return out.str();
    }
    if (ask.site == "referee.seasons") {
        if (g_allowed.empty()) return {};
        std::ostringstream out;
        for (const auto& way : g_allowed) {
            out << way << " | (no referee: a plan invented by the seeded "
                          "generator)\n";
        }
        return out.str();
    }
    if (ask.site == "referee.season") {
        return "(no referee: a season told by the seeded generator, which "
               "tells no story)";
    }
    if (ask.site == "referee.arrival") {
        // Every kind rated, at one chance the seed picks inside the
        // graph's bounds, so some seasons break and most do not.
        if (g_allowed.empty()) return {};
        const std::string chance = chance_from(roll);
        if (chance.empty()) return {};
        std::ostringstream out;
        for (const auto& kind : g_allowed) out << kind << " | " << chance << "\n";
        return out.str();
    }
    if (ask.site == "referee.doors") {
        // The first rung the graph lists, which the book makes the one
        // that permits no effect, so the doors carry none; every door
        // the director writes, priced by the seed. The prose names
        // itself machine-made, as the background does.
        const auto weights = g_vocab.find("weights");
        const auto doors = g_vocab.find("doors");
        if (weights == g_vocab.end() || weights->second.empty() ||
            doors == g_vocab.end() || doors->second.empty()) {
            return {};
        }
        const std::string chance = chance_from(roll);
        if (chance.empty()) return {};
        std::ostringstream out;
        out << "weight | " << weights->second.front() << "\nsituation\n"
            << "(no referee: a situation invented by the seeded generator, "
               "which writes no prose)\n";
        for (const auto& door : doors->second) {
            out << "door | " << door << " | " << chance
                << " | offered by the generator\n";
        }
        return out.str();
    }
    if (ask.site == "referee.price") {
        const std::string chance = chance_from(roll);
        if (chance.empty()) return {};
        return "chance | " + chance + "\n";
    }
    if (ask.site == "referee.moment.aftermath") {
        return "(no referee: an outcome named by the seeded generator, "
               "which tells no story)";
    }
    if (ask.site == "chargen.plan") {
        return "(no player: a plan invented by the seeded generator, which "
               "makes no plans)";
    }
    return {};
}

// ------------------------------------------------------------- book
// The graph as the book's reader sees it. This is the endpoint the
// writing loop iterates against: write a chapter, regenerate the
// seeds, and read back what the world now holds, in the exact words
// it will cite. Reads the graph and nothing else, so what it prints
// is what the game can actually see.
int print_book() {
    kg::KGModule world(game_registry());
    world.setMode(kg::KGMode::MINIMAL);
    const auto primitives = voyager::make_procedure_registry();
    std::string why;
    if (!voyager::load_rules(world, VOYAGER_GAME_DIR, VOYAGER_CORPUS_DIR,
                             VOYAGER_BOOK_CORPUS_DIR, primitives, why)) {
        std::cout << "the rules did not load: " << why << "\n";
        return 1;
    }
    std::cout << "edition " << voyager::rules_edition(world) << "\n";
    const auto print_typed = [&world](const char* heading,
                                      const char* type,
                                      const char* key_slot,
                                      const char* text_slot) {
        std::cout << "\n" << heading << "\n";
        for (const kg::EntityID id : world.findByType(type)) {
            const std::string key = world.getProperty(id, key_slot);
            if (!key.empty()) std::cout << "  [" << key << "] ";
            else std::cout << "  ";
            std::cout << world.getProperty(id, text_slot) << "\n";
        }
    };
    print_typed("-- the kinds a moment can be --", "MomentKind",
                "moment_kind_key", "source_quote");
    print_typed("-- the ways a season is spent --", "SeasonMode",
                "season_mode_key", "name");
    print_typed("-- what the book leaves open --", "UnsettledQuestion",
                "", "question_text");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--book") == 0) return print_book();
    }
    std::string mode = "--random";
    std::string argument = "1";
    uint64_t pinned = 0;
    size_t fork_at = 0;
    std::string fork_instead;
    bool fork_given = false;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--seed") == 0) {
            pinned = std::stoull(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--at") == 0) {
            fork_at = std::stoull(argv[i + 1]);
            fork_given = true;
        } else if (std::strcmp(argv[i], "--instead") == 0) {
            fork_instead = argv[i + 1];
        } else if (std::strncmp(argv[i], "--", 2) == 0) {
            mode = argv[i];
            argument = argv[i + 1];
        }
    }

    // Where the answers come from. The session below cannot tell which.
    std::unique_ptr<replay::InputSource> source;
    std::string tape_path;
#ifdef VOYAGER_WITH_LLM
    voyager::ModelReferee model;
#endif

    if (mode == "--replay") {
        std::string error;
        auto taped = replay::TapedInput::open(argument, error);
        if (!taped) {
            std::cout << "no replay: " << error << "\n";
            return 1;
        }
        std::cout << "replaying " << argument << " (" << taped->size()
                  << " answers, no model)\n";
        source = std::move(taped);
    } else if (mode == "--forks") {
        std::string error;
        auto taped = replay::TapedInput::open(argument, error);
        if (!taped) {
            std::cout << "no tape: " << error << "\n";
            return 1;
        }
        if (!taped->records_alternatives()) {
            std::cout << argument
                      << " was recorded before tapes carried their "
                         "alternatives, so its branches cannot be seen.\n";
            return 1;
        }
        for (const auto& fork : taped->forks()) {
            std::cout << "[" << fork.index << "] took '" << fork.taken
                      << "', could have taken";
            for (const auto& other : fork.untaken) {
                std::cout << " '" << other << "'";
            }
            std::cout << "\n";
        }
        return 0;
    } else if (mode == "--fork") {
        std::string error;
        auto trunk = replay::TapedInput::open(argument, error);
        if (!trunk) {
            std::cout << "no tape: " << error << "\n";
            return 1;
        }
        if (!fork_given || fork_instead.empty()) {
            std::cout << "--fork needs --at N and --instead KEY. Run "
                         "--forks " << argument
                      << " to see which decisions had another road.\n";
            return 1;
        }
        // Read the trunk's seed through a SECOND handle: asking the one
        // driving the run would eat the entry the run itself needs, and
        // the branch would then differ from its trunk in the dice as
        // well as in the decision.
        std::string peek_error;
        auto peek = replay::TapedInput::open(argument, peek_error);
        const uint64_t trunk_seed = peek ? peek->seed("chargen", 1) : 1;
        static replay::RandomInput beyond(trunk_seed, synthetic);
        auto branch = replay::ForkedInput::create(
            std::move(trunk), fork_at, fork_instead, beyond, error);
        if (!branch) {
            std::cout << "no fork: " << error << "\n";
            return 1;
        }
        std::cout << "forking " << argument << " at " << fork_at
                  << ", answering '" << fork_instead << "' instead\n";
        tape_path = (std::filesystem::temp_directory_path() /
                     ("voyager-fork-" + std::to_string(fork_at) + ".tape"))
                        .string();
        source = std::move(branch);
    } else if (mode == "--record") {
        tape_path = argument;
#ifdef VOYAGER_WITH_LLM
        std::string why;
        if (!model.initialize(game_path("referee/brief.md"), why)) {
            std::cout << "no referee: " << why << "\n";
            return 1;
        }
        std::cout << "recording to " << tape_path << " (referee: "
                  << model.who() << ")\n";
        source = std::make_unique<replay::LiveInput>(
            [&model](const replay::Ask& ask, std::string& out,
                     std::string& error) {
                voyager::RefereeQuestion question;
                question.site = ask.site;
                question.prompt = ask.prompt;
                question.allowed = ask.offered;
                if (ask.site == "chargen.door") {
                    // The model stops being the referee here and
                    // becomes the player: one door, taken.
                    std::ostringstream prompt;
                    prompt << "You are the PLAYER now, not the referee. "
                              "One of these doors is yours.\n\n"
                           << ask.prompt
                           << "\n\nAnswer with one key alone, exactly "
                              "as it is written above, and nothing "
                              "else.";
                    question.prompt = prompt.str();
                } else if (ask.site == "chargen.plan") {
                    question.prompt =
                        "You are the PLAYER now, not the referee. You "
                        "chose to say what you would do.\n\n" + ask.prompt +
                        "\n\nSay it, first person, two sentences, and "
                        "nothing else.";
                }
                std::string reply;
                if (!model.answer(question, reply, error)) return false;
                if (ask.offered.empty()) {
                    out = reply;
                    return true;
                }
                const std::string picked = trimmed(reply);
                if (std::find(ask.offered.begin(), ask.offered.end(),
                              picked) == ask.offered.end()) {
                    error = "the model answered '" + picked +
                            "', which was not on offer";
                    return false;
                }
                out = picked;
                return true;
            });
#else
        std::cout << "--record needs a build with LLM support; this one "
                     "has none. Use --random N to exercise the rules "
                     "without a model.\n";
        return 1;
#endif
    } else {
        // --random N seeds the WHOLE run: the generator answers every
        // question and hands out the dice seed too, so one number
        // reproduces everything. --seed would have nothing left to do.
        if (pinned) {
            std::cout << "--seed does nothing with --random: the N in "
                         "--random N already seeds the dice and every "
                         "answer. Use --random " << pinned << ".\n";
            return 1;
        }
        source = std::make_unique<replay::RandomInput>(
            std::stoull(argument), synthetic);
        tape_path = (std::filesystem::temp_directory_path() /
                     ("voyager-" + argument + ".tape")).string();
    }

    replay::RunTape tape(*source, tape_path);
    kg::KGModule world(game_registry());

    // The bus is declared before the session so it outlives it: the
    // recorder unsubscribes on the way out.
    logosphere::EventBus bus;
    logosphere::telemetry::Session telemetry;

    world.setMode(kg::KGMode::MINIMAL);
    const auto primitives = voyager::make_procedure_registry();
    std::string why;
    if (!voyager::load_rules(world, VOYAGER_GAME_DIR, VOYAGER_CORPUS_DIR,
                             VOYAGER_BOOK_CORPUS_DIR,
                             primitives, why)) {
        std::cout << "the rules did not load: " << why << "\n";
        return 1;
    }

    const std::string edition = voyager::rules_edition(world);
    tape.set_edition(edition);
    if (mode == "--replay" || mode == "--fork") {
        std::string open_error, why_not;
        auto check = replay::TapedInput::open(argument, open_error);
        if (check && !check->fits_edition(edition, why_not)) {
            std::cout << "the tape does not fit these rules: " << why_not
                      << "\n";
            return 1;
        }
    }

    // Attached AFTER the rules are in: loading them writes tens of
    // thousands of properties, and a trace of the library swamps the
    // trace of the life.
    world.set_event_bus(&bus);
    auto* facts = static_cast<replay::RunRecorder*>(
        telemetry.register_instrument(
            std::make_unique<replay::RunRecorder>(bus)));

    // The dice seed is a decision like any other, so it is taped and a
    // replayed run rolls the same dice as the run it came from.
    logosphere::dice::DiceService dice;
    const uint64_t fallback =
        pinned ? pinned
               : static_cast<uint64_t>(std::chrono::steady_clock::now()
                                           .time_since_epoch().count());
    const uint64_t seed = tape.seed("chargen", fallback);
    dice.seed_stream("chargen", seed);

    voyager::Session session(world, dice);
    session.set_arbiter(
        mode == "--replay" ? "tape:" + argument
        : mode == "--fork" ? "fork of " + argument
#ifdef VOYAGER_WITH_LLM
        : mode == "--record" ? model.who() + ", taped to " + tape_path
#endif
                             : "seeded generator");

    // The referee, through the same seam as everything else. In
    // --random this answers with no model and no key; in --replay it
    // comes off the tape. Either way the engine still owns what the
    // answer is allowed to be.
    session.set_referee(
        [&tape](const voyager::RefereeQuestion& question,
                std::string& answer, std::string& error) {
            replay::Ask ask;
            ask.site = "referee." + question.site;
            ask.prompt = question.prompt;
            // Deliberately free-form: the background is prose and the
            // career offer is a LIST, and `offered` is validated
            // against a single answer. What a synthetic source may draw
            // from goes through g_allowed instead.
            g_allowed = question.allowed;
            g_low = question.low;
            g_high = question.high;
            g_vocab = question.vocab;
            return tape.ask(ask, answer, error);
        });

    std::string error;
    if (!session.begin(error)) {
        std::cout << "could not begin: " << error << "\n";
        return 1;
    }

    int guard = 0;
    while (!session.finished() && !session.choices().empty()) {
        if (++guard > 256) {
            std::cout << "gave up after 256 decisions\n";
            return 1;
        }
        // The keys are the ANSWER and the labels go in the PROMPT,
        // which is never matched on. This is the ask that carries the
        // model-authored option set into the tape.
        replay::Ask ask;
        ask.site = "chargen.door";
        std::ostringstream prompt;
        prompt << session.prompt();
        for (const auto& choice : session.choices()) {
            ask.offered.push_back(choice.key);
            prompt << "\n  " << choice.key;
            if (!choice.label.empty()) prompt << " -- " << choice.label;
            if (!choice.detail.empty()) prompt << " (" << choice.detail << ")";
        }
        ask.prompt = prompt.str();
        std::string answer;
        if (!tape.ask(ask, answer, error)) {
            std::cout << "the run stopped: " << error << "\n";
            return 1;
        }
        if (!session.choose(answer, error)) {
            std::cout << "'" << answer << "' was refused: " << error << "\n";
            return 1;
        }
        // The player's own door: a second, free-form ask carries the
        // player's words, taped verbatim like the referee's prose.
        if (session.awaiting_plan()) {
            replay::Ask plan;
            plan.site = "chargen.plan";
            plan.prompt = session.prompt();
            std::string words;
            if (!tape.ask(plan, words, error)) {
                std::cout << "the run stopped: " << error << "\n";
                return 1;
            }
            if (!session.choose(words, error)) {
                std::cout << "the plan was refused: " << error << "\n";
                return 1;
            }
        }
    }

    tape.flush();
    kg::Query who;
    who.types = {"Character", "Narration", "ArbiterDecision"};
    facts->snapshot_kg(world, who);

    // The sheet, read out of the graph. Nothing here held a copy of it.
    voyager::Sheet sheet;
    if (!voyager::read_sheet(world, session.character(), sheet, error)) {
        std::cout << "the sheet could not be read: " << error << "\n";
        return 1;
    }
    std::cout << "\n";
    for (const auto& line : sheet.lines) {
        std::cout << "  " << line.label << " " << line.value << " ("
                  << line.modifier << ")\n";
    }
    std::cout << "  age " << sheet.age << "\n";
    std::cout << "  career: "
              << (sheet.career.empty() ? "none" : sheet.career) << "\n\n";
    std::cout << sheet.background << "\n\n";
    std::cout << "  [decisions] " << tape.entries() << "\n";
    std::cout << "  [node] " << tape.node() << "\n";
    if (!tape_path.empty()) std::cout << "  [tape] " << tape_path << "\n";
    return 0;
}
