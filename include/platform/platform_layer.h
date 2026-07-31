#ifndef LOGOSPHERE_PLATFORM_LAYER_H
#define LOGOSPHERE_PLATFORM_LAYER_H

#include "../application.h"
#include <memory>

namespace Logosphere {

/**
 * PlatformLayer - Base implementation of IApplication for platform-specific code
 * 
 * This class handles the platform-specific parts (window, display)
 * while leaving game logic abstract for games to implement
 */
class PlatformLayer : public IApplication {
protected:
    GLFWwindow* window = nullptr;
    int window_width;
    int window_height;
    const char* app_name;
    
public:
    PlatformLayer() : window_width(1600), window_height(1200), app_name("Logosphere") {}
    virtual ~PlatformLayer() = default;
    
    // IApplication interface - Platform implementation
    bool initialize() override;
    void shutdown() override;
    GLFWwindow* get_window() override { return window; }
    
    // Window configuration (can be overridden)
    int get_window_width() const override { return window_width; }
    int get_window_height() const override { return window_height; }
    const char* get_app_name() const override { return app_name; }
    
    // Set window properties before initialization
    void set_window_size(int width, int height) {
        window_width = width;
        window_height = height;
    }
    
    void set_app_name(const char* name) {
        app_name = name;
    }
};

/**
 * Create platform-specific implementation
 * Each platform provides this function
 */
std::unique_ptr<PlatformLayer> create_platform_layer();

} // namespace Logosphere

#endif // LOGOSPHERE_PLATFORM_LAYER_H