// =============================================================================
// SHADOW LOD ON A WALL: what LOD costs the SHADOW, not the object
// =============================================================================
// WHY THIS EXISTS SEPARATELY FROM test_sphere_lod_quality.
// That test judges a sphere by how the SPHERE looks, which is the criterion a
// rasterizer would use: pick LOD by screen size and nobody notices. This engine
// traces shadows against the same triangles, so the mesh is also the occluder,
// and shadow size is set by the LIGHT, not the camera.
//
// A sphere close to a light casts a hugely magnified shadow. The magnification
// is (light->wall distance) / (light->sphere distance), so an object small
// enough for the camera to demote can be projecting a facet-edged blob across
// the whole wall. Judging LOD on the object alone cannot see that. This scene
// makes it the entire point:
//
//   light .................. wall
//         S1   S2   S3   S4
//         10x  3.3x 1.7x 1.1x     <- same radius, wildly different shadows
//
// All four spheres are identical. Their shadows are not. Cycle LOD and watch
// the leftmost shadow's silhouette go polygonal long before the sphere does.
//
// CONTROLS
//   SPACE  cycle LOD (0=20, 1=80, 2=320, 3=1280 triangles/sphere)
//   S      toggle analytic smooth normals (shading only; rays still hit the
//          real triangles, which is exactly the tension worth seeing)
//   ESC    exit
//
// TWO TRAPS THIS AVOIDS, both documented in docs/VISUAL_TESTS.md and both
// present in test_sphere_lod_quality:
//   1. ui->draw_text between render() and present() is SILENTLY ERASED, because
//      present() clears the overlay plane first. That test's HUD has never been
//      visible. The readout here goes in the WINDOW TITLE.
//   2. Without poll_events() the input state is never refreshed: real presses
//      do nothing and the uninitialised key array fires phantom SPACE and ESC.
//      That is why the other test exits on its own the moment it opens.
//
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_shadow_lod_wall --no-head
//   ./build-release/logosphere-tests --test test_shadow_lod_wall --no-head   (headless smoke)
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/particle_geometry_v2.h"
#include "logosphere/rendering/render_pipeline.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int tri_count_for(int level) {
    int n = 20;
    for (int i = 0; i < level; ++i) n *= 4;
    return n;
}

// Kinematic and at rest: this scene is about light and geometry, not physics.
int add_static(Engine& engine, const Particle& proto) {
    Particle p = proto;
    int id = engine.add_particle(p);
    auto v = engine.get_particle_system().lock_particles_for_write();
    v[id].solver_mode = ParticleSolverMode::KINEMATIC;
    v[id].owner = ParticleOwner::DYNAMICS;
    v[id].is_at_rest = true;
    return id;
}

}  // namespace

