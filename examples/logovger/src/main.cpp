// Logovger entry point. See logovger_app.h for the design.
#include "logovger_app.h"

#include <chrono>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--headless") == 0) headless = true;

    logovger::LogovgerApplication app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width = 1400;
    config.window_height = 1000;
    config.window_title = "Logovger - character creation";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = true;

    if (engine.initialize(config) < 0) {
        std::cerr << "[logovger] engine init failed" << std::endl;
        return 1;
    }

    auto last = std::chrono::high_resolution_clock::now();
    while (headless ? false : engine.should_continue()) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;   // hitch guard

        engine.update(dt);
        engine.render();
        engine.present();
    }

    engine.shutdown();
    return 0;
}
