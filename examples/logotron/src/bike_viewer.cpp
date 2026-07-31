// bike_viewer — standalone visual + diagnostic rig for the Logotron
// motorcycle. One bike at the world origin, a lit pad. Arrow keys / A-D
// rotate the assembly continuously around world Z. Mouse wheel zooms.
//
// All rendering of the bike goes through
// logosphere::assembly::RigidAssembly so the viewer is also an honest
// test of the engine primitive — if rotation / composition breaks, the
// coherent=YES/NO flag in the continuous dump will show it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include <GLFW/glfw3.h>

#include "application.h"
#include "core/engine.h"
#include "core/camera_system.h"
#include "core/particle_system.h"
#include "logosphere/assembly/rigid_assembly.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/rendering/rasterization_math.h"
#include "logotron_ontology_registry.h"
#include "particle.h"

namespace la = logosphere::assembly;

namespace {

constexpr float kPadHalf        = 6.0f;
constexpr float kPadThickness   = 0.10f;
constexpr float kPadCenterZ     = -kPadThickness * 0.5f;   // pad TOP at z = 0 exactly
constexpr float kBikeLookZ      = 2.50f;  // outside body's vertical range
                                           // (body extends z=[0.26, 0.58]).
                                           // A camera inside the body's
                                           // bounding volume breaks front/
                                           // back-facing disambiguation and
                                           // renders only the silhouette rim.
constexpr float kRotSpeed       = 1.50f;
constexpr float kRotStep        = 0.15f;
constexpr float kPoleTopZ       = 1.80f;                   // lamppost pole top
constexpr float kCardLightZ     = kPoleTopZ + 0.60f;       // cardinal light floats ABOVE its pole
                                                            // so the pole doesn't occlude the ray
                                                            // from the scene to the light.
constexpr float kOverheadZ      = 4.50f;
constexpr float kLightOrbit     = 4.5f;                    // cardinal distance from bike center
constexpr float kLightLumens    = 1500000.0f;

struct LightPose { const char* label; float x, y, z; };
constexpr LightPose kLightPoses[] = {
    {"0: overhead (+Z)", 0.0f,  0.0f, kOverheadZ},
    {"1: east (+X)",     kLightOrbit, 0.0f, kCardLightZ},
    {"2: north (+Y)",    0.0f,  kLightOrbit, kCardLightZ},
    {"3: west (-X)",    -kLightOrbit, 0.0f, kCardLightZ},
    {"4: south (-Y)",    0.0f, -kLightOrbit, kCardLightZ},
};
constexpr int kNumLightPoses = sizeof(kLightPoses) / sizeof(kLightPoses[0]);

struct PoleSpec { float x, y; float r, g, b; const char* label; };
constexpr PoleSpec kPoles[] = {
    {kLightOrbit,  0.0f, 1.00f, 0.55f, 0.10f, "east pole (orange)"},
    {0.0f,  kLightOrbit, 0.15f, 0.90f, 0.25f, "north pole (green)"},
    {-kLightOrbit, 0.0f, 0.25f, 0.55f, 1.00f, "west pole (blue)"},
    {0.0f, -kLightOrbit, 1.00f, 0.95f, 0.20f, "south pole (yellow)"},
};
constexpr int kNumPoles = sizeof(kPoles) / sizeof(kPoles[0]);

kg::EntityID spawn_pad(kg::KGModule& kg, Engine& engine) {
    auto& ps = engine.get_particle_system();
    auto e = kg.createEntity("Arena");
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = 0.0f; p.y = 0.0f; p.z = kPadCenterZ;
    p.width     = kPadHalf * 2.0f;
    p.height    = kPadHalf * 2.0f;
    p.thickness = kPadThickness;
    // Medium-gray pad so shadows read visibly. Dark pads just look
    // shadowed regardless of whether any ray got blocked.
    p.r = 0.55f; p.g = 0.57f; p.b = 0.62f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    ps.add_particle_to_entity(p, &kg, e);
    return e;
}

void spawn_cardinal_pole(kg::KGModule& kg, Engine& engine, const PoleSpec& s) {
    auto& ps = engine.get_particle_system();
    auto e = kg.createEntity("ArenaWall");
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = s.x; p.y = s.y; p.z = kPoleTopZ * 0.5f;
    p.width = 0.10f; p.height = 0.10f; p.thickness = kPoleTopZ;
    p.r = s.r; p.g = s.g; p.b = s.b; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.owner = ParticleOwner::STATIC;
    p.is_at_rest = true;
    ps.add_particle_to_entity(p, &kg, e);
}

// Single light particle; moves between LightPoses on SPACE. Returns the
// render index so we can update the particle's XYZ each time without
// destroying/respawning.
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
    return ps.add_particle_to_entity(p, &kg, e);
}

