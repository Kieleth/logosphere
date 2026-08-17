// The seed verifier: extracted rule data enters only after verification.
//
// Design under test: docs/RPG_MODULE.md, step 4. A seed file (the
// envelope + KG-ops with @alias binders) is verified by five checks:
//
//   VERBATIM   every source_quote is a byte-exact substring of the
//              cited source file. No normalization.
//   SCHEMA     the seed loads through the seed loader (alias
//              resolution + validate_kg_op + apply) into a throwaway
//              world - refs-resolve comes for free.
//   VALUE      numeric slots have their digits in the entity's own
//              quote; table-row bands equal the quoted leading cell.
//   SEMANTIC   table row types, ordered outcome composition, and
//              procedure primitive and routing contracts agree with the
//              loaded ontology and graph structure.
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
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"
#include "logosphere/rules/procedure_runner.h"
#include "generated/earth_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/space_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

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
const char* kPrerequisiteFixture =
    LOGOSPHERE_SOURCE_DIR "/tests/fixtures/seed/prerequisite_base.json";
const char* kDependentFixture =
    LOGOSPHERE_SOURCE_DIR "/tests/fixtures/seed/prerequisite_dependent.json";
const char* kStandaloneFixture =
    LOGOSPHERE_SOURCE_DIR "/tests/fixtures/seed/rulebook_smoke.json";

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
    reg.extend(cepheus_book1_character_creation::ontology::registry());
    reg.extend(cepheus_book1_skills::ontology::registry());
    return reg;
}

