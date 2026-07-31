// Tic-Tac-Toe game logic test — exercises the shared TicTacToe class
// (examples/tictactoe/src/tictactoe.h) the same way the console game
// does: build the KG, extend with the tictactoe ontology, play scripted
// moves, and assert on win/draw/rejection behavior.
//
// Usage:
//   ./build/test_tictactoe

#include "logosphere/kg/kg_module.h"
#include "generated/logosphere_ontology_registry.h"
#include "tictactoe_ontology_registry.h"
#include "tictactoe.h"

#include <iostream>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

void init(kg::KGModule& kg) {
    kg.setMode(kg::KGMode::MINIMAL);
    kg.extendOntology(tictactoe::ontology::registry());
}

} // namespace

void test_ontology_extension_adds_tictactoe_types() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);

    ASSERT(kg.getRegistry().hasEntityType("Board"), "Board type registered");
    ASSERT(kg.getRegistry().hasEntityType("Cell"), "Cell type registered");
    ASSERT(kg.getRegistry().hasEntityType("Player"), "Player type registered");
    // The game reuses the engine's relations rather than declaring its
    // own: generate_registry.py derives relation types solely from the
    // engine's WorldRelationType enum, so a game-declared relation would
    // be dropped the next time the registry is regenerated.
    ASSERT(kg.getRegistry().hasRelationType("MANAGES"), "MANAGES relation available");
    ASSERT(kg.getRegistry().isValidRelation("MANAGES", "Player", "Cell"),
           "MANAGES(Player, Cell) is valid");
    ASSERT(kg.getRegistry().hasRelationType("HAS_PART"), "HAS_PART relation available");
    ASSERT(kg.getRegistry().isSubtypeOf("Board", "WorldEntity"), "Board is a WorldEntity");
    ASSERT(kg.getRegistry().isSubtypeOf("Cell", "WorldEntity"), "Cell is a WorldEntity");
}

// Regression guard. The registry used to be hand-authored and declared a
// game-specific OCCUPIES relation, which scripts/generate_registry.py
// would never re-emit -- so regenerating the ontology silently broke the
// game. Every relation the game uses must exist in a freshly generated
// registry, which means it must come from the engine's own set.
void test_game_only_uses_engine_relations() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    game.make_move(0, 0, 0);
    game.make_move(1, 1, 1);

    for (const char* rel : {"HAS_PART", "MANAGES"}) {
        ASSERT(kg.getRegistry().hasRelationType(rel),
               "relation survives ontology regeneration");
    }
    // Nothing may depend on a relation the generator does not emit.
    ASSERT(!kg.getRegistry().hasRelationType("OCCUPIES"),
           "no hand-authored relation lingers in the registry");
}

void test_board_setup_creates_nine_cells_and_two_players() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    ASSERT(kg.exists(game.board()), "board entity exists");
    ASSERT(kg.getType(game.board()) == "Board", "board has type Board");

    for (int r = 0; r < tictactoe::kBoardSize; ++r) {
        for (int c = 0; c < tictactoe::kBoardSize; ++c) {
            ASSERT(kg.exists(game.cell(r, c)), "cell exists");
            ASSERT(game.mark_at(r, c).empty(), "cell starts unmarked");
        }
    }

    auto board_parts = kg.getRelated(game.board(), "HAS_PART");
    ASSERT(board_parts.size() == 9, "board HAS_PART nine cells");

    ASSERT(game.symbol(0) == "X", "player 0 is X");
    ASSERT(game.symbol(1) == "O", "player 1 is O");
}

void test_move_marks_cell_and_records_occupies_relation() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    auto err = game.make_move(/*player=*/0, 1, 1);
    ASSERT(!err.has_value(), "valid move succeeds");
    ASSERT(game.mark_at(1, 1) == "X", "cell marked with X");

    auto claimed = kg.getRelated(game.player(0), "MANAGES");
    ASSERT(claimed.size() == 1, "player has one MANAGES relation");
    ASSERT(claimed[0] == game.cell(1, 1), "MANAGES points at the marked cell");
}

void test_move_rejects_out_of_range() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    auto err = game.make_move(0, 3, 0);
    ASSERT(err.has_value(), "out-of-range move rejected");
    ASSERT(kg.getRelated(game.player(0), "MANAGES").empty(), "no relation created for rejected move");
}

void test_move_rejects_occupied_cell() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    ASSERT(!game.make_move(0, 0, 0).has_value(), "first move on cell succeeds");
    auto err = game.make_move(1, 0, 0);
    ASSERT(err.has_value(), "second move on same cell rejected");
    ASSERT(game.mark_at(0, 0) == "X", "cell still holds the first mark");
}

void test_horizontal_win() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    // X: (0,0) (0,1) (0,2) top row.  O: (1,0) (1,1) elsewhere.
    game.make_move(0, 0, 0);
    game.make_move(1, 1, 0);
    game.make_move(0, 0, 1);
    game.make_move(1, 1, 1);
    ASSERT(game.check_winner().empty(), "no winner before third mark");
    game.make_move(0, 0, 2);

    ASSERT(game.check_winner() == "X", "X wins top row");
    ASSERT(!game.is_full(), "board not full when won early");
}

void test_diagonal_win() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    // O: (0,0) (1,1) (2,2) main diagonal. X: elsewhere.
    game.make_move(1, 0, 0);
    game.make_move(0, 0, 1);
    game.make_move(1, 1, 1);
    game.make_move(0, 0, 2);
    game.make_move(1, 2, 2);

    ASSERT(game.check_winner() == "O", "O wins main diagonal");
}

void test_draw() {
    kg::KGModule kg(logosphere::ontology::registry());
    init(kg);
    tictactoe::TicTacToe game(kg);

    // X | O | X
    // X | O | O
    // O | X | X
    const int moves[9][3] = {
        {0, 0, 0}, {1, 0, 1}, {0, 0, 2},
        {1, 1, 1}, {0, 1, 0}, {1, 1, 2},
        {0, 2, 1}, {1, 2, 0}, {0, 2, 2},
    };
    for (const auto& m : moves) {
        auto err = game.make_move(m[0], m[1], m[2]);
        ASSERT(!err.has_value(), "draw-sequence move accepted");
    }

    ASSERT(game.check_winner().empty(), "no winner in a draw");
    ASSERT(game.is_full(), "board is full");
}

int main() {
    std::cout << "=== Tic-Tac-Toe Tests ===" << std::endl;

    test_ontology_extension_adds_tictactoe_types();
    test_game_only_uses_engine_relations();
    test_board_setup_creates_nine_cells_and_two_players();
    test_move_marks_cell_and_records_occupies_relation();
    test_move_rejects_out_of_range();
    test_move_rejects_occupied_cell();
    test_horizontal_win();
    test_diagonal_win();
    test_draw();

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
