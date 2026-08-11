#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_meta_graph.h"
#include "logosphere/kg/qualified_reference.h"
#include "logosphere/rules/rule_program_validator.h"
#include "logosphere/rules/rule_static_type.h"
#include "logosphere/events/event_bus.h"
#include "generated/rulebook_ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "... ";                                     \
    try { name(); ++passed; std::cout << "PASS\n"; }                     \
    catch (const std::exception& error) {                                 \
        ++failed; std::cout << "FAIL: " << error.what() << '\n';         \
    }

#define REQUIRE(condition, message)                                      \
    if (!(condition)) throw std::runtime_error(message)

kg::OntologyRegistry binding_registry() {
    kg::OntologyRegistry out = rulebook::ontology::registry();
    kg::OntologyRegistry test("schema://rule-bindings-test");
    test.addEntityType("TestIntegerExpression", "IntegerExpression", false);
    test.addAncestors("TestIntegerExpression",
                      {"IntegerExpression", "NumericExpression",
                       "ScalarExpression", "Expression", "Entity"});
    test.addEntityType("TestBooleanExpression", "BooleanExpression", false);
    test.addAncestors("TestBooleanExpression",
                      {"BooleanExpression", "ScalarExpression",
                       "Expression", "Entity"});
    out.extend(test);
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = binding_registry();
    kg::KGModule world{ontology};
    kg::EntityID context = kg::INVALID_ENTITY;
    unsigned key = 0;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        kg::OntologyMetaGraphReport meta;
        REQUIRE(kg::materialize_ontology_meta_graph(world, meta), meta.error);
        context = world.createEntity("RuntimeContext");
        world.setProperty(context, "context_key", "session:rule-bindings");
        world.setProperty(context, "context_kind", "session");
    }

    kg::EntityID create(const std::string& type) {
        return world.createEntity(type);
    }

    void make_addressable(kg::EntityID entity,
                          const std::string& prefix) {
        world.setProperty(entity, "identity_context",
                          std::to_string(context));
        world.setProperty(entity, "entity_key",
                          prefix + "-" + std::to_string(++key));
    }

    kg::EntityID descriptor(const std::string& type) {
        const auto entity = create(type);
        make_addressable(entity, "type");
        return entity;
    }

    kg::EntityID parameter(const std::string& name,
                           kg::EntityID type) {
        const auto entity = create("FunctionParameterSpec");
        make_addressable(entity, "parameter");
        world.setProperty(entity, "parameter_key", name);
        world.setProperty(entity, "parameter_type", std::to_string(type));
        return entity;
    }

    kg::EntityID signature(const std::string& name,
                           kg::EntityID result,
                           const std::vector<kg::EntityID>& parameters) {
        const auto entity = create("FunctionSignature");
        make_addressable(entity, name);
        world.setProperty(entity, "signature_result_type",
                          std::to_string(result));
        for (const auto parameter : parameters) {
            world.createRelation(entity, "FUNCTION_SIGNATURE_HAS_PARAMETER",
                                 parameter);
        }
        return entity;
    }

    kg::EntityID argument(kg::EntityID parameter,
                          kg::EntityID expression) {
        const auto entity = create("ArgumentBinding");
        world.setProperty(entity, "binding_parameter",
                          std::to_string(parameter));
        world.setProperty(entity, "binding_argument",
                          std::to_string(expression));
        return entity;
    }

    kg::EntityID local(const std::string& name,
                       kg::EntityID expression) {
        const auto entity = create("LocalBinding");
        world.setProperty(entity, "binding_key", name);
        world.setProperty(entity, "binding_value",
                          std::to_string(expression));
        return entity;
    }
};

kg::EntityID resolve(Fixture& fixture, const std::string& path) {
    const auto result = kg::resolve_qualified_reference(path, fixture.world);
    REQUIRE(result.ok, "resolve " + path + ": " + result.error);
    return result.entity;
}

