// Why small trees were bare poles, shown rather than described.
//
// Issue #21, thread 2. Space colonization grows toward a cloud of
// attractors and, each iteration, deletes every attractor within
// kill_distance of ANY node. So the whole algorithm turns on one
// comparison: how big is the kill radius next to the crown it is
// working in.
//
//   crown_reach   = min(max(crown_radius, crown_height*0.45, 2), height*0.6)
//   kill_distance = min(4, max(1.5, 0.5*crown_reach))     <- the old floor
//
// crown_reach knows how tall the tree is. The 1.5 m floor did not. On a
// 1 m tree the crown is 0.6 m across and the kill radius was 1.5 m, so
// the root node stood in the middle of the cloud and deleted all 80
// attractors on the first iteration. One segment. Every time.
//
// This builds that comparison out of particles so it can be looked at:
// the same crown, the same attractors, two kill radii, side by side.
// The broken one is a red ring that visibly swallows the entire crown.
//
//   ./build/test_tree_collapse_demo                   # numbers, asserted
//   LOGOSPHERE_VISUAL=1 ./build/test_tree_collapse_demo   # SPACE next, ESC stop

#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/tree_generator.h"
#include "logosphere/worldgen/space_colonization.h"
#include "logosphere/kg/kg_module.h"
#include "generated/earth_ontology_registry.h"
#include "particle.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "    FAIL: " << msg << std::endl; }         \
    } while (0)

namespace {

bool g_visual = false;
bool g_quit = false;

// --------------------------------------------------------------- the maths

// Unchanged by the fix. Only what is derived FROM it changed, which is
// the whole point: the crown always knew the tree's size.
float crown_reach_of(float height, float crown_radius) {
    const float canopy_start = 0.45f;
    const float crown_height = height * (1.0f - canopy_start);
    float r = std::max({crown_radius, crown_height * 0.45f, 2.0f});
    return std::min(r, height * 0.6f);
}

// The three lengths, before and after. Mirrors tree_generator.cpp; the
// "before" values are historical constants and cannot drift.
struct Lengths { float attraction, kill, segment; };

Lengths before_fix(float crown_reach) {
    return { std::max(5.0f, crown_reach * 0.8f),
             std::min(4.0f, std::max(1.5f, 0.5f * crown_reach)),
             0.5f };
}
Lengths after_fix(float crown_reach) {
    return { crown_reach * 0.80f, crown_reach * 0.50f, crown_reach * 0.07f };
}

// One crown, one set of lengths, grown by the real algorithm.
TreeSkeleton grow_crown(float height, float crown_radius, const Lengths& L,
                        std::vector<Vec3>* out_attractors = nullptr) {
    const float crown_reach = crown_reach_of(height, crown_radius);
    const float crown_height = height * 0.55f;
    const float trunk_top_z = height * 0.45f;

    SpaceColonizationParams p;
    p.root_position = Vec3(0, 0, trunk_top_z);
    p.root_direction = Vec3(0, 0, 1);
    p.attractor_center = Vec3(0, 0, trunk_top_z + crown_height * 0.5f);
    p.attractor_radius = crown_reach;
    p.attractor_count = 80;          // the floor, which small trees hit
    p.attraction_range = L.attraction;
    p.kill_distance = L.kill;
    p.segment_length = L.segment;
    p.initial_thickness = 0.1f;
    p.thickness_taper = 0.9f;
    p.max_iterations = 240;
    p.random_seed = 4242;

    SpaceColonization sc;
    TreeSkeleton skel = sc.generate(p);
    if (out_attractors) {
        // Same sphere the algorithm filled, for drawing.
        out_attractors->clear();
        SpaceColonizationParams q = p;
        for (int i = 0; i < 80; ++i) {
            // Deterministic scatter for the picture only.
            float a = static_cast<float>(i) * 2.39996f;
            float t = static_cast<float>(i) / 80.0f;
            float rr = crown_reach * std::cbrt(t);
            float z = crown_reach * (2.0f * t - 1.0f) * 0.7f;
            out_attractors->push_back(Vec3(
                p.attractor_center.x + rr * std::cos(a),
                p.attractor_center.y + rr * std::sin(a),
                p.attractor_center.z + z));
        }
    }
    return skel;
}

// --------------------------------------------------------------- drawing

void dot(Engine& e, Vec3 at, float size, float r, float g, float b) {
    Particle p{};
    p.shape = ParticleShape::SPHERE;
    p.x = at.x; p.y = at.y; p.z = at.z;
    p.width = p.height = p.thickness = size;
    p.size = size;
    p.r = r; p.g = g; p.b = b; p.a = 1.0f;
    p.SetMaterial(Materials::Type::WOOD_HARD);
    p.solver_mode = ParticleSolverMode::KINEMATIC;
    p.is_at_rest = true;
    e.add_particle(p);
}

// A skeleton, drawn as beads along every branch.
void draw_skeleton(Engine& e, const TreeSkeleton& s, Vec3 origin,
                   float r, float g, float b) {
    for (int i = 0; i < s.segment_count(); ++i) {
        const BranchSegment& seg = s.get_segment(i);
        for (int k = 0; k <= 2; ++k) {
            const float t = static_cast<float>(k) / 2.0f;
            Vec3 at(origin.x + seg.start.x + (seg.end.x - seg.start.x) * t,
                    origin.y + seg.start.y + (seg.end.y - seg.start.y) * t,
                    origin.z + seg.start.z + (seg.end.z - seg.start.z) * t);
            dot(e, at, std::max(0.03f, seg.thickness), r, g, b);
        }
    }
}

// The kill radius, as a ring around the root node. This is the object
// the whole bug is about: when it is wider than the crown, the crown
// dies on iteration one.
void draw_ring(Engine& e, Vec3 centre, float radius,
               float r, float g, float b) {
    const int n = 64;
    for (int i = 0; i < n; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / n;
        dot(e, Vec3(centre.x + radius * std::cos(a),
                    centre.y + radius * std::sin(a),
                    centre.z),
            0.07f, r, g, b);
    }
}

void draw_attractors(Engine& e, const std::vector<Vec3>& pts, Vec3 origin) {
    for (const Vec3& v : pts)
        dot(e, Vec3(origin.x + v.x, origin.y + v.y, origin.z + v.z),
            0.05f, 1.0f, 0.9f, 0.35f);
}

void ground(Engine& e, float top_z, float half) {
    for (float x = -half; x <= half; x += 2.0f)
        for (float y = -half; y <= half; y += 2.0f) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.x = x; p.y = y; p.z = top_z - 0.25f;
            p.width = p.height = 2.0f;
            p.thickness = 0.5f;
            p.size = 2.0f;
            p.r = 0.40f; p.g = 0.38f; p.b = 0.34f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            p.solver_mode = ParticleSolverMode::KINEMATIC;
            p.is_at_rest = true;
            e.add_particle(p);
        }
}

