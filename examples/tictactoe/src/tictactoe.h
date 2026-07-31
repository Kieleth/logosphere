// Tic-Tac-Toe game state, modeled entirely on the Logosphere Knowledge
// Graph: a Board entity HAS_PART nine Cell entities (row/col/mark
// properties), two Player entities (symbol property), and MANAGES
// relations recording each move. Shared by the console game
// (examples/tictactoe/src/main.cpp) and the headless test
// (tests/test_tictactoe.cpp) so both exercise the same logic.
#pragma once

#include "logosphere/kg/kg_module.h"

#include <array>
#include <optional>
#include <string>

namespace tictactoe {

constexpr int kBoardSize = 3;

class TicTacToe {
public:
    explicit TicTacToe(kg::KGModule& kg) : kg_(kg) {
        board_ = kg_.createEntity("Board");
        for (int r = 0; r < kBoardSize; ++r) {
            for (int c = 0; c < kBoardSize; ++c) {
                auto cell = kg_.createEntity("Cell");
                kg_.setProperty(cell, "row", std::to_string(r));
                kg_.setProperty(cell, "col", std::to_string(c));
                kg_.setProperty(cell, "mark", "");
                kg_.createRelation(board_, "HAS_PART", cell);
                cells_[r][c] = cell;
            }
        }
        players_[0] = make_player("X", "Player X");
        players_[1] = make_player("O", "Player O");
    }

    kg::EntityID board() const { return board_; }
    kg::EntityID player(int player_index) const { return players_[player_index]; }
    kg::EntityID cell(int row, int col) const { return cells_[row][col]; }

    std::string symbol(int player_index) const {
        return kg_.getProperty(players_[player_index], "symbol");
    }

    // Returns an error message on failure, std::nullopt on success.
    std::optional<std::string> make_move(int player_index, int row, int col) {
        if (row < 0 || row >= kBoardSize || col < 0 || col >= kBoardSize) {
            return "cell out of range";
        }
        if (!mark_at(row, col).empty()) {
            return "cell already occupied";
        }
        auto cell_id = cells_[row][col];
        kg_.setProperty(cell_id, "mark", symbol(player_index));
        // MANAGES is an engine relation. Games reuse the engine's set
        // rather than declaring their own, because
        // scripts/generate_registry.py derives relation types solely
        // from the engine's WorldRelationType enum -- a game-declared
        // relation would vanish on the next regeneration.
        kg_.createRelation(players_[player_index], "MANAGES", cell_id);
        return std::nullopt;
    }

    // Returns the winning symbol ("X"/"O"), or "" if no winner yet.
    std::string check_winner() const {
        static const int lines[8][3][2] = {
            {{0, 0}, {0, 1}, {0, 2}}, {{1, 0}, {1, 1}, {1, 2}}, {{2, 0}, {2, 1}, {2, 2}},
            {{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 1}, {2, 1}}, {{0, 2}, {1, 2}, {2, 2}},
            {{0, 0}, {1, 1}, {2, 2}}, {{0, 2}, {1, 1}, {2, 0}},
        };
        for (const auto& line : lines) {
            std::string a = mark_at(line[0][0], line[0][1]);
            if (a.empty()) continue;
            if (a == mark_at(line[1][0], line[1][1]) && a == mark_at(line[2][0], line[2][1])) {
                return a;
            }
        }
        return "";
    }

    bool is_full() const {
        for (int r = 0; r < kBoardSize; ++r) {
            for (int c = 0; c < kBoardSize; ++c) {
                if (mark_at(r, c).empty()) return false;
            }
        }
        return true;
    }

    std::string mark_at(int row, int col) const {
        return kg_.getProperty(cells_[row][col], "mark");
    }

private:
    kg::EntityID make_player(const std::string& sym, const std::string& name) {
        auto id = kg_.createEntity("Player");
        kg_.setProperty(id, "symbol", sym);
        kg_.setProperty(id, "player_name", name);
        return id;
    }

    kg::KGModule& kg_;
    kg::EntityID board_ = kg::INVALID_ENTITY;
    std::array<std::array<kg::EntityID, kBoardSize>, kBoardSize> cells_{};
    kg::EntityID players_[2] = {kg::INVALID_ENTITY, kg::INVALID_ENTITY};
};

} // namespace tictactoe
