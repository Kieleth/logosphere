// The capability store: rules evaluated for ANY tracked entity, the
// answer cached, and written back into the KG.
//
// The gap this guards against returning (#36 finding, #37 roadmap):
// capability response rules were humanoid-only in practice — the only
// recompute path lived in HumanoidLocomotion, so a wounded non-humanoid
// creature had rules, health, a damage system, and nothing that ever
// evaluated the rules. The store closes that. This file proves the
// closure and, above all, that the KG WRITE-BACK DOES NOT FEED ITSELF:
// derived state written into the same KG that triggers recomputes is
// one missing guard away from an infinite loop on the bus, so the loop
// guard is asserted by COUNT, not trusted.
//
// Headless-core safe: KG + bus + capability only, no Engine.
//
// Usage:
//   ./build/test_capability_store

#undef NDEBUG

#include "generated/logosphere_ontology_registry.h"
#include "logosphere/capability/capability_store.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"

#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

namespace {

using capability::CapabilityStore;

// A creature that is NOT a humanoid, with one leg part carrying health
// and a speed_cap rule — the exact shape that used to get zero rule
// evaluation.
struct Beast {
    kg::EntityID entity = 0;
    kg::EntityID leg = 0;
};

struct World {
    kg::KGModule kg{logosphere::ontology::registry()};
    logosphere::EventBus bus;
    CapabilityStore store;

    World() {
        kg.setMode(kg::KGMode::MINIMAL);
        kg.set_event_bus(&bus);      // property writes emit STATE_CHANGE
        store.initialize(&kg, &bus);
    }

