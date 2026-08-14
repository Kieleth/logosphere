// Loading event rules out of the KG.
//
// A TransformationRule is authored as KG properties and parsed by
// ParticleInteractionSystem::load_rules_from_kg. Two doors stand
// between a typo and a live rule: KGCore::setProperty's ontology gate
// (malleus H1) refuses undeclared keys and non-member enum values at
// write time, and this parse refuses whatever still arrives malformed
// (lenient-gate content, pre-gate tapes, ops replay). This file pins
// the SECOND door. The tests that manufacture bad trigger values open
// the first door explicitly (GateLeniency below) so the second one can
// be exercised at all.
//
// Issue #36 gave rules a third slot. The three are deliberately not
// symmetric, and this file is where that asymmetry is pinned down:
//
//   trigger    closed   parsed through the GENERATED ontology vocabulary,
//                       so the schema and the loader cannot drift
//   effect     floor    known names accepted, unknown warns and skips
//                       until a game registers one
//   condition  open     NOT validated here by design; its evaluator is
//                       chosen when the event fires
//
// Headless-safe: KG plus the interaction system, no physics, no render.
//
// Usage:
//   ./build/test_interaction_rule_loading

#undef NDEBUG

#include "generated/logosphere_ontology_registry.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"

#include <cstdlib>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

namespace {

using logosphere::interaction::ParticleInteractionSystem;
using logosphere::interaction::TransformationRule;
using Trigger = TransformationRule::Trigger;
using Effect = TransformationRule::Effect;

struct Fixture {
    kg::KGModule kg{logosphere::ontology::registry()};
    Fixture() { kg.setMode(kg::KGMode::MINIMAL); }

