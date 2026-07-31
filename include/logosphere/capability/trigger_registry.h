// TriggerRegistry: pluggable response-rule trigger evaluators.
//
// Response rules have triggers expressed as strings: "health_below:50",
// "destroyed", "relation_missing:HAS_PART". The registry maps the name
// prefix to an evaluator function. Games can register custom triggers
// at startup without modifying engine code.
//
// Usage:
//   auto& registry = capability::TriggerRegistry::instance();
//   registry.register_trigger("idle_longer_than",
//       [](const TriggerContext& ctx, const std::string& args) -> bool {
//           float seconds = std::stof(args);
//           // ... game logic
//           return false;
//       });
//
// The engine pre-registers these built-ins:
//   health_below:<pct>        fires when (health_ratio * 100) < pct
//   health_above:<pct>        fires when (health_ratio * 100) > pct
//   destroyed                 fires when health_ratio < 0.01
//   relation_missing:<type>   fires when entity_id has no <type> relation to part_id
//   relation_present:<type>   fires when entity_id has a <type> relation to part_id
//   age_above:<seconds>       fires when (now - created_at) > seconds
//   age_below:<seconds>       fires when (now - created_at) < seconds
//   compound:(<expr>)         AND/OR composition of other trigger expressions
//
// Trigger syntax: "<name>" or "<name>:<args>". The first colon separates
// the name from the argument string; evaluators parse the args themselves.
//
// compound grammar (v1): atoms separated by uppercase " AND " or " OR "
// tokens. Single operator per expression; mix requires nesting via
// another compound. Silent false on empty, mixed, or unbalanced parens.

#pragma once

#include "logosphere/kg/kg_types.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace kg { class KGModule; }

namespace capability {

struct TriggerContext {
    kg::EntityID part_id;
    kg::EntityID entity_id;
    kg::KGModule& kg;
    float health_ratio;  // computed once per part (0.0 - 1.0)
};

using TriggerFn = std::function<bool(const TriggerContext& ctx, const std::string& args)>;

class TriggerRegistry {
public:
    // Access the engine-wide registry. Lazily initialized with built-in triggers.
    static TriggerRegistry& instance();

    // Register or replace a trigger evaluator. Name should not contain ':'.
    void register_trigger(const std::string& name, TriggerFn fn);

    // Evaluate a trigger expression (e.g. "health_below:50"). Returns false
    // if the trigger name is unknown or if the evaluator returns false.
    bool evaluate(const std::string& trigger_expr, const TriggerContext& ctx) const;

    // Introspection
    bool is_registered(const std::string& name) const;

private:
    TriggerRegistry();
    std::unordered_map<std::string, TriggerFn> triggers_;
};

} // namespace capability
