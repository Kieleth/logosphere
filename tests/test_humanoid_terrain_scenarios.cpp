// One humanoid, one situation at a time.
//
// This replaces the way test_humanoid_strata_walk was being read. That
// test drives Eva AND several NPCs with identical velocity commands,
// so they bunch and jam into each other; its legs claim to climb a
// staircase and sprint 100 m, but she never leaves a 7 m box. It
// passes anyway, because it only asks whether she came apart. It is a
// dismemberment guard wearing a locomotion guard's name.
//
// So this one is deliberately the opposite:
//
//   ONE humanoid. Nobody to collide with, nothing to blame.
//   ONE terrain feature per scenario, built from scratch each time.
//   PROGRESS IS ASSERTED. If she does not go anywhere, it fails. That
//     is the check whose absence let the old test pass while she
//     shuffled on the spot.
//   NO ABSOLUTE HEIGHTS. Every scenario sits at a different, awkward
//     ground level, and she is placed by asking the ground locator.
//
// Watchable: LOGOSPHERE_VISUAL=1 opens a window, follows her, and
// waits at the end of each scenario. SPACE moves to the next one,
// ESC stops. Headless and silent otherwise, which is what CI runs.
//
//   ./build/test_humanoid_terrain_scenarios
//   LOGOSPHERE_VISUAL=1 ./build/test_humanoid_terrain_scenarios

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
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

bool g_visual = false;
bool g_quit = false;

// ---------------------------------------------------------------- world

// A slab of ground. Kinematic, because terrain owns its position.
void slab(Engine& e, float cx, float cy, float top_z,
          float size_x, float size_y, float thickness = 0.5f) {
    const float tile = 2.0f;
    for (float x = cx - size_x * 0.5f; x < cx + size_x * 0.5f; x += tile) {
        for (float y = cy - size_y * 0.5f; y < cy + size_y * 0.5f; y += tile) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.x = x + tile * 0.5f;
            p.y = y + tile * 0.5f;
            p.z = top_z - thickness * 0.5f;
            p.width = p.height = tile;
            p.thickness = thickness;
            p.size = tile;
            p.r = 0.45f; p.g = 0.42f; p.b = 0.38f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            p.solver_mode = ParticleSolverMode::KINEMATIC;
            p.is_at_rest = true;
            e.add_particle(p);
        }
    }
}

struct Walker {
    int hips = -1;
    std::vector<unsigned int> all;
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
    return true;
}

// ---------------------------------------------------------------- viewing

GLFWwindow* window_of(Engine& e) {
    if (!g_visual || !e.get_platform()) return nullptr;
    return static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
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
    e.get_camera_system().update_follow_target(hx, hy, hz);
    e.render();
    e.present();
    if (auto* p = e.get_platform()) p->poll_events();
    if (GLFWwindow* win = window_of(e)) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) g_quit = true;
    }
}

