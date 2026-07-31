// =============================================================================
// Trail Fade Lab — interactive visual test for the crashed-trail fade
// =============================================================================
// A standalone window that reproduces ONLY the crashed-AI trail fade, driven
// by YOU, with none of the game's noise (no Director, no AI, no respawn racing
// the fade). Two identical rows of orange, self-emissive trail segments —
// styled by the same walls.h helpers the real game uses:
//
//   TOP row    = CONTROL. Never fades. Your bright reference.
//   BOTTOM row = the crashed-trail fade, fired on demand.
//
// Controls (also shown on-screen):
//   SPACE  fade the bottom row (alpha ramp over 2s, then it deletes)
//   R      reset both rows to bright
//   close window to quit
//
// Watching the fading row against the always-bright control answers the open
// question directly: does the self-emissive glow actually dim as alpha ramps?
// Because nothing here deletes the trail out from under the fade (unlike the
// game, where the AI respawn wipes it at ~2s), this also exercises the fade's
// OWN deferred deletion path — the one the game masks.
//
// Build/run:  cmake --build build --target logotron_trail_fade_lab
//             ./build/logotron/logotron_trail_fade_lab
// =============================================================================

#include "application.h"
#include "core/engine.h"
#include "core/camera_system.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"
#include "ui/ui_system.h"
#include "ui/text_window.h"

#include "walls.h"      // logotron::set_wall_geometry / set_wall_color
#include "materials.h"
#include "particle.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

constexpr float kFadeDur   = 2.0f;   // matches Logotron's kTrailFadeDuration
constexpr int   kSegments  = 6;      // segments per row
constexpr float kSegSpan   = 0.9f;   // segment length (small gap between)
constexpr float kRowY_fade =  0.9f;
constexpr float kRowY_ctrl = -0.9f;

enum class State { READY, FADING, FADED };

} // namespace

class TrailFadeLabApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();

        // One fade_out rule, reused every fire (same as the game arms
        // at the crash seal).
        fade_rule_ = kg.createEntity("TransformationRule");
        kg.setProperty(fade_rule_, "trigger", "on_timer");
        kg.setProperty(fade_rule_, "effect", "fade_out");
        kg.setProperty(fade_rule_, "duration_s", std::to_string(kFadeDur));
        kg.setProperty(fade_rule_, "name", "trail_fade_lab");
        engine_->get_interaction_system().load_rules_from_kg(kg);

        // Iso camera centered on the rows (screen-center == camera pos
        // in the engine's isometric projection).
        auto& cam = engine_->get_camera_system();
        cam.set_pixels_per_unit(64.0f);
        cam.set_position(0.0f, 0.0f, logotron::kWallTrailZ);
        cam.look_at(0.0f, 0.0f, logotron::kWallTrailZ);

        hud_ = std::make_unique<TextWindow>("TRAIL FADE LAB", "fade_lab_hud");
        hud_->set_bounds(24, 24, 480, 176);
        hud_->set_max_lines(8);
        hud_->set_background_alpha(190);
        engine_->get_ui_system()->add_widget(hud_.get());

        spawn_rows();
        set_state(State::READY);
    }

    void update_game(float dt) override {
        if (!engine_) return;
        if (state_ == State::FADING) {
            elapsed_ += dt;
            // Fade completes when the sampled particle is gone.
            auto& kg = engine_->get_kg();
            if (sample_kgid_ == 0 ||
                kg.getRenderIndex(sample_kgid_) == kg::INVALID_RENDER_INDEX) {
                set_state(State::FADED);
            } else if (++frame_ % 6 == 0) {
                update_hud();   // live alpha readout
            }
        }
    }

    bool handle_key(int key, int /*scancode*/, int action, int /*mods*/) override {
        if (action != GLFW_PRESS) return false;
        if (key == GLFW_KEY_SPACE) {
            if (state_ == State::READY) {
                engine_->get_interaction_system().arm_transformation(
                    fade_rule_, fading_ids_);
                elapsed_ = 0.0f;
                set_state(State::FADING);
            } else if (state_ == State::FADED) {
                reset_rows();
            }
            return true;
        }
        if (key == GLFW_KEY_R) { reset_rows(); return true; }
        return false;
    }

