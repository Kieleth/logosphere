// Generic L0 source-corpus boundary.
//
// The caller declares what belongs to the corpus and supplies exact-byte
// access. The engine, never the caller, derives representation and edition
// identity. Nothing in this fixture knows about a game or filesystem.

#undef NDEBUG

#include "logosphere/kg/kg_module.h"
#include "logosphere/text/source_corpus.h"
#include "logosphere/text/source_manifest.h"
#include "logosphere/text/source_target.h"
#include "generated/rule_language_ontology_registry.h"

#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

using logosphere::text::SourceAccess;
using logosphere::text::SourceCorpusDeclaration;
using logosphere::text::SourceReadResult;
using logosphere::text::SourceRepresentationDeclaration;
using logosphere::text::materialize_source_corpus;
using logosphere::text::materialize_source_corpus_into_kg;
using rule_language::ontology::SourceMediaType;

static_assert(
    !std::is_default_constructible_v<SourceRepresentationDeclaration>,
    "source declarations must explicitly provide path, typed media, and revision");

SourceRepresentationDeclaration source(std::string path,
                                       std::string revision = "revision-a") {
    return {std::move(path), SourceMediaType::UTF8_TEXT,
            std::move(revision)};
}

struct MapAccess final : SourceAccess {
    std::unordered_map<std::string, std::string> content;
    mutable int reads = 0;

    explicit MapAccess(
        std::unordered_map<std::string, std::string> initial = {})
        : content(std::move(initial)) {}

    SourceReadResult read_exact(
        const SourceRepresentationDeclaration& declaration) const override {
        ++reads;
        const auto found = content.find(declaration.source_file);
        if (found == content.end())
            return {false, {}, "not available"};
        return {true, found->second, {}};
    }
};

struct GeneratedAccess final : SourceAccess {
    SourceReadResult read_exact(
        const SourceRepresentationDeclaration& declaration) const override {
        if (declaration.source_file == "a.md") return {true, "alpha", {}};
        if (declaration.source_file == "b.md") return {true, "beta", {}};
        return {false, {}, "unknown generated source"};
    }
};

void test_declaration_order_and_access_mechanism_do_not_change_identity() {
    MapAccess mapped{{{"a.md", "alpha"}, {"b.md", "beta"}}};
    GeneratedAccess generated;
    const SourceCorpusDeclaration forward{
        "test", {source("a.md"), source("b.md")}};
    const SourceCorpusDeclaration reverse{
        "test", {source("b.md"), source("a.md")}};

    const auto first = materialize_source_corpus(forward, mapped);
    const auto second = materialize_source_corpus(reverse, generated);
    CHECK(first.ok && second.ok &&
              first.manifest_digest == second.manifest_digest &&
              first.edition_key == second.edition_key,
          "corpus identity depends on declared representations, not adapter or order");
    CHECK(first.representations.size() == 2 &&
              first.representations[0].source_file == "a.md" &&
              first.representations[1].source_file == "b.md",
          "the engine emits one deterministic representation inventory");
}

void test_engine_derives_digest_and_length_from_exact_bytes() {
    MapAccess access{{{"binary.dat", std::string("a\0b", 3)}}};
    const SourceCorpusDeclaration declaration{
        "test", {source("binary.dat")}};

    const auto result = materialize_source_corpus(declaration, access);
    CHECK(result.ok && result.representations.size() == 1 &&
              result.representations[0].source_byte_length == 3 &&
              result.representations[0].source_digest ==
                  logosphere::text::sha256_hex(std::string("a\0b", 3)),
          "the caller cannot supply or forge representation digest and length");
}

void test_bytes_and_layer_are_identity_but_revision_is_provenance() {
    MapAccess original{{{"rules.md", "same"}}};
    MapAccess changed{{{"rules.md", "changed"}}};
    const auto revision_a = materialize_source_corpus(
        {"test", {source("rules.md", "revision-a")}}, original);
    const auto revision_b = materialize_source_corpus(
        {"test", {source("rules.md", "revision-b")}}, original);
    const auto other_bytes = materialize_source_corpus(
        {"test", {source("rules.md", "revision-a")}}, changed);
    const auto other_layer = materialize_source_corpus(
        {"other", {source("rules.md", "revision-a")}}, original);

    CHECK(revision_a.ok && revision_b.ok &&
              revision_a.manifest_digest == revision_b.manifest_digest &&
              revision_a.edition_key == revision_b.edition_key,
          "source revision is retained provenance, not byte identity");
    CHECK(other_bytes.ok &&
              other_bytes.manifest_digest != revision_a.manifest_digest,
          "changed bytes produce a different representation manifest");
    CHECK(other_layer.ok &&
              other_layer.manifest_digest == revision_a.manifest_digest &&
              other_layer.edition_key != revision_a.edition_key,
          "the source layer completes edition identity outside the manifest");
}

