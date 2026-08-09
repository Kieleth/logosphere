// The contact producer: physics contacts become ontological events.
//
// Before issue #36 the ontological CollisionEvent was a class with no
// instances. It is declared in schema/logosphere.yaml, it has a channel
// on the bus, the journal would record it, and grep for `collisions().`
// across src/ include/ examples/ returned six hits, all six inside
// test_event_bus.cpp. Physics computed the geometry every frame and it
// died at the layer boundary.
//
// process_contacts is the producer. What it must get right:
//   - resolve a particle to its body part AND the creature that owns it
//   - report a touch ONCE per episode, not once per frame it persists
//   - carry the geometry, so a consequence has something to scale with
//   - cost nothing when nobody is listening
//
// That last one is not a nicety. Contacts are not rare: a tiled floor
// has every tile touching its neighbours, so an ungated producer would
// resolve thousands of pairs per frame to build events nobody reads.
// The gate is asserted here, not assumed, because a fail-fast nobody
// checks is decorative.
//
// Headless-safe: KG, interaction system and bus. No physics, no render:
// contacts are handed in as the POD the engine translates them into.
//
// Usage:
//   ./build/test_contact_producer

#undef NDEBUG

#include "generated/logosphere_ontology_registry.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

namespace {

using logosphere::EventBus;
using logosphere::interaction::ParticleInteractionSystem;
using logosphere::interaction::RigidContact;
namespace onto = logosphere::ontology;

// Two creatures, one body part each, each part backed by one particle.
// The minimum shape that can answer "who touched whom".
struct World {
    kg::KGModule kg{logosphere::ontology::registry()};
    EventBus bus;
    ParticleInteractionSystem sys;

    kg::EntityID predator = 0, prey = 0;
    kg::EntityID jaw = 0, flank = 0;
    uint32_t jaw_idx = 10, flank_idx = 20;

    World() {
        kg.setMode(kg::KGMode::MINIMAL);
        predator = kg.createEntity("Humanoid");
        prey     = kg.createEntity("Humanoid");
        jaw      = kg.createEntity("Head");
        flank    = kg.createEntity("Torso");
        kg.createRelation(predator, "HAS_PART", jaw);
        kg.createRelation(prey, "HAS_PART", flank);
        kg.createKGParticle(jaw, jaw_idx);
        kg.createKGParticle(flank, flank_idx);
    }

    RigidContact touch() const {
        RigidContact c;
        c.particle_a = jaw_idx;
        c.particle_b = flank_idx;
        c.normal_x = 1.0f;            // from A toward B
        c.contact_x = 3.0f; c.contact_y = 1.0f; c.contact_z = 0.5f;
        c.penetration = 0.04f;
        c.approach_speed = -2.5f;     // closing
        return c;
    }
};

// ------------------------------------------------------------- the gate

// The claim is "no listener means no work", and the only honest way to
// check it is to run a contact through with nobody listening and show
// that nothing came out. Without this the gate could be inverted and
// every other test here would still pass.
void test_silence_when_nobody_is_listening() {
    World w;
    check(!w.sys.wants_contacts(&w.bus),
          "a fresh system with no rules and no subscribers wants nothing");

    size_t emitted = w.sys.process_contacts({w.touch()}, w.kg, &w.bus);
    std::cout << "  [measure] contacts processed with no listener: emitted="
              << emitted << " channel_count=" << w.bus.collisions().event_count()
              << std::endl;
    check(emitted == 0, "nothing is emitted (" + std::to_string(emitted) + ")");
    check(w.bus.collisions().event_count() == 0,
          "and nothing reached the channel");
}

// The control for the gate: identical input, one subscriber, and now it
// must produce. If both cases came out the same the gate test above
// would be measuring nothing.
void test_a_subscriber_opens_the_gate() {
    World w;
    int seen = 0;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent&) { ++seen; });

    check(w.sys.wants_contacts(&w.bus), "a subscriber makes it want contacts");
    size_t emitted = w.sys.process_contacts({w.touch()}, w.kg, &w.bus);
    std::cout << "  [measure] same contact, one subscriber: emitted="
              << emitted << " delivered=" << seen << std::endl;
    check(emitted == 1, "the identical input now produces an event");
    check(seen == 1, "and the subscriber receives it");
}

