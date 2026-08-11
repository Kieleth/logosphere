// Capture the "bike body cardinal face goes black under a cardinal light"
// bug in data, headlessly.
//
// The visual evidence (bike_viewer, west light at (-4.5, 0, 2.4), bike
// at origin yaw=0):
//   * CPU Lambert math + AABB occluder check says body_left
//     (world (-0.275, 0, 0.42), normal (-1, 0, 0)) should be bright cyan
//     — n·L = 0.905, no AABB on the ray path, tone-mapped to 255, base
//     color (0.15, 0.95, 1.00).
//   * The screen renders that pixel as black.
//
// The CPU Lambert math is simple enough not to be wrong. The AABB
// occluder test is coarse — a real shadow ray works at the TRIANGLE
// level (Möller-Trumbore), which is how the GPU shader does it. This
// test bridges that gap: it emits the SAME shadow triangles the render
// pipeline emits and does a per-triangle ray intersection from each
// body cardinal surface point to the light, mirroring
// shadow_rays_deferred.metal on CPU.
//
// Test output:
//   * per-cardinal-face: expected lambert, ray-vs-each-triangle result
//   * which triangle (if any) blocks the body_left → light ray
//   * PASS if a lit face is unblocked AND a shadowed face stays blocked
//
// If the test FAILS — a face with n·L > 0 and no legitimate blocker is
// reported as hit — we have the bug at CPU / triangle level, which is
// debuggable. If it PASSES, the CPU path is correct and the bug must
// be in the GPU rendering after the shadow test (rasterization, tone
// map, etc.), which would need GPU readback to localize further.
//
// Headless: links only logosphere_core. No GPU, no rendering, no Metal.

#include "particle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

// =============================================================================
// Bike scene, as bike_viewer assembles it. Mirrors the Logotron
// motorcycle (body ellipsoid + 2 wheel ellipsoids) at the origin,
// facing north (yaw=0). Pad at z=0, plus the four cardinal poles + one
// light. Light is at WEST cardinal, which is where the symptom shows.
// =============================================================================

static constexpr float kLightLumens  = 1500000.0f;
static constexpr float kLightOrbit   = 4.5f;
static constexpr float kPoleTopZ     = 1.80f;
static constexpr float kCardLightZ   = kPoleTopZ + 0.60f;

struct Pt3 { float x, y, z; };
struct Tri { Pt3 a, b, c; int particle_index; };

static std::vector<Particle> build_bike_scene() {
    std::vector<Particle> ps;

    // [0] pad — gray BOX, top at z=0
    {
        Particle p{}; p.shape = ParticleShape::BOX;
        p.x = 0; p.y = 0; p.z = 0.05f;  // sit ON the turtle
        p.width = 12.0f; p.height = 12.0f; p.thickness = 0.10f;
        p.r = 0.55f; p.g = 0.57f; p.b = 0.62f; p.a = 1.0f;
        ps.push_back(p);
    }
    // [1] body ellipsoid
    {
        Particle p{}; p.shape = ParticleShape::ELLIPSOID;
        p.x = 0; p.y = 0; p.z = 0.42f;            // wheel_r + body_h/2 = 0.26 + 0.16
        p.width = 0.55f; p.height = 1.80f; p.thickness = 0.32f;
        p.r = 0.15f; p.g = 0.95f; p.b = 1.00f; p.a = 1.0f;
        ps.push_back(p);
    }
    // [2] front wheel (at yaw=0, local +Y → world +Y)
    {
        Particle p{}; p.shape = ParticleShape::ELLIPSOID;
        p.x = 0; p.y = +0.70f; p.z = 0.26f;
        p.width = 0.14f; p.height = 0.52f; p.thickness = 0.52f;
        p.r = 0.06f; p.g = 0.14f; p.b = 0.18f; p.a = 1.0f;
        ps.push_back(p);
    }
    // [3] rear wheel
    {
        Particle p{}; p.shape = ParticleShape::ELLIPSOID;
        p.x = 0; p.y = -0.70f; p.z = 0.26f;
        p.width = 0.14f; p.height = 0.52f; p.thickness = 0.52f;
        p.r = 0.06f; p.g = 0.14f; p.b = 0.18f; p.a = 1.0f;
        ps.push_back(p);
    }
    // [4-7] four cardinal poles
    auto add_pole = [&](float x, float y, float r, float g, float b) {
        Particle p{}; p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = kPoleTopZ * 0.5f;
        p.width = 0.10f; p.height = 0.10f; p.thickness = kPoleTopZ;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        ps.push_back(p);
    };
    add_pole( kLightOrbit,  0.0f, 1.0f, 0.55f, 0.10f);          // [4] east (orange)
    add_pole( 0.0f,  kLightOrbit, 0.15f, 0.90f, 0.25f);         // [5] north (green)
    add_pole(-kLightOrbit,  0.0f, 0.25f, 0.55f, 1.00f);         // [6] west (blue)
    add_pole( 0.0f, -kLightOrbit, 1.00f, 0.95f, 0.20f);         // [7] south (yellow)
    // [8] light at west cardinal (emitter)
    {
        Particle p{}; p.shape = ParticleShape::BOX;
        p.x = -kLightOrbit; p.y = 0; p.z = kCardLightZ;
        p.width = 0.2f; p.height = 0.2f; p.thickness = 0.2f;
        p.r = 0.95f; p.g = 0.97f; p.b = 1.00f; p.a = 1.0f;
        p.is_light_source   = true;
        p.emission_strength = kLightLumens;
        p.emission_radius   = 40.0f;
        ps.push_back(p);
    }
    return ps;
}

