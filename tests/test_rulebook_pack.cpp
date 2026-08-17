// The rulebook pack: what any book of rules is made of.
//
// Design under test: docs/RPG_MODULE.md. The pack earns its classes
// by the rule of two, so this test instantiates each class from the
// Cepheus SRD chapter 1 (examples/logovger/srd/cepheus), with the
// verbatim quotes the classes were justified by. Three contracts:
//
//   1. LAYERING - the vocabulary exists only when the pack is
//      loaded; abstract Outcome never instantiates.
//   2. CITATION - every source_quote written here string-matches
//      into the vendored SRD file it claims. This is the ingestion
//      verifier's verbatim check in miniature: a misquote fails the
//      build, which is precisely what it will do to a hallucinated
//      extraction.
//   3. CONNECTION - a DiceExpression entity is not documentation:
//      handed to the DiceService it rolls, multiplier included,
//      and the roll is a citable journaled fact.
//
// Usage:
//   ./build/test_rulebook_pack

#undef NDEBUG

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/ontology_validator.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology.h"
#include "generated/rulebook_ontology_registry.h"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
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

// ----------------------------------------------------------- layering

void test_core_only_world_has_no_rulebook() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    // Positive control first: a DISABLED module returns INVALID for
    // everything, which would make the three rejections below pass
    // vacuously. Wall proves the module is alive and choosing.
    CHECK(kg.createEntity("Wall") != kg::INVALID_ENTITY,
          "the core-only world is alive (control)");
    CHECK(kg.createEntity("TaskCheck") == kg::INVALID_ENTITY,
          "a core-only world has no TaskCheck - if this passes, the "
          "module leaked back into the core");
    CHECK(kg.createEntity("RollableTable") == kg::INVALID_ENTITY,
          "nor a RollableTable");
    CHECK(kg.createEntity("AdvanceSkill") == kg::INVALID_ENTITY,
          "nor any outcome kind");
}

// Compile coverage for the generated TYPE surface. Nothing else in
// the build includes a pack's ontology header, which is how a
// non-compiling one once shipped green: the topo-sort ordered structs
// by inheritance only, and TableEntry embedded std::optional<Outcome>
// before Outcome existed. Including the header here makes that class
// of generator bug a build failure.
void test_the_generated_type_surface_compiles() {
    static_assert(std::is_base_of<rulebook::ontology::Outcome,
                                  rulebook::ontology::EnsureSkillLevel>::value,
                  "EnsureSkillLevel is an Outcome");
    static_assert(std::is_base_of<rulebook::ontology::Outcome,
                                  rulebook::ontology::AdvanceSkill>::value,
                  "AdvanceSkill is an Outcome");
    static_assert(std::is_base_of<rulebook::ontology::GainMoney,
                                  rulebook::ontology::GainFixedMoney>::value,
                  "fixed money is a concrete GainMoney outcome");
    static_assert(std::is_base_of<rulebook::ontology::GainMoney,
                                  rulebook::ontology::GainRolledMoney>::value,
                  "rolled money is a concrete GainMoney outcome");
    static_assert(std::is_base_of<rulebook::ontology::Cited,
                                  rulebook::ontology::Outcome>::value,
                  "every Outcome is Cited");
    static_assert(std::is_base_of<rulebook::ontology::Outcome,
                                  rulebook::ontology::NoEffect>::value,
                  "NoEffect is an Outcome");
    static_assert(std::is_base_of<rulebook::ontology::Outcome,
                                  rulebook::ontology::OutcomeSequence>::value,
                  "OutcomeSequence is an Outcome");
    static_assert(std::is_base_of<rulebook::ontology::Outcome,
                                  rulebook::ontology::OutcomeChoice>::value,
                  "OutcomeChoice is an Outcome");
    rulebook::ontology::TableEntry row;
    // roll_max is optional now: a row may be open at the top, which
    // the aging table's "1+" band needs, so an unset one must come up
    // ABSENT rather than as a zero that reads like a real ceiling.
    CHECK(row.roll_min == 0 && !row.roll_max.has_value() &&
              !row.source_quote.has_value(),
          "required generated fields are deterministically initialized, "
          "and optional ones are absent rather than zero");
}

