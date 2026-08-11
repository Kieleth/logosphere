// A predator's senses: what it can and cannot know.
//
// src/sense_system.{h,cpp} is compiled into the engine and had no
// tests. Its own header says "Core sensing in logosphere (reusable),
// game config in logomancers", which is the right split, so this
// guards the reusable half with a creature that belongs to no genre:
// a predator, some prey, a rock in the way.
//
// The behaviour is lifted from the shambler demos in logomancers
// rather than the shambler itself. Those eight files turned out to
// have ZERO assertions between them and an infinite loop each: they
// are watchable demos, not tests, and exit 0 whether the creature
// hunted perfectly or stood still. What they encode that IS worth
// keeping is the scenarios, so the scenarios are what came across.
//
// The property that matters most here is the one a shortcut destroys.
// A creature that reads its target's position straight out of the
// particle array knows everything: through walls, from any distance,
// facing the other way. Senses are what make a creature's knowledge
// smaller than the world, and smaller knowledge is the whole reason
// hiding, stalking and ambush exist. Each test below is a thing the
// predator must NOT be able to know.
//
// Usage:
//   ./build/test_predator_senses

#include "core/engine.h"
#include "core/particle_system.h"
#include "sense_system.h"
#include "particle.h"
#include "core/camera_system.h"
#include "ui/ui_system.h"
#include "logosphere/rendering/pixel_buffer.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static bool g_visual = false;
static bool g_quit = false;

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "  FAIL: " << msg << std::endl; }           \
    } while (0)

namespace {

constexpr float PI_F = 3.14159265358979f;

// Facing convention, from CLAUDE.md: yaw 0 is +Y (north), +pi/2 is +X
// (east), clockwise viewed from +Z.
constexpr float NORTH = 0.0f;
constexpr float SOUTH = PI_F;

struct World {
    Engine engine;
    SenseSystem senses;

    // Default headless: the asserting tests each build their own World
    // and must not each open a window. Only the watchable scenes ask
    // for a display.
    explicit World(bool with_display = false) : engine(nullptr) {
        EngineConfig cfg;
        cfg.create_display = with_display;
        cfg.window_width = 1100;
        cfg.window_height = 720;
        cfg.window_title = "predator senses";
        cfg.show_debug_overlay = false;
        cfg.enable_chat_window = false;
        if (engine.initialize(cfg) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
    }
    ~World() { engine.shutdown(); }

    // Something to be seen or smelled.
    // 2 m across, deliberately. cast_vision_cone SAMPLES the cone with
    // a fixed number of rays: 32 rays over 100 degrees is 3.2 degrees
    // apart, which at 10 m is 0.56 m between neighbouring rays. A 0.5 m
    // animal falls BETWEEN the rays and is invisible at that range,
    // which is a property of the sensor rather than a bug, but it makes
    // a 0.5 m target a coin toss and a coin toss is not a test.
    //
    // Height matters for the same reason: of the six pitch angles, one
    // is horizontal and five aim DOWNWARD ({0, -0.15 ... -0.65}), tuned
    // by their own comment for "small ground objects" seen from a
    // standing creature. Prey at the viewer's eye height is served by
    // exactly one of the six.
    int prey(float x, float y, float z = 1.0f,
             OdorType odor = OdorType::LIVING_FLESH) {
        Particle p{};
        p.shape = ParticleShape::SPHERE;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = 2.0f;
        p.size = 2.0f;
        p.r = 0.8f; p.g = 0.3f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::FLESH);
        p.odor_type = odor;
        // Both are required, and both default to 0. check_smell skips
        // any particle with odor_radius <= 0, so an odour type alone
        // emits nothing at all.
        p.odor_radius = 20.0f;
        p.odor_intensity = 1.0f;
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        return engine.add_particle(p);
    }

    // Something to hide behind. Odourless, so it cannot be confused
    // with prey by the smell test.
    int boulder(float x, float y, float w, float d, float h) {
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
        return engine.add_particle(p);
    }

