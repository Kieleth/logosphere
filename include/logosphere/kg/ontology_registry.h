#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace kg {

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
};

struct RelationTypeDef {
    std::string name;
    std::unordered_set<std::string> valid_source_types;
    std::unordered_set<std::string> valid_target_types;
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
};

/// Runtime ontology registry. The KG is constructed FROM this.
/// Contains all valid entity types, relation types, and property
/// schemas. Every KG write operation validates against this registry
/// as a precondition — invalid data never materializes.
///
/// This is the TBox. The KG instances are the ABox.
class OntologyRegistry {
public:
    OntologyRegistry() = default;

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
        for (const auto& [k, v] : other.entity_types_) entity_types_[k] = v;
        for (const auto& [k, v] : other.relation_types_) relation_types_[k] = v;
        for (const auto& [k, v] : other.properties_) {
            auto& existing = properties_[k];
            for (const auto& p : v) existing.push_back(p);
        }
        for (const auto& [k, v] : other.ancestors_) {
            ancestors_[k].insert(v.begin(), v.end());
        }
    }

    // --- Population (used by generated code) ---

    void addEntityType(const std::string& name, const std::string& parent, bool is_abstract) {
        entity_types_[name] = {name, parent, is_abstract, {}};
    }

    void addFacets(const std::string& type,
                   const std::unordered_set<std::string>& facets) {
        auto it = entity_types_.find(type);
        if (it != entity_types_.end())
            it->second.facets.insert(facets.begin(), facets.end());
    }

    void addAncestors(const std::string& type, const std::unordered_set<std::string>& ancestors) {
        ancestors_[type] = ancestors;
    }

    void addRelationType(const std::string& name,
                         const std::unordered_set<std::string>& sources,
                         const std::unordered_set<std::string>& targets) {
        relation_types_[name] = {name, sources, targets};
    }

    void addProperty(const std::string& entity_type, const std::string& prop_name,
                     const std::string& value_type, bool required) {
        properties_[entity_type].push_back({prop_name, value_type, required,
                                            false, false, 0.0, 0.0});
    }

    // Overload with optional range annotations. Pass any combination
    // of has_min / has_max flags; the bound for the unset side is
    // ignored.
    void addProperty(const std::string& entity_type, const std::string& prop_name,
                     const std::string& value_type, bool required,
                     bool has_min, double min_value,
                     bool has_max, double max_value) {
        properties_[entity_type].push_back({prop_name, value_type, required,
                                            has_min, has_max, min_value, max_value});
    }

    // Entity-reference property (a class-ranged slot in the schema):
    // the value is an entity id whose type must be, or be a subtype
    // of, target_class. Generated code uses this for class ranges.
    void addRefProperty(const std::string& entity_type, const std::string& prop_name,
                        bool required, const std::string& target_class) {
        PropertyDef def{prop_name, "entity_ref", required,
                        false, false, 0.0, 0.0};
        def.ref_target = target_class;
        properties_[entity_type].push_back(def);
    }

private:
    bool check_property(const std::string& entity_type, const std::string& prop) const {
        auto it = properties_.find(entity_type);
        if (it == properties_.end()) return false;
        for (const auto& p : it->second) {
            if (p.name == prop) return true;
        }
        return false;
    }

    std::unordered_map<std::string, EntityTypeDef> entity_types_;
    std::unordered_map<std::string, RelationTypeDef> relation_types_;
    std::unordered_map<std::string, std::vector<PropertyDef>> properties_;
    std::unordered_map<std::string, std::unordered_set<std::string>> ancestors_;
};

} // namespace kg
