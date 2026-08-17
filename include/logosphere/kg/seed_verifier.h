#ifndef LOGOSPHERE_KG_SEED_VERIFIER_H
#define LOGOSPHERE_KG_SEED_VERIFIER_H

// The ingestion verifier validates extracted rule data before it can
// enter the engine (docs/RPG_MODULE.md). Runs five checks over a
// parsed seed file against the source text it claims to come from.
//
// All checks read the LOADED world (the seed applied through the
// loader), never the raw op list: a quote injected or rewritten by
// a later set_property op is ground truth for VALUE, so it must be
// the same state VERBATIM certifies. SCHEMA therefore runs first;
// when it fails, the schema violation alone fails the verification
// and the other checks are not run.
//
//   SCHEMA     the seed loads through the seed loader into a
//              throwaway MINIMAL-mode world built from the given
//              registry, so every op passes alias resolution and
//              validate_kg_op - refs-resolve comes for free.
//   VERBATIM   every loaded entity with a legacy source_quote has it as an
//              exact BYTE substring of its source file. The file is
//              the entity's own source_file property when set
//              (multi-file seeds are legal), else the envelope's
//              source.file, both relative to source_root. No
//              normalization: the SRD has en dashes and
//              multiplication signs, and byte-exact is the
//              discipline. A hallucinated quote matches nothing.
//              source_section is required on every cited entity and
//              must equal the nearest Markdown heading above at least
//              one exact occurrence of the quote.
//              A Cited entity without source_quote must instead have edition
//              origin and be the result of one or more reconciled
//              IngestionClaims whose SourceTargets resolve against exact
//              representation bytes. Legacy locator fields and exact ledger
//              evidence cannot coexist on one rule. Types without the slot
//              are exempt. Source paths containing ".." are rejected before
//              opening.
//   VALUE      every numeric slot on a quoted entity must EQUAL one
//              of the quote's number tokens (digit runs; embedded
//              thousands-commas normalize, "10,000" -> "10000";
//              sign dropped). TableEntry / LookupEntry bands must
//              equal the band the quoted leading markdown cell
//              declares, in the notations the book itself prints:
//              "| 3 |", "| 3-5 |", "| 3–5 |" (en dash),
//              "| 0 through 2 |", markdown-escaped negatives
//              ("| \-6 |"). A hallucinated number matches nothing
//              either. DiceExpression fields must jointly match one
//              dice expression in the quote, so 6D2 cannot pass on
//              the separate number tokens in "2D6".
//   SEMANTIC   lookup tables name one known concrete LookupEntry subtype
//              and contain only rows of that type; rollable tables contain
//              TableEntry rows; ordered outcomes and procedures have
//              contiguous steps; procedure primitives and route labels
//              match the supplied primitive registry, and routes remain
//              inside their Procedure.
//   INVARIANT  the envelope's assertions hold: count_of_type,
//              unique_name_per_type (every instance of a listed
//              type carries a distinct non-empty name, read from
//              the loaded world), band_coverage.
//
// Source drift: when source_root/SOURCE_COMMIT exists and disagrees
// with the envelope's source.commit pin, the report carries a drift
// WARNING - report, not gate (owner CI ruling); ok() ignores it.
//
// Registry-parameterized on purpose: the engine CLI
// (tools/logosphere_verify.cpp) passes the merged engine packs; a
// game verifies its own seed files by calling this library with its
// own registry. The verifier has no game knowledge.

#include "logosphere/kg/seed_loader.h"

#include <cstddef>
#include <string>
#include <vector>

namespace logosphere::rules {
class ProcedurePrimitiveRegistry;
}

namespace logosphere::text {
class SourceAccess;
struct SourceCorpusDeclaration;
}

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

    // Non-gating findings (source-commit drift). ok() ignores them.
    std::vector<std::string> warnings;

    // Summary counts - what was actually checked, not just pass/fail.
    size_t quotes_checked     = 0;
    size_t ops_loaded         = 0;
    size_t values_checked     = 0;
    size_t bands_derived      = 0;
    size_t semantics_checked  = 0;
    size_t invariants_checked = 0;

    bool ok() const { return violations.empty(); }

    size_t count(const std::string& check) const {
        size_t n = 0;
        for (const auto& v : violations)
            if (v.check == check) ++n;
        return n;
    }
};

// Result of verifying and loading an ordered seed sequence. R11 lives in
// this operation: each seed is verified with every earlier seed as a
// prerequisite, then loaded into the caller's world. A failure stops before
// that seed mutates the world. Earlier seeds remain loaded because each seed,
// not the sequence, is the transactional unit.
struct SeedSequenceLoadReport {
    bool        ok = true;
    int         failed_seed = -1;
    std::string error;
    size_t      seeds_verified = 0;
    size_t      seeds_loaded = 0;
    std::vector<SeedVerifyReport> verifications;
};

// Verification loads the seed into a scratch world, so a seed that
// references entities another seed owns (with canonical @@entity paths)
// cannot be verified alone: its references resolve against nothing. Pass the
// seeds it depends on, in load order, and they are loaded into the
// same scratch world first. Their own violations are NOT reported
// here; verify each of them in turn, which is what an ordered manifest
// gives you for free.
SeedVerifyReport verify_seed(const SeedEnvelope& seed,
                             const std::string& source_root,
                             const OntologyRegistry& registry,
                             const logosphere::rules::
                                 ProcedurePrimitiveRegistry*
                                     procedure_primitives = nullptr,
                             const std::vector<const SeedEnvelope*>&
                                 prerequisites = {});

// Edition-scoped verification materializes the declared exact corpus into the
// scratch world, then loads prerequisites and the target through
// load_seed_in_edition. Each rule may use either the transitional structural
// locator or exact SourceTarget ledger evidence, never both. Every UTF8_TEXT
// ledger target requires a TextQuoteSelector that converges with its primary
// byte range, so character offsets cannot pass as byte offsets.
SeedVerifyReport verify_seed_in_edition(
    const SeedEnvelope& seed,
    const std::string& source_root,
    const logosphere::text::SourceCorpusDeclaration& corpus,
    const logosphere::text::SourceAccess& source_access,
    const OntologyRegistry& registry,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives = nullptr,
    const std::vector<const SeedEnvelope*>& prerequisites = {});

// Verify and load seeds in declared dependency order. This is the single
// owner of prerequisite accumulation for game startup and verification tools.
// Empty input is invalid: a caller that requires rule data must not silently
// start with none.
bool verify_and_load_seed_sequence(
    const std::vector<SeedEnvelope>& seeds,
    const std::string& source_root,
    KGModule& world,
    SeedSequenceLoadReport& report,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives = nullptr);

// Materialize one exact corpus in the caller's world, verify every seed in a
// scratch world containing the same corpus, then load each seed in dependency
// order with the resulting IngestionEditionContext as its identity scope.
bool verify_and_load_seed_sequence_in_edition(
    const std::vector<SeedEnvelope>& seeds,
    const std::string& source_root,
    const logosphere::text::SourceCorpusDeclaration& corpus,
    const logosphere::text::SourceAccess& source_access,
    KGModule& world,
    SeedSequenceLoadReport& report,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives = nullptr);

}  // namespace kg

#endif  // LOGOSPHERE_KG_SEED_VERIFIER_H
