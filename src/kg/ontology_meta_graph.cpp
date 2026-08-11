#include "logosphere/kg/ontology_meta_graph.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/qualified_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kg {
namespace {

constexpr const char* kContextKey = "ontology-meta:active";
constexpr const char* kContextAlias =
    "_logosphere_ontology_meta_context";

struct MetaLink {
    std::string relation;
    std::string target_key;
};

struct MetaNode {
    std::string key;
    std::string type;
    std::vector<std::pair<std::string, std::string>> properties;
    std::vector<MetaLink> links;
};

std::string canonical(QualifiedReferenceKind kind,
                      std::initializer_list<std::string> segments) {
    QualifiedReference reference;
    reference.kind = kind;
    reference.segments.assign(segments.begin(), segments.end());
    return format_qualified_reference(reference);
}

const char* boolean(bool value) {
    return value ? "true" : "false";
}

std::string finite_double(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "Ontology property bound must be finite");
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10)
           << value;
    return stream.str();
}

std::vector<std::pair<std::string, std::string>> common_properties(
    const std::string& key, const std::string& name) {
    return {{"qualified_key", key}, {"ontology_name", name}};
}

EntityRef symbolic(const std::string& alias) {
    return EntityRef{INVALID_ENTITY, alias};
}

bool preflight_vocabulary(const OntologyRegistry& ontology,
                          std::string& error) {
    static constexpr std::array<const char*, 9> kTypes{{
        "OntologyMetaEntity",
        "OntologyMetaContext",
        "OntologyClassMeta",
        "OntologyPropertyMeta",
        "OntologyRelationMeta",
        "OntologyFacetMeta",
        "OntologyValueKindMeta",
        "OntologyEnumMeta",
        "OntologyEnumMemberMeta",
    }};
    static constexpr std::array<const char*, 11> kRelations{{
        "ONTOLOGY_META_CONTAINS",
        "ONTOLOGY_CLASS_DIRECT_PARENT",
        "ONTOLOGY_CLASS_HAS_FACET",
        "ONTOLOGY_PROPERTY_DECLARING_CLASS",
        "ONTOLOGY_PROPERTY_VALUE_KIND",
        "ONTOLOGY_PROPERTY_REFERENCE_TARGET",
        "ONTOLOGY_PROPERTY_ENUM_TYPE",
        "ONTOLOGY_RELATION_VALID_SOURCE",
        "ONTOLOGY_RELATION_VALID_TARGET",
        "ONTOLOGY_ENUM_HAS_MEMBER",
        "ONTOLOGY_ENUM_MEMBER_OWNER",
    }};
    for (const char* type : kTypes) {
        if (!ontology.hasEntityType(type)) {
            error = "ontology meta-graph requires entity type '" +
                    std::string(type) + "'";
            return false;
        }
    }
    for (const char* relation : kRelations) {
        if (!ontology.hasRelationType(relation)) {
            error = "ontology meta-graph requires relation type '" +
                    std::string(relation) + "'";
            return false;
        }
    }
    return true;
}

