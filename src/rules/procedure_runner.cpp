#include "logosphere/rules/procedure_runner.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace logosphere::rules {
namespace {

using kg::EntityID;

struct ProcedureGraph {
    std::vector<EntityID> steps;
    std::unordered_map<EntityID, size_t> positions;
    std::unordered_map<
        EntityID, std::unordered_map<std::string, EntityID>> routes;
};

ProcedureResult failure(const std::string& error) {
    ProcedureResult result;
    result.error = error;
    return result;
}

bool parse_index(const std::string& value, int& out, std::string& error) {
    if (value.empty()) {
        error = "missing required step_index";
        return false;
    }
    try {
        size_t end = 0;
        const long long parsed = std::stoll(value, &end);
        if (end != value.size() || parsed < 0 ||
            parsed > std::numeric_limits<int>::max()) {
            throw std::invalid_argument("range");
        }
        out = static_cast<int>(parsed);
        return true;
    } catch (...) {
        error = "invalid step_index '" + value + "'";
        return false;
    }
}

bool parse_entity(const std::string& value, EntityID& out,
                  std::string& error) {
    if (value.empty()) {
        error = "missing required next_step";
        return false;
    }
    try {
        size_t end = 0;
        const unsigned long long parsed = std::stoull(value, &end);
        if (end != value.size() || parsed == 0 ||
            parsed > std::numeric_limits<EntityID>::max()) {
            throw std::invalid_argument("range");
        }
        out = static_cast<EntityID>(parsed);
        return true;
    } catch (...) {
        error = "invalid next_step entity reference '" + value + "'";
        return false;
    }
}

bool build_graph(const kg::KGModule& kg,
                 const ProcedurePrimitiveRegistry& primitives,
                 EntityID procedure, ProcedureGraph& graph,
                 std::string& error) {
    if (!kg.exists(procedure) ||
        !kg.getRegistry().isSubtypeOf(kg.getType(procedure), "Procedure")) {
        error = "procedure entity does not exist or is not a Procedure";
        return false;
    }

    const auto parts = kg.getRelated(procedure, "HAS_PART");
    if (parts.empty()) {
        error = "Procedure has no ProcedureStep parts";
        return false;
    }
    std::vector<std::pair<int, EntityID>> indexed;
    for (const EntityID part : parts) {
        const std::string type = kg.getType(part);
        if (!kg.getRegistry().isSubtypeOf(type, "ProcedureStep")) {
            error = "Procedure contains " + type +
                    ", not ProcedureStep";
            return false;
        }
        int index = 0;
        if (!parse_index(kg.getProperty(part, "step_index"), index,
                         error)) {
            return false;
        }
        const std::string primitive =
            kg.getProperty(part, "primitive_ref");
        if (primitive.empty()) {
            error = "ProcedureStep " + std::to_string(part) +
                    " has no primitive_ref";
            return false;
        }
        if (!primitives.contract(primitive)) {
            error = "ProcedureStep " + std::to_string(part) +
                    " names unknown primitive '" + primitive + "'";
            return false;
        }
        if (!primitives.handler(primitive)) {
            error = "procedure primitive '" + primitive +
                    "' has no bound handler";
            return false;
        }
        indexed.emplace_back(index, part);
    }
    std::sort(indexed.begin(), indexed.end());
    for (size_t index = 0; index < indexed.size(); ++index) {
        if (indexed[index].first != static_cast<int>(index)) {
            error = "Procedure step_index values must be contiguous from 0";
            return false;
        }
        graph.positions.emplace(indexed[index].second, index);
        graph.steps.push_back(indexed[index].second);
    }

    for (const EntityID step : graph.steps) {
        const std::string primitive =
            kg.getProperty(step, "primitive_ref");
        const auto* contract = primitives.contract(primitive);
        auto& routes = graph.routes[step];
        for (const EntityID part : kg.getRelated(step, "HAS_PART")) {
            const std::string type = kg.getType(part);
            if (!kg.getRegistry().isSubtypeOf(type, "StepRoute")) {
                error = "ProcedureStep contains " + type +
                        ", not StepRoute";
                return false;
            }
            const std::string label = kg.getProperty(part, "route_label");
            if (label.empty()) {
                error = "StepRoute has no route_label";
                return false;
            }
            if (!contract->route_labels.count(label)) {
                error = "StepRoute label '" + label +
                        "' is not declared by primitive '" + primitive +
                        "'";
                return false;
            }
            EntityID next = kg::INVALID_ENTITY;
            if (!parse_entity(kg.getProperty(part, "next_step"), next,
                              error)) {
                return false;
            }
            if (!graph.positions.count(next)) {
                error = "StepRoute '" + label +
                        "' points outside Procedure";
                return false;
            }
            if (!routes.emplace(label, next).second) {
                error = "ProcedureStep has duplicate route_label '" + label +
                        "'";
                return false;
            }
        }
    }
    return true;
}

bool valid_pending(const ProcedurePrimitiveResult& primitive,
                   std::string& error) {
    if (primitive.prompt.empty()) {
        error = "pending primitive must provide a non-empty prompt";
        return false;
    }
    if (primitive.choices.empty()) {
        error = "pending primitive must provide at least one choice";
        return false;
    }
    std::unordered_set<std::string> keys;
    for (const auto& choice : primitive.choices) {
        if (choice.key.empty() || choice.label.empty()) {
            error = "pending primitive choices require key and label";
            return false;
        }
        if (!keys.insert(choice.key).second) {
            error = "pending primitive has duplicate choice key '" +
                    choice.key + "'";
            return false;
        }
    }
    return true;
}

}  // namespace

