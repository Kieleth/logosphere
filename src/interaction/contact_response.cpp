#include "logosphere/interaction/contact_response.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"

#include <cstdio>
#include <cstdlib>

namespace logosphere::interaction {

namespace {

// "<name>:<args>" -> the two halves. Only the FIRST colon separates, so
// an argument may contain colons (a compound predicate, a relation
// path). Same rule as the capability registries.
void split_expr(const std::string& expr, std::string& name, std::string& args) {
    const size_t colon = expr.find(':');
    if (colon == std::string::npos) {
        name = expr;
        args.clear();
    } else {
        name = expr.substr(0, colon);
        args = expr.substr(colon + 1);
    }
}

// Args that are meant to be a number but are not should say so once,
// loudly, rather than silently behaving as 0 (which for a threshold
// means "always fires").
bool parse_float(const std::string& s, const char* who, float& out) {
    if (s.empty()) {
        std::fprintf(stderr, "[CONTACT] %s: missing numeric argument\n", who);
        return false;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') {
        std::fprintf(stderr, "[CONTACT] %s: '%s' is not a number\n",
                     who, s.c_str());
        return false;
    }
    out = static_cast<float>(v);
    return true;
}

}  // namespace

// ---------------------------------------------------------- conditions

std::string ContactConditionRegistry::name_of(const std::string& expr) {
    std::string name, args;
    split_expr(expr, name, args);
    return name;
}

ContactConditionRegistry& ContactConditionRegistry::instance() {
    static ContactConditionRegistry registry;
    return registry;
}

ContactConditionRegistry::ContactConditionRegistry() {
    // with_type:<EntityType>
    //
    // The decision behind this (design note D2): the contact-side role
    // is the KG entity type, not the interaction profile's category bit.
    // Category bits also drive physics filtering, so labelling a
    // creature for a rule would change who it collides with. A type is
    // inert: it cannot perturb the solver no matter what a game writes.
    register_condition("with_type",
        [](const ContactContext& ctx, const std::string& args) {
            return !args.empty() && ctx.other_type == args;
        });

    // impact_above:<m/s>
    //
    // approach_speed carries the SOLVER's sign: negative while closing.
    // A caller asking "harder than 2 m/s" means closing speed, so the
    // comparison is on magnitude of the closing component. A separating
    // pair never satisfies this, which is what stops a rule firing again
    // as two bodies drift apart.
    register_condition("impact_above",
        [](const ContactContext& ctx, const std::string& args) {
            float threshold = 0.0f;
            if (!parse_float(args, "impact_above", threshold)) return false;
            if (ctx.approach_speed >= 0.0f) return false;   // separating
            return -ctx.approach_speed > threshold;
        });

    // with_part_type:<EntityType>
    //
    // Which part of the other thing touched, as opposed to what it is.
    // "bitten by a Head" differs from "stepped on by a Foot", and armour
    // cares about which of MY parts was struck, which is self_part.
    register_condition("with_part_type",
        [](const ContactContext& ctx, const std::string& args) {
            if (args.empty() || !ctx.kg) return false;
            if (ctx.other_part == kg::INVALID_ENTITY) return false;
            return ctx.kg->getType(ctx.other_part) == args;
        });
}

void ContactConditionRegistry::register_condition(const std::string& name,
                                                  ContactConditionFn fn) {
    conditions_[name] = std::move(fn);
}

bool ContactConditionRegistry::is_registered(const std::string& name) const {
    return conditions_.count(name) > 0;
}

bool ContactConditionRegistry::evaluate(const std::string& expr,
                                        const ContactContext& ctx) const {
    // No condition means the trigger alone decides. This is the common
    // case and must stay cheap.
    if (expr.empty()) return true;

    std::string name, args;
    split_expr(expr, name, args);
    auto it = conditions_.find(name);
    if (it == conditions_.end()) {
        // Fail CLOSED. An unregistered condition firing on everything
        // would turn one typo into a rule that reacts to every contact
        // in the world.
        std::fprintf(stderr,
                     "[CONTACT] unknown condition '%s' — rule will not fire\n",
                     name.c_str());
        return false;
    }
    return it->second(ctx, args);
}

// ------------------------------------------------------------- effects

ContactEffectRegistry& ContactEffectRegistry::instance() {
    static ContactEffectRegistry registry;
    return registry;
}

ContactEffectRegistry::ContactEffectRegistry() {
    // knockback:<speed>
    //
    // Deposits an impulse along the contact normal, away from whatever
    // was touched, and moves NOTHING.
    //
    // That is the whole point. A KINEMATIC body has inv_mass 0, so the
    // solver applies no impulse to it and never will: its position
    // belongs to an external writer. Writing velocity onto it here would
    // be overwritten by that writer on the next frame, and writing
    // position would break the authority contract outright. So the push
    // is handed to the owner, who decides what a push means for a
    // creature mid-stride. Engine ships the mechanism, game ships the
    // policy.
    //
    // Nothing drains the inbox by default. An unclaimed impulse is a
    // game that has not wired its mover yet, not an engine failure.
    register_effect("knockback",
        [](ContactEffectContext& ctx, const std::string& args) {
            float speed = 0.0f;
            if (!parse_float(args, "knockback", speed)) return;
            if (ctx.contact.self_particle == 0) return;   // nothing to push
            ctx.system.deposit_impulse(ctx.contact.self_particle,
                                       ctx.contact.normal_x * speed,
                                       ctx.contact.normal_y * speed,
                                       ctx.contact.normal_z * speed);
        });

    // emit_event:<name>
    //
    // The seam a game uses when the consequence lives outside the
    // engine: react on the bus with the full contact already resolved.
    register_effect("emit_event",
        [](ContactEffectContext& ctx, const std::string& args) {
            if (!ctx.bus) return;
            onto::TransformationEvent e;
            e.rule_name = args;
            e.source_entity_id = std::to_string(ctx.contact.self_entity);
            e.target_entity_id = std::to_string(ctx.contact.other_entity);
            ctx.bus->transformations().emit(e);
        });
}

void ContactEffectRegistry::register_effect(const std::string& name,
                                            ContactEffectFn fn) {
    effects_[name] = std::move(fn);
}

bool ContactEffectRegistry::is_registered(const std::string& name) const {
    return effects_.count(name) > 0;
}

void ContactEffectRegistry::apply(const std::string& expr,
                                  ContactEffectContext& ctx) const {
    if (expr.empty()) return;
    std::string name, args;
    split_expr(expr, name, args);
    auto it = effects_.find(name);
    if (it == effects_.end()) {
        std::fprintf(stderr, "[CONTACT] unknown effect '%s' — nothing done\n",
                     name.c_str());
        return;
    }
    it->second(ctx, args);
}

}  // namespace logosphere::interaction
