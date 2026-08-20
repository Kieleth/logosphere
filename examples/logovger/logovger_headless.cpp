// Logovger with no window: a life played by whoever is asked.
//
// The same session the game runs, driven by an InputSource instead of
// a person. Every decision the rules leave open goes through one seam,
// including the referee's, so the run can be:
//
//   --random N     answers invented from a seed. No model, no human.
//                  Explores the rulebook and is reproducible.
//   --record FILE  answers taken live, written down.
//   --replay FILE  answers read back. The same life, exactly.
//
// The point of routing the REFEREE through the same seam is that
// "which characteristics does aging take" is a decision like any
// other. In --random it is answered by the dice-free generator, so a
// full life plays with no API key at all; in --replay it comes off the
// tape, so a recorded life reproduces without calling a model.
//
// Usage:
//   ./build/logovger-headless --random 7
//   ./build/logovger-headless --replay /tmp/life.tape

#include "chargen/chargen.h"
#include "chargen/procedure_catalog.h"
#include "chargen/rule_seed_loader.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_query.h"
#include "logosphere/replay/run_recorder.h"
#include "logosphere/replay/run_tape.h"
#include "logosphere/telemetry/session.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"

#ifdef LOGOVGER_WITH_LLM
#include "adjudicator.h"
#endif

#include <chrono>
#include <filesystem>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

namespace replay = logosphere::replay;

#ifndef LOGOVGER_GAME_DIR
#define LOGOVGER_GAME_DIR "."
#endif

std::string game_path(const std::string& rel) {
    return std::string(LOGOVGER_GAME_DIR) + "/" + rel;
}

kg::OntologyRegistry game_registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());
    out.extend(cepheus_book1_skills::ontology::registry());
    out.extend(cepheus_book1_character_creation::ontology::registry());
    return out;
}

// The same rule seeds the windowed game loads, verified the same way.
bool load_rules(kg::KGModule& world, std::string& why) {
    world.setMode(kg::KGMode::MINIMAL);
    const auto primitives = logovger::make_chargen_procedure_registry();
    return logovger::load_rule_seeds(
        world, LOGOVGER_GAME_DIR, primitives, why);
}

#ifdef LOGOVGER_WITH_LLM
// The model, and what it can see while it plays. The sheet is watched
// rather than copied so each question carries the life as it stands,
// which is what makes this playing rather than picking.
logovger::Adjudicator g_player;
const logovger::CharacterSheet* g_life = nullptr;
uint64_t g_seed = 0;

