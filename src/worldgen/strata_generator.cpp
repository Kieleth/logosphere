#include "logosphere/worldgen/strata_generator.h"

#include "../core/engine.h"
#include "../core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "../particle.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace StrataGenerator {

namespace {

constexpr float FRAME_DT = 1.0f / 60.0f;

float sample_uniform(std::mt19937& rng, float lo, float hi) {
    if (lo >= hi) return lo;
    std::uniform_real_distribution<float> d(lo, hi);
    return d(rng);
}

float sample_thickness(std::mt19937& rng, const LayerSpec& s) {
    if (s.thickness_stddev <= 0.0f) return s.thickness_mean;
    std::normal_distribution<float> d(s.thickness_mean, s.thickness_stddev);
    float v = d(rng);
    return std::clamp(v, s.thickness_mean * 0.25f, s.thickness_mean * 3.0f);
}

// Settle the whole particle system until every non-light particle reports
// is_at_rest=true, or until max_frames runs out. Returns frames taken.
// Settle, counting only particles in `layer_ids`. Stops when all of them
// report is_at_rest, or when max_frames elapses.
int settle(Engine& engine, const std::vector<int>& layer_ids,
           int max_frames, size_t& out_at_rest) {
    auto& ps = engine.get_particle_system();
    for (int f = 0; f < max_frames; f++) {
        engine.update(FRAME_DT);
        size_t rest = 0;
        {
            auto view = ps.lock_particles_for_read();
            for (int id : layer_ids) {
                if (id < 0 || static_cast<size_t>(id) >= view.size()) continue;
                if (view[id].is_at_rest) rest++;
            }
        }
        out_at_rest = rest;
        if (rest == layer_ids.size()) return f + 1;
    }
    return max_frames;
}

// Walk particles, return highest top_z for the given ids.
float max_top_z_for(Engine& engine, const std::vector<int>& ids) {
    auto& ps = engine.get_particle_system();
    auto view = ps.lock_particles_for_read();
    float hi = 0.0f;
    for (int id : ids) {
        if (id < 0 || static_cast<size_t>(id) >= view.size()) continue;
        const auto& p = view[id];
        float top = p.z + p.thickness * 0.5f;
        if (top > hi) hi = top;
    }
    return hi;
}

// Edge-to-edge OrganicGluon between two tiles that share a face in cardinal
// direction (1,0,0) or (0,1,0). Pattern mirrors PhysicsRockGenerator::create_rock_gluon.
void create_bond(PhysicsSystem& physics,
                 ParticleSystem& ps,
                 int id_a, int id_b,
                 float bond_strength) {
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

        // Contact face area: edge-perpendicular dimensions averaged over both tiles.
        // For cardinal adjacency in XY, the shared face is width (perpendicular axis) × thickness.
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

} // namespace

LayerResult spawn_layer(Engine& engine, const LayerSpec& spec,
                        const ChunkBounds& bounds, float base_z,
                        std::mt19937& rng) {
    LayerResult layer;

    // Uniform grid when size_min == size_max (deterministic, tests rely on this).
    // Variable sizes fall back to the uniform cell at size_max to avoid overlap.
    float cell = std::max(spec.size_min, spec.size_max);
    float width  = bounds.max_x - bounds.min_x;
    float height = bounds.max_y - bounds.min_y;
    layer.nx = std::max(1, static_cast<int>(std::floor(width  / cell)));
    layer.ny = std::max(1, static_cast<int>(std::floor(height / cell)));

    // Keep grid centered in chunk.
    float used_w = layer.nx * cell;
    float used_h = layer.ny * cell;
    float pad_x = (width  - used_w) * 0.5f;
    float pad_y = (height - used_h) * 0.5f;

    // (grid_x, grid_y) → particle_id for bond adjacency lookup.
    layer.grid.assign(static_cast<size_t>(layer.nx) * layer.ny, -1);

    for (int gy = 0; gy < layer.ny; gy++) {
        for (int gx = 0; gx < layer.nx; gx++) {
            float tile_w = sample_uniform(rng, spec.size_min, spec.size_max);
            float tile_h = sample_uniform(rng, spec.size_min, spec.size_max);
            float tile_t = sample_thickness(rng, spec);

            float cx = bounds.min_x + pad_x + gx * cell + cell * 0.5f;
            float cy = bounds.min_y + pad_y + gy * cell + cell * 0.5f;

            Particle p = {};
            p.shape     = ParticleShape::BOX;
            p.x         = cx;
            p.y         = cy;
            p.z         = base_z + spec.spawn_gap_z + tile_t * 0.5f;
            p.width     = tile_w;
            p.height    = tile_h;
            p.thickness = tile_t;
            p.size      = tile_w;

            p.is_at_rest = false;
            p.vx = p.vy = p.vz = 0.0f;

            if (spec.rotation_jitter > 0.0f) {
                p.rotation_z = sample_uniform(rng, -spec.rotation_jitter,
                                              spec.rotation_jitter);
                float tilt = spec.rotation_jitter * 0.3f;
                p.rotation_x = sample_uniform(rng, -tilt, tilt);
                p.rotation_y = sample_uniform(rng, -tilt, tilt);
            }

            float cv = sample_uniform(rng, -spec.color_variance, spec.color_variance);
            p.r = std::clamp(spec.r + cv, 0.0f, 1.0f);
            p.g = std::clamp(spec.g + cv, 0.0f, 1.0f);
            p.b = std::clamp(spec.b + cv, 0.0f, 1.0f);
            p.a = 1.0f;
            p.friction = spec.friction;

            p.SetMaterial(spec.material);

            int pid = engine.add_particle(p);
            layer.particle_ids.push_back(pid);
            layer.grid[static_cast<size_t>(gy) * layer.nx + gx] = pid;
        }
    }
    return layer;
}

bool layer_at_rest(Engine& engine, const std::vector<int>& ids,
                   size_t& out_at_rest) {
    auto& ps = engine.get_particle_system();
    size_t rest = 0;
    {
        auto view = ps.lock_particles_for_read();
        for (int id : ids) {
            if (id < 0 || static_cast<size_t>(id) >= view.size()) continue;
            if (view[id].is_at_rest) rest++;
        }
    }
    out_at_rest = rest;
    return rest == ids.size();
}

void bond_layer(Engine& engine, LayerResult& layer, const LayerSpec& spec) {
    if (!spec.bond_within_layer) return;
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    for (int gy = 0; gy < layer.ny; gy++) {
        for (int gx = 0; gx < layer.nx; gx++) {
            int idx = layer.grid[static_cast<size_t>(gy) * layer.nx + gx];
            if (idx < 0) continue;
            if (gx + 1 < layer.nx) {
                int r = layer.grid[static_cast<size_t>(gy) * layer.nx + (gx + 1)];
                if (r >= 0) {
                    create_bond(physics, ps, idx, r, spec.bond_strength);
                    layer.bond_count++;
                }
            }
            if (gy + 1 < layer.ny) {
                int d = layer.grid[(static_cast<size_t>(gy) + 1) * layer.nx + gx];
                if (d >= 0) {
                    create_bond(physics, ps, idx, d, spec.bond_strength);
                    layer.bond_count++;
                }
            }
        }
    }
}

float layer_top_z(Engine& engine, const std::vector<int>& ids) {
    return max_top_z_for(engine, ids);
}

Result generate(Engine& engine,
                const std::vector<LayerSpec>& specs,
                const ChunkBounds& bounds,
                std::mt19937& rng) {
    Result result;
    result.layers.reserve(specs.size());

    float base_z = 0.0f;  // sits on the turtle (z=0)

    for (const auto& spec : specs) {
        LayerResult layer = spawn_layer(engine, spec, bounds, base_z, rng);

        // Settle this layer before bonding or placing the next.
        size_t at_rest = 0;
        int took = settle(engine, layer.particle_ids, spec.max_settle_frames, at_rest);
        layer.settle_frames_taken = took;
        layer.at_rest_count       = at_rest;

        // Bond cardinal neighbors after settling — positions are stable.
        bond_layer(engine, layer, spec);

        layer.max_top_z = max_top_z_for(engine, layer.particle_ids);
        base_z = layer.max_top_z;

        std::cout << "[strata] layer=" << spec.name
                  << " tiles=" << layer.particle_ids.size()
                  << " bonds=" << layer.bond_count
                  << " settle_frames=" << took
                  << " at_rest=" << at_rest << "/" << layer.particle_ids.size()
                  << " top_z=" << layer.max_top_z
                  << std::endl;

        result.layers.push_back(std::move(layer));
    }

    return result;
}

std::vector<LayerSpec> earth_preset() {
    std::vector<LayerSpec> specs;

    // Real soil profile: FLUSH structural layers below (a walker's
    // foot must never find a chimney to the turtle), loose scatter
    // only on top. Uniform size == cell size → gapless grid.

    // Bedrock: huge flat slabs, cement-bonded (the gluon escape hatch:
    // OrganicGluon as mortar). Breaks only under real violence
    // (force > contact_area * 8000, Eden's proven bedrock number).
    LayerSpec bedrock;
    bedrock.name = "bedrock";
    bedrock.material = Materials::Type::STONE;
    bedrock.size_min = 5.0f;
    bedrock.size_max = 5.0f;
    bedrock.thickness_mean = 0.5f;
    bedrock.r = 0.40f; bedrock.g = 0.40f; bedrock.b = 0.42f;
    bedrock.bond_within_layer = true;
    bedrock.bond_strength = 8000.0f;
    bedrock.friction = 0.7f;
    specs.push_back(bedrock);

    // Filler: compacted subsoil — flush, unbonded. Holds a walker;
    // a heavy impact still shoves it (mass, not magic, keeps it put).
    LayerSpec filler;
    filler.name = "filler";
    filler.material = Materials::Type::STONE;
    filler.size_min = 1.8f;
    filler.size_max = 1.8f;
    filler.thickness_mean = 0.35f;
    filler.thickness_stddev = 0.03f;
    filler.r = 0.46f; filler.g = 0.42f; filler.b = 0.36f;
    filler.color_variance = 0.05f;
    filler.friction = 0.85f;
    specs.push_back(filler);

    // Organic topsoil: small loose clods, unbonded, well jittered —
    // the visible splash. Clods much smaller than a boulder face get
    // off-center, angled contacts and squirt sideways; flush slabs
    // could only compress.
    LayerSpec organic;
    organic.name = "organic";
    organic.material = Materials::Type::DIRT;
    organic.size_min = 0.5f;
    organic.size_max = 0.9f;
    organic.thickness_mean = 0.18f;
    organic.r = 0.33f; organic.g = 0.42f; organic.b = 0.24f;
    organic.color_variance = 0.06f;
    organic.rotation_jitter = 0.4f;
    organic.friction = 0.7f;
    organic.max_settle_frames = 300;
    specs.push_back(organic);

    return specs;
}

} // namespace StrataGenerator
