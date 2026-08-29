// The primitives a stored procedure may name, and the exits a seed may
// route on.
//
// THE ROUTE CONTRACT. A ProcedureStep names a primitive; it never
// computes. The registry declares which names exist and, for each,
// which labels that primitive is allowed to report. A seed routing on
// a label the primitive cannot report is refused at verification, not
// discovered at run time. Name plus exits is the whole contract, and it
// is what keeps the flow data rather than a chain of calls.
//
// Three names, because the slice has three steps. None of them declares
// a route label: this slice does not branch, and declaring an exit
// nothing can report would be a contract that lies. Adding a branch is
// a label here and a StepRoute in the seed, in that order.

#ifndef VOYAGER_PROCEDURE_CATALOG_H
#define VOYAGER_PROCEDURE_CATALOG_H

#include "logosphere/rules/procedure_runner.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace voyager {

inline logosphere::rules::ProcedurePrimitiveRegistry
make_procedure_registry() {
    logosphere::rules::ProcedurePrimitiveRegistry registry;
    std::string error;
    const auto add = [&](const std::string& name,
                         const std::vector<std::string>& labels = {}) {
        if (!registry.declare_primitive(name, labels, error)) {
            throw std::logic_error(error);
        }
    };
    // Roll one throw per Characteristic the graph holds, in the order
    // the graph holds them, into the slot each one names.
    add("roll_characteristics");
    // Ask the referee where this person comes from, and keep the answer
    // on the character.
    add("narrate_background");
    // Build the legal set from the graph, let the referee narrow it,
    // and suspend until somebody picks.
    add("choose_career");
    // Offer the ways the book fixes for spending a season, suspend
    // until somebody picks, and record the year it costs.
    add("spend_season");
    // Break the season: the referee sets the situation and its chance
    // within the book's bounds, the engine draws, the referee tells
    // what it did, and the record is what stage will be counted from.
    add("face_moment");
    return registry;
}

}  // namespace voyager

#endif  // VOYAGER_PROCEDURE_CATALOG_H
