// Pacman on Logosphere.
//
// The whole game lives in this one IApplication. It is header-only for
// the same reason logotron's is: the headless acceptance test wants to
// instantiate the exact class the windowed game runs, not a
// reconstruction of it (docs/testing_guidelines.md rule 6).
//
// Layering, deliberately:
//   maze.h/.cpp   pure rules. No engine, no KG. Fully testable alone.
//   this file     the same rules bound to KG entities and particles.
//   main.cpp      the loop.
//
// The knowledge graph is the record, not a mirror: score, lives, every
// actor's cell position, every ghost's mood and current target are KG
// properties written through setProperty. Anything watching the
// state_changes channel sees the whole game.
//
// Rendering is deliberately flat. Every particle is is_self_emissive,
// which makes its colour its final pixel colour and means the game
// needs no lights, no sun and no shadow rig. Pacman is a 2D game; a
// lighting pass would only be something else to get wrong.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <GLFW/glfw3.h>

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "core/projection_mode.h"
#include "logosphere/kg/kg_module.h"
#include "particle.h"
#include "ui/ui_system.h"
#include "key_mapper.h"
#include "ui/text_window.h"

#include "maze.h"
#include "pacman_ontology_registry.h"

namespace pacman {

// ---------------------------------------------------------------------
// Tuning. All of it game policy, none of it engine.
// ---------------------------------------------------------------------

inline constexpr float kAvatarSpeed      = 6.4f;   // cells / second
inline constexpr float kGhostSpeed       = 5.8f;
inline constexpr float kFrightenedSpeed  = 3.6f;
inline constexpr float kEyesSpeed        = 14.0f;
inline constexpr float kFrightenedSecs   = 7.0f;
inline constexpr float kReadySecs        = 2.0f;
inline constexpr float kDyingSecs        = 1.6f;
inline constexpr int   kStartingLives    = 3;
inline constexpr int   kPelletPoints     = 10;
inline constexpr int   kEnergizerPoints  = 50;
inline constexpr float kCatchDistance    = 0.7f;   // cells, centre to centre

// Scatter / chase schedule, in seconds. Classic level-1 timings.
inline constexpr float kScatterChase[8] = {7, 20, 7, 20, 5, 20, 5, 1e9f};

// z layers. BirdsEyeProjection depth is -(z - camera_z), so higher z
// draws in front. Nothing here relies on physics; the engine's solver
// is switched off for this game.
//
// Every one of these is a CENTRE, and every body's BOTTOM must sit at
// or above z = 0: the world turtle lives there and
// ParticleSystem::assert_above_turtle aborts the process on a
// violation (src/core/particle_system.cpp:112, strict by default).
// So the floor's centre is half its own thickness, not zero.
inline constexpr float kFloorThickness = 0.2f;
inline constexpr float kFloorZ   = 0.5f * kFloorThickness;
inline constexpr float kWallZ    = 0.50f;
inline constexpr float kPelletZ  = 1.00f;
inline constexpr float kActorZ   = 1.30f;

// How far north of the board centre the camera parks, in cells. Shifts
// the board down the screen so the HUD band at the top is clear of it.
inline constexpr float kBoardOffsetY = 1.6f;

enum class Mood : uint8_t { SCATTER, CHASE, FRIGHTENED, EATEN };
enum class Phase : uint8_t { READY, PLAYING, DYING, LEVEL_CLEARED, GAME_OVER };

inline const char* mood_name(Mood m) {
    switch (m) {
        case Mood::SCATTER:    return "SCATTER";
        case Mood::CHASE:      return "CHASE";
        case Mood::FRIGHTENED: return "FRIGHTENED";
        case Mood::EATEN:      return "EATEN";
    }
    return "SCATTER";
}

inline const char* phase_name(Phase p) {
    switch (p) {
        case Phase::READY:         return "READY";
        case Phase::PLAYING:       return "PLAYING";
        case Phase::DYING:         return "DYING";
        case Phase::LEVEL_CLEARED: return "LEVEL_CLEARED";
        case Phase::GAME_OVER:     return "GAME_OVER";
    }
    return "READY";
}

struct GhostSlot {
    kg::EntityID entity = kg::INVALID_ENTITY;
    Walker       walker;
    Mood         mood = Mood::SCATTER;
    const char*  name = "BLINKY";
    int          scatter_col = 0;
    int          scatter_row = 0;
    float        release_at = 0.0f;   // round-elapsed seconds
    bool         in_house = false;
    float        r = 1.0f, g = 0.0f, b = 0.0f;
    float        frightened_left = 0.0f;
};

struct PelletSlot {
    kg::EntityID entity = kg::INVALID_ENTITY;
    int  col = 0;
    int  row = 0;
    bool energizer = false;
    bool eaten = false;
};

}  // namespace pacman

