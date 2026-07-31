// lighting_config.h - Complete centralized lighting configuration
// 
// EDUCATIONAL NOTE: The Lighting Configuration System
//
// After struggling with scattered magic numbers causing brightness inconsistencies,
// shadow rendering failures, and accumulation bugs, I realized we needed a single
// source of truth for ALL lighting parameters.
//
// This module centralizes every constant, threshold, and tuning parameter related
// to lighting, with clear documentation about what each value does and why.
//
// Key lessons learned:
// 1. Magic numbers scattered across files lead to inconsistencies
// 2. Different render paths using different normalization = visual bugs
// 3. Without context (lumens, distances), values are meaningless
// 4. Accumulation bugs happen when expectations don't match reality

#ifndef LIGHTING_CONFIG_H
#define LIGHTING_CONFIG_H

#include <algorithm>
#include <cstdint>
#include <memory>
#include <cmath>
#include <string>
#include <vector>

class LightingConfig {
public:
    // ===== RAY TRACING CORE PARAMETERS =====
    struct RayTracing {
        // Ray emission - GAME PARAMETER: Adjusted for computational performance vs quality
        int rays_per_light = 1000;              // LIGHT_RAY_COUNT - number of rays each light emits
        float ray_distribution = 1.618034f;     // Golden ratio for Fibonacci sphere - PHYSICS CONSTANT
        
        // Ray propagation - PHYSICS PARAMETERS
        int max_bounces = 1;                    // MAX_LIGHT_BOUNCES - 0=direct only (global illumination later)
        float min_intensity_threshold = 0.01f;  // Below 0.01 lumens/ray = imperceptible (human vision limit)
        float min_distance = 0.1f;              // 10cm minimum to avoid singularity (realistic light source size)
        
        // Ray cutoff - GAME PARAMETER: Balanced for performance vs realism
        float default_emission_radius = 30.0f;  // 30 meters maximum range (reasonable for game scenes)
    } ray_tracing;
    
    // ===== LIGHT SOURCE INTENSITIES =====
    struct LightSources {
        // Real-world references - PHYSICS CONSTANTS (accurate lumens)
        struct RealWorld {
            float candle = 12.0f;                   // Candle flame: 12 lumens
            float torch = 400.0f;                   // Fire torch: 400 lumens  
            float led_flashlight = 100.0f;          // Small LED flashlight: 100 lumens
            float incandescent_40w = 450.0f;        // 40W incandescent bulb: 450 lumens
            float incandescent_60w = 800.0f;        // 60W incandescent bulb: 800 lumens
            float incandescent_100w = 1600.0f;      // 100W incandescent bulb: 1600 lumens
            float car_headlight = 1200.0f;          // Car headlight: 1200 lumens
            float bonfire = 50000.0f;               // Large bonfire: 50,000 lumens
            float street_light = 10000.0f;          // Street lamp: 10,000 lumens
            float stadium_light = 100000.0f;        // Stadium floodlight: 100,000 lumens
            float full_moon = 0.25f;                // Full moon illuminance: 0.25 lux (not lumens!)
            float overcast_day = 1000.0f;           // Overcast daylight: ~1000 lux
            float direct_sunlight = 100000.0f;      // Direct sunlight: ~100,000 lux
        } real_world;
        
        // Game emission strengths - REAL LUMENS (engine distributes across rays internally)
        // CRITICAL FIX: These are actual lumen values, NOT multiplied by ray count!
        // The engine does: intensity_per_ray = emission_strength / rays_per_light
        struct GameScale {
            float dim_candle = 12.0f;           // Candle: 12 lumens (matches real_world.candle)
            float flashlight = 100.0f;          // LED flashlight: 100 lumens
            float torch = 400.0f;               // Fire torch: 400 lumens (matches real_world.torch)
            float bright_bulb = 1600.0f;        // 100W bulb: 1600 lumens (matches real_world.incandescent_100w)
            float car_headlight = 1200.0f;      // Car headlight: 1200 lumens
            float street_light = 10000.0f;      // Street light: 10,000 lumens  
            float bonfire = 50000.0f;           // Large bonfire: 50,000 lumens
            float stadium_light = 100000.0f;    // Stadium floodlight: 100,000 lumens (most common test)
        } game_scale;
        
