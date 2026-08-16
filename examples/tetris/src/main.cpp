// Tetris on Logosphere — driver.
//
// The engine is a library: it does not own the loop. This is the same
// update / render / present chain examples/logotron/src/main.cpp
// drives, minus the LLM.
//
//   ./build/tetris/tetris                     windowed
//   ./build/tetris/tetris --no-head --exit-after 5 --seed 7   headless

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "tetris_app.h"

// Headless proof that something was actually drawn. Reads the GPU
// frame back and writes a binary PPM, because the alternative is
// claiming the board renders without ever having looked at it.
static bool write_shot(Engine& engine, const std::string& path) {
    int w = 0, h = 0;
    if (!engine.read_latest_framebuffer(nullptr, w, h)) {
        // First call with a null buffer only asks for the size; if the
        // engine refuses that too there is no frame to read.
    }
    w = engine.get_resolution_manager().get_render_width();
    h = engine.get_resolution_manager().get_render_height();
    if (w <= 0 || h <= 0) return false;

    std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0u);
    int gw = w, gh = h;
    if (!engine.read_latest_framebuffer(px.data(), gw, gh)) {
        std::fprintf(stderr, "[tetris] read_latest_framebuffer failed\n");
        return false;
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", gw, gh);
    for (size_t i = 0; i < static_cast<size_t>(gw) * gh; i++) {
        const uint32_t p = px[i];                    // BGRA
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((p >> 16) & 0xFF),
            static_cast<unsigned char>((p >> 8) & 0xFF),
            static_cast<unsigned char>(p & 0xFF)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("[tetris] wrote %s (%dx%d)\n", path.c_str(), gw, gh);
    return true;
}

int main(int argc, char* argv[]) {
    bool headless = false;
    double exit_after = -1.0;
    unsigned seed = 0;
    bool have_seed = false;
    std::string shot_path;
    int shot_frame = 60;
    bool autoplay = false;
    bool test_restart = false;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--no-head" || arg == "--headless") headless = true;
        else if (arg == "--exit-after" && i + 1 < argc) exit_after = std::stod(argv[++i]);
        else if (arg == "--shot" && i + 1 < argc) shot_path = argv[++i];
        else if (arg == "--auto") autoplay = true;
        else if (arg == "--test-restart") test_restart = true;
        else if (arg == "--shot-frame" && i + 1 < argc) shot_frame = std::stoi(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<unsigned>(std::stoul(argv[++i]));
            have_seed = true;
        }
    }

    tetris::TetrisApplication app;
    if (have_seed) app.set_seed(seed);
    app.set_autoplay(autoplay);

    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width = 1000;
    config.window_height = 1200;
    config.window_title = "Tetris";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) < 0) {
        std::fprintf(stderr, "[tetris] engine.initialize() failed\n");
        return 1;
    }

    if (headless && exit_after <= 0.0) {
        std::fprintf(stderr,
                     "[tetris] headless without --exit-after would never "
                     "return; using 10 s\n");
        exit_after = 10.0;
    }

    const auto started = std::chrono::high_resolution_clock::now();
    auto last = started;
    long frame = 0;
    bool restarted = false, restart_ok = true;

    // A screenshot run steps a fixed 1/60 s so the board looks the same
    // every time; a real run uses the wall clock.
    const bool shooting = !shot_path.empty();

    // should_continue() answers false with no window, so headless runs
    // on the wall-clock budget instead. Same shape as logotron.
    while (headless ? true : engine.should_continue()) {
        const auto now = std::chrono::high_resolution_clock::now();
        double dt = shooting ? (1.0 / 60.0)
                             : std::chrono::duration<double>(now - last).count();
        last = now;
        if (dt > 0.1) dt = 0.1;

        if (!shooting && exit_after > 0.0 &&
            std::chrono::duration<double>(now - started).count() >= exit_after) {
            break;
        }

        engine.update(dt);
        // Headless still renders when a shot was asked for: the GPU
        // path runs without a window, only present() needs one.
        if (!headless || shooting) engine.render();
        if (!headless) engine.present();
        frame++;

        if (shooting && frame >= shot_frame) {
            write_shot(engine, shot_path);
            break;
        }
        if (headless && !shooting && app.over()) {
            // Exercise the R key through the same handler the keyboard
            // reaches, once, then let the run finish. Logotron drives
            // its own reset from main the same way
            // (examples/logotron/src/main.cpp, --auto-reset-after).
            if (test_restart && !restarted) {
                std::printf("[tetris] restart probe: score before=%d lines=%d\n",
                            app.score(), app.lines());
                app.handle_key(GLFW_KEY_R, 0, GLFW_PRESS, 0);
                restarted = true;
                if (app.over() || app.score() != 0 || app.lines() != 0) {
                    std::fprintf(stderr, "[tetris] RESTART FAILED: over=%d "
                                 "score=%d lines=%d\n",
                                 app.over() ? 1 : 0, app.score(), app.lines());
                    restart_ok = false;
                }
                continue;
            }
            break;
        }
    }

    // Does the graph grow without bound? A game that creates and
    // destroys a mino entity per cell is the obvious stress on that.
    const auto st = engine.get_kg().getStats();
    std::printf("[tetris] final: score=%d lines=%d level=%d over=%s\n",
                app.score(), app.lines(), app.level(),
                app.over() ? "yes" : "no");
    std::printf("[tetris] kg: entities=%zu relations=%zu bytes=%zu | particles=%zu\n",
                st.entity_count, st.relation_count, st.memory_bytes,
                engine.get_particle_system().count());
    if (test_restart) {
        std::printf("[tetris] restart probe: %s (played on to score=%d lines=%d)\n",
                    (restarted && restart_ok) ? "PASS" : "FAIL",
                    app.score(), app.lines());
    }
    engine.shutdown();
    return (test_restart && !(restarted && restart_ok)) ? 1 : 0;
}
