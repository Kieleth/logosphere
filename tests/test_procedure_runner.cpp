// Data-driven procedure execution: exact primitives, explicit routes, and
// suspension at external choices.

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/procedure_runner.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                        \
    std::cout << "  " #name "... ";                                     \
    try { name(); ++tests_passed; std::cout << "PASS\n"; }               \
    catch (const std::exception& error) {                                 \
        ++tests_failed; std::cout << "FAIL: " << error.what() << '\n';   \
    }

#define REQUIRE(condition, message)                                      \
    if (!(condition)) throw std::runtime_error(message)

namespace {

namespace rules = logosphere::rules;

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    out.extend(rulebook::ontology::registry());
    return out;
}

struct Fixture {
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    kg::EntityID target = kg::INVALID_ENTITY;
    kg::EntityID procedure = kg::INVALID_ENTITY;
    std::vector<kg::EntityID> steps;

    Fixture() {
        world.setMode(kg::KGMode::MINIMAL);
        target = world.createEntity("Entity");
        procedure = world.createEntity("Procedure");
    }

    kg::EntityID step(const std::string& primitive) {
        const auto id = world.createEntity("ProcedureStep");
        world.setProperty(id, "step_index", std::to_string(steps.size()));
        world.setProperty(id, "primitive_ref", primitive);
        world.createRelation(procedure, "HAS_PART", id);
        steps.push_back(id);
        return id;
    }

    kg::EntityID route(kg::EntityID from, const std::string& label,
                       kg::EntityID to) {
        const auto id = world.createEntity("StepRoute");
        world.setProperty(id, "route_label", label);
        world.setProperty(id, "next_step", std::to_string(to));
        world.createRelation(from, "HAS_PART", id);
        return id;
    }
};

void declare(rules::ProcedurePrimitiveRegistry& registry,
             const std::string& name,
             const std::vector<std::string>& labels = {}) {
    std::string error;
    REQUIRE(registry.declare_primitive(name, labels, error),
            "could not declare " + name + ": " + error);
}

void bind(rules::ProcedurePrimitiveRegistry& registry,
          const std::string& name, rules::ProcedurePrimitive handler) {
    std::string error;
    REQUIRE(registry.bind_primitive(name, std::move(handler), error),
            "could not bind " + name + ": " + error);
}

void routes_and_choices_drive_execution_from_data() {
    Fixture f;
    const auto begin = f.step("begin");
    f.step("must_not_run");
    const auto choose = f.step("choose");
    f.step("finish");
    f.route(begin, "skip", choose);

    rules::ProcedurePrimitiveRegistry primitives;
    declare(primitives, "begin", {"skip"});
    declare(primitives, "must_not_run");
    declare(primitives, "choose");
    declare(primitives, "finish");

    std::vector<std::string> calls;
    bind(primitives, "begin",
         [&](const rules::ProcedurePrimitiveContext&) {
             calls.push_back("begin");
             return rules::ProcedurePrimitiveResult::advance("skip");
         });
    bind(primitives, "must_not_run",
         [&](const rules::ProcedurePrimitiveContext&) {
             calls.push_back("wrong");
             return rules::ProcedurePrimitiveResult::advance();
         });
    bind(primitives, "choose",
         [&](const rules::ProcedurePrimitiveContext& context) {
             if (!context.input) {
                 return rules::ProcedurePrimitiveResult::pending(
                     "Pick one", {{"a", "Alpha", "first"},
                                  {"b", "Beta", "second"}});
             }
             calls.push_back("choose:" + *context.input);
             return rules::ProcedurePrimitiveResult::advance();
         });
    bind(primitives, "finish",
         [&](const rules::ProcedurePrimitiveContext&) {
             calls.push_back("finish");
             return rules::ProcedurePrimitiveResult::complete();
         });

    rules::ProcedureRunner runner(f.world, primitives);
    const auto pending = runner.start(f.procedure, f.target);
    REQUIRE(pending.status == rules::ProcedureStatus::PENDING &&
                pending.cursor.step == choose &&
                pending.prompt == "Pick one" &&
                pending.choices.size() == 2 &&
                calls == std::vector<std::string>{"begin"},
            "the route must skip step 1 and suspend at the choice");

    const auto complete = runner.resume(pending.cursor, "b");
    const std::vector<std::string> expected{
        "begin", "choose:b", "finish"};
    REQUIRE(complete.status == rules::ProcedureStatus::COMPLETED &&
                calls == expected,
            "resume must invoke the suspended step once with the input");
}

