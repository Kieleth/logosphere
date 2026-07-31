#include "logosphere/worldgen/strata_floor_generator.h"

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/worldgen/noise.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

StrataFloorGenerator::StrataFloorGenerator() = default;
StrataFloorGenerator::~StrataFloorGenerator() = default;

void StrataFloorGenerator::initialize(Engine* engine, kg::KGModule* kg) {
    engine_ = engine;
    kg_     = kg;

    chunk_system_.initialize(engine, kg, "strata_floor_system");

    chunk_system_.set_create_callback(
        [this](const ChunkCoord& coord, float chunk_size, kg::EntityID existing) {
            return create_strata_chunk(coord, chunk_size, existing);
        });
    chunk_system_.set_destroy_callback(
        [this](const ChunkData& data) { destroy_strata_chunk(data); });

    std::cout << "[StrataFloorGenerator] Initialized — "
              << layers_.size() << " layers, "
              << tiles_per_chunk_ << "×" << tiles_per_chunk_ << " tiles/chunk, "
              << tile_size_ << "m tile size\n";
}

void StrataFloorGenerator::update(float observer_x, float observer_y) {
    chunk_system_.update(observer_x, observer_y);
}

void StrataFloorGenerator::preload_chunks_around(float world_x, float world_y, int radius_chunks) {
    chunk_system_.preload_chunks_around(world_x, world_y, radius_chunks);
}

void StrataFloorGenerator::set_load_radius(float meters)   { chunk_system_.set_load_radius(meters); }
void StrataFloorGenerator::set_unload_radius(float meters) { chunk_system_.set_unload_radius(meters); }

void StrataFloorGenerator::set_tile_size(float meters) {
    tile_size_ = meters;
    chunk_system_.set_chunk_size(tile_size_ * tiles_per_chunk_);
}

void StrataFloorGenerator::set_tiles_per_chunk(int tiles) {
    tiles_per_chunk_ = tiles;
    chunk_system_.set_chunk_size(tile_size_ * tiles_per_chunk_);
}

void StrataFloorGenerator::set_enabled(bool enabled) { chunk_system_.set_enabled(enabled); }
bool StrataFloorGenerator::is_enabled() const        { return chunk_system_.is_enabled(); }
int  StrataFloorGenerator::get_active_chunk_count() const { return chunk_system_.get_active_chunk_count(); }
void StrataFloorGenerator::clear_all_chunks()        { chunk_system_.clear_all_chunks(); }

// -----------------------------------------------------------------------------
// Edge-to-edge OrganicGluon between two tiles sharing a cardinal face.
// Pattern mirrors PhysicsRockGenerator::create_rock_gluon.
// -----------------------------------------------------------------------------
static void bond_tiles(PhysicsSystem& physics, ParticleSystem& ps,
                       int id_a, int id_b, float bond_strength) {
    if (id_a < 0 || id_b < 0) return;

    Vec3 offset_a, offset_b;
    float contact_area = 0.0f;
    {
        auto view = ps.lock_particles_for_read();
        if (static_cast<size_t>(id_a) >= view.size() ||
            static_cast<size_t>(id_b) >= view.size()) return;

        const Particle& pa = view[id_a];
        const Particle& pb = view[id_b];
        float dx = pb.x - pa.x;
        float dy = pb.y - pa.y;
        float dz = pb.z - pa.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < 1e-6f) return;
        dx /= dist; dy /= dist; dz /= dist;

        float half_ext_a = std::abs(dx) * pa.width  * 0.5f
                         + std::abs(dy) * pa.height * 0.5f
                         + std::abs(dz) * pa.thickness * 0.5f;
        float half_ext_b = std::abs(dx) * pb.width  * 0.5f
                         + std::abs(dy) * pb.height * 0.5f
                         + std::abs(dz) * pb.thickness * 0.5f;

        offset_a = Vec3(dx * half_ext_a,  dy * half_ext_a,  dz * half_ext_a);
        offset_b = Vec3(-dx * half_ext_b, -dy * half_ext_b, -dz * half_ext_b);

        float face_a = (std::abs(dx) > 0.5f ? pa.height : pa.width) * pa.thickness;
        float face_b = (std::abs(dx) > 0.5f ? pb.height : pb.width) * pb.thickness;
        contact_area = (face_a + face_b) * 0.5f;
    }

    auto gluon = std::make_unique<OrganicGluon>();
    gluon->offset_a        = offset_a;
    gluon->offset_b        = offset_b;
    gluon->target_distance = 0.0f;
    gluon->contact_area    = contact_area;
    gluon->stiffness       = 50000.0f;
    gluon->damping         = 1000.0f;

    {
        auto wview = ps.lock_particles_for_write();
        wview[id_a].material_strength = bond_strength;
        wview[id_b].material_strength = bond_strength;
    }

    physics.add_gluon_between(static_cast<size_t>(id_a),
                              static_cast<size_t>(id_b),
                              std::move(gluon));
}

