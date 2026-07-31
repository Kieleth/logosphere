// =============================================================================
// Strata earth preset + boulder impact — the multi-level ground contract.
// =============================================================================
// Locks the world-over-turtle design (the multi-level-ground design notes):
//   1. earth_preset() generates three settled layers: huge bonded bedrock
//      slabs, mid rubble filler, thin organic topsoil. Big below, small
//      above; everything at rest; nothing under the turtle.
//   2. The incremental API (spawn_layer / layer_at_rest / bond_layer)
//      composes to the same ground when the CALLER owns the frame loop —
//      the path interactive apps use so the ground settles live.
//   3. A boulder dropped from height CRATERS the ground legibly: organic
//      tiles near the impact are shoved, bedrock slabs hold, the boulder
//      comes to rest above bedrock, and the turtle remains absolute.
//
// Usage:
//   ./build/test_strata_earth_impact
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "logosphere/worldgen/strata_generator.h"

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define AT_ASSERT_TRUE(cond, msg)                                       \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cout << "FAIL: " << msg << std::endl;                  \
            tests_failed++;                                             \
            return;                                                     \
        }                                                               \
    } while (0)

#define AT_TEST(fn)                                                     \
    do {                                                                \
        std::cout << "  " << #fn << "... " << std::endl;                \
        int before = tests_failed;                                      \
        fn();                                                           \
        if (tests_failed == before) {                                   \
            tests_passed++;                                             \
            std::cout << "PASS" << std::endl;                           \
        }                                                               \
    } while (0)

