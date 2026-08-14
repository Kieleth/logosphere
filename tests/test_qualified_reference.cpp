#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/qualified_reference.h"
#include "logosphere/kg/ontology_validator.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "... ";                                     \
    try { name(); ++tests_passed; std::cout << "PASS\n"; }               \
    catch (const std::exception& error) {                                 \
        ++tests_failed; std::cout << "FAIL: " << error.what() << '\n';   \
    }

#define REQUIRE(condition, message)                                      \
    if (!(condition)) throw std::runtime_error(message)

namespace {

kg::OntologyRegistry registry() {
    kg::OntologyRegistry out;
    out.addEntityType("KnowledgeContext", "", true);
    out.addEntityType("DocumentContext", "KnowledgeContext", false);
    out.addAncestors("DocumentContext", {"KnowledgeContext"});
    out.addProperty("KnowledgeContext", "context_key",
                    kg::PropertyValueKind::String, true, true);
    out.addEntityType("Addressable", "", true);
    out.addRefProperty("Addressable", "identity_context", true,
                       "KnowledgeContext", true);
    out.addProperty("Addressable", "entity_key",
                    kg::PropertyValueKind::String, true, true);
    out.addEntityType("Parent", "Addressable", false);
    out.addAncestors("Parent", {"Addressable"});
    // The setProperty ontology gate (Malleus H1) rejects undeclared
    // keys, so the fixture declares the mutable display properties
    // the tests write.
    out.addProperty("Parent", "name", kg::PropertyValueKind::String,
                    false);
    out.addProperty("Parent", "source_aliases",
                    kg::PropertyValueKind::String, false);
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    kg::EntityID context = kg::INVALID_ENTITY;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        context = world.createEntity("DocumentContext");
        world.setProperty(context, "context_key", "source-document:test");
    }

    kg::EntityID add_parent(const std::string& key,
                            const std::string& name = "mutable display") {
        const auto id = world.createEntity("Parent");
        world.setProperty(id, "identity_context", std::to_string(context));
        world.setProperty(id, "entity_key", key);
        world.setProperty(id, "name", name);
        return id;
    }
};

void segment_encoding_is_canonical() {
    REQUIRE(kg::encode_qualified_reference_segment("Jack/of All") ==
                "Jack%2Fof%20All",
            "slash and space must use uppercase byte encoding");
    REQUIRE(kg::encode_qualified_reference_segment("AZaz09._~-") ==
                "AZaz09._~-",
            "the canonical literal set must remain literal");
}

void every_reference_shape_parses_exactly() {
    const std::vector<std::string> paths = {
        "@@meta/class/Character",
        "@@meta/property/Character/strength",
        "@@meta/relation/HAS_PART",
        "@@meta/facet/rulebook",
        "@@meta/value-kind/entity_ref",
        "@@meta/enum/EntityStatus",
        "@@meta/enum-member/EntityStatus/ACTIVE",
        "@@entity/source-document%3Atest/Parent/admin%2Fprimary",
    };
    for (const auto& path : paths) {
        kg::QualifiedReference parsed;
        std::string error;
        REQUIRE(kg::parse_qualified_reference(path, parsed, error),
                "canonical path must parse: " + path + ": " + error);
        REQUIRE(kg::format_qualified_reference(parsed) == path,
                "parse and format must reproduce the exact bytes: " + path);
    }
}

void noncanonical_or_legacy_paths_are_rejected() {
    const std::vector<std::string> paths = {
        "@@Parent:Admin",
        "@@entity/context/Parent",
        "@@entity//Parent/key",
        "@@entity/context/Parent/.",
        "@@entity/context/Parent/..",
        "@@entity/context/Parent/key%2fpart",
        "@@entity/context/Parent/%41dmin",
        "@@entity/context/Parent/raw space",
        "@@entity/context/Parent/%C0%AF",
    };
    for (const auto& path : paths) {
        kg::QualifiedReference parsed;
        std::string error;
        REQUIRE(!kg::parse_qualified_reference(path, parsed, error),
                "invalid or legacy path must fail: " + path);
        REQUIRE(!error.empty(), "failure must explain the invalid path");
    }
}