void test_the_pack_grants_the_vocabulary() {
    kg::KGModule kg(rulebook::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    const char* instantiable[] = {
        "DiceExpression", "TaskCheck", "RollableTable", "TableEntry",
        "LookupTable", "EnsureSkillLevel", "AdvanceSkill",
        "ModifyAttribute", "GainFixedMoney", "GainRolledMoney",
        "GrantTableRoll", "NoEffect", "OutcomeSequence", "OutcomeStep",
        "OutcomeChoice", "OutcomeOption", "CurrencyBalance",
        "RuleConstant", "Procedure", "ProcedureStep", "StepRoute",
        "JudgmentPoint"};
    bool all = true;
    for (const char* t : instantiable)
        if (kg.createEntity(t) == kg::INVALID_ENTITY) {
            all = false;
            std::cout << "  [measure] missing type: " << t << std::endl;
        }
    CHECK(all, "all twenty-two concrete engine classes create");

    CHECK(kg.createEntity("Outcome") == kg::INVALID_ENTITY,
          "abstract Outcome is rejected at creation - a consequence "
          "must say which kind it is");
    CHECK(kg.createEntity("LookupEntry") == kg::INVALID_ENTITY,
          "abstract LookupEntry is rejected - a game must declare the "
          "typed result carried by the row");
    CHECK(kg.createEntity("GainMoney") == kg::INVALID_ENTITY,
          "abstract GainMoney is rejected - fixed and rolled amounts "
          "cannot share one sparse shape");
    CHECK(!kg.getRegistry().hasEntityType("GrantSkill"),
          "the ambiguous GrantSkill type has no compatibility fallback");
    CHECK(kg.createEntity("Cited") == kg::INVALID_ENTITY,
          "the Cited mixin is a trait, not a thing");
    CHECK(kg.createEntity("Wall") != kg::INVALID_ENTITY,
          "and the core underneath is intact");
}

void test_table_results_have_one_complete_shape() {
    const auto& reg = rulebook::ontology::registry();

    auto required = [&](const std::string& type, const std::string& property,
                        kg::PropertyValueKind value_kind) {
        const auto* def = reg.findProperty(type, property);
        return def && def->required && def->value_kind == value_kind;
    };

    const auto* table_dice = reg.findProperty("RollableTable", "dice");
    CHECK(table_dice && table_dice->required &&
              table_dice->value_kind == kg::PropertyValueKind::EntityRef &&
              table_dice->ref_target == "DiceExpression",
          "every RollableTable requires the DiceExpression that selects it");

    const auto* entry_type = reg.findProperty("LookupTable", "entry_type");
    CHECK(entry_type && entry_type->required &&
              entry_type->value_kind == kg::PropertyValueKind::String,
          "every LookupTable requires the concrete LookupEntry subtype it "
          "contains");
    CHECK(!reg.hasProperty("LookupEntry", "outcome"),
          "a lookup row is the typed answer, not a sparse outcome wrapper");
    const auto* key_min = reg.findProperty("LookupEntry", "key_min");
    const auto* key_max = reg.findProperty("LookupEntry", "key_max");
    const auto* min_unbounded =
        reg.findProperty("LookupEntry", "key_min_unbounded");
    const auto* max_unbounded =
        reg.findProperty("LookupEntry", "key_max_unbounded");
    CHECK(key_min && !key_min->required &&
              key_min->value_kind == kg::PropertyValueKind::Integer &&
              key_max && !key_max->required &&
              key_max->value_kind == kg::PropertyValueKind::Integer &&
              min_unbounded && !min_unbounded->required &&
              min_unbounded->value_kind == kg::PropertyValueKind::Boolean &&
              max_unbounded && !max_unbounded->required &&
              max_unbounded->value_kind == kg::PropertyValueKind::Boolean,
          "lookup bounds are optional only through explicit unbounded flags");

    const auto* row_outcome = reg.findProperty("TableEntry", "outcome");
    CHECK(row_outcome && row_outcome->required &&
              row_outcome->value_kind == kg::PropertyValueKind::EntityRef &&
              row_outcome->ref_target == "Outcome",
          "every rollable row requires one typed root Outcome");

    const auto* step_index = reg.findProperty("OutcomeStep", "step_index");
    const auto* step_outcome = reg.findProperty("OutcomeStep", "outcome");
    CHECK(step_index && step_index->required &&
              step_index->value_kind == kg::PropertyValueKind::Integer,
          "every OutcomeStep requires its explicit order");
    CHECK(step_outcome && step_outcome->required &&
              step_outcome->value_kind == kg::PropertyValueKind::EntityRef &&
              step_outcome->ref_target == "Outcome",
          "every OutcomeStep requires its typed child Outcome");

    CHECK(required("EnsureSkillLevel", "skill",
                   kg::PropertyValueKind::EntityRef) &&
              required("EnsureSkillLevel", "skill_level",
                       kg::PropertyValueKind::Integer),
          "EnsureSkillLevel requires its skill and minimum level");
    CHECK(required("AdvanceSkill", "skill",
                   kg::PropertyValueKind::EntityRef) &&
              required("AdvanceSkill", "initial_skill_level",
                       kg::PropertyValueKind::Integer) &&
              required("AdvanceSkill", "existing_skill_delta",
                       kg::PropertyValueKind::Integer),
          "AdvanceSkill carries both branches of its state-dependent rule");
    CHECK(required("SkillRating", "skill",
                   kg::PropertyValueKind::EntityRef) &&
              required("SkillRating", "skill_level",
                       kg::PropertyValueKind::Integer),
          "runtime SkillRating state is complete at creation");

    CHECK(reg.isAbstract("GainMoney") &&
              required("GainMoney", "currency",
                       kg::PropertyValueKind::EntityRef),
          "GainMoney is an abstract currency-denominated outcome");
    CHECK(required("GainFixedMoney", "amount",
                   kg::PropertyValueKind::Integer) &&
              !reg.hasProperty("GainFixedMoney", "amount_dice"),
          "fixed money requires only its fixed amount");
    CHECK(required("GainRolledMoney", "amount_dice",
                   kg::PropertyValueKind::EntityRef) &&
              !reg.hasProperty("GainRolledMoney", "amount"),
          "rolled money requires only its DiceExpression");
    CHECK(required("CurrencyBalance", "currency",
                   kg::PropertyValueKind::EntityRef) &&
              required("CurrencyBalance", "balance_amount",
                       kg::PropertyValueKind::Integer),
          "runtime money state is a typed balance per currency");

    CHECK(required("ModifyAttribute", "attribute_ref",
                   kg::PropertyValueKind::String) &&
              required("ModifyAttribute", "attribute_delta",
                       kg::PropertyValueKind::Integer),
          "ModifyAttribute cannot reach execution without both operands");
    // roll_count is deliberately NOT required. Cepheus writes "Roll on
    // the Injury table" with no number at all, so the seed stores no
    // number; a 1 there would be a figure the source never printed.
    // Absent means one, and the executor is what honours that. What a
    // table-roll cannot do without is the table.
    const auto* roll_count = reg.findProperty("GrantTableRoll",
                                              "roll_count");
    CHECK(required("GrantTableRoll", "table",
                   kg::PropertyValueKind::EntityRef) &&
              roll_count && !roll_count->required &&
              roll_count->value_kind == kg::PropertyValueKind::Integer,
          "a table-roll names its table; its count is optional, for the "
          "bare instructions the book prints with no number");

    CHECK(required("OutcomeChoice", "choice_authority",
                   kg::PropertyValueKind::String),
          "every choice names who must resolve it");
    CHECK(required("OutcomeOption", "option_index",
                   kg::PropertyValueKind::Integer) &&
              required("OutcomeOption", "option_label",
                       kg::PropertyValueKind::String) &&
              required("OutcomeOption", "outcome",
                       kg::PropertyValueKind::EntityRef),
          "every choice option is ordered, labeled, and typed");

    CHECK(required("ProcedureStep", "step_index",
                   kg::PropertyValueKind::Integer) &&
              required("ProcedureStep", "primitive_ref",
                       kg::PropertyValueKind::String),
          "every procedure step is ordered and names one primitive");
    CHECK(required("StepRoute", "route_label",
                   kg::PropertyValueKind::String) &&
              required("StepRoute", "next_step",
                       kg::PropertyValueKind::EntityRef),
          "every procedure route names its label and destination");
}

void test_task_checks_declare_the_complete_mechanic() {
    const auto reg = rulebook::ontology::registry();
    const auto required = [&](const std::string& property,
                              kg::PropertyValueKind kind) {
        const auto* def = reg.findProperty("TaskCheck", property);
        return def && def->required && def->value_kind == kind;
    };
    // attribute_ref is deliberately NOT required. Cepheus prints
    // re-enlistment as a bare "6+", a throw no characteristic
    // modifies, and a check without a modifier source is still a
    // check. What a throw cannot do without is dice and a target.
    const auto* attribute = reg.findProperty("TaskCheck", "attribute_ref");
    CHECK(attribute && !attribute->required &&
              attribute->value_kind == kg::PropertyValueKind::String,
          "TaskCheck's attribute is optional, for throws the book "
          "prints with no characteristic at all");
    // The modifier lookup travels with the attribute: a throw with no
    // characteristic has nothing to look up. Both optional, together.
    const auto* lookup = reg.findProperty("TaskCheck", "modifier_table");
    const auto* column = reg.findProperty("TaskCheck", "modifier_property");
    CHECK(lookup && !lookup->required && column && !column->required,
          "TaskCheck's modifier lookup is optional, like the attribute "
          "it exists to modify");
    CHECK(required("target_number", kg::PropertyValueKind::Integer) &&
              required("dice", kg::PropertyValueKind::EntityRef),
          "TaskCheck requires its dice "
          "result column, and threshold");
    const auto* table = reg.findProperty("TaskCheck", "modifier_table");
    CHECK(table && table->ref_target == "LookupTable",
          "TaskCheck.modifier_table is a typed LookupTable reference");
}

void test_rule_contexts_are_explicit_kg_entities() {
    const auto& reg = rulebook::ontology::registry();

    CHECK(reg.hasEntityType("KnowledgeContext") &&
              reg.isAbstract("KnowledgeContext"),
          "KnowledgeContext is an abstract ontology concept");
    CHECK(reg.isSubtypeOf("SourceLayerContext", "KnowledgeContext") &&
              reg.isSubtypeOf("SourceDocumentContext", "KnowledgeContext") &&
              reg.isSubtypeOf("RuntimeContext", "KnowledgeContext"),
          "source layers, source documents, and runtime growth scopes are "
          "explicit context entities");

    const auto* context_key =
        reg.findProperty("KnowledgeContext", "context_key");
    const auto* source_layer =
        reg.findProperty("SourceLayerContext", "source_layer");
    const auto* source_commit =
        reg.findProperty("SourceDocumentContext", "source_commit");
    const auto* source_layer_context =
        reg.findProperty("SourceDocumentContext", "source_layer_context");
    const auto* runtime_kind =
        reg.findProperty("RuntimeContext", "context_kind");
    CHECK(context_key && context_key->required &&
              source_layer && source_layer->required &&
              source_commit && source_commit->required &&
              source_layer_context && source_layer_context->required &&
              source_layer_context->value_kind ==
                  kg::PropertyValueKind::EntityRef &&
              source_layer_context->ref_target == "SourceLayerContext" &&
              runtime_kind && runtime_kind->required,
          "contexts carry complete identity, source lineage, and runtime "
          "scope kind data");

    const auto* origin = reg.findProperty("TaskCheck", "origin_context");
    const auto* fork = reg.findProperty("TaskCheck", "fork_of");
    CHECK(origin && origin->required &&
              origin->value_kind == kg::PropertyValueKind::EntityRef &&
              origin->ref_target == "KnowledgeContext",
          "every cited rule requires an explicit origin context");
    CHECK(fork && !fork->required &&
              fork->value_kind == kg::PropertyValueKind::EntityRef &&
              fork->ref_target == "Entity",
          "a rule fork records its source as typed KG lineage");
}

void test_source_leaf_identity_is_a_typed_schema_tuple() {
    const auto& reg = rulebook::ontology::registry();

    CHECK(reg.isSubtypeOf("SourceRepresentationContext", "KnowledgeContext"),
          "an exact source representation is a reusable knowledge context");
    CHECK(reg.hasEntityType("SourceSelector") &&
              reg.isAbstract("SourceSelector"),
          "SourceSelector is an abstract schema concept, not a kind string");
    CHECK(reg.isSubtypeOf("ByteRangeSelector", "SourceSelector") &&
              reg.isSubtypeOf("TextQuoteSelector", "SourceSelector"),
          "byte ranges and text quotes are concrete typed selectors");
    CHECK(reg.isSubtypeOf("SourceTarget", "Addressable"),
          "a source target has portable structured identity");

    const auto* media =
        reg.findProperty("SourceRepresentationContext", "source_media_type");
    const auto* algorithm = reg.findProperty("SourceRepresentationContext",
                                             "source_digest_algorithm");
    const auto* digest =
        reg.findProperty("SourceRepresentationContext", "source_digest");
    const auto* length = reg.findProperty("SourceRepresentationContext",
                                          "source_byte_length");
    CHECK(media && media->required &&
              media->value_kind == kg::PropertyValueKind::Enum &&
              media->enum_type == "SourceMediaType" &&
              algorithm && algorithm->required &&
              algorithm->value_kind == kg::PropertyValueKind::Enum &&
              algorithm->enum_type == "SourceDigestAlgorithm" &&
              digest && digest->required &&
              digest->value_kind == kg::PropertyValueKind::String &&
              length && length->required &&
              length->value_kind == kg::PropertyValueKind::Integer,
          "a representation requires typed media, digest, and byte length");

    const auto* start =
        reg.findProperty("ByteRangeSelector", "source_byte_start");
    const auto* end =
        reg.findProperty("ByteRangeSelector", "source_byte_end");
    CHECK(start && start->required && start->has_min &&
              start->min_value == 0 &&
              start->value_kind == kg::PropertyValueKind::Integer &&
              end && end->required && end->has_min && end->min_value == 0 &&
              end->value_kind == kg::PropertyValueKind::Integer,
          "a byte selector requires non-negative inclusive-start and "
          "exclusive-end positions");

    const auto* representation =
        reg.findProperty("SourceTarget", "target_representation");
    const auto* primary =
        reg.findProperty("SourceTarget", "target_primary_selector");
    const auto* quote =
        reg.findProperty("SourceTarget", "target_quote_selector");
    CHECK(representation && representation->required &&
              representation->value_kind ==
                  kg::PropertyValueKind::EntityRef &&
              representation->ref_target == "SourceRepresentationContext" &&
              primary && primary->required &&
              primary->value_kind == kg::PropertyValueKind::EntityRef &&
              primary->ref_target == "SourceSelector" &&
              quote && !quote->required &&
              quote->value_kind == kg::PropertyValueKind::EntityRef &&
              quote->ref_target == "TextQuoteSelector",
          "a target requires representation plus typed primary selector, "
          "with an optional typed quote selector");
}

void test_rule_content_has_portable_addressable_identity() {
    const auto& reg = rulebook::ontology::registry();
    CHECK(reg.hasEntityType("Addressable") && reg.isAbstract("Addressable"),
          "Addressable is reusable abstract ontology vocabulary");
    CHECK(reg.isSubtypeOf("TaskCheck", "Addressable"),
          "cited rule content carries portable identity");

    const auto* context =
        reg.findProperty("Addressable", "identity_context");
    const auto* key = reg.findProperty("Addressable", "entity_key");
    CHECK(context && context->required && context->create_only &&
              context->value_kind == kg::PropertyValueKind::EntityRef &&
              context->ref_target == "KnowledgeContext",
          "portable identity names one immutable KnowledgeContext");
    CHECK(key && key->required && key->create_only &&
              key->value_kind == kg::PropertyValueKind::String,
          "portable identity has one immutable machine key");
}

void test_rule_origin_and_published_content_are_immutable() {
    kg::KGModule kg(rulebook::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);

    const auto source_layer = kg.createEntity("SourceLayerContext");
    kg.setProperty(source_layer, "context_key", "source-layer:cepheus");
    kg.setProperty(source_layer, "source_layer", "cepheus");
    const auto source_document = kg.createEntity("SourceDocumentContext");
    kg.setProperty(source_document, "context_key", "source-document:test");
    kg.setProperty(source_document, "source_layer", "cepheus");
    kg.setProperty(source_document, "source_file", "book.md");
    kg.setProperty(source_document, "source_commit", "abc123");
    kg.setProperty(source_document, "source_layer_context",
                   std::to_string(source_layer));

    const auto runtime = kg.createEntity("RuntimeContext");
    kg.setProperty(runtime, "context_key", "session:one");
    kg.setProperty(runtime, "context_kind", "session");

    const auto published = kg.createEntity("RuleConstant");
    kg.setProperty(published, "origin_context",
                   std::to_string(source_document));
    kg.setProperty(published, "constant_value", "18");
    const auto improvised = kg.createEntity("RuleConstant");
    kg.setProperty(improvised, "origin_context", std::to_string(runtime));
    kg.setProperty(improvised, "constant_value", "1");

    const auto rewrite_origin = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{{improvised, ""}, "origin_context",
                                     std::to_string(source_document)}},
        kg, kg.getRegistry());
    CHECK(!rewrite_origin.ok &&
              rewrite_origin.reason.find("creation-only") !=
                  std::string::npos,
          "origin_context cannot be rewritten after entity creation");

    const auto rewrite_published = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{{published, ""}, "constant_value",
                                     "21"}},
        kg, kg.getRegistry());
    CHECK(!rewrite_published.ok &&
              rewrite_published.reason.find("sealed origin") !=
                  std::string::npos,
          "content from a published source context rejects mutation");

    const auto rewrite_runtime = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{{improvised, ""}, "constant_value",
                                     "2"}},
        kg, kg.getRegistry());
    CHECK(rewrite_runtime.ok,
          "runtime-context content remains mutable through validated ops");

    const auto relate_published = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetRelation{{published, ""}, "SPECIALIZES",
                                     {improvised, ""}}},
        kg, kg.getRegistry());
    CHECK(!relate_published.ok &&
              relate_published.reason.find("sealed origin") !=
                  std::string::npos,
          "published rule composition is immutable with its properties");

    const auto forge_source_context = kg::validate_kg_op(
        kg::KGOp{kg::KGOpCreateEntity{
            "SourceLayerContext",
            {{"context_key", "source-layer:forged"},
             {"source_layer", "forged"}},
            ""}},
        kg, kg.getRegistry());
    CHECK(!forge_source_context.ok &&
              forge_source_context.reason.find("trusted seed ingestion") !=
                  std::string::npos,
          "runtime operations cannot manufacture source contexts");

    const auto claim_source_origin = kg::validate_kg_op(
        kg::KGOp{kg::KGOpCreateEntity{
            "RuleConstant",
            {{"origin_context", std::to_string(source_document)},
             {"identity_context", std::to_string(runtime)},
             {"entity_key", "forged-source-rule"},
             {"constant_value", "21"}},
            ""}},
        kg, kg.getRegistry());
    CHECK(!claim_source_origin.ok &&
              claim_source_origin.reason.find("cannot claim") !=
                  std::string::npos,
          "runtime content cannot claim provenance from a sealed source");
}

