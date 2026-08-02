// One humanoid, one situation at a time, measured against the ground
// the whole way.
//
// This replaces the way test_humanoid_strata_integrity was being read. That
// test drives Eva AND several NPCs with identical velocity commands,
// so they bunch and jam into each other; its cases claim to climb a
// staircase and sprint 100 m, but she never leaves a 7 m box. It
// passes anyway, because it only asks whether she came apart. It is a
// dismemberment guard wearing a locomotion guard's name.
//
// So this one is deliberately the opposite:
//
//   ONE humanoid. Nobody to collide with, nothing to blame.
//   ONE terrain feature per scenario, built from scratch each time.
//   EVERY FEATURE APPROACHED FROM SEVERAL ANGLES. The terrain is a
//     centred plateau and she walks in radially, so a heading is just
//     an angle and none of them is the axis the code was written on.
//   SHE FACES WHERE SHE WALKS. Commanding velocity alone made her
//     strafe sideways along her own path.
//   THE WHOLE PATH IS MEASURED, not just the endpoints. Her height
//     above the real surface beneath her is sampled every frame from
//     the ground locator, and so is the length of each knee. A test
//     that only looks at where she finished cannot see her sink, fly,
//     teleport, or come apart at the joints on the way.
//   NO ABSOLUTE HEIGHTS. Every scenario sits at a different, awkward
//     ground level, and she is placed by asking the ground locator.
//
// Watchable:
//   ./build/test_humanoid_terrain_scenarios
//   LOGOSPHERE_VISUAL=1 ./build/test_humanoid_terrain_scenarios  # SPACE next, ESC stop
//   LOGOSPHERE_TRACE=1  ./build/test_humanoid_terrain_scenarios  # path + ParticleTracer
//   LOGOSPHERE_SHOT=<dir> ./build/...                            # write frames as .ppm

#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/core/ground_locator.h"
#include "particle.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_xfailed = 0;

namespace {

bool g_visual = false;
bool g_quit = false;

constexpr float TILE = 2.0f;
constexpr float APPROACH_R = 9.0f;    // where she starts, from the centre
constexpr float PLATEAU_R  = 5.0f;    // radius of the raised ground

// ---------------------------------------------------------------- world

// Terrain is kinematic: it owns its own position, nothing pushes it.
void tile_at(Engine& e, float x, float y, float top_z, float thickness) {
    Particle p{};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y;
    p.z = top_z - thickness * 0.5f;
    p.width = p.height = TILE;
    p.thickness = thickness;
    p.size = TILE;
    p.r = 0.45f; p.g = 0.42f; p.b = 0.38f; p.a = 1.0f;
    p.SetMaterial(Materials::Type::STONE);
    p.solver_mode = ParticleSolverMode::KINEMATIC;
    p.is_at_rest = true;
    e.add_particle(p);
}

// A square of ground, and optionally a raised disc in the middle of
// it. A disc rather than a step across the map, so that every approach
// angle meets the same feature: the heading stops being a special case
// of the axis somebody happened to write the code along.
void terrain(Engine& e, float ground, float plateau_z, float plateau_r) {
    const float half = APPROACH_R + 4.0f;
    for (float x = -half; x <= half; x += TILE) {
        for (float y = -half; y <= half; y += TILE) {
            tile_at(e, x, y, ground, 0.5f);
            if (plateau_z != 0.0f &&
                std::sqrt(x * x + y * y) <= plateau_r) {
                // Thick enough to reach the ground below it, so the
                // feature is a step and not a shelf floating over a
                // gap she can walk into.
                const float thick = std::max(0.5f,
                                             (plateau_z - ground) + 0.8f);
                tile_at(e, x, y, plateau_z, thick);
            }
        }
    }
}

// Loose things underfoot. Dynamic and light: they should be kicked
// aside, not climbed, and never take her down.
std::vector<unsigned int> litter(Engine& e, int count, float ground,
                                 float dir_x, float dir_y) {
    std::vector<unsigned int> ids;
    for (int i = 0; i < count; ++i) {
        // Closer together than a stride, so no heading can thread
        // between them. At 1.7 m spacing she straddled the lot on one
        // approach and the scenario proved nothing.
        const float t = 2.0f + i * 0.8f;
        // Swept across the stride rather than lined up on it. A step
        // width to the side and she walks past; dead on the centreline
        // and her feet straddle it, which both read as "she ignored the
        // debris" when they really mean "she missed it".
        const float side = -0.20f + 0.045f * i;
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = -dir_x * (APPROACH_R - t) - dir_y * side;
        p.y = -dir_y * (APPROACH_R - t) + dir_x * side;
        p.z = ground + 0.06f;
        p.width = p.height = 0.12f;
        p.thickness = 0.12f;
        p.size = 0.12f;
        p.r = 0.55f; p.g = 0.35f; p.b = 0.2f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::WOOD_SOFT);
        p.solver_mode = ParticleSolverMode::DYNAMIC;
        ids.push_back(static_cast<unsigned int>(e.add_particle(p)));
    }
    return ids;
}

