// logosphere-verify - the ingestion verifier CLI.
//
// Usage:
//   logosphere-verify [--prerequisite <seed.json>]...
//       <seed.json> <source-root>
//
// Runs the three-check verifier (verbatim / schema+refs / value)
// plus the envelope's invariants over a seed file, against the
// source tree it cites. Exit 0 when clean, 1 on any violation.
//
// Engine boundary: this tool knows the ENGINE registries only
// (logosphere core + rulebook + earth + space, merged). Game seed
// files that use game types are verified by game-side test targets
// calling kg::verify_seed with the game's own registry.

#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/seed_verifier.h"
#include "generated/earth_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/space_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void print_violation(const kg::SeedViolation& v) {
    std::cout << "  [" << v.check << "] ";
    if (v.op_index >= 0) std::cout << "ops[" << v.op_index << "] ";
    if (!v.alias.empty()) std::cout << "@" << v.alias << " ";
    std::cout << "- " << v.reason << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    const char* usage =
        "usage: logosphere-verify [--prerequisite <seed.json>]... "
        "<seed.json> <source-root>";
    std::vector<std::string> seed_paths;
    int cursor = 1;
    while (cursor < argc &&
           std::string(argv[cursor]) == "--prerequisite") {
        if (cursor + 1 >= argc) {
            std::cerr << usage << std::endl;
            return 1;
        }
        seed_paths.emplace_back(argv[cursor + 1]);
        cursor += 2;
    }
    if (argc - cursor != 2) {
        std::cerr << usage << std::endl;
        return 1;
    }
    seed_paths.emplace_back(argv[cursor]);
    const std::string source_root = argv[cursor + 1];

    std::vector<kg::SeedEnvelope> seeds;
    seeds.reserve(seed_paths.size());
    for (const auto& seed_path : seed_paths) {
        const std::string text = slurp(seed_path);
        if (text.empty()) {
            std::cerr << "logosphere-verify: cannot read " << seed_path
                      << std::endl;
            return 1;
        }
        const kg::SeedParseResult parsed = kg::parse_seed_envelope(text);
        if (!parsed.ok()) {
            std::cerr << "logosphere-verify: " << seed_path << ": "
                      << parsed.error << std::endl;
            return 1;
        }
        seeds.push_back(std::move(parsed.seed));
    }

    // The engine's full vocabulary: core plus every shipped pack.
    kg::OntologyRegistry registry = logosphere::ontology::registry();
    registry.extend(space::ontology::registry());
    registry.extend(earth::ontology::registry());
    registry.extend(rulebook::ontology::registry());

    kg::KGModule world(registry);
    world.setMode(kg::KGMode::MINIMAL);
    kg::SeedSequenceLoadReport sequence;
    const bool ok = kg::verify_and_load_seed_sequence(
        seeds, source_root, world, sequence);
    const kg::SeedVerifyReport& report = sequence.verifications.back();
    const size_t target_index = seed_paths.size() - 1;
    const size_t reported_index = sequence.failed_seed >= 0
        ? static_cast<size_t>(sequence.failed_seed)
        : target_index;
    const std::string& seed_path = seed_paths[reported_index];
    const kg::SeedEnvelope& target = seeds[reported_index];

    std::cout << "logosphere-verify: " << seed_path << std::endl;
    std::cout << "  source: " << target.source.file << " @ "
              << target.source.commit << std::endl;
    std::cout << "  layer: " << target.layer << std::endl;
    // Drift warnings report without gating (exit stays 0 when clean).
    for (size_t index = 0; index < sequence.verifications.size(); ++index) {
        for (const auto& warning :
             sequence.verifications[index].warnings) {
            std::cout << "  [warn] " << seed_paths[index] << ": "
                      << warning << std::endl;
        }
    }
    for (const auto& v : report.violations) print_violation(v);
    std::cout << "  checked: " << report.quotes_checked << " quotes, "
              << report.ops_loaded << "/" << target.ops.size()
              << " ops loaded, " << report.values_checked << " values, "
              << report.bands_derived << " bands, "
              << report.invariants_checked << " invariants" << std::endl;
    std::cout << "  sequence: " << sequence.seeds_verified << "/"
              << seeds.size() << " seeds verified, "
              << sequence.seeds_loaded << " loaded" << std::endl;
    if (ok) {
        std::cout << "  VERIFIED" << std::endl;
        return 0;
    }
    std::cout << "  FAILED at " << seed_path << ": " << sequence.error
              << std::endl;
    return 1;
}