        // Common emission radii - PHYSICS-BASED (realistic light falloff distances)
        struct EmissionRadii {
            float candle = 5.0f;                // Candle visible to ~5m in darkness
            float flashlight = 20.0f;           // LED flashlight effective to ~20m
            float torch = 15.0f;                // Fire torch effective to ~15m  
            float room_light = 10.0f;           // Room bulb lights ~10m radius
            float car_headlight = 100.0f;       // Car headlight reaches ~100m
            float street_light = 50.0f;         // Street light covers ~50m radius
            float bonfire = 100.0f;             // Bonfire visible to ~100m
            float stadium_light = 200.0f;       // Stadium light covers ~200m
        } radii;
    } light_sources;
    
    // ===== SURFACE LIGHT GRIDS =====
    struct SurfaceGrids {
        // Grid resolution (cells per surface)
        int default_cols = 10;              // From light_system.cpp line 1565
        int default_rows = 10;              // 10x10 grid per surface
        
        // Quality presets
        int low_quality_resolution = 5;     // 5x5 for performance
        int medium_quality_resolution = 10; // 10x10 standard
        int high_quality_resolution = 20;   // 20x20 for quality
        int ultra_quality_resolution = 50;  // 50x50 for beauty shots
        
        // Grid behavior
        bool clear_per_frame = true;        // Reset grids each update
        float cell_blend_radius = 1.0f;     // How much cells influence neighbors
    } surface_grids;
    
    // ===== BRIGHTNESS AND NORMALIZATION =====
    struct Brightness {
        // Expected light intensities - PHYSICS-CALCULATED from real lumen values
        // Formula: lumens / (4π * distance²) for point light in 3D space
        struct ExpectedIntensities {
            // Stadium light (100,000 lumens) at various distances - PHYSICS CONSTANTS
            float stadium_at_1m = 7957.7f;     // 100,000 / (4π * 1²) = 7957.7
            float stadium_at_3m = 884.2f;      // 100,000 / (4π * 9) = 884.2
            float stadium_at_5m = 318.3f;      // 100,000 / (4π * 25) = 318.3
            float stadium_at_10m = 79.6f;      // 100,000 / (4π * 100) = 79.6
            
            // Torch (400 lumens) at various distances - PHYSICS CONSTANTS  
            float torch_at_1m = 31.8f;         // 400 / (4π * 1²) = 31.8
            float torch_at_3m = 3.5f;          // 400 / (4π * 9) = 3.5
            float torch_at_5m = 1.3f;          // 400 / (4π * 25) = 1.3
            
            // Legacy single threshold - DEPRECATED in favor of Zone System
            float max_expected = 50.0f;        // Used only in Simple Threshold mode
        } expected;
        
        // Brightness clamping - GAME PARAMETERS
        float max_brightness_multiplier = 1.0f;  // Cap at full bright (100%)
        float shadow_threshold = 0.1f;           // Below 0.1 lumens = complete darkness (human vision limit)
        
        // Special darkness values - GAME PARAMETERS (visual design choices)
        float memory_darkness = 0.1f;            // Remembered areas are 10% bright
        float twilight_ambient = 0.0f;           // NO ambient - pure darkness without light sources
    } brightness;
    
    // ===== TONE MAPPING SYSTEM =====
    // Multi-mode tone mapping inspired by the masters of computer graphics
    struct ToneMapping {
        // Current active mode
        enum Mode {
            SIMPLE_THRESHOLD = 0,    // Carmack style - fixed threshold (performance)
            ZONE_SYSTEM = 1,         // Ansel Adams style - DEFAULT (quality + flexibility)  
            ADAPTIVE_EXPOSURE = 2    // Modern HDR style - experimental (automatic)
        } current_mode = ZONE_SYSTEM;
        
        // Zone System Parameters (Mode 1 - DEFAULT)
        // Based on Ansel Adams' photographic zone system adapted for 3D graphics
        struct ZoneSystem {
            // Zone boundaries (lux values)
            float shadow_threshold = 10.0f;       // 0-10 lux = Zone I-III (deep shadows)
            float midtone_threshold = 100.0f;     // 10-100 lux = Zone IV-VI (normal lighting)
            // Above 100 lux = Zone VII-X (bright highlights)
            