struct Walker {
    int hips = -1;
    std::vector<unsigned int> all;
    // Knees, as the joints that were seen to come apart: {thigh, shin}
    // for each leg. left_leg_ids is {foot, shin, thigh, toe}.
    unsigned int l_thigh = 0, l_shin = 0, r_thigh = 0, r_shin = 0;
    unsigned int l_foot = 0, r_foot = 0;
};

// Place her by ASKING, never by assuming.
bool spawn_walker(Engine& e, float x, float y, Walker& out) {
    logosphere::PlacementRequest req;
    req.x = x; req.y = y;
    req.mode = logosphere::SupportMode::STANDING;
    req.footprint = 0.6f;
    req.height = 1.8f;
    auto place = e.get_ground_locator().locate(req);
    if (!place.found) {
        std::cout << "    cannot place walker: " << place.reason << std::endl;
        return false;
    }

    auto& wg = e.get_worldgen_system();
    HumanoidSpec spec = HumanoidSpec::default_human();
    auto body = wg.get_humanoid_generator().generate_humanoid_physics(
        x, y, place.z + 0.1f, -1, spec, false);
    body.create_kg_entities(e.get_kg(), "Humanoid", 180.0f, 800.0f);
    e.get_humanoid_locomotion().register_humanoid_direct(
        body.hips_id, body.left_leg_ids, body.right_leg_ids,
        body.left_arm_ids, body.right_arm_ids, body.torso_ids,
        180.0f, 800.0f);
    e.get_humanoid_locomotion().set_volitional(body.hips_id, true);

    out.hips = body.hips_id;
    out.all.clear();
    for (auto v : {body.left_leg_ids, body.right_leg_ids, body.left_arm_ids,
                   body.right_arm_ids, body.torso_ids})
        for (auto i : v) out.all.push_back(i);
    out.all.push_back(body.hips_id);

    if (body.left_leg_ids.size() >= 3) {
        out.l_foot  = static_cast<unsigned int>(body.left_leg_ids[0]);
        out.l_shin  = static_cast<unsigned int>(body.left_leg_ids[1]);
        out.l_thigh = static_cast<unsigned int>(body.left_leg_ids[2]);
    }
    if (body.right_leg_ids.size() >= 3) {
        out.r_foot  = static_cast<unsigned int>(body.right_leg_ids[0]);
        out.r_shin  = static_cast<unsigned int>(body.right_leg_ids[1]);
        out.r_thigh = static_cast<unsigned int>(body.right_leg_ids[2]);
    }
    return true;
}

// ---------------------------------------------------------------- viewing

// A window on an unlit scene with the camera at the origin shows
// black, which is exactly what the first watchable run did. Light and
// frame it explicitly. The lights are queued in headless too, so both
// modes are the same world and a visual surprise means a real
// difference rather than a setup difference.
void light_and_frame(Engine& e, float x, float y, float ground) {
    // Height matters more than strength: at 25 m up, inverse-square
    // falloff left the whole scene at about 15% brightness.
    e.get_particle_system().queue_light(-6.0f, -8.0f, ground + 12.0f,
                                        4000000.0f, 200.0f,
                                        1.0f, 0.95f, 0.9f);
    e.get_particle_system().queue_light(10.0f, 6.0f, ground + 10.0f,
                                        1500000.0f, 200.0f,
                                        0.85f, 0.9f, 1.0f);
    if (!g_visual) return;
    auto& cam = e.get_camera_system();
    cam.set_position(x - 8.0f, y - 8.0f, ground + 10.0f);
    cam.look_at(x, y, ground + 1.0f);
    // Orthographic projection: moving the camera in does not make her
    // bigger, pixels-per-unit does. At the default 20 she is about 36
    // px tall, too small to watch a gait.
    cam.adjust_zoom(28.0f);
    e.set_camera_follow_enabled(true);
}

