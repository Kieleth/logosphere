#ifndef STRATA_GENERATOR_H
#define STRATA_GENERATOR_H

#include <string>
#include <vector>
#include <random>
#include "materials.h"

class Engine;

// =============================================================================
// Strata Generator — layered ground as first-class engine primitive.
// =============================================================================
// Replaces the "one heavy layer of floor tiles" hack (HEAVY_STATIC) with
// sequentially-settled stacked layers. Each layer is regular physics particles.
// Optionally, adjacent tiles within a layer are bonded via OrganicGluon so the
// layer behaves as a cohesive slab that still breaks under enough force.
//
// Nothing is pinned. The turtle boundary catches gravity; mass + friction +
// (optional) bonding keep layers in place. A falling body or impact propagates
// through normal collision and gluon-breaking physics.
//
// Policy lives in the LayerSpec list supplied by the caller (game or test).
// The generator provides mechanism only.
// =============================================================================

namespace StrataGenerator {

struct LayerSpec {
    std::string name = "layer";
    Materials::Type material = Materials::Type::STONE;

    // Tile XY footprint. size_min == size_max → uniform grid (deterministic).
    float size_min = 1.0f;
    float size_max = 1.0f;

    // Tile Z (thickness). Sampled from normal(thickness_mean, thickness_stddev).
    float thickness_mean   = 0.2f;
    float thickness_stddev = 0.0f;

    // Base color + per-tile variance.
    float r = 0.5f, g = 0.5f, b = 0.5f;
    float color_variance = 0.04f;

    // Drop height above the previous layer's surface. Tiles spawn at
    // (base_z + spawn_gap_z + thickness/2) and fall into contact under
    // gravity instead of teleporting into the surface. Keeps SAT honest
    // (a teleport into an existing body confuses axis selection).
    float spawn_gap_z = 0.05f;

    // Max frames to run physics for this layer to settle (at 1/60 s).
    int max_settle_frames = 180;

    // If true, create OrganicGluon between each tile and its right / bottom
    // grid neighbor (cardinal adjacency). Produces a cohesive slab that still
    // breaks under enough force (force > contact_area * bond_strength).
    bool  bond_within_layer = false;
    float bond_strength     = 0.0f;  // material_strength for breaking calc

    // Max random rotation (radians) applied per tile at spawn (full range
    // around Z, 30% of it around X/Y). Zero = axis-aligned slabs. Loose
    // rubble wants jitter: irregular contact normals are what give an
    // impact its lateral scatter — flush axis-aligned boxes can only
    // compress straight down.
    float rotation_jitter = 0.0f;

    // Tile friction. Flush structural layers want it HIGH (compacted
    // subsoil ~0.85) or an impact's impulse rings through the rigid
    // grid like a Newton's cradle and the whole field slides.
    float friction = 0.5f;
};

struct ChunkBounds {
    float min_x = 0.0f, max_x = 0.0f;
    float min_y = 0.0f, max_y = 0.0f;
};

struct LayerResult {
    std::vector<int> particle_ids;
    // Grid adjacency for bonding (nx * ny cells, -1 = empty). Filled by
    // spawn_layer; consumed by bond_layer.
    std::vector<int> grid;
    int    nx = 0, ny = 0;
    size_t bond_count          = 0;
    int    settle_frames_taken = 0;
    size_t at_rest_count       = 0;
    float  max_top_z           = 0.0f;
};

struct Result {
    std::vector<LayerResult> layers;
};

// -----------------------------------------------------------------------------
// Incremental API — for callers that settle with their OWN frame loop.
// generate() below composes exactly these, stepping the engine itself;
// an interactive app instead calls spawn_layer, lets its normal frames
// run physics, polls layer_at_rest, then bond_layer, then spawns the
// next layer at base_z = layer_top_z of the settled one. The ground
// pours in and settles live, no blocked frames.
// -----------------------------------------------------------------------------

// Spawn one layer's tiles above base_z (no settling, no bonds).
LayerResult spawn_layer(Engine& engine, const LayerSpec& spec,
                        const ChunkBounds& bounds, float base_z,
                        std::mt19937& rng);

// True when every id reports is_at_rest. out_at_rest = how many do.
bool layer_at_rest(Engine& engine, const std::vector<int>& ids,
                   size_t& out_at_rest);

// Create intra-layer OrganicGluon bonds (cardinal neighbors). Call only
// after the layer has settled — bond offsets are cut from live positions.
void bond_layer(Engine& engine, LayerResult& layer, const LayerSpec& spec);

// Highest top face among ids (base_z for the next layer).
float layer_top_z(Engine& engine, const std::vector<int>& ids);

// Generate all layers sequentially, settling physics between each
// (steps the engine internally — do NOT call from inside a frame).
// Particles are added to the engine and, for bonded layers, OrganicGluon
// constraints are registered with the physics system.
Result generate(Engine& engine,
                const std::vector<LayerSpec>& specs,
                const ChunkBounds& bounds,
                std::mt19937& rng);

// -----------------------------------------------------------------------------
// Presets — proven layer tables shipped as convenience (games may always
// pass their own specs). earth(): huge flat cement-bonded bedrock slabs,
// mid rubble dropped into the gaps, thin organic topsoil. Bond strength
// matches Eden's bedrock (8000 N/m²).
// -----------------------------------------------------------------------------
std::vector<LayerSpec> earth_preset();

} // namespace StrataGenerator

#endif // STRATA_GENERATOR_H