// Two test-only typed outcomes keep the band-notation fixtures honest:
// the rows still carry a consequence matching their cited text, without
// teaching the engine what vehicle hits or aging reductions mean.
kg::OntologyRegistry band_registry() {
    auto reg = engine_registry();
    kg::OntologyRegistry extension("schema://band-notation-test");
    extension.addEntityType("SingleHitOutcome", "Outcome", false);
    extension.addAncestors(
        "SingleHitOutcome",
        {"Outcome", "Cited", "Entity", "Describable", "Identifiable",
         "Temporal"});
    extension.addEntityType("AgingReductionOutcome", "Outcome", false);
    extension.addAncestors(
        "AgingReductionOutcome",
        {"Outcome", "Cited", "Entity", "Describable", "Identifiable",
         "Temporal"});
    reg.extend(extension);
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

kg::SeedEnvelope parse_seed_fixture(const char* path) {
    const std::string text = slurp(path);
    CHECK(!text.empty(), std::string(path) + " is readable");
    kg::SeedParseResult parsed = kg::parse_seed_envelope(text);
    CHECK(parsed.ok(), std::string(path) + " parses: " + parsed.error);
    return parsed.seed;
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
    CHECK(seed.ops.size() == 28, "twenty-eight ops parsed");
    CHECK(seed.layer == "cepheus", "layer round-trips");
    CHECK(seed.source.file == "book1/character-creation.md",
          "source file round-trips");
    CHECK(seed.invariants.count_of_type.size() == 13 &&
              seed.invariants.unique_name_per_type.size() == 6 &&
              seed.invariants.band_coverage.size() == 3,
          "all three invariant kinds parsed");
    const auto* mishap = find_coverage(seed, "mishap_table");
    CHECK(mishap && mishap->lo == 2 && mishap->hi == 3,
          "band_coverage alias stripped and range kept");
}

void test_unbounded_band_coverage_parses_explicit_null() {
    const auto parsed = kg::parse_seed_envelope(
        R"({"source":{"file":"f","commit":"c"},"layer":"x",
             "invariants":{"band_coverage":{"@t":[0,null]}},"ops":[]})");
    CHECK(parsed.ok(), "an explicit null upper coverage bound parses: " +
                           parsed.error);
    if (!parsed.ok()) return;
    const auto& coverage = parsed.seed.invariants.band_coverage.front();
    CHECK(coverage.lo.has_value() && *coverage.lo == 0 &&
              !coverage.hi.has_value(),
          "null means explicitly upper-unbounded, not a numeric default");
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
    CHECK(report.ops_applied == 28, "all twenty-eight ops applied");
    CHECK(report.bindings.size() == 20, "twenty aliases bound");

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

// The failure this is written from: cepheus_book1_skills.json and
// cepheus_careers.json each created "Gun Combat", and nothing
// complained, because uniqueness was only ever checked inside one
// file. Seeds share a world; the promise has to be checked there.
void test_a_second_seed_cannot_recreate_a_name() {
    auto reg = engine_registry();
    kg::KGModule world(reg);
    world.setMode(kg::KGMode::MINIMAL);

    const char* skill_op =
        "{\"op\":\"create_entity\",\"type\":\"Skill\","
        "\"as\":\"@gun\",\"properties\":{\"name\":\"Gun Combat\"}}";

    auto first = mini_seed("book1/skills.md", skill_op);
    first.invariants.unique_name_per_type = {"Skill"};
    kg::SeedLoadReport a;
    CHECK(kg::load_seed(first, world, a), "the first seed loads: " + a.error);

    auto second = mini_seed("book1/character-creation.md", skill_op);
    second.invariants.unique_name_per_type = {"Skill"};
    kg::SeedLoadReport b;
    CHECK(!kg::load_seed(second, world, b),
          "a second seed must not recreate a name already loaded");
    CHECK(b.error.find("canonical @@entity/") != std::string::npos,
          "the error must say how to reference it instead: " + b.error);
    CHECK(world.findByType("Skill").size() == 1,
          "the refused seed must leave nothing behind");
    std::cout << "  [measure] " << b.error << std::endl;
}

void test_loader_materializes_seed_origin_contexts() {
    kg::SeedEnvelope seed = parse_fixture();
    kg::KGModule world(engine_registry());
    world.setMode(kg::KGMode::MINIMAL);

    kg::SeedLoadReport report;
    const bool ok = kg::load_seed(seed, world, report);
    CHECK(ok, "a cited seed loads with engine-owned origin context: " +
                  report.error);
    if (!ok) return;

    const auto layers = world.findByType("SourceLayerContext");
    const auto documents = world.findByType("SourceDocumentContext");
    CHECK(layers.size() == 1 && documents.size() == 1,
          "one source layer and one exact source document become KG "
          "entities");
    if (layers.size() != 1 || documents.size() != 1) return;

    CHECK(world.getProperty(layers[0], "source_layer") == seed.layer &&
              world.getProperty(documents[0], "source_file") ==
                  seed.source.file &&
              world.getProperty(documents[0], "source_commit") ==
                  seed.source.commit &&
              world.getProperty(documents[0], "source_layer_context") ==
                  std::to_string(layers[0]),
          "context entities preserve the envelope layer, file, commit, and "
          "parent layer");

    const auto check = report.bindings.at("int_throw");
    CHECK(world.getProperty(check, "origin_context") ==
              std::to_string(documents[0]),
          "a cited rule points to the exact source document context");
    CHECK(world.getProperty(check, "identity_context") ==
                  std::to_string(documents[0]) &&
              world.getProperty(check, "entity_key") == "int_throw",
          "a cited rule gets portable identity from its source document "
          "and seed alias");

    kg::KGOpBatchReport batch;
    CHECK(!kg::apply_kg_ops_atomically(
              {kg::KGOp{kg::KGOpSetProperty{{check, ""}, "target_number",
                                             "99"}}},
              world, batch) &&
              batch.error.find("sealed origin") != std::string::npos &&
              world.getProperty(check, "target_number") == "8",
          "the validated runtime path cannot mutate a published rule");
}

void test_loader_owns_seed_portable_identity() {
    {
        kg::SeedEnvelope seed = mini_seed(
            "book1/character-creation.md",
            R"({"op":"create_entity","type":"RuleConstant",
                "as":"@rule","properties":{"name":"age",
                "constant_value":18,"entity_key":"forged"}})");
        kg::KGModule world(engine_registry());
        world.setMode(kg::KGMode::MINIMAL);
        kg::SeedLoadReport report;

        CHECK(!kg::load_seed(seed, world, report),
              "seed content cannot choose its portable identity");
        CHECK(report.error.find("entity_key is seed-loader-owned") !=
                  std::string::npos,
              "the rejection names the loader-owned identity field: " +
                  report.error);
        CHECK(world.findByType("RuleConstant").empty(),
              "rejected identity forgery leaves no rule content");
    }

    {
        kg::SeedEnvelope seed = mini_seed(
            "book1/character-creation.md",
            R"({"op":"create_entity","type":"RuleConstant",
                "properties":{"name":"age","constant_value":18}})");
        kg::KGModule world(engine_registry());
        world.setMode(kg::KGMode::MINIMAL);
        kg::SeedLoadReport report;

        CHECK(!kg::load_seed(seed, world, report),
              "addressable seed content requires a machine alias");
        CHECK(report.error.find("requires a non-empty create_entity alias") !=
                  std::string::npos,
              "the missing alias error explains the identity requirement: " +
                  report.error);
        CHECK(world.findByType("RuleConstant").empty(),
              "missing identity alias leaves no rule content");
    }
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