// --------------------------------------------------------------- viewing

Engine* make_engine() {
    auto* e = new Engine(nullptr);
    EngineConfig cfg;
    cfg.create_display = g_visual;
    cfg.window_width = 1100;
    cfg.window_height = 720;
    cfg.window_title = "why small trees were poles";
    cfg.show_debug_overlay = false;
    cfg.enable_chat_window = false;
    if (e->initialize(cfg) < 0) { delete e; return nullptr; }
    e->get_kg().extendOntology(earth::ontology::registry());
    return e;
}

void light_and_frame(Engine& e, Vec3 look, float zoom) {
    e.get_particle_system().queue_light(look.x - 10.0f, look.y - 12.0f,
                                        look.z + 14.0f, 4000000.0f, 200.0f,
                                        1.0f, 0.95f, 0.9f);
    e.get_particle_system().queue_light(look.x + 12.0f, look.y + 6.0f,
                                        look.z + 10.0f, 1500000.0f, 200.0f,
                                        0.85f, 0.9f, 1.0f);
    if (!g_visual) return;
    auto& cam = e.get_camera_system();
    cam.set_position(look.x - 8.0f, look.y - 8.0f, look.z + 8.0f);
    cam.look_at(look.x, look.y, look.z);
    cam.adjust_zoom(zoom);
}

// LOGOSPHERE_SHOT=<dir> writes the view instead of waiting on it. A
// picture that explains something is a claim about pixels, and this is
// how that claim gets checked.
void shoot(Engine& e, const std::string& name) {
    const char* dir = std::getenv("LOGOSPHERE_SHOT");
    if (!dir || !g_visual) return;
    for (int i = 0; i < 3; ++i) { e.update(1.0 / 60.0); e.render(); e.present(); }
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
        const uint32_t q = px[i];
        unsigned char rgb[3] = {
            static_cast<unsigned char>((q >> 16) & 0xFF),
            static_cast<unsigned char>((q >> 8) & 0xFF),
            static_cast<unsigned char>(q & 0xFF)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::cout << "    shot -> " << name << ".ppm" << std::endl;
}

void show(Engine& e, const char* caption) {
    if (!g_visual || g_quit) {
        return;
    }
    if (std::getenv("LOGOSPHERE_SHOT")) { shoot(e, caption); return; }
    std::cout << "    [" << caption
              << "]  SPACE for the next view, ESC to stop." << std::endl;
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    bool released = false;
    while (!g_quit) {
        e.update(1.0 / 60.0);
        e.render();
        e.present();
        e.get_platform()->poll_events();
        if (!win) break;
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; break; }
        const bool space = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (!space) released = true;
        if (space && released) break;
        if (e.get_platform()->should_close()) { g_quit = true; break; }
    }
}

