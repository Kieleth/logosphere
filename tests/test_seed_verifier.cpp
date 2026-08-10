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

#include "logosphere/events/event_bus.h"
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

// The vendored SRD root - the ONE place the examples path lives.
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

// Envelope maps come back key-sorted, so never index band_coverage
// positionally - find the assertion by the alias it names.
kg::SeedInvariants::BandCoverage* find_coverage(kg::SeedEnvelope& seed,
                                                const std::string& alias) {
    for (auto& c : seed.invariants.band_coverage)
        if (c.alias == alias) return &c;
    return nullptr;
}

// Does any violation of this check carry `needle` in its reason?
bool reason_contains(const kg::SeedVerifyReport& report,
                     const std::string& check, const std::string& needle) {
    for (const auto& v : report.violations) {
        if (v.check == check && v.reason.find(needle) != std::string::npos) {
            std::cout << "  [measure] " << check << ": " << v.reason
                      << std::endl;
            return true;
        }
    }
    return false;
}

// Append a set_property op targeting an alias bound earlier.
void append_set(kg::SeedEnvelope& seed, const std::string& alias,
                const std::string& prop, const std::string& value) {
    kg::KGOpSetProperty sp;
    sp.target.symbolic = alias;
    sp.property = prop;
    sp.value = value;
    seed.ops.push_back(kg::KGOp{sp});
}

