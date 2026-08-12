#ifndef LOGOVGER_CHARGEN_PROCEDURE_CATALOG_H
#define LOGOVGER_CHARGEN_PROCEDURE_CATALOG_H

#include "logosphere/rules/procedure_runner.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace logovger {

inline logosphere::rules::ProcedurePrimitiveRegistry
make_chargen_procedure_registry() {
    logosphere::rules::ProcedurePrimitiveRegistry registry;
    std::string error;
    const auto add = [&](const std::string& name,
                         const std::vector<std::string>& labels = {}) {
        if (!registry.declare_primitive(name, labels, error)) {
            throw std::logic_error(error);
        }
    };
    add("generate_characteristics");
    add("choose_career", {"finish"});
    add("roll_qualification", {"passed", "failed"});
    // Turned away: the book gives exactly two ways to spend the term.
    add("draft_or_drifter", {"took_it"});
    add("basic_training");
    add("roll_survival", {"passed", "failed"});
    // Optional, once a term, and skipped entirely by the seven
    // careers the book says have no hierarchy.
    add("roll_commission");
    add("roll_advancement");
    add("roll_training");
    add("advance_term");
    // The years land at the end of a term, before the character
    // decides whether to serve another. Aging only costs you
    // something, unless the crisis it can trigger goes unpaid.
    add("roll_aging", {"died"});
    // A failed survival throw is death, unless the Referee allows the
    // mishap table instead. Either way the career ends; what is in
    // question is whether the character walks away from it.
    add("survival_mishap", {"died"});
    // Two of the three outcomes are not the player's to choose.
    add("roll_reenlistment", {"forced", "must_leave", "may_choose"});
    add("choose_term_end", {"continue", "muster_out"});
    // Benefits taken, the character is free to look for another
    // career or to stop.
    add("muster_out", {"continue"});
    add("finish_character");
    return registry;
}

}  // namespace logovger

#endif  // LOGOVGER_CHARGEN_PROCEDURE_CATALOG_H
