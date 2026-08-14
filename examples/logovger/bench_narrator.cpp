// How long does a player wait for a story?
//
// The narrator holds the next decision until the beat has been told,
// so this latency is felt directly: it is the pause between clicking
// and being allowed to click again. That makes it a gameplay number,
// not an infrastructure one, and worth measuring rather than guessing.
//
// Headless and honest:
//
//   * every beat is UNIQUE, so nothing is served from the cache. A
//     benchmark that measures cache hits measures a hash table.
//   * the cache is written to a scratch directory and the real one is
//     left alone, so running this does not poison a play session.
//   * it reports the spread, not just a mean. One slow beat in ten is
//     what a player notices.
//
// Compare two backends by running it twice:
//
//   ./build/logovger-bench-narrator
//   LOGOVGER_LLM=mlx LOGOVGER_LLM_MODEL=qwen2.5-7b-instruct \
//       ./build/logovger-bench-narrator
//
// Usage: logovger-bench-narrator [beats]

#include "src/narrator.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

size_t words(const std::string& text) {
    size_t count = 0;
    bool inside = false;
    for (char c : text) {
        const bool space = (c == ' ' || c == '\n' || c == '\t');
        if (!space && !inside) ++count;
        inside = !space;
    }
    return count;
}

// A term the way the game builds one: throws already resolved, the
// sheet as it stands. The index makes every beat different so the
// cache never answers.
logovger::Beat term_beat(int index) {
    logovger::Beat beat;
    beat.kind = "term";
    beat.term = 1 + (index % 6);
    beat.facts = {
        "survived: 2D6 = " + std::to_string(4 + (index % 8)) +
            " +1 DM -> " + std::to_string(5 + (index % 8)) + " vs 5+",
        "gained Gun Combat-" + std::to_string(1 + (index % 3)) +
            ": Mercenary Service Skills",
        "term ends: age " + std::to_string(22 + 4 * (index % 5)),
    };
    beat.roll_ids = {static_cast<uint64_t>(100 + index)};
    beat.sheet =
        "Str 9 (DM +1), Dex 7 (DM +0), End 8 (DM +0), Int 6 (DM +0), "
        "Edu 5 (DM -1), Soc " + std::to_string(4 + (index % 6)) +
        " (DM -1)\nUPP 978654, age 26, 2 term(s), currently Mercenary"
        "\nSkills: Gun Combat-1 Melee Combat-0 Recon-1";
    return beat;
}

}  // namespace

int main(int argc, char** argv) {
    const int beats = argc > 1 ? std::atoi(argv[1]) : 8;
    if (beats <= 0) {
        std::printf("nothing to measure\n");
        return 2;
    }

    // Never touch the play cache: this writes hundreds of stories no
    // life will ever ask for again. Saying so in a comment is not the
    // same as doing it, which is how the first run of this benchmark
    // put fifty-two files into the cache a session reads from.
    const std::string scratch = "/tmp/logovger-bench-cache";
    ::setenv("LOGOVGER_NARRATION_CACHE", scratch.c_str(), 1);
    if (std::system(("rm -rf " + scratch).c_str()) != 0) {
        /* nothing there on a first run, which is fine */
    }

    logovger::Narrator narrator;
    std::string error;
    const std::string lore =
        std::string(LOGOSPHERE_SOURCE_DIR) +
        "/examples/logovger/lore/voyager.md";
    if (!narrator.initialize(lore, error)) {
        std::printf("cannot measure: %s\n", error.c_str());
        return 2;
    }
    std::printf("\n=== narrator latency: %s / %s ===\n\n",
                narrator.backend().c_str(), narrator.model().c_str());

    std::vector<double> timings;
    std::vector<size_t> lengths;
    int failures = 0;

    for (int i = 0; i < beats; ++i) {
        const auto beat = term_beat(i);
        bool done = false;
        std::string prose;
        const auto started = Clock::now();
        // A unique seed per beat, so nothing is cached and every
        // measurement is a real round trip.
        const uint64_t seed = 900000000ull + static_cast<uint64_t>(i);
        if (!narrator.narrate(beat, seed,
                              [&](const std::string& reply) {
                                  prose = reply;
                                  done = true;
                              })) {
            std::printf("  narrator not ready\n");
            return 2;
        }
        while (!done) {
            narrator.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (Ms(Clock::now() - started).count() > 120000) break;
        }
        const double elapsed = Ms(Clock::now() - started).count();
        if (prose.empty()) {
            ++failures;
            std::printf("  beat %-2d  FAILED after %.0f ms\n", i, elapsed);
            continue;
        }
        timings.push_back(elapsed);
        lengths.push_back(words(prose));
        std::printf("  beat %-2d  %7.0f ms   %2zu words\n", i, elapsed,
                    words(prose));
    }

    if (timings.empty()) {
        std::printf("\nno beat completed. %d failure(s)\n\n", failures);
        return 1;
    }
    std::sort(timings.begin(), timings.end());
    const double median = timings[timings.size() / 2];
    double total = 0;
    for (double t : timings) total += t;
    size_t word_total = 0;
    for (size_t w : lengths) word_total += w;

    std::printf("\n  beats      %zu (%d failed)\n", timings.size(), failures);
    std::printf("  fastest    %.0f ms\n", timings.front());
    std::printf("  median     %.0f ms\n", median);
    std::printf("  slowest    %.0f ms\n", timings.back());
    std::printf("  mean       %.0f ms\n", total / timings.size());
    std::printf("  words      %.1f average\n",
                static_cast<double>(word_total) / lengths.size());
    std::printf("\n  A player waits this long between a decision and\n"
                "  being allowed the next one.\n\n");
    return 0;
}
