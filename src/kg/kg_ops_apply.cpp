#include "logosphere/kg/kg_ops_apply.h"

#include "logosphere/kg/kg_module.h"

#include <variant>
#include <string>

namespace kg {

namespace {

ApplyResult fail(const std::string& reason) {
    return ApplyResult{false, reason, INVALID_ENTITY};
}

ApplyResult apply_create(const KGOpCreateEntity& op, KGModule& kg) {
    EntityID id = kg.createEntity(op.type);
    if (id == INVALID_ENTITY) {
        return fail("create_entity: KG rejected '" + op.type + "'");
    }
    for (const auto& [k, v] : op.properties) {
        kg.setProperty(id, k, v);
    }
    ApplyResult r;
    r.created_id = id;
    return r;
}

ApplyResult apply_destroy(const KGOpDestroyEntity& op, KGModule& kg) {
    if (op.target.is_symbolic()) {
        return fail("destroy_entity: unresolved symbolic ref");
    }
    if (!kg.exists(op.target.id)) {
        return fail("destroy_entity: entity " + std::to_string(op.target.id) +
                    " does not exist");
    }
    kg.destroyEntity(op.target.id);
    return ApplyResult{};
}

ApplyResult apply_set_property(const KGOpSetProperty& op, KGModule& kg) {
    if (op.target.is_symbolic()) {
        return fail("set_property: unresolved symbolic ref");
    }
    if (!kg.exists(op.target.id)) {
        return fail("set_property: entity " + std::to_string(op.target.id) +
                    " does not exist");
    }
    kg.setProperty(op.target.id, op.property, op.value);
    return ApplyResult{};
}

ApplyResult apply_set_relation(const KGOpSetRelation& op, KGModule& kg) {
    if (op.from.is_symbolic() || op.to.is_symbolic()) {
        return fail("set_relation: unresolved symbolic ref");
    }
    if (!kg.exists(op.from.id) || !kg.exists(op.to.id)) {
        return fail("set_relation: from/to entity missing");
    }
    kg.createRelation(op.from.id, op.relation, op.to.id);
    return ApplyResult{};
}

}  // namespace

ApplyResult apply_kg_op(const KGOp& op, KGModule& kg) {
    return std::visit([&](const auto& concrete) -> ApplyResult {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, KGOpCreateEntity>)  return apply_create(concrete, kg);
        if constexpr (std::is_same_v<T, KGOpDestroyEntity>) return apply_destroy(concrete, kg);
        if constexpr (std::is_same_v<T, KGOpSetProperty>)   return apply_set_property(concrete, kg);
        if constexpr (std::is_same_v<T, KGOpSetRelation>)   return apply_set_relation(concrete, kg);
        if constexpr (std::is_same_v<T, KGOpPlayCinematic>) {
            // Apply is a no-op for cinematics — the visual lives
            // in the host's cinematic registry, fired through the
            // MutationPlaybackRegistry. Returning ok lets the
            // host's apply loop dispatch begin_play normally.
            return ApplyResult{};
        }
        return fail("unknown op variant");
    }, op);
}

}  // namespace kg