            // Zone multipliers - how much to boost/compress each zone
            float shadow_multiplier = 7.5f;       // Boost shadows: 0-75 RGB (0-10 lux → 0-75)
            float midtone_multiplier = 1.39f;     // Gentle midtone boost: 75-200 RGB (10-100 lux → 75-200)  
            float highlight_multiplier = 0.55f;   // Compress highlights: 200-255 RGB (100+ lux → 200-255)
            
            // Zone boundaries in final RGB space
            float shadow_rgb_max = 75.0f;         // Shadows map to 0-75 RGB
            float midtone_rgb_max = 200.0f;       // Midtones map to 75-200 RGB
            // Highlights map to 200-255 RGB
        } zone_system;
        
        // Simple Threshold Parameters (Mode 0 - PERFORMANCE)  
        struct SimpleThreshold {
            float fixed_threshold = 15.0f;        // Single threshold - simple but limited
        } simple_threshold;
        
        // Adaptive Exposure Parameters (Mode 2 - EXPERIMENTAL)
        // TODO[LIGHT-001]: Implement adaptive exposure as optional mode
        struct AdaptiveExposure {
            float adaptation_speed = 2.0f;        // How fast exposure changes (seconds)
            float min_exposure = 0.1f;            // Prevent over-darkening scenes
            float max_exposure = 10.0f;           // Prevent over-brightening scenes  
            float target_brightness = 0.18f;      // Target middle gray (photography standard)
            
            // Current exposure state (updated each frame)
            float current_exposure = 1.0f;        // Current exposure multiplier
            float scene_average = 50.0f;          // Last calculated scene average
        } adaptive_exposure;
        
    } tone_mapping;
    
    // ===== MATERIAL PROPERTIES =====
    struct Materials {
        // Default material properties - PHYSICS-BASED (realistic material behavior)
        float default_reflectivity = 0.1f;       // 10% reflected (concrete/stone) - PHYSICS CONSTANT
        float default_roughness = 0.8f;          // 80% diffuse reflection (matte surface) - PHYSICS CONSTANT
        
        // Color blending weights - GAME PARAMETERS (visual design choice)
        float material_color_weight = 0.8f;      // 80% object color (preserve material appearance)
        float light_tint_weight = 0.2f;          // 20% light color (subtle tinting effect)
        
        // Realistic material reflectivities - PHYSICS CONSTANTS
        float fresh_snow = 0.9f;                 // Fresh snow: 90% reflective
        float white_paper = 0.8f;                // White paper: 80% reflective  
        float concrete = 0.3f;                   // Concrete: 30% reflective
        float wood = 0.1f;                       // Wood: 10% reflective
        float grass = 0.25f;                     // Grass: 25% reflective
        float asphalt = 0.04f;                   // Asphalt: 4% reflective
        float charcoal = 0.04f;                  // Charcoal: 4% reflective (nearly black body)
        float mirror = 0.95f;                    // Mirror: 95% reflective
    } materials;
    
    // ===== PHYSICS CONSTANTS =====
    struct Physics {
        // Light propagation - PHYSICS CONSTANTS (immutable laws of physics)
        bool use_inverse_square = true;         // Physically correct light falloff
        float four_pi = 12.566370f;             // 4π for solid angle (exact value)
        float pi = 3.141593f;                   // π (exact value)
        
        // Speed of light and energy
        float light_speed = 299792458.0f;       // Speed of light in m/s (not used but available)
    } physics;
    
    // ===== RENDERING ADJUSTMENTS =====
    struct Rendering {
        // Height scaling for isometric (render_system.mm)
        float isometric_height_scale = 0.8f;    // Lines 476, 496, 631
        
        // Depth adjustment
        float perspective_depth_scale = 1.0f;
        float isometric_depth_scale = 0.5f;
        
        // Debug colors
        struct DebugColors {
            float no_light_r = 0.0f;
            float no_light_g = 0.0f;  
            float no_light_b = 0.0f;
        } debug;
    } rendering;
    
