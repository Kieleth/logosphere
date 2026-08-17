// Production rule identity must name the exact corpus edition. Legacy
// document contexts remain temporarily as citation origins only.

#undef NDEBUG

#include "chargen/procedure_catalog.h"
#include "chargen/rule_seed_loader.h"

#include "logosphere/kg/kg_module.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <sstream>
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
}

}  // namespace

int main() {
    std::cout << "Logovger production rule identity\n";
    test_production_rules_use_exact_edition_identity();
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
