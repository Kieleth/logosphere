// Predator and prey collide, and bounce. The whole chain, end to end.
//
// This is the slice issue #36 exists for. Everything up to the touch was
// already working and guarded (GOAP, pathfinding, senses, contact
// reporting on both channels); what stopped was the consequence. A
// predator that reached its prey had no engine-supported way to DO
// anything.
//
// The chain under test, each link built in an earlier phase:
//
//   contact          physics resolves it (already existed)
//   -> CollisionEvent  the producer, phase 3
//   -> condition       with_type:Predator, evaluated per side
//   -> effect          knockback:<speed>
//   -> impulse inbox   deposited against a STABLE particle id
//   -> the mover       drains it and decides what a push means
//
// WHY THE INBOX. Both creatures are KINEMATIC, so inv_mass is 0 and the
// solver will never push them: their positions belong to an external
// writer. Writing velocity here would be overwritten by that writer next
// frame; writing position would break solver authority outright. So the
// push is delivered to whoever owns the position. In this test the owner
// is `Creature::step`, ten lines below; in a real game it is the
// locomotion system, which is a harder problem and deliberately not in
// this slice.
//
// WHAT A GREEN RUN ENTITLES YOU TO BELIEVE: a contact between two
// KG-backed creatures can select a rule by the OTHER party's ontology
// type and deposit a directed impulse that the position owner can act
// on. It does NOT prove anything about humanoid locomotion absorbing a
// push, which is untouched here.
//
// Headless-safe: KG, interaction system, bus. Contacts are handed in as
// the POD the engine translates physics into, so no solver runs.
//
// Usage:
//   ./build/test_contact_bounce

#undef NDEBUG

#include "generated/logosphere_ontology_registry.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/interaction/contact_response.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <cmath>
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
using logosphere::interaction::ContactConditionRegistry;
using logosphere::interaction::ContactEffectContext;
using logosphere::interaction::ContactEffectRegistry;
using logosphere::interaction::ContactContext;
using logosphere::interaction::ParticleInteractionSystem;
using logosphere::interaction::RigidContact;
namespace onto = logosphere::ontology;

constexpr float kRadius = 0.5f;   // both creatures, so they touch at 1.0 m

// One creature: a KG entity, one body part, one KG-backed particle, and
// a position this test owns. Being the position owner is the point:
// KINEMATIC means somebody outside physics moves you, and that somebody
// is who the impulse is delivered to.
struct Creature {
    kg::EntityID entity = 0;
    kg::EntityID part = 0;
    kg::KGParticleID particle = 0;
    uint32_t render_idx = 0;

    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;

    // Drain whatever was deposited and apply it. A game could damp it,
    // ignore it while braced, or scale it by mass; that decision living
    // here rather than in the engine is the boundary working.
    void step(ParticleInteractionSystem& sys, float dt) {
        float ix = 0.0f, iy = 0.0f, iz = 0.0f;
        if (sys.take_impulse(particle, ix, iy, iz)) {
            vx += ix;
            vy += iy;
        }
        x += vx * dt;
        y += vy * dt;
    }
};

// Predator and Prey are GAME vocabulary, so they are not in the engine
// ontology and should not be: `Creature` is abstract there precisely
// because what creatures exist is a game's business. The test declares
// them the way a game does, through OntologyRegistry::extend.
//
// That makes this a stronger check than using two built-in types would
// be. The claim is that a contact rule matches on a KG type; proving it
// with types the engine has never heard of proves the mechanism is
// generic rather than accidentally tied to the shipped vocabulary.
kg::OntologyRegistry game_types() {
    kg::OntologyRegistry reg;
    reg.addEntityType("Predator", "LivingEntity", /*is_abstract=*/false);
    reg.addEntityType("Prey", "LivingEntity", false);
    reg.addAncestors("Predator", {"LivingEntity", "WorldEntity", "Entity"});
    reg.addAncestors("Prey", {"LivingEntity", "WorldEntity", "Entity"});
    return reg;
}

