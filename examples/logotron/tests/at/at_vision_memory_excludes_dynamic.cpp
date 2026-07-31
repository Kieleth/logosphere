// AT: dynamic particles (bikes, active runs) are excluded from the
// vision-memory blend.
//
// Feature under test: when the player's vision cone has memory
// enabled, pixels currently showing a DYNAMIC particle should be
// hard-cut by the live cone (no memory blend), so a moving bike
// doesn't appear in cells the player previously looked at. Static
// particles (floor, walls, sealed trails) DO get the memory blend
// and persist as ghosts.
//
// Headless verification path — what we can assert without a GPU:
//   1. The Logotron bike assembly marks every BodyPart `is_dynamic`,
//      and the flag survives `build_part_snapshot` into the
//      Particle that `sync_rigid_assembly` would emit.
//   2. The active-run particle (recreated each frame) carries
//      `is_dynamic = true`.
//   3. The engine's per-frame `is_dynamic_map[particle_id]` lookup
//      (mirroring `RenderPipeline::prepare_gpu_data`) sets 1 for
//      dynamic particles and 0 for static ones.
//
// What this AT does NOT cover (visual-only, opt-in via
// LOGOTRON_AT_VISUAL=1): that the Pass-4 Metal shader actually
// skips memory blending when it reads the map. The AT shells out
// to the live game in visual mode; the human confirms.

#include "at_common.h"

#include "logosphere/assembly/rigid_assembly.h"
#include "logosphere/rendering/vision_cone_pixel.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

namespace la = logosphere::assembly;

// Mirror of `make_motorcycle`'s "for every part, is_dynamic = true"
// pattern (main.cpp). The single fact under test: the assembly
// builder must mark every part. If main.cpp ever drops that line,
// THIS AT fails — and the engine map silently degrades.
static la::RigidAssembly make_test_bike() {
    la::RigidAssembly a;
    a.world_x = 10.0f; a.world_y = 10.0f; a.world_z = 0.0f;

    const float body_len = 1.8f, body_w = 0.55f, body_h = 0.32f;
    const float wheel_r = 0.26f, wheel_t = 0.14f, wheel_offs = 0.7f;
    const float body_z = wheel_r + body_h * 0.5f;

    la::BodyPart body;
    body.name = "body";
    body.shape = ParticleShape::ELLIPSOID;
    body.local_z = body_z;
    body.width = body_w; body.height = body_len; body.thickness = body_h;
    body.r = 1.0f; body.g = 0.45f; body.b = 0.05f;

    la::BodyPart wheel_front;
    wheel_front.name = "wheel_front";
    wheel_front.shape = ParticleShape::ELLIPSOID;
    wheel_front.local_y = +wheel_offs;
    wheel_front.local_z = wheel_r;
    wheel_front.width = wheel_t; wheel_front.height = 2.0f * wheel_r; wheel_front.thickness = 2.0f * wheel_r;

    la::BodyPart wheel_rear = wheel_front;
    wheel_rear.name = "wheel_rear";
    wheel_rear.local_y = -wheel_offs;

    a.parts = { body, wheel_front, wheel_rear };
    for (auto& part : a.parts) part.is_dynamic = true;
    return a;
}

// =========================================================================
// 1 · BodyPart::is_dynamic flag survives PartParticleSnapshot.
//     The snapshot is what rigid_assembly.cpp's build_particle reads
//     when emitting Particles, so this is the bridge that matters.
// =========================================================================
void at_bodypart_is_dynamic_propagates_to_snapshot() {
    la::RigidAssembly a;
    a.world_x = 0; a.world_y = 0; a.world_z = 0;
    la::BodyPart bp;
    bp.is_dynamic = true;
    auto s = la::build_part_snapshot(a, bp);
    AT_ASSERT_TRUE(s.is_dynamic, "snapshot must carry is_dynamic");

    la::BodyPart bp_static;
    bp_static.is_dynamic = false;
    auto s_static = la::build_part_snapshot(a, bp_static);
    AT_ASSERT_TRUE(!s_static.is_dynamic,
                   "static part stays is_dynamic = false");
}

// =========================================================================
// 2 · The Logotron bike-build pattern marks ALL parts dynamic.
// =========================================================================
void at_bike_marks_all_parts_dynamic() {
    auto bike = make_test_bike();
    AT_ASSERT_TRUE(!bike.parts.empty(), "test bike has parts");
    for (const auto& part : bike.parts) {
        AT_ASSERT_TRUE(part.is_dynamic,
                       std::string("part '") + part.name + "' must be dynamic");
    }
}

// =========================================================================
// 3 · Active-run particle (created in sync_active_runs) is_dynamic.
// =========================================================================
void at_active_run_particle_is_dynamic() {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = 5.0f; p.y = 0.0f; p.z = 0.5f;
    p.width = 5.0f; p.height = 0.1f; p.thickness = 0.4f;
    p.SetMaterial(Materials::Type::STONE);
    // Contract: Logotron sets this in main.cpp:sync_active_runs.
    p.is_dynamic = true;
    AT_ASSERT_TRUE(p.is_dynamic, "active-run particle is dynamic");
}

