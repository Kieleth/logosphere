// ellipsoid_lighting_test — non-uniform-scale sibling of sphere_lighting_test.
// Exact same scene, same lights, same camera; the ONLY difference is the test
// body is an ELLIPSOID with the bike-body aspect ratio (0.55 × 1.80 × 0.32)
// instead of a uniform SPHERE. If sphere_lighting_test renders correctly and
// this test shows the "lit rim, dark interior" symptom, the bug is isolated
// to the ellipsoid (non-uniform scale) rendering path — populate_icosphere's
// per-axis scale is the only code difference between the two.
//
// Controls:
//   SPACE    — advance the light through 5 positions:
//                0: overhead (+Z)
//                1: east     (+X, same z as sphere)
//                2: north    (+Y)
//                3: west     (-X)
//                4: south    (-Y)
//   SHIFT+P  — toggle continuous telemetry dump
//   P        — print one dump frame
//   ESC      — quit
//
// For each light position, telemetry prints the 6 cardinal surface points
// on the sphere (the poles and the 4 equatorial compass points) with:
//   - world position
//   - outward surface normal
//   - Lambert n·L for the current light direction
//   - expected raw lux and tone-mapped 0-255 brightness
//   - expected RGB after the cyan base color
//   - screen pixel the user should eyeball
//
// Contract: the ONE surface point facing directly toward the light has
// n·L = 1.0 and must be bright cyan on screen. Points 90° away from the
// light have n·L = 0 and must be black. Points in between must shade
// smoothly. If the screen shows only the one-at-normal-aligned pixel lit
// and the rest black, the GPU's lighting is collapsing the gradient to
// a step — the visible symptom we're after.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "application.h"
#include "core/engine.h"
#include "core/camera_system.h"
#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"
#include "logotron_ontology_registry.h"
#include "particle.h"

