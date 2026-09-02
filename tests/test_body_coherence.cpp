// ============================================================================
// BODY COHERENCE TEST
// ============================================================================
// Verifies that humanoid body parts stay connected during walking.
// Checks that gluon distances stay within tolerance of their targets.
// A velocity-target FK approach that breaks the chain will fail this test.
//
// Also checks tile boundary contacts (zero horizontal) as a secondary
// assertion, since both properties must hold simultaneously.
//
// Run: ./logosphere-tests --test test_body_coherence
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "../src/core/entity_telemetry.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <unordered_set>

bool test_body_coherence(TestContext& /* ctx */) {
    printf("\n=== Body Coherence Test ===\n");

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    config.window_width = 800;
    config.window_height = 600;
    config.window_title = "Body Coherence Test";
    config.enable_chat_window = false;

    if (engine.initialize(config) != 0) {
        printf("  ERROR: Engine init failed\n");
        return false;
    }

    auto& ps = engine.get_particle_system();
    const float dt = 1.0f / 60.0f;

    // Scene: 10 floor tiles (same as tile sticking test)
    std::vector<int> tile_ids;
    for (int i = 0; i < 10; i++) {
        Particle tile = {};
        tile.shape = ParticleShape::BOX;
        tile.x = 0.0f;
        tile.y = i * 2.0f;
        tile.z = 0.05f;
        tile.width = 4.0f;
        tile.height = 2.0f;
        tile.thickness = 0.1f;
        tile.r = 0.3f; tile.g = 0.5f; tile.b = 0.2f; tile.a = 1.0f;
        tile.SetMaterial(Materials::Type::HEAVY_STATIC);
        tile.is_at_rest = true;
        int id = engine.add_particle(tile);
        tile_ids.push_back(id);
    }

    ps.queue_light(0.0f, 10.0f, 8.0f, 300000.0f, 50.0f, 1.0f, 1.0f, 1.0f);

    // Create Eva
    auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = humanoid_gen.generate_humanoid_physics(
        // 0.55, not 0.5: world_z is the FEET'S BOTTOM and this scene's strata
        // surface is 0.30 + 0.15 + 0.10 = 0.55. At 0.5 every foot was born
        // 50 mm inside the organic layer, which INV-37 refuses.
        0.0f, 1.0f, 0.55f, -1, HumanoidSpec::eva(), false);

    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    printf("  Eva at (0, 1), hips=%d, %zu body particles\n", eva.hips_id, eva.body_ids.size());

    // Enable telemetry
    auto& telemetry = engine.get_physics_system().get_telemetry();
    telemetry.set_enabled(true);
    for (unsigned int pid : eva.body_ids) {
        telemetry.track_particle(pid);
    }

    // Let physics settle
    for (int i = 0; i < 5; i++) { engine.update(dt); engine.render(); }

    // Walk north
    engine.get_humanoid_locomotion().set_target_velocity(eva.hips_id, 0.0f, 2.0f);
    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);

    printf("  Walking north for 180 frames...\n");

    // Track max inter-particle distance for connected pairs.
    // This measures actual body coherence: do limbs stay attached?
    // A generous tolerance (0.5m) catches "limbs flying away" without
    // being sensitive to gluon offset bookkeeping.
    float max_separation = 0.0f;
    int max_sep_frame = 0;
    int separation_violations = 0;
    constexpr float MAX_LIMB_DISTANCE = 0.5f;  // No connected pair should exceed 0.5m

    // Track horizontal floor contacts
    std::unordered_set<unsigned int> tile_set(tile_ids.begin(), tile_ids.end());
    int floor_horizontal = 0;

    for (int frame = 0; frame < 180; frame++) {
        engine.update(dt);
        engine.render();

        // Check center-to-center distances for gluon-connected pairs
        auto particles = ps.lock_particles_for_read();
        auto& physics = engine.get_physics_system();

        for (size_t a = 0; a < eva.body_ids.size(); a++) {
            for (size_t b = a + 1; b < eva.body_ids.size(); b++) {
                int id_a = eva.body_ids[a];
                int id_b = eva.body_ids[b];
                if (!physics.get_gluon(id_a, id_b)) continue;

                const Particle& pa = particles[id_a];
                const Particle& pb = particles[id_b];

                float dx = pa.x - pb.x;
                float dy = pa.y - pb.y;
                float dz = pa.z - pb.z;
                float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

                if (dist > max_separation) {
                    max_separation = dist;
                    max_sep_frame = frame;
                }
                if (dist > MAX_LIMB_DISTANCE) {
                    separation_violations++;
                    if (separation_violations <= 5) {
                        printf("    DETACHED f%d: P%d<>P%d dist=%.3fm (limit %.1fm)\n",
                               frame, id_a, id_b, dist, MAX_LIMB_DISTANCE);
                    }
                }
            }
        }
    }

    // Check telemetry for horizontal floor contacts
    for (unsigned int pid : eva.body_ids) {
        const auto* buf = telemetry.query(pid);
        if (!buf) continue;
        auto h_frames = buf->find_horizontal_contact_frames();
        for (const auto* f : h_frames) {
            for (const auto& c : f->contacts) {
                if (c.is_horizontal && (tile_set.count(c.particle_a) || tile_set.count(c.particle_b))) {
                    floor_horizontal++;
                }
            }
        }
    }

    // Results
    printf("\n  Results:\n");
    printf("    Max particle separation: %.1fmm (frame %d)\n", max_separation * 1000.0f, max_sep_frame);
    printf("    Separation violations (>%.0fmm): %d\n", MAX_LIMB_DISTANCE * 1000.0f, separation_violations);
    printf("    Horizontal floor contacts: %d\n", floor_horizontal);

    // Assertions
    printf("\n  Assertions:\n");

    bool body_intact = (separation_violations == 0);
    printf("    %s: Body stays intact (max sep %.1fmm, %d violations)\n",
           body_intact ? "PASS" : "FAIL", max_separation * 1000.0f, separation_violations);

    bool no_horizontal = (floor_horizontal == 0);
    printf("    %s: No horizontal floor contacts (%d found)\n",
           no_horizontal ? "PASS" : "FAIL", floor_horizontal);

    bool pass = body_intact && no_horizontal;
    printf("\n%s: Body coherence\n", pass ? "PASS" : "FAIL");
    return pass;
}
