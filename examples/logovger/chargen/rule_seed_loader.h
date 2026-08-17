#ifndef LOGOVGER_RULE_SEED_LOADER_H
#define LOGOVGER_RULE_SEED_LOADER_H

#include "chargen/rule_seeds.h"
#include "logosphere/kg/seed_verifier.h"
#include "logosphere/text/source_corpus.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace logovger {

class RuleSourceAccess final : public logosphere::text::SourceAccess {
public:
    explicit RuleSourceAccess(std::string source_root)
        : source_root_(std::move(source_root)) {}

    logosphere::text::SourceReadResult read_exact(
        const logosphere::text::SourceRepresentationDeclaration& declaration)
        const override {
        if (declaration.source_file.find("..") != std::string::npos) {
            return {false, {}, "path traversal is not allowed"};
        }
        std::ifstream file(source_root_ + "/" + declaration.source_file,
                           std::ios::binary);
        if (!file) return {false, {}, "file is unreadable"};
        std::ostringstream bytes;
        bytes << file.rdbuf();
        if (file.bad()) return {false, {}, "file read failed"};
        return {true, bytes.str(), {}};
    }

private:
    std::string source_root_;
};

inline bool parse_rule_seeds(const std::string& game_root,
                             std::vector<kg::SeedEnvelope>& seeds,
                             std::string& why) {
    seeds.clear();
    seeds.reserve(kRuleSeedCount);
    why.clear();
    for (const char* relative : kRuleSeeds) {
        const std::string path = game_root + "/" + relative;
        std::ifstream file(path, std::ios::binary);
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

inline bool declare_rule_source_corpus(
    const std::vector<kg::SeedEnvelope>& seeds,
    logosphere::text::SourceCorpusDeclaration& corpus,
    std::string& why) {
    corpus = {};
    why.clear();
    if (seeds.empty()) {
        why = "production rule seed sequence is empty";
        return false;
    }

    const std::string& layer = seeds.front().layer;
    const std::string& revision = seeds.front().source.commit;
    std::vector<bool> represented(kRuleSourceFileCount, false);
    for (const auto& seed : seeds) {
        if (seed.layer != layer) {
            why = "production rule seeds declare mixed source layers";
            return false;
        }
        if (seed.source.commit != revision) {
            why = "production rule seeds declare mixed source revisions";
            return false;
        }
        const auto found = std::find(
            std::begin(kRuleSourceFiles), std::end(kRuleSourceFiles),
            seed.source.file);
        if (found == std::end(kRuleSourceFiles)) {
            why = "production seed source_file is undeclared corpus member: " +
                  seed.source.file;
            return false;
        }
        represented[static_cast<std::size_t>(
            found - std::begin(kRuleSourceFiles))] = true;
    }
    for (std::size_t index = 0; index < represented.size(); ++index) {
        if (!represented[index]) {
            why = "declared rule corpus member has no production seed: " +
                  std::string(kRuleSourceFiles[index]);
            return false;
        }
    }

    corpus.source_layer = layer;
    corpus.representations.reserve(kRuleSourceFileCount);
    for (const char* source_file : kRuleSourceFiles) {
        corpus.representations.emplace_back(
            source_file,
            rule_language::ontology::SourceMediaType::UTF8_TEXT,
            revision);
    }
    return true;
}

inline bool load_rule_seeds(
    kg::KGModule& world,
    const std::string& game_root,
    const logosphere::rules::ProcedurePrimitiveRegistry& procedures,
    std::string& why) {
    std::vector<kg::SeedEnvelope> seeds;
    if (!parse_rule_seeds(game_root, seeds, why)) return false;

    logosphere::text::SourceCorpusDeclaration corpus;
    if (!declare_rule_source_corpus(seeds, corpus, why)) return false;
    RuleSourceAccess source_access(game_root + "/srd/cepheus");

    kg::SeedSequenceLoadReport report;
    if (kg::verify_and_load_seed_sequence_in_edition(
            seeds, game_root + "/srd/cepheus", corpus, source_access,
            world, report, &procedures)) {
        return true;
    }
    if (report.failed_seed >= 0) {
        why = std::string(kRuleSeeds[report.failed_seed]) + ": ";
    }
    why += report.error;
    return false;
}

}  // namespace logovger

#endif  // LOGOVGER_RULE_SEED_LOADER_H
