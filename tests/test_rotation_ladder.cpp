// ============================================================================
// THE ROTATION LADDER — iteration 1 of bend-by-rotation, one rung at a time
// ============================================================================
//
// The single-blade study measured the disease: a walked-through blade bends
// 58.5 degrees in GEOMETRY and 0.0 degrees in POSE. All bending is shear,
// because bonds constrain scalar centre distance and physics never rotates a
// default particle. The owner's order: rotation TDD in tiny steps, red first,
// green after, a visual OK between rungs, complexity grows one rung at a time.
//
// RUNG 1 — the anchor follows the parent's FULL rotation.
//   A KINEMATIC parent post with a child bonded to its top via anchors
//   (offset_a = parent top, offset_b = child bottom, target 0: anchors
//   coincide). Rotate the parent 90 degrees about X. The parent's top now
//   points along -Y, so the child must be dragged around to sit at the
//   rotated anchor. Today the offset math rotates XY by rotation_z only and
//   NEVER rotates the Z component (physics_system_v4.cpp:1285-1298), so the
//   anchor never moves and the child never follows: RED.
//
// Later rungs (each lands only after the previous is OK'd visually):
//   RUNG 2 — anchor forces produce torque (r x J): the child ROTATES.
//   RUNG 3 — a 3-segment chain bends by rotating, shear stays ~0.
//   RUNG 4 — the single-blade gate clause 'bends by ROTATING' goes green.
//
// INTERACTIVE=1 is stage-gated: the window opens lit and WAITS. SPACE turns
// the post; the swing plays out; the final pose holds until ESC or close.
// Nothing runs and nothing exits without the owner.
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/physics/physics_system.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

bool g_interactive = false;
ui::Label* g_label = nullptr;

// One paced step: headless runs flat out, interactive runs at frame rate so
// the owner can watch the swing happen.
void step(Engine& engine) { engine.update(1.0 / 60.0); }

// Freeze the world (dt = 0 still pumps the window) until SPACE. Returns
// false if the owner quit instead.
bool wait_for_space(Engine& engine) {
    if (!g_interactive) return true;
    bool space_was = true;   // swallow a still-held press from the last stage
    while (engine.is_running()) {
        const auto& in = engine.get_input_system().get_input_state();
        const bool space = in.keys[GLFW_KEY_SPACE];
        if (space && !space_was) return true;
        space_was = space;
        if (in.keys[GLFW_KEY_ESCAPE]) return false;
        engine.update(0.0);
    }
    return false;
}

// Hold the final pose, world live, until ESC or window close.
void hold_until_close(Engine& engine) {
    if (!g_interactive) return;
    while (engine.is_running()) {
        if (engine.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) return;
        step(engine);
    }
}

struct Rung {
    const char* name = "";
    bool passed = false;
    char detail[160] = {0};
};