struct World {
    kg::KGModule kg{logosphere::ontology::registry()};
    EventBus bus;
    ParticleInteractionSystem sys;
    Creature predator, prey;

    // `prey_type` is a parameter so the negative case can build an
    // otherwise identical world whose types do not match the rule.
    World(const char* predator_type = "Predator",
          const char* prey_type = "Prey") {
        kg.setMode(kg::KGMode::MINIMAL);
        kg.extendOntology(game_types());
        predator = make(predator_type, "Head", 100);
        prey     = make(prey_type, "Torso", 200);
    }

    Creature make(const char* type, const char* part_type, uint32_t idx) {
        Creature c;
        c.entity = kg.createEntity(type);
        c.part = kg.createEntity(part_type);
        kg.createRelation(c.entity, "HAS_PART", c.part);
        c.render_idx = idx;
        c.particle = kg.createKGParticle(c.part, idx);
        return c;
    }

    void add_rule(const char* condition, const char* effect) {
        auto r = kg.createEntity("TransformationRule");
        kg.setProperty(r, "trigger", "on_contact");
        kg.setProperty(r, "condition", condition);
        kg.setProperty(r, "effect", effect);
        sys.load_rules_from_kg(kg);
    }

    // The contact physics would report if the two overlapped, built from
    // their actual positions so the normal is real geometry rather than
    // a constant. Empty when they are apart, which is what closes the
    // episode and lets a second touch be reported.
    std::vector<RigidContact> contacts_now() const {
        const float dx = prey.x - predator.x;
        const float dy = prey.y - predator.y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d >= 2.0f * kRadius || d < 1e-6f) return {};

        RigidContact c;
        c.particle_a = predator.render_idx;
        c.particle_b = prey.render_idx;
        c.normal_x = dx / d;              // from A toward B
        c.normal_y = dy / d;
        c.contact_x = predator.x + c.normal_x * kRadius;
        c.contact_y = predator.y + c.normal_y * kRadius;
        c.penetration = 2.0f * kRadius - d;
        // Closing speed along the normal, solver sign: negative closing.
        c.approach_speed =
            -( (predator.vx - prey.vx) * c.normal_x +
               (predator.vy - prey.vy) * c.normal_y );
        return c.penetration > 0.0f ? std::vector<RigidContact>{c}
                                    : std::vector<RigidContact>{};
    }

    float separation() const {
        const float dx = prey.x - predator.x;
        const float dy = prey.y - predator.y;
        return std::sqrt(dx * dx + dy * dy);
    }
};

// Run the encounter and report what happened. The predator walks at the
// prey; the prey stands still.
struct Outcome {
    float closest = 1e9f;      // nearest they ever got
    float final_sep = 0.0f;
    float prey_travel = 0.0f;
    bool  touched = false;
    int   events = 0;
};

Outcome run(World& w, int frames = 240, float dt = 1.0f / 60.0f) {
    Outcome o;
    w.bus.collisions().subscribe([&](const onto::CollisionEvent&) { ++o.events; });

    w.predator.x = -3.0f;
    w.predator.vx = 2.0f;          // closing at 2 m/s
    w.prey.x = 0.0f;
    const float prey_start_x = w.prey.x;

    for (int f = 0; f < frames; ++f) {
        auto contacts = w.contacts_now();
        if (!contacts.empty()) o.touched = true;
        w.sys.process_contacts(contacts, w.kg, &w.bus);
        w.predator.step(w.sys, dt);
        w.prey.step(w.sys, dt);
        o.closest = std::min(o.closest, w.separation());
    }
    o.final_sep = w.separation();
    o.prey_travel = std::abs(w.prey.x - prey_start_x);
    return o;
}

// ------------------------------------------------------- the slice