void addressable_resolution_uses_immutable_identity_not_name() {
    Fixture f;
    const auto expected = f.add_parent("admin/primary", "Admin");
    f.world.setProperty(expected, "name", "Renamed display");
    f.world.setProperty(expected, "source_aliases", "Admin; Other");

    const auto result = kg::resolve_qualified_reference(
        "@@entity/source-document%3Atest/Parent/admin%2Fprimary", f.world);
    REQUIRE(result.ok && result.entity == expected,
            "the exact context, concrete type, and entity key must resolve: " +
                result.error);
}

void missing_and_duplicate_addressable_identities_fail() {
    Fixture f;
    auto missing = kg::resolve_qualified_reference(
        "@@entity/source-document%3Atest/Parent/missing", f.world);
    REQUIRE(!missing.ok && missing.error.find("no matching") !=
                               std::string::npos,
            "zero matches must fail loudly");

    f.add_parent("duplicate");
    f.add_parent("duplicate");
    auto duplicate = kg::resolve_qualified_reference(
        "@@entity/source-document%3Atest/Parent/duplicate", f.world);
    REQUIRE(!duplicate.ok && duplicate.error.find("2 matching") !=
                                 std::string::npos,
            "a violated uniqueness invariant must not pick one entity");
}

void validated_creates_enforce_addressable_identity_uniqueness() {
    Fixture f;
    f.add_parent("admin");

    kg::KGOp duplicate = kg::KGOpCreateEntity{
        "Parent",
        {{"identity_context", std::to_string(f.context)},
         {"entity_key", "admin"}}};
    auto duplicate_result =
        kg::validate_kg_op(duplicate, f.world, f.ontology);
    REQUIRE(!duplicate_result.ok &&
                duplicate_result.reason.find("duplicate Addressable identity") !=
                    std::string::npos,
            "the exact identity tuple must be unique");

    kg::KGOp empty_key = kg::KGOpCreateEntity{
        "Parent",
        {{"identity_context", std::to_string(f.context)}, {"entity_key", ""}}};
    auto empty_result = kg::validate_kg_op(empty_key, f.world, f.ontology);
    REQUIRE(!empty_result.ok &&
                empty_result.reason.find("empty entity_key") !=
                    std::string::npos,
            "an Addressable identity cannot contain an empty key");

    const auto other_context = f.world.createEntity("DocumentContext");
    f.world.setProperty(other_context, "context_key", "source-document:other");
    kg::KGOp other_scope = kg::KGOpCreateEntity{
        "Parent",
        {{"identity_context", std::to_string(other_context)},
         {"entity_key", "admin"}}};
    auto other_result = kg::validate_kg_op(other_scope, f.world, f.ontology);
    REQUIRE(other_result.ok,
            "the same type and key in another context is a distinct identity: " +
                other_result.reason);
}

void validated_creates_enforce_global_context_key_uniqueness() {
    Fixture f;
    kg::KGOp duplicate = kg::KGOpCreateEntity{
        "DocumentContext", {{"context_key", "source-document:test"}}};
    auto result = kg::validate_kg_op(duplicate, f.world, f.ontology);
    REQUIRE(!result.ok &&
                result.reason.find("duplicate KnowledgeContext key") !=
                    std::string::npos,
            "context_key must be globally unique across context subtypes");
}

}  // namespace

int main() {
    std::cout << "Qualified reference tests\n";
    TEST(segment_encoding_is_canonical);
    TEST(every_reference_shape_parses_exactly);
    TEST(noncanonical_or_legacy_paths_are_rejected);
    TEST(addressable_resolution_uses_immutable_identity_not_name);
    TEST(missing_and_duplicate_addressable_identities_fail);
    TEST(validated_creates_enforce_addressable_identity_uniqueness);
    TEST(validated_creates_enforce_global_context_key_uniqueness);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