// Build the bike as a rigid assembly. Body sits above the wheels; the
// body and wheels ride the assembly yaw unchanged (local_yaw = 0).
la::RigidAssembly make_motorcycle_assembly(kg::EntityID entity) {
    const float body_len   = 1.80f;
    const float body_w     = 0.55f;
    const float body_h     = 0.32f;
    const float wheel_r    = 0.26f;
    const float wheel_t    = 0.14f;
    const float wheel_offs = 0.70f;
    const float body_z     = wheel_r + body_h * 0.5f;

    la::RigidAssembly a;
    a.entity = entity;
    a.world_x = a.world_y = a.world_z = 0.0f;
    a.world_yaw = 0.0f;

    // Engine convention: local +Y = forward. The body is long along Y.
    la::BodyPart body;
    body.name = "body";
    body.shape = ParticleShape::ELLIPSOID;
    body.local_z = body_z;
    body.width = body_w;        // X (sideways)
    body.height = body_len;     // Y (forward) — the LONG axis
    body.thickness = body_h;    // Z (up)
    body.r = 0.15f; body.g = 0.95f; body.b = 1.00f; body.a = 1.0f;

    // Wheel disc lies in the YZ plane (contains travel + vertical),
    // axle along X. width=axle, height=diameter-forward, thickness=diameter-up.
    la::BodyPart wheel_front;
    wheel_front.name = "wheel_front";
    wheel_front.shape = ParticleShape::ELLIPSOID;
    wheel_front.local_y = +wheel_offs;  // forward end
    wheel_front.local_z = wheel_r;
    wheel_front.width     = wheel_t;
    wheel_front.height    = 2.0f * wheel_r;
    wheel_front.thickness = 2.0f * wheel_r;
    wheel_front.r = 0.06f; wheel_front.g = 0.14f; wheel_front.b = 0.18f; wheel_front.a = 1.0f;

    la::BodyPart wheel_rear = wheel_front;
    wheel_rear.name = "wheel_rear";
    wheel_rear.local_y = -wheel_offs;   // rear end

    // Orientation marker: a bright orange "prow" sticking UP and
    // FORWARD from the body's nose. Raising it above the body's top
    // plane (+Z) makes "which end is the front?" unambiguous under
    // iso projection — otherwise a flat ellipsoid looks the same from
    // both ends and you can't tell the bike is flipped 180°.
    la::BodyPart nose;
    nose.name = "nose_marker";
    nose.shape = ParticleShape::SPHERE;
    nose.local_y = body_len * 0.5f - 0.05f;                // front tip of body
    nose.local_z = body_z + body_h * 0.5f + 0.20f;         // 20 cm above body top
    nose.size = 0.22f;
    nose.r = 1.0f; nose.g = 0.55f; nose.b = 0.05f; nose.a = 1.0f;
    nose.is_light_source   = true;
    nose.emission_strength = 400000.0f;
    nose.emission_radius   = 8.0f;

    // Tail marker: a smaller, dimmer cyan "fin" at the back, also
    // raised slightly. Makes front/back asymmetric by shape AND color.
    la::BodyPart tail;
    tail.name = "tail_marker";
    tail.shape = ParticleShape::SPHERE;
    tail.local_y = -body_len * 0.5f + 0.05f;                // rear tip of body
    tail.local_z = body_z + body_h * 0.5f + 0.08f;          // 8 cm above body top
    tail.size = 0.14f;
    tail.r = 0.12f; tail.g = 0.70f; tail.b = 1.00f; tail.a = 1.0f;

    a.parts = { body, wheel_front, wheel_rear, nose, tail };
    return a;
}

}  // namespace

