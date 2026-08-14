#ifndef LOGOSPHERE_RULES_RULE_FORK_H
#define LOGOSPHERE_RULES_RULE_FORK_H

#include "logosphere/kg/kg_types.h"

#include <string>
#include <utility>
#include <vector>

namespace kg {
class KGModule;
}

namespace logosphere::rules {

struct RuleForkRequest {
    kg::EntityID source = kg::INVALID_ENTITY;
    kg::EntityID destination_context = kg::INVALID_ENTITY;
    std::string entity_key;
    std::vector<std::pair<std::string, std::string>> overrides;
};

struct RuleForkResult {
    bool ok = false;
    std::string error;
    kg::EntityID entity = kg::INVALID_ENTITY;
};

// Copy one Contextual/Cited rule into a RuntimeContext. The copy keeps every
// ordinary property and outgoing relation, gives the copy the caller-supplied
// portable identity in the destination context, replaces only named
// overrides, records immediate fork lineage, and commits through the atomic
// validated KG-op boundary. The source is read only.
RuleForkResult fork_rule(kg::KGModule& kg, const RuleForkRequest& request);

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_RULE_FORK_H