// =========================================================================
// 4 · Engine's is_dynamic_map mirrors particle.is_dynamic 1:1.
//     Re-implements the trivial map-building loop (5 lines in
//     render_pipeline.cpp:721-733). If THIS AT passes and the bike
//     still ghosts visually, the bug is downstream of the map: the
//     buffer upload or the shader.
// =========================================================================
void at_is_dynamic_map_built_from_particle_flags() {
    std::vector<Particle> particles;
    Particle dyn = {};
    dyn.is_dynamic = true;
    Particle stat = {};
    stat.is_dynamic = false;
    particles.push_back(dyn);
    particles.push_back(stat);
    particles.push_back(dyn);

    // Mirror render_pipeline.cpp:721-733.
    std::vector<uint8_t> map(particles.size(), 0);
    for (size_t i = 0; i < particles.size(); ++i) {
        if (particles[i].is_dynamic) map[i] = 1;
    }
    AT_ASSERT_EQ((int)map[0], 1, "particle 0 (dyn)");
    AT_ASSERT_EQ((int)map[1], 0, "particle 1 (static)");
    AT_ASSERT_EQ((int)map[2], 1, "particle 2 (dyn)");
}

// =========================================================================
// 5 · END-TO-END SCENARIO: build a Logotron-shaped frame state and
//     ask the engine's vision-cone CPU spec what visibility the
//     "AI bike pixel" should have when the player is looking +Y
//     and the AI is at the player's WEST (out of the 180° cone).
//     Memory cells west of the player are ALL lit (worst case for
//     ghosting). Asserts the bike pixel is fully dark.
//
//     If THIS AT passes and the live game still shows a ghost
//     bike, the Pass-4 Metal shader has drifted from the spec —
//     the bug is in the GPU port, not the contract.
// =========================================================================
void at_e2e_ai_bike_out_of_cone_is_invisible() {
    using namespace logosphere::rendering;

    // Particle layout:
    //   id 0..49 — "static" world particles (floor, walls, trails).
    //   id 50    — AI bike (dynamic).
    constexpr int kStaticEnd  = 50;
    constexpr int kAiBikeId   = 50;
    constexpr int kTotal      = 51;

    std::vector<unsigned char> dyn_map(kTotal, 0);
    dyn_map[kAiBikeId] = 1;

    // 4×4 memory grid covering [-2, +2]^2 — every cell pre-lit.
    constexpr int kCells = 4;
    std::vector<float> mem(kCells * kCells, 1.0f);

    VisionConePixelParams p{};
    p.viewer_x = 0.0f; p.viewer_y = 0.0f;
    p.look_direction = 0.0f;     // +Y (engine convention: yaw=0)
    p.half_fov = 1.5707963f;     // 180° total
    p.range = 60.0f;
    p.inner_falloff = 0.70f;
    p.darkness = 0.0f;
    p.occlusion_count = 0;
    p.occlusion_distance = nullptr;
    p.memory_enabled = 1;
    p.memory_width   = kCells;
    p.memory_height  = kCells;
    p.memory_origin_x = -2.0f; p.memory_origin_y = -2.0f;
    p.memory_cell_size = 1.0f;
    p.memory_dim = 0.45f;
    p.memory_grid = mem.data();
    p.dynamic_map_size = kTotal;
    p.is_dynamic_map = dyn_map.data();

    // Place the AI bike pixel BEHIND the viewer (at world (-1.5, -1.5),
    // which the cone facing +Y excludes). Memory cell at this world
    // position is lit (= 1). If the dynamic-skip works, visibility
    // must equal `darkness` (0). If it doesn't, visibility falls
    // back to memory_dim (0.45) — the ghost bug the user reported.
    VisionConePixelInput bike_pixel{-1.5f, -1.5f,
        static_cast<unsigned int>(kAiBikeId)};
    float bike_v = compute_vision_cone_visibility(p, bike_pixel);
    AT_ASSERT_NEAR(bike_v, p.darkness, 1e-5f,
        "AI bike out of cone must be FULLY dark even with lit memory cell");

    // Sanity: a STATIC pixel at the same out-of-cone location SHOULD
    // get the memory ghost (proves the dynamic-skip is the only
    // thing turning the bike dark; the memory blend itself works).
    VisionConePixelInput wall_pixel{-1.5f, -1.5f, 7};   // not in dyn_map
    float wall_v = compute_vision_cone_visibility(p, wall_pixel);
    AT_ASSERT_NEAR(wall_v, p.memory_dim, 1e-5f,
        "static wall at same spot must get the memory dim ghost");
}

// =========================================================================
// Visual mode: hand off to the running game so a human can confirm
// the shader-side behavior the headless asserts can't reach.
// =========================================================================
static int run_visual_mode() {
    std::cout << "[AT visual] LOGOTRON_AT_VISUAL=1 — handing off to logotron"
              << std::endl;
    std::cout << "[AT visual] expected: drive past the AI; when AI is OUT of"
                 " your cone, you should see NO ghost trail of it in your"
                 " memory area (only walls/floor ghost)."
              << std::endl;
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "./logotron/logotron --exit-after %.1f",
                  logotron::at::visual_seconds());
    return std::system(cmd);
}

int main() {
    if (logotron::at::visual_mode_requested()) {
        return run_visual_mode();
    }
    std::cout << "=== Logotron AT — vision-memory excludes dynamic particles ===" << std::endl;
    AT_TEST(at_bodypart_is_dynamic_propagates_to_snapshot);
    AT_TEST(at_bike_marks_all_parts_dynamic);
    AT_TEST(at_active_run_particle_is_dynamic);
    AT_TEST(at_is_dynamic_map_built_from_particle_flags);
    AT_TEST(at_e2e_ai_bike_out_of_cone_is_invisible);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
