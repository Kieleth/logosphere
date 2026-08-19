// RunTape: where a run's answers come from.
//
// Three sources behind one interface, and the claims are: a seeded
// source answers the same way twice (so a fuzz run is reproducible), a
// tape replays exactly what was recorded, and a tape that no longer
// fits the run ABORTS rather than quietly asking live, because a
// replay that falls back to live is a test reporting success for a
// build it never exercised.

#include "logosphere/replay/run_tape.h"

#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace replay = logosphere::replay;

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                        \
    do {                                                                 \
        if (condition) { ++passed; }                                     \
        else { ++failed; std::cout << "FAIL: " << (message) << "\n"; }   \
    } while (false)

replay::Ask pick(const char* site, std::vector<std::string> offered) {
    replay::Ask ask;
    ask.site = site;
    ask.prompt = "choose";
    ask.offered = std::move(offered);
    return ask;
}

// Not "/tmp": that path does not exist on Windows, where the writes
// silently went nowhere and every replay then failed to find its tape.
std::string tape_path(const char* name) {
    return (std::filesystem::temp_directory_path() /
            (std::string("logosphere-tape-test-") + name + ".jsonl"))
        .string();
}

// A seeded source is reproducible: that is what makes a fuzz run worth
// recording at all.
void the_same_seed_answers_the_same_way() {
    const auto play = [](uint64_t seed) {
        replay::RandomInput source(seed);
        std::string joined, answer, error;
        for (int i = 0; i < 20; ++i) {
            source.answer(pick("chargen", {"a", "b", "c", "d"}), answer,
                          error);
            joined += answer;
        }
        return joined;
    };
    const std::string first = play(7);
    const std::string second = play(7);
    const std::string other = play(8);
    std::cout << "  [measure] seed 7: " << first << "\n";
    std::cout << "  [measure] seed 8: " << other << "\n";
    CHECK(first == second, "the same seed replays the same answers");
    CHECK(first != other,
          "and a different seed explores differently, or it is not "
          "fuzzing anything");
}

// A random source never invents an answer the rules did not offer.
void a_random_answer_is_always_one_that_was_offered() {
    replay::RandomInput source(42);
    std::string answer, error;
    bool all_offered = true;
    for (int i = 0; i < 200; ++i) {
        source.answer(pick("chargen", {"x", "y"}), answer, error);
        if (answer != "x" && answer != "y") all_offered = false;
    }
    CHECK(all_offered, "200 answers, every one of them on the menu");
}

// Record a run, replay it, get the same answers in the same order.
void a_tape_replays_what_was_recorded() {
    const std::string path = tape_path("roundtrip");
    std::vector<std::string> recorded;
    {
        replay::RandomInput source(3);
        replay::RunTape tape(source, path);
        std::string answer, error;
        for (int i = 0; i < 10; ++i) {
            CHECK(tape.ask(pick("chargen", {"1", "2", "3"}), answer, error),
                  "the recording run answered: " + error);
            recorded.push_back(answer);
        }
    }   // flushed on destruction

    std::string error;
    auto taped = replay::TapedInput::open(path, error);
    CHECK(taped != nullptr, "the tape reads back: " + error);
    if (!taped) return;

    std::vector<std::string> replayed;
    std::string answer;
    for (int i = 0; i < 10; ++i) {
        CHECK(taped->answer(pick("chargen", {"1", "2", "3"}), answer, error),
              "the replay answered: " + error);
        replayed.push_back(answer);
    }
    CHECK(recorded == replayed,
          "every answer came back in the order it was given");
    std::remove(path.c_str());
}

// The seed is taped too, or the dice diverge and nothing else matters.
void the_seed_is_part_of_the_tape() {
    const std::string path = tape_path("seed");
    uint64_t recorded = 0;
    {
        replay::RandomInput source(11);
        replay::RunTape tape(source, path);
        recorded = tape.seed("chargen", 999);
    }
    std::string error;
    auto taped = replay::TapedInput::open(path, error);
    CHECK(taped != nullptr, "the tape reads back: " + error);
    if (!taped) return;
    CHECK(taped->seed("chargen", 999) == recorded,
          "the replayed seed is the recorded one, so the dice agree");
    std::remove(path.c_str());
}