class PacmanApplication : public Logosphere::IApplication {
public:
    // ---- IApplication ------------------------------------------------
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }
    const char* get_app_name() const override { return "Pacman"; }
    void* getKGModule() override {
        return engine_ ? static_cast<void*>(&engine_->get_kg()) : nullptr;
    }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();

        kg.extendOntology(pacman::ontology::registry());

        // The board verifies itself before anything is built from it.
        // A maze is a hand-typed data file with no compiler; this is
        // its compiler.
        const auto problems = board_.validate();
        std::cout << "[pacman] board " << board_.cols() << "x" << board_.rows()
                  << ", " << board_.pellet_count() << " pellets, "
                  << problems.size() << " defect(s)" << std::endl;
        for (const auto& p : problems)
            std::cerr << "[pacman] BOARD DEFECT: " << p << std::endl;
        board_ok_ = problems.empty();

        // The solver has no job here. Every position in this game is
        // written by the game itself, on a grid, and nothing falls.
        // Leaving physics on would mean arguing with it every frame.
        engine_->set_physics_enabled(false);

        maze_entity_ = kg.createEntity("Maze");
        kg.setProperty(maze_entity_, "maze_cols", std::to_string(board_.cols()));
        kg.setProperty(maze_entity_, "maze_rows", std::to_string(board_.rows()));
        kg.setProperty(maze_entity_, "level", "1");
        kg.setProperty(maze_entity_, "high_score", "0");

        // One rule, armed whenever a pellet is eaten. This is the
        // engine's sanctioned way for a game to remove a particle:
        // the interaction system ramps alpha and then hands the index
        // to the engine's deferred deletion queue, which is safe
        // against in-flight GPU frames. See GAME_LAYER.md §6.
        pellet_pop_rule_ = kg.createEntity("TransformationRule");
        kg.setProperty(pellet_pop_rule_, "trigger", "on_timer");
        kg.setProperty(pellet_pop_rule_, "effect", "fade_out");
        kg.setProperty(pellet_pop_rule_, "duration_s", "0.12");
        kg.setProperty(pellet_pop_rule_, "name", "pellet_pop");
        engine_->get_interaction_system().load_rules_from_kg(kg);

        build_floor(kg);
        build_walls(kg);
        build_pellets(kg);
        build_avatar(kg);
        build_ghosts(kg);

        setup_camera();
        setup_hud();

        reset_round(/*full_restart=*/true);

        std::cout << "[pacman] controls: arrows or WASD to steer, "
                     "R restart, ESC quit." << std::endl;
    }

    void update_game(float dt) override {
        if (!engine_) return;
        if (dt > 0.1f) dt = 0.1f;   // a stall must not teleport anyone
        anim_clock_ += dt;
        tick(dt);
        sync_particles();
        update_hud();
    }

    bool handle_key(int key, int /*scancode*/, int action, int /*mods*/) override {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
        switch (key) {
            case GLFW_KEY_UP:    case GLFW_KEY_W: queue(pacman::Dir::UP);    return true;
            case GLFW_KEY_DOWN:  case GLFW_KEY_S: queue(pacman::Dir::DOWN);  return true;
            case GLFW_KEY_LEFT:  case GLFW_KEY_A: queue(pacman::Dir::LEFT);  return true;
            case GLFW_KEY_RIGHT: case GLFW_KEY_D: queue(pacman::Dir::RIGHT); return true;
            case GLFW_KEY_R:     reset_round(/*full_restart=*/true);         return true;
            default: return false;
        }
    }

    // ---- test / tooling surface ---------------------------------------
    // The acceptance test drives the same object the window drives; it
    // needs to read the result, not reconstruct it.
    int   score()             const { return score_; }
    int   lives()             const { return lives_; }
    int   pellets_remaining() const { return pellets_remaining_; }
    pacman::Phase phase()     const { return phase_; }
    bool  board_ok()          const { return board_ok_; }
    const pacman::Board& board() const { return board_; }
    const pacman::Walker& avatar() const { return avatar_; }
    const std::vector<pacman::GhostSlot>& ghosts() const { return ghosts_; }
    int   ghosts_eaten_total() const { return ghosts_eaten_total_; }
    int   pellets_eaten_total() const { return pellets_eaten_total_; }
    int   deaths_total()       const { return deaths_total_; }
    kg::EntityID maze_entity() const { return maze_entity_; }

    void queue(pacman::Dir d) { avatar_.desired = d; }

