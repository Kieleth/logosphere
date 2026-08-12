#include "logosphere/replay/headless_run.h"

#include "core/engine.h"
#include "core/game_time.h"

namespace logosphere::replay {

HeadlessRun::HeadlessRun(Engine& engine, RunSpec spec)
    : engine_(engine), spec_(spec) {
    if (spec_.reset_game_time) {
        // Process-wide singleton. Without this a second run in one
        // process starts at the first run's clock, which shows up as a
        // run that behaves differently depending on what ran before
        // it: the hardest kind of difference to chase.
        GameTime::reset();
    }
}

uint64_t HeadlessRun::run(const std::function<bool(uint64_t)>& on_frame) {
    stopped_ = false;
    exhausted_ = false;
    while (!stopped_) {
        if (spec_.max_frames != 0 && frame_ >= spec_.max_frames) {
            exhausted_ = true;
            break;
        }
        // update() and nothing else. render() would move the deferred
        // deletion drain to the other side of the frame.
        engine_.update(spec_.dt);
        ++frame_;
        if (on_frame && !on_frame(frame_)) break;
    }
    return frame_;
}

}  // namespace logosphere::replay
