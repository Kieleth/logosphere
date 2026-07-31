// Capture the negative-depth-to-uint gbuffer bug in data.
//
// The orthographic-aware compute_depth for iso (and cabinet, bird's-eye)
// returns a SIGNED value — it's defined as monotone in the view
// direction but nothing anchors it to be positive. When the camera sits
// near the scene plane (bike_viewer pattern: cam_z = scene object_z),
// points on the "close" side of the scene produce negative depth.
//
// rasterize_gbuffer.metal:165 encodes depth into a uint32 atomic:
//     uint depth_uint = (uint)(depth * 1000.0f);
//
// A float-to-uint cast of a negative value in Metal/HLSL is implementation
// defined but in practice wraps or saturates near 0xFFFFFFFF. Either way
// the ORDERING between a negative-depth front face and a positive-depth
// back face is inverted: the back face's small positive uint wins the
// atomic_compare_exchange and the gbuffer ends up with a back-facing
// normal. Shading with that wrong normal gives n·L < 0 → black.
//
// This test asserts the contract the gbuffer depth encoding must satisfy:
// for any two points in the scene, the one physically closer (smaller
// compute_depth) must also have the smaller encoded uint. If that fails
// with signed depths and the current `(uint)(f * 1000.0f)` encoding,
// the test fails and the bug is proven in data.

#include "projection_system.h"
#include "logosphere/rendering/depth_encoding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

// Encoding the engine uses — pulled from the single source of truth
// (logosphere/rendering/depth_encoding.h). This file is the C++ mirror
// of src/rendering/gpu/depth_encoding.metal; both are required to stay
// in lockstep. Using it here means the test literally CANNOT assert a
// stale contract: if the shader encoding changes, the header must
// change, and the test picks up the new encoding immediately.
static int32_t encode_depth_for_gbuffer(float depth) {
    return logosphere::rendering::encode_depth(depth);
}

// Bike_viewer configuration: camera at (0, 0, 0.42) (sits at bike-body
// elevation). With this camera, points on the body's -X side produce
// NEGATIVE iso compute_depth values, points on +X side produce positive.
static constexpr float kCamX = 0.0f;
static constexpr float kCamY = 0.0f;
static constexpr float kCamZ = 0.42f;

void test_iso_depth_positive_for_bike_viewer_camera() {
    // Sanity check: with camera at scene level, iso compute_depth
    // returns negative values for the close side of a body centered at
    // the origin. This is the setup that triggers the bug.
    IsometricProjection iso;
    const float d_west = iso.compute_depth(-0.275f, 0.0f, 0.42f, kCamX, kCamY, kCamZ);
    const float d_east = iso.compute_depth( 0.275f, 0.0f, 0.42f, kCamX, kCamY, kCamZ);

    std::printf("    [setup] bike camera at (0, 0, 0.42):\n");
    std::printf("            body_left  (-0.275, 0, 0.42) compute_depth = %+8.4f\n", d_west);
    std::printf("            body_right (+0.275, 0, 0.42) compute_depth = %+8.4f\n", d_east);

    if (!(d_west < 0.0f)) {
        throw std::runtime_error("premise broken: body_left compute_depth should be negative "
                                  "with this camera, got " + std::to_string(d_west));
    }
    if (!(d_west < d_east)) {
        throw std::runtime_error("ordering premise broken: body_left should be closer than "
                                  "body_right (smaller depth)");
    }
}

// The heart of the bug. With negative float depth for the close face and
// positive for the far face, the uint encoding in rasterize_gbuffer.metal
// flips the ordering. The front-facing triangle that SHOULD win the
// depth test ends up with a larger uint than the back-facing triangle.
void test_uint_encoding_preserves_depth_ordering() {
    IsometricProjection iso;

    // A pair of world points at the same bike pixel column: front
    // (body_left, -X side) vs back (body_right, +X side).
    const float d_near = iso.compute_depth(-0.275f, 0.0f, 0.42f, kCamX, kCamY, kCamZ);
    const float d_far  = iso.compute_depth( 0.275f, 0.0f, 0.42f, kCamX, kCamY, kCamZ);

    const int32_t u_near = encode_depth_for_gbuffer(d_near);
    const int32_t u_far  = encode_depth_for_gbuffer(d_far);

    std::printf("    float depths:  near=%+8.4f  far=%+8.4f   (near < far ? %s)\n",
                d_near, d_far, (d_near < d_far) ? "YES" : "no");
    std::printf("    uint encoded:  near=%10u  far=%10u   (near < far ? %s)\n",
                u_near, u_far, (u_near < u_far) ? "YES" : "NO  ← BUG");

    // The contract the gbuffer depth test relies on.
    if (!(u_near < u_far)) {
        throw std::runtime_error(
            std::string("uint encoding inverts depth ordering for signed float inputs. ") +
            "Front-face uint=" + std::to_string(u_near) +
            " is NOT less than back-face uint=" + std::to_string(u_far) +
            ". The gbuffer atomic_compare_exchange will pick the back face, "
            "write its outward normal (pointing into the light's opposite direction), "
            "and Lambert clamp to 0 in lighting pass → pixel renders black. "
            "This is the root cause of the 'bike body dark under cardinal light' bug.");
    }
}

