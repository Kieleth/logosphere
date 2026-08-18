#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kg {

class OntologyCollision : public std::runtime_error {
public:
    OntologyCollision(std::string kind, std::string definition,
                      std::string existing_source,
                      std::string incoming_source)
        : std::runtime_error(
              "Ontology " + kind + " collision for '" + definition +
              "': " + existing_source + " conflicts with " +
              incoming_source),
          kind_(std::move(kind)),
          definition_(std::move(definition)),
          existing_source_(std::move(existing_source)),
          incoming_source_(std::move(incoming_source)) {}

    const std::string& kind() const { return kind_; }
    const std::string& definition() const { return definition_; }
    const std::string& existing_source() const {
        return existing_source_;
    }
    const std::string& incoming_source() const {
        return incoming_source_;
    }

private:
    std::string kind_;
    std::string definition_;
    std::string existing_source_;
    std::string incoming_source_;
};

struct EntityTypeDef {
    std::string name;
    std::string parent;  // empty if root type
    bool is_abstract = false;
    // Free-form semantic tags declared in schema YAML
    // (annotations: facets:). The engine defines no facet names;
    // games declare meaning here and query layers filter on it.
    // Explicit per type — no is_a inheritance (see
    // docs/KNOWLEDGE_LAYER.md).
    std::unordered_set<std::string> facets;
    std::string source = "<runtime>";
};

struct RelationTypeDef {
    std::string name;
    std::unordered_set<std::string> valid_source_types;
    std::unordered_set<std::string> valid_target_types;
    std::string source = "<runtime>";
};

enum class PropertyValueKind {
    String,
    Boolean,
    Integer,
    Float,
    EntityRef,
    Enum,
    LegacyDateTime,
};

inline const char* property_value_kind_name(PropertyValueKind kind) {
    switch (kind) {
        case PropertyValueKind::String: return "string";
        case PropertyValueKind::Boolean: return "boolean";
        case PropertyValueKind::Integer: return "integer";
        case PropertyValueKind::Float: return "float";
        case PropertyValueKind::EntityRef: return "entity_ref";
        case PropertyValueKind::Enum: return "enum";
        case PropertyValueKind::LegacyDateTime: return "datetime";
    }
    throw std::invalid_argument("Unknown property value kind");
}

struct EnumTypeDef {
    std::string name;
    std::unordered_set<std::string> members;
    // Schema `annotations: case_insensitive: true`: membership checks
    // fold case, matching consumers that normalize on read (e.g. the
    // interaction loader's to_upper on TransformationTrigger). Default
    // is exact matching — the closed-enum contract.
    bool case_insensitive = false;
    std::string source = "<runtime>";
};

struct PropertyDef {
    std::string name;
    PropertyValueKind value_kind = PropertyValueKind::String;
    bool required = false;
    bool identifier = false;
    // Creation-time identity and lineage cannot be rewritten through the
    // validated mutation path. A changed origin is a new entity, not a set.
    bool create_only = false;

    // Optional range annotations from the schema. The validator
    // honours these when present; if has_min / has_max is false
    // the corresponding bound is "no constraint". Step is reserved
    // for a later iteration (e.g. integer enum-step semantics).
    bool   has_min = false;
    bool   has_max = false;
    double min_value = 0.0;
    double max_value = 0.0;

    // Set only when value_kind == Enum: the exact nominal enum whose
    // members are valid for this property.
    std::string enum_type;

    // Set only when value_kind == EntityRef: the class the
    // referenced entity must be, or be a subtype of. Enforced on
    // the validated write path; empty means unconstrained.
    std::string ref_target;
    std::string source = "<runtime>";
};

/// Runtime ontology registry. The KG is constructed FROM this.
/// Contains all valid entity types, relation types, and property
/// schemas.
///
/// What validates against it, and where (this comment used to claim
/// "every KG write operation validates" — for years that was only true
/// of createEntity; Malleus H1 closed the property hole):
///  - createEntity rejects unknown and abstract types (kg_core.cpp).
///  - setProperty gates every property write — declared-on-type,
///    value-type coercion, schema min/max — through
///    validate_property_write (kg_core.cpp; strict abort by default,
///    KG_GATE_LENIENT=1 to inventory violations without dying).
///  - The LLM KGOp path (ontology_validator.cpp) additionally checks
///    entity existence and relation domain/range before ops apply.
///  - Engine-side createRelation does NOT validate domain/range, and
///    the generated domain/range is vacuous anyway (Malleus H2, open).
///
/// This is the TBox. The KG instances are the ABox.
class OntologyRegistry {
public:
    explicit OntologyRegistry(std::string source = "<runtime>")
        : active_source_(std::move(source)) {
        if (active_source_.empty()) {
            throw std::invalid_argument("Ontology source cannot be empty");
        }
    }