void test_predator_and_prey_collide_and_bounce() {
    World w;
    // "When a Predator touches me, throw me back." Conditions ask about
    // the OTHER party, effects act on SELF, so this is the prey's rule
    // and the prey is what moves. `with_type:Prey` would be the
    // predator's rule and would bounce the PREDATOR instead; that
    // reading is exercised below so neither can rot.
    w.add_rule("with_type:Predator", "knockback:3.0");
    Outcome o = run(w);

    std::cout << "  [measure] predator closes on prey:" << std::endl;
    std::cout << "  [measure]   touched=" << o.touched
              << " collision events=" << o.events << std::endl;
    std::cout << "  [measure]   closest approach " << o.closest
              << " m, final separation " << o.final_sep << " m" << std::endl;
    std::cout << "  [measure]   prey pushed " << o.prey_travel << " m"
              << std::endl;

    check(o.touched, "they actually made contact");
    check(o.events >= 1, "the contact reached the bus (" +
          std::to_string(o.events) + " event(s))");
    // The claim in the name. The prey started at rest and was moved only
    // by an impulse that arrived through the rule.
    check(o.prey_travel > 0.5f,
          "the prey was knocked back (" + std::to_string(o.prey_travel) +
          " m from a standing start)");
    check(o.final_sep > o.closest,
          "and they end further apart than their closest approach (" +
          std::to_string(o.final_sep) + " vs " + std::to_string(o.closest) + ")");
}

// The mirror reading, pinned so the semantics cannot drift silently.
// Same encounter, condition naming the other type: now the STRIKER
// bounces off and the struck one never moves.
void test_the_mirror_rule_bounces_the_striker_instead() {
    World w;
    w.add_rule("with_type:Prey", "knockback:3.0");
    Outcome o = run(w);

    std::cout << "  [measure] with_type:Prey instead -> prey pushed "
              << o.prey_travel << " m, final separation " << o.final_sep
              << " m" << std::endl;
    check(o.prey_travel == 0.0f,
          "the prey does not move: it has no rule of its own (" +
          std::to_string(o.prey_travel) + " m)");
    check(o.final_sep > o.closest,
          "but they still separate, because the PREDATOR bounced off");
}

// THE CONTROL the design owes. Identical geometry, identical closing
// speed, identical rule text — only the striker's ontology type differs,
// so with_type:Predator matches nothing.
//
// Without this, "the prey moved" is unfalsifiable: any push from any
// source would read as a pass.
void test_a_type_the_rule_does_not_name_does_not_bounce() {
    World w("Prey", "Prey");   // the striker is NOT a Predator
    w.add_rule("with_type:Predator", "knockback:3.0");
    Outcome o = run(w);

    std::cout << "  [measure] same encounter, striker is not a Predator:"
              << std::endl;
    std::cout << "  [measure]   touched=" << o.touched
              << " prey pushed " << o.prey_travel << " m (must be 0)"
              << std::endl;

    check(o.touched, "they still make contact, so the difference is the RULE");
    check(o.events >= 1, "and the contact is still reported");
    check(o.prey_travel == 0.0f,
          "but nothing pushes the prey (" + std::to_string(o.prey_travel) +
          " m)");
}

// The other half of the same control: right type, but the rule's
// condition names a threshold the encounter never reaches.
void test_a_gentle_touch_below_the_threshold_does_not_bounce() {
    World w;
    w.add_rule("impact_above:50.0", "knockback:3.0");   // 2 m/s closing
    Outcome o = run(w);

    std::cout << "  [measure] closing at 2 m/s against impact_above:50 -> prey pushed "
              << o.prey_travel << " m (must be 0)" << std::endl;
    check(o.touched, "contact happens");
    check(o.prey_travel == 0.0f, "the threshold is respected");
}

void test_the_same_threshold_met_does_bounce() {
    World w;
    w.add_rule("impact_above:1.0", "knockback:3.0");    // 2 m/s clears it
    Outcome o = run(w);

    std::cout << "  [measure] closing at 2 m/s against impact_above:1 -> prey pushed "
              << o.prey_travel << " m" << std::endl;
    check(o.prey_travel > 0.5f,
          "the identical encounter DOES bounce once the threshold is met (" +
          std::to_string(o.prey_travel) + " m)");
}

// -------------------------------------------------- direction and sign

