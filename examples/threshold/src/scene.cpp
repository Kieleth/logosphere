#include "scene.h"

#include "level.h"

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "materials.h"

#include <iostream>
#include <vector>

namespace threshold {

void build_level(Engine& engine) {
    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(kTileSize);
    // Chunks are 2D (tiles_per_chunk^2), and preload_chunks_around loads a
    // (2*radius+1)^2 grid of them -- the level is a narrow ~30m corridor
    // along X at a single fixed Y, so a naively large chunk/radius here
    // balloons into a huge-area 2D floor (a first pass at
    // tiles_per_chunk=40, radius=3 produced a 7x7 grid of 40x40 chunks,
    // ~156k particles, and 8+ second frames once chunk streaming/BVH
    // rebuild kicked in). 16m chunks with a radius-1 (3x3) preload comfortably
    // covers the level width in one axis while staying small in the other.
    strata.set_tiles_per_chunk(16);
    strata.set_load_radius(40.0f);
    strata.set_unload_radius(60.0f);

    std::vector<StrataLayerSpec> layers;
    StrataLayerSpec bedrock;
    bedrock.name = "bedrock";
    bedrock.material = Materials::Type::STONE;
    bedrock.thickness = kBedrockThickness;
    bedrock.r = 0.35f; bedrock.g = 0.33f; bedrock.b = 0.30f;
    // Unlike eden's bedrock (built to survive a meteor/boulder impact and
    // needs its slab to hold together), nothing here stresses the floor,
    // so it doesn't need inter-tile gluon bonds -- those are the dominant
    // physics-solver cost for a dense 1m-tile grid (~450ms/frame with
    // bonding on, single digits ms without, for the same tile count).
    bedrock.bond_within_layer = false;
    layers.push_back(bedrock);

    StrataLayerSpec walkway;
    walkway.name = "walkway";
    walkway.material = Materials::Type::STONE;
    walkway.thickness = 0.15f;
    walkway.r = 0.55f; walkway.g = 0.52f; walkway.b = 0.45f;
    walkway.bond_within_layer = false;
    layers.push_back(walkway);

    strata.set_layers(std::move(layers));

    strata.set_tile_skip_mask([](float x, float /*y*/, size_t layer_idx) {
        (void)layer_idx;
        if (x >= kGapMinX && x <= kGapMaxX) return true;   // true hole: both layers skipped
        // Leave the loose-tile column to spawn_loose_tile() instead, so
        // there isn't a static strata tile sitting under/overlapping the
        // separate collapsible one.
        const float half = kTileSize * 0.5f;
        return x >= kLooseTileX - half && x < kLooseTileX + half;
    });

    strata.set_enabled(true);
    // Preload from the level's midpoint, not its start, so one radius-1
    // (3x3 chunk) preload covers the whole ~30m corridor from the gap to
    // the exit without needing to stream more chunks in as the Prince walks.
    constexpr float kLevelMidX = (kPrinceStartX + kExitX) / 2.0f;
    strata.preload_chunks_around(kLevelMidX, kFixedDepthY, 1);

    std::cout << "[threshold] level: bedrock+walkway strata floor, gap at x="
              << kGapMinX << ".." << kGapMaxX << "\n";
}

int spawn_loose_tile(Engine& engine) {
    FloorTileConfig cfg;
    cfg.tile_width = kTileSize;
    cfg.tile_height = kTileSize;
    // Matches the strata walkway layer's thickness (build_level above) so
    // the tile sits flush with the surrounding floor, not proud of or
    // sunk into it.
    cfg.tile_thickness = 0.15f;
    cfg.r = 0.55f; cfg.g = 0.52f; cfg.b = 0.45f;

    auto& ps = engine.get_particle_system();
    std::vector<int> ids = ps.create_floor_grid(kLooseTileX, kFixedDepthY, 1, 1, cfg);
    if (ids.empty()) return -1;

    // create_floor_grid() always places a tile flush with the world's
    // turtle boundary (z = thickness/2) -- there's no arbitrary-Z overload.
    // The strata floor's bedrock+walkway stack sits higher than that (see
    // build_level: layers stack bottom-up from z=0), so without this the
    // loose tile would sit sunk below the surrounding walkway, and worse,
    // would already be resting on the turtle with no room to actually fall
    // when woken. Lift it to match the walkway's real height.
    auto view = ps.lock_particles_for_write();
    view[ids[0]].z = kBedrockThickness + cfg.tile_thickness * 0.5f;

    return ids[0];
}

uint32_t spawn_spikes(Engine& engine) {
    using logosphere::interaction::InteractionProfile;

    // Any id != 0 works (0 is the reserved default profile); this is the
    // only profile this example registers.
    constexpr uint32_t kSpikeProfileId = 1;

    InteractionProfile spikes;
    spikes.id = kSpikeProfileId;
    spikes.category = 1u << 1;   // distinct from the default profile's bit 0
    spikes.collides_with = 0;    // sensor only -- never a rigid contact with anything
    // Any nonzero medium parameter makes declares_medium() true, which is
    // what turns a filtered overlap into a VolumeEvent episode instead of
    // just a silently-dropped non-contact. The drag itself is negligible
    // (spikes shouldn't visibly slow a falling Prince, just hurt him).
    spikes.drag_coefficient = 0.01f;
    engine.get_interaction_system().register_profile(spikes);

    Particle p{};
    p.shape = ParticleShape::BOX;
    p.x = (kGapMinX + kGapMaxX) * 0.5f;
    p.y = kFixedDepthY;
    p.z = 0.05f;   // pit floor, near the turtle boundary
    p.width = kGapMaxX - kGapMinX;
    p.height = kTileSize;
    p.thickness = 0.1f;
    p.SetMaterial(Materials::Type::IRON);
    p.r = 0.55f; p.g = 0.55f; p.b = 0.6f;
    p.is_at_rest = true;
    p.interaction_profile_id = kSpikeProfileId;

    auto& ps = engine.get_particle_system();
    ps.queue_particle_addition(p);
    ps.flush_pending_particles();

    std::cout << "[threshold] spikes armed at x=" << kGapMinX << ".." << kGapMaxX
              << " profile=" << kSpikeProfileId << "\n";
    return kSpikeProfileId;
}

SpawnedHumanoid spawn_humanoid(Engine& engine, float x, float reflexes_ms, float grit_w) {
    auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
    auto physics = humanoid_gen.generate_humanoid_physics(
        x, kFixedDepthY, 0.5f, -1, HumanoidSpec::default_human(), false);

    SpawnedHumanoid out;
    out.hips_id = physics.hips_id;
    // right_arm_ids = {shoulder, upper_arm, forearm, hand} (see
    // HumanoidGenerator::generate_humanoid_physics).
    if (physics.right_arm_ids.size() >= 4) {
        out.right_forearm_id = physics.right_arm_ids[2];
        out.right_hand_id = physics.right_arm_ids[3];
    }

    auto& kg = engine.get_kg();
    physics.create_kg_entities(kg, "Human", reflexes_ms, grit_w);
    out.entity_id = physics.entity_id;

    engine.get_humanoid_locomotion().register_humanoid_direct(
        out.hips_id,
        physics.left_leg_ids, physics.right_leg_ids,
        physics.left_arm_ids, physics.right_arm_ids,
        physics.torso_ids, reflexes_ms, grit_w, out.entity_id);

    return out;
}

int spawn_sword(Engine& engine) {
    using logosphere::interaction::InteractionProfile;

    // Distinct from the spike profile (id 1): sensor-only like it, but
    // declares no medium, so it never opens a VolumeEvent episode either
    // -- purely invisible to every system except the renderer.
    constexpr uint32_t kSwordPropProfileId = 2;
    InteractionProfile prop;
    prop.id = kSwordPropProfileId;
    prop.category = 1u << 2;
    prop.collides_with = 0;
    engine.get_interaction_system().register_profile(prop);

    Particle p{};
    p.shape = ParticleShape::BOX;
    p.x = 0.0f; p.y = kFixedDepthY; p.z = 1.0f;   // parked; update_guard repositions every frame
    p.width = 0.6f;      // blade length, along local +X
    p.height = 0.05f;
    p.thickness = 0.05f;
    p.SetMaterial(Materials::Type::STEEL);
    p.r = 0.75f; p.g = 0.78f; p.b = 0.82f;
    p.is_at_rest = true;
    p.solver_mode = ParticleSolverMode::KINEMATIC;
    p.interaction_profile_id = kSwordPropProfileId;

    auto& ps = engine.get_particle_system();
    int id = ps.queue_particle_addition(p);
    ps.flush_pending_particles();
    return id;
}

} // namespace threshold
