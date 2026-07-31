// CapabilityProfile: KG body graph capability aggregation.
//
// Queries KG body graph, categorizes parts into capability factors via
// declared cap_list bindings and aggregation modes (average, minimum,
// binary, weighted_sum). Response rules and cascade are evaluated here.
//
// Does NOT derive dynamics parameters (walk speed, stiffness, etc.).
// That is the game's responsibility via DynamicsParams::from_capability().

#include "logosphere/capability/capability_profile.h"
#include "logosphere/capability/trigger_registry.h"
#include "logosphere/capability/effect_registry.h"
#include "logosphere/core/kg_parse.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/events/event_bus.h"
#include "generated/logosphere_ontology.h"
#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <unordered_set>

// Reference human for default FORGE inputs
static constexpr float REF_REFLEXES = 250.0f;
static constexpr float REF_GRIT     = 500.0f;
static constexpr float REF_LEG      = 0.9f;
static constexpr float REF_HEIGHT   = 1.8f;

// ============================================================================
// Flat construction (backward compat for tests)
// ============================================================================

CapabilityProfile CapabilityProfile::compute(float reflexes, float grit,
                                              float mass_kg, float leg_len, float height) {
    CapabilityProfile p;
    p.reflexes_ms = reflexes;
    p.grit_W = grit;
    p.mass = mass_kg;
    p.leg_length = leg_len;
    p.total_height = height;

    // Full capability (healthy entity with all parts)
    p.locomotion_factor = 1.0f;
    p.manipulation_factor = 1.0f;
    p.rotation_factor = 1.0f;
    p.perception_factor = 1.0f;
    p.left_arm_factor = 1.0f;
    p.right_arm_factor = 1.0f;
    p.left_leg_factor = 1.0f;
    p.right_leg_factor = 1.0f;

    return p;
}

// ============================================================================
// KG construction: queries body graph for capabilities
// ============================================================================

static float read_health_ratio(kg::KGModule& kg, kg::EntityID part_id) {
    auto h = kg.getProperty(part_id, "health");
    auto mh = kg.getProperty(part_id, "max_health");
    if (h.empty() || mh.empty()) return 1.0f;
    auto health = kg_parse::to_float(h, "health", part_id);
    auto max_health = kg_parse::to_float(mh, "max_health", part_id);
    if (!health || !max_health) return 1.0f;  // malformed → treat as full HP
    return *max_health > 0 ? std::clamp(*health / *max_health, 0.0f, 1.0f) : 0.0f;
}

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        std::string token = s.substr(start, end - start);
        if (!token.empty()) result.push_back(token);
        start = end + 1;
    }
    return result;
}

struct CapContribution {
    float health_weighted_sum = 0;
    float weight_sum = 0;
    int count = 0;
    float minimum = 1.0f;
    bool any_below_threshold = false;
};