void ontology_declares_closed_descriptor_and_binding_shapes() {
    const auto& ontology = rulebook::ontology::registry();
    const char* descriptors[] = {
        "BooleanTypeDescriptor", "NumericTypeDescriptor",
        "IntegerTypeDescriptor", "FloatTypeDescriptor",
        "StringTypeDescriptor", "EntityTypeDescriptor",
        "BooleanCollectionTypeDescriptor",
        "IntegerCollectionTypeDescriptor", "FloatCollectionTypeDescriptor",
        "StringCollectionTypeDescriptor", "EntityCollectionTypeDescriptor",
        "FunctionTypeDescriptor", "OutcomePlanTypeDescriptor",
    };
    REQUIRE(ontology.hasEntityType("ValueTypeDescriptor") &&
                ontology.isAbstract("ValueTypeDescriptor"),
            "ValueTypeDescriptor is an abstract closed root");
    for (const char* type : descriptors) {
        REQUIRE(ontology.hasEntityType(type) && !ontology.isAbstract(type) &&
                    ontology.isSubtypeOf(type, "ValueTypeDescriptor") &&
                    ontology.isSubtypeOf(type, "Addressable"),
                std::string(type) + " is a concrete addressable descriptor");
    }

    const auto* entity_class =
        ontology.findProperty("EntityTypeDescriptor", "descriptor_entity_class");
    const auto* element_class = ontology.findProperty(
        "EntityCollectionTypeDescriptor", "descriptor_element_class");
    const auto* function_signature = ontology.findProperty(
        "FunctionTypeDescriptor", "descriptor_function_signature");
    REQUIRE(entity_class && entity_class->required &&
                entity_class->ref_target == "OntologyClassMeta" &&
                element_class && element_class->required &&
                element_class->ref_target == "OntologyClassMeta" &&
                function_signature && function_signature->required &&
                function_signature->ref_target == "FunctionSignature",
            "refined descriptors require exact typed references");

    const auto* result = ontology.findProperty(
        "FunctionSignature", "signature_result_type");
    const auto* parameter_key = ontology.findProperty(
        "FunctionParameterSpec", "parameter_key");
    const auto* parameter_type = ontology.findProperty(
        "FunctionParameterSpec", "parameter_type");
    REQUIRE(result && result->required &&
                result->ref_target == "ValueTypeDescriptor" &&
                parameter_key && parameter_key->required &&
                parameter_type && parameter_type->required &&
                parameter_type->ref_target == "ValueTypeDescriptor",
            "signatures and named parameters use the shared descriptor grammar");

    REQUIRE(ontology.isValidRelation(
                "FUNCTION_SIGNATURE_HAS_PARAMETER", "FunctionSignature",
                "FunctionParameterSpec") &&
                ontology.isValidRelation(
                    "LET_EXPRESSION_HAS_BINDING", "LetIntegerExpression",
                    "LocalBinding"),
            "unordered ownership relations have closed endpoints");

    const auto* integer_body =
        ontology.findProperty("LetIntegerExpression", "let_body");
    const auto* boolean_body =
        ontology.findProperty("LetBooleanExpression", "let_body");
    REQUIRE(integer_body && integer_body->required &&
                integer_body->ref_target == "IntegerExpression" &&
                boolean_body && boolean_body->required &&
                boolean_body->ref_target == "BooleanExpression",
            "let result classes enforce their body family at KG write time");
    REQUIRE(!ontology.hasEntityType("EnumTypeDescriptor") &&
                !ontology.hasEntityType("TemporalTypeDescriptor") &&
                !ontology.hasEntityType("ApplyExpression"),
            "deferred families and generic application escape hatches stay absent");
}

