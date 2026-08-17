// Ingestion-edition identity from a canonical source-representation manifest.
//
// The ontology owns the terms. This test pins the mechanical projection:
// membership order is irrelevant, logical paths and bytes are identity, and
// repository commit IDs remain provenance.

#undef NDEBUG

#include "logosphere/kg/kg_module.h"
#include "logosphere/text/source_manifest.h"
#include "logosphere/text/source_target.h"
#include "generated/rule_language_ontology_registry.h"

#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

using kg::EntityID;
using logosphere::text::build_source_manifest;
using logosphere::text::canonical_ingestion_edition_key;
using logosphere::text::resolve_ingestion_edition;
using logosphere::text::sha256_hex;

struct Fixture {
    kg::KGModule world{rule_language::ontology::registry()};
    EntityID layer = kg::INVALID_ENTITY;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        layer = world.createEntity("SourceLayerContext");
        world.setProperty(layer, "context_key", "source-layer:test");
        world.setProperty(layer, "source_layer", "test");
    }

    EntityID representation(const std::string& path, const std::string& bytes,
                            const std::string& revision = "revision-a") {
        const auto id = world.createEntity("SourceRepresentationContext");
        world.setProperty(id, "context_key",
                          "source-representation:" + path + ":" +
                              sha256_hex(bytes));
        world.setProperty(id, "source_layer", "test");
        world.setProperty(id, "source_file", path);
        world.setProperty(id, "source_revision", revision);
        world.setProperty(id, "source_layer_context", std::to_string(layer));
        world.setProperty(id, "source_media_type", "UTF8_TEXT");
        world.setProperty(id, "source_digest_algorithm", "SHA256");
        world.setProperty(id, "source_digest", sha256_hex(bytes));
        world.setProperty(id, "source_byte_length",
                          std::to_string(bytes.size()));
        return id;
    }

    EntityID edition(const std::vector<EntityID>& representations) {
        const auto manifest =
            build_source_manifest(world, "test", layer, representations);
        if (!manifest.ok) return kg::INVALID_ENTITY;

        const auto id = world.createEntity("IngestionEditionContext");
        world.setProperty(id, "context_key", manifest.edition_key);
        world.setProperty(id, "source_layer", "test");
        world.setProperty(id, "source_layer_context", std::to_string(layer));
        world.setProperty(id, "source_manifest_format",
                          "LENGTH_PREFIXED_V1");
        world.setProperty(id, "source_manifest_digest_algorithm", "SHA256");
        world.setProperty(id, "source_manifest_digest", manifest.digest);
        world.setProperty(id, "source_representation_count",
                          std::to_string(representations.size()));
        for (const auto representation : representations)
            world.createRelation(id, "EDITION_INCLUDES_REPRESENTATION",
                                 representation);
        return id;
    }
};

void test_manifest_encoding_is_pinned_and_order_independent() {
    Fixture f;
    const auto beta = f.representation("b.md", "beta");
    const auto alpha = f.representation("a.md", "alpha");

    const auto first =
        build_source_manifest(f.world, "test", f.layer, {beta, alpha});
    const auto second =
        build_source_manifest(f.world, "test", f.layer, {alpha, beta});
    const std::string expected_alpha =
        "18:LENGTH_PREFIXED_V11:24:a.md9:UTF8_TEXT6:SHA25664:" +
        sha256_hex("alpha") + "1:5";
    CHECK(first.ok && second.ok &&
              first.canonical_bytes == second.canonical_bytes &&
              first.digest == second.digest &&
              first.edition_key == second.edition_key,
          "manifest identity ignores relation and insertion order");
    CHECK(first.canonical_bytes.rfind(expected_alpha, 0) == 0,
          "the versioned length-prefixed encoding and path sort are pinned");
}

void test_paths_and_bytes_are_identity_but_revision_is_provenance() {
    Fixture f;
    const auto original =
        f.representation("rules.md", "same", "revision-a");
    const auto other_revision =
        f.representation("rules.md", "same", "revision-b");
    const auto renamed =
        f.representation("renamed.md", "same", "revision-a");
    const auto changed =
        f.representation("rules.md", "changed", "revision-a");

    const auto a = build_source_manifest(f.world, "test", f.layer, {original});
    const auto b =
        build_source_manifest(f.world, "test", f.layer, {other_revision});
    const auto c = build_source_manifest(f.world, "test", f.layer, {renamed});
    const auto d = build_source_manifest(f.world, "test", f.layer, {changed});
    CHECK(a.ok && b.ok && a.digest == b.digest,
          "source revision changes do not invalidate identical source bytes");
    CHECK(c.ok && c.digest != a.digest,
          "renaming a logical source changes edition identity");
    CHECK(d.ok && d.digest != a.digest,
          "changing represented bytes changes edition identity");
}

