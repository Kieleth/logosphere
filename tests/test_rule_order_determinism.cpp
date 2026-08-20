// =============================================================================
// TWO RULES, ONE CONTACT: THE ORDER IS A CONTRACT, NOT A HASH
// =============================================================================
// When two contact rules both match one contact, BOTH apply. That is not
// a design proposal, it is what the engine does
// (`particle_interaction_system.cpp`, the ON_CONTACT loop): every rule is
// walked, its condition checked, its effect applied. No election, no
// suppression. The owner's ruling of 2026-08-15 keeps it that way.
//
// So the question is never who wins. It is IN WHAT ORDER they apply,
// which decides the outcome the moment two effects touch the same thing:
// `state.set` and `profile.swap` are both last-write-wins.
//
// WHAT THIS CAUGHT. That loop used to iterate `rules_`, an
// `unordered_map<uint32_t, TransformationRule>`. The sequence was
// therefore a property of how EntityIDs hash and how many buckets the
// map happened to have, which is to say a property nobody chose.
// Measured, before the fix, it was also INVERTED: the rule authored
// SECOND applied FIRST.
//
//     [measure] alpha authored first: beta -> alpha -> beta -> alpha
//     [measure] beta  authored first: alpha -> beta -> alpha -> beta
//
// That is stable for one binary and one set of ids, so it is not a
// coin-flip. It is worse in a quieter way: it is unportable and
// unpredictable. Add a rule, cross a rehash boundary, change STL, and
// every last-write-wins outcome in the game can reorder with nothing in
// the authored content having changed.
//
// AN EXPECTATION I GOT WRONG, RECORDED BECAUSE IT MATTERS. This test
// first asserted that the order must NOT depend on which rule was
// authored first. That assertion is incorrect, and not merely unbuilt.
// These two rules are unconditional and identically shaped, which makes
// them EQUALLY SPECIFIC, and for equally specific rules authoring order
// is the right answer, not an accident to be designed away. The code was
// not loosened to fit; the expectation was wrong and is replaced here.
//
// WHAT IS ASSERTED. The order is the DECLARED contract: ascending rule
// id, which is authoring order. Both directions are checked, so the test
// cannot pass by the effects simply never firing. When the specificity
// key lands it becomes the primary comparator and rule id stays the final
// tiebreak, so this expectation survives that change unaltered.
//
// Run: ./build/test_rule_order_determinism
// =============================================================================

#undef NDEBUG

#include "generated/logosphere_ontology_registry.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/interaction/contact_response.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using logosphere::EventBus;
using logosphere::interaction::ContactEffectContext;
using logosphere::interaction::ContactEffectRegistry;
using logosphere::interaction::ParticleInteractionSystem;
using logosphere::interaction::RigidContact;

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}

// Every application of the test effect appends its tag here, in the order
// the engine applied it. This IS the measurement.
std::vector<std::string> g_order;

// Two game types the engine has never heard of, so nothing here can be
// accidentally tied to the shipped vocabulary.
kg::OntologyRegistry game_types() {
    kg::OntologyRegistry reg;
    reg.addEntityType("Striker", "LivingEntity", /*is_abstract=*/false);
    reg.addEntityType("Struck", "LivingEntity", false);
    reg.addAncestors("Striker", {"LivingEntity", "WorldEntity", "Entity"});
    reg.addAncestors("Struck", {"LivingEntity", "WorldEntity", "Entity"});
    return reg;
}

// One rule as authored: the tag its effect records, and the condition
// that decides whether it matches ("" = unconditional, matches always).
struct Rule {
    const char* tag;
    const char* condition;
};

// One contact between two KG-backed particles and the given rules, in the
// order given. The first authored receives the lower EntityID.
std::vector<std::string> run_rules(const std::vector<Rule>& rules) {
    g_order.clear();

    kg::KGModule kg{logosphere::ontology::registry()};
    EventBus bus;
    ParticleInteractionSystem sys;
    kg.setMode(kg::KGMode::MINIMAL);
    kg.extendOntology(game_types());

    auto body = [&](const char* type, const char* part, uint32_t idx) {
        kg::EntityID e = kg.createEntity(type);
        kg::EntityID p = kg.createEntity(part);
        kg.createRelation(e, "HAS_PART", p);
        kg.createKGParticle(p, idx);
    };
    body("Striker", "Head", 100);
    body("Struck", "Torso", 200);

    for (const Rule& rule : rules) {
        kg::EntityID r = kg.createEntity("TransformationRule");
        kg.setProperty(r, "trigger", "on_contact");
        kg.setProperty(r, "effect", std::string("record:") + rule.tag);
        if (rule.condition && *rule.condition)
            kg.setProperty(r, "condition", rule.condition);
    }
    sys.load_rules_from_kg(kg);

    RigidContact c;
    c.particle_a = 100;
    c.particle_b = 200;
    c.normal_x = 1.0f;
    c.contact_x = 0.5f;
    c.penetration = 0.05f;
    c.approach_speed = -4.0f;      // solver sign: negative is closing
    sys.process_contacts({ c }, kg, &bus);

    return g_order;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += " -> ";
        s += v[i];
    }
    return s.empty() ? "(nothing applied)" : s;
}

