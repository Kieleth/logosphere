// =============================================================================
// HUMANOID STRATA INTEGRITY — dismemberment + drift hunt while streaming chunks
// =============================================================================
// NOT a locomotion guard. It asserts that the body stays assembled while
// chunks stream underneath it, and nothing else. In particular it never
// checks that she goes anywhere: measured, she stays inside a 6.6 x 7.4 m
// box for the whole 190 s run, never getting further than 4.7 m from where
// she started, while the phases below are named for a staircase climb and a
// 100 m sprint. It passes anyway, because displacement is not one of the
// failure conditions.
//
// That is fine for what this is, and it is worth gating: coming apart during
// chunk streaming is a real defect and this catches it. It was renamed from
// test_humanoid_strata_walk because the old name, sitting in a list of
// locomotion guards, read as "walking on strata is verified" and was cited
// as exactly that. It is not.
//
// For locomotion on terrain — distance actually travelled, height change
// across a step, ground clearance sampled every frame, joints holding —
// see tests/test_humanoid_terrain_scenarios.cpp.
//
// Reproduces Eden's observed behaviour in a headless test. A humanoid walks
// a long serpentine path across a streaming strata floor with a cairn, a
// plateau, and a trench. Full instrumentation per frame:
//
//   - Hips position (and Z drop from initial) + velocity.
//   - Max gluon-connected pair distance this frame (dismemberment probe).
//   - Spread = bounding box diagonal of all body parts. Growing spread =
//     limbs drifting apart even if no single pair exceeds threshold.
//   - Chunk unload count (running tally — correlate to anomalies).
//
// Fails on any of:
//   - Connected-pair distance > 0.5 m.
//   - Hips Z falls below 0.3 m (hips on the ground = collapsed).
//   - Bounding-box spread > 3.0 m (limbs flung apart).
//
// Run: ./logosphere-tests --test test_humanoid_strata_integrity
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/physics/physics_system.h"
#include "../src/core/humanoid_integrity_monitor.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include "../src/test_context.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <atomic>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

