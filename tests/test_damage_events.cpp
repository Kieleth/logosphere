// DamageSystem event emission test suite
//
// Validates that DamageSystem emits DamageEvent and DeathEvent
// through the EventBus when damage is applied and entities die.
//
// Usage:
//   ./build/test_damage_events

#include "logosphere/damage/damage_system.h"
#include "logosphere/events/event_bus.h"
#include <iostream>
#include <string>

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

void test_damage_emits_event() {
    logosphere::EventBus bus;
    DamageSystem damage(nullptr, &bus);

    damage.register_entity(1, 100.0f);
    auto reader = bus.damage().create_reader();

    damage.apply(1, 25.0f, DamageType::Slash);

    ASSERT(reader.has_unread(), "damage event emitted");
    auto& evt = reader.read();
    ASSERT(evt.damage_amount && *evt.damage_amount == 25.0f, "amount correct");
    ASSERT(evt.damage_type && *evt.damage_type == onto::DamageType::SLASH, "type mapped correctly");
    ASSERT(evt.target_entity_id && *evt.target_entity_id == "1", "target entity id set");
}

void test_death_emits_event() {
    logosphere::EventBus bus;
    DamageSystem damage(nullptr, &bus);

    damage.register_entity(2, 50.0f);
    auto death_reader = bus.deaths().create_reader();

    damage.apply(2, 60.0f, DamageType::Pierce);  // overkill

    ASSERT(death_reader.has_unread(), "death event emitted");
    auto& evt = death_reader.read();
    ASSERT(evt.target_entity_id && *evt.target_entity_id == "2", "dead entity id correct");
}

void test_damage_and_death_both_emitted() {
    logosphere::EventBus bus;
    DamageSystem damage(nullptr, &bus);

    damage.register_entity(3, 30.0f);
    auto damage_reader = bus.damage().create_reader();
    auto death_reader = bus.deaths().create_reader();

    damage.apply(3, 50.0f, DamageType::Blunt);

    // Should have both a damage event AND a death event
    ASSERT(damage_reader.has_unread(), "damage event present");
    ASSERT(death_reader.has_unread(), "death event present");
}

void test_no_bus_no_events() {
    // DamageSystem without bus should work fine (backward compat)
    DamageSystem damage(nullptr, nullptr);
    damage.register_entity(4, 100.0f);

    // Should not crash
    damage.apply(4, 10.0f, DamageType::Fire);
    ASSERT(damage.get_hp(4) == 90.0f, "damage still applied");
}

void test_callbacks_still_work_with_bus() {
    logosphere::EventBus bus;
    DamageSystem damage(nullptr, &bus);

    damage.register_entity(5, 100.0f);

    int callback_count = 0;
    damage.on_damage = [&](kg::EntityID, float, float, DamageType) {
        callback_count++;
    };

    auto reader = bus.damage().create_reader();
    damage.apply(5, 10.0f, DamageType::Cold);

    ASSERT(callback_count == 1, "callback still fires");
    ASSERT(reader.has_unread(), "event also in bus");
    // History stays queryable without a pre-existing reader:
    ASSERT(bus.damage().collect_since(0).size() == 1, "journal retains it");
}

void test_multiple_damage_events() {
    logosphere::EventBus bus;
    DamageSystem damage(nullptr, &bus);

    damage.register_entity(6, 100.0f);
    auto reader = bus.damage().create_reader();

    damage.apply(6, 10.0f, DamageType::Blunt);
    damage.apply(6, 15.0f, DamageType::Slash);
    damage.apply(6, 20.0f, DamageType::Pierce);

    auto events = reader.drain();
    ASSERT(events.size() == 3, "3 damage events");
    ASSERT(events[0].damage_amount && *events[0].damage_amount == 10.0f, "first damage");
    ASSERT(events[1].damage_amount && *events[1].damage_amount == 15.0f, "second damage");
    ASSERT(events[2].damage_amount && *events[2].damage_amount == 20.0f, "third damage");
}

void test_dead_entity_no_more_events() {
    logosphere::EventBus bus;
    DamageSystem damage(nullptr, &bus);

    damage.register_entity(7, 50.0f);
    auto reader = bus.damage().create_reader();

    damage.apply(7, 60.0f, DamageType::Pure);   // kills
    damage.apply(7, 10.0f, DamageType::Pure);   // dead, should not emit

    auto events = reader.drain();
    ASSERT(events.size() == 1, "only 1 damage event (dead entity ignored)");
}

void test_damage_type_mapping() {
    logosphere::EventBus bus;
    DamageSystem damage(nullptr, &bus);

    damage.register_entity(8, 1000.0f);  // lots of HP
    auto reader = bus.damage().create_reader();

    damage.apply(8, 1.0f, DamageType::Blunt);
    damage.apply(8, 1.0f, DamageType::Slash);
    damage.apply(8, 1.0f, DamageType::Pierce);
    damage.apply(8, 1.0f, DamageType::Bite);
    damage.apply(8, 1.0f, DamageType::Fire);
    damage.apply(8, 1.0f, DamageType::Cold);
    damage.apply(8, 1.0f, DamageType::Poison);
    damage.apply(8, 1.0f, DamageType::Pure);

    auto events = reader.drain();
    ASSERT(events.size() == 8, "8 damage events");
    ASSERT(*events[0].damage_type == onto::DamageType::BLUNT, "blunt mapped");
    ASSERT(*events[1].damage_type == onto::DamageType::SLASH, "slash mapped");
    ASSERT(*events[2].damage_type == onto::DamageType::PIERCE, "pierce mapped");
    ASSERT(*events[3].damage_type == onto::DamageType::BITE, "bite mapped");
    ASSERT(*events[4].damage_type == onto::DamageType::FIRE, "fire mapped");
    ASSERT(*events[5].damage_type == onto::DamageType::COLD, "cold mapped");
    ASSERT(*events[6].damage_type == onto::DamageType::POISON, "poison mapped");
    ASSERT(*events[7].damage_type == onto::DamageType::PURE, "pure mapped");
}

void test_set_event_bus_after_construction() {
    logosphere::EventBus bus;
    DamageSystem damage;  // no bus

    damage.register_entity(9, 100.0f);
    damage.apply(9, 5.0f, DamageType::Blunt);  // no bus, no event

    damage.set_event_bus(&bus);
    auto reader = bus.damage().create_reader();

    damage.apply(9, 10.0f, DamageType::Slash);

    ASSERT(reader.has_unread(), "events after setting bus");
    auto& evt = reader.read();
    ASSERT(evt.damage_amount && *evt.damage_amount == 10.0f, "correct amount after bus set");
}

// --- Main ---

int main() {
    std::cout << "=== DamageSystem Event Emission Tests ===" << std::endl;

    TEST(damage_emits_event);
    TEST(death_emits_event);
    TEST(damage_and_death_both_emitted);
    TEST(no_bus_no_events);
    TEST(callbacks_still_work_with_bus);
    TEST(multiple_damage_events);
    TEST(dead_entity_no_more_events);
    TEST(damage_type_mapping);
    TEST(set_event_bus_after_construction);

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