// Hold at the end of a scenario so it can be looked at. SPACE goes on,
// ESC stops. Headless runs straight through.
void wait_for_space(Engine& e, const Walker& w, const char* verdict) {
    if (!g_visual || g_quit) return;
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

// ---------------------------------------------------------------- scenario

struct Result {
    bool placed = false;
    float progress = 0.0f;      // distance actually travelled, metres
    float start_z = 0.0f, end_z = 0.0f;
    float min_hips_z = 1e9f, max_hips_z = -1e9f;
    float worst_spread = 0.0f;  // bounding box diagonal of the body
};

Result walk(Engine& e, Walker& w, float vx, float vy, int frames) {
    Result r;
    r.placed = true;
    const bool tracing = std::getenv("LOGOSPHERE_TRACE") != nullptr;
    if (tracing) e.get_particle_tracer().trace(w.hips, "hips");
    float last_x = 0.0f, last_z = 0.0f;
    bool jumped = false;
    float sx = 0, sy = 0;
    {
        auto view = e.get_particle_system().lock_particles_for_read();
        sx = view[w.hips].x; sy = view[w.hips].y;
        r.start_z = view[w.hips].z;
    }
    e.get_humanoid_locomotion().set_target_velocity(w.hips, vx, vy);

    for (int f = 0; f < frames && !g_quit; ++f) {
        e.update(1.0 / 60.0);
        draw(e, w);
        auto view = e.get_particle_system().lock_particles_for_read();
        const float hz = view[w.hips].z;
        r.min_hips_z = std::min(r.min_hips_z, hz);
        r.max_hips_z = std::max(r.max_hips_z, hz);
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (unsigned int i : w.all) {
            if (i >= view.size()) continue;
            const Particle& p = view[i];
            lo[0] = std::min(lo[0], p.x); hi[0] = std::max(hi[0], p.x);
            lo[1] = std::min(lo[1], p.y); hi[1] = std::max(hi[1], p.y);
            lo[2] = std::min(lo[2], p.z); hi[2] = std::max(hi[2], p.z);
        }
        const float dx = hi[0]-lo[0], dy = hi[1]-lo[1], dz = hi[2]-lo[2];
        r.worst_spread = std::max(r.worst_spread,
                                  std::sqrt(dx*dx + dy*dy + dz*dz));
        // LOGOSPHERE_TRACE=1 prints the path once a second. For when a
        // scenario ends somewhere impossible and the end state alone
        // does not say how it got there.
        if (tracing) {
            const Particle& h = view[w.hips];
            if (f % 60 == 0)
                std::cout << "      t=" << (f / 60) << "s  x=" << h.x
                          << " z=" << h.z << "  v=(" << h.vx << "," << h.vz
                          << ")" << std::endl;
            // A walker moving 1.2 m/s covers 2 cm a frame. Anything
            // near a metre is not locomotion, it is someone writing a
            // position. Catch it in the act and ask the tracer who.
            const float step = std::fabs(h.x - last_x) +
                               std::fabs(h.z - last_z);
            if (f > 0 && step > 0.5f) {
                std::cout << "      JUMP at frame " << f << ": x " << last_x
                          << " -> " << h.x << ", z " << last_z << " -> "
                          << h.z << "  (" << step << " m)" << std::endl;
                if (step > 2.0f && !jumped) {   // dump for the big one
                    jumped = true;
                    e.get_particle_tracer().dump(std::cout, 2);
                }
            }
            last_x = h.x; last_z = h.z;
        }
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
// reported loudly every run but does not gate, and if it ever starts
// passing the harness says so, so it gets promoted rather than
// quietly staying excused.
int xfailed = 0;

void check(bool ok, const std::string& msg, bool xfail = false) {
    if (ok) { tests_passed++; }
    else if (xfail) {
        xfailed++;
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

    // Every scenario names a ground height that no constant would have
    // guessed, and none of them is zero.
    struct Case {
        const char* name;
        const char* watch;
        float ground;         // height of the ground she starts on
        float step_at;        // x of a terrain change (0 = none)
        float step_z;         // height of that change
        float vx, vy;
        int frames;
        float min_progress;   // she must actually go somewhere
        // What the terrain should DO to her. Asserting progress alone
        // is how a scenario passes while she walks along the face of a
        // wall she was supposed to climb.
        float expect_rise;    // hips height change she should end with
        float rise_tol;
        // A scenario the engine does not survive yet. Reported every
        // run, never gating, never deleted.
        bool xfail;
        const char* issue;
    };
    const Case cases[] = {
        {"flat ground, well above zero",
         "she should simply walk, and hold her height",
         1.40f, 0.0f, 0.0f,  1.2f, 0.0f, 600, 3.0f,   0.00f, 0.05f,
         false, nullptr},

        {"step up",
         "a 0.35 m rise: she should end up standing on top of it",
         1.40f, 6.0f, 1.75f, 1.2f, 0.0f, 720, 3.0f,  +0.35f, 0.12f,
         false, nullptr},

        {"step down",
         "a 0.40 m drop: she should step down, not fall over",
         1.80f, 6.0f, 1.40f, 1.2f, 0.0f, 720, 3.0f,  -0.40f, 0.12f,
         false, nullptr},

        // She is not stopped by this. She is fired through it. The
        // hips are written straight through the face by animation, and
        // once she is inside, every wall tile she overlaps contributes
        // its own depenetration push; the pushes sum, drive her deeper,
        // and reach more tiles. Measured escalation, all forward:
        // 0.73, 1.16, 3.07, 3.07, 3.73, 3.73 m in consecutive frames,
        // ending past the far side of a 12 m wall and below z = 0.
        // LOGOSPHERE_TRACE=1 prints the path and the tracer dump.
        {"a wall too tall to climb",
         "a 1.0 m face: she should NOT get on top, and should survive "
         "walking into it",
         0.80f, 6.0f, 1.80f, 1.2f, 0.0f, 900, 2.5f,   0.00f, 0.15f,
         true, "#29 engine fires her through a solid wall"},
    };

    for (const Case& c : cases) {
        if (g_quit) break;
        std::cout << "\n  " << c.name << (c.xfail ? "   [known-red: " : "")
                  << (c.xfail ? c.issue : "") << (c.xfail ? "]" : "")
                  << "\n    " << c.watch << std::endl;

        Engine* e = make_engine();
        if (!e) { check(false, "engine init"); continue; }

        // Ground she starts on, then the feature ahead of her.
        slab(*e, 0.0f, 0.0f, c.ground, 16.0f, 8.0f);
        if (c.step_at != 0.0f) {
            // The feature must reach DOWN to the ground she is on. A
            // default thickness leaves a gap under a tall rise, which
            // makes it an overhanging shelf rather than a step - she
            // then walks into the space beneath it and the scenario
            // measures something nobody described.
            const float thick = std::max(0.5f, (c.step_z - c.ground) + 0.8f);
            slab(*e, c.step_at + 5.0f, 0.0f, c.step_z, 12.0f, 8.0f, thick);
        }
        for (int i = 0; i < 5; ++i) e->update(1.0 / 60.0);

        Walker w;
        if (!spawn_walker(*e, -5.0f, 0.0f, w)) {
            check(false, std::string(c.name) + ": could not place walker");
            e->shutdown(); delete e; continue;
        }
        for (int i = 0; i < 60; ++i) { e->update(1.0 / 60.0); draw(*e, w); }

        Result r = walk(*e, w, c.vx, c.vy, c.frames);

        std::cout << "    travelled " << r.progress << " m, hips "
                  << r.start_z << " -> " << r.end_z << " (range "
                  << r.min_hips_z << ".." << r.max_hips_z
                  << "), worst spread " << r.worst_spread << " m"
                  << std::endl;

        // The assertion the old test never made.
        check(r.progress >= c.min_progress,
              std::string(c.name) + ": she must actually travel (went " +
              std::to_string(r.progress) + " m, needed " +
              std::to_string(c.min_progress) + ")", c.xfail);
        // She stays on her feet: hips never near the ground she is on.
        check(r.min_hips_z > c.ground + 0.3f,
              std::string(c.name) + ": stayed upright (lowest hips " +
              std::to_string(r.min_hips_z) + ", ground " +
              std::to_string(c.ground) + ")", c.xfail);
        // Did the terrain do what the scenario says it does? A step up
        // must lift her; a wall must not.
        const float rise = r.end_z - r.start_z;
        check(std::fabs(rise - c.expect_rise) <= c.rise_tol,
              std::string(c.name) + ": height change should be " +
              std::to_string(c.expect_rise) + " m, was " +
              std::to_string(rise) + " m", c.xfail);
        // And in one piece.
        check(r.worst_spread < 3.0f,
              std::string(c.name) + ": stayed whole (spread " +
              std::to_string(r.worst_spread) + " m)", c.xfail);

        wait_for_space(*e, w, tests_failed == 0 ? "ok" : "see failures above");
        e->shutdown();
        delete e;
    }

    std::cout << "\n" << tests_passed << " passed, " << tests_failed
              << " failed";
    if (xfailed) std::cout << ", " << xfailed << " known-red (not gating)";
    else std::cout << "\n  every known-red scenario now passes: promote it "
                      "to gating.";
    std::cout << (g_quit ? "  (stopped early)" : "") << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