void test_loader_rejects_missing_required_properties_atomically() {
    kg::OntologyRegistry reg("schema://required-test");
    reg.addEntityType("RequiredRecord", "", false);
    reg.addProperty("RequiredRecord", "code",
                    kg::PropertyValueKind::String, true);

    kg::SeedEnvelope seed;
    seed.ops.push_back(kg::KGOp{kg::KGOpCreateEntity{
        "RequiredRecord", {{"code", "complete"}}, "first"}});
    seed.ops.push_back(kg::KGOp{kg::KGOpCreateEntity{
        "RequiredRecord", {}, "missing"}});

    kg::KGModule world(reg);
    world.setMode(kg::KGMode::MINIMAL);
    kg::SeedLoadReport report;
    const bool ok = kg::load_seed(seed, world, report);

    CHECK(!ok, "a seed entity missing a required property is rejected");
    CHECK(report.error.find("RequiredRecord.code") != std::string::npos,
          "the load error names the missing required property");
    CHECK(world.findByType("RequiredRecord").empty(),
          "the earlier valid entity is rolled back with the failed seed");
    CHECK(report.ops_applied == 0 && report.bindings.empty(),
          "the rejected seed exposes no partial load state");
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
    CHECK(events.relations().event_count() == 8,
          "the committed seed publishes its eight relation events once");
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

void test_seed_content_cannot_create_engine_owned_contexts() {
    kg::SeedEnvelope seed = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"SourceLayerContext",
            "as":"@forged_origin",
            "properties":{"context_key":"source-layer:forged",
                          "source_layer":"forged"}})");
    kg::KGModule world(engine_registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::SeedLoadReport report;

    CHECK(!kg::load_seed(seed, world, report),
          "seed content cannot manufacture engine-owned origin contexts");
    CHECK(report.error.find("seed-loader-owned") != std::string::npos,
          "the rejection names the ownership boundary");
    CHECK(world.findByType("SourceLayerContext").empty() &&
              world.findByType("SourceDocumentContext").empty(),
          "the rejected provenance forgery leaves no context entities");
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
              << report.semantics_checked << " semantics, "
              << report.invariants_checked << " invariants" << std::endl;
    CHECK(report.ok(), "the positive fixture verifies clean");
    CHECK(report.warnings.empty(),
          "and the envelope pin matches the vendored SOURCE_COMMIT");
    CHECK(report.quotes_checked == 20, "twenty quotes checked");
    CHECK(report.ops_loaded == 28, "twenty-eight ops loaded");
    CHECK(report.values_checked == 12,
          "twelve numeric values checked - a row's band is proven by "
          "the row KEY it addresses, not by digits in its text");
    CHECK(report.bands_derived == 6, "six bands derived from quotes");
    CHECK(report.semantics_checked == 13,
          "thirteen table, check, and outcome structures checked");
    CHECK(report.invariants_checked == 22,
          "twenty-two declared invariants checked");
}

// The book prints its row bands four ways; honest extraction quotes
// them byte-exact, so the derivation must read all four. The "N-M"
// and "N through M" shapes ride in the positive fixture above; these
// two are the remaining notations, each with a real SRD quote.
void test_band_shapes_the_book_actually_prints() {
    // En dash (U+2013), Vehicle Damage table.
    kg::SeedEnvelope en_dash = mini_seed(
        "book1/personal-combat.md",
        R"({"op":"create_entity","type":"SingleHitOutcome","as":"@hit",
            "properties":{"name":"single_hit",
              "source_section":"Vehicle Damage",
              "source_quote":"| 1–3 | Single Hit |"}},
           {"op":"create_entity","type":"TableEntry","as":"@vd",
            "properties":{"name":"vd_1_3","roll_min":1,"roll_max":3,
              "outcome":"@hit",
              "source_section":"Vehicle Damage",
              "source_quote":"| 1–3 | Single Hit |"}})");
    auto r1 = kg::verify_seed(en_dash, kSourceRoot, band_registry());
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
        R"({"op":"create_entity","type":"AgingReductionOutcome","as":"@reduce",
            "properties":{"name":"aging_reduction",
              "source_section":"Aging",
              "source_quote":"| \\-6 | Reduce three physical characteristics by 2, reduce one mental characteristic by 1 |"}},
           {"op":"create_entity","type":"TableEntry","as":"@aging",
            "properties":{"name":"aging_minus_6","roll_min":-6,
              "roll_max":-6,"outcome":"@reduce",
              "source_section":"Aging",
              "source_quote":"| \\-6 | Reduce three physical characteristics by 2, reduce one mental characteristic by 1 |"}})");
    auto r2 = kg::verify_seed(escaped, kSourceRoot, band_registry());
    for (const auto& v : r2.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    std::cout << "  [measure] escaped negative: " << r2.bands_derived
              << " band derived" << std::endl;
    CHECK(r2.ok() && r2.bands_derived == 1,
          "a markdown-escaped negative cell '| \\-6 |' derives to [-6, -6]");
}