    // ===== COLLISION AND PRECISION =====
    struct Precision {
        // From various files
        float collision_epsilon = 0.1f;         // physics_system.h line 35
        float ray_parallel_threshold = 0.0001f; // light_system.cpp
        float zero_threshold = 0.00001f;        // observer_system.cpp line 124
        float distance_epsilon = 0.001f;        // camera_system.cpp line 136
    } precision;
    
    // ===== DEBUG SETTINGS =====
    struct Debug {
        // Output limits
        int max_ray_debug_prints = 20;
        int max_grid_creates = 100;
        int max_impact_logs = 10;
        int max_pixel_logs = 10;
        
        // Visualization
        bool show_ray_paths = false;
        bool show_grid_cells = false;
        bool show_intensity_values = false;
        bool show_shadow_calculations = false;
    } debug;
    
    // ===== SINGLETON ACCESS =====
    static LightingConfig& get() {
        static LightingConfig instance;
        return instance;
    }
    
    // ===== HELPER METHODS =====
    
    // Calculate intensity at a given distance using real physics - PHYSICS-BASED CALCULATION
    float calculate_intensity_at_distance(float emission_strength_lumens, float distance_meters) const {
        float safe_distance = std::max(distance_meters, ray_tracing.min_distance);
        
        // Real physics: Point light intensity = lumens / (4π * distance²)
        // This gives illuminance in lux (lumens per square meter)
        return emission_strength_lumens / (physics.four_pi * safe_distance * safe_distance);
    }
    
    // OPTIMIZED: Calculate intensity using squared distance directly (avoids sqrt)
    float calculate_intensity_at_distance_sq(float emission_strength_lumens, float distance_sq) const {
        float safe_distance_sq = std::max(distance_sq, ray_tracing.min_distance * ray_tracing.min_distance);
        
        // Real physics: Point light intensity = lumens / (4π * distance²)
        // This gives illuminance in lux (lumens per square meter)
        return emission_strength_lumens / (physics.four_pi * safe_distance_sq);
    }
    
    // DEPRECATED: Legacy single threshold normalization - use tone_map_to_rgb() instead
    float normalize_for_rendering(float raw_intensity_lux) const {
        float normalized = raw_intensity_lux / brightness.expected.max_expected;
        return std::min(normalized, brightness.max_brightness_multiplier);
    }
    
    // NEW: Multi-mode tone mapping - maps lux values to RGB (0-255)
    int tone_map_to_rgb(float raw_intensity_lux) const {
        switch(tone_mapping.current_mode) {
            case ToneMapping::SIMPLE_THRESHOLD:
                return tone_map_simple_threshold(raw_intensity_lux);
            case ToneMapping::ZONE_SYSTEM:
                return tone_map_zone_system(raw_intensity_lux);
            case ToneMapping::ADAPTIVE_EXPOSURE:
                // TODO[LIGHT-001]: Implement adaptive exposure
                return tone_map_zone_system(raw_intensity_lux); // Fallback to zone system
            default:
                return tone_map_zone_system(raw_intensity_lux);
        }
    }
    
    // Simple Threshold Mode - Carmack style (performance)
    int tone_map_simple_threshold(float raw_intensity_lux) const {
        float normalized = raw_intensity_lux / tone_mapping.simple_threshold.fixed_threshold;
        normalized = std::min(normalized, brightness.max_brightness_multiplier);
        return (int)(normalized * 255.0f);
    }
    
    // Zone System Mode - Ansel Adams style (DEFAULT - quality + flexibility)
    int tone_map_zone_system(float raw_intensity_lux) const {
        const auto& zone = tone_mapping.zone_system;
        
        // CRITICAL: Ensure true darkness - 0 lux MUST equal 0 RGB
        // But allow very small positive values through for debugging
        if (raw_intensity_lux < 0.001f) {
            return 0;  // Complete darkness
        }
        
        if (raw_intensity_lux <= zone.shadow_threshold) {
            // Zone I-III: Deep shadows (0-10 lux → 0-75 RGB)
            float shadow_ratio = raw_intensity_lux / zone.shadow_threshold;
            return (int)(shadow_ratio * zone.shadow_rgb_max);
            
        } else if (raw_intensity_lux <= zone.midtone_threshold) {
            // Zone IV-VI: Normal lighting (10-100 lux → 75-200 RGB)
            float midtone_ratio = (raw_intensity_lux - zone.shadow_threshold) / 
                                 (zone.midtone_threshold - zone.shadow_threshold);
            float midtone_range = zone.midtone_rgb_max - zone.shadow_rgb_max;
            return (int)(zone.shadow_rgb_max + midtone_ratio * midtone_range);
            
        } else {
            // Zone VII-X: Bright highlights (100+ lux → 200-255 RGB)  
            float highlight_excess = raw_intensity_lux - zone.midtone_threshold;
            float compressed_excess = highlight_excess * zone.highlight_multiplier;
            float highlight_range = 255.0f - zone.midtone_rgb_max;
            int result = (int)(zone.midtone_rgb_max + std::min(compressed_excess, highlight_range));
            return std::min(result, 255);
        }
    }
    