// ------------------------------------------------- chapter 1, as data
//
// The world below is shared by the citation and dice tests: the point
// is that ONE set of entities is simultaneously well-formed KG data,
// verbatim-cited text, and executable dice.

kg::KGModule* world = nullptr;

struct CitedRecord {
    kg::EntityID id;
    std::string label;
};
std::vector<CitedRecord> cited;

const char* kSrdRoot = "book1/character-creation.md";

kg::EntityID cite(kg::KGModule& kg, kg::EntityID id, const std::string& label,
                  const std::string& section, const std::string& quote) {
    kg.setProperty(id, "source_file", kSrdRoot);
    kg.setProperty(id, "source_section", section);
    kg.setProperty(id, "source_quote", quote);
    cited.push_back({id, label});
    return id;
}

void test_chapter_one_instantiates() {
    static kg::KGModule kg(rulebook::ontology::registry());
    kg::OntologyRegistry row_types("schema://rulebook-contract-rows");
    row_types.addEntityType("CharacteristicModifierEntry", "LookupEntry",
                            false);
    row_types.addAncestors(
        "CharacteristicModifierEntry",
        {"LookupEntry", "Cited", "Entity", "Describable", "Identifiable",
         "Temporal"});
    row_types.addProperty("CharacteristicModifierEntry",
                          "characteristic_modifier",
                          kg::PropertyValueKind::Integer, true);
    row_types.addEntityType("NobilityEntry", "LookupEntry", false);
    row_types.addAncestors(
        "NobilityEntry",
        {"LookupEntry", "Cited", "Entity", "Describable", "Identifiable",
         "Temporal"});
    row_types.addProperty("NobilityEntry", "title",
                          kg::PropertyValueKind::String, true);
    kg.extendOntology(row_types);
    kg.setMode(kg::KGMode::MINIMAL);
    world = &kg;

    // DiceExpression: the chargen 2D6 and the medical bill. The x in
    // the source is U+00D7, split-escaped so no compiler guesses at
    // the encoding.
    auto d2d6 = cite(kg, kg.createEntity("DiceExpression"), "2D6",
                     "Generating Characteristic Scores",
                     "Roll your six characteristics using 2D6, and record "
                     "them in the standard order");
    kg.setProperty(d2d6, "dice_count", "2");
    kg.setProperty(d2d6, "dice_sides", "6");

    auto dmed = cite(kg, kg.createEntity("DiceExpression"), "1D6x10000",
                     "Injury Crisis",
                     "he can pay 1D6\xC3\x97" "10,000 Credits for medical care");
    kg.setProperty(dmed, "dice_count", "1");
    kg.setProperty(dmed, "dice_sides", "6");
    kg.setProperty(dmed, "dice_multiplier", "10000");

    // TaskCheck: the book's own definition of a throw, and one career
    // survival throw. attribute_ref values resolve against the cepheus
    // Character's declared slots (the step-4 verifier's job; the data
    // is written resolvable from day one).
    auto int8 = cite(kg, kg.createEntity("TaskCheck"), "Int 8+",
                     "Careers",
                     "A throw of Int 8+ means 'roll 2D6, add your "
                     "Intelligence DM, and you succeed if you roll an 8 or "
                     "more'.");
    kg.setProperty(int8, "attribute_ref", "intelligence");
    kg.setProperty(int8, "target_number", "8");
    kg.setProperty(int8, "dice", std::to_string(d2d6));

    auto dex5 = cite(kg, kg.createEntity("TaskCheck"), "Athlete survival",
                     "Career Tables",
                     "| Survival | Dex 5+ | Dex 5+ | Int 6+ | Str 6+ | "
                     "Dex 7+ | Edu 4+ |");
    kg.setProperty(dex5, "attribute_ref", "dexterity");
    kg.setProperty(dex5, "target_number", "5");
    kg.setProperty(dex5, "dice", std::to_string(d2d6));

    // Outcomes first, so the rows below can point at them.
    auto injury_out = cite(kg, kg.createEntity("ModifyAttribute"),
                           "injury -2 Str",
                           "Injuries",
                           "| 3 | Missing eye or limb. Reduce Strength or "
                           "Dexterity by 2. |");
    kg.setProperty(injury_out, "attribute_ref", "strength");
    kg.setProperty(injury_out, "attribute_delta", "-2");

    // The currency, like the skill below, is the game's vocabulary
    // (credits are one currency of one game); a placeholder entity
    // stands in for it until the cepheus monetary pack exists.
    auto currency_stub = kg.createEntity("Entity");
    CHECK(currency_stub != kg::INVALID_ENTITY, "the currency stand-in exists");

    auto debt_out = cite(kg, kg.createEntity("GainFixedMoney"), "Cr10000 debt",
                         "Survival",
                         "| 3 | Honorably discharged from the service after "
                         "a long legal battle. Legal issues create a debt "
                         "of Cr10,000. |");
    kg.setProperty(debt_out, "amount", "-10000");
    kg.setProperty(debt_out, "currency", std::to_string(currency_stub));

    auto med_out = cite(kg, kg.createEntity("GainRolledMoney"), "medical bill",
                        "Injury Crisis",
                        "he can pay 1D6\xC3\x97" "10,000 Credits for medical care");
    kg.setProperty(med_out, "currency", std::to_string(currency_stub));
    kg.setProperty(med_out, "amount_dice", std::to_string(dmed));

    auto basic_out = cite(kg, kg.createEntity("EnsureSkillLevel"),
                          "basic training",
                          "Basic Training",
                          "For your first career only, you get all the "
                          "skills listed in the Service Skills table at "
                          "Level 0 as your basic training.");
    // The skill vocabulary is the game's (step 3); the reference shape
    // is the engine's. A placeholder entity stands in for it here.
    auto skill_stub = kg.createEntity("Entity");
    CHECK(skill_stub != kg::INVALID_ENTITY, "the skill stand-in exists");
    kg.setProperty(basic_out, "skill", std::to_string(skill_stub));
    kg.setProperty(basic_out, "skill_level", "0");

    // RollableTable + rows: mishaps (1D6) and aging (2D6).
    auto mishaps = cite(kg, kg.createEntity("RollableTable"),
                        "Survival Mishaps",
                        "Survival", "| 1D6 | Mishaps |");
    auto d1d6 = kg.createEntity("DiceExpression");
    CHECK(d1d6 != kg::INVALID_ENTITY, "the mishap die exists");
    kg.setProperty(d1d6, "dice_count", "1");
    kg.setProperty(d1d6, "dice_sides", "6");
    kg.setProperty(mishaps, "dice", std::to_string(d1d6));

    auto aging = cite(kg, kg.createEntity("RollableTable"), "Aging Table",
                      "Aging", "| 2D6 | Effects of Aging |");
    kg.setProperty(aging, "dice", std::to_string(d2d6));

    auto mishap_row = cite(kg, kg.createEntity("TableEntry"), "mishap row 3",
                           "Survival",
                           "| 3 | Honorably discharged from the service "
                           "after a long legal battle. Legal issues create "
                           "a debt of Cr10,000. |");
    kg.setProperty(mishap_row, "roll_min", "3");
    kg.setProperty(mishap_row, "roll_max", "3");
    kg.setProperty(mishap_row, "outcome", std::to_string(debt_out));
    CHECK(kg.createRelation(mishaps, "HAS_PART", mishap_row) !=
              kg::INVALID_RELATION,
          "a table owns its rows through HAS_PART");

    auto injury_row = cite(kg, kg.createEntity("TableEntry"), "injury row 3",
                           "Injuries",
                           "| 3 | Missing eye or limb. Reduce Strength or "
                           "Dexterity by 2. |");
    kg.setProperty(injury_row, "roll_min", "3");
    kg.setProperty(injury_row, "roll_max", "3");
    kg.setProperty(injury_row, "outcome", std::to_string(injury_out));

    // LookupTable + rows: keyed by what the character already is. The
    // concrete game row carries the characteristic modifier as typed data;
    // the engine only validates and selects that row.
    auto dm_table = cite(kg, kg.createEntity("LookupTable"),
                         "char modifier table",
                         "Characteristic Modifiers",
                         "| Score Range | PseudoHex | Characteristic "
                         "Modifier |");
    kg.setProperty(dm_table, "entry_type", "CharacteristicModifierEntry");
    auto dm_row = cite(kg, kg.createEntity("CharacteristicModifierEntry"),
                       "DM row 0-2", "Characteristic Modifiers",
                       "| 0 through 2 | 0-2 | \\-2 |");
    kg.setProperty(dm_row, "key_min", "0");
    kg.setProperty(dm_row, "key_max", "2");
    kg.setProperty(dm_row, "characteristic_modifier", "-2");
    CHECK(kg.createRelation(dm_table, "HAS_PART", dm_row) !=
              kg::INVALID_RELATION,
          "a lookup table owns its rows the same way");

    auto nobility = cite(kg, kg.createEntity("LookupTable"),
                         "Titles of Nobility",
                         "Social Standing and Noble Titles",
                         "| Social Standing | Title of Nobility |");
    kg.setProperty(nobility, "attribute_ref", "social_standing");
    kg.setProperty(nobility, "entry_type", "NobilityEntry");
    auto lord_row = cite(kg, kg.createEntity("NobilityEntry"),
                         "Soc 10 Lord", "Social Standing and Noble Titles",
                         "| 10 (A) | Lord (Lady) |");
    kg.setProperty(lord_row, "key_min", "10");
    kg.setProperty(lord_row, "key_max", "10");
    kg.setProperty(lord_row, "title", "Lord (Lady)");
    kg.createRelation(nobility, "HAS_PART", lord_row);

    // GrantTableRoll: commission's reward, aimed at a career table.
    auto extra_roll = cite(kg, kg.createEntity("GrantTableRoll"),
                           "commission extra roll",
                           "Commission and Advancement",
                           "you gain an extra roll on any of the Skills "
                           "and Training Tables for this career");
    kg.setProperty(extra_roll, "table", std::to_string(mishaps));
    kg.setProperty(extra_roll, "roll_count", "1");

    // Procedure + steps + routes: the checklist as a state machine,
    // gotos transcribed as routing data, logic left to primitives.
    auto checklist = cite(kg, kg.createEntity("Procedure"),
                          "chargen checklist",
                          "Character Creation Checklist",
                          "## Character Creation Checklist");
    auto chargen = cite(kg, kg.createEntity("Procedure"),
                        "characteristic generation",
                        "Generating Characteristic Scores",
                        "Generating characteristics scores is fairly "
                        "straightforward. Roll your six characteristics "
                        "using 2D6");

    auto survival_step = cite(kg, kg.createEntity("ProcedureStep"),
                              "survival step",
                              "Character Creation Checklist",
                              "Roll for survival, as indicated in the "
                              "description of the career.");
    kg.setProperty(survival_step, "step_index", "5");
    kg.setProperty(survival_step, "primitive_ref", "roll_survival");
    CHECK(kg.createRelation(checklist, "HAS_PART", survival_step) !=
              kg::INVALID_RELATION,
          "a procedure owns its steps through HAS_PART");

    auto aging_step = cite(kg, kg.createEntity("ProcedureStep"),
                           "aging step",
                           "Character Creation Checklist",
                           "Increase your age by 4 years.");
    kg.setProperty(aging_step, "step_index", "8");
    kg.setProperty(aging_step, "primitive_ref", "advance_age");
    kg.createRelation(checklist, "HAS_PART", aging_step);

    auto mishap_step = cite(kg, kg.createEntity("ProcedureStep"),
                            "mishap step",
                            "Survival",
                            "With the Referee's approval, you can keep the "
                            "character that fails a survival roll and roll "
                            "on the Survival Mishaps table instead.");
    kg.setProperty(mishap_step, "primitive_ref", "roll_on_table");

    auto fail_route = cite(kg, kg.createEntity("StepRoute"),
                           "survival-fail route",
                           "Survival",
                           "With the Referee's approval, you can keep the "
                           "character that fails a survival roll and roll "
                           "on the Survival Mishaps table instead.");
    kg.setProperty(fail_route, "route_label", "failed");
    kg.setProperty(fail_route, "next_step", std::to_string(mishap_step));
    CHECK(kg.createRelation(survival_step, "HAS_PART", fail_route) !=
              kg::INVALID_RELATION,
          "a step owns its routes through HAS_PART");

    auto natural12 = cite(kg, kg.createEntity("StepRoute"),
                          "natural-12 route",
                          "Reenlistment and Retirement",
                          "If you roll a natural 12, you cannot leave this "
                          "career and must continue for another term");
    kg.setProperty(natural12, "route_label", "natural_12");
    kg.setProperty(natural12, "next_step", std::to_string(survival_step));

    // RuleConstant: numbers the book fixes. The en dash in the DM
    // quote is U+2013, split-escaped like the multiplication sign.
    auto age18 = cite(kg, kg.createEntity("RuleConstant"), "age of majority",
                      "Chapter 1: Character Creation",
                      "All characters begin at the age of majority, "
                      "typically 18.");
    kg.setProperty(age18, "constant_value", "18");

    auto prior_dm = cite(kg, kg.createEntity("RuleConstant"),
                         "prior-career DM",
                         "Qualifying and the Draft",
                         "You suffer a DM\xE2\x80\x93"
                         "2 to qualification rolls for each previous career "
                         "you have entered.");
    kg.setProperty(prior_dm, "constant_value", "-2");

    // JudgmentPoint: where the book hands the wheel to the Referee.
    cite(kg, kg.createEntity("JudgmentPoint"), "term-cap judgment",
         "Reenlistment and Retirement",
         "The Referee may want to change the maximum number of terms "
         "spent in character creation from 7 to something else.");
    cite(kg, kg.createEntity("JudgmentPoint"), "survival judgment",
         "Survival",
         "With the Referee's approval, you can keep the character that "
         "fails a survival roll and roll on the Survival Mishaps table "
         "instead.");

    size_t landed = 0;
    for (const auto& rec : cited)
        if (rec.id != kg::INVALID_ENTITY) ++landed;
    std::cout << "  [measure] " << landed << "/" << cited.size()
              << " cited chapter-1 entities in the graph" << std::endl;
    CHECK(landed == 28 && cited.size() == 28,
          "twenty-eight cited instances landed, every id valid");

    // Spot-check that slot data round-trips through the validator.
    CHECK(kg.getProperty(dmed, "dice_multiplier") == "10000",
          "the multiplier survived the schema");
    CHECK(kg.getProperty(injury_row, "roll_min") == "3" &&
              kg.getProperty(injury_row, "roll_max") == "3",
          "a single-value row is a band of one");
    CHECK(kg.getProperty(prior_dm, "constant_value") == "-2",
          "a negative constant survives");

    // The schema still bites on the validated write path (the one the
    // book's ingestion and the referee use; direct setProperty is the
    // engine-internal fast path and does not schema-check).
    const auto reg = rulebook::ontology::registry();
    kg::EntityRef ref;
    ref.id = d2d6;
    auto bad = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{ref, "dice_count", "500"}}, kg, reg);
    CHECK(!bad.ok, "dice_count 500 is rejected by the schema max");
    auto good = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{ref, "dice_count", "2"}}, kg, reg);
    CHECK(good.ok, "and an in-range value passes the same gate");

    // Entity references are typed on the same path: a class-ranged
    // slot takes only an existing entity of that class (or subtype).
    kg::EntityRef check_ref;
    check_ref.id = int8;
    auto ref_ok = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{check_ref, "dice",
                                     std::to_string(d2d6)}}, kg, reg);
    CHECK(ref_ok.ok, "TaskCheck.dice takes a DiceExpression id");
    auto ref_junk = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{check_ref, "dice", "banana"}}, kg, reg);
    CHECK(!ref_junk.ok, "TaskCheck.dice refuses a non-id value");
    auto ref_wrong = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{check_ref, "dice",
                                     std::to_string(dex5)}}, kg, reg);
    CHECK(!ref_wrong.ok,
          "TaskCheck.dice refuses a TaskCheck id (wrong class)");
    kg::EntityRef row_ref;
    row_ref.id = injury_row;
    auto ref_subtype = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{row_ref, "outcome",
                                     std::to_string(injury_out)}}, kg, reg);
    CHECK(ref_subtype.ok,
          "TableEntry.outcome takes a ModifyAttribute, a subtype of "
          "Outcome");
}