// A rule is the other kind of listener, and it must open the gate with
// no subscriber present at all.
void test_a_contact_rule_opens_the_gate() {
    World w;
    auto rule = w.kg.createEntity("TransformationRule");
    w.kg.setProperty(rule, "trigger", "on_contact");
    w.kg.setProperty(rule, "effect", "emit_event");
    w.sys.load_rules_from_kg(w.kg);

    check(w.sys.has_contact_rules(), "the loaded rule is seen as a contact rule");
    check(w.sys.wants_contacts(nullptr),
          "which is enough on its own, with no bus at all");

    size_t emitted = w.sys.process_contacts({w.touch()}, w.kg, &w.bus);
    std::cout << "  [measure] rule present, no subscriber: emitted=" << emitted
              << std::endl;
    check(emitted == 1, "the producer runs for the rule's benefit");
}

// A rule that is not about contacts must NOT open the gate. Otherwise
// any game using fade_out pays the contact resolution cost forever.
void test_an_unrelated_rule_does_not_open_the_gate() {
    World w;
    auto rule = w.kg.createEntity("TransformationRule");
    w.kg.setProperty(rule, "trigger", "on_timer");
    w.kg.setProperty(rule, "effect", "fade_out");
    w.sys.load_rules_from_kg(w.kg);

    std::cout << "  [measure] on_timer rule loaded: has_contact_rules="
              << w.sys.has_contact_rules() << std::endl;
    check(!w.sys.has_contact_rules(), "an on_timer rule is not a contact rule");
    check(!w.sys.wants_contacts(&w.bus), "so the gate stays shut");
}

// ------------------------------------------------- what the event says

void test_the_event_names_both_parties_and_their_parts() {
    World w;
    onto::CollisionEvent got;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent& e) { got = e; });
    w.sys.process_contacts({w.touch()}, w.kg, &w.bus);

    std::cout << "  [measure] source entity=" << got.source_entity_id.value_or("-")
              << " part=" << got.source_part_id.value_or("-")
              << ", target entity=" << got.target_entity_id.value_or("-")
              << " part=" << got.target_part_id.value_or("-") << std::endl;

    check(got.source_entity_id.value_or("") == std::to_string(w.predator),
          "the striking side resolves to the predator, not to its jaw");
    check(got.source_part_id.value_or("") == std::to_string(w.jaw),
          "and the jaw is reported separately, because armour is per-part");
    check(got.target_entity_id.value_or("") == std::to_string(w.prey),
          "the struck side resolves to the prey");
    check(got.target_part_id.value_or("") == std::to_string(w.flank),
          "and names the flank");
}

void test_the_event_carries_the_contact() {
    World w;
    onto::CollisionEvent got;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent& e) { got = e; });
    w.sys.process_contacts({w.touch()}, w.kg, &w.bus);

    std::cout << "  [measure] normal=(" << got.normal_x.value_or(0) << ","
              << got.normal_y.value_or(0) << "," << got.normal_z.value_or(0)
              << ") point=(" << got.contact_x.value_or(0) << ","
              << got.contact_y.value_or(0) << "," << got.contact_z.value_or(0)
              << ") pen=" << got.penetration.value_or(0)
              << " approach=" << got.approach_speed.value_or(0) << std::endl;

    check(got.normal_x.value_or(0.0f) == 1.0f, "the normal survives the crossing");
    check(got.contact_z.value_or(0.0f) == 0.5f, "so does the contact point");
    check(got.penetration.value_or(0.0f) > 0.0f, "and the penetration depth");
    // The sign is the solver's and must not be reinterpreted on the way
    // out: a consequence keyed on "closing fast" reads this directly.
    check(got.approach_speed.value_or(0.0f) < 0.0f,
          "approach speed stays NEGATIVE while closing (" +
          std::to_string(got.approach_speed.value_or(0.0f)) + ")");
}

// -------------------------------------------------------- episode logic

