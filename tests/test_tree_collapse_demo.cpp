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
                   float r, float g, float b, float bead = 0.06f) {
    for (int i = 0; i < s.segment_count(); ++i) {
        const BranchSegment& seg = s.get_segment(i);
        const float len = seg.length();
        // Beads spaced by their own size, so a branch reads as a
        // branch. Three dots per segment read as three dots.
        const int n = std::max(2, static_cast<int>(len / (bead * 0.6f)));
        for (int k = 0; k <= n; ++k) {
            const float t = static_cast<float>(k) / n;
            Vec3 at(origin.x + seg.start.x + (seg.end.x - seg.start.x) * t,
                    origin.y + seg.start.y + (seg.end.y - seg.start.y) * t,
                    origin.z + seg.start.z + (seg.end.z - seg.start.z) * t);
            dot(e, at, bead, r, g, b);
        }
    }
}

// The trunk the crown sits on. Both rows get one, because "bare pole"
// is a statement about a tree, and a crown floating with no trunk
// under it is not a picture of anything.
void draw_trunk(Engine& e, Vec3 origin, float height, float bead) {
    const float top = height * 0.45f;
    const int n = std::max(3, static_cast<int>(top / (bead * 0.6f)));
    for (int k = 0; k <= n; ++k) {
        const float t = static_cast<float>(k) / n;
        dot(e, Vec3(origin.x, origin.y, origin.z + top * t),
            bead * 1.35f, 0.34f, 0.24f, 0.15f);
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
    cfg.window_title = "why small trees were poles (issue #21)";
    cfg.show_debug_overlay = false;
    cfg.enable_chat_window = false;
    if (e->initialize(cfg) < 0) { delete e; return nullptr; }
    e->get_kg().extendOntology(earth::ontology::registry());
    return e;
}

// ONE window for the whole demo. Building an Engine per view opened and
// destroyed a window per view, which is a flicker and nothing else.
// Everything is laid out once, far enough apart that framing one
// station shows only that station, and SPACE walks the camera between
// them.
struct Station {
    const char* title;
    const char* caption;
    Vec3  look;
    float zoom;      // pixels per world metre
};

void look_at_station(Engine& e, const Station& st) {
    auto& cam = e.get_camera_system();
    cam.set_pixels_per_unit(st.zoom);
    cam.set_position(st.look.x - 8.0f, st.look.y - 8.0f, st.look.z + 8.0f);
    cam.look_at(st.look.x, st.look.y, st.look.z);
}

// Hold here until SPACE. The window keeps rendering the whole time, so
// the picture is steady and can actually be looked at.
void hold(Engine& e, const Station& st, int index, int total) {
    std::cout << "\n  [" << (index + 1) << "/" << total << "] " << st.title
              << "\n      " << st.caption
              << "\n      SPACE for the next view, ESC to stop." << std::endl;
    const char* shot_dir = std::getenv("LOGOSPHERE_SHOT");
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());

    bool released = false;
    int frames = 0;
    while (!g_quit) {
        e.update(1.0 / 60.0);
        e.render();
        e.present();
        e.get_platform()->poll_events();
        ++frames;

        if (shot_dir) {                       // scripted: grab and go
            if (frames >= 4) {
                e.get_renderer().wait_for_completion();
                int w = 0, h = 0;
                std::vector<uint32_t> px(
                    static_cast<size_t>(e.get_render_buffer().width()) *
                    e.get_render_buffer().height());
                if (e.read_latest_framebuffer(px.data(), w, h)) {
                    std::string path = std::string(shot_dir) + "/" +
                                       std::to_string(index) + ".ppm";
                    if (FILE* f = std::fopen(path.c_str(), "wb")) {
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
                        std::cout << "      shot -> " << path << std::endl;
                    }
                }
                return;
            }
            continue;
        }

        if (!win) return;                     // no window: nothing to hold
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; return; }
        const bool space = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (!space) released = true;          // ignore a held-over press
        if (space && released) return;
        if (e.get_platform()->should_close()) { g_quit = true; return; }
    }
}

// --------------------------------------------------------------- the scene

