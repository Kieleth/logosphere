#include "logosphere/rules/rule_fork.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace logosphere::rules {
namespace {

RuleForkResult fail(const std::string& error) {
    return RuleForkResult{false, error, kg::INVALID_ENTITY};
}

bool excluded_copy_property(const kg::PropertyDef& property) {
    return property.identifier || property.name == "origin_context" ||
           property.name == "fork_of" || property.name == "created_at" ||
           property.name == "updated_at";
}

}  // namespace

RuleForkResult fork_rule(kg::KGModule& world,
                         const RuleForkRequest& request) {
    if (!world.exists(request.source)) {
        return fail("fork source entity " + std::to_string(request.source) +
                    " does not exist");
    }
    if (!world.exists(request.destination_context)) {
        return fail("fork destination context " +
                    std::to_string(request.destination_context) +
                    " does not exist");
    }

    const kg::OntologyRegistry& ontology = world.getRegistry();
    const std::string source_type = world.getType(request.source);
    if (!ontology.isSubtypeOf(source_type, "Cited")) {
        return fail("fork source " + std::to_string(request.source) + " (" +
                    source_type + ") is not Cited rule content");
    }
    const std::string context_type =
        world.getType(request.destination_context);
    if (!ontology.isSubtypeOf(context_type, "RuntimeContext")) {
        return fail("fork destination " +
                    std::to_string(request.destination_context) + " (" +
                    context_type + ") is not a RuntimeContext");
    }
    if (!world.hasProperty(request.source, "origin_context")) {
        return fail("fork source " + std::to_string(request.source) +
                    " is missing required origin_context");
    }

    std::map<std::string, std::string> properties;
    for (const auto& [name, value] :
         world.getPropertiesWithPrefix(request.source, "")) {
        const kg::PropertyDef* definition =
            ontology.findProperty(source_type, name);
        if (!definition) {
            return fail("fork source " + std::to_string(request.source) +
                        " has undeclared property '" + name + "'");
        }
        if (!excluded_copy_property(*definition)) {
            properties.emplace(name, value);
        }
    }

    std::unordered_set<std::string> overridden;
    for (const auto& [name, value] : request.overrides) {
        if (!overridden.insert(name).second) {
            return fail("fork request repeats override '" + name + "'");
        }
        if (name == "origin_context" || name == "fork_of") {
            return fail("fork request cannot override provenance property '" +
                        name + "'");
        }
        properties[name] = value;
    }
    properties["origin_context"] =
        std::to_string(request.destination_context);
    properties["fork_of"] = std::to_string(request.source);

    std::vector<std::pair<std::string, std::string>> create_properties(
        properties.begin(), properties.end());
    constexpr const char* kForkAlias = "_logosphere_rule_fork";
    std::vector<kg::KGOp> plan;
    plan.emplace_back(
        kg::KGOpCreateEntity{source_type, std::move(create_properties),
                             kForkAlias});

    std::vector<std::string> relation_types;
    relation_types.reserve(ontology.relationTypes().size());
    for (const auto& [name, definition] : ontology.relationTypes()) {
        (void)definition;
        relation_types.push_back(name);
    }
    std::sort(relation_types.begin(), relation_types.end());
    for (const std::string& relation : relation_types) {
        auto targets = world.getRelated(request.source, relation);
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()),
                      targets.end());
        for (const kg::EntityID target : targets) {
            plan.emplace_back(kg::KGOpSetRelation{
                {kg::INVALID_ENTITY, kForkAlias}, relation, {target, ""}});
        }
    }

    kg::KGOpBatchReport batch;
    if (!kg::apply_kg_ops_atomically(plan, world, batch)) {
        return fail(batch.error);
    }
    const auto fork = batch.bindings.find(kForkAlias);
    if (fork == batch.bindings.end() || !world.exists(fork->second)) {
        return fail("fork transaction committed without binding its entity");
    }
    return RuleForkResult{true, "", fork->second};
}

}  // namespace logosphere::rules
