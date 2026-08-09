// logosphere-verify - the ingestion verifier CLI.
//
// Usage:
//   logosphere-verify <seed.json> <source-root>
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
#include "logosphere/kg/seed_verifier.h"
#include "generated/earth_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/space_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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
    if (argc != 3) {
        std::cerr << "usage: logosphere-verify <seed.json> <source-root>"
                  << std::endl;
        return 1;
    }
    const std::string seed_path = argv[1];
    const std::string source_root = argv[2];

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

    // The engine's full vocabulary: core plus every shipped pack.
    kg::OntologyRegistry registry = logosphere::ontology::registry();
    registry.extend(space::ontology::registry());
    registry.extend(earth::ontology::registry());
    registry.extend(rulebook::ontology::registry());

    const kg::SeedVerifyReport report =
        kg::verify_seed(parsed.seed, source_root, registry);

    std::cout << "logosphere-verify: " << seed_path << std::endl;
    std::cout << "  source: " << parsed.seed.source.file << " @ "
              << parsed.seed.source.commit << std::endl;
    std::cout << "  layer: " << parsed.seed.layer << std::endl;
    for (const auto& v : report.violations) print_violation(v);
    std::cout << "  checked: " << report.quotes_checked << " quotes, "
              << report.ops_loaded << "/" << parsed.seed.ops.size()
              << " ops loaded, " << report.values_checked << " values, "
              << report.bands_derived << " bands, "
              << report.invariants_checked << " invariants" << std::endl;
    if (report.ok()) {
        std::cout << "  VERIFIED" << std::endl;
        return 0;
    }
    std::cout << "  FAILED (" << report.violations.size() << " violation"
              << (report.violations.size() == 1 ? "" : "s") << ")"
              << std::endl;
    return 1;
}