    // --- Type queries ---

    bool hasEntityType(const std::string& type) const {
        return entity_types_.count(type) > 0;
    }

    bool hasEnumType(const std::string& type) const {
        return enum_types_.count(type) > 0;
    }

    bool isEnumMember(const std::string& type,
                      const std::string& member) const {
        auto it = enum_types_.find(type);
        if (it == enum_types_.end()) return false;
        if (it->second.members.count(member) > 0) return true;
        if (!it->second.case_insensitive) return false;
        const std::string folded = fold_upper(member);
        for (const auto& m : it->second.members) {
            if (fold_upper(m) == folded) return true;
        }
        return false;
    }

    // --- Facets (Knowledge layer selection substrate) ---

    bool hasFacet(const std::string& type, const std::string& facet) const {
        auto it = entity_types_.find(type);
        return it != entity_types_.end() && it->second.facets.count(facet) > 0;
    }

    // Sorted for deterministic downstream iteration.
    std::vector<std::string> typesWithFacet(const std::string& facet) const {
        std::vector<std::string> out;
        for (const auto& [name, def] : entity_types_)
            if (def.facets.count(facet)) out.push_back(name);
        std::sort(out.begin(), out.end());
        return out;
    }

    bool isAbstract(const std::string& type) const {
        auto it = entity_types_.find(type);
        return it != entity_types_.end() && it->second.is_abstract;
    }

    bool isSubtypeOf(const std::string& child, const std::string& parent) const {
        if (child == parent) return true;
        auto it = ancestors_.find(child);
        if (it == ancestors_.end()) return false;
        return it->second.count(parent) > 0;
    }

    // --- Relation queries ---

    bool hasRelationType(const std::string& type) const {
        return relation_types_.count(type) > 0;
    }

    bool isValidRelation(const std::string& rel_type,
                         const std::string& source_entity_type,
                         const std::string& target_entity_type) const {
        auto it = relation_types_.find(rel_type);
        if (it == relation_types_.end()) return false;

        const auto& def = it->second;
        bool src_ok = false;
        for (const auto& valid_src : def.valid_source_types) {
            if (isSubtypeOf(source_entity_type, valid_src)) { src_ok = true; break; }
        }
        if (!src_ok) return false;

        bool tgt_ok = false;
        for (const auto& valid_tgt : def.valid_target_types) {
            if (isSubtypeOf(target_entity_type, valid_tgt)) { tgt_ok = true; break; }
        }
        return tgt_ok;
    }

    // --- Property queries ---

    bool hasProperty(const std::string& entity_type, const std::string& prop) const {
        return findProperty(entity_type, prop) != nullptr;
    }

    // Open property namespaces: a declared key PREFIX under which a
    // type accepts any property, untyped. For engine surfaces that are
    // open BY DESIGN — e.g. rule.<i>.payload.* event payload bags the
    // effect system forwards verbatim. Prefer exact slots (typed);
    // namespaces are the escape hatch for genuinely open vocabularies,
    // never a shortcut around declaring known keys.
    bool hasPropertyNamespace(const std::string& entity_type,
                              const std::string& key) const {
        if (check_namespace(entity_type, key)) return true;
        auto anc_it = ancestors_.find(entity_type);
        if (anc_it == ancestors_.end()) return false;
        for (const auto& ancestor : anc_it->second) {
            if (check_namespace(ancestor, key)) return true;
        }
        return false;
    }

    // --- Introspection ---

    const std::unordered_map<std::string, EntityTypeDef>& entityTypes() const { return entity_types_; }
    const std::unordered_map<std::string, RelationTypeDef>& relationTypes() const { return relation_types_; }
    const std::unordered_map<std::string, EnumTypeDef>& enumTypes() const {
        return enum_types_;
    }