void test_duplicate_paths_and_empty_manifests_fail_loudly() {
    Fixture f;
    const auto first = f.representation("same.md", "one");
    const auto second = f.representation("same.md", "two");

    const auto duplicate =
        build_source_manifest(f.world, "test", f.layer, {first, second});
    const auto empty = build_source_manifest(f.world, "test", f.layer, {});
    CHECK(!duplicate.ok && duplicate.reason.find("duplicate") != std::string::npos,
          "two representations cannot claim one logical source path");
    CHECK(!empty.ok && empty.reason.find("empty") != std::string::npos,
          "an edition cannot silently represent an empty source corpus");
}

void test_every_representation_must_belong_to_the_edition_layer() {
    Fixture f;
    const auto representation = f.representation("rules.md", "rules");
    f.world.setProperty(representation, "source_layer", "other");

    const auto result =
        build_source_manifest(f.world, "test", f.layer, {representation});
    CHECK(!result.ok && result.reason.find("source_layer") != std::string::npos,
          "cross-layer representation membership fails");
}

void test_missing_required_representation_data_fails_loudly() {
    Fixture f;
    const auto representation = f.representation("rules.md", "rules");
    f.world.removeProperty(representation, "source_digest");

    const auto result =
        build_source_manifest(f.world, "test", f.layer, {representation});
    CHECK(!result.ok &&
              result.reason == "missing required source_digest",
          "a missing representation digest is never filled or inferred");
}

void test_edition_resolves_from_typed_membership() {
    Fixture f;
    const auto first = f.representation("a.md", "alpha");
    const auto second = f.representation("b.md", "beta");
    const auto edition = f.edition({second, first});

    const auto resolved = resolve_ingestion_edition(f.world, edition);
    CHECK(resolved.ok &&
              resolved.edition_key == f.world.getProperty(edition, "context_key"),
          "a complete typed edition validates and reproduces its identity");
}

void test_manifest_count_digest_and_context_key_drift_fail() {
    Fixture f;
    const auto representation = f.representation("rules.md", "rules");

    const auto wrong_count = f.edition({representation});
    f.world.setProperty(wrong_count, "source_representation_count", "2");
    const auto count_result =
        resolve_ingestion_edition(f.world, wrong_count);
    CHECK(!count_result.ok && count_result.reason.find("count") != std::string::npos,
          "missing membership cannot hide behind a declared count");

    const auto wrong_digest = f.edition({representation});
    f.world.setProperty(wrong_digest, "source_manifest_digest",
                        std::string(64, '0'));
    const auto digest_result =
        resolve_ingestion_edition(f.world, wrong_digest);
    CHECK(!digest_result.ok &&
              digest_result.reason.find("digest") != std::string::npos,
          "declared manifest digest drift fails");

    const auto wrong_key = f.edition({representation});
    f.world.setProperty(wrong_key, "context_key", "ingestion-edition:forged");
    const auto key_result = resolve_ingestion_edition(f.world, wrong_key);
    CHECK(!key_result.ok && key_result.reason.find("context_key") !=
                                std::string::npos,
          "a forged compact context key fails against structured identity");
}

void test_context_key_projection_is_unambiguous() {
    const std::string digest(64, 'a');
    CHECK(canonical_ingestion_edition_key("ab:c", digest) ==
              "ingestion-edition:v1:4:ab:c:sha256:" + digest,
          "the compact key pins its version and byte-length-prefixed layer");
}

}  // namespace

int main() {
    std::cout << "Ingestion edition source manifests" << std::endl;
    test_manifest_encoding_is_pinned_and_order_independent();
    test_paths_and_bytes_are_identity_but_revision_is_provenance();
    test_duplicate_paths_and_empty_manifests_fail_loudly();
    test_every_representation_must_belong_to_the_edition_layer();
    test_missing_required_representation_data_fails_loudly();
    test_edition_resolves_from_typed_membership();
    test_manifest_count_digest_and_context_key_drift_fail();
    test_context_key_projection_is_unambiguous();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
