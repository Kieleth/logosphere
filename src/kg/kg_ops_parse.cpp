#include "logosphere/kg/kg_ops_parse.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <stdexcept>
#include <variant>
#include <string>

namespace kg {

const char* kg_op_kind_name(const KGOp& op) {
    return std::visit([](const auto& v) -> const char* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, KGOpCreateEntity>)  return "create_entity";
        if constexpr (std::is_same_v<T, KGOpDestroyEntity>) return "destroy_entity";
        if constexpr (std::is_same_v<T, KGOpSetProperty>)   return "set_property";
        if constexpr (std::is_same_v<T, KGOpSetRelation>)   return "set_relation";
        if constexpr (std::is_same_v<T, KGOpPlayCinematic>) return "play_cinematic";
        return "unknown";
    }, op);
}

namespace {

using nlohmann::json;

// Parse an EntityRef from a JSON value. Accepts:
//   - string starting with '@' → symbolic alias (without the @)
//   - string of digits         → numeric id
//   - integer                  → numeric id
EntityRef parse_entity_ref(const json& v, std::string& warn_out) {
    EntityRef out;
    if (v.is_number_integer()) {
        out.id = static_cast<EntityID>(v.get<long long>());
        return out;
    }
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        if (!s.empty() && s[0] == '@') {
            // Strict like the "as" binder: a bare "@" would
            // otherwise decay into a ref that is neither symbolic
            // nor numeric and slip past resolution.
            if (s.size() < 2) {
                warn_out = "entity ref alias must be '@alias' with a "
                           "non-empty name";
                return out;
            }
            out.symbolic = s.substr(1);
            return out;
        }
        try {
            out.id = static_cast<EntityID>(std::stoll(s));
            return out;
        } catch (...) {
            warn_out = "entity ref must be @alias, integer, or stringified id; got '" + s + "'";
            return out;
        }
    }
    warn_out = "entity ref must be string or integer";
    return out;
}

// Convert any JSON scalar to its string form so it can land in the
// KG (which stores all properties as strings). Numbers go through
// json::dump so we don't lose precision via stringstream.
std::string scalar_to_string(const json& v) {
    if (v.is_string())  return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number())  return v.dump();  // preserves integer/float form
    if (v.is_null())    return "";
    return v.dump();  // arrays/objects: pass through as compact JSON
}

bool parse_create_entity(const json& obj, KGOp& out, std::string& warn) {
    if (!obj.contains("type") || !obj.at("type").is_string()) {
        warn = "create_entity missing 'type'";
        return false;
    }
    KGOpCreateEntity ce;
    ce.type = obj.at("type").get<std::string>();
    if (obj.contains("properties")) {
        const auto& props = obj.at("properties");
        if (!props.is_object()) {
            warn = "create_entity 'properties' must be object";
            return false;
        }
        for (auto it = props.begin(); it != props.end(); ++it) {
            ce.properties.emplace_back(it.key(), scalar_to_string(it.value()));
        }
    }
    // Optional symbolic binder. Strict: when present it must be a
    // string of the form "@alias" with a non-empty name - anything
    // else is refused rather than normalized, so a seed file with a
    // mistyped binder fails loudly instead of silently binding
    // nothing.
    if (obj.contains("as")) {
        const auto& as = obj.at("as");
        if (!as.is_string()) {
            warn = "create_entity 'as' must be a string";
            return false;
        }
        const std::string s = as.get<std::string>();
        if (s.size() < 2 || s[0] != '@') {
            warn = "create_entity 'as' must be '@alias' with a non-empty "
                   "name, got '" + s + "'";
            return false;
        }
        ce.as = s.substr(1);
    }
    out = std::move(ce);
    return true;
}

bool parse_destroy_entity(const json& obj, KGOp& out, std::string& warn) {
    if (!obj.contains("target")) {
        warn = "destroy_entity missing 'target'";
        return false;
    }
    KGOpDestroyEntity de;
    std::string ref_warn;
    de.target = parse_entity_ref(obj.at("target"), ref_warn);
    if (!ref_warn.empty()) {
        warn = "destroy_entity target: " + ref_warn;
        return false;
    }
    out = std::move(de);
    return true;
}

bool parse_set_property(const json& obj, KGOp& out, std::string& warn) {
    if (!obj.contains("target") || !obj.contains("property") ||
        !obj.contains("value")) {
        warn = "set_property requires target, property, value";
        return false;
    }
    if (!obj.at("property").is_string()) {
        warn = "set_property 'property' must be string";
        return false;
    }
    KGOpSetProperty sp;
    std::string ref_warn;
    sp.target = parse_entity_ref(obj.at("target"), ref_warn);
    if (!ref_warn.empty()) {
        warn = "set_property target: " + ref_warn;
        return false;
    }
    sp.property = obj.at("property").get<std::string>();
    sp.value    = scalar_to_string(obj.at("value"));
    out = std::move(sp);
    return true;
}