    // Properties declared DIRECTLY on this type (does not include
    // inherited properties from ancestors). Use ancestorsOf() to
    // walk the chain when you need the full set.
    const std::vector<PropertyDef>& propertiesOf(const std::string& entity_type) const {
        static const std::vector<PropertyDef> kEmpty;
        auto it = properties_.find(entity_type);
        return it == properties_.end() ? kEmpty : it->second;
    }

    // The PropertyDef for prop on this type, walking ancestors.
    // nullptr when no such property exists anywhere on the chain.
    // Shared by the validator (value kind / range / ref checks) and
    // the seed loader (entity_ref alias resolution).
    const PropertyDef* findProperty(const std::string& entity_type,
                                    const std::string& prop) const {
        for (const PropertyDef* property :
             effective_properties(entity_type)) {
            if (property->name == prop) return property;
        }
        return nullptr;
    }

    // Required properties available on this type, including inherited
    // mixin and parent properties. Sorted by name for deterministic errors.
    std::vector<const PropertyDef*> requiredPropertiesOf(
        const std::string& entity_type) const {
        std::vector<const PropertyDef*> required;
        for (const PropertyDef* property : effective_properties(entity_type)) {
            if (property->required) required.push_back(property);
        }
        return required;
    }

    // The schema identifier available on this type. A valid registry has at
    // most one. KGCore materializes it from EntityID at creation time.
    const PropertyDef* identifierPropertyOf(
        const std::string& entity_type) const {
        const PropertyDef* identifier = nullptr;
        for (const PropertyDef* property : effective_properties(entity_type)) {
            if (!property->identifier) continue;
            if (identifier && identifier->name != property->name) {
                throw std::invalid_argument(
                    "Ontology entity type '" + entity_type +
                    "' inherits multiple identifier properties: '" +
                    identifier->name + "' and '" + property->name + "'");
            }
            identifier = property;
        }
        return identifier;
    }

    // Ancestors of a type (transitive parent chain). Empty if the
    // type is unknown or a root type.
    const std::unordered_set<std::string>& ancestorsOf(const std::string& entity_type) const {
        static const std::unordered_set<std::string> kEmpty;
        auto it = ancestors_.find(entity_type);
        return it == ancestors_.end() ? kEmpty : it->second;
    }

    bool empty() const { return entity_types_.empty(); }

    // Validate every definition that names another entity type. Building a
    // registry is incremental, so this runs at the use and extension
    // boundaries after all layers have been assembled.
    void validateReferences() const {
        for (const auto& [name, def] : entity_types_) {
            if (!def.parent.empty() && !hasEntityType(def.parent)) {
                throw_reference("entity parent", name, def.source,
                                def.parent);
            }
            (void)identifierPropertyOf(name);
        }
        for (const auto& [name, def] : relation_types_) {
            for (const auto& source_type : def.valid_source_types) {
                if (!hasEntityType(source_type)) {
                    throw_reference("relation source", name, def.source,
                                    source_type);
                }
            }
            for (const auto& target_type : def.valid_target_types) {
                if (!hasEntityType(target_type)) {
                    throw_reference("relation target", name, def.source,
                                    target_type);
                }
            }
        }
        for (const auto& [owner, properties] : properties_) {
            for (const auto& property : properties) {
                if (property.value_kind == PropertyValueKind::EntityRef &&
                    !property.ref_target.empty() &&
                    !hasEntityType(property.ref_target)) {
                    throw_reference("property", owner + "." + property.name,
                                    property.source, property.ref_target);
                }
                if (property.value_kind == PropertyValueKind::Enum &&
                    !hasEnumType(property.enum_type)) {
                    throw_reference("property", owner + "." + property.name,
                                    property.source, property.enum_type,
                                    "enum type");
                }
            }
        }
    }

    // --- Extension (additive only) ---

