// The game's own book, held to the schema, and both held to the graph.
//
// This is the gate of the loop the project now runs on: a chapter is
// written as narrative, reflected into the graph by the extractor, and
// played from there. Every red condition below is one way the loop can
// silently stop being a loop:
//
//   1. CLOSED VOCABULARY, BOTH DIRECTIONS. Every value the schema's
//      kind enum declares has exactly one entity in the graph, and
//      every entity's key is a declared value. A kind written into the
//      book but missing from the schema is refused at load (the enum's
//      job); a kind declared in the schema but absent from the book is
//      caught HERE, because a declared capability nothing instantiates
//      reads as working. Same for the season modes.
//   2. THE OPEN QUESTIONS ARE RECORDS. The book's Unsettled section
//      loads as entities the machinery can count. An undecided rule
//      and a nonexistent rule must not look identical from inside.
//   3. THE BOOK'S WORDS ARE THE GRAPH'S WORDS. No shipping source
//      spells a kind or a season mode. Same defect class as the
//      characteristics scan, same non-vacuity: the forbidden list is
//      built from the graph, so a sixth kind is a sixth forbidden word
//      with nothing to update here.
//   4. TWO BOOKS, TWO EDITIONS. The world carries one edition per
//      corpus and the combined edition names both, so a change to
//      EITHER book changes what a tape pins. Pinning only one was the
//      exact shape of the rank bug: a gate that covers half of what it
//      reads as covering.

#undef NDEBUG

#include "procedure_catalog.h"
#include "rule_loader.h"
#include "test_support.h"

#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/voyager_chargen_ontology_registry.h"

#include <iostream>
#include <map>
#include <set>
#include <string>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                       \
    do {                                                                \
        if (condition) {                                                \
            ++passed;                                                   \
        } else {                                                        \
            ++failed;                                                   \
            std::cout << "FAIL: " << message << '\n';                   \
        }                                                               \
    } while (false)

kg::OntologyRegistry game_registry() {
    auto registry = logosphere::ontology::registry();
    registry.extend(rulebook::ontology::registry());
    registry.extend(voyager_chargen::ontology::registry());
    return registry;
}

// Every declared value instantiated exactly once, every instance
// declared: the closed vocabulary, measured from both sides.
void check_vocabulary(const kg::KGModule& world,
                      const kg::OntologyRegistry& registry,
                      const std::string& enum_name,
                      const std::string& entity_type,
                      const std::string& key_slot,
                      std::set<std::string>& graph_words) {
    const auto& enums = registry.enumTypes();
    const auto declared = enums.find(enum_name);
    CHECK(declared != enums.end(),
          "the schema declares no enum '" << enum_name << "'");
    if (declared == enums.end()) return;

    std::map<std::string, int> instances;
    for (const kg::EntityID id : world.findByType(entity_type)) {
        instances[world.getProperty(id, key_slot)]++;
        graph_words.insert(world.getProperty(id, "name"));
        graph_words.insert(world.getProperty(id, key_slot));
    }
    for (const auto& value : declared->second.members) {
        const auto found = instances.find(value);
        CHECK(found != instances.end(),
              enum_name << " declares '" << value << "' and the book "
              "defines no such " << entity_type << ": a declared "
              "capability nothing instantiates reads as working");
        if (found != instances.end()) {
            CHECK(found->second == 1,
                  "'" << value << "' has " << found->second << " "
                  << entity_type << " entities; the book defines each "
                  "kind once");
        }
    }
    for (const auto& [key, count] : instances) {
        (void)count;
        CHECK(declared->second.members.count(key) > 0,
              entity_type << " '" << key << "' is in the graph with a "
              "key the schema does not declare, which the load should "
              "have refused");
    }
}

}  // namespace

int main() {
    const auto registry = game_registry();
    kg::KGModule world(game_registry());
    world.setMode(kg::KGMode::MINIMAL);

    const auto primitives = voyager::make_procedure_registry();
    std::string error;
    if (!voyager::load_rules(world, VOYAGER_GAME_DIR, VOYAGER_CORPUS_DIR,
                             VOYAGER_BOOK_CORPUS_DIR,
                             primitives, error)) {
        std::cout << "FAIL: the rules did not verify or load: " << error
                  << '\n';
        return 1;
    }

    // ---- 1. the closed vocabularies, both directions ---------------
    std::set<std::string> graph_words;
    check_vocabulary(world, registry, "MomentKindKey", "MomentKind",
                     "moment_kind_key", graph_words);
    check_vocabulary(world, registry, "SeasonModeKey", "SeasonMode",
                     "season_mode_key", graph_words);

    // Each kind carries the book's own defining words, because the
    // referee narrates kinds from the book, never from a model's
    // private idea of the word.
    for (const kg::EntityID id : world.findByType("MomentKind")) {
        CHECK(!world.getProperty(id, "source_quote").empty(),
              "kind '" << world.getProperty(id, "name")
                       << "' carries no defining quote");
    }

    // ---- 2. the open questions are records -------------------------
    const auto unsettled = world.findByType("UnsettledQuestion");
    CHECK(!unsettled.empty(),
          "the book's Unsettled section loaded as nothing; an undecided "
          "rule and a nonexistent rule now look identical");
    for (const kg::EntityID id : unsettled) {
        CHECK(!world.getProperty(id, "question_text").empty(),
              "an unsettled question with no text is a record of "
              "nothing");
    }

    // ---- 3. the book's words are the graph's words -----------------
    graph_words.erase("");
    CHECK(graph_words.size() >= 10,
          "only " << graph_words.size() << " forbidden words were "
          "collected, so this scan is weaker than it reads");
    std::size_t generated_skipped = 0;
    const auto sources =
        voyager_test::shipping_sources(VOYAGER_GAME_DIR, generated_skipped);
    CHECK(!sources.empty(),
          "no shipping source was scanned, so this check proved nothing");
    CHECK(generated_skipped >= 1,
          "the generated-file exclusion excluded nothing: either "
          "src/generated moved or the path match broke, and a broken "
          "match reports the schema's own generated output as leaks");
    for (const auto& path : sources) {
        const std::string text = voyager_test::slurp(path);
        CHECK(!text.empty(), "could not read " << path);
        for (const std::string& word : graph_words) {
            CHECK(!voyager_test::names_word(text, word),
                  path.filename().string() << " spells '" << word
                  << "'. The book's vocabulary is the graph's; a name "
                     "in C++ is the leak this game was written to not "
                     "have.");
        }
    }

    // ---- 4. two books, two editions --------------------------------
    const auto editions = world.findByType("IngestionEditionContext");
    CHECK(editions.size() == 2,
          "two corpora loaded and the world holds " << editions.size()
          << " edition context(s); a tape would pin the wrong rules");
    const std::string combined = voyager::rules_edition(world);
    CHECK(combined.find("voyager-book") != std::string::npos,
          "the combined edition does not name the game's own book, so "
          "rewriting a chapter would not change what a tape pins");
    CHECK(combined.find("+") != std::string::npos,
          "the combined edition holds one part; a change to one book "
          "must change the whole string");

    std::cout << (failed == 0 ? "OK " : "FAILED ") << passed
              << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
