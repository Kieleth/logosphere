// Voyager entry point. The design is in voyager_app.h.
#include "voyager_app.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    bool pin = false;
    unsigned long long pinned = 0;
    for (int i = 1; i < argc; ++i) {
        // Characters are drawn from real entropy by default. --seed N
        // asks for one again: the dice are the only thing a seed fixes,
        // so a rerun with the same number rolls the same person.
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            pinned = std::strtoull(argv[++i], nullptr, 10);
            pin = true;
        }
    }

    voyager::VoyagerApplication app;
    if (pin) app.pin_seed(pinned);
    Engine engine(&app);

    EngineConfig config;
    config.create_display = true;
    config.window_width = voyager::kScreenW;
    config.window_height = voyager::kScreenH;
    config.window_title = "Voyager";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    // No chat box. This screen is read and clicked, not typed into.
    config.enable_chat_window = false;

    if (engine.initialize(config) < 0) {
        std::cerr << "[voyager] engine init failed" << std::endl;
        return 1;
    }

    auto last = std::chrono::high_resolution_clock::now();
    while (engine.should_continue()) {
        const auto now = std::chrono::high_resolution_clock::now();
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