logosphere::rules::ProcedurePrimitiveRegistry procedure_contracts() {
    logosphere::rules::ProcedurePrimitiveRegistry registry;
    std::string error;
    CHECK(registry.declare_primitive("choose_career", {"failed"}, error),
          "the choose_career contract declares: " + error);
    CHECK(registry.declare_primitive("finish_character", {}, error),
          "the finish_character contract declares: " + error);
    return registry;
}

kg::SeedEnvelope complete_procedure_seed() {
    return mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"Procedure","as":"@procedure",
            "properties":{"name":"test_chargen",
              "source_section":"Character Creation Checklist",
              "source_kind":"heading",
              "source_quote":"Character Creation Checklist"}},
           {"op":"create_entity","type":"ProcedureStep","as":"@choose",
            "properties":{"name":"choose","step_index":0,
              "primitive_ref":"choose_career",
              "source_section":"Character Creation Checklist",
              "source_quote":"Choose a career. You cannot choose a career you've already left except Drifter."}},
           {"op":"create_entity","type":"ProcedureStep","as":"@finish",
            "properties":{"name":"finish","step_index":1,
              "primitive_ref":"finish_character",
              "source_section":"Character Creation Checklist",
              "source_quote":"If you wish to leave this career, go to step 10."}},
           {"op":"create_entity","type":"StepRoute","as":"@failed",
            "properties":{"name":"failed","route_label":"failed",
              "next_step":"@finish",
              "source_section":"Character Creation Checklist",
              "source_quote":"If you do not qualify for that career"}},
           {"op":"set_relation","from":"@procedure","relation":"HAS_PART","to":"@choose"},
           {"op":"set_relation","from":"@procedure","relation":"HAS_PART","to":"@finish"},
           {"op":"set_relation","from":"@choose","relation":"HAS_PART","to":"@failed"})");
}