std::vector<MetaNode> reflect_registry(const OntologyRegistry& ontology) {
    std::vector<MetaNode> nodes;

    std::unordered_set<std::string> facets;
    for (const auto& [name, definition] : ontology.entityTypes()) {
        const std::string key = canonical(
            QualifiedReferenceKind::MetaClass, {name});
        MetaNode node{key, "OntologyClassMeta",
                      common_properties(key, name), {}};
        node.properties.emplace_back("definition_source",
                                     definition.source);
        node.properties.emplace_back("class_abstract",
                                     boolean(definition.is_abstract));
        node.properties.emplace_back("has_direct_parent",
                                     boolean(!definition.parent.empty()));
        if (!definition.parent.empty()) {
            node.links.push_back({
                "ONTOLOGY_CLASS_DIRECT_PARENT",
                canonical(QualifiedReferenceKind::MetaClass,
                          {definition.parent}),
            });
        }
        for (const auto& facet : definition.facets) {
            facets.insert(facet);
            node.links.push_back({
                "ONTOLOGY_CLASS_HAS_FACET",
                canonical(QualifiedReferenceKind::MetaFacet, {facet}),
            });
        }
        nodes.push_back(std::move(node));

        for (const PropertyDef& definition : ontology.propertiesOf(name)) {
            const std::string property_key = canonical(
                QualifiedReferenceKind::MetaProperty,
                {name, definition.name});
            MetaNode property{property_key, "OntologyPropertyMeta",
                              common_properties(property_key,
                                                definition.name),
                              {}};
            property.properties.emplace_back("definition_source",
                                             definition.source);
            property.properties.emplace_back(
                "property_required", boolean(definition.required));
            property.properties.emplace_back(
                "property_identifier", boolean(definition.identifier));
            property.properties.emplace_back(
                "property_create_only", boolean(definition.create_only));
            property.properties.emplace_back(
                "has_minimum", boolean(definition.has_min));
            if (definition.has_min) {
                property.properties.emplace_back(
                    "minimum_value", finite_double(definition.min_value));
            }
            property.properties.emplace_back(
                "has_maximum", boolean(definition.has_max));
            if (definition.has_max) {
                property.properties.emplace_back(
                    "maximum_value", finite_double(definition.max_value));
            }
            const bool has_reference_target =
                definition.value_kind == PropertyValueKind::EntityRef &&
                !definition.ref_target.empty();
            property.properties.emplace_back(
                "has_reference_target", boolean(has_reference_target));
            const bool has_enum_type =
                definition.value_kind == PropertyValueKind::Enum &&
                !definition.enum_type.empty();
            property.properties.emplace_back(
                "has_enum_type", boolean(has_enum_type));
            property.links.push_back({
                "ONTOLOGY_PROPERTY_DECLARING_CLASS", key,
            });
            property.links.push_back({
                "ONTOLOGY_PROPERTY_VALUE_KIND",
                canonical(QualifiedReferenceKind::MetaValueKind,
                          {property_value_kind_name(definition.value_kind)}),
            });
            if (has_reference_target) {
                property.links.push_back({
                    "ONTOLOGY_PROPERTY_REFERENCE_TARGET",
                    canonical(QualifiedReferenceKind::MetaClass,
                              {definition.ref_target}),
                });
            }
            if (has_enum_type) {
                property.links.push_back({
                    "ONTOLOGY_PROPERTY_ENUM_TYPE",
                    canonical(QualifiedReferenceKind::MetaEnum,
                              {definition.enum_type}),
                });
            }
            nodes.push_back(std::move(property));
        }
    }

    for (const std::string& facet : facets) {
        const std::string key = canonical(
            QualifiedReferenceKind::MetaFacet, {facet});
        nodes.push_back({key, "OntologyFacetMeta",
                         common_properties(key, facet), {}});
    }

    static constexpr std::array<PropertyValueKind, 7> kValueKinds{{
        PropertyValueKind::String,
        PropertyValueKind::Boolean,
        PropertyValueKind::Integer,
        PropertyValueKind::Float,
        PropertyValueKind::EntityRef,
        PropertyValueKind::Enum,
        PropertyValueKind::LegacyDateTime,
    }};
    for (const PropertyValueKind kind : kValueKinds) {
        const std::string name = property_value_kind_name(kind);
        const std::string key = canonical(
            QualifiedReferenceKind::MetaValueKind, {name});
        nodes.push_back({key, "OntologyValueKindMeta",
                         common_properties(key, name), {}});
    }

    for (const auto& [name, definition] : ontology.relationTypes()) {
        const std::string key = canonical(
            QualifiedReferenceKind::MetaRelation, {name});
        MetaNode node{key, "OntologyRelationMeta",
                      common_properties(key, name), {}};
        node.properties.emplace_back("definition_source",
                                     definition.source);
        for (const std::string& source : definition.valid_source_types) {
            node.links.push_back({
                "ONTOLOGY_RELATION_VALID_SOURCE",
                canonical(QualifiedReferenceKind::MetaClass, {source}),
            });
        }
        for (const std::string& target : definition.valid_target_types) {
            node.links.push_back({
                "ONTOLOGY_RELATION_VALID_TARGET",
                canonical(QualifiedReferenceKind::MetaClass, {target}),
            });
        }
        nodes.push_back(std::move(node));
    }

    for (const auto& [name, definition] : ontology.enumTypes()) {
        const std::string key = canonical(
            QualifiedReferenceKind::MetaEnum, {name});
        MetaNode enum_node{key, "OntologyEnumMeta",
                           common_properties(key, name), {}};
        enum_node.properties.emplace_back("definition_source",
                                          definition.source);
        for (const std::string& member : definition.members) {
            const std::string member_key = canonical(
                QualifiedReferenceKind::MetaEnumMember, {name, member});
            enum_node.links.push_back(
                {"ONTOLOGY_ENUM_HAS_MEMBER", member_key});
            MetaNode member_node{
                member_key,
                "OntologyEnumMemberMeta",
                common_properties(member_key, member),
                {{"ONTOLOGY_ENUM_MEMBER_OWNER", key}},
            };
            member_node.properties.emplace_back("definition_source",
                                                definition.source);
            nodes.push_back(std::move(member_node));
        }
        nodes.push_back(std::move(enum_node));
    }

    std::sort(nodes.begin(), nodes.end(),
              [](const MetaNode& left, const MetaNode& right) {
                  return left.key < right.key;
              });
    for (MetaNode& node : nodes) {
        std::sort(node.links.begin(), node.links.end(),
                  [](const MetaLink& left, const MetaLink& right) {
                      return std::tie(left.relation, left.target_key) <
                             std::tie(right.relation, right.target_key);
                  });
    }
    return nodes;
}

}  // namespace