// Emit shadow triangles the same way render_pipeline.cpp:249-345 does:
// skip particles flagged is_light_source, call Particle::GetShadowTriangles,
// attach the particle index so we can report who cast the shadow.
static std::vector<Tri> emit_shadow_triangles(const std::vector<Particle>& ps) {
    std::vector<Tri> out;
    std::vector<float> buf;
    for (size_t i = 0; i < ps.size(); ++i) {
        if (ps[i].is_light_source) continue;
        buf.clear();
        ps[i].GetShadowTriangles(buf);
        for (size_t v = 0; v + 9 <= buf.size(); v += 9) {
            Tri t{};
            t.a = {buf[v+0], buf[v+1], buf[v+2]};
            t.b = {buf[v+3], buf[v+4], buf[v+5]};
            t.c = {buf[v+6], buf[v+7], buf[v+8]};
            t.particle_index = static_cast<int>(i);
            out.push_back(t);
        }
    }
    return out;
}

// Möller-Trumbore, mirroring src/rendering/gpu/gpu_math.metal:28-83.
// Two-sided (|det| < eps parallel check). Returns true if ray hits
// triangle at some t in (kMinT, max_distance).
static constexpr float kMinT = 0.001f;
static constexpr float kEpsilon = 1e-7f;
static bool ray_hits_triangle(const Pt3& o, const Pt3& d, float max_distance,
                              const Pt3& v0, const Pt3& v1, const Pt3& v2) {
    const float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
    const float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;
    const float px = d.y * e2z - d.z * e2y;
    const float py = d.z * e2x - d.x * e2z;
    const float pz = d.x * e2y - d.y * e2x;
    const float det = e1x * px + e1y * py + e1z * pz;
    if (std::fabs(det) < kEpsilon) return false;
    const float inv = 1.0f / det;
    const float tvx = o.x - v0.x, tvy = o.y - v0.y, tvz = o.z - v0.z;
    const float u = (tvx * px + tvy * py + tvz * pz) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const float qx = tvy * e1z - tvz * e1y;
    const float qy = tvz * e1x - tvx * e1z;
    const float qz = tvx * e1y - tvy * e1x;
    const float v = (d.x * qx + d.y * qy + d.z * qz) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = (e2x * qx + e2y * qy + e2z * qz) * inv;
    return (t > kMinT && t < max_distance);
}

struct TraceResult {
    bool blocked = false;
    int  blocker_particle_index = -1;
    size_t triangles_tested = 0;
};

// Brute-force trace: Möller-Trumbore against every shadow triangle.
// Matches shadow_rays_deferred.metal's per-triangle intersection. The
// only thing this skips vs the shader is the BVH traversal, which only
// affects speed, not which triangles are eligible to block.
static TraceResult trace_shadow_brute(const std::vector<Tri>& tris,
                                       const Pt3& from, const Pt3& to) {
    TraceResult r{};
    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len < kMinT) return r;
    Pt3 d = { dx/len, dy/len, dz/len };
    for (const auto& t : tris) {
        r.triangles_tested++;
        if (ray_hits_triangle(from, d, len, t.a, t.b, t.c)) {
            r.blocked = true;
            r.blocker_particle_index = t.particle_index;
            return r;
        }
    }
    return r;
}

static constexpr float kShadowRayOffset = 0.05f;   // matches optimization_flags.metal
static constexpr float kFourPi          = 12.566370614359172f;

struct Sample {
    const char* name;
    Pt3 pos;
    Pt3 normal;
    bool should_be_lit;   // n·L > 0 with the west light
    bool should_be_blocked_by_scene_geometry;  // true if there's a legitimate blocker
};

// Six cardinal surface points on the body ellipsoid at (0, 0, 0.42)
// with extents ±0.275 (X) ±0.9 (Y) ±0.16 (Z). At bike yaw=0, local +Y
// = world +Y, so body's "forward cap" is north (+Y) and "back cap" is
// south (-Y). Left flank (−X) is west-facing — with a west light, this
// is the primary lit face and the one dark in the screenshot.
static const Sample kBodySamples[] = {
    {"body_fwd_cap ", {+0.000f, +0.900f, 0.420f}, { 0.0f, +1.0f, 0.0f}, false, false},
    {"body_back_cap", {+0.000f, -0.900f, 0.420f}, { 0.0f, -1.0f, 0.0f}, false, false},
    {"body_right   ", {+0.275f, +0.000f, 0.420f}, {+1.0f,  0.0f, 0.0f}, false, false},
    {"body_left    ", {-0.275f, +0.000f, 0.420f}, {-1.0f,  0.0f, 0.0f}, true,  false},
    {"body_top     ", {+0.000f, +0.000f, 0.580f}, { 0.0f,  0.0f, 1.0f}, true,  false},
    {"body_bottom  ", {+0.000f, +0.000f, 0.260f}, { 0.0f,  0.0f,-1.0f}, false, false},
};

