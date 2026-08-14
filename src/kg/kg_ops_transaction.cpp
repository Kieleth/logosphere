#include "logosphere/kg/kg_ops_transaction.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/finite_float.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_apply.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/ontology_validator.h"
#include "logosphere/kg/qualified_reference.h"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace kg {
namespace {

using SymbolTable = std::unordered_map<std::string, EntityID>;

struct UndoCreatedEntity { EntityID id = INVALID_ENTITY; };
struct UndoProperty {
    EntityID id = INVALID_ENTITY;
    std::string key;
    bool existed = false;
    std::string value;
};
struct UndoRelation {
    EntityID from = INVALID_ENTITY;
    std::string type;
    EntityID to = INVALID_ENTITY;
};

using Undo = std::variant<UndoCreatedEntity, UndoProperty, UndoRelation>;
using BufferedEvent = std::variant<logosphere::ontology::WorldEvent,
                                   logosphere::ontology::RelationEvent>;

bool relation_exists(const KGModule& kg, EntityID from,
                     const std::string& type, EntityID to) {
    const auto related = kg.getRelated(from, type);
    return std::find(related.begin(), related.end(), to) != related.end();
}

void buffer_property_event(std::vector<BufferedEvent>& events, EntityID id,
                           const std::string& key, const std::string& value,
                           const std::string& previous) {
    if (value == previous) return;
    logosphere::ontology::WorldEvent event;
    event.target_entity_id = std::to_string(id);
    event.event_type = "STATE_CHANGE";
    event.payload_keys = {"property", "value", "prev"};
    event.payload_values = {key, value, previous};
    events.emplace_back(std::move(event));
}

void buffer_relation_event(std::vector<BufferedEvent>& events,
                           const KGOpSetRelation& relation) {
    logosphere::ontology::RelationEvent event;
    event.source_entity_id = std::to_string(relation.from.id);
    event.target_entity_id = std::to_string(relation.to.id);
    event.relation_type = relation.relation;
    event.event_type = "RELATION_CREATED";
    events.emplace_back(std::move(event));
}

bool resolve_ref(EntityRef& ref, const KGModule& kg,
                 const SymbolTable& symbols, std::string& error) {
    if (!ref.is_symbolic()) return true;
    if (!ref.symbolic.empty() && ref.symbolic.front() == '@') {
        // EntityRef parsing strips the first '@' from every symbolic value.
        const QualifiedResolveResult resolved =
            resolve_qualified_reference("@" + ref.symbolic, kg);
        if (!resolved.ok) {
            error = resolved.error;
            return false;
        }
        ref.id = resolved.entity;
        ref.symbolic.clear();
        return true;
    }
    const auto found = symbols.find(ref.symbolic);
    if (found == symbols.end()) {
        error = "unresolved alias @" + ref.symbolic;
        return false;
    }
    ref.id = found->second;
    ref.symbolic.clear();
    return true;
}

bool resolve_ref_value(const OntologyRegistry& ontology, const KGModule& kg,
                       const std::string& type, const std::string& property,
                       std::string& value, const SymbolTable& symbols,
                       std::string& error) {
    if (value.size() < 2 || value.front() != '@') return true;
    const PropertyDef* definition = ontology.findProperty(type, property);
    if (!definition ||
        definition->value_kind != PropertyValueKind::EntityRef) return true;
    if (value.size() > 2 && value[1] == '@') {
        const QualifiedResolveResult resolved =
            resolve_qualified_reference(value, kg);
        if (!resolved.ok) {
            error = "property '" + property + "': " + resolved.error;
            return false;
        }
        value = std::to_string(resolved.entity);
        return true;
    }
    const auto found = symbols.find(value.substr(1));
    if (found == symbols.end()) {
        error = "property '" + property + "': unresolved alias " + value;
        return false;
    }
    value = std::to_string(found->second);
    return true;
}

bool resolve_op(KGOp& op, const KGModule& kg,
                const OntologyRegistry& ontology,
                const SymbolTable& symbols, std::string& error) {
    if (auto* create = std::get_if<KGOpCreateEntity>(&op)) {
        for (auto& [property, value] : create->properties) {
            if (!resolve_ref_value(ontology, kg, create->type, property,
                                   value, symbols, error)) {
                return false;
            }
        }
        return true;
    }
    if (auto* destroy = std::get_if<KGOpDestroyEntity>(&op)) {
        return resolve_ref(destroy->target, kg, symbols, error);
    }
    if (auto* set = std::get_if<KGOpSetProperty>(&op)) {
        if (!resolve_ref(set->target, kg, symbols, error)) return false;
        const std::string type = kg.getType(set->target.id);
        return type.empty() ||
               resolve_ref_value(ontology, kg, type, set->property,
                                 set->value,
                                 symbols, error);
    }
    if (auto* relation = std::get_if<KGOpSetRelation>(&op)) {
        return resolve_ref(relation->from, kg, symbols, error) &&
               resolve_ref(relation->to, kg, symbols, error);
    }
    if (auto* cinematic = std::get_if<KGOpPlayCinematic>(&op)) {
        return resolve_ref(cinematic->target, kg, symbols, error);
    }
    error = "unknown op variant";
    return false;
}

void canonicalize_float_value(const OntologyRegistry& ontology,
                              const std::string& type,
                              const std::string& property,
                              std::string& value) {
    const PropertyDef* definition = ontology.findProperty(type, property);
    if (!definition ||
        definition->value_kind != PropertyValueKind::Float) {
        return;
    }
    // Validation has already proved this is a complete finite float.
    // Normalize either signed source representation of zero before storage
    // and event publication.
    const auto parsed = parse_finite_binary64(value);
    if (parsed.ok && parsed.value == 0.0) value = "0";
}

void canonicalize_float_values(KGOp& op, const KGModule& kg,
                               const OntologyRegistry& ontology) {
    if (auto* create = std::get_if<KGOpCreateEntity>(&op)) {
        for (auto& [property, value] : create->properties) {
            canonicalize_float_value(ontology, create->type, property, value);
        }
        return;
    }
    if (auto* set = std::get_if<KGOpSetProperty>(&op)) {
        canonicalize_float_value(ontology, kg.getType(set->target.id),
                                 set->property, set->value);
    }
}

}  // namespace

