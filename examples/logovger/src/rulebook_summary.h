// What the rulebook in the graph actually holds, counted from the
// graph at the moment it is said.
//
// WHY THIS EXISTS. The opening line of the game used to be a sentence
// with three numbers typed into it: "24 careers, 48 throws, 150
// rollable-table rows, all cited." One of the three was right. The
// seeds carry 24 careers, 106 TaskChecks and 938 TableEntry rows
// across 148 RollableTables, and they got there by growing, which is
// something a string literal in a header cannot notice. Correcting the
// numbers to today's values would only restart the same clock; the
// first line a player reads has to be counted where it is printed.
//
// The claim "all cited" is not counted here because it is not a count:
// the ingestion verifier proves it at load, and the game refuses to
// start when it fails.

#ifndef LOGOVGER_RULEBOOK_SUMMARY_H
#define LOGOVGER_RULEBOOK_SUMMARY_H

#include "logosphere/kg/kg_module.h"

#include <string>

namespace logovger {

inline std::string rulebook_summary(const kg::KGModule& kg) {
    const auto count = [&kg](const char* type) {
        return std::to_string(kg.findByType(type).size());
    };
    return "The rulebook is in the graph: " + count("Career") +
           " careers, " + count("TaskCheck") + " throws, " +
           count("TableEntry") + " rollable-table rows across " +
           count("RollableTable") + " tables, all cited.";
}

}  // namespace logovger

#endif  // LOGOVGER_RULEBOOK_SUMMARY_H
