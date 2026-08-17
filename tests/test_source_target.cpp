// Standards-shaped source targets, resolved from LinkML-generated KG types.
//
// The model reader decides the range. The source tool holds the bytes.
// This test proves that the typed tuple identifies duplicate and empty
// leaves, and that the tool rejects source drift or selector disagreement.

#undef NDEBUG

#include "logosphere/kg/kg_module.h"
#include "logosphere/text/source_target.h"
#include "generated/rule_language_ontology_registry.h"

#include <iostream>
#include <string>

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
using logosphere::text::canonical_byte_range_key;
using logosphere::text::resolve_text_target;
using logosphere::text::sha256_hex;

struct Fixture {
    kg::KGModule world{rule_language::ontology::registry()};
    EntityID layer = kg::INVALID_ENTITY;
    EntityID representation = kg::INVALID_ENTITY;

    explicit Fixture(const std::string& bytes) {
        world.setMode(kg::KGMode::MINIMAL);
        layer = world.createEntity("SourceLayerContext");
        world.setProperty(layer, "context_key", "source-layer:test");
        world.setProperty(layer, "source_layer", "test");

        representation = world.createEntity("SourceRepresentationContext");
        world.setProperty(representation, "context_key",
                          "source-representation:test:" + sha256_hex(bytes));
        world.setProperty(representation, "source_layer", "test");
        world.setProperty(representation, "source_file", "fixture.md");
        world.setProperty(representation, "source_commit", "fixture");
        world.setProperty(representation, "source_layer_context",
                          std::to_string(layer));
        world.setProperty(representation, "source_media_type", "UTF8_TEXT");
        world.setProperty(representation, "source_digest_algorithm", "SHA256");
        world.setProperty(representation, "source_digest", sha256_hex(bytes));
        world.setProperty(representation, "source_byte_length",
                          std::to_string(bytes.size()));
    }

    EntityID target(long long start, long long end,
                    const std::string* quote = nullptr) {
        const auto selector = world.createEntity("ByteRangeSelector");
        world.setProperty(selector, "identity_context",
                          std::to_string(representation));
        world.setProperty(selector, "entity_key",
                          canonical_byte_range_key(start, end));
        world.setProperty(selector, "source_byte_start", std::to_string(start));
        world.setProperty(selector, "source_byte_end", std::to_string(end));

        EntityID quote_selector = kg::INVALID_ENTITY;
        if (quote) {
            quote_selector = world.createEntity("TextQuoteSelector");
            world.setProperty(quote_selector, "identity_context",
                              std::to_string(representation));
            world.setProperty(quote_selector, "entity_key",
                              "quote:" + sha256_hex(*quote));
            world.setProperty(quote_selector, "source_quote_exact", *quote);
        }

        const auto result = world.createEntity("SourceTarget");
        world.setProperty(result, "identity_context",
                          std::to_string(representation));
        world.setProperty(result, "entity_key",
                          canonical_byte_range_key(start, end));
        world.setProperty(result, "target_representation",
                          std::to_string(representation));
        world.setProperty(result, "target_primary_selector",
                          std::to_string(selector));
        if (quote_selector != kg::INVALID_ENTITY)
            world.setProperty(result, "target_quote_selector",
                              std::to_string(quote_selector));
        return result;
    }
};

void test_sha256_matches_the_standard_vector() {
    CHECK(sha256_hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "source digests use standard SHA-256, not a process-local hash");
}

void test_duplicate_text_has_distinct_structured_identity() {
    const std::string bytes = "same|same";
    Fixture f(bytes);
    const std::string quote = "same";
    const auto first = f.target(0, 4, &quote);
    const auto second = f.target(5, 9, &quote);

    const auto a = resolve_text_target(f.world, first, bytes);
    const auto b = resolve_text_target(f.world, second, bytes);
    CHECK(a.ok && b.ok && a.text == "same" && b.text == "same",
          "both duplicate leaves resolve to the same exact text");
    CHECK(f.world.getProperty(first, "entity_key") !=
              f.world.getProperty(second, "entity_key"),
          "their byte selectors give them distinct structured identities");
}

void test_empty_leaves_are_addressable() {
    const std::string bytes = "||";
    Fixture f(bytes);
    const auto first = f.target(0, 0);
    const auto second = f.target(1, 1);

    const auto a = resolve_text_target(f.world, first, bytes);
    const auto b = resolve_text_target(f.world, second, bytes);
    CHECK(a.ok && b.ok && a.text.empty() && b.text.empty(),
          "zero-width byte ranges address empty leaves without fake text");
    CHECK(f.world.getProperty(first, "entity_key") !=
              f.world.getProperty(second, "entity_key"),
          "two empty leaves at different positions have different identity");
}

void test_changed_source_bytes_fail_digest_verification() {
    const std::string original = "rule=5";
    Fixture f(original);
    const std::string quote = "5";
    const auto target = f.target(5, 6, &quote);

    const auto result = resolve_text_target(f.world, target, "rule=6");
    CHECK(!result.ok && result.reason.find("digest") != std::string::npos,
          "same-length source drift fails before the selected text is used");
}

void test_quote_must_converge_with_the_primary_selector() {
    const std::string bytes = "Scout: Int 6+";
    Fixture f(bytes);
    const std::string wrong = "Int 5+";
    const auto target = f.target(7, 13, &wrong);

    const auto result = resolve_text_target(f.world, target, bytes);
    CHECK(!result.ok && result.reason.find("quote") != std::string::npos,
          "a supporting quote that disagrees with its byte range fails");
}

void test_invalid_range_fails_loudly() {
    const std::string bytes = "abc";
    Fixture f(bytes);
    const auto target = f.target(3, 2);

    const auto result = resolve_text_target(f.world, target, bytes);
    CHECK(!result.ok && result.reason.find("start") != std::string::npos,
          "a range whose end precedes its start fails with the bad bounds");
}

}  // namespace

int main() {
    std::cout << "Source targets (representation plus typed selector)"
              << std::endl;
    test_sha256_matches_the_standard_vector();
    test_duplicate_text_has_distinct_structured_identity();
    test_empty_leaves_are_addressable();
    test_changed_source_bytes_fail_digest_verification();
    test_quote_must_converge_with_the_primary_selector();
    test_invalid_range_fails_loudly();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
