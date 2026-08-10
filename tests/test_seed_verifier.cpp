// The seed verifier: Shelob extracts, the engine verifies.
//
// Design under test: docs/RPG_MODULE.md, step 4. A seed file (the
// envelope + KG-ops with @alias binders) is verified by three checks
// plus the envelope's invariants:
//
//   VERBATIM   every source_quote is a byte-exact substring of the
//              cited source file. No normalization.
//   SCHEMA     the seed loads through the seed loader (alias
//              resolution + validate_kg_op + apply) into a throwaway
//              world - refs-resolve comes for free.
//   VALUE      numeric slots have their digits in the entity's own
//              quote; table-row bands equal the quoted leading cell.
//   INVARIANT  count_of_type / unique_name_per_type / band_coverage.
//
// The positive fixture cites the vendored Cepheus SRD with quotes
// already proven byte-exact by test_rulebook_pack. Each negative
// case mutates the parsed envelope in memory and asserts the RIGHT
// check bites, not just that something failed.
//
// Usage:
//   ./build/test_seed_verifier

#undef NDEBUG

#include "logosphere/kg/kg_module.h"
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
#include <variant>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

// The vendored SRD root - the ONE place the examples path lives, so
// Keep the source root tied to the canonical Logovger directory.
const char* kSourceRoot =
    LOGOSPHERE_SOURCE_DIR "/examples/logovger/srd/cepheus";
const char* kFixture =
    LOGOSPHERE_SOURCE_DIR "/tests/fixtures/seed/chargen_ch1.json";

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The same merged registry the CLI uses: core + every engine pack.
kg::OntologyRegistry engine_registry() {
    kg::OntologyRegistry reg = logosphere::ontology::registry();
    reg.extend(space::ontology::registry());
    reg.extend(earth::ontology::registry());
    reg.extend(rulebook::ontology::registry());
    return reg;
}

// Fresh parse of the positive fixture; negatives mutate their copy.
kg::SeedEnvelope parse_fixture() {
    const std::string text = slurp(kFixture);
    CHECK(!text.empty(), "the fixture file is readable");
    kg::SeedParseResult r = kg::parse_seed_envelope(text);
    CHECK(r.ok(), "the positive fixture parses: " + r.error);
    return r.seed;
}

kg::KGOpCreateEntity* find_create(kg::SeedEnvelope& seed,
                                  const std::string& alias) {
    for (auto& op : seed.ops) {
        if (auto* ce = std::get_if<kg::KGOpCreateEntity>(&op);
            ce && ce->as == alias) {
            return ce;
        }
    }
    return nullptr;
}

bool set_prop(kg::SeedEnvelope& seed, const std::string& alias,
              const std::string& key, const std::string& value) {
    kg::KGOpCreateEntity* ce = find_create(seed, alias);
    if (!ce) return false;
    for (auto& [k, v] : ce->properties) {
        if (k == key) { v = value; return true; }
    }
    return false;
}

bool has_check(const kg::SeedVerifyReport& report,
               const std::string& check) {
    return report.count(check) > 0;
}

void print_first(const kg::SeedVerifyReport& report,
                 const std::string& check) {
    for (const auto& v : report.violations) {
        if (v.check == check) {
            std::cout << "  [measure] " << check << ": " << v.reason
                      << std::endl;
            return;
        }
    }
}

// ------------------------------------------------------ the envelope

void test_envelope_parses() {
    kg::SeedEnvelope seed = parse_fixture();
    std::cout << "  [measure] " << seed.ops.size() << " ops, layer '"
              << seed.layer << "', source " << seed.source.file << " @ "
              << seed.source.commit.substr(0, 7) << std::endl;
    CHECK(seed.ops.size() == 10, "ten ops parsed");
    CHECK(seed.layer == "cepheus", "layer round-trips");
    CHECK(seed.source.file == "book1/character-creation.md",
          "source file round-trips");
    CHECK(seed.invariants.count_of_type.size() == 5 &&
              seed.invariants.unique_name_per_type.size() == 3 &&
              seed.invariants.band_coverage.size() == 1,
          "all three invariant kinds parsed");
    CHECK(seed.invariants.band_coverage[0].alias == "mishap_table" &&
              seed.invariants.band_coverage[0].lo == 2 &&
              seed.invariants.band_coverage[0].hi == 3,
          "band_coverage alias stripped and range kept");
}

