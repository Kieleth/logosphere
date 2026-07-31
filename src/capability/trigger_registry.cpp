#include "logosphere/capability/trigger_registry.h"
#include "logosphere/core/kg_parse.h"
#include "logosphere/kg/kg_module.h"
#include "core/game_time.h"
#include <algorithm>
#include <cctype>
#include <vector>

namespace capability {

TriggerRegistry& TriggerRegistry::instance() {
    static TriggerRegistry reg;
    return reg;
}

TriggerRegistry::TriggerRegistry() {
    // Built-in triggers

    register_trigger("health_below",
        [](const TriggerContext& ctx, const std::string& args) {
            auto threshold = kg_parse::to_float(args, "rule.trigger:health_below", ctx.part_id);
            if (!threshold) return false;
            return (ctx.health_ratio * 100.0f) < *threshold;
        });

    register_trigger("health_above",
        [](const TriggerContext& ctx, const std::string& args) {
            auto threshold = kg_parse::to_float(args, "rule.trigger:health_above", ctx.part_id);
            if (!threshold) return false;
            return (ctx.health_ratio * 100.0f) > *threshold;
        });

    register_trigger("destroyed",
        [](const TriggerContext& ctx, const std::string&) {
            return ctx.health_ratio < 0.01f;
        });

    register_trigger("relation_missing",
        [](const TriggerContext& ctx, const std::string& args) {
            if (args.empty()) return false;
            auto related = ctx.kg.getRelated(ctx.entity_id, args);
            return std::find(related.begin(), related.end(), ctx.part_id) == related.end();
        });

    register_trigger("relation_present",
        [](const TriggerContext& ctx, const std::string& args) {
            if (args.empty()) return false;
            auto related = ctx.kg.getRelated(ctx.entity_id, args);
            return std::find(related.begin(), related.end(), ctx.part_id) != related.end();
        });

    // age_above:<seconds> — fires when the part's "created_at" property
    // indicates the part is older than <seconds>. Silent no-op if the
    // "created_at" property is not set (games opt in by setting it).
    register_trigger("age_above",
        [](const TriggerContext& ctx, const std::string& args) {
            auto threshold = kg_parse::to_double(args, "rule.trigger:age_above", ctx.part_id);
            if (!threshold) return false;
            auto created_at_str = ctx.kg.getProperty(ctx.part_id, "created_at");
            if (created_at_str.empty()) return false;
            auto created_at = kg_parse::to_double(created_at_str, "created_at", ctx.part_id);
            if (!created_at) return false;
            double now = GameTime::get_current_time();
            return (now - *created_at) > *threshold;
        });

    // age_below:<seconds> — fires when the part's "created_at" indicates
    // the part is younger than <seconds>. Also a no-op without created_at.
    register_trigger("age_below",
        [](const TriggerContext& ctx, const std::string& args) {
            auto threshold = kg_parse::to_double(args, "rule.trigger:age_below", ctx.part_id);
            if (!threshold) return false;
            auto created_at_str = ctx.kg.getProperty(ctx.part_id, "created_at");
            if (created_at_str.empty()) return false;
            auto created_at = kg_parse::to_double(created_at_str, "created_at", ctx.part_id);
            if (!created_at) return false;
            double now = GameTime::get_current_time();
            return (now - *created_at) < *threshold;
        });

    // compound:(<expr>) — conjunction or disjunction of other triggers.
    // v1 grammar: atoms separated by uppercase " AND " or " OR " tokens.
    // One top-level operator per expression (no mixed precedence without
    // explicit grouping). Each atom is re-dispatched through the registry,
    // so nesting and custom triggers work. Silent false on: empty, mixed
    // AND/OR, unbalanced parens, unknown inner name.
    register_trigger("compound",
        [](const TriggerContext& ctx, const std::string& raw_args) {
            auto trim = [](std::string s) {
                size_t a = 0, b = s.size();
                while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
                while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
                return s.substr(a, b - a);
            };

            std::string expr = trim(raw_args);
            if (expr.empty()) return false;

            // Fail-closed on unbalanced parens.
            int depth = 0;
            for (char c : expr) {
                if (c == '(') depth++;
                else if (c == ')') { depth--; if (depth < 0) return false; }
            }
            if (depth != 0) return false;

            // Strip one layer of outer parens if they enclose the whole expr.
            if (expr.size() >= 2 && expr.front() == '(' && expr.back() == ')') {
                int d = 0; bool wraps = true;
                for (size_t i = 0; i < expr.size(); i++) {
                    if (expr[i] == '(') d++;
                    else if (expr[i] == ')') d--;
                    if (d == 0 && i + 1 < expr.size()) { wraps = false; break; }
                }
                if (wraps) expr = trim(expr.substr(1, expr.size() - 2));
            }
            if (expr.empty()) return false;

            bool has_and = expr.find(" AND ") != std::string::npos;
            bool has_or  = expr.find(" OR ")  != std::string::npos;
            if (has_and && has_or) return false;  // v1: single operator only

            auto split = [](const std::string& s, const std::string& sep) {
                std::vector<std::string> out;
                size_t start = 0;
                while (true) {
                    size_t pos = s.find(sep, start);
                    if (pos == std::string::npos) { out.push_back(s.substr(start)); break; }
                    out.push_back(s.substr(start, pos - start));
                    start = pos + sep.size();
                }
                return out;
            };

            const auto& reg = TriggerRegistry::instance();
            if (has_or) {
                for (auto& atom : split(expr, " OR ")) {
                    std::string a = trim(atom);
                    if (a.empty()) continue;
                    if (reg.evaluate(a, ctx)) return true;
                }
                return false;
            }
            // AND (or single atom with no operator)
            for (auto& atom : split(expr, " AND ")) {
                std::string a = trim(atom);
                if (a.empty()) return false;
                if (!reg.evaluate(a, ctx)) return false;
            }
            return true;
        });
}

void TriggerRegistry::register_trigger(const std::string& name, TriggerFn fn) {
    triggers_[name] = std::move(fn);
}

bool TriggerRegistry::is_registered(const std::string& name) const {
    return triggers_.count(name) > 0;
}

bool TriggerRegistry::evaluate(const std::string& trigger_expr, const TriggerContext& ctx) const {
    if (trigger_expr.empty()) return false;

    // Split on first colon: "<name>" or "<name>:<args>"
    auto colon = trigger_expr.find(':');
    std::string name = (colon == std::string::npos) ? trigger_expr : trigger_expr.substr(0, colon);
    std::string args = (colon == std::string::npos) ? "" : trigger_expr.substr(colon + 1);

    auto it = triggers_.find(name);
    if (it == triggers_.end()) return false;
    return it->second(ctx, args);
}

} // namespace capability
