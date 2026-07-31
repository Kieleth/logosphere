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

    onto::TransformationRule rule;
    rule.trigger = "on_volume_enter";
    rule.effect = "swap_profile";
    rule.target_profile = 4;
    rule.duration_s = 2.0f;
    assert(rule.trigger.value() == "on_volume_enter");
    std::printf("[PASS] TransformationRule struct + slots\n");

    // --- events derive from WorldEvent (bus compatibility) ---
    onto::ContactFilteredEvent cf;
    onto::WorldEvent* as_world = &cf;
    assert(as_world != nullptr);
    std::printf("[PASS] ContactFilteredEvent is a WorldEvent\n");

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
