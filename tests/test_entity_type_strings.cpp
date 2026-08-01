// Every entity type the engine names must exist in the ontology.
//
// The KG rejects unknown types and returns INVALID_ENTITY. Callers
// rarely check, so a typo does not crash: the entity simply never
// exists, and the code around it carries on as if it did. That is the
// worst failure mode this codebase has, because it is invisible until
// something asks the graph a question months later.
//
// Four of these were live when this test was written:
//
//   "left_leg" "right_leg" "left_arm" "right_arm"
//       passed as TYPES by the humanoid generator, so every
//       humanoid's arms and legs silently failed to become entities
//       while its Torso and Head succeeded.
//   "tree"
//       lowercase, so the physics tree generator's entity was
//       rejected and its activator never fired.
//
// This test reads the source rather than running it, because the
// broken paths produce no error to observe - that is the whole
// problem.
//
// Usage:
//   ./build/test_entity_type_strings

#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/earth_ontology_registry.h"
#include "generated/space_ontology_registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Baked in by CMake: this test reads the source tree, and ctest runs
// it from the build directory rather than the repo root.
#ifndef LOGOSPHERE_SOURCE_DIR
#define LOGOSPHERE_SOURCE_DIR "."
#endif

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

// Types created from a runtime variable rather than a literal, or
// deliberately tested for rejection. Anything listed here is a
// promise that a human checked it.
const std::set<std::string> kAllowed = {
    // Empty on purpose. A type named in code must be declared in some
    // ontology layer; if you are tempted to add one here, the type is
    // probably missing from a schema instead.
};

}  // namespace

int main() {
    std::cout << "Entity type strings (every named type must exist)"
              << std::endl;

    // Every type any layer declares, read from the generated
    // registries as text. Parsing rather than linking is deliberate:
    // this binary is headless and cannot link a game's registry, but
    // a game's types are still legitimately named in its own source.
    std::set<std::string> known;
    const std::regex decl(R"RX(addEntityType\(\s*"([A-Za-z_][A-Za-z0-9_]*)")RX");
    for (const char* rel : {"src", "examples"}) {
        const fs::path root = fs::path(LOGOSPHERE_SOURCE_DIR) / rel;
        if (!fs::exists(root)) continue;
        for (auto it = fs::recursive_directory_iterator(root);
             it != fs::recursive_directory_iterator(); ++it) {
            const auto& q = it->path();
            if (q.filename().string().find("_ontology_registry.cpp")
                == std::string::npos) continue;
            std::ifstream in(q);
            std::string l;
            while (std::getline(in, l))
                for (std::sregex_iterator m(l.begin(), l.end(), decl), e;
                     m != e; ++m)
                    known.insert((*m)[1]);
        }
    }
    std::cout << "  " << known.size()
              << " entity types declared across all ontology layers"
              << std::endl;
    if (known.size() < 50) {
        std::cout << "FAIL: too few types found — the registry scan is "
                     "broken, so this test proves nothing" << std::endl;
        return 1;
    }

    // Collect every literal handed to createEntity / createEntityAtPosition
    // / createChildEntityWithParticles across the engine and its games.
    const std::regex call(
        // Custom delimiter: the pattern itself contains )" which
        // would close a plain R"( ... )".
        R"RX(create(?:Entity|EntityAtPosition|ChildEntityWithParticles)\s*\(\s*(?:[A-Za-z_][A-Za-z0-9_]*\s*,\s*)?"([A-Za-z_][A-Za-z0-9_]*)")RX");

    std::vector<fs::path> roots;
    for (const char* r : {"src", "examples"}) {
        fs::path pr = fs::path(LOGOSPHERE_SOURCE_DIR) / r;
        if (fs::exists(pr)) roots.emplace_back(pr);
    }
    if (roots.empty()) {
        std::cout << "FAIL: run from the repository root; no src/ found"
                  << std::endl;
        return 1;
    }

    std::set<std::string> offenders;
    int scanned = 0, literals = 0;
    for (const auto& root : roots) {
        for (auto it = fs::recursive_directory_iterator(root);
             it != fs::recursive_directory_iterator(); ++it) {
            const auto& p = it->path();
            if (p.string().find("/generated/") != std::string::npos) continue;
            auto ext = p.extension().string();
            if (ext != ".cpp" && ext != ".h") continue;
            std::ifstream in(p);
            if (!in) continue;
            ++scanned;
            std::string line;
            bool in_block_comment = false;
            while (std::getline(in, line)) {
                // A type named in prose is documentation, not a call.
                if (in_block_comment) {
                    if (line.find("*/") != std::string::npos)
                        in_block_comment = false;
                    continue;
                }
                std::string trimmed = line.substr(
                    std::min(line.find_first_not_of(" \t"), line.size()));
                if (trimmed.rfind("//", 0) == 0) continue;
                if (trimmed.rfind("/*", 0) == 0) {
                    if (trimmed.find("*/") == std::string::npos)
                        in_block_comment = true;
                    continue;
                }
                for (std::sregex_iterator m(line.begin(), line.end(), call),
                     e; m != e; ++m) {
                    const std::string type = (*m)[1];
                    ++literals;
                    if (kAllowed.count(type)) continue;
                    if (!known.count(type))
                        offenders.insert(type + "  (" + p.string() + ")");
                }
            }
        }
    }

    std::cout << "  scanned " << scanned << " files, " << literals
              << " entity-type literals" << std::endl;

    if (offenders.empty()) {
        tests_passed++;
    } else {
        tests_failed++;
        std::cout << "FAIL: these types are named in code but do not exist "
                     "in any ontology layer. createEntity returns "
                     "INVALID_ENTITY for each, silently:" << std::endl;
        for (const auto& o : offenders) std::cout << "    " << o << std::endl;
    }

    // Guard the guard: if the scan matched nothing, it is broken and
    // would pass forever.
    if (literals > 20) {
        tests_passed++;
    } else {
        tests_failed++;
        std::cout << "FAIL: only " << literals << " literals matched — the "
                     "scan regex is broken, so this test proves nothing"
                  << std::endl;
    }

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
