#include "logosphere/kg/ontology_validator.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
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
    out_type = kg.getType(ref.id);
    if (out_type.empty()) {
        return fail("entity " + std::to_string(ref.id) +
                    " has no known type");
    }
    return ValidationResult{};
}

ValidationResult entity_ref_id(const std::string& where,
                               const std::string& value,
                               EntityID& out) {
    unsigned long long parsed = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed == 0 || parsed > std::numeric_limits<EntityID>::max()) {
        return fail(where + ": expected an entity id, got '" + value + "'");
    }
    out = static_cast<EntityID>(parsed);
    return ValidationResult{};
}

bool is_sealed_origin_type(const OntologyRegistry& ont,
                           const std::string& type) {
    return ont.hasFacet(type, "sealed-origin") ||
           ont.isSubtypeOf(type, "SourceLayerContext") ||
           ont.isSubtypeOf(type, "SourceDocumentContext");
}

bool is_seed_owned_type(const OntologyRegistry& ont,
                        const std::string& type) {
    return ont.hasFacet(type, "seed-owned") ||
           ont.isSubtypeOf(type, "SourceLayerContext") ||
           ont.isSubtypeOf(type, "SourceDocumentContext");
}

ValidationResult require_mutable(EntityID id, const KGModule& kg,
                                 const OntologyRegistry& ont,
                                 const ValidationOptions& options) {
    if (options.constructing.count(id)) return ValidationResult{};

    const std::string type = kg.getType(id);
    if (is_sealed_origin_type(ont, type)) {
        return fail("entity " + std::to_string(id) + " (" + type +
                    ") is a sealed origin context");
    }
    if (!ont.hasProperty(type, "origin_context") ||
        !kg.hasProperty(id, "origin_context")) {
        return ValidationResult{};
    }

    EntityID context = INVALID_ENTITY;
    if (auto parsed = entity_ref_id(
            "entity " + std::to_string(id) + " origin_context",
            kg.getProperty(id, "origin_context"), context);
        !parsed.ok) {
        return parsed;
    }
    if (!kg.exists(context)) {
        return fail("entity " + std::to_string(id) +
                    " origin_context references missing entity " +
                    std::to_string(context));
    }
    const std::string context_type = kg.getType(context);
    if (is_sealed_origin_type(ont, context_type)) {
        return fail("entity " + std::to_string(id) + " has sealed origin " +
                    std::to_string(context) + " (" + context_type + ")");
    }
    return ValidationResult{};
}

ValidationResult validate_destroy(const KGOpDestroyEntity& op,
                                  const KGModule& kg,
                                  const OntologyRegistry& ont,
                                  const ValidationOptions& options) {
    std::string t;
    if (auto result = resolve_type(op.target, kg, t); !result.ok) {
        return result;
    }
    return require_mutable(op.target.id, kg, ont, options);
}

// Entity-reference values (class-ranged slots, e.g. TableEntry.outcome
// ranging Outcome): the value must be the id of an existing entity
// whose type is, or is a subtype of, the schema-declared target class.
// Without this, the one validated write path would accept "banana" or
// a Wall where a rule expects an Outcome.
ValidationResult check_entity_ref(const std::string& where,
                                  const PropertyDef& def,
                                  const std::string& value,
                                  const KGModule& kg,
                                  const OntologyRegistry& ont) {
    unsigned long long parsed = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed == 0 ||
        parsed > std::numeric_limits<EntityID>::max()) {
        return fail(where + ": expected an entity id for '" + def.name
                    + "', got '" + value + "'");
    }
    const EntityID id = static_cast<EntityID>(parsed);
    std::string actual_type;
    EntityRef ref;
    ref.id = id;
    if (auto r = resolve_type(ref, kg, actual_type); !r.ok) {
        return fail(where + " '" + def.name + "': " + r.reason);
    }
    if (!def.ref_target.empty() &&
        !ont.isSubtypeOf(actual_type, def.ref_target)) {
        return fail(where + " '" + def.name + "': entity " + value
                    + " is a " + actual_type + ", not a "
                    + def.ref_target);
    }
    return ValidationResult{};
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
                                 const KGModule& kg,
                                 const OntologyRegistry& ont,
                                 const ValidationOptions& options) {
    if (op.type.empty()) return fail("create_entity: empty type");
    if (!ont.hasEntityType(op.type)) {
        return fail("create_entity: type '" + op.type
                    + "' is not in the ontology");
    }
    if (ont.isAbstract(op.type)) {
        return fail("create_entity: type '" + op.type
                    + "' is abstract; cannot instantiate");
    }
    if (is_seed_owned_type(ont, op.type) &&
        options.authority != MutationAuthority::SeedIngestion) {
        return fail("create_entity: type '" + op.type +
                    "' is owned by trusted seed ingestion");
    }
    std::unordered_set<std::string> provided;
    for (const auto& [name, value] : op.properties) {
        (void)value;
        if (!provided.insert(name).second) {
            return fail("create_entity '" + op.type +
                        "': duplicate property '" + name + "'");
        }
    }
    for (const PropertyDef* required :
         ont.requiredPropertiesOf(op.type)) {
        if (required->identifier) continue;
        if (!provided.count(required->name)) {
            return fail("create_entity: missing required property '" +
                        op.type + "." + required->name + "'");
        }
    }
    // Each named property must exist on the type (or an ancestor)
    // AND pass the same value-type + range checks set_property gets
    // — otherwise create_entity is a validation bypass (an LLM could
    // seed a 999 m tree that set_property would refuse).
    for (const auto& [k, v] : op.properties) {
        if (!ont.hasProperty(op.type, k)) {
            return fail("create_entity: '" + op.type
                        + "' has no property '" + k + "'");
        }
        const PropertyDef* def = ont.findProperty(op.type, k);
        if (!def) continue;
        if (def->identifier) {
            return fail("create_entity '" + op.type + "." + k +
                        "': identifier is engine-managed");
        }
        if (def->value_type == "entity_ref") {
            if (auto r = check_entity_ref("create_entity '" + op.type + "'",
                                          *def, v, kg, ont); !r.ok) {
                return r;
            }
            continue;
        }
        auto coerce = coerce_property_value(def->value_type, v);
        if (!coerce.ok) {
            return fail("create_entity '" + op.type + "." + k + "': "
                        + coerce.reason);
        }
        if (coerce.numeric) {
            if (def->has_min && coerce.number < def->min_value) {
                return fail("create_entity '" + op.type + "." + k
                            + "': value " + v + " below schema minimum "
                            + std::to_string(def->min_value));
            }
            if (def->has_max && coerce.number > def->max_value) {
                return fail("create_entity '" + op.type + "." + k
                            + "': value " + v + " above schema maximum "
                            + std::to_string(def->max_value));
            }
        }
    }

    if (options.authority != MutationAuthority::SeedIngestion &&
        ont.isSubtypeOf(op.type, "Cited")) {
        const auto origin = std::find_if(
            op.properties.begin(), op.properties.end(),
            [](const auto& property) {
                return property.first == "origin_context";
            });
        if (origin != op.properties.end()) {
            EntityID context = INVALID_ENTITY;
            if (auto parsed = entity_ref_id(
                    "create_entity '" + op.type + ".origin_context'",
                    origin->second, context);
                !parsed.ok) {
                return parsed;
            }
            if (kg.exists(context) &&
                is_sealed_origin_type(ont, kg.getType(context))) {
                return fail("create_entity: runtime content cannot claim a "
                            "sealed source origin");
            }
        }
    }
    return ValidationResult{};
}

