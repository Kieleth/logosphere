// ============================================================================
// ROCK SHADOW VERTICAL ARTIFACT TEST
// ============================================================================
// Minimal scene: 1 floor + 1 rock + 1 light.
// Detects vertical bright lines at shadow boundaries caused by the
// penumbra blur clamp interacting with shadow edge geometry.
//
// Run:
//   ./logosphere-tests --test test_rock_shadow_artifact
// ============================================================================

#include "../src/test_context.h"
#include "../src/core/engine.h"
#include "../src/particle.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

struct RockPx { uint8_t b, g, r, a; };

static RockPx rpx(const uint8_t* d, int w, int x, int y) {
    RockPx p; int i=(y*w+x)*4;
    p.b=d[i]; p.g=d[i+1]; p.r=d[i+2]; p.a=d[i+3]; return p;
}
static float rbri(RockPx p) { return (p.r+p.g+p.b)/3.0f; }

static void rdump(const uint8_t* d, int w, int h, const char* path) {
    FILE* f=fopen(path,"wb"); if(!f)return;
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int i=0;i<w*h;i++){fputc(d[i*4+2],f);fputc(d[i*4+1],f);fputc(d[i*4+0],f);}
    fclose(f);
}

bool test_rock_shadow_artifact(TestContext& ctx) {
    printf("\n=== Rock Shadow Vertical Artifact Test ===\n");

    auto& engine = ctx.get_engine();
    auto& ps = engine.get_particle_system();
    auto& rs = engine;
    int w = rs.get_resolution_manager().get_render_width();
    int h = rs.get_resolution_manager().get_render_height();
    size_t buf_size = w * h * 4;
    const float dt = 1.0f / 60.0f;

    // ================================================================
    // Scene: floor + rock + streetlight. Nothing else.
    // ================================================================
    ctx.clear_particles();

    // Floor
    Particle floor_p = {};
    floor_p.shape = ParticleShape::BOX;
    floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.05f;
    floor_p.width = 40; floor_p.height = 40; floor_p.thickness = 0.1f;
    floor_p.r = 0.3f; floor_p.g = 0.5f; floor_p.b = 0.2f; floor_p.a = 1.0f;
    floor_p.SetMaterial(Materials::Type::HEAVY_STATIC);
    floor_p.owner = ParticleOwner::STATIC;
    floor_p.is_at_rest = true;
    ctx.add_test_particle(floor_p);

    // Multiple rocks at different positions (matching Eden layout)
    float rock_spots[][4] = {
        { 2.0f, -2.0f, 0.15f, 0.3f},
        {-1.0f,  3.0f, 0.15f, 0.25f},
        { 4.0f,  1.0f, 0.15f, 0.35f},
        {-3.0f, -1.0f, 0.15f, 0.2f},
        { 0.0f,  0.0f, 0.15f, 0.4f},  // Center
    };
    for (auto& rs : rock_spots) {
        Particle rock = {};
        rock.shape = ParticleShape::BOX;
        rock.x = rs[0]; rock.y = rs[1]; rock.z = rs[2];
        rock.width = rs[3]; rock.height = rs[3] * 0.7f; rock.thickness = rs[3] * 0.5f;
        rock.r = 0.5f; rock.g = 0.45f; rock.b = 0.4f; rock.a = 1.0f;
        rock.SetMaterial(Materials::Type::STONE);
        rock.owner = ParticleOwner::STATIC;
        rock.is_at_rest = true;
        ctx.add_test_particle(rock);
    }

    // Single light from SW, elevated
    ps.queue_light(-6.0f, 6.0f, 5.0f, 500000.0f, 100.0f, 1.0f, 0.95f, 0.9f);

    printf("  Scene: floor + rock at (0,0) + 1 streetlight\n");

    // Render
    for (int i = 0; i < 30; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> frame(buf_size);
    std::memcpy(frame.data(), rs.get_render_buffer().get_native_data(), buf_size);
    const uint8_t* data = frame.data();
    rdump(data, w, h, "/tmp/rock_shadow.ppm");
    printf("  Dumped /tmp/rock_shadow.ppm\n");

    // ================================================================
    // Find the rock in screen space
    // ================================================================
    int rock_sx = w/2, rock_sy = h/2;
    float best_rock = 999;
    for (int y = 10; y < h - 10; y += 2) {
        for (int x = 10; x < w - 10; x += 2) {
            auto px = rpx(data, w, x, y);
            // Rock is grey (R≈G≈B, brightness 60-120)
            float b = rbri(px);
            if (b > 50 && b < 130) {
                float grey_score = std::abs(px.r - px.g) + std::abs(px.g - px.b) + std::abs(px.r - px.b);
                if (grey_score < 30 && grey_score < best_rock) {
                    best_rock = grey_score;
                    rock_sx = x; rock_sy = y;
                }
            }
        }
    }
    printf("  Rock screen pos: (%d,%d) bri=%.1f\n", rock_sx, rock_sy, rbri(rpx(data, w, rock_sx, rock_sy)));

    // ================================================================
    // Scan shadow boundary for vertical artifacts
    // ================================================================
    // The shadow extends from the rock toward the NE (away from SW light).
    // Scan vertical columns in the shadow zone. A vertical artifact shows
    // as a column where brightness is significantly different from its
    // horizontal neighbors.

    int artifact_count = 0;
    int total_columns = 0;

    // Scan area: around the rock's shadow region
    printf("\n  Shadow boundary scan:\n");
    for (int dx = -50; dx < 150; dx += 2) {
        // For this column, compute brightness at 5 vertical positions
        float col_bri[5];
        int col_x = rock_sx + dx;
        int col_valid = 0;

        for (int i = 0; i < 5; i++) {
            int col_y = rock_sy + 10 + i * 5;
            if (col_x >= w - 3 || col_y >= h - 3) continue;
            auto px = rpx(data, w, col_x, col_y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue; // background
            col_bri[col_valid++] = rbri(px);
        }
        if (col_valid < 3) continue;

        // Check: is this column's average significantly different from neighbors?
        float col_avg = 0;
        for (int i = 0; i < col_valid; i++) col_avg += col_bri[i];
        col_avg /= col_valid;

        // Sample left and right neighbor columns
        float left_avg = 0, right_avg = 0;
        int ln = 0, rn = 0;
        for (int i = 0; i < 5; i++) {
            int cy = rock_sy + 10 + i * 5;
            if (cy >= h - 3) continue;

            int lx = col_x - 3;
            if (lx >= 3) {
                auto px = rpx(data, w, lx, cy);
                if (!(px.r == 10 && px.g == 10 && px.b == 15)) {
                    left_avg += rbri(px); ln++;
                }
            }
            int rx = col_x + 3;
            if (rx < w - 3) {
                auto px = rpx(data, w, rx, cy);
                if (!(px.r == 10 && px.g == 10 && px.b == 15)) {
                    right_avg += rbri(px); rn++;
                }
            }
        }
        if (ln > 0) left_avg /= ln;
        if (rn > 0) right_avg /= rn;

        total_columns++;

        // Artifact: column brightness differs from BOTH neighbors by > 5
        float neighbor_avg = (left_avg + right_avg) / 2.0f;
        float diff = col_avg - neighbor_avg;

        if (std::abs(diff) > 5.0f) {
            artifact_count++;
            if (artifact_count <= 3) {
                printf("    ARTIFACT at x=%d: col=%.1f neighbors=%.1f diff=%+.1f\n",
                       col_x, col_avg, neighbor_avg, diff);
            }
        }
    }

    float artifact_pct = total_columns > 0 ? 100.0f * artifact_count / total_columns : 0;
    printf("  Columns scanned: %d, artifacts: %d (%.1f%%)\n",
           total_columns, artifact_count, artifact_pct);

    // ================================================================
    // Method 2: Pixel-level vertical streak detection
    // ================================================================
    // A vertical artifact: a pixel is significantly brighter or darker
    // than BOTH its left and right neighbors, AND the pixel above/below
    // has the same anomaly (forming a vertical line).

    int streaks = 0;
    int floor_pixels_checked = 0;

    // Scan entire rendered area for vertical streaks
    for (int y = 10; y < h - 10; y++) {
        for (int x = 10; x < w - 10; x++) {
            if (x < 3 || x >= w - 3 || y < 3 || y >= h - 3) continue;
            auto px = rpx(data, w, x, y);
            if (px.r == 10 && px.g == 10 && px.b == 15) continue;

            float center = rbri(px);
            if (center > 150.0f) continue;  // Skip light particle pixels
            float left = rbri(rpx(data, w, x - 1, y));
            float right = rbri(rpx(data, w, x + 1, y));
            if (left > 150.0f || right > 150.0f) continue;
            float above = rbri(rpx(data, w, x, y - 1));
            float below = rbri(rpx(data, w, x, y + 1));

            // Skip background neighbors
            auto lpx = rpx(data, w, x-1, y);
            auto rpx2 = rpx(data, w, x+1, y);
            if (lpx.r == 10 && lpx.g == 10 && lpx.b == 15) continue;
            if (rpx2.r == 10 && rpx2.g == 10 && rpx2.b == 15) continue;

            floor_pixels_checked++;

            // Vertical streak: center differs from both left AND right by > 3
            float h_avg = (left + right) / 2.0f;
            float h_diff = center - h_avg;

            if (std::abs(h_diff) > 3.0f) {
                // Check if vertical neighbors also differ in the same direction
                float above_h_avg = (rbri(rpx(data, w, x-1, y-1)) + rbri(rpx(data, w, x+1, y-1))) / 2.0f;
                float below_h_avg = (rbri(rpx(data, w, x-1, y+1)) + rbri(rpx(data, w, x+1, y+1))) / 2.0f;
                float above_diff = above - above_h_avg;
                float below_diff = below - below_h_avg;

                // All three (above, center, below) deviate in same direction = vertical streak
                if ((h_diff > 0 && above_diff > 2.0f && below_diff > 2.0f) ||
                    (h_diff < 0 && above_diff < -2.0f && below_diff < -2.0f)) {
                    streaks++;
                    if (streaks <= 3) {
                        printf("    STREAK at (%d,%d): center=%.1f h_avg=%.1f diff=%+.1f\n",
                               x, y, center, h_avg, h_diff);
                    }
                }
            }
        }
    }

    float streak_pct = floor_pixels_checked > 0 ? 100.0f * streaks / floor_pixels_checked : 0;
    printf("  Method 2: %d floor pixels, %d vertical streaks (%.2f%%)\n",
           floor_pixels_checked, streaks, streak_pct);

    // ================================================================
    // Method 3: Dark halo around rock (lit side darkened by shadow blur)
    // ================================================================
    // The penumbra blur averages lit floor pixels near the rock with
    // the dark shadow behind the rock. Both are floor (same particle_id),
    // so the edge-preserving check doesn't help. The result: the lit
    // floor right next to the rock appears darker than floor further away.

    printf("\n  Method 3: Dark halo detection\n");

    // Render WITHOUT the rocks for comparison
    ctx.clear_particles();
    ctx.add_test_particle(floor_p);
    ps.queue_light(-6.0f, 6.0f, 5.0f, 500000.0f, 100.0f, 1.0f, 0.95f, 0.9f);
    for (int i = 0; i < 30; i++) { engine.update(dt); engine.render(); }
    rs.get_render_pipeline().wait_for_gpu_completion();

    std::vector<uint8_t> no_rocks(buf_size);
    std::memcpy(no_rocks.data(), rs.get_render_buffer().get_native_data(), buf_size);
    rdump(no_rocks.data(), w, h, "/tmp/rock_no_rocks.ppm");

    // Compare: pixels near the rock positions should not be darker WITH rocks
    // than WITHOUT (on the lit side of the rock)
    int halo_violations = 0;
    int halo_checked = 0;

    for (int y = 10; y < h - 10; y++) {
        for (int x = 10; x < w - 10; x++) {
            auto px_with = rpx(data, w, x, y);
            auto px_without = rpx(no_rocks.data(), w, x, y);

            // Skip background and rock surface pixels
            if (px_with.r == 10 && px_with.g == 10 && px_with.b == 15) continue;
            if (px_without.r == 10 && px_without.g == 10 && px_without.b == 15) continue;
            float bri_with = rbri(px_with);
            float bri_without = rbri(px_without);
            if (bri_with < 3 || bri_without < 3) continue;

            // Skip pixels that are clearly on the rock (not floor)
            if (std::abs(px_with.r - px_without.r) > 50) continue;

            halo_checked++;

            // Floor pixel darkened by > 3 units when rocks are present
            float darkening = bri_without - bri_with;
            if (darkening > 3.0f) {
                halo_violations++;
                if (halo_violations <= 3) {
                    printf("    HALO at (%d,%d): without=%.1f with=%.1f darkened=%.1f\n",
                           x, y, bri_without, bri_with, darkening);
                }
            }
        }
    }

    float halo_pct = halo_checked > 0 ? 100.0f * halo_violations / halo_checked : 0;
    printf("  Floor pixels checked: %d, darkened by halo: %d (%.2f%%)\n",
           halo_checked, halo_violations, halo_pct);

    // Shadows (darkening directly behind rock) are expected.
    // Halo (darkening on lit side near rock) is the bug.
    // Accept up to 1% of floor pixels being darkened (shadows are expected)
    bool no_halo = halo_pct < 1.0f;
    printf("  %s: Dark halo (%.2f%%, threshold 1%%)\n",
           no_halo ? "PASS" : "FAIL", halo_pct);

    bool pass = artifact_pct < 5.0f && streak_pct < 0.5f && no_halo;
    printf("\n%s: Rock shadow artifact (columns %.1f%%, streaks %.2f%%, halo %.2f%%)\n",
           pass ? "PASS" : "FAIL", artifact_pct, streak_pct, halo_pct);
    return pass;
}