// The commit the vendored tree actually pins, so mini-seeds below
// stay drift-warning-free across re-vendoring.
std::string pinned_commit() {
    std::string s = slurp(std::string(kSourceRoot) + "/SOURCE_COMMIT");
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' ||
                          s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

// Parse a small hand-built seed, stamping the real pinned commit.
kg::SeedEnvelope mini_seed(const std::string& file,
                           const std::string& ops_json) {
    const std::string text =
        "{\"source\":{\"file\":\"" + file + "\",\"commit\":\"" +
        pinned_commit() + "\"},\"layer\":\"test\",\"ops\":[" + ops_json +
        "]}";
    kg::SeedParseResult r = kg::parse_seed_envelope(text);
    CHECK(r.ok(), "mini seed parses: " + r.error);
    return r.seed;
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
    CHECK(seed.ops.size() == 20, "twenty ops parsed");
    CHECK(seed.layer == "cepheus", "layer round-trips");
    CHECK(seed.source.file == "book1/character-creation.md",
          "source file round-trips");
    CHECK(seed.invariants.count_of_type.size() == 7 &&
              seed.invariants.unique_name_per_type.size() == 4 &&
              seed.invariants.band_coverage.size() == 3,
          "all three invariant kinds parsed");
    const auto* mishap = find_coverage(seed, "mishap_table");
    CHECK(mishap && mishap->lo == 2 && mishap->hi == 3,
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
    CHECK(report.ops_applied == 20, "all twenty ops applied");
    CHECK(report.bindings.size() == 14, "fourteen aliases bound");

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

void test_loader_failure_rolls_back_the_whole_seed() {
    kg::SeedEnvelope seed = parse_fixture();
    const kg::OntologyRegistry reg = engine_registry();
    kg::KGModule world(reg);
    world.setMode(kg::KGMode::MINIMAL);

    const auto existing_constant = world.createEntity("RuleConstant");
    world.setProperty(existing_constant, "constant_value", "7");
    const auto existing_table = world.createEntity("RollableTable");
    const auto existing_row = world.createEntity("TableEntry");

    kg::KGOpSetProperty update_existing;
    update_existing.target.id = existing_constant;
    update_existing.property = "constant_value";
    update_existing.value = "99";
    seed.ops.push_back(kg::KGOp{update_existing});

    kg::KGOpSetProperty add_existing;
    add_existing.target.id = existing_constant;
    add_existing.property = "source_section";
    add_existing.value = "temporary";
    seed.ops.push_back(kg::KGOp{add_existing});

    kg::KGOpSetRelation relate_existing;
    relate_existing.from.id = existing_table;
    relate_existing.relation = "HAS_PART";
    relate_existing.to.id = existing_row;
    seed.ops.push_back(kg::KGOp{relate_existing});

    kg::KGOpSetRelation invalid;
    invalid.from.symbolic = "mishap_table";
    invalid.relation = "HAS_PART";
    invalid.to.symbolic = "missing_row";
    seed.ops.push_back(kg::KGOp{invalid});

    logosphere::EventBus events;
    world.set_event_bus(&events);
    const auto before = world.getStats();

    kg::SeedLoadReport report;
    const bool ok = kg::load_seed(seed, world, report);
    const auto after = world.getStats();

    CHECK(!ok, "a late dangling alias rejects the seed");
    CHECK(after.entity_count == before.entity_count,
          "a rejected seed leaves the entity set unchanged");
    CHECK(after.relation_count == before.relation_count,
          "a rejected seed leaves the relation set unchanged");
    CHECK(world.getProperty(existing_constant, "constant_value") == "7",
          "a rejected seed restores pre-existing property values");
    CHECK(!world.hasProperty(existing_constant, "source_section"),
          "a rejected seed removes properties that did not exist before");
    CHECK(world.getRelated(existing_table, "HAS_PART").empty(),
          "a rejected seed removes relations added to existing entities");
    CHECK(events.get_stats().total_events == 0,
          "a rejected seed emits no mutation events");
    CHECK(report.ops_applied == 0 && report.bindings.empty() &&
              report.created_ids.empty(),
          "a rejected seed exposes no partial load state");
}

void test_loader_publishes_events_after_commit() {
    kg::SeedEnvelope seed = parse_fixture();
    const kg::OntologyRegistry reg = engine_registry();
    kg::KGModule world(reg);
    world.setMode(kg::KGMode::MINIMAL);
    logosphere::EventBus events;
    world.set_event_bus(&events);

    bool every_callback_saw_the_complete_graph = true;
    size_t callbacks = 0;
    auto inspect = [&](const auto&) {
        ++callbacks;
        const auto tables = world.findByProperty("name", "survival_mishaps");
        every_callback_saw_the_complete_graph =
            every_callback_saw_the_complete_graph && tables.size() == 1 &&
            world.getRelated(tables[0], "HAS_PART").size() == 2;
    };
    events.state_changes().subscribe(inspect);
    events.relations().subscribe(inspect);

    kg::SeedLoadReport report;
    const bool ok = kg::load_seed(seed, world, report);

    CHECK(ok, "the positive seed commits with an event bus attached");
    CHECK(callbacks > 0, "a committed seed publishes mutation events");
    CHECK(every_callback_saw_the_complete_graph,
          "seed callbacks observe only the fully committed graph");
    CHECK(events.relations().event_count() == 6,
          "the committed seed publishes its six relation events once");
}

// A duplicate binder must fail BEFORE the offending op writes: a
// report that says "failed" while the world grew an entity is a lie
// about what happened.
void test_duplicate_alias_binder_mutates_nothing() {
    kg::SeedEnvelope seed = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"RuleConstant","as":"@dup",
            "properties":{"name":"one","constant_value":18,
              "source_quote":"All characters begin at the age of majority, typically 18."}},
           {"op":"create_entity","type":"RuleConstant","as":"@dup",
            "properties":{"name":"two","constant_value":18,
              "source_quote":"All characters begin at the age of majority, typically 18."}})");
    kg::KGModule world(engine_registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::SeedLoadReport report;
    const bool ok = kg::load_seed(seed, world, report);
    const size_t constants = world.findByType("RuleConstant").size();
    std::cout << "  [measure] load ok=" << (ok ? "true" : "false")
              << ", RuleConstant entities in world=" << constants
              << ", error: " << report.error << std::endl;
    CHECK(!ok, "a duplicate alias binder fails the load");
    CHECK(report.failed_op == 1, "and names the second op as the culprit");
    CHECK(constants == 0,
          "the whole seed rolls back, including the first entity");
    CHECK(report.ops_applied == 0 && report.bindings.empty(),
          "the failed load exposes no partial report state");
}

// The header promises a cleared report; a reused one leaked bindings
// across files, so a later seed could mutate an earlier seed's entity
// through a stale alias.
void test_report_reuse_is_fresh() {
    const kg::OntologyRegistry reg = engine_registry();
    kg::SeedEnvelope first = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"RuleConstant","as":"@shared",
            "properties":{"name":"first","constant_value":18,
              "source_quote":"All characters begin at the age of majority, typically 18."}})");
    // The second seed never binds @shared, so a fresh report must
    // leave it unresolvable.
    kg::SeedEnvelope second = mini_seed(
        "book1/character-creation.md",
        R"({"op":"set_property","target":"@shared","property":"name",
            "value":"hijacked"})");

    kg::KGModule world(reg);
    world.setMode(kg::KGMode::MINIMAL);
    kg::SeedLoadReport report;
    CHECK(kg::load_seed(first, world, report), "the first seed loads");
    const kg::EntityID id = report.bindings.at("shared");

    const bool ok = kg::load_seed(second, world, report);
    std::cout << "  [measure] second load ok=" << (ok ? "true" : "false")
              << ", bindings after=" << report.bindings.size()
              << ", name still '" << world.getProperty(id, "name") << "'"
              << std::endl;
    CHECK(!ok, "the second seed cannot resolve an alias it never bound");
    CHECK(report.bindings.empty(),
          "the report was cleared - no bindings leaked between seeds");
    CHECK(world.getProperty(id, "name") == "first",
          "and the first seed's entity was not mutated through a stale "
          "alias");
}

