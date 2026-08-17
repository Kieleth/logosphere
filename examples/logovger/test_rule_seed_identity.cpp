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
    CHECK(targets.size() == 4 && coverages.size() == 4 &&
              claims.size() == 6,
          "Injury Crisis persists four atomic source leaves and six atomic "
          "claims");
    CHECK(world.findByType("CoverageDecision").size() == 4 &&
              world.findByType("ClaimDecision").size() == 6,
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
    std::set<std::string> selected;
    bool targets_resolve = targets.size() == 4;
    for (const auto target : targets) {
        const auto result =
            logosphere::text::resolve_text_target(world, target, chapter);
        targets_resolve = targets_resolve && result.ok;
        if (result.ok) selected.insert(result.text);
    }
    CHECK(targets_resolve &&
              selected.count("Injury Crisis") == 1 &&
              selected.count(
                  "If any characteristic is reduced to 0, then the character "
                  "suffers an injury crisis.") == 1 &&
              selected.count(
                  "The character dies unless he can pay 1D6×10,000 Credits "
                  "for medical care, which will bring any characteristics "
                  "back up to 1.") == 1 &&
              selected.count(
                  "The character automatically fails any Qualification "
                  "checks from now on – he must either continue in the "
                  "career he is in or become a Drifter if he wishes to take "
                  "any more terms.") == 1,
          "every Injury Crisis target resolves to the exact selected source "
          "bytes");

    const auto ledger = kg::reconcile_ingestion_ledger(world, targets);
    CHECK(ledger.ok,
          "the production Injury Crisis ledger closes: " + ledger.error);

    kg::EntityID credits = kg::INVALID_ENTITY;
    for (const auto candidate : world.findByType("Currency")) {
        if (world.getProperty(candidate, "entity_key") == "credits") {
            credits = candidate;
        }
    }
    CHECK(credits != kg::INVALID_ENTITY,
          "the migrated Credits rule remains loaded");
    if (credits != kg::INVALID_ENTITY) {
        bool has_legacy_locator = false;
        for (const char* field :
             {"source_file", "source_section", "source_quote", "source_kind",
              "source_table", "source_row", "source_column"}) {
            has_legacy_locator =
                has_legacy_locator || world.hasProperty(credits, field);
        }
        CHECK(!has_legacy_locator &&
                  world.getProperty(credits, "origin_context") ==
                      std::to_string(editions.front()),
              "Credits has edition origin and no surviving legacy locator "
              "field");
        CHECK(world.getRelatedReverse(credits, "CLAIM_MATERIALIZES").size() ==
                  1,
              "one exact evidenced claim materializes the Credits rule");
    }
}

}  // namespace

int main() {
    std::cout << "Logovger production rule identity\n";
    test_production_rules_use_exact_edition_identity();
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
