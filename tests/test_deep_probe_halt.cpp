// Regression: DeepProbeManager halt semantics are PER-PROBE, captured at
// registration time from the manager's arming mode.
//
// The bug this pins down: halt_on_trigger_ was a single manager-wide
// flag consulted at FIRE time, so a benign dump-only probe registered
// before set_halt_on_trigger(true) still halted the engine when it
// fired. Eden's "Sanity frame-600" telemetry probe froze the whole app
// at frame 600 that way, while its comment promised "does NOT set halt,
// so play continues."
//
// Contract locked here:
//   1. A probe registered while arming is OFF never requests a halt,
//      even if arming is turned on afterward.
//   2. A probe registered while arming is ON requests a halt when it
//      fires.
//
// Tests must assert in every build type (Release passes -DNDEBUG).
#undef NDEBUG

#include "core/engine.h"
#include "core/deep_probe.h"

#include <cassert>
#include <cstdio>

int main() {
    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    if (engine.initialize(cfg) != 0) {
        std::printf("[FAIL] engine init\n");
        return 1;
    }

    auto& mgr = engine.get_deep_probe_manager();

    // Registered while arming is OFF: dump-only, forever.
    bool benign_fired = false;
    mgr.register_probe(
        "benign telemetry",
        [](Engine&, int frame) { return frame == 2; },
        [&](Engine&, int) { benign_fired = true; });

    // Arm halting, THEN register the watchdog.
    mgr.set_halt_on_trigger(true);
    bool watchdog_fired = false;
    mgr.register_probe(
        "halting watchdog",
        [](Engine&, int frame) { return frame == 4; },
        [&](Engine&, int) { watchdog_fired = true; });

    // Frame 2: the benign probe fires. It was registered pre-arming, so
    // it must NOT halt, even though arming is on NOW.
    mgr.update(engine, 1);
    mgr.update(engine, 2);
    assert(benign_fired);
    assert(!mgr.halt_requested() && "dump-only probe must never halt");
    std::printf("[PASS] pre-arming probe fired without halting\n");

    // Frame 4: the watchdog fires and halts.
    mgr.update(engine, 3);
    mgr.update(engine, 4);
    assert(watchdog_fired);
    assert(mgr.halt_requested() && "armed probe must request halt on fire");
    std::printf("[PASS] armed probe halts on fire\n");

    engine.shutdown();
    std::printf("[OK] deep-probe halt semantics\n");
    return 0;
}
