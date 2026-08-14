#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_meta_graph.h"
#include "logosphere/kg/qualified_reference.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

int passed = 0;
int failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "... ";                                     \
    try { name(); ++passed; std::cout << "PASS\n"; }                     \
    catch (const std::exception& error) {                                 \
        ++failed; std::cout << "FAIL: " << error.what() << '\n';         \
    }

#define REQUIRE(condition, message)                                      \
    if (!(condition)) throw std::runtime_error(message)

kg::EntityID resolve(kg::KGModule& world, const std::string& path) {
    const auto result = kg::resolve_qualified_reference(path, world);
    REQUIRE(result.ok, "resolve " + path + ": " + result.error);
    return result.entity;
}

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());
    return out;
}

size_t expected_node_count(const kg::OntologyRegistry& ontology) {
    size_t properties = 0;
    std::unordered_set<std::string> facets;
    size_t members = 0;
    for (const auto& [name, definition] : ontology.entityTypes()) {
        properties += ontology.propertiesOf(name).size();
        facets.insert(definition.facets.begin(), definition.facets.end());
    }
    for (const auto& [name, definition] : ontology.enumTypes()) {
        (void)name;
        members += definition.members.size();
    }
    return ontology.entityTypes().size() + properties +
           ontology.relationTypes().size() + facets.size() + 7 +
           ontology.enumTypes().size() + members;
}

void complete_registry_semantics_materialize_and_resolve() {
    kg::KGModule world(registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::OntologyMetaGraphReport report;
    REQUIRE(kg::materialize_ontology_meta_graph(world, report),
            "materialization succeeds: " + report.error);
    REQUIRE(report.node_count == expected_node_count(world.getRegistry()) &&
                report.canonical_keys.size() == report.node_count,
            "every retained registry definition has one canonical node");
    REQUIRE(std::is_sorted(report.canonical_keys.begin(),
                           report.canonical_keys.end()),
            "reported canonical keys are deterministic");

    const auto task = resolve(world, "@@meta/class/TaskCheck");
    REQUIRE(world.getType(task) == "OntologyClassMeta" &&
                world.getProperty(task, "ontology_name") == "TaskCheck" &&
                world.getProperty(task, "qualified_key") ==
                    "@@meta/class/TaskCheck" &&
                !world.getProperty(task, "definition_source").empty(),
            "class reflection keeps identity, name, and provenance");

    const auto root = resolve(world, "@@meta/class/Entity");
    REQUIRE(world.getProperty(root, "has_direct_parent") == "false" &&
                world.getRelated(root,
                    "ONTOLOGY_CLASS_DIRECT_PARENT").empty(),
            "root classes represent parent absence explicitly");

    const auto key = resolve(
        world, "@@meta/property/Addressable/entity_key");
    REQUIRE(world.getType(key) == "OntologyPropertyMeta" &&
                world.getProperty(key, "property_required") == "true" &&
                world.getProperty(key, "property_create_only") == "true" &&
                world.getProperty(key, "has_minimum") == "false" &&
                world.getProperty(key, "has_maximum") == "false" &&
                world.getProperty(key, "has_reference_target") == "false" &&
                world.getProperty(key, "has_enum_type") == "false",
            "property reflection keeps flags and explicit absence");
    REQUIRE(world.getRelated(key,
                "ONTOLOGY_PROPERTY_DECLARING_CLASS") ==
                std::vector<kg::EntityID>{resolve(
                    world, "@@meta/class/Addressable")} &&
                world.getRelated(key, "ONTOLOGY_PROPERTY_VALUE_KIND") ==
                std::vector<kg::EntityID>{resolve(
                    world, "@@meta/value-kind/string")},
            "property reflection uses canonical typed links");
    REQUIRE(!kg::resolve_qualified_reference(
                 "@@meta/property/TaskCheck/entity_key", world).ok,
            "inherited property aliases do not resolve");

    const auto relation = resolve(world, "@@meta/relation/HAS_PART");
    REQUIRE(!world.getProperty(relation, "definition_source").empty() &&
                !world.getRelated(
                    relation, "ONTOLOGY_RELATION_VALID_SOURCE").empty() &&
                !world.getRelated(
                    relation, "ONTOLOGY_RELATION_VALID_TARGET").empty(),
            "relation reflection keeps provenance and typed endpoint sets");

    const auto enum_type = resolve(world, "@@meta/enum/WorldRelationType");
    const auto enum_member = resolve(
        world, "@@meta/enum-member/WorldRelationType/HAS_PART");
    REQUIRE(world.getRelated(enum_type, "ONTOLOGY_ENUM_HAS_MEMBER").size() ==
                world.getRegistry().enumTypes().at("WorldRelationType")
                    .members.size() &&
                world.getRelated(enum_member,
                    "ONTOLOGY_ENUM_MEMBER_OWNER") ==
                std::vector<kg::EntityID>{enum_type},
            "nominal enum definitions and complete membership survive");

    (void)resolve(world, "@@meta/facet/ontology-meta");
    (void)resolve(world, "@@meta/value-kind/entity_ref");
    (void)resolve(world, "@@meta/relation/ONTOLOGY_META_CONTAINS");
}

void meta_entities_reject_every_external_mutation_authority() {
    kg::KGModule world(registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::OntologyMetaGraphReport report;
    REQUIRE(kg::materialize_ontology_meta_graph(world, report), report.error);
    const auto task = resolve(world, "@@meta/class/TaskCheck");

    kg::KGOpBatchReport runtime;
    REQUIRE(!kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpSetProperty{
                    {task, ""}, "ontology_name", "Forged"}}},
                world, runtime) &&
                runtime.error.find("immutable ontology meta-graph") !=
                    std::string::npos,
            "ordinary runtime mutation cannot change meta entities");

    kg::KGOpBatchReport seed;
    REQUIRE(!kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "OntologyFacetMeta",
                    {{"qualified_key", "@@meta/facet/forged"},
                     {"ontology_name", "forged"}}, "forged"}}},
                world, seed, kg::MutationAuthority::SeedIngestion) &&
                seed.error.find("engine-owned ontology materialization") !=
                    std::string::npos,
            "seed ingestion cannot create meta entities");
}