void test_procedures_verify_against_registered_primitive_contracts() {
    const auto contracts = procedure_contracts();
    const auto complete = kg::verify_seed(
        complete_procedure_seed(), kSourceRoot, engine_registry(),
        &contracts);
    for (const auto& violation : complete.violations) {
        std::cout << "  [measure] UNEXPECTED [" << violation.check << "] "
                  << violation.reason << std::endl;
    }
    CHECK(complete.ok(),
          "a complete procedure verifies against its primitive contracts");

    const auto no_catalog = kg::verify_seed(
        complete_procedure_seed(), kSourceRoot, engine_registry());
    CHECK(reason_contains(no_catalog, "semantic", "primitive registry"),
          "procedure verification fails when no primitive registry exists");

    auto unknown = complete_procedure_seed();
    CHECK(set_prop(unknown, "choose", "primitive_ref", "invented"),
          "the unknown primitive mutation was applied");
    const auto unknown_report = kg::verify_seed(
        unknown, kSourceRoot, engine_registry(), &contracts);
    CHECK(reason_contains(unknown_report, "semantic",
                          "unknown primitive 'invented'"),
          "an unknown primitive fails semantic verification");

    auto label = complete_procedure_seed();
    CHECK(set_prop(label, "failed", "route_label", "maybe"),
          "the undeclared route-label mutation was applied");
    const auto label_report = kg::verify_seed(
        label, kSourceRoot, engine_registry(), &contracts);
    CHECK(reason_contains(label_report, "semantic",
                          "undeclared route_label 'maybe'"),
          "a route label outside the primitive contract fails");

    auto gap = complete_procedure_seed();
    CHECK(set_prop(gap, "finish", "step_index", "2"),
          "the procedure step gap mutation was applied");
    const auto gap_report = kg::verify_seed(
        gap, kSourceRoot, engine_registry(), &contracts);
    CHECK(reason_contains(gap_report, "semantic", "not contiguous"),
          "procedure step indices must be contiguous from zero");

    auto duplicate = complete_procedure_seed();
    duplicate.ops.push_back(kg::KGOp{kg::KGOpCreateEntity{
        "StepRoute",
        {{"name", "failed_again"},
         {"route_label", "failed"},
         {"next_step", "@finish"},
         {"source_section", "Character Creation Checklist"},
         {"source_quote", "If you do not qualify for that career"}},
        "failed_again"}});
    kg::KGOpSetRelation attach_duplicate;
    attach_duplicate.from.symbolic = "choose";
    attach_duplicate.relation = "HAS_PART";
    attach_duplicate.to.symbolic = "failed_again";
    duplicate.ops.push_back(kg::KGOp{attach_duplicate});
    const auto duplicate_report = kg::verify_seed(
        duplicate, kSourceRoot, engine_registry(), &contracts);
    CHECK(reason_contains(duplicate_report, "semantic",
                          "duplicate route_label 'failed'"),
          "one procedure step cannot define the same route twice");

    const auto cross_procedure = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"Procedure","as":"@first",
            "properties":{"name":"first",
              "source_section":"Character Creation Checklist",
              "source_kind":"heading",
              "source_quote":"Character Creation Checklist"}},
           {"op":"create_entity","type":"ProcedureStep","as":"@choose",
            "properties":{"name":"choose","step_index":0,
              "primitive_ref":"choose_career",
              "source_section":"Character Creation Checklist",
              "source_quote":"Choose a career. You cannot choose a career you've already left except Drifter."}},
           {"op":"create_entity","type":"Procedure","as":"@second",
            "properties":{"name":"second",
              "source_section":"Character Creation Checklist",
              "source_kind":"heading",
              "source_quote":"Character Creation Checklist"}},
           {"op":"create_entity","type":"ProcedureStep","as":"@finish",
            "properties":{"name":"finish","step_index":0,
              "primitive_ref":"finish_character",
              "source_section":"Character Creation Checklist",
              "source_quote":"If you wish to leave this career, go to step 10."}},
           {"op":"create_entity","type":"StepRoute","as":"@escaped",
            "properties":{"name":"escaped","route_label":"failed",
              "next_step":"@finish",
              "source_section":"Character Creation Checklist",
              "source_quote":"If you do not qualify for that career"}},
           {"op":"set_relation","from":"@first","relation":"HAS_PART","to":"@choose"},
           {"op":"set_relation","from":"@second","relation":"HAS_PART","to":"@finish"},
           {"op":"set_relation","from":"@choose","relation":"HAS_PART","to":"@escaped"})");
    const auto cross_report = kg::verify_seed(
        cross_procedure, kSourceRoot, engine_registry(), &contracts);
    CHECK(reason_contains(cross_report, "semantic", "outside Procedure"),
          "a route cannot jump to a step owned by another procedure");
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
    CHECK(reason_contains(report, "verbatim", "is not in section"),
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

void test_missing_source_section_fails_citation() {
    kg::SeedEnvelope seed = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"RuleConstant","as":"@age",
            "properties":{"name":"age","constant_value":18,
              "source_quote":"All characters begin at the age of majority, typically 18."}})");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "a cited entity without a source section fails");
    CHECK(reason_contains(report, "verbatim", "source_section"),
          "and the citation error names the missing source_section");
}

void test_wrong_source_section_fails_citation() {
    kg::SeedEnvelope seed = parse_fixture();
    CHECK(set_prop(seed, "age_of_majority", "source_section", "Survival"),
          "mutation applied (real quote assigned to the wrong section)");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "a real quote under the wrong heading fails");
    CHECK(reason_contains(report, "verbatim", "is not in section"),
          "and the citation error explains the section mismatch: the "
          "locator names the section it was asked for and could not "
          "find the text under");
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
        R"({"op":"create_entity","type":"Entity","as":"@skill"},
            {"op":"create_entity","type":"SkillRating","as":"@rating",
             "properties":{"name":"slug_rifle_2","skill":"@skill",
                           "skill_level":2}})");
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
        R"({"op":"create_entity","type":"RuleConstant","as":"@t",
            "properties":{"name":"bogus","constant_value":5,
              "source_section":"Careers",
              "source_quote":"A throw of Int 8+ means 'roll 2D6, add your Intelligence DM, and you succeed if you roll an 8 or more'."}})");
    auto r_wrong = kg::verify_seed(wrong, kSourceRoot, engine_registry());
    CHECK(!r_wrong.ok(), "5 does not pass on a quote that only says 8");
    CHECK(reason_contains(r_wrong, "value", "equal no number token"),
          "and VALUE says the digits equal no token");

    // The same slot with a number the quote really prints: passes.
    kg::SeedEnvelope right = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"RuleConstant","as":"@t",
            "properties":{"name":"real","constant_value":8,
              "source_section":"Careers",
              "source_quote":"A throw of Int 8+ means 'roll 2D6, add your Intelligence DM, and you succeed if you roll an 8 or more'."}})");
    auto r_right = kg::verify_seed(right, kSourceRoot, engine_registry());
    for (const auto& v : r_right.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    CHECK(r_right.ok(), "8 passes on the quote that prints 8");

    // Thousands-comma: the book writes Cr10,000; the datum is 10000.
    kg::SeedEnvelope comma = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"Entity","as":"@credits"},
            {"op":"create_entity","type":"GainFixedMoney","as":"@debt",
            "properties":{"name":"legal_debt","currency":"@credits",
              "amount":-10000,
              "source_section":"Survival",
              "source_quote":"| 3 | Honorably discharged from the service after a long legal battle. Legal issues create a debt of Cr10,000. |"}})");
    auto r_comma = kg::verify_seed(comma, kSourceRoot, engine_registry());
    for (const auto& v : r_comma.violations)
        std::cout << "  [measure] UNEXPECTED [" << v.check << "] "
                  << v.reason << std::endl;
    CHECK(r_comma.ok(),
          "10000 matches the book's 'Cr10,000' - commas inside a number "
          "token are absorbed, and the sign is dropped");
}

