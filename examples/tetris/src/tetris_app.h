// Tetris on Logosphere.
//
// The well is 10 x 20 cells of world space seen from directly above
// (ProjectionMode::BirdsEye), one box particle per occupied cell. Every
// locked cell is a TetrisMino entity in the knowledge graph and the
// falling piece is a TetrisPiece entity; Well::cells_ holds their ids,
// so "is this square taken" and "which entity is standing there" are
// the same question.
//
// Blocks are self-emissive and KINEMATIC: nothing in the scene is
// lit and nothing in the scene is integrated. A tetromino falls
// because the game says so on a timer, not because gravity pulls it,
// which is the only honest way to do it — real gravity would need
// friction, stacking and rest thresholds to hold a stack of 200
// boxes in a grid, and would still drift.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "platform/platform_layer.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "logosphere/kg/kg_module.h"
#include "particle.h"
#include "ui/ui_system.h"
#include "ui/widgets.h"

#include "tetris_ontology_registry.h"
#include "tetromino.h"
#include "well.h"

// Eden does exactly this (examples/eden/src/main.cpp:39). MacOSPlatform
// has no header of its own; PlatformLayer supplies initialize(),
// shutdown() and get_window(), and this empty derivation is how the
// examples name it.
namespace Logosphere {
class MacOSPlatform : public PlatformLayer {};
}  // namespace Logosphere

namespace tetris {

// World geometry. One cell is one metre; the well is centred on the
// origin so the bird's-eye camera can sit at (0, 0) and stay there.
inline constexpr float kCellSize   = 1.0f;
inline constexpr float kBlockInset = 0.06f;   // gap between neighbours
inline constexpr float kBlockZ     = 1.0f;
inline constexpr float kBlockDepth = 0.60f;
inline constexpr float kFrameZ     = 0.90f;
inline constexpr float kCameraZ    = 40.0f;

inline float cell_center_x(int col) {
    return (static_cast<float>(col) - 0.5f * (kCols - 1)) * kCellSize;
}
inline float cell_center_y(int row) {
    return (static_cast<float>(row) - 0.5f * (kRows - 1)) * kCellSize;
}

class TetrisApplication : public Logosphere::MacOSPlatform {
public:
    TetrisApplication() {
        set_app_name("Tetris - Logosphere");
        set_window_size(1000, 1200);
    }

    void set_seed(unsigned s) { seed_ = s; seeded_ = true; }
    int  score() const  { return score_; }
    int  lines() const  { return lines_; }
    int  level() const  { return level_for(lines_); }
    bool over() const   { return game_over_; }
    const Well& well() const { return well_; }

    // ---------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------
    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();
        kg.extendOntology(tetris::ontology::registry());

        engine_->set_projection_mode(ProjectionMode::BirdsEye);
        frame_camera();

        well_entity_ = kg.createEntity("TetrisWell");
        kg.setProperty(well_entity_, "well_columns", std::to_string(kCols));
        kg.setProperty(well_entity_, "well_rows", std::to_string(kRows));
        build_frame();

        build_hud();

        // No light in this scene, on purpose. An overhead light source
        // is a particle like any other, and the first version of this
        // file had one: deleting the falling piece's particles then
        // swap-and-popped it, and the engine said so —
        //   [PARTICLE_SWAP WARNING] Light source particle at index 7
        //   being swapped to index 3 (emission=900000)
        // examples/logotron/src/walls.h:30 records that exact churn
        // crashing the GPU command queue on 2026-04-27. Everything
        // here is self-emissive instead, so nothing needs lighting and
        // the lights array stays empty.

        if (!seeded_) seed_ = static_cast<unsigned>(std::random_device{}());
        rng_.seed(seed_);
        std::printf("[tetris] seed=%u\n", seed_);

        refill_bag();
        spawn_next();

        std::printf(
            "[tetris] LEFT/RIGHT move, UP or X rotate CW, Z rotate CCW,\n"
            "[tetris] DOWN soft drop, SPACE hard drop, R restart, ESC quit.\n");
    }

    void set_autoplay(bool on) { autoplay_ = on; }

    void update_game(float dt) override {
        refresh_hud();
        if (game_over_) return;
        if (autoplay_) { autoplay_step(); return; }
        elapsed_ += dt;

        const float interval = soft_dropping_ ? kSoftDropInterval
                                              : fall_interval_for(level());
        fall_timer_ += dt;
        while (fall_timer_ >= interval) {
            fall_timer_ -= interval;
            if (!try_move(0, -1)) { lock_piece(); break; }
            if (soft_dropping_) score_ += 1;
        }
    }