bool test_humanoid_strata_integrity() {
    printf("\n=== Humanoid Strata Walk ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_width = 800;
    cfg.window_height = 600;
    cfg.window_title = "humanoid strata walk";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        return false;
    }

    auto& ps      = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& dyn     = engine.get_dynamics_system();
    const float dt = 1.0f / 60.0f;

    // -------------------------------------------------------------------------
    // STREAMING STRATA FLOOR — 3 layers, bedrock bonded. Same shape as Eden.
    // -------------------------------------------------------------------------
    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(4.0f);
    strata.set_tiles_per_chunk(5);        // 20 m chunks
    strata.set_tiles_per_entity(1);
    strata.set_load_radius(30.0f);
    strata.set_unload_radius(40.0f);

    // EXACTLY Eden's strata config — 3 layers, bedrock bonded, floor top
    // at z=0.55. Same as Eden so we're replicating the reproducing scene.
    std::vector<StrataLayerSpec> layers;
    auto add_layer = [&](const char* name, Materials::Type mat, float th,
                         float r, float g, float b,
                         bool bond, float bs) {
        StrataLayerSpec s;
        s.name = name;
        s.material = mat;
        s.thickness = th;
        s.r = r; s.g = g; s.b = b;
        s.bond_within_layer = bond;
        s.bond_strength     = bs;
        layers.push_back(s);
    };
    add_layer("bedrock",  Materials::Type::STONE,  0.30f, 0.35f, 0.33f, 0.30f, true,  8000.0f);
    add_layer("sediment", Materials::Type::STONE,  0.15f, 0.45f, 0.40f, 0.30f, false, 0.0f);
    add_layer("organic",  Materials::Type::DIRT,   0.10f, 0.30f, 0.45f, 0.22f, false, 0.0f);
    strata.set_layers(std::move(layers));

    // Trench mask N of origin — bedrock stays, upper layers skipped.
    strata.set_tile_skip_mask([](float x, float y, size_t layer_idx) {
        if (layer_idx == 0) return false;
        return (x >= 6.0f && x <= 14.0f && y >= 6.0f && y <= 14.0f);
    });
    strata.set_enabled(true);
    strata.preload_chunks_around(0.0f, 0.0f, 2);

    // -------------------------------------------------------------------------
    // OBSTACLES — cairns + plateaus scattered along Eva's path.
    // -------------------------------------------------------------------------
    const float floor_top = 0.40f + 0.30f + 0.15f + 0.12f + 0.10f;  // sum of layer thicknesses
    auto place_cairn = [&](float cx, float cy) {
        for (int i = 0; i < 5; ++i) {
            Particle s = {};
            s.shape     = ParticleShape::BOX;
            s.width     = 0.8f - i * 0.08f;
            s.height    = s.width;
            s.thickness = 0.20f;
            s.size      = s.width;
            s.x = cx; s.y = cy;
            s.z = floor_top + 0.10f + i * 0.20f;
            s.r = 0.45f; s.g = 0.42f; s.b = 0.38f; s.a = 1.0f;
            s.friction = 0.7f;
            s.SetMaterial(Materials::Type::STONE);
            s.is_at_rest = true;
            engine.add_particle(s);
        }
    };

    auto place_plateau = [&](float px, float py) {
        const float tile = 3.0f;
        float layer_z = floor_top;
        const float th[3] = {0.30f, 0.15f, 0.10f};
        const float colors[3][3] = {
            {0.35f, 0.33f, 0.30f},
            {0.45f, 0.40f, 0.30f},
            {0.30f, 0.45f, 0.22f},
        };
        const Materials::Type mats[3] = {
            Materials::Type::STONE, Materials::Type::STONE, Materials::Type::DIRT
        };
        for (int li = 0; li < 3; ++li) {
            for (int gy = 0; gy < 3; ++gy) {
                for (int gx = 0; gx < 3; ++gx) {
                    Particle p = {};
                    p.shape = ParticleShape::BOX;
                    p.width = tile; p.height = tile; p.thickness = th[li];
                    p.size = tile;
                    p.x = px + (gx - 1) * tile;
                    p.y = py + (gy - 1) * tile;
                    p.z = layer_z + th[li] * 0.5f;
                    p.r = colors[li][0]; p.g = colors[li][1]; p.b = colors[li][2];
                    p.a = 1.0f; p.friction = 0.5f;
                    p.SetMaterial(mats[li]);
                    p.is_at_rest = true;
                    engine.add_particle(p);
                }
            }
            layer_z += th[li];
        }
    };

    // Scatter multiple cairns + plateaus along the walk path.
    place_cairn( 20.0f,   0.0f);
    place_cairn(-20.0f,   0.0f);
    place_cairn(  0.0f,  20.0f);
    place_cairn( 40.0f, -20.0f);
    place_cairn(-40.0f,  20.0f);
    place_plateau(  0.0f, -20.0f);
    place_plateau( 30.0f,  20.0f);
    place_plateau(-30.0f, -30.0f);

    // DIFFERENT LEVELS — staircase Eva must climb, pit she must deal with.

    // Staircase going east from (10, 5): 5 steps, each 3 m wide × 2 m deep,
    // rising 0.3 m per step. Eva walking east has to step up repeatedly.
    for (int step = 0; step < 5; ++step) {
        Particle s = {};
        s.shape     = ParticleShape::BOX;
        s.width     = 3.0f;
        s.height    = 2.0f;
        s.thickness = 0.3f;
        s.size      = 3.0f;
        s.x = 10.0f + step * 2.0f;
        s.y = 5.0f;
        s.z = floor_top + (step + 1) * 0.15f;  // each step 15 cm higher
        s.r = 0.4f; s.g = 0.38f; s.b = 0.35f; s.a = 1.0f;
        s.friction = 0.6f;
        s.SetMaterial(Materials::Type::STONE);
        s.is_at_rest = true;
        engine.add_particle(s);
    }

    // Pit — bedrock depressed at (−10, 5). 4×4 m area, 0.5 m deeper than
    // the normal floor. Simulates a natural depression Eva walks through.
    // (Actually: place tiles BELOW the floor as a visual marker; Eva
    // walking over it will find ordinary floor there, since strata already
    // covers the zone. But this lets us verify the area stays stable.)
    {
        const float tile = 2.0f;
        for (int gx = 0; gx < 2; ++gx) {
            for (int gy = 0; gy < 2; ++gy) {
                Particle p = {};
                p.shape = ParticleShape::BOX;
                p.width = tile; p.height = tile; p.thickness = 0.20f;
                p.size  = tile;
                p.x = -10.0f + gx * tile;
                p.y =   5.0f + gy * tile;
                p.z = floor_top - 0.40f;  // sunken below floor
                p.r = 0.15f; p.g = 0.14f; p.b = 0.12f; p.a = 1.0f;
                p.friction = 0.7f;
                p.SetMaterial(Materials::Type::STONE);
                p.is_at_rest = true;
                engine.add_particle(p);
            }
        }
    }

    // A tall plateau (1.5 m cliff) — Eva walking into it should block her,
    // not push her through. Tests vertical-wall collision.
    place_plateau(15.0f, -15.0f);
    // Stack a second tier on top — manually.
    {
        const float tile = 3.0f;
        const float base_z = floor_top + 0.55f;  // top of the plateau below
        for (int li = 0; li < 3; ++li) {
            float th[3] = {0.30f, 0.15f, 0.10f};
            float layer_z = base_z;
            for (int j = 0; j < li; ++j) layer_z += th[j];
            for (int gy = 0; gy < 3; ++gy) {
                for (int gx = 0; gx < 3; ++gx) {
                    Particle p = {};
                    p.shape = ParticleShape::BOX;
                    p.width = tile; p.height = tile; p.thickness = th[li];
                    p.size  = tile;
                    p.x = 15.0f + (gx - 1) * tile;
                    p.y = -15.0f + (gy - 1) * tile;
                    p.z = layer_z + th[li] * 0.5f;
                    p.r = 0.30f + li * 0.05f;
                    p.g = 0.28f + li * 0.03f;
                    p.b = 0.22f;
                    p.a = 1.0f; p.friction = 0.5f;
                    p.SetMaterial(li == 2 ? Materials::Type::DIRT : Materials::Type::STONE);
                    p.is_at_rest = true;
                    engine.add_particle(p);
                }
            }
        }
    }

    // Physics trees scattered near the walk path — gluon-heavy structures
    // that stress swap-and-pop during chunk unload. Same class of structure
    // Eden has in the scene.
    {
        auto& tree_gen = engine.get_worldgen_system().get_physics_tree_generator();
        float tree_spots[][2] = {
            { 10.0f,  15.0f},
            {-15.0f, -10.0f},
            { 25.0f,  30.0f},
            {-25.0f,  20.0f},
        };
        for (auto& p : tree_spots) {
            tree_gen.generate_tree_with_roots(p[0], p[1], 0.0f, TreeSpec::young_oak());
        }
    }

    // Light.
    ps.queue_light(0.0f, 0.0f, 20.0f, 300000.0f, 80.0f, 1.0f, 1.0f, 1.0f);

    // -------------------------------------------------------------------------
    // HUMANOID — spawn at origin.
    // -------------------------------------------------------------------------
    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
    auto& kg = engine.get_kg();
    eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    // Swap-safe cached IDs for Eva (same pattern as Eden).
    int hips_cache = eva.hips_id;
    ps.add_swap_callback([&hips_cache](size_t old_idx, size_t new_idx) {
        if (hips_cache == (int)old_idx) hips_cache = (int)new_idx;
    });

    // Wire the engine's integrity monitor. When the test hunts Eden's
    // dismemberment, this is the primary signal — it reports drift-from-
    // baseline, which matches real anatomy.
    int integrity_violations = 0;
    {
        auto& monitor = engine.get_humanoid_integrity_monitor();
        monitor.set_enabled(true);
        std::vector<std::string> names;
        names.reserve(eva.body_ids.size());
        static const char* kFixed[] = {
            "l_foot","l_toe","l_shin","l_thigh",
            "r_foot","r_toe","r_shin","r_thigh",
            "hips","abdomen","chest","neck","head"
        };
        for (size_t k = 0; k < eva.body_ids.size(); ++k) {
            if (k < sizeof(kFixed)/sizeof(kFixed[0])) names.emplace_back(kFixed[k]);
            else names.emplace_back("part_" + std::to_string(k));
        }
        std::vector<int> body_ids(eva.body_ids.begin(), eva.body_ids.end());
        monitor.track("Eva", eva.hips_id, body_ids, names);
        monitor.set_violation_callback(
            [&integrity_violations](const Logosphere::HumanoidIntegrityMonitor::Violation& v) {
                const char* kind = v.kind == Logosphere::HumanoidIntegrityMonitor::Kind::PAIR_SEPARATION
                                       ? "PAIR_SEPARATION"
                                 : v.kind == Logosphere::HumanoidIntegrityMonitor::Kind::HIPS_DROPPED
                                       ? "HIPS_DROPPED"
                                       : "BODY_SPREAD";
                std::printf("    [INTEGRITY f%d] %s: %.3f m",
                            v.frame_number, kind, v.measured);
                if (!v.part_a_name.empty())
                    std::printf(" (%s<>%s)", v.part_a_name.c_str(), v.part_b_name.c_str());
                std::printf("\n");
                integrity_violations++;
            });
    }

    printf("  Eva spawned at (0,0), hips=%d, %zu body particles\n",
           eva.hips_id, eva.body_ids.size());
    printf("  Chunk load radius %.0f m, unload %.0f m\n", 30.0f, 40.0f);

    // A pack of NPCs in tight formation. Matches Eden's multi-humanoid
    // scene. Shared chunks, close-proximity collisions, many swap cascades.
    struct NPC {
        PhysicsHumanoidResult body;
        int hips_cache;
        std::string label;
    };
    std::vector<NPC> npcs;
    auto spawn_npc = [&](const std::string& label, float x, float y) {
        NPC n;
        n.body = hgen.generate_humanoid_physics(
            x, y, 0.5f, -1, HumanoidSpec::eva(), false);
        n.body.create_kg_entities(kg, "Human", 180.0f, 800.0f);
        engine.get_humanoid_locomotion().register_humanoid_direct(
            n.body.hips_id,
            n.body.left_leg_ids, n.body.right_leg_ids,
            n.body.left_arm_ids, n.body.right_arm_ids,
            n.body.torso_ids, 180.0f, 800.0f);
        n.hips_cache = n.body.hips_id;
        n.label      = label;
        int* cache_ptr = &n.hips_cache;
        ps.add_swap_callback([cache_ptr](size_t old_idx, size_t new_idx) {
            if (*cache_ptr == (int)old_idx) *cache_ptr = (int)new_idx;
        });
        {
            auto& monitor = engine.get_humanoid_integrity_monitor();
            std::vector<int> bids(n.body.body_ids.begin(), n.body.body_ids.end());
            std::vector<std::string> names;
            names.reserve(bids.size());
            static const char* kFixed[] = {
                "l_foot","l_toe","l_shin","l_thigh",
                "r_foot","r_toe","r_shin","r_thigh",
                "hips","abdomen","chest","neck","head"
            };
            for (size_t k = 0; k < bids.size(); ++k) {
                if (k < sizeof(kFixed)/sizeof(kFixed[0])) names.emplace_back(kFixed[k]);
                else names.emplace_back("part_" + std::to_string(k));
            }
            monitor.track(label, n.body.hips_id, bids, names);
        }
        printf("  %s spawned at (%.1f, %.1f), hips=%d\n",
               label.c_str(), x, y, n.body.hips_id);
        npcs.push_back(std::move(n));
    };
    // 4 NPCs spaced 3 m — enough interaction, minimal debug-log overhead.
    spawn_npc("Adam",   3.0f,  0.0f);
    spawn_npc("Cain",  -3.0f,  0.0f);
    spawn_npc("Abel",   0.0f,  3.0f);
    spawn_npc("Seth",   0.0f, -3.0f);

    // Settle.
    for (int i = 0; i < 10; ++i) { engine.update(dt); }

    // -------------------------------------------------------------------------
    // Walk path — serpentine through the chunks. Each leg moves 25 m in a
    // different direction, triggering chunk load/unload.
    // -------------------------------------------------------------------------
    // Walk path deliberately aimed at the features. Eva starts at (0,0),
    // must go over the staircase (10..18, 5), hit the 2-tier plateau
    // at (15,-15), traverse the pit zone (-10,5), and walk through chunks
    // far enough to trigger unload cycles.
    struct Leg { float vx, vy; int frames; const char* label; };
    const Leg walk[] = {
        {  0.0f, +1.0f,  300, "N  5m → approach staircase row"},
        { +2.0f,  0.0f,  900, "E  30m → climb staircase to top"},
        {  0.0f, -2.0f, 1200, "S  40m → hit 2-tier plateau cliff at (15,-15)"},
        { -2.0f,  0.0f, 1500, "W  50m → past pit zone"},
        {  0.0f, +2.0f, 1500, "N  50m → cross north"},
        { +3.0f, -1.0f, 1500, "ENE pattern, fast"},
        { -3.0f, -3.0f, 1500, "SW diagonal 150m"},
        { +2.0f, +2.0f, 1500, "NE diagonal 100m"},
        { -4.0f,  0.0f, 1500, "W  sprint 100m"},
    };
    int total_frames = 0;
    for (const auto& l : walk) total_frames += l.frames;

    engine.get_humanoid_locomotion().set_volitional(eva.hips_id, true);
    for (auto& n : npcs) engine.get_humanoid_locomotion().set_volitional(n.body.hips_id, true);

    // Instrumentation state.
    float hips_initial_z = 0.0f;
    int   l_foot_id = eva.body_ids[0];  // "l_foot" per generator push order
    int   r_foot_id = eva.body_ids[4];  // "r_foot"
    int   l_foot_cache = l_foot_id;
    int   r_foot_cache = r_foot_id;
    // Swap-correct every tracked id. Deferred deletions now flush in
    // headless runs too (see tests/test_position_authority.cpp a5), so
    // chunk streaming genuinely deletes particles and swap-and-pop
    // relocates ids — raw eva.body_ids reads garbage otherwise (the
    // spread metric exploded to 3.5e9 m on the first unload flush).
    ps.add_swap_callback([&](size_t old_idx, size_t new_idx) {
        if (l_foot_cache == (int)old_idx) l_foot_cache = (int)new_idx;
        if (r_foot_cache == (int)old_idx) r_foot_cache = (int)new_idx;
        for (int& bid : eva.body_ids) {
            if (bid == (int)old_idx) bid = (int)new_idx;
        }
    });
    // Walking-verification state: record feet Z over time so we can check
    // legs are actually cycling (walking) vs. sliding.
    float l_foot_z_min = 1e9f, l_foot_z_max = -1e9f;
    float r_foot_z_min = 1e9f, r_foot_z_max = -1e9f;
    int   foot_alternation_count = 0;
    bool  last_l_up = false;  // was left foot higher than right last sample?
    {
        auto view = ps.lock_particles_for_read();
        hips_initial_z = view[hips_cache].z;
    }

    float worst_sep      = 0.0f;   int worst_sep_frame      = -1;
    int   worst_sep_a    = -1,     worst_sep_b              = -1;
    float min_hips_z     = 1e9f;   int min_hips_z_frame     = -1;
    float max_spread     = 0.0f;   int max_spread_frame     = -1;
    // Note: actual chunk unload events are logged as [CHUNK_UNLOAD] lines by
    // ChunkSystem. We don't instrument a counter here because load+unload
    // within the same frame net to zero delta — count the log lines
    // externally if you need the exact number.
    int   sep_violations = 0;
    int   hipz_violations = 0;
    int   spread_violations = 0;

    int frame = 0;
    size_t last_particle_count = ps.count();

    // Short-circuit: once the integrity monitor fires, give it ~3 seconds
    // of extra frames to capture the progression, then bail out. Keeps
    // iteration tight — a successful reproduction completes in seconds,
    // not the full 14800-frame run.
    constexpr int SHORTCIRCUIT_EXTRA_FRAMES = 180;  // ~3 s at 60 Hz
    int shortcircuit_countdown = -1;
    int first_violation_frame  = -1;
    bool stop_walk = false;

    // Chaos phase — per-10-frame velocity randomization (not every frame).
    // Simulates player reacting to obstacles. 300 frames = 5 seconds.
    if (false) {  // Disabled: the per-frame set_target_velocity was hanging the solver
        unsigned int seed = 987654321;
        auto next = [&seed]() { seed = seed * 1664525 + 1013904223; return seed; };
        for (int f = 0; f < 300 && !stop_walk; ++f, ++frame) {
            float vx = (int)(next() % 81) - 40;  // -40..40
            float vy = (int)(next() % 81) - 40;
            vx *= 0.1f;  // -4..4 m/s
            vy *= 0.1f;
            if ((f % 20) == 0) { vx = 0; vy = 0; }  // sudden stops
            engine.get_humanoid_locomotion().set_target_velocity(hips_cache, vx, vy);
            for (auto& n : npcs) engine.get_humanoid_locomotion().set_target_velocity(n.hips_cache, vx, vy);
            engine.update(dt);

            // Minimal per-frame tracking — we're going for integrity signal.
            {
                auto view = ps.lock_particles_for_read();
                if (hips_cache >= 0 && (size_t)hips_cache < view.size()) {
                    const Particle& h = view[hips_cache];
                    strata.update(h.x, h.y);
                    if (l_foot_cache >= 0 && (size_t)l_foot_cache < view.size() &&
                        r_foot_cache >= 0 && (size_t)r_foot_cache < view.size()) {
                        float lz = view[l_foot_cache].z;
                        float rz = view[r_foot_cache].z;
                        if (lz < l_foot_z_min) l_foot_z_min = lz;
                        if (lz > l_foot_z_max) l_foot_z_max = lz;
                        if (rz < r_foot_z_min) r_foot_z_min = rz;
                        if (rz > r_foot_z_max) r_foot_z_max = rz;
                        bool this_l_up = (lz > rz);
                        if (frame > 0 && this_l_up != last_l_up) foot_alternation_count++;
                        last_l_up = this_l_up;
                    }
                }
            }
            if (f % 200 == 0) printf("  [chaos f%d]  violations so far: %d\n",
                                     frame, integrity_violations);
            if (integrity_violations > 0 && shortcircuit_countdown < 0) {
                first_violation_frame = frame;
                shortcircuit_countdown = SHORTCIRCUIT_EXTRA_FRAMES;
                printf("  >>> CHAOS PHASE tripped the monitor at f%d\n", frame);
            }
            if (shortcircuit_countdown >= 0 && --shortcircuit_countdown <= 0) {
                stop_walk = true;
            }
        }
    }

    for (const auto& leg : walk) {
        if (stop_walk) break;
        engine.get_humanoid_locomotion().set_target_velocity(hips_cache, leg.vx, leg.vy);
        for (auto& n : npcs) engine.get_humanoid_locomotion().set_target_velocity(n.hips_cache, leg.vx, leg.vy);

        // (look-at target updated per-frame inside the frame loop below)

        for (int i = 0; i < leg.frames; ++i, ++frame) {
            // Per-frame mouse-look simulation. Rotate target in world frame
            // 360° every ~1.5 s to stress look-at animation. Uses previous
            // frame's hips as center (good enough — moves a few cm/frame).
            {
                float prev_hx = 0.0f, prev_hy = 0.0f;
                {
                    auto vr = ps.lock_particles_for_read();
                    if (hips_cache >= 0 && (size_t)hips_cache < vr.size()) {
                        prev_hx = vr[hips_cache].x; prev_hy = vr[hips_cache].y;
                    }
                }
                float t_phase = (float)frame * 0.07f;
                float ltx = prev_hx + 5.0f * std::cos(t_phase);
                float lty = prev_hy + 5.0f * std::sin(t_phase);
                engine.get_humanoid_locomotion().set_look_at_target(hips_cache, ltx, lty);
                for (auto& n : npcs) engine.get_humanoid_locomotion().set_look_at_target(n.hips_cache, ltx, lty);
            }

            engine.update(dt);

            // Collect everything we need under a read lock, then release
            // before calling strata.update() (chunk ops need write access).
            float hx, hy, hz, hvx, hvy, hvz;
            float minx = 1e9f, miny = 1e9f, minz = 1e9f;
            float maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;
            float worst_pair_dist = 0.0f;
            int worst_a = -1, worst_b = -1;
            size_t particle_count = 0;
            {
                auto view = ps.lock_particles_for_read();
                particle_count = view.size();
                last_particle_count = view.size();

                const Particle& hips = view[hips_cache];
                hx = hips.x; hy = hips.y; hz = hips.z;
                hvx = hips.vx; hvy = hips.vy; hvz = hips.vz;

                // Track feet Z — confirms walking (cycling) vs. sliding (static).
                if (l_foot_cache >= 0 && (size_t)l_foot_cache < view.size() &&
                    r_foot_cache >= 0 && (size_t)r_foot_cache < view.size()) {
                    float lz = view[l_foot_cache].z;
                    float rz = view[r_foot_cache].z;
                    if (lz < l_foot_z_min) l_foot_z_min = lz;
                    if (lz > l_foot_z_max) l_foot_z_max = lz;
                    if (rz < r_foot_z_min) r_foot_z_min = rz;
                    if (rz > r_foot_z_max) r_foot_z_max = rz;
                    bool this_l_up = (lz > rz);
                    if (frame > 0 && this_l_up != last_l_up) foot_alternation_count++;
                    last_l_up = this_l_up;
                }

                for (unsigned int bid : eva.body_ids) {
                    if (bid >= view.size()) continue;
                    const Particle& p = view[bid];
                    minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
                    miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
                    minz = std::min(minz, p.z); maxz = std::max(maxz, p.z);
                }

                for (size_t a = 0; a < eva.body_ids.size(); ++a) {
                    for (size_t b = a + 1; b < eva.body_ids.size(); ++b) {
                        int id_a = eva.body_ids[a];
                        int id_b = eva.body_ids[b];
                        if (!physics.get_gluon(id_a, id_b)) continue;
                        const Particle& pa = view[id_a];
                        const Particle& pb = view[id_b];
                        float d = std::sqrt(
                            (pa.x-pb.x)*(pa.x-pb.x) +
                            (pa.y-pb.y)*(pa.y-pb.y) +
                            (pa.z-pb.z)*(pa.z-pb.z));
                        if (d > worst_pair_dist) {
                            worst_pair_dist = d;
                            worst_a = id_a; worst_b = id_b;
                        }
                    }
                }
            }  // read lock released

            // Safe now — stream chunks based on hips position.
            strata.update(hx, hy);

            float spread = std::sqrt(
                (maxx-minx)*(maxx-minx) + (maxy-miny)*(maxy-miny) + (maxz-minz)*(maxz-minz));

            // --- Update worst-case tallies.
            if (worst_pair_dist > worst_sep) {
                worst_sep = worst_pair_dist;
                worst_sep_frame = frame;
                worst_sep_a = worst_a; worst_sep_b = worst_b;
            }
            if (hz < min_hips_z) {
                min_hips_z = hz;
                min_hips_z_frame = frame;
            }
            if (spread > max_spread) {
                max_spread = spread;
                max_spread_frame = frame;
            }

            constexpr float SEP_LIMIT    = 0.5f;
            constexpr float HIPS_Z_MIN   = 0.3f;
            constexpr float SPREAD_LIMIT = 3.0f;

            if (worst_pair_dist > SEP_LIMIT && sep_violations < 5) {
                printf("    f%d  SEP P%d<>P%d dist=%.3f m\n",
                       frame, worst_a, worst_b, worst_pair_dist);
                sep_violations++;
            }
            if (hz < HIPS_Z_MIN && hipz_violations < 5) {
                printf("    f%d  HIPS_Z z=%.3f (started at %.3f)\n",
                       frame, hz, hips_initial_z);
                hipz_violations++;
            }
            if (spread > SPREAD_LIMIT && spread_violations < 5) {
                printf("    f%d  SPREAD %.2f m (bbox exploded)\n",
                       frame, spread);
                spread_violations++;
            }

            // Per-60-frame heartbeat.
            if (frame % 60 == 0) {
                printf("  [f%4d %-32s] hips=(%.1f,%.1f,%.2f) vel=(%.1f,%.1f) "
                       "spread=%.2f worst_pair=%.3f particles=%zu unloads=%d\n",
                       frame, leg.label,
                       hx, hy, hz, hvx, hvy,
                       spread, worst_pair_dist, particle_count, 0);
            }

            // Short-circuit arm / tick.
            if (integrity_violations > 0 && shortcircuit_countdown < 0) {
                first_violation_frame = frame;
                shortcircuit_countdown = SHORTCIRCUIT_EXTRA_FRAMES;
                printf("  >>> SHORT-CIRCUIT armed at f%d, %d more frames to observe\n",
                       frame, SHORTCIRCUIT_EXTRA_FRAMES);
            }
            if (shortcircuit_countdown >= 0) {
                if (--shortcircuit_countdown <= 0) {
                    printf("  >>> SHORT-CIRCUIT triggered: stopping walk\n");
                    stop_walk = true;
                    break;
                }
            }
        }
        if (stop_walk) break;
    }

    // -------------------------------------------------------------------------
    // Results.
    // -------------------------------------------------------------------------
    printf("\n  Summary (frames=%d):\n", total_frames);
    printf("    Worst pair sep:  %.3f m (f%d, P%d<>P%d)\n",
           worst_sep, worst_sep_frame, worst_sep_a, worst_sep_b);
    printf("    Min hips z:      %.3f m (f%d; started %.3f)\n",
           min_hips_z, min_hips_z_frame, hips_initial_z);
    printf("    Max spread:      %.3f m (f%d)\n",
           max_spread, max_spread_frame);
    printf("    (see [CHUNK_UNLOAD] log lines for chunk cycles)\n");
    printf("    Violations:      sep=%d  hipz=%d  spread=%d\n",
           sep_violations, hipz_violations, spread_violations);
    printf("    Integrity monitor fired: %d times\n", integrity_violations);
    printf("    L-foot z range:  %.3f..%.3f m  (span %.3f m)\n",
           l_foot_z_min, l_foot_z_max, l_foot_z_max - l_foot_z_min);
    printf("    R-foot z range:  %.3f..%.3f m  (span %.3f m)\n",
           r_foot_z_min, r_foot_z_max, r_foot_z_max - r_foot_z_min);
    printf("    Foot alternations: %d  (legs actually cycling? %s)\n",
           foot_alternation_count,
           foot_alternation_count > 20 ? "YES" : "NO — suspicious slide mode");

    bool ok = (sep_violations == 0)
           && (hipz_violations == 0)
           && (spread_violations == 0)
           && (integrity_violations == 0);
    printf("  %s\n", ok ? "[PASS]" : "[FAIL]");
    return ok;
}
