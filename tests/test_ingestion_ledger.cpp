// Two-level ingestion ledger with append-only decision history.

#undef NDEBUG

#include "logosphere/kg/ingestion_ledger.h"
#include "logosphere/kg/kg_module.h"
#include "generated/rulebook_ontology_registry.h"

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

struct Fixture {
    kg::KGModule world{rulebook::ontology::registry()};
    std::vector<EntityID> enumerated;

    Fixture() { world.setMode(kg::KGMode::MINIMAL); }

    EntityID target(const std::string& key) {
        const auto id = world.createEntity("SourceTarget");
        world.setProperty(id, "entity_key", key);
        enumerated.push_back(id);
        return id;
    }

    EntityID representation(unsigned long long byte_length,
                            const std::string& media = "UTF8_TEXT") {
        const auto id = world.createEntity("SourceRepresentationContext");
        world.setProperty(id, "context_key", "representation:test");
        world.setProperty(id, "source_media_type", media);
        world.setProperty(id, "source_byte_length",
                          std::to_string(byte_length));
        return id;
    }

    EntityID range(EntityID representation, unsigned long long start,
                   unsigned long long end, const std::string& key) {
        const auto id = world.createEntity("ByteRangeSelector");
        world.setProperty(id, "identity_context",
                          std::to_string(representation));
        world.setProperty(id, "entity_key", key);
        world.setProperty(id, "source_byte_start", std::to_string(start));
        world.setProperty(id, "source_byte_end", std::to_string(end));
        return id;
    }

    EntityID target_range(EntityID representation, unsigned long long start,
                          unsigned long long end, const std::string& key) {
        const auto selector = range(representation, start, end, key + ":range");
        const auto id = target(key);
        world.setProperty(id, "identity_context",
                          std::to_string(representation));
        world.setProperty(id, "target_representation",
                          std::to_string(representation));
        world.setProperty(id, "target_primary_selector",
                          std::to_string(selector));
        return id;
    }

    EntityID exclusion(EntityID representation, unsigned long long start,
                       unsigned long long end, const std::string& kind,
                       const std::string& key) {
        const auto selector = range(representation, start, end, key + ":range");
        const auto id = world.createEntity("SourceExclusion");
        world.setProperty(id, "identity_context",
                          std::to_string(representation));
        world.setProperty(id, "entity_key", key);
        world.setProperty(id, "exclusion_selector", std::to_string(selector));
        world.setProperty(id, "exclusion_kind", kind);
        return id;
    }

    EntityID complete_partition(EntityID representation) {
        const auto id = world.createEntity("CompleteSourcePartition");
        world.setProperty(id, "identity_context",
                          std::to_string(representation));
        world.setProperty(id, "entity_key", "partition:complete");
        return id;
    }

    EntityID coverage(EntityID target, const std::string& key) {
        const auto id = world.createEntity("SourceCoverage");
        world.setProperty(id, "entity_key", key);
        world.setProperty(id, "coverage_target", std::to_string(target));
        return id;
    }

    EntityID claim(const std::string& key, const std::string& statement,
                   const std::vector<EntityID>& coverage) {
        const auto id = world.createEntity("IngestionClaim");
        world.setProperty(id, "entity_key", key);
        world.setProperty(id, "claim_statement", statement);
        for (const auto evidence : coverage)
            world.createRelation(id, "CLAIM_SUPPORTED_BY", evidence);
        return id;
    }

    EntityID materialize(EntityID claim, const std::string& name) {
        const auto result = world.createEntity("RuleConstant");
        world.setProperty(result, "name", name);
        world.setProperty(result, "constant_value", "1");
        world.createRelation(claim, "CLAIM_MATERIALIZES", result);
        return result;
    }

    EntityID coverage_decision(EntityID coverage, int sequence,
                               const std::string& judgement) {
        const auto id = world.createEntity("CoverageDecision");
        decision_base(id, coverage, sequence);
        world.setProperty(id, "coverage_judgement", judgement);
        return id;
    }