bool materialize_ontology_meta_graph(KGModule& world,
                                     OntologyMetaGraphReport& report) {
    report = OntologyMetaGraphReport{};
    if (!world.isEnabled()) {
        report.error =
            "ontology meta-graph requires an enabled KGModule";
        return false;
    }
    const OntologyRegistry& ontology = world.getRegistry();
    if (!preflight_vocabulary(ontology, report.error)) return false;

    std::vector<MetaNode> nodes;
    try {
        nodes = reflect_registry(ontology);
    } catch (const std::exception& error) {
        report.error = std::string("ontology meta-graph reflection failed: ") +
                       error.what();
        return false;
    }

    std::unordered_map<std::string, size_t> positions;
    positions.reserve(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        if (!positions.emplace(nodes[index].key, index).second) {
            report.error = "ontology meta-graph duplicate canonical key '" +
                           nodes[index].key + "'";
            return false;
        }
    }
    for (const MetaNode& node : nodes) {
        for (const MetaLink& link : node.links) {
            if (!positions.count(link.target_key)) {
                report.error = "ontology meta-graph link from '" + node.key +
                               "' targets missing canonical key '" +
                               link.target_key + "'";
                return false;
            }
        }
    }

    std::vector<KGOp> plan;
    const bool create_context =
        world.ontology_meta_context_ == INVALID_ENTITY ||
        !world.exists(world.ontology_meta_context_);
    if (create_context) {
        plan.emplace_back(KGOpCreateEntity{
            "OntologyMetaContext", {{"context_key", kContextKey}},
            kContextAlias});
    }

    std::vector<std::string> aliases;
    aliases.reserve(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        aliases.push_back("_logosphere_ontology_meta_" +
                          std::to_string(index));
        plan.emplace_back(KGOpCreateEntity{
            nodes[index].type, nodes[index].properties, aliases.back()});
    }

    const EntityRef context_ref = create_context
        ? symbolic(kContextAlias)
        : EntityRef{world.ontology_meta_context_, ""};
    for (size_t index = 0; index < nodes.size(); ++index) {
        plan.emplace_back(KGOpSetRelation{
            context_ref, "ONTOLOGY_META_CONTAINS", symbolic(aliases[index])});
        for (const MetaLink& link : nodes[index].links) {
            plan.emplace_back(KGOpSetRelation{
                symbolic(aliases[index]), link.relation,
                symbolic(aliases[positions.at(link.target_key)])});
        }
    }

    KGOpBatchReport batch;
    if (!apply_kg_ops_atomically(
            plan, world, batch,
            MutationAuthority::OntologyMaterialization)) {
        report.error = batch.error;
        return false;
    }

    EntityID context = world.ontology_meta_context_;
    if (create_context) {
        const auto found = batch.bindings.find(kContextAlias);
        if (found == batch.bindings.end()) {
            report.error =
                "ontology meta-graph committed without its context binding";
            return false;
        }
        context = found->second;
    }

    std::unordered_map<std::string, EntityID> new_index;
    std::vector<EntityID> new_entities;
    new_index.reserve(nodes.size());
    new_entities.reserve(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index) {
        const auto found = batch.bindings.find(aliases[index]);
        if (found == batch.bindings.end()) {
            report.error = "ontology meta-graph committed without binding '" +
                           nodes[index].key + "'";
            return false;
        }
        new_index.emplace(nodes[index].key, found->second);
        new_entities.push_back(found->second);
    }

    std::vector<EntityID> old_entities =
        std::move(world.ontology_meta_entities_);
    world.ontology_meta_context_ = context;
    world.ontology_meta_index_ = std::move(new_index);
    world.ontology_meta_entities_ = new_entities;
    world.ontology_meta_current_ = true;

    logosphere::EventBus* event_bus = world.get_event_bus();
    world.set_event_bus(nullptr);
    for (const EntityID id : old_entities) {
        if (world.exists(id)) world.destroyEntity(id);
    }
    world.set_event_bus(event_bus);

    report.ok = true;
    report.context = context;
    report.node_count = nodes.size();
    report.canonical_keys.reserve(nodes.size());
    for (const MetaNode& node : nodes) {
        report.canonical_keys.push_back(node.key);
    }
    return true;
}

}  // namespace kg
