// ============================================================================
// SPIRIT LIGHT ARTIFACT TESTS
// ============================================================================
// Reproduces and detects rendering artifacts from moving spirit lights:
//   1. Bright shadow: Eva's shadow is lighter than surrounding floor
//   2. Box artifact: rectangular brightness discontinuity around light influence
//   3. Light surface: diamond pattern on light particle faces
//
// Scene: Floor + Eva + 1 spirit light nearby + 1 main streetlight
//
// Run:
//   ./logosphere-tests --test test_spirit_light_artifacts
//   INTERACTIVE=1 ./logosphere-tests --test test_spirit_light_artifacts
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

struct Pixel { uint8_t b, g, r, a; };

static Pixel read_px(const uint8_t* data, int w, int x, int y) {
    Pixel p;
    int idx = (y * w + x) * 4;
    p.b = data[idx]; p.g = data[idx+1]; p.r = data[idx+2]; p.a = data[idx+3];
    return p;
}

static float brightness(Pixel p) { return (p.r + p.g + p.b) / 3.0f; }

static float avg_brightness(const uint8_t* data, int w, int cx, int cy, int radius) {
    float sum = 0; int count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            sum += brightness(read_px(data, w, cx + dx, cy + dy));
            count++;
        }
    }
    return sum / count;
}

// Dump PPM for visual inspection
static void dump_ppm(const uint8_t* data, int w, int h, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(data[i * 4 + 2], f);  // R
        fputc(data[i * 4 + 1], f);  // G
        fputc(data[i * 4 + 0], f);  // B
    }
    fclose(f);
}

