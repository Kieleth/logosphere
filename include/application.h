#ifndef LOGOSPHERE_APPLICATION_H
#define LOGOSPHERE_APPLICATION_H

#include <cstdint>
#include <functional>
#include "particle.h"

// Forward declarations
struct GLFWwindow;

namespace Logosphere {

/**
 * IApplication - Interface between engine and platform/game layers
 * 
 * This interface allows the engine to remain independent of:
 * - Platform-specific code (window creation, display)
 * - Game-specific logic (initialization, updates)
 * 
 * Games implement this interface to use the Logosphere engine
 */
class IApplication {
public:
    virtual ~IApplication() = default;
    
    // ===== Lifecycle =====
    /**
     * Initialize the application (window, resources, etc.)
     * Called once at startup before engine initialization
     */
    virtual bool initialize() = 0;
    
    /**
     * Clean up application resources
     * Called once at shutdown after engine cleanup
     */
    virtual void shutdown() = 0;
    
    // ===== Platform Layer =====
    /**
     * Get the GLFW window handle
     * Engine needs this for input handling
     */
    virtual GLFWwindow* get_window() = 0;
    
    // ===== Game-Specific Logic (Optional) =====
    /**
     * Initialize game-specific content
     * Called after engine systems are initialized
     * @param engine_ptr Pointer to the engine for accessing systems
     */
    virtual void initialize_game(void* engine_ptr = nullptr) {}
    
    /**
     * Update game logic
     * Called each frame before physics/animation
     * @param dt Delta time in seconds
     */
    virtual void update_game(float dt) {}

    /**
     * Update game logic after physics and animation
     * Called after physics resolution and kinematic animations are applied.
     * Use this for collision detection that needs to see animated positions
     * (e.g., punch hit detection needs post-animation hand positions).
     * @param dt Delta time in seconds
     */
    virtual void update_game_post_physics(float dt) {}
    
    /**
     * Render game-specific visuals
     * Called after engine rendering, before display
     */
    virtual void render_game() {}
    
    /**
     * Handle keyboard input
     * @param key GLFW key code
     * @param scancode Platform-specific scancode
     * @param action GLFW_PRESS, GLFW_RELEASE, or GLFW_REPEAT
     * @param mods Modifier keys bitfield
     * @return true if handled, false to pass to engine
     */
    virtual bool handle_key(int key, int scancode, int action, int mods) { 
        (void)key; (void)scancode; (void)action; (void)mods;
        return false; 
    }
    
    /**
     * Handle mouse button input
     * @param button GLFW mouse button code
     * @param action GLFW_PRESS or GLFW_RELEASE
     * @param mods Modifier keys bitfield
     * @return true if handled, false to pass to engine
     */
    virtual bool handle_mouse_button(int button, int action, int mods) {
        (void)button; (void)action; (void)mods;
        return false;
    }
    
    /**
     * Handle mouse movement
     * @param xpos Mouse X position in screen coordinates
     * @param ypos Mouse Y position in screen coordinates
     * @return true if handled, false to pass to engine
     */
    virtual bool handle_mouse_move(double xpos, double ypos) {
        (void)xpos; (void)ypos;
        return false;
    }
    
    /**
     * Handle mouse scroll
     * @param xoffset Horizontal scroll offset
     * @param yoffset Vertical scroll offset
     * @return true if handled, false to pass to engine
     */
    virtual bool handle_mouse_scroll(double xoffset, double yoffset) {
        (void)xoffset; (void)yoffset;
        return false;
    }
    
    // ===== Game Content Creation (Optional) =====
    /**
     * Create initial scene/level
     * Called after initialize_game()
     * 
     * @param add_particle Function to add particles to the world
     *        Pass a Particle struct and it will be added to the engine's particle system
     *        Returns the ID of the added particle
     * @return ID of the entity that should receive input (or 0 for no input target)
     *         NOTE: This now returns an entity ID, not a particle ID!
     * 
     * TODO[ARCH-003]: Expand this to a full SceneBuilder interface with lights, sounds, etc.
     */
    virtual unsigned int create_initial_scene(
        std::function<int(const Particle&)> add_particle = nullptr
    ) { return 0; }
    
    /**
     * Get the desired window width
     * Used during initialization
     */
    virtual int get_window_width() const { return 1600; }
    
    /**
     * Get the desired window height
     * Used during initialization
     */
    virtual int get_window_height() const { return 1200; }
    
    /**
     * Get the Knowledge Graph module pointer (optional)
     * Used by the KG Inspector for debugging
     * @return Pointer to KG module, or nullptr if not using KG
     */
    virtual void* getKGModule() { return nullptr; }
    
    /**
     * Get the application name
     * Used for window title
     */
    virtual const char* get_app_name() const { return "Logosphere Application"; }
};

} // namespace Logosphere

#endif // LOGOSPHERE_APPLICATION_H