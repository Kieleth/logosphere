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
    // The engine's text field is the open door: it shows only while
    // the player's own words are wanted, and is hidden otherwise.
    config.enable_chat_window = true;

    if (engine.initialize(config) < 0) {
        std::cerr << "[voyager] engine init failed" << std::endl;
        return 1;
    }
    // The game draws on the window's own grid, so the pointer and the
    // drawing agree without any mapping. Bigger type comes from the
    // draw surface's scaled text, in this game's own widgets, not from
    // rendering smaller and scaling up: that put every click short by
    // the scale (2026-09-02).

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
