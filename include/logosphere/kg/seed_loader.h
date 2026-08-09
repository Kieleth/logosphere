#ifndef LOGOSPHERE_KG_SEED_LOADER_H
#define LOGOSPHERE_KG_SEED_LOADER_H

// Seed files: KG-ops with provenance. A seed file is how a body of
// rule data (or any pre-authored KG content) enters a world at game
// start - one envelope pinning the source text it was extracted
// from, plus an ordered list of KG-ops using symbolic @aliases
// bound by create ops ("as": "@gun_combat").
//
// Envelope shape (JSON):
//
//   {
//     "source": {"file": "book1/character-creation.md",
//                "commit": "<sha of the vendored source>"},
//     "layer": "cepheus",
//     "invariants": {
//       "count_of_type": {"RuleConstant": 2},
//       "unique_name_per_type": ["RuleConstant"],
//       "band_coverage": {"@mishap_table": [1, 6]}
//     },
//     "ops": [
//       {"op": "create_entity", "type": "RuleConstant",
//        "as": "@age_of_majority", "properties": {...}},
//       ...
//     ]
//   }
//
// The loader applies a parsed seed to a KG through the one
// validated write path, in file order: resolve @aliases (targets
// AND entity_ref property values), validate_kg_op, apply, bind the
// created id to the op's "as" alias. It fails loudly on the first
// violation with the op index and reason - a seed file is trusted
// content and a partial load is a bug, not a state.
//
// This is the same loader step 6 (game-start loading) uses: a game
// calls load_seed with its own KGModule. The ingestion verifier
// (seed_verifier.h) runs it against a throwaway world as its
// schema check.
//
// Engine-side helper. Generic - no game knowledge.

#include "logosphere/kg/kg_ops.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace kg {

class KGModule;

// --- Envelope ------------------------------------------------------

struct SeedSource {
    std::string file;    // path relative to the game's source root
    std::string commit;  // pin of the vendored source text
};

// The three invariant kinds the envelope may assert. Deliberately
// nothing fancier (owner ruling, 2026-08-09).
struct SeedInvariants {
    // Created entities of exactly this type must number N.
    std::vector<std::pair<std::string, long long>> count_of_type;
    // Among creates of this type, no two share a "name" property.
    std::vector<std::string> unique_name_per_type;
    // The table bound to this alias must have HAS_PART rows whose
    // bands tile [lo, hi] inclusive - no gaps, no overlaps.
    struct BandCoverage {
        std::string alias;  // stored without the leading '@'
        long long   lo = 0;
        long long   hi = 0;
    };
    std::vector<BandCoverage> band_coverage;
};

struct SeedEnvelope {
    SeedSource        source;
    std::string       layer;
    SeedInvariants    invariants;
    std::vector<KGOp> ops;
};

struct SeedParseResult {
    SeedEnvelope seed;
    std::string  error;  // empty on success

    bool ok() const { return error.empty(); }
};

// Parse a seed file. Loud: any missing or malformed field is a
// fatal error (unlike parse_kg_ops, which drops malformed ops with
// warnings - a silently dropped op would skew every invariant).
SeedParseResult parse_seed_envelope(const std::string& json_text);

// --- Loading -------------------------------------------------------

struct SeedLoadReport {
    bool        ok = true;
    int         failed_op = -1;  // index into seed.ops; -1 when ok
    std::string error;           // first violation, with reason
    size_t      ops_applied = 0;

    // Alias -> created entity id, from the ops' "as" binders.
    std::unordered_map<std::string, EntityID> bindings;
    // Per op: the entity it created (INVALID_ENTITY for non-creates).
    std::vector<EntityID> created_ids;
};

// Apply a parsed seed to the KG through the validated write path,
// in file order. Stops at the first violation. Validation runs
// against kg.getRegistry() - the registry the world was built from.
// Returns report.ok.
bool load_seed(const SeedEnvelope& seed, KGModule& kg,
               SeedLoadReport& report);

}  // namespace kg

#endif  // LOGOSPHERE_KG_SEED_LOADER_H
