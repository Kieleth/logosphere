// The rules, into the world, verified on the way in.
//
// Two seed files, in dependency order, through the engine's ingestion
// verifier. Everything the book fixes is in them; nothing the book
// fixes is anywhere else. The game will not start on rules that did
// not verify, and it says which seed and why rather than starting
// half-loaded.
//
// The bytes the seeds cite live OUTSIDE this game, under corpora/, and
// the path arrives as VOYAGER_CORPUS_DIR from a
// logosphere_game_corpus() declaration in CMake. It is never derived
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

// ORDER IS DEPENDENCY ORDER. The rules seed owns every cited entity the
// book prints; the procedure seed names primitives and depends on the
// primitive registry rather than on the rules, but it is loaded second
// so that a world holding a procedure always holds the rules that
// procedure walks over.
inline constexpr const char* kRuleSeeds[] = {
    "seeds/voyager_cepheus_rules.json",
    "seeds/voyager_chargen_procedure.json",
};
inline constexpr std::size_t kRuleSeedCount =
    sizeof(kRuleSeeds) / sizeof(kRuleSeeds[0]);

// Every file of the corpus this game reads. One chapter: the slice
// ends at career selection and nothing in it cites anything else.
inline constexpr const char* kCorpusFiles[] = {
    "book1/character-creation.md",
};
inline constexpr std::size_t kCorpusFileCount =
    sizeof(kCorpusFiles) / sizeof(kCorpusFiles[0]);

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
                             std::vector<kg::SeedEnvelope>& seeds,
                             std::string& why) {
    seeds.clear();
    why.clear();
    for (const char* relative : kRuleSeeds) {
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

// The corpus every seed declares it was read from. Derived from the
// seeds themselves rather than restated here, so a seed that starts
// citing a second file is a loud failure instead of a silent one.
inline bool declare_corpus(
    const std::vector<kg::SeedEnvelope>& seeds,
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
        if (std::find(std::begin(kCorpusFiles), std::end(kCorpusFiles),
                      seed.source.file) == std::end(kCorpusFiles)) {
            why = "seed cites a file this game did not declare: " +
                  seed.source.file;
            return false;
        }
    }
    corpus.source_layer = layer;
    for (const char* file : kCorpusFiles) {
        corpus.representations.emplace_back(
            file, rule_language::ontology::SourceMediaType::UTF8_TEXT,
            revision);
    }
    return true;
}

// Verify and load, in order, into `world`. False with `why` naming the
// seed that failed and what it failed on.
inline bool load_rules(
    kg::KGModule& world,
    const std::string& game_root,
    const std::string& corpus_root,
    const logosphere::rules::ProcedurePrimitiveRegistry& primitives,
    std::string& why) {
    std::vector<kg::SeedEnvelope> seeds;
    if (!parse_rule_seeds(game_root, seeds, why)) return false;

    logosphere::text::SourceCorpusDeclaration corpus;
    if (!declare_corpus(seeds, corpus, why)) return false;
    const CorpusFiles bytes(corpus_root);

    kg::SeedSequenceLoadReport report;
    if (kg::verify_and_load_seed_sequence_in_edition(
            seeds, corpus_root, corpus, bytes, world, report, &primitives)) {
        return true;
    }
    why.clear();
    if (report.failed_seed >= 0) {
        why = std::string(kRuleSeeds[report.failed_seed]) + ": ";
    }
    why += report.error;
    return false;
}

// Which rulebook a run was played against, read off the graph the
// loader just built. A tape holds the seed and the answers; the world
// comes from those THROUGH the rules, so a tape replayed after the
// rules moved can run green and produce a different character. Empty
// when the world holds no edition, which is a question that cannot be
// answered rather than a mismatch.
inline std::string rules_edition(const kg::KGModule& world) {
    const auto editions = world.findByType("IngestionEditionContext");
    if (editions.empty()) return {};
    return world.getProperty(editions.front(), "context_key");
}

}  // namespace voyager

#endif  // VOYAGER_RULE_LOADER_H