    void extend(const OntologyRegistry& other) {
        // Preflight every definition. A collision must not leave an
        // order-dependent prefix of the incoming registry installed.
        for (const auto& [name, incoming] : other.entity_types_) {
            auto existing = entity_types_.find(name);
            if (existing != entity_types_.end() &&
                !same_entity(existing->second, incoming)) {
                throw_collision("entity type", name, existing->second.source,
                                incoming.source);
            }
        }
        for (const auto& [name, incoming] : other.relation_types_) {
            auto existing = relation_types_.find(name);
            if (existing != relation_types_.end() &&
                !same_relation(existing->second, incoming)) {
                throw_collision("relation type", name,
                                existing->second.source, incoming.source);
            }
        }
        for (const auto& [name, incoming] : other.enum_types_) {
            auto existing = enum_types_.find(name);
            if (existing != enum_types_.end() &&
                !same_enum(existing->second, incoming)) {
                throw_collision("enum type", name, existing->second.source,
                                incoming.source);
            }
        }
        for (const auto& [type, incoming_properties] : other.properties_) {
            for (const auto& incoming : incoming_properties) {
                if (const PropertyDef* existing =
                        direct_property(type, incoming.name);
                    existing && !same_property(*existing, incoming)) {
                    throw_collision("property", type + "." + incoming.name,
                                    existing->source, incoming.source);
                }
            }
        }
        for (const auto& [type, incoming] : other.ancestors_) {
            auto existing = ancestors_.find(type);
            if (existing != ancestors_.end() && existing->second != incoming) {
                throw_collision("ancestor set", type,
                                source_for_entity(type),
                                other.source_for_entity(type));
            }
        }

        OntologyRegistry combined = *this;
        for (const auto& [name, def] : other.entity_types_)
            combined.entity_types_.emplace(name, def);
        for (const auto& [name, def] : other.relation_types_)
            combined.relation_types_.emplace(name, def);
        for (const auto& [name, def] : other.enum_types_)
            combined.enum_types_.emplace(name, def);
        for (const auto& [k, v] : other.properties_) {
            auto& existing = combined.properties_[k];
            for (const auto& p : v) {
                if (!combined.direct_property(k, p.name)) {
                    existing.push_back(p);
                }
            }
        }
        for (const auto& [k, v] : other.property_namespaces_) {
            auto& existing = combined.property_namespaces_[k];
            existing.insert(existing.end(), v.begin(), v.end());
        }
        for (const auto& [type, ancestors] : other.ancestors_)
            combined.ancestors_.emplace(type, ancestors);
        combined.validateReferences();
        *this = std::move(combined);
    }

    // --- Population (used by generated code) ---

    void setSource(std::string source) {
        if (source.empty()) {
            throw std::invalid_argument("Ontology source cannot be empty");
        }
        active_source_ = std::move(source);
    }

    void addEntityType(const std::string& name, const std::string& parent, bool is_abstract) {
        EntityTypeDef incoming{name, parent, is_abstract, {}, active_source_};
        auto [it, inserted] = entity_types_.emplace(name, incoming);
        if (!inserted && !same_entity(it->second, incoming)) {
            throw_collision("entity type", name, it->second.source,
                            incoming.source);
        }
    }

    void addFacets(const std::string& type,
                   const std::unordered_set<std::string>& facets) {
        auto it = entity_types_.find(type);
        if (it != entity_types_.end())
            it->second.facets.insert(facets.begin(), facets.end());
    }

    void addAncestors(const std::string& type, const std::unordered_set<std::string>& ancestors) {
        auto [it, inserted] = ancestors_.emplace(type, ancestors);
        if (!inserted && it->second != ancestors) {
            throw_collision("ancestor set", type, source_for_entity(type),
                            active_source_);
        }
    }

    void addRelationType(const std::string& name,
                         const std::unordered_set<std::string>& sources,
                         const std::unordered_set<std::string>& targets) {
        RelationTypeDef incoming{name, sources, targets, active_source_};
        auto [it, inserted] = relation_types_.emplace(name, incoming);
        if (!inserted && !same_relation(it->second, incoming)) {
            throw_collision("relation type", name, it->second.source,
                            incoming.source);
        }
    }

    void addEnumType(const std::string& name,
                     const std::unordered_set<std::string>& members,
                     bool case_insensitive = false) {
        if (name.empty()) {
            throw std::invalid_argument("Ontology enum type name cannot be empty");
        }
        if (members.empty()) {
            throw std::invalid_argument("Ontology enum type '" + name +
                                        "' has no members");
        }
        for (const auto& member : members) {
            if (member.empty()) {
                throw std::invalid_argument("Ontology enum type '" + name +
                                            "' has an empty member");
            }
        }
        EnumTypeDef incoming{name, members, case_insensitive,
                             active_source_};
        auto [it, inserted] = enum_types_.emplace(name, incoming);
        if (!inserted && !same_enum(it->second, incoming)) {
            throw_collision("enum type", name, it->second.source,
                            incoming.source);
        }
    }