    EntityID claim_decision(EntityID claim, int sequence,
                            const std::string& disposition,
                            const std::string& gap = {},
                            EntityID related = kg::INVALID_ENTITY) {
        const auto id = world.createEntity("ClaimDecision");
        decision_base(id, claim, sequence);
        world.setProperty(id, "claim_disposition", disposition);
        if (!gap.empty()) world.setProperty(id, "claim_gap_kind", gap);
        if (related != kg::INVALID_ENTITY)
            world.setProperty(id, "related_claim", std::to_string(related));
        return id;
    }

private:
    void decision_base(EntityID id, EntityID subject, int sequence) {
        world.setProperty(id, "event_type", "ARBITER_DECISION");
        world.setProperty(id, "decision_subject", std::to_string(subject));
        world.setProperty(id, "decision_sequence", std::to_string(sequence));
        world.setProperty(id, "decision_question", "How is this disposed?");
        world.setProperty(id, "decision_reason", "fixture");
        world.setProperty(id, "arbiter", "fixture");
    }
};

void test_complete_fixture_reconciles() {
    Fixture f;

    const auto empty_target = f.target("byte-range:0:0");
    const auto one_target = f.target("byte-range:1:2");
    const auto compound_target = f.target("byte-range:3:9");
    const auto empty = f.coverage(empty_target, "coverage:empty");
    const auto one = f.coverage(one_target, "coverage:one");
    const auto compound = f.coverage(compound_target, "coverage:compound");

    // History remains present. Only the latest decision is current.
    f.coverage_decision(empty, 0, "CLAIMS_PRESENT");
    f.coverage_decision(empty, 1, "NO_RULE_CONTENT");

    const auto single = f.claim("claim:single", "One complete rule.", {one});
    f.materialize(single, "single");
    f.claim_decision(single, 0, "RAISED", "ONTOLOGY_GAP");
    f.claim_decision(single, 1, "MATERIALIZED");

    const auto partial =
        f.claim("claim:partial", "One partly expressible rule.", {compound});
    f.materialize(partial, "partial");
    f.claim_decision(partial, 0, "PARTIAL", "RULE_LANGUAGE_GAP");

    const auto raised =
        f.claim("claim:raised", "One untypable rule.", {compound});
    f.claim_decision(raised, 0, "RAISED", "ONTOLOGY_GAP");

    const auto crossing = f.claim(
        "claim:crossing", "One rule supported across source leaves.",
        {one, compound});
    f.materialize(crossing, "crossing");
    f.claim_decision(crossing, 0, "MATERIALIZED");

    f.coverage_decision(one, 0, "CLAIMS_PRESENT");
    f.coverage_decision(compound, 0, "CLAIMS_PRESENT");

    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(report.ok, "zero, one, compound, and crossing claims reconcile: " +
                         report.error);
    CHECK(report.coverage_count == 3 && report.claim_count == 4 &&
              report.decision_count == 9,
          "the report counts enduring records and complete decision history");
}

void test_duplicate_and_contradictory_claims_remain_visible() {
    Fixture f;
    const auto target = f.target("byte-range:0:4");
    const auto coverage = f.coverage(target, "coverage:claims");

    const auto original =
        f.claim("claim:original", "The stated value is one.", {coverage});
    f.materialize(original, "original");
    f.claim_decision(original, 0, "MATERIALIZED");

    const auto duplicate =
        f.claim("claim:duplicate", "The stated value is one.", {coverage});
    f.claim_decision(duplicate, 0, "DUPLICATE", {}, original);

    const auto contradiction =
        f.claim("claim:contradiction", "The stated value is two.", {coverage});
    f.materialize(contradiction, "contradiction");
    f.claim_decision(contradiction, 0, "CONTRADICTORY", {}, original);

    f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");

    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(report.ok && report.claim_count == 3,
          "duplicate and contradictory claims reconcile without disappearing: " +
              report.error);
}

void test_superseded_claim_points_to_its_generalized_replacement() {
    Fixture f;
    const auto target = f.target("byte-range:0:4");
    const auto coverage = f.coverage(target, "coverage:claims");

    const auto narrow =
        f.claim("claim:narrow", "This cause produces a crisis.", {coverage});
    f.claim_decision(narrow, 0, "RAISED", "RULE_LANGUAGE_GAP");

    const auto generalized = f.claim(
        "claim:general", "Every listed cause produces a crisis.", {coverage});
    f.materialize(generalized, "generalized");
    f.claim_decision(generalized, 0, "MATERIALIZED");
    f.claim_decision(narrow, 1, "SUPERSEDED", {}, generalized);
    f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");

    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(report.ok && report.claim_count == 2 && report.decision_count == 4,
          "a narrower claim keeps its history and points to the current "
          "generalized replacement: " + report.error);
}

