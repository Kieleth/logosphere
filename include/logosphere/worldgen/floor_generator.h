#ifndef FLOOR_GENERATOR_H
#define FLOOR_GENERATOR_H

#include "logosphere/worldgen/chunk_system.h"

// Forward declarations
class Engine;
namespace kg { class KGModule; }
struct FloorTileConfig;

// Floor terrain types
// Each type has distinct color palette modulated by Perlin noise
enum class FloorType {
    GRASS,   // Green grass (outdoor)
    SAND,    // Beige/tan sand (outdoor)
    STONE,   // Gray stone (indoor/outdoor)
    WOOD,    // Brown wood planks (indoor)
    EARTH    // Brown/tan dirt (outdoor)
};

// Floor-specific chunk generator
// Uses ChunkSystem for spatial management, generates floor tiles as content
class FloorGenerator {
public:
    FloorGenerator();
    ~FloorGenerator();

    // Initialize with engine and KG
    void initialize(Engine* engine, kg::KGModule* kg);

    // Update floor based on observer position (delegates to ChunkSystem)
    void update(float observer_x, float observer_y);

    // Pre-load chunks around a position (for startup)
    void preload_chunks_around(float world_x, float world_y, int radius_chunks = 1);

    // Configuration
    void set_load_radius(float meters);
    void set_unload_radius(float meters);
    void set_chunk_size(float meters);
    void set_tiles_per_chunk(int tiles) {
        tiles_per_chunk_ = tiles;
        chunk_system_.set_chunk_size(tile_size_ * tiles_per_chunk_);
    }
    void set_tiles_per_entity(int n) { tiles_per_entity_ = n; }  // N×N tiles per floor entity
    void set_update_frequency(int frames);

    // Floor appearance
    void set_tile_size(float meters) {
        tile_size_ = meters;
        // Auto-compute chunk size from tile_size * tiles_per_chunk
        chunk_system_.set_chunk_size(tile_size_ * tiles_per_chunk_);
    }
    void set_tile_thickness(float meters) { tile_thickness_ = meters; }
    void set_tile_color(float r, float g, float b) {
        tile_r_ = r; tile_g_ = g; tile_b_ = b;
    }
    void set_color_variance(float variance) { color_variance_ = variance; }

    // Floor type control (determines color palette)
    void set_floor_type(FloorType type) { floor_type_ = type; }
    FloorType get_floor_type() const { return floor_type_; }

    // Enable/disable
    void set_enabled(bool enabled);
    bool is_enabled() const;

    // Query
    int get_active_chunk_count() const;

    // Manual control
    void clear_all_chunks();

private:
    // Callbacks for ChunkSystem
    ChunkData create_floor_chunk(const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity);
    void destroy_floor_chunk(const ChunkData& data);

    // Color generation helper
    // noise_value: Perlin noise in range [-1, 1]
    // Returns RGB colors in [0, 1]
    void get_terrain_color(FloorType type, float noise_value, float& r, float& g, float& b);

    Engine* engine_;
    kg::KGModule* kg_;

    // Generic chunk system (handles spatial logic)
    ChunkSystem chunk_system_;

    // Floor-specific config
    int tiles_per_chunk_ = 10;        // 10×10 tiles per chunk (was 5×5)
    int tiles_per_entity_ = 2;        // 2×2 tiles per floor entity (configurable for BVH optimization)
    float tile_size_ = 0.5f;          // 0.5m tiles (was 2m) - less visible grid
    float tile_thickness_ = 0.1f;     // 10cm thick
    float tile_r_ = 0.4f;             // Earth brown
    float tile_g_ = 0.35f;
    float tile_b_ = 0.25f;
    float color_variance_ = 0.08f;    // Color variation per tile
    FloorType floor_type_ = FloorType::EARTH;  // Terrain type
};

#endif // FLOOR_GENERATOR_H
