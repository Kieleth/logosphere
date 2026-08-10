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

struct PropertyDef {
    std::string name;
    std::string value_type;  // "string", "float", "integer", "boolean"
    bool required = false;

    // Optional range annotations from the schema. The validator
    // honours these when present; if has_min / has_max is false
    // the corresponding bound is "no constraint". Step is reserved
    // for a later iteration (e.g. integer enum-step semantics).
    bool   has_min = false;
    bool   has_max = false;
    double min_value = 0.0;
    double max_value = 0.0;

    // Set only when value_type == "entity_ref": the class the
    // referenced entity must be, or be a subtype of. Enforced on
    // the validated write path; empty means unconstrained.
    std::string ref_target;
    std::string source = "<runtime>";
};

/// Runtime ontology registry. The KG is constructed FROM this.
/// Contains all valid entity types, relation types, and property
/// schemas. Every KG write operation validates against this registry
/// as a precondition — invalid data never materializes.
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
        if (check_property(entity_type, prop)) return true;
        auto anc_it = ancestors_.find(entity_type);
        if (anc_it == ancestors_.end()) return false;
        for (const auto& ancestor : anc_it->second) {
            if (check_property(ancestor, prop)) return true;
        }
        return false;
    }

    // --- Introspection ---

    const std::unordered_map<std::string, EntityTypeDef>& entityTypes() const { return entity_types_; }
    const std::unordered_map<std::string, RelationTypeDef>& relationTypes() const { return relation_types_; }

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
    // Shared by the validator (value_type / range / ref checks) and
    // the seed loader (entity_ref alias resolution).
    const PropertyDef* findProperty(const std::string& entity_type,
                                    const std::string& prop) const {
        if (const PropertyDef* p = direct_property(entity_type, prop)) return p;
        auto anc_it = ancestors_.find(entity_type);
        if (anc_it == ancestors_.end()) return nullptr;
        for (const auto& ancestor : anc_it->second) {
            if (const PropertyDef* p = direct_property(ancestor, prop)) return p;
        }
        return nullptr;
    }

    // Ancestors of a type (transitive parent chain). Empty if the
    // type is unknown or a root type.
    const std::unordered_set<std::string>& ancestorsOf(const std::string& entity_type) const {
        static const std::unordered_set<std::string> kEmpty;
        auto it = ancestors_.find(entity_type);
        return it == ancestors_.end() ? kEmpty : it->second;
    }

    bool empty() const { return entity_types_.empty(); }

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

        for (const auto& [name, def] : other.entity_types_)
            entity_types_.emplace(name, def);
        for (const auto& [name, def] : other.relation_types_)
            relation_types_.emplace(name, def);
        for (const auto& [k, v] : other.properties_) {
            auto& existing = properties_[k];
            for (const auto& p : v) {
                if (!direct_property(k, p.name)) existing.push_back(p);
            }
        }
        for (const auto& [type, ancestors] : other.ancestors_)
            ancestors_.emplace(type, ancestors);
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

    void addProperty(const std::string& entity_type, const std::string& prop_name,
                     const std::string& value_type, bool required) {
        add_property(entity_type,
                     {prop_name, value_type, required, false, false,
                      0.0, 0.0, "", active_source_});
    }

    // Overload with optional range annotations. Pass any combination
    // of has_min / has_max flags; the bound for the unset side is
    // ignored.
    void addProperty(const std::string& entity_type, const std::string& prop_name,
                     const std::string& value_type, bool required,
                     bool has_min, double min_value,
                     bool has_max, double max_value) {
        add_property(entity_type,
                     {prop_name, value_type, required, has_min, has_max,
                      min_value, max_value, "", active_source_});
    }

    // Entity-reference property (a class-ranged slot in the schema):
    // the value is an entity id whose type must be, or be a subtype
    // of, target_class. Generated code uses this for class ranges.
    void addRefProperty(const std::string& entity_type, const std::string& prop_name,
                        bool required, const std::string& target_class) {
        PropertyDef def{prop_name, "entity_ref", required,
                        false, false, 0.0, 0.0, target_class,
                        active_source_};
        add_property(entity_type, std::move(def));
    }

private:
    [[noreturn]] static void throw_collision(
        const std::string& kind, const std::string& definition,
        const std::string& existing_source,
        const std::string& incoming_source) {
        throw OntologyCollision(kind, definition, existing_source,
                                incoming_source);
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

    static bool same_property(const PropertyDef& a, const PropertyDef& b) {
        return a.name == b.name && a.value_type == b.value_type &&
               a.required == b.required && a.has_min == b.has_min &&
               a.has_max == b.has_max && a.min_value == b.min_value &&
               a.max_value == b.max_value && a.ref_target == b.ref_target;
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

    bool check_property(const std::string& entity_type, const std::string& prop) const {
        return direct_property(entity_type, prop) != nullptr;
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

    std::unordered_map<std::string, EntityTypeDef> entity_types_;
    std::unordered_map<std::string, RelationTypeDef> relation_types_;
    std::unordered_map<std::string, std::vector<PropertyDef>> properties_;
    std::unordered_map<std::string, std::unordered_set<std::string>> ancestors_;
    std::string active_source_;
};

} // namespace kg