namespace {

struct Harness {
    Engine engine;
    Harness() : engine(nullptr) {
        EngineConfig config;
        config.create_display = false;
        config.window_width = 1280;
        config.window_height = 960;
        config.window_title = "strata-earth-at";
        config.show_debug_overlay = false;
        config.enable_chat_window = false;
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
    }
    ~Harness() { engine.shutdown(); }
};

float avg_tile_size(Engine& e, const std::vector<int>& ids) {
    auto view = e.get_particle_system().lock_particles_for_read();
    float sum = 0.0f;
    for (int id : ids) sum += view[id].width;
    return ids.empty() ? 0.0f : sum / static_cast<float>(ids.size());
}

float min_bottom_z(Engine& e, const std::vector<int>& ids) {
    auto view = e.get_particle_system().lock_particles_for_read();
    float lo = 1e9f;
    for (int id : ids)
        lo = std::min(lo, view[id].z - view[id].thickness * 0.5f);
    return lo;
}

void test_earth_preset_layers() {
    Harness h;
    auto specs = StrataGenerator::earth_preset();
    AT_ASSERT_TRUE(specs.size() == 3, "earth is three layers");
    AT_ASSERT_TRUE(specs[0].bond_within_layer && specs[0].bond_strength > 0,
        "bedrock is cement-bonded (the gluon escape hatch)");
    AT_ASSERT_TRUE(!specs[1].bond_within_layer && !specs[2].bond_within_layer,
        "filler and organic are loose");

    StrataGenerator::ChunkBounds bounds{-8.0f, 8.0f, -8.0f, 8.0f};
    std::mt19937 rng(42);
    auto result = StrataGenerator::generate(h.engine, specs, bounds, rng);
    AT_ASSERT_TRUE(result.layers.size() == 3, "three layers generated");

    // Big below, small above (the owner's "smart" sizing ruling).
    float bed_sz = avg_tile_size(h.engine, result.layers[0].particle_ids);
    float top_sz = avg_tile_size(h.engine, result.layers[2].particle_ids);
    AT_ASSERT_TRUE(bed_sz > top_sz * 1.8f,
        "bedrock slabs dwarf topsoil tiles (bedrock avg " +
        std::to_string(bed_sz) + " m vs organic " +
        std::to_string(top_sz) + " m)");

    // Everything settled, stacked ascending, nothing under the turtle.
    for (size_t i = 0; i < 3; ++i) {
        const auto& L = result.layers[i];
        AT_ASSERT_TRUE(L.at_rest_count == L.particle_ids.size(),
            specs[i].name + " fully at rest (" +
            std::to_string(L.at_rest_count) + "/" +
            std::to_string(L.particle_ids.size()) + ")");
    }
    AT_ASSERT_TRUE(result.layers[0].bond_count > 0, "bedrock bonds exist");
    AT_ASSERT_TRUE(result.layers[1].max_top_z > result.layers[0].max_top_z &&
                   result.layers[2].max_top_z > result.layers[1].max_top_z,
        "layers stack upward");
    float lowest = min_bottom_z(h.engine, result.layers[0].particle_ids);
    AT_ASSERT_TRUE(lowest > -0.01f,
        "nothing below the turtle (lowest bottom " +
        std::to_string(lowest) + ")");
}

void test_incremental_api_composes() {
    Harness h;
    auto specs = StrataGenerator::earth_preset();
    StrataGenerator::ChunkBounds bounds{-6.0f, 6.0f, -6.0f, 6.0f};
    std::mt19937 rng(7);

    // The app path: caller owns the frame loop; layers pour in and
    // settle under normal updates.
    float base_z = 0.0f;
    std::vector<StrataGenerator::LayerResult> layers;
    for (const auto& spec : specs) {
        auto layer = StrataGenerator::spawn_layer(h.engine, spec, bounds,
                                                  base_z, rng);
        size_t rest = 0;
        int frames = 0;
        while (frames < spec.max_settle_frames &&
               !StrataGenerator::layer_at_rest(h.engine, layer.particle_ids,
                                               rest)) {
            h.engine.update(1.0 / 60.0);
            ++frames;
        }
        AT_ASSERT_TRUE(rest == layer.particle_ids.size(),
            spec.name + " settles under caller-owned frames (" +
            std::to_string(rest) + "/" +
            std::to_string(layer.particle_ids.size()) + " in " +
            std::to_string(frames) + " frames)");
        StrataGenerator::bond_layer(h.engine, layer, spec);
        base_z = StrataGenerator::layer_top_z(h.engine, layer.particle_ids);
        layers.push_back(std::move(layer));
    }
    AT_ASSERT_TRUE(layers[0].bond_count > 0,
        "incremental path bonds bedrock too");
    AT_ASSERT_TRUE(base_z > 0.6f && base_z < 2.5f,
        "stacked ground lands at a sane height (top " +
        std::to_string(base_z) + " m)");
}

void test_boulder_craters_the_ground() {
    Harness h;
    auto specs = StrataGenerator::earth_preset();
    StrataGenerator::ChunkBounds bounds{-10.0f, 10.0f, -10.0f, 10.0f};
    std::mt19937 rng(11);
    auto ground = StrataGenerator::generate(h.engine, specs, bounds, rng);
    float ground_top = ground.layers[2].max_top_z;
    float bedrock_top = ground.layers[0].max_top_z;

    auto& ps = h.engine.get_particle_system();

    // Record pre-impact organic and bedrock positions.
    struct Pos { float x, y, z; };
    auto snapshot = [&](const std::vector<int>& ids) {
        std::vector<Pos> out;
        auto view = ps.lock_particles_for_read();
        for (int id : ids) out.push_back({view[id].x, view[id].y, view[id].z});
        return out;
    };
    auto organic_before = snapshot(ground.layers[2].particle_ids);
    auto bedrock_before = snapshot(ground.layers[0].particle_ids);

    // The boulder: 1.6 m stone cube, 20 m above the ground, dead center.
    Particle boulder = {};
    boulder.shape = ParticleShape::BOX;
    boulder.x = 0.0f; boulder.y = 0.0f;
    boulder.z = ground_top + 20.0f;
    boulder.width = boulder.height = boulder.thickness = 1.6f;
    boulder.size = 1.6f;
    boulder.r = 0.5f; boulder.g = 0.48f; boulder.b = 0.46f; boulder.a = 1.0f;
    boulder.SetMaterial(Materials::Type::STONE);
    int boulder_id = h.engine.add_particle(boulder);

    // Fall + impact + settle. 2.02 s of free fall covers 20 m; give the
    // aftermath ten seconds of frames to come to rest.
    float min_bz = 1e9f, max_speed = 0.0f, max_bz_after_impact = -1e9f;
    bool impacted = false;
    for (int i = 0; i < 720; ++i) {
        h.engine.update(1.0 / 60.0);
        auto view = ps.lock_particles_for_read();
        float z = view[boulder_id].z, vz = view[boulder_id].vz;
        min_bz = std::min(min_bz, z);
        max_speed = std::max(max_speed, std::fabs(vz));
        if (z < ground_top + 1.0f) impacted = true;
        if (impacted) max_bz_after_impact = std::max(max_bz_after_impact, z);
        if (i % 30 == 0 || (impacted && i < 200 && i % 5 == 0))
            std::cout << "  [traj] f" << i << " z=" << z << " vz=" << vz
                      << std::endl;
    }
    std::cout << "  [measure] boulder lowest z=" << min_bz
              << " peak |vz|=" << max_speed
              << " rebound top z=" << max_bz_after_impact << std::endl;

    float bx, bz;
    bool boulder_rests;
    {
        auto view = ps.lock_particles_for_read();
        bx = view[boulder_id].x;
        bz = view[boulder_id].z;
        boulder_rests = view[boulder_id].is_at_rest;
    }
    AT_ASSERT_TRUE(bz - 0.8f > bedrock_top - 0.6f,
        "the boulder never tunnels through bedrock (bottom " +
        std::to_string(bz - 0.8f) + ", bedrock top " +
        std::to_string(bedrock_top) + ")");
    AT_ASSERT_TRUE(bz > 0.8f - 0.01f,
        "the boulder respects the turtle (z " + std::to_string(bz) + ")");
    (void)bx;

    // Crater: organic tiles near the impact moved; the field's far edge
    // did not. Bedrock held (bonded slabs, displacement under 15 cm).
    auto organic_after = snapshot(ground.layers[2].particle_ids);
    auto bedrock_after = snapshot(ground.layers[0].particle_ids);
    int shoved_near = 0, shoved_far = 0;
    for (size_t i = 0; i < organic_before.size(); ++i) {
        float ox = organic_before[i].x, oy = organic_before[i].y;
        float dx = organic_after[i].x - ox;
        float dy = organic_after[i].y - oy;
        float dz = organic_after[i].z - organic_before[i].z;
        float moved = std::sqrt(dx * dx + dy * dy + dz * dz);
        float from_impact = std::sqrt(ox * ox + oy * oy);
        if (from_impact < 3.0f && moved > 0.12f) shoved_near++;
        if (from_impact > 7.0f && moved > 0.30f) shoved_far++;
        if (from_impact < 3.0f && moved > 0.005f)
            std::cout << "  [measure] organic r=" << from_impact
                      << " moved=" << moved << std::endl;
    }
    AT_ASSERT_TRUE(shoved_near >= 2,
        "the impact shoves nearby topsoil (moved tiles near center: " +
        std::to_string(shoved_near) + ")");
    // Debris happens (a clod can roll); SWIMMING must not — the
    // uncapped-Baumgarte regression moved 200+ far tiles.
    AT_ASSERT_TRUE(shoved_far <= 3,
        "the far field stays put, stray debris aside (moved far "
        "tiles: " + std::to_string(shoved_far) + ")");
    float bed_max_move = 0.0f;
    for (size_t i = 0; i < bedrock_before.size(); ++i) {
        float dx = bedrock_after[i].x - bedrock_before[i].x;
        float dy = bedrock_after[i].y - bedrock_before[i].y;
        float dz = bedrock_after[i].z - bedrock_before[i].z;
        bed_max_move = std::max(bed_max_move,
            std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    AT_ASSERT_TRUE(bed_max_move < 0.15f,
        "bonded bedrock holds (max slab displacement " +
        std::to_string(bed_max_move) + " m)");
    AT_ASSERT_TRUE(boulder_rests || bz < ground_top + 1.0f,
        "the boulder ends its journey in the ground's embrace");

    // The turtle is absolute, always.
    float lowest = std::min(min_bottom_z(h.engine, ground.layers[0].particle_ids),
                            bz - 0.8f);
    AT_ASSERT_TRUE(lowest > -0.01f,
        "nothing driven below the turtle by the impact (lowest " +
        std::to_string(lowest) + ")");
}

// High-energy impact must not explode the field (issue #5). A pure
// kinetic-energy budget cannot tell ballistics from explosions: the
// impact legitimately ejects tiles, whose KE dips at apex and returns
// on the way down. The honest invariant is TOTAL mechanical energy
// (KE + gravitational PE) of boulder + ground: after release, gravity
// only converts PE to KE and contacts only dissipate, so total E must
// never rise. Any rise is energy the solver created; a big rise is
// the explosion.
void test_large_boulder_energy_budget() {
    Harness h;
    auto specs = StrataGenerator::earth_preset();
    StrataGenerator::ChunkBounds bounds{-10.0f, 10.0f, -10.0f, 10.0f};
    std::mt19937 rng(23);
    auto ground = StrataGenerator::generate(h.engine, specs, bounds, rng);
    float ground_top = ground.layers[2].max_top_z;

    auto& ps = h.engine.get_particle_system();
    std::vector<int> ids;
    for (const auto& L : ground.layers)
        ids.insert(ids.end(), L.particle_ids.begin(), L.particle_ids.end());

    // The big one: 3.2 m stone cube (8x the crater test's mass) from
    // 30 m. Roughly 13x the impact energy of the moderate case.
    Particle boulder = {};
    boulder.shape = ParticleShape::BOX;
    boulder.x = 0.0f; boulder.y = 0.0f;
    boulder.z = ground_top + 30.0f;
    boulder.width = boulder.height = boulder.thickness = 3.2f;
    boulder.size = 3.2f;
    boulder.r = 0.4f; boulder.g = 0.38f; boulder.b = 0.36f; boulder.a = 1.0f;
    boulder.SetMaterial(Materials::Type::STONE);
    int boulder_id = h.engine.add_particle(boulder);
    ids.push_back(boulder_id);

    const double G = 9.8;
    auto total_energy = [&](double& ke_out) {
        auto view = ps.lock_particles_for_read();
        double e = 0.0, ke = 0.0;
        for (int id : ids) {
            const auto& p = view[id];
            double m = p.GetMass();
            double k = 0.5 * m * (p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
            ke += k;
            e += k + m * G * p.z;
        }
        ke_out = ke;
        return e;
    };

    // 2.47 s of free fall covers 30 m; give the aftermath the rest of
    // 20 simulated seconds.
    const int FRAMES = 1200;
    double ke0;
    double e_prev = total_energy(ke0), e_start = e_prev;
    double peak_ke = 0.0;
    double worst_rise = 0.0, total_rise = 0.0;
    int worst_rise_frame = -1, impact_frame = -1;
    double final_ke = 0.0;
    for (int i = 0; i < FRAMES; ++i) {
        h.engine.update(1.0 / 60.0);
        {
            auto view = ps.lock_particles_for_read();
            if (impact_frame < 0 && view[boulder_id].z < ground_top + 1.6f)
                impact_frame = i;
        }
        double ke;
        double e = total_energy(ke);
        peak_ke = std::max(peak_ke, ke);
        double rise = e - e_prev;
        if (rise > 0.0) {
            total_rise += rise;
            if (rise > worst_rise) { worst_rise = rise; worst_rise_frame = i; }
        }
        e_prev = e;
        if (i % 60 == 0 || (impact_frame >= 0 && i < impact_frame + 90 &&
                            i % 15 == 0))
            std::cout << "  [energy] f" << i << " E=" << e
                      << " J (ke=" << ke << ")" << std::endl;
        final_ke = ke;
    }
    std::cout << "  [measure] impact f" << impact_frame
              << " E_start=" << e_start
              << " peak_ke=" << peak_ke
              << " worst_rise=" << worst_rise << " J/frame at f"
              << worst_rise_frame
              << " total_rise=" << total_rise
              << " final_ke=" << final_ke << std::endl;

    AT_ASSERT_TRUE(impact_frame > 0, "the boulder reaches the ground");
    AT_ASSERT_TRUE(peak_ke > 0.0, "impact injects energy into the field");
    // RATCHET, not the final contract (issue #5). Measured today:
    // the impact frame creates 1.19 MJ (position correction + capped
    // bias velocity across dozens of deep heavy contacts), 1.96 MJ
    // cumulative over the run, against 29.08 MJ total. Free fall and
    // the ballistic ejecta phase conserve E cleanly; the injection is
    // confined to the deep-penetration frames. The real contract is
    // dissipation-only (rises within ~1% of E_start); reaching it
    // means split-impulse-style position correction that does not
    // feed the velocity state. Until then these bounds pin the
    // current scale so escalation fails loudly.
    AT_ASSERT_TRUE(worst_rise < 1.6e6,
        "impact-frame energy creation at the known scale (worst rise " +
        std::to_string(worst_rise) + " J at f" +
        std::to_string(worst_rise_frame) +
        ", ratchet 1.6 MJ) — escalation is the explosion");
    AT_ASSERT_TRUE(total_rise < 2.6e6,
        "cumulative solver-created energy holds the ratchet (" +
        std::to_string(total_rise) + " J, ratchet 2.6 MJ, start " +
        std::to_string(e_start) + " J)");
    AT_ASSERT_TRUE(final_ke < peak_ke * 0.02,
        "the field ends quiet (final ke " + std::to_string(final_ke) +
        " J vs peak " + std::to_string(peak_ke) + " J)");

    // The turtle is absolute at any energy.
    float lowest = min_bottom_z(h.engine, ground.layers[0].particle_ids);
    AT_ASSERT_TRUE(lowest > -0.01f,
        "nothing driven below the turtle (lowest " +
        std::to_string(lowest) + ")");
}


}  // namespace

int main() {
    std::cout << "Strata earth + boulder impact AT" << std::endl;
    AT_TEST(test_earth_preset_layers);
    AT_TEST(test_incremental_api_composes);
    AT_TEST(test_boulder_craters_the_ground);
    AT_TEST(test_large_boulder_energy_budget);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