void validated_writes_enforce_local_shapes_but_allow_incomplete_drafts() {
    Fixture fixture;
    kg::KGOpBatchReport missing_refinement;
    REQUIRE(!kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "EntityTypeDescriptor",
                    {{"identity_context", std::to_string(fixture.context)},
                     {"entity_key", "missing-refinement"}},
                    "descriptor"}}},
                fixture.world, missing_refinement) &&
                missing_refinement.error.find("descriptor_entity_class") !=
                    std::string::npos,
            "required local descriptor data fails at write time: " +
                missing_refinement.error);

    kg::KGOpBatchReport wrong_refinement;
    REQUIRE(!kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "EntityTypeDescriptor",
                    {{"identity_context", std::to_string(fixture.context)},
                     {"entity_key", "wrong-refinement"},
                     {"descriptor_entity_class",
                      std::to_string(fixture.context)}},
                    "descriptor"}}},
                fixture.world, wrong_refinement) &&
                wrong_refinement.error.find("OntologyClassMeta") !=
                    std::string::npos,
            "refinement references must target immutable class metadata: " +
                wrong_refinement.error);

    const auto integer = fixture.descriptor("IntegerTypeDescriptor");
    kg::KGOpBatchReport draft;
    REQUIRE(kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "FunctionSignature",
                    {{"identity_context", std::to_string(fixture.context)},
                     {"entity_key", "draft-signature"},
                     {"signature_result_type", std::to_string(integer)}},
                    "signature"}}},
                fixture.world, draft),
            "a locally complete signature draft may precede parameter links: " +
                draft.error);
    const auto validation = logosphere::rules::validate_function_signature(
        fixture.world, draft.bindings.at("signature"));
    REQUIRE(!validation.ok &&
                validation.error.find("at least one parameter") !=
                    std::string::npos,
            "explicit validation rejects the incomplete cross-entity draft: " +
                validation.error);

    const auto boolean = fixture.create("TestBooleanExpression");
    kg::KGOpBatchReport wrong_body;
    REQUIRE(!kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "LetIntegerExpression",
                    {{"let_body", std::to_string(boolean)}}, "let"}}},
                fixture.world, wrong_body) &&
                wrong_body.error.find("IntegerExpression") !=
                    std::string::npos,
            "direct typed let bodies reject the wrong family at write time: " +
                wrong_body.error);
}

void descriptors_are_structural_and_preserve_exact_refinements() {
    Fixture fixture;
    const auto integer_a = fixture.descriptor("IntegerTypeDescriptor");
    const auto integer_b = fixture.descriptor("IntegerTypeDescriptor");
    const auto floating = fixture.descriptor("FloatTypeDescriptor");
    const auto numeric = fixture.descriptor("NumericTypeDescriptor");

    const auto equal = logosphere::rules::validate_type_descriptor_assignability(
        fixture.world, integer_a, integer_b);
    REQUIRE(equal.ok, "descriptor entity identity does not split Integer: " +
                          equal.error);
    const auto widening =
        logosphere::rules::validate_type_descriptor_assignability(
            fixture.world, numeric, integer_a);
    REQUIRE(widening.ok,
            "an Integer value is assignable to the Numeric broad family: " +
                widening.error);
    const auto narrowing =
        logosphere::rules::validate_type_descriptor_assignability(
            fixture.world, integer_a, floating);
    REQUIRE(!narrowing.ok &&
                narrowing.error.find("expected Integer") != std::string::npos &&
                narrowing.error.find("actual Float") != std::string::npos,
            "descriptor mismatch names expected and actual families: " +
                narrowing.error);

    const auto task_meta = resolve(fixture, "@@meta/class/TaskCheck");
    const auto dice_meta = resolve(fixture, "@@meta/class/DiceExpression");
    const auto entity_a = fixture.descriptor("EntityTypeDescriptor");
    const auto entity_b = fixture.descriptor("EntityTypeDescriptor");
    const auto entity_other = fixture.descriptor("EntityTypeDescriptor");
    fixture.world.setProperty(entity_a, "descriptor_entity_class",
                              std::to_string(task_meta));
    fixture.world.setProperty(entity_b, "descriptor_entity_class",
                              std::to_string(task_meta));
    fixture.world.setProperty(entity_other, "descriptor_entity_class",
                              std::to_string(dice_meta));
    REQUIRE(logosphere::rules::validate_type_descriptor_assignability(
                fixture.world, entity_a, entity_b).ok,
            "equal canonical class refinements are structurally compatible");
    const auto mismatch =
        logosphere::rules::validate_type_descriptor_assignability(
            fixture.world, entity_a, entity_other);
    REQUIRE(!mismatch.ok &&
                mismatch.error.find("@@meta/class/TaskCheck") !=
                    std::string::npos &&
                mismatch.error.find("@@meta/class/DiceExpression") !=
                    std::string::npos,
            "entity incompatibility reports both exact refinements: " +
                mismatch.error);

    const auto parameter = fixture.parameter("value", integer_a);
    const auto signature_a =
        fixture.signature("signature-a", integer_a, {parameter});
    const auto signature_b =
        fixture.signature("signature-b", integer_b, {parameter});
    const auto function_a = fixture.descriptor("FunctionTypeDescriptor");
    const auto function_a_copy = fixture.descriptor("FunctionTypeDescriptor");
    const auto function_b = fixture.descriptor("FunctionTypeDescriptor");
    fixture.world.setProperty(function_a, "descriptor_function_signature",
                              std::to_string(signature_a));
    fixture.world.setProperty(function_a_copy,
                              "descriptor_function_signature",
                              std::to_string(signature_a));
    fixture.world.setProperty(function_b, "descriptor_function_signature",
                              std::to_string(signature_b));
    REQUIRE(logosphere::rules::validate_type_descriptor_assignability(
                fixture.world, function_a, function_a_copy).ok,
            "function descriptors sharing one exact signature are compatible");
    REQUIRE(!logosphere::rules::validate_type_descriptor_assignability(
                 fixture.world, function_a, function_b).ok,
            "structurally similar but separately identified signatures are not interchangeable");
}

