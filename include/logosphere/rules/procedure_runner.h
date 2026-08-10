#ifndef LOGOSPHERE_RULES_PROCEDURE_RUNNER_H
#define LOGOSPHERE_RULES_PROCEDURE_RUNNER_H

// Data-driven procedure orchestration.
//
// Procedure and ProcedureStep entities own order and routing. Exact named
// primitive handlers own game behavior. A primitive either advances with an
// optional route label, suspends for external input, completes the procedure,
// or fails with an actionable error.

#include "logosphere/kg/kg_types.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kg {
class KGModule;
}

namespace logosphere::rules {

struct ProcedureChoice {
    std::string key;
    std::string label;
    std::string detail;
};

enum class ProcedurePrimitiveStatus {
    ADVANCE,
    PENDING,
    COMPLETE,
    FAILED,
};

struct ProcedurePrimitiveResult {
    ProcedurePrimitiveStatus status = ProcedurePrimitiveStatus::FAILED;
    std::string route_label;
    std::string prompt;
    std::vector<ProcedureChoice> choices;
    std::string error;

    static ProcedurePrimitiveResult advance(
        const std::string& route_label = {});
    static ProcedurePrimitiveResult pending(
        const std::string& prompt, std::vector<ProcedureChoice> choices);
    static ProcedurePrimitiveResult complete();
    static ProcedurePrimitiveResult failed(const std::string& error);
};

struct ProcedurePrimitiveContext {
    kg::EntityID procedure = kg::INVALID_ENTITY;
    kg::EntityID step = kg::INVALID_ENTITY;
    kg::EntityID target = kg::INVALID_ENTITY;
    const kg::KGModule& kg;
    std::optional<std::string> input;
};

using ProcedurePrimitive =
    std::function<ProcedurePrimitiveResult(const ProcedurePrimitiveContext&)>;

struct ProcedurePrimitiveContract {
    std::string name;
    std::unordered_set<std::string> route_labels;
};

class ProcedurePrimitiveRegistry {
public:
    bool declare_primitive(const std::string& name,
                           const std::vector<std::string>& route_labels,
                           std::string& error);
    bool bind_primitive(const std::string& name,
                        ProcedurePrimitive primitive,
                        std::string& error);

    const ProcedurePrimitiveContract* contract(
        const std::string& name) const;
    const ProcedurePrimitive* handler(const std::string& name) const;

private:
    struct Entry {
        ProcedurePrimitiveContract contract;
        ProcedurePrimitive handler;
    };
    std::unordered_map<std::string, Entry> entries_;
};

enum class ProcedureStatus { PENDING, COMPLETED, FAILED };

struct ProcedureCursor {
    kg::EntityID procedure = kg::INVALID_ENTITY;
    kg::EntityID step = kg::INVALID_ENTITY;
    kg::EntityID target = kg::INVALID_ENTITY;
};

struct ProcedureResult {
    ProcedureStatus status = ProcedureStatus::FAILED;
    ProcedureCursor cursor;
    std::string prompt;
    std::vector<ProcedureChoice> choices;
    std::string error;
};

class ProcedureRunner {
public:
    ProcedureRunner(const kg::KGModule& kg,
                    const ProcedurePrimitiveRegistry& primitives,
                    size_t transition_limit = 1024)
        : kg_(kg), primitives_(primitives),
          transition_limit_(transition_limit) {}

    bool validate(kg::EntityID procedure, std::string& error) const;
    ProcedureResult start(kg::EntityID procedure,
                          kg::EntityID target) const;
    ProcedureResult resume(const ProcedureCursor& cursor,
                           const std::string& input) const;

private:
    ProcedureResult run(kg::EntityID procedure, kg::EntityID step,
                        kg::EntityID target,
                        std::optional<std::string> input) const;

    const kg::KGModule& kg_;
    const ProcedurePrimitiveRegistry& primitives_;
    size_t transition_limit_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_PROCEDURE_RUNNER_H