// ------------------------------------------------------------ citation

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void test_every_quote_is_verbatim() {
    // The miniature of the step-4 verifier: every source_quote written
    // above must be an exact substring of the file it cites. A quote
    // that "sounds right" fails here, exactly as a hallucinated
    // extraction will.
    std::map<std::string, std::string> files;
    bool all = true;
    for (const auto& rec : cited) {
        const std::string file = world->getProperty(rec.id, "source_file");
        const std::string quote = world->getProperty(rec.id, "source_quote");
        if (file.empty() || quote.empty()) {
            all = false;
            std::cout << "  [measure] uncited: " << rec.label << std::endl;
            continue;
        }
        if (!files.count(file))
            files[file] = slurp(std::string(LOGOSPHERE_SOURCE_DIR) +
                                "/examples/logovger/srd/cepheus/" + file);
        if (files[file].find(quote) == std::string::npos) {
            all = false;
            std::cout << "  [measure] NOT VERBATIM (" << rec.label
                      << "): " << quote.substr(0, 60) << "..." << std::endl;
        }
    }
    CHECK(!files.empty() && !files.begin()->second.empty(),
          "the vendored SRD file is readable from the test");
    CHECK(all, "all cited quotes string-match into the vendored SRD");
}

// ---------------------------------------------------------- connection

