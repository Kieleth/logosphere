// EventBus: Typed registry of EventChannels for all game event types.
//
// Central hub for the two-tier event system. Each WorldEventType gets
// its own EventChannel with independent signal subscribers and log readers.
//
// Usage:
//   EventBus bus;
//   bus.damage().subscribe([](const DamageEvent& e) { on_damaged(e); });
//   auto reader = bus.damage().create_reader();
//   bus.damage().emit(DamageEvent{...});
//   bus.advance_frame(game_time);  // stamp clock for journal entries

#pragma once

#include "logosphere/events/event_channel.h"
#include "generated/logosphere_ontology.h"
#include <cstddef>

namespace logosphere {

namespace onto = logosphere::ontology;

class EventBus {
    EventChannel<onto::CollisionEvent> collisions_;
    EventChannel<onto::DamageEvent> damage_;
    EventChannel<onto::SpawnEvent> spawns_;
    EventChannel<onto::DeathEvent> deaths_;
    EventChannel<onto::PerceptionEvent> perception_;
    EventChannel<onto::WorldEvent> state_changes_;  // STATE_CHANGE: property/relation mutations
    EventChannel<onto::RelationEvent> relations_;   // Topological changes (HAS_PART etc.)
    // Particle interaction model (the particle-interaction design notes)
    EventChannel<onto::ContactFilteredEvent> contact_filtered_;  // pair excluded from narrow phase
    EventChannel<onto::VolumeEvent> volume_;                     // medium entry/exit episodes
    EventChannel<onto::TransformationEvent> transformations_;    // TransformationRule fired
    EventChannel<onto::DiceRollEvent> dice_rolls_;               // engine-side rolls, citable facts

    uint64_t frame_ = 0;  // journal stamp clock, advanced once per engine frame

public:
    // Typed channel access
    EventChannel<onto::CollisionEvent>& collisions() { return collisions_; }
    EventChannel<onto::DamageEvent>& damage() { return damage_; }
    EventChannel<onto::SpawnEvent>& spawns() { return spawns_; }
    EventChannel<onto::DeathEvent>& deaths() { return deaths_; }
    EventChannel<onto::PerceptionEvent>& perception() { return perception_; }
    EventChannel<onto::WorldEvent>& state_changes() { return state_changes_; }
    EventChannel<onto::RelationEvent>& relations() { return relations_; }
    EventChannel<onto::ContactFilteredEvent>& contact_filtered() { return contact_filtered_; }
    EventChannel<onto::VolumeEvent>& volume() { return volume_; }
    EventChannel<onto::TransformationEvent>& transformations() { return transformations_; }
    EventChannel<onto::DiceRollEvent>& dice_rolls() { return dice_rolls_; }

    const EventChannel<onto::CollisionEvent>& collisions() const { return collisions_; }
    const EventChannel<onto::DamageEvent>& damage() const { return damage_; }
    const EventChannel<onto::SpawnEvent>& spawns() const { return spawns_; }
    const EventChannel<onto::DeathEvent>& deaths() const { return deaths_; }
    const EventChannel<onto::PerceptionEvent>& perception() const { return perception_; }
    const EventChannel<onto::WorldEvent>& state_changes() const { return state_changes_; }
    const EventChannel<onto::RelationEvent>& relations() const { return relations_; }
    const EventChannel<onto::ContactFilteredEvent>& contact_filtered() const { return contact_filtered_; }
    const EventChannel<onto::VolumeEvent>& volume() const { return volume_; }
    const EventChannel<onto::TransformationEvent>& transformations() const { return transformations_; }
    const EventChannel<onto::DiceRollEvent>& dice_rolls() const { return dice_rolls_; }

    // Advance the journal stamp clock on every channel. Call once per
    // frame with the scaled game clock; retention does not depend on
    // it (see event_log.h), it only timestamps subsequent emits.
    void advance_frame(double game_time = 0.0) {
        ++frame_;
        collisions_.advance_frame(frame_, game_time);
        damage_.advance_frame(frame_, game_time);
        spawns_.advance_frame(frame_, game_time);
        deaths_.advance_frame(frame_, game_time);
        perception_.advance_frame(frame_, game_time);
        state_changes_.advance_frame(frame_, game_time);
        relations_.advance_frame(frame_, game_time);
        contact_filtered_.advance_frame(frame_, game_time);
        volume_.advance_frame(frame_, game_time);
        transformations_.advance_frame(frame_, game_time);
        dice_rolls_.advance_frame(frame_, game_time);
    }

    uint64_t frame() const { return frame_; }

    // Diagnostics
    struct Stats {
        size_t total_events;
        size_t total_subscribers;
    };

    Stats get_stats() const {
        return {
            collisions_.event_count() + damage_.event_count() +
            spawns_.event_count() + deaths_.event_count() +
            perception_.event_count() + state_changes_.event_count() +
            relations_.event_count() + contact_filtered_.event_count() +
            volume_.event_count() + transformations_.event_count() +
            dice_rolls_.event_count(),

            collisions_.subscriber_count() + damage_.subscriber_count() +
            spawns_.subscriber_count() + deaths_.subscriber_count() +
            perception_.subscriber_count() + state_changes_.subscriber_count() +
            relations_.subscriber_count() + contact_filtered_.subscriber_count() +
            volume_.subscriber_count() + transformations_.subscriber_count() +
            dice_rolls_.subscriber_count()
        };
    }
};

} // namespace logosphere