void test_empty_and_duplicate_declarations_fail_before_source_access() {
    MapAccess access{{{"same.md", "content"}}};
    const auto empty =
        materialize_source_corpus({"test", {}}, access);
    const auto duplicate = materialize_source_corpus(
        {"test", {source("same.md"), source("same.md", "revision-b")}},
        access);

    CHECK(!empty.ok && empty.reason.find("empty") != std::string::npos,
          "a corpus cannot silently declare no representations");
    CHECK(!duplicate.ok &&
              duplicate.reason.find("duplicate") != std::string::npos &&
              access.reads == 0,
          "duplicate logical membership fails before any bytes are consumed");
}

void test_missing_layer_and_revision_fail_before_source_access() {
    MapAccess access{{{"rules.md", "rules"}}};
    const auto missing_layer = materialize_source_corpus(
        {"", {source("rules.md")}}, access);
    const auto missing_revision = materialize_source_corpus(
        {"test", {source("rules.md", "")}}, access);

    CHECK(!missing_layer.ok &&
              missing_layer.reason.find("source_layer") != std::string::npos,
          "a corpus cannot receive a default source layer");
    CHECK(!missing_revision.ok &&
              missing_revision.reason.find("source_revision") !=
                  std::string::npos &&
              access.reads == 0,
          "missing provenance fails before source bytes are consumed");
}

void test_missing_bytes_fail_with_the_declared_source_identity() {
    MapAccess access;
    const auto result = materialize_source_corpus(
        {"test", {source("missing.md")}}, access);

    CHECK(!result.ok && result.reason.find("missing.md") != std::string::npos &&
              result.reason.find("not available") != std::string::npos,
          "source access failure names both the representation and provider reason");
}

void test_empty_representation_bytes_are_valid_and_addressable() {
    MapAccess access{{{"empty.md", ""}}};
    const auto result = materialize_source_corpus(
        {"test", {source("empty.md")}}, access);

    CHECK(result.ok && result.representations[0].source_byte_length == 0,
          "an empty file remains an explicit representation in a non-empty corpus");
}

void test_invalid_typed_media_fails_loudly() {
    MapAccess access{{{"rules.md", "rules"}}};
    auto invalid = source("rules.md");
    invalid.source_media_type = static_cast<SourceMediaType>(999);
    const auto result =
        materialize_source_corpus({"test", {invalid}}, access);

    CHECK(!result.ok && result.reason.find("media") != std::string::npos,
          "an unknown typed media value cannot fall through as text");
}

void test_one_validated_operation_materializes_the_complete_context_graph() {
    kg::KGModule world{rule_language::ontology::registry()};
    world.setMode(kg::KGMode::MINIMAL);
    MapAccess access{{{"a.md", "alpha"}, {"b.md", "beta"}}};
    const auto result = materialize_source_corpus_into_kg(
        {"test", {source("b.md"), source("a.md")}}, access, world);

    CHECK(result.ok && result.source_layer_context != kg::INVALID_ENTITY &&
              result.ingestion_edition_context != kg::INVALID_ENTITY &&
              result.source_representations.size() == 2 &&
              result.source_revision_observations.size() == 2,
          "one engine operation returns the complete materialized context graph");
    CHECK(world.findByType("SourceLayerContext").size() == 1 &&
              world.findByType("SourceRepresentationContext").size() == 2 &&
              world.findByType("SourceRevisionObservation").size() == 2 &&
              world.findByType("IngestionEditionContext").size() == 1,
          "the operation writes layer, representations, observations, and edition");
    CHECK(world.getRelated(result.ingestion_edition_context,
                           "EDITION_INCLUDES_REPRESENTATION") ==
              result.source_representations,
          "the edition owns its sorted exact representation membership");
    bool observations_match = true;
    for (std::size_t index = 0;
         index < result.source_revision_observations.size(); ++index) {
        const auto observation = result.source_revision_observations[index];
        observations_match = observations_match &&
            world.getProperty(observation, "identity_context") ==
                std::to_string(result.source_representations[index]) &&
            world.getProperty(observation, "source_revision") == "revision-a";
    }
    CHECK(observations_match,
          "each immutable observation links one revision to one representation");
    CHECK(logosphere::text::resolve_ingestion_edition(
              world, result.ingestion_edition_context).ok,
          "the persisted edition reproduces the engine-derived manifest identity");
}

