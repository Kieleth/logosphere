// Hand-authored in place of generate_ontology.py output -- see the header
// comment in tictactoe_ontology_registry.h for why. DO NOT EDIT without
// also updating examples/tictactoe/schema/tictactoe.yaml.
#include "logosphere/kg/ontology_registry.h"

namespace tictactoe::ontology {

static kg::OntologyRegistry build_registry() {
    kg::OntologyRegistry reg;

    // Entity types
    reg.addEntityType("Board", "WorldEntity", false);
    reg.addEntityType("Cell", "WorldEntity", false);
    reg.addEntityType("Player", "Entity", false);

    // Inheritance chains (must be declared explicitly for isSubtypeOf()
    // to work -- see kg::OntologyRegistry::isSubtypeOf).
    reg.addAncestors("Board", {"Describable", "Entity", "Identifiable", "Spatial", "Statusable", "Temporal", "WorldEntity"});
    reg.addAncestors("Cell", {"Describable", "Entity", "Identifiable", "Spatial", "Statusable", "Temporal", "WorldEntity"});
    reg.addAncestors("Player", {"Describable", "Entity", "Identifiable", "Temporal"});

    // Event types (documentation parity with the schema; the game itself
    // drives win/draw detection synchronously rather than emitting these
    // as KG entities -- see examples/tictactoe/src/main.cpp)
    reg.addEntityType("TicTacToeEvent", "Event", false);

    // Relation types (documentation parity with the schema)
    reg.addEntityType("TicTacToeRelation", "Relation", false);

    // Custom relations
    reg.addRelationType("OCCUPIES", /*sources=*/{"Player"}, /*targets=*/{"Cell"});

    // Custom properties
    reg.addProperty("Cell", "row", "integer", false);
    reg.addProperty("Cell", "col", "integer", false);
    reg.addProperty("Cell", "mark", "string", false);
    reg.addProperty("Player", "symbol", "string", false);
    reg.addProperty("Player", "player_name", "string", false);

    return reg;
}

const kg::OntologyRegistry& registry() {
    static kg::OntologyRegistry reg = build_registry();
    return reg;
}

} // namespace tictactoe::ontology
