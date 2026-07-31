// Parses a Director JSON response into typed KGOps. The LLM
// returns one JSON document per round-end:
//
//   {
//     "thoughts": "Player is dominating, shrinking the arena.",
//     "ops": [
//       {"op": "create_entity", "type": "TrailSegment",
//        "properties": {...}},
//       {"op": "set_property", "target": "@arena",
//        "property": "arena_w", "value": "32"},
//       ...
//     ]
//   }
//
// Unknown ops + malformed entries become DirectorResponse::warnings
// and are dropped. A document that fails to parse at the JSON
// level surfaces in `parse_error`. The KGOp parsing itself lives
// in kg::parse_kg_ops; this thin layer just splits the LLM
// envelope (`thoughts` + `ops`).

#pragma once

#include "logosphere/kg/kg_ops.h"

#include <string>
#include <vector>

namespace logotron::director {

struct DirectorResponse {
    std::string               thoughts;
    std::vector<kg::KGOp>     kg_ops;
    std::vector<std::string>  warnings;
    std::string parse_error;
};

DirectorResponse parse_director_json(const std::string& json_str);

} // namespace logotron::director