void test_missing_enumerated_leaf_fails() {
    Fixture f;
    f.target("byte-range:0:1");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(!report.ok && report.error.find("missing coverage") !=
                            std::string::npos,
          "an enumerated leaf cannot disappear before coverage");
}

void test_complete_partition_requires_exact_byte_cover() {
    {
        Fixture f;
        const auto representation = f.representation(6);
        const auto first =
            f.target_range(representation, 0, 2, "target:first");
        const auto second =
            f.target_range(representation, 4, 6, "target:second");
        f.exclusion(representation, 2, 4, "LAYOUT", "exclusion:middle");
        f.complete_partition(representation);
        const auto first_coverage = f.coverage(first, "coverage:first");
        const auto second_coverage = f.coverage(second, "coverage:second");
        f.coverage_decision(first_coverage, 0, "NO_RULE_CONTENT");
        f.coverage_decision(second_coverage, 0, "NO_RULE_CONTENT");

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(report.ok,
              "targets and typed exclusions can cover every source byte: " +
                  report.error);
    }
    {
        Fixture f;
        const auto representation = f.representation(6);
        const auto first =
            f.target_range(representation, 0, 2, "target:first");
        const auto second =
            f.target_range(representation, 4, 6, "target:second");
        f.exclusion(representation, 3, 4, "LAYOUT", "exclusion:middle");
        f.complete_partition(representation);
        const auto first_coverage = f.coverage(first, "coverage:first");
        const auto second_coverage = f.coverage(second, "coverage:second");
        f.coverage_decision(first_coverage, 0, "NO_RULE_CONTENT");
        f.coverage_decision(second_coverage, 0, "NO_RULE_CONTENT");

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok && report.error.find("gap [2,3)") != std::string::npos,
              "one missing source byte fails an asserted complete partition");
    }
    {
        Fixture f;
        const auto representation = f.representation(6);
        const auto first =
            f.target_range(representation, 0, 3, "target:first");
        const auto second =
            f.target_range(representation, 4, 6, "target:second");
        f.exclusion(representation, 2, 4, "SYNTAX", "exclusion:middle");
        f.complete_partition(representation);
        const auto first_coverage = f.coverage(first, "coverage:first");
        const auto second_coverage = f.coverage(second, "coverage:second");
        f.coverage_decision(first_coverage, 0, "NO_RULE_CONTENT");
        f.coverage_decision(second_coverage, 0, "NO_RULE_CONTENT");

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok && report.error.find("overlap") != std::string::npos,
              "one source byte cannot belong to a leaf and an exclusion");
    }
    {
        Fixture f;
        const auto representation = f.representation(6);
        const auto target =
            f.target_range(representation, 0, 2, "target:first");
        f.exclusion(representation, 2, 7, "LAYOUT", "exclusion:rest");
        f.complete_partition(representation);
        const auto coverage = f.coverage(target, "coverage:first");
        f.coverage_decision(coverage, 0, "NO_RULE_CONTENT");

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok &&
                  report.error.find("outside representation") !=
                      std::string::npos,
              "a partition range cannot exceed the exact source length");
    }
}

void test_partial_migration_makes_no_complete_partition_claim() {
    Fixture f;
    const auto representation = f.representation(6);
    const auto first =
        f.target_range(representation, 0, 2, "target:first");
    const auto second =
        f.target_range(representation, 4, 6, "target:second");
    const auto first_coverage = f.coverage(first, "coverage:first");
    const auto second_coverage = f.coverage(second, "coverage:second");
    f.coverage_decision(first_coverage, 0, "NO_RULE_CONTENT");
    f.coverage_decision(second_coverage, 0, "NO_RULE_CONTENT");

    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(report.ok,
          "partial migration stays valid until it explicitly asserts a "
          "complete source partition: " + report.error);
}

