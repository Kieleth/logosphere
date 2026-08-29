// The rules, into the world, verified on the way in.
//
// Two corpora now feed this game: the vendored published book it grew
// from, and Voyager's own book, authored in this repository and
// ingested by exactly the same machinery. Each corpus is its own
// group: its own source layer, its own file list, its own seeds in
// dependency order, its own edition. The game will not start on rules
// that did not verify, and it says which seed and why rather than
// starting half-loaded.
//
// The bytes the seeds cite live OUTSIDE this game, under corpora/, and
// each root arrives from a logosphere_game_corpus() declaration in
// CMake (VOYAGER_CORPUS_DIR, VOYAGER_BOOK_CORPUS_DIR). Never derived
// from this file's location: a book that lives inside one game's tree
// is a book a second game cannot read without changing the first.

#ifndef VOYAGER_RULE_LOADER_H
#define VOYAGER_RULE_LOADER_H

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/seed_verifier.h"
#include "logosphere/rules/procedure_runner.h"
#include "logosphere/text/source_corpus.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace voyager {

// ORDER IS DEPENDENCY ORDER, within a group and across groups. The
// published book's rules load first; the game's own book depends on
// nothing over there today, but a later chapter may point at an
// entity the first corpus owns, and load order is the only order.
inline constexpr const char* kCepheusSeeds[] = {
    "seeds/voyager_cepheus_rules.json",
    "seeds/voyager_chargen_procedure.json",
};
inline constexpr const char* kCepheusFiles[] = {
    "book1/character-creation.md",
};
inline constexpr const char* kBookSeeds[] = {
    "seeds/voyager_book_rules.json",
    "seeds/voyager_book_play_rules.json",
    "seeds/voyager_life_procedure.json",
};
inline constexpr const char* kBookFiles[] = {
    "01-the-shape-of-a-career.md",
    "02-seasons-and-moments-in-play.md",
};

// One corpus and everything this game reads from it. A third book is
// a row in load_rules, not a new mechanism.
struct CorpusGroup {
    const char* const* seeds;
    std::size_t seed_count;
    const char* const* files;
    std::size_t file_count;
};

// L0 byte access: the file, exactly, or a reason. No normalisation
// anywhere on this path, because byte-exact is what makes a citation
// worth having.
class CorpusFiles final : public logosphere::text::SourceAccess {
public:
    explicit CorpusFiles(std::string root) : root_(std::move(root)) {}

    logosphere::text::SourceReadResult read_exact(
        const logosphere::text::SourceRepresentationDeclaration& declaration)
        const override {
        if (declaration.source_file.find("..") != std::string::npos) {
            return {false, {}, "path traversal is not allowed"};
        }
        std::ifstream file(root_ + "/" + declaration.source_file,
                           std::ios::binary);
        if (!file) return {false, {}, "file is unreadable"};
        std::ostringstream bytes;
        bytes << file.rdbuf();
        if (file.bad()) return {false, {}, "file read failed"};
        return {true, bytes.str(), {}};
    }

private:
    std::string root_;
};

inline bool parse_rule_seeds(const std::string& game_root,
                             const CorpusGroup& group,
                             std::vector<kg::SeedEnvelope>& seeds,
                             std::string& why) {
    seeds.clear();
    why.clear();
    for (std::size_t i = 0; i < group.seed_count; ++i) {
        const char* relative = group.seeds[i];
        std::ifstream file(game_root + "/" + relative, std::ios::binary);
        if (!file) {
            why = std::string(relative) + " is unreadable";
            return false;
        }
        std::ostringstream text;
        text << file.rdbuf();
        const kg::SeedParseResult parsed =
            kg::parse_seed_envelope(text.str());
        if (!parsed.ok()) {
            why = std::string(relative) + ": " + parsed.error;
            return false;
        }
        seeds.push_back(std::move(parsed.seed));
    }
    return true;
}