class BikeViewerApplication : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();

        kg.extendOntology(logotron::ontology::registry());

        spawn_pad(kg, *engine_);
        for (const auto& pole : kPoles) spawn_cardinal_pole(kg, *engine_, pole);
        light_particle_idx_ = spawn_light_particle(kg, *engine_, kLightPoses[light_pose_idx_]);

        auto bike_entity = kg.createEntity("Motorcycle");
        bike_ = make_motorcycle_assembly(bike_entity);
        la::sync_rigid_assembly(kg, engine_->get_particle_system(), bike_);

        auto& cam = engine_->get_camera_system();
        cam.set_position(0.0f, 0.0f, kBikeLookZ);
        cam.look_at(0.0f, 0.0f, kBikeLookZ);
        cam.set_pixels_per_unit(60.0f);

        std::cout << "[bike-viewer] controls: LEFT/RIGHT rotate bike, "
                     "SHIFT+arrow fast, SPACE cycle light, R reset yaw, "
                     "scroll zoom, P dump, SHIFT+P toggle continuous, ESC quit."
                  << std::endl;
        std::cout << "[bike-viewer] current light: " << kLightPoses[light_pose_idx_].label << std::endl;
    }

    // Move the single light particle to the next cardinal pose. Same
    // mechanism as sphere_lighting_test — write directly to the
    // ParticleSystem's mutable vector, next frame's pipeline picks up
    // the new position.
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
        std::cout << "[bike-viewer] light moved to " << p.label << std::endl;
    }

    void update_game(float dt) override {
        if (!engine_ || bike_.entity == kg::INVALID_ENTITY) return;
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();

        // Compass convention (see docs/ARCHITECTURE.md): positive yaw = CW from
        // above = bike turns RIGHT (toward east). So:
        //   LEFT key  → yaw decreases (CCW, bike turns left / west)
        //   RIGHT key → yaw increases (CW,  bike turns right / east)
        float rate = kRotSpeed * (fast_mod_ ? 3.0f : 1.0f);
        if (rotating_left_)  bike_.world_yaw -= rate * dt;
        if (rotating_right_) bike_.world_yaw += rate * dt;

        la::sync_rigid_assembly(kg, ps, bike_);

        if (continuous_dump_) {
            dump_accum_ += dt;
            if (dump_accum_ >= 0.20f) {
                print_nose_telemetry();
                dump_accum_ = 0.0f;
            }
        }
        if (dump_pending_) {
            la::dump_assembly(std::cout, bike_, kg, ps);
            print_nose_telemetry();
            dump_pending_ = false;
        }

        auto& cam = engine_->get_camera_system();
        cam.set_position(0.0f, 0.0f, kBikeLookZ);
        cam.look_at(0.0f, 0.0f, kBikeLookZ);
    }

    // Dense per-sample telemetry. One line per call, grep-friendly,
    // so we can correlate a screenshot with the numbers the engine is
    // actually computing. The contract under test: orange (nose, z=0.78)
    // and blue (tail, z=0.66) BOTH sit above the body's tallest point
    // (z ≤ 0.58) at every yaw, so both must always win the depth test
    // against the body region directly beneath each of them. If either
    // shows in_front=NO, the depth bug is back.
    //
    // The "body_under_X" point is the top of the body ellipsoid at the
    // local-Y position immediately under marker X — the specific
    // silhouette pixel the depth buffer pits the marker against. Using
    // CameraSystem::compute_depth here so the telemetry uses the SAME
    // metric the rasterizer writes into the depth buffer; otherwise we
    // would be testing one thing and the GPU another.
    void print_nose_telemetry() {
        if (!engine_ || bike_.parts.size() < 5) return;
        const auto& cam = engine_->get_camera_system();
        const float rad2deg = 57.29577951308232f;
        const float yaw = bike_.world_yaw;

        const LightPose& L = kLightPoses[light_pose_idx_];
        std::printf("\n*** CURRENT LIGHT: %s @ (%+.2f,%+.2f,%+.2f) ***\n",
                    L.label, L.x, L.y, L.z);

        const la::BodyPart& body = bike_.parts[0];
        const la::BodyPart& nose = bike_.parts[3];
        const la::BodyPart& tail = bike_.parts[4];

        float nx, ny, nz; la::part_world_position(bike_, nose, nx, ny, nz);
        float tx, ty, tz; la::part_world_position(bike_, tail, tx, ty, tz);
        float bx, by, bz; la::part_world_position(bike_, body, bx, by, bz);

        // Body silhouette point directly beneath each marker. The
        // marker sits at the body's local +Y or -Y tip (offset 0.94 of
        // half-height — see make_motorcycle_assembly). The body top at
        // that local-Y is body_z + body_h/2 * sqrt(1 - (ly/hh)²).
        auto body_top_at_local_y = [&](float local_y_signed) {
            const float hh = body.height * 0.5f;
            const float ratio = std::min(1.0f, std::fabs(local_y_signed) / hh);
            const float lz_surf = body.thickness * 0.5f *
                                  std::sqrt(std::max(0.0f, 1.0f - ratio * ratio));
            const float s = std::sin(yaw), co = std::cos(yaw);
            float wx = bike_.world_x + co * 0.0f + s * local_y_signed;
            float wy = bike_.world_y - s * 0.0f + co * local_y_signed;
            float wz = bz + lz_surf;
            return std::array<float, 3>{wx, wy, wz};
        };
        auto bn = body_top_at_local_y(nose.local_y);   // body top under nose
        auto bt = body_top_at_local_y(tail.local_y);   // body top under tail

        int n_sx, n_sy, t_sx, t_sy, bn_sx, bn_sy, bt_sx, bt_sy;
        cam.world_to_screen(nx, ny, nz, n_sx, n_sy);
        cam.world_to_screen(tx, ty, tz, t_sx, t_sy);
        cam.world_to_screen(bn[0], bn[1], bn[2], bn_sx, bn_sy);
        cam.world_to_screen(bt[0], bt[1], bt[2], bt_sx, bt_sy);

        const float nose_depth     = cam.compute_depth(nx, ny, nz);
        const float tail_depth     = cam.compute_depth(tx, ty, tz);
        const float body_under_nose = cam.compute_depth(bn[0], bn[1], bn[2]);
        const float body_under_tail = cam.compute_depth(bt[0], bt[1], bt[2]);

        const bool nose_in_front = (nose_depth < body_under_nose);
        const bool tail_in_front = (tail_depth < body_under_tail);
        const bool both_visible  = nose_in_front && tail_in_front;

        std::printf(
            "[telem] yaw=%+7.3frad (%+6.1fdeg) | "
            "ORANGE_nose W=(%+6.3f,%+6.3f,%+6.3f) S=(%4d,%4d) d=%+8.4f "
            "vs body_under_nose W=(%+6.3f,%+6.3f,%+6.3f) S=(%4d,%4d) d=%+8.4f "
            "Δ=%+7.4f in_front=%s | "
            "BLUE_tail W=(%+6.3f,%+6.3f,%+6.3f) S=(%4d,%4d) d=%+8.4f "
            "vs body_under_tail W=(%+6.3f,%+6.3f,%+6.3f) S=(%4d,%4d) d=%+8.4f "
            "Δ=%+7.4f in_front=%s | "
            "BOTH_VISIBLE=%s\n",
            yaw, yaw * rad2deg,
            nx, ny, nz, n_sx, n_sy, nose_depth,
            bn[0], bn[1], bn[2], bn_sx, bn_sy, body_under_nose,
            nose_depth - body_under_nose, nose_in_front ? "YES" : "NO ",
            tx, ty, tz, t_sx, t_sy, tail_depth,
            bt[0], bt[1], bt[2], bt_sx, bt_sy, body_under_tail,
            tail_depth - body_under_tail, tail_in_front ? "YES" : "NO ",
            both_visible ? "YES" : "NO  ← BUG");
        std::fflush(stdout);

        // Body-cardinal surface points. Six points on the ellipsoid
        // (fwd/back caps along local Y, left/right flanks along local X,
        // top/bottom along local Z), transformed by the bike's yaw so
        // "forward" tracks the current nose direction. For each: the
        // expected Lambert + tone + RGB against the active light. The
        // west cap under a west light should blaze; if telemetry says
        // it should and the screen says it doesn't, the engine is
        // dropping that ray.
        dump_body_cardinal_lighting();
    }

    void dump_body_cardinal_lighting() const {
        const LightPose& L = kLightPoses[light_pose_idx_];
        const la::BodyPart& body = bike_.parts[0];
        const float half_len  = body.height    * 0.5f;   // local +Y extent (long axis)
        const float half_w    = body.width     * 0.5f;   // local +X extent (sideways)
        const float half_h    = body.thickness * 0.5f;   // local +Z extent (up)

        const float yaw = bike_.world_yaw;
        const float s = std::sin(yaw), co = std::cos(yaw);
        // local +Y  (forward)  -> world
        const float fx = +s,         fy = +co;
        // local +X  (starboard)-> world  (compass CW rotation)
        const float rx = +co,        ry = -s;

        struct Pt { const char* name; float pos[3]; float norm[3]; };
        const Pt pts[] = {
            {"body_fwd_cap ",
                { bike_.world_x + half_len * fx, bike_.world_y + half_len * fy, body.local_z },
                { fx, fy, 0.0f }},
            {"body_back_cap",
                { bike_.world_x - half_len * fx, bike_.world_y - half_len * fy, body.local_z },
                { -fx, -fy, 0.0f }},
            {"body_right   ",
                { bike_.world_x + half_w * rx, bike_.world_y + half_w * ry, body.local_z },
                { rx, ry, 0.0f }},
            {"body_left    ",
                { bike_.world_x - half_w * rx, bike_.world_y - half_w * ry, body.local_z },
                { -rx, -ry, 0.0f }},
            {"body_top     ",
                { bike_.world_x, bike_.world_y, body.local_z + half_h },
                { 0.0f, 0.0f, 1.0f }},
            {"body_bottom  ",
                { bike_.world_x, bike_.world_y, body.local_z - half_h },
                { 0.0f, 0.0f, -1.0f }},
        };

        std::printf("[body] light=%s  yaw=%+6.1fdeg\n", L.label,
                    yaw * 57.29577951308232f);
        for (const auto& p : pts) {
            expected_and_print(p.name, p.pos[0], p.pos[1], p.pos[2],
                               p.norm[0], p.norm[1], p.norm[2],
                               L.x, L.y, L.z, 0.15f, 0.95f, 1.00f);
        }
    }

    // Mirrors apply_lighting_deferred.metal::tone_map_zone_system.
    static int tone_map(float raw_lux) {
        if (raw_lux < 0.001f) return 0;
        if (raw_lux <= 10.0f) return (int)((raw_lux / 10.0f) * 75.0f);
        if (raw_lux <= 100.0f) {
            float t = (raw_lux - 10.0f) / 90.0f;
            return (int)(75.0f + t * 125.0f);
        }
        float excess = (raw_lux - 100.0f) * 0.55f;
        int v = (int)(200.0f + std::min(excess, 55.0f));
        return std::min(v, 255);
    }

    // CPU mirror of the Lambert + inverse-square + tone map that the
    // GPU pipeline applies, with an occluder list sourced from the
    // ParticleSystem's AABBs. If the engine and this math disagree,
    // the engine has a bug.
    void expected_and_print(const char* label,
                            float wx, float wy, float wz,
                            float nx, float ny, float nz,
                            float lx, float ly, float lz,
                            float br, float bg, float bb) const {
        float dx = lx - wx, dy = ly - wy, dz = lz - wz;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        float inv = dist > 1e-6f ? 1.0f / dist : 0.0f;
        float Lx = dx*inv, Ly = dy*inv, Lz = dz*inv;
        float lambert = std::max(0.0f, nx*Lx + ny*Ly + nz*Lz);
        float intensity = kLightLumens / (12.566370614359172f * std::max(dist*dist, 0.0001f));
        float lit = intensity * lambert;
        int tone = tone_map(lit);
        int r8 = (int)(br * tone), g8 = (int)(bg * tone), b8 = (int)(bb * tone);

        int sx, sy;
        engine_->get_camera_system().world_to_screen(wx, wy, wz, sx, sy);

        // Occluder list: which non-light particles' AABBs sit between
        // this sample point and the light. Skip particles very close
        // to the sample (same body — approximated by 0.2m radius).
        std::string occ;
        {
            auto view = engine_->get_particle_system().lock_particles_for_read();
            for (size_t i = 0; i < view.size(); ++i) {
                const auto& p = view[i];
                if (p.is_light_source) continue;
                float sdx = p.x - wx, sdy = p.y - wy, sdz = p.z - wz;
                if (sdx*sdx + sdy*sdy + sdz*sdz < 0.04f) continue;
                float hw, hh, hd;
                if (p.shape == ParticleShape::SPHERE) {
                    hw = hh = hd = p.size * 0.5f;
                } else {
                    hw = p.width * 0.5f; hh = p.height * 0.5f; hd = p.thickness * 0.5f;
                }
                if (segment_hits_aabb(wx, wy, wz, lx, ly, lz, p.x, p.y, p.z, hw, hh, hd)) {
                    char buf[24]; std::snprintf(buf, sizeof(buf), "p[%zu] ", i);
                    occ += buf;
                }
            }
            if (occ.empty()) occ = "(none)";
        }

        std::printf("  %s W=(%+6.3f,%+6.3f,%+6.3f) N=(%+5.2f,%+5.2f,%+5.2f) "
                    "n·L=%.3f dist=%.2f lit=%.0f tone=%3d RGB=(%3d,%3d,%3d) "
                    "screen=(%4d,%4d) occluders=%s\n",
                    label, wx, wy, wz, nx, ny, nz,
                    lambert, dist, lit, tone, r8, g8, b8, sx, sy, occ.c_str());
        std::fflush(stdout);
    }

    static bool segment_hits_aabb(float ox, float oy, float oz,
                                  float tx, float ty, float tz,
                                  float cx, float cy, float cz,
                                  float hw, float hh, float hd) {
        float dx = tx - ox, dy = ty - oy, dz = tz - oz;
        float bx0 = cx - hw, bx1 = cx + hw;
        float by0 = cy - hh, by1 = cy + hh;
        float bz0 = cz - hd, bz1 = cz + hd;
        float tmin = 0.0f, tmax = 1.0f;
        auto slab = [&](float o, float d, float lo, float hi) {
            if (std::fabs(d) < 1e-6f) return !(o < lo || o > hi);
            float t1 = (lo - o) / d;
            float t2 = (hi - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            return tmin <= tmax;
        };
        if (!slab(ox, dx, bx0, bx1)) return false;
        if (!slab(oy, dy, by0, by1)) return false;
        if (!slab(oz, dz, bz0, bz1)) return false;
        return tmin <= tmax && tmin < 1.0f && tmax > 0.0f;
    }

    bool handle_key(int key, int /*scancode*/, int action, int mods) override {
        const bool down = (action == GLFW_PRESS || action == GLFW_REPEAT);
        fast_mod_ = (mods & GLFW_MOD_SHIFT) != 0;
        switch (key) {
            case GLFW_KEY_LEFT:
            case GLFW_KEY_A:
                rotating_left_ = down;
                if (action == GLFW_PRESS) bike_.world_yaw -= kRotStep;
                return true;
            case GLFW_KEY_RIGHT:
            case GLFW_KEY_D:
                rotating_right_ = down;
                if (action == GLFW_PRESS) bike_.world_yaw += kRotStep;
                return true;
            case GLFW_KEY_SPACE:
                if (action == GLFW_PRESS) advance_light();
                return true;
            case GLFW_KEY_R:
                if (action == GLFW_PRESS) {
                    bike_.world_yaw = 0.0f;
                    std::cout << "[bike-viewer] yaw reset." << std::endl;
                }
                return true;
            case GLFW_KEY_P:
                if (action == GLFW_PRESS) {
                    if (mods & GLFW_MOD_SHIFT) {
                        continuous_dump_ = !continuous_dump_;
                        std::cout << "[bike-viewer] continuous dump "
                                  << (continuous_dump_ ? "ON" : "OFF") << std::endl;
                    } else {
                        dump_pending_ = true;
                    }
                }
                return true;
            default:
                return false;
        }
    }

    bool handle_mouse_scroll(double /*xoffset*/, double yoffset) override {
        if (!engine_) return false;
        engine_->get_camera_system().adjust_zoom(static_cast<float>(yoffset) * 3.0f);
        return true;
    }

    int get_window_width()  const override { return 1600; }
    int get_window_height() const override { return 1200; }
    const char* get_app_name() const override { return "Bike Viewer"; }

private:
    Engine* engine_ = nullptr;
    la::RigidAssembly bike_;
    bool rotating_left_  = false;
    bool rotating_right_ = false;
    bool fast_mod_ = false;
    bool dump_pending_ = false;
    bool continuous_dump_ = true;
    float dump_accum_ = 0.0f;

    int light_particle_idx_ = -1;
    int light_pose_idx_ = 0;
};

