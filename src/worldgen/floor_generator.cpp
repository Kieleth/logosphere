#include "logosphere/worldgen/floor_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/worldgen/noise.h"
#include <iostream>
#include <cstdlib>
#include <string>

FloorGenerator::FloorGenerator()
    : engine_(nullptr)
    , kg_(nullptr)
{
}

FloorGenerator::~FloorGenerator() {
}

void FloorGenerator::initialize(Engine* engine, kg::KGModule* kg) {
    engine_ = engine;
    kg_ = kg;

    // Initialize underlying chunk system
    chunk_system_.initialize(engine, kg, "floor_system");

    // Register our floor-specific callbacks
    chunk_system_.set_create_callback(
        [this](const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity) {
            return create_floor_chunk(coord, chunk_size, existing_entity);
        }
    );

    chunk_system_.set_destroy_callback(
        [this](const ChunkData& data) {
            destroy_floor_chunk(data);
        }
    );

    std::cout << "[FloorGenerator] Initialized with " << tiles_per_chunk_ << "×" << tiles_per_chunk_
              << " tiles per chunk" << std::endl;
}

void FloorGenerator::update(float observer_x, float observer_y) {
    chunk_system_.update(observer_x, observer_y);
}

void FloorGenerator::preload_chunks_around(float world_x, float world_y, int radius_chunks) {
    chunk_system_.preload_chunks_around(world_x, world_y, radius_chunks);
}

void FloorGenerator::set_load_radius(float meters) {
    chunk_system_.set_load_radius(meters);
}

void FloorGenerator::set_unload_radius(float meters) {
    chunk_system_.set_unload_radius(meters);
}

void FloorGenerator::set_chunk_size(float meters) {
    chunk_system_.set_chunk_size(meters);
}

void FloorGenerator::set_update_frequency(int frames) {
    chunk_system_.set_update_frequency(frames);
}

void FloorGenerator::set_enabled(bool enabled) {
    chunk_system_.set_enabled(enabled);
}

bool FloorGenerator::is_enabled() const {
    return chunk_system_.is_enabled();
}

int FloorGenerator::get_active_chunk_count() const {
    return chunk_system_.get_active_chunk_count();
}

void FloorGenerator::clear_all_chunks() {
    chunk_system_.clear_all_chunks();
}