bool ask_the_player(const replay::Ask& ask, std::string& out,
                    std::string& error) {
    logovger::Judgment judgment;
    judgment.rule = "You are making this character, one decision at a "
                    "time, and you live with what you choose.";
    judgment.question = ask.prompt;
    judgment.options = ask.offered;
    judgment.count = 1;
    judgment.hint = "play a life worth reading, not the safest one";
    if (g_life) judgment.history = logovger::format_life(*g_life);

    std::vector<std::string> chosen;
    std::string reason;
    if (!g_player.decide(judgment, g_seed, chosen, reason, error)) {
        return false;
    }
    if (chosen.empty()) {
        error = "the player chose nothing at '" + ask.site + "'";
        return false;
    }
    out = chosen.front();
    if (!reason.empty()) {
        std::cout << "    [player] " << out << ": " << reason << "\n";
    }
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    std::string mode = "--random";
    std::string argument = "1";
    uint64_t pinned = 0;                 // 0 = not pinned
    size_t fork_at = 0;                  // --fork: which decision
    std::string fork_instead;            // --fork: answer it differently
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

    // Where the answers come from. The game below cannot tell which.
    std::unique_ptr<replay::InputSource> source;
    std::string tape_path;
    if (mode == "--replay") {
        std::string error;
        auto taped = replay::TapedInput::open(argument, error);
        if (!taped) {
            std::cout << "no replay: " << error << "\n";
            return 1;
        }
        std::cout << "replaying " << argument << " (" << taped->size()
                  << " answers)\n";
        source = std::move(taped);
    } else if (mode == "--forks") {
        // What else this life could have done. Reads the tape and
        // prints every decision that had another legal answer.
        std::string error;
        auto taped = replay::TapedInput::open(argument, error);
        if (!taped) {
            std::cout << "no tape: " << error << "\n";
            return 1;
        }
        if (!taped->records_alternatives()) {
            std::cout << argument
                      << " was recorded before tapes carried their "
                         "alternatives, so its branches cannot be seen. "
                         "Re-record it.\n";
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
        // The counterfactual. Replay this life up to one decision,
        // answer that decision differently, and let the rules play out
        // the rest. The trunk's seed comes with it, so the ONLY thing
        // that changed is the choice.
        std::string error;
        auto trunk = replay::TapedInput::open(argument, error);
        if (!trunk) {
            std::cout << "no tape: " << error << "\n";
            return 1;
        }
        if (!fork_given || fork_instead.empty()) {
            std::cout << "--fork needs --at N and --instead ANSWER. "
                         "Run --forks " << argument
                      << " to see which decisions have another road.\n";
            return 1;
        }
        // Everything past the fork has no tape to read, so it is
        // played by the generator, seeded from the trunk's own seed so
        // the branch is reproducible.
        //
        // Read through a SECOND handle. TapedInput::seed advances the
        // cursor when the seed is the entry it is sitting on, so asking
        // the trunk for it here would eat the entry the run itself
        // needs, the run would fall back to a different seed, and the
        // branch would differ from its trunk in the dice as well as the
        // decision. Measured: it changed the character's UPP, which is
        // rolled before any decision is made and must be identical.
        std::string peek_error;
        auto peek = replay::TapedInput::open(argument, peek_error);
        const uint64_t trunk_seed = peek ? peek->seed("chargen", 1) : 1;
        static replay::RandomInput beyond(trunk_seed);
        auto branch = replay::ForkedInput::create(
            std::move(trunk), fork_at, fork_instead, beyond, error);
        if (!branch) {
            std::cout << "no fork: " << error << "\n";
            return 1;
        }
        std::cout << "forking " << argument << " at " << fork_at
                  << ", answering '" << fork_instead << "' instead\n";
        tape_path = (std::filesystem::temp_directory_path() /
                     ("logovger-fork-" + std::to_string(fork_at) + "-" +
                      fork_instead + ".tape")).string();
        source = std::move(branch);
    } else if (mode == "--record") {
        tape_path = argument;
#ifdef LOGOVGER_WITH_LLM
        // The model plays the character. Every question the book asks
        // goes to it, with the life so far for context, and every
        // answer is taped: a life it plays once replays forever
        // without calling it again.
        std::string why;
        if (!g_player.initialize(game_path("lore/voyager.md"), why)) {
            std::cout << "no player: " << why << "\n";
            return 1;
        }
        source = std::make_unique<replay::LiveInput>(ask_the_player);
#else
        std::cout << "--record needs a build with LLM support; this one "
                     "has none. Use --random N to play without a model.\n";
        return 1;
#endif
    } else {
        // --random N seeds the WHOLE run: the generator answers every
        // question and hands out the dice seed too, so one number
        // reproduces everything. --seed would have nothing left to do,
        // and a flag that silently does nothing is worse than one that
        // refuses.
        if (pinned) {
            std::cout << "--seed does nothing with --random: the N in "
                         "--random N already seeds the dice and every "
                         "answer. Use --random " << pinned << ".\n";
            return 1;
        }
        source = std::make_unique<replay::RandomInput>(
            std::stoull(argument));
        tape_path = (std::filesystem::temp_directory_path() /
                     ("logovger-" + argument + ".tape")).string();
    }
    replay::RunTape tape(*source, tape_path);

    kg::KGModule world(game_registry());

    // The other half of a run. The tape holds what was DECIDED; this
    // holds what HAPPENED, as JSONL, and the two answer different
    // questions: replay the tape to get the same life, diff the facts
    // to find out whether a change altered one.
    //
    // The bus is declared before the session so it outlives it: the
    // recorder unsubscribes on the way out.
    logosphere::EventBus bus;
    logosphere::telemetry::Session telemetry;

    std::string why;
    if (!load_rules(world, why)) {
        std::cout << "the rules did not load: " << why << "\n";
        return 1;
    }

    // Which rulebook this life is played against. The tape holds the
    // seed and the answers; the world comes from those THROUGH THE
    // RULES, so without this a tape replayed after the rules move can
    // run green and produce a different life. The graph already knows
    // its own edition, so the tape just has to write it down.
    std::string edition;
    {
        const auto editions = world.findByType("IngestionEditionContext");
        if (!editions.empty()) {
            edition = world.getProperty(editions.front(), "context_key");
        }
    }
    tape.set_edition(edition);

    if (mode == "--replay" || mode == "--fork") {
        // Read back through a second handle: the one driving the run is
        // already positioned, and asking it anything moves the cursor.
        std::string open_error, why_not;
        auto check = replay::TapedInput::open(argument, open_error);
        if (check && !check->fits_edition(edition, why_not)) {
            std::cout << "the tape does not fit these rules: " << why_not
                      << "\n";
            return 1;
        }
    }

    // Attached AFTER the rulebook is in. Loading the seeds writes tens
    // of thousands of properties, and a trace of the library swamps
    // the trace of the life: 35k facts before the first die is rolled.
    // What a run is worth diffing over starts here.
    world.set_event_bus(&bus);
    auto* facts = static_cast<replay::RunRecorder*>(
        telemetry.register_instrument(
            std::make_unique<replay::RunRecorder>(bus)));

    // The dice seed is a decision like any other, so it is taped and a
    // replayed run rolls the same dice as the run it came from.
    //
    // Live sources take the fallback, so a recorded run needs a real
    // one or every recording starts on the same dice. Entropy by
    // default and --seed to pin: the seed being taped is what keeps
    // entropy compatible with exact replay.
    logosphere::dice::DiceService dice;
    const uint64_t fallback =
        pinned ? pinned
               : static_cast<uint64_t>(
                     std::chrono::steady_clock::now()
                         .time_since_epoch().count());
    const uint64_t seed = tape.seed("chargen", fallback);
    dice.seed_stream("chargen", seed);

    logovger::ChargenSession session(world, dice);
#ifdef LOGOVGER_WITH_LLM
    // What the player can see: the life as it grows, and the seed, so
    // its answers cache per life rather than across all of them.
    g_life = &session.sheet();
    g_seed = seed;
#endif

    // Who is answering, in words, for the record each decision leaves.
    // The session writes that record itself; this driver only says who
    // made it. A decision is a decision whoever made it, off a tape,
    // from a model, or invented from the seed, and writing the record
    // in each driver is how a third driver comes to write nothing.
    session.set_arbiter(mode == "--replay"  ? "tape:" + tape_path
                        : mode == "--record" ? "model, taped to " + tape_path
                                             : "seeded generator");

    // The referee, through the same seam as the player. In --random
    // this answers with no model and no key; in --replay it comes off
    // the tape. Either way the engine still holds the answer to the
    // rule.
    session.set_attribute_selector(
        [&tape](const logosphere::rules::AttributeSelectionRequest& request,
                std::vector<std::string>& chosen, std::string& error) {
            for (int i = 0; i < request.count; ++i) {
                replay::Ask ask;
                ask.site = "referee.attribute";
                ask.prompt = "which characteristic gives way";
                for (const auto& name : request.eligible) {
                    // Never offer one this rule already spent, nor one
                    // already picked for this same change.
                    if (std::find(request.already_taken.begin(),
                                  request.already_taken.end(), name) !=
                        request.already_taken.end()) continue;
                    if (std::find(chosen.begin(), chosen.end(), name) !=
                        chosen.end()) continue;
                    ask.offered.push_back(name);
                }
                if (ask.offered.empty()) {
                    error = "nothing left to take";
                    return false;
                }
                std::string answer;
                if (!tape.ask(ask, answer, error)) return false;
                chosen.push_back(answer);
            }
            return true;
        });

    // A rule that prints a fork asks it here. Injury 3 offers Strength
    // or Dexterity; mishap 1 offers "the same as a result of 2" or
    // "roll twice and take the lower". The executor refuses to guess,
    // by design, so a driver that leaves this unanswered ends the life
    // at the fork: measured at 19 of 200 seeds before this was wired.
    //
    // It goes to the same answer source as everything else rather than
    // taking option 0, so --record tapes which branch was taken and
    // --replay reproduces it. A silently collapsed fork would look
    // identical on the sheet and be invisible in the tape.
    session.set_choice_resolver(
        [&tape](const logosphere::rules::PendingChoice& request,
                int& option, std::string& error) {
            if (request.options.empty()) {
                error = "a rule offers a choice with no options";
                return false;
            }
            // The ANSWER stays the option index, because that is what
            // the tape stores and what replay matches on. The PROMPT
            // carries the labels, because prompt is defined as what a
            // human would have been shown and is never matched on, so
            // wording it properly cannot invalidate a tape.
            //
            // This dropped the labels until 2026-08-19 and sent bare
            // "1, 2, 3" to whoever was answering. A human clicking a
            // button still had the screen; a model had nothing. The
            // first recorded life stopped here, and the model was right
            // to stop: it asked what the three training tables were
            // rather than guess. ChoiceOption has carried `label` all
            // along.
            replay::Ask ask;
            ask.site = "referee.choice";
            std::ostringstream prompt;
            prompt << "the rule prints a fork; which branch";
            for (const auto& choice : request.options) {
                ask.offered.push_back(std::to_string(choice.option_index));
                prompt << "\n  " << choice.option_index << " = "
                       << (choice.label.empty() ? "(unnamed branch)"
                                                : choice.label);
            }
            ask.prompt = prompt.str();
            std::string answer;
            if (!tape.ask(ask, answer, error)) return false;
            for (const auto& choice : request.options) {
                if (std::to_string(choice.option_index) != answer) continue;
                option = choice.option_index;
                return true;
            }
            error = "'" + answer + "' is not a branch this rule offers";
            return false;
        });

    std::string error;
    if (!session.begin(seed, error)) {
        std::cout << "could not begin: " << error << "\n";
        return 1;
    }

    // The life, one decision at a time.
    int guard = 0;
    while (!session.finished() && !session.choices().empty()) {
        if (++guard > 400) {
            std::cout << "gave up after 400 decisions\n";
            return 1;
        }
        // Same split as referee.choice above: the key is the ANSWER
        // and the labels go in the PROMPT, which is never matched on.
        //
        // The keys alone are what a model was being handed until
        // 2026-08-19, and the prompts say things like "click one to
        // read what it can give you". There is nothing to click. A
        // recorded life stopped at the training tables with the model
        // saying, correctly, that it could see "CHOOSE EXACTLY 1 OF:
        // 1, 2, 3" and no indication of what 1, 2 or 3 were. It asked
        // instead of guessing, which is the behaviour we want and the
        // reason the gap was visible at all rather than silently
        // producing an arbitrary life.
        //
        // ProcedureChoice has carried label, detail and subject the
        // whole time, and its own header says a player should be able
        // to read an option before taking it.
        replay::Ask ask;
        ask.site = "chargen";
        std::ostringstream prompt;
        prompt << session.prompt();
        for (const auto& choice : session.choices()) {
            ask.offered.push_back(choice.key);
            prompt << "\n  " << choice.key;
            if (!choice.label.empty()) prompt << " = " << choice.label;
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
    }

    tape.flush();
    // Ids in a trace mean nothing without types, and creating an
    // entity emits no event, so the world is written once at the end
    // of the run alongside the facts.
    kg::Query who;
    who.types = {"Character", "SkillRating", "CurrencyBalance",
                 "PossessionHolding"};
    facts->snapshot_kg(world, who);
    std::cout << logovger::format_life(session.sheet());
    std::cout << "  [decisions] " << tape.entries() << "\n";
    if (!tape_path.empty()) {
        std::cout << "  [tape] " << tape_path << "\n";
    }
    return 0;
}
