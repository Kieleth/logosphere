// A predator hunting, live.
//
// The senses guard (test_predator_senses) proves each sense in
// isolation from a standing start. This one puts them together and
// lets it run: a predator that looks, chases what it sees, loses it
// behind a rock, goes to where it last saw it, and casts about when it
// gets there. Sight, smell, A* pathfinding and movement in one loop.
//
// That combination is the thing nothing in this engine guarded. GOAP
// and pathfinding each have their own test now, but "does a creature
// actually hunt" is a question about them working together over time,
// and it can only be answered by running.
//
// The interesting behaviour is the losing, not the chasing. A creature
// that always knows where its prey is does not need senses, does not
// need memory, and cannot be hidden from. Every assertion below is
// about the gap between what the world contains and what the predator
// knows.
//
//   ./build/test_predator_hunt                      numbers, asserted
//   LOGOSPHERE_VISUAL=1 ./build/test_predator_hunt  watch it, SPACE/ESC

#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "sense_system.h"
#include "npc-ai/pathfinding_system.h"
#include "ui/ui_system.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "particle.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "  FAIL: " << msg << std::endl; }           \
    } while (0)

namespace {

bool g_visual = false;
bool g_quit = false;

constexpr float PI_F = 3.14159265358979f;
constexpr int   FAN_POOL = 900;      // reused every frame, never grown

// What the predator is doing. Named for what it knows, not for a
// fantasy: these are the three states any hunter has.
enum class Hunt {
    CASTING,     // no idea where the prey is; sweeping for a scent
    STALKING,    // can see it right now
    SEARCHING,   // cannot see it, but remembers where it was
};

const char* hunt_name(Hunt h) {
    switch (h) {
        case Hunt::CASTING:   return "CASTING";
        case Hunt::STALKING:  return "STALKING";
        case Hunt::SEARCHING: return "SEARCHING";
    }
    return "?";
}

struct Trace {
    int frames = 0;
    int frames_seen = 0;
    int frames_searching = 0;
    float closest = 1e9f;
    float start_distance = 0.0f;
    bool  ever_saw = false;
    bool  ever_lost_after_seeing = false;
    bool  saw_through_boulder = false;   // must stay false
    float furthest_seen = 0.0f;
};

struct Hunt3D {
    Engine engine;
    SenseSystem senses;
    pathfinding::NavGrid grid;
    pathfinding::Pathfinder finder;

    int predator = -1;
    int prey = -1;
    std::vector<int> fan_pool;
    std::vector<int> boulders;

    float px = 0, py = 0, facing = 0.0f;
    pathfinding::Path path;
    Hunt state = Hunt::CASTING;
    float last_known_x = 0, last_known_y = 0;
    bool  has_memory = false;
    float cast_timer = 0.0f;

    explicit Hunt3D(bool with_display) : engine(nullptr) {
        EngineConfig cfg;
        cfg.create_display = with_display;
        cfg.window_width = 1100;
        cfg.window_height = 720;
        cfg.window_title = "a predator hunting";
        cfg.show_debug_overlay = false;
        cfg.enable_chat_window = false;
        if (engine.initialize(cfg) < 0)
            throw std::runtime_error("Engine::initialize() failed");
        grid.init(-40.0f, -40.0f, 40.0f, 40.0f, 1.0f);
    }
    ~Hunt3D() { engine.shutdown(); }

    int add(ParticleShape shape, float x, float y, float z, float size,
            float r, float g, float b, OdorType odor = OdorType::NONE) {
        Particle p{};
        p.shape = shape;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = size;
        p.size = size;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(shape == ParticleShape::BOX ? Materials::Type::STONE
                                                  : Materials::Type::FLESH);
        p.odor_type = odor;
        if (odor != OdorType::NONE) {
            p.odor_radius = 25.0f;
            p.odor_intensity = 1.0f;
        }
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        return engine.add_particle(p);
    }