void test_envelope_parser_is_loud() {
    struct Case { const char* label; const char* json; };
    const Case cases[] = {
        {"missing layer",
         R"({"source":{"file":"f","commit":"c"},"ops":[]})"},
        {"missing source.commit",
         R"({"source":{"file":"f"},"layer":"x","ops":[]})"},
        {"unknown invariant kind",
         R"({"source":{"file":"f","commit":"c"},"layer":"x",
             "invariants":{"row_count":3},"ops":[]})"},
        {"malformed op",
         R"({"source":{"file":"f","commit":"c"},"layer":"x",
             "ops":[{"op":"create_entity"}]})"},
        {"bad as binder",
         R"({"source":{"file":"f","commit":"c"},"layer":"x",
             "ops":[{"op":"create_entity","type":"Wall","as":"wall"}]})"},
        {"unknown top-level field",
         R"({"source":{"file":"f","commit":"c"},"layer":"x",
             "invariant":{},"ops":[]})"},
        {"band_coverage range not [lo,hi]",
         R"({"source":{"file":"f","commit":"c"},"layer":"x",
             "invariants":{"band_coverage":{"@t":[1]}},"ops":[]})"},
    };
    for (const auto& c : cases) {
        kg::SeedParseResult r = kg::parse_seed_envelope(c.json);
        std::cout << "  [measure] " << c.label << " -> "
                  << (r.ok() ? "ACCEPTED" : r.error) << std::endl;
        CHECK(!r.ok(), std::string("loud on ") + c.label);
    }
}

// -------------------------------------------------------- the loader

void test_loader_binds_and_resolves() {
    kg::SeedEnvelope seed = parse_fixture();
    const kg::OntologyRegistry reg = engine_registry();
    kg::KGModule world(reg);
    world.setMode(kg::KGMode::MINIMAL);

    kg::SeedLoadReport report;
    const bool ok = kg::load_seed(seed, world, report);
    CHECK(ok, "the positive seed loads: " + report.error);
    std::cout << "  [measure] " << report.ops_applied << "/"
              << seed.ops.size() << " ops applied, "
              << report.bindings.size() << " aliases bound" << std::endl;
    CHECK(report.ops_applied == 10, "all ten ops applied");
    CHECK(report.bindings.size() == 8, "eight aliases bound");

    // Alias resolution is real: the TaskCheck's dice slot holds the
    // numeric id the @d2d6 binder was bound to.
    const auto d2d6 = report.bindings.at("d2d6");
    const auto check = report.bindings.at("int_throw");
    CHECK(world.getProperty(check, "dice") == std::to_string(d2d6),
          "@d2d6 in a property value resolved to the created id");
    CHECK(world.getRelated(report.bindings.at("mishap_table"),
                           "HAS_PART").size() == 2,
          "the table owns its two rows through resolved relations");
}

// ------------------------------------------------- the positive path

void test_positive_seed_verifies() {
    kg::SeedEnvelope seed = parse_fixture();
    const kg::SeedVerifyReport report =
        kg::verify_seed(seed, kSourceRoot, engine_registry());
    for (const auto& v : report.violations) {
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    }
    std::cout << "  [measure] checked: " << report.quotes_checked
              << " quotes, " << report.ops_loaded << " ops, "
              << report.values_checked << " values, "
              << report.bands_derived << " bands, "
              << report.invariants_checked << " invariants" << std::endl;
    CHECK(report.ok(), "the positive fixture verifies clean");
    CHECK(report.quotes_checked == 8, "eight quotes checked");
    CHECK(report.ops_loaded == 10, "ten ops loaded");
    CHECK(report.values_checked == 11, "eleven numeric values checked");
    CHECK(report.bands_derived == 2, "two bands derived from quotes");
    CHECK(report.invariants_checked == 9, "nine invariants checked");
}

// ------------------------------------------------ each check must bite

void test_misquote_fails_verbatim() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "age_of_majority", "source_quote",
                   "All characters begin at the age of majority, "
                   "typicaly 18."),
          "mutation applied (one character dropped)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "verbatim");
    CHECK(!report.ok(), "a misquote fails verification");
    CHECK(has_check(report, "verbatim"),
          "and the VERBATIM check is the one that names it");
}

void test_dangling_ref_fails_schema() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "int_throw", "dice", "@nonexistent"),
          "mutation applied (ref to an unbound alias)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "schema");
    CHECK(!report.ok(), "a dangling @ref fails verification");
    CHECK(has_check(report, "schema"),
          "and the SCHEMA check is the one that names it");
}