void test_dice_fields_must_match_the_same_quoted_expression() {
    kg::SeedEnvelope seed = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"DiceExpression","as":"@swapped",
            "properties":{"name":"swapped_2d6","dice_count":6,
              "dice_sides":2,
              "source_section":"Generating Characteristic Scores",
              "source_quote":"Roll your six characteristics using 2D6, and record them in the standard order"}})");
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    CHECK(!report.ok(), "6D2 cannot pass against a quote that says 2D6");
    CHECK(reason_contains(report, "value", "quoted dice expression"),
          "and VALUE reports a field-bound dice mismatch");

    kg::SeedEnvelope multiplied = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"DiceExpression","as":"@medical",
            "properties":{"name":"medical_cost","dice_count":1,
              "dice_sides":6,"dice_multiplier":10000,
              "source_section":"Aging Crisis",
              "source_quote":"he can pay 1D6×10,000 Credits for medical care"}})");
    const auto multiplied_report = kg::verify_seed(
        multiplied, kSourceRoot, engine_registry());
    CHECK(multiplied_report.ok(),
          "1D6x10000 matches the book's multiplied dice expression");

    kg::SeedEnvelope en_dash_modifier = mini_seed(
        "book3/environments-and-hazards.md",
        R"({"op":"create_entity","type":"DiceExpression","as":"@duration",
            "properties":{"name":"regina_flu_duration","dice_count":1,
              "dice_sides":6,"dice_modifier":-2,
              "source_section":"Diseases","source_kind":"cell",
              "source_table":"Disease","source_row":"Regina Flu",
              "source_column":"Damage",
              "source_quote":"1D6–2"}})");
    const auto en_dash_report = kg::verify_seed(
        en_dash_modifier, kSourceRoot, engine_registry());
    CHECK(en_dash_report.ok(),
          "1D6-2 matches the book's en-dash modifier notation");

    kg::SeedEnvelope oversized = mini_seed(
        "book1/character-creation.md",
        R"({"op":"create_entity","type":"DiceExpression","as":"@huge",
            "properties":{"name":"oversized_modifier","dice_count":2,
              "dice_sides":6,"dice_modifier":2147483648,
              "source_section":"Generating Characteristic Scores",
              "source_quote":"Roll your six characteristics using 2D6, and record them in the standard order"}})");
    bool threw = false;
    kg::SeedVerifyReport oversized_report;
    try {
        oversized_report = kg::verify_seed(
            oversized, kSourceRoot, engine_registry());
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(!threw, "an oversized dice field produces a report, not an exception");
    CHECK(!oversized_report.ok(),
          "an oversized dice field fails verification");
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
              "source_section":"Chapter 1: Character Creation",
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

// Books spell small numbers out. "Reduce three physical
// characteristics by 2" states a count of three as plainly as a
// numeral would, and a verifier that only reads digits can prove the
// 2 and nothing about the three.
void test_a_count_written_as_a_word_is_proof() {
    const char* op =
        "{\"op\":\"create_entity\",\"type\":\"RuleConstant\","
        "\"as\":\"@spelled\",\"properties\":{"
        "\"name\":\"aging_physical_count\",\"constant_value\":\"3\","
        "\"source_section\":\"Aging\",\"source_kind\":\"cell\","
        "\"source_table\":\"2D6\",\"source_row\":\"-5\","
        "\"source_column\":\"Effects of Aging\","
        "\"source_quote\":\"Reduce three physical characteristics by 2.\"}}";
    auto seed = mini_seed("book1/character-creation.md", op);
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "value");
    CHECK(report.count("value") == 0,
          "a count spelled as a word proves the slot that carries it");

    // And it is still proof, not permission: a number the text does
    // not state, in words or digits, must fail exactly as before.
    CHECK(set_prop(seed, "spelled", "constant_value", "4"),
          "the wrong-count mutation applied");
    const auto wrong = kg::verify_seed(seed, kSourceRoot,
                                       engine_registry());
    CHECK(!wrong.ok() && wrong.count("value") > 0,
          "a count the quote does not state still fails");
}

