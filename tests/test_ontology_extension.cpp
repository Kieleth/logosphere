// Runtime ontology extension example + test
//
// Shows how a game adds its own entity types, relations, and properties
// on top of the engine's ontology. This is the canonical pattern for
// games that need domain-specific types beyond what the engine ships.
//
// Usage:
//   ./build/test_ontology_extension

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"
#include <iostream>
#include <cassert>
#include <exception>
#include <unordered_set>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Game-side: build a minimal farming-game ontology
// ============================================================================
// A real game would generate this via generate_registry.py. Doing it by
// hand here to keep the example self-contained and auditable.

static kg::OntologyRegistry build_farming_ontology() {
    kg::OntologyRegistry reg;

    // The game introduces an abstract Plant branch under the engine's
    // LivingEntity type, then adds its domain-specific concrete types.
    reg.addEntityType("Plant",    "LivingEntity", true);
    reg.addEntityType("Crop",     "Plant", /*is_abstract=*/false);
    reg.addEntityType("Carrot",   "Crop",  false);
    reg.addEntityType("Tomato",   "Crop",  false);
    reg.addEntityType("Soil",     "Entity", false);

    // Ancestors must be declared explicitly for isSubtypeOf() to work.
    reg.addAncestors("Plant",  {"LivingEntity", "WorldEntity", "Entity"});
    reg.addAncestors("Crop",   {"Plant", "LivingEntity", "WorldEntity", "Entity"});
    reg.addAncestors("Carrot", {"Crop", "Plant", "LivingEntity", "WorldEntity", "Entity"});
    reg.addAncestors("Tomato", {"Crop", "Plant", "LivingEntity", "WorldEntity", "Entity"});
    reg.addAncestors("Soil",   {"Entity"});

    // Custom relations
    reg.addRelationType("PLANTED_IN",
        /*sources=*/{"Crop"},
        /*targets=*/{"Soil"});

    // Custom properties
    reg.addProperty("Crop", "growth_stage",  "integer", /*required=*/false);
    reg.addProperty("Crop", "water_level",   "float",   false);
    reg.addProperty("Soil", "fertility",     "float",   false);
    reg.addProperty("Soil", "moisture",      "float",   false);

    return reg;
}

// ============================================================================
// Tests
// ============================================================================

void test_engine_types_still_work_after_extension() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    kg.extendOntology(build_farming_ontology());

    // Engine types survive
    ASSERT(kg.getRegistry().hasEntityType("Humanoid"), "engine type still present");
    ASSERT(kg.getRegistry().hasEntityType("BodyPart"), "engine abstract type still present");

    // Can still create engine entities
    auto eva = kg.createEntity("Humanoid");
    ASSERT(kg.exists(eva), "engine entity created after extension");
}

void test_custom_entity_types_available() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    kg.extendOntology(build_farming_ontology());

    ASSERT(kg.getRegistry().hasEntityType("Crop"),   "custom type Crop added");
    ASSERT(kg.getRegistry().hasEntityType("Carrot"), "custom type Carrot added");
    ASSERT(kg.getRegistry().hasEntityType("Soil"),   "custom type Soil added");

    // Subtype relationships work across the boundary
    ASSERT(kg.getRegistry().isSubtypeOf("Carrot", "Crop"),
           "Carrot is subtype of Crop");
    ASSERT(kg.getRegistry().isSubtypeOf("Carrot", "Plant"),
           "Carrot inherits engine ancestor Plant");
    ASSERT(kg.getRegistry().isSubtypeOf("Carrot", "LivingEntity"),
           "Carrot inherits deeper engine ancestor LivingEntity");
}

void test_custom_entities_can_be_created() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    kg.extendOntology(build_farming_ontology());

    auto carrot = kg.createEntity("Carrot");
    auto soil   = kg.createEntity("Soil");

    ASSERT(kg.exists(carrot), "Carrot entity exists");
    ASSERT(kg.exists(soil),   "Soil entity exists");
    ASSERT(kg.getType(carrot) == "Carrot", "type preserved");
}

