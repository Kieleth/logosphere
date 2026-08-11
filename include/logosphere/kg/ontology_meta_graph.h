#ifndef LOGOSPHERE_KG_ONTOLOGY_META_GRAPH_H
#define LOGOSPHERE_KG_ONTOLOGY_META_GRAPH_H

#include "logosphere/kg/kg_types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kg {

class KGModule;

struct OntologyMetaGraphReport {
    bool ok = false;
    std::string error;
    EntityID context = INVALID_ENTITY;
    size_t node_count = 0;
    std::vector<std::string> canonical_keys;
};

// Reflect the complete composed OntologyRegistry into an immutable KG graph.
// Candidate construction and validation are atomic. A successful rebuild
// replaces and removes the previously published graph.
bool materialize_ontology_meta_graph(KGModule& world,
                                     OntologyMetaGraphReport& report);

}  // namespace kg

#endif  // LOGOSPHERE_KG_ONTOLOGY_META_GRAPH_H
