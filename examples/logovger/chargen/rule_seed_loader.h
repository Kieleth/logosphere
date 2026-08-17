#ifndef LOGOVGER_RULE_SEED_LOADER_H
#define LOGOVGER_RULE_SEED_LOADER_H

#include "chargen/rule_seeds.h"
#include "logosphere/kg/seed_verifier.h"

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace logovger {

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

inline bool load_rule_seeds(
    kg::KGModule& world,
    const std::string& game_root,
    const logosphere::rules::ProcedurePrimitiveRegistry& procedures,
    std::string& why) {
    std::vector<kg::SeedEnvelope> seeds;
    if (!parse_rule_seeds(game_root, seeds, why)) return false;

    kg::SeedSequenceLoadReport report;
    if (kg::verify_and_load_seed_sequence(
            seeds, game_root + "/srd/cepheus", world, report,
            &procedures)) {
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
