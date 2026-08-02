// =============================================================================
// PARTICLE INTERACTION — ontology classes (Phase 0 contract)
// =============================================================================
// The interaction model is KG-declared: games author
// ParticleInteractionProfile and TransformationRule entities in their
// ontology/KG (the particle-interaction design notes). This
// locks the generated surface: the classes exist as typed C++ structs
// with the v1 slots, the generated registry knows them as concrete
// entity types, and KG entities of these types can be created.
//
// Tests must assert in every build type.
#undef NDEBUG

#include "generated/logosphere_ontology.h"
#include "generated/logosphere_ontology_registry.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <cassert>
#include <cstdio>

namespace onto = logosphere::ontology;

int main() {
    // --- generated structs carry the v1 slots (compile-time proof,
    //     runtime spot checks) ---
    // (KG slots are int32; the interaction system's C++ registry holds
    //  the full uint32 mask domain — that contract lands in Phase 1.)
    onto::ParticleInteractionProfile profile;
    profile.category_bit = 2;
    profile.collides_with_mask = 13;
    profile.drag_coefficient = 0.8f;
    profile.buoyancy_factor = 1.1f;
    profile.field_fx = 0.0f;
    profile.field_fy = 0.0f;
    profile.field_fz = 4.5f;
    assert(profile.category_bit.value() == 2);
    assert(profile.drag_coefficient.value() > 0.79f);
    std::printf("[PASS] ParticleInteractionProfile struct + slots\n");

    // An event rule answers three separable questions, and the slot
    // TYPES are the contract that they stay separable (#36):
    //   trigger    WHICH engine event source   closed enum
    //   condition  WHETHER this one matters    open string
    //   effect     WHAT to do                  open string
    // Two of the three are deliberately NOT enums. A closed range would
    // assert that the engine knows every legal value, which is false the
    // moment a game registers its own condition or effect.
    onto::TransformationRule rule;
    rule.trigger = onto::TransformationTrigger::ON_VOLUME_ENTER;
    rule.condition = "with_type:Prey";
    rule.effect = "swap_profile";
    rule.target_profile = 4;
    rule.duration_s = 2.0f;
    assert(rule.trigger.value() == onto::TransformationTrigger::ON_VOLUME_ENTER);
    assert(rule.condition.value() == "with_type:Prey");
    std::printf("[PASS] TransformationRule: trigger typed, condition/effect open\n");

    // A game-registered effect name the engine has never heard of must
    // be representable. This is the control for the claim above: if
    // someone re-types `effect` to TransformationEffect, this stops
    // compiling, which is the point.
    rule.effect = "bleed:0.4";
    assert(rule.effect.value() == "bleed:0.4");
    std::printf("[PASS] an unknown game effect is a legal slot value\n");

    // The trigger vocabulary round-trips through the generated helpers,
    // which is how the KG loader parses it.
    onto::TransformationTrigger parsed{};
    assert(onto::from_string("ON_CONTACT", parsed));
    assert(parsed == onto::TransformationTrigger::ON_CONTACT);
    assert(!onto::from_string("on_contact", parsed));   // loader normalizes case first
    assert(!onto::from_string("ON_NONSENSE", parsed));
    std::printf("[PASS] trigger vocabulary parses, and rejects what it should\n");

    // --- events derive from WorldEvent (bus compatibility) ---
    onto::ContactFilteredEvent cf;
    onto::WorldEvent* as_world = &cf;
    assert(as_world != nullptr);
    std::printf("[PASS] ContactFilteredEvent is a WorldEvent\n");

    // --- CollisionEvent carries the contact, not just the fact of it ---
    // A consequence needs geometry and energy: knockback needs the
    // normal, absorption needs the speed, blood needs the point, and
    // armour is per-part. Slots absent means #36 cannot be built on it.
    onto::CollisionEvent col;
    col.source_entity_id = "11";
    col.target_entity_id = "22";
    col.source_part_id   = "33";
    col.target_part_id   = "44";
    col.normal_x = 1.0f; col.normal_y = 0.0f; col.normal_z = 0.0f;
    col.contact_x = 2.5f; col.contact_y = -1.0f; col.contact_z = 0.75f;
    col.penetration = 0.02f;
    col.approach_speed = -3.5f;      // negative = approaching
    onto::WorldEvent* col_as_world = &col;
    assert(col_as_world != nullptr);
    assert(col.approach_speed.value() < 0.0f);
    assert(col.source_part_id.value() == "33");
    std::printf("[PASS] CollisionEvent carries geometry, energy and both parts\n");

    // --- the generated registry knows the classes as concrete types ---
    const auto& registry = logosphere::ontology::registry();
    assert(registry.hasEntityType("ParticleInteractionProfile"));
    assert(!registry.isAbstract("ParticleInteractionProfile"));
    assert(registry.hasEntityType("TransformationRule"));
    assert(!registry.isAbstract("TransformationRule"));
    std::printf("[PASS] registry knows the interaction classes\n");

    // --- KG entities of the new types can be created ---
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    auto pid = kg.createEntity("ParticleInteractionProfile");
    auto rid = kg.createEntity("TransformationRule");
    assert(pid != kg::INVALID_ENTITY);
    assert(rid != kg::INVALID_ENTITY);
    std::printf("[PASS] KG entities instantiate\n");

    std::printf("[OK] interaction ontology surface\n");
    return 0;
}