void signatures_and_named_bindings_validate_as_unordered_complete_sets() {
    Fixture fixture;
    const auto integer_type = fixture.descriptor("IntegerTypeDescriptor");
    const auto boolean_type = fixture.descriptor("BooleanTypeDescriptor");
    const auto count = fixture.parameter("count", integer_type);
    const auto enabled = fixture.parameter("enabled", boolean_type);
    const auto signature = fixture.signature(
        "signature", integer_type, {enabled, count});

    const auto signature_result =
        logosphere::rules::validate_function_signature(fixture.world,
                                                       signature);
    REQUIRE(signature_result.ok,
            "a non-empty uniquely named typed signature validates: " +
                signature_result.error);

    const auto integer = fixture.create("TestIntegerExpression");
    const auto boolean = fixture.create("TestBooleanExpression");
    const auto count_binding = fixture.argument(count, integer);
    const auto enabled_binding = fixture.argument(enabled, boolean);
    const auto valid = logosphere::rules::validate_argument_bindings(
        fixture.world, signature, {enabled_binding, count_binding});
    REQUIRE(valid.ok, "binding order has no semantics: " + valid.error);

    const auto missing = logosphere::rules::validate_argument_bindings(
        fixture.world, signature, {count_binding});
    REQUIRE(!missing.ok && missing.error.find("enabled") != std::string::npos,
            "missing bindings identify the parameter key: " + missing.error);

    const auto duplicate_count = fixture.argument(count, integer);
    const auto duplicate = logosphere::rules::validate_argument_bindings(
        fixture.world, signature, {count_binding, duplicate_count,
                                   enabled_binding});
    REQUIRE(!duplicate.ok &&
                duplicate.error.find("duplicate") != std::string::npos &&
                duplicate.error.find("count") != std::string::npos,
            "duplicate bindings fail deterministically: " + duplicate.error);

    const auto foreign_parameter = fixture.parameter("foreign", integer_type);
    const auto foreign_binding = fixture.argument(foreign_parameter, integer);
    const auto foreign = logosphere::rules::validate_argument_bindings(
        fixture.world, signature,
        {count_binding, enabled_binding, foreign_binding});
    REQUIRE(!foreign.ok && foreign.error.find("foreign") != std::string::npos,
            "parameters outside the exact signature fail: " + foreign.error);

    const auto wrong_count = fixture.argument(count, boolean);
    const auto incompatible = logosphere::rules::validate_argument_bindings(
        fixture.world, signature, {wrong_count, enabled_binding});
    REQUIRE(!incompatible.ok &&
                incompatible.error.find("count") != std::string::npos &&
                incompatible.error.find("expected Integer") !=
                    std::string::npos &&
                incompatible.error.find("actual Boolean") !=
                    std::string::npos,
            "incompatible arguments report parameter and both types: " +
                incompatible.error);
}

