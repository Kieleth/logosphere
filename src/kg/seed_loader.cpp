#include "logosphere/kg/seed_loader.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_apply.h"
#include "logosphere/kg/kg_ops_parse.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/ontology_validator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace kg {

namespace {

using nlohmann::json;

// --- Envelope parsing ----------------------------------------------

// Every helper writes a full "where: why" message into err and
// returns false; parse_seed_envelope surfaces the first one as the
// fatal error. No field is defaulted - a seed file that omits its
// source pin or its layer is malformed, not minimal.

bool require_string(const json& obj, const char* key, const char* where,
                    std::string& out, std::string& err) {
    if (!obj.contains(key) || !obj.at(key).is_string() ||
        obj.at(key).get<std::string>().empty()) {
        err = std::string(where) + ": missing or empty string field '" +
              key + "'";
        return false;
    }
    out = obj.at(key).get<std::string>();
    return true;
}

// "@alias" -> "alias", refusing anything that is not an @-prefixed
// non-empty name (same strictness as the create op's "as" binder).
bool strip_alias(const std::string& s, const char* where,
                 std::string& out, std::string& err) {
    if (s.size() < 2 || s[0] != '@') {
        err = std::string(where) + ": expected '@alias', got '" + s + "'";
        return false;
    }
    out = s.substr(1);
    return true;
}

bool parse_invariants(const json& inv, SeedInvariants& out,
                      std::string& err) {
    if (!inv.is_object()) {
        err = "invariants: must be an object";
        return false;
    }
    for (auto it = inv.begin(); it != inv.end(); ++it) {
        const std::string& kind = it.key();
        const json& v = it.value();
        if (kind == "count_of_type") {
            if (!v.is_object()) {
                err = "invariants.count_of_type: must be an object of "
                      "type -> count";
                return false;
            }
            for (auto c = v.begin(); c != v.end(); ++c) {
                if (!c.value().is_number_integer() ||
                    c.value().get<long long>() < 0) {
                    err = "invariants.count_of_type." + c.key() +
                          ": count must be a non-negative integer";
                    return false;
                }
                out.count_of_type.emplace_back(c.key(),
                                               c.value().get<long long>());
            }
        } else if (kind == "unique_name_per_type") {
            if (!v.is_array()) {
                err = "invariants.unique_name_per_type: must be an array "
                      "of type names";
                return false;
            }
            for (const auto& t : v) {
                if (!t.is_string() || t.get<std::string>().empty()) {
                    err = "invariants.unique_name_per_type: entries must "
                          "be non-empty type names";
                    return false;
                }
                out.unique_name_per_type.push_back(t.get<std::string>());
            }
        } else if (kind == "band_coverage") {
            if (!v.is_object()) {
                err = "invariants.band_coverage: must be an object of "
                      "@alias -> [lo, hi]";
                return false;
            }
            for (auto b = v.begin(); b != v.end(); ++b) {
                SeedInvariants::BandCoverage cov;
                if (!strip_alias(b.key(), "invariants.band_coverage key",
                                 cov.alias, err)) {
                    return false;
                }
                const json& range = b.value();
                if (!range.is_array() || range.size() != 2 ||
                    !range[0].is_number_integer() ||
                    !range[1].is_number_integer()) {
                    err = "invariants.band_coverage." + b.key() +
                          ": range must be [lo, hi] integers";
                    return false;
                }
                cov.lo = range[0].get<long long>();
                cov.hi = range[1].get<long long>();
                if (cov.lo > cov.hi) {
                    err = "invariants.band_coverage." + b.key() +
                          ": lo > hi";
                    return false;
                }
                out.band_coverage.push_back(std::move(cov));
            }
        } else {
            err = "invariants: unknown assertion kind '" + kind +
                  "' (count_of_type, unique_name_per_type, "
                  "band_coverage)";
            return false;
        }
    }
    return true;
}

// --- Alias resolution ----------------------------------------------

using SymbolTable = std::unordered_map<std::string, EntityID>;

struct UndoCreatedEntity {
    EntityID id = INVALID_ENTITY;
};

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
    logosphere::ontology::WorldEvent evt;
    evt.target_entity_id = std::to_string(id);
    evt.event_type = "STATE_CHANGE";
    evt.payload_keys = {"property", "value", "prev"};
    evt.payload_values = {key, value, previous};
    events.emplace_back(std::move(evt));
}

