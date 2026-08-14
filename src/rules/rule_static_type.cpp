#include "logosphere/rules/rule_static_type.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/qualified_reference.h"

#include <array>
#include <charconv>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace logosphere::rules {
namespace {

RuleStaticTypeResult fail(kg::EntityID node, std::string ontology_type,
                          std::string error) {
    RuleStaticTypeResult result;
    result.node = node;
    result.ontology_type = std::move(ontology_type);
    result.error = std::move(error);
    return result;
}

RuleStaticTypeResult success(kg::EntityID node, std::string ontology_type,
                             RuleValueFamily family,
                             std::string refinement = {},
                             kg::EntityID function_signature =
                                 kg::INVALID_ENTITY) {
    RuleStaticTypeResult result;
    result.ok = true;
    result.node = node;
    result.ontology_type = std::move(ontology_type);
    result.type.family = family;
    result.type.refinement = std::move(refinement);
    result.type.function_signature = function_signature;
    return result;
}

bool accepts(RuleValueFamily expected, RuleValueFamily actual) {
    if (expected == actual) return true;
    return expected == RuleValueFamily::Numeric &&
           (actual == RuleValueFamily::Integer ||
            actual == RuleValueFamily::Float);
}

bool exactly_one_related(const kg::KGModule& world, kg::EntityID source,
                         const std::string& relation, kg::EntityID& target,
                         std::string& error) {
    const auto related = world.getRelated(source, relation);
    if (related.size() != 1) {
        error = "meta entity " + std::to_string(source) + " has " +
                std::to_string(related.size()) + " '" + relation +
                "' links, expected exactly one";
        return false;
    }
    target = related.front();
    return true;
}

bool entity_property(const kg::KGModule& world, kg::EntityID source,
                     const std::string& property,
                     const std::string& required_type, kg::EntityID& target,
                     std::string& error) {
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

std::string format_type(const RuleStaticType& type) {
    std::string out = rule_value_family_name(type.family);
    if (!type.refinement.empty()) out += "<" + type.refinement + ">";
    if (type.function_signature != kg::INVALID_ENTITY) {
        out += "<signature " + std::to_string(type.function_signature) + ">";
    }
    return out;
}

bool exact_type(const RuleStaticType& expected,
                const RuleStaticType& actual) {
    return expected.family == actual.family &&
           expected.refinement == actual.refinement &&
           expected.function_signature == actual.function_signature;
}

RuleStaticTypeResult infer_expression_impl(
    const kg::KGModule& world, kg::EntityID expression,
    std::unordered_set<kg::EntityID>& visiting);

}  // namespace

const char* rule_value_family_name(RuleValueFamily family) {
    switch (family) {
        case RuleValueFamily::Boolean: return "Boolean";
        case RuleValueFamily::Numeric: return "Numeric";
        case RuleValueFamily::Integer: return "Integer";
        case RuleValueFamily::Float: return "Float";
        case RuleValueFamily::String: return "String";
        case RuleValueFamily::Entity: return "Entity";
        case RuleValueFamily::BooleanCollection:
            return "BooleanCollection";
        case RuleValueFamily::IntegerCollection:
            return "IntegerCollection";
        case RuleValueFamily::FloatCollection: return "FloatCollection";
        case RuleValueFamily::StringCollection: return "StringCollection";
        case RuleValueFamily::EntityCollection: return "EntityCollection";
        case RuleValueFamily::Function: return "Function";
        case RuleValueFamily::OutcomePlan: return "OutcomePlan";
    }
    return "Unknown";
}

RuleStaticTypeResult infer_value_type_descriptor(
    const kg::KGModule& world, kg::EntityID descriptor) {
    if (!world.hasCurrentOntologyMetaGraph()) {
        return fail(descriptor, "",
                    "static rule validation requires a current ontology "
                    "meta-graph");
    }
    if (!world.exists(descriptor)) {
        return fail(descriptor, "", "type descriptor entity " +
                    std::to_string(descriptor) + " does not exist");
    }
    const std::string type = world.getType(descriptor);
    const auto& ontology = world.getRegistry();
    if (!ontology.isSubtypeOf(type, "ValueTypeDescriptor")) {
        return fail(descriptor, type, "entity " +
                    std::to_string(descriptor) + " (" + type +
                    ") is not a ValueTypeDescriptor");
    }
    if (ontology.isAbstract(type)) {
        return fail(descriptor, type, "type descriptor entity " +
                    std::to_string(descriptor) + " has abstract type '" +
                    type + "'");
    }

    static constexpr std::array<
        std::pair<const char*, RuleValueFamily>, 13> kDescriptors{{
        {"BooleanTypeDescriptor", RuleValueFamily::Boolean},
        {"NumericTypeDescriptor", RuleValueFamily::Numeric},
        {"IntegerTypeDescriptor", RuleValueFamily::Integer},
        {"FloatTypeDescriptor", RuleValueFamily::Float},
        {"StringTypeDescriptor", RuleValueFamily::String},
        {"EntityTypeDescriptor", RuleValueFamily::Entity},
        {"BooleanCollectionTypeDescriptor",
         RuleValueFamily::BooleanCollection},
        {"IntegerCollectionTypeDescriptor",
         RuleValueFamily::IntegerCollection},
        {"FloatCollectionTypeDescriptor", RuleValueFamily::FloatCollection},
        {"StringCollectionTypeDescriptor", RuleValueFamily::StringCollection},
        {"EntityCollectionTypeDescriptor", RuleValueFamily::EntityCollection},
        {"FunctionTypeDescriptor", RuleValueFamily::Function},
        {"OutcomePlanTypeDescriptor", RuleValueFamily::OutcomePlan},
    }};
    RuleValueFamily family = RuleValueFamily::Boolean;
    bool found = false;
    for (const auto& [descriptor_type, candidate] : kDescriptors) {
        if (type == descriptor_type) {
            family = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        return fail(descriptor, type, "type descriptor entity " +
                    std::to_string(descriptor) + " has unsupported concrete "
                    "descriptor type '" + type + "'");
    }

    std::string property_error;
    kg::EntityID refinement = kg::INVALID_ENTITY;
    if (family == RuleValueFamily::Entity) {
        if (!entity_property(world, descriptor, "descriptor_entity_class",
                             "OntologyClassMeta", refinement,
                             property_error)) {
            return fail(descriptor, type, property_error);
        }
        return success(descriptor, type, family,
                       world.getProperty(refinement, "qualified_key"));
    }
    if (family == RuleValueFamily::EntityCollection) {
        if (!entity_property(world, descriptor, "descriptor_element_class",
                             "OntologyClassMeta", refinement,
                             property_error)) {
            return fail(descriptor, type, property_error);
        }
        return success(descriptor, type, family,
                       world.getProperty(refinement, "qualified_key"));
    }
    if (family == RuleValueFamily::Function) {
        if (!entity_property(world, descriptor,
                             "descriptor_function_signature",
                             "FunctionSignature", refinement,
                             property_error)) {
            return fail(descriptor, type, property_error);
        }
        return success(descriptor, type, family, {}, refinement);
    }
    return success(descriptor, type, family);
}

RuleTypeCompatibilityResult validate_type_descriptor_assignability(
    const kg::KGModule& world, kg::EntityID expected_descriptor,
    kg::EntityID actual_descriptor) {
    RuleTypeCompatibilityResult result;
    const RuleStaticTypeResult expected =
        infer_value_type_descriptor(world, expected_descriptor);
    if (!expected.ok) {
        result.error = "expected type descriptor invalid: " + expected.error;
        return result;
    }
    const RuleStaticTypeResult actual =
        infer_value_type_descriptor(world, actual_descriptor);
    if (!actual.ok) {
        result.error = "actual type descriptor invalid: " + actual.error;
        return result;
    }
    result.expected = expected.type;
    result.actual = actual.type;
    return validate_rule_static_type_assignability(expected.type, actual.type);
}

RuleTypeCompatibilityResult validate_rule_static_type_assignability(
    const RuleStaticType& expected, const RuleStaticType& actual) {
    RuleTypeCompatibilityResult result;
    result.expected = expected;
    result.actual = actual;
    if (!accepts(expected.family, actual.family)) {
        result.error = "expected " + format_type(expected) +
                       ", actual " + format_type(actual);
        return result;
    }
    if ((expected.family == RuleValueFamily::Entity ||
         expected.family == RuleValueFamily::EntityCollection) &&
        expected.refinement != actual.refinement) {
        result.error = "expected " + format_type(expected) +
                       ", actual " + format_type(actual) +
                       "; ontology inheritance compatibility is not selected";
        return result;
    }
    if (expected.family == RuleValueFamily::Function &&
        expected.function_signature != actual.function_signature) {
        result.error = "expected " + format_type(expected) +
                       ", actual " + format_type(actual);
        return result;
    }
    result.ok = true;
    return result;
}

namespace {

RuleStaticTypeResult infer_expression_impl(
    const kg::KGModule& world, kg::EntityID expression,
    std::unordered_set<kg::EntityID>& visiting) {
    if (!world.hasCurrentOntologyMetaGraph()) {
        return fail(expression, "",
                    "static rule validation requires a current ontology "
                    "meta-graph");
    }
    if (!world.exists(expression)) {
        return fail(expression, "", "expression entity " +
                    std::to_string(expression) + " does not exist");
    }
    const std::string type = world.getType(expression);
    const auto& ontology = world.getRegistry();
    if (!ontology.isSubtypeOf(type, "Expression")) {
        return fail(expression, type, "entity " +
                    std::to_string(expression) + " (" + type +
                    ") is not an Expression");
    }
    if (ontology.isAbstract(type)) {
        return fail(expression, type, "expression entity " +
                    std::to_string(expression) + " has abstract type '" +
                    type + "'");
    }
    if (!visiting.insert(expression).second) {
        return fail(expression, type, "expression dependency cycle reaches " +
                    std::to_string(expression) + " (" + type + ")");
    }

    static constexpr std::array<
        std::pair<const char*, RuleValueFamily>, 11> kConcreteFamilies{{
        {"BooleanExpression", RuleValueFamily::Boolean},
        {"IntegerExpression", RuleValueFamily::Integer},
        {"FloatExpression", RuleValueFamily::Float},
        {"StringExpression", RuleValueFamily::String},
        {"EntityExpression", RuleValueFamily::Entity},
        {"BooleanCollectionExpression", RuleValueFamily::BooleanCollection},
        {"IntegerCollectionExpression", RuleValueFamily::IntegerCollection},
        {"FloatCollectionExpression", RuleValueFamily::FloatCollection},
        {"StringCollectionExpression", RuleValueFamily::StringCollection},
        {"EntityCollectionExpression", RuleValueFamily::EntityCollection},
        {"FunctionExpression", RuleValueFamily::Function},
    }};
    std::vector<RuleValueFamily> matches;
    for (const auto& [family_type, family] : kConcreteFamilies) {
        if (ontology.isSubtypeOf(type, family_type)) matches.push_back(family);
    }
    if (ontology.isSubtypeOf(type, "OutcomePlanExpression")) {
        matches.push_back(RuleValueFamily::OutcomePlan);
    }
    if (matches.empty() && ontology.isSubtypeOf(type, "NumericExpression")) {
        matches.push_back(RuleValueFamily::Numeric);
    }
    if (matches.empty()) {
        visiting.erase(expression);
        return fail(expression, type, "expression entity " +
                    std::to_string(expression) + " (" + type +
                    ") has no concrete result family");
    }
    if (matches.size() != 1) {
        visiting.erase(expression);
        return fail(expression, type, "expression entity " +
                    std::to_string(expression) + " (" + type +
                    ") inherits multiple result families");
    }
    const RuleStaticType declared{matches.front(), {}, kg::INVALID_ENTITY};

    auto finish_from = [&](RuleStaticTypeResult inferred,
                           const std::string& source) {
        visiting.erase(expression);
        if (!inferred.ok) return inferred;
        if (!exact_type(declared, RuleStaticType{inferred.type.family, {},
                                                 kg::INVALID_ENTITY})) {
            return fail(expression, type, "expression entity " +
                        std::to_string(expression) + " (" + type +
                        ") declares " + format_type(declared) + " but " +
                        source + " has " + format_type(inferred.type));
        }
        inferred.node = expression;
        inferred.ontology_type = type;
        return inferred;
    };

    if (ontology.isSubtypeOf(type, "ParameterReadExpression")) {
        kg::EntityID parameter = kg::INVALID_ENTITY;
        std::string property_error;
        if (!entity_property(world, expression, "parameter_spec",
                             "FunctionParameterSpec", parameter,
                             property_error)) {
            visiting.erase(expression);
            return fail(expression, type, property_error);
        }
        kg::EntityID descriptor = kg::INVALID_ENTITY;
        if (!entity_property(world, parameter, "parameter_type",
                             "ValueTypeDescriptor", descriptor,
                             property_error)) {
            visiting.erase(expression);
            return fail(expression, type, property_error);
        }
        RuleStaticTypeResult inferred =
            infer_value_type_descriptor(world, descriptor);
        return finish_from(std::move(inferred), "parameter '" +
                           world.getProperty(parameter, "parameter_key") + "'");
    }
    if (ontology.isSubtypeOf(type, "LocalReadExpression")) {
        kg::EntityID binding = kg::INVALID_ENTITY;
        std::string property_error;
        if (!entity_property(world, expression, "local_binding",
                             "LocalBinding", binding, property_error)) {
            visiting.erase(expression);
            return fail(expression, type, property_error);
        }
        kg::EntityID value = kg::INVALID_ENTITY;
        if (!entity_property(world, binding, "binding_value", "Expression",
                             value, property_error)) {
            visiting.erase(expression);
            return fail(expression, type, property_error);
        }
        RuleStaticTypeResult inferred =
            infer_expression_impl(world, value, visiting);
        return finish_from(std::move(inferred), "local binding '" +
                           world.getProperty(binding, "binding_key") + "'");
    }
    if (ontology.isSubtypeOf(type, "LetExpression")) {
        kg::EntityID body = kg::INVALID_ENTITY;
        std::string property_error;
        if (!entity_property(world, expression, "let_body", "Expression",
                             body, property_error)) {
            visiting.erase(expression);
            return fail(expression, type, property_error);
        }
        RuleStaticTypeResult inferred =
            infer_expression_impl(world, body, visiting);
        return finish_from(std::move(inferred), "let body");
    }

    visiting.erase(expression);
    if (declared.family == RuleValueFamily::Entity ||
        declared.family == RuleValueFamily::EntityCollection) {
        return fail(expression, type, "expression entity " +
                    std::to_string(expression) + " (" + type +
                    ") has no ontology-class refinement source");
    }
    if (declared.family == RuleValueFamily::Function) {
        return fail(expression, type, "expression entity " +
                    std::to_string(expression) + " (" + type +
                    ") has no FunctionSignature refinement source");
    }
    return success(expression, type, declared.family);
}

}  // namespace

RuleStaticTypeResult infer_expression_static_type(
    const kg::KGModule& world, kg::EntityID expression) {
    std::unordered_set<kg::EntityID> visiting;
    return infer_expression_impl(world, expression, visiting);
}

RuleStaticTypeResult validate_expression_static_type(
    const kg::KGModule& world, kg::EntityID expression,
    RuleValueFamily expected) {
    RuleStaticTypeResult inferred =
        infer_expression_static_type(world, expression);
    if (!inferred.ok) return inferred;
    if (accepts(expected, inferred.type.family)) return inferred;
    inferred.ok = false;
    inferred.error = "expression entity " + std::to_string(expression) +
                     " (" + inferred.ontology_type + ") expected " +
                     rule_value_family_name(expected) + ", actual " +
                     rule_value_family_name(inferred.type.family);
    return inferred;
}

RuleStaticTypeResult infer_property_static_type(
    const kg::KGModule& world,
    const std::string& canonical_property_reference) {
    kg::QualifiedReference parsed;
    std::string parse_error;
    if (!kg::parse_qualified_reference(canonical_property_reference, parsed,
                                       parse_error) ||
        parsed.kind != kg::QualifiedReferenceKind::MetaProperty) {
        return fail(kg::INVALID_ENTITY, "",
                    "static property type requires a canonical "
                    "@@meta/property reference: " + parse_error);
    }
    const auto resolved = kg::resolve_qualified_reference(
        canonical_property_reference, world);
    if (!resolved.ok) {
        return fail(kg::INVALID_ENTITY, "", resolved.error);
    }
    const kg::EntityID property = resolved.entity;
    const std::string property_type = world.getType(property);
    if (property_type != "OntologyPropertyMeta") {
        return fail(property, property_type,
                    "qualified property reference resolved to " +
                    property_type + ", not OntologyPropertyMeta");
    }

    kg::EntityID kind = kg::INVALID_ENTITY;
    std::string relation_error;
    if (!exactly_one_related(world, property,
                             "ONTOLOGY_PROPERTY_VALUE_KIND", kind,
                             relation_error)) {
        return fail(property, property_type, relation_error);
    }
    const std::string kind_name = world.getProperty(kind, "ontology_name");
    if (kind_name == "boolean") {
        return success(property, property_type, RuleValueFamily::Boolean);
    }
    if (kind_name == "integer") {
        return success(property, property_type, RuleValueFamily::Integer);
    }
    if (kind_name == "float") {
        return success(property, property_type, RuleValueFamily::Float);
    }
    if (kind_name == "string") {
        return success(property, property_type, RuleValueFamily::String);
    }
    if (kind_name == "entity_ref") {
        kg::EntityID target = kg::INVALID_ENTITY;
        if (!exactly_one_related(world, property,
                                 "ONTOLOGY_PROPERTY_REFERENCE_TARGET",
                                 target, relation_error)) {
            return fail(property, property_type,
                        "entity-reference property '" +
                        canonical_property_reference +
                        "' has no exact class refinement: " +
                        relation_error);
        }
        return success(property, property_type, RuleValueFamily::Entity,
                       world.getProperty(target, "qualified_key"));
    }
    if (kind_name == "enum") {
        kg::EntityID enum_type = kg::INVALID_ENTITY;
        if (!exactly_one_related(world, property,
                                 "ONTOLOGY_PROPERTY_ENUM_TYPE", enum_type,
                                 relation_error)) {
            return fail(property, property_type, relation_error);
        }
        return fail(property, property_type,
                    "property '" + canonical_property_reference +
                    "' uses enum '" +
                    world.getProperty(enum_type, "ontology_name") +
                    "', an unsupported executable family");
    }
    if (kind_name == "datetime") {
        return fail(property, property_type,
                    "property '" + canonical_property_reference +
                    "' uses legacy datetime, an unsupported executable "
                    "family");
    }
    return fail(property, property_type,
                "property '" + canonical_property_reference +
                "' has unknown registry value kind '" + kind_name + "'");
}

}  // namespace logosphere::rules