CapabilityProfile CapabilityProfile::compute_from_kg(kg::KGModule& kg, kg::EntityID entity_id,
                                                      float mass_kg, float leg_len, float height) {
    CapabilityProfile p;
    p.mass = mass_kg;
    p.leg_length = leg_len;
    p.total_height = height;

    auto reflexes_str = kg.getProperty(entity_id, "reflexes_ms");
    auto grit_str = kg.getProperty(entity_id, "grit_W");
    p.reflexes_ms = reflexes_str.empty() ? REF_REFLEXES
        : kg_parse::to_float(reflexes_str, "reflexes_ms", entity_id).value_or(REF_REFLEXES);
    p.grit_W = grit_str.empty() ? REF_GRIT
        : kg_parse::to_float(grit_str, "grit_W", entity_id).value_or(REF_GRIT);

    p.left_arm_factor = 0;
    p.right_arm_factor = 0;
    p.left_leg_factor = 0;
    p.right_leg_factor = 0;

    std::unordered_map<std::string, CapContribution> contributions;
    bool used_data_driven = false;

    auto parts = kg.getRelated(entity_id, "HAS_PART");

    // Pre-scan rule groups declared on the entity. Games flip these
    // flags (rule_group.M.name / rule_group.M.enabled) to bundle rules
    // on/off as a set — e.g., "flying" rules only fire while airborne.
    // Fail-closed: a rule referencing an undeclared group is skipped.
    std::unordered_map<std::string, bool> group_enabled;
    for (int gi = 0; gi < 8; gi++) {
        std::string gprefix = "rule_group." + std::to_string(gi);
        auto gname = kg.getProperty(entity_id, gprefix + ".name");
        if (gname.empty()) break;
        group_enabled[gname] = (kg.getProperty(entity_id, gprefix + ".enabled") == "true");
    }

    auto rule_gated_out = [&](kg::EntityID pid, const std::string& rule_prefix) {
        auto group = kg.getProperty(pid, rule_prefix + ".group");
        if (group.empty()) return false;              // ungrouped → always active
        auto it = group_enabled.find(group);
        if (it == group_enabled.end()) return true;   // undeclared → fail-closed
        return !it->second;                           // disabled → skip
    };

    // Pass 1: evaluate response rules, collect cascade + cap modifications
    //
    // cascade_factors[part_id] → minimum factor to apply to that part's
    // effective health in Pass 2. Per-part granularity lets games cascade
    // damage via named relations (e.g., relation:GRIP_ON:0.3 reaches the
    // held weapon) while preserving the classic "children:" global scope.
    std::unordered_map<kg::EntityID, float> cascade_factors;
    std::unordered_set<std::string> disabled_caps;           // cap_disable:<name>
    std::unordered_map<std::string, float> cap_modifiers;   // cap_modifier:<name>:<factor>, compound multiply

    auto trim = [](std::string s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
        return s.substr(a, b - a);
    };

    auto min_merge = [&](kg::EntityID id, float f) {
        auto it = cascade_factors.find(id);
        if (it == cascade_factors.end()) cascade_factors[id] = f;
        else it->second = std::min(it->second, f);
    };

    const auto& trigger_registry = capability::TriggerRegistry::instance();

    for (auto part_id : parts) {
        float health = read_health_ratio(kg, part_id);

        for (int ri = 0; ri < 8; ri++) {
            std::string prefix = "rule." + std::to_string(ri);
            auto trigger = kg.getProperty(part_id, prefix + ".trigger");
            if (trigger.empty()) break;
            if (rule_gated_out(part_id, prefix)) continue;

            capability::TriggerContext ctx{part_id, entity_id, kg, health};
            bool fires = trigger_registry.evaluate(trigger, ctx);

            if (fires) {
                capability::EffectContext effect_ctx{
                    part_id, entity_id, kg, prefix,
                    p.speed_cap, disabled_caps, cap_modifiers
                };
                const auto& effect_registry = capability::EffectRegistry::instance();
                for (int ei = 0; ei < 4; ei++) {
                    std::string ekey = prefix + (ei == 0 ? ".effect" : ".effect_" + std::to_string(ei + 1));
                    auto effect = kg.getProperty(part_id, ekey);
                    if (effect.empty()) break;
                    effect_registry.apply(effect, effect_ctx);
                }

                // cascade: CSV of clauses. "children:<f>" is an alias for
                // "relation:HAS_PART:<f>" (entity-level scope). "relation:
                // <TYPE>:<f>" reaches entities related to the firing part
                // via TYPE. Malformed clauses skipped silently.
                auto cascade_str = kg.getProperty(part_id, prefix + ".cascade");
                if (!cascade_str.empty()) {
                    for (const auto& raw_clause : split_csv(cascade_str)) {
                        std::string clause = trim(raw_clause);
                        if (clause.rfind("children:", 0) == 0) {
                            auto f = kg_parse::to_float(clause.substr(9),
                                "rule.cascade:children", part_id);
                            if (f) for (auto p_id : parts) min_merge(p_id, *f);
                        } else if (clause.rfind("relation:", 0) == 0) {
                            std::string rest = clause.substr(9);
                            auto colon = rest.find(':');
                            if (colon == std::string::npos) continue;
                            std::string rel_type = trim(rest.substr(0, colon));
                            if (rel_type.empty()) continue;
                            auto f = kg_parse::to_float(trim(rest.substr(colon + 1)),
                                "rule.cascade:relation", part_id);
                            if (!f) continue;
                            for (auto t : kg.getRelated(part_id, rel_type)) {
                                min_merge(t, *f);
                            }
                        }
                    }
                }
            }
        }
    }

    // Pass 2: compute capability contributions with cascade applied
    for (auto part_id : parts) {
        float raw_health = read_health_ratio(kg, part_id);
        std::string name = kg.getProperty(part_id, "body_part_name");

        float health = raw_health;
        bool own_rule_triggered = false;
        for (int ri = 0; ri < 8; ri++) {
            std::string prefix = "rule." + std::to_string(ri);
            auto trigger = kg.getProperty(part_id, prefix + ".trigger");
            if (trigger.empty()) break;
            if (rule_gated_out(part_id, prefix)) continue;
            capability::TriggerContext ctx{part_id, entity_id, kg, raw_health};
            if (trigger_registry.evaluate(trigger, ctx)) { own_rule_triggered = true; break; }
        }
        if (!own_rule_triggered) {
            auto it = cascade_factors.find(part_id);
            if (it != cascade_factors.end() && it->second < 1.0f) {
                health *= it->second;
            }
        }

        std::string cap_list = kg.getProperty(part_id, "cap_list");
        if (!cap_list.empty()) {
            used_data_driven = true;
            auto cap_names = split_csv(cap_list);
            for (const auto& cap_name : cap_names) {
                float weight = 1.0f;
                auto w_str = kg.getProperty(part_id, "cap." + cap_name + ".weight");
                if (!w_str.empty()) {
                    weight = kg_parse::to_float(w_str, "cap.weight", part_id).value_or(1.0f);
                }

                auto& c = contributions[cap_name];
                c.health_weighted_sum += weight * health;
                c.weight_sum += weight;
                c.count++;
                c.minimum = std::min(c.minimum, weight * health);

                auto thresh_str = kg.getProperty(part_id, "cap." + cap_name + ".binary_threshold");
                float threshold = 0.5f;
                if (!thresh_str.empty()) {
                    threshold = kg_parse::to_float(thresh_str, "cap.binary_threshold", part_id).value_or(0.5f);
                }
                if (health < threshold) c.any_below_threshold = true;

                auto side = kg.getProperty(part_id, "cap." + cap_name + ".side");
                if (!side.empty()) {
                    p.side_hints[cap_name + "." + side] = health;
                }
            }
        } else {
            // Legacy fallback: type-based categorization
            std::string type = kg.getType(part_id);
            if (type == "Leg") {
                contributions["locomotion"].health_weighted_sum += health;
                contributions["locomotion"].count++;
                if (name.find("left") != std::string::npos) p.left_leg_factor = health;
                else if (name.find("right") != std::string::npos) p.right_leg_factor = health;
            } else if (type == "Arm") {
                contributions["manipulation"].health_weighted_sum += health;
                contributions["manipulation"].count++;
                if (name.find("left") != std::string::npos) p.left_arm_factor = health;
                else if (name.find("right") != std::string::npos) p.right_arm_factor = health;
            } else if (type == "Torso" || type == "Hips" || type == "Abdomen") {
                auto& c = contributions["rotation"];
                c.minimum = std::min(c.minimum, health);
                c.count++;
            } else if (type == "Head") {
                auto& c = contributions["perception"];
                c.minimum = std::min(c.minimum, health);
                c.count++;
            }
        }
    }

    // Aggregate each capability by mode
    for (auto& [cap_name, c] : contributions) {
        std::string mode = kg.getProperty(entity_id, "cap." + cap_name + ".default_mode");
        if (mode.empty()) {
            if (cap_name == "locomotion" || cap_name == "manipulation") mode = "average";
            else if (cap_name == "rotation" || cap_name == "perception") mode = "minimum";
            else mode = "average";
        }

        float expected = 0;
        auto exp_str = kg.getProperty(entity_id, "cap." + cap_name + ".expected_count");
        if (!exp_str.empty()) {
            expected = kg_parse::to_float(exp_str, "cap.expected_count", entity_id).value_or(0.0f);
        }

        float factor = 0;
        if (mode == "average") {
            float divisor = expected > 0 ? expected : static_cast<float>(std::max(c.count, 1));
            factor = c.health_weighted_sum / divisor;
        } else if (mode == "minimum") {
            factor = c.count > 0 ? c.minimum : 1.0f;
        } else if (mode == "binary") {
            factor = (c.count > 0 && !c.any_below_threshold) ? 1.0f : 0.0f;
        } else if (mode == "weighted_sum") {
            auto norm_str = kg.getProperty(entity_id, "cap." + cap_name + ".normalize");
            float normalize = norm_str.empty() ? c.weight_sum
                : kg_parse::to_float(norm_str, "cap.normalize", entity_id).value_or(c.weight_sum);
            factor = normalize > 0 ? c.health_weighted_sum / normalize : 0;
        }

        p.capabilities[cap_name] = std::clamp(factor, 0.0f, 1.0f);
    }

    // Apply cap_disable and cap_modifier effects from response rules.
    // cap_disable wins over cap_modifier. Also applies to capabilities
    // that had no body parts contributing (factor defaults to 0).
    for (const auto& cap_name : disabled_caps) {
        p.capabilities[cap_name] = 0.0f;
    }
    for (const auto& [cap_name, factor] : cap_modifiers) {
        if (disabled_caps.count(cap_name)) continue;  // disable wins
        auto it = p.capabilities.find(cap_name);
        if (it != p.capabilities.end()) {
            it->second = std::clamp(it->second * factor, 0.0f, 1.0f);
        }
    }

    auto get_cap = [&](const std::string& name, float fallback) {
        auto it = p.capabilities.find(name);
        return it != p.capabilities.end() ? it->second : fallback;
    };

    p.locomotion_factor = get_cap("locomotion", 0.0f);
    p.manipulation_factor = get_cap("manipulation", 0.0f);
    p.rotation_factor = get_cap("rotation", 1.0f);
    p.perception_factor = get_cap("perception", 1.0f);

    if (p.side_hints.count("locomotion.left")) p.left_leg_factor = p.side_hints["locomotion.left"];
    if (p.side_hints.count("locomotion.right")) p.right_leg_factor = p.side_hints["locomotion.right"];
    if (p.side_hints.count("manipulation.left")) p.left_arm_factor = p.side_hints["manipulation.left"];
    if (p.side_hints.count("manipulation.right")) p.right_arm_factor = p.side_hints["manipulation.right"];

    std::cout << "[CAP] Entity " << entity_id
              << " reflexes=" << p.reflexes_ms << "ms grit=" << p.grit_W << "W"
              << " loco=" << p.locomotion_factor
              << " manip=" << p.manipulation_factor
              << " rot=" << p.rotation_factor
              << " percep=" << p.perception_factor
              << " speed_cap=" << p.speed_cap;
    if (used_data_driven) std::cout << " [KG-driven]";
    std::cout << std::endl;

    return p;
}