int main(int argc, char* argv[]) {
    bool headless = false;
    double auto_exit_at = -1.0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-head" || arg == "--headless") headless = true;
        else if (arg == "--exit-after" && i + 1 < argc) auto_exit_at = std::stod(argv[++i]);
    }

    std::cout << "╔══════════════════════════════════════╗" << std::endl;
    std::cout << "║  BIKE-VIEWER — motorcycle inspector  ║" << std::endl;
    std::cout << "╚══════════════════════════════════════╝" << std::endl;

    BikeViewerApplication app;
    Engine engine(&app);

    EngineConfig config;
    config.create_display = !headless;
    config.window_width  = 1600;
    config.window_height = 1200;
    config.window_title  = "Bike Viewer";
    config.show_debug_overlay = false;
    config.show_kg_inspector  = false;
    config.enable_chat_window = false;

    if (engine.initialize(config) < 0) {
        std::cerr << "[bike-viewer] engine.initialize() failed." << std::endl;
        return 1;
    }
    if (headless) {
        engine.shutdown();
        return 0;
    }

    auto started    = std::chrono::high_resolution_clock::now();
    auto last_frame = started;
    while (engine.should_continue()) {
        auto now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - started).count();
        if (auto_exit_at > 0 && elapsed >= auto_exit_at) {
            std::cerr << "[bike-viewer] --exit-after " << auto_exit_at << " s — exiting." << std::endl;
            break;
        }
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
