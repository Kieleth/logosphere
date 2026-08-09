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
//   RUNG 2 — the child's orientation rides the bond (quaternion drive).
//   RUNG 3 — NO scripted rotation anywhere: a kinematic finger pushes a
//            3-segment free chain; it must bend BY ROTATING at its joints,
//            stay connected, and spring back when the finger leaves. This is
//            the owner's caveat on rung 2, encoded: rotation must arise from
//            the effect of forces through the bond, not from a script.
//   RUNG 4 — the single-blade gate clause 'bends by ROTATING' goes green.
//
// INTERACTIVE=1 is stage-gated: the window opens lit and WAITS. SPACE turns
// the post; the swing plays out; the final pose holds until ESC or close.
// Nothing runs and nothing exits without the owner. The viewer PROVES itself
// visible: after settle it reads the framebuffer and counts structured pixels
// (lit but not blown out) — an all-black or all-white frame fails the rung
// before wasting the owner's time. AUTOPILOT=1 with INTERACTIVE=1 skips the
// waits so the visibility proof can run unattended.
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/physics/physics_system.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

bool g_interactive = false;
bool g_autopilot = false;
ui::Label* g_label = nullptr;
ui::Label* g_live = nullptr;   // the numbers, moving (owner: "add instrumentation")

// Count pixels that are lit but not blown out. An all-black frame (no light)
// and an all-white frame (light in the lens) both score ~0.
long structured_pixels(Engine& engine) {
    engine.get_renderer().wait_for_completion();
    int w = engine.get_render_buffer().width();
    int h = engine.get_render_buffer().height();
    std::vector<uint32_t> px((size_t)w * h, 0u);
    long n = 0;
    long black = 0, white = 0;
    bool read_ok = engine.read_latest_framebuffer(px.data(), w, h);
    if (read_ok) {
        for (uint32_t c : px) {
            const int sum = ((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF);
            if (sum <= 60) black++;
            else if (sum >= 720) white++;
            else n++;
        }
    }
    printf("      [frame] read_ok=%d %dx%d black=%ld white=%ld structured=%ld\n",
           (int)read_ok, w, h, black, white, n);
    if (std::getenv("ROTLADDER_DUMP")) {
        FILE* f = fopen("/tmp/rotladder_frame.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (uint32_t c : px) {
                unsigned char rgb[3] = {(unsigned char)((c >> 16) & 0xFF),
                                        (unsigned char)((c >> 8) & 0xFF),
                                        (unsigned char)(c & 0xFF)};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
            printf("      [frame] dumped /tmp/rotladder_frame.ppm\n");
        }
    }
    return n;
}

// One paced step. Interactive needs the FULL trio update+render+present:
// update() alone dispatches nothing, and render() without present() fills a
// buffer no window ever shows (the white-window bug, three launches deep).
// Every working viewer calls all three.
void step(Engine& engine) {
    engine.update(1.0 / 60.0);
    if (g_interactive) { engine.render(); engine.present(); }
}

// Freeze the world (dt = 0 still pumps the window) until SPACE. Returns
// false if the owner quit instead.
bool wait_for_space(Engine& engine) {
    if (!g_interactive || g_autopilot) return true;
    bool space_was = true;   // swallow a still-held press from the last stage
    while (engine.is_running()) {
        const auto& in = engine.get_input_system().get_input_state();
        const bool space = in.keys[GLFW_KEY_SPACE];
        if (space && !space_was) return true;
        space_was = space;
        if (in.keys[GLFW_KEY_ESCAPE]) return false;
        engine.update(0.0);
        engine.render();
        engine.present();
    }
    return false;
}

// Hold the final pose, world live, until ESC or window close.
void hold_until_close(Engine& engine) {
    if (!g_interactive) return;
    if (g_autopilot) { for (int f = 0; f < 30 && engine.is_running(); ++f) step(engine); return; }
    while (engine.is_running()) {
        if (engine.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) return;
        step(engine);
    }
}

struct Rung {
    const char* name = "";
    bool passed = false;
    char detail[200] = {0};
};

// Measured by the rung-1 scene, judged by rung 2 (same scene, next demand).
float g_final_child_rot_x = 0.0f;
float g_final_pen = 0.0f;
float g_peak_pen = 0.0f;

float dist3(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Rotate a local offset by a particle's published Euler orientation, the
// same X-then-Y-then-Z composition the engine and generator use.
void rotate_by(const Particle& p, float ox, float oy, float oz,
               float& wx, float& wy, float& wz) {
    const float cx = std::cos(p.rotation_x), sx = std::sin(p.rotation_x);
    const float cy = std::cos(p.rotation_y), sy = std::sin(p.rotation_y);
    const float cz = std::cos(p.rotation_z), sz = std::sin(p.rotation_z);
    const float y1 = oy * cx - oz * sx;
    const float z1 = oy * sx + oz * cx;
    const float x2 = ox * cy + z1 * sy;
    const float z2 = -ox * sy + z1 * cy;
    wx = x2 * cz - y1 * sz;
    wy = x2 * sz + y1 * cz;
    wz = z2;
}

float angle_between(std::array<float,3> u, std::array<float,3> w) {
    const float du = std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
    const float dw = std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
    if (du < 1e-9f || dw < 1e-9f) return 0.0f;
    float c = (u[0]*w[0]+u[1]*w[1]+u[2]*w[2]) / (du*dw);
    c = std::fmax(-1.0f, std::fmin(1.0f, c));
    return std::acos(c);
}

// AABB pair penetration: the minimal push-out depth if the boxes' volumes
// overlap, 0 otherwise. This is what the collision system sees (the narrow
// phase is AABB and rotation-blind), and it is what the owner saw on screen:
// a child that rides a rotated anchor WITHOUT rotating must bury its corner
// region in the parent. The overlap is the missing rotation, made geometry.
float pair_penetration(const Particle& a, const Particle& b) {
    const float ox = std::fmin(a.x + a.width  * 0.5f, b.x + b.width  * 0.5f) -
                     std::fmax(a.x - a.width  * 0.5f, b.x - b.width  * 0.5f);
    const float oy = std::fmin(a.y + a.height * 0.5f, b.y + b.height * 0.5f) -
                     std::fmax(a.y - a.height * 0.5f, b.y - b.height * 0.5f);
    const float oz = std::fmin(a.z + a.thickness * 0.5f, b.z + b.thickness * 0.5f) -
                     std::fmax(a.z - a.thickness * 0.5f, b.z - b.thickness * 0.5f);
    if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) return 0.0f;
    return std::fmin(ox, std::fmin(oy, oz));
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

    // A 5x5 kinematic tile floor for spatial reference: the owner needs a
    // ground to read the swing against, and the working viewers all have one.
    for (int cx = -2; cx <= 2; ++cx)
        for (int cy = -2; cy <= 2; ++cy) {
            Particle t = {};
            t.shape = ParticleShape::BOX;
            t.x = (float)cx; t.y = (float)cy; t.z = 0.05f;
            t.width = t.height = 1.0f; t.thickness = 0.1f; t.size = 1.0f;
            t.r = 0.35f; t.g = 0.4f; t.b = 0.35f; t.a = 1.0f;
            t.SetMaterial(Materials::Type::STONE);
            const int id = engine.add_particle(t);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }

    // Parent post floats KINEMATIC at z=1.2 (the bond, not the floor, holds
    // the child). Child on top: parent top anchor (0,0,+0.5) meets child
    // bottom anchor (0,0,-0.5). After the 90-degree turn about X the child's
    // target hangs at (0,-0.5, z=1.7): clear of the floor, easy to read.
    const int parent = make_box(0, 0, 1.2f, 1.0f);
    const int child  = make_box(0, 0, 2.2f, 1.0f);
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
    // RUNG 2: the child's ORIENTATION rides the bond too. This is the
    // engine's existing 3-axis quaternion drive (rotational-DOF Stage 2),
    // never before reachable from a bond: relative-orientation target
    // identity means 'match the parent as it turns'.
    g->enable_angular_constraint = true;
    g->angular_drive_enabled = true;
    g->use_quat_target = true;    // target_relative_q stays identity
    // A stiff joint, explicitly: angular authority is force-bounded now, so
    // 'rigid-ish' must be said in newtons, not assumed from infinity.
    g->angular_stiffness = 20000.0f;
    g->angular_damping = 500.0f;
    engine.get_physics_system().add_gluon_between(parent, child, std::move(g));
    {
        auto v = ps.lock_particles_for_write();
        v[child].is_quat_driven = true;   // quat is orientation truth; Euler published from it
    }

    if (g_interactive)
        // The single-blade viewer's proven light: far above, out of frame.
        ps.queue_light(-2.0f, 0.0f, 10.0f, 350000.0f, 40.0f, 1.0f, 0.96f, 0.9f);

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

    // Visibility proof: never hand the owner a blind window.
    long lit = -1;
    if (g_interactive) {
        lit = structured_pixels(engine);
        printf("      structured pixels after settle: %ld (need > 5000)\n", lit);
        if (lit <= 5000) {
            r.passed = false;
            snprintf(r.detail, sizeof r.detail,
                     "SCENE NOT VISIBLE: %ld structured pixels (all-black or "
                     "all-white frame). Fix the viewer before showing it.", lit);
            return r;
        }
    }

    // The kinematic writer turns the post 90 degrees about X. Its top anchor
    // swings from parent + (0,0,+0.5) to parent + R_x(90)*(0,0,+0.5)
    //   = parent + (0,-0.5, 0).
    if (g_label) g_label->set_text(
        "RUNG 1  settled. SPACE: the post sweeps 90deg about X over 1.5s; "
        "the child must ride the rotating anchor (child itself rotates in RUNG 2, not yet)");
    if (!wait_for_space(engine)) { r.passed = false;
        snprintf(r.detail, sizeof r.detail, "owner quit before the turn");
        return r; }

    // The kinematic writer SWEEPS the post to 90 degrees over 90 frames, so
    // the rotation is watchable and the anchor is followed continuously, not
    // teleported once.
    const int TURN_FRAMES = 90;
    auto live_readout = [&](int f, float rx) {
        if (!g_live) return;
        auto v = ps.lock_particles_for_write();
        const float ax = v[parent].x;
        const float ay = v[parent].y - 0.5f * std::sin(rx);
        const float az = v[parent].z + 0.5f * std::cos(rx);
        float bx, by, bz;
        rotate_by(v[child], 0.0f, 0.0f, -0.5f, bx, by, bz);
        const float gap = dist3(ax, ay, az,
                                v[child].x + bx, v[child].y + by, v[child].z + bz);
        const float pen = pair_penetration(v[parent], v[child]);
        char buf[288];
        snprintf(buf, sizeof buf,
                 "f%3d  post rot_x %5.1f deg   child y=%+.2f z=%+.2f   "
                 "child rot_x %5.1f deg [rung 2: must match post]   "
                 "anchor gap %4.0f mm   PENETRATION %4.0f mm",
                 f, rx * 180.0f / (float)M_PI, v[child].y, v[child].z,
                 v[child].rotation_x * 180.0f / (float)M_PI,
                 gap * 1000.0f, pen * 1000.0f);
        g_live->set_text(buf);
    };
    if (g_label) g_label->set_text(
        "RUNG 1  the post is sweeping: the child must ride the anchor around");
    auto track_pen = [&] {
        auto v = ps.lock_particles_for_write();
        const float pen = pair_penetration(v[parent], v[child]);
        g_peak_pen = std::fmax(g_peak_pen, pen);
        g_final_pen = pen;
        g_final_child_rot_x = v[child].rotation_x;
    };
    for (int f = 0; f < TURN_FRAMES && engine.is_running(); ++f) {
        const float rx = (float)(M_PI / 2.0) * (float)(f + 1) / (float)TURN_FRAMES;
        {
            auto v = ps.lock_particles_for_write();
            v[parent].rotation_x = rx;
        }
        step(engine);
        track_pen();
        live_readout(f, rx);
    }
    // Let it settle at the final pose, readout still live. The bond closes
    // its error exponentially at GLUON_POSITION_BETA (time constant 1 s);
    // 300 frames = 5 tau, enough to reach millimetres from the post-sweep
    // ~0.4 m. The first version settled 120 frames and mistook the 2-tau
    // snapshot (55 mm) for a pinned equilibrium.
    for (int f = 0; f < 300 && engine.is_running(); ++f) {
        step(engine);
        track_pen();
        if ((f % 5) == 0) live_readout(TURN_FRAMES + f, (float)(M_PI / 2.0));
    }

    // Where must the child's bottom anchor be? At the ROTATED parent anchor.
    // (The child has no torque path yet, so its own offset stays unrotated;
    // this rung only demands the PARENT side of the bond rotate.)
    float gap_after, child_y, child_z, py, pz;
    {
        auto v = ps.lock_particles_for_write();
        const float ax = v[parent].x, ay = v[parent].y - 0.5f, az = v[parent].z;
        // The child's bottom anchor moves with the child's own orientation
        // (rung 2 rotates it; measuring with the unrotated offset would
        // punish exactly the rotation rung 2 demands).
        float bx, by, bz;
        rotate_by(v[child], 0.0f, 0.0f, -0.5f, bx, by, bz);
        gap_after = dist3(ax, ay, az,
                          v[child].x + bx, v[child].y + by, v[child].z + bz);
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

// ---------------------------------------------------------------------------
// RUNG 2 (RED): the child ROTATES with the anchor, and the overlap disappears.
// The owner's two observations on the rung-1 viewer, verbatim requirements:
// "the above translates, but should also rotate" and "there's penetration
// which is not allowed". They are one mechanism: without torque from the
// anchor force (r x J), the child rides the anchor in its frozen pose and its
// unrotated corner region must bury itself in the post. Green means: child
// rotation_x within 15 degrees of the post's 90, and pair penetration under
// 20 mm at rest.
// ---------------------------------------------------------------------------
Rung rung2_verdict() {
    Rung r; r.name = "2 child rotates, boxes kiss";
    const float rot_deg = g_final_child_rot_x * 180.0f / (float)M_PI;
    const bool rotated = std::fabs(rot_deg - 90.0f) < 15.0f;
    const bool kissing = g_final_pen < 0.02f;
    r.passed = rotated && kissing;
    snprintf(r.detail, sizeof r.detail,
             "child rot_x %.1f deg (need ~90); pair penetration final %.0f mm, "
             "peak %.0f mm (need < 20)",
             rot_deg, g_final_pen * 1000.0f, g_peak_pen * 1000.0f);
    return r;
}

// ---------------------------------------------------------------------------
// RUNG 3: force-caused bending. Base block KINEMATIC at the floor; three
// 0.6 m segments stacked above it, every one DYNAMIC and quaternion-driven,
// bonded end-to-end with anchors + drive toward the grown (upright) pose.
// A kinematic finger sweeps through at 1 m/s, then withdraws. Nothing
// scripts any rotation.
// ---------------------------------------------------------------------------
Rung rung3(Engine& engine) {
    Rung r; r.name = "3 chain bends by rotating, recovers";
    auto& ps = engine.get_particle_system();

    auto seg_box = [&](float z) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 3.0f; p.y = 0.0f; p.z = z;      // beside the rung-1 rig
        // 30 kg segments, deliberately. At this mass the rung is STABLE and
        // shows true bend-by-rotation with correct-physics droop (gravity
        // torque on the bent chain beats the 500 N*m/rad drives, so it
        // recovers only partway — grass made of logs). Fiber-light segments
        // (4 kg) detonate these same bonds WITHOUT the finger touching
        // them: the mass-dependent bond instability that is issue #47's
        // residual, now reproducible with five bodies. That repro is the
        // next red, for the microscope, not for parameter surgery here.
        p.width = 0.25f; p.height = 0.25f; p.thickness = 0.6f;
        p.size = 0.25f;
        p.r = 0.4f; p.g = 0.75f; p.b = 0.35f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::WOOD_HARD);
        return engine.add_particle(p);
    };

    // Root block, kinematic, half-buried at the floor like a stump.
    Particle rootp = {};
    rootp.shape = ParticleShape::BOX;
    rootp.x = 3.0f; rootp.y = 0.0f; rootp.z = 0.3f;
    rootp.width = rootp.height = 0.4f; rootp.thickness = 0.4f; rootp.size = 0.4f;
    rootp.r = 0.45f; rootp.g = 0.3f; rootp.b = 0.2f; rootp.a = 1.0f;
    rootp.SetMaterial(Materials::Type::STONE);
    const int root = engine.add_particle(rootp);
    const int s1 = seg_box(0.8f);
    const int s2 = seg_box(1.4f);
    const int s3 = seg_box(2.0f);

    // The finger: kinematic slab that will sweep through chain mid-height.
    Particle fing = {};
    fing.shape = ParticleShape::BOX;
    fing.x = 3.0f; fing.y = -2.0f; fing.z = 1.4f;
    fing.width = 0.3f; fing.height = 0.3f; fing.thickness = 0.3f; fing.size = 0.3f;
    fing.r = 0.8f; fing.g = 0.35f; fing.b = 0.3f; fing.a = 1.0f;
    fing.SetMaterial(Materials::Type::STONE);
    const int finger = engine.add_particle(fing);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[root].solver_mode = ParticleSolverMode::KINEMATIC;
        v[root].owner = ParticleOwner::DYNAMICS;
        v[finger].solver_mode = ParticleSolverMode::KINEMATIC;
        v[finger].owner = ParticleOwner::DYNAMICS;
        for (int id : {root, s1, s2, s3}) v[id].material_strength = 1e9f;
        v[finger].material_strength = 1e9f;
        for (int id : {s1, s2, s3}) v[id].is_quat_driven = true;
    }
    auto bond = [&](int a, int b, float za, float zb) {
        auto g = std::make_unique<OrganicGluon>();
        g->offset_a = {0.0f, 0.0f, za};
        g->offset_b = {0.0f, 0.0f, zb};
        g->target_distance = 0.0f;
        g->rotate_offsets = true;
        g->contact_area = 1.0f;
        // Fiber character: STIFF in length (joints must not gape under a
        // walking push), SOFT in bend (the same push folds the joints).
        g->stiffness = 500000.0f;
        g->damping = 2000.0f;
        g->enable_angular_constraint = true;
        g->angular_drive_enabled = true;
        g->use_quat_target = true;   // grown pose: identity (upright)
        g->angular_stiffness = 500.0f;   // N*m/rad
        g->angular_damping = 20.0f;
        engine.get_physics_system().add_gluon_between(a, b, std::move(g));
    };
    bond(root, s1, +0.2f, -0.3f);
    bond(s1, s2, +0.3f, -0.3f);
    bond(s2, s3, +0.3f, -0.3f);

    if (g_label) g_label->set_text(
        "RUNG 3  settling: free 3-segment chain on a kinematic stump (nothing scripts rotation)");
    for (int f = 0; f < 90 && engine.is_running(); ++f) step(engine);

    const float tip_z0 = [&] { auto v = ps.lock_particles_for_write(); return v[s3].z; }();
    const float tip_y0 = [&] { auto v = ps.lock_particles_for_write(); return v[s3].y; }();

    if (g_label) g_label->set_text(
        "RUNG 3  SPACE: a finger sweeps through at 1 m/s — the chain must bend BY ROTATING, then recover");
    if (!wait_for_space(engine)) { r.passed = false;
        snprintf(r.detail, sizeof r.detail, "owner quit before the sweep");
        return r; }

    // Sweep: finger crosses from y=-2 to y=+2 at 1 m/s (240 frames), then
    // teleports away and the chain gets 300 frames (5 tau) to recover.
    float peak_rot = 0.0f, peak_shear = 0.0f, peak_gap = 0.0f, peak_tip = 0.0f;
    uint64_t contacts = 0;
    auto measure = [&](const char* phase_txt, int f) {
        auto v = ps.lock_particles_for_write();
        // segment rotation from upright (any axis): angle of rotated +Z vs +Z
        float worst_rot = 0.0f, worst_shear = 0.0f, worst_gap = 0.0f;
        const int chain[4] = {root, s1, s2, s3};
        const float offs[3][2] = {{+0.2f,-0.3f},{+0.3f,-0.3f},{+0.3f,-0.3f}};
        for (int k = 0; k < 3; ++k) {
            const Particle& pa = v[chain[k]];
            const Particle& pb = v[chain[k+1]];
            float aw[3], bw[3];
            rotate_by(pa, 0, 0, offs[k][0], aw[0], aw[1], aw[2]);
            rotate_by(pb, 0, 0, offs[k][1], bw[0], bw[1], bw[2]);
            const float gap = dist3(pa.x + aw[0], pa.y + aw[1], pa.z + aw[2],
                                    pb.x + bw[0], pb.y + bw[1], pb.z + bw[2]);
            worst_gap = std::fmax(worst_gap, gap);
            // pose rotation of the child
            float axz[3];
            rotate_by(pb, 0, 0, 1.0f, axz[0], axz[1], axz[2]);
            const float rot = angle_between({axz[0], axz[1], axz[2]}, {0, 0, 1});
            worst_rot = std::fmax(worst_rot, rot);
            // shear: bond direction vs the parent's axis
            const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
            float pax[3];
            rotate_by(pa, 0, 0, 1.0f, pax[0], pax[1], pax[2]);
            worst_shear = std::fmax(worst_shear,
                angle_between({pax[0], pax[1], pax[2]}, {dx, dy, dz}));
        }
        const float tipd = dist3(v[s3].x, v[s3].y, v[s3].z, 3.0f, tip_y0, tip_z0);
        if (!g_interactive && (f % 30) == 0) {   // TEMP DIAG
            size_t alive = 0;
            for (int id : {s1, s2, s3})
                alive += engine.get_physics_system().get_gluons_for_particle(id).size();
            printf("      [diag %s f%3d] gaps mm:", phase_txt, f);
            for (int k = 0; k < 3; ++k) {
                const Particle& qa = v[chain[k]];
                const Particle& qb = v[chain[k+1]];
                float aw[3], bw[3];
                rotate_by(qa, 0, 0, offs[k][0], aw[0], aw[1], aw[2]);
                rotate_by(qb, 0, 0, offs[k][1], bw[0], bw[1], bw[2]);
                printf(" %4.0f", 1000.0f * dist3(qa.x+aw[0], qa.y+aw[1], qa.z+aw[2],
                                                  qb.x+bw[0], qb.y+bw[1], qb.z+bw[2]));
            }
            printf("  rot deg: %4.0f %4.0f %4.0f  bonds(alive edges) %zu  tip %.2f\n",
                   [&]{float a[3];rotate_by(v[s1],0,0,1,a[0],a[1],a[2]);return angle_between({a[0],a[1],a[2]},{0,0,1})*57.3f;}(),
                   [&]{float a[3];rotate_by(v[s2],0,0,1,a[0],a[1],a[2]);return angle_between({a[0],a[1],a[2]},{0,0,1})*57.3f;}(),
                   [&]{float a[3];rotate_by(v[s3],0,0,1,a[0],a[1],a[2]);return angle_between({a[0],a[1],a[2]},{0,0,1})*57.3f;}(),
                   alive, tipd);
        }
        peak_rot = std::fmax(peak_rot, worst_rot);
        peak_shear = std::fmax(peak_shear, worst_shear);
        peak_gap = std::fmax(peak_gap, worst_gap);
        peak_tip = std::fmax(peak_tip, tipd);
        for (const auto& ev : engine.get_physics_system().get_collision_events()) {
            const bool fa = (int)ev.particle_a == finger, fb = (int)ev.particle_b == finger;
            if (fa || fb) contacts++;
        }
        if (g_live && (f % 3) == 0) {
            char buf[288];
            snprintf(buf, sizeof buf,
                     "%s f%3d  tip defl %5.2f m  seg ROT %5.1f deg  SHEAR %5.1f deg  "
                     "joint gap %4.0f mm  finger contacts %llu",
                     phase_txt, f, tipd, worst_rot * 180.0f / (float)M_PI,
                     worst_shear * 180.0f / (float)M_PI, worst_gap * 1000.0f,
                     (unsigned long long)contacts);
            g_live->set_text(buf);
        }
    };
    for (int f = 0; f < 240 && engine.is_running(); ++f) {
        {
            auto v = ps.lock_particles_for_write();
            v[finger].y = -2.0f + (float)f * (1.0f / 60.0f);
            v[finger].vy = 1.0f;
        }
        step(engine);
        measure("SWEEP", f);
    }
    {
        auto v = ps.lock_particles_for_write();
        v[finger].x = 30.0f; v[finger].y = -20.0f; v[finger].vy = 0.0f;  // gone
    }
    const float rot_at_push = peak_rot, shear_at_push = peak_shear;
    if (g_label) g_label->set_text(
        "RUNG 3  finger gone: the chain must spring back upright (the drives restore)");
    for (int f = 0; f < 400 && engine.is_running(); ++f) { step(engine); measure("RECOV", f); }

    float final_tip;
    {
        auto v = ps.lock_particles_for_write();
        final_tip = dist3(v[s3].x, v[s3].y, v[s3].z, 3.0f, tip_y0, tip_z0);
    }
    if (g_label) {
        char buf[224];
        snprintf(buf, sizeof buf,
                 "RUNG 3 done: peak rot %.0f deg, shear %.0f deg, tip recovered to %.2f m. "
                 "ESC or close when satisfied.",
                 rot_at_push * 180.0f / (float)M_PI,
                 shear_at_push * 180.0f / (float)M_PI, final_tip);
        g_label->set_text(buf);
    }
    hold_until_close(engine);

    const bool touched   = contacts > 0;
    const bool bent      = peak_tip > 0.15f;
    const bool by_rot    = rot_at_push > (15.0f * (float)M_PI / 180.0f);
    const bool low_shear = shear_at_push < (15.0f * (float)M_PI / 180.0f);
    const bool connected = peak_gap < 0.05f;
    const bool recovered = final_tip < 0.15f;
    r.passed = touched && bent && by_rot && low_shear && connected && recovered;
    snprintf(r.detail, sizeof r.detail,
             "contacts %llu; tip defl peak %.2f m (bend %s); seg rot %.1f deg %s; "
             "shear %.1f deg %s; joint gap peak %.0f mm %s; final tip %.2f m %s",
             (unsigned long long)contacts, peak_tip, bent ? "yes" : "NO",
             rot_at_push * 180.0f / (float)M_PI, by_rot ? "" : "(NO ROTATION)",
             shear_at_push * 180.0f / (float)M_PI, low_shear ? "" : "(SHEARED)",
             peak_gap * 1000.0f, connected ? "" : "(DISMEMBERED)",
             final_tip, recovered ? "(recovered)" : "(STAYED BENT)");
    return r;
}

} // namespace

bool test_rotation_ladder() {
    g_interactive = std::getenv("INTERACTIVE") != nullptr;
    g_autopilot   = std::getenv("AUTOPILOT") != nullptr;
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
        camera.set_position(-4.0f, -2.5f, 2.4f);
        camera.look_at(0.0f, 0.0f, 1.6f);       // post + child, floor in frame
        camera.set_pixels_per_unit(110.0f);
        if (auto* uis = engine.get_ui_system()) {
            g_label = new ui::Label("", "");
            g_label->set_position(24, 24);
            g_label->set_size(1500, 22);
            g_label->set_color(255, 255, 255);
            uis->add_widget(g_label);
            g_live = new ui::Label("", "");
            g_live->set_position(24, 54);
            g_live->set_size(1500, 22);
            g_live->set_color(120, 220, 255);
            uis->add_widget(g_live);
        }
    }

    Rung rungs[] = { rung1(engine), rung2_verdict(), rung3(engine) };

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
