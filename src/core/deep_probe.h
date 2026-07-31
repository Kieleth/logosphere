#ifndef DEEP_PROBE_H
#define DEEP_PROBE_H

// =============================================================================
// Deep Probe System — custom debugging watchpoints for live engine state.
// =============================================================================
// A probe is (trigger, dump, optional halt). Games register probes that check
// a predicate each frame (or on specific event-bus events). When the predicate
// fires, the probe dumps whatever state it cares about and optionally halts
// the engine so you can inspect the exact frame.
//
// Why: bugs like the "spider Eva" dismemberment only surface in live play,
// not in our tests. We need a way to attach a tailor-made listener, wait for
// the exact condition, and dump everything at that instant without wading
// through thousands of frames of noise.
//
// Probes are pure opt-in. Zero overhead when none are registered (update()
// is a no-op). Games drop in a probe for the bug they're chasing, ship it,
// remove it when done.
//
// Typical shape from a game:
//
//   auto& mgr = engine.get_deep_probe_manager();
//   mgr.set_halt_on_trigger(true);   // arm halting for probes registered below
//   mgr.register_probe("Eva legs",
//       [&](Engine& e, int f) {
//           // predicate: true → halt and dump
//           auto view = e.get_particle_system().lock_particles_for_read();
//           float d = distance(view[thigh], view[hips]);
//           return d > baseline + 0.1f;
//       },
//       [&](Engine& e, int f) {
//           // dump: print whatever state matters
//           std::cout << "Eva legs separating at f" << f << "\n";
//           dump_body_state(e, eva_body_ids);
//       });
//
// =============================================================================

#include <functional>
#include <string>
#include <vector>

class Engine;

namespace Logosphere {

class DeepProbeManager {
public:
    // Predicate: return true to fire the probe.
    using Trigger = std::function<bool(Engine&, int frame)>;
    // Dump: invoked once when the probe fires. Do whatever logging you want.
    using Dump    = std::function<void(Engine&, int frame)>;

    DeepProbeManager();
    ~DeepProbeManager();

    // Register a probe. Returns a handle for unregister. The probe fires at
    // most once unless you clear_fired(handle) — keeps dumps compact.
    int register_probe(const std::string& name, Trigger trig, Dump dump);
    void unregister_probe(int handle);
    void clear_fired(int handle);

    // Called once per frame by the engine. Evaluates every live probe.
    // No-op when no probes registered (zero overhead).
    void update(Engine& engine, int frame);

    // Global toggle — turn the whole system on/off without unregistering.
    void set_enabled(bool on) { enabled_ = on; }
    bool is_enabled() const   { return enabled_; }

    // Arming mode for SUBSEQUENT registrations: a probe captures the
    // current setting at register_probe() time. A probe registered while
    // this is true halts the engine when it fires (games check
    // halt_requested() after update_game() and bail out of the main
    // loop); probes registered while it is false only dump and play
    // continues. Per-probe capture is the point: a benign telemetry
    // probe must never inherit halting from a watchdog registered later
    // (that exact bug froze Eden at frame 600).
    void set_halt_on_trigger(bool halt) { halt_on_trigger_ = halt; }
    bool halt_requested() const         { return halt_requested_; }

    size_t probe_count() const { return probes_.size(); }

private:
    struct Probe {
        int          handle;
        std::string  name;
        Trigger      trigger;
        Dump         dump;
        bool         fired = false;
        bool         halt  = false;  // captured from halt_on_trigger_ at registration
    };

    bool enabled_         = true;
    bool halt_on_trigger_ = false;
    bool halt_requested_  = false;
    int  next_handle_     = 1;
    std::vector<Probe> probes_;
};

} // namespace Logosphere

#endif // DEEP_PROBE_H
