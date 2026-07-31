// Logogenesis entry point. See logogenesis_app.h for the design.
#include "logogenesis_app.h"

#include <chrono>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) headless = true;
    }

    logogenesis::LogogenesisApplication app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Logogenesis";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = true;

    if (engine.initialize(config) < 0) {
        std::cerr << "[logogenesis] engine init failed" << std::endl;
        return 1;
    }

    auto last = std::chrono::high_resolution_clock::now();
    while (headless ? true : engine.should_continue()) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;   // hitch guard

        engine.update(dt);
        if (!headless) {
            engine.render();
            engine.present();
        }
    }

    engine.shutdown();
    return 0;
}
