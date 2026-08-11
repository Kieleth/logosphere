#ifndef LOGOSPHERE_RULES_RULE_PROGRAM_VALIDATOR_H
#define LOGOSPHERE_RULES_RULE_PROGRAM_VALIDATOR_H

#include "logosphere/kg/kg_types.h"

#include <string>
#include <vector>

namespace kg {
class KGModule;
}

namespace logosphere::rules {

struct RuleProgramValidationResult {
    bool ok = false;
    std::string error;
    // Populated for a valid root let block. Every binding appears once in
    // deterministic eager evaluation order, including unused bindings.
    std::vector<kg::EntityID> evaluation_order;
};

RuleProgramValidationResult validate_function_signature(
    const kg::KGModule& world, kg::EntityID signature);

RuleProgramValidationResult validate_argument_bindings(
    const kg::KGModule& world, kg::EntityID signature,
    const std::vector<kg::EntityID>& bindings);

RuleProgramValidationResult validate_let_expression(
    const kg::KGModule& world, kg::EntityID let_expression);

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_RULE_PROGRAM_VALIDATOR_H