void test_bike_body_facing_light_must_not_self_shadow() {
    auto ps = build_bike_scene();
    // Sanity: particle 8 is our light.
    if (ps.size() != 9 || !ps[8].is_light_source) {
        throw std::runtime_error("scene not set up as expected (particle 8 must be the light)");
    }
    const Pt3 light = { ps[8].x, ps[8].y, ps[8].z };
    auto tris = emit_shadow_triangles(ps);

    std::printf("    [scene] %zu particles, %zu shadow triangles\n",
                ps.size(), tris.size());
    std::printf("    [light] west cardinal @ (%.2f, %.2f, %.2f)\n",
                light.x, light.y, light.z);

    bool any_unexpected_block = false;
    std::string failure_message;

    for (const auto& s : kBodySamples) {
        // Lambert against the light direction.
        float dx = light.x - s.pos.x, dy = light.y - s.pos.y, dz = light.z - s.pos.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        float inv = 1.0f / dist;
        float lam = std::max(0.0f, s.normal.x*dx*inv + s.normal.y*dy*inv + s.normal.z*dz*inv);

        // Offset origin along the surface normal, same as the GPU shader.
        Pt3 origin = {
            s.pos.x + s.normal.x * kShadowRayOffset,
            s.pos.y + s.normal.y * kShadowRayOffset,
            s.pos.z + s.normal.z * kShadowRayOffset,
        };
        auto r = trace_shadow_brute(tris, origin, light);

        float intensity = kLightLumens / (kFourPi * std::max(dist*dist, 0.0001f));
        float lit_lux = intensity * lam;

        std::printf("    %s  n·L=%.3f  dist=%.2f  lit=%.0f lux  "
                    "ray blocked=%s",
                    s.name, lam, dist, lit_lux, r.blocked ? "YES" : "no ");
        if (r.blocked) {
            std::printf(" by p[%d]%s", r.blocker_particle_index,
                        (r.blocker_particle_index == 1) ? " (SELF — body)" :
                        (r.blocker_particle_index == 2 || r.blocker_particle_index == 3) ? " (wheel)" :
                        "");
        }
        std::printf("  (tested %zu triangles)\n", r.triangles_tested);

        // The contract: a face that should be lit AND isn't near any
        // legitimate blocker MUST have an unblocked shadow ray. If the
        // ray is blocked, record why so the test failure can pinpoint
        // the offending triangle.
        if (s.should_be_lit && !s.should_be_blocked_by_scene_geometry && r.blocked) {
            any_unexpected_block = true;
            failure_message += std::string(s.name) +
                " should be lit (n·L=" + std::to_string(lam) +
                ") but shadow ray hit p[" +
                std::to_string(r.blocker_particle_index) + "]. ";
        }
    }

    if (any_unexpected_block) {
        throw std::runtime_error("unexpected shadow block: " + failure_message);
    }
}

// A secondary contract: faces with n·L ≤ 0 (facing away from light)
// can either be blocked or not — either way the lighting model
// clamps them to 0. What must NOT happen: a face with HIGH Lambert
// (> 0.5) being blocked by one of the BIKE's own particles. If that
// happens we've located a self-shadow bug at the triangle level.
void test_no_self_shadow_between_body_and_own_wheels() {
    auto ps = build_bike_scene();
    const Pt3 light = { ps[8].x, ps[8].y, ps[8].z };
    auto tris = emit_shadow_triangles(ps);

    const auto& left = kBodySamples[3];  // body_left
    Pt3 origin = {
        left.pos.x + left.normal.x * kShadowRayOffset,
        left.pos.y + left.normal.y * kShadowRayOffset,
        left.pos.z + left.normal.z * kShadowRayOffset,
    };
    auto r = trace_shadow_brute(tris, origin, light);
    if (r.blocked) {
        throw std::runtime_error(
            std::string("body_left ray blocked by p[") +
            std::to_string(r.blocker_particle_index) +
            "] — that's either self-shadow on the body (p=1), a wheel "
            "occluding at a near-zero distance (p=2 or 3), or a pole "
            "that shouldn't be in the line of sight (p=6 is the west "
            "pole directly under the light; p=4/5/7 are the other poles). "
            "The GPU is producing the same verdict — screen pixel goes black.");
    }
}

int main() {
    std::cout << "=== Bike body shadow — per-triangle trace at cardinal faces ===" << std::endl;
    TEST(bike_body_facing_light_must_not_self_shadow);
    TEST(no_self_shadow_between_body_and_own_wheels);
    std::cout << std::endl << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