    // Get appropriate grid resolution based on quality setting
    int get_grid_resolution(int quality_level) const {
        switch(quality_level) {
            case 0: return surface_grids.low_quality_resolution;
            case 1: return surface_grids.medium_quality_resolution;
            case 2: return surface_grids.high_quality_resolution;
            case 3: return surface_grids.ultra_quality_resolution;
            default: return surface_grids.default_cols;
        }
    }
    
    // Check if an intensity value indicates shadow - PHYSICS-BASED 
    bool is_in_shadow(float intensity_lux) const {
        // Below 0.1 lux is effectively darkness for human vision
        return intensity_lux < brightness.shadow_threshold;
    }
    
    // Blend material and light colors
    void blend_colors(float material_r, float material_g, float material_b,
                     float light_r, float light_g, float light_b,
                     float& out_r, float& out_g, float& out_b) const {
        out_r = material_r * materials.material_color_weight + 
                light_r * materials.light_tint_weight;
        out_g = material_g * materials.material_color_weight + 
                light_g * materials.light_tint_weight;
        out_b = material_b * materials.material_color_weight + 
                light_b * materials.light_tint_weight;
    }
    
private:
    // Private constructor for singleton
    LightingConfig() = default;
};

// ===== MODULAR LIGHTING STRATEGY SYSTEM =====
// Added to support swappable lighting algorithms based on performance needs

// Forward declarations
struct Particle;
struct Surface;
class ParticleSystem;

// Color information returned by lighting strategies
struct SurfaceColor {
    uint8_t r, g, b;
    float intensity;  // For debug/visualization
};

// Abstract interface for lighting strategies
// This allows swapping different lighting algorithms at runtime
// based on performance requirements and quality needs
class ILightingStrategy {
public:
    virtual ~ILightingStrategy() = default;
    
    // Initialize the strategy with render dimensions
    // Called when strategy is first selected or window resizes
    virtual void initialize(int width, int height) = 0;
    
    // Update lighting for all surfaces in the scene
    // This is the main lighting calculation method
    // particle_system: Access to all particles
    // light_indices: Indices of particles that emit light
    virtual void update_lighting(
        ParticleSystem& particle_system,
        const std::vector<int>& light_indices
    ) = 0;
    
    // Get the lit color at a specific point on a surface
    // particle_id: ID of the particle containing the surface
    // surface_index: Which surface on the particle (0-5 for cube)
    // u, v: Normalized [0,1] coordinates on the surface
    // base_r/g/b: Base material color
    // Returns: Color with lighting applied
    virtual SurfaceColor get_surface_color_at(
        int particle_id,
        int surface_index, 
        float u, float v,
        float base_r, float base_g, float base_b
    ) const = 0;
    
    // Performance metrics
    virtual float get_last_update_ms() const = 0;
    virtual std::string get_strategy_name() const = 0;
    
    // Optional: Clear any cached data
    virtual void clear_cache() {}
    
    // Optional: Get debug information
    virtual std::string get_debug_info() const { return ""; }
};

// Types of lighting strategies available
enum class LightingStrategyType {
    PixelToLight,      // Simple per-pixel ray to each light
    GridBased,         // Current implementation with surface grids
    Hierarchical,      // Adaptive grid refinement
    ShadowMap         // Industry standard shadow mapping
};

// Factory for creating lighting strategies
class LightingStrategyFactory {
public:
    static std::unique_ptr<ILightingStrategy> create(LightingStrategyType type);
};

#endif // LIGHTING_CONFIG_H