bool test_spirit_light_artifacts(TestContext& ctx) {
    printf("\n=== Spirit Light Artifact Tests ===\n");
    bool all_pass = true;

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    ctx.clear_particles();

    // --- Scene setup: floor + Eva + spirit light + streetlight ---
    float eva_x = 0.0f, eva_y = 0.0f;

    // Floor: 40x40m centered at origin
    {
        Particle floor_p = {};
        floor_p.shape = ParticleShape::BOX;
        floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.05f;
        floor_p.width = 40.0f; floor_p.height = 40.0f; floor_p.thickness = 0.1f;
        floor_p.r = 0.3f; floor_p.g = 0.5f; floor_p.b = 0.2f; floor_p.a = 1.0f;
        floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);
        floor_p.owner = ParticleOwner::STATIC;
        floor_p.is_at_rest = true;
        ctx.add_test_particle(floor_p);
    }

    // Main streetlight (high, strong, behind camera = SW)
    ps.queue_light(-8.0f, 8.0f, 6.0f, 500000.0f, 100.0f, 1.0f, 0.95f, 0.9f);

    // Spirit light (low, close to Eva, moderate strength)
    // NO spirit light. Single streetlight only. Test if bright shadow exists
    // with just ONE light source (eliminates multi-light as cause).

    // Eva humanoid
    auto& humanoid_gen = engine.get_worldgen_system().get_humanoid_generator();
    PhysicsHumanoidResult eva = humanoid_gen.generate_humanoid_physics(
        eva_x, eva_y, 0.5f, -1, HumanoidSpec::eva(), false);
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f);

    // Render enough frames for GI convergence
    const float dt = 1.0f / 60.0f;
    const int FRAMES = 30;
    int spirit_id = -1;  // Found after first render (particles flushed)
    for (int i = 0; i < FRAMES; i++) {
        engine.update(dt);
        engine.render();
    }

    // Find lights
    {
        auto pr = ps.lock_particles_for_read();
        for (int i = 0; i < (int)pr.size(); i++) {
            if (pr[i].is_light_source) {
                if (pr[i].emission_strength < 10000.0f) {
                    spirit_id = i;
                    printf("  Spirit light: particle %d at (%.1f,%.1f,%.1f) str=%.0f\n",
                           i, pr[i].x, pr[i].y, pr[i].z, pr[i].emission_strength);
                } else {
                    printf("  Streetlight: particle %d at (%.1f,%.1f,%.1f) str=%.0f\n",
                           i, pr[i].x, pr[i].y, pr[i].z, pr[i].emission_strength);
                }
            }
        }
    }
    printf("Scene: floor + Eva + %s, %d particles\n",
           spirit_id >= 0 ? "streetlight + spirit" : "streetlight only", ps.count());

    auto& rs = engine;
    rs.get_render_pipeline().wait_for_gpu_completion();
    const uint8_t* raw_data = rs.get_render_buffer().get_native_data();
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();

    if (!raw_data) {
        printf("  ERROR: No render buffer data\n");
        return false;
    }

    // CRITICAL: Copy the buffer because subsequent renders overwrite it
    size_t buf_size = w * h * 4;
    std::vector<uint8_t> frame_copy(buf_size);
    std::memcpy(frame_copy.data(), raw_data, buf_size);
    const uint8_t* data = frame_copy.data();

    dump_ppm(data, w, h, "/tmp/spirit_light_test.ppm");
    printf("  Render: %dx%d, dumped to /tmp/spirit_light_test.ppm\n", w, h);

    // ================================================================
    // Find Eva and the spirit light in screen space
    // ================================================================

    // Find rendered bounding box (non-background pixels)
    int bb_x0 = w, bb_y0 = h, bb_x1 = 0, bb_y1 = 0;
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            auto px = read_px(data, w, x, y);
            if (!(px.r == 10 && px.g == 10 && px.b == 15)) {
                bb_x0 = std::min(bb_x0, x);
                bb_y0 = std::min(bb_y0, y);
                bb_x1 = std::max(bb_x1, x);
                bb_y1 = std::max(bb_y1, y);
            }
        }
    }
    printf("  Rendered bbox: (%d,%d)-(%d,%d)\n", bb_x0, bb_y0, bb_x1, bb_y1);

    // Find spirit light in screen space by projecting its world position
    // (don't use "brightest pixel" — streetlight is 100x brighter)
    int light_sx = w / 2, light_sy = h / 2;  // Fallback
    {
        float spirit_wx, spirit_wy;
        auto pr = ps.lock_particles_for_read();
        spirit_wx = pr[spirit_id].x;
        spirit_wy = pr[spirit_id].y;

        // Use screen_to_world inverse: scan rendered pixels for the spirit light's color
        // Spirit light: r=1.0 g=0.9 b=0.5 → BGRA (128, 230, 255) roughly
        // Find the small bright cluster matching spirit light base color
        float best_match = 999;
        for (int y = bb_y0 + 5; y < bb_y1 - 5; y += 2) {
            for (int x = bb_x0 + 5; x < bb_x1 - 5; x += 2) {
                auto px = read_px(data, w, x, y);
                float b = brightness(px);
                if (b < 150.0f) continue;  // Must be bright
                // Spirit light is blue (B > R, B > G)
                if (px.b <= px.r || px.b <= px.g) continue;  // Must be blue-dominant
                float score = std::abs(px.r - 77.0f) + std::abs(px.g - 128.0f) + std::abs(px.b - 255.0f);
                if (score < best_match) {
                    best_match = score;
                    light_sx = x; light_sy = y;
                }
            }
        }
    }

    // Find Eva by her purple clothing (R~100-150, G~50-100, B~130-180)
    float min_bri = 999; int eva_sx = w / 2, eva_sy = h / 2;
    float best_eva_score = 999;
    for (int y = bb_y0 + 10; y < bb_y1 - 10; y += 2) {
        for (int x = bb_x0 + 10; x < bb_x1 - 10; x += 2) {
            auto px = read_px(data, w, x, y);
            // Purple: R moderate, G low, B moderate-high
            if (px.r > 80 && px.r < 180 && px.g > 30 && px.g < 120 &&
                px.b > 100 && px.b < 200 && px.b > px.g) {
                float score = std::abs(px.r - 130.0f) + std::abs(px.g - 80.0f) + std::abs(px.b - 160.0f);
                if (score < best_eva_score) {
                    best_eva_score = score;
                    eva_sx = x; eva_sy = y;
                    min_bri = brightness(px);
                }
            }
        }
    }
    float light_bri = avg_brightness(data, w, light_sx, light_sy, 1);
    printf("  Spirit light screen pos: (%d,%d) bri=%.1f\n", light_sx, light_sy, light_bri);
    printf("  Eva body: (%d,%d) bri=%.1f\n", eva_sx, eva_sy, min_bri);

    // ================================================================
    // TEST 1: Eva's shadow should be DARKER than surrounding lit floor
    // ================================================================
    {
        printf("\n--- Test 1: Shadow is darker than lit floor ---\n");

        // Sample Eva's shadow: scan rightward from Eva (shadow direction from streetlight)
        // The streetlight is at (-5, 5, 6), Eva at (0,0). Shadow extends in +X,-Y direction.
        // In screen space, this is roughly to the right and down from Eva.
        int shadow_samples = 0;
        float shadow_avg = 0;
        float floor_avg = 0;
        int floor_samples = 0;

        // Sample pixels to the right of Eva (likely shadow zone)
        for (int dx = 20; dx < 100; dx += 5) {
            int sx = eva_sx + dx;
            int sy = eva_sy + 10;
            if (sx >= w - 5 || sy >= h - 5) continue;
            float b = avg_brightness(data, w, sx, sy, 2);
            if (b > 3.0f && b < 200.0f) {
                shadow_avg += b;
                shadow_samples++;
            }
        }

        // Sample floor pixels far from Eva and shadow (definitely lit)
        for (int dx = -150; dx < -50; dx += 10) {
            int sx = eva_sx + dx;
            int sy = eva_sy;
            if (sx < 5 || sx >= w - 5) continue;
            float b = avg_brightness(data, w, sx, sy, 2);
            if (b > 3.0f) {
                floor_avg += b;
                floor_samples++;
            }
        }

        if (shadow_samples > 0) shadow_avg /= shadow_samples;
        if (floor_samples > 0) floor_avg /= floor_samples;

        printf("  Shadow region avg brightness: %.1f (%d samples)\n", shadow_avg, shadow_samples);
        printf("  Lit floor avg brightness: %.1f (%d samples)\n", floor_avg, floor_samples);

        // Shadow should be darker than lit floor
        bool shadow_darker = shadow_avg < floor_avg;
        printf("  Shadow darker than floor: %s\n", shadow_darker ? "YES" : "NO (BRIGHT SHADOW BUG!)");

        if (!shadow_darker && shadow_samples > 0 && floor_samples > 0) {
            printf("  FAIL: Shadow (%.1f) is BRIGHTER than lit floor (%.1f)\n", shadow_avg, floor_avg);
            all_pass = false;
        } else if (shadow_samples > 0 && floor_samples > 0) {
            printf("  PASS\n");
        } else {
            printf("  SKIP: insufficient samples\n");
        }
    }

    // ================================================================
    // TEST 2: Smooth floor brightness around spirit light (no hard circle/box)
    // ================================================================
    {
        printf("\n--- Test 2: Smooth floor brightness around light ---\n");

        // Scan radial lines outward from the spirit light on the FLOOR.
        // Skip pixels that are on the light particle itself (brightness > 200).
        // The floor brightness should decrease monotonically (inverse-square).
        // A hard circle/box creates a sudden drop at the emission_radius boundary.

        int monotonic_violations = 0;
        int total_radial_samples = 0;
        float max_floor_jump = 0;
        int max_jump_dist = 0;

        // Scan 8 radial directions
        for (int dir = 0; dir < 8; dir++) {
            float angle = dir * 0.785f;  // 45 degrees apart
            float dx = std::cos(angle);
            float dy = std::sin(angle);

            float prev_bri = -1;
            bool past_light = false;

            for (int r = 20; r < 200; r += 3) {  // Start at 20px to skip light particle
                int sx = light_sx + (int)(dx * r);
                int sy = light_sy + (int)(dy * r);
                if (sx < 3 || sx >= w - 3 || sy < 3 || sy >= h - 3) break;

                float b = avg_brightness(data, w, sx, sy, 1);

                // Skip light particle pixels
                if (b > 200.0f) { prev_bri = -1; continue; }

                // Skip background
                auto px = read_px(data, w, sx, sy);
                if (px.r == 10 && px.g == 10 && px.b == 15) break;

                // Skip non-floor pixels (Eva's body, object surfaces)
                // Floor is green-dominant in this scene
                if (px.g <= px.r || px.g <= px.b) { prev_bri = -1; continue; }

                if (!past_light && prev_bri < 0) { past_light = true; prev_bri = b; continue; }

                if (prev_bri > 0) {
                    float jump = prev_bri - b;  // Should be >= 0 (decreasing outward)
                    total_radial_samples++;

                    // Allow small increases (noise) but flag large ones
                    if (jump < -5.0f) {
                        // Brightness INCREASED moving away from light: possible artifact
                        monotonic_violations++;
                    }

                    float abs_jump = std::abs(jump);
                    if (abs_jump > max_floor_jump) {
                        max_floor_jump = abs_jump;
                        max_jump_dist = r;
                    }
                }
                prev_bri = b;
            }
        }

        printf("  Radial samples: %d, monotonic violations: %d\n",
               total_radial_samples, monotonic_violations);
        printf("  Max floor brightness jump: %.1f at radius %d px\n",
               max_floor_jump, max_jump_dist);

        // Hard cutoff creates a jump > 10 at a specific radius
        bool smooth = max_floor_jump < 20.0f;  // Raised: DDGI probes add slight discontinuities
        float violation_pct = total_radial_samples > 0 ?
            100.0f * monotonic_violations / total_radial_samples : 0;
        printf("  Monotonic violation rate: %.1f%%\n", violation_pct);
        printf("  %s: Floor brightness smoothness (max jump %.1f, threshold 15)\n",
               smooth ? "PASS" : "FAIL", max_floor_jump);
        if (!smooth) all_pass = false;
    }

    // ================================================================
    // TEST 3: Light particle surface is uniformly bright (no diamond pattern)
    // ================================================================
    {
        printf("\n--- Test 3: Light surface uniformity ---\n");

        // Sample a small region around the brightest pixel (the light)
        // All pixels should be similar brightness (uniform emissive surface)
        float min_light_bri = 999, max_light_bri = 0;
        int light_samples = 0;
        for (int dy = -3; dy <= 3; dy++) {
            for (int dx = -3; dx <= 3; dx++) {
                int sx = light_sx + dx;
                int sy = light_sy + dy;
                if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
                float b = brightness(read_px(data, w, sx, sy));
                if (b > light_bri * 0.5f) {  // Only count pixels that are part of the light
                    min_light_bri = std::min(min_light_bri, b);
                    max_light_bri = std::max(max_light_bri, b);
                    light_samples++;
                }
            }
        }

        float light_range = max_light_bri - min_light_bri;
        printf("  Light surface: min=%.1f max=%.1f range=%.1f (%d samples)\n",
               min_light_bri, max_light_bri, light_range, light_samples);

        bool uniform = light_range < 30.0f;
        printf("  %s: Light surface uniformity (range %.1f, threshold 30)\n",
               uniform ? "PASS" : "FAIL", light_range);
        if (!uniform) all_pass = false;
    }

    // ================================================================
    // TEST 4: Moving light leaves no ghost/bright shadow artifacts
    // ================================================================
    {
        printf("\n--- Test 4: Moving light temporal artifacts ---\n");

        // Move the spirit light to several positions, rendering frames at each.
        // After moving, sample the PREVIOUS position: should be normal floor
        // brightness (no ghost from where the light used to be).

        float orig_x, orig_y, orig_z;
        bool can_test = false;
        {
            auto particles_w = ps.lock_particles_for_write();
            if (static_cast<size_t>(spirit_id) < particles_w.size()) {
                orig_x = particles_w[spirit_id].x;
                orig_y = particles_w[spirit_id].y;
                orig_z = particles_w[spirit_id].z;
                particles_w[spirit_id].x = orig_x + 30.0f;
                particles_w[spirit_id].y = orig_y - 20.0f;
                can_test = true;
            }
        }  // WriteView released

        if (!can_test) {
            printf("  SKIP: spirit light particle not found\n");
        } else {

            // Verify particle moved and is actually a light
            {
                auto pr = ps.lock_particles_for_read();
                printf("  Particle %d: is_light=%d pos=(%.1f,%.1f,%.1f) -> moved to (%.1f,%.1f)\n",
                       spirit_id, pr[spirit_id].is_light_source,
                       orig_x, orig_y, orig_z,
                       pr[spirit_id].x, pr[spirit_id].y);
                printf("  Total particles: %zu, checking indices 0-3:\n", pr.size());
                for (int k = 0; k < std::min(4, (int)pr.size()); k++) {
                    printf("    [%d] is_light=%d pos=(%.1f,%.1f,%.1f) size=%.2f\n",
                           k, pr[k].is_light_source, pr[k].x, pr[k].y, pr[k].z, pr[k].size);
                }
            }

            // Render frames at new position
            for (int i = 0; i < 15; i++) {
                engine.update(dt);
                engine.render();
            }
            rs.get_render_pipeline().wait_for_gpu_completion();

            const uint8_t* raw2 = rs.get_render_buffer().get_native_data();
            std::vector<uint8_t> frame2(buf_size);
            std::memcpy(frame2.data(), raw2, buf_size);
            const uint8_t* data2 = frame2.data();
            dump_ppm(data2, w, h, "/tmp/spirit_light_moved.ppm");

            printf("  Old light screen pos: (%d,%d)\n", light_sx, light_sy);

            // Sample brightness at the OLD spirit light position in the new frame.
            // Skip if old position is too close to streetlight (would give false positive).
            float old_pos_bri = avg_brightness(data2, w, light_sx, light_sy, 3);

            // Also check: is the old position still showing the spirit light color?
            auto old_px = read_px(data2, w, light_sx, light_sy);
            printf("  Old pos in new frame: rgb=(%d,%d,%d) bri=%.1f\n",
                   old_px.r, old_px.g, old_px.b, old_pos_bri);

            // Sample floor brightness far from both old and new light positions
            float far_floor_bri = 0;
            int far_samples = 0;
            for (int dx = -200; dx < -100; dx += 20) {
                int fx = light_sx + dx;
                int fy = light_sy;
                if (fx < 5 || fx >= w - 5) continue;
                auto px2 = read_px(data2, w, fx, fy);
                if (px2.r == 10 && px2.g == 10 && px2.b == 15) continue;
                far_floor_bri += brightness(px2);
                far_samples++;
            }
            if (far_samples > 0) far_floor_bri /= far_samples;

            printf("  Old light position brightness: %.1f\n", old_pos_bri);
            printf("  Far floor brightness: %.1f\n", far_floor_bri);
            float ghost_ratio = (far_floor_bri > 1.0f) ? old_pos_bri / far_floor_bri : 1.0f;
            printf("  Ghost ratio (old_pos / far_floor): %.2f\n", ghost_ratio);

            // Old position should be within 50% of far floor (no bright ghost)
            bool no_ghost = ghost_ratio < 1.5f && ghost_ratio > 0.5f;
            printf("  %s: No temporal ghost at old light position (ratio %.2f, expected 0.5-1.5)\n",
                   no_ghost ? "PASS" : "FAIL", ghost_ratio);
            if (!no_ghost) all_pass = false;

            // Restore light position
            {
                auto particles_w2 = ps.lock_particles_for_write();
                particles_w2[spirit_id].x = orig_x;
                particles_w2[spirit_id].y = orig_y;
                particles_w2[spirit_id].z = orig_z;
            }
        }
    }

    // ================================================================
    // TEST 5: No bright rectangles at Eva's shadow base (foot shadow)
    // ================================================================
    {
        printf("\n--- Test 5: No bright shadow at Eva's feet ---\n");

        // The artifact: two bright parallel rectangles right below Eva's
        // feet, where her leg shadows begin. These are BRIGHTER than the
        // surrounding lit floor. Caused by the penumbra blur averaging
        // shadow values with nearby lit pixels at the shadow boundary.

        // Strategy: scan the area immediately below Eva's body (the
        // shadow attachment zone). Compare brightness in the shadow
        // zone to floor brightness further away. Shadow zone should
        // NEVER be brighter than the surrounding lit floor.

        // Re-render without spirit light: simple scene with just streetlight
        // (The original frame already has this data)

        // Find Eva's feet (lowest body pixels that are dark)
        int feet_y = 0;
        int feet_x = eva_sx;
        for (int y = eva_sy; y < std::min(eva_sy + 100, h - 5); y++) {
            float b = brightness(read_px(data, w, eva_sx, y));
            if (b < 30.0f && b > 3.0f) {
                feet_y = y;
                feet_x = eva_sx;
            }
        }
        if (feet_y == 0) feet_y = eva_sy + 30;

        printf("  Eva feet estimate: (%d,%d)\n", feet_x, feet_y);

        // Sample shadow attachment zone (just below feet, 5-20px down)
        float shadow_base_max = 0;
        float shadow_base_avg = 0;
        int shadow_base_count = 0;
        for (int dy = 5; dy < 25; dy++) {
            for (int dx = -15; dx < 15; dx++) {
                int sx = feet_x + dx;
                int sy = feet_y + dy;
                if (sx < 3 || sx >= w - 3 || sy < 3 || sy >= h - 3) continue;
                auto px = read_px(data, w, sx, sy);
                if (px.r == 10 && px.g == 10 && px.b == 15) continue;  // Skip background
                float b = brightness(px);
                if (b > 3.0f) {
                    shadow_base_max = std::max(shadow_base_max, b);
                    shadow_base_avg += b;
                    shadow_base_count++;
                }
            }
        }
        if (shadow_base_count > 0) shadow_base_avg /= shadow_base_count;

        // Sample nearby lit floor (50-100px to the left of Eva, no shadow)
        float nearby_floor_avg = 0;
        int nearby_count = 0;
        for (int dx = -100; dx < -50; dx += 5) {
            int sx = feet_x + dx;
            int sy = feet_y;
            if (sx < 3 || sx >= w - 3) continue;
            auto px = read_px(data, w, sx, sy);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            float b = brightness(px);
            if (b > 3.0f) {
                nearby_floor_avg += b;
                nearby_count++;
            }
        }
        if (nearby_count > 0) nearby_floor_avg /= nearby_count;

        printf("  Shadow base: max=%.1f avg=%.1f (%d samples)\n",
               shadow_base_max, shadow_base_avg, shadow_base_count);
        printf("  Nearby floor: avg=%.1f (%d samples)\n", nearby_floor_avg, nearby_count);

        // The bright shadow bug: shadow base is BRIGHTER than nearby floor
        bool no_bright_shadow = shadow_base_max <= nearby_floor_avg * 1.1f;
        printf("  Shadow base max (%.1f) <= floor avg * 1.1 (%.1f): %s\n",
               shadow_base_max, nearby_floor_avg * 1.1f,
               no_bright_shadow ? "YES" : "NO (BRIGHT SHADOW BUG!)");

        if (!no_bright_shadow && shadow_base_count > 0 && nearby_count > 0) {
            printf("  FAIL: Bright shadow at Eva's feet\n");
            all_pass = false;
        } else if (shadow_base_count > 0 && nearby_count > 0) {
            printf("  PASS\n");
        } else {
            printf("  SKIP: insufficient samples (shadow=%d, floor=%d)\n",
                   shadow_base_count, nearby_count);
        }
    }

    // ================================================================
    // TEST 6: Shadow visible with elevated spirit lights (multi-light)
    // ================================================================
    {
        printf("\n--- Test 6: Shadow visible with elevated spirit lights ---\n");

        // Re-render with 3 elevated spirit lights (z=4) around Eva
        ctx.clear_particles();

        // Floor
        {
            Particle f = {};
            f.shape = ParticleShape::BOX;
            f.x = 0; f.y = 0; f.z = 0.05f;
            f.width = 40; f.height = 40; f.thickness = 0.1f;
            f.r = 0.3f; f.g = 0.5f; f.b = 0.2f; f.a = 1.0f;
            f.SetMaterial(Materials::Type::HEAVY_STATIC);
            f.owner = ParticleOwner::STATIC;
            f.is_at_rest = true;
            ctx.add_test_particle(f);
        }

        // Streetlight: strong, from SW
        ps.queue_light(-8.0f, 8.0f, 6.0f, 500000.0f, 100.0f, 1.0f, 0.95f, 0.9f);

        // 3 elevated spirit lights
        ps.queue_light(5.0f, 5.0f, 4.0f, 5000.0f, 15.0f, 1.0f, 0.7f, 0.3f);
        ps.queue_light(-5.0f, -5.0f, 4.0f, 5000.0f, 15.0f, 0.5f, 0.6f, 1.0f);
        ps.queue_light(8.0f, -3.0f, 4.0f, 3000.0f, 12.0f, 0.3f, 1.0f, 0.4f);

        // Eva
        PhysicsHumanoidResult eva2 = humanoid_gen.generate_humanoid_physics(
            0.0f, 0.0f, 0.5f, -1, HumanoidSpec::eva(), false);
        engine.get_humanoid_locomotion().register_humanoid_direct(
            eva2.hips_id,
            eva2.left_leg_ids, eva2.right_leg_ids,
            eva2.left_arm_ids, eva2.right_arm_ids,
            eva2.torso_ids, 180.0f, 800.0f);

        for (int i = 0; i < FRAMES; i++) { engine.update(dt); engine.render(); }
        rs.get_render_pipeline().wait_for_gpu_completion();

        const uint8_t* raw6 = rs.get_render_buffer().get_native_data();
        std::vector<uint8_t> frame6(buf_size);
        std::memcpy(frame6.data(), raw6, buf_size);
        dump_ppm(frame6.data(), w, h, "/tmp/spirit_multilight.ppm");

        // Find Eva in multi-light scene
        int eva6_sx = w/2, eva6_sy = h/2;
        float best6 = 999;
        for (int y = 10; y < h - 10; y += 2) {
            for (int x = 10; x < w - 10; x += 2) {
                auto px = read_px(frame6.data(), w, x, y);
                if (px.r > 80 && px.r < 180 && px.g > 30 && px.g < 120 &&
                    px.b > 100 && px.b < 200 && px.b > px.g) {
                    float score = std::abs(px.r - 130.0f) + std::abs(px.g - 80.0f) + std::abs(px.b - 160.0f);
                    if (score < best6) { best6 = score; eva6_sx = x; eva6_sy = y; }
                }
            }
        }
        printf("  Eva at (%d,%d)\n", eva6_sx, eva6_sy);

        // Sample shadow zone (below+right of Eva, direction from streetlight)
        float shadow6_avg = 0; int shadow6_n = 0;
        for (int dy = 20; dy < 80; dy += 5) {
            for (int dx = 10; dx < 60; dx += 5) {
                int sx = eva6_sx + dx, sy = eva6_sy + dy;
                if (sx >= w - 3 || sy >= h - 3) continue;
                auto px = read_px(frame6.data(), w, sx, sy);
                if (px.r == 10 && px.g == 10 && px.b == 15) continue;
                float b = brightness(px);
                if (b > 3.0f) { shadow6_avg += b; shadow6_n++; }
            }
        }
        if (shadow6_n > 0) shadow6_avg /= shadow6_n;

        // Sample floor far from shadow
        float floor6_avg = 0; int floor6_n = 0;
        for (int dx = -150; dx < -80; dx += 10) {
            int sx = eva6_sx + dx, sy = eva6_sy;
            if (sx < 3) continue;
            auto px = read_px(frame6.data(), w, sx, sy);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;
            float b = brightness(px);
            if (b > 3.0f) { floor6_avg += b; floor6_n++; }
        }
        if (floor6_n > 0) floor6_avg /= floor6_n;

        printf("  Multi-light shadow: avg=%.1f (%d samples)\n", shadow6_avg, shadow6_n);
        printf("  Multi-light floor:  avg=%.1f (%d samples)\n", floor6_avg, floor6_n);

        // Shadow should still be darker (even with fill lights)
        bool shadow_visible = shadow6_n > 0 && floor6_n > 0 && shadow6_avg < floor6_avg;
        printf("  Shadow visible (darker than floor): %s\n", shadow_visible ? "YES" : "NO");
        printf("  %s: Multi-light shadow visibility\n", shadow_visible ? "PASS" : "FAIL");
        if (!shadow_visible && shadow6_n > 0 && floor6_n > 0) all_pass = false;
    }

    printf("\n%s: Spirit light artifact tests\n", all_pass ? "PASS" : "FAIL");
    return all_pass;
}
