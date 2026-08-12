#include "logosphere/rules/planned_world.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/rules/outcome_executor.h"

#include <variant>

namespace logosphere::rules {
namespace {

bool same_ref(const kg::EntityRef& left, const kg::EntityRef& right) {
    return left.id == right.id && left.symbolic == right.symbolic;
}

// The pending create that named this symbolic ref, if any.
const kg::KGOpCreateEntity* pending_create(const OutcomePlan& plan,
                                           const kg::EntityRef& target) {
    if (!target.is_symbolic()) return nullptr;
    for (const auto& op : plan.ops) {
        const auto* create = std::get_if<kg::KGOpCreateEntity>(&op);
        if (create && create->as == target.symbolic) return create;
    }
    return nullptr;
}

}  // namespace

std::string PlannedWorld::property(const kg::EntityRef& target,
                                   const std::string& key) const {
    // Reverse: the newest write wins, and a read never sees its own op
    // because its op is not in the plan yet.
    for (auto it = plan_.ops.rbegin(); it != plan_.ops.rend(); ++it) {
        const auto* set = std::get_if<kg::KGOpSetProperty>(&*it);
        if (set && same_ref(set->target, target) && set->property == key) {
            return set->value;
        }
    }
    if (target.is_numeric()) return world_.getProperty(target.id, key);
    if (const auto* create = pending_create(plan_, target)) {
        for (const auto& [name, value] : create->properties) {
            if (name == key) return value;
        }
    }
    return {};
}

bool PlannedWorld::has_property(const kg::EntityRef& target,
                                const std::string& key) const {
    if (was_written(target, key)) return true;
    if (target.is_numeric()) return world_.hasProperty(target.id, key);
    return !property(target, key).empty();
}

std::string PlannedWorld::type_of(const kg::EntityRef& target) const {
    if (target.is_numeric()) {
        return world_.exists(target.id) ? world_.getType(target.id)
                                        : std::string();
    }
    const auto* create = pending_create(plan_, target);
    return create ? create->type : std::string();
}

bool PlannedWorld::exists(const kg::EntityRef& target) const {
    if (target.is_numeric()) return world_.exists(target.id);
    return pending_create(plan_, target) != nullptr;
}

std::vector<kg::EntityRef> PlannedWorld::related(
    const kg::EntityRef& from, const std::string& relation) const {
    std::vector<kg::EntityRef> out;
    // Committed first. A symbolic source has no committed edges,
    // because nothing that does not exist yet can already be related.
    if (from.is_numeric()) {
        for (const kg::EntityID id : world_.getRelated(from.id, relation)) {
            out.push_back(kg::EntityRef{id, ""});
        }
    }
    // Then pending, in plan order.
    for (const auto& op : plan_.ops) {
        const auto* set = std::get_if<kg::KGOpSetRelation>(&op);
        if (!set || set->relation != relation) continue;
        if (!same_ref(set->from, from)) continue;
        out.push_back(set->to);
    }
    return out;
}

bool PlannedWorld::was_written(const kg::EntityRef& target,
                               const std::string& key) const {
    for (const auto& op : plan_.ops) {
        const auto* set = std::get_if<kg::KGOpSetProperty>(&op);
        if (set && same_ref(set->target, target) && set->property == key) {
            return true;
        }
    }
    return false;
}

}  // namespace logosphere::rules