void duplicate_and_cyclic_signatures_fail_boundedly() {
    Fixture fixture;
    const auto integer_type = fixture.descriptor("IntegerTypeDescriptor");
    const auto first = fixture.parameter("same", integer_type);
    const auto second = fixture.parameter("same", integer_type);
    const auto duplicate = fixture.signature(
        "duplicate-signature", integer_type, {first, second});
    const auto duplicate_result =
        logosphere::rules::validate_function_signature(fixture.world,
                                                       duplicate);
    REQUIRE(!duplicate_result.ok &&
                duplicate_result.error.find("duplicate parameter key 'same'") !=
                    std::string::npos &&
                duplicate_result.error.find(std::to_string(first)) !=
                    std::string::npos &&
                duplicate_result.error.find(std::to_string(second)) !=
                    std::string::npos,
            "duplicate keys identify both parameter entities: " +
                duplicate_result.error);

    const auto parameter = fixture.parameter("value", integer_type);
    const auto recursive = fixture.signature(
        "recursive-signature", integer_type, {parameter});
    const auto function_type = fixture.descriptor("FunctionTypeDescriptor");
    fixture.world.setProperty(function_type, "descriptor_function_signature",
                              std::to_string(recursive));
    fixture.world.setProperty(recursive, "signature_result_type",
                              std::to_string(function_type));
    const auto cyclic = logosphere::rules::validate_function_signature(
        fixture.world, recursive);
    REQUIRE(!cyclic.ok && cyclic.error.find("cyclic") != std::string::npos &&
                cyclic.error.find(std::to_string(recursive)) !=
                    std::string::npos,
            "recursive type graphs fail with bounded actionable diagnostics: " +
                cyclic.error);
}

void parameter_reads_propagate_descriptor_types_and_refinements() {
    Fixture fixture;
    const auto integer_type = fixture.descriptor("IntegerTypeDescriptor");
    const auto integer_parameter = fixture.parameter("count", integer_type);
    const auto integer_read = fixture.create("ReadParameterIntegerExpression");
    fixture.world.setProperty(integer_read, "parameter_spec",
                              std::to_string(integer_parameter));
    const auto inferred = logosphere::rules::infer_expression_static_type(
        fixture.world, integer_read);
    REQUIRE(inferred.ok &&
                inferred.type.family ==
                    logosphere::rules::RuleValueFamily::Integer,
            "parameter reads derive their type from the descriptor: " +
                inferred.error);

    const auto wrong_read = fixture.create("ReadParameterBooleanExpression");
    fixture.world.setProperty(wrong_read, "parameter_spec",
                              std::to_string(integer_parameter));
    const auto wrong = logosphere::rules::infer_expression_static_type(
        fixture.world, wrong_read);
    REQUIRE(!wrong.ok && wrong.error.find("count") != std::string::npos &&
                wrong.error.find("Boolean") != std::string::npos &&
                wrong.error.find("Integer") != std::string::npos,
            "a read class cannot contradict its parameter descriptor: " +
                wrong.error);

    const auto task_meta = resolve(fixture, "@@meta/class/TaskCheck");
    const auto entity_type = fixture.descriptor("EntityTypeDescriptor");
    fixture.world.setProperty(entity_type, "descriptor_entity_class",
                              std::to_string(task_meta));
    const auto entity_parameter = fixture.parameter("check", entity_type);
    const auto entity_read = fixture.create("ReadParameterEntityExpression");
    fixture.world.setProperty(entity_read, "parameter_spec",
                              std::to_string(entity_parameter));
    const auto entity = logosphere::rules::infer_expression_static_type(
        fixture.world, entity_read);
    REQUIRE(entity.ok &&
                entity.type.family ==
                    logosphere::rules::RuleValueFamily::Entity &&
                entity.type.refinement == "@@meta/class/TaskCheck",
            "entity parameter reads preserve exact ontology refinement: " +
                entity.error);
}