namespace {

constexpr float kPadHalf      = 4.0f;                    // smaller pad so it's not the whole frame
constexpr float kPadThickness = 0.10f;
constexpr float kPadCenterZ   = -kPadThickness * 0.5f;   // top at z = 0 exactly
// Bike-body aspect ratio: very flat along Z (thickness 0.32), long along Y
// (length 1.80), medium along X (width 0.55). This is the exact shape the
// Logotron motorcycle body uses.
constexpr float kEllipW       = 0.55f;                    // X extent (width)
constexpr float kEllipH       = 1.80f;                    // Y extent (length, "forward")
constexpr float kEllipT       = 0.32f;                    // Z extent (thickness, "up")
constexpr float kEllipHalfZ   = kEllipT * 0.5f;
constexpr float kSphereCenter = kEllipHalfZ;              // bottom touches pad top
constexpr float kPoleTopZ     = 1.20f;                    // top of cardinal poles
constexpr float kCardLightZ   = kPoleTopZ + 0.60f;        // cardinal light floats ABOVE its pole —
                                                           // otherwise the pole sits exactly between
                                                           // the light and the rest of the scene and
                                                           // occludes all light rays through itself.
constexpr float kLightLumens  = 1500000.0f;
constexpr float kLightOrbit   = 3.0f;                    // horizontal distance from sphere center
constexpr float kOverheadZ    = 4.00f;                    // overhead light height

struct LightPose { const char* label; float x, y, z; };
constexpr LightPose kLightPoses[] = {
    {"0: overhead (+Z)", 0.0f,  0.0f, kOverheadZ},
    {"1: east (+X)",     kLightOrbit, 0.0f, kCardLightZ},
    {"2: north (+Y)",    0.0f,  kLightOrbit, kCardLightZ},
    {"3: west (-X)",    -kLightOrbit, 0.0f, kCardLightZ},
    {"4: south (-Y)",    0.0f, -kLightOrbit, kCardLightZ},
};
constexpr int kNumLightPoses = sizeof(kLightPoses) / sizeof(kLightPoses[0]);

// Pole color per cardinal — helps visually identify which light position
// is active. Not used for lighting (poles aren't emissive).
struct PoleSpec { float x, y; float top_z; float r, g, b; const char* label; };
constexpr PoleSpec kPoles[] = {
    {kLightOrbit,  0.0f, kPoleTopZ, 1.00f, 0.55f, 0.10f, "east pole (orange)"},
    {0.0f,  kLightOrbit, kPoleTopZ, 0.15f, 0.90f, 0.25f, "north pole (green)"},
    {-kLightOrbit, 0.0f, kPoleTopZ, 0.25f, 0.55f, 1.00f, "west pole (blue)"},
    {0.0f, -kLightOrbit, kPoleTopZ, 1.00f, 0.95f, 0.20f, "south pole (yellow)"},
};
constexpr int kNumPoles = sizeof(kPoles) / sizeof(kPoles[0]);

// Mirrors apply_lighting_deferred.metal::tone_map_zone_system. Update
// here when that shader's tone curve moves.
static int tone_map_zone_system(float raw_lux) {
    const float shadow_threshold = 10.0f;
    const float midtone_threshold = 100.0f;
    const float shadow_rgb_max = 75.0f;
    const float midtone_rgb_max = 200.0f;
    if (raw_lux < 0.001f) return 0;
    if (raw_lux <= shadow_threshold)
        return (int)((raw_lux / shadow_threshold) * shadow_rgb_max);
    if (raw_lux <= midtone_threshold) {
        float t = (raw_lux - shadow_threshold) / (midtone_threshold - shadow_threshold);
        return (int)(shadow_rgb_max + t * (midtone_rgb_max - shadow_rgb_max));
    }
    float excess = raw_lux - midtone_threshold;
    float compressed = excess * 0.55f;
    int v = (int)(midtone_rgb_max + std::min(compressed, 255.0f - midtone_rgb_max));
    return std::min(v, 255);
}

kg::EntityID spawn_pad(kg::KGModule& kg, Engine& engine) {
    auto& ps = engine.get_particle_system();
    auto e = kg.createEntity("Arena");
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = 0.0f; p.y = 0.0f; p.z = kPadCenterZ;
    p.width     = kPadHalf * 2.0f;
    p.height    = kPadHalf * 2.0f;
    p.thickness = kPadThickness;
    // Medium-gray pad so shadows are actually visible on it. Too-dark a
    // pad reads as "shadow" regardless of lighting; the previous dark
    // navy was masking the real behavior.
    p.r = 0.55f; p.g = 0.57f; p.b = 0.62f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    ps.add_particle_to_entity(p, &kg, e);
    return e;
}

// Color-coded vertical post for each cardinal light position. Static,
// non-emissive — purely visual markers so it's obvious on screen WHICH
// cardinal direction the active light is occupying.
void spawn_cardinal_pole(kg::KGModule& kg, Engine& engine, const PoleSpec& s) {
    auto& ps = engine.get_particle_system();
    auto e = kg.createEntity("ArenaWall");
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = s.x; p.y = s.y; p.z = s.top_z * 0.5f;   // centered between ground and top
    p.width = 0.08f; p.height = 0.08f; p.thickness = s.top_z;
    p.r = s.r; p.g = s.g; p.b = s.b; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    ps.add_particle_to_entity(p, &kg, e);
}

// Thin pole for the overhead light — tall vertical pole OFFSET from the
// sphere so it doesn't clip through it. Sits a bit outside the pad edge
// so the user sees "that's where the overhead beam drops from." The
// actual light particle moves to (0,0,kOverheadZ) regardless.
void spawn_overhead_pole(kg::KGModule& kg, Engine& engine) {
    auto& ps = engine.get_particle_system();
    auto e = kg.createEntity("ArenaWall");
    Particle p = {};
    p.shape = ParticleShape::BOX;
    const float pole_x = -kLightOrbit * 1.3f;   // NW of the sphere
    const float pole_y =  kLightOrbit * 1.3f;
    p.x = pole_x; p.y = pole_y; p.z = kOverheadZ * 0.5f;
    p.width = 0.10f; p.height = 0.10f; p.thickness = kOverheadZ;
    p.r = 0.80f; p.g = 0.80f; p.b = 0.85f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    ps.add_particle_to_entity(p, &kg, e);
}

int spawn_light_particle(kg::KGModule& kg, Engine& engine, const LightPose& pose) {
    auto& ps = engine.get_particle_system();
    auto e = kg.createEntity("ArenaWall");
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = pose.x; p.y = pose.y; p.z = pose.z;
    p.width = 0.2f; p.height = 0.2f; p.thickness = 0.2f;
    p.r = 0.95f; p.g = 0.97f; p.b = 1.00f; p.a = 1.0f;
    p.is_light_source   = true;
    p.emission_strength = kLightLumens;
    p.emission_radius   = 40.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    int idx = ps.add_particle_to_entity(p, &kg, e);
    return idx;
}

kg::EntityID spawn_test_sphere(kg::KGModule& kg, Engine& engine) {
    auto& ps = engine.get_particle_system();
    auto e = kg.createEntity("Motorcycle");
    Particle p = {};
    p.shape = ParticleShape::ELLIPSOID;
    p.x = 0.0f; p.y = 0.0f; p.z = kSphereCenter;
    // Non-uniform per-axis dimensions — this is the whole point of the test.
    p.width     = kEllipW;
    p.height    = kEllipH;
    p.thickness = kEllipT;
    p.r = 0.15f; p.g = 0.95f; p.b = 1.00f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    ps.add_particle_to_entity(p, &kg, e);
    return e;
}

}  // namespace

class SphereLightingApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();
        kg.extendOntology(logotron::ontology::registry());

        spawn_pad(kg, *engine_);
        spawn_test_sphere(kg, *engine_);
        for (const auto& pole : kPoles) spawn_cardinal_pole(kg, *engine_, pole);
        spawn_overhead_pole(kg, *engine_);
        light_particle_idx_ = spawn_light_particle(kg, *engine_, kLightPoses[light_pose_idx_]);

        auto& cam = engine_->get_camera_system();
        cam.set_position(0.0f, 0.0f, kSphereCenter);
        cam.look_at(0.0f, 0.0f, kSphereCenter);
        cam.set_pixels_per_unit(120.0f);

        dump_particle_inventory();

        std::cout << "[sphere-lighting] SPACE=cycle light, SHIFT+P=toggle continuous dump, "
                     "P=single dump, ESC=quit." << std::endl;
        std::cout << "[sphere-lighting] current light: " << kLightPoses[light_pose_idx_].label << std::endl;
    }

    // One-shot startup inventory: every particle in the ParticleSystem,
    // its position, shape, color, and critically its is_light_source
    // flag. If a particle meant to emit light is NOT flagged, the
    // shadow BVH will treat it as an occluder (line 251 render_pipeline.cpp
    // only skips `is_light_source == true`). Inversely, if a non-light
    // particle IS flagged, it won't cast shadows. Either condition
    // produces the symptoms we're chasing. Print once so we can grep.
    void dump_particle_inventory() const {
        auto view = engine_->get_particle_system().lock_particles_for_read();
        std::printf("[inventory] %zu particles:\n", view.size());
        for (size_t i = 0; i < view.size(); ++i) {
            const auto& p = view[i];
            const char* shape =
                p.shape == ParticleShape::SPHERE    ? "SPHERE"    :
                p.shape == ParticleShape::ELLIPSOID ? "ELLIPSOID" :
                p.shape == ParticleShape::BOX       ? "BOX"       : "OTHER";
            std::printf("  [%2zu] shape=%-9s pos=(%+6.3f,%+6.3f,%+6.3f)  "
                        "w×h×t=(%.2f,%.2f,%.2f)  color=(%.2f,%.2f,%.2f)  "
                        "light=%d  emit=%.0f\n",
                        i, shape, p.x, p.y, p.z,
                        p.width, p.height, p.thickness,
                        p.r, p.g, p.b,
                        (int)p.is_light_source, p.emission_strength);
        }
        std::fflush(stdout);
    }

    void update_game(float dt) override {
        if (!engine_) return;
        if (continuous_dump_) {
            dump_accum_ += dt;
            if (dump_accum_ >= 0.20f) {
                print_telemetry();
                dump_accum_ = 0.0f;
            }
        }
        if (dump_pending_) { print_telemetry(); dump_pending_ = false; }
    }

    bool handle_key(int key, int /*scancode*/, int action, int mods) override {
        if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
            advance_light();
            return true;
        }
        if (key == GLFW_KEY_P && action == GLFW_PRESS) {
            if (mods & GLFW_MOD_SHIFT) {
                continuous_dump_ = !continuous_dump_;
                std::cout << "[sphere-lighting] continuous dump "
                          << (continuous_dump_ ? "ON" : "OFF") << std::endl;
            } else {
                dump_pending_ = true;
            }
            return true;
        }
        return false;
    }

    bool handle_mouse_scroll(double, double yoffset) override {
        if (!engine_) return false;
        engine_->get_camera_system().adjust_zoom(static_cast<float>(yoffset) * 3.0f);
        return true;
    }

    int get_window_width()  const override { return 1600; }
    int get_window_height() const override { return 1200; }
    const char* get_app_name() const override { return "Sphere Lighting Test"; }

