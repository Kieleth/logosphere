// Production rule identity must name the exact corpus edition. Legacy
// document contexts remain temporarily as citation origins only.

#undef NDEBUG

#include "chargen/procedure_catalog.h"
#include "chargen/rule_seed_loader.h"

#include "logosphere/kg/ingestion_ledger.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/text/source_target.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                      \
    do {                                                               \
        if (condition) {                                               \
            ++passed;                                                  \
        } else {                                                       \
            ++failed;                                                  \
            std::cout << "FAIL: " << message << '\n';                 \
        }                                                              \
    } while (false)

std::string slurp(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream bytes;
    bytes << input.rdbuf();
    return bytes.str();
}

kg::OntologyRegistry game_registry() {
    auto registry = logosphere::ontology::registry();
    registry.extend(rulebook::ontology::registry());
    registry.extend(cepheus_book1_skills::ontology::registry());
    registry.extend(cepheus_book1_character_creation::ontology::registry());
    return registry;
}

kg::EntityID find_addressable(const kg::KGModule& world,
                              const std::string& type,
                              const std::string& key) {
    for (const auto candidate : world.findByProperty("entity_key", key)) {
        if (world.getType(candidate) == type) return candidate;
    }
    return kg::INVALID_ENTITY;
}

kg::EntityID find_typed_by_property(const kg::KGModule& world,
                                    const std::string& type,
                                    const std::string& property,
                                    const std::string& value) {
    for (const auto candidate : world.findByProperty(property, value)) {
        if (world.getType(candidate) == type) return candidate;
    }
    return kg::INVALID_ENTITY;
}

bool has_legacy_locator(const kg::KGModule& world, kg::EntityID entity) {
    for (const char* field :
         {"source_file", "source_section", "source_quote", "source_kind",
          "source_table", "source_row", "source_column"}) {
        if (world.hasProperty(entity, field)) return true;
    }
    return false;
}

