// Logovger entry point. See logovger_app.h for the design.
#include "logovger_app.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    bool headless = false;
    bool pin = false;
    unsigned long long pinned = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) headless = true;
        // Lives are random by default. --seed N replays a specific
        // one: the seed of every life is printed on screen, so a life
        // worth keeping can be asked for again.
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            pinned = std::strtoull(argv[++i], nullptr, 10);
            pin = true;
        }
    }

    logovger::LogovgerApplication app;
    if (pin) app.pin_seed(pinned);
    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width = logovger::kScreenW;
    config.window_height = logovger::kScreenH;
    config.window_title = "Logovger - character creation";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    // No chat box: this screen is clickable. A rulebook you can
    // interrogate should not be a scrolling log.
    config.enable_chat_window = false;

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
