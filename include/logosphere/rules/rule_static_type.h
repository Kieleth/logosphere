#ifndef LOGOSPHERE_RULES_RULE_STATIC_TYPE_H
#define LOGOSPHERE_RULES_RULE_STATIC_TYPE_H

#include "logosphere/kg/kg_types.h"

#include <string>

namespace kg {
class KGModule;
}

namespace logosphere::rules {

enum class RuleValueFamily {
    Boolean,
    Numeric,
    Integer,
    Float,
    String,
    Entity,
    BooleanCollection,
    IntegerCollection,
    FloatCollection,
    StringCollection,
    EntityCollection,
    Function,
    OutcomePlan,
};

const char* rule_value_family_name(RuleValueFamily family);

struct RuleStaticType {
    RuleValueFamily family = RuleValueFamily::Boolean;
    // Canonical @@meta/class path for Entity and EntityCollection values.
    std::string refinement;
    // Exact FunctionSignature identity for Function values.
    kg::EntityID function_signature = kg::INVALID_ENTITY;
};

struct RuleStaticTypeResult {
    bool ok = false;
    std::string error;
    kg::EntityID node = kg::INVALID_ENTITY;
    std::string ontology_type;
    RuleStaticType type;
};

struct RuleTypeCompatibilityResult {
    bool ok = false;
    std::string error;
    RuleStaticType expected;
    RuleStaticType actual;
};

RuleTypeCompatibilityResult validate_rule_static_type_assignability(
    const RuleStaticType& expected, const RuleStaticType& actual);

// Infer the type stored by one concrete ValueTypeDescriptor.
RuleStaticTypeResult infer_value_type_descriptor(
    const kg::KGModule& world, kg::EntityID descriptor);

// Check assignment from actual into expected. Numeric accepts Integer or
// Float. Entity and collection refinements are exact in the active slice;
// Function requires the same signature identity.
RuleTypeCompatibilityResult validate_type_descriptor_assignability(
    const kg::KGModule& world, kg::EntityID expected_descriptor,
    kg::EntityID actual_descriptor);

// Infer the broad result family fixed by a concrete Expression subclass and
// propagate descriptor refinements through parameter, local, and let reads.
RuleStaticTypeResult infer_expression_static_type(
    const kg::KGModule& world, kg::EntityID expression);

RuleStaticTypeResult validate_expression_static_type(
    const kg::KGModule& world, kg::EntityID expression,
    RuleValueFamily expected);

// Infer a property read's family from one canonical direct-property meta
// entity. Enum and legacy datetime properties fail as unsupported executable
// families and are never reinterpreted as String.
RuleStaticTypeResult infer_property_static_type(
    const kg::KGModule& world,
    const std::string& canonical_property_reference);

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_RULE_STATIC_TYPE_H
