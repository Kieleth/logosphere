#ifndef LOGOSPHERE_KG_KG_OPS_PARSE_H
#define LOGOSPHERE_KG_KG_OPS_PARSE_H

// JSON → KGOp parser. Reads the "ops" array from a Director-style
// LLM response. Permissive but loud:
//   - unknown op types become warnings (the op is dropped)
//   - missing required fields become warnings
//   - bad top-level JSON becomes a fatal parse_error
//
// Engine-side helper. Generic — no game-specific keywords.

#include "logosphere/kg/kg_ops.h"

#include <string>
#include <vector>

namespace kg {

struct KGOpParseResult {
    std::vector<KGOp>        ops;
    std::vector<std::string> warnings;
    std::string              parse_error;  // empty on success
};

// Parse the LLM response payload. Accepts:
//   - the whole response object: {"ops":[...]}  (preferred)
//   - or just the array: [...]
KGOpParseResult parse_kg_ops(const std::string& json);

}  // namespace kg

#endif  // LOGOSPHERE_KG_KG_OPS_PARSE_H