// --------------------------------------------------------------- the views

// VIEW 1. The cause, at the size where it bites. One crown, one set of
// attractors, two kill radii. The red ring is the old one.
void view_the_cause() {
    const float H = 1.0f;                 // a 1 m tree
    const float CR = crown_reach_of(H, 0.35f);
    const Lengths old_L = before_fix(CR);
    const Lengths new_L = after_fix(CR);

    std::cout << "\n  Why a 1 m tree collapsed" << std::endl;
    std::cout << "    crown is " << CR << " m across" << std::endl;
    std::cout << "    kill radius BEFORE " << old_L.kill << " m  ("
              << (old_L.kill / CR) << "x the crown - it swallows it)"
              << std::endl;
    std::cout << "    kill radius AFTER  " << new_L.kill << " m  ("
              << (new_L.kill / CR) << "x the crown)" << std::endl;

    std::vector<Vec3> attractors;
    TreeSkeleton broken = grow_crown(H, 0.35f, old_L, &attractors);
    TreeSkeleton fixed  = grow_crown(H, 0.35f, new_L);

    std::cout << "    segments grown BEFORE " << broken.segment_count()
              << ", AFTER " << fixed.segment_count() << std::endl;

    CHECK(old_L.kill > CR,
          "the old kill radius really is wider than the whole crown (" +
          std::to_string(old_L.kill) + " vs " + std::to_string(CR) + ")");
    CHECK(new_L.kill < CR,
          "the new one fits inside it (" + std::to_string(new_L.kill) +
          " vs " + std::to_string(CR) + ")");
    CHECK(broken.segment_count() <= 2,
          "the old lengths collapse the crown to nothing (" +
          std::to_string(broken.segment_count()) + " segments)");
    CHECK(fixed.segment_count() > 20,
          "the new ones grow a real crown (" +
          std::to_string(fixed.segment_count()) + " segments)");

    if (!g_visual) return;
    Engine* e = make_engine();
    if (!e) return;
    ground(*e, 0.0f, 12.0f);

    // Close together and zoomed hard: the whole subject is 0.6 m
    // across, so at any normal framing it is a speck.
    const Vec3 left(-1.8f, 0.0f, 0.0f);
    const Vec3 right(1.8f, 0.0f, 0.0f);
    const Vec3 root_off(0.0f, 0.0f, H * 0.45f);

    // Left: the old lengths. Attractors, the kill ring that engulfs
    // them, and the single segment that survives.
    draw_attractors(*e, attractors, left);
    draw_ring(*e, Vec3(left.x, left.y, left.z + root_off.z), old_L.kill,
              1.0f, 0.15f, 0.1f);
    draw_skeleton(*e, broken, left, 0.9f, 0.3f, 0.2f);

    // Right: the new lengths. Same attractors, a ring that fits inside
    // the crown, and a tree.
    draw_attractors(*e, attractors, right);
    draw_ring(*e, Vec3(right.x, right.y, right.z + root_off.z), new_L.kill,
              0.2f, 1.0f, 0.35f);
    draw_skeleton(*e, fixed, right, 0.35f, 0.85f, 0.4f);

    light_and_frame(*e, Vec3(0.0f, 0.0f, 0.8f), 140.0f);
    for (int i = 0; i < 3; ++i) e->update(1.0 / 60.0);
    std::cout << "    LEFT red ring = old kill radius, swallowing every "
                 "attractor. RIGHT green = fixed." << std::endl;
    show(*e, "the cause");
    e->shutdown();
    delete e;
}

