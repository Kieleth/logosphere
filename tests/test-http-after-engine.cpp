// Test HTTP client from main thread after engine initialization
// Tests if engine init breaks socket operations

#include <iostream>
#include "application.h"
#include "core/engine.h"
#include "logosphere/llm/http_client.h"

// Headless test application (same as logomancers but no window)
class HTTPTestApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }
    void initialize_game(void* engine_ptr) override {
        std::cout << "[Test] Headless engine initialized" << std::endl;
    }
    unsigned int create_initial_scene(std::function<int(const Particle&)> add_particle) override {
        // Minimal scene: just one light
        Particle light = {};
        light.x = 0.0f;
        light.y = 0.0f;
        light.z = 10.0f;
        light.size = 0.5f;
        light.r = 1.0f;
        light.g = 1.0f;
        light.b = 1.0f;
        light.SetMaterial(Materials::Type::LIGHT);  // Floats - gravity negligible
        light.is_light_source = true;
        light.emission_strength = 1000000.0f;
        light.emission_radius = 200.0f;
        add_particle(light);
        return 1;
    }
};

int main() {
    std::cout << "Testing HTTP after engine init (main thread)..." << std::endl;

    // Initialize application and engine
    HTTPTestApp app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = false;
    config.window_width = 1600;
    config.window_height = 1200;

    if (engine.initialize(config) < 0) {
        std::cerr << "Failed to initialize engine" << std::endl;
        return 1;
    }

    std::cout << "Engine initialized, now testing HTTP..." << std::endl;

    // Make HTTP request from main thread
    Logosphere::HTTPClient client("localhost", 8000);

    std::string json = R"({"model": "mlx-community/Qwen2.5-32B-Instruct-4bit", "messages": [{"role": "user", "content": "Say hi"}], "max_tokens": 5})";

    std::cout << "Sending " << json.length() << " byte request..." << std::endl;

    auto response = client.post("/v1/chat/completions", json, "application/json");

    if (response.error) {
        std::cout << "ERROR: " << response.error_message << std::endl;
        std::cout << "Status code: " << response.status_code << std::endl;
        return 1;
    }

    std::cout << "SUCCESS!" << std::endl;
    std::cout << "Status: " << response.status_code << std::endl;
    std::cout << "Body length: " << response.body.length() << std::endl;

    return 0;
}