private:
    // -----------------------------------------------------------------
    // Scene construction
    // -----------------------------------------------------------------

    // Every particle in this game is styled through here, so the
    // emissive decision is made once and cannot drift between object
    // kinds. is_self_emissive means the deferred shader treats colour *
    // emission_strength as the final pixel and skips lighting entirely
    // — the reason this game needs no sun, no lights and no shadow rig.
    void style_flat(Particle& p, float r, float g, float b, float emission) {
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.owner = ParticleOwner::STATIC;
        p.is_at_rest = true;
        p.is_light_source  = false;   // never joins the scene-lights array
        p.is_self_emissive = true;    // colour IS the pixel; no lighting rig
        p.emission_strength = emission;
        p.emission_radius   = 0.0f;
    }

    void build_floor(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        floor_entity_ = kg.createEntity("Maze");
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = pacman::kFloorZ;
        p.width  = static_cast<float>(board_.cols()) + 1.0f;
        p.height = static_cast<float>(board_.rows()) + 1.0f;
        p.thickness = pacman::kFloorThickness;
        style_flat(p, 0.015f, 0.015f, 0.045f, 1.0f);
        ps.add_particle_to_entity(p, &kg, floor_entity_);
    }

    void build_walls(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        int made = 0;
        for (int r = 0; r < board_.rows(); ++r) {
            for (int c = 0; c < board_.cols(); ++c) {
                const pacman::Tile t = board_.at(c, r);
                if (t != pacman::Tile::WALL && t != pacman::Tile::DOOR) continue;

                auto e = kg.createEntity("MazeWall");
                kg.setProperty(e, "cell_col", std::to_string(c));
                kg.setProperty(e, "cell_row", std::to_string(r));

                Particle p{};
                p.shape = ParticleShape::BOX;
                board_.cell_to_world(static_cast<float>(c),
                                     static_cast<float>(r), p.x, p.y);
                p.z = pacman::kWallZ;
                if (t == pacman::Tile::DOOR) {
                    p.width = 0.94f; p.height = 0.18f; p.thickness = 0.3f;
                    style_flat(p, 0.95f, 0.72f, 0.85f, 1.6f);   // pink sill
                } else {
                    p.width = 0.94f; p.height = 0.94f; p.thickness = 0.6f;
                    style_flat(p, 0.13f, 0.20f, 0.92f, 1.9f);   // maze blue
                }
                ps.add_particle_to_entity(p, &kg, e);
                ++made;
            }
        }
        std::cout << "[pacman] " << made << " wall cells" << std::endl;
    }

    void build_pellets(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        for (int r = 0; r < board_.rows(); ++r) {
            for (int c = 0; c < board_.cols(); ++c) {
                const pacman::Tile t = board_.at(c, r);
                const bool energizer = (t == pacman::Tile::ENERGIZER);
                if (t != pacman::Tile::PELLET && !energizer) continue;

                pacman::PelletSlot slot;
                slot.col = c; slot.row = r; slot.energizer = energizer;
                slot.entity = kg.createEntity(energizer ? "PowerPellet" : "Pellet");
                kg.setProperty(slot.entity, "cell_col", std::to_string(c));
                kg.setProperty(slot.entity, "cell_row", std::to_string(r));
                kg.setProperty(slot.entity, "eaten", "0");
                kg.setProperty(slot.entity, "points",
                               std::to_string(energizer ? pacman::kEnergizerPoints
                                                        : pacman::kPelletPoints));

                Particle p{};
                p.shape = ParticleShape::SPHERE;
                board_.cell_to_world(static_cast<float>(c),
                                     static_cast<float>(r), p.x, p.y);
                p.z = pacman::kPelletZ;
                // 0.15 m is the engine's minimum visible dimension
                // (src/particle_types.h:26) — a smaller pellet would be
                // culled as a degenerate triangle rather than drawn small.
                p.size = energizer ? 0.62f : 0.24f;
                style_flat(p, 1.0f, 0.83f, 0.62f, energizer ? 2.6f : 2.0f);
                ps.add_particle_to_entity(p, &kg, slot.entity);

                pellet_index_[key(c, r)] = static_cast<int>(pellets_.size());
                pellets_.push_back(slot);
            }
        }
        pellets_remaining_ = static_cast<int>(pellets_.size());
        std::cout << "[pacman] " << pellets_.size() << " pellets laid" << std::endl;
    }

    void build_avatar(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        avatar_entity_ = kg.createEntity("Avatar");
        Particle p{};
        p.shape = ParticleShape::SPHERE;
        p.z = pacman::kActorZ;
        p.size = 0.92f;
        style_flat(p, 1.0f, 0.93f, 0.10f, 2.4f);
        ps.add_particle_to_entity(p, &kg, avatar_entity_);
    }

    void build_ghosts(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        struct Spec { const char* name; float r, g, b; int sc, sr;
                      int start_c, start_r; float release; bool in_house; };
        const Spec specs[4] = {
            // Blinky starts on the board, the other three in the house.
            {"BLINKY", 1.00f, 0.16f, 0.16f, board_.cols() - 2, 0,
             pacman::kHouseExitCol, pacman::kHouseExitRow, 0.0f, false},
            {"PINKY",  1.00f, 0.60f, 0.85f, 1, 0,
             13, 14, 2.0f, true},
            {"INKY",   0.20f, 0.95f, 0.95f, board_.cols() - 2, board_.rows() - 1,
             11, 14, 5.0f, true},
            {"CLYDE",  1.00f, 0.62f, 0.18f, 1, board_.rows() - 1,
             16, 14, 8.0f, true},
        };
        for (const auto& s : specs) {
            pacman::GhostSlot gs;
            gs.name = s.name;
            gs.r = s.r; gs.g = s.g; gs.b = s.b;
            gs.scatter_col = s.sc; gs.scatter_row = s.sr;
            gs.release_at = s.release;
            gs.in_house = s.in_house;
            gs.walker.col = static_cast<float>(s.start_c);
            gs.walker.row = static_cast<float>(s.start_r);
            gs.walker.dir = pacman::Dir::LEFT;
            gs.walker.speed = pacman::kGhostSpeed;

            gs.entity = kg.createEntity("Ghost");
            kg.setProperty(gs.entity, "ghost_name", s.name);
            kg.setProperty(gs.entity, "scatter_col", std::to_string(s.sc));
            kg.setProperty(gs.entity, "scatter_row", std::to_string(s.sr));
            kg.setProperty(gs.entity, "house_release_at", std::to_string(s.release));

            Particle p{};
            p.shape = ParticleShape::SPHERE;
            p.z = pacman::kActorZ;
            p.size = 0.90f;
            style_flat(p, s.r, s.g, s.b, 2.2f);
            ps.add_particle_to_entity(p, &kg, gs.entity);

            ghosts_.push_back(gs);
        }
    }

    void setup_camera() {
        engine_->set_projection_mode(ProjectionMode::BirdsEye);
        auto& cam = engine_->get_camera_system();
        // BirdsEyeProjection puts the CAMERA POSITION at screen centre
        // (src/projection_system.cpp:190), so centring the board means
        // parking the camera on the board's centre, which cell_to_world
        // already defines as the world origin.
        // Parking the camera 1.6 cells NORTH of the board centre pushes
        // the board DOWN the screen (screen_y = h/2 - (wy - cy) * ppu),
        // which is what clears a band at the top for the HUD.
        cam.set_position(0.0f, pacman::kBoardOffsetY, 30.0f);
        cam.look_at(0.0f, pacman::kBoardOffsetY, 0.0f);
        fit_camera_zoom();
    }

    void fit_camera_zoom() {
        auto& rm = engine_->get_resolution_manager();
        const float rw = static_cast<float>(rm.get_render_width());
        const float rh = static_cast<float>(rm.get_render_height());
        if (rw <= 0.0f || rh <= 0.0f) return;
        // Two cells of side margin; four and a half vertically, which is
        // the HUD band plus a little breathing room.
        const float fit_w = rw / (board_.cols() + 2.0f);
        const float fit_h = rh / (board_.rows() + 4.5f);
        engine_->get_camera_system().set_pixels_per_unit(std::min(fit_w, fit_h));
    }

    void setup_hud() {
        auto* ui = engine_->get_ui_system();
        if (!ui) return;   // headless: no display, no widgets
        auto& rm = engine_->get_resolution_manager();
        const int rw = rm.get_render_width();
        hud_ = std::make_unique<TextWindow>("PACMAN", "pacman_hud");
        const int w = 300, h = 78;
        hud_->set_bounds(rw / 2 - w / 2, 16, w, h);
        hud_->set_max_lines(3);
        hud_->set_newest_at_top(false);
        hud_->set_background_alpha(190);
        ui->add_widget(hud_.get());
    }

    // -----------------------------------------------------------------
    // Round state
    // -----------------------------------------------------------------

    void reset_round(bool full_restart) {
        if (full_restart) {
            score_ = 0;
            lives_ = pacman::kStartingLives;
            deaths_total_ = 0;
            ghosts_eaten_total_ = 0;
            relay_all_pellets();
        }
        avatar_.col = static_cast<float>(pacman::kAvatarStartCol);
        avatar_.row = static_cast<float>(pacman::kAvatarStartRow);
        avatar_.dir = pacman::Dir::NONE;
        avatar_.desired = pacman::Dir::NONE;
        avatar_.speed = pacman::kAvatarSpeed;

        const int starts[4][2] = {{pacman::kHouseExitCol, pacman::kHouseExitRow},
                                  {13, 14}, {11, 14}, {16, 14}};
        const bool houses[4] = {false, true, true, true};
        const float rel[4] = {0.0f, 2.0f, 5.0f, 8.0f};
        for (size_t i = 0; i < ghosts_.size(); ++i) {
            auto& gh = ghosts_[i];
            gh.walker.col = static_cast<float>(starts[i][0]);
            gh.walker.row = static_cast<float>(starts[i][1]);
            gh.walker.dir = pacman::Dir::LEFT;
            gh.walker.desired = pacman::Dir::NONE;
            gh.walker.speed = pacman::kGhostSpeed;
            gh.mood = pacman::Mood::SCATTER;
            gh.in_house = houses[i];
            gh.release_at = rel[i];
            gh.frightened_left = 0.0f;
        }
        phase_ = pacman::Phase::READY;
        phase_timer_ = pacman::kReadySecs;
        round_elapsed_ = 0.0f;
        wave_index_ = 0;
        wave_timer_ = pacman::kScatterChase[0];
        ghost_chain_ = 0;
        write_maze_state();
    }

    void relay_all_pellets() {
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();
        for (auto& s : pellets_) {
            if (!s.eaten) continue;
            s.eaten = false;
            kg.setProperty(s.entity, "eaten", "0");
            // The particle was faded and deleted. Lay a new one and
            // rebind it to the same KG entity: the entity is the pellet,
            // the particle is only how it looks.
            Particle p{};
            p.shape = ParticleShape::SPHERE;
            board_.cell_to_world(static_cast<float>(s.col),
                                 static_cast<float>(s.row), p.x, p.y);
            p.z = pacman::kPelletZ;
            p.size = s.energizer ? 0.62f : 0.24f;
            style_flat(p, 1.0f, 0.83f, 0.62f, s.energizer ? 2.6f : 2.0f);
            ps.add_particle_to_entity(p, &kg, s.entity);
        }
        pellets_remaining_ = static_cast<int>(pellets_.size());
    }

    void write_maze_state() {
        auto& kg = engine_->get_kg();
        kg.setProperty(maze_entity_, "score", std::to_string(score_));
        kg.setProperty(maze_entity_, "lives", std::to_string(lives_));
        kg.setProperty(maze_entity_, "pellets_remaining",
                       std::to_string(pellets_remaining_));
        kg.setProperty(maze_entity_, "round_phase", pacman::phase_name(phase_));
        if (score_ > high_score_) {
            high_score_ = score_;
            kg.setProperty(maze_entity_, "high_score", std::to_string(high_score_));
        }
    }

    // -----------------------------------------------------------------
    // The round
    // -----------------------------------------------------------------

    void tick(float dt) {
        switch (phase_) {
            case pacman::Phase::READY:
                phase_timer_ -= dt;
                if (phase_timer_ <= 0.0f) {
                    phase_ = pacman::Phase::PLAYING;
                    write_maze_state();
                }
                return;
            case pacman::Phase::DYING:
                phase_timer_ -= dt;
                if (phase_timer_ <= 0.0f) {
                    if (lives_ > 0) reset_round(/*full_restart=*/false);
                    else { phase_ = pacman::Phase::GAME_OVER; write_maze_state(); }
                }
                return;
            case pacman::Phase::LEVEL_CLEARED:
            case pacman::Phase::GAME_OVER:
                return;
            case pacman::Phase::PLAYING:
                break;
        }

        round_elapsed_ += dt;
        advance_waves(dt);
        pacman::step_walker(avatar_, board_, dt, /*ghost=*/false);
        eat_here();
        for (auto& gh : ghosts_) step_ghost(gh, dt);
        resolve_contacts();
        write_actor_state();
    }

    void advance_waves(float dt) {
        if (wave_index_ >= 7) return;
        wave_timer_ -= dt;
        if (wave_timer_ > 0.0f) return;
        ++wave_index_;
        wave_timer_ = pacman::kScatterChase[wave_index_];
        // A wave change reverses every ghost that is not frightened or
        // eaten. That reversal is the arcade's tell that the mood flipped.
        for (auto& gh : ghosts_) {
            if (gh.mood == pacman::Mood::FRIGHTENED ||
                gh.mood == pacman::Mood::EATEN) continue;
            gh.mood = wave_is_chase() ? pacman::Mood::CHASE
                                      : pacman::Mood::SCATTER;
            gh.walker.desired = pacman::opposite(gh.walker.dir);
        }
    }

    bool wave_is_chase() const { return (wave_index_ % 2) == 1; }

    void eat_here() {
        const int c = board_.wrap_col(pacman::cell_of(avatar_.col));
        const int r = pacman::cell_of(avatar_.row);
        auto it = pellet_index_.find(key(c, r));
        if (it == pellet_index_.end()) return;
        auto& slot = pellets_[it->second];
        if (slot.eaten) return;

        // The avatar must actually be on the pellet, not merely in its
        // cell: half a cell away is a miss.
        const float dc = avatar_.col - c;
        const float dr = avatar_.row - r;
        if (dc * dc + dr * dr > 0.30f * 0.30f) return;

        slot.eaten = true;
        --pellets_remaining_;
        ++pellets_eaten_total_;   // survives restarts; the AT measures it
        score_ += slot.energizer ? pacman::kEnergizerPoints : pacman::kPelletPoints;

        auto& kg = engine_->get_kg();
        kg.setProperty(slot.entity, "eaten", "1");
        pop_particles_of(slot.entity);

        if (slot.energizer) {
            ghost_chain_ = 0;
            for (auto& gh : ghosts_) {
                if (gh.mood == pacman::Mood::EATEN) continue;
                gh.mood = pacman::Mood::FRIGHTENED;
                gh.frightened_left = pacman::kFrightenedSecs;
                gh.walker.desired = pacman::opposite(gh.walker.dir);
            }
        }

        write_maze_state();
        if (pellets_remaining_ <= 0) {
            phase_ = pacman::Phase::LEVEL_CLEARED;
            write_maze_state();
        }
    }

    // Hand the entity's particles to the engine's fade-then-delete rule.
    void pop_particles_of(kg::EntityID e) {
        auto& kg = engine_->get_kg();
        std::vector<kg::KGParticleID> ids = kg.getEntityKGParticles(e);
        if (ids.empty()) return;
        engine_->get_interaction_system().arm_transformation(pellet_pop_rule_, ids);
    }

    void step_ghost(pacman::GhostSlot& gh, float dt) {
        // Mood clocks first, so speed and targeting agree with each other
        // this frame rather than lagging one behind.
        if (gh.mood == pacman::Mood::FRIGHTENED) {
            gh.frightened_left -= dt;
            if (gh.frightened_left <= 0.0f)
                gh.mood = wave_is_chase() ? pacman::Mood::CHASE
                                          : pacman::Mood::SCATTER;
        }

        gh.walker.speed = ghost_speed(gh);

        int tc = 0, tr = 0;
        pick_target(gh, tc, tr);

        // Decide at cell centres only. Between centres a ghost is
        // committed, which is what makes their motion readable.
        if (pacman::at_cell_centre(gh.walker, 0.12f)) {
            const int c = board_.wrap_col(pacman::cell_of(gh.walker.col));
            const int r = pacman::cell_of(gh.walker.row);
            pacman::Dir next;
            if (gh.mood == pacman::Mood::FRIGHTENED)
                next = pacman::choose_random_dir(board_, c, r, gh.walker.dir, rng_);
            else
                next = pacman::choose_ghost_dir(board_, c, r, gh.walker.dir, tc, tr);
            if (next != pacman::Dir::NONE && next != gh.walker.dir) {
                gh.walker.desired = next;
            }
        }

        pacman::step_walker(gh.walker, board_, dt, /*ghost=*/true);

        // Eyes reaching the house become a ghost again.
        if (gh.mood == pacman::Mood::EATEN) {
            const float dc = gh.walker.col - pacman::kHouseCentreCol;
            const float dr = gh.walker.row - pacman::kHouseCentreRow;
            if (dc * dc + dr * dr < 0.5f) {
                gh.mood = wave_is_chase() ? pacman::Mood::CHASE
                                          : pacman::Mood::SCATTER;
                gh.in_house = true;
                gh.release_at = round_elapsed_ + 1.2f;
            }
        } else if (gh.in_house && gh.walker.row <= pacman::kHouseExitRow + 0.05f) {
            gh.in_house = false;
        }
    }

    float ghost_speed(const pacman::GhostSlot& gh) const {
        switch (gh.mood) {
            case pacman::Mood::FRIGHTENED: return pacman::kFrightenedSpeed;
            case pacman::Mood::EATEN:      return pacman::kEyesSpeed;
            default:
                // Held in the house: shuffle, do not sprint.
                if (gh.in_house && round_elapsed_ < gh.release_at)
                    return pacman::kGhostSpeed * 0.45f;
                return pacman::kGhostSpeed;
        }
    }

    void pick_target(const pacman::GhostSlot& gh, int& tc, int& tr) const {
        if (gh.mood == pacman::Mood::EATEN) {
            tc = pacman::kHouseCentreCol; tr = pacman::kHouseCentreRow;
            return;
        }
        if (gh.in_house) {
            // Waiting: bounce against the house centre. Released: leave.
            if (round_elapsed_ < gh.release_at) {
                tc = pacman::kHouseCentreCol; tr = pacman::kHouseCentreRow;
            } else {
                tc = pacman::kHouseExitCol; tr = pacman::kHouseExitRow;
            }
            return;
        }
        if (gh.mood == pacman::Mood::SCATTER ||
            gh.mood == pacman::Mood::FRIGHTENED) {
            tc = gh.scatter_col; tr = gh.scatter_row;
            return;
        }

        const int ac = board_.wrap_col(pacman::cell_of(avatar_.col));
        const int ar = pacman::cell_of(avatar_.row);
        int adc = 0, adr = 0;
        pacman::dir_delta(avatar_.dir, adc, adr);

        const std::string n(gh.name);
        if (n == "BLINKY") {
            tc = ac; tr = ar;
        } else if (n == "PINKY") {
            tc = ac + adc * 4; tr = ar + adr * 4;
        } else if (n == "INKY") {
            // The avatar two ahead, reflected through Blinky. Reproduces
            // the arcade's pincer without either ghost knowing about it.
            const int pc = ac + adc * 2, pr = ar + adr * 2;
            const int bc = board_.wrap_col(pacman::cell_of(ghosts_[0].walker.col));
            const int br = pacman::cell_of(ghosts_[0].walker.row);
            tc = pc + (pc - bc); tr = pr + (pr - br);
        } else {  // CLYDE
            const float dc = gh.walker.col - ac, dr = gh.walker.row - ar;
            if (dc * dc + dr * dr > 64.0f) { tc = ac; tr = ar; }
            else { tc = gh.scatter_col; tr = gh.scatter_row; }
        }
    }

    void resolve_contacts() {
        for (auto& gh : ghosts_) {
            const float dc = gh.walker.col - avatar_.col;
            const float dr = gh.walker.row - avatar_.row;
            if (dc * dc + dr * dr > pacman::kCatchDistance * pacman::kCatchDistance)
                continue;

            if (gh.mood == pacman::Mood::FRIGHTENED) {
                ghost_chain_ = std::min(ghost_chain_ + 1, 4);
                score_ += 200 * (1 << (ghost_chain_ - 1));   // 200/400/800/1600
                ++ghosts_eaten_total_;
                gh.mood = pacman::Mood::EATEN;
                gh.frightened_left = 0.0f;
                write_maze_state();
            } else if (gh.mood != pacman::Mood::EATEN) {
                --lives_;
                ++deaths_total_;
                phase_ = pacman::Phase::DYING;
                phase_timer_ = pacman::kDyingSecs;
                avatar_.dir = pacman::Dir::NONE;
                avatar_.desired = pacman::Dir::NONE;
                write_maze_state();
                return;
            }
        }
    }

    void write_actor_state() {
        auto& kg = engine_->get_kg();
        kg.setProperty(avatar_entity_, "cell_x", fmt(avatar_.col));
        kg.setProperty(avatar_entity_, "cell_y", fmt(avatar_.row));
        kg.setProperty(avatar_entity_, "lane_direction", pacman::dir_name(avatar_.dir));
        kg.setProperty(avatar_entity_, "desired_direction",
                       pacman::dir_name(avatar_.desired));
        for (const auto& gh : ghosts_) {
            kg.setProperty(gh.entity, "cell_x", fmt(gh.walker.col));
            kg.setProperty(gh.entity, "cell_y", fmt(gh.walker.row));
            kg.setProperty(gh.entity, "lane_direction",
                           pacman::dir_name(gh.walker.dir));
            kg.setProperty(gh.entity, "ghost_mood", pacman::mood_name(gh.mood));
            int tc = 0, tr = 0;
            pick_target(gh, tc, tr);
            kg.setProperty(gh.entity, "target_col", std::to_string(tc));
            kg.setProperty(gh.entity, "target_row", std::to_string(tr));
        }
    }

    // -----------------------------------------------------------------
    // Presentation
    // -----------------------------------------------------------------

    void sync_particles() {
        auto& ps = engine_->get_particle_system();
        auto view = ps.lock_particles_for_write();

        move_entity(view, ps, avatar_entity_, avatar_.col, avatar_.row,
                    pacman::kActorZ);
        // Chomp: the avatar pulses instead of animating a mouth. One
        // float, and it reads as alive at this zoom.
        const float chomp = 0.86f + 0.10f * std::sin(anim_clock_ * 16.0f);
        set_entity_size(view, ps, avatar_entity_,
                        avatar_.dir == pacman::Dir::NONE ? 0.92f : chomp);

        for (const auto& gh : ghosts_) {
            move_entity(view, ps, gh.entity, gh.walker.col, gh.walker.row,
                        pacman::kActorZ);
            float r = gh.r, g = gh.g, b = gh.b, em = 2.2f, size = 0.90f;
            if (gh.mood == pacman::Mood::FRIGHTENED) {
                // Pale periwinkle, not the arcade's navy: the maze walls
                // here ARE bright blue, and a navy ghost on a blue wall
                // is invisible at this zoom. Flashes white over the last
                // 1.2 s, the arcade's "about to wear off" warning.
                const bool ending = gh.frightened_left < 1.2f &&
                                    std::fmod(gh.frightened_left, 0.3f) > 0.15f;
                r = ending ? 1.00f : 0.48f;
                g = ending ? 1.00f : 0.55f;
                b = ending ? 1.00f : 1.00f;
                em = 2.8f;
            } else if (gh.mood == pacman::Mood::EATEN) {
                r = 0.55f; g = 0.60f; b = 0.95f; em = 1.0f; size = 0.42f;
            }
            set_entity_color(view, ps, gh.entity, r, g, b, em);
            set_entity_size(view, ps, gh.entity, size);
        }

        // Energizers breathe so they read as different from pellets.
        const float pulse = 0.55f + 0.12f * std::sin(anim_clock_ * 6.0f);
        for (const auto& s : pellets_) {
            if (s.eaten || !s.energizer) continue;
            set_entity_size(view, ps, s.entity, pulse);
        }
    }

    static void move_entity(ParticleSystem::WriteView& view, ParticleSystem& ps,
                            kg::EntityID e, float col, float row, float z) {
        (void)ps;
        // Deliberately re-read the index list every frame instead of
        // caching it. Deleting a pellet swap-and-pops the particle
        // array, so any cached render index is one deletion away from
        // pointing at somebody else. ParticleSystem keeps this mapping
        // correct across the swap (particle_system.cpp:282).
        for (int idx : g_indices(ps, e)) {
            if (idx < 0 || static_cast<size_t>(idx) >= view.size()) continue;
            float wx, wy;
            g_board().cell_to_world(col, row, wx, wy);
            view[idx].x = wx;
            view[idx].y = wy;
            view[idx].z = z;
        }
    }

    static void set_entity_size(ParticleSystem::WriteView& view,
                                ParticleSystem& ps, kg::EntityID e, float size) {
        for (int idx : g_indices(ps, e)) {
            if (idx < 0 || static_cast<size_t>(idx) >= view.size()) continue;
            view[idx].size = size;
        }
    }

    static void set_entity_color(ParticleSystem::WriteView& view,
                                 ParticleSystem& ps, kg::EntityID e,
                                 float r, float g, float b, float em) {
        for (int idx : g_indices(ps, e)) {
            if (idx < 0 || static_cast<size_t>(idx) >= view.size()) continue;
            view[idx].r = r; view[idx].g = g; view[idx].b = b;
            view[idx].emission_strength = em;
        }
    }

    static const std::vector<int>& g_indices(ParticleSystem& ps, kg::EntityID e) {
        return ps.get_entity_particle_indices(e);
    }

    // The board is a singleton in practice (one maze per process) and
    // the particle writers are static helpers, so it is reached through
    // here rather than threaded through every signature.
    static pacman::Board& g_board() {
        static pacman::Board b;
        return b;
    }

    void update_hud() {
        if (!hud_) return;
        char line[96];
        hud_->clear();
        std::snprintf(line, sizeof(line), "SCORE %-7d HIGH %d", score_, high_score_);
        hud_->add_line(line, 255, 240, 90);
        std::snprintf(line, sizeof(line), "LIVES %d   PELLETS %d",
                      lives_, pellets_remaining_);
        hud_->add_line(line, 190, 220, 255);
        const char* banner =
            phase_ == pacman::Phase::READY         ? "READY!" :
            phase_ == pacman::Phase::DYING         ? "CAUGHT" :
            phase_ == pacman::Phase::LEVEL_CLEARED ? "LEVEL CLEARED" :
            phase_ == pacman::Phase::GAME_OVER     ? "GAME OVER - R TO RESTART" : "";
        if (banner[0]) hud_->add_line(banner, 255, 120, 120);
    }

    static std::string fmt(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        return buf;
    }

    static uint32_t key(int c, int r) {
        return static_cast<uint32_t>(r) * 1000u + static_cast<uint32_t>(c);
    }

