#include "deep_probe.h"

#include "engine.h"

#include <algorithm>
#include <cstdio>

namespace Logosphere {

DeepProbeManager::DeepProbeManager()  = default;
DeepProbeManager::~DeepProbeManager() = default;

int DeepProbeManager::register_probe(const std::string& name,
                                     Trigger trig, Dump dump) {
    Probe p;
    p.handle  = next_handle_++;
    p.name    = name;
    p.trigger = std::move(trig);
    p.dump    = std::move(dump);
    p.halt    = halt_on_trigger_;  // captured now; later mode changes don't retrofit
    probes_.push_back(std::move(p));
    std::printf("[DEEP_PROBE] registered '%s' (handle %d, %s)\n",
                probes_.back().name.c_str(), probes_.back().handle,
                probes_.back().halt ? "halts on fire" : "dump-only");
    return probes_.back().handle;
}

void DeepProbeManager::unregister_probe(int handle) {
    probes_.erase(
        std::remove_if(probes_.begin(), probes_.end(),
                       [handle](const Probe& p) { return p.handle == handle; }),
        probes_.end());
}

void DeepProbeManager::clear_fired(int handle) {
    for (auto& p : probes_) if (p.handle == handle) { p.fired = false; return; }
}

void DeepProbeManager::update(Engine& engine, int frame) {
    if (!enabled_ || probes_.empty()) return;
    for (auto& p : probes_) {
        if (p.fired)       continue;
        if (!p.trigger)    continue;
        if (!p.trigger(engine, frame)) continue;

        p.fired = true;
        std::printf("\n========== [DEEP_PROBE FIRED] %s @ frame %d ==========\n",
                    p.name.c_str(), frame);
        if (p.dump) p.dump(engine, frame);
        std::printf("==========================================================\n\n");
        if (p.halt) {
            halt_requested_ = true;
            std::printf("[DEEP_PROBE] '%s' is a halting probe — halt_requested_=true.\n",
                        p.name.c_str());
        }
    }
}

} // namespace Logosphere