    void addProperty(const std::string& entity_type, const std::string& prop_name,
                     PropertyValueKind value_kind, bool required) {
        require_scalar_property_kind(value_kind, entity_type, prop_name);
        add_property(entity_type,
                     {prop_name, value_kind, required, false, false,
                      false, false,
                      0.0, 0.0, "", "", active_source_});
    }

    void addProperty(const std::string& entity_type,
                     const std::string& prop_name,
                     PropertyValueKind value_kind, bool required,
                     bool create_only) {
        require_scalar_property_kind(value_kind, entity_type, prop_name);
        add_property(entity_type,
                     {prop_name, value_kind, required, false, create_only,
                      false, false, 0.0, 0.0, "", "", active_source_});
    }

    // Overload with optional range annotations. Pass any combination
    // of has_min / has_max flags; the bound for the unset side is
    // ignored.
    void addProperty(const std::string& entity_type, const std::string& prop_name,
                     PropertyValueKind value_kind, bool required,
                     bool has_min, double min_value,
                     bool has_max, double max_value,
                     bool create_only = false) {
        require_scalar_property_kind(value_kind, entity_type, prop_name);
        add_property(entity_type,
                     {prop_name, value_kind, required, false, create_only,
                      has_min, has_max,
                      min_value, max_value, "", "", active_source_});
    }

    void addIdentifierProperty(const std::string& entity_type,
                               const std::string& prop_name,
                               PropertyValueKind value_kind,
                               bool required) {
        require_scalar_property_kind(value_kind, entity_type, prop_name);
        add_property(entity_type,
                     {prop_name, value_kind, required, true, false,
                      false, false,
                      0.0, 0.0, "", "", active_source_});
    }

    void addEnumProperty(const std::string& entity_type,
                         const std::string& prop_name,
                         const std::string& enum_type, bool required,
                         bool create_only = false) {
        if (enum_type.empty()) {
            throw std::invalid_argument("Ontology enum property '" +
                                        entity_type + "." + prop_name +
                                        "' has no enum type");
        }
        add_property(entity_type,
                     {prop_name, PropertyValueKind::Enum, required, false,
                      create_only, false, false, 0.0, 0.0, enum_type, "",
                      active_source_});
    }

    // Entity-reference property (a class-ranged slot in the schema):
    // the value is an entity id whose type must be, or be a subtype
    // of, target_class. Generated code uses this for class ranges.
    void addRefProperty(const std::string& entity_type, const std::string& prop_name,
                        bool required, const std::string& target_class,
                        bool create_only = false) {
        PropertyDef def{prop_name, PropertyValueKind::EntityRef, required,
                        false, create_only, false, false, 0.0, 0.0,
                        "", target_class,
                        active_source_};
        add_property(entity_type, std::move(def));
    }

    void addPropertyNamespace(const std::string& entity_type,
                              const std::string& prefix) {
        property_namespaces_[entity_type].push_back(prefix);
    }

private:
    [[noreturn]] static void throw_collision(
        const std::string& kind, const std::string& definition,
        const std::string& existing_source,
        const std::string& incoming_source) {
        throw OntologyCollision(kind, definition, existing_source,
                                incoming_source);
    }

    [[noreturn]] static void throw_reference(
        const std::string& kind, const std::string& definition,
        const std::string& source, const std::string& target,
        const std::string& target_kind = "entity type") {
        throw std::invalid_argument(
            "Ontology " + kind + " '" + definition + "' from " + source +
            " references unknown " + target_kind + " '" + target + "'");
    }

    static bool same_entity(const EntityTypeDef& a,
                            const EntityTypeDef& b) {
        return a.name == b.name && a.parent == b.parent &&
               a.is_abstract == b.is_abstract && a.facets == b.facets;
    }

    static bool same_relation(const RelationTypeDef& a,
                              const RelationTypeDef& b) {
        return a.name == b.name &&
               a.valid_source_types == b.valid_source_types &&
               a.valid_target_types == b.valid_target_types;
    }

    static bool same_enum(const EnumTypeDef& a, const EnumTypeDef& b) {
        return a.name == b.name && a.members == b.members &&
               a.case_insensitive == b.case_insensitive;
    }