bool test_shadow_lod_wall() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;

    printf("\n=== SHADOW LOD ON A WALL (%s) ===\n",
           interactive ? "interactive: SPACE=LOD, S=smooth, ESC=exit" : "headless smoke");

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.window_title = "Shadow LOD on a wall";
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }
    auto& ps = engine.get_particle_system();

    // ---- Floor -------------------------------------------------------------
    const int FLOOR = 30;
    for (int r = 0; r < FLOOR; ++r)
        for (int c = 0; c < FLOOR; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = c - FLOOR / 2.0f; p.y = r - FLOOR / 2.0f; p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.62f; p.g = 0.62f; p.b = 0.66f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            add_static(engine, p);
        }

    // ---- The wall ----------------------------------------------------------
    // Pale and large, so a shadow landing on it is unmissable. Vertical, facing
    // the lights, at y = +12.
    const float WALL_Y = 12.0f;
    for (int c = -15; c <= 15; ++c)
        for (int z = 0; z < 18; ++z) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)c; p.y = WALL_Y; p.z = 0.5f + z * 1.0f;
            p.width = 1.0f; p.height = 0.4f; p.thickness = 1.0f; p.size = 1.0f;
            p.r = 0.80f; p.g = 0.79f; p.b = 0.74f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            add_static(engine, p);
        }

    // ---- The four spheres --------------------------------------------------
    // IDENTICAL radius, placed at increasing distance from the light, so the
    // shadow each one throws differs by ~10x in size. That spread is the whole
    // experiment: LOD picked from the object's screen size is blind to it.
    const float LIGHT_Y = -8.0f, LIGHT_Z = 7.0f;
    const float sphere_y[4] = { -6.0f, -2.0f, 4.0f, 10.0f };
    const float sphere_x[4] = { -7.5f, -2.5f, 2.5f, 7.5f };
    for (int i = 0; i < 4; ++i) {
        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        p.x = sphere_x[i]; p.y = sphere_y[i]; p.z = 3.0f;
        p.width = p.height = p.thickness = 2.0f; p.size = 2.0f;
        p.r = 0.88f; p.g = 0.36f; p.b = 0.26f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        add_static(engine, p);
        const float mag = (WALL_Y - LIGHT_Y) / (sphere_y[i] - LIGHT_Y);
        printf("  sphere %d at y=%+.1f  ->  shadow magnification %.1fx\n", i, sphere_y[i], mag);
    }

    // One bright light behind the spheres, aimed at the wall. A single light
    // keeps the silhouette hard, which is what shows facet edges; more lights
    // would soften them and hide the thing under test.
    ps.queue_light(0.0f, LIGHT_Y, LIGHT_Z, 420000.0f, 60.0f, 1.0f, 0.96f, 0.90f);
    ps.flush_pending_particles();

    if (!interactive) {
        // Headless smoke: prove the scene builds and renders. The judgement
        // this test exists for is visual and cannot be made here.
        for (int f = 0; f < 40; ++f) { engine.update(1.0 / 60.0); engine.render(); }
        engine.get_renderer().wait_for_completion();
        printf("\n  headless smoke OK. Run with INTERACTIVE=1 to actually judge it.\n  PASS\n");
        engine.shutdown();
        return true;
    }

    const int levels[4] = {0, 1, 2, 3};
    int cur = 2;                       // start at today's default
    bool smooth = ::logosphere::get_smooth_sphere_normals();
    logosphere::set_sphere_lod(levels[cur]);

    bool space_was = false, s_was = false;
    long frame = 0;
    while (engine.is_running()) {
        engine.update(1.0 / 60.0);
        engine.render();

        // TRAP 1: the HUD goes in the window title. draw_text here would be
        // erased by present() and you would judge a picture with no label.
        if (GLFWwindow* win = (GLFWwindow*)engine.get_window_handle()) {
            char title[220];
            snprintf(title, sizeof(title),
                     "LOD %d (%d tris/sphere)  |  normals: %s  |  SPACE=LOD  S=smooth  ESC=exit",
                     levels[cur], tri_count_for(levels[cur]), smooth ? "SMOOTH" : "flat");
            glfwSetWindowTitle(win, title);
        }
        engine.present();

        // TRAP 2: without this the input array is never refreshed, real keys do
        // nothing, and uninitialised state fires phantom SPACE and ESC.
        engine.get_platform()->poll_events();
        ++frame;

        const auto& in = engine.get_input_system();
        const bool space_now = in.get_input_state().keys[GLFW_KEY_SPACE];
        if (space_now && !space_was) {
            cur = (cur + 1) % 4;
            logosphere::set_sphere_lod(levels[cur]);
            printf("  LOD %d: %d triangles/sphere\n", levels[cur], tri_count_for(levels[cur]));
            fflush(stdout);
        }
        space_was = space_now;

        const bool s_now = in.get_input_state().keys[GLFW_KEY_S];
        if (s_now && !s_was) {
            smooth = !smooth;
            ::logosphere::set_smooth_sphere_normals(smooth);
            printf("  smooth normals: %s\n", smooth ? "ON" : "off");
            fflush(stdout);
        }
        s_was = s_now;

        if (in.get_input_state().keys[GLFW_KEY_ESCAPE]) { printf("\n  ESC: exiting\n"); break; }
    }

    printf("\n=== closed after %ld frames ===\n", frame);
    engine.shutdown();
    return true;
}