// The failure this prevents: a humanoid standing still on a floor emits
// a contact event every frame forever, and any rule bound to it fires
// sixty times a second.
void test_a_persistent_touch_reports_once() {
    World w;
    int seen = 0;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent&) { ++seen; });

    for (int frame = 0; frame < 30; ++frame) {
        w.sys.process_contacts({w.touch()}, w.kg, &w.bus);
    }
    std::cout << "  [measure] same pair touching for 30 frames: " << seen
              << " event(s)" << std::endl;
    check(seen == 1, "one episode, one event (" + std::to_string(seen) + ")");
}

// The control for the episode test: if separation did not close the
// episode, "once per episode" would be indistinguishable from "once
// ever", and a second bite would go unreported.
void test_separating_and_touching_again_reports_again() {
    World w;
    int seen = 0;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent&) { ++seen; });

    for (int f = 0; f < 5; ++f) w.sys.process_contacts({w.touch()}, w.kg, &w.bus);
    const int after_first = seen;
    for (int f = 0; f < 5; ++f) w.sys.process_contacts({}, w.kg, &w.bus);   // apart
    for (int f = 0; f < 5; ++f) w.sys.process_contacts({w.touch()}, w.kg, &w.bus);

    std::cout << "  [measure] touch/apart/touch: " << after_first << " then "
              << seen << " total" << std::endl;
    check(after_first == 1, "the first episode reported once");
    check(seen == 2, "and the second touch is a new episode (" +
          std::to_string(seen) + ")");
}

// The solver records a pair once per substep. Those are one touch.
void test_substep_duplicates_collapse() {
    World w;
    int seen = 0;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent&) { ++seen; });

    std::vector<RigidContact> substeps{w.touch(), w.touch(), w.touch()};
    w.sys.process_contacts(substeps, w.kg, &w.bus);
    std::cout << "  [measure] 3 substep records of one pair: " << seen
              << " event(s)" << std::endl;
    check(seen == 1, "deduped to a single touch (" + std::to_string(seen) + ")");
}

// ----------------------------------------------------------- boundaries

// Floor tiles, debris and anything else spawned outside the KG have no
// entity to name. They must be skipped silently, and they are also the
// bulk of a real scene's contacts.
void test_particles_outside_the_ontology_are_skipped() {
    World w;
    int seen = 0;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent&) { ++seen; });

    RigidContact tile_on_tile;
    tile_on_tile.particle_a = 900;    // never registered with the KG
    tile_on_tile.particle_b = 901;
    w.sys.process_contacts({tile_on_tile}, w.kg, &w.bus);

    std::cout << "  [measure] contact between two non-KG particles: " << seen
              << " event(s)" << std::endl;
    check(seen == 0, "no entity to name, so no event (" +
          std::to_string(seen) + ")");
}

// Half-known is different from unknown: a creature stepping on scenery
// still deserves an event, with the side it can name filled in.
void test_a_half_resolved_contact_still_reports() {
    World w;
    onto::CollisionEvent got;
    int seen = 0;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent& e) {
        got = e; ++seen;
    });

    RigidContact c;
    c.particle_a = w.jaw_idx;
    c.particle_b = 900;               // scenery, outside the KG
    w.sys.process_contacts({c}, w.kg, &w.bus);

    std::cout << "  [measure] creature vs non-KG particle: " << seen
              << " event(s), source=" << got.source_entity_id.value_or("-")
              << " target=" << got.target_entity_id.value_or("-") << std::endl;
    check(seen == 1, "the touch is reported");
    check(got.source_entity_id.has_value(), "the known side is named");
    check(!got.target_entity_id.has_value(),
          "and the unknown side is left absent rather than invented");
}

void test_a_part_with_no_owner_is_its_own_entity() {
    World w;
    auto lone = w.kg.createEntity("Torso");    // no incoming HAS_PART
    w.kg.createKGParticle(lone, 55);

    onto::CollisionEvent got;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent& e) { got = e; });

    RigidContact c;
    c.particle_a = w.jaw_idx;
    c.particle_b = 55;
    w.sys.process_contacts({c}, w.kg, &w.bus);

    std::cout << "  [measure] unowned part: entity="
              << got.target_entity_id.value_or("-") << " part="
              << got.target_part_id.value_or("-")
              << " (both should be " << lone << ")" << std::endl;
    check(got.target_entity_id.value_or("") == std::to_string(lone),
          "an unowned part stands in as its own entity");
}