void extension_invalidates_then_rebuilds_the_complete_graph() {
    kg::KGModule world(registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::OntologyMetaGraphReport first;
    REQUIRE(kg::materialize_ontology_meta_graph(world, first), first.error);
    const auto old_task = resolve(world, "@@meta/class/TaskCheck");

    kg::OntologyRegistry extension("schema://meta-test-extension");
    extension.addEntityType("ReflectedExtension", "Entity", false);
    extension.addAncestors("ReflectedExtension", {"Entity"});
    world.extendOntology(extension);
    const auto stale = kg::resolve_qualified_reference(
        "@@meta/class/TaskCheck", world);
    REQUIRE(!stale.ok && stale.error.find("not current") != std::string::npos,
            "registry extension invalidates all meta resolution");

    kg::OntologyMetaGraphReport rebuilt;
    REQUIRE(kg::materialize_ontology_meta_graph(world, rebuilt),
            "rebuild succeeds: " + rebuilt.error);
    (void)resolve(world, "@@meta/class/ReflectedExtension");
    REQUIRE(!world.exists(old_task),
            "successful publication removes the superseded graph");
}

void missing_meta_vocabulary_fails_without_publishing_anything() {
    kg::KGModule world(logosphere::ontology::registry());
    world.setMode(kg::KGMode::MINIMAL);
    const auto before = world.getStats();
    kg::OntologyMetaGraphReport report;
    REQUIRE(!kg::materialize_ontology_meta_graph(world, report) &&
                report.error.find("OntologyMetaEntity") != std::string::npos,
            "materialization requires its declared ontology vocabulary");
    const auto after = world.getStats();
    REQUIRE(after.entity_count == before.entity_count &&
                after.relation_count == before.relation_count,
            "failed materialization publishes no partial graph");
}

}  // namespace

int main() {
    std::cout << "Ontology registry meta-graph\n";
    TEST(complete_registry_semantics_materialize_and_resolve);
    TEST(meta_entities_reject_every_external_mutation_authority);
    TEST(extension_invalidates_then_rebuilds_the_complete_graph);
    TEST(missing_meta_vocabulary_fails_without_publishing_anything);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