void test_custom_relations_validated() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    kg.extendOntology(build_farming_ontology());

    auto carrot = kg.createEntity("Carrot");
    auto soil   = kg.createEntity("Soil");

    // Valid: Crop PLANTED_IN Soil
    auto rel = kg.createRelation(carrot, "PLANTED_IN", soil);
    ASSERT(rel != kg::INVALID_RELATION, "valid relation created");

    // Registry validates
    ASSERT(kg.getRegistry().isValidRelation("PLANTED_IN", "Carrot", "Soil"),
           "PLANTED_IN(Carrot, Soil) validated");
    ASSERT(!kg.getRegistry().isValidRelation("PLANTED_IN", "Soil", "Carrot"),
           "PLANTED_IN reversed is invalid");
}

void test_custom_properties() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    kg.extendOntology(build_farming_ontology());

    auto tomato = kg.createEntity("Tomato");
    kg.setProperty(tomato, "growth_stage", "3");
    kg.setProperty(tomato, "water_level", "0.75");

    ASSERT(kg.getProperty(tomato, "growth_stage") == "3", "growth_stage round-trip");
    ASSERT(kg.getProperty(tomato, "water_level") == "0.75", "water_level round-trip");

    // Schema query reports the property available (via Crop ancestor)
    ASSERT(kg.getRegistry().hasProperty("Crop", "growth_stage"),
           "growth_stage declared on Crop");
}

void test_reextending_is_idempotent() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    auto farming = build_farming_ontology();
    kg.extendOntology(farming);
    const size_t properties_after_first =
        kg.getRegistry().propertiesOf("Crop").size();
    kg.extendOntology(farming);  // second call, same registry

    ASSERT(properties_after_first == 2,
           "the farming extension declares two Crop properties");
    ASSERT(kg.getRegistry().propertiesOf("Crop").size() ==
               properties_after_first,
           "re-extension does not duplicate identical properties");

    auto crop = kg.createEntity("Crop");
    ASSERT(kg.exists(crop), "entity creation works after re-extension");
}

void test_conflicting_entity_type_fails_atomically() {
    kg::OntologyRegistry base("schema://base");
    base.addEntityType("Thing", "Entity", false);

    kg::OntologyRegistry extension("schema://incoming");
    extension.addEntityType("NewType", "Entity", false);
    extension.addEntityType("Thing", "WorldEntity", false);

    bool threw = false;
    bool provenance_is_exact = false;
    try {
        base.extend(extension);
    } catch (const kg::OntologyCollision& collision) {
        threw = true;
        provenance_is_exact =
            collision.kind() == "entity type" &&
            collision.definition() == "Thing" &&
            collision.existing_source() == "schema://base" &&
            collision.incoming_source() == "schema://incoming";
    }

    ASSERT(threw, "a conflicting entity definition throws");
    ASSERT(provenance_is_exact,
           "the collision identifies both schema sources");
    ASSERT(base.entityTypes().at("Thing").parent == "Entity",
           "the existing entity definition survives the conflict");
    ASSERT(!base.hasEntityType("NewType"),
           "a conflicting extension lands no earlier definitions");
}

void test_conflicting_relation_type_fails_atomically() {
    kg::OntologyRegistry base;
    base.addRelationType("LINKS", {"Entity"}, {"Entity"});

    kg::OntologyRegistry extension;
    extension.addEntityType("NewType", "Entity", false);
    extension.addRelationType("LINKS", {"WorldEntity"}, {"Entity"});

    bool threw = false;
    try {
        base.extend(extension);
    } catch (const std::exception&) {
        threw = true;
    }

    ASSERT(threw, "a conflicting relation definition throws");
    ASSERT(base.relationTypes().at("LINKS").valid_source_types ==
               std::unordered_set<std::string>{"Entity"},
           "the existing relation definition survives the conflict");
    ASSERT(!base.hasEntityType("NewType"),
           "a relation conflict leaves the whole extension unapplied");
}