ProcedurePrimitiveResult ProcedurePrimitiveResult::advance(
    const std::string& route_label) {
    ProcedurePrimitiveResult result;
    result.status = ProcedurePrimitiveStatus::ADVANCE;
    result.route_label = route_label;
    return result;
}

ProcedurePrimitiveResult ProcedurePrimitiveResult::pending(
    const std::string& prompt, std::vector<ProcedureChoice> choices) {
    ProcedurePrimitiveResult result;
    result.status = ProcedurePrimitiveStatus::PENDING;
    result.prompt = prompt;
    result.choices = std::move(choices);
    return result;
}

ProcedurePrimitiveResult ProcedurePrimitiveResult::complete() {
    ProcedurePrimitiveResult result;
    result.status = ProcedurePrimitiveStatus::COMPLETE;
    return result;
}

ProcedurePrimitiveResult ProcedurePrimitiveResult::failed(
    const std::string& error) {
    ProcedurePrimitiveResult result;
    result.error = error;
    return result;
}

bool ProcedurePrimitiveRegistry::declare_primitive(
    const std::string& name, const std::vector<std::string>& route_labels,
    std::string& error) {
    error.clear();
    if (name.empty()) {
        error = "procedure primitive name cannot be empty";
        return false;
    }
    if (entries_.count(name)) {
        error = "procedure primitive '" + name + "' is already declared";
        return false;
    }
    Entry entry;
    entry.contract.name = name;
    for (const auto& label : route_labels) {
        if (label.empty()) {
            error = "procedure primitive route labels cannot be empty";
            return false;
        }
        if (!entry.contract.route_labels.insert(label).second) {
            error = "procedure primitive '" + name +
                    "' has duplicate route label '" + label + "'";
            return false;
        }
    }
    entries_.emplace(name, std::move(entry));
    return true;
}

bool ProcedurePrimitiveRegistry::bind_primitive(
    const std::string& name, ProcedurePrimitive primitive,
    std::string& error) {
    error.clear();
    const auto found = entries_.find(name);
    if (found == entries_.end()) {
        error = "procedure primitive '" + name + "' is not declared";
        return false;
    }
    if (!primitive) {
        error = "procedure primitive '" + name + "' handler is empty";
        return false;
    }
    if (found->second.handler) {
        error = "procedure primitive '" + name +
                "' handler is already bound";
        return false;
    }
    found->second.handler = std::move(primitive);
    return true;
}

const ProcedurePrimitiveContract* ProcedurePrimitiveRegistry::contract(
    const std::string& name) const {
    const auto found = entries_.find(name);
    return found == entries_.end() ? nullptr : &found->second.contract;
}

const ProcedurePrimitive* ProcedurePrimitiveRegistry::handler(
    const std::string& name) const {
    const auto found = entries_.find(name);
    if (found == entries_.end() || !found->second.handler) return nullptr;
    return &found->second.handler;
}