void test_a_dice_entity_rolls_through_the_service() {
    // Find the medical-bill expression by its slots and roll it: the
    // KG data and the dice engine meet, multiplier included.
    logosphere::dice::DiceExpression expr;
    bool built = false;
    for (const auto& rec : cited) {
        if (rec.label != "1D6x10000") continue;
        expr.count = std::stoi(world->getProperty(rec.id, "dice_count"));
        expr.sides = std::stoi(world->getProperty(rec.id, "dice_sides"));
        expr.multiplier =
            std::stoi(world->getProperty(rec.id, "dice_multiplier"));
        built = true;
    }
    CHECK(built, "the medical-bill entity was found by its label");
    CHECK(expr.to_string() == "1D6x10000",
          "the entity's slots rebuild the canonical expression");

    logosphere::dice::DiceService svc;
    svc.seed_stream("chargen", 42);
    bool in_bounds = true;
    for (int i = 0; i < 50; ++i) {
        const auto r = svc.roll(expr, "chargen", "medical care");
        if (r.total < 10000 || r.total > 60000 || r.total % 10000 != 0)
            in_bounds = false;
    }
    CHECK(in_bounds, "50 rolls of the entity: 10000..60000 in whole steps");
    CHECK(svc.find(1) != nullptr && svc.find(1)->purpose == "medical care",
          "and the roll is a citable journaled fact");
}

