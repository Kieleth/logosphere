// =============================================================================
// PARTICLE INTERACTION — EventBus channels (Phase 0 contract)
// =============================================================================
// The interaction model NOTIFIES through the two-tier bus and never
// mutates the KG itself (the particle-interaction design notes).
// This locks the three new channels into the same contract the existing
// seven honor: synchronous subscribers fire on emit, journal readers
// drain at their own pace, stats include the traffic.
//
// Channels under contract:
//   bus.contact_filtered()  — a profile pair was excluded from narrow phase
//   bus.volume()            — a particle entered/exited a declared medium
//   bus.transformations()   — a TransformationRule fired
//
// Tests must assert in every build type.
#undef NDEBUG

#include "logosphere/events/event_bus.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace logosphere;
    EventBus bus;

    // --- contact_filtered: sync tier ---
    int sync_hits = 0;
    int seen_a = 0, seen_b = 0;
    bus.contact_filtered().subscribe([&](const onto::ContactFilteredEvent& e) {
        ++sync_hits;
        seen_a = e.profile_a.value_or(0);
        seen_b = e.profile_b.value_or(0);
    });

    onto::ContactFilteredEvent cf;
    cf.profile_a = 3;
    cf.profile_b = 7;
    bus.contact_filtered().emit(cf);

    assert(sync_hits == 1 && "sync subscriber fires on emit");
    assert(seen_a == 3 && seen_b == 7 && "slots travel through the channel");
    std::printf("[PASS] contact_filtered sync tier\n");

    // --- volume: journal tier drains at the reader's pace ---
    auto reader = bus.volume().create_reader();
    onto::VolumeEvent ve;
    ve.entered = true;
    ve.medium_profile = 5;
    bus.volume().emit(ve);
    bus.advance_frame();

    auto items = reader.drain();
    assert(items.size() == 1 && "log reader sees exactly the emitted event");
    assert(items[0].entered.value_or(false) &&
           "entered flag preserved through the log");
    std::printf("[PASS] volume log tier\n");

    // --- transformations: emits count into stats ---
    auto before = bus.get_stats();
    onto::TransformationEvent te;
    te.rule_name = "fade_out";
    bus.transformations().emit(te);
    auto after = bus.get_stats();
    assert(after.total_events == before.total_events + 1 &&
           "transformation traffic shows in bus stats");
    std::printf("[PASS] transformations stats\n");

    std::printf("[OK] interaction event channels\n");
    return 0;
}