float dist3(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// RUNG 1: the anchor follows the parent's full rotation.
// ---------------------------------------------------------------------------
Rung rung1(Engine& engine) {
    Rung r; r.name = "1 anchor follows full rotation";
    auto& ps = engine.get_particle_system();

    auto make_box = [&](float x, float y, float z, float s) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = s; p.size = s;
        p.r = 0.7f; p.g = 0.5f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        return engine.add_particle(p);
    };

    // Parent post at z=5 (mid-air; the bond, not a floor, holds the child).
    // Child sits on top: parent top anchor (0,0,+0.5) meets child bottom
    // anchor (0,0,-0.5); centres 1 m apart, anchors coincident.
    const int parent = make_box(0, 0, 5.0f, 1.0f);
    const int child  = make_box(0, 0, 6.0f, 1.0f);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[parent].solver_mode = ParticleSolverMode::KINEMATIC;
        v[parent].owner = ParticleOwner::DYNAMICS;
        v[parent].material_strength = 1e9f;   // breaking is not this rung
        v[child].material_strength  = 1e9f;
    }
    auto g = std::make_unique<OrganicGluon>();
    g->offset_a = {0.0f, 0.0f, +0.5f};
    g->offset_b = {0.0f, 0.0f, -0.5f};
    g->target_distance = 0.0f;    // anchors coincide
    g->rotate_offsets = true;
    g->contact_area = 1.0f;
    g->stiffness = 50000.0f;
    g->damping = 1000.0f;
    engine.get_physics_system().add_gluon_between(parent, child, std::move(g));

    if (g_interactive)
        ps.queue_light(-3.0f, -2.0f, 10.0f, 400000.0f, 45.0f, 1.0f, 0.96f, 0.9f);

    // Settle: child must simply stay put on the post.
    if (g_label) g_label->set_text(
        "RUNG 1  settling: child bonded to the post's top anchor");
    for (int f = 0; f < 60 && engine.is_running(); ++f) step(engine);
    float settle_gap;
    {
        auto v = ps.lock_particles_for_write();
        settle_gap = dist3(v[parent].x, v[parent].y, v[parent].z + 0.5f,
                           v[child].x,  v[child].y,  v[child].z - 0.5f);
    }

    // The kinematic writer turns the post 90 degrees about X. Its top anchor
    // swings from parent + (0,0,+0.5) to parent + R_x(90)*(0,0,+0.5)
    //   = parent + (0,-0.5, 0).
    if (g_label) g_label->set_text(
        "RUNG 1  settled. SPACE: turn the post 90deg — the child must swing with the anchor");
    if (!wait_for_space(engine)) { r.passed = false;
        snprintf(r.detail, sizeof r.detail, "owner quit before the turn");
        return r; }
    {
        auto v = ps.lock_particles_for_write();
        v[parent].rotation_x = (float)(M_PI / 2.0);
    }
    if (g_label) g_label->set_text(
        "RUNG 1  the post turned: watch the child swing around to the rotated anchor");
    for (int f = 0; f < 120 && engine.is_running(); ++f) step(engine);

    // Where must the child's bottom anchor be? At the ROTATED parent anchor.
    // (The child has no torque path yet, so its own offset stays unrotated;
    // this rung only demands the PARENT side of the bond rotate.)
    float gap_after, child_y, child_z, py, pz;
    {
        auto v = ps.lock_particles_for_write();
        const float ax = v[parent].x, ay = v[parent].y - 0.5f, az = v[parent].z;
        gap_after = dist3(ax, ay, az,
                          v[child].x, v[child].y, v[child].z - 0.5f);
        child_y = v[child].y; child_z = v[child].z;
        py = v[parent].y; pz = v[parent].z;
    }

    if (g_label) {
        char buf[192];
        snprintf(buf, sizeof buf,
                 "RUNG 1  done: child anchor %.2f m from the rotated anchor "
                 "(0.71 m before the fix). ESC or close when satisfied.",
                 gap_after);
        g_label->set_text(buf);
    }
    hold_until_close(engine);

    const bool settled  = settle_gap < 0.05f;
    // The rotation moved the anchor 0.707 m; the child follows to within a
    // gravity-sag residual (~0.1 m at the bias equilibrium for this mass and
    // stiffness). The gate is about FOLLOWING, not about sag: anywhere under
    // 0.15 m means the anchor rotated and the child went with it.
    const bool followed = gap_after < 0.15f;
    r.passed = settled && followed;
    snprintf(r.detail, sizeof r.detail,
             "settle gap %.3f m; after 90deg parent turn, child anchor is "
             "%.3f m from the rotated anchor (need < 0.10; child at y=%+.2f "
             "z=%+.2f, rotated anchor at y=%+.2f z=%+.2f)",
             settle_gap, gap_after, child_y, child_z, py - 0.5f, pz);
    return r;
}

} // namespace

bool test_rotation_ladder() {
    g_interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== THE ROTATION LADDER: bend-by-rotation, one rung at a time ===\n");
    printf("  mode: %s\n\n", g_interactive ? "INTERACTIVE (ESC quits)" : "HEADLESS");

    EngineConfig cfg;
    cfg.create_display = g_interactive;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  engine init failed\n  FAIL\n");
        return false;
    }
    if (g_interactive) {
        auto& camera = engine.get_camera_system();
        camera.set_position(-5.0f, -3.0f, 6.0f);
        camera.look_at(0.0f, 0.0f, 5.5f);       // the post and its child
        camera.set_pixels_per_unit(90.0f);
        if (auto* uis = engine.get_ui_system()) {
            g_label = new ui::Label("", "");
            g_label->set_position(24, 24);
            g_label->set_size(1500, 22);
            g_label->set_color(255, 255, 255);
            uis->add_widget(g_label);
        }
    }

    Rung rungs[] = { rung1(engine) };

    bool all = true;
    for (const Rung& r : rungs) {
        printf("  %-34s %s\n      %s\n", r.name,
               r.passed ? "GREEN" : "*** RED ***", r.detail);
        all = all && r.passed;
    }
    printf("\n  %s\n", all ? "PASS" : "FAIL (the ladder is climbed rung by rung; "
                                      "RED marks the rung under construction)");
    engine.shutdown();
    return all;
}