void let_blocks_validate_scope_cycles_and_deterministic_eager_order() {
    Fixture fixture;
    const auto literal = fixture.create("TestIntegerExpression");
    const auto alpha = fixture.local("alpha", literal);
    const auto read_alpha = fixture.create("ReadLocalIntegerExpression");
    fixture.world.setProperty(read_alpha, "local_binding",
                              std::to_string(alpha));
    const auto beta = fixture.local("beta", read_alpha);
    const auto read_beta = fixture.create("ReadLocalIntegerExpression");
    fixture.world.setProperty(read_beta, "local_binding",
                              std::to_string(beta));
    const auto unused = fixture.local("zeta", literal);
    const auto block = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(block, "let_body", std::to_string(read_beta));
    fixture.world.createRelation(block, "LET_EXPRESSION_HAS_BINDING", unused);
    fixture.world.createRelation(block, "LET_EXPRESSION_HAS_BINDING", beta);
    fixture.world.createRelation(block, "LET_EXPRESSION_HAS_BINDING", alpha);

    const auto valid =
        logosphere::rules::validate_let_expression(fixture.world, block);
    const std::vector<kg::EntityID> expected_order{alpha, beta, unused};
    REQUIRE(valid.ok &&
                valid.evaluation_order == expected_order,
            "all bindings evaluate once in deterministic topological key order: " +
                valid.error);

    const auto external = fixture.local("external", literal);
    const auto owned = fixture.local("owned", literal);
    const auto read_external = fixture.create("ReadLocalIntegerExpression");
    fixture.world.setProperty(read_external, "local_binding",
                              std::to_string(external));
    const auto out_of_scope = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(out_of_scope, "let_body",
                              std::to_string(read_external));
    fixture.world.createRelation(out_of_scope,
                                 "LET_EXPRESSION_HAS_BINDING", owned);
    const auto scope = logosphere::rules::validate_let_expression(
        fixture.world, out_of_scope);
    REQUIRE(!scope.ok && scope.error.find("out of scope") != std::string::npos,
            "direct binding identity cannot escape lexical scope: " +
                scope.error);

    const auto x = fixture.create("LocalBinding");
    const auto y = fixture.create("LocalBinding");
    fixture.world.setProperty(x, "binding_key", "x");
    fixture.world.setProperty(y, "binding_key", "y");
    const auto read_x = fixture.create("ReadLocalIntegerExpression");
    const auto read_y = fixture.create("ReadLocalIntegerExpression");
    fixture.world.setProperty(read_x, "local_binding", std::to_string(x));
    fixture.world.setProperty(read_y, "local_binding", std::to_string(y));
    fixture.world.setProperty(x, "binding_value", std::to_string(read_y));
    fixture.world.setProperty(y, "binding_value", std::to_string(read_x));
    const auto cycle_block = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(cycle_block, "let_body", std::to_string(read_x));
    fixture.world.createRelation(cycle_block,
                                 "LET_EXPRESSION_HAS_BINDING", x);
    fixture.world.createRelation(cycle_block,
                                 "LET_EXPRESSION_HAS_BINDING", y);
    const auto cycle = logosphere::rules::validate_let_expression(
        fixture.world, cycle_block);
    REQUIRE(!cycle.ok && cycle.error.find("cycle") != std::string::npos &&
                cycle.error.find("x") != std::string::npos &&
                cycle.error.find("y") != std::string::npos,
            "peer dependency cycles fail with both binding keys: " +
                cycle.error);
}