// VIEW 2. The consequence, across sizes. Same heights, both parameter
// sets, in a row. Before is a line of poles.
void view_before_and_after() {
    // 1 to 6 m rather than up to 12: one frame cannot show a 1 m tree
    // and a 12 m tree usefully at the same time, and the small end is
    // where the bug lived.
    const std::vector<float> heights = {1.0f, 2.0f, 3.0f, 4.0f, 6.0f};
    std::cout << "\n  What it looked like at each size" << std::endl;
    std::cout << "    height   segments BEFORE   segments AFTER" << std::endl;

    Engine* e = g_visual ? make_engine() : nullptr;
    if (g_visual && !e) return;
    if (e) ground(*e, 0.0f, 30.0f);

    float x = -10.0f;
    int collapsed_before = 0, collapsed_after = 0;
    bool always_richer = true;
    for (float h : heights) {
        const float CR = crown_reach_of(h, h * 0.35f);
        TreeSkeleton b = grow_crown(h, h * 0.35f, before_fix(CR));
        TreeSkeleton a = grow_crown(h, h * 0.35f, after_fix(CR));
        std::cout << "    " << std::setw(5) << h
                  << std::setw(16) << b.segment_count()
                  << std::setw(17) << a.segment_count() << std::endl;
        if (b.segment_count() <= 2) collapsed_before++;
        if (a.segment_count() <= 2) collapsed_after++;
        if (a.segment_count() <= b.segment_count()) always_richer = false;

        if (e) {
            draw_skeleton(*e, b, Vec3(x, -3.0f, 0.0f), 0.9f, 0.3f, 0.2f);
            draw_skeleton(*e, a, Vec3(x, 3.0f, 0.0f), 0.35f, 0.85f, 0.4f);
        }
        x += 5.0f;
    }

    // The smallest sizes collapsed to a single segment; the rest were
    // stunted rather than dead. Both are the same cause at different
    // severities, so assert both rather than pretending it is one
    // clean cliff.
    CHECK(collapsed_before >= 2,
          "the old lengths collapsed the smallest sizes to one segment (" +
          std::to_string(collapsed_before) + " of 5 did)");
    CHECK(collapsed_after == 0,
          "and the new ones collapse none (" +
          std::to_string(collapsed_after) + " of 5)");
    CHECK(always_richer,
          "every size grows more crown than it did before");

    if (!e) return;
    light_and_frame(*e, Vec3(0.0f, 0.0f, 2.0f), 26.0f);
    for (int i = 0; i < 3; ++i) e->update(1.0 / 60.0);
    std::cout << "    FRONT row (red) = before, a line of poles. "
                 "BACK row (green) = after." << std::endl;
    show(*e, "before and after");
    e->shutdown();
    delete e;
}

// VIEW 3. The same thing through the shipping generator, so the demo
// is not just arguing with itself: real Tree entities, real particles.
void view_real_trees() {
    std::cout << "\n  Through the real generator (particles per tree)"
              << std::endl;
    Engine* e = make_engine();
    if (!e) { CHECK(false, "engine init"); return; }
    if (g_visual) ground(*e, 0.0f, 30.0f);

    auto& tg = e->get_worldgen_system().get_tree_generator();
    const std::vector<float> heights = {1.0f, 2.0f, 3.0f, 6.0f, 12.0f};
    float x = -14.0f;
    size_t smallest = 0;
    for (float h : heights) {
        TreeSpec spec;
        spec.height = h;
        spec.crown_radius = h * 0.35f;
        spec.random_seed = 4242;
        kg::EntityID t = tg.generate_tree_space_colonization(x, 0.0f, 0.0f,
                                                             spec);
        const size_t n = (t == kg::INVALID_ENTITY)
                       ? 0 : e->get_kg().getEntityKGParticles(t).size();
        std::cout << "    " << std::setw(5) << h << " m -> " << n
                  << " particles" << std::endl;
        if (h == 1.0f) smallest = n;
        x += 7.0f;
    }
    // 4 particles was the collapsed tree: 3 trunk segments and one
    // crown segment.
    CHECK(smallest > 50,
          "a 1 m tree from the shipping generator is a tree, not the "
          "4-particle pole it used to be (got " +
          std::to_string(smallest) + ")");

    if (g_visual) {
        light_and_frame(*e, Vec3(0.0f, 0.0f, 3.0f), 6.0f);
        for (int i = 0; i < 30; ++i) e->update(1.0 / 60.0);
        std::cout << "    Real trees from the engine, 1 m to 12 m."
                  << std::endl;
        show(*e, "real trees");
    }
    e->shutdown();
    delete e;
}

}  // namespace

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::cout << "Why small trees were bare poles (issue #21)"
              << (g_visual ? "  [SPACE next, ESC stop]" : "") << std::endl;
    view_the_cause();
    if (!g_quit) view_before_and_after();
    if (!g_quit) view_real_trees();
    std::cout << "\n" << tests_passed << " passed, " << tests_failed
              << " failed" << (g_quit ? "  (stopped early)" : "")
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