    // Let the BVH catch up with what was just added.
    void settle() { for (int i = 0; i < 3; ++i) engine.update(1.0 / 60.0); }

    SenseResult look(const SenseConfig& cfg, float from_x, float from_y,
                     float facing, const std::vector<int>& exclude = {}) {
        auto view = engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = engine.get_particle_system().get_shadow_bvh();
        if (!bvh) return {};
        return senses.cast_vision_cone(cfg, from_x, from_y, 1.0f, facing,
                                       view.get(), *bvh, {}, exclude);
    }

    SmellResult sniff(const SmellConfig& cfg, float x, float y,
                      float discernment = 1.0f,
                      const std::vector<int>& exclude = {}) {
        auto view = engine.get_particle_system().lock_particles_for_read();
        return senses.check_smell(cfg, x, y, 1.0f, discernment,
                                  view.get(), exclude);
    }

    // Smell is a rand() roll against
    //   P = intensity * (1 - (d/r)^2) * state_factor * sensitivity
    // so a single sniff proves nothing either way. Sniff repeatedly and
    // measure the rate, which is the thing the model actually claims.
    float sniff_rate(const SmellConfig& cfg, float x, float y, int n = 400) {
        int hits = 0;
        for (int i = 0; i < n; ++i)
            if (sniff(cfg, x, y).detected) ++hits;
        return static_cast<float>(hits) / static_cast<float>(n);
    }
};

// A predator: a wide, long-range hunter's eye. Deliberately NOT one of
// the archetype presets, because a test that uses a preset only proves
// that preset works.
SenseConfig predator_eyes() {
    SenseConfig c;
    c.fov_degrees = 100.0f;
    c.vision_range = 25.0f;
    c.ray_count = 32;
    c.vision_quality = 1.0f;
    return c;
}

SmellConfig predator_nose() {
    SmellConfig c;
    c.sensitivity = 1.0f;
    c.state_factor = 1.0f;
    c.attracted_to = {OdorType::LIVING_FLESH, OdorType::BLOOD};
    return c;
}

// ---------------------------------------------------------------- sight

void test_it_sees_what_is_in_front_of_it() {
    World w;
    const int p = w.prey(0.0f, 10.0f);       // due north, well in range
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    std::cout << "  [measure] prey 10 m due north: " << r.visible_targets.size()
              << " target(s) visible" << std::endl;
    CHECK(r.any_visible(), "prey straight ahead is seen");
    CHECK(r.find_by_id(p) != nullptr, "and it is the right particle");
    if (const SenseTarget* t = r.find_by_id(p)) {
        std::cout << "  [measure] reported distance " << t->distance
                  << " m, angle offset " << t->angle_offset << " rad"
                  << std::endl;
        CHECK(std::fabs(t->distance - 10.0f) < 1.0f,
              "at roughly the right distance (" +
              std::to_string(t->distance) + ")");
        CHECK(std::fabs(t->angle_offset) < 0.15f,
              "and dead ahead (" + std::to_string(t->angle_offset) + " rad)");
    }
}

// The first thing a shortcut throws away: a predator facing away
// should not know what is behind it.
void test_it_cannot_see_behind_itself() {
    World w;
    const int p = w.prey(0.0f, 10.0f);       // north
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, SOUTH);   // looking away
    std::cout << "  [measure] facing south, prey to the north: "
              << r.visible_targets.size() << " visible" << std::endl;
    CHECK(r.find_by_id(p) == nullptr,
          "prey behind the predator is not seen");
}

void test_range_is_a_real_limit() {
    World w;
    const int near_prey = w.prey(0.0f, 10.0f);
    const int far_prey  = w.prey(0.0f, 40.0f);   // past a 25 m range
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    std::cout << "  [measure] range 25 m: prey at 10 m seen="
              << (r.find_by_id(near_prey) != nullptr)
              << ", prey at 40 m seen="
              << (r.find_by_id(far_prey) != nullptr) << std::endl;
    CHECK(r.find_by_id(near_prey) != nullptr, "prey inside the range is seen");
    CHECK(r.find_by_id(far_prey) == nullptr, "prey beyond it is not");
}

