// EventBus test suite
//
// Validates the typed channel registry using real ontology event types.
//
// Usage:
//   ./build/test_event_bus

#include "logosphere/events/event_bus.h"
#include <iostream>
#include <string>
#include <stdexcept>

namespace onto = logosphere::ontology;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " << #name << "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg) + " [" + #cond + "]")

// --- Tests ---

void test_damage_channel_signal() {
    logosphere::EventBus bus;
    int called = 0;
    float received_amount = 0;

    bus.damage().subscribe([&](const onto::DamageEvent& e) {
        called++;
        if (e.damage_amount) received_amount = *e.damage_amount;
    });

    onto::DamageEvent evt;
    evt.source_entity_id = "attacker_1";
    evt.target_entity_id = "victim_2";
    evt.damage_type = onto::DamageType::SLASH;
    evt.damage_amount = 25.0f;

    bus.damage().emit(evt);

    ASSERT(called == 1, "damage subscriber called");
    ASSERT(received_amount == 25.0f, "damage amount correct");
}

void test_damage_channel_reader() {
    logosphere::EventBus bus;
    auto reader = bus.damage().create_reader();

    onto::DamageEvent evt;
    evt.damage_type = onto::DamageType::BLUNT;
    evt.damage_amount = 10.0f;

    bus.damage().emit(evt);

    ASSERT(reader.has_unread(), "reader sees event");
    auto& e = reader.read();
    ASSERT(e.damage_amount && *e.damage_amount == 10.0f, "amount correct");
    ASSERT(e.damage_type && *e.damage_type == onto::DamageType::BLUNT, "type correct");
}

void test_collision_channel() {
    logosphere::EventBus bus;
    auto reader = bus.collisions().create_reader();

    onto::CollisionEvent evt;
    evt.source_entity_id = "entity_a";
    evt.target_entity_id = "entity_b";
    evt.collision_force = 150.0f;

    bus.collisions().emit(evt);

    auto items = reader.drain();
    ASSERT(items.size() == 1, "one collision event");
    ASSERT(items[0].collision_force && *items[0].collision_force == 150.0f, "force correct");
}

void test_death_channel() {
    logosphere::EventBus bus;
    bool death_signaled = false;

    bus.deaths().subscribe([&](const onto::DeathEvent&) {
        death_signaled = true;
    });

    onto::DeathEvent evt;
    evt.target_entity_id = "dead_entity";
    bus.deaths().emit(evt);

    ASSERT(death_signaled, "death signal fired");
}

void test_perception_channel() {
    logosphere::EventBus bus;
    auto reader = bus.perception().create_reader();

    onto::PerceptionEvent evt;
    evt.source_entity_id = "shambler_1";
    evt.target_entity_id = "player";
    evt.memory_layer = onto::MemoryLayer::ACTIVE_VISION;

    bus.perception().emit(evt);

    auto items = reader.drain();
    ASSERT(items.size() == 1, "one perception event");
    ASSERT(items[0].memory_layer && *items[0].memory_layer == onto::MemoryLayer::ACTIVE_VISION, "layer correct");
}

void test_channels_independent() {
    logosphere::EventBus bus;
    auto damage_reader = bus.damage().create_reader();
    auto collision_reader = bus.collisions().create_reader();

    onto::DamageEvent dmg;
    dmg.damage_amount = 5.0f;
    bus.damage().emit(dmg);

    ASSERT(damage_reader.has_unread(), "damage reader sees damage event");
    ASSERT(!collision_reader.has_unread(), "collision reader doesn't see damage event");
}

void test_advance_frame_all_channels() {
    logosphere::EventBus bus;

    onto::DamageEvent dmg;
    dmg.damage_amount = 1.0f;
    bus.damage().emit(dmg);

    onto::CollisionEvent col;
    col.collision_force = 2.0f;
    bus.collisions().emit(col);

    bus.advance_frame(1.0 / 60.0);
    bus.advance_frame(2.0 / 60.0);
    // Journal retention: events survive frames. New readers are
    // subscriptions (start at head); history is collect_since.

    auto dr = bus.damage().create_reader();
    auto cr = bus.collisions().create_reader();
    ASSERT(!dr.has_unread(), "reader starts at head");
    ASSERT(!cr.has_unread(), "reader starts at head");
    ASSERT(bus.damage().event_count() == 1, "event retained in journal");
    ASSERT(bus.damage().collect_since(0).size() == 1, "history queryable");
}

void test_stats() {
    logosphere::EventBus bus;

    auto stats0 = bus.get_stats();
    ASSERT(stats0.total_events == 0, "no events initially");
    ASSERT(stats0.total_subscribers == 0, "no subscribers initially");

    bus.damage().subscribe([](const onto::DamageEvent&) {});
    bus.deaths().subscribe([](const onto::DeathEvent&) {});

    onto::DamageEvent dmg;
    bus.damage().emit(dmg);
    bus.damage().emit(dmg);

    onto::CollisionEvent col;
    bus.collisions().emit(col);

    auto stats1 = bus.get_stats();
    ASSERT(stats1.total_events == 3, "3 total events");
    ASSERT(stats1.total_subscribers == 2, "2 total subscribers");
}

void test_spawn_channel() {
    logosphere::EventBus bus;
    auto reader = bus.spawns().create_reader();

    onto::SpawnEvent evt;
    evt.source_entity_id = "spawner";
    evt.target_entity_id = "new_entity";
    bus.spawns().emit(evt);

    ASSERT(reader.has_unread(), "spawn event logged");
    auto& e = reader.read();
    ASSERT(e.target_entity_id && *e.target_entity_id == "new_entity", "entity id correct");
}

// --- Main ---

int main() {
    std::cout << "=== EventBus Tests ===" << std::endl;

    TEST(damage_channel_signal);
    TEST(damage_channel_reader);
    TEST(collision_channel);
    TEST(death_channel);
    TEST(perception_channel);
    TEST(channels_independent);
    TEST(advance_frame_all_channels);
    TEST(stats);
    TEST(spawn_channel);

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
