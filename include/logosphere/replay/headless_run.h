#ifndef LOGOSPHERE_REPLAY_HEADLESS_RUN_H
#define LOGOSPHERE_REPLAY_HEADLESS_RUN_H

// HeadlessRun: drive the engine with no window and no clock.
//
// Almost a hundred files under tests/ and examples/ set
// EngineConfig::create_display = false and then hand-roll the same
// loop. This is that loop, written once, with the two properties a
// recorded or repeatable run needs:
//
//   FIXED STEP. Every tick advances by the dt you chose. Nothing here
//   reads a wall clock, and neither does the engine's time tree:
//   TimeSystem::tick derives game time, scaling and the physics
//   accumulator purely from the value passed in. Supply a constant and
//   the whole clock becomes a function of frame count.
//
//   NO RENDER. run() calls update() and nothing else. Never render(),
//   never present(), never poll_events(). That is not only for speed:
//   the engine drains deferred deletions on one side or the other
//   depending on whether anything has rendered yet, and staying
//   headless keeps that fork permanently on the update side.
//
// It is generic mechanism. It knows about frames and one callback, and
// nothing whatever about what a game does inside a frame. Pair it with
// RunRecorder to get a diffable trace of what happened.
//
// USAGE
//
//   MyGame game;
//   Engine engine(&game);
//   EngineConfig config;
//   config.create_display = false;
//   engine.initialize(config);
//
//   replay::HeadlessRun run(engine, {.dt = 1.0 / 60.0, .max_frames = 600});
//   const uint64_t frames = run.run([&](uint64_t) {
//       return !game.finished();      // false stops the run
//   });
//
// WHAT IT DOES NOT DO. It does not make a run deterministic on its
// own. Fixed dt and no rendering remove two sources of variation; a
// run that streams chunks asynchronously, generates rocks, or calls
// out to a model still varies for reasons this class cannot reach.
// See docs/OBSERVING_CHANGE.md for what is and is not repeatable.

#include <cstdint>
#include <functional>

class Engine;

namespace logosphere::replay {

struct RunSpec {
    // Constant, and never measured. 1/60 is a convention, not a
    // requirement: any fixed value gives a repeatable clock.
    double dt = 1.0 / 60.0;

    // 0 means "until the callback says stop". A non-zero cap is a
    // guard against a run that never finishes, and reaching it is
    // reported rather than treated as success.
    uint64_t max_frames = 0;

    // Game time is a process-wide singleton, so a second run in the
    // same process would otherwise inherit the first one's clock.
    bool reset_game_time = true;
};

class HeadlessRun {
public:
    explicit HeadlessRun(Engine& engine, RunSpec spec = {});

    HeadlessRun(const HeadlessRun&) = delete;
    HeadlessRun& operator=(const HeadlessRun&) = delete;

    // Ticks the engine, then calls on_frame(frame). Returning false
    // from on_frame stops the run. Returns the number of frames run.
    // An empty callback runs to max_frames.
    uint64_t run(const std::function<bool(uint64_t frame)>& on_frame = {});

    // True when the run stopped because it hit max_frames rather than
    // because the callback asked it to. A caller that cares about the
    // difference between "finished" and "gave up" reads this.
    bool exhausted() const { return exhausted_; }

    uint64_t frame() const { return frame_; }
    const RunSpec& spec() const { return spec_; }

    // Stop at the end of the current frame. Safe to call from inside
    // the callback.
    void stop() { stopped_ = true; }

private:
    Engine& engine_;
    RunSpec spec_;
    uint64_t frame_ = 0;
    bool stopped_ = false;
    bool exhausted_ = false;
};

}  // namespace logosphere::replay

#endif  // LOGOSPHERE_REPLAY_HEADLESS_RUN_H