    // ---------------------------------------------------------------
    // Input
    // ---------------------------------------------------------------
    bool handle_key(int key, int, int action, int) override {
        if (action == GLFW_RELEASE) {
            if (key == GLFW_KEY_DOWN) soft_dropping_ = false;
            return key == GLFW_KEY_DOWN;
        }
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;

        switch (key) {
            case GLFW_KEY_ESCAPE: if (engine_) engine_->stop(); return true;
            case GLFW_KEY_R:      restart(); return true;
            default: break;
        }
        if (game_over_) return false;

        switch (key) {
            case GLFW_KEY_LEFT:  try_move(-1, 0); return true;
            case GLFW_KEY_RIGHT: try_move(1, 0);  return true;
            case GLFW_KEY_DOWN:
                soft_dropping_ = true;
                if (!try_move(0, -1)) lock_piece(); else score_ += 1;
                return true;
            case GLFW_KEY_UP:
            case GLFW_KEY_X:     try_rotate(1);  return true;
            case GLFW_KEY_Z:     try_rotate(-1); return true;
            case GLFW_KEY_SPACE: hard_drop();    return true;
            default: return false;
        }
    }

    // ---------------------------------------------------------------
    // Moves. Every one of them goes through fits(), so there is a
    // single place that knows what a legal placement is.
    // ---------------------------------------------------------------
    bool try_move(int dcol, int drow) {
        if (game_over_) return false;
        if (!well_.fits(kind_, rot_, px_ + dcol, py_ + drow)) return false;
        px_ += dcol;
        py_ += drow;
        sync_piece();
        return true;
    }

    bool try_rotate(int dir) {
        if (game_over_) return false;
        const int next = (rot_ + dir + 4) & 3;
        for (const Cell& k : kKicks) {
            if (!well_.fits(kind_, next, px_ + k.dx, py_ + k.dy)) continue;
            rot_ = next;
            px_ += k.dx;
            py_ += k.dy;
            sync_piece();
            return true;
        }
        return false;
    }

    void hard_drop() {
        if (game_over_) return;
        int dropped = 0;
        while (well_.fits(kind_, rot_, px_, py_ - 1)) { py_--; dropped++; }
        score_ += 2 * dropped;
        sync_piece();
        lock_piece();
    }

    void restart() {
        auto& kg = engine_->get_kg();
        std::vector<int> doomed = all_locked_particles();
        if (!doomed.empty())
            engine_->get_particle_system().delete_particles_immediate(doomed);
        for (int r = 0; r < kRows; r++)
            for (int c = 0; c < kCols; c++)
                if (well_.at(c, r) != kg::INVALID_ENTITY)
                    kg.destroyEntity(well_.at(c, r));
        well_.clear();
        score_ = 0;
        lines_ = 0;
        elapsed_ = 0.0f;
        fall_timer_ = 0.0f;
        game_over_ = false;
        soft_dropping_ = false;
        rng_.seed(seed_);
        bag_.clear();
        refill_bag();
        spawn_next();
        std::printf("[tetris] restarted\n");
    }

private:
    static constexpr float kSoftDropInterval = 0.045f;

    // ---------------------------------------------------------------
    // Autoplay: not a feature, a test harness. Headless is the source
    // of truth in this repo, and "line clears work" is not a claim you
    // can make by watching a piece fall for eight seconds. This picks
    // the best landing for the current piece and hard-drops it, so a
    // headless run reaches a line clear in under a second and a top
    // out shortly after.
    // ---------------------------------------------------------------
    void autoplay_step() {
        int best_rot = rot_, best_px = px_;
        float best = -1e9f;
        int best_py = py_;
        for (int r = 0; r < 4; r++) {
            for (int x = -3; x < kCols; x++) {
                // Highest row this orientation can occupy at all. The
                // spawn row is NOT that row for a vertical I, whose box
                // reaches three cells above its floor.
                int entry = -1;
                for (int y = kRows - 1; y >= 0; y--) {
                    if (well_.fits(kind_, r, x, y)) { entry = y; break; }
                }
                if (entry < 0) continue;
                int y = entry;
                while (well_.fits(kind_, r, x, y - 1)) y--;
                const float v = evaluate(kind_, r, x, y);
                if (v > best) { best = v; best_rot = r; best_px = x; best_py = entry; }
            }
        }
        rot_ = best_rot;
        px_ = best_px;
        py_ = best_py;
        sync_piece();
        hard_drop();
    }