// A book can state a number without writing it. Cepheus: "An
// additional benefit is gained if the character held rank O4" is a
// count of one, carried by the indefinite article, and no tokeniser
// will ever find it. implied_by names the words that carry it and
// becomes the proof - but only if those words are really there.
void test_a_count_the_text_implies_is_proved_by_the_words_that_imply_it() {
    const char* op =
        "{\"op\":\"create_entity\",\"type\":\"RuleConstant\","
        "\"as\":\"@implied\",\"properties\":{"
        "\"name\":\"rank_o4_extra_benefits\",\"constant_value\":\"1\","
        "\"source_section\":\"Mustering Out Benefits\","
        "\"source_kind\":\"sentence\","
        "\"implied_by\":\"An additional benefit is gained\","
        "\"source_quote\":\"An additional benefit is gained if the "
        "character held rank O4, and two for rank O5.\"}}";
    auto seed = mini_seed("book1/character-creation.md", op);
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "value");
    CHECK(report.count("value") == 0,
          "a count the words imply is proved by the words that imply it, "
          "where no digit and no number word exists to prove it");

    // A reading, not a licence: the phrase has to be IN the quote, so
    // a marker invented to smuggle a number past the check fails.
    CHECK(set_prop(seed, "implied", "implied_by",
                   "a benefit is gained for every hat worn"),
          "the invented-phrase mutation applied");
    const auto invented = kg::verify_seed(seed, kSourceRoot,
                                          engine_registry());
    CHECK(!invented.ok() && invented.count("value") > 0,
          "a phrase the quote does not contain proves nothing");
}

// "| 1+ |" is "1 or higher" in the shorthand the book prints, and it
// appears three times in the vendored SRD.
void test_an_open_topped_band_is_derived() {
    const char* op =
        "{\"op\":\"create_entity\",\"type\":\"RuleConstant\","
        "\"as\":\"@plus\",\"properties\":{"
        "\"name\":\"aging_no_effect_floor\",\"constant_value\":\"1\","
        "\"source_section\":\"Aging\",\"source_kind\":\"cell\","
        "\"source_table\":\"2D6\",\"source_row\":\"1+\","
        "\"source_column\":\"Effects of Aging\","
        "\"source_quote\":\"No effect\"}}";
    auto seed = mini_seed("book1/character-creation.md", op);
    const auto report = kg::verify_seed(seed, kSourceRoot,
                                        engine_registry());
    print_first(report, "verbatim");
    CHECK(report.count("verbatim") == 0,
          "a row keyed '1+' resolves in the source like any other");
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
            v.reason.find("age_of_majority") != std::string::npos &&
            v.reason.find("2 times") != std::string::npos) {
            names_dup = true;
        }
    }
    CHECK(names_dup,
          "the reason names the duplicate and how many there are");
}

