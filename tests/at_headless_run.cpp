// HeadlessRun: the engine driven with no window and no clock.
//
// The claims worth testing are not "it loops". They are that the clock
// advances by exactly the dt supplied and by nothing else, that the
// same run twice produces the same clock, that a second run in one
// process does not inherit the first one's time, and that a run which
// hits its frame cap says so rather than reporting success.

#include "core/engine.h"
#include "core/game_time.h"
#include "logosphere/replay/headless_run.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

namespace replay = logosphere::replay;

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                        \
    do {                                                                 \
        if (condition) { ++passed; }                                     \
        else { ++failed; std::cout << "FAIL: " << (message) << "\n"; }   \
    } while (false)

// The engine with no game attached and no display: the smallest thing
// a harness has to be able to drive.
struct Headless {
    Engine engine;
    Headless() {
        EngineConfig config;
        config.create_display = false;
        config.show_debug_overlay = false;
        config.show_kg_inspector = false;
        if (engine.initialize(config) < 0) {
            throw std::runtime_error("engine did not initialize headless");
        }
    }
    ~Headless() { engine.shutdown(); }
};

// Frame count times dt, and nothing else. If anything read a real
// clock this drifts.
void the_clock_is_exactly_frames_times_dt() {
    Headless h;
    replay::RunSpec spec;
    spec.dt = 1.0 / 60.0;
    spec.max_frames = 120;
    replay::HeadlessRun run(h.engine, spec);

    const double before = GameTime::get_current_time();
    const uint64_t frames = run.run();
    const double elapsed = GameTime::get_current_time() - before;

    const double expected = 120.0 / 60.0;
    std::cout << "  [measure] " << frames << " frames, clock advanced "
              << elapsed << "s (expected " << expected << "s)\n";
    CHECK(frames == 120, "it ran exactly the frames asked for, got " +
                             std::to_string(frames));
    CHECK(std::fabs(elapsed - expected) < 1e-9,
          "the clock is frames x dt to the last bit, off by " +
              std::to_string(elapsed - expected));
    CHECK(run.exhausted(),
          "and it reports that it stopped on the cap, not by choice");
}

// A different dt gives a different clock, which is the control: if the
// engine were reading a wall clock, both runs would agree instead.
void a_different_dt_gives_a_different_clock() {
    const auto elapsed_for = [](double dt, uint64_t frames) {
        Headless h;
        replay::RunSpec spec;
        spec.dt = dt;
        spec.max_frames = frames;
        replay::HeadlessRun run(h.engine, spec);
        const double before = GameTime::get_current_time();
        run.run();
        return GameTime::get_current_time() - before;
    };
    const double fast = elapsed_for(1.0 / 60.0, 60);
    const double slow = elapsed_for(1.0 / 10.0, 60);
    std::cout << "  [measure] 60 frames at 1/60 = " << fast
              << "s, at 1/10 = " << slow << "s\n";
    CHECK(std::fabs(fast - 1.0) < 1e-9 && std::fabs(slow - 6.0) < 1e-9,
          "dt is what advances the clock, not elapsed real time");
}

// Game time is process-wide. Without the reset, a second run starts
// wherever the first one stopped.
void a_second_run_does_not_inherit_the_first_ones_clock() {
    {
        Headless first;
        replay::RunSpec spec;
        spec.max_frames = 60;
        replay::HeadlessRun run(first.engine, spec);
        run.run();
    }
    const double after_first = GameTime::get_current_time();
    CHECK(after_first > 0.0,
          "the first run did move the process clock (" +
              std::to_string(after_first) + "s)");

    Headless second;
    replay::RunSpec spec;
    spec.max_frames = 60;
    replay::HeadlessRun run(second.engine, spec);   // resets by default
    const double at_start = GameTime::get_current_time();
    CHECK(at_start == 0.0,
          "the second run starts from zero, not from " +
              std::to_string(after_first) + "s");

    // The control: ask it NOT to reset, and the inheritance is back.
    // Without this the assertion above could pass for a build where
    // the clock never advanced at all.
    run.run();
    const double carried = GameTime::get_current_time();
    Headless third;
    replay::RunSpec keep;
    keep.max_frames = 1;
    keep.reset_game_time = false;
    replay::HeadlessRun kept(third.engine, keep);
    CHECK(GameTime::get_current_time() == carried,
          "and with reset_game_time off it deliberately carries over");
}

// The callback owns when to stop, and stopping that way is not
// exhaustion.
void the_callback_can_stop_the_run() {
    Headless h;
    replay::RunSpec spec;
    spec.max_frames = 1000;
    replay::HeadlessRun run(h.engine, spec);
    const uint64_t frames = run.run([](uint64_t frame) {
        return frame < 7;          // stop once seven have run
    });
    CHECK(frames == 7, "it stopped when asked, at " +
                           std::to_string(frames) + " frames");
    CHECK(!run.exhausted(),
          "and that is finishing, not giving up on the cap");
}

}  // namespace

int main() {
    std::cout << "=== HeadlessRun: no window, no clock ===\n";
    try {
        the_clock_is_exactly_frames_times_dt();
        a_different_dt_gives_a_different_clock();
        a_second_run_does_not_inherit_the_first_ones_clock();
        the_callback_can_stop_the_run();
    } catch (const std::exception& error) {
        std::cout << "FAIL: " << error.what() << "\n";
        ++failed;
    }
    std::cout << "\n[measure] " << passed << " passed, " << failed
              << " failed\n";
    return failed == 0 ? 0 : 1;
}