// Sweep every realistic bike body surface point to show the bug is
// systemic, not a single unlucky pair. For the bike's body at origin
// with the viewer camera at scene z, roughly half the ellipsoid's
// surface produces negative depth, which the uint encoding then
// scrambles relative to the positive-depth half.
void test_uint_encoding_inverts_many_pairs() {
    IsometricProjection iso;
    const float body_cx = 0.0f, body_cy = 0.0f, body_cz = 0.42f;
    const float half_w = 0.275f, half_h = 0.90f, half_d = 0.16f;

    int inversions = 0;
    int pairs = 0;
    struct Pair { float ax, ay, az, bx, by, bz; float da, db; int32_t ua, ub; };
    Pair worst{};

    // For every cardinal ±axis direction, pick a pair of diametrically
    // opposite surface points (front vs back of the ellipsoid along that
    // axis). If the encoding preserves ordering, the "near" one is the
    // one whose compute_depth is smaller and its uint must also be smaller.
    const float axes[3][3] = {
        {half_w, 0.0f, 0.0f},
        {0.0f, half_h, 0.0f},
        {0.0f, 0.0f, half_d},
    };
    for (const auto& ax : axes) {
        for (int sign_swap = 0; sign_swap < 2; ++sign_swap) {
            Pair p{};
            p.ax = body_cx + (sign_swap ? -ax[0] : +ax[0]);
            p.ay = body_cy + (sign_swap ? -ax[1] : +ax[1]);
            p.az = body_cz + (sign_swap ? -ax[2] : +ax[2]);
            p.bx = body_cx - (sign_swap ? -ax[0] : +ax[0]);
            p.by = body_cy - (sign_swap ? -ax[1] : +ax[1]);
            p.bz = body_cz - (sign_swap ? -ax[2] : +ax[2]);
            p.da = iso.compute_depth(p.ax, p.ay, p.az, kCamX, kCamY, kCamZ);
            p.db = iso.compute_depth(p.bx, p.by, p.bz, kCamX, kCamY, kCamZ);
            p.ua = encode_depth_for_gbuffer(p.da);
            p.ub = encode_depth_for_gbuffer(p.db);
            const bool float_order = p.da < p.db;
            const bool uint_order  = p.ua < p.ub;
            if (float_order != uint_order) {
                inversions++;
                worst = p;
            }
            pairs++;
        }
    }

    std::printf("    tested %d front/back axis pairs, %d had uint-vs-float ordering "
                "inversions.\n", pairs, inversions);
    if (inversions > 0) {
        std::printf("    worst case: A=(%+.3f,%+.3f,%+.3f) depth=%+8.4f uint=%10u\n",
                    worst.ax, worst.ay, worst.az, worst.da, worst.ua);
        std::printf("                 B=(%+.3f,%+.3f,%+.3f) depth=%+8.4f uint=%10u\n",
                    worst.bx, worst.by, worst.bz, worst.db, worst.ub);
        throw std::runtime_error(std::to_string(inversions) +
            " body-axis pairs have inverted depth ordering under the current "
            "gbuffer uint encoding. Every inversion manifests as a back-facing "
            "triangle winning the atomic depth test at a pixel that should "
            "show the body's front face — the bike body's cardinal-light "
            "darkness bug.");
    }
}

// When float-to-uint saturates negatives to 0 (Apple/clang behavior),
// multiple distinct negative depths all collapse to the SAME uint key.
// The gbuffer then can't order ANY two front-side pixels against each
// other — they all tie at 0 and the depth test's (<=) + epsilon
// fall-through picks whichever triangle writes last. Non-deterministic.
// Exact same symptom if Metal does the same thing, or worse (wrap).
void test_negative_depths_distinguishable_under_uint_encoding() {
    IsometricProjection iso;
    // Five points spread over the body's "close" half — all negative depth.
    const float samples[][3] = {
        {-0.275f,  0.00f,  0.42f},
        {-0.20f,   0.20f,  0.50f},
        {-0.10f,  -0.15f,  0.55f},
        {-0.25f,  -0.10f,  0.35f},
        {-0.05f,   0.05f,  0.58f},
    };
    std::vector<std::pair<float, int32_t>> rows;
    std::printf("    five distinct close-side points:\n");
    for (auto& s : samples) {
        float d = iso.compute_depth(s[0], s[1], s[2], kCamX, kCamY, kCamZ);
        int32_t u = encode_depth_for_gbuffer(d);
        std::printf("      (%+.3f,%+.3f,%+.3f)  depth=%+8.4f  uint=%10u\n",
                    s[0], s[1], s[2], d, u);
        rows.emplace_back(d, u);
    }
    // Count how many unique uint values we got.
    std::vector<int32_t> uniq;
    for (auto& r : rows) if (std::find(uniq.begin(), uniq.end(), r.second) == uniq.end())
        uniq.push_back(r.second);
    std::printf("    %zu unique float depths, %zu unique uint encodings.\n",
                rows.size(), uniq.size());
    if (uniq.size() < rows.size()) {
        throw std::runtime_error(
            "uint encoding collapses " + std::to_string(rows.size()) +
            " distinct negative depths into only " + std::to_string(uniq.size()) +
            " unique keys. Every float-to-uint cast of a negative value saturates "
            "to 0, so the gbuffer's atomic depth test cannot distinguish which "
            "front-side triangle is closest. Which one wins the pixel (and thus "
            "what normal the lighting pass sees) becomes non-deterministic based "
            "on triangle processing order. This is the bike body cardinal-light "
            "darkness bug: the surface that should 'win' sometimes loses to a "
            "back-face of the same body, whose normal Lambert-clamps to zero.");
    }
}

int main() {
    std::cout << "=== GBuffer depth uint encoding — contract vs signed compute_depth ===" << std::endl;
    TEST(iso_depth_positive_for_bike_viewer_camera);
    TEST(uint_encoding_preserves_depth_ordering);
    TEST(uint_encoding_inverts_many_pairs);
    TEST(negative_depths_distinguishable_under_uint_encoding);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