void test_complete_partition_contract_rejects_invalid_structure() {
    {
        Fixture f;
        const auto representation = f.representation(0);
        f.complete_partition(representation);
        f.complete_partition(representation);

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok &&
                  report.error.find("duplicate CompleteSourcePartition") !=
                      std::string::npos,
              "one representation cannot make two completeness assertions");
    }
    {
        Fixture f;
        const auto represented = f.representation(2);
        const auto other = f.representation(2);
        const auto selector = f.range(other, 0, 2, "range:other");
        const auto target = f.target("target:cross-representation");
        f.world.setProperty(target, "identity_context",
                            std::to_string(represented));
        f.world.setProperty(target, "target_representation",
                            std::to_string(represented));
        f.world.setProperty(target, "target_primary_selector",
                            std::to_string(selector));
        f.complete_partition(represented);
        const auto coverage = f.coverage(target, "coverage:target");
        f.coverage_decision(coverage, 0, "NO_RULE_CONTENT");

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok &&
                  report.error.find("selector belongs to source representation") !=
                      std::string::npos,
              "a partition leaf selector cannot belong to another "
              "representation");
    }
    {
        Fixture f;
        const auto represented = f.representation(1);
        const auto other = f.representation(1);
        const auto target =
            f.target_range(represented, 0, 1, "target:wrong-identity");
        f.world.setProperty(target, "identity_context",
                            std::to_string(other));
        f.complete_partition(represented);
        const auto coverage = f.coverage(target, "coverage:target");
        f.coverage_decision(coverage, 0, "NO_RULE_CONTENT");

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok &&
                  report.error.find(
                      "identity_context must equal target_representation") !=
                      std::string::npos,
              "a partition target cannot claim identity in another exact "
              "representation");
    }
    {
        Fixture f;
        const auto representation = f.representation(1);
        const auto target =
            f.target_range(representation, 0, 1, "target:all");
        f.exclusion(representation, 1, 1, "LAYOUT", "exclusion:empty");
        f.complete_partition(representation);
        const auto coverage = f.coverage(target, "coverage:all");
        f.coverage_decision(coverage, 0, "NO_RULE_CONTENT");

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok &&
                  report.error.find("must contain at least one byte") !=
                      std::string::npos,
              "an exclusion cannot hide an empty interval");
    }
    {
        Fixture f;
        const auto representation =
            f.world.createEntity("SourceRepresentationContext");
        f.world.setProperty(representation, "context_key",
                            "representation:missing-length");
        f.world.setProperty(representation, "source_media_type",
                            "UTF8_TEXT");
        f.complete_partition(representation);

        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok &&
                  report.error.find("source_byte_length") !=
                      std::string::npos,
              "an asserted partition fails loudly without exact source "
              "length metadata");
    }
}

void test_duplicate_coverage_fails() {
    Fixture f;
    const auto target = f.target("byte-range:0:1");
    const auto a = f.coverage(target, "coverage:a");
    const auto b = f.coverage(target, "coverage:b");
    f.coverage_decision(a, 0, "NO_RULE_CONTENT");
    f.coverage_decision(b, 0, "NO_RULE_CONTENT");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(!report.ok && report.error.find("duplicate coverage") !=
                            std::string::npos,
          "one source target cannot acquire two coverage truths");
}

void test_unlinked_claim_fails() {
    Fixture f;
    const auto target = f.target("byte-range:0:1");
    const auto coverage = f.coverage(target, "coverage:a");
    f.coverage_decision(coverage, 0, "NO_RULE_CONTENT");
    const auto claim = f.claim("claim:orphan", "No evidence.", {});
    f.claim_decision(claim, 0, "RAISED", "ONTOLOGY_GAP");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(!report.ok && report.error.find("CLAIM_SUPPORTED_BY") !=
                            std::string::npos,
          "every claim must cite at least one coverage record");
}

void test_silent_zero_claim_coverage_fails() {
    Fixture f;
    const auto target = f.target("byte-range:0:1");
    const auto coverage = f.coverage(target, "coverage:a");
    f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(!report.ok && report.error.find("zero claims") !=
                            std::string::npos,
          "zero claims requires an explicit no-rule-content decision");
}

