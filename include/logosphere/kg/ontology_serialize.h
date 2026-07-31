#ifndef LOGOSPHERE_KG_ONTOLOGY_SERIALIZE_H
#define LOGOSPHERE_KG_ONTOLOGY_SERIALIZE_H

// Compact JSON serialisers for ontology + KG snapshots, intended
// for embedding in LLM prompts. Generic engine helpers — any game
// that wants its LLM director / agent to "see" the ontology + KG
// state uses these.
//
// The output is dense JSON (no pretty-printing) so a couple
// hundred entities fit comfortably in any modern model's context
// window. Format is stable and documented in line with each
// function so prompt authors can describe it to the model.

#include <sstream>
#include <string>
#include <vector>

namespace kg {

namespace detail {
// JSON string escaper shared by the serializers and the query
// renderer (kg_query.cpp). Implementation in ontology_serialize.cpp.
void emit_json_string(std::ostringstream& os, const std::string& s);
}  // namespace detail

class OntologyRegistry;
class KGModule;

// Serialise a slice of the ontology — every requested type with
// its parent, abstract flag, and DIRECT properties (not
// inherited). Inherited properties are reachable via the parent
// chain; pass ancestors explicitly if the LLM should see them
// expanded.
//
// Output format (one type, expanded for readability):
//   {
//     "Cycle": {
//       "parent": "PhysicsEntity",
//       "abstract": false,
//       "properties": {
//         "max_speed": {"type": "float"},
//         "x":         {"type": "float", "required": true}
//       }
//     },
//     ...
//   }
//
// Real output is single-line JSON with no extra whitespace.
// Unknown types are silently skipped (a warning is the caller's
// concern — the snapshot is best-effort context for the model).
std::string serialize_ontology_slice(
    const OntologyRegistry& registry,
    const std::vector<std::string>& type_names);

// KG state snapshots live in the query algebra now:
// run_query + render_query_json (logosphere/kg/kg_query.h) emit the
// same stable one-entity-per-line JSON with facet selection,
// projection, and budgets. serialize_kg_snapshot (a type-list
// wrapper) was deleted 2026-07-30 once its last consumer migrated.

}  // namespace kg

#endif  // LOGOSPHERE_KG_ONTOLOGY_SERIALIZE_H
