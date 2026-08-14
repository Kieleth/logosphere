#include "logosphere/kg/ontology_validator.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/finite_float.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/qualified_reference.h"

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

bool is_ontology_meta_type(const OntologyRegistry& ont,
                           const std::string& type) {
    return ont.isSubtypeOf(type, "OntologyMetaEntity") ||
           ont.isSubtypeOf(type, "OntologyMetaContext");
}

ValidationResult require_mutable(EntityID id, const KGModule& kg,
                                 const OntologyRegistry& ont,
                                 const ValidationOptions& options) {
    const std::string type = kg.getType(id);
    if (is_ontology_meta_type(ont, type)) {
        if (options.authority ==
            MutationAuthority::OntologyMaterialization) {
            return ValidationResult{};
        }
        return fail("entity " + std::to_string(id) + " (" + type +
                    ") belongs to the immutable ontology meta-graph");
    }
    if (options.constructing.count(id)) return ValidationResult{};
    if (is_sealed_origin_type(ont, type)) {
        return fail("entity " + std::to_string(id) + " (" + type +
                    ") is a sealed origin context");
    }
    for (const std::string context_property :
         {"origin_context", "identity_context"}) {
        if (!ont.hasProperty(type, context_property) ||
            !kg.hasProperty(id, context_property)) {
            continue;
        }
        EntityID context = INVALID_ENTITY;
        if (auto parsed = entity_ref_id(
                "entity " + std::to_string(id) + " " + context_property,
                kg.getProperty(id, context_property), context);
            !parsed.ok) {
            return parsed;
        }
        if (!kg.exists(context)) {
            return fail("entity " + std::to_string(id) + " " +
                        context_property + " references missing entity " +
                        std::to_string(context));
        }
        const std::string context_type = kg.getType(context);
        if (is_sealed_origin_type(ont, context_type)) {
            const std::string label = context_property == "origin_context"
                                          ? "sealed origin"
                                          : "sealed identity context";
            return fail("entity " + std::to_string(id) + " has " + label +
                        " " + std::to_string(context) + " (" +
                        context_type + ")");
        }
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
// value kind. Returns ok + parsed numeric (when applicable) so the
// range check below doesn't reparse.
struct CoerceResult {
    bool   ok = true;
    bool   numeric = false;
    double number  = 0.0;
    std::string reason;
};
CoerceResult coerce_property_value(PropertyValueKind value_kind,
                                   const std::string& value) {
    CoerceResult c;
    switch (value_kind) {
        case PropertyValueKind::String:
        case PropertyValueKind::LegacyDateTime:
            return c;

        case PropertyValueKind::Boolean:
            if (value == "true" || value == "false" || value == "0" ||
                value == "1") {
                return c;
            }
            c.ok = false;
            c.reason = "expected boolean (true/false), got '" + value + "'";
            return c;

        case PropertyValueKind::Integer:
            try {
                size_t end = 0;
                const long long n = std::stoll(value, &end);
                if (end != value.size()) {
                    throw std::invalid_argument("trailing garbage");
                }
                c.numeric = true;
                c.number = static_cast<double>(n);
                return c;
            } catch (...) {
                c.ok = false;
                c.reason = "expected integer, got '" + value + "'";
                return c;
            }

        case PropertyValueKind::Float:
            if (const auto parsed = parse_finite_binary64(value);
                parsed.ok) {
                c.numeric = true;
                c.number = parsed.value;
                return c;
            } else {
                c.ok = false;
                c.reason = parsed.error;
                return c;
            }

        case PropertyValueKind::EntityRef:
        case PropertyValueKind::Enum:
            c.ok = false;
            c.reason = "internal error: refined property kind reached scalar coercion";
            return c;
    }
    c.ok = false;
    c.reason = "unknown property value kind";
    return c;
}

ValidationResult validate_property_value(const std::string& where,
                                         const PropertyDef& def,
                                         const std::string& value,
                                         const KGModule& kg,
                                         const OntologyRegistry& ont) {
    if (def.value_kind == PropertyValueKind::EntityRef) {
        return check_entity_ref(where, def, value, kg, ont);
    }
    if (def.value_kind == PropertyValueKind::Enum) {
        if (!ont.hasEnumType(def.enum_type)) {
            return fail(where + ": property '" + def.name +
                        "' references unknown enum type '" + def.enum_type +
                        "'");
        }
        if (!ont.isEnumMember(def.enum_type, value)) {
            return fail(where + ": value '" + value + "' is not a member of " +
                        "enum '" + def.enum_type + "' for property '" +
                        def.name + "'");
        }
        return ValidationResult{};
    }

    const auto coerce = coerce_property_value(def.value_kind, value);
    if (!coerce.ok) {
        return fail(where + ": " + coerce.reason);
    }
    if (coerce.numeric) {
        if (def.has_min && coerce.number < def.min_value) {
            return fail(where + ": value " + value +
                        " below schema minimum " +
                        std::to_string(def.min_value));
        }
        if (def.has_max && coerce.number > def.max_value) {
            return fail(where + ": value " + value +
                        " above schema maximum " +
                        std::to_string(def.max_value));
        }
    }
    return ValidationResult{};
}

const std::string* create_property(const KGOpCreateEntity& op,
                                   const std::string& name) {
    const auto found = std::find_if(
        op.properties.begin(), op.properties.end(),
        [&](const auto& property) { return property.first == name; });
    return found == op.properties.end() ? nullptr : &found->second;
}

ValidationResult validate_portable_identity(const KGOpCreateEntity& op,
                                            const KGModule& kg,
                                            const OntologyRegistry& ont) {
    if (ont.isSubtypeOf(op.type, "KnowledgeContext")) {
        const std::string* context_key = create_property(op, "context_key");
        if (!context_key || context_key->empty()) {
            return fail("create_entity '" + op.type +
                        "': empty context_key");
        }
        try {
            (void)encode_qualified_reference_segment(*context_key);
        } catch (const std::invalid_argument& error) {
            return fail("create_entity '" + op.type +
                        "': invalid context_key: " + error.what());
        }
        for (EntityID id : kg.findByProperty("context_key", *context_key)) {
            if (ont.isSubtypeOf(kg.getType(id), "KnowledgeContext")) {
                return fail("create_entity '" + op.type +
                            "': duplicate KnowledgeContext key '" +
                            *context_key + "'");
            }
        }
    }

    if (!ont.isSubtypeOf(op.type, "Addressable")) {
        return ValidationResult{};
    }
    const std::string* identity_context =
        create_property(op, "identity_context");
    const std::string* entity_key = create_property(op, "entity_key");
    if (!identity_context || identity_context->empty()) {
        return fail("create_entity '" + op.type +
                    "': empty identity_context");
    }
    if (!entity_key || entity_key->empty()) {
        return fail("create_entity '" + op.type + "': empty entity_key");
    }
    try {
        (void)encode_qualified_reference_segment(*entity_key);
    } catch (const std::invalid_argument& error) {
        return fail("create_entity '" + op.type +
                    "': invalid entity_key: " + error.what());
    }
    for (EntityID id : kg.findByType(op.type)) {
        if (kg.getProperty(id, "identity_context") == *identity_context &&
            kg.getProperty(id, "entity_key") == *entity_key) {
            return fail("create_entity '" + op.type +
                        "': duplicate Addressable identity (" +
                        *identity_context + ", " + op.type + ", " +
                        *entity_key + ")");
        }
    }
    return ValidationResult{};
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
    if (is_ontology_meta_type(ont, op.type) &&
        options.authority !=
            MutationAuthority::OntologyMaterialization) {
        return fail("create_entity: type '" + op.type +
                    "' is owned by engine-owned ontology materialization");
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
    // Each named property must pass the exact checks set_property gets
    // — otherwise create_entity is a validation bypass (an LLM could
    // seed a 999 m tree that set_property would refuse). The shared
    // gate (validate_property_write) is namespace-aware and runs the
    // declared-on-type + scalar/enum checks; the KG-backed checks
    // (entity-ref existence, identifier protection) layer on top.
    for (const auto& [k, v] : op.properties) {
        if (auto r = validate_property_write(ont, op.type, k, v); !r.ok) {
            return fail("create_entity: " + r.reason);
        }
        const PropertyDef* def = ont.findProperty(op.type, k);
        if (!def) continue;  // namespace-accepted key: untyped by design
        if (def->identifier) {
            return fail("create_entity '" + op.type + "." + k +
                        "': identifier is engine-managed");
        }
        if (auto result = validate_property_value(
                "create_entity '" + op.type + "." + k + "'", *def, v, kg,
                ont);
            !result.ok) {
            return result;
        }
    }

    if (auto identity = validate_portable_identity(op, kg, ont);
        !identity.ok) {
        return identity;
    }

    if (options.authority != MutationAuthority::SeedIngestion &&
        ont.isSubtypeOf(op.type, "Addressable")) {
        const std::string* identity_context =
            create_property(op, "identity_context");
        if (identity_context) {
            EntityID context = INVALID_ENTITY;
            if (auto parsed = entity_ref_id(
                    "create_entity '" + op.type + ".identity_context'",
                    *identity_context, context);
                !parsed.ok) {
                return parsed;
            }
            if (kg.exists(context) &&
                is_sealed_origin_type(ont, kg.getType(context))) {
                return fail("create_entity: runtime addressable content "
                            "cannot claim a sealed identity context");
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
    // Shared gate first: the same namespace-aware declared-on-type +
    // value checks KGCore::setProperty runs — the two paths cannot
    // drift. Covers empty name, undeclared key (unless under a
    // declared open namespace), scalar coercion, enum membership,
    // schema min/max.
    if (auto r = validate_property_write(ont, t, op.property, op.value);
        !r.ok) {
        return fail("set_property: " + r.reason);
    }

    // KG-backed checks layer on top: mutability, identifier
    // protection, create_only, entity-ref existence (C.3).
    const PropertyDef* def = ont.findProperty(t, op.property);
    if (is_ontology_meta_type(ont, t)) {
        if (auto immutable =
                require_mutable(op.target.id, kg, ont, options);
            !immutable.ok) {
            return immutable;
        }
    }
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
    if (def) {
        return validate_property_value(
            "set_property '" + t + "." + op.property + "'", *def,
            op.value, kg, ont);
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
    const PropertyDef* def = ont.findProperty(entity_type, property);
    if (def) {
        if (def->value_kind == PropertyValueKind::Enum) {
            if (!ont.hasEnumType(def->enum_type) ||
                !ont.isEnumMember(def->enum_type, value)) {
                return fail("'" + entity_type + "." + property
                            + "': value '" + value
                            + "' is not a member of enum '"
                            + def->enum_type + "'");
            }
            return ValidationResult{};
        }
        if (def->value_kind == PropertyValueKind::EntityRef) {
            // Existence + subtype checks need the KG; the KGOp paths
            // run them via validate_property_value. This KG-less door
            // has nothing further to check.
            return ValidationResult{};
        }
        auto coerce = coerce_property_value(def->value_kind, value);
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