// A contact is evaluated once per resolved side, so each rule appears
// twice. Reduce to the order the rules were applied IN, which is what the
// last-write-wins question turns on.
std::vector<std::string> first_pass(const std::vector<std::string>& v) {
    std::vector<std::string> out;
    for (const auto& tag : v) {
        bool seen = false;
        for (const auto& s : out) if (s == tag) { seen = true; break; }
        if (seen) break;
        out.push_back(tag);
    }
    return out;
}

}  // namespace

int main() {
    std::printf("\n=== two rules, one contact: is the order a contract? ===\n");

    // Registered before any load: the loader skips a contact rule whose
    // effect name is not registered.
    ContactEffectRegistry::instance().register_effect(
        "record", [](ContactEffectContext&, const std::string& args) {
            g_order.push_back(args);
        });

    std::printf("\n-- equally specific rules: the tiebreak is authoring --\n");
    const auto a = run_rules({ {"alpha", ""}, {"beta", ""} });
    const auto b = run_rules({ {"beta", ""}, {"alpha", ""} });

    std::printf("  [measure] alpha authored first: %s\n", join(a).c_str());
    std::printf("  [measure] beta  authored first: %s\n", join(b).c_str());

    const auto pa = first_pass(a);
    const auto pb = first_pass(b);
    std::printf("  [measure] application order A: %s\n", join(pa).c_str());
    std::printf("  [measure] application order B: %s\n", join(pb).c_str());

    // Control first: the rest can only mean something if both rules
    // really fired in both runs. A green from "nothing applied" is a lie.
    check(pa.size() == 2 && pb.size() == 2,
          "both rules applied in both runs (the check is not vacuous)");

    // The contract, checked in BOTH directions. One direction alone would
    // also pass under a table that always emitted alphabetical order, or
    // one that always put `alpha` first by luck of hashing.
    check(pa.size() == 2 && pa[0] == "alpha" && pa[1] == "beta",
          "authored alpha then beta: alpha applies first");
    check(pb.size() == 2 && pb[0] == "beta" && pb[1] == "alpha",
          "authored beta then alpha: beta applies first (the control)");

    printf("\n  Equally specific rules apply in authoring order, which is\n"
           "  the declared tiebreak beneath the ordering by meaning below.\n");

    // -- The ordering that is about MEANING ---------------------------
    //
    // A rule with no condition describes EVERY contact. A rule that names
    // a condition describes fewer. The general one must apply FIRST, so
    // that when both write the same thing the narrow one's write is the
    // one left standing. This is what lets a blanket default be
    // overridden by a specific case, and it must hold no matter which of
    // the two the author happened to type first.
    //
    // `with_type:Struck` is the condition, and it matches: the contact
    // below is evaluated per side, and one of those sides sees a Struck
    // as the other party.
    std::printf("\n-- general before specific, whatever the authoring order --\n");
    const auto gs = run_rules({ {"general", ""},
                                {"specific", "with_type:Struck"} });
    const auto sg = run_rules({ {"specific", "with_type:Struck"},
                                {"general", ""} });

    std::printf("  [measure] general authored first:  %s\n", join(gs).c_str());
    std::printf("  [measure] specific authored first: %s\n", join(sg).c_str());

    const auto pgs = first_pass(gs);
    const auto psg = first_pass(sg);

    // Control: the conditional rule has to actually match, or "general
    // first" would be trivially true because `specific` never ran.
    check(pgs.size() == 2 && psg.size() == 2,
          "the conditional rule really matched (both rules ran)");

    check(pgs.size() == 2 && pgs[0] == "general" && pgs[1] == "specific",
          "authored general first: the general rule still applies first");
    check(psg.size() == 2 && psg[0] == "general" && psg[1] == "specific",
          "authored specific first: the general rule STILL applies first "
          "(order comes from meaning, not from typing order)");

    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "ORDER IS A DECLARED CONTRACT"
                              : "ORDER IS NOT WHAT THE CONTRACT SAYS",
                failures);
    return failures == 0 ? 0 : 1;
}