void the_complete_graph_validates_before_the_first_primitive_runs() {
    Fixture f;
    f.step("known");
    f.step("unknown");
    rules::ProcedurePrimitiveRegistry primitives;
    declare(primitives, "known");
    int calls = 0;
    bind(primitives, "known",
         [&](const rules::ProcedurePrimitiveContext&) {
             ++calls;
             return rules::ProcedurePrimitiveResult::advance();
         });

    rules::ProcedureRunner runner(f.world, primitives);
    const auto result = runner.start(f.procedure, f.target);
    REQUIRE(result.status == rules::ProcedureStatus::FAILED && calls == 0 &&
                result.error.find("unknown") != std::string::npos,
            "a late unknown primitive must stop before earlier side effects");
}

void primitive_and_route_contracts_are_exact() {
    rules::ProcedurePrimitiveRegistry primitives;
    std::string error;
    REQUIRE(primitives.declare_primitive("check", {"passed", "failed"},
                                         error),
            "the first contract must register");
    REQUIRE(!primitives.declare_primitive("check", {"other"}, error) &&
                error.find("already declared") != std::string::npos,
            "a duplicate contract cannot replace the original");
    REQUIRE(!primitives.bind_primitive(
                "missing",
                [](const rules::ProcedurePrimitiveContext&) {
                    return rules::ProcedurePrimitiveResult::advance();
                },
                error) &&
                error.find("not declared") != std::string::npos,
            "a handler cannot exist without a declared contract");

    Fixture f;
    f.step("check");
    bind(primitives, "check",
         [](const rules::ProcedurePrimitiveContext&) {
             return rules::ProcedurePrimitiveResult::advance("maybe");
         });
    rules::ProcedureRunner runner(f.world, primitives);
    const auto result = runner.start(f.procedure, f.target);
    REQUIRE(result.status == rules::ProcedureStatus::FAILED &&
                result.error.find("undeclared route label 'maybe'") !=
                    std::string::npos,
            "a primitive cannot invent a route label at runtime");
}

void malformed_structure_and_non_suspending_cycles_fail_loudly() {
    Fixture f;
    const auto loop = f.step("loop");
    f.route(loop, "again", loop);
    rules::ProcedurePrimitiveRegistry primitives;
    declare(primitives, "loop", {"again"});
    bind(primitives, "loop",
         [](const rules::ProcedurePrimitiveContext&) {
             return rules::ProcedurePrimitiveResult::advance("again");
         });
    rules::ProcedureRunner runner(f.world, primitives, 8);
    const auto cycled = runner.start(f.procedure, f.target);
    REQUIRE(cycled.status == rules::ProcedureStatus::FAILED &&
                cycled.error.find("transition limit") != std::string::npos,
            "a synchronous route cycle must hit a mechanical guard");

    Fixture outside;
    const auto source = outside.step("loop");
    const auto foreign_procedure = outside.world.createEntity("Procedure");
    const auto foreign = outside.world.createEntity("ProcedureStep");
    outside.world.setProperty(foreign, "step_index", "0");
    outside.world.setProperty(foreign, "primitive_ref", "loop");
    outside.world.createRelation(foreign_procedure, "HAS_PART", foreign);
    outside.route(source, "again", foreign);
    rules::ProcedureRunner outside_runner(outside.world, primitives);
    const auto escaped = outside_runner.start(outside.procedure,
                                              outside.target);
    REQUIRE(escaped.status == rules::ProcedureStatus::FAILED &&
                escaped.error.find("outside Procedure") !=
                    std::string::npos,
            "a route cannot jump into another procedure");
}

void pending_results_require_a_real_question() {
    Fixture f;
    f.step("empty_prompt");
    rules::ProcedurePrimitiveRegistry primitives;
    declare(primitives, "empty_prompt");
    bind(primitives, "empty_prompt",
         [](const rules::ProcedurePrimitiveContext&) {
             return rules::ProcedurePrimitiveResult::pending("", {});
         });
    rules::ProcedureRunner runner(f.world, primitives);
    const auto result = runner.start(f.procedure, f.target);
    REQUIRE(result.status == rules::ProcedureStatus::FAILED &&
                result.error.find("non-empty prompt") != std::string::npos,
            "an unusable suspension must not escape the runner");
}

}  // namespace

int main() {
    std::cout << "Procedure runner\n";
    TEST(routes_and_choices_drive_execution_from_data);
    TEST(the_complete_graph_validates_before_the_first_primitive_runs);
    TEST(primitive_and_route_contracts_are_exact);
    TEST(malformed_structure_and_non_suspending_cycles_fail_loudly);
    TEST(pending_results_require_a_real_question);
    std::cout << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