// The corpus every seed of a group declares it was read from. Derived
// from the seeds themselves rather than restated here, so a seed that
// starts citing a second file is a loud failure instead of a silent
// one.
inline bool declare_corpus(
    const std::vector<kg::SeedEnvelope>& seeds,
    const CorpusGroup& group,
    logosphere::text::SourceCorpusDeclaration& corpus,
    std::string& why) {
    corpus = {};
    why.clear();
    if (seeds.empty()) {
        why = "no rule seeds: a game that needs rules must not start "
              "with none";
        return false;
    }
    const std::string& layer = seeds.front().layer;
    const std::string& revision = seeds.front().source.commit;
    for (const auto& seed : seeds) {
        if (seed.layer != layer) {
            why = "rule seeds declare mixed source layers";
            return false;
        }
        if (seed.source.commit != revision) {
            why = "rule seeds declare mixed source revisions";
            return false;
        }
        bool declared = false;
        for (std::size_t i = 0; i < group.file_count; ++i) {
            if (seed.source.file == group.files[i]) declared = true;
        }
        if (!declared) {
            why = "seed cites a file this game did not declare: " +
                  seed.source.file;
            return false;
        }
    }
    corpus.source_layer = layer;
    for (std::size_t i = 0; i < group.file_count; ++i) {
        corpus.representations.emplace_back(
            group.files[i],
            rule_language::ontology::SourceMediaType::UTF8_TEXT,
            revision);
    }
    return true;
}

// Verify and load one corpus group, in order, into `world`.
inline bool load_group(
    kg::KGModule& world,
    const std::string& game_root,
    const std::string& corpus_root,
    const CorpusGroup& group,
    const logosphere::rules::ProcedurePrimitiveRegistry& primitives,
    std::string& why) {
    std::vector<kg::SeedEnvelope> seeds;
    if (!parse_rule_seeds(game_root, group, seeds, why)) return false;

    logosphere::text::SourceCorpusDeclaration corpus;
    if (!declare_corpus(seeds, group, corpus, why)) return false;
    const CorpusFiles bytes(corpus_root);

    kg::SeedSequenceLoadReport report;
    if (kg::verify_and_load_seed_sequence_in_edition(
            seeds, corpus_root, corpus, bytes, world, report, &primitives)) {
        return true;
    }
    why.clear();
    if (report.failed_seed >= 0 &&
        static_cast<std::size_t>(report.failed_seed) < group.seed_count) {
        why = std::string(group.seeds[report.failed_seed]) + ": ";
    }
    why += report.error;
    return false;
}

// Both corpora, verified and loaded. False with `why` naming the seed
// that failed and what it failed on.
inline bool load_rules(
    kg::KGModule& world,
    const std::string& game_root,
    const std::string& cepheus_corpus_root,
    const std::string& book_corpus_root,
    const logosphere::rules::ProcedurePrimitiveRegistry& primitives,
    std::string& why) {
    const CorpusGroup cepheus{kCepheusSeeds,
                              sizeof(kCepheusSeeds) / sizeof(char*),
                              kCepheusFiles,
                              sizeof(kCepheusFiles) / sizeof(char*)};
    const CorpusGroup book{kBookSeeds, sizeof(kBookSeeds) / sizeof(char*),
                           kBookFiles, sizeof(kBookFiles) / sizeof(char*)};
    return load_group(world, game_root, cepheus_corpus_root, cepheus,
                      primitives, why) &&
           load_group(world, game_root, book_corpus_root, book, primitives,
                      why);
}

// Which rules a run was played against, read off the graph the loader
// just built. One edition per corpus; the combined string is every
// edition, sorted and joined, so a change to EITHER book changes it.
// Taking only the first was correct when there was one corpus and
// would now silently pin half the rules, which is the half-open gate
// this module keeps paying for. Empty when the world holds no edition,
// which is a question that cannot be answered rather than a mismatch.
inline std::string rules_edition(const kg::KGModule& world) {
    const auto editions = world.findByType("IngestionEditionContext");
    if (editions.empty()) return {};
    std::vector<std::string> keys;
    keys.reserve(editions.size());
    for (const kg::EntityID id : editions) {
        keys.push_back(world.getProperty(id, "context_key"));
    }
    std::sort(keys.begin(), keys.end());
    std::string combined;
    for (const auto& key : keys) {
        if (!combined.empty()) combined += "+";
        combined += key;
    }
    return combined;
}

}  // namespace voyager

#endif  // VOYAGER_RULE_LOADER_H