    kg::EntityID rule(const char* trigger, const char* effect,
                      const char* condition = nullptr) {
        auto id = kg.createEntity("TransformationRule");
        kg.setProperty(id, "trigger", trigger);
        kg.setProperty(id, "effect", effect);
        if (condition) kg.setProperty(id, "condition", condition);
        return id;
    }
};

// The write gate would abort this binary on a non-member trigger value
// before the loader ever saw it — the right behavior everywhere except
// in the two tests whose PURPOSE is to hand the loader malformed rules
// and watch it refuse them. Scoped leniency: the gate still prints the
// violation, the write lands, the loader's door gets exercised, and
// every other test in this binary stays strict.
struct GateLeniency {
    GateLeniency() {
#ifdef _WIN32
        _putenv_s("KG_GATE_LENIENT", "1");
#else
        setenv("KG_GATE_LENIENT", "1", 1);
#endif
    }
    ~GateLeniency() {
#ifdef _WIN32
        _putenv_s("KG_GATE_LENIENT", "");
#else
        unsetenv("KG_GATE_LENIENT");
#endif
    }
};

// ------------------------------------------------------- the vocabulary

void test_the_new_contact_trigger_loads() {
    Fixture fx;
    fx.rule("on_contact", "emit_event");

    ParticleInteractionSystem sys;
    size_t n = sys.load_rules_from_kg(fx.kg);
    std::cout << "  [measure] on_contact rules loaded: " << n << std::endl;
    check(n == 1, "ON_CONTACT is a loadable trigger (" +
          std::to_string(n) + " loaded)");
}

// The schema spells enums upper; rules in the wild are authored lower.
// Both must resolve to the same rule, or the ontology describes a
// vocabulary the loader does not accept.
void test_case_does_not_change_meaning() {
    for (const char* spelling : {"on_contact", "ON_CONTACT", "On_Contact"}) {
        Fixture fx;
        fx.rule(spelling, "emit_event");
        ParticleInteractionSystem sys;
        size_t n = sys.load_rules_from_kg(fx.kg);
        check(n == 1, std::string("trigger spelled '") + spelling +
              "' loads (" + std::to_string(n) + ")");
    }
    // Same for the effect, which is normalized by the same rule.
    Fixture fx;
    fx.rule("ON_TIMER", "FADE_OUT");
    ParticleInteractionSystem sys;
    check(sys.load_rules_from_kg(fx.kg) == 1, "effect case is normalized too");
    std::cout << "  [measure] every case spelling resolved to one rule"
              << std::endl;
}

// The control for the case test. If normalization were implemented as
// "accept anything vaguely similar", this would pass too and the parse
// would be worthless.
void test_nonsense_is_still_rejected() {
    GateLeniency open_first_door;
    Fixture fx;
    fx.rule("on_contract", "emit_event");     // one letter from on_contact
    fx.rule("on_timer", "explode");           // effect nobody registered
    fx.rule("", "emit_event");                // absent trigger
    fx.rule("on_timer", "");                  // absent effect

    ParticleInteractionSystem sys;
    size_t n = sys.load_rules_from_kg(fx.kg);
    std::cout << "  [measure] 4 malformed rules -> " << n << " loaded"
              << " (expected 0)" << std::endl;
    check(n == 0, "a rule that cannot say when or what is not loaded (" +
          std::to_string(n) + ")");
}

// A bad rule must not take its neighbours down with it.
void test_one_bad_rule_does_not_poison_the_batch() {
    GateLeniency open_first_door;
    Fixture fx;
    fx.rule("on_contact", "emit_event");
    fx.rule("nonsense", "emit_event");
    fx.rule("on_timer", "fade_out");

    ParticleInteractionSystem sys;
    size_t n = sys.load_rules_from_kg(fx.kg);
    std::cout << "  [measure] 2 good + 1 bad -> " << n << " loaded" << std::endl;
    check(n == 2, "the good rules load and only the bad one is skipped (" +
          std::to_string(n) + ")");
    check(sys.rule_count() == 2, "and the system holds exactly those two");
}

// --------------------------------------------------------- the condition

void test_the_condition_is_carried_verbatim() {
    Fixture fx;
    // A condition name the engine has never heard of. It must survive
    // loading untouched: validating it here would close a slot the
    // schema declares open, and would reject every game predicate.
    fx.rule("on_contact", "emit_event", "hungrier_than:0.7");

    ParticleInteractionSystem sys;
    size_t n = sys.load_rules_from_kg(fx.kg);
    std::cout << "  [measure] rule with an unknown condition: " << n
              << " loaded" << std::endl;
    check(n == 1, "an unrecognized condition does NOT reject the rule (" +
          std::to_string(n) + ")");
}

void test_an_absent_condition_is_empty_not_broken() {
    Fixture fx;
    fx.rule("on_contact", "emit_event");   // no condition property at all

    ParticleInteractionSystem sys;
    check(sys.load_rules_from_kg(fx.kg) == 1,
          "a rule with no condition loads: empty means unconditional");
}

// ----------------------------------------------------------- boundaries

void test_loading_twice_replaces_rather_than_duplicates() {
    Fixture fx;
    fx.rule("on_contact", "emit_event");

    ParticleInteractionSystem sys;
    sys.load_rules_from_kg(fx.kg);
    sys.load_rules_from_kg(fx.kg);
    std::cout << "  [measure] after loading the same KG twice: "
              << sys.rule_count() << " rule(s)" << std::endl;
    check(sys.rule_count() == 1,
          "rules are keyed by entity id, so a reload replaces (" +
          std::to_string(sys.rule_count()) + ")");
}

void test_an_empty_kg_loads_nothing_and_says_so() {
    Fixture fx;
    ParticleInteractionSystem sys;
    size_t n = sys.load_rules_from_kg(fx.kg);
    check(n == 0 && sys.rule_count() == 0,
          "no rules in the KG means no rules loaded, not a default rule");
}

}  // namespace

int main() {
    std::cout << "Event rule loading (load_rules_from_kg)" << std::endl;
    test_the_new_contact_trigger_loads();
    test_case_does_not_change_meaning();
    test_nonsense_is_still_rejected();
    test_one_bad_rule_does_not_poison_the_batch();
    test_the_condition_is_carried_verbatim();
    test_an_absent_condition_is_empty_not_broken();
    test_loading_twice_replaces_rather_than_duplicates();
    test_an_empty_kg_loads_nothing_and_says_so();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