// Station A: the cause, at the size where it bites.
void build_cause(Engine& e, Vec3 at) {
    const float H = 1.0f;
    const float CR = crown_reach_of(H, 0.35f);
    std::vector<Vec3> attractors;
    TreeSkeleton broken = grow_crown(H, 0.35f, before_fix(CR), &attractors);
    TreeSkeleton fixed  = grow_crown(H, 0.35f, after_fix(CR));

    const Vec3 left(at.x - 1.8f, at.y, at.z);
    const Vec3 right(at.x + 1.8f, at.y, at.z);
    const float root_z = H * 0.45f;

    draw_attractors(e, attractors, left);
    draw_ring(e, Vec3(left.x, left.y, left.z + root_z),
              before_fix(CR).kill, 1.0f, 0.15f, 0.1f);
    draw_trunk(e, left, H, 0.05f);
    draw_skeleton(e, broken, left, 0.30f, 0.62f, 0.24f, 0.05f);

    draw_attractors(e, attractors, right);
    draw_ring(e, Vec3(right.x, right.y, right.z + root_z),
              after_fix(CR).kill, 0.2f, 1.0f, 0.35f);
    draw_trunk(e, right, H, 0.05f);
    draw_skeleton(e, fixed, right, 0.30f, 0.62f, 0.24f, 0.05f);
}

// Stations B and C: the same five heights, grown each way.
void build_row(Engine& e, Vec3 at, bool is_before) {
    const float heights[] = {1.0f, 2.0f, 3.0f, 4.0f, 6.0f};
    float x = at.x - 10.0f;
    for (float h : heights) {
        const float CR = crown_reach_of(h, h * 0.35f);
        TreeSkeleton sk = grow_crown(h, h * 0.35f,
                                     is_before ? before_fix(CR)
                                               : after_fix(CR));
        const float bead = std::max(0.045f, h * 0.030f);
        draw_trunk(e, Vec3(x, at.y, at.z), h, bead);
        draw_skeleton(e, sk, Vec3(x, at.y, at.z),
                      0.30f, 0.62f, 0.24f, bead);
        x += 5.0f;
    }
}

// Station D: real trees straight from the shipping generator, so the
// demo is not just arguing with itself.
void build_real(Engine& e, Vec3 at) {
    auto& tg = e.get_worldgen_system().get_tree_generator();
    const float heights[] = {1.0f, 2.0f, 3.0f, 4.0f, 6.0f};
    float x = at.x - 10.0f;
    for (float h : heights) {
        TreeSpec spec;
        spec.height = h;
        spec.crown_radius = h * 0.35f;
        spec.random_seed = 4242;
        tg.generate_tree_space_colonization(x, at.y, at.z, spec);
        x += 5.0f;
    }
}

// --------------------------------------------------------------- numbers