    Beast beast() {
        Beast b;
        b.entity = kg.createEntity("Humanoid");   // concrete engine type;
                                                  // the store does not care
        b.leg = kg.createEntity("Leg");
        kg.createRelation(b.entity, "HAS_PART", b.leg);
        kg.setProperty(b.leg, "body_part_name", "leg");
        kg.setProperty(b.leg, "health", "100");
        kg.setProperty(b.leg, "max_health", "100");
        kg.setProperty(b.leg, "cap_list", "locomotion");
        kg.setProperty(b.leg, "rule.0.trigger", "health_below:60");
        kg.setProperty(b.leg, "rule.0.effect", "speed_cap:0.5");
        return b;
    }
};

// ----------------------------------------------------------- the basics

void test_tracking_computes_and_serves() {
    World w;
    Beast b = w.beast();
    w.store.track(b.entity);

    const auto* p = w.store.get(b.entity);
    std::cout << "  [measure] tracked: profile=" << (p != nullptr)
              << " speed_cap=" << (p ? p->speed_cap : -1.0f)
              << " recomputes=" << w.store.recompute_count(b.entity)
              << std::endl;
    check(p != nullptr, "a tracked entity has a profile");
    check(p && p->speed_cap == 1.0f, "healthy: no cap");
    check(w.store.recompute_count(b.entity) == 1,
          "track() computed exactly once");
}

void test_an_untracked_entity_is_nobody() {
    World w;
    Beast b = w.beast();   // rules and all, but never tracked
    check(w.store.get(b.entity) == nullptr,
          "an untracked entity has no profile (opt-in is opt-in)");
    // Damage it: nothing may happen. This is the control for every
    // recompute assertion below — if untracked entities recompute, the
    // eager path is just 'always', and opt-in is a lie.
    w.kg.setProperty(b.leg, "health", "10");
    check(w.store.recompute_count(b.entity) == 0,
          "and its parts' writes recompute nothing");
}

// ------------------------------------------------------- the wound path

void test_damage_flips_the_cap_through_the_bus() {
    World w;
    Beast b = w.beast();
    w.store.track(b.entity);

    // The wound: a part-health write, exactly what DamageSystem does.
    w.kg.setProperty(b.leg, "health", "40");   // below the rule's 60

    const auto* p = w.store.get(b.entity);
    std::cout << "  [measure] after wound: speed_cap="
              << (p ? p->speed_cap : -1.0f) << " recomputes="
              << w.store.recompute_count(b.entity) << std::endl;
    check(p && p->speed_cap == 0.5f,
          "the rule fired and the cap is live in the store (" +
          std::to_string(p ? p->speed_cap : -1.0f) + ")");

    // Heal it: rules are state predicates, the cap must lift.
    w.kg.setProperty(b.leg, "health", "95");
    p = w.store.get(b.entity);
    check(p && p->speed_cap == 1.0f, "healing lifts the cap again");
}

void test_a_part_of_a_part_still_reaches_the_root() {
    World w;
    Beast b = w.beast();
    // A toe on the leg: two reverse hops from the creature.
    auto toe = w.kg.createEntity("Foot");
    w.kg.createRelation(b.leg, "HAS_PART", toe);
    w.store.track(b.entity);
    const int before = w.store.recompute_count(b.entity);

    // ANY part write must recompute the creature; the key is
    // incidental, but it has to be a declared one to pass the write
    // gate — body_part_name is a plain String on every BodyPart.
    w.kg.setProperty(toe, "body_part_name", "changed");
    std::cout << "  [measure] toe write: recomputes " << before << " -> "
              << w.store.recompute_count(b.entity) << std::endl;
    check(w.store.recompute_count(b.entity) == before + 1,
          "a write two HAS_PART hops down still recomputes the creature");
}

// ------------------------------------------------------- the write-back

void test_capabilities_are_readable_from_the_kg() {
    World w;
    Beast b = w.beast();
    w.store.track(b.entity);
    w.kg.setProperty(b.leg, "health", "40");

    const std::string cap = w.kg.getProperty(b.entity, "capability.speed_cap");
    const std::string loco = w.kg.getProperty(b.entity, "capability.locomotion");
    std::cout << "  [measure] KG reads: capability.speed_cap='" << cap
              << "' capability.locomotion='" << loco << "'" << std::endl;
    check(cap.rfind("0.5", 0) == 0,
          "the cap is readable from the KG itself ('" + cap + "') — the "
          "medium a director or LLM reads");
    check(!loco.empty(), "so are the generic capability factors");
}

// THE loop-guard proof. The write-back emits STATE_CHANGE into the same
// bus that triggers recomputes; without both guards this test does not
// fail, it never returns. Asserted by count: N wounds = N + 1 computes.
void test_the_write_back_does_not_feed_itself() {
    World w;
    Beast b = w.beast();
    w.store.track(b.entity);

    for (int i = 0; i < 5; ++i)
        w.kg.setProperty(b.leg, "health", std::to_string(90 - i * 10));

    const int n = w.store.recompute_count(b.entity);
    std::cout << "  [measure] 5 wounds -> " << n
              << " recomputes (1 track + 5)" << std::endl;
    check(n == 6, "N wounds mean exactly N + 1 recomputes (" +
          std::to_string(n) + "), so the capability.* echo is swallowed");

    // The control that proves the count CAN move: one more wound.
    w.kg.setProperty(b.leg, "health", "5");
    check(w.store.recompute_count(b.entity) == 7,
          "and a real write still gets through the guards");
}

// A no-op write emits nothing (KGModule skips unchanged values), so it
// must not recompute either — pinned so eager stays 'on change', not
// 'on touch'.
void test_a_no_op_write_recomputes_nothing() {
    World w;
    Beast b = w.beast();
    w.store.track(b.entity);
    const int before = w.store.recompute_count(b.entity);
    w.kg.setProperty(b.leg, "health", "100");   // already 100
    check(w.store.recompute_count(b.entity) == before,
          "writing the value a property already has recomputes nothing");
}

void test_untrack_stops_the_world() {
    World w;
    Beast b = w.beast();
    w.store.track(b.entity);
    w.store.untrack(b.entity);
    w.kg.setProperty(b.leg, "health", "10");
    check(w.store.get(b.entity) == nullptr, "untracked: no profile");
    check(w.store.recompute_count(b.entity) == 0,
          "and no recomputes after untrack");
}

}  // namespace

int main() {
    std::cout << "Capability store (entity -> profile -> KG)" << std::endl;
    test_tracking_computes_and_serves();
    test_an_untracked_entity_is_nobody();
    test_damage_flips_the_cap_through_the_bus();
    test_a_part_of_a_part_still_reaches_the_root();
    test_capabilities_are_readable_from_the_kg();
    test_the_write_back_does_not_feed_itself();
    test_a_no_op_write_recomputes_nothing();
    test_untrack_stops_the_world();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
