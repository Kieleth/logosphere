// Knowledge-layer query algebra over KG + ontology registry.
// Contract and determinism rules in kg_query.h; design in
// docs/KNOWLEDGE_LAYER.md.

#include "logosphere/kg/kg_query.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/ontology_serialize.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace kg {

namespace {

// Selection order: query types first (as given), then facet-expanded
// types (sorted per facet by the registry), duplicates dropped.
std::vector<std::string> selection_types(const KGModule& kg, const Query& q) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& t : q.types) {
        if (seen.insert(t).second) out.push_back(t);
    }
    for (const auto& f : q.facets) {
        for (const auto& t : kg.getRegistry().typesWithFacet(f)) {
            if (seen.insert(t).second) out.push_back(t);
        }
    }
    return out;
}

bool passes_where(const KGModule& kg, EntityID id, const Query& q) {
    for (const auto& pred : q.where) {
        if (kg.getProperty(id, pred.key) != pred.value) return false;
    }
    return true;
}

}  // namespace

std::vector<QueryRow> run_query(const KGModule& kg, const Query& q) {
    std::vector<QueryRow> rows;
    for (const auto& type : selection_types(kg, q)) {
        if (rows.size() >= q.limit) break;
        auto ids = kg.findByType(type);
        std::sort(ids.begin(), ids.end());
        for (auto id : ids) {
            if (rows.size() >= q.limit) break;
            if (!passes_where(kg, id, q)) continue;

            QueryRow row;
            row.id = id;
            row.type = type;
            if (q.props.empty()) {
                row.props = kg.getPropertiesWithPrefix(id, "");
            } else {
                for (const auto& key : q.props) {
                    auto v = kg.getProperty(id, key);
                    if (!v.empty()) row.props.emplace_back(key, v);
                }
            }
            std::sort(row.props.begin(), row.props.end(),
                      [](const auto& a, const auto& b) {
                          return a.first < b.first;
                      });
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

size_t count_query(const KGModule& kg, const Query& q) {
    size_t n = 0;
    for (const auto& type : selection_types(kg, q)) {
        for (auto id : kg.findByType(type)) {
            if (!passes_where(kg, id, q)) continue;
            if (++n >= q.limit) return n;
        }
    }
    return n;
}

std::string render_query_json(const std::vector<QueryRow>& rows) {
    std::ostringstream os;
    for (const auto& row : rows) {
        os << "{\"id\":" << row.id;
        os << ",\"type\":";
        detail::emit_json_string(os, row.type);
        os << ",\"props\":{";
        bool first = true;
        for (const auto& [k, v] : row.props) {
            if (!first) os << ',';
            first = false;
            detail::emit_json_string(os, k);
            os << ':';
            detail::emit_json_string(os, v);
        }
        os << "}}\n";
    }
    return os.str();
}

}  // namespace kg
