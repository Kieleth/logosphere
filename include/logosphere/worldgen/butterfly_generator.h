#ifndef BUTTERFLY_GENERATOR_H
#define BUTTERFLY_GENERATOR_H

#include "logosphere/kg/kg_types.h"
#include <vector>
#include <array>

// Forward declarations
class Engine;
namespace kg { class KGModule; }

// Butterfly specification for procedural generation
struct ButterflySpec {
    // Dimensions
    float body_length = 0.15f;        // Total body length (head to tail)
    float wing_span = 0.25f;          // Total wingspan (tip to tip)
    float wing_height = 0.15f;        // Wing height (top to bottom)

    // Body segments
    float head_size = 0.03f;          // Head diameter
    float thorax_size = 0.04f;        // Thorax diameter
    float abdomen_size = 0.03f;       // Abdomen (tail) diameter

    // Colors
    float wing_r = 1.0f, wing_g = 0.6f, wing_b = 0.2f;   // Orange wings (fallback)
    float body_r = 0.2f, body_g = 0.2f, body_b = 0.2f;   // Dark body

    // Multi-segment anatomy (for variable butterflies)
    int num_thorax_segments = 2;      // 1-4 thorax segments, each with wing pair
    std::vector<std::array<float, 3>> segment_wing_colors;  // RGB per segment (empty = use wing_r/g/b)

    // Physics
    float material_density = 300.0f;  // Very light (insect)

    // Flight behavior parameters
    float wing_beat_frequency = 8.0f;  // 8 Hz wing beats (fast flutter)
    float wing_beat_amplitude = 0.08f; // 8cm up/down wing movement
    float flight_speed = 1.0f;         // 1 m/s cruising speed
    float glide_probability = 0.1f;    // 10% chance to glide each second

    // Named presets
    static ButterflySpec monarch();      // Orange/black monarch
    static ButterflySpec blue_morpho();  // Bright blue wings
    static ButterflySpec small_white();  // Small white butterfly
};

// Procedural butterfly generator
class ButterflyGenerator {
public:
    ButterflyGenerator();
    ~ButterflyGenerator();

    // Initialize with engine and KG access
    void initialize(Engine* engine, kg::KGModule* kg);

    // Generate butterfly entity at world position
    // Returns KG entity ID for the complete butterfly
    kg::EntityID generate_butterfly(float world_x, float world_y, float world_z,
                                     const ButterflySpec& spec);

private:
    // Helper: Create body segment particle bound to entity
    unsigned int create_body_segment(float x, float y, float z, float size,
                                      float r, float g, float b,
                                      kg::EntityID entity);

    // Helper: Create wing particle bound to entity
    unsigned int create_wing(float x, float y, float z,
                            float width, float height, float thickness,
                            float r, float g, float b,
                            kg::EntityID entity);

    Engine* engine_;
    kg::KGModule* kg_;
};

#endif // BUTTERFLY_GENERATOR_H