public:
    // Advances the presentation clocks. Separate from update_game so the
    // headless test can run the simulation without animating anything.
    void advance_anim(float dt) { anim_clock_ += dt; }

private:
    Engine* engine_ = nullptr;

    pacman::Board& board_ = g_board();
    bool board_ok_ = false;

    kg::EntityID maze_entity_    = kg::INVALID_ENTITY;
    kg::EntityID floor_entity_   = kg::INVALID_ENTITY;
    kg::EntityID avatar_entity_  = kg::INVALID_ENTITY;
    kg::EntityID pellet_pop_rule_ = kg::INVALID_ENTITY;

    pacman::Walker avatar_;
    std::vector<pacman::GhostSlot> ghosts_;
    std::vector<pacman::PelletSlot> pellets_;
    std::unordered_map<uint32_t, int> pellet_index_;

    pacman::Phase phase_ = pacman::Phase::READY;
    float phase_timer_ = 0.0f;
    float round_elapsed_ = 0.0f;
    float anim_clock_ = 0.0f;
    int   wave_index_ = 0;
    float wave_timer_ = 0.0f;
    int   ghost_chain_ = 0;

    int score_ = 0;
    int high_score_ = 0;
    int lives_ = pacman::kStartingLives;
    int pellets_remaining_ = 0;
    int ghosts_eaten_total_ = 0;
    int pellets_eaten_total_ = 0;   // cumulative across restarts
    int deaths_total_ = 0;

    uint32_t rng_ = 0x1234567u;

    std::unique_ptr<TextWindow> hud_;
};
