// Generic L0 source-corpus boundary.
//
// The caller declares what belongs to the corpus and supplies exact-byte
// access. The engine, never the caller, derives representation and edition
// identity. Nothing in this fixture knows about a game or filesystem.

#undef NDEBUG

#include "logosphere/text/source_corpus.h"
#include "logosphere/text/source_target.h"

#include <iostream>
#include <string>
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
using rule_language::ontology::SourceMediaType;

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

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