void test_loader_rejects_non_data_ops_atomically() {
    const kg::OntologyRegistry reg = engine_registry();

    {
        kg::SeedEnvelope seed = parse_fixture();
        kg::KGModule world(reg);
        world.setMode(kg::KGMode::MINIMAL);
        const auto existing = world.createEntity("RuleConstant");
        const auto before = world.getStats();

        kg::KGOpDestroyEntity destroy;
        destroy.target.id = existing;
        seed.ops.push_back(kg::KGOp{destroy});
        kg::SeedLoadReport report;

        CHECK(!kg::load_seed(seed, world, report),
              "a seed rejects destroy_entity");
        const auto after = world.getStats();
        CHECK(world.exists(existing) &&
                  after.entity_count == before.entity_count &&
                  after.relation_count == before.relation_count,
              "rejected destruction leaves the complete world unchanged");
        CHECK(report.error.find("not allowed") != std::string::npos,
              "rejected destruction explains the seed-layer rule");
    }

    {
        kg::SeedEnvelope seed = parse_fixture();
        kg::KGModule world(reg);
        world.setMode(kg::KGMode::MINIMAL);
        const auto before = world.getStats();

        kg::KGOpPlayCinematic cinematic;
        cinematic.name = "not_seed_data";
        seed.ops.push_back(kg::KGOp{cinematic});
        kg::SeedLoadReport report;

        CHECK(!kg::load_seed(seed, world, report),
              "a seed rejects play_cinematic");
        const auto after = world.getStats();
        CHECK(after.entity_count == before.entity_count &&
                  after.relation_count == before.relation_count,
              "rejected cinematic data leaves the world unchanged");
        CHECK(report.error.find("not allowed") != std::string::npos,
              "rejected cinematic data explains the seed-file rule");
    }
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
    CHECK(report.warnings.empty(),
          "and the envelope pin matches the vendored SOURCE_COMMIT");
    CHECK(report.quotes_checked == 14, "fourteen quotes checked");
    CHECK(report.ops_loaded == 20, "twenty ops loaded");
    CHECK(report.values_checked == 19, "nineteen numeric values checked");
    CHECK(report.bands_derived == 6, "six bands derived from quotes");
    CHECK(report.invariants_checked == 14, "fourteen invariants checked");
}