    static std::string fold_upper(const std::string& s) {
        std::string out = s;
        for (char& c : out) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return out;
    }

    static bool same_property(const PropertyDef& a, const PropertyDef& b) {
        return a.name == b.name && a.value_kind == b.value_kind &&
               a.required == b.required &&
               a.identifier == b.identifier &&
               a.create_only == b.create_only &&
               a.has_min == b.has_min &&
               a.has_max == b.has_max && a.min_value == b.min_value &&
               a.max_value == b.max_value && a.enum_type == b.enum_type &&
               a.ref_target == b.ref_target;
    }

    static void require_scalar_property_kind(
        PropertyValueKind kind, const std::string& entity_type,
        const std::string& prop_name) {
        if (kind == PropertyValueKind::EntityRef ||
            kind == PropertyValueKind::Enum) {
            throw std::invalid_argument(
                "Ontology property '" + entity_type + "." + prop_name +
                "' requires its refined registration method");
        }
    }

    std::string source_for_entity(const std::string& type) const {
        auto it = entity_types_.find(type);
        return it == entity_types_.end() ? active_source_ : it->second.source;
    }

    void add_property(const std::string& entity_type, PropertyDef incoming) {
        if (const PropertyDef* existing =
                direct_property(entity_type, incoming.name)) {
            if (!same_property(*existing, incoming)) {
                throw_collision("property",
                                entity_type + "." + incoming.name,
                                existing->source, incoming.source);
            }
            return;
        }
        properties_[entity_type].push_back(std::move(incoming));
    }

    std::vector<const PropertyDef*> effective_properties(
        const std::string& entity_type) const {
        struct OwnedProperty {
            std::string owner;
            const PropertyDef* definition;
        };
        std::unordered_map<std::string, OwnedProperty> by_name;
        auto add_properties = [&](const std::string& owner) {
            auto it = properties_.find(owner);
            if (it == properties_.end()) return;
            for (const auto& property : it->second) {
                auto [selected, inserted] = by_name.emplace(
                    property.name, OwnedProperty{owner, &property});
                if (inserted || selected->second.owner == entity_type) {
                    continue;
                }
                if (isSubtypeOf(owner, selected->second.owner)) {
                    selected->second = {owner, &property};
                }
            }
        };

        // Direct properties override inherited definitions. Among inherited
        // definitions, the nearest refining ancestor wins over its own base.
        add_properties(entity_type);
        std::vector<std::string> ancestors;
        if (auto it = ancestors_.find(entity_type); it != ancestors_.end()) {
            ancestors.assign(it->second.begin(), it->second.end());
            std::sort(ancestors.begin(), ancestors.end());
        }
        for (const auto& ancestor : ancestors) add_properties(ancestor);

        std::vector<const PropertyDef*> out;
        out.reserve(by_name.size());
        for (const auto& [name, property] : by_name)
            out.push_back(property.definition);
        std::sort(out.begin(), out.end(), [](const PropertyDef* a,
                                             const PropertyDef* b) {
            return a->name < b->name;
        });
        return out;
    }

    const PropertyDef* direct_property(const std::string& entity_type,
                                       const std::string& prop) const {
        auto it = properties_.find(entity_type);
        if (it == properties_.end()) return nullptr;
        for (const auto& p : it->second) {
            if (p.name == prop) return &p;
        }
        return nullptr;
    }

    bool check_namespace(const std::string& entity_type, const std::string& key) const {
        auto it = property_namespaces_.find(entity_type);
        if (it == property_namespaces_.end()) return false;
        for (const auto& prefix : it->second) {
            if (key.size() > prefix.size() &&
                key.compare(0, prefix.size(), prefix) == 0) return true;
        }
        return false;
    }

    std::unordered_map<std::string, EntityTypeDef> entity_types_;
    std::unordered_map<std::string, RelationTypeDef> relation_types_;
    std::unordered_map<std::string, EnumTypeDef> enum_types_;
    std::unordered_map<std::string, std::vector<PropertyDef>> properties_;
    std::unordered_map<std::string, std::unordered_set<std::string>> ancestors_;
    std::unordered_map<std::string, std::vector<std::string>> property_namespaces_;
    std::string active_source_;
};

} // namespace kg