// THE ONE THAT MATTERS. Occlusion is what makes cover mean anything,
// and it is exactly what reading a position out of the particle array
// throws away.
void test_a_boulder_hides_prey() {
    World w;
    const int p = w.prey(0.0f, 12.0f);
    // A wide, tall boulder squarely between predator and prey.
    w.boulder(0.0f, 6.0f, 8.0f, 1.0f, 4.0f);
    w.settle();

    SenseResult r = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    std::cout << "  [measure] boulder at 6 m, prey at 12 m: prey seen="
              << (r.find_by_id(p) != nullptr) << std::endl;
    CHECK(r.find_by_id(p) == nullptr,
          "prey behind cover is NOT seen, which is what makes cover cover");

    // The same question asked the other way round must agree. The prey
    // has to be EXCLUDED to ask it at all: can_see_point is a shadow
    // ray, so the prey's own surface sits between the viewer and the
    // prey's centre and blocks it whether or not a boulder exists.
    // Without the exclusion this check reads "blocked" in an empty
    // field and proves nothing.
    bool direct = false;
    {
        auto view = w.engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = w.engine.get_particle_system().get_shadow_bvh();
        if (bvh) direct = w.senses.can_see_point(0.0f, 0.0f, 1.0f,
                                                 0.0f, 12.0f, 1.0f,
                                                 view.get(), *bvh, {p});
    }
    // The control: the identical query with no boulder in the world.
    bool clear_field = false;
    {
        World open;
        const int q = open.prey(0.0f, 12.0f);
        open.settle();
        auto view = open.engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = open.engine.get_particle_system().get_shadow_bvh();
        if (bvh) clear_field = open.senses.can_see_point(0.0f, 0.0f, 1.0f,
                                                        0.0f, 12.0f, 1.0f,
                                                        view.get(), *bvh, {q});
    }
    std::cout << "  [measure] can_see_point to the prey's spot: with a "
                 "boulder " << direct << ", in an empty field "
              << clear_field << std::endl;
    CHECK(!direct, "can_see_point agrees with the cone");
    CHECK(clear_field,
          "and the same query is TRUE with no boulder, so the check is "
          "measuring the boulder rather than always saying no");
}

void test_stepping_aside_reveals_the_prey() {
    World w;
    const int p = w.prey(0.0f, 12.0f);
    w.boulder(0.0f, 6.0f, 4.0f, 1.0f, 4.0f);   // narrower: can be flanked
    w.settle();

    SenseResult blocked = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    // Move well clear of the boulder's edge and look north-east.
    SenseResult flanked = w.look(predator_eyes(), 9.0f, 0.0f,
                                 std::atan2(-9.0f, 12.0f));

    std::cout << "  [measure] from behind cover seen="
              << (blocked.find_by_id(p) != nullptr)
              << ", after flanking seen="
              << (flanked.find_by_id(p) != nullptr) << std::endl;
    CHECK(blocked.find_by_id(p) == nullptr, "hidden from straight on");
    CHECK(flanked.find_by_id(p) != nullptr,
          "and revealed by moving around the cover, so occlusion is "
          "geometry rather than a blanket refusal");
}