// The book prints its row bands four ways; honest extraction quotes
// them byte-exact, so the derivation must read all four. The "N-M"
// and "N through M" shapes ride in the positive fixture above; these
// two are the remaining notations, each with a real SRD quote.
void test_band_shapes_the_book_actually_prints() {
    // En dash (U+2013), Vehicle Damage table.
    kg::SeedEnvelope en_dash = mini_seed(
        "book1/personal-combat.md",
        R"({"op":"create_entity","type":"TableEntry","as":"@vd",
            "properties":{"name":"vd_1_3","roll_min":1,"roll_max":3,
              "source_quote":"| 1–3 | Single Hit |"}})");
    auto r1 = kg::verify_seed(en_dash, kSourceRoot, engine_registry());
    for (const auto& v : r1.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    std::cout << "  [measure] en dash: " << r1.bands_derived
              << " band derived" << std::endl;
    CHECK(r1.ok() && r1.bands_derived == 1,
          "an en-dash band '| 1-3 |' (U+2013) derives to [1, 3]");

    // Markdown-escaped negative, Aging table.
    kg::SeedEnvelope escaped = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"TableEntry","as":"@aging",
            "properties":{"name":"aging_minus_6","roll_min":-6,
              "roll_max":-6,
              "source_quote":"| \\-6 | Reduce three physical characteristics by 2, reduce one mental characteristic by 1 |"}})");
    auto r2 = kg::verify_seed(escaped, kSourceRoot, engine_registry());
    for (const auto& v : r2.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    std::cout << "  [measure] escaped negative: " << r2.bands_derived
              << " band derived" << std::endl;
    CHECK(r2.ok() && r2.bands_derived == 1,
          "a markdown-escaped negative cell '| \\-6 |' derives to [-6, -6]");
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

// THE BLOCKER this review found: a quote written by a later
// set_property op is what VALUE treats as ground truth, so VERBATIM
// must certify that same post-load state. Checking the raw create
// ops instead let an injected quote through uncertified.
void test_quote_rewritten_by_set_property_is_still_checked() {
    kg::SeedEnvelope seed = parse_fixture();
    append_set(seed, "age_of_majority", "source_quote",
               "All characters begin at the age of majority, typically "
               "19.");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "a quote injected by set_property is checked");
    CHECK(reason_contains(report, "verbatim", "not a byte-exact substring"),
          "and VERBATIM is what names it - the loaded world is the "
          "state under audit");
}

// A rewrite that lands on real book text must still PASS, or the
// check above would be trivially satisfied by refusing every
// set_property.
void test_rewritten_quote_that_is_real_passes() {
    kg::SeedEnvelope seed = parse_fixture();
    append_set(seed, "age_of_majority", "source_quote",
               "All characters begin at the age of majority, typically "
               "18.");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    for (const auto& v : report.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    CHECK(report.ok(), "a set_property rewrite to real text verifies");
}

// Per-entity source_file is the schema's promise (the Cited mixin
// documents quote-into-source_file), and it makes multi-file seeds
// legal. The positive fixture proves the passing direction with two
// rows quoted out of book1/skills.md; this is the failing one.
void test_per_entity_source_file_is_honored() {
    kg::SeedEnvelope seed = parse_fixture();
    // law_row_0's quote lives in skills.md; point it at the envelope
    // file, where that text does not appear.
    CHECK(set_prop(seed, "law_row_0", "source_file",
                   "book1/character-creation.md"),
          "mutation applied (row repointed at the wrong file)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "a quote checked against the wrong file fails");
    CHECK(reason_contains(report, "verbatim", "book1/character-creation.md"),
          "and the reason names the file it was checked against");
}

// The Cited contract: a type that declares source_quote must carry
// one when it ships as ingested data. Types without the slot are
// exempt - SkillRating is the canonical case: game STATE, not book
// content, so it is the one rulebook class without the Cited mixin
// and it has no quote to give.
void test_uncited_entity_of_a_cited_type_fails() {
    kg::SeedEnvelope seed = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"RuleConstant","as":"@naked",
            "properties":{"name":"uncited","constant_value":18}})");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "an uncited RuleConstant fails");
    CHECK(reason_contains(report, "verbatim", "uncited ingested entity"),
          "and VERBATIM names it as uncited");
}