    void boulder(float x, float y, float w, float d, float h) {
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = h * 0.5f;
        p.width = w; p.height = d; p.thickness = h;
        p.size = std::max(w, d);
        p.r = 0.45f; p.g = 0.43f; p.b = 0.40f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.odor_type = OdorType::NONE;
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        boulders.push_back(engine.add_particle(p));
        // The same rock, told to the navigator. Sight and navigation
        // must agree about the world or the creature walks into what
        // it can see.
        grid.set_blocked_rect(x - w * 0.5f - 0.6f, y - d * 0.5f - 0.6f,
                              x + w * 0.5f + 0.6f, y + d * 0.5f + 0.6f, true);
    }

    void ground() {
        if (!g_visual) return;
        for (float x = -40.0f; x <= 40.0f; x += 4.0f)
            for (float y = -40.0f; y <= 40.0f; y += 4.0f) {
                Particle p{};
                p.shape = ParticleShape::BOX;
                p.x = x; p.y = y; p.z = -0.25f;
                p.width = p.height = 4.0f; p.thickness = 0.5f; p.size = 4.0f;
                p.r = 0.38f; p.g = 0.36f; p.b = 0.33f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                p.solver_mode = ParticleSolverMode::KINEMATIC;
                p.is_at_rest = true;
                engine.add_particle(p);
            }
    }

    void make_fan_pool() {
        if (!g_visual) return;
        for (int i = 0; i < FAN_POOL; ++i)
            fan_pool.push_back(add(ParticleShape::SPHERE, 0, 0, -200.0f,
                                   0.42f, 0.25f, 0.85f, 0.35f));
    }

    void settle() { for (int i = 0; i < 4; ++i) engine.update(1.0 / 60.0); }

    // exclude_ids is documented as "self + body parts", and it means
    // it: every ray starts inside the predator's own 1.6 m sphere, so
    // without this the creature is blinded by its own body and sees
    // nothing anywhere. Its own debug markers go in here too.
    std::vector<int> blind_to() const {
        std::vector<int> v = fan_pool;
        if (predator >= 0) v.push_back(predator);
        return v;
    }

    SenseConfig eyes() const {
        SenseConfig c;
        c.fov_degrees = 100.0f;
        c.vision_range = 22.0f;
        c.ray_count = 32;
        return c;
    }
    SmellConfig nose() const {
        SmellConfig c;
        c.attracted_to = {OdorType::LIVING_FLESH};
        c.sensitivity = 1.0f;
        c.state_factor = 1.0f;
        return c;
    }

    void move_particle(int id, float x, float y) {
        auto w = engine.get_particle_system().lock_particles_for_write();
        auto& all = w.get_particles();
        if (id >= 0 && id < static_cast<int>(all.size())) {
            all[id].x = x; all[id].y = y;
        }
    }

    bool can_see_prey(float& out_x, float& out_y) {
        auto view = engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = engine.get_particle_system().get_shadow_bvh();
        if (!bvh) return false;
        SenseResult r = senses.cast_vision_cone(eyes(), px, py, 1.0f, facing,
                                                view.get(), *bvh,
                                                {prey}, blind_to());
        if (const SenseTarget* t = r.find_by_id(prey)) {
            out_x = t->x; out_y = t->y;
            return true;
        }
        return false;
    }

    // Redraw the visibility fan by MOVING a fixed pool of markers, so
    // the picture animates without the particle count growing.
    void refresh_fan() {
        if (!g_visual || fan_pool.empty()) return;
        std::vector<std::pair<float,float>> pts;
        {
            auto view = engine.get_particle_system().lock_particles_for_read();
            const BVH* bvh = engine.get_particle_system().get_shadow_bvh();
            if (!bvh) return;
            const float half = 50.0f * PI_F / 180.0f;
            const int rays = 30;
            for (int i = 0; i < rays && pts.size() < fan_pool.size(); ++i) {
                const float t = static_cast<float>(i) / (rays - 1);
                const float a = facing - half + t * 2.0f * half;
                const float dx = std::sin(a), dy = std::cos(a);
                for (float d = 1.2f; d <= 22.0f; d += 1.1f) {
                    if (pts.size() >= fan_pool.size()) break;
                    const float qx = px + dx * d, qy = py + dy * d;
                    if (!senses.can_see_point(px, py, 1.0f, qx, qy, 1.0f,
                                              view.get(), *bvh, blind_to()))
                        break;
                    pts.emplace_back(qx, qy);
                }
            }
        }
        auto w = engine.get_particle_system().lock_particles_for_write();
        auto& all = w.get_particles();
        for (size_t i = 0; i < fan_pool.size(); ++i) {
            Particle& p = all[fan_pool[i]];
            if (i < pts.size()) {
                p.x = pts[i].first; p.y = pts[i].second; p.z = 0.3f;
                // Colour says what the predator is doing.
                if (state == Hunt::STALKING) { p.r = 1.0f; p.g = 0.45f; p.b = 0.2f; }
                else if (state == Hunt::SEARCHING) { p.r = 0.95f; p.g = 0.85f; p.b = 0.25f; }
                else { p.r = 0.25f; p.g = 0.85f; p.b = 0.35f; }
            } else {
                p.z = -200.0f;                 // parked out of sight
            }
        }
    }