// THE important one. A tape recorded against different rules must
// stop the run, not fall back to answering live.
void a_tape_that_no_longer_fits_aborts() {
    const std::string path = tape_path("mismatch");
    {
        replay::RandomInput source(5);
        replay::RunTape tape(source, path);
        std::string answer, error;
        tape.ask(pick("chargen", {"a", "b"}), answer, error);
    }
    std::string error;
    auto taped = replay::TapedInput::open(path, error);
    CHECK(taped != nullptr, "the tape reads back");
    if (!taped) return;

    // The rules changed: this site no longer offers what was recorded.
    std::string answer;
    const bool ok = taped->answer(pick("chargen", {"x", "y"}), answer,
                                  error);
    CHECK(!ok, "an answer the run no longer offers is refused");
    CHECK(error.find("does not offer") != std::string::npos,
          "and it says why: " + error);

    // A different decision point entirely.
    auto again = replay::TapedInput::open(path, error);
    const bool wrong_site = again->answer(pick("combat", {"a", "b"}),
                                          answer, error);
    CHECK(!wrong_site, "a tape asked at the wrong site is refused");
    CHECK(error.find("does not fit") != std::string::npos,
          "naming both sites: " + error);

    // And running past the end is divergence, not success.
    auto third = replay::TapedInput::open(path, error);
    third->answer(pick("chargen", {"a", "b"}), answer, error);
    const bool past_end = third->answer(pick("chargen", {"a", "b"}), answer,
                                        error);
    CHECK(!past_end && error.find("ran out") != std::string::npos,
          "a run that wants more than the tape holds is refused: " + error);
    std::remove(path.c_str());
}

// The tape refuses to record an answer the rules did not offer, so a
// bad live source cannot write a tape that only fails later.
void a_tape_will_not_record_an_impossible_answer() {
    const std::string path = tape_path("impossible");
    replay::LiveInput source(
        [](const replay::Ask&, std::string& out, std::string&) {
            out = "nonsense";
            return true;
        });
    replay::RunTape tape(source, path);
    std::string answer, error;
    const bool ok = tape.ask(pick("chargen", {"a", "b"}), answer, error);
    CHECK(!ok, "recording an answer that was never offered is refused");
    CHECK(error.find("not one of the answers") != std::string::npos,
          "and says so: " + error);
    std::remove(path.c_str());
}

// ------------------------------------------------------- the forks

// A tape records what ELSE was legal, so it carries its own branches.
void a_tape_remembers_the_roads_not_taken() {
    const std::string path = tape_path("forks");
    {
        replay::RandomInput source(7);
        replay::RunTape tape(source, path);
        std::string answer, error;
        tape.ask(pick("chargen", {"1", "2", "3"}), answer, error);
        tape.ask(pick("chargen", {"a", "b"}), answer, error);
    }
    std::string error;
    auto taped = replay::TapedInput::open(path, error);
    CHECK(taped != nullptr, "the tape reads back: " + error);
    if (!taped) return;
    CHECK(taped->records_alternatives(),
          "a tape written now knows it recorded the alternatives");
    const auto forks = taped->forks();
    CHECK(forks.size() == 2, "both decisions are forks");
    if (forks.size() == 2) {
        CHECK(forks[0].untaken.size() == 2,
              "the first offered three, so two were not taken");
        CHECK(forks[1].untaken.size() == 1,
              "the second offered two, so one was not taken");
        CHECK(std::find(forks[0].untaken.begin(), forks[0].untaken.end(),
                        forks[0].taken) == forks[0].untaken.end(),
              "the answer it gave is not among the ones it did not");
    }
    std::remove(path.c_str());
}