private:
    // Move the light particle to the next cardinal pose. Writes directly
    // to the ParticleSystem's mutable vector — the next frame's G-buffer
    // rebuild picks up the new position naturally, same as any other
    // moving particle.
    void advance_light() {
        light_pose_idx_ = (light_pose_idx_ + 1) % kNumLightPoses;
        const LightPose& p = kLightPoses[light_pose_idx_];
        auto& ps = engine_->get_particle_system();
        auto view = ps.lock_particles_for_write();
        if (light_particle_idx_ >= 0 && light_particle_idx_ < (int)view.size()) {
            view[light_particle_idx_].x = p.x;
            view[light_particle_idx_].y = p.y;
            view[light_particle_idx_].z = p.z;
        }
        std::cout << "[sphere-lighting] light moved to " << p.label << std::endl;
    }

    struct ExpectedLighting {
        float lambertian, distance_to_light, intensity_lux, lit_lux;
        int   tone_mapped, r8, g8, b8, screen_x, screen_y;
    };

    ExpectedLighting compute_expected(float wx, float wy, float wz,
                                      float nx, float ny, float nz,
                                      float lx, float ly, float lz,
                                      float base_r, float base_g, float base_b) const {
        ExpectedLighting e{};
        const float dx = lx - wx, dy = ly - wy, dz = lz - wz;
        const float dist_sq = dx*dx + dy*dy + dz*dz;
        const float dist = std::sqrt(dist_sq);
        const float invd = 1.0f / dist;
        const float Lx = dx * invd, Ly = dy * invd, Lz = dz * invd;

        e.lambertian       = std::max(0.0f, nx*Lx + ny*Ly + nz*Lz);
        e.distance_to_light = dist;
        const float four_pi = 12.566370614359172f;
        e.intensity_lux    = kLightLumens / (four_pi * std::max(dist_sq, 0.0001f));
        e.lit_lux          = e.intensity_lux * e.lambertian;
        e.tone_mapped      = tone_map_zone_system(e.lit_lux);
        e.r8 = (int)(base_r * e.tone_mapped);
        e.g8 = (int)(base_g * e.tone_mapped);
        e.b8 = (int)(base_b * e.tone_mapped);
        engine_->get_camera_system().world_to_screen(wx, wy, wz, e.screen_x, e.screen_y);
        return e;
    }

    // Test whether the line segment from `origin` to `target` passes
    // through the axis-aligned box centered at `cx,cy,cz` with the
    // given half-extents. If it does, the ray-to-light from `origin`
    // through `target` (the light position) would be marked "in
    // shadow" by the engine — so we know the pipeline's decision
    // regardless of any bug.
    static bool segment_hits_aabb(float ox, float oy, float oz,
                                  float tx, float ty, float tz,
                                  float cx, float cy, float cz,
                                  float hw, float hh, float hd) {
        float dx = tx - ox, dy = ty - oy, dz = tz - oz;
        float bmin_x = cx - hw, bmax_x = cx + hw;
        float bmin_y = cy - hh, bmax_y = cy + hh;
        float bmin_z = cz - hd, bmax_z = cz + hd;
        float tmin = 0.0f, tmax = 1.0f;
        auto slab = [&](float o, float d, float bmin, float bmax) {
            if (std::fabs(d) < 1e-6f) return !(o < bmin || o > bmax);
            float t1 = (bmin - o) / d;
            float t2 = (bmax - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            return tmin <= tmax;
        };
        if (!slab(ox, dx, bmin_x, bmax_x)) return false;
        if (!slab(oy, dy, bmin_y, bmax_y)) return false;
        if (!slab(oz, dz, bmin_z, bmax_z)) return false;
        return tmin <= tmax && tmin < 1.0f && tmax > 0.0f;
    }

    // For a sample point, list which non-light particles sit between
    // it and the current light along the ray's straight path. If any
    // are found, the engine will mark the pixel in shadow. Lets us
    // tell "the engine is wrong" apart from "correctly occluded."
    std::string list_occluders(float wx, float wy, float wz,
                               float lx, float ly, float lz) const {
        std::string out;
        auto view = engine_->get_particle_system().lock_particles_for_read();
        for (size_t i = 0; i < view.size(); ++i) {
            const auto& p = view[i];
            if (p.is_light_source) continue;
            // Skip if this IS the sample point's own particle — approximate
            // by a tiny distance-to-particle-center test (0.2m).
            float psx = p.x - wx, psy = p.y - wy, psz = p.z - wz;
            if (psx*psx + psy*psy + psz*psz < 0.04f) continue;

            float hw, hh, hd;
            if (p.shape == ParticleShape::SPHERE) {
                hw = hh = hd = p.size * 0.5f;
            } else {
                hw = p.width * 0.5f; hh = p.height * 0.5f; hd = p.thickness * 0.5f;
            }
            if (segment_hits_aabb(wx, wy, wz, lx, ly, lz, p.x, p.y, p.z, hw, hh, hd)) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "p[%zu] ", i);
                out += buf;
            }
        }
        if (out.empty()) out = "(none)";
        return out;
    }

    void print_one(const char* label, float wx, float wy, float wz,
                   float nx, float ny, float nz,
                   float lx, float ly, float lz,
                   float base_r, float base_g, float base_b) const {
        ExpectedLighting e = compute_expected(wx, wy, wz, nx, ny, nz, lx, ly, lz,
                                              base_r, base_g, base_b);
        auto occ = list_occluders(wx, wy, wz, lx, ly, lz);
        std::printf(
            "  %-14s W=(%+6.3f,%+6.3f,%+6.3f) N=(%+5.2f,%+5.2f,%+5.2f) "
            "n·L=%.3f dist=%.2f lit=%.0f tone=%3d RGB=(%3d,%3d,%3d) screen=(%4d,%4d) "
            "occluders=%s\n",
            label, wx, wy, wz, nx, ny, nz,
            e.lambertian, e.distance_to_light,
            e.lit_lux, e.tone_mapped, e.r8, e.g8, e.b8,
            e.screen_x, e.screen_y,
            occ.c_str());
        std::fflush(stdout);
    }

    void print_telemetry() const {
        const LightPose& L = kLightPoses[light_pose_idx_];
        std::printf("\n*** CURRENT LIGHT: %s @ (%+.2f,%+.2f,%+.2f) ***\n", L.label, L.x, L.y, L.z);
        std::printf("[telem] expected per-surface brightness (6 cardinal points on the ELLIPSOID):\n");

        // Ellipsoid cardinal surface points. A direction `d` on the unit
        // sphere maps to a surface point (d.x*hw, d.y*hh, d.z*ht) on the
        // ellipsoid. The true outward normal there is NOT d — for the
        // implicit (x/a)² + (y/b)² + (z/c)² = 1, grad ∝ (x/a², y/b², z/c²),
        // so the normal is proportional to (d.x/hw, d.y/hh, d.z/ht)
        // normalized. For the 6 on-axis cardinal points that happens to
        // collapse back to ±ê (since the other components are zero), so the
        // normals here are still ±ê per axis. Leaving the math spelled out
        // below so an off-axis point added later gets the right normal.
        struct P { const char* name; float dx, dy, dz; };
        const P dirs[] = {
            {"top    +Z",  0.0f,  0.0f,  1.0f},
            {"east   +X",  1.0f,  0.0f,  0.0f},
            {"north  +Y",  0.0f,  1.0f,  0.0f},
            {"west   -X", -1.0f,  0.0f,  0.0f},
            {"south  -Y",  0.0f, -1.0f,  0.0f},
            {"bottom -Z",  0.0f,  0.0f, -1.0f},
        };
        const float hw = kEllipW * 0.5f;
        const float hh = kEllipH * 0.5f;
        const float ht = kEllipT * 0.5f;
        for (const auto& d : dirs) {
            float wx = d.dx * hw;
            float wy = d.dy * hh;
            float wz = kSphereCenter + d.dz * ht;
            // Ellipsoid outward normal at this surface point.
            float nx = d.dx / hw;
            float ny = d.dy / hh;
            float nz = d.dz / ht;
            float nlen = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (nlen > 1e-6f) { nx /= nlen; ny /= nlen; nz /= nlen; }
            print_one(d.name, wx, wy, wz, nx, ny, nz, L.x, L.y, L.z,
                      0.15f, 0.95f, 1.00f);
        }

        // 9 sample points across the PAD's top surface (normal = +Z).
        // Any one of them appearing dark on screen when the telemetry
        // says "tone=255" means the shadow ray from that pad pixel to
        // the current light is being rejected by the pipeline — and
        // nothing in this scene should legitimately block those rays.
        std::printf("[telem] expected PAD top brightness (normal=+Z) at a 3×3 grid on the pad:\n");
        struct PadPt { const char* name; float x, y; };
        const PadPt pad_pts[] = {
            {"pad NW", -3.0f,  3.0f}, {"pad N ",  0.0f,  3.0f}, {"pad NE",  3.0f,  3.0f},
            {"pad W ", -3.0f,  0.0f}, {"pad C ",  0.0f,  0.0f}, {"pad E ",  3.0f,  0.0f},
            {"pad SW", -3.0f, -3.0f}, {"pad S ",  0.0f, -3.0f}, {"pad SE",  3.0f, -3.0f},
        };
        for (const auto& p : pad_pts) {
            print_one(p.name, p.x, p.y, 0.0f, 0.0f, 0.0f, 1.0f, L.x, L.y, L.z,
                      0.55f, 0.57f, 0.62f);
        }
    }

    Engine* engine_ = nullptr;
    int  light_particle_idx_ = -1;
    int  light_pose_idx_ = 0;
    bool dump_pending_ = false;
    bool continuous_dump_ = true;
    float dump_accum_ = 0.0f;
};

int main(int argc, char* argv[]) {
    bool headless = false;
    double auto_exit_at = -1.0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-head" || arg == "--headless") headless = true;
        else if (arg == "--exit-after" && i + 1 < argc) auto_exit_at = std::stod(argv[++i]);
    }

    std::cout << "╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║  SPHERE-LIGHTING-TEST — cardinal sweep   ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝" << std::endl;

    SphereLightingApp app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width  = 1600;
    config.window_height = 1200;
    config.window_title  = "Sphere Lighting Test";
    config.show_debug_overlay = false;
    config.show_kg_inspector  = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) < 0) {
        std::cerr << "[sphere-lighting] engine.initialize() failed." << std::endl;
        return 1;
    }
    if (headless) { engine.shutdown(); return 0; }

    auto started    = std::chrono::high_resolution_clock::now();
    auto last_frame = started;
    while (engine.should_continue()) {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - started).count();
        if (auto_exit_at > 0 && elapsed >= auto_exit_at) break;
        double dt = std::chrono::duration<double>(now - last_frame).count();
        last_frame = now;
        if (dt > 0.1) dt = 0.1;
        engine.update(dt);
        engine.render();
        engine.present();
    }
    engine.shutdown();
    return 0;
}