// A damaged eye must actually cost the creature something.
void test_a_blind_eye_costs_half_the_cone() {
    World w;
    const int left_prey  = w.prey(-8.0f, 8.0f);   // to the predator's left
    const int right_prey = w.prey( 8.0f, 8.0f);   // to its right
    w.settle();

    SenseConfig both = predator_eyes();
    SenseResult r_both = w.look(both, 0.0f, 0.0f, NORTH);

    SenseConfig half = predator_eyes();
    half.left_eye_blind = true;
    SenseResult r_half = w.look(half, 0.0f, 0.0f, NORTH);

    std::cout << "  [measure] both eyes: " << r_both.visible_targets.size()
              << " visible, left eye blind: "
              << r_half.visible_targets.size() << std::endl;
    CHECK(r_both.find_by_id(left_prey) && r_both.find_by_id(right_prey),
          "a whole predator sees prey on both sides");
    CHECK(r_half.visible_targets.size() < r_both.visible_targets.size(),
          "a blind eye genuinely narrows the cone (" +
          std::to_string(r_half.visible_targets.size()) + " vs " +
          std::to_string(r_both.visible_targets.size()) + ")");
}

// ---------------------------------------------------------------- smell

// Smell exists precisely because sight is narrow and blockable. This is
// the sense the shambler's shortcut made unreachable, so it is worth
// proving it works on its own terms.
void test_smell_reaches_where_sight_cannot() {
    World w;
    const int p = w.prey(0.0f, 6.0f);            // north, close
    w.boulder(0.0f, 3.0f, 10.0f, 1.0f, 4.0f);    // fully blocking
    w.settle();

    SenseResult sight = w.look(predator_eyes(), 0.0f, 0.0f, NORTH);
    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f);

    // Ask about the PREY, not about whether anything at all is visible.
    // With no target filter the cone reports every particle it hits, so
    // the boulder itself is a perfectly correct visible target and
    // any_visible() is true whenever the predator can see the thing
    // blocking its view.
    // P = 1.0 * (1 - (6/20)^2) = 0.91 at this distance.
    std::cout << "  [measure] behind a boulder: prey seen="
              << (sight.find_by_id(p) != nullptr)
              << ", boulder itself visible=" << sight.any_visible()
              << ", smelled on " << (rate * 100.0f)
              << "% of sniffs (model predicts 91%)" << std::endl;
    CHECK(sight.find_by_id(p) == nullptr, "the prey is hidden by the boulder");
    CHECK(sight.any_visible(),
          "while the boulder itself is plainly visible, which is what "
          "being behind cover means");
    CHECK(rate > 0.75f,
          "but smell still finds it, reliably, which is the whole point "
          "of having a nose as well as eyes (rate " +
          std::to_string(rate) + ")");
}

void test_smell_is_omnidirectional() {
    World w;
    w.prey(0.0f, -8.0f);                 // directly BEHIND a north-facer
    w.settle();

    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f);
    std::cout << "  [measure] prey 8 m behind: smelled on "
              << (rate * 100.0f) << "% of sniffs" << std::endl;
    CHECK(rate > 0.5f,
          "smell does not care which way the predator faces (rate " +
          std::to_string(rate) + ")");
}

// The model's whole shape is that closer smells better. If the rate
// does not fall with distance, the falloff term is decoration.
void test_smell_fades_with_distance() {
    World w;
    w.prey(0.0f, 3.0f);
    w.settle();
    const float near_rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f);

    World f;
    f.prey(0.0f, 18.0f);                 // near the 20 m odour radius
    f.settle();
    const float far_rate = f.sniff_rate(predator_nose(), 0.0f, 0.0f);

    std::cout << "  [measure] 3 m: " << (near_rate * 100.0f)
              << "%, 18 m: " << (far_rate * 100.0f)
              << "% (model predicts 98% and 19%)" << std::endl;
    CHECK(near_rate > far_rate + 0.2f,
          "smell fades with distance (" + std::to_string(near_rate) +
          " vs " + std::to_string(far_rate) + ")");
    CHECK(near_rate > 0.85f, "close prey is smelled almost every time");
}

// Beyond the odour radius there is no roll at all: it must be never,
// not rarely.
void test_beyond_the_odour_radius_is_never() {
    World w;
    w.prey(0.0f, 30.0f);                 // odour radius is 20 m
    w.settle();

    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f, 200);
    std::cout << "  [measure] prey 30 m away, odour radius 20 m: "
              << (rate * 100.0f) << "% of sniffs" << std::endl;
    CHECK(rate == 0.0f,
          "outside the odour radius it is never detected, not merely "
          "seldom (rate " + std::to_string(rate) + ")");
}