void buffer_relation_event(std::vector<BufferedEvent>& events,
                           const KGOpSetRelation& relation) {
    logosphere::ontology::RelationEvent evt;
    evt.source_entity_id = std::to_string(relation.from.id);
    evt.target_entity_id = std::to_string(relation.to.id);
    evt.relation_type = relation.relation;
    evt.event_type = "RELATION_CREATED";
    events.emplace_back(std::move(evt));
}

bool resolve_ref(EntityRef& ref, const SymbolTable& syms,
                 std::string& err) {
    if (!ref.is_symbolic()) return true;
    auto it = syms.find(ref.symbolic);
    if (it == syms.end()) {
        err = "unresolved alias @" + ref.symbolic;
        return false;
    }
    ref.id = it->second;
    ref.symbolic.clear();
    return true;
}

// Resolve one "@alias" property value against the symbol table when
// the slot is an entity_ref on `type`. Non-ref slots pass through
// untouched; a ref slot with an unknown alias fails loudly.
bool resolve_ref_value(const OntologyRegistry& ont, const std::string& type,
                       const std::string& prop, std::string& value,
                       const SymbolTable& syms, std::string& err) {
    if (value.size() < 2 || value[0] != '@') return true;
    const PropertyDef* def = ont.findProperty(type, prop);
    if (!def || def->value_type != "entity_ref") return true;
    auto it = syms.find(value.substr(1));
    if (it == syms.end()) {
        err = "property '" + prop + "': unresolved alias " + value;
        return false;
    }
    value = std::to_string(it->second);
    return true;
}

// Resolve every alias in the op - targets and entity_ref property
// values - in place. Returns false with a reason on the first
// alias the symbol table does not know.
bool resolve_op(KGOp& op, const KGModule& kg, const OntologyRegistry& ont,
                const SymbolTable& syms, std::string& err) {
    if (auto* ce = std::get_if<KGOpCreateEntity>(&op)) {
        for (auto& [k, v] : ce->properties) {
            if (!resolve_ref_value(ont, ce->type, k, v, syms, err))
                return false;
        }
        return true;
    }
    if (auto* de = std::get_if<KGOpDestroyEntity>(&op)) {
        return resolve_ref(de->target, syms, err);
    }
    if (auto* sp = std::get_if<KGOpSetProperty>(&op)) {
        if (!resolve_ref(sp->target, syms, err)) return false;
        const std::string type = kg.getType(sp->target.id);
        if (!type.empty() &&
            !resolve_ref_value(ont, type, sp->property, sp->value, syms,
                               err)) {
            return false;
        }
        return true;
    }
    if (auto* sr = std::get_if<KGOpSetRelation>(&op)) {
        return resolve_ref(sr->from, syms, err) &&
               resolve_ref(sr->to, syms, err);
    }
    if (auto* pc = std::get_if<KGOpPlayCinematic>(&op)) {
        return resolve_ref(pc->target, syms, err);
    }
    err = "unknown op variant";
    return false;
}

}  // namespace

SeedParseResult parse_seed_envelope(const std::string& json_text) {
    SeedParseResult result;

    json root;
    try {
        root = json::parse(json_text);
    } catch (const std::exception& e) {
        result.error = std::string("invalid JSON: ") + e.what();
        return result;
    }
    if (!root.is_object()) {
        result.error = "seed file: root must be an object";
        return result;
    }

    // Unknown top-level keys are refused: a typo'd "invariant" key
    // silently asserting nothing is exactly the failure mode a seed
    // format exists to prevent.
    for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string& k = it.key();
        if (k != "source" && k != "layer" && k != "invariants" &&
            k != "ops") {
            result.error = "seed file: unknown top-level field '" + k + "'";
            return result;
        }
    }

    if (!root.contains("source") || !root.at("source").is_object()) {
        result.error = "seed file: missing 'source' object";
        return result;
    }
    if (!require_string(root.at("source"), "file", "source",
                        result.seed.source.file, result.error) ||
        !require_string(root.at("source"), "commit", "source",
                        result.seed.source.commit, result.error) ||
        !require_string(root, "layer", "seed file", result.seed.layer,
                        result.error)) {
        return result;
    }

    if (root.contains("invariants") &&
        !parse_invariants(root.at("invariants"), result.seed.invariants,
                          result.error)) {
        return result;
    }

    if (!root.contains("ops") || !root.at("ops").is_array()) {
        result.error = "seed file: missing 'ops' array";
        return result;
    }
    // Reuse the wire-grammar parser, but promote its per-op warnings
    // to fatal: a dropped op would skew every invariant below.
    KGOpParseResult ops = parse_kg_ops(json_text);
    if (!ops.parse_error.empty()) {
        result.error = ops.parse_error;
        return result;
    }
    if (!ops.warnings.empty()) {
        result.error = "seed file: " + ops.warnings.front();
        return result;
    }
    result.seed.ops = std::move(ops.ops);
    return result;
}

