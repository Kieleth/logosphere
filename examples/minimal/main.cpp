// The smallest complete game on Logosphere.
//
// A window, a floor, five boxes, and one you drive with the arrow keys.
// Everything here is mandatory. Nothing here is decoration. If you are
// reading docs/GETTING_STARTED.md, this is the file it quotes, and it is
// compiled by CI, so it cannot quietly stop being true.
//
//   ./build/minimal/minimal                      windowed
//   ./build/minimal/minimal --no-head --exit-after 2    no window
//   ./build/minimal/minimal --shot out.ppm --exit-after 2   proof it drew
//
// Six things are not obvious and every one of them will cost you an
// afternoon if you guess:
//
//   1. The GAME owns main(). The engine is a library you drive, not a
//      framework that calls you. See the loop at the bottom.
//   2. get_window() returns nullptr. The engine makes the window from
//      EngineConfig; the hook is for games that bring their own.
//   3. ProjectionMode::BirdsEye is what makes a 2D game possible. The
//      camera position lands at SCREEN CENTRE, so to centre something,
//      park the camera on it.
//   4. set_pixels_per_unit is the zoom. Without it your world is either
//      one pixel wide or off the edge of the screen.
//   5. is_self_emissive means "colour IS the pixel". Without it a
//      particle is lit by a scene that has no lights in it, and you get
//      a black screen and no error.
//   6. z is a CENTRE, and the bottom of a body may not go below zero.
//      Centre a floor at z = 0 and the engine aborts on purpose, with a
//      good message. That is the world turtle, and it is not negotiable.

#include <chrono>
#include <cstdio>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "application.h"
#include "core/camera_system.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "core/projection_mode.h"

namespace {
constexpr float kFloorThickness = 0.20f;
constexpr float kFloorZ         = kFloorThickness * 0.5f;  // bottom sits at 0
constexpr float kBoxSize        = 1.0f;
constexpr float kBoxZ           = kFloorThickness + kBoxSize * 0.5f;
constexpr float kStep           = 1.0f;   // world units per key press
constexpr float kHalfWorld      = 7.0f;   // the box stays inside this
}  // namespace

class MinimalApplication : public Logosphere::IApplication {
public:
    // ---- the five IApplication members every game implements ----------
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }  // see note 2
    const char* get_app_name() const override { return "Minimal"; }
    void* getKGModule() override {
        return engine_ ? static_cast<void*>(&engine_->get_kg()) : nullptr;
    }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);

        // Note 3 and 4: without these two calls you render a correct
        // world that nobody can see.
        engine_->set_projection_mode(ProjectionMode::BirdsEye);
        auto& cam = engine_->get_camera_system();
        cam.set_position(0.0f, 0.0f, 30.0f);
        cam.look_at(0.0f, 0.0f, 0.0f);
        cam.set_pixels_per_unit(48.0f);

        build_floor();
        build_boxes();

        std::cout << "[minimal] arrow keys move the yellow box, R recentres"
                  << std::endl;
    }

    // Called every frame with the real elapsed time. The four decor boxes
    // bob so you can see at a glance that the loop is alive.
    void update_game(float dt) override {
        clock_ += dt;
        auto& ps = engine_->get_particle_system();
        auto view = ps.lock_particles_for_write();
        for (size_t i = 0; i < decor_.size(); ++i) {
            const int idx = decor_[i];
            if (idx < 0 || static_cast<size_t>(idx) >= view.size()) continue;
            view[idx].z = kBoxZ + 0.25f * std::sin(clock_ * 2.0f +
                                                   static_cast<float>(i));
        }
        if (player_ >= 0 && static_cast<size_t>(player_) < view.size()) {
            view[player_].x = px_;
            view[player_].y = py_;
        }
    }

    bool handle_key(int key, int, int action, int) override {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
        switch (key) {
            case GLFW_KEY_UP:    py_ += kStep; break;
            case GLFW_KEY_DOWN:  py_ -= kStep; break;
            case GLFW_KEY_LEFT:  px_ -= kStep; break;
            case GLFW_KEY_RIGHT: px_ += kStep; break;
            case GLFW_KEY_R:     px_ = py_ = 0.0f; break;
            default: return false;
        }
        px_ = std::fmax(-kHalfWorld, std::fmin(kHalfWorld, px_));
        py_ = std::fmax(-kHalfWorld, std::fmin(kHalfWorld, py_));
        return true;
    }

    // So the headless run can prove it moved rather than promise it.
    float player_x() const { return px_; }
    float player_y() const { return py_; }