ValidationResult validate_set_property(const KGOpSetProperty& op,
                                       const KGModule& kg,
                                       const OntologyRegistry& ont,
                                       const ValidationOptions& options) {
    std::string t;
    if (auto r = resolve_type(op.target, kg, t); !r.ok) return r;
    if (op.property.empty()) return fail("set_property: empty property name");
    if (!ont.hasProperty(t, op.property)) {
        return fail("set_property: '" + t + "' has no property '"
                    + op.property + "'");
    }

    // Value-type + range checks (C.3). Look up the PropertyDef
    // for the schema-declared value_type + min/max.
    const PropertyDef* def = ont.findProperty(t, op.property);
    if (def && def->identifier) {
        return fail("set_property '" + t + "." + op.property +
                    "': identifier is engine-managed");
    }
    if (def && def->create_only) {
        return fail("set_property '" + t + "." + op.property +
                    "': creation-only property");
    }
    if (auto mutable_result =
            require_mutable(op.target.id, kg, ont, options);
        !mutable_result.ok) {
        return mutable_result;
    }
    if (def && def->value_type == "entity_ref") {
        return check_entity_ref("set_property '" + t + "'", *def, op.value,
                                kg, ont);
    }
    if (def) {
        auto coerce = coerce_property_value(def->value_type, op.value);
        if (!coerce.ok) {
            return fail("set_property '" + t + "." + op.property + "': "
                        + coerce.reason);
        }
        if (coerce.numeric) {
            if (def->has_min && coerce.number < def->min_value) {
                return fail("set_property '" + t + "." + op.property
                            + "': value " + op.value
                            + " below schema minimum "
                            + std::to_string(def->min_value));
            }
            if (def->has_max && coerce.number > def->max_value) {
                return fail("set_property '" + t + "." + op.property
                            + "': value " + op.value
                            + " above schema maximum "
                            + std::to_string(def->max_value));
            }
        }
    }
    return ValidationResult{};
}

ValidationResult validate_set_relation(const KGOpSetRelation& op,
                                       const KGModule& kg,
                                       const OntologyRegistry& ont,
                                       const ValidationOptions& options) {
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
    return require_mutable(op.from.id, kg, ont, options);
}

}  // namespace

ValidationResult validate_kg_op(const KGOp& op,
                                const KGModule& kg,
                                const OntologyRegistry& ont,
                                const ValidationOptions& options) {
    return std::visit([&](const auto& concrete) -> ValidationResult {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, KGOpCreateEntity>)
            return validate_create(concrete, kg, ont, options);
        if constexpr (std::is_same_v<T, KGOpDestroyEntity>)
            return validate_destroy(concrete, kg, ont, options);
        if constexpr (std::is_same_v<T, KGOpSetProperty>)
            return validate_set_property(concrete, kg, ont, options);
        if constexpr (std::is_same_v<T, KGOpSetRelation>)
            return validate_set_relation(concrete, kg, ont, options);
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
