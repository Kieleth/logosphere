// Logomanpac on Logosphere — entry point and main loop.
//
//   ./build/logomanpac/logomanpac                 windowed
//   ./build/logomanpac/logomanpac --no-head --exit-after 20
//                                         no window, same game logic
//
// The --no-head flag is parsed HERE, by the game, not by the engine:
// nothing in the engine looks at argv. Every example in this repo
// parses it separately and they do not agree with each other, so this
// one says what it does.

#include "logomanpac_app.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    bool headless = false;
    double exit_after = -1.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-head" || a == "--headless") headless = true;
        else if (a == "--exit-after" && i + 1 < argc) exit_after = std::stod(argv[++i]);
    }
    if (headless && exit_after <= 0.0) exit_after = 20.0;

    std::cout << "LOGOMANPAC on Logosphere" << std::endl;

    LogomanpacApplication app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width  = 1100;
    config.window_height = 1200;
    config.window_title  = "Logomanpac";
    config.show_debug_overlay = false;
    config.show_kg_inspector  = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) < 0) {
        std::cerr << "[logomanpac] engine.initialize() failed" << std::endl;
        return 1;
    }

    auto started    = std::chrono::high_resolution_clock::now();
    auto last_frame = started;

    // engine.should_continue() answers false immediately with no window,
    // so the headless path runs on the wall-clock budget instead.
    while (headless ? true : engine.should_continue()) {
        const auto now = std::chrono::high_resolution_clock::now();
        const double elapsed = std::chrono::duration<double>(now - started).count();
        if (exit_after > 0.0 && elapsed >= exit_after) break;

        double dt = std::chrono::duration<double>(now - last_frame).count();
        last_frame = now;
        if (dt > 0.1) dt = 0.1;

        engine.update(dt);
        if (!headless) {
            engine.render();
            engine.present();
        }
    }

    std::cout << "[logomanpac] final score " << app.score()
              << ", lives " << app.lives()
              << ", pellets left " << app.pellets_remaining() << std::endl;
    engine.shutdown();
    return 0;
}