void test_type_without_the_slot_is_exempt() {
    kg::SeedEnvelope seed = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"SkillRating","as":"@rating",
            "properties":{"name":"slug_rifle_2","skill_level":2}})");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    for (const auto& v : report.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    std::cout << "  [measure] quotes checked on a non-Cited type: "
              << report.quotes_checked << std::endl;
    CHECK(report.ok(),
          "a type that declares no source_quote is not required to cite");
}

void test_path_traversal_is_refused() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "law_row_0", "source_file",
                   "../../../../etc/passwd"),
          "mutation applied (source_file escapes the source root)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "a traversing source path fails");
    CHECK(reason_contains(report, "verbatim", "path traversal"),
          "and it is refused as traversal, before any file is opened");
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
    CHECK(reason_contains(report, "schema", "is a RollableTable, not a "
                                            "DiceExpression"),
          "and the reason names the class mismatch, not just 'invalid'");
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

// Substring containment passed 5 against "Dex 15+". The value must
// EQUAL one of the quote's number tokens, with thousands-commas
// absorbed inside a token.
void test_value_must_equal_a_whole_number_token() {
    const char* dex15 =
        "| Survival | Dex 15+ | Dex 5+ | Int 6+ | Str 6+ | Dex 7+ | "
        "Edu 4+ |";
    (void)dex15;  // shape reference; the seeds below quote real text

    // 5 against a quote whose only numbers are 15 and 8: must fail.
    kg::SeedEnvelope wrong = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"TaskCheck","as":"@t",
            "properties":{"name":"bogus","target_number":5,
              "source_quote":"A throw of Int 8+ means 'roll 2D6, add your Intelligence DM, and you succeed if you roll an 8 or more'."}})");
    auto r_wrong = kg::verify_seed(wrong, kSourceRoot, engine_registry());
    CHECK(!r_wrong.ok(), "5 does not pass on a quote that only says 8");
    CHECK(reason_contains(r_wrong, "value", "equal no number token"),
          "and VALUE says the digits equal no token");

    // The same slot with a number the quote really prints: passes.
    kg::SeedEnvelope right = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"TaskCheck","as":"@t",
            "properties":{"name":"real","target_number":8,
              "source_quote":"A throw of Int 8+ means 'roll 2D6, add your Intelligence DM, and you succeed if you roll an 8 or more'."}})");
    auto r_right = kg::verify_seed(right, kSourceRoot, engine_registry());
    for (const auto& v : r_right.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    CHECK(r_right.ok(), "8 passes on the quote that prints 8");

    // Thousands-comma: the book writes Cr10,000; the datum is 10000.
    kg::SeedEnvelope comma = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"GainMoney","as":"@debt",
            "properties":{"name":"legal_debt","amount":-10000,
              "source_quote":"| 3 | Honorably discharged from the service after a long legal battle. Legal issues create a debt of Cr10,000. |"}})");
    auto r_comma = kg::verify_seed(comma, kSourceRoot, engine_registry());
    for (const auto& v : r_comma.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    CHECK(r_comma.ok(),
          "10000 matches the book's 'Cr10,000' - commas inside a number "
          "token are absorbed, and the sign is dropped");
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
    // The dm rows tile [0,5]; declaring [0,7] leaves 6 uncovered at
    // the top, which is a gap, not an overlap.
    auto* cov = find_coverage(seed, "dm_table");
    CHECK(cov != nullptr, "the dm_table coverage assertion is present");
    cov->hi = 7;
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "a coverage gap fails");
    CHECK(reason_contains(report, "invariant", "gap"),
          "and the reason names the gap");
}

// A row that reaches outside the declared range is its own fault
// mode; calling it an "overlap" sent the reader hunting for a second
// row that does not exist.
void test_band_outside_declared_range_says_so() {
    kg::SeedEnvelope seed = parse_fixture();
    // Rows tile [0,5]; declare [1,5] so dm_row_0_2 starts below it.
    auto* cov = find_coverage(seed, "dm_table");
    CHECK(cov != nullptr, "the dm_table coverage assertion is present");
    cov->lo = 1;
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "a row reaching outside the declared range fails");
    CHECK(reason_contains(report, "invariant", "outside declared range"),
          "and it is reported as outside the range, not as an overlap");
}

