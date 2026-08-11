#include "logosphere/rules/rule_program_validator.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/rule_static_type.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace logosphere::rules {
namespace {

RuleProgramValidationResult fail(std::string error) {
    RuleProgramValidationResult result;
    result.error = std::move(error);
    return result;
}

RuleProgramValidationResult success() {
    RuleProgramValidationResult result;
    result.ok = true;
    return result;
}

bool entity_property(const kg::KGModule& world, kg::EntityID source,
                     const std::string& property,
                     const std::string& required_type, kg::EntityID& target,
                     std::string& error) {
    if (!world.exists(source)) {
        error = "entity " + std::to_string(source) + " does not exist";
        return false;
    }
    if (!world.hasProperty(source, property)) {
        error = "entity " + std::to_string(source) + " (" +
                world.getType(source) + ") is missing required property '" +
                property + "'";
        return false;
    }
    const std::string value = world.getProperty(source, property);
    unsigned long long parsed = 0;
    const auto [end, status] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (status != std::errc{} || end != value.data() + value.size() ||
        parsed == 0 || parsed > std::numeric_limits<kg::EntityID>::max()) {
        error = "entity " + std::to_string(source) + " property '" +
                property + "' is not an entity id";
        return false;
    }
    target = static_cast<kg::EntityID>(parsed);
    if (!world.exists(target)) {
        error = "entity " + std::to_string(source) + " property '" +
                property + "' references missing entity " + value;
        return false;
    }
    if (!world.getRegistry().isSubtypeOf(world.getType(target),
                                         required_type)) {
        error = "entity " + std::to_string(source) + " property '" +
                property + "' references " + world.getType(target) +
                ", expected " + required_type;
        return false;
    }
    return true;
}

struct SignatureValidationState {
    std::unordered_set<kg::EntityID> visiting;
    std::unordered_set<kg::EntityID> validated;
};

RuleProgramValidationResult validate_signature_impl(
    const kg::KGModule& world, kg::EntityID signature,
    SignatureValidationState& state);

RuleProgramValidationResult validate_nested_function_descriptor(
    const kg::KGModule& world, kg::EntityID descriptor,
    SignatureValidationState& state) {
    const RuleStaticTypeResult type =
        infer_value_type_descriptor(world, descriptor);
    if (!type.ok) return fail(type.error);
    if (type.type.family != RuleValueFamily::Function) return success();
    return validate_signature_impl(world, type.type.function_signature, state);
}

RuleProgramValidationResult validate_signature_impl(
    const kg::KGModule& world, kg::EntityID signature,
    SignatureValidationState& state) {
    if (state.validated.count(signature)) return success();
    if (!world.exists(signature) ||
        !world.getRegistry().isSubtypeOf(world.getType(signature),
                                         "FunctionSignature")) {
        return fail("function signature " + std::to_string(signature) +
                    " does not exist as FunctionSignature");
    }
    if (!state.visiting.insert(signature).second) {
        return fail("cyclic higher-order signature graph reaches signature " +
                    std::to_string(signature));
    }

    kg::EntityID result_descriptor = kg::INVALID_ENTITY;
    std::string property_error;
    if (!entity_property(world, signature, "signature_result_type",
                         "ValueTypeDescriptor", result_descriptor,
                         property_error)) {
        state.visiting.erase(signature);
        return fail("signature " + std::to_string(signature) + ": " +
                    property_error);
    }
    if (auto nested = validate_nested_function_descriptor(
            world, result_descriptor, state);
        !nested.ok) {
        state.visiting.erase(signature);
        return fail("signature " + std::to_string(signature) +
                    " result: " + nested.error);
    }

    std::vector<kg::EntityID> parameters = world.getRelated(
        signature, "FUNCTION_SIGNATURE_HAS_PARAMETER");
    if (parameters.empty()) {
        state.visiting.erase(signature);
        return fail("function signature " + std::to_string(signature) +
                    " requires at least one parameter");
    }
    std::sort(parameters.begin(), parameters.end());
    std::map<std::string, kg::EntityID> keys;
    for (const kg::EntityID parameter : parameters) {
        if (!world.exists(parameter) ||
            !world.getRegistry().isSubtypeOf(world.getType(parameter),
                                             "FunctionParameterSpec")) {
            state.visiting.erase(signature);
            return fail("function signature " + std::to_string(signature) +
                        " references non-parameter entity " +
                        std::to_string(parameter));
        }
        const std::string key = world.getProperty(parameter, "parameter_key");
        if (key.empty()) {
            state.visiting.erase(signature);
            return fail("function signature " + std::to_string(signature) +
                        " parameter " + std::to_string(parameter) +
                        " has empty parameter_key");
        }
        const auto [existing, inserted] = keys.emplace(key, parameter);
        if (!inserted) {
            state.visiting.erase(signature);
            return fail("function signature " + std::to_string(signature) +
                        " has duplicate parameter key '" + key +
                        "' on entities " + std::to_string(existing->second) +
                        " and " + std::to_string(parameter));
        }
        kg::EntityID descriptor = kg::INVALID_ENTITY;
        if (!entity_property(world, parameter, "parameter_type",
                             "ValueTypeDescriptor", descriptor,
                             property_error)) {
            state.visiting.erase(signature);
            return fail("signature " + std::to_string(signature) +
                        " parameter '" + key + "': " + property_error);
        }
        if (auto nested = validate_nested_function_descriptor(
                world, descriptor, state);
            !nested.ok) {
            state.visiting.erase(signature);
            return fail("signature " + std::to_string(signature) +
                        " parameter '" + key + "': " + nested.error);
        }
    }

    state.visiting.erase(signature);
    state.validated.insert(signature);
    return success();
}

struct ScopeFrame {
    kg::EntityID block = kg::INVALID_ENTITY;
    std::map<kg::EntityID, std::string> keys;
    std::map<kg::EntityID, std::set<kg::EntityID>> dependencies;
};

struct ScopeValidationState {
    std::vector<ScopeFrame> frames;
    std::vector<kg::EntityID> owners;
    std::unordered_set<kg::EntityID> expression_path;
    std::unordered_set<kg::EntityID> block_path;
};

bool validate_block(const kg::KGModule& world, kg::EntityID block,
                    ScopeValidationState& state,
                    std::vector<kg::EntityID>* root_order,
                    std::string& error);

std::vector<const kg::PropertyDef*> expression_child_properties(
    const kg::OntologyRegistry& ontology, const std::string& type) {
    std::map<std::string, const kg::PropertyDef*> by_name;
    auto include = [&](const std::string& owner) {
        for (const auto& property : ontology.propertiesOf(owner)) {
            if (property.value_kind == kg::PropertyValueKind::EntityRef &&
                ontology.isSubtypeOf(property.ref_target, "Expression")) {
                by_name[property.name] = &property;
            }
        }
    };
    std::vector<std::string> ancestors(
        ontology.ancestorsOf(type).begin(), ontology.ancestorsOf(type).end());
    std::sort(ancestors.begin(), ancestors.end());
    for (const auto& ancestor : ancestors) include(ancestor);
    // The concrete class wins when it narrows an inherited expression slot.
    include(type);
    std::vector<const kg::PropertyDef*> result;
    for (const auto& [name, property] : by_name) {
        (void)name;
        result.push_back(property);
    }
    return result;
}

bool validate_expression_scope(const kg::KGModule& world,
                               kg::EntityID expression,
                               ScopeValidationState& state,
                               std::string& error) {
    if (!world.exists(expression) ||
        !world.getRegistry().isSubtypeOf(world.getType(expression),
                                         "Expression")) {
        error = "lexical graph references non-expression entity " +
                std::to_string(expression);
        return false;
    }
    if (!state.expression_path.insert(expression).second) {
        error = "expression dependency cycle reaches entity " +
                std::to_string(expression) + " (" +
                world.getType(expression) + ")";
        return false;
    }
    const auto finish = [&](bool ok) {
        state.expression_path.erase(expression);
        return ok;
    };
    const auto& ontology = world.getRegistry();
    const std::string type = world.getType(expression);

    if (ontology.isSubtypeOf(type, "LocalReadExpression")) {
        kg::EntityID target = kg::INVALID_ENTITY;
        if (!entity_property(world, expression, "local_binding",
                             "LocalBinding", target, error)) {
            return finish(false);
        }
        size_t scope = state.frames.size();
        while (scope > 0) {
            --scope;
            if (state.frames[scope].keys.count(target)) break;
        }
        if (state.frames.empty() ||
            !state.frames[scope].keys.count(target)) {
            error = "local reference " + std::to_string(expression) +
                    " targets binding " + std::to_string(target) +
                    " out of scope";
            return finish(false);
        }
        const kg::EntityID owner = state.owners[scope];
        if (owner != kg::INVALID_ENTITY) {
            state.frames[scope].dependencies[owner].insert(target);
        }
        return finish(true);
    }

    if (ontology.isSubtypeOf(type, "LetExpression")) {
        const bool valid = validate_block(world, expression, state, nullptr,
                                          error);
        return finish(valid);
    }

    for (const kg::PropertyDef* property :
         expression_child_properties(ontology, type)) {
        if (!world.hasProperty(expression, property->name)) continue;
        kg::EntityID child = kg::INVALID_ENTITY;
        if (!entity_property(world, expression, property->name, "Expression",
                             child, error) ||
            !validate_expression_scope(world, child, state, error)) {
            return finish(false);
        }
    }
    return finish(true);
}

bool topological_order(const ScopeFrame& frame,
                       std::vector<kg::EntityID>& order,
                       std::string& error) {
    std::map<kg::EntityID, std::set<kg::EntityID>> dependencies =
        frame.dependencies;
    auto by_key = [&](kg::EntityID left, kg::EntityID right) {
        const std::string& left_key = frame.keys.at(left);
        const std::string& right_key = frame.keys.at(right);
        return left_key < right_key ||
               (left_key == right_key && left < right);
    };
    std::set<kg::EntityID, decltype(by_key)> ready(by_key);
    for (const auto& [binding, key] : frame.keys) {
        (void)key;
        if (dependencies[binding].empty()) ready.insert(binding);
    }
    while (!ready.empty()) {
        const kg::EntityID next = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(next);
        for (auto& [binding, required] : dependencies) {
            if (!required.erase(next) || !required.empty()) continue;
            if (std::find(order.begin(), order.end(), binding) == order.end()) {
                ready.insert(binding);
            }
        }
    }
    if (order.size() == frame.keys.size()) return true;

    std::vector<std::string> cycle;
    for (const auto& [binding, key] : frame.keys) {
        if (std::find(order.begin(), order.end(), binding) == order.end()) {
            cycle.push_back(key);
        }
    }
    std::sort(cycle.begin(), cycle.end());
    error = "let block " + std::to_string(frame.block) +
            " dependency cycle among bindings";
    for (const auto& key : cycle) error += " '" + key + "'";
    return false;
}

bool validate_block(const kg::KGModule& world, kg::EntityID block,
                    ScopeValidationState& state,
                    std::vector<kg::EntityID>* root_order,
                    std::string& error) {
    if (!world.exists(block) ||
        !world.getRegistry().isSubtypeOf(world.getType(block),
                                         "LetExpression")) {
        error = "entity " + std::to_string(block) +
                " is not a LetExpression";
        return false;
    }
    if (!state.block_path.insert(block).second) {
        error = "lexical block cycle reaches let expression " +
                std::to_string(block);
        return false;
    }
    const auto leave = [&](bool ok) {
        state.block_path.erase(block);
        return ok;
    };

    std::vector<kg::EntityID> bindings = world.getRelated(
        block, "LET_EXPRESSION_HAS_BINDING");
    if (bindings.empty()) {
        error = "let expression " + std::to_string(block) +
                " requires at least one local binding";
        return leave(false);
    }
    std::sort(bindings.begin(), bindings.end());
    ScopeFrame frame;
    frame.block = block;
    std::map<std::string, kg::EntityID> key_owner;
    for (const kg::EntityID binding : bindings) {
        if (!world.exists(binding) ||
            !world.getRegistry().isSubtypeOf(world.getType(binding),
                                             "LocalBinding")) {
            error = "let expression " + std::to_string(block) +
                    " references non-binding entity " +
                    std::to_string(binding);
            return leave(false);
        }
        if (frame.keys.count(binding)) {
            error = "let expression " + std::to_string(block) +
                    " contains duplicate binding entity " +
                    std::to_string(binding);
            return leave(false);
        }
        const auto owners = world.getRelatedReverse(
            binding, "LET_EXPRESSION_HAS_BINDING");
        if (owners.size() != 1 || owners.front() != block) {
            error = "local binding " + std::to_string(binding) +
                    " must be owned by exactly one let expression";
            return leave(false);
        }
        const std::string key = world.getProperty(binding, "binding_key");
        if (key.empty()) {
            error = "let expression " + std::to_string(block) +
                    " binding " + std::to_string(binding) +
                    " has empty binding_key";
            return leave(false);
        }
        const auto [existing, inserted] = key_owner.emplace(key, binding);
        if (!inserted) {
            error = "let expression " + std::to_string(block) +
                    " has duplicate binding key '" + key + "' on entities " +
                    std::to_string(existing->second) + " and " +
                    std::to_string(binding);
            return leave(false);
        }
        frame.keys.emplace(binding, key);
        frame.dependencies.emplace(binding, std::set<kg::EntityID>{});
    }

    state.frames.push_back(std::move(frame));
    state.owners.push_back(kg::INVALID_ENTITY);
    auto pop_scope = [&]() {
        state.owners.pop_back();
        state.frames.pop_back();
    };

    std::vector<kg::EntityID> sorted_bindings = bindings;
    std::sort(sorted_bindings.begin(), sorted_bindings.end(),
              [&](kg::EntityID left, kg::EntityID right) {
                  return state.frames.back().keys.at(left) <
                         state.frames.back().keys.at(right);
              });
    for (const kg::EntityID binding : sorted_bindings) {
        kg::EntityID value = kg::INVALID_ENTITY;
        if (!entity_property(world, binding, "binding_value", "Expression",
                             value, error)) {
            pop_scope();
            return leave(false);
        }
        state.owners.back() = binding;
        if (!validate_expression_scope(world, value, state, error)) {
            pop_scope();
            return leave(false);
        }
        state.owners.back() = kg::INVALID_ENTITY;
    }

    kg::EntityID body = kg::INVALID_ENTITY;
    if (!entity_property(world, block, "let_body", "Expression", body,
                         error) ||
        !validate_expression_scope(world, body, state, error)) {
        pop_scope();
        return leave(false);
    }

    std::vector<kg::EntityID> order;
    if (!topological_order(state.frames.back(), order, error)) {
        pop_scope();
        return leave(false);
    }
    for (const kg::EntityID binding : order) {
        kg::EntityID value = kg::INVALID_ENTITY;
        if (!entity_property(world, binding, "binding_value", "Expression",
                             value, error)) {
            pop_scope();
            return leave(false);
        }
        const RuleStaticTypeResult type =
            infer_expression_static_type(world, value);
        if (!type.ok) {
            error = "let binding '" + state.frames.back().keys.at(binding) +
                    "': " + type.error;
            pop_scope();
            return leave(false);
        }
    }
    const RuleStaticTypeResult body_type =
        infer_expression_static_type(world, block);
    if (!body_type.ok) {
        error = body_type.error;
        pop_scope();
        return leave(false);
    }
    if (root_order) *root_order = order;
    pop_scope();
    return leave(true);
}

}  // namespace