void nested_let_blocks_read_enclosing_bindings_without_capture_or_shadowing() {
    Fixture fixture;
    const auto literal = fixture.create("TestIntegerExpression");
    const auto outer_binding = fixture.local("outer", literal);
    const auto read_outer = fixture.create("ReadLocalIntegerExpression");
    fixture.world.setProperty(read_outer, "local_binding",
                              std::to_string(outer_binding));
    const auto inner_binding = fixture.local("inner", read_outer);
    const auto read_inner = fixture.create("ReadLocalIntegerExpression");
    fixture.world.setProperty(read_inner, "local_binding",
                              std::to_string(inner_binding));
    const auto inner = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(inner, "let_body", std::to_string(read_inner));
    fixture.world.createRelation(inner, "LET_EXPRESSION_HAS_BINDING",
                                 inner_binding);
    const auto outer = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(outer, "let_body", std::to_string(inner));
    fixture.world.createRelation(outer, "LET_EXPRESSION_HAS_BINDING",
                                 outer_binding);

    const auto valid =
        logosphere::rules::validate_let_expression(fixture.world, outer);
    const std::vector<kg::EntityID> outer_order{outer_binding};
    REQUIRE(valid.ok &&
                valid.evaluation_order == outer_order,
            "a nested block may read its lexically enclosing block: " +
                valid.error);

    const auto duplicate_a = fixture.local("duplicate", literal);
    const auto duplicate_b = fixture.local("duplicate", literal);
    const auto duplicate_block = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(duplicate_block, "let_body",
                              std::to_string(literal));
    fixture.world.createRelation(duplicate_block,
                                 "LET_EXPRESSION_HAS_BINDING", duplicate_a);
    fixture.world.createRelation(duplicate_block,
                                 "LET_EXPRESSION_HAS_BINDING", duplicate_b);
    const auto duplicate = logosphere::rules::validate_let_expression(
        fixture.world, duplicate_block);
    REQUIRE(!duplicate.ok &&
                duplicate.error.find("duplicate binding key 'duplicate'") !=
                    std::string::npos,
            "same-block keys are unique even though nested keys never shadow: " +
                duplicate.error);

    const auto empty = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(empty, "let_body", std::to_string(literal));
    const auto empty_result = logosphere::rules::validate_let_expression(
        fixture.world, empty);
    REQUIRE(!empty_result.ok &&
                empty_result.error.find("at least one local binding") !=
                    std::string::npos,
            "an empty lexical block remains an invalid draft: " +
                empty_result.error);
}

void signature_binding_and_let_validation_are_read_only() {
    Fixture fixture;
    const auto integer_type = fixture.descriptor("IntegerTypeDescriptor");
    const auto parameter = fixture.parameter("value", integer_type);
    const auto signature = fixture.signature(
        "read-only-signature", integer_type, {parameter});
    const auto literal = fixture.create("TestIntegerExpression");
    const auto argument = fixture.argument(parameter, literal);
    const auto local = fixture.local("value", literal);
    const auto block = fixture.create("LetIntegerExpression");
    fixture.world.setProperty(block, "let_body", std::to_string(literal));
    fixture.world.createRelation(block, "LET_EXPRESSION_HAS_BINDING", local);

    logosphere::EventBus events;
    int property_events = 0;
    int relation_events = 0;
    events.state_changes().subscribe(
        [&](const logosphere::ontology::WorldEvent&) { ++property_events; });
    events.relations().subscribe(
        [&](const logosphere::ontology::RelationEvent&) { ++relation_events; });
    fixture.world.set_event_bus(&events);
    const auto before = fixture.world.getStats();

    const auto signature_result =
        logosphere::rules::validate_function_signature(fixture.world,
                                                       signature);
    const auto bindings_result = logosphere::rules::validate_argument_bindings(
        fixture.world, signature, {argument});
    const auto let_result = logosphere::rules::validate_let_expression(
        fixture.world, block);
    const auto after = fixture.world.getStats();
    REQUIRE(signature_result.ok && bindings_result.ok && let_result.ok &&
                before.entity_count == after.entity_count &&
                before.relation_count == after.relation_count &&
                property_events == 0 && relation_events == 0,
            "slice 3 static validation mutates nothing and emits no events");
}