private:
    // Every particle is styled through here, so note 5 is decided once.
    static void style(Particle& p, float r, float g, float b) {
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.owner = ParticleOwner::STATIC;
        p.is_at_rest = true;
        p.is_light_source  = false;
        p.is_self_emissive = true;   // note 5
        p.emission_strength = 1.0f;
        p.emission_radius   = 0.0f;
    }

    void build_floor() {
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = kFloorZ;   // note 6
        p.width = p.height = kHalfWorld * 2.0f + kBoxSize * 2.0f;
        p.thickness = kFloorThickness;
        style(p, 0.05f, 0.05f, 0.12f);
        // AutoParticle is a concrete type in the engine's own ontology,
        // so this example needs no schema of its own. Your game declares
        // its types instead: see docs/GETTING_STARTED.md.
        ps.add_particle_to_entity(p, &kg, kg.createEntity("AutoParticle"));
    }

    void build_boxes() {
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();
        const float corners[4][2] = {{-4, 4}, {4, 4}, {-4, -4}, {4, -4}};
        for (const auto& c : corners) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.x = c[0]; p.y = c[1]; p.z = kBoxZ;
            p.width = p.height = p.thickness = kBoxSize;
            style(p, 0.20f, 0.45f, 0.95f);
            decor_.push_back(
                ps.add_particle_to_entity(p, &kg, kg.createEntity("AutoParticle")));
        }
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = px_; p.y = py_; p.z = kBoxZ;
        p.width = p.height = p.thickness = kBoxSize;
        style(p, 1.0f, 0.85f, 0.10f);
        // Caching the index is safe HERE because this example never
        // deletes a particle. Deletion swap-and-pops the array, so a game
        // that removes things must look the index up each frame instead.
        player_ = ps.add_particle_to_entity(p, &kg, kg.createEntity("AutoParticle"));
    }

    Engine* engine_ = nullptr;
    std::vector<int> decor_;
    int   player_ = -1;
    float px_ = 0.0f, py_ = 0.0f;
    float clock_ = 0.0f;
};

int main(int argc, char* argv[]) {
    bool headless = false;
    double exit_after = -1.0;
    std::string shot;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-head" || a == "--headless") headless = true;
        else if (a == "--exit-after" && i + 1 < argc) exit_after = std::stod(argv[++i]);
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
    }
    if (headless && exit_after <= 0.0) exit_after = 2.0;

    MinimalApplication app;
    Engine engine(&app);           // note 1: you own this object

    EngineConfig config;
    config.create_display = !headless;
    config.window_width  = 900;
    config.window_height = 900;
    config.window_title  = "Minimal";
    if (engine.initialize(config) < 0) {
        std::cerr << "[minimal] engine.initialize() failed" << std::endl;
        return 1;
    }

    const auto started = std::chrono::high_resolution_clock::now();
    auto last = started;
    // should_continue() answers false immediately with no window, so the
    // headless path runs on a clock instead.
    while (headless ? true : engine.should_continue()) {
        const auto now = std::chrono::high_resolution_clock::now();
        if (exit_after > 0.0 &&
            std::chrono::duration<double>(now - started).count() >= exit_after) break;
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt > 0.1) dt = 0.1;

        engine.update(dt);
        if (!headless || !shot.empty()) { engine.render(); engine.present(); }
    }

    // --shot proves the thing rendered instead of promising it. The
    // engine hands back BGRA; this writes a PPM any viewer can open.
    if (!shot.empty()) {
        int w = 0, h = 0;
        std::vector<uint32_t> px(4096 * 4096);
        if (engine.read_latest_framebuffer(px.data(), w, h)) {
            if (FILE* f = std::fopen(shot.c_str(), "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (int i = 0; i < w * h; ++i) {
                    const uint32_t p = px[i];
                    const unsigned char rgb[3] = {
                        static_cast<unsigned char>((p >> 16) & 0xFF),
                        static_cast<unsigned char>((p >> 8) & 0xFF),
                        static_cast<unsigned char>(p & 0xFF)};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
                std::cout << "[minimal] wrote " << shot << " (" << w << "x" << h
                          << ")" << std::endl;
            }
        }
    }

    std::cout << "[minimal] player ended at " << app.player_x() << ", "
              << app.player_y() << std::endl;
    engine.shutdown();
    return 0;
}