    // One tick of the hunt. This is the whole creature.
    void tick(float dt, Trace& tr) {
        float seen_x = 0, seen_y = 0;
        const bool seen = can_see_prey(seen_x, seen_y);

        tr.frames++;
        if (seen) {
            tr.frames_seen++;
            tr.ever_saw = true;
            last_known_x = seen_x; last_known_y = seen_y;
            has_memory = true;
            state = Hunt::STALKING;
        } else if (has_memory) {
            if (state == Hunt::STALKING) tr.ever_lost_after_seeing = true;
            state = Hunt::SEARCHING;
        } else {
            state = Hunt::CASTING;
        }
        if (state == Hunt::SEARCHING) tr.frames_searching++;

        // Where to go: what it can see, else what it remembers, else a
        // scent, else nothing.
        float goal_x = px, goal_y = py;
        bool have_goal = false;
        if (state == Hunt::STALKING || state == Hunt::SEARCHING) {
            goal_x = last_known_x; goal_y = last_known_y;
            have_goal = true;
        } else {
            auto view = engine.get_particle_system().lock_particles_for_read();
            SmellResult sm = senses.check_smell(nose(), px, py, 1.0f, 1.0f,
                                                view.get(), blind_to());
            if (sm.detected) {
                goal_x = px + std::sin(sm.direction) * 6.0f;
                goal_y = py + std::cos(sm.direction) * 6.0f;
                have_goal = true;
            }
        }

        // Arrived at a memory with nothing in sight: the trail is cold.
        if (state == Hunt::SEARCHING &&
            std::hypot(last_known_x - px, last_known_y - py) < 1.5f) {
            has_memory = false;
            state = Hunt::CASTING;
            have_goal = false;
        }

        if (have_goal) {
            path = finder.find_path(grid, {px, py}, {goal_x, goal_y});
            if (path.valid && !path.empty()) {
                pathfinding::Point wp = path.current();
                if (std::hypot(wp.x - px, wp.y - py) < 0.6f && path.size() > 1)
                    { path.advance(); wp = path.current(); }
                const float dx = wp.x - px, dy = wp.y - py;
                const float len = std::hypot(dx, dy);
                if (len > 0.01f) {
                    const float speed = 5.0f;
                    px += (dx / len) * speed * dt;
                    py += (dy / len) * speed * dt;
                    facing = std::atan2(dx, dy);   // 0 = +Y
                }
            }
        } else {
            cast_timer += dt;                      // sweep, looking about
            facing += dt * 1.1f;
        }

        move_particle(predator, px, py);

        const float d = std::hypot(px - prey_x(), py - prey_y());
        tr.closest = std::min(tr.closest, d);
        if (seen) tr.furthest_seen = std::max(tr.furthest_seen, d);
    }

    float prey_x() const { return prey_pos_x; }
    float prey_y() const { return prey_pos_y; }
    float prey_pos_x = 0, prey_pos_y = 0;