void source_addressable_type_graphs_inherit_the_sealed_context_guard() {
    Fixture fixture;
    const auto source_layer = fixture.create("SourceLayerContext");
    fixture.world.setProperty(source_layer, "context_key",
                              "source-layer:bindings");
    fixture.world.setProperty(source_layer, "source_layer", "bindings");
    const auto source = fixture.create("SourceDocumentContext");
    fixture.world.setProperty(source, "context_key",
                              "source-document:bindings");
    fixture.world.setProperty(source, "source_layer", "bindings");
    fixture.world.setProperty(source, "source_file", "rules.yaml");
    fixture.world.setProperty(source, "source_commit", "abc123");
    fixture.world.setProperty(source, "source_layer_context",
                              std::to_string(source_layer));

    const auto task_meta = resolve(fixture, "@@meta/class/TaskCheck");
    const auto dice_meta = resolve(fixture, "@@meta/class/DiceExpression");
    const auto published = fixture.create("EntityTypeDescriptor");
    fixture.world.setProperty(published, "identity_context",
                              std::to_string(source));
    fixture.world.setProperty(published, "entity_key", "published-type");
    fixture.world.setProperty(published, "descriptor_entity_class",
                              std::to_string(task_meta));

    const auto mutate = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{
            {published, ""}, "descriptor_entity_class",
            std::to_string(dice_meta)}},
        fixture.world, fixture.world.getRegistry());
    REQUIRE(!mutate.ok &&
                mutate.reason.find("sealed identity context") !=
                    std::string::npos,
            "source-addressed descriptors reject every validated mutation: " +
                mutate.reason);

    kg::KGOpBatchReport forge;
    REQUIRE(!kg::apply_kg_ops_atomically(
                {kg::KGOp{kg::KGOpCreateEntity{
                    "IntegerTypeDescriptor",
                    {{"identity_context", std::to_string(source)},
                     {"entity_key", "forged-source-type"}},
                    "forged"}}},
                fixture.world, forge) &&
                forge.error.find("sealed identity context") !=
                    std::string::npos,
            "runtime creation cannot claim a sealed source identity: " +
                forge.error);

    const auto draft = fixture.descriptor("EntityTypeDescriptor");
    fixture.world.setProperty(draft, "descriptor_entity_class",
                              std::to_string(task_meta));
    const auto edit_draft = kg::validate_kg_op(
        kg::KGOp{kg::KGOpSetProperty{
            {draft, ""}, "descriptor_entity_class",
            std::to_string(dice_meta)}},
        fixture.world, fixture.world.getRegistry());
    REQUIRE(edit_draft.ok,
            "runtime-context descriptors remain editable drafts: " +
                edit_draft.reason);
}

}  // namespace

int main() {
    std::cout << "Rule-language signatures and bindings\n";
    TEST(ontology_declares_closed_descriptor_and_binding_shapes);
    TEST(validated_writes_enforce_local_shapes_but_allow_incomplete_drafts);
    TEST(descriptors_are_structural_and_preserve_exact_refinements);
    TEST(signatures_and_named_bindings_validate_as_unordered_complete_sets);
    TEST(duplicate_and_cyclic_signatures_fail_boundedly);
    TEST(parameter_reads_propagate_descriptor_types_and_refinements);
    TEST(let_blocks_validate_scope_cycles_and_deterministic_eager_order);
    TEST(nested_let_blocks_read_enclosing_bindings_without_capture_or_shadowing);
    TEST(signature_binding_and_let_validation_are_read_only);
    TEST(source_addressable_type_graphs_inherit_the_sealed_context_guard);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
