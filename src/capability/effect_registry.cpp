#include "logosphere/capability/effect_registry.h"
#include "logosphere/core/kg_parse.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology.h"
#include <algorithm>

namespace capability {

EffectRegistry& EffectRegistry::instance() {
    static EffectRegistry reg;
    return reg;
}

EffectRegistry::EffectRegistry() {
    // speed_cap:<value> — caps the entity speed_cap at value (min of all active caps)
    register_effect("speed_cap",
        [](EffectContext& ctx, const std::string& args) {
            auto cap = kg_parse::to_float(args, "rule.effect:speed_cap", ctx.part_id);
            if (!cap) return;
            ctx.speed_cap = std::min(ctx.speed_cap, *cap);
        });

    // cap_disable:<capability> — force capability factor to 0
    register_effect("cap_disable",
        [](EffectContext& ctx, const std::string& args) {
            if (args.empty()) return;
            ctx.disabled_caps.insert(args);
        });

    // cap_modifier:<capability>:<factor> — compound-multiply capability factor
    register_effect("cap_modifier",
        [](EffectContext& ctx, const std::string& args) {
            auto colon = args.find(':');
            if (colon == std::string::npos) return;
            std::string cap_name = args.substr(0, colon);
            auto factor = kg_parse::to_float(args.substr(colon + 1),
                                             "rule.effect:cap_modifier", ctx.part_id);
            if (!factor) return;
            auto it = ctx.cap_modifiers.find(cap_name);
            if (it == ctx.cap_modifiers.end()) ctx.cap_modifiers[cap_name] = *factor;
            else it->second *= *factor;
        });

    // emit_event:<type> — fire WorldEvent on state_changes, payload from rule.N.payload.*
    register_effect("emit_event",
        [](EffectContext& ctx, const std::string& args) {
            if (args.empty()) return;
            auto* bus = ctx.kg.get_event_bus();
            if (!bus) return;
            logosphere::ontology::WorldEvent evt;
            evt.event_type = args;
            evt.source_entity_id = std::to_string(ctx.part_id);
            evt.target_entity_id = std::to_string(ctx.entity_id);
            std::string payload_prefix = ctx.rule_prefix + ".payload.";
            auto payload_props = ctx.kg.getPropertiesWithPrefix(ctx.part_id, payload_prefix);
            for (const auto& [key, value] : payload_props) {
                evt.payload_keys.push_back(key.substr(payload_prefix.size()));
                evt.payload_values.push_back(value);
            }
            bus->state_changes().emit(std::move(evt));
        });
}

void EffectRegistry::register_effect(const std::string& name, EffectFn fn) {
    effects_[name] = std::move(fn);
}

bool EffectRegistry::is_registered(const std::string& name) const {
    return effects_.count(name) > 0;
}

void EffectRegistry::apply(const std::string& effect_expr, EffectContext& ctx) const {
    if (effect_expr.empty()) return;
    auto colon = effect_expr.find(':');
    std::string name = (colon == std::string::npos) ? effect_expr : effect_expr.substr(0, colon);
    std::string args = (colon == std::string::npos) ? "" : effect_expr.substr(colon + 1);

    auto it = effects_.find(name);
    if (it == effects_.end()) return;
    it->second(ctx, args);
}

} // namespace capability
