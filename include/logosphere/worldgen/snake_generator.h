#ifndef SNAKE_GENERATOR_H
#define SNAKE_GENERATOR_H

#include "logosphere/kg/kg_types.h"

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// Snake specification for procedural generation
struct SnakeSpec {
    // Dimensions
    float total_length = 2.0f;      // Total length from head to tail (meters)
    int num_segments = 20;           // Number of body segments
    float head_size = 0.08f;         // Head width/height (meters)
    float body_thickness = 0.06f;    // Body segment diameter (meters)
    float taper_factor = 0.8f;       // Tail tapers to this fraction of body thickness

    // Colors
    float scale_r = 0.2f, scale_g = 0.6f, scale_b = 0.2f;  // Green scales
    float head_r = 0.8f, head_g = 0.6f, head_b = 0.2f;     // Yellow head
    float eye_r = 0.0f, eye_g = 0.0f, eye_b = 0.0f;        // Black eyes

    // Physics
    float material_density = 1100.0f;  // Reptile density (kg/m³, slightly denser than water)

    // Named presets
    static SnakeSpec garden_snake();   // Small green garden snake
    static SnakeSpec python();         // Large constrictor
    static SnakeSpec coral_snake();    // Colorful banded snake
};

// Procedural snake generator
class SnakeGenerator {
public:
    SnakeGenerator();
    ~SnakeGenerator();

    // Initialize with engine and KG access
    void initialize(Engine* engine, kg::KGModule* kg);

    // Generate snake entity at world position
    // Returns KG entity ID for the complete snake
    kg::EntityID generate_snake(float world_x, float world_y, float world_z, const SnakeSpec& spec);

private:
    // Helper: Create single segment particle bound to entity
    unsigned int create_segment(float x, float y, float z, float width, float depth, float height,
                                 float r, float g, float b, kg::EntityID entity);

    // Helper: Create head particle with eyes bound to entity
    unsigned int create_head(float x, float y, float z, const SnakeSpec& spec, kg::EntityID entity);

    Engine* engine_;
    kg::KGModule* kg_;
};

#endif // SNAKE_GENERATOR_H