void test_same_content_in_a_new_revision_reuses_identity_and_adds_provenance() {
    kg::KGModule world{rule_language::ontology::registry()};
    world.setMode(kg::KGMode::MINIMAL);
    MapAccess access{{{"rules.md", "same"}}};
    const auto first = materialize_source_corpus_into_kg(
        {"test", {source("rules.md", "revision-a")}}, access, world);
    const auto second = materialize_source_corpus_into_kg(
        {"test", {source("rules.md", "revision-b")}}, access, world);
    const auto repeated = materialize_source_corpus_into_kg(
        {"test", {source("rules.md", "revision-b")}}, access, world);

    CHECK(first.ok && second.ok && repeated.ok &&
              first.source_layer_context == second.source_layer_context &&
              first.source_representations == second.source_representations &&
              first.ingestion_edition_context ==
                  second.ingestion_edition_context,
          "revision-only change reuses layer, content, and edition identity");
    CHECK(world.findByType("SourceRevisionObservation").size() == 2 &&
              first.source_revision_observations !=
                  second.source_revision_observations &&
              second.source_revision_observations ==
                  repeated.source_revision_observations,
          "distinct revisions stay queryable and exact replay is idempotent");
}

void test_one_revision_cannot_claim_two_byte_sequences_for_one_path() {
    kg::KGModule world{rule_language::ontology::registry()};
    world.setMode(kg::KGMode::MINIMAL);
    MapAccess original{{{"rules.md", "original"}}};
    MapAccess changed{{{"rules.md", "changed"}}};
    const auto first = materialize_source_corpus_into_kg(
        {"test", {source("rules.md", "revision-a")}}, original, world);
    const auto entities_before = world.getStats().entity_count;
    const auto relations_before = world.getStats().relation_count;
    const auto conflict = materialize_source_corpus_into_kg(
        {"test", {source("rules.md", "revision-a")}}, changed, world);

    CHECK(first.ok && !conflict.ok &&
              conflict.reason.find("revision-a") != std::string::npos &&
              conflict.reason.find("rules.md") != std::string::npos,
          "conflicting bytes at one revision and logical path fail loudly");
    CHECK(world.getStats().entity_count == entities_before &&
              world.getStats().relation_count == relations_before,
          "a provenance conflict publishes no partial context graph");
}

void test_access_failure_publishes_no_contexts() {
    kg::KGModule world{rule_language::ontology::registry()};
    world.setMode(kg::KGMode::MINIMAL);
    MapAccess missing;
    const auto result = materialize_source_corpus_into_kg(
        {"test", {source("missing.md")}}, missing, world);

    CHECK(!result.ok && world.findByType("SourceLayerContext").empty() &&
              world.findByType("SourceRepresentationContext").empty() &&
              world.findByType("SourceRevisionObservation").empty() &&
              world.findByType("IngestionEditionContext").empty(),
          "source access failure occurs before any KG mutation");
}

void test_representation_identity_is_layer_scoped() {
    kg::KGModule world{rule_language::ontology::registry()};
    world.setMode(kg::KGMode::MINIMAL);
    MapAccess access{{{"rules.md", "same"}}};
    const auto first = materialize_source_corpus_into_kg(
        {"first", {source("rules.md")}}, access, world);
    const auto second = materialize_source_corpus_into_kg(
        {"second", {source("rules.md")}}, access, world);

    CHECK(first.ok && second.ok &&
              first.source_representations != second.source_representations &&
              world.findByType("SourceRepresentationContext").size() == 2,
          "the same path and bytes in different authored layers remain distinct contexts");
}

void test_a_later_corpus_can_observe_another_file_at_the_same_revision() {
    kg::KGModule world{rule_language::ontology::registry()};
    world.setMode(kg::KGMode::MINIMAL);
    MapAccess access{{{"a.md", "alpha"}, {"b.md", "beta"}}};
    const auto first = materialize_source_corpus_into_kg(
        {"test", {source("a.md", "revision-a")}}, access, world);
    const auto expanded = materialize_source_corpus_into_kg(
        {"test", {source("a.md", "revision-a"),
                  source("b.md", "revision-a")}},
        access, world);

    CHECK(first.ok && expanded.ok &&
              world.findByType("SourceRepresentationContext").size() == 2,
          "provenance observations can grow without mutating an earlier record");
}

}  // namespace

int main() {
    std::cout << "Generic source corpus declaration and access" << std::endl;
    test_declaration_order_and_access_mechanism_do_not_change_identity();
    test_engine_derives_digest_and_length_from_exact_bytes();
    test_bytes_and_layer_are_identity_but_revision_is_provenance();
    test_empty_and_duplicate_declarations_fail_before_source_access();
    test_missing_layer_and_revision_fail_before_source_access();
    test_missing_bytes_fail_with_the_declared_source_identity();
    test_empty_representation_bytes_are_valid_and_addressable();
    test_invalid_typed_media_fails_loudly();
    test_one_validated_operation_materializes_the_complete_context_graph();
    test_same_content_in_a_new_revision_reuses_identity_and_adds_provenance();
    test_one_revision_cannot_claim_two_byte_sequences_for_one_path();
    test_access_failure_publishes_no_contexts();
    test_representation_identity_is_layer_scoped();
    test_a_later_corpus_can_observe_another_file_at_the_same_revision();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
