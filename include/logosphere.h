#pragma once

/**
 * Logosphere - The Universal Organizing Principle for Particle-Based Reality
 * 
 * "From Chaos, Order. From Particles, Worlds. From Logos, Meaning."
 * 
 * The Logosphere provides the cosmic framework for organizing particle swarms
 * into coherent, meaningful worlds. It serves as the bridge between the 
 * fundamental chaos of individual particles and the emergent order of
 * complex realities.
 */

namespace Logosphere {
    
    // Forward declarations for the core organizing systems
    class ParticleSystem;    // The fundamental units of reality
    class RenderSystem;      // Visual manifestation of the Logos
    class PhysicsSystem;     // Natural laws governing interactions
    class LightSystem;       // Illumination and visibility
    class VisionSystem;      // Perception and awareness
    class InputSystem;       // Interface between consciousness and reality
    
    /**
     * The primary interface to the Logosphere.
     * This represents the cosmic organizing principle itself.
     */
    class Engine {
    public:
        // Core lifecycle
        virtual void initialize() = 0;
        virtual void update(float delta_time) = 0;
        virtual void render() = 0;
        virtual void shutdown() = 0;
        
        // System access
        virtual ParticleSystem* get_particle_system() = 0;
        virtual RenderSystem* get_render_system() = 0;
        virtual PhysicsSystem* get_physics_system() = 0;
        virtual LightSystem* get_light_system() = 0;
        virtual VisionSystem* get_vision_system() = 0;
        virtual InputSystem* get_input_system() = 0;
        
        virtual ~Engine() = default;
    };
    
    /**
     * Configuration for initializing the Logosphere.
     * These parameters define the fundamental constants of your reality.
     */
    struct LogosphereConfig {
        int window_width = 1600;
        int window_height = 1200;
        int target_fps = 60;
        bool enable_vsync = true;
        bool enable_debug_overlay = false;
    };
    
    /**
     * Factory function to create a Logosphere instance.
     * This births a new universe governed by the Logos.
     */
    Engine* create_logosphere(const LogosphereConfig& config = {});
    
    /**
     * Version information
     */
    const char* get_version();
    const char* get_build_date();
    
} // namespace Logosphere