void test_broken_decision_sequence_fails() {
    Fixture f;
    const auto target = f.target("byte-range:0:1");
    const auto coverage = f.coverage(target, "coverage:a");
    f.coverage_decision(coverage, 1, "NO_RULE_CONTENT");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(!report.ok && report.error.find("decision sequence") !=
                            std::string::npos,
          "decision history cannot start late or contain a gap");
}

void test_partial_claim_requires_typed_gap() {
    Fixture f;
    const auto target = f.target("byte-range:0:1");
    const auto coverage = f.coverage(target, "coverage:a");
    const auto claim = f.claim("claim:partial", "Partial.", {coverage});
    f.materialize(claim, "partial");
    f.claim_decision(claim, 0, "PARTIAL");
    f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(!report.ok && report.error.find("gap kind") != std::string::npos,
          "partial cannot hide whether ontology or rule language blocked it");
}

void test_partial_claim_accepts_source_gap() {
    Fixture f;
    const auto target = f.target("byte-range:0:1");
    const auto coverage = f.coverage(target, "coverage:source-gap");
    const auto claim =
        f.claim("claim:source-gap", "The source leaves a band open.",
                {coverage});
    f.materialize(claim, "source-gap-reading");
    f.claim_decision(claim, 0, "PARTIAL", "SOURCE_GAP");
    f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(report.ok,
          "a partial materialization can name a source defect without "
          "misclassifying it as ontology or rule language: " + report.error);
}

void test_duplicate_requires_related_claim() {
    Fixture f;
    const auto target = f.target("byte-range:0:1");
    const auto coverage = f.coverage(target, "coverage:a");
    const auto claim = f.claim("claim:duplicate", "Duplicate.", {coverage});
    f.claim_decision(claim, 0, "DUPLICATE");
    f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");
    const auto report =
        kg::reconcile_ingestion_ledger(f.world, f.enumerated);
    CHECK(!report.ok && report.error.find("related claim") !=
                            std::string::npos,
          "duplicate is a typed relation to a claim, not an unsupported label");
}

void test_superseded_requires_replacement_and_relinquishes_results() {
    {
        Fixture f;
        const auto target = f.target("byte-range:0:1");
        const auto coverage = f.coverage(target, "coverage:a");
        const auto claim =
            f.claim("claim:superseded", "Superseded.", {coverage});
        f.claim_decision(claim, 0, "SUPERSEDED");
        f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");
        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok && report.error.find("replacement claim") !=
                                std::string::npos,
              "superseded is a typed link to its replacement, not a label");
    }
    {
        Fixture f;
        const auto target = f.target("byte-range:0:1");
        const auto coverage = f.coverage(target, "coverage:a");
        const auto replacement =
            f.claim("claim:replacement", "Replacement.", {coverage});
        f.materialize(replacement, "replacement");
        f.claim_decision(replacement, 0, "MATERIALIZED");
        const auto old = f.claim("claim:old", "Old.", {coverage});
        f.materialize(old, "old");
        f.claim_decision(old, 0, "SUPERSEDED", {}, replacement);
        f.coverage_decision(coverage, 0, "CLAIMS_PRESENT");
        const auto report =
            kg::reconcile_ingestion_ledger(f.world, f.enumerated);
        CHECK(!report.ok && report.error.find("materialized results") !=
                                std::string::npos,
              "a superseded claim cannot keep authoritative graph results");
    }
}

}  // namespace

int main() {
    std::cout << "Ingestion ledger reconciliation" << std::endl;
    test_complete_fixture_reconciles();
    test_duplicate_and_contradictory_claims_remain_visible();
    test_superseded_claim_points_to_its_generalized_replacement();
    test_missing_enumerated_leaf_fails();
    test_complete_partition_requires_exact_byte_cover();
    test_partial_migration_makes_no_complete_partition_claim();
    test_complete_partition_contract_rejects_invalid_structure();
    test_duplicate_coverage_fails();
    test_unlinked_claim_fails();
    test_silent_zero_claim_coverage_fails();
    test_broken_decision_sequence_fails();
    test_partial_claim_requires_typed_gap();
    test_partial_claim_accepts_source_gap();
    test_duplicate_requires_related_claim();
    test_superseded_requires_replacement_and_relinquishes_results();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