    // Landing quality: clearing lines is worth a lot, stacking high and
    // burying holes are worth negative. Crude on purpose.
    float evaluate(Kind kind, int rot, int px, int py) const {
        bool occ[kRows][kCols];
        for (int r = 0; r < kRows; r++)
            for (int c = 0; c < kCols; c++) occ[r][c] = well_.occupied(c, r);
        for (const Cell& c : kShapes[static_cast<int>(kind)][rot]) {
            const int col = px + c.dx, row = py + c.dy;
            if (!Well::inside(col, row)) return -1e9f;
            occ[row][col] = true;
        }
        int cleared = 0, holes = 0, top = 0, bumpiness = 0;
        int height[kCols] = {0};
        for (int r = 0; r < kRows; r++) {
            bool full = true;
            for (int c = 0; c < kCols; c++) if (!occ[r][c]) { full = false; break; }
            if (full) cleared++;
        }
        for (int c = 0; c < kCols; c++) {
            int h = 0;
            bool seen = false;
            for (int r = kRows - 1; r >= 0; r--) {
                if (occ[r][c]) { if (!seen) { seen = true; h = r + 1; } }
                else if (seen) holes++;
            }
            height[c] = h;
            if (h > top) top = h;
        }
        for (int c = 1; c < kCols; c++) {
            const int d = height[c] - height[c - 1];
            bumpiness += d < 0 ? -d : d;
        }
        return 12.0f * cleared - 0.9f * top - 4.0f * holes - 0.3f * bumpiness;
    }

    // ---------------------------------------------------------------
    // Camera
    // ---------------------------------------------------------------
    void frame_camera() {
        auto& cam = engine_->get_camera_system();
        cam.set_position(0.0f, 0.0f, kCameraZ);
        cam.look_at(0.0f, 0.0f, 0.0f);
        const int rh = engine_->get_resolution_manager().get_render_height();
        // Two cells of margin above and below the 20-row well.
        const float ppu = static_cast<float>(rh > 0 ? rh : 1200) / (kRows + 3.0f);
        cam.set_pixels_per_unit(ppu);
    }

    // ---------------------------------------------------------------
    // HUD. docs/VISUAL_TESTS.md:20 is emphatic that immediate-mode
    // text drawn between render() and present() is cleared before it
    // reaches the screen, and that a registered widget is the only
    // route that survives. So: labels.
    // ---------------------------------------------------------------
    void build_hud() {
        UISystem* ui = engine_->get_ui_system();
        if (!ui) return;
        const int x = 24;
        int y = 24;
        const char* ids[kHudLines] = {"tetris_score", "tetris_lines",
                                      "tetris_level", "tetris_state"};
        const uint8_t col[kHudLines][3] = {{255, 240, 140}, {160, 230, 255},
                                           {200, 200, 220}, {255, 120, 120}};
        for (int i = 0; i < kHudLines; i++) {
            hud_[i] = new ui::Label("", ids[i]);
            hud_[i]->set_position(x, y);
            hud_[i]->set_color(col[i][0], col[i][1], col[i][2]);
            ui->add_widget(hud_[i]);       // UISystem takes ownership
            y += 22;
        }
    }

    void refresh_hud() {
        if (!hud_[0]) return;
        hud_[0]->set_text("SCORE " + std::to_string(score_));
        hud_[1]->set_text("LINES " + std::to_string(lines_));
        hud_[2]->set_text("LEVEL " + std::to_string(level()));
        hud_[3]->set_text(game_over_ ? "GAME OVER - R TO RESTART" : "");
    }

    // ---------------------------------------------------------------
    // Particles
    // ---------------------------------------------------------------
    static Particle make_block(int col, int row, Kind kind) {
        Particle p;
        p.shape = ParticleShape::BOX;
        p.x = cell_center_x(col);
        p.y = cell_center_y(row);
        p.z = kBlockZ;
        p.width = p.height = kCellSize - 2.0f * kBlockInset;
        p.thickness = kBlockDepth;
        p.SetMaterial(Materials::Type::STONE);
        p.owner = ParticleOwner::STATIC;
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        const Rgb& c = kColors[static_cast<int>(kind)];
        p.r = c.r; p.g = c.g; p.b = c.b; p.a = 1.0f;
        p.is_light_source = false;
        p.is_self_emissive = true;
        p.emission_strength = 2.2f;
        p.emission_radius = 0.0f;
        return p;
    }

