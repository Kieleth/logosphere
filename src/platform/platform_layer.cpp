#include "platform/platform_layer.h"
#include <GLFW/glfw3.h>
#include <iostream>

namespace Logosphere {

bool PlatformLayer::initialize() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    
    // Configure GLFW for no OpenGL context (we're software rendering)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    // Create window
    window = glfwCreateWindow(
        window_width, 
        window_height, 
        app_name, 
        nullptr, 
        nullptr
    );
    
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    
    // Center window on screen
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (primary) {
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        if (mode) {
            int xpos = (mode->width - window_width) / 2;
            int ypos = (mode->height - window_height) / 2;
            glfwSetWindowPos(window, xpos, ypos);
        }
    }
    
    return true;
}

void PlatformLayer::shutdown() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

} // namespace Logosphere