// -----------------------------------------------------------------------------
// Create one chunk: stack N layers of tile_size×tile_size tiles, each at the
// height of the stack below, with optional cohesion bonds per layer.
// -----------------------------------------------------------------------------
ChunkData StrataFloorGenerator::create_strata_chunk(const ChunkCoord& coord,
                                                    float chunk_size,
                                                    kg::EntityID existing_entity) {
    ChunkData data;

    if (existing_entity != kg::INVALID_ENTITY) {
        data.kg_entity = existing_entity;
    } else {
        // Reuse FloorChunk — strata is a multi-layer floor, same KG contract.
        data.kg_entity = kg_->createEntity("FloorChunk");
        kg_->setProperty(data.kg_entity, "chunk_x", std::to_string(coord.x));
        kg_->setProperty(data.kg_entity, "chunk_y", std::to_string(coord.y));
        kg_->setProperty(data.kg_entity, "type", "strata");
    }

    const float grid_cx = coord.world_x(chunk_size);
    const float grid_cy = coord.world_y(chunk_size);
    const float start_x = grid_cx - (tiles_per_chunk_ * tile_size_) * 0.5f + tile_size_ * 0.5f;
    const float start_y = grid_cy - (tiles_per_chunk_ * tile_size_) * 0.5f + tile_size_ * 0.5f;

    auto& ps      = engine_->get_particle_system();
    auto& physics = engine_->get_physics_system();

    float base_z = 0.0f;

    for (size_t layer_idx = 0; layer_idx < layers_.size(); ++layer_idx) {
        const auto& layer = layers_[layer_idx];

        // Per-layer entity groups the layer's tiles for KG relations.
        // FloorTile — grouping container for KGParticle bindings, matches
        // FloorGenerator's convention so downstream systems (BVH, Entity
        // grouping) treat it uniformly.
        kg::EntityID layer_entity = kg_->createEntity("FloorTile");
        kg_->setProperty(layer_entity, "layer_name", layer.name);
        kg_->setProperty(layer_entity, "layer_index", std::to_string(layer_idx));
        kg_->createRelation(data.kg_entity, "CONTAINS", layer_entity);
        data.entity_ids.push_back(layer_entity);

        // (grid_x, grid_y) → particle id, for bond adjacency.
        std::vector<int> grid(tiles_per_chunk_ * tiles_per_chunk_, -1);

        for (int ty = 0; ty < tiles_per_chunk_; ++ty) {
            for (int tx = 0; tx < tiles_per_chunk_; ++tx) {
                float wx = start_x + tx * tile_size_;
                float wy = start_y + ty * tile_size_;

                // Optional game-side mask lets the caller omit specific tiles
                // per layer (trench carving, exposed bedrock, craters...). The
                // grid cell stays -1 so bonding steps skip the missing tile
                // naturally.
                if (skip_mask_ && skip_mask_(wx, wy, layer_idx)) {
                    continue;
                }

                // Perlin noise for per-tile color variation (same pattern as FloorGenerator).
                float coarse = noise::perlin_noise_2d(wx * 0.12f, wy * 0.12f) * 0.7f;
                float fine   = noise::perlin_noise_2d(wx * 0.4f,  wy * 0.4f)  * 0.3f;
                float n      = coarse + fine;

                Particle p = {};
                p.shape     = ParticleShape::BOX;
                p.x         = wx;
                p.y         = wy;
                p.z         = base_z + layer.thickness * 0.5f;
                p.width     = tile_size_;
                p.height    = tile_size_;
                p.thickness = layer.thickness;
                p.size      = tile_size_;
                p.r         = std::clamp(layer.r + n * layer.color_variance, 0.0f, 1.0f);
                p.g         = std::clamp(layer.g + n * layer.color_variance * 0.8f, 0.0f, 1.0f);
                p.b         = std::clamp(layer.b + n * layer.color_variance * 0.5f, 0.0f, 1.0f);
                p.a         = 1.0f;
                p.friction  = 0.5f;
                p.reflectivity = 0.3f;

                p.SetMaterial(layer.material);

                // Precomputed stack position — at rest on load, solver will
                // re-evaluate if anything disturbs it (contacts wake, SetMass
                // wakes, etc.).
                p.is_at_rest = true;
                p.vx = p.vy = p.vz = 0.0f;

                int pid = engine_->add_particle(p);
                grid[ty * tiles_per_chunk_ + tx] = pid;

                kg_->createKGParticle(layer_entity, static_cast<kg::RenderIndex>(pid));
            }
        }

        if (layer.bond_within_layer) {
            for (int ty = 0; ty < tiles_per_chunk_; ++ty) {
                for (int tx = 0; tx < tiles_per_chunk_; ++tx) {
                    int id = grid[ty * tiles_per_chunk_ + tx];
                    if (id < 0) continue;
                    if (tx + 1 < tiles_per_chunk_) {
                        int r = grid[ty * tiles_per_chunk_ + (tx + 1)];
                        bond_tiles(physics, ps, id, r, layer.bond_strength);
                    }
                    if (ty + 1 < tiles_per_chunk_) {
                        int d = grid[(ty + 1) * tiles_per_chunk_ + tx];
                        bond_tiles(physics, ps, id, d, layer.bond_strength);
                    }
                }
            }
        }

        base_z += layer.thickness;
    }

    static int first_logs = 0;
    if (first_logs < 3) {
        std::cout << "[StrataFloor] chunk (" << coord.x << "," << coord.y << "): "
                  << layers_.size() << " layers, "
                  << (tiles_per_chunk_ * tiles_per_chunk_ * layers_.size()) << " tiles total, "
                  << "top z=" << base_z << "m\n";
        ++first_logs;
    }

    return data;
}

void StrataFloorGenerator::destroy_strata_chunk(const ChunkData& /*data*/) {
    // ChunkSystem handles particle + KG cleanup via tracked entity_ids.
}