void test_wrong_class_ref_fails_schema() {
    kg::SeedEnvelope seed = parse_fixture();
    // TaskCheck.dice ranges DiceExpression; point it at the table.
    // Appended as a set_property op so @mishap_table is already
    // bound when it runs - the alias RESOLVES, and the class check
    // is what must bite (distinct from the dangling case above).
    kg::KGOpSetProperty sp;
    sp.target.symbolic = "int_throw";
    sp.property = "dice";
    sp.value = "@mishap_table";
    seed.ops.push_back(kg::KGOp{sp});
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "schema");
    CHECK(!report.ok(), "a wrong-class ref fails verification");
    CHECK(has_check(report, "schema"),
          "and the SCHEMA check is the one that names it");
}

void test_wrong_digits_fail_value() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "age_of_majority", "constant_value", "99"),
          "mutation applied (99 where the quote says 18)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "value");
    CHECK(!report.ok(), "digits absent from the quote fail verification");
    CHECK(has_check(report, "value"),
          "and the VALUE check is the one that names it");
}

void test_band_mismatch_fails_value() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "mishap_row_2", "roll_min", "5") &&
              set_prop(seed, "mishap_row_2", "roll_max", "5"),
          "mutation applied (band [5,5] where the cell says | 2 |)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "value");
    CHECK(!report.ok(), "a band that contradicts its cell fails");
    CHECK(has_check(report, "value"),
          "and the VALUE check is the one that names it");
    bool names_band = false;
    for (const auto& v : report.violations) {
        if (v.check == "value" &&
            v.reason.find("does not equal the quoted cell") !=
                std::string::npos) {
            names_band = true;
            std::cout << "  [measure] value: " << v.reason << std::endl;
        }
    }
    CHECK(names_band, "the band derivation itself bites, not just digits");
}

void test_count_mismatch_fails_invariant() {
    kg::SeedEnvelope seed = parse_fixture();
    bool mutated = false;
    for (auto& [type, n] : seed.invariants.count_of_type) {
        if (type == "RuleConstant") { n = 3; mutated = true; }
    }
    CHECK(mutated, "mutation applied (expect 3 RuleConstants, seed has 2)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "invariant");
    CHECK(!report.ok(), "a count_of_type mismatch fails");
    CHECK(has_check(report, "invariant"),
          "and the INVARIANT check is the one that names it");
}

void test_band_gap_fails_invariant() {
    kg::SeedEnvelope seed = parse_fixture();
    // Rows tile [2,3]; declaring [1,3] leaves 1 claimed by no row.
    seed.invariants.band_coverage[0].lo = 1;
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "invariant");
    CHECK(!report.ok(), "a coverage gap fails");
    CHECK(has_check(report, "invariant"),
          "and the INVARIANT check is the one that names it");
    bool names_gap = false;
    for (const auto& v : report.violations) {
        if (v.check == "invariant" &&
            v.reason.find("gap") != std::string::npos) {
            names_gap = true;
        }
    }
    CHECK(names_gap, "the reason names the gap");
}

void test_duplicate_name_fails_invariant() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "prior_career_dm", "name", "age_of_majority"),
          "mutation applied (two RuleConstants named age_of_majority)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "invariant");
    CHECK(!report.ok(), "a duplicate name fails");
    CHECK(has_check(report, "invariant"),
          "and the INVARIANT check is the one that names it");
    bool names_dup = false;
    for (const auto& v : report.violations) {
        if (v.check == "invariant" &&
            v.reason.find("duplicate name") != std::string::npos) {
            names_dup = true;
        }
    }
    CHECK(names_dup, "the reason names the duplicate");
}

}  // namespace

int main() {
    std::cout << "Seed verifier (Shelob extracts, the engine verifies)"
              << std::endl;
    test_envelope_parses();
    test_envelope_parser_is_loud();
    test_loader_binds_and_resolves();
    test_positive_seed_verifies();
    test_misquote_fails_verbatim();
    test_dangling_ref_fails_schema();
    test_wrong_class_ref_fails_schema();
    test_wrong_digits_fail_value();
    test_band_mismatch_fails_value();
    test_count_mismatch_fails_invariant();
    test_band_gap_fails_invariant();
    test_duplicate_name_fails_invariant();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