void test_production_rules_use_exact_edition_identity() {
    const std::string game_root =
        std::string(LOGOSPHERE_SOURCE_DIR) + "/examples/logovger";
    std::vector<kg::SeedEnvelope> seeds;
    std::string error;
    CHECK(logovger::parse_rule_seeds(game_root, seeds, error),
          "the production rule seeds parse before identity inspection: " +
              error);
    if (seeds.size() != logovger::kRuleSeedCount) return;

    logosphere::text::SourceCorpusDeclaration corpus;
    CHECK(logovger::declare_rule_source_corpus(seeds, corpus, error) &&
              corpus.source_layer == "cepheus" &&
              corpus.representations.size() == 2,
          "the application declares both exact rule-source representations: " +
              error);
    auto mismatched = seeds;
    mismatched.front().source.commit = "different-revision";
    CHECK(!logovger::declare_rule_source_corpus(mismatched, corpus, error) &&
              error.find("revision") != std::string::npos,
          "the rule corpus refuses mixed source revisions");
    auto undeclared = seeds;
    undeclared.front().source.file = "book1/undeclared.md";
    CHECK(!logovger::declare_rule_source_corpus(undeclared, corpus, error) &&
              error.find("undeclared") != std::string::npos,
          "a seed cannot silently expand application-owned corpus membership");

    bool no_document_identity_references = true;
    for (const char* relative : logovger::kRuleSeeds) {
        if (slurp(game_root + "/" + relative)
                .find("@@entity/source-document") != std::string::npos) {
            no_document_identity_references = false;
        }
    }
    CHECK(no_document_identity_references,
          "production references name the exact ingestion edition, not a "
          "legacy source document context");

    kg::KGModule world(game_registry());
    world.setMode(kg::KGMode::MINIMAL);
    const auto procedures = logovger::make_chargen_procedure_registry();
    CHECK(logovger::load_rule_seeds(world, game_root, procedures, error),
          "the production rules load for identity inspection: " + error);

    const auto editions = world.findByType("IngestionEditionContext");
    CHECK(editions.size() == 1,
          "one exact ingestion edition scopes the complete production corpus");
    if (editions.size() != 1) return;
    std::cout << "  [measure] edition context: "
              << world.getProperty(editions.front(), "context_key") << '\n';

    bool every_rule_uses_edition = true;
    std::size_t addressable_rules = 0;
    for (const auto& seed : seeds) {
        for (const auto& op : seed.ops) {
            const auto* create = std::get_if<kg::KGOpCreateEntity>(&op);
            if (!create ||
                !world.getRegistry().isSubtypeOf(create->type,
                                                  "Addressable")) {
                continue;
            }
            if (world.getRegistry().isSubtypeOf(create->type,
                                                "SourceSelector") ||
                world.getRegistry().isSubtypeOf(create->type,
                                                "SourceTarget")) {
                continue;
            }
            ++addressable_rules;
            std::vector<kg::EntityID> exact_matches;
            for (const auto candidate :
                 world.findByProperty("entity_key", create->as)) {
                if (world.getType(candidate) == create->type) {
                    exact_matches.push_back(candidate);
                }
            }
            if (exact_matches.size() != 1 ||
                world.getProperty(exact_matches.front(),
                                  "identity_context") !=
                    std::to_string(editions.front())) {
                every_rule_uses_edition = false;
            }
        }
    }
    CHECK(addressable_rules > 0 && every_rule_uses_edition,
          "every production Addressable rule is identified inside that "
          "edition");

    CHECK(!world.findByType("SourceDocumentContext").empty(),
          "document contexts remain only as the explicit transitional "
          "origin for legacy citation evidence");

    const auto targets = world.findByType("SourceTarget");
    const auto coverages = world.findByType("SourceCoverage");
    const auto claims = world.findByType("IngestionClaim");
    CHECK(targets.size() == 28 && coverages.size() == 28 &&
              claims.size() == 24,
          "Injury Crisis, Medical Care, and Medical Debt persist 28 atomic "
          "source leaves and 24 atomic claims");
    CHECK(world.findByType("CoverageDecision").size() == 28 &&
              world.findByType("ClaimDecision").size() == 24,
          "every production coverage and claim has its initial append-only "
          "decision");
    CHECK(!world.getRegistry().hasFacet("SourceCoverage",
                                        "no-instance-declared") &&
              !world.getRegistry().hasFacet("IngestionClaim",
                                             "no-instance-declared") &&
              !world.getRegistry().hasFacet("CoverageDecision",
                                             "no-instance-declared") &&
              !world.getRegistry().hasFacet("ClaimDecision",
                                             "no-instance-declared"),
          "production ledger types no longer claim to have no instances");

    const std::string chapter =
        slurp(game_root + "/srd/cepheus/book1/character-creation.md");
    std::multiset<std::string> selected;
    bool targets_resolve = targets.size() == 28;
    for (const auto target : targets) {
        const auto result =
            logosphere::text::resolve_text_target(world, target, chapter);
        targets_resolve = targets_resolve && result.ok;
        if (result.ok) selected.insert(result.text);
    }
    const std::multiset<std::string> expected{
        "Injury Crisis",
        "If any characteristic is reduced to 0, then the character suffers "
        "an injury crisis.",
        "The character dies unless he can pay 1D6×10,000 Credits for "
        "medical care, which will bring any characteristics back up to 1.",
        "The character automatically fails any Qualification checks from "
        "now on – he must either continue in the career he is in or become "
        "a Drifter if he wishes to take any more terms.",
        "Medical Care",
        "If your character has been injured, then medical care may be able "
        "to undo the effects of damage.",
        "The restoration of a lost characteristic costs Cr5,000 per point.",
        "If your character was injured in the service of a patron or "
        "organization, then a portion of his medical care may be paid for "
        "by that patron.",
        "Roll 2D6 on the table below, adding your Rank as a DM.",
        "The result is how much of his medical care is paid for by his "
        "employer.",
        "Career",
        "Roll of 4+",
        "Roll of 8+",
        "Roll of 12+",
        "Aerospace System Defense, Marine, Maritime System Defense, Navy, "
        "Scout, Surface System Defense",
        "75%", "100%", "100%",
        "Agent, Athlete, Bureaucrat, Diplomat, Entertainer, Hunter, "
        "Mercenary, Merchant, Noble, Physician, Pirate, Scientist, "
        "Technician",
        "50%", "75%", "100%",
        "Barbarian, Belter, Colonist, Drifter, Rogue",
        "0%", "50%", "75%",
        "Medical Debt",
        "During finishing touches, you must pay any outstanding costs from "
        "medical care or anagathic drugs out of your Benefits before "
        "anything else."};
    CHECK(targets_resolve && selected == expected,
          "every migrated target resolves to its exact source leaf, "
          "including duplicate table values");

    const auto ledger = kg::reconcile_ingestion_ledger(world, targets);
    CHECK(ledger.ok,
          "the production Injury Crisis and Medical Care ledger closes: " +
              ledger.error);

    const kg::EntityID credits =
        find_addressable(world, "Currency", "credits");
    CHECK(credits != kg::INVALID_ENTITY,
          "the migrated Credits rule remains loaded");
    if (credits != kg::INVALID_ENTITY) {
        CHECK(!has_legacy_locator(world, credits) &&
                  world.getProperty(credits, "origin_context") ==
                      std::to_string(editions.front()),
              "Credits has edition origin and no surviving legacy locator "
              "field");
        CHECK(world.getRelatedReverse(credits, "CLAIM_MATERIALIZES").size() ==
                  1,
              "one exact evidenced claim materializes the Credits rule");
    }

    const kg::EntityID restoration_cost = find_addressable(
        world, "RuleConstant", "medical_care_restoration_cost_per_point");
    CHECK(restoration_cost != kg::INVALID_ENTITY,
          "Medical Care materializes its fixed restoration cost");
    if (restoration_cost != kg::INVALID_ENTITY) {
        CHECK(world.getProperty(restoration_cost, "constant_value") ==
                      "5000" &&
                  !has_legacy_locator(world, restoration_cost) &&
                  world.getProperty(restoration_cost, "origin_context") ==
                      std::to_string(editions.front()),
              "the restoration cost is exact, edition-origin rule data with "
              "no legacy locator");
    }

    const kg::EntityID cost_claim = find_addressable(
        world, "IngestionClaim", "medical_care_cost_claim");
    CHECK(cost_claim != kg::INVALID_ENTITY &&
              world.getRelated(cost_claim, "CLAIM_MATERIALIZES") ==
                  std::vector<kg::EntityID>{restoration_cost} &&
              world.getRelated(cost_claim, "CLAIM_RESOLVED_AGAINST") ==
                  std::vector<kg::EntityID>{credits},
          "the partial cost claim materializes the constant and records the "
          "prior Credits concept it resolved against");

    const kg::EntityID table_claim = find_addressable(
        world, "IngestionClaim", "medical_care_group1_roll4_claim");
    CHECK(table_claim != kg::INVALID_ENTITY &&
              world.getRelated(table_claim, "CLAIM_SUPPORTED_BY").size() ==
                  3,
          "a medical-coverage percentage claim retains career, threshold, "
          "and result-cell evidence");

    const kg::EntityID benefit_tables =
        find_addressable(world, "SubjectLookupTable", "benefit_tables");
    const kg::EntityID finish_character =
        find_addressable(world, "ProcedureStep", "finish_character");
    const kg::EntityID medical_debt_coverage = find_addressable(
        world, "SourceCoverage", "medical_debt_sentence_coverage");
    const kg::EntityID medical_debt_medical_claim = find_addressable(
        world, "IngestionClaim", "medical_debt_medical_claim");
    const kg::EntityID medical_debt_anagathic_claim = find_addressable(
        world, "IngestionClaim", "medical_debt_anagathic_claim");
    const kg::EntityID medical_debt_priority_claim = find_addressable(
        world, "IngestionClaim", "medical_debt_priority_claim");
    CHECK(benefit_tables != kg::INVALID_ENTITY &&
              finish_character != kg::INVALID_ENTITY &&
              medical_debt_coverage != kg::INVALID_ENTITY &&
              medical_debt_medical_claim != kg::INVALID_ENTITY &&
              medical_debt_anagathic_claim != kg::INVALID_ENTITY &&
              medical_debt_priority_claim != kg::INVALID_ENTITY,
          "Medical Debt resolves its three claims against existing Benefits, "
          "Medical Care, and finishing-touch graph concepts");
    if (medical_debt_medical_claim != kg::INVALID_ENTITY &&
        medical_debt_anagathic_claim != kg::INVALID_ENTITY &&
        medical_debt_priority_claim != kg::INVALID_ENTITY) {
        CHECK(world.getRelated(medical_debt_medical_claim,
                               "CLAIM_SUPPORTED_BY") ==
                      std::vector<kg::EntityID>{medical_debt_coverage} &&
                  world.getRelated(medical_debt_anagathic_claim,
                                   "CLAIM_SUPPORTED_BY") ==
                      std::vector<kg::EntityID>{medical_debt_coverage} &&
                  world.getRelated(medical_debt_priority_claim,
                                   "CLAIM_SUPPORTED_BY") ==
                      std::vector<kg::EntityID>{medical_debt_coverage},
              "one compound Medical Debt source leaf supports three atomic "
              "claims without duplicating its evidence target");
        const auto medical_resolved = world.getRelated(
            medical_debt_medical_claim, "CLAIM_RESOLVED_AGAINST");
        const std::set<kg::EntityID> expected_medical_resolved{
            restoration_cost, benefit_tables, finish_character};
        CHECK(std::set<kg::EntityID>(medical_resolved.begin(),
                                    medical_resolved.end()) ==
                  expected_medical_resolved,
              "the medical-cost debt claim records all three prior concepts "
              "used during interpretation");
        const kg::EntityID anagathic_decision = find_typed_by_property(
            world, "ClaimDecision", "decision_subject",
            std::to_string(medical_debt_anagathic_claim));
        CHECK(anagathic_decision != kg::INVALID_ENTITY &&
                  world.getProperty(anagathic_decision, "claim_disposition") ==
                      "RAISED" &&
                  world.getProperty(anagathic_decision, "claim_gap_kind") ==
                      "ONTOLOGY_GAP",
              "the absent anagathic concept is exposed as an ontology gap, "
              "not hidden as executable rule data");
    }

    std::size_t no_rule = 0;
    std::size_t claims_present = 0;
    for (const auto decision : world.findByType("CoverageDecision")) {
        const std::string judgement =
            world.getProperty(decision, "coverage_judgement");
        no_rule += judgement == "NO_RULE_CONTENT";
        claims_present += judgement == "CLAIMS_PRESENT";
    }
    std::size_t partial = 0;
    std::size_t raised = 0;
    for (const auto decision : world.findByType("ClaimDecision")) {
        const std::string disposition =
            world.getProperty(decision, "claim_disposition");
        partial += disposition == "PARTIAL";
        raised += disposition == "RAISED";
    }
    CHECK(no_rule == 4 && claims_present == 24 && partial == 2 &&
              raised == 22,
          "the complete production ledger exposes four zero-claim leaves, "
          "two partial claims, and twenty-two raised claims");
}

}  // namespace

int main() {
    std::cout << "Logovger production rule identity\n";
    test_production_rules_use_exact_edition_identity();
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