    void move_prey(float x, float y) {
        prey_pos_x = x; prey_pos_y = y;
        move_particle(prey, x, y);
    }
};

// ---------------------------------------------------------------- viewing

void draw_caption(Engine& e, const std::vector<std::string>& lines) {
    auto* ui = e.get_ui_system();
    if (!ui) return;
    int y = 18;
    for (const std::string& l : lines) {
        const bool key = !l.empty() && l[0] == '>';
        ui->draw_text(20, y, key ? l.substr(1) : l,
                      key ? 255 : 225, key ? 235 : 225, key ? 140 : 225);
        y += 15;
    }
}

void bring_to_front(Engine& e) {
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return;
    glfwShowWindow(win);
    glfwRestoreWindow(win);
    for (int i = 0; i < 8; ++i) { glfwPollEvents(); glfwFocusWindow(win); }
    std::cout << "    window focused: "
              << (glfwGetWindowAttrib(win, GLFW_FOCUSED) ? "yes"
                                                         : "NO - click it")
              << std::endl;
}

bool pump(Engine& e) {
    e.get_platform()->poll_events();
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return true;
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; return false; }
    if (e.get_platform()->should_close()) { g_quit = true; return false; }
    return true;
}

}  // namespace

// ---------------------------------------------------------------- the hunt

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::srand(20260802u);
    std::cout << "A predator hunting (senses + pathfinding, over time)"
              << std::endl;

    Hunt3D h(g_visual);
    h.ground();

    // A pillar of rock the prey can break line of sight behind. Placed
    // between the two of them, off to one side, so the prey has to be
    // driven behind it rather than starting hidden.
    h.boulder(6.0f, 10.0f, 7.0f, 2.0f, 5.0f);

    h.predator = h.add(ParticleShape::SPHERE, -14.0f, -6.0f, 1.0f, 1.6f,
                       0.95f, 0.75f, 0.2f);
    h.prey = h.add(ParticleShape::SPHERE, 0.0f, 0.0f, 1.0f, 2.0f,
                   0.85f, 0.25f, 0.25f, OdorType::LIVING_FLESH);
    h.make_fan_pool();
    h.settle();

    h.px = -14.0f; h.py = -6.0f; h.facing = std::atan2(14.0f, 6.0f);
    h.move_prey(0.0f, 0.0f);

    if (g_visual) {
        h.engine.get_particle_system().queue_light(-10.0f, -25.0f, 45.0f,
                                                   26000000.0f, 700.0f,
                                                   1.0f, 0.95f, 0.9f);
        h.engine.get_particle_system().queue_light(20.0f, 20.0f, 40.0f,
                                                   16000000.0f, 700.0f,
                                                   0.85f, 0.9f, 1.0f);
        auto& cam = h.engine.get_camera_system();
        cam.set_pixels_per_unit(19.0f);
        cam.set_position(-14.0f, -20.0f, 16.0f);
        cam.look_at(2.0f, 2.0f, 1.0f);
        for (int i = 0; i < 6; ++i) h.engine.update(1.0 / 60.0);
        bring_to_front(h.engine);
    }

    Trace tr;
    {
        tr.start_distance = std::hypot(h.px - 0.0f, h.py - 0.0f);
    }

    // The prey walks a path that takes it BEHIND the pillar and out the
    // far side. The predator is not told any of this.
    const int TOTAL = 900;                    // 15 s at 60 Hz
    const float dt = 1.0f / 60.0f;
    int lost_at_frame = -1;

    for (int f = 0; f < TOTAL && !g_quit; ++f) {
        // Prey route: east, then north behind the pillar, then east again.
        float t = static_cast<float>(f) / TOTAL;
        float ax, ay;
        if (t < 0.35f)      { ax = 0.0f + t / 0.35f * 6.0f;  ay = 0.0f + t / 0.35f * 6.0f; }
        else if (t < 0.62f) { float u = (t - 0.35f) / 0.27f; ax = 6.0f + u * 3.0f; ay = 6.0f + u * 7.0f; }
        else                { float u = (t - 0.62f) / 0.38f; ax = 9.0f + u * 10.0f; ay = 13.0f - u * 2.0f; }
        h.move_prey(ax, ay);

        const Hunt before = h.state;
        h.tick(dt, tr);
        if (before == Hunt::STALKING && h.state == Hunt::SEARCHING &&
            lost_at_frame < 0)
            lost_at_frame = f;

        // Occlusion honesty, checked every frame it claims sight.
        //
        // NOT "is the prey's centre visible": the prey is 2 m across,
        // so the cone can legitimately catch an edge of it while a ray
        // to the centre is blocked. Seeing part of something is seeing
        // it. The real invariant is that it never reports prey that is
        // FULLY hidden, so this samples across the body and only
        // objects if every one of those points is blocked.
        if (h.state == Hunt::STALKING) {
            auto view = h.engine.get_particle_system().lock_particles_for_read();
            const BVH* bvh = h.engine.get_particle_system().get_shadow_bvh();
            if (bvh) {
                // The PREY must be excluded as well as the predator.
                // can_see_point is a shadow ray, so the prey's own
                // surface sits between the viewer and the prey's
                // centre and blocks it every single time. Asking "can
                // I see the middle of that animal" is always no.
                // Excluding it asks the question that was meant: is
                // the line clear of everything ELSE.
                std::vector<int> ignore = h.blind_to();
                ignore.push_back(h.prey);
                const float ox[5] = {0.0f, 0.9f, -0.9f, 0.0f,  0.0f};
                const float oy[5] = {0.0f, 0.0f,  0.0f, 0.9f, -0.9f};
                bool any_visible = false;
                for (int k = 0; k < 5 && !any_visible; ++k)
                    if (h.senses.can_see_point(h.px, h.py, 1.0f,
                                               h.prey_x() + ox[k],
                                               h.prey_y() + oy[k], 1.0f,
                                               view.get(), *bvh, ignore))
                        any_visible = true;
                if (!any_visible) tr.saw_through_boulder = true;
            }
        }

        if (g_visual) {
            h.refresh_fan();
            h.engine.update(dt);
            h.engine.render();
            char l1[128], l2[128], l3[128];
            std::snprintf(l1, sizeof(l1), ">A PREDATOR HUNTING    t = %.1fs", f * dt);
            std::snprintf(l2, sizeof(l2), "state %-10s   distance to prey %.1f m",
                          hunt_name(h.state), std::hypot(h.px - h.prey_x(), h.py - h.prey_y()));
            std::snprintf(l3, sizeof(l3), "seen on %d of %d frames        closest so far %.1f m",
                          tr.frames_seen, tr.frames, tr.closest);
            draw_caption(h.engine, {
                l1, "",
                l2, l3, "",
                ">GREEN cone = casting about, no idea where the prey is",
                ">ORANGE     = it can see the prey right now",
                ">YELLOW     = lost sight, walking to where it last saw it",
                "",
                "The cone is drawn with can_see_point, so the shadow",
                "behind the rock is the real occlusion result.",
                "",
                "ESC to stop"});
            h.engine.present();
            if (!pump(h.engine)) break;
        } else {
            h.engine.update(dt);
        }
    }

    const float ended = std::hypot(h.px - h.prey_x(), h.py - h.prey_y());
    std::cout << "\n  [measure] " << tr.frames << " frames. saw prey on "
              << tr.frames_seen << ", searched on " << tr.frames_searching
              << std::endl;
    std::cout << "  [measure] distance: started " << tr.start_distance
              << " m, closest " << tr.closest << " m, ended " << ended
              << " m" << std::endl;
    std::cout << "  [measure] first lost sight at frame " << lost_at_frame
              << std::endl;

    // It has to actually hunt.
    CHECK(tr.ever_saw, "the predator sees the prey at some point");
    CHECK(tr.closest < tr.start_distance - 4.0f,
          "and closes on it (started " + std::to_string(tr.start_distance) +
          " m, closest " + std::to_string(tr.closest) + " m)");

    // And it has to be capable of NOT knowing, which is the whole point.
    CHECK(tr.frames_seen < tr.frames,
          "there are frames where it cannot see the prey (" +
          std::to_string(tr.frames_seen) + " of " +
          std::to_string(tr.frames) + ")");
    CHECK(tr.ever_lost_after_seeing,
          "it loses sight after having had it, rather than knowing "
          "for ever once it has looked");
    CHECK(tr.frames_searching > 0,
          "and it goes looking, using a memory rather than a live feed (" +
          std::to_string(tr.frames_searching) + " frames)");

    // The hard invariant: it never claims to see through rock.
    CHECK(!tr.saw_through_boulder,
          "it never sees the prey through the boulder");

    std::cout << "\n" << tests_passed << " passed, " << tests_failed
              << " failed" << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