// The window is created unfocused and lands BEHIND the launching
// terminal, so the scenario runs where nobody can see it and every
// SPACE press goes to the shell. The engine logs it plainly:
// "Final window state: visible=1 focused=0". Raise it and take the
// keyboard, or "watchable" is a lie.
void bring_to_front(Engine& e);

GLFWwindow* window_of(Engine& e) {
    if (!g_visual || !e.get_platform()) return nullptr;
    return static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
}

void bring_to_front(Engine& e) {
    if (!g_visual) return;
    GLFWwindow* win = window_of(e);
    if (!win) return;
    glfwShowWindow(win);
    glfwRestoreWindow(win);
    // macOS focuses the process, not just the window, and a
    // CLI-launched app is not frontmost. Asking across a few pumped
    // frames is what makes it stick.
    for (int i = 0; i < 8; ++i) {
        glfwPollEvents();
        glfwFocusWindow(win);
    }
    std::cout << "    window focused: "
              << (glfwGetWindowAttrib(win, GLFW_FOCUSED) ? "yes"
                                                         : "NO - click it")
              << std::endl;
}

void draw(Engine& e, const Walker& w) {
    if (!g_visual) return;
    float hx = 0, hy = 0, hz = 0;
    {
        auto view = e.get_particle_system().lock_particles_for_read();
        if (w.hips >= 0 && static_cast<size_t>(w.hips) < view.size()) {
            hx = view[w.hips].x; hy = view[w.hips].y; hz = view[w.hips].z;
        }
    }
    // Rigid follow, not the deadzone follow. The deadzone is 5 m,
    // which at this zoom is a third of the viewport, so she walks off
    // the bottom of the frame and the thing being watched is the thing
    // not on screen.
    auto& cam = e.get_camera_system();
    cam.set_position(hx - 8.0f, hy - 8.0f, hz + 9.0f);
    cam.look_at(hx, hy, hz);
    e.render();
    e.present();
    if (auto* p = e.get_platform()) p->poll_events();
    if (GLFWwindow* win = window_of(e)) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) g_quit = true;
    }
}