void test_it_ignores_odours_it_does_not_hunt() {
    World w;
    w.prey(0.0f, 5.0f, 1.0f, OdorType::SMOKE);   // present, but not food
    w.settle();

    const float rate = w.sniff_rate(predator_nose(), 0.0f, 0.0f, 200);
    std::cout << "  [measure] only smoke nearby: " << (rate * 100.0f)
              << "% of sniffs" << std::endl;
    CHECK(rate == 0.0f, "an odour outside attracted_to is not a meal");
}

void test_nothing_to_smell_is_answered_cleanly() {
    World w;
    w.boulder(0.0f, 5.0f, 4.0f, 1.0f, 3.0f);     // odourless
    w.settle();

    SmellResult r = w.sniff(predator_nose(), 0.0f, 0.0f);
    CHECK(!r.detected, "an empty world smells of nothing");
    CHECK(r.source_particle_id == -1,
          "and reports no source rather than a stale one");
}


// ======================================================================
// WATCHABLE
// ======================================================================
//
// The scenes below are the SAME geometry and the SAME SenseConfig the
// assertions above use, and they are drawn with the sense API itself
// rather than a picture of it: every green bead is a point the engine
// was asked about with can_see_point and answered yes. The shadow
// behind a boulder is therefore the real occlusion result, not an
// artist's impression of one.

void dot(Engine& e, float x, float y, float z, float size,
         float r, float g, float b) {
    Particle p{};
    p.shape = ParticleShape::SPHERE;
    p.x = x; p.y = y; p.z = z;
    p.width = p.height = p.thickness = size;
    p.size = size;
    p.r = r; p.g = g; p.b = b; p.a = 1.0f;
    p.SetMaterial(Materials::Type::WOOD_HARD);
    p.solver_mode = ParticleSolverMode::KINEMATIC;
    p.is_at_rest = true;
    e.add_particle(p);
}

// Sweep the cone and march each ray outward, asking can_see_point at
// every step and stopping where the answer turns to no. What is left
// is the creature's actual field of view, drawn.
void draw_visibility_fan(World& w, const SenseConfig& cfg,
                         float ox, float oy, float facing,
                         float r, float g, float b) {
    const float half = (cfg.fov_degrees * 0.5f) * PI_F / 180.0f;
    float lo = -half, hi = half;
    if (cfg.left_eye_blind)  lo = 0.0f;      // matches cast_vision_cone
    if (cfg.right_eye_blind) hi = 0.0f;
    const int rays = 40;
    const float step = 0.9f;
    const float range = cfg.vision_range * cfg.vision_quality;

    std::vector<std::pair<float,float>> marks;
    {
        auto view = w.engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = w.engine.get_particle_system().get_shadow_bvh();
        if (!bvh) return;
        for (int i = 0; i < rays; ++i) {
            const float t = (rays == 1) ? 0.5f
                          : static_cast<float>(i) / (rays - 1);
            const float a = facing + lo + t * (hi - lo);
            const float dx = std::sin(a), dy = std::cos(a);
            for (float d = 0.8f; d <= range; d += step) {
                const float px = ox + dx * d, py = oy + dy * d;
                if (!w.senses.can_see_point(ox, oy, 1.0f, px, py, 1.0f,
                                            view.get(), *bvh, {})) break;
                marks.emplace_back(px, py);
            }
        }
    }
    std::cout << "      [fan] " << marks.size() << " visible points from ("
              << ox << "," << oy << ")" << std::endl;
    for (auto& m : marks) dot(w.engine, m.first, m.second, 0.30f, 0.42f, r, g, b);
}