// ----------------------------------------------------------- relations

void test_specializes_is_vocabulary() {
    kg::KGModule kg(rulebook::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    auto broad = kg.createEntity("Entity");
    auto narrow = kg.createEntity("Entity");
    CHECK(kg.createRelation(narrow, "SPECIALIZES", broad) !=
              kg::INVALID_RELATION,
          "SPECIALIZES relates a narrower thing to the one it refines");
    CHECK(kg.createRelation(narrow, "CASCADES_FROM", broad) ==
              kg::INVALID_RELATION,
          "an undeclared relation is still rejected loudly (expected "
          "[KG] REJECTED above)");
}

}  // namespace

int main() {
    std::cout << "Rulebook pack (what any book of rules is made of)"
              << std::endl;
    std::cout << "note: [KG] REJECTED lines below are EXPECTED - they "
                 "are the contract." << std::endl;
    test_core_only_world_has_no_rulebook();
    test_the_generated_type_surface_compiles();
    test_the_pack_grants_the_vocabulary();
    test_table_results_have_one_complete_shape();
    test_task_checks_declare_the_complete_mechanic();
    test_rule_contexts_are_explicit_kg_entities();
    test_source_leaf_identity_is_a_typed_schema_tuple();
    test_rule_content_has_portable_addressable_identity();
    test_rule_origin_and_published_content_are_immutable();
    test_chapter_one_instantiates();
    test_every_quote_is_verbatim();
    test_a_dice_entity_rolls_through_the_service();
    test_specializes_is_vocabulary();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