// The normal is re-oriented per side so +normal always means "away from
// the other party". If that were wrong the prey would be pulled INTO the
// predator, which a distance-only assertion could still call a pass on
// some frames.
void test_the_push_goes_away_from_the_other_party() {
    World w;
    w.add_rule("with_type:Predator", "knockback:3.0");

    // Approach from the opposite side: predator to the RIGHT of the prey,
    // moving left. The push must reverse with it.
    w.bus.collisions().subscribe([](const onto::CollisionEvent&) {});
    w.predator.x = 3.0f;
    w.predator.vx = -2.0f;
    w.prey.x = 0.0f;

    for (int f = 0; f < 240; ++f) {
        w.sys.process_contacts(w.contacts_now(), w.kg, &w.bus);
        w.predator.step(w.sys, 1.0f / 60.0f);
        w.prey.step(w.sys, 1.0f / 60.0f);
    }

    std::cout << "  [measure] struck from the right: prey ended at x="
              << w.prey.x << " (must be negative)" << std::endl;
    check(w.prey.x < -0.5f,
          "the prey is driven away from the predator, not toward it (" +
          std::to_string(w.prey.x) + ")");
}

// A rule is evaluated from BOTH sides, so a symmetric rule pushes both
// creatures apart. That is what makes it a bounce rather than a shove.
void test_a_symmetric_rule_pushes_both() {
    World w("Prey", "Prey");   // both match
    w.add_rule("with_type:Prey", "knockback:3.0");

    w.bus.collisions().subscribe([](const onto::CollisionEvent&) {});
    w.predator.x = -3.0f; w.predator.vx = 2.0f;
    w.prey.x = 0.0f;

    for (int f = 0; f < 240; ++f) {
        w.sys.process_contacts(w.contacts_now(), w.kg, &w.bus);
        w.predator.step(w.sys, 1.0f / 60.0f);
        w.prey.step(w.sys, 1.0f / 60.0f);
    }

    std::cout << "  [measure] both sides match: A ended vx=" << w.predator.vx
              << ", B ended vx=" << w.prey.vx << std::endl;
    check(w.prey.vx > 0.0f, "the struck one is pushed forward");
    check(w.predator.vx < 2.0f,
          "and the striker is slowed or reversed by its own half of the rule (" +
          std::to_string(w.predator.vx) + " from 2.0)");
}

// ----------------------------------------------------- the inbox itself

void test_an_undrained_impulse_waits_rather_than_vanishing() {
    World w;
    w.add_rule("with_type:Predator", "knockback:3.0");
    w.bus.collisions().subscribe([](const onto::CollisionEvent&) {});

    w.predator.x = -0.6f;    // already overlapping
    w.prey.x = 0.0f;
    w.sys.process_contacts(w.contacts_now(), w.kg, &w.bus);

    std::cout << "  [measure] impulses waiting after one contact: "
              << w.sys.pending_impulse_count() << std::endl;
    check(w.sys.pending_impulse_count() == 1,
          "the impulse is queued for whoever owns the position");

    float ix = 0, iy = 0, iz = 0;
    check(w.sys.take_impulse(w.prey.particle, ix, iy, iz),
          "and the owner can take it");
    check(ix > 0.0f, "pointing away from the predator (" +
          std::to_string(ix) + ")");
    check(w.sys.pending_impulse_count() == 0, "draining clears it");
    check(!w.sys.take_impulse(w.prey.particle, ix, iy, iz),
          "and a second take finds nothing, so a push cannot be applied twice");
}

void test_impulses_accumulate_and_can_cancel() {
    World w;
    // Struck from both sides in the same frame: the two should sum, not
    // race. Deposited directly, because arranging a real sandwich would
    // test the geometry rather than the accumulator.
    w.sys.deposit_impulse(w.prey.particle, 3.0f, 0.0f, 0.0f);
    w.sys.deposit_impulse(w.prey.particle, -3.0f, 0.0f, 0.0f);

    float ix = 0, iy = 0, iz = 0;
    check(w.sys.take_impulse(w.prey.particle, ix, iy, iz), "one entry, not two");
    std::cout << "  [measure] equal and opposite pushes sum to x=" << ix
              << std::endl;
    check(std::abs(ix) < 1e-6f, "and they cancel (" + std::to_string(ix) + ")");
}