void test_conflicting_property_fails_atomically() {
    kg::OntologyRegistry base;
    base.addEntityType("Thing", "Entity", false);
    base.addProperty("Thing", "score", "integer", false);

    kg::OntologyRegistry extension;
    extension.addEntityType("NewType", "Entity", false);
    extension.addProperty("Thing", "score", "string", false);

    bool threw = false;
    try {
        base.extend(extension);
    } catch (const std::exception&) {
        threw = true;
    }

    ASSERT(threw, "a conflicting property definition throws");
    ASSERT(base.propertiesOf("Thing").size() == 1 &&
               base.propertiesOf("Thing")[0].value_type == "integer",
           "the existing property definition survives the conflict");
    ASSERT(!base.hasEntityType("NewType"),
           "a property conflict leaves the whole extension unapplied");
}

void test_unknown_property_ref_target_fails_atomically() {
    kg::OntologyRegistry base("schema://base");
    base.addEntityType("Entity", "", true);

    kg::OntologyRegistry extension("schema://incoming");
    extension.addEntityType("NewType", "Entity", false);
    extension.addRefProperty("NewType", "owner", true, "MissingType");

    bool threw = false;
    std::string reason;
    try {
        base.extend(extension);
    } catch (const std::exception& error) {
        threw = true;
        reason = error.what();
    }

    ASSERT(threw, "a property targeting an unknown entity type throws");
    ASSERT(reason.find("NewType.owner") != std::string::npos &&
               reason.find("MissingType") != std::string::npos &&
               reason.find("schema://incoming") != std::string::npos,
           "the reference error names property, target, and source");
    ASSERT(!base.hasEntityType("NewType"),
           "an invalid reference leaves the whole extension unapplied");
}

void test_unknown_entity_parent_fails_atomically() {
    kg::OntologyRegistry base("schema://base");
    base.addEntityType("Entity", "", true);

    kg::OntologyRegistry extension("schema://incoming");
    extension.addEntityType("Orphan", "MissingParent", false);

    bool threw = false;
    std::string reason;
    try {
        base.extend(extension);
    } catch (const std::exception& error) {
        threw = true;
        reason = error.what();
    }

    ASSERT(threw, "an entity with an unknown parent throws");
    ASSERT(reason.find("Orphan") != std::string::npos &&
               reason.find("MissingParent") != std::string::npos,
           "the parent error names the child and missing parent");
    ASSERT(!base.hasEntityType("Orphan"),
           "an invalid parent leaves the whole extension unapplied");
}

void test_unknown_relation_endpoint_fails_atomically() {
    kg::OntologyRegistry base("schema://base");
    base.addEntityType("Entity", "", true);

    kg::OntologyRegistry extension("schema://incoming");
    extension.addEntityType("NewType", "Entity", false);
    extension.addRelationType("OWNS", {"NewType"}, {"MissingType"});

    bool threw = false;
    try {
        base.extend(extension);
    } catch (const std::exception&) {
        threw = true;
    }

    ASSERT(threw, "a relation targeting an unknown entity type throws");
    ASSERT(!base.hasEntityType("NewType") &&
               !base.hasRelationType("OWNS"),
           "an invalid relation leaves the whole extension unapplied");
}

void test_invalid_base_registry_is_rejected_when_used() {
    kg::OntologyRegistry invalid("schema://invalid");
    invalid.addEntityType("Thing", "", false);
    invalid.addRefProperty("Thing", "owner", false, "MissingType");

    bool threw = false;
    try {
        kg::KGModule world(invalid);
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT(threw, "KG construction rejects an invalid registry");
}

int main() {
    std::cout << "=== Ontology Extension Tests ===" << std::endl;

    test_engine_types_still_work_after_extension();
    test_custom_entity_types_available();
    test_custom_entities_can_be_created();
    test_custom_relations_validated();
    test_custom_properties();
    test_reextending_is_idempotent();
    test_conflicting_entity_type_fails_atomically();
    test_conflicting_relation_type_fails_atomically();
    test_conflicting_property_fails_atomically();
    test_unknown_property_ref_target_fails_atomically();
    test_unknown_entity_parent_fails_atomically();
    test_unknown_relation_endpoint_fails_atomically();
    test_invalid_base_registry_is_rejected_when_used();

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