// The odour radius, as a ring on the ground.
void draw_ring(Engine& e, float cx, float cy, float radius,
               float r, float g, float b) {
    const int n = 72;
    for (int i = 0; i < n; ++i) {
        const float a = 2.0f * PI_F * static_cast<float>(i) / n;
        dot(e, cx + radius * std::cos(a), cy + radius * std::sin(a),
            0.25f, 0.18f, r, g, b);
    }
}

// A marker for the predator itself.
void draw_predator(Engine& e, float x, float y, float facing) {
    dot(e, x, y, 1.0f, 0.9f, 0.95f, 0.75f, 0.2f);
    for (float d = 0.6f; d <= 2.4f; d += 0.3f)          // a snout, to read facing
        dot(e, x + std::sin(facing) * d, y + std::cos(facing) * d,
            1.0f, 0.28f, 1.0f, 0.85f, 0.3f);
}

struct Station {
    const char* title;
    std::vector<std::string> lines;
    float look_x, look_y;
    float zoom;
};

void draw_caption(Engine& e, const Station& st, int i, int n) {
    auto* ui = e.get_ui_system();
    if (!ui) return;
    char head[160];
    std::snprintf(head, sizeof(head), "[%d/%d]  %s", i + 1, n, st.title);
    int y = 18;
    ui->draw_text(20, y, head, 255, 235, 140);
    y += 21;
    for (const std::string& l : st.lines) {
        const bool key = !l.empty() && l[0] == '>';
        ui->draw_text(20, y, key ? l.substr(1) : l,
                      key ? 150 : 225, key ? 255 : 225, key ? 170 : 225);
        y += 15;
    }
    ui->draw_text(20, y + 6, "SPACE = next view      ESC = stop",
                  170, 190, 255);
}

void hold(Engine& e, const Station& st, int i, int n) {
    std::cout << "\n  [" << (i + 1) << "/" << n << "] " << st.title
              << std::endl;
    for (const std::string& l : st.lines)
        std::cout << "      " << (l.empty() || l[0] != '>' ? l : l.substr(1))
                  << std::endl;
    const char* shot = std::getenv("LOGOSPHERE_SHOT");
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    bool released = false;
    int frames = 0;
    while (!g_quit) {
        e.update(1.0 / 60.0);
        e.render();
        draw_caption(e, st, i, n);
        ++frames;
        if (shot && frames >= 4) {          // capture BEFORE present clears it
            e.get_renderer().wait_for_completion();
            int ww = 0, hh = 0;
            std::vector<uint32_t> px(
                static_cast<size_t>(e.get_render_buffer().width()) *
                e.get_render_buffer().height());
            if (e.read_latest_framebuffer(px.data(), ww, hh)) {
                const PixelBuffer& ui = e.get_ui_overlay_buffer();
                for (int yy = 0; yy < hh && yy < ui.height(); ++yy)
                    for (int xx = 0; xx < ww && xx < ui.width(); ++xx) {
                        const EnhancedPixel q = ui.get_pixel(xx, yy);
                        if (q.a == 0) continue;
                        px[yy * ww + xx] = (0xFFu << 24) |
                            (static_cast<uint32_t>(q.r) << 16) |
                            (static_cast<uint32_t>(q.g) << 8) |
                             static_cast<uint32_t>(q.b);
                    }
                std::string path = std::string(shot) + "/" +
                                   std::to_string(i) + ".ppm";
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%d %d\n255\n", ww, hh);
                    for (int k = 0; k < ww * hh; ++k) {
                        const uint32_t q = px[k];
                        unsigned char rgb[3] = {
                            static_cast<unsigned char>((q >> 16) & 0xFF),
                            static_cast<unsigned char>((q >> 8) & 0xFF),
                            static_cast<unsigned char>(q & 0xFF)};
                        std::fwrite(rgb, 1, 3, f);
                    }
                    std::fclose(f);
                    std::cout << "      shot -> " << path << std::endl;
                }
            }
            e.present();
            return;
        }
        e.present();
        e.get_platform()->poll_events();
        if (!win) return;
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; return; }
        const bool sp = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (!sp) released = true;
        if (sp && released) return;
        if (e.get_platform()->should_close()) { g_quit = true; return; }
    }
}