void test_no_contacts_is_not_an_error() {
    World w;
    w.bus.collisions().subscribe([](const onto::CollisionEvent&) {});
    size_t emitted = w.sys.process_contacts({}, w.kg, &w.bus);
    check(emitted == 0, "an empty frame emits nothing and does not crash");
}

// ---------------------------------------------------------- the cost

// The design claimed resolution is expensive enough to need a gate.
// That claim was an estimate, so measure it: what a frame of contacts
// actually costs, and what the gate actually saves.
//
// Not a performance gate. It prints numbers so the next person deciding
// whether to keep or drop the gate argues from data.
void measure_the_cost_of_resolution() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    EventBus bus;
    ParticleInteractionSystem sys;

    // A crowd: 200 parts on 200 owners, every part KG-backed. This is
    // the WORST case for the producer, because every contact resolves
    // fully. A real scene is mostly floor tiles, which bail early.
    constexpr int kParts = 200;
    std::vector<uint32_t> idx;
    for (int i = 0; i < kParts; ++i) {
        auto owner = kg.createEntity("Humanoid");
        auto part  = kg.createEntity("Torso");
        kg.createRelation(owner, "HAS_PART", part);
        auto render_idx = static_cast<uint32_t>(1000 + i);
        kg.createKGParticle(part, render_idx);
        idx.push_back(render_idx);
    }

    // One contact per adjacent pair, all first-touch so none is thrown
    // away by episode dedup.
    std::vector<RigidContact> contacts;
    for (int i = 0; i + 1 < kParts; ++i) {
        RigidContact c;
        c.particle_a = idx[i];
        c.particle_b = idx[i + 1];
        c.normal_z = 1.0f;
        c.approach_speed = -1.0f;
        contacts.push_back(c);
    }

    bus.collisions().subscribe([](const onto::CollisionEvent&) {});

    // Gate open: full resolution. Re-seed each round, since after the
    // first the episodes are open and the work would be skipped.
    constexpr int kRounds = 200;
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t total = 0;
    for (int r = 0; r < kRounds; ++r) {
        sys.process_contacts({}, kg, &bus);            // close episodes
        total += sys.process_contacts(contacts, kg, &bus);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    const double open_us =
        std::chrono::duration<double, std::micro>(t1 - t0).count() / kRounds;

    // Gate shut: identical input, nothing listening.
    ParticleInteractionSystem shut;
    EventBus quiet_bus;
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int r = 0; r < kRounds; ++r) {
        shut.process_contacts(contacts, kg, &quiet_bus);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    const double shut_us =
        std::chrono::duration<double, std::micro>(t3 - t2).count() / kRounds;

    std::cout << "  [measure] " << contacts.size()
              << " fully-resolvable contacts per frame:" << std::endl;
    std::cout << "  [measure]   gate open  " << open_us << " us/frame ("
              << (open_us * 1000.0 / static_cast<double>(contacts.size()))
              << " ns per contact)" << std::endl;
    std::cout << "  [measure]   gate shut  " << shut_us << " us/frame"
              << std::endl;
    std::cout << "  [measure]   events emitted across " << kRounds
              << " rounds: " << total << std::endl;

    check(total == kRounds * contacts.size(),
          "every round reported every pair, so the timing covers real work");
    check(shut_us < open_us,
          "the gate is cheaper than the work it skips (" +
          std::to_string(shut_us) + " vs " + std::to_string(open_us) + " us)");
}

}  // namespace

int main() {
    std::cout << "Contact producer (process_contacts)" << std::endl;
    test_silence_when_nobody_is_listening();
    test_a_subscriber_opens_the_gate();
    test_a_contact_rule_opens_the_gate();
    test_an_unrelated_rule_does_not_open_the_gate();
    test_the_event_names_both_parties_and_their_parts();
    test_the_event_carries_the_contact();
    test_a_persistent_touch_reports_once();
    test_separating_and_touching_again_reports_again();
    test_substep_duplicates_collapse();
    test_particles_outside_the_ontology_are_skipped();
    test_a_half_resolved_contact_still_reports();
    test_a_part_with_no_owner_is_its_own_entity();
    test_no_contacts_is_not_an_error();
    measure_the_cost_of_resolution();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