void measure() {
    const float H = 1.0f;
    const float CR = crown_reach_of(H, 0.35f);
    const Lengths oldL = before_fix(CR), newL = after_fix(CR);

    std::cout << "\n  Why a 1 m tree collapsed" << std::endl;
    std::cout << "    crown is " << CR << " m across" << std::endl;
    std::cout << "    kill radius BEFORE " << oldL.kill << " m ("
              << (oldL.kill / CR) << "x the crown, it swallows it)"
              << std::endl;
    std::cout << "    kill radius AFTER  " << newL.kill << " m ("
              << (newL.kill / CR) << "x the crown)" << std::endl;

    TreeSkeleton broken = grow_crown(H, 0.35f, oldL);
    TreeSkeleton fixed  = grow_crown(H, 0.35f, newL);
    std::cout << "    segments grown BEFORE " << broken.segment_count()
              << ", AFTER " << fixed.segment_count() << std::endl;

    CHECK(oldL.kill > CR,
          "the old kill radius really is wider than the whole crown (" +
          std::to_string(oldL.kill) + " vs " + std::to_string(CR) + ")");
    CHECK(newL.kill < CR, "the new one fits inside it");
    CHECK(broken.segment_count() <= 2,
          "the old lengths collapse the crown to nothing (" +
          std::to_string(broken.segment_count()) + " segments)");
    CHECK(fixed.segment_count() > 20,
          "the new ones grow a real crown (" +
          std::to_string(fixed.segment_count()) + " segments)");

    std::cout << "\n  At each size" << std::endl;
    std::cout << "    height   segments BEFORE   segments AFTER" << std::endl;
    int collapsed_before = 0, collapsed_after = 0;
    bool always_richer = true;
    for (float h : {1.0f, 2.0f, 3.0f, 4.0f, 6.0f}) {
        const float cr = crown_reach_of(h, h * 0.35f);
        TreeSkeleton b = grow_crown(h, h * 0.35f, before_fix(cr));
        TreeSkeleton a = grow_crown(h, h * 0.35f, after_fix(cr));
        std::cout << "    " << std::setw(5) << h
                  << std::setw(16) << b.segment_count()
                  << std::setw(17) << a.segment_count() << std::endl;
        if (b.segment_count() <= 2) collapsed_before++;
        if (a.segment_count() <= 2) collapsed_after++;
        if (a.segment_count() <= b.segment_count()) always_richer = false;
    }
    CHECK(collapsed_before >= 2,
          "the old lengths collapsed the smallest sizes to one segment (" +
          std::to_string(collapsed_before) + " of 5)");
    CHECK(collapsed_after == 0, "and the new ones collapse none");
    CHECK(always_richer, "every size grows more crown than it did before");
}

}  // namespace

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::cout << "Why small trees were bare poles (issue #21)" << std::endl;

    measure();

    Engine* e = make_engine();
    if (!e) { CHECK(false, "engine init"); }
    else {
        // Stations are far apart so that framing one shows only it.
        const Vec3 A(-60.0f, 0.0f, 0.0f);
        const Vec3 B(0.0f, -40.0f, 0.0f);
        const Vec3 C(0.0f, 40.0f, 0.0f);
        const Vec3 D(60.0f, 0.0f, 0.0f);

        if (g_visual) {
            ground(*e, 0.0f, 90.0f);
            build_cause(*e, A);
            build_row(*e, B, /*is_before=*/true);
            build_row(*e, C, /*is_before=*/false);
        }
        build_real(*e, D);            // asserted below, drawn only if visual

        size_t smallest = 0;
        {   // the shipping generator's own answer for a 1 m tree
            auto& tg = e->get_worldgen_system().get_tree_generator();
            TreeSpec spec;
            spec.height = 1.0f;
            spec.crown_radius = 0.35f;
            spec.random_seed = 4242;
            kg::EntityID t = tg.generate_tree_space_colonization(
                D.x, D.y + 8.0f, D.z, spec);
            if (t != kg::INVALID_ENTITY)
                smallest = e->get_kg().getEntityKGParticles(t).size();
        }
        std::cout << "\n  Shipping generator, 1 m tree: " << smallest
                  << " particles (was 4, a bare pole)" << std::endl;
        CHECK(smallest > 50,
              "a 1 m tree from the real generator is a tree (got " +
              std::to_string(smallest) + ")");

        if (g_visual) {
            e->get_particle_system().queue_light(-20.0f, -30.0f, 30.0f,
                                                 9000000.0f, 400.0f,
                                                 1.0f, 0.95f, 0.9f);
            e->get_particle_system().queue_light(30.0f, 20.0f, 26.0f,
                                                 5000000.0f, 400.0f,
                                                 0.85f, 0.9f, 1.0f);
            for (int i = 0; i < 10; ++i) e->update(1.0 / 60.0);

            const Station stations[] = {
                {"THE CAUSE, on a 1 m tree",
                 "LEFT: the old 1.5 m kill radius as a red ring. It "
                 "swallows the whole 0.6 m crown, so every attractor dies "
                 "on iteration one and one segment survives. RIGHT: the "
                 "new radius fits inside the crown, and a tree grows.",
                 Vec3(A.x, A.y, 0.8f), 150.0f},
                {"BEFORE: 1, 2, 3, 4, 6 m",
                 "Grown with the old lengths. The small ones are bare "
                 "poles: trunk, and nothing on top.",
                 Vec3(B.x, B.y, 2.0f), 26.0f},
                {"AFTER: the same five heights",
                 "Same camera, same sizes, new lengths. Every one has a "
                 "crown.",
                 Vec3(C.x, C.y, 2.0f), 26.0f},
                {"REAL TREES from the shipping generator",
                 "Not the demo's own maths: actual Tree entities and "
                 "particles, 1 m to 6 m.",
                 Vec3(D.x, D.y, 2.0f), 26.0f},
            };
            const int n = static_cast<int>(sizeof(stations) /
                                           sizeof(stations[0]));
            for (int i = 0; i < n && !g_quit; ++i) {
                look_at_station(*e, stations[i]);
                hold(*e, stations[i], i, n);
            }
        }
        e->shutdown();
        delete e;
    }

    std::cout << "\n" << tests_passed << " passed, " << tests_failed
              << " failed" << (g_quit ? "  (stopped early)" : "")
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