bool apply_kg_ops_atomically(const std::vector<KGOp>& ops, KGModule& kg,
                             KGOpBatchReport& report,
                             MutationAuthority authority) {
    report = KGOpBatchReport{};
    const OntologyRegistry& ontology = kg.getRegistry();
    logosphere::EventBus* event_bus = kg.get_event_bus();
    std::vector<Undo> undo;
    std::vector<BufferedEvent> buffered_events;
    kg.set_event_bus(nullptr);
    ValidationOptions validation;
    validation.authority = authority;

    auto fail = [&](size_t index, const std::string& reason) {
        for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
            std::visit([&](const auto& action) {
                using T = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<T, UndoCreatedEntity>) {
                    kg.destroyEntity(action.id);
                } else if constexpr (std::is_same_v<T, UndoProperty>) {
                    if (action.existed) {
                        kg.setProperty(action.id, action.key, action.value);
                    } else {
                        kg.removeProperty(action.id, action.key);
                    }
                } else if constexpr (std::is_same_v<T, UndoRelation>) {
                    kg.destroyRelation(action.from, action.type, action.to);
                }
            }, *it);
        }
        kg.set_event_bus(event_bus);
        report = KGOpBatchReport{};
        report.ok = false;
        report.failed_op = static_cast<int>(index);
        report.error = "ops[" + std::to_string(index) + "]: " + reason;
        return false;
    };

    for (size_t index = 0; index < ops.size(); ++index) {
        if (std::holds_alternative<KGOpDestroyEntity>(ops[index])) {
            return fail(index,
                        "destroy_entity is not allowed in an additive "
                        "atomic batch");
        }
        if (std::holds_alternative<KGOpPlayCinematic>(ops[index])) {
            return fail(index,
                        "play_cinematic is not allowed in an additive "
                        "atomic batch");
        }

        const auto* original_create =
            std::get_if<KGOpCreateEntity>(&ops[index]);
        if (original_create && !original_create->as.empty() &&
            report.bindings.count(original_create->as)) {
            return fail(index, "duplicate alias binder @" +
                                   original_create->as);
        }

        KGOp op = ops[index];
        std::string error;
        if (!resolve_op(op, kg, ontology, report.bindings, error)) {
            return fail(index, error);
        }
        if (const auto validated =
                validate_kg_op(op, kg, ontology, validation);
            !validated.ok) {
            return fail(index, validated.reason);
        }
        canonicalize_float_values(op, kg, ontology);

        UndoProperty property_undo;
        bool has_property_undo = false;
        if (const auto* set = std::get_if<KGOpSetProperty>(&op)) {
            property_undo.id = set->target.id;
            property_undo.key = set->property;
            property_undo.existed = kg.hasProperty(set->target.id,
                                                   set->property);
            property_undo.value = kg.getProperty(set->target.id,
                                                 set->property);
            has_property_undo = true;
        }
        if (const auto* relation = std::get_if<KGOpSetRelation>(&op);
            relation && relation_exists(kg, relation->from.id,
                                        relation->relation,
                                        relation->to.id)) {
            return fail(index, "set_relation: relation already exists");
        }

        const ApplyResult applied = apply_kg_op(op, kg);
        if (!applied.ok) return fail(index, applied.reason);

        if (const auto* create = std::get_if<KGOpCreateEntity>(&op)) {
            undo.emplace_back(UndoCreatedEntity{applied.created_id});
            validation.constructing.insert(applied.created_id);
            for (const auto& [key, value] : create->properties) {
                buffer_property_event(buffered_events, applied.created_id,
                                      key, value, "");
            }
        } else if (const auto* set = std::get_if<KGOpSetProperty>(&op)) {
            undo.emplace_back(std::move(property_undo));
            if (has_property_undo) {
                const auto& old = std::get<UndoProperty>(undo.back());
                buffer_property_event(buffered_events, set->target.id,
                                      set->property, set->value, old.value);
            }
        } else if (const auto* relation =
                       std::get_if<KGOpSetRelation>(&op)) {
            undo.emplace_back(UndoRelation{relation->from.id,
                                           relation->relation,
                                           relation->to.id});
            buffer_relation_event(buffered_events, *relation);
        }

        report.created_ids.push_back(applied.created_id);
        if (original_create && !original_create->as.empty()) {
            report.bindings.emplace(original_create->as,
                                    applied.created_id);
        }
        ++report.ops_applied;
    }

    kg.set_event_bus(event_bus);
    if (event_bus) {
        for (auto& event : buffered_events) {
            std::visit([&](auto& concrete) {
                using T = std::decay_t<decltype(concrete)>;
                if constexpr (std::is_same_v<
                                  T, logosphere::ontology::WorldEvent>) {
                    event_bus->state_changes().emit(std::move(concrete));
                } else {
                    event_bus->relations().emit(std::move(concrete));
                }
            }, event);
        }
    }
    return true;
}

}  // namespace kg