bool ProcedureRunner::validate(EntityID procedure, std::string& error) const {
    ProcedureGraph graph;
    return build_graph(kg_, primitives_, procedure, graph, error);
}

ProcedureResult ProcedureRunner::start(EntityID procedure,
                                       EntityID target) const {
    if (!kg_.exists(target)) return failure("procedure target does not exist");
    ProcedureGraph graph;
    std::string error;
    if (!build_graph(kg_, primitives_, procedure, graph, error)) {
        return failure(error);
    }
    return run(procedure, graph.steps.front(), target, std::nullopt);
}

ProcedureResult ProcedureRunner::resume(const ProcedureCursor& cursor,
                                        const std::string& input) const {
    if (cursor.procedure == kg::INVALID_ENTITY ||
        cursor.step == kg::INVALID_ENTITY ||
        cursor.target == kg::INVALID_ENTITY) {
        return failure("procedure cursor is incomplete");
    }
    if (input.empty()) return failure("procedure resume input is empty");
    return run(cursor.procedure, cursor.step, cursor.target, input);
}

ProcedureResult ProcedureRunner::run(
    EntityID procedure, EntityID step, EntityID target,
    std::optional<std::string> input) const {
    if (!kg_.exists(target)) return failure("procedure target does not exist");
    ProcedureGraph graph;
    std::string error;
    if (!build_graph(kg_, primitives_, procedure, graph, error)) {
        return failure(error);
    }
    if (!graph.positions.count(step)) {
        return failure("procedure cursor step is outside Procedure");
    }

    EntityID current = step;
    for (size_t transitions = 0; transitions < transition_limit_;
         ++transitions) {
        const std::string primitive_name =
            kg_.getProperty(current, "primitive_ref");
        const auto* contract = primitives_.contract(primitive_name);
        const auto* handler = primitives_.handler(primitive_name);
        ProcedurePrimitiveResult primitive;
        try {
            primitive = (*handler)(ProcedurePrimitiveContext{
                procedure, current, target, kg_, std::move(input)});
        } catch (const std::exception& exception) {
            return failure("procedure primitive '" + primitive_name +
                           "' threw: " + exception.what());
        } catch (...) {
            return failure("procedure primitive '" + primitive_name +
                           "' threw an unknown exception");
        }
        input.reset();

        if (!primitive.route_label.empty() &&
            !contract->route_labels.count(primitive.route_label)) {
            return failure("procedure primitive '" + primitive_name +
                           "' returned undeclared route label '" +
                           primitive.route_label + "'");
        }
        if (primitive.status == ProcedurePrimitiveStatus::FAILED) {
            return failure(
                primitive.error.empty()
                    ? "procedure primitive '" + primitive_name +
                          "' failed without a reason"
                    : primitive.error);
        }
        if (primitive.status == ProcedurePrimitiveStatus::PENDING) {
            if (!primitive.route_label.empty()) {
                return failure("pending procedure primitive '" +
                               primitive_name +
                               "' cannot return a route label");
            }
            if (!valid_pending(primitive, error)) return failure(error);
            ProcedureResult result;
            result.status = ProcedureStatus::PENDING;
            result.cursor = {procedure, current, target};
            result.prompt = std::move(primitive.prompt);
            result.choices = std::move(primitive.choices);
            return result;
        }
        if (primitive.status == ProcedurePrimitiveStatus::COMPLETE) {
            if (!primitive.route_label.empty()) {
                return failure("completed procedure primitive '" +
                               primitive_name +
                               "' cannot return a route label");
            }
            ProcedureResult result;
            result.status = ProcedureStatus::COMPLETED;
            return result;
        }

        const auto route_map = graph.routes.find(current);
        if (!primitive.route_label.empty() && route_map != graph.routes.end()) {
            const auto route = route_map->second.find(primitive.route_label);
            if (route != route_map->second.end()) {
                current = route->second;
                continue;
            }
        }
        const size_t position = graph.positions.at(current);
        if (position + 1 == graph.steps.size()) {
            ProcedureResult result;
            result.status = ProcedureStatus::COMPLETED;
            return result;
        }
        current = graph.steps[position + 1];
    }
    return failure("procedure transition limit exceeded without suspension "
                   "or completion");
}

}  // namespace logosphere::rules
