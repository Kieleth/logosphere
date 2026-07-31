// EffectRegistry: pluggable response-rule effect applicators.
//
// Response rules fire a list of effects when their trigger condition
// holds. Each effect is a string like "speed_cap:0.5" or
// "cap_disable:locomotion". The registry maps the name prefix to an
// applicator function that mutates the capability computation state.
//
// Symmetric with TriggerRegistry. Games can register custom effects at
// startup without modifying engine code.
//
// Usage:
//   auto& registry = capability::EffectRegistry::instance();
//   registry.register_effect("heal",
//       [](capability::EffectContext& ctx, const std::string& args) {
//           float amount = std::stof(args);
//           float cur = std::stof(ctx.kg.getProperty(ctx.part_id, "health"));
//           ctx.kg.setProperty(ctx.part_id, "health", std::to_string(cur + amount));
//       });
//
// Built-in effects (registered by the engine):
//   speed_cap:<value>                 cap the entity speed_cap field
//   cap_disable:<capability>          force capability factor to 0
//   cap_modifier:<capability>:<f>     compound-multiply capability factor
//   emit_event:<type>                 emit WorldEvent with payload from KG props
//
// Effect syntax: "<name>" or "<name>:<args>". The first colon separates
// the name from the argument string; applicators parse args themselves.

#pragma once

#include "logosphere/kg/kg_types.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace kg { class KGModule; }

namespace capability {

// Mutable state being accumulated during response-rule evaluation.
// Effects mutate these to influence the final CapabilityProfile.
struct EffectContext {
    kg::EntityID part_id;        // body part the rule is attached to
    kg::EntityID entity_id;      // root entity
    kg::KGModule& kg;
    const std::string& rule_prefix;  // e.g. "rule.0" — used for payload lookup

    // Collectors applied after aggregation
    float& speed_cap;
    std::unordered_set<std::string>& disabled_caps;
    std::unordered_map<std::string, float>& cap_modifiers;
};

using EffectFn = std::function<void(EffectContext& ctx, const std::string& args)>;

class EffectRegistry {
public:
    static EffectRegistry& instance();

    void register_effect(const std::string& name, EffectFn fn);

    // Apply an effect expression (e.g. "speed_cap:0.5"). No-op if the
    // effect name is not registered. No-op on empty expression.
    void apply(const std::string& effect_expr, EffectContext& ctx) const;

    bool is_registered(const std::string& name) const;

private:
    EffectRegistry();
    std::unordered_map<std::string, EffectFn> effects_;
};

} // namespace capability
