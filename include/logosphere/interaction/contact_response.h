// Contact response: what a rule sees when two things touch, and the
// registries that let a game answer.
//
// Issue #36. An event rule asks three separable questions and the
// ontology gives each its own slot (schema/logosphere.yaml,
// TransformationRule):
//
//   trigger    WHICH engine event source   closed, ON_CONTACT is one
//   condition  WHETHER this one matters    open, registry below
//   effect     WHAT to do                  open, registry below
//
// The two registries here mirror capability::TriggerRegistry and
// capability::EffectRegistry in shape, deliberately, so there is one
// pattern to learn. They are NOT the same registries, because the
// contexts differ in kind: a capability rule is a state predicate over
// the KG and has no occurrence to look at, while everything here is
// about one specific touch. See docs/GAME_LAYER.md for which to reach
// for.
//
// Engine effects stay mechanism, never policy. KNOCKBACK delivers an
// impulse; deciding what a push does to a walking creature is the
// game's. Bleeding and armour absorption are game effects registered
// here, and the engine ships neither.

#pragma once

#include "logosphere/kg/kg_types.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace kg { class KGModule; }
namespace logosphere { class EventBus; }

namespace logosphere::interaction {

class ParticleInteractionSystem;

// ONE SIDE's view of a contact.
//
// A contact has two parties, and a rule is evaluated from each side in
// turn, the same way process_filtered_overlaps already records both
// directions of an episode open. `self` is the side the rule acts for;
// `other` is what it touched.
//
// READ THIS BEFORE WRITING A RULE. Conditions ask about `other`; effects
// act on `self`. A rule therefore says "how I react to being touched by
// X", never "what I do to X". So:
//
//   with_type:Predator -> knockback     the PREY is thrown back
//   with_type:Prey     -> knockback     the PREDATOR bounces off
//
// The second is the one people write when they mean the first. It is not
// a bug and it is not backwards: keeping effects on `self` is what stops
// a rule mutating a creature that never opted in, and it matches the
// capability rules, where a rule on a part affects that part's entity.
//
// The consequence to design around: an entity with NO rule of its own is
// unaffected by everything. If prey should be thrown back, the prey needs
// the rule. That is deliberate (nothing happens to you that your own
// ontology did not allow) and it is the single most likely thing to trip
// someone up.
//
// The normal is re-oriented per side to point AWAY from other, toward
// self. That is not a convenience: it deletes the sign question from
// every condition and effect anyone will ever write here. Pushing self
// away from other is always +normal, on a wall, on a ceiling, in zero-g.
struct ContactContext {
    kg::EntityID self_entity  = kg::INVALID_ENTITY;
    kg::EntityID self_part    = kg::INVALID_ENTITY;
    kg::EntityID other_entity = kg::INVALID_ENTITY;
    kg::EntityID other_part   = kg::INVALID_ENTITY;

    // Ontology type names. Empty when that side is not KG-backed, which
    // is the ordinary case for floor tiles and debris.
    std::string self_type;
    std::string other_type;

    // Stable ids, so an effect can address a particle across the
    // swap-and-pop that render indices do not survive.
    kg::KGParticleID self_particle  = 0;
    kg::KGParticleID other_particle = 0;

    float normal_x = 0.0f;   // unit, away from other toward self
    float normal_y = 0.0f;
    float normal_z = 0.0f;
    float contact_x = 0.0f;  // world space
    float contact_y = 0.0f;
    float contact_z = 0.0f;
    float penetration = 0.0f;      // metres
    float approach_speed = 0.0f;   // NEGATIVE while closing (solver sign)

    const kg::KGModule* kg = nullptr;
};

// What an effect is handed. The system reference is how KNOCKBACK
// reaches the impulse inbox without this header knowing about particles.
struct ContactEffectContext {
    const ContactContext& contact;
    ParticleInteractionSystem& system;
    logosphere::EventBus* bus = nullptr;
};

using ContactConditionFn =
    std::function<bool(const ContactContext& ctx, const std::string& args)>;
using ContactEffectFn =
    std::function<void(ContactEffectContext& ctx, const std::string& args)>;

// Whether a contact matters to a rule.
//
// Expression syntax is "<name>" or "<name>:<args>", the first colon
// separating them, evaluators parsing their own args. An EMPTY
// expression is unconditional and true: a rule with no condition cares
// about every contact its trigger selects.
//
// An unknown name is FALSE, not true. A rule whose condition nobody
// registered must not fire on everything; failing closed makes a typo a
// silent no-op instead of a silent catastrophe.
class ContactConditionRegistry {
public:
    static ContactConditionRegistry& instance();

    // Register or replace. Name must not contain ':'.
    void register_condition(const std::string& name, ContactConditionFn fn);

    bool evaluate(const std::string& expr, const ContactContext& ctx) const;
    bool is_registered(const std::string& name) const;

    // The name half of an expression, for validation at rule load.
    static std::string name_of(const std::string& expr);

private:
    ContactConditionRegistry();
    std::unordered_map<std::string, ContactConditionFn> conditions_;
};

// What to do about it.
//
// Same syntax. An unknown name is a no-op and is reported where it is
// caught, because an effect that silently does nothing is worse than one
// that says so.
class ContactEffectRegistry {
public:
    static ContactEffectRegistry& instance();

    void register_effect(const std::string& name, ContactEffectFn fn);

    void apply(const std::string& expr, ContactEffectContext& ctx) const;
    bool is_registered(const std::string& name) const;

private:
    ContactEffectRegistry();
    std::unordered_map<std::string, ContactEffectFn> effects_;
};

}  // namespace logosphere::interaction