private:
    float sample_alpha() {
        if (sample_kgid_ == 0) return 0.0f;
        auto& kg = engine_->get_kg();
        auto idx = kg.getRenderIndex(sample_kgid_);
        if (idx == kg::INVALID_RENDER_INDEX) return 0.0f;
        auto v = engine_->get_particle_system().lock_particles_for_read();
        return v[idx].a;
    }

    void update_hud() {
        if (!hud_) return;
        hud_->clear();
        hud_->add_line("Bottom row = crashed-trail fade", 255, 170, 60);
        hud_->add_line("Top row    = control (stays bright)", 200, 200, 200);
        hud_->add_line("", 0, 0, 0);
        switch (state_) {
        case State::READY:
            hud_->add_line(">> PRESS SPACE to fade the bottom row", 120, 255, 120);
            break;
        case State::FADING: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "fading...  alpha = %.2f", sample_alpha());
            hud_->add_line(buf, 255, 230, 120);
            break;
        }
        case State::FADED:
            hud_->add_line("faded + deleted.  SPACE = reset", 120, 200, 255);
            break;
        }
        hud_->add_line("R = reset anytime", 150, 150, 150);
    }

    void set_state(State s) {
        state_ = s;
        update_hud();
        const char* name = s == State::READY ? "READY"
                         : s == State::FADING ? "FADING" : "FADED";
        std::fprintf(stderr, "[fade-lab] state -> %s\n", name);
    }

    kg::KGParticleID spawn_segment(float sx, float y, float ex) {
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();
        auto ent = kg.createEntity("Rock");   // engine type; visual is walls.h
        lab_entities_.push_back(ent);

        Particle p{};
        logotron::set_wall_geometry(p, sx, y, ex, y);   // resting, like real trails
        logotron::set_wall_color(p, /*is_player=*/false, /*is_director=*/false);
        ps.add_particle_to_entity(p, &kg, ent);

        auto kgids = kg.getEntityKGParticles(ent);
        return kgids.empty() ? kg::KGParticleID{0} : kgids[0];
    }

    void spawn_rows() {
        fading_ids_.clear();
        for (int i = 0; i < kSegments; ++i) {
            float sx = -3.0f + static_cast<float>(i) * 1.0f;
            spawn_segment(sx, kRowY_ctrl, sx + kSegSpan);          // control
            kg::KGParticleID id = spawn_segment(sx, kRowY_fade, sx + kSegSpan);
            if (id != 0) fading_ids_.push_back(id);                // fading
        }
        sample_kgid_ = fading_ids_.empty() ? 0 : fading_ids_.front();
    }

    void wipe_all() {
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();
        std::vector<int> idxs;
        for (auto ent : lab_entities_) {
            for (auto kgid : kg.getEntityKGParticles(ent)) {
                auto ri = kg.getRenderIndex(kgid);
                if (ri != kg::INVALID_RENDER_INDEX) idxs.push_back(static_cast<int>(ri));
            }
        }
        if (!idxs.empty()) ps.delete_particles_immediate(idxs);
        for (auto ent : lab_entities_) kg.destroyEntity(ent);
        lab_entities_.clear();
        fading_ids_.clear();
        sample_kgid_ = 0;
    }

    void reset_rows() {
        wipe_all();
        spawn_rows();
        set_state(State::READY);
    }

    Engine* engine_ = nullptr;
    kg::EntityID fade_rule_ = kg::INVALID_ENTITY;
    std::unique_ptr<TextWindow> hud_;
    std::vector<kg::EntityID> lab_entities_;
    std::vector<kg::KGParticleID> fading_ids_;
    kg::KGParticleID sample_kgid_ = 0;
    State state_ = State::READY;
    float elapsed_ = 0.0f;
    unsigned frame_ = 0;
};

int main() {
    std::fprintf(stderr,
        "==============================================================\n"
        "  Logotron Trail Fade Lab (interactive)\n"
        "  Bring the window to the front, then:\n"
        "    SPACE = fade the bottom row (2s, then it deletes)\n"
        "    R     = reset both rows to bright\n"
        "    close window to quit\n"
        "==============================================================\n");

    TrailFadeLabApp app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display     = true;
    config.window_width       = 1400;
    config.window_height      = 900;
    config.window_title       = "Logotron - Trail Fade Lab (SPACE to fade)";
    config.show_debug_overlay = false;
    config.show_kg_inspector  = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) < 0) {
        std::fprintf(stderr, "[fade-lab] ERROR: engine.initialize() failed.\n");
        return 1;
    }

    auto last = std::chrono::high_resolution_clock::now();
    while (engine.should_continue()) {
        auto now = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt > 0.1) dt = 0.1;
        engine.update(dt);
        engine.render();
        engine.present();
    }

    engine.shutdown();
    return 0;
}
