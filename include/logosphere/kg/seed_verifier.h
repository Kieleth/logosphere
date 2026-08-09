#ifndef LOGOSPHERE_KG_SEED_VERIFIER_H
#define LOGOSPHERE_KG_SEED_VERIFIER_H

// The ingestion verifier - the machine half of "LLM extracts,
// machine verifies" (docs/RPG_MODULE.md). Runs three checks over a
// parsed seed file against the source text it claims to come from:
//
//   VERBATIM   every created entity with a source_quote has it as
//              an exact BYTE substring of source_root/<source.file>.
//              No normalization: the SRD has en dashes and
//              multiplication signs, and byte-exact is the
//              discipline. A hallucinated quote matches nothing.
//   SCHEMA     the seed loads through the seed loader into a
//              throwaway MINIMAL-mode world built from the given
//              registry, so every op passes alias resolution and
//              validate_kg_op - refs-resolve comes for free.
//   VALUE      every numeric slot on a quoted entity has its digits
//              (absolute value) somewhere in that entity's own
//              quote, and TableEntry / LookupEntry bands equal the
//              band the quoted leading markdown cell declares
//              ("| 3 |" or "| 3-5 |"). A hallucinated number
//              matches nothing either.
//   INVARIANT  the envelope's assertions hold: count_of_type,
//              unique_name_per_type, band_coverage.
//
// Registry-parameterized on purpose: the engine CLI
// (tools/logosphere_verify.cpp) passes the merged engine packs; a
// game verifies its own seed files by calling this library with its
// own registry. The verifier has no game knowledge.
//
// VALUE and INVARIANT checks need the loaded world, so they run
// only when SCHEMA passed; a schema failure already fails the
// verification. VERBATIM always runs.

#include "logosphere/kg/seed_loader.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kg {

class OntologyRegistry;

struct SeedViolation {
    // Which check bit: "verbatim", "schema", "value", "invariant".
    std::string check;
    int         op_index = -1;  // index into seed.ops; -1 if not op-scoped
    std::string alias;          // the entity's "as" binder, when it has one
    std::string reason;
};

struct SeedVerifyReport {
    std::vector<SeedViolation> violations;

    // Summary counts - what was actually checked, not just pass/fail.
    size_t quotes_checked     = 0;
    size_t ops_loaded         = 0;
    size_t values_checked     = 0;
    size_t bands_derived      = 0;
    size_t invariants_checked = 0;

    bool ok() const { return violations.empty(); }

    size_t count(const std::string& check) const {
        size_t n = 0;
        for (const auto& v : violations)
            if (v.check == check) ++n;
        return n;
    }
};

SeedVerifyReport verify_seed(const SeedEnvelope& seed,
                             const std::string& source_root,
                             const OntologyRegistry& registry);

}  // namespace kg

#endif  // LOGOSPHERE_KG_SEED_VERIFIER_H