bool load_seed(const SeedEnvelope& seed, KGModule& kg,
               SeedLoadReport& report) {
    // Fresh report per load: a reused report must not leak bindings
    // (or partial state) from an earlier seed into this one.
    report = SeedLoadReport{};
    const OntologyRegistry& ont = kg.getRegistry();
    logosphere::EventBus* event_bus = kg.get_event_bus();
    std::vector<Undo> undo;
    std::vector<BufferedEvent> buffered_events;
    kg.set_event_bus(nullptr);

    auto fail = [&](size_t i, const std::string& reason) {
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
        report = SeedLoadReport{};
        report.ok = false;
        report.failed_op = static_cast<int>(i);
        report.error = "ops[" + std::to_string(i) + "]: " + reason;
        return false;
    };

    for (size_t i = 0; i < seed.ops.size(); ++i) {
        if (std::holds_alternative<KGOpDestroyEntity>(seed.ops[i])) {
            return fail(i, "destroy_entity is not allowed in an additive "
                           "seed layer");
        }
        if (std::holds_alternative<KGOpPlayCinematic>(seed.ops[i])) {
            return fail(i, "play_cinematic is not allowed in a seed file");
        }
        // A duplicate binder fails BEFORE the op touches the world -
        // the violating op must mutate nothing.
        const auto* ce = std::get_if<KGOpCreateEntity>(&seed.ops[i]);
        if (ce && !ce->as.empty() && report.bindings.count(ce->as)) {
            return fail(i, "duplicate alias binder @" + ce->as);
        }
        KGOp op = seed.ops[i];  // copy; aliases resolve in place
        std::string err;
        if (!resolve_op(op, kg, ont, report.bindings, err)) {
            return fail(i, err);
        }
        if (auto v = validate_kg_op(op, kg, ont); !v.ok) {
            return fail(i, v.reason);
        }

        UndoProperty property_undo;
        bool has_property_undo = false;
        if (const auto* sp = std::get_if<KGOpSetProperty>(&op)) {
            property_undo.id = sp->target.id;
            property_undo.key = sp->property;
            property_undo.existed = kg.hasProperty(sp->target.id,
                                                   sp->property);
            property_undo.value = kg.getProperty(sp->target.id,
                                                  sp->property);
            has_property_undo = true;
        }
        if (const auto* sr = std::get_if<KGOpSetRelation>(&op);
            sr && relation_exists(kg, sr->from.id, sr->relation,
                                  sr->to.id)) {
            return fail(i, "set_relation: relation already exists");
        }

        const ApplyResult applied = apply_kg_op(op, kg);
        if (!applied.ok) {
            return fail(i, applied.reason);
        }

        if (const auto* ce = std::get_if<KGOpCreateEntity>(&op)) {
            undo.emplace_back(UndoCreatedEntity{applied.created_id});
            for (const auto& [key, value] : ce->properties) {
                buffer_property_event(buffered_events, applied.created_id,
                                      key, value, "");
            }
        } else if (const auto* sp = std::get_if<KGOpSetProperty>(&op)) {
            undo.emplace_back(std::move(property_undo));
            if (has_property_undo) {
                const auto& old = std::get<UndoProperty>(undo.back());
                buffer_property_event(buffered_events, sp->target.id,
                                      sp->property, sp->value, old.value);
            }
        } else if (const auto* sr = std::get_if<KGOpSetRelation>(&op)) {
            undo.emplace_back(UndoRelation{sr->from.id, sr->relation,
                                           sr->to.id});
            buffer_relation_event(buffered_events, *sr);
        }

        report.created_ids.push_back(applied.created_id);
        if (ce && !ce->as.empty()) {
            report.bindings.emplace(ce->as, applied.created_id);
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
    return report.ok;
}

}  // namespace kg