// "Watchable" is a claim about pixels, and pixels are the only way to
// check it: the first version of this harness opened a window on an
// unlit scene at the wrong camera and reported everything green.
void shoot(Engine& e, const std::string& name) {
    const char* dir = std::getenv("LOGOSPHERE_SHOT");
    if (!dir || !g_visual) return;
    e.render();
    e.get_renderer().wait_for_completion();
    int w = 0, h = 0;
    std::vector<uint32_t> px(
        static_cast<size_t>(e.get_render_buffer().width()) *
        e.get_render_buffer().height());
    if (!e.read_latest_framebuffer(px.data(), w, h)) return;
    FILE* f = std::fopen((std::string(dir) + "/" + name + ".ppm").c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        const uint32_t p = px[i];
        unsigned char rgb[3] = {
            static_cast<unsigned char>((p >> 16) & 0xFF),
            static_cast<unsigned char>((p >> 8) & 0xFF),
            static_cast<unsigned char>(p & 0xFF)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

void wait_for_space(Engine& e, const Walker& w, const char* verdict) {
    if (!g_visual || g_quit) return;
    if (std::getenv("LOGOSPHERE_SHOT")) return;   // scripted: no waiting
    std::cout << "    [" << verdict
              << "]  SPACE for the next scenario, ESC to stop." << std::endl;
    GLFWwindow* win = window_of(e);
    if (!win) return;
    bool released = false;
    while (!g_quit) {
        draw(e, w);
        const bool space = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (!space) released = true;          // ignore a held-over press
        if (space && released) break;
        if (e.get_platform()->should_close()) { g_quit = true; break; }
    }
}

// ---------------------------------------------------------------- the walk

// What the whole path looked like, not just where it ended.
struct Result {
    bool  placed = false;
    float progress = 0.0f;        // straight-line distance travelled
    float start_z = 0.0f, end_z = 0.0f;

    // Registered against the real ground under her, every frame.
    int   frames = 0;
    int   frames_over_ground = 0; // the locator found a surface below
    float min_clearance =  1e9f;  // hips height above that surface
    float max_clearance = -1e9f;
    float biggest_hop = 0.0f;     // largest single-frame move

    // Knee length vs its length at rest. The joint should not stretch.
    float rest_knee = 0.0f;
    float worst_knee = 0.0f;      // largest absolute deviation, metres
    // Hips-to-foot length, frame over frame. A knee that visibly pops
    // is the hips moving while the foot is planted, which a joint
    // length alone cannot see: the chain is rebuilt by IK each frame,
    // so the bones stay the right length while the leg still snaps.
    float worst_leg_jerk = 0.0f;
    float closest_debris = 1e9f;   // nearest her feet came to the litter
    float worst_spread = 0.0f;    // bounding box diagonal of the body
};

float dist(const Particle& a, const Particle& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Result walk(Engine& e, Walker& w, float dir_x, float dir_y, float speed,
            int frames, const std::vector<unsigned int>& junk = {}) {
    Result r;
    r.placed = true;
    const bool tracing = std::getenv("LOGOSPHERE_TRACE") != nullptr;
    if (tracing) e.get_particle_tracer().trace(w.hips, "hips");

    auto& loco = e.get_humanoid_locomotion();
    // Face where she is going. Yaw is clockwise from +Y, so a heading
    // of (dx, dy) is atan2(dx, dy). Without this she keeps facing north
    // and slides sideways along her own path.
    loco.set_facing_direction(w.hips, std::atan2(dir_x, dir_y));
    loco.set_target_velocity(w.hips, dir_x * speed, dir_y * speed);

    float sx = 0, sy = 0, last_x = 0, last_z = 0;
    float last_lleg = -1.0f, last_rleg = -1.0f;
    {
        auto view = e.get_particle_system().lock_particles_for_read();
        sx = view[w.hips].x; sy = view[w.hips].y;
        r.start_z = view[w.hips].z;
        last_x = sx; last_z = r.start_z;
        if (w.l_thigh && w.l_shin)
            r.rest_knee = dist(view[w.l_thigh], view[w.l_shin]);
    }

    auto& locator = e.get_ground_locator();
    bool dumped = false;

    for (int f = 0; f < frames && !g_quit; ++f) {
        e.update(1.0 / 60.0);
        draw(e, w);

        auto view = e.get_particle_system().lock_particles_for_read();
        const Particle& h = view[w.hips];
        r.frames++;

        // Register her against the ground actually beneath her. The
        // lock-free overload, because the read lock is already held.
        float surface = 0.0f;
        if (locator.surface_at(h.x, h.y, surface, view.get(), 0.6f, &w.all)) {
            r.frames_over_ground++;
            const float clear = h.z - surface;
            r.min_clearance = std::min(r.min_clearance, clear);
            r.max_clearance = std::max(r.max_clearance, clear);
        }

        // Knees. Both legs, worst deviation from rest length.
        if (w.l_thigh && w.l_shin && r.rest_knee > 0.0f) {
            r.worst_knee = std::max(r.worst_knee,
                std::fabs(dist(view[w.l_thigh], view[w.l_shin]) - r.rest_knee));
            r.worst_knee = std::max(r.worst_knee,
                std::fabs(dist(view[w.r_thigh], view[w.r_shin]) - r.rest_knee));
        }

        for (unsigned int j : junk) {
            if (j >= view.size()) continue;
            if (w.l_foot) r.closest_debris =
                std::min(r.closest_debris, dist(view[w.l_foot], view[j]));
            if (w.r_foot) r.closest_debris =
                std::min(r.closest_debris, dist(view[w.r_foot], view[j]));
        }

        if (w.l_foot && w.r_foot) {
            const float lleg = dist(h, view[w.l_foot]);
            const float rleg = dist(h, view[w.r_foot]);
            if (last_lleg >= 0.0f) {
                r.worst_leg_jerk = std::max(r.worst_leg_jerk,
                                            std::fabs(lleg - last_lleg));
                r.worst_leg_jerk = std::max(r.worst_leg_jerk,
                                            std::fabs(rleg - last_rleg));
            }
            last_lleg = lleg; last_rleg = rleg;
        }

        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (unsigned int i : w.all) {
            if (i >= view.size()) continue;
            const Particle& p = view[i];
            lo[0] = std::min(lo[0], p.x); hi[0] = std::max(hi[0], p.x);
            lo[1] = std::min(lo[1], p.y); hi[1] = std::max(hi[1], p.y);
            lo[2] = std::min(lo[2], p.z); hi[2] = std::max(hi[2], p.z);
        }
        const float bx = hi[0]-lo[0], by = hi[1]-lo[1], bz = hi[2]-lo[2];
        r.worst_spread = std::max(r.worst_spread,
                                  std::sqrt(bx*bx + by*by + bz*bz));

        // A walker at 1.2 m/s covers 2 cm a frame. Anything near a
        // metre is not locomotion, it is someone writing a position.
        const float hop = std::fabs(h.x - last_x) + std::fabs(h.z - last_z);
        if (f > 0) r.biggest_hop = std::max(r.biggest_hop, hop);
        if (tracing) {
            if (f % 60 == 0)
                std::cout << "      t=" << (f / 60) << "s  x=" << h.x
                          << " z=" << h.z << " clear=" << (h.z - surface)
                          << std::endl;
            if (f > 0 && hop > 0.10f) {
                std::cout << "      JUMP f" << f << ": x " << last_x << " -> "
                          << h.x << ", z " << last_z << " -> " << h.z
                          << "  (" << hop << " m)" << std::endl;
                if (hop > 0.10f && !dumped) {
                    dumped = true;
                    e.get_particle_tracer().dump(std::cout, 3);
                }
            }
        }
        last_x = h.x; last_z = h.z;
    }
    {
        auto view = e.get_particle_system().lock_particles_for_read();
        const float dx = view[w.hips].x - sx, dy = view[w.hips].y - sy;
        r.progress = std::sqrt(dx*dx + dy*dy);
        r.end_z = view[w.hips].z;
    }
    return r;
}

Engine* make_engine() {
    auto* e = new Engine(nullptr);
    EngineConfig cfg;
    cfg.create_display = g_visual;
    cfg.window_width = 1000;
    cfg.window_height = 700;
    cfg.window_title = "humanoid terrain scenarios";
    cfg.show_debug_overlay = false;
    cfg.enable_chat_window = false;
    if (e->initialize(cfg) < 0) { delete e; return nullptr; }
    return e;
}

// Same convention as physics_guard_runner: a documented known-red is
// reported loudly on every run but does not gate, and if it ever
// starts passing the harness says so, so it gets promoted rather than
// quietly staying excused.
void check(bool ok, const std::string& msg, bool xfail = false) {
    if (ok) { tests_passed++; }
    else if (xfail) {
        tests_xfailed++;
        std::cout << "    XFAIL: " << msg << "  (known-red, not gating)"
                  << std::endl;
    }
    else { tests_failed++; std::cout << "    FAIL: " << msg << std::endl; }
}

}  // namespace

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::cout << "Humanoid terrain scenarios"
              << (g_visual ? "  [watchable: SPACE next, ESC stop]" : "")
              << std::endl;

    struct Case {
        const char* name;
        const char* watch;
        float ground;         // the ground she starts on. Never zero.
        float plateau_z;      // raised disc in the middle, 0 for none
        bool  outward;        // start on the plateau and walk off it
        int   litter_count;   // loose objects strewn along her path
        int   frames;
        float expect_rise;    // hips height change the terrain should give
        float rise_tol;
        float max_clear;      // ceiling on hips height above the surface
        // This case crosses a height change, so it trips the snap in
        // issue #30: step_climb.boost is cancelled by ground_correct
        // every frame, and the height is applied as a one-frame ground
        // snap that pops the legs. Only the smoothness checks are
        // excused; everything else on the case still gates.
        bool  snaps;
        bool  xfail;          // the whole case is known-red
        const char* issue;
    };
    const Case cases[] = {
        {"flat ground, well above zero",
         "she should simply walk, and hold her height",
         1.40f, 0.0f, false, 0, 480,   0.00f, 0.06f, 1.05f, false, false, nullptr},

        {"step up",
         "a 0.35 m rise: she should end up standing on top of it",
         1.40f, 1.75f, false, 0, 480, +0.35f, 0.14f, 1.40f, true, false, nullptr},

        {"step down",
         "a 0.40 m drop: she should step down, not fall over",
         1.40f, 1.80f, true,  0, 480, -0.40f, 0.14f, 1.45f, true, false, nullptr},

        {"litter underfoot",
         "loose objects on the ground: kicked aside, never a trap",
         1.40f, 0.0f, false, 10, 480,  0.00f, 0.20f, 1.25f, true, false, nullptr},

        // She is not stopped by this. She is fired through it. The hips
        // are written straight through the face by animation, and once
        // she is inside, every wall tile she overlaps contributes its
        // own depenetration push; the pushes sum, drive her deeper, and
        // reach still more tiles. Measured escalation, all forward:
        // 0.73, 1.16, 3.07, 3.73 m in consecutive frames, ending past
        // the far side and below z = 0. Issue #29.
        {"a wall too tall to climb",
         "a 1.0 m face: she should NOT get on top, and should survive it",
         0.80f, 1.80f, false, 0, 540,   0.00f, 0.15f, 1.05f, true,
         true, "#29 engine fires her through a solid wall"},
    };

    // Every feature met from several headings. None of these is an
    // axis the locomotion code was written along.
    struct Angle { const char* name; float deg; };
    const Angle angles[] = {
        {"from the south",     0.0f},
        {"from the southwest", 55.0f},
        {"from the west",     125.0f},
    };

    // LOGOSPHERE_ONLY=<substring> runs one scenario, for when a single
    // case is being traced.
    const char* only = std::getenv("LOGOSPHERE_ONLY");

    for (const Case& c : cases) {
        if (g_quit) break;
        if (only && std::string(c.name).find(only) == std::string::npos)
            continue;
        std::cout << "\n  " << c.name << (c.xfail ? "   [known-red: " : "")
                  << (c.xfail ? c.issue : "") << (c.xfail ? "]" : "")
                  << "\n    " << c.watch << std::endl;

        for (const Angle& a : angles) {
            if (g_quit) break;
            const float th = a.deg * 3.14159265f / 180.0f;
            // Unit vector she travels along.
            const float dx = std::sin(th), dy = std::cos(th);
            // Outward cases start near the middle and leave; inward
            // cases start out at the rim and come in.
            const float start_r = c.outward ? 1.0f : APPROACH_R;
            const float sx = -dx * start_r, sy = -dy * start_r;
            const float travel_x = c.outward ? dx : dx;
            const float travel_y = c.outward ? dy : dy;

            Engine* e = make_engine();
            if (!e) { check(false, "engine init"); continue; }

            terrain(*e, c.ground, c.plateau_z, PLATEAU_R);
            std::vector<unsigned int> junk;
            if (c.litter_count)
                junk = litter(*e, c.litter_count, c.ground, dx, dy);
            light_and_frame(*e, sx, sy, c.ground);
            for (int i = 0; i < 5; ++i) e->update(1.0 / 60.0);

            Walker w;
            if (!spawn_walker(*e, sx, sy, w)) {
                check(false, std::string(c.name) + " " + a.name +
                             ": could not place walker");
                e->shutdown(); delete e; continue;
            }
            bring_to_front(*e);
            for (int i = 0; i < 60; ++i) { e->update(1.0/60.0); draw(*e, w); }

            std::vector<float> junk_x, junk_y;
            {
                auto view = e->get_particle_system().lock_particles_for_read();
                for (unsigned int j : junk) {
                    junk_x.push_back(view[j].x);
                    junk_y.push_back(view[j].y);
                }
            }

            Result r = walk(*e, w, travel_x, travel_y, 1.2f, c.frames, junk);

            const float rise = r.end_z - r.start_z;
            const float over = r.frames ? (100.0f * r.frames_over_ground /
                                           r.frames) : 0.0f;
            std::cout << "    " << a.name << ": travelled " << r.progress
                      << " m, rise " << rise << " m, clearance "
                      << r.min_clearance << ".." << r.max_clearance
                      << " m over " << over << "% of the path"
                      << ", worst knee " << r.worst_knee
                      << ", leg jerk " << r.worst_leg_jerk
                      << " m, biggest hop " << r.biggest_hop << " m"
                      << std::endl;

            const std::string who = std::string(c.name) + " " + a.name;

            // She must actually go somewhere. This is the check whose
            // absence let the old test pass while she shuffled on the
            // spot.
            check(r.progress >= 3.0f,
                  who + ": must actually travel (went " +
                  std::to_string(r.progress) + " m)", c.xfail);

            // The terrain must do what the scenario says it does.
            check(std::fabs(rise - c.expect_rise) <= c.rise_tol,
                  who + ": height change should be " +
                  std::to_string(c.expect_rise) + " m, was " +
                  std::to_string(rise) + " m", c.xfail);

            // Registered against the ground the whole way: she is over
            // real ground, and she neither sinks into it nor floats.
            check(over >= 95.0f,
                  who + ": should be over ground for the whole path (was " +
                  std::to_string(over) + "%)", c.xfail);
            check(r.min_clearance > 0.55f,
                  who + ": never sinks toward the surface (closest " +
                  std::to_string(r.min_clearance) + " m)", c.xfail);
            check(r.max_clearance < c.max_clear,
                  who + ": never floats above it (highest " +
                  std::to_string(r.max_clearance) + " m, ceiling " +
                  std::to_string(c.max_clear) + ")", c.xfail || c.snaps);

            // Nobody writes her position but locomotion. She walks at
            // 1.2 m/s, which is 2 cm a frame; a tenth of a metre in one
            // frame is a write, not a stride.
            check(r.biggest_hop < 0.10f,
                  who + ": no teleporting (biggest single frame " +
                  std::to_string(r.biggest_hop) + " m)", c.xfail || c.snaps);

            // And the legs do not pop when her height changes.
            check(r.worst_leg_jerk < 0.10f,
                  who + ": legs do not snap (hips-to-foot moved " +
                  std::to_string(r.worst_leg_jerk) +
                  " m in one frame)", c.xfail || c.snaps);

            // And she stays assembled, knees included.
            check(r.worst_knee < 0.10f,
                  who + ": knees hold together (stretched " +
                  std::to_string(r.worst_knee) + " m past rest " +
                  std::to_string(r.rest_knee) + ")", c.xfail);
            check(r.worst_spread < 3.0f,
                  who + ": stayed whole (spread " +
                  std::to_string(r.worst_spread) + " m)", c.xfail);

            // A scenario that reports the same numbers as flat ground
            // is not testing anything. If she never disturbs the
            // debris she is walking through it, and "litter underfoot"
            // means nothing.
            if (!junk.empty()) {
                float moved = 0.0f;
                auto view = e->get_particle_system().lock_particles_for_read();
                for (size_t j = 0; j < junk.size(); ++j) {
                    const float ddx = view[junk[j]].x - junk_x[j];
                    const float ddy = view[junk[j]].y - junk_y[j];
                    moved = std::max(moved, std::sqrt(ddx*ddx + ddy*ddy));
                }
                std::cout << "      debris shifted " << moved
                          << " m; her feet came within "
                          << r.closest_debris << " m of it" << std::endl;
                check(moved > 0.05f,
                      who + ": she must actually meet the debris (it "
                      "shifted " + std::to_string(moved) +
                      " m; unmoved means she walked through it)", c.xfail);
            }

            std::string shot(std::string(c.name) + "_" + a.name);
            for (char& ch : shot) if (ch == ' ' || ch == ',') ch = '_';
            shoot(*e, shot);
            wait_for_space(*e, w, tests_failed ? "see failures" : "ok");
            e->shutdown();
            delete e;
        }
    }

    std::cout << "\n" << tests_passed << " passed, " << tests_failed
              << " failed";
    if (tests_xfailed)
        std::cout << ", " << tests_xfailed << " known-red (not gating)";
    else
        std::cout << "\n  every known-red scenario now passes: promote it.";
    std::cout << (g_quit ? "  (stopped early)" : "") << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
