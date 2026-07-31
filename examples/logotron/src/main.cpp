// Logotron — Tron-style light cycle arena with LLM Weirden Director.
//
// v0.3: player cycle moves on a fixed-timestep tick. Arrow keys turn
// the cycle (Tron rule: no 180° U-turns, see is_legal_turn). No
// trails, no collisions yet. Runs the standard engine update/render/
// present loop. Quit with ESC.
//
// See LOGOTRON.md for the design overview and DEVLOG.md for the
// running build narrative.

#include "logotron_app.h"


int main(int argc, char* argv[]) {
    bool headless = false;
    double auto_reset_at = -1.0;  // seconds; <0 = disabled
    double auto_exit_at  = -1.0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-head" || arg == "--headless") headless = true;
        else if (arg == "--auto-reset-after" && i + 1 < argc) auto_reset_at = std::stod(argv[++i]);
        else if (arg == "--exit-after" && i + 1 < argc) auto_exit_at = std::stod(argv[++i]);
    }

    std::cout << "╔══════════════════════════════════════╗" << std::endl;
    std::cout << "║  LOGOTRON — foundation scaffold      ║" << std::endl;
    std::cout << "║  Light cycle arena on Logosphere     ║" << std::endl;
    std::cout << "╚══════════════════════════════════════╝" << std::endl;

    LogotronApplication app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Logotron";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;

    int observer = engine.initialize(config);
    if (observer < 0) {
        std::cerr << "[logotron] ERROR: engine.initialize() failed." << std::endl;
        return 1;
    }

    // Main loop. Same shape as Eden. Variable wall-clock dt; cycle
    // ticking is fixed-period inside update_game.
    //
    // Headless mode (--no-head / --headless) drives the SAME main
    // loop, just skipping render() + present() since there's no
    // window and no Metal pipeline. update_game still runs every
    // tick — Director request submit + drain, KG mutations, AI
    // stepping, telemetry. That's the whole point of headless: the
    // full game logic without GPU. Required for fast iteration on
    // Director / KG bugs and for headless ATs.
    auto last_frame = std::chrono::high_resolution_clock::now();
    auto started    = std::chrono::high_resolution_clock::now();
    bool auto_reset_done = false;
    if (headless) {
        std::cout << "[logotron] headless mode: running main loop "
                     "(no window, no render). --exit-after is the "
                     "only way out — set it." << std::endl;
        if (auto_exit_at <= 0) {
            std::cerr << "[logotron] WARNING: headless without --exit-after N "
                         "would loop forever; defaulting to 30 s." << std::endl;
            auto_exit_at = 30.0;
        }
    }
    // engine.should_continue() returns false immediately in headless
    // because PlatformMacOS::should_close() answers true when there's
    // no window. So the headless loop runs purely on the
    // --exit-after wall-clock budget.
    while (headless ? true : engine.should_continue()) {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - started).count();
        if (auto_reset_at > 0 && !auto_reset_done && elapsed >= auto_reset_at) {
            std::cerr << "[logotron] --auto-reset-after " << auto_reset_at
                      << " s — firing reset now." << std::endl;
            app.handle_key(GLFW_KEY_R, 0, GLFW_PRESS, 0);
            auto_reset_done = true;
        }
        if (auto_exit_at > 0 && elapsed >= auto_exit_at) {
            std::cerr << "[logotron] --exit-after " << auto_exit_at
                      << " s — exiting." << std::endl;
            break;
        }
        double dt = std::chrono::duration<double>(now - last_frame).count();
        last_frame = now;
        if (dt > 0.1) dt = 0.1;  // clamp huge stalls

        engine.update(dt);
        if (!headless) {
            engine.render();
            engine.present();
        }
    }

    std::cout << "[logotron] main loop exited; shutting down." << std::endl;
    engine.shutdown();
    return 0;
}