// The counterfactual: same trunk, one decision changed, and everything
// after it comes from somewhere else.
void a_fork_replays_the_trunk_then_diverges() {
    const std::string path = tape_path("branch");
    {
        replay::RandomInput source(11);
        replay::RunTape tape(source, path);
        std::string answer, error;
        for (int i = 0; i < 3; ++i) {
            tape.ask(pick("chargen", {"1", "2", "3"}), answer, error);
        }
    }
    std::string error;
    auto trunk = replay::TapedInput::open(path, error);
    CHECK(trunk != nullptr, "the trunk reads back: " + error);
    if (!trunk) return;

    const auto forks = trunk->forks();
    CHECK(!forks.empty(), "the trunk has somewhere to fork");
    if (forks.empty()) return;
    const auto& second = forks[1];
    const std::string instead = second.untaken.front();
    const std::string taken_before = forks[0].taken;

    replay::RandomInput after(99);
    auto branch = replay::ForkedInput::create(std::move(trunk), second.index,
                                              instead, after, error);
    CHECK(branch != nullptr, "the branch forks: " + error);
    if (!branch) return;

    std::string a1, a2, a3;
    CHECK(branch->answer(pick("chargen", {"1", "2", "3"}), a1, error),
          "the branch replays the trunk's first answer");
    CHECK(a1 == taken_before,
          "and it is the SAME answer: everything before the fork is the "
          "same life");
    CHECK(!branch->diverged(), "it has not diverged yet");
    CHECK(branch->answer(pick("chargen", {"1", "2", "3"}), a2, error),
          "the branch answers at the fork");
    CHECK(a2 == instead, "with the road not taken, not the one it took");
    CHECK(branch->diverged(), "and now it has diverged");
    CHECK(branch->answer(pick("chargen", {"1", "2", "3"}), a3, error),
          "and everything after comes from the other source");
    std::remove(path.c_str());
}

// The refusals. A fork that accepts anything is not a counterfactual.
void a_fork_refuses_what_was_never_offered() {
    const std::string path = tape_path("illegal");
    {
        replay::RandomInput source(3);
        replay::RunTape tape(source, path);
        std::string answer, error;
        tape.ask(pick("chargen", {"1", "2"}), answer, error);
    }
    std::string error;
    replay::RandomInput after(1);

    auto trunk = replay::TapedInput::open(path, error);
    auto invented = replay::ForkedInput::create(std::move(trunk), 0,
                                                "never-offered", after, error);
    CHECK(invented == nullptr, "a fork onto an answer nobody offered refuses");
    CHECK(error.find("not offered") != std::string::npos,
          "and says so: " + error);

    auto trunk2 = replay::TapedInput::open(path, error);
    const std::string same = trunk2->forks().front().taken;
    auto nochange = replay::ForkedInput::create(std::move(trunk2), 0, same,
                                                after, error);
    CHECK(nochange == nullptr,
          "forking onto the answer it already gave is not a fork");

    auto trunk3 = replay::TapedInput::open(path, error);
    auto past_end = replay::ForkedInput::create(std::move(trunk3), 99, "1",
                                                after, error);
    CHECK(past_end == nullptr, "forking past the end of the tape refuses");
    std::remove(path.c_str());
}

// An old tape has no alternatives recorded. It must say it cannot see
// them rather than report that there are none.
void an_old_tape_admits_it_cannot_see_the_branches() {
    const std::string path = tape_path("legacy");
    {
        std::ofstream out(path);
        out << "{\"kind\":\"seed\",\"site\":\"chargen\",\"answer\":\"7\"}\n";
        out << "{\"kind\":\"ask\",\"site\":\"chargen\",\"answer\":\"1\","
               "\"prompt\":\"pick\"}\n";
    }
    std::string error;
    auto taped = replay::TapedInput::open(path, error);
    CHECK(taped != nullptr, "an old tape still opens: " + error);
    if (!taped) return;
    CHECK(!taped->records_alternatives(),
          "and reports that it does not carry alternatives");
    CHECK(taped->forks().empty(), "so it offers no forks");

    replay::RandomInput after(1);
    auto branch = replay::ForkedInput::create(std::move(taped), 1, "2",
                                              after, error);
    CHECK(branch == nullptr, "and cannot be forked");
    CHECK(error.find("records no alternatives") != std::string::npos,
          "for the honest reason: " + error);
    std::remove(path.c_str());
}

}  // namespace

int main() {
    std::cout << "=== RunTape: recorded, replayed, or invented ===\n";
    the_same_seed_answers_the_same_way();
    a_random_answer_is_always_one_that_was_offered();
    a_tape_replays_what_was_recorded();
    the_seed_is_part_of_the_tape();
    a_tape_that_no_longer_fits_aborts();
    a_tape_will_not_record_an_impossible_answer();
    a_tape_remembers_the_roads_not_taken();
    a_fork_replays_the_trunk_then_diverges();
    a_fork_refuses_what_was_never_offered();
    an_old_tape_admits_it_cannot_see_the_branches();
    std::cout << "\n[measure] " << passed << " passed, " << failed
              << " failed\n";
    return failed == 0 ? 0 : 1;
}
