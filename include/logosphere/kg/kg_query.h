#ifndef LOGOSPHERE_KG_KG_QUERY_H
#define LOGOSPHERE_KG_KG_QUERY_H

// The Knowledge layer's query algebra: one small composable value
// type over KG + ontology registry that serves EVERY consumer class
// (meta-agent prompts, in-world AI loops, inspectors, save/replay).
// select (types | facets | property equals) -> project (property
// subset) -> limit; aggregation v1 is count.
//
// The same Query serves C++ callers (run_query -> rows) and
// serialization callers (render_query_json -> the stable JSON-lines
// snapshot format). Deterministic by construction: types iterate in
// the order given (facet-expanded types sorted, appended after),
// entities sorted by id, properties sorted by key.
//
// Design + boundaries: docs/KNOWLEDGE_LAYER.md.
// Deliberately absent: string query language, spatial selection (the
// KG has no spatial index), joins.

#include "logosphere/kg/kg_types.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace kg {

class KGModule;

struct Query {
    // --- selection ---
    // Entity types to include, iterated in this order. Unknown types
    // select nothing (matching findByType).
    std::vector<std::string> types;
    // Facets to include: expanded through the registry to the sorted
    // list of types carrying each facet, appended after `types`
    // (duplicates skipped). See OntologyRegistry::typesWithFacet.
    std::vector<std::string> facets;
    // Property predicates, ANDed. String equality (v1; typed
    // comparison waits for a consumer that needs it).
    struct PropEq {
        std::string key;
        std::string value;
    };
    std::vector<PropEq> where;

    // --- projection ---
    // Properties to include per row. Empty = all properties.
    std::vector<std::string> props;

    // --- budget ---
    // Max rows across the whole query (applied after selection order
    // above, so "first N of the types you asked for first").
    size_t limit = SIZE_MAX;
};

struct QueryRow {
    EntityID id = 0;
    std::string type;
    // Projected properties, sorted by key.
    std::vector<std::pair<std::string, std::string>> props;
};

// Execute. Rows ordered: query type order, then id ascending.
std::vector<QueryRow> run_query(const KGModule& kg, const Query& q);

// Aggregate v1: row count without materializing projections.
size_t count_query(const KGModule& kg, const Query& q);

// Render rows in the stable snapshot format — one JSON object per
// line: {"id":N,"type":"T","props":{"k":"v",...}}. Byte-compatible
// with the legacy serialize_kg_snapshot output.
std::string render_query_json(const std::vector<QueryRow>& rows);

}  // namespace kg

#endif  // LOGOSPHERE_KG_KG_QUERY_H
