// Threshold entry point. See threshold_app.h for the design.
#include "threshold_app.h"

#include "core/engine.h"

#include <chrono>
#include <iostream>

int main() {
    threshold::ThresholdApp app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = true;
    config.window_width = 1600;
    config.window_height = 500;
    config.window_title = "Threshold";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;

    if (engine.initialize(config) < 0) {
        std::cerr << "[threshold] engine init failed" << std::endl;
        return 1;
    }

    auto last = std::chrono::high_resolution_clock::now();
    while (engine.should_continue()) {
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