RuleProgramValidationResult validate_function_signature(
    const kg::KGModule& world, kg::EntityID signature) {
    if (!world.hasCurrentOntologyMetaGraph()) {
        return fail("function signature validation requires a current "
                    "ontology meta-graph");
    }
    SignatureValidationState state;
    return validate_signature_impl(world, signature, state);
}

RuleProgramValidationResult validate_argument_bindings(
    const kg::KGModule& world, kg::EntityID signature,
    const std::vector<kg::EntityID>& bindings) {
    if (auto validated = validate_function_signature(world, signature);
        !validated.ok) {
        return validated;
    }

    const std::vector<kg::EntityID> parameter_list = world.getRelated(
        signature, "FUNCTION_SIGNATURE_HAS_PARAMETER");
    std::unordered_set<kg::EntityID> parameters(parameter_list.begin(),
                                                 parameter_list.end());
    std::map<std::string, kg::EntityID> parameter_by_key;
    for (const kg::EntityID parameter : parameter_list) {
        parameter_by_key.emplace(world.getProperty(parameter, "parameter_key"),
                                 parameter);
    }

    struct BindingRecord {
        kg::EntityID binding = kg::INVALID_ENTITY;
        kg::EntityID parameter = kg::INVALID_ENTITY;
        kg::EntityID argument = kg::INVALID_ENTITY;
        std::string key;
    };
    std::vector<BindingRecord> records;
    for (const kg::EntityID binding : bindings) {
        if (!world.exists(binding) ||
            !world.getRegistry().isSubtypeOf(world.getType(binding),
                                             "ArgumentBinding")) {
            return fail("argument binding entity " + std::to_string(binding) +
                        " does not exist as ArgumentBinding");
        }
        BindingRecord record;
        record.binding = binding;
        std::string property_error;
        if (!entity_property(world, binding, "binding_parameter",
                             "FunctionParameterSpec", record.parameter,
                             property_error) ||
            !entity_property(world, binding, "binding_argument", "Expression",
                             record.argument, property_error)) {
            return fail(property_error);
        }
        record.key = world.getProperty(record.parameter, "parameter_key");
        records.push_back(std::move(record));
    }
    std::sort(records.begin(), records.end(),
              [](const BindingRecord& left, const BindingRecord& right) {
                  return left.key < right.key ||
                         (left.key == right.key && left.binding < right.binding);
              });

    std::unordered_set<kg::EntityID> bound;
    for (const BindingRecord& record : records) {
        if (!parameters.count(record.parameter)) {
            return fail("argument binding " +
                        std::to_string(record.binding) + " targets foreign "
                        "parameter '" + record.key + "' outside signature " +
                        std::to_string(signature));
        }
        if (!bound.insert(record.parameter).second) {
            return fail("duplicate argument binding for parameter '" +
                        record.key + "' in signature " +
                        std::to_string(signature));
        }
        kg::EntityID expected_descriptor = kg::INVALID_ENTITY;
        std::string property_error;
        if (!entity_property(world, record.parameter, "parameter_type",
                             "ValueTypeDescriptor", expected_descriptor,
                             property_error)) {
            return fail(property_error);
        }
        const RuleStaticTypeResult expected =
            infer_value_type_descriptor(world, expected_descriptor);
        const RuleStaticTypeResult actual =
            infer_expression_static_type(world, record.argument);
        if (!expected.ok) return fail(expected.error);
        if (!actual.ok) return fail(actual.error);
        const auto compatible = validate_rule_static_type_assignability(
            expected.type, actual.type);
        if (!compatible.ok) {
            return fail("parameter '" + record.key + "': " +
                        compatible.error);
        }
    }
    for (const auto& [key, parameter] : parameter_by_key) {
        if (!bound.count(parameter)) {
            return fail("missing argument binding for parameter '" + key +
                        "' in signature " + std::to_string(signature));
        }
    }
    return success();
}

RuleProgramValidationResult validate_let_expression(
    const kg::KGModule& world, kg::EntityID let_expression) {
    if (!world.hasCurrentOntologyMetaGraph()) {
        return fail("let validation requires a current ontology meta-graph");
    }
    ScopeValidationState state;
    RuleProgramValidationResult result;
    if (!validate_block(world, let_expression, state,
                        &result.evaluation_order, result.error)) {
        result.evaluation_order.clear();
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace logosphere::rules