// The window is created unfocused and lands BEHIND the launching
// terminal, so every SPACE press goes to the shell. Raise it and take
// the keyboard, or none of this is watchable.
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

// The scenes, in one window, at stations far enough apart that framing
// one shows only that one. Same geometry and same config as the
// assertions above.
void show_the_scenes() {
    if (!g_visual) return;
    World w(/*with_display=*/true);   // one engine, one window, no flicker

    // Ground, so distance is readable.
    for (float x = -40.0f; x <= 290.0f; x += 4.0f)
        for (float y = -45.0f; y <= 45.0f; y += 4.0f) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.x = x; p.y = y; p.z = 0.25f;  // sit ON the turtle
            p.width = p.height = 4.0f; p.thickness = 0.5f; p.size = 4.0f;
            p.r = 0.38f; p.g = 0.36f; p.b = 0.33f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            p.odor_type = OdorType::NONE;
            p.solver_mode = ParticleSolverMode::KINEMATIC;
            p.is_at_rest = true;
            w.engine.add_particle(p);
        }

    const SenseConfig eyes = predator_eyes();

    // A: open ground. The bare shape of the cone.
    w.prey(0.0f, 10.0f);
    // B: a boulder, and the shadow it casts.
    w.prey(60.0f, 12.0f);
    w.boulder(60.0f, 6.0f, 8.0f, 1.0f, 4.0f);
    // C: the same boulder flanked.
    w.prey(120.0f, 12.0f);
    w.boulder(120.0f, 6.0f, 4.0f, 1.0f, 4.0f);
    // D: one eye gone.
    w.prey(180.0f - 8.0f, 8.0f);
    w.prey(180.0f + 8.0f, 8.0f);
    w.settle();

    // Fans are drawn AFTER settle so the boulders are in the BVH they
    // are cast against.
    draw_predator(w.engine, 0.0f, 0.0f, NORTH);
    draw_visibility_fan(w, eyes, 0.0f, 0.0f, NORTH, 0.25f, 0.85f, 0.35f);

    draw_predator(w.engine, 60.0f, 0.0f, NORTH);
    draw_visibility_fan(w, eyes, 60.0f, 0.0f, NORTH, 0.25f, 0.85f, 0.35f);

    draw_predator(w.engine, 129.0f, 0.0f, std::atan2(-9.0f, 12.0f));
    draw_visibility_fan(w, eyes, 129.0f, 0.0f, std::atan2(-9.0f, 12.0f),
                        0.3f, 0.8f, 1.0f);

    SenseConfig one_eye = eyes;
    one_eye.left_eye_blind = true;
    draw_predator(w.engine, 180.0f, 0.0f, NORTH);
    draw_visibility_fan(w, one_eye, 180.0f, 0.0f, NORTH, 1.0f, 0.65f, 0.25f);

    // E: smell. Sight blocked, nose not.
    w.prey(240.0f, 6.0f);
    w.boulder(240.0f, 3.0f, 10.0f, 1.0f, 4.0f);
    w.settle();
    draw_predator(w.engine, 240.0f, 0.0f, NORTH);
    draw_visibility_fan(w, eyes, 240.0f, 0.0f, NORTH, 0.25f, 0.85f, 0.35f);
    draw_ring(w.engine, 240.0f, 6.0f, 20.0f, 0.95f, 0.55f, 0.15f);

    w.engine.get_particle_system().queue_light(60.0f, -40.0f, 55.0f,
                                               40000000.0f, 900.0f,
                                               1.0f, 0.95f, 0.9f);
    w.engine.get_particle_system().queue_light(200.0f, 35.0f, 50.0f,
                                               30000000.0f, 900.0f,
                                               0.85f, 0.9f, 1.0f);
    for (int i = 0; i < 10; ++i) w.engine.update(1.0 / 60.0);

    const Station stations[] = {
        {"THE CONE  -  what a predator can see at all",
         {"Every green bead is a point the engine was asked about with",
          "can_see_point and answered YES. This is the real field of",
          "view, not a drawing of one.",
          "",
          ">100 degrees wide, 25 m deep. Behind the predator: nothing.",
          "",
          "The pale marker is the predator, its snout showing facing."},
         0.0f, 8.0f, 22.0f},

        {"COVER  -  a boulder casts a shadow in the senses",
         {"Same cone, same predator. A boulder 6 m ahead.",
          "",
          ">The wedge of darkness behind it is prey the predator",
          ">CANNOT see. That shadow is what makes cover mean anything,",
          ">and it is exactly what reading a position out of the",
          ">particle array throws away.",
          "",
          "The prey sits at 12 m, inside the shadow. Measured: not seen."},
         60.0f, 8.0f, 22.0f},

        {"FLANKING  -  occlusion is geometry, not a refusal",
         {"The same prey at 12 m, from a predator that has moved aside",
          "and turned to look back in.",
          "",
          ">Now it is visible. The engine is not refusing to report prey",
          ">near boulders; it is answering a question about a straight",
          ">line, so moving changes the answer."},
         126.0f, 8.0f, 22.0f},

        {"A BLINDED EYE  -  half a cone",
         {"left_eye_blind = true. cast_vision_cone clamps the sweep to",
          "one side of centre rather than dimming it.",
          "",
          ">Prey stands 8 m to each side. With both eyes it sees two.",
          ">With one, it sees one. The damage costs it something real."},
         180.0f, 8.0f, 22.0f},

        {"THE NOSE  -  why a predator needs more than eyes",
         {"Sight is blocked by the boulder: the green cone stops dead.",
          "The orange ring is the prey's 20 m odour radius.",
          "",
          ">Smell is omnidirectional and ignores cover. Measured over",
          ">400 sniffs at this distance: 92% detected, against the",
          ">model's own prediction of 91%.",
          "",
          "P = intensity * (1 - (d/r)^2) * state * sensitivity",
          "",
          "This is the sense the shambler's direct-tracking shortcut",
          "made unreachable, by setting food_known before it could fire."},
         240.0f, 4.0f, 15.0f},
    };

    const int n = static_cast<int>(sizeof(stations) / sizeof(stations[0]));
    bring_to_front(w.engine);
    for (int i = 0; i < n && !g_quit; ++i) {
        auto& cam = w.engine.get_camera_system();
        cam.set_pixels_per_unit(stations[i].zoom);
        cam.set_position(stations[i].look_x - 8.0f,
                         stations[i].look_y - 8.0f, 9.0f);
        cam.look_at(stations[i].look_x, stations[i].look_y, 1.0f);
        hold(w.engine, stations[i], i, n);
    }
}

}  // namespace

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    // check_smell rolls rand(); pin the seed so a rate is
    // reproducible rather than a different number every run.
    std::srand(20260802u);
    std::cout << "Predator senses (src/sense_system)" << std::endl;
    test_it_sees_what_is_in_front_of_it();
    test_it_cannot_see_behind_itself();
    test_range_is_a_real_limit();
    test_a_boulder_hides_prey();
    test_stepping_aside_reveals_the_prey();
    test_a_blind_eye_costs_half_the_cone();
    test_smell_reaches_where_sight_cannot();
    test_smell_is_omnidirectional();
    test_smell_fades_with_distance();
    test_beyond_the_odour_radius_is_never();
    test_it_ignores_odours_it_does_not_hunt();
    test_nothing_to_smell_is_answered_cleanly();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;

    if (g_visual && tests_failed == 0) show_the_scenes();

    return tests_failed == 0 ? 0 : 1;
}