    // Well frame: two side rails and a floor rail, one cell outside
    // the playfield so the board reads as a container.
    void build_frame() {
        auto& ps = engine_->get_particle_system();
        auto& kg = engine_->get_kg();
        const float half_w = 0.5f * kCols * kCellSize;
        const float half_h = 0.5f * kRows * kCellSize;
        const float t = 0.45f;

        struct Rail { float x, y, w, h; };
        const Rail rails[3] = {
            {-half_w - t * 0.5f, 0.0f, t, kRows * kCellSize + 2 * t},
            { half_w + t * 0.5f, 0.0f, t, kRows * kCellSize + 2 * t},
            { 0.0f, -half_h - t * 0.5f, kCols * kCellSize + 2 * t, t},
        };
        for (const Rail& rail : rails) {
            Particle p;
            p.shape = ParticleShape::BOX;
            p.x = rail.x; p.y = rail.y; p.z = kFrameZ;
            p.width = rail.w; p.height = rail.h; p.thickness = 0.5f;
            p.SetMaterial(Materials::Type::STONE);
            p.owner = ParticleOwner::STATIC;
            p.solver_mode = ParticleSolverMode::KINEMATIC;
            p.is_at_rest = true;
            p.r = 0.35f; p.g = 0.40f; p.b = 0.55f; p.a = 1.0f;
            p.is_light_source = false;
            p.is_self_emissive = true;
            p.emission_strength = 0.55f;
            ps.add_particle_to_entity(p, &kg, well_entity_);
        }
    }

    // The falling piece is redrawn by deleting its four particles and
    // adding four more. Writing x/y in place would be cheaper, but
    // delete-and-re-add is the path the engine's own example uses for
    // a particle that moves every frame (logotron walls.h, "deletes +
    // re-adds this particle EVERY FRAME"), and four particles a
    // keystroke is not worth inventing a second way.
    void sync_piece() {
        auto& ps = engine_->get_particle_system();
        auto& kg = engine_->get_kg();

        const auto& idx = ps.get_entity_particle_indices(piece_entity_);
        if (!idx.empty()) {
            std::vector<int> doomed(idx.begin(), idx.end());
            ps.delete_particles_immediate(doomed);
        }
        for (const Cell& c : kShapes[static_cast<int>(kind_)][rot_]) {
            ps.add_particle_to_entity(make_block(px_ + c.dx, py_ + c.dy, kind_),
                                      &kg, piece_entity_);
        }
        kg.setProperty(piece_entity_, "cell_column", std::to_string(px_));
        kg.setProperty(piece_entity_, "cell_row", std::to_string(py_));
        kg.setProperty(piece_entity_, "piece_rotation", std::to_string(rot_));
    }

    std::vector<int> all_locked_particles() const {
        auto& ps = engine_->get_particle_system();
        std::vector<int> out;
        for (int r = 0; r < kRows; r++) {
            for (int c = 0; c < kCols; c++) {
                const kg::EntityID e = well_.at(c, r);
                if (e == kg::INVALID_ENTITY) continue;
                const auto& idx = ps.get_entity_particle_indices(e);
                out.insert(out.end(), idx.begin(), idx.end());
            }
        }
        return out;
    }

    // After a collapse every surviving mino may have moved down, so
    // the whole locked field is redrawn from the grid. At most 200
    // particles and only on a line clear.
    void redraw_locked_field() {
        auto& ps = engine_->get_particle_system();
        auto& kg = engine_->get_kg();
        std::vector<int> doomed = all_locked_particles();
        if (!doomed.empty()) ps.delete_particles_immediate(doomed);

        for (int r = 0; r < kRows; r++) {
            for (int c = 0; c < kCols; c++) {
                const kg::EntityID e = well_.at(c, r);
                if (e == kg::INVALID_ENTITY) continue;
                kg.setProperty(e, "cell_column", std::to_string(c));
                kg.setProperty(e, "cell_row", std::to_string(r));
                ps.add_particle_to_entity(
                    make_block(c, r, kind_from_name(kg.getProperty(e, "tetromino_kind"))),
                    &kg, e);
            }
        }
    }

