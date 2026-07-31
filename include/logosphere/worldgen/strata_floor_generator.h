#ifndef STRATA_FLOOR_GENERATOR_H
#define STRATA_FLOOR_GENERATOR_H

#include <functional>
#include <string>
#include <vector>
#include "materials.h"
#include "logosphere/worldgen/chunk_system.h"

class Engine;
namespace kg { class KGModule; }

// =============================================================================
// StrataFloorGenerator — streaming layered ground on top of ChunkSystem.
// =============================================================================
// Same API shape as `FloorGenerator` (tile_size, load_radius, preload_around)
// but emits a stack of layers per chunk instead of a single layer.
//
// Tiles are placed at precomputed heights (no physics settling during chunk
// load). Each layer's tiles sit exactly on top of the previous layer, with
// is_at_rest=true so the solver skips them until disturbed. Bonded layers
// create OrganicGluon between cardinal neighbors (pattern from
// PhysicsRockGenerator).
//
// The generator provides mechanism; games configure policy (which materials
// for which layer, how thick, which layers are bonded). Same separation
// principle as FloorGenerator's floor_type enum.
// =============================================================================

struct StrataLayerSpec {
    std::string name = "layer";
    Materials::Type material = Materials::Type::STONE;
    float thickness = 0.2f;
    float r = 0.5f, g = 0.5f, b = 0.5f;
    float color_variance = 0.05f;
    bool  bond_within_layer = false;
    float bond_strength     = 0.0f;  // material_strength for break calc
};

class StrataFloorGenerator {
public:
    StrataFloorGenerator();
    ~StrataFloorGenerator();

    void initialize(Engine* engine, kg::KGModule* kg);
    void update(float observer_x, float observer_y);
    void preload_chunks_around(float world_x, float world_y, int radius_chunks = 1);

    // Configuration (matches FloorGenerator shape where applicable).
    void set_load_radius(float meters);
    void set_unload_radius(float meters);
    void set_tile_size(float meters);
    void set_tiles_per_chunk(int tiles);
    void set_tiles_per_entity(int n) { tiles_per_entity_ = n; }

    // Layer stack (bottom to top).
    void set_layers(std::vector<StrataLayerSpec> layers) { layers_ = std::move(layers); }
    const std::vector<StrataLayerSpec>& get_layers() const { return layers_; }

    // Per-tile skip mask — return true to omit the tile at (world_x, world_y)
    // in the given layer. Games use this to carve trenches, craters, or
    // exposed-bedrock zones where a prescribed region drops the upper layers
    // while keeping bedrock intact. If unset, every tile in every layer is
    // placed. The mask runs on chunk generation only; already-loaded chunks
    // aren't retroactively modified.
    using SkipMask = std::function<bool(float world_x, float world_y, size_t layer_index)>;
    void set_tile_skip_mask(SkipMask mask) { skip_mask_ = std::move(mask); }

    void set_enabled(bool enabled);
    bool is_enabled() const;

    int  get_active_chunk_count() const;
    void clear_all_chunks();

private:
    ChunkData create_strata_chunk(const ChunkCoord& coord, float chunk_size,
                                  kg::EntityID existing_entity);
    void destroy_strata_chunk(const ChunkData& data);

    Engine*        engine_ = nullptr;
    kg::KGModule*  kg_     = nullptr;
    ChunkSystem    chunk_system_;

    std::vector<StrataLayerSpec> layers_;
    SkipMask skip_mask_;             // optional; null = place every tile
    float tile_size_        = 1.0f;
    int   tiles_per_chunk_  = 10;    // N × N tiles per chunk side
    int   tiles_per_entity_ = 2;     // N × N tiles per KG entity (BVH grouping)
};

#endif // STRATA_FLOOR_GENERATOR_H