void test_unnamed_instance_fails_invariant() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "dm_row_3_5", "name", ""),
          "mutation applied (a listed type's instance loses its name)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "an unnamed instance of a listed type fails");
    CHECK(reason_contains(report, "invariant", "instance has no name"),
          "and the reason says so - you cannot dedupe the unnamed");
}

// Source drift reports, it does not gate (owner CI ruling): a
// vendored tree that moved past the pin is a re-extraction TODO.
void test_source_commit_drift_warns_without_failing() {
    const std::string pinned = pinned_commit();
    std::cout << "  [measure] vendored SOURCE_COMMIT = "
              << pinned.substr(0, 12) << std::endl;
    CHECK(!pinned.empty(),
          "the vendored tree carries a SOURCE_COMMIT to compare against");

    // Matching pin: no warning (the positive fixture proves this too).
    kg::SeedEnvelope matched = parse_fixture();
    matched.source.commit = pinned;
    const auto clean = kg::verify_seed(matched, kSourceRoot,
                                       engine_registry());
    CHECK(clean.warnings.empty(), "a matching pin warns about nothing");

    // Drifted pin: warning, still verified.
    kg::SeedEnvelope drifted = parse_fixture();
    drifted.source.commit = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    const auto warned = kg::verify_seed(drifted, kSourceRoot,
                                        engine_registry());
    for (const auto& w : warned.warnings)
        std::cout << "  [measure] warning: " << w << std::endl;
    CHECK(warned.warnings.size() == 1, "a drifted pin produces one warning");
    CHECK(warned.ok(),
          "and drift does NOT fail verification - report, not gate");
}

void test_missing_source_commit_file_has_no_opinion() {
    // A source root with no SOURCE_COMMIT beside it: nothing to
    // compare, so nothing to say.
    kg::SeedEnvelope seed = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"RuleConstant","as":"@c",
            "properties":{"name":"age","constant_value":18,
              "source_quote":"All characters begin at the age of majority, typically 18."}})");
    const std::string sub_root =
        std::string(LOGOSPHERE_SOURCE_DIR) + "/examples/logovger/srd";
    seed.source.file = "cepheus/book1/character-creation.md";
    const auto report = kg::verify_seed(seed, sub_root, engine_registry());
    std::cout << "  [measure] warnings without a SOURCE_COMMIT file: "
              << report.warnings.size() << std::endl;
    CHECK(report.warnings.empty(),
          "no SOURCE_COMMIT beside the root means no drift opinion");
    CHECK(report.ok(), "and the seed still verifies through a deeper root");
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
    test_loader_failure_rolls_back_the_whole_seed();
    test_loader_publishes_events_after_commit();
    test_duplicate_alias_binder_mutates_nothing();
    test_report_reuse_is_fresh();
    test_loader_rejects_non_data_ops_atomically();
    test_positive_seed_verifies();
    test_band_shapes_the_book_actually_prints();
    test_misquote_fails_verbatim();
    test_quote_rewritten_by_set_property_is_still_checked();
    test_rewritten_quote_that_is_real_passes();
    test_per_entity_source_file_is_honored();
    test_uncited_entity_of_a_cited_type_fails();
    test_type_without_the_slot_is_exempt();
    test_path_traversal_is_refused();
    test_dangling_ref_fails_schema();
    test_wrong_class_ref_fails_schema();
    test_wrong_digits_fail_value();
    test_value_must_equal_a_whole_number_token();
    test_band_mismatch_fails_value();
    test_count_mismatch_fails_invariant();
    test_band_gap_fails_invariant();
    test_band_outside_declared_range_says_so();
    test_unnamed_instance_fails_invariant();
    test_duplicate_name_fails_invariant();
    test_source_commit_drift_warns_without_failing();
    test_missing_source_commit_file_has_no_opinion();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
