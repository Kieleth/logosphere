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
    add("choose_career");
    add("roll_qualification", {"passed", "failed"});
    add("roll_survival", {"passed", "failed"});
    add("roll_training");
    add("advance_term");
    add("choose_term_end", {"continue", "muster_out"});
    add("finish_character");
    return registry;
}

}  // namespace logovger

#endif  // LOGOVGER_CHARGEN_PROCEDURE_CATALOG_H
