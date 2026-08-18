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

#include <algorithm>
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

kg::EntityID latest_claim_decision(const kg::KGModule& world,
                                   kg::EntityID claim) {
    kg::EntityID latest = kg::INVALID_ENTITY;
    unsigned long long latest_sequence = 0;
    for (const auto candidate : world.findByProperty(
             "decision_subject", std::to_string(claim))) {
        if (world.getType(candidate) != "ClaimDecision") continue;
        const auto sequence = std::stoull(
            world.getProperty(candidate, "decision_sequence"));
        if (latest == kg::INVALID_ENTITY || sequence > latest_sequence) {
            latest = candidate;
            latest_sequence = sequence;
        }
    }
    return latest;
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
                                                "SourceTarget") ||
                world.getRegistry().hasFacet(create->type,
                                             "source-partition")) {
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
    CHECK(targets.size() == 2057 && coverages.size() == 2057 &&
              claims.size() == 1554,
          "migrated production content persists 2057 atomic source leaves "
          "and 1554 enduring atomic claims");
    CHECK(world.findByType("CoverageDecision").size() == 2057 &&
              world.findByType("ClaimDecision").size() == 1559,
          "production retains 2057 coverage decisions and complete "
          "append-only histories for 1554 claims");
    CHECK(!world.getRegistry().hasFacet("SourceCoverage",
                                        "no-instance-declared") &&
              !world.getRegistry().hasFacet("IngestionClaim",
                                             "no-instance-declared") &&
              !world.getRegistry().hasFacet("CoverageDecision",
                                             "no-instance-declared") &&
              !world.getRegistry().hasFacet("ClaimDecision",
                                             "no-instance-declared"),
          "production ledger types no longer claim to have no instances");

    const std::string character_chapter =
        slurp(game_root + "/srd/cepheus/book1/character-creation.md");
    const std::string skills_chapter =
        slurp(game_root + "/srd/cepheus/book1/skills.md");
    std::multiset<std::string> selected;
    bool targets_resolve = targets.size() == 2057;
    std::size_t character_targets = 0;
    for (const auto target : targets) {
        const std::string representation_text =
            world.getProperty(target, "target_representation");
        if (representation_text.empty()) {
            targets_resolve = false;
            continue;
        }
        const auto representation = static_cast<kg::EntityID>(
            std::stoull(representation_text));
        const std::string source_file =
            world.getProperty(representation, "source_file");
        const std::string* source = nullptr;
        if (source_file == "book1/character-creation.md") {
            source = &character_chapter;
            ++character_targets;
        } else if (source_file == "book1/skills.md") {
            source = &skills_chapter;
        } else {
            targets_resolve = false;
            continue;
        }
        const auto result =
            logosphere::text::resolve_text_target(world, target, *source);
        targets_resolve = targets_resolve && result.ok;
        if (result.ok && source_file == "book1/character-creation.md") {
            selected.insert(result.text);
        }
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
        "anything else.",
        "Aging",
        "The effects of aging begin when a character reaches 34 years of age.",
        "At the end of the fourth term, and at the end of every term "
        "thereafter, the character must roll 2D6 on the Aging Table.",
        "Apply the character's total number of terms as a negative Dice "
        "Modifier on this table.",
        "2D6", "Effects of Aging",
        "\\-6",
        "Reduce three physical characteristics by 2, reduce one mental "
        "characteristic by 1",
        "\\-5", "Reduce three physical characteristics by 2.",
        "\\-4",
        "Reduce two physical characteristics by 2, reduce one physical "
        "characteristic by 1",
        "\\-3",
        "Reduce one physical characteristic by 2, reduce two physical "
        "characteristic by 1",
        "\\-2", "Reduce three physical characteristics by 1",
        "\\-1", "Reduce two physical characteristics by 1",
        "0", "Reduce one physical characteristic by 1",
        "1+", "No effect",
        "Aging Crisis",
        "If any characteristic is reduced to 0 by aging, then the character "
        "suffers an aging crisis.",
        "The character dies unless he can pay 1D6×10,000 Credits for "
        "medical care, which will bring any characteristics back up to 1.",
        "The character automatically fails any Qualification checks from "
        "now on – he must either continue in the career he is in or become "
        "a Drifter if he wishes to take any more terms."};
    const bool retained_previous_targets = std::includes(
        selected.begin(), selected.end(), expected.begin(), expected.end());
    CHECK(targets_resolve && character_targets == 1620 &&
              retained_previous_targets,
          "every migrated target resolves against its own exact source "
          "representation while retaining the earlier exact leaves");

    const auto ledger = kg::reconcile_ingestion_ledger(world, targets);
    CHECK(ledger.ok,
          "the complete production exact-evidence ledger closes: " +
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

    struct ExpectedRule {
        const char* type;
        const char* key;
    };
    const std::vector<ExpectedRule> aging_rules{
        {"RuleConstant", "aging_start_age"},
        {"RollableTable", "aging_table"},
        {"ModifyAttributesInGroup", "aging_m6_c0"},
        {"ModifyAttributesInGroup", "aging_m6_c1"},
        {"OutcomeSequence", "aging_m6"},
        {"OutcomeStep", "aging_m6_s0"},
        {"OutcomeStep", "aging_m6_s1"},
        {"TableEntry", "aging_row_m6"},
        {"ModifyAttributesInGroup", "aging_m5_c0"},
        {"TableEntry", "aging_row_m5"},
        {"ModifyAttributesInGroup", "aging_m4_c0"},
        {"ModifyAttributesInGroup", "aging_m4_c1"},
        {"OutcomeSequence", "aging_m4"},
        {"OutcomeStep", "aging_m4_s0"},
        {"OutcomeStep", "aging_m4_s1"},
        {"TableEntry", "aging_row_m4"},
        {"ModifyAttributesInGroup", "aging_m3_c0"},
        {"ModifyAttributesInGroup", "aging_m3_c1"},
        {"OutcomeSequence", "aging_m3"},
        {"OutcomeStep", "aging_m3_s0"},
        {"OutcomeStep", "aging_m3_s1"},
        {"TableEntry", "aging_row_m3"},
        {"ModifyAttributesInGroup", "aging_m2_c0"},
        {"TableEntry", "aging_row_m2"},
        {"ModifyAttributesInGroup", "aging_m1_c0"},
        {"TableEntry", "aging_row_m1"},
        {"ModifyAttributesInGroup", "aging_0_c0"},
        {"TableEntry", "aging_row_0"},
        {"NoEffect", "aging_1p_none"},
        {"TableEntry", "aging_row_1p"},
    };
    bool every_aging_rule_is_exact = true;
    for (const auto& expected_rule : aging_rules) {
        const kg::EntityID rule = find_addressable(
            world, expected_rule.type, expected_rule.key);
        every_aging_rule_is_exact =
            every_aging_rule_is_exact && rule != kg::INVALID_ENTITY &&
            !has_legacy_locator(world, rule) &&
            world.getProperty(rule, "origin_context") ==
                std::to_string(editions.front()) &&
            !world.getRelatedReverse(rule, "CLAIM_MATERIALIZES").empty();
    }
    CHECK(every_aging_rule_is_exact,
          "all 30 Aging rules use exact claims with no surviving structural "
          "locator");

    const kg::EntityID aging_floor =
        find_addressable(world, "TableEntry", "aging_row_m6");
    const kg::EntityID aging_floor_claim = find_addressable(
        world, "IngestionClaim", "aging_row_m6_claim");
    const kg::EntityID aging_floor_decision = find_typed_by_property(
        world, "ClaimDecision", "decision_subject",
        std::to_string(aging_floor_claim));
    CHECK(aging_floor != kg::INVALID_ENTITY &&
              aging_floor_claim != kg::INVALID_ENTITY &&
              aging_floor_decision != kg::INVALID_ENTITY &&
              world.getProperty(aging_floor, "roll_min_unbounded") == "true" &&
              world.hasProperty(aging_floor, "source_defect") &&
              world.getProperty(aging_floor_decision, "claim_disposition") ==
                  "PARTIAL" &&
              world.getProperty(aging_floor_decision, "claim_gap_kind") ==
                  "SOURCE_GAP" &&
              world.getRelated(aging_floor_claim, "CLAIM_SUPPORTED_BY").size() ==
                  4,
          "the inferred Aging floor remains executable but is exposed as a "
          "partial source-gap reading over exact table evidence");

    const kg::EntityID roll_aging =
        find_addressable(world, "ProcedureStep", "roll_aging");
    CHECK(roll_aging != kg::INVALID_ENTITY &&
              has_legacy_locator(world, roll_aging) &&
              world.getRelatedReverse(roll_aging, "CLAIM_MATERIALIZES").empty(),
          "the checklist-owned roll_aging step remains on its separate legacy "
          "evidence until that source leaf is migrated");

    const kg::EntityID injury_medical_coverage = find_addressable(
        world, "SourceCoverage", "injury_crisis_medical_coverage");
    const kg::EntityID injury_qualification_coverage = find_addressable(
        world, "SourceCoverage", "injury_crisis_qualification_coverage");
    const kg::EntityID aging_crisis_medical_coverage = find_addressable(
        world, "SourceCoverage", "aging_crisis_medical_coverage");
    const kg::EntityID aging_crisis_qualification_coverage = find_addressable(
        world, "SourceCoverage", "aging_crisis_qualification_coverage");
    const std::set<kg::EntityID> medical_evidence{
        injury_medical_coverage, aging_crisis_medical_coverage};
    const std::set<kg::EntityID> qualification_evidence{
        injury_qualification_coverage, aging_crisis_qualification_coverage};

    struct GeneralCrisisClaim {
        const char* key;
        const char* disposition;
        const std::set<kg::EntityID>* evidence;
    };
    const std::vector<GeneralCrisisClaim> generalized_claims{
        {"crisis_death_claim", "RAISED", &medical_evidence},
        {"crisis_cost_claim", "PARTIAL", &medical_evidence},
        {"crisis_restore_claim", "PARTIAL", &medical_evidence},
        {"crisis_qualification_claim", "RAISED", &qualification_evidence},
        {"crisis_career_claim", "RAISED", &qualification_evidence},
    };
    bool generalized_claims_span_both_occurrences = true;
    for (const auto& expected_claim : generalized_claims) {
        const kg::EntityID claim = find_addressable(
            world, "IngestionClaim", expected_claim.key);
        const kg::EntityID decision = latest_claim_decision(world, claim);
        const auto evidence = world.getRelated(claim, "CLAIM_SUPPORTED_BY");
        generalized_claims_span_both_occurrences =
            generalized_claims_span_both_occurrences &&
            claim != kg::INVALID_ENTITY && decision != kg::INVALID_ENTITY &&
            world.getProperty(decision, "claim_disposition") ==
                expected_claim.disposition &&
            world.getProperty(decision, "claim_gap_kind") ==
                "RULE_LANGUAGE_GAP" &&
            std::set<kg::EntityID>(evidence.begin(), evidence.end()) ==
                *expected_claim.evidence;
    }
    CHECK(generalized_claims_span_both_occurrences,
          "five generalized crisis claims retain exact evidence from both "
          "Injury Crisis and Aging Crisis");

    struct SupersededCrisisClaim {
        const char* old_key;
        const char* replacement_key;
    };
    const std::vector<SupersededCrisisClaim> superseded_claims{
        {"injury_crisis_death_claim", "crisis_death_claim"},
        {"injury_crisis_cost_claim", "crisis_cost_claim"},
        {"injury_crisis_restore_claim", "crisis_restore_claim"},
        {"injury_crisis_qualification_claim", "crisis_qualification_claim"},
        {"injury_crisis_career_claim", "crisis_career_claim"},
    };
    bool every_narrow_claim_is_superseded = true;
    for (const auto& expected_claim : superseded_claims) {
        const kg::EntityID old_claim = find_addressable(
            world, "IngestionClaim", expected_claim.old_key);
        const kg::EntityID replacement = find_addressable(
            world, "IngestionClaim", expected_claim.replacement_key);
        const kg::EntityID decision = latest_claim_decision(world, old_claim);
        every_narrow_claim_is_superseded =
            every_narrow_claim_is_superseded &&
            old_claim != kg::INVALID_ENTITY &&
            replacement != kg::INVALID_ENTITY &&
            world.findByProperty("decision_subject",
                                 std::to_string(old_claim)).size() == 2 &&
            decision != kg::INVALID_ENTITY &&
            world.getProperty(decision, "claim_disposition") ==
                "SUPERSEDED" &&
            world.getProperty(decision, "related_claim") ==
                std::to_string(replacement) &&
            world.getRelated(old_claim, "CLAIM_MATERIALIZES").empty();
    }
    CHECK(every_narrow_claim_is_superseded,
          "the five occurrence-specific Injury Crisis claims retain their "
          "initial decisions and point to generalized replacements");

    const kg::EntityID crisis_cost_claim = find_addressable(
        world, "IngestionClaim", "crisis_cost_claim");
    const kg::EntityID crisis_restore_claim = find_addressable(
        world, "IngestionClaim", "crisis_restore_claim");
    const kg::EntityID crisis_restore_value = find_addressable(
        world, "RuleConstant", "crisis_restore_value");
    CHECK(crisis_restore_value != kg::INVALID_ENTITY &&
              !has_legacy_locator(world, crisis_restore_value) &&
              world.getRelated(crisis_cost_claim, "CLAIM_MATERIALIZES") ==
                  std::vector<kg::EntityID>{credits} &&
              world.getRelated(crisis_restore_claim, "CLAIM_MATERIALIZES") ==
                  std::vector<kg::EntityID>{crisis_restore_value},
          "general crisis claims own the shared Credits and restoration-value "
          "materializations without legacy evidence");

    const kg::EntityID aging_crisis_unpaid = find_addressable(
        world, "StepRoute", "aging_crisis_unpaid");
    const kg::EntityID aging_crisis_route_claim = find_addressable(
        world, "IngestionClaim", "aging_crisis_unpaid_route_claim");
    const kg::EntityID aging_crisis_route_decision =
        latest_claim_decision(world, aging_crisis_route_claim);
    CHECK(aging_crisis_unpaid != kg::INVALID_ENTITY &&
              aging_crisis_route_claim != kg::INVALID_ENTITY &&
              aging_crisis_route_decision != kg::INVALID_ENTITY &&
              !has_legacy_locator(world, aging_crisis_unpaid) &&
              world.getProperty(aging_crisis_route_decision,
                                "claim_disposition") == "PARTIAL" &&
              world.getProperty(aging_crisis_route_decision,
                                "claim_gap_kind") == "RULE_LANGUAGE_GAP" &&
              world.getRelated(aging_crisis_route_claim,
                               "CLAIM_MATERIALIZES") ==
                  std::vector<kg::EntityID>{aging_crisis_unpaid} &&
              world.getRelated(aging_crisis_route_claim,
                               "CLAIM_SUPPORTED_BY") ==
                  std::vector<kg::EntityID>{aging_crisis_medical_coverage},
          "the aging-specific procedure route has exact partial evidence "
          "without pretending it expresses the complete pay-or-die rule");

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
    std::size_t materialized = 0;
    std::size_t superseded = 0;
    std::size_t duplicate = 0;
    for (const auto claim : claims) {
        const auto decision = latest_claim_decision(world, claim);
        const std::string disposition =
            world.getProperty(decision, "claim_disposition");
        partial += disposition == "PARTIAL";
        raised += disposition == "RAISED";
        materialized += disposition == "MATERIALIZED";
        superseded += disposition == "SUPERSEDED";
        duplicate += disposition == "DUPLICATE";
    }
    CHECK(no_rule == 38 && claims_present == 2019 && partial == 7 &&
              raised == 253 && materialized == 1220 && superseded == 5 &&
              duplicate == 69,
          "the production ledger exposes all current coverage judgements and "
          "claim dispositions, including repeated source occurrences");
}

}  // namespace

int main() {
    std::cout << "Logovger production rule identity\n";
    test_production_rules_use_exact_edition_identity();
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
