// Minimal HTTP client test - no engine initialization
// Tests if the HTTP client works in isolation

#include <iostream>
#include "logosphere/llm/http_client.h"

int main() {
    std::cout << "Testing HTTP client standalone (no engine)..." << std::endl;

    Logosphere::HTTPClient client("localhost", 8000);

    // Test 1: Simple request
    std::string json = R"({"model": "mlx-community/Qwen2.5-32B-Instruct-4bit", "messages": [{"role": "user", "content": "Say hi"}], "max_tokens": 5})";

    std::cout << "Sending " << json.length() << " byte request..." << std::endl;

    auto response = client.post("/v1/chat/completions", json, "application/json");

    if (response.error) {
        std::cout << "❌ ERROR: " << response.error_message << std::endl;
        std::cout << "Status code: " << response.status_code << std::endl;
        return 1;
    }

    std::cout << "✅ SUCCESS!" << std::endl;
    std::cout << "Status: " << response.status_code << std::endl;
    std::cout << "Body length: " << response.body.length() << std::endl;
    std::cout << "Body: " << response.body.substr(0, 200) << "..." << std::endl;

    return 0;
}
