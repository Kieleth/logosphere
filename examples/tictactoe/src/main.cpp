// Tic-Tac-Toe on Logosphere -- headless/console example.
//
// Demonstrates the framework's most portable slice: ontology extension,
// Knowledge Graph entities/relations/properties, and the KG's automatic
// event emission on the EventBus. No Engine, no IApplication, no
// rendering -- builds under every LOGOSPHERE_PROFILE (core/physics/full).
// See examples/tictactoe/README.md for how this maps onto the engine and
// how to grow it into a full windowed macOS example later.
//
// Game state lives in tictactoe.h, shared with tests/test_tictactoe.cpp.
//
// Usage:
//   ./tictactoe

#include "logosphere/kg/kg_module.h"
#include "logosphere/events/event_bus.h"
#include "generated/logosphere_ontology_registry.h"
#include "tictactoe_ontology_registry.h"
#include "tictactoe.h"

#include <iostream>
#include <string>

int main() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.extendOntology(tictactoe::ontology::registry());

    logosphere::EventBus bus;
    kg.set_event_bus(&bus);

    // The KG auto-emits on relations()/state_changes() whenever a move is
    // recorded -- games observe, they don't need to emit these by hand.
    // Same mechanism exercised by tests/test_relation_events.cpp and
    // tests/test_kg_setproperty_events.cpp.
    bus.relations().subscribe([](const logosphere::ontology::RelationEvent& e) {
        if (e.event_type == "RELATION_CREATED" && e.relation_type && *e.relation_type == "MANAGES") {
            std::cout << "[event] move recorded: entity " << (e.source_entity_id ? *e.source_entity_id : "?")
                      << " MANAGES entity " << (e.target_entity_id ? *e.target_entity_id : "?") << "\n";
        }
    });

    tictactoe::TicTacToe game(kg);

    auto print_board = [&game]() {
        for (int r = 0; r < tictactoe::kBoardSize; ++r) {
            for (int c = 0; c < tictactoe::kBoardSize; ++c) {
                std::string m = game.mark_at(r, c);
                std::cout << (m.empty() ? "." : m);
                if (c < tictactoe::kBoardSize - 1) std::cout << " | ";
            }
            std::cout << "\n";
            if (r < tictactoe::kBoardSize - 1) std::cout << "--+---+--\n";
        }
    };

    std::cout << "Tic-Tac-Toe on Logosphere\n";
    std::cout << "Enter moves as: row col (0-2 0-2). Ctrl-D to quit.\n\n";
    print_board();

    int current = 0;
    while (true) {
        std::cout << "\nPlayer " << game.symbol(current) << ", enter row col: ";
        int row = 0, col = 0;
        if (!(std::cin >> row >> col)) {
            std::cout << "\nNo more input, exiting.\n";
            return 0;
        }

        auto error = game.make_move(current, row, col);
        if (error) {
            std::cout << "Invalid move: " << *error << ". Try again.\n";
            continue;
        }

        print_board();

        auto winner = game.check_winner();
        if (!winner.empty()) {
            std::cout << "\nPlayer " << winner << " wins!\n";
            return 0;
        }
        if (game.is_full()) {
            std::cout << "\nIt's a draw.\n";
            return 0;
        }

        current = 1 - current;
    }
}