    static Kind kind_from_name(const std::string& n) {
        for (int i = 0; i < kKindCount; i++)
            if (n == kind_name(static_cast<Kind>(i))) return static_cast<Kind>(i);
        return Kind::I;
    }

    // ---------------------------------------------------------------
    // Piece lifecycle
    // ---------------------------------------------------------------
    void refill_bag() {
        std::vector<int> bag;
        for (int i = 0; i < kKindCount; i++) bag.push_back(i);
        std::shuffle(bag.begin(), bag.end(), rng_);
        for (int i : bag) bag_.push_back(static_cast<Kind>(i));
    }

    void spawn_next() {
        if (bag_.empty()) refill_bag();
        kind_ = bag_.front();
        bag_.erase(bag_.begin());
        rot_ = 0;
        px_ = (kCols - 4) / 2;
        py_ = kRows - 3;

        auto& kg = engine_->get_kg();
        if (piece_entity_ == kg::INVALID_ENTITY) {
            piece_entity_ = kg.createEntity("TetrisPiece");
        }
        kg.setProperty(piece_entity_, "tetromino_kind", kind_name(kind_));

        if (!well_.fits(kind_, rot_, px_, py_)) {
            game_over_ = true;
            std::printf("[tetris] TOP OUT — score %d, lines %d, level %d\n",
                        score_, lines_, level());
        }
        sync_piece();
    }

    void lock_piece() {
        auto& ps = engine_->get_particle_system();
        auto& kg = engine_->get_kg();

        // The piece entity's particles go; each cell becomes a mino
        // entity of its own with a particle of its own.
        const auto& idx = ps.get_entity_particle_indices(piece_entity_);
        if (!idx.empty()) {
            std::vector<int> doomed(idx.begin(), idx.end());
            ps.delete_particles_immediate(doomed);
        }
        for (const Cell& c : kShapes[static_cast<int>(kind_)][rot_]) {
            const int col = px_ + c.dx, row = py_ + c.dy;
            if (!Well::inside(col, row)) continue;
            kg::EntityID mino = kg.createEntity("TetrisMino");
            kg.setProperty(mino, "cell_column", std::to_string(col));
            kg.setProperty(mino, "cell_row", std::to_string(row));
            kg.setProperty(mino, "tetromino_kind", kind_name(kind_));
            well_.put(col, row, mino);
            ps.add_particle_to_entity(make_block(col, row, kind_), &kg, mino);
        }

        const std::vector<int> full = well_.full_rows();
        if (!full.empty()) {
            for (int r : full) {
                for (int c = 0; c < kCols; c++) {
                    const kg::EntityID e = well_.at(c, r);
                    if (e == kg::INVALID_ENTITY) continue;
                    const auto& pidx = ps.get_entity_particle_indices(e);
                    if (!pidx.empty()) {
                        std::vector<int> d(pidx.begin(), pidx.end());
                        ps.delete_particles_immediate(d);
                    }
                    kg.destroyEntity(e);
                }
            }
            well_.collapse(full);
            const int n = static_cast<int>(full.size());
            lines_ += n;
            score_ += line_score(n, level());
            std::printf("[tetris] cleared %d — score %d, lines %d, level %d\n",
                        n, score_, lines_, level());
            redraw_locked_field();
        }

        soft_dropping_ = false;
        fall_timer_ = 0.0f;
        spawn_next();
    }

    // ---------------------------------------------------------------
    static constexpr int kHudLines = 4;

    Engine* engine_ = nullptr;
    Well well_;
    ui::Label* hud_[kHudLines] = {nullptr, nullptr, nullptr, nullptr};

    kg::EntityID well_entity_  = kg::INVALID_ENTITY;
    kg::EntityID piece_entity_ = kg::INVALID_ENTITY;

    Kind kind_ = Kind::I;
    int  rot_ = 0, px_ = 3, py_ = kRows - 3;

    std::vector<Kind> bag_;
    std::mt19937 rng_;
    unsigned seed_ = 1u;
    bool seeded_ = false;

    int   score_ = 0, lines_ = 0;
    float elapsed_ = 0.0f, fall_timer_ = 0.0f;
    bool  game_over_ = false, soft_dropping_ = false, autoplay_ = false;
};

}  // namespace tetris