bool parse_play_cinematic(const json& obj, KGOp& out, std::string& warn) {
    if (!obj.contains("name") || !obj.at("name").is_string()) {
        warn = "play_cinematic missing 'name' string";
        return false;
    }
    KGOpPlayCinematic pc;
    pc.name = obj.at("name").get<std::string>();
    if (obj.contains("target")) {
        std::string ref_warn;
        pc.target = parse_entity_ref(obj.at("target"), ref_warn);
        if (!ref_warn.empty()) {
            warn = "play_cinematic target: " + ref_warn;
            return false;
        }
    }
    if (obj.contains("params")) {
        const auto& params = obj.at("params");
        if (!params.is_object()) {
            warn = "play_cinematic 'params' must be object";
            return false;
        }
        for (auto it = params.begin(); it != params.end(); ++it) {
            pc.params.emplace_back(it.key(), scalar_to_string(it.value()));
        }
    }
    out = std::move(pc);
    return true;
}

bool parse_set_relation(const json& obj, KGOp& out, std::string& warn) {
    if (!obj.contains("from") || !obj.contains("to") ||
        !obj.contains("relation")) {
        warn = "set_relation requires from, to, relation";
        return false;
    }
    if (!obj.at("relation").is_string()) {
        warn = "set_relation 'relation' must be string";
        return false;
    }
    KGOpSetRelation sr;
    std::string ref_warn;
    sr.from = parse_entity_ref(obj.at("from"), ref_warn);
    if (!ref_warn.empty()) {
        warn = "set_relation from: " + ref_warn;
        return false;
    }
    sr.to = parse_entity_ref(obj.at("to"), ref_warn);
    if (!ref_warn.empty()) {
        warn = "set_relation to: " + ref_warn;
        return false;
    }
    sr.relation = obj.at("relation").get<std::string>();
    out = std::move(sr);
    return true;
}

}  // namespace

KGOpParseResult parse_kg_ops(const std::string& json_text) {
    KGOpParseResult result;

    json root;
    try {
        root = json::parse(json_text);
    } catch (const std::exception& e) {
        result.parse_error = std::string("invalid JSON: ") + e.what();
        return result;
    }

    // Accept either {"ops":[...]} or a bare [...] for flexibility.
    const json* ops_array = nullptr;
    if (root.is_object() && root.contains("ops") && root.at("ops").is_array()) {
        ops_array = &root.at("ops");
    } else if (root.is_array()) {
        ops_array = &root;
    } else {
        // Not an error per se — caller may have asked us to parse a
        // response that uses the v0.9 "mutations" shape. Surface as
        // a soft warning so the caller knows to fall back, not as
        // a fatal.
        result.warnings.push_back(
            "no 'ops' array found at root (response may use legacy shape)");
        return result;
    }

    for (size_t i = 0; i < ops_array->size(); ++i) {
        const auto& entry = (*ops_array)[i];
        if (!entry.is_object()) {
            result.warnings.push_back(
                "ops[" + std::to_string(i) + "] is not an object");
            continue;
        }
        if (!entry.contains("op") || !entry.at("op").is_string()) {
            result.warnings.push_back(
                "ops[" + std::to_string(i) + "] missing 'op' string");
            continue;
        }
        std::string op_kind = entry.at("op").get<std::string>();

        KGOp parsed;
        std::string warn;
        bool ok = false;
        if      (op_kind == "create_entity")  ok = parse_create_entity(entry,  parsed, warn);
        else if (op_kind == "destroy_entity") ok = parse_destroy_entity(entry, parsed, warn);
        else if (op_kind == "set_property")   ok = parse_set_property(entry,   parsed, warn);
        else if (op_kind == "set_relation")   ok = parse_set_relation(entry,   parsed, warn);
        else if (op_kind == "play_cinematic") ok = parse_play_cinematic(entry, parsed, warn);
        else {
            result.warnings.push_back(
                "ops[" + std::to_string(i) + "] unknown op kind: " + op_kind);
            continue;
        }

        if (!ok) {
            result.warnings.push_back(
                "ops[" + std::to_string(i) + "]: " + warn);
            continue;
        }
        result.ops.push_back(std::move(parsed));
    }

    return result;
}

}  // namespace kg
