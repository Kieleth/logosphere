#include "logosphere/kg/ontology_validator.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <stdexcept>
#include <variant>
#include <string>

namespace kg {

namespace {

ValidationResult fail(const std::string& reason) {
    return ValidationResult{false, reason};
}

// Resolve a numeric ref + look up the entity's type, or return a
// failure reason if either step doesn't work.
ValidationResult resolve_type(const EntityRef& ref,
                              const KGModule& kg,
                              std::string& out_type) {
    if (ref.is_symbolic()) {
        return fail("unresolved symbolic ref @" + ref.symbolic
                    + " (host must resolve before validation)");
    }
    if (!kg.exists(ref.id)) {
        return fail("entity " + std::to_string(ref.id) + " does not exist");
    }
    // KGModule doesn't expose a direct getEntityType; the registered
    // type lives in entity.type, accessible via type-iterating queries.
    // Cheapest route: walk every type in the registry, ask
    // findByType, see which one contains our id. For Logotron-scale
    // ontologies (~50 types) this is fine.
    for (const auto& [type_name, def] : kg.getRegistry().entityTypes()) {
        const auto ids = kg.findByType(type_name);
        for (auto id : ids) {
            if (id == ref.id) { out_type = type_name; return ValidationResult{}; }
        }
    }
    return fail("entity " + std::to_string(ref.id) + " has no known type");
}

ValidationResult validate_destroy(const KGOpDestroyEntity& op,
                                  const KGModule& kg) {
    std::string t;
    return resolve_type(op.target, kg, t);  // existence check is enough
}

// Locate the PropertyDef for `prop` on `type` (walking ancestors).
// Returns nullptr if no such property exists. Used by the value /
// range checks below.
const PropertyDef* find_property_def(const OntologyRegistry& ont,
                                     const std::string& type,
                                     const std::string& prop) {
    for (const auto& p : ont.propertiesOf(type)) {
        if (p.name == prop) return &p;
    }
    for (const auto& anc : ont.ancestorsOf(type)) {
        for (const auto& p : ont.propertiesOf(anc)) {
            if (p.name == prop) return &p;
        }
    }
    return nullptr;
}

// Coerce a property value (KG stores strings) against its declared
// value_type. Returns ok + parsed numeric (when applicable) so the
// range check below doesn't reparse.
struct CoerceResult {
    bool   ok = true;
    bool   numeric = false;
    double number  = 0.0;
    std::string reason;
};
CoerceResult coerce_property_value(const std::string& value_type,
                                   const std::string& value) {
    CoerceResult c;
    if (value_type == "string") {
        return c;  // any string ok
    }
    if (value_type == "boolean") {
        if (value == "true" || value == "false" || value == "0" || value == "1") return c;
        c.ok = false; c.reason = "expected boolean (true/false), got '" + value + "'";
        return c;
    }
    if (value_type == "integer") {
        try {
            size_t end = 0;
            long long n = std::stoll(value, &end);
            if (end != value.size()) throw std::invalid_argument("trailing garbage");
            c.numeric = true; c.number = static_cast<double>(n);
            return c;
        } catch (...) {
            c.ok = false; c.reason = "expected integer, got '" + value + "'";
            return c;
        }
    }
    if (value_type == "float") {
        try {
            size_t end = 0;
            double n = std::stod(value, &end);
            if (end != value.size()) throw std::invalid_argument("trailing garbage");
            c.numeric = true; c.number = n;
            return c;
        } catch (...) {
            c.ok = false; c.reason = "expected float, got '" + value + "'";
            return c;
        }
    }
    // Unknown value_type — pass through (the schema may have a
    // type the validator doesn't know about; better to allow than
    // to block legitimate ops).
    return c;
}

ValidationResult validate_create(const KGOpCreateEntity& op,
                                 const OntologyRegistry& ont) {
    if (op.type.empty()) return fail("create_entity: empty type");
    if (!ont.hasEntityType(op.type)) {
        return fail("create_entity: type '" + op.type
                    + "' is not in the ontology");
    }
    if (ont.isAbstract(op.type)) {
        return fail("create_entity: type '" + op.type
                    + "' is abstract; cannot instantiate");
    }
    // Each named property must pass the exact checks set_property gets
    // — otherwise create_entity is a validation bypass (an LLM could
    // seed a 999 m tree that set_property would refuse).
    for (const auto& [k, v] : op.properties) {
        if (auto r = validate_property_write(ont, op.type, k, v); !r.ok) {
            return fail("create_entity: " + r.reason);
        }
    }
    return ValidationResult{};
}

ValidationResult validate_set_property(const KGOpSetProperty& op,
                                       const KGModule& kg,
                                       const OntologyRegistry& ont) {
    std::string t;
    if (auto r = resolve_type(op.target, kg, t); !r.ok) return r;
    if (auto r = validate_property_write(ont, t, op.property, op.value); !r.ok) {
        return fail("set_property: " + r.reason);
    }
    return ValidationResult{};
}

ValidationResult validate_set_relation(const KGOpSetRelation& op,
                                       const KGModule& kg,
                                       const OntologyRegistry& ont) {
    if (op.relation.empty()) return fail("set_relation: empty relation name");
    if (!ont.hasRelationType(op.relation)) {
        return fail("set_relation: relation '" + op.relation
                    + "' is not in the ontology");
    }
    std::string from_t, to_t;
    if (auto r = resolve_type(op.from, kg, from_t); !r.ok) return r;
    if (auto r = resolve_type(op.to,   kg, to_t);   !r.ok) return r;
    if (!ont.isValidRelation(op.relation, from_t, to_t)) {
        return fail("set_relation: '" + op.relation
                    + "' does not accept (" + from_t + " -> " + to_t + ")");
    }
    return ValidationResult{};
}

}  // namespace

ValidationResult validate_property_write(const OntologyRegistry& ont,
                                         const std::string& entity_type,
                                         const std::string& property,
                                         const std::string& value) {
    if (property.empty()) return fail("empty property name");
    if (!ont.hasProperty(entity_type, property)) {
        // Declared open namespaces (e.g. rule.<i>.payload.*) accept any
        // key under their prefix, untyped — see hasPropertyNamespace.
        if (ont.hasPropertyNamespace(entity_type, property)) {
            return ValidationResult{};
        }
        return fail("'" + entity_type + "' has no property '"
                    + property + "'");
    }
    const PropertyDef* def = find_property_def(ont, entity_type, property);
    if (def) {
        auto coerce = coerce_property_value(def->value_type, value);
        if (!coerce.ok) {
            return fail("'" + entity_type + "." + property + "': "
                        + coerce.reason);
        }
        if (coerce.numeric) {
            if (def->has_min && coerce.number < def->min_value) {
                return fail("'" + entity_type + "." + property
                            + "': value " + value
                            + " below schema minimum "
                            + std::to_string(def->min_value));
            }
            if (def->has_max && coerce.number > def->max_value) {
                return fail("'" + entity_type + "." + property
                            + "': value " + value
                            + " above schema maximum "
                            + std::to_string(def->max_value));
            }
        }
    }
    return ValidationResult{};
}

ValidationResult validate_kg_op(const KGOp& op,
                                const KGModule& kg,
                                const OntologyRegistry& ont) {
    return std::visit([&](const auto& concrete) -> ValidationResult {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, KGOpCreateEntity>)
            return validate_create(concrete, ont);
        if constexpr (std::is_same_v<T, KGOpDestroyEntity>)
            return validate_destroy(concrete, kg);
        if constexpr (std::is_same_v<T, KGOpSetProperty>)
            return validate_set_property(concrete, kg, ont);
        if constexpr (std::is_same_v<T, KGOpSetRelation>)
            return validate_set_relation(concrete, kg, ont);
        if constexpr (std::is_same_v<T, KGOpPlayCinematic>) {
            // The host's cinematic registry is the gate for
            // names; the validator's job is just to enforce that
            // any target ref is resolved (numeric, not @alias).
            if (concrete.name.empty()) {
                return fail("play_cinematic: empty name");
            }
            if (concrete.target.is_symbolic()) {
                return fail("play_cinematic: unresolved symbolic target @" +
                            concrete.target.symbolic);
            }
            // INVALID_ENTITY target is OK — many cinematics are
            // standalone (no target needed).
            return ValidationResult{};
        }
        return fail("unknown op variant");
    }, op);
}

}  // namespace kg