void test_seed_sequence_is_cumulative_and_ordered() {
    kg::KGModule empty_world(engine_registry());
    empty_world.setMode(kg::KGMode::MINIMAL);
    kg::SeedSequenceLoadReport empty_report;
    CHECK(!kg::verify_and_load_seed_sequence(
              {}, kSourceRoot, empty_world, empty_report),
          "an empty required seed sequence is refused");
    CHECK(empty_report.error.find("empty") != std::string::npos,
          "the empty-sequence refusal is actionable");

    std::vector<kg::SeedEnvelope> ordered{
        parse_seed_fixture(kPrerequisiteFixture),
        parse_seed_fixture(kDependentFixture),
    };

    kg::KGModule world(engine_registry());
    world.setMode(kg::KGMode::MINIMAL);
    kg::SeedSequenceLoadReport report;
    CHECK(kg::verify_and_load_seed_sequence(
              ordered, kSourceRoot, world, report),
          "the dependent seed verifies and loads after its prerequisite: " +
              report.error);
    std::cout << "  [measure] cumulative sequence loaded "
              << report.seeds_loaded << " seeds" << std::endl;
    CHECK(report.seeds_loaded == 2,
          "the cumulative loader reports both loaded seeds");
    CHECK(world.findByType("RuleConstant").size() == 2,
          "both ordered seed consequences reached the world");

    std::vector<kg::SeedEnvelope> reversed{ordered[1], ordered[0]};
    kg::KGModule reversed_world(engine_registry());
    reversed_world.setMode(kg::KGMode::MINIMAL);
    kg::SeedSequenceLoadReport reversed_report;
    CHECK(!kg::verify_and_load_seed_sequence(
              reversed, kSourceRoot, reversed_world, reversed_report),
          "a dependent seed presented before its prerequisite is refused");
    std::cout << "  [measure] wrong-order refusal: "
              << reversed_report.error << std::endl;
    CHECK(reversed_report.failed_seed == 0,
          "the report identifies the first, wrongly ordered seed");
    CHECK(reversed_report.error.find(
              "no matching Addressable entity is loaded") !=
              std::string::npos,
          "the refusal names the missing prerequisite entity");
    CHECK(reversed_world.findByType("RuleConstant").empty(),
          "wrong order mutates nothing before refusal");

    kg::SeedEnvelope drifted = parse_seed_fixture(kPrerequisiteFixture);
    drifted.source.commit = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    std::vector<kg::SeedEnvelope> warning_sequence{
        std::move(drifted), parse_seed_fixture(kStandaloneFixture)};
    kg::KGModule warning_world(engine_registry());
    warning_world.setMode(kg::KGMode::MINIMAL);
    kg::SeedSequenceLoadReport warning_report;
    CHECK(kg::verify_and_load_seed_sequence(
              warning_sequence, kSourceRoot, warning_world,
              warning_report),
          "a non-gating prerequisite warning does not fail the sequence: " +
              warning_report.error);
    CHECK(warning_report.verifications.size() == 2,
          "the sequence retains one verification report per seed");
    CHECK(warning_report.verifications.front().warnings.size() == 1 &&
              warning_report.verifications.back().warnings.empty(),
          "an earlier prerequisite warning survives a clean target");
}

}  // namespace

int main() {
    std::cout << "Seed verifier (extracted rule data, engine verification)"
              << std::endl;
    test_envelope_parses();
    test_unbounded_band_coverage_parses_explicit_null();
    test_envelope_parser_is_loud();
    test_loader_binds_and_resolves();
    test_a_second_seed_cannot_recreate_a_name();
    test_loader_materializes_seed_origin_contexts();
    test_loader_owns_seed_portable_identity();
    test_loader_failure_rolls_back_the_whole_seed();
    test_loader_rejects_missing_required_properties_atomically();
    test_loader_publishes_events_after_commit();
    test_duplicate_alias_binder_mutates_nothing();
    test_report_reuse_is_fresh();
    test_loader_rejects_non_data_ops_atomically();
    test_seed_content_cannot_create_engine_owned_contexts();
    test_positive_seed_verifies();
    test_band_shapes_the_book_actually_prints();
    test_procedures_verify_against_registered_primitive_contracts();
    test_misquote_fails_verbatim();
    test_quote_rewritten_by_set_property_is_still_checked();
    test_rewritten_quote_that_is_real_passes();
    test_per_entity_source_file_is_honored();
    test_missing_source_section_fails_citation();
    test_wrong_source_section_fails_citation();
    test_uncited_entity_of_a_cited_type_fails();
    test_type_without_the_slot_is_exempt();
    test_path_traversal_is_refused();
    test_dangling_ref_fails_schema();
    test_wrong_class_ref_fails_schema();
    test_wrong_digits_fail_value();
    test_value_must_equal_a_whole_number_token();
    test_dice_fields_must_match_the_same_quoted_expression();
    test_band_mismatch_fails_value();
    test_count_mismatch_fails_invariant();
    test_band_gap_fails_invariant();
    test_band_outside_declared_range_says_so();
    test_unnamed_instance_fails_invariant();
    test_a_count_written_as_a_word_is_proof();
    test_a_count_the_text_implies_is_proved_by_the_words_that_imply_it();
    test_an_open_topped_band_is_derived();
    test_duplicate_name_fails_invariant();
    test_source_commit_drift_warns_without_failing();
    test_missing_source_commit_file_has_no_opinion();
    test_seed_sequence_is_cumulative_and_ordered();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