// --------------------------------------------------------- extensibility

// The schema says the effect vocabulary is a floor, not a ceiling. If a
// game cannot register its own effect, that claim is false. Bleeding is
// the example from the issue, so use it.
void test_a_game_can_register_its_own_effect() {
    int bleed_calls = 0;
    float bled_at_x = 0.0f;
    std::string bled_part;
    ContactEffectRegistry::instance().register_effect(
        "bleed", [&](ContactEffectContext& ctx, const std::string& args) {
            ++bleed_calls;
            bled_at_x = ctx.contact.contact_x;
            bled_part = std::to_string(ctx.contact.self_part);
            (void)args;
        });

    World w;
    w.add_rule("with_type:Predator", "bleed:0.4");
    w.bus.collisions().subscribe([](const onto::CollisionEvent&) {});
    w.predator.x = -0.6f;
    w.prey.x = 0.0f;
    w.sys.process_contacts(w.contacts_now(), w.kg, &w.bus);

    std::cout << "  [measure] game effect 'bleed' fired " << bleed_calls
              << " time(s), at x=" << bled_at_x << " on part " << bled_part
              << std::endl;
    check(bleed_calls == 1,
          "an effect the engine has never heard of runs (" +
          std::to_string(bleed_calls) + ")");
    check(!bled_part.empty() && bled_part != "0",
          "and it is told which part was struck, which is what armour needs");
}

void test_a_game_can_register_its_own_condition() {
    ContactConditionRegistry::instance().register_condition(
        "never", [](const ContactContext&, const std::string&) { return false; });

    World w;
    w.add_rule("never", "knockback:3.0");
    Outcome o = run(w);
    std::cout << "  [measure] rule gated by a game condition returning false: "
              << "prey pushed " << o.prey_travel << " m" << std::endl;
    check(o.touched, "contact happens");
    check(o.prey_travel == 0.0f, "and the game's condition suppresses the rule");
}

// A condition nobody registered must fail CLOSED. Failing open would
// turn one typo into a rule reacting to every contact in the world.
void test_an_unknown_condition_fails_closed() {
    World w;
    w.add_rule("wiht_type:Predator", "knockback:3.0");   // typo
    Outcome o = run(w);
    std::cout << "  [measure] misspelled condition: prey pushed "
              << o.prey_travel << " m (must be 0)" << std::endl;
    check(o.prey_travel == 0.0f,
          "an unregistered condition fires on NOTHING, not on everything");
}

// An effect nobody registered must be caught at load, not silently
// carried as a rule that does nothing forever.
void test_an_unknown_effect_is_refused_at_load() {
    World w;
    auto r = w.kg.createEntity("TransformationRule");
    w.kg.setProperty(r, "trigger", "on_contact");
    w.kg.setProperty(r, "effect", "vaporize");
    size_t n = w.sys.load_rules_from_kg(w.kg);
    std::cout << "  [measure] rule with an unregistered contact effect: "
              << n << " loaded (must be 0)" << std::endl;
    check(n == 0, "refused at load, where the name is still visible");
}

}  // namespace

int main() {
    std::cout << "Contact bounce: the whole chain (issue #36)" << std::endl;
    test_predator_and_prey_collide_and_bounce();
    test_the_mirror_rule_bounces_the_striker_instead();
    test_a_type_the_rule_does_not_name_does_not_bounce();
    test_a_gentle_touch_below_the_threshold_does_not_bounce();
    test_the_same_threshold_met_does_bounce();
    test_the_push_goes_away_from_the_other_party();
    test_a_symmetric_rule_pushes_both();
    test_an_undrained_impulse_waits_rather_than_vanishing();
    test_impulses_accumulate_and_can_cancel();
    test_a_game_can_register_its_own_effect();
    test_a_game_can_register_its_own_condition();
    test_an_unknown_condition_fails_closed();
    test_an_unknown_effect_is_refused_at_load();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