ChunkData FloorGenerator::create_floor_chunk(const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity) {
    ChunkData data;

    // === CHUNK ENTITY (PERMAWORLD anchor) ===
    // Reason: Chunk entity stores chunk_x/chunk_y for find_existing_chunk_entity() lookup.
    // It acts as an index/container for floor_tile entities via "CONTAINS" relation.
    if (existing_entity != kg::INVALID_ENTITY) {
        data.kg_entity = existing_entity;
        std::cout << "[FloorGenerator] Reloading chunk entity " << existing_entity
                  << " for (" << coord.x << "," << coord.y << ")" << std::endl;
    } else {
        data.kg_entity = kg_->createEntity("FloorChunk");
        kg_->setProperty(data.kg_entity, "chunk_x", std::to_string(coord.x));
        kg_->setProperty(data.kg_entity, "chunk_y", std::to_string(coord.y));
        // floor_kind (declared) instead of the undeclared "type" shadow key:
        // distinguishes this generator's chunks from the strata generator's.
        kg_->setProperty(data.kg_entity, "floor_kind", "floor");
        std::cout << "[FloorGenerator] Created chunk entity " << data.kg_entity
                  << " for (" << coord.x << "," << coord.y << ")" << std::endl;
    }

    // === EXISTING FLOOR TILE ENTITIES (for reload) ===
    // Reason: On reload, query "CONTAINS" relation to get existing floor_tile entity IDs.
    // This allows reusing entity IDs for PERMAWORLD persistence.
    std::vector<kg::EntityID> existing_floor_tiles;
    if (existing_entity != kg::INVALID_ENTITY) {
        existing_floor_tiles = kg_->getRelated(existing_entity, "CONTAINS");
        std::cout << "[FloorGenerator]   Found " << existing_floor_tiles.size()
                  << " existing floor_tile entities to reuse" << std::endl;
    }

    // === WORLD POSITION CALCULATION ===
    // coord.world_x/y returns chunk CENTER already
    float grid_center_x = coord.world_x(chunk_size);
    float grid_center_y = coord.world_y(chunk_size);

    // === TERRAIN COLOR (unchanged) ===
    float noise_value = noise::perlin_noise_2d(grid_center_x * 0.1f, grid_center_y * 0.1f);
    float terrain_r, terrain_g, terrain_b;
    get_terrain_color(floor_type_, noise_value, terrain_r, terrain_g, terrain_b);

    FloorTileConfig config;
    config.tile_width = tile_size_;
    config.tile_height = tile_size_;
    config.tile_thickness = tile_thickness_;
    config.r = terrain_r;
    config.g = terrain_g;
    config.b = terrain_b;
    config.color_variance = color_variance_;

    // === CREATE ALL TILE PARTICLES ===
    // Reason: Particles are created first, then grouped into floor_tile entities.
    // create_floor_grid returns particle indices in row-major order.
    auto particle_ids = engine_->get_particle_system().create_floor_grid(
        grid_center_x, grid_center_y,
        tiles_per_chunk_, tiles_per_chunk_, config);

    // === GROUP TILES INTO FLOOR_TILE ENTITIES ===
    // Reason: BVH optimization - smaller entities = smaller directional groups.
    // tiles_per_entity_ = N means N×N tiles per entity (e.g., 2 = 2×2 = 4 tiles = 48 tris).
    // This reduces Stage 3 linear scan from ~5000 tris/group to ~8-50 tris/group.
    int entity_grid_size = tiles_per_chunk_ / tiles_per_entity_;  // e.g., 50/2 = 25 entities per row
    int total_floor_entities = entity_grid_size * entity_grid_size;
    size_t reused_count = 0;

    for (int ey = 0; ey < entity_grid_size; ++ey) {
        for (int ex = 0; ex < entity_grid_size; ++ex) {
            int entity_idx = ey * entity_grid_size + ex;

            // === CREATE OR REUSE FLOOR_TILE ENTITY ===
            // Reason: Reuse existing entity IDs on reload for PERMAWORLD persistence.
            kg::EntityID floor_tile_entity;
            if (entity_idx < static_cast<int>(existing_floor_tiles.size())) {
                floor_tile_entity = existing_floor_tiles[entity_idx];
                reused_count++;
            } else {
                floor_tile_entity = kg_->createEntity("FloorTile");
                // Link to chunk via "CONTAINS" relation (for streaming/reload)
                kg_->createRelation(data.kg_entity, "CONTAINS", floor_tile_entity);
            }

            // === BIND TILES TO THIS FLOOR_TILE ENTITY ===
            // Reason: Each floor_tile entity owns N×N tiles. KGParticles bind
            // render indices to entity, enabling getEntityByRenderIndex() lookup.
            for (int ly = 0; ly < tiles_per_entity_; ++ly) {
                for (int lx = 0; lx < tiles_per_entity_; ++lx) {
                    int gx = ex * tiles_per_entity_ + lx;  // Global tile X
                    int gy = ey * tiles_per_entity_ + ly;  // Global tile Y
                    int particle_idx = gy * tiles_per_chunk_ + gx;  // Row-major index

                    kg_->createKGParticle(floor_tile_entity,
                        static_cast<kg::RenderIndex>(particle_ids[particle_idx]));
                }
            }

            // === TRACK FOR UNLOAD ===
            // Reason: ChunkSystem uses entity_ids for unload. Each floor_tile entity
            // must be tracked so its particles get queued for deletion.
            data.entity_ids.push_back(floor_tile_entity);
        }
    }

    // DEBUG: Log entity counts
    static int debug_count = 0;
    if (debug_count < 3) {
        std::cout << "[FloorGen] Chunk (" << coord.x << "," << coord.y << "): "
                  << total_floor_entities << " floor_tile entities ("
                  << tiles_per_entity_ << "×" << tiles_per_entity_ << " tiles each, "
                  << tiles_per_entity_ * tiles_per_entity_ * 12 << " tris/entity), "
                  << "reused=" << reused_count << std::endl;
        debug_count++;
    }

    return data;
}

void FloorGenerator::get_terrain_color(FloorType type, float noise_value, float& r, float& g, float& b) {
    // Base color palettes for each terrain type
    float base_r, base_g, base_b;

    switch (type) {
        case FloorType::GRASS:
            base_r = 0.2f; base_g = 0.6f; base_b = 0.2f;  // Green
            break;
        case FloorType::SAND:
            base_r = 0.8f; base_g = 0.7f; base_b = 0.5f;  // Beige/tan
            break;
        case FloorType::STONE:
            base_r = 0.5f; base_g = 0.5f; base_b = 0.5f;  // Gray
            break;
        case FloorType::WOOD:
            base_r = 0.4f; base_g = 0.25f; base_b = 0.15f;  // Brown
            break;
        case FloorType::EARTH:
        default:
            base_r = 0.4f; base_g = 0.35f; base_b = 0.25f;  // Dirt brown
            break;
    }

    // Modulate by noise (noise in [-1, 1] → brightness variation ±0.15)
    float brightness_mod = noise_value * 0.15f;
    r = base_r + brightness_mod;
    g = base_g + brightness_mod;
    b = base_b + brightness_mod;

    // Clamp to [0, 1]
    r = std::max(0.0f, std::min(1.0f, r));
    g = std::max(0.0f, std::min(1.0f, g));
    b = std::max(0.0f, std::min(1.0f, b));
}

void FloorGenerator::destroy_floor_chunk(const ChunkData& data) {
    // ChunkSystem handles particle deletion and KG cleanup
    // This is for floor-specific cleanup if needed
    // (Currently nothing extra to do)
}
