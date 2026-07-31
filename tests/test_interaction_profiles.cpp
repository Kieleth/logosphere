// =============================================================================
// PARTICLE INTERACTION — profile registry + should_contact (Phase 1 contract)
// =============================================================================
// Particles carry `interaction_profile_id` (the KG EntityID of a
// ParticleInteractionProfile; 0 = INVALID_ENTITY = engine default).
// The ParticleInteractionSystem resolves pairs with Box2D-style
// symmetric masking:
//
//   contact(a, b)  <=>  (cat_a & mask_b) && (cat_b & mask_a)
//
// Contracts:
//   c1  default world: profile 0 vs anything (including 0) contacts —
//       existing behavior is byte-identical until a game opts in.
//   c2  unknown nonzero profile ids resolve to the default (contact):
//       a stale id degrades to physics-as-usual, never to tunneling
//       both ways ("particles are bodies").
//   c3  the mask rule is the symmetric AND above, full uint32 domain
//       (bit 31, full masks) — this is where the KG's int32 slots are
//       widened into the engine's uint32 registry.
//   c4  KG round-trip: ParticleInteractionProfile entities load via
//       load_profiles_from_kg (kg_parse, diagnostic-not-crash on
//       malformed optionals) and drive should_contact; medium params
//       are readable back for the Phase-3 force pass.
//   c5  Particle carries the field, default 0.
//
// Tests must assert in every build type.
#undef NDEBUG

#include "logosphere/interaction/particle_interaction_system.h"
#include "generated/logosphere_ontology_registry.h"
#include "logosphere/kg/kg_module.h"
#include "particle.h"

#include <cassert>
#include <cstdio>

int main() {
    using logosphere::interaction::InteractionProfile;
    using logosphere::interaction::ParticleInteractionSystem;

    // --- c5: the particle field exists, defaults to 0 ---
    Particle p{};
    assert(p.interaction_profile_id == 0u);
    p.interaction_profile_id = 42u;
    assert(p.interaction_profile_id == 42u);
    std::printf("[PASS] c5 Particle.interaction_profile_id (default 0)\n");

    // --- c1: default world contacts everything ---
    ParticleInteractionSystem sys;
    assert(sys.should_contact(0u, 0u));
    assert(sys.should_contact(0u, 7u));   // 7 unknown -> default
    std::printf("[PASS] c1 default profiles always contact\n");

    // --- c3: symmetric AND rule over the full uint32 domain ---
    InteractionProfile ghost;              // passes through solids...
    ghost.id = 10u;
    ghost.category = 1u << 31;             // top bit: uint32 domain
    ghost.collides_with = 0u;              // ...collides with nothing
    sys.register_profile(ghost);

    InteractionProfile solid;
    solid.id = 11u;
    solid.category = 1u << 0;
    solid.collides_with = 0xFFFFFFFFu;     // collides with everything willing
    sys.register_profile(solid);

    assert(!sys.should_contact(10u, 11u) && "ghost declines: filtered");
    assert(!sys.should_contact(11u, 10u) && "order must not matter");
    assert(sys.should_contact(11u, 11u) && "solid vs solid contacts");
    assert(!sys.should_contact(10u, 10u) && "ghost vs ghost filtered");
    std::printf("[PASS] c3 symmetric mask rule, uint32 domain\n");

    // --- c2: unknown nonzero ids degrade to default (contact) ---
    assert(!sys.should_contact(10u, 999u) &&
           "ghost still declines an unknown-id partner (its mask is 0)");
    assert(sys.should_contact(11u, 999u) && "unknown id contacts a solid");
    assert(sys.should_contact(999u, 998u) && "two unknown ids contact");
    std::printf("[PASS] c2 unknown ids degrade to default\n");

    // --- c4: KG round-trip ---
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    auto water = kg.createEntity("ParticleInteractionProfile");
    kg.setProperty(water, "category_bit", "2");         // category = 1<<2
    kg.setProperty(water, "collides_with_mask", "0");   // rigid-contacts nothing
    kg.setProperty(water, "drag_coefficient", "0.8");
    kg.setProperty(water, "buoyancy_factor", "1.1");

    auto rock = kg.createEntity("ParticleInteractionProfile");
    kg.setProperty(rock, "category_bit", "0");          // category = 1<<0
    kg.setProperty(rock, "collides_with_mask", "4294967295");

    ParticleInteractionSystem kg_sys;
    size_t loaded = kg_sys.load_profiles_from_kg(kg);
    assert(loaded == 2 && "both KG profiles load");

    assert(!kg_sys.should_contact(water, rock) && "water filters rock");
    assert(kg_sys.should_contact(rock, rock) && "rock contacts rock");

    const InteractionProfile* w = kg_sys.find_profile(water);
    assert(w != nullptr);
    assert(w->drag_coefficient > 0.79f && w->drag_coefficient < 0.81f);
    assert(w->buoyancy_factor > 1.09f && w->buoyancy_factor < 1.11f);
    std::printf("[PASS] c4 KG round-trip (%zu profiles, medium params readable)\n",
                loaded);

    std::printf("[OK] interaction profiles + should_contact\n");
    return 0;
}
