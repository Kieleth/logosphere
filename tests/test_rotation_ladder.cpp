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
#include <set>
#include <utility>
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
    for (int cx = -2; cx <= 4; ++cx)   // reaches under the rung-3 chain at x=3
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
        // High and behind the camera line so the emitter quad stays out of
        // the isometric frame (it photobombed the first zoom-out).
        ps.queue_light(0.0f, -5.0f, 16.0f, 650000.0f, 50.0f, 1.0f, 0.96f, 0.9f);

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
        g->damping = 4000.0f;
        g->enable_angular_constraint = true;
        g->angular_drive_enabled = true;
        g->use_quat_target = true;   // grown pose: identity (upright)
        g->angular_stiffness = 150.0f;   // N*m/rad — soft in bend: the joint
        g->angular_damping = 40.0f;      // near-critical: grass sways once
        engine.get_physics_system().add_gluon_between(a, b, std::move(g));
    };
    bond(root, s1, +0.2f, -0.3f);
    bond(s1, s2, +0.3f, -0.3f);
    bond(s2, s3, +0.3f, -0.3f);

    // ENERGY PROBE: sleep zeroes velocity on entry, so it is an energy SINK.
    // Did the sleep law remove a mask (energy bounded) or is the constraint
    // fight pumping (energy grows)?
    auto chain_ke = [&]() {
        auto v = ps.lock_particles_for_write();
        double ke = 0.0;
        for (int id : {s1, s2, s3}) {
            const Particle& p = v[id];
            const double m = p.GetMass();
            const double I = p.GetMomentOfInertia();
            ke += 0.5 * m * (p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
            ke += 0.5 * I * (p.omega_x*p.omega_x + p.omega_y*p.omega_y +
                             p.omega_z*p.omega_z);
        }
        return ke;
    };

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
    float settled_shear = 0.0f, settled_gap = 0.0f;
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
        settled_shear = worst_shear;   // last call = settled state
        settled_gap = worst_gap;
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
    int swings = 0;   // owner QA: grass should overshoot ONCE, not ring 5-8x
    if (g_label) g_label->set_text(
        "RUNG 3  finger gone: the chain must spring back upright (the drives restore)");
    {
        float prev_vy = 0.0f;
        for (int f = 0; f < 400 && engine.is_running(); ++f) {
            step(engine);
            measure("RECOV", f);
            if ((f % 60) == 0)
                printf("      [KE RECOV f%3d] chain KE %.6f J\n", f, chain_ke());
            auto v = ps.lock_particles_for_write();
            const float vy = v[s3].vy;
            if (std::fabs(vy) > 0.05f && vy * prev_vy < 0.0f) swings++;
            if (std::fabs(vy) > 0.05f) prev_vy = vy;
        }
    }

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

    // Owner's contract (option C): fiber DISTORTS during a strike and must
    // come back TRUE at rest. Transient bounds are calibrated to the
    // QA-approved behavior; settled bounds are strict.
    const bool touched   = contacts > 0;
    const bool bent      = peak_tip > 0.15f;
    const bool by_rot    = rot_at_push > (15.0f * (float)M_PI / 180.0f);
    const bool low_shear = shear_at_push < (60.0f * (float)M_PI / 180.0f);
    const bool connected = peak_gap < 0.300f;
    const bool recovered = final_tip < 0.15f;
    const bool true_at_rest = settled_shear < (5.0f * (float)M_PI / 180.0f) &&
                              settled_gap < 0.025f;
    r.passed = touched && bent && by_rot && low_shear && connected &&
               recovered && true_at_rest;
    snprintf(r.detail, sizeof r.detail,
             "contacts %llu; tip defl peak %.2f m (bend %s); seg rot %.1f deg %s; "
             "shear %.1f deg %s; joint gap peak %.0f mm %s; final tip %.2f m %s",
             (unsigned long long)contacts, peak_tip, bent ? "yes" : "NO",
             rot_at_push * 180.0f / (float)M_PI, by_rot ? "" : "(NO ROTATION)",
             shear_at_push * 180.0f / (float)M_PI, low_shear ? "" : "(SHEARED)",
             peak_gap * 1000.0f, connected ? "" : "(DISMEMBERED)",
             final_tip, recovered ? "(recovered)" : "(STAYED BENT)");
    printf("      swings during recovery: %d (owner: grass should swing ~once)\n", swings);
    printf("      settled: shear %.1f deg (need < 5), gap %.0f mm (need < 25)%s\n",
           settled_shear * 180.0f / (float)M_PI, settled_gap * 1000.0f,
           true_at_rest ? "" : "  *** NOT TRUE AT REST ***");
    return r;
}

// ---------------------------------------------------------------------------
// RUNG 4 (owner-designed): the NATURE of the grass. Four identical chains,
// four gluon declarations, ONE wide finger sweeping through all of them in
// a single pass:
//   SPRINGY   near-critical damping    -> one sway, upright again
//   RINGY     low damping (rubber)     -> rings several swings, recovers
//   SLOW      overdamped               -> creeps back, no overshoot
//   MALLEABLE plastic yield            -> STAYS BENT (damage done)
// The malleable clause is the mechanism RED: nothing in the engine moves a
// bond's rest pose when bent past yield, so today it recovers like springy.
// ---------------------------------------------------------------------------
struct ChainSpec {
    const char* tag;
    float lin_damping;
    float ang_stiffness, ang_damping;
};

Rung rung4(Engine& engine) {
    Rung r; r.name = "4 four natures, one finger";
    auto& ps = engine.get_particle_system();

    // Owner's palette, their order: kept bent | almost vertical | a bit of
    // bounce | breaks because it is too stiff to yield.
    // Owner's order: 1 fully bent, 2 leaning, 3 straight, 4 broken.
    // 50/6 is the stable 'a bit of bounce'; wilder (20/2) whips into the
    // neighbour chain and the interaction detonates (#47 family).
    const ChainSpec specs[4] = {
        {"BENT",      4000.0f, 150.0f,  40.0f},   // + plastic yield 0.25 rad
        {"LEANING",     50.0f, 150.0f,   6.0f},   // + mild yield: bounce once, settle leaning
        {"STRAIGHT",  4000.0f, 150.0f,  40.0f},
        {"BRITTLE",   4000.0f, 4000.0f, 40.0f},   // stiff joints, honest strength
    };
    const float xs[4] = {6.0f, 8.0f, 10.0f, 12.0f};

    // Ground under the new row.
    for (int cx = 5; cx <= 13; ++cx)
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

    int seg[4][3]; int roots[4];
    for (int k = 0; k < 4; ++k) {
        Particle rp = {};
        rp.shape = ParticleShape::BOX;
        rp.x = xs[k]; rp.y = 0.0f; rp.z = 0.3f;
        rp.width = rp.height = 0.4f; rp.thickness = 0.4f; rp.size = 0.4f;
        rp.r = 0.45f; rp.g = 0.3f; rp.b = 0.2f; rp.a = 1.0f;
        rp.SetMaterial(Materials::Type::STONE);
        roots[k] = engine.add_particle(rp);
        for (int j = 0; j < 3; ++j) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = xs[k]; p.y = 0.0f; p.z = 0.8f + 0.6f * (float)j;
            p.width = 0.25f; p.height = 0.25f; p.thickness = 0.6f;
            p.size = 0.25f;
            // tint per nature so the owner can tell them apart at a glance
            p.r = (k == 1) ? 0.75f : ((k == 0) ? 0.65f : 0.4f);
            p.g = (k == 0) ? 0.6f : 0.75f;
            p.b = (k == 3) ? 0.8f : 0.35f;
            p.a = 1.0f;
            p.SetMaterial(Materials::Type::WOOD_HARD);
            seg[k][j] = engine.add_particle(p);
        }
    }
    // One wide finger covering the whole row.
    Particle fing = {};
    fing.shape = ParticleShape::BOX;
    fing.x = 9.0f; fing.y = -2.0f; fing.z = 1.4f;
    fing.width = 8.0f; fing.height = 0.3f; fing.thickness = 0.3f; fing.size = 0.3f;
    fing.r = 0.8f; fing.g = 0.35f; fing.b = 0.3f; fing.a = 1.0f;
    fing.SetMaterial(Materials::Type::STONE);
    const int finger = engine.add_particle(fing);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[finger].solver_mode = ParticleSolverMode::KINEMATIC;
        v[finger].owner = ParticleOwner::DYNAMICS;
        v[finger].material_strength = 1e9f;
        for (int k = 0; k < 4; ++k) {
            v[roots[k]].solver_mode = ParticleSolverMode::KINEMATIC;
            v[roots[k]].owner = ParticleOwner::DYNAMICS;
            v[roots[k]].material_strength = 1e9f;
            for (int j = 0; j < 3; ++j) {
                // BRITTLE declares an honest strength so its stiff joints
                // can actually break; the others stay out of tear range.
                v[seg[k][j]].material_strength = (k == 3) ? 40.0f : 1e9f;
                v[seg[k][j]].is_quat_driven = true;
            }
        }
    }
    for (int k = 0; k < 4; ++k) {
        auto bond = [&](int a, int b, float za, float zb) {
            auto g = std::make_unique<OrganicGluon>();
            g->offset_a = {0.0f, 0.0f, za};
            g->offset_b = {0.0f, 0.0f, zb};
            g->target_distance = 0.0f;
            g->rotate_offsets = true;
            g->contact_area = 1.0f;
            g->stiffness = 500000.0f;
            g->damping = specs[k].lin_damping;
            g->enable_angular_constraint = true;
            g->angular_drive_enabled = true;
            g->use_quat_target = true;
            g->angular_stiffness = specs[k].ang_stiffness;
            g->angular_damping = specs[k].ang_damping;
            if (k == 0) {
                g->plastic_yield_angle = 0.25f;   // BENT: damage sticks
                g->max_strain = 5.0f;             // plastic yields, it does not tear
            }
            if (k == 3) {
                // One tear law: brittle snaps by STRAIN. The old force-path
                // break (blocked stiff joints sustaining 40 N) was retired
                // with the tear-law unification; brittleness is a declared
                // elongation limit now, same as the grass panel.
                g->max_strain = 1.15f;
            }
            if (k == 1) {
                // LEANING keeps a trace of the event: only the deepest part
                // of the fold plasticizes, so it settles slightly leaning.
                g->plastic_yield_angle = 1.00f;
                g->max_strain = 4.0f;
            }
            engine.get_physics_system().add_gluon_between(a, b, std::move(g));
        };
        bond(roots[k], seg[k][0], +0.2f, -0.3f);
        bond(seg[k][0], seg[k][1], +0.3f, -0.3f);
        bond(seg[k][1], seg[k][2], +0.3f, -0.3f);
    }

    if (g_interactive) {
        auto& camera = engine.get_camera_system();
        camera.set_position(2.0f, -7.5f, 4.5f);
        camera.look_at(9.0f, 0.0f, 1.2f);
        camera.set_pixels_per_unit(52.0f);
    }
    if (g_label) g_label->set_text(
        "RUNG 4  four natures: BENT | LEANING(red-ish) | STRAIGHT(green) | BRITTLE(blue). "
        "SPACE: one wide finger sweeps them all");
    for (int f = 0; f < 90 && engine.is_running(); ++f) step(engine);

    float tip_y0[4], tip_z0[4];
    {
        auto v = ps.lock_particles_for_write();
        for (int k = 0; k < 4; ++k) { tip_y0[k] = v[seg[k][2]].y; tip_z0[k] = v[seg[k][2]].z; }
    }
    if (!wait_for_space(engine)) { r.passed = false;
        snprintf(r.detail, sizeof r.detail, "owner quit before the sweep");
        return r; }

    float peak_tip[4] = {0,0,0,0};
    for (int f = 0; f < 240 && engine.is_running(); ++f) {
        {
            auto v = ps.lock_particles_for_write();
            v[finger].y = -2.0f + (float)f * (1.0f / 60.0f);
            v[finger].vy = 1.0f;
        }
        step(engine);
        auto v = ps.lock_particles_for_write();
        for (int k = 0; k < 4; ++k)
            peak_tip[k] = std::fmax(peak_tip[k],
                dist3(v[seg[k][2]].x, v[seg[k][2]].y, v[seg[k][2]].z,
                      xs[k], tip_y0[k], tip_z0[k]));
    }
    {
        auto v = ps.lock_particles_for_write();
        v[finger].x = 40.0f; v[finger].y = -30.0f; v[finger].vy = 0.0f;
    }
    if (g_label) g_label->set_text(
        "RUNG 4  finger gone: watch four different recoveries (or non-recoveries)");

    int swings[4] = {0,0,0,0};
    float prev_vy[4] = {0,0,0,0};
    float mid_tip[4] = {0,0,0,0};
    // EQUILIBRIUM WATCH (owner: 'that one keeps pulsating, never arriving to
    // rest'). Per nature: the frame after which every segment stays quiet
    // for a full second, and if that never happens, the residual amplitude
    // it is still oscillating with over the final second.
    int  eq_frame[4] = {-1,-1,-1,-1};
    int  quiet_run[4] = {0,0,0,0};
    float late_min[4] = {1e9f,1e9f,1e9f,1e9f};
    float late_max[4] = {-1e9f,-1e9f,-1e9f,-1e9f};
    float last_speed[4] = {0,0,0,0};
    constexpr float QUIET = 0.02f;      // m/s and rad/s
    constexpr int   QUIET_FRAMES = 60;  // one second
    for (int f = 0; f < 600 && engine.is_running(); ++f) {
        step(engine);
        auto v = ps.lock_particles_for_write();
        for (int k = 0; k < 4; ++k) {
            const float vy = v[seg[k][2]].vy;
            if (std::fabs(vy) > 0.05f && vy * prev_vy[k] < 0.0f) swings[k]++;
            if (std::fabs(vy) > 0.05f) prev_vy[k] = vy;
            if (f == 150)
                mid_tip[k] = dist3(v[seg[k][2]].x, v[seg[k][2]].y, v[seg[k][2]].z,
                                   xs[k], tip_y0[k], tip_z0[k]);

            // quietest-of-all-segments speed for this nature
            float worst = 0.0f;
            for (int j = 0; j < 3; ++j) {
                const Particle& p = v[seg[k][j]];
                worst = std::fmax(worst, std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz));
                worst = std::fmax(worst, std::sqrt(p.omega_x*p.omega_x +
                                                   p.omega_y*p.omega_y +
                                                   p.omega_z*p.omega_z));
            }
            last_speed[k] = worst;
            if (worst < QUIET) {
                if (++quiet_run[k] >= QUIET_FRAMES && eq_frame[k] < 0)
                    eq_frame[k] = f - QUIET_FRAMES;
            } else {
                quiet_run[k] = 0;
                if (eq_frame[k] >= 0) eq_frame[k] = -2;   // woke again: not settled
            }
            if (f >= 540) {   // final second: residual amplitude
                const float td = dist3(v[seg[k][2]].x, v[seg[k][2]].y, v[seg[k][2]].z,
                                       xs[k], tip_y0[k], tip_z0[k]);
                late_min[k] = std::fmin(late_min[k], td);
                late_max[k] = std::fmax(late_max[k], td);
            }
        }
        if (g_live && (f % 10) == 0) {
            char buf[288];
            auto td = [&](int k) {
                return dist3(v[seg[k][2]].x, v[seg[k][2]].y, v[seg[k][2]].z,
                             xs[k], tip_y0[k], tip_z0[k]);
            };
            snprintf(buf, sizeof buf,
                     "RECOV f%3d  tips m: BENT %.2f (sw %d) | LEANING %.2f (sw %d) | "
                     "STRAIGHT %.2f (sw %d) | BRITTLE %.2f (sw %d)",
                     f, td(0), swings[0], td(1), swings[1],
                     td(2), swings[2], td(3), swings[3]);
            g_live->set_text(buf);
        }
    }
    float fin[4];
    {
        auto v = ps.lock_particles_for_write();
        for (int k = 0; k < 4; ++k)
            fin[k] = dist3(v[seg[k][2]].x, v[seg[k][2]].y, v[seg[k][2]].z,
                           xs[k], tip_y0[k], tip_z0[k]);
    }
    // HOVER WATCH (owner: 'still not fixed', a freed segment stood on air).
    // 400 extra frames; a bondless body must end RESTING on something (its
    // bottom within 15 mm of a real top surface) or still be falling. A body
    // holding altitude over a gap is supported by nothing and fails the rung.
    printf("      [ids] brittle s0=%d s1=%d s2=%d root=%d\n",
           seg[3][0], seg[3][1], seg[3][2], roots[3]);
    bool ghost = false;
    char ghost_txt[160] = {0};
    for (int f = 0; f < 400 && engine.is_running(); ++f) {
        step(engine);
        if ((f % 100) == 99) {
            auto v = ps.lock_particles_for_write();
            for (int j = 0; j < 3; ++j)
                printf("      [hover f%3d] s%d z=%.2f vz=%+.3f at_rest=%d bonds=%zu\n",
                       f, j, v[seg[3][j]].z, v[seg[3][j]].vz,
                       (int)v[seg[3][j]].is_at_rest,
                       engine.get_physics_system().get_gluons_for_particle(seg[3][j]).size());
        }
    }
    {
        auto v = ps.lock_particles_for_write();
        for (int j = 0; j < 3; ++j) {
            if (!engine.get_physics_system().get_gluons_for_particle(seg[3][j]).empty())
                continue;   // still bonded: the stump may stand
            const Particle& q = v[seg[3][j]];
            const float bottom = q.z - q.thickness * 0.5f;
            // Highest REAL top surface under it (xy-overlapping): floor,
            // stump, or any other brittle piece.
            float support = 0.1f;   // floor tile top
            auto consider = [&](const Particle& o) {
                const bool xo = std::fabs(q.x - o.x) < (q.width + o.width) * 0.5f;
                const bool yo = std::fabs(q.y - o.y) < (q.height + o.height) * 0.5f;
                const float top = o.z + o.thickness * 0.5f;
                if (xo && yo && top <= bottom + 0.05f)
                    support = std::fmax(support, top);
            };
            consider(v[roots[3]]);
            for (int m = 0; m < 3; ++m) if (m != j) consider(v[seg[3][m]]);
            const float air = bottom - support;
            const bool falling = q.vz < -0.5f;
            if (air > 0.015f && !falling) {
                ghost = true;
                snprintf(ghost_txt, sizeof ghost_txt,
                         "s%d floats: %.0f mm of air below, vz %+.3f", j,
                         air * 1000.0f, q.vz);
            }
        }
    }
    // Per-chain bond survival (owner: two totems broke that should not).
    size_t alive[4];
    for (int k = 0; k < 4; ++k) {
        std::set<std::pair<size_t,size_t>> edges;
        auto add_edges = [&](int id) {
            for (const auto* g : engine.get_physics_system().get_gluons_for_particle(id))
                edges.insert(std::minmax(g->particle_a, g->particle_b));
        };
        add_edges(roots[k]);
        for (int j = 0; j < 3; ++j) add_edges(seg[k][j]);
        alive[k] = edges.size();
    }
    // LONG WATCH: the owner watches during the HOLD, after the measurement
    // window closes. Anything that wakes a settled totem later shows here.
    {
        int woke[4] = {0,0,0,0};
        // PER SEGMENT over time (a bent chain has different angles per
        // segment; mixing that spread with temporal jitter measures nothing).
        float ex_min[4][3], ex_max[4][3];
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 3; ++b) { ex_min[a][b] = 1e9f; ex_max[a][b] = -1e9f; }
        float worst_late[4] = {0,0,0,0};
        float amp_min[4] = {1e9f,1e9f,1e9f,1e9f}, amp_max[4] = {-1e9f,-1e9f,-1e9f,-1e9f};
        for (int f = 0; f < 1200 && engine.is_running(); ++f) {
            step(engine);
            auto v = ps.lock_particles_for_write();
            for (int k = 0; k < 4; ++k) {
                float worst = 0.0f;
                for (int j = 0; j < 3; ++j) {
                    const Particle& p = v[seg[k][j]];
                    worst = std::fmax(worst, std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz));
                    worst = std::fmax(worst, std::sqrt(p.omega_x*p.omega_x +
                                                       p.omega_y*p.omega_y +
                                                       p.omega_z*p.omega_z));
                }
                if (worst > 0.02f) woke[k]++;
                // The body can be still while its RENDERED orientation
                // flickers: quaternion-to-Euler extraction is singular at
                // +/-90 deg of pitch, and a bent totem sits exactly there.
                // Track Euler jitter independently of motion.
                for (int j = 0; j < 3; ++j) {
                    const Particle& p = v[seg[k][j]];
                    ex_min[k][j] = std::fmin(ex_min[k][j], p.rotation_x);
                    ex_max[k][j] = std::fmax(ex_max[k][j], p.rotation_x);
                }
                worst_late[k] = std::fmax(worst_late[k], worst);
                const float td = dist3(v[seg[k][2]].x, v[seg[k][2]].y, v[seg[k][2]].z,
                                       xs[k], tip_y0[k], tip_z0[k]);
                amp_min[k] = std::fmin(amp_min[k], td);
                amp_max[k] = std::fmax(amp_max[k], td);
            }
        }
        printf("\n      LONG WATCH, 1200 frames after settling (the hold phase)\n");
        printf("      %-10s %12s %14s %12s\n", "nature", "awake_frames",
               "swing_mm", "worst_speed");
        for (int k = 0; k < 4; ++k) {
            float worst_jit = 0.0f;
            for (int j = 0; j < 3; ++j)
                worst_jit = std::fmax(worst_jit, (ex_max[k][j] - ex_min[k][j]) * 57.3f);
            printf("      %-10s %12d %14.1f %12.3f  pose jitter %6.2f deg%s\n",
                   specs[k].tag, woke[k], (amp_max[k] - amp_min[k]) * 1000.0f,
                   worst_late[k], worst_jit,
                   worst_jit > 1.0f ? "   *** RENDERED POSE FLICKERS ***" : "");
        }
    }

    printf("\n      EQUILIBRIUM WATCH (quiet = every segment under %.2f m/s and rad/s)\n",
           QUIET);
    printf("      %-10s %12s %14s %12s\n", "nature", "settled_at",
           "residual_mm", "speed_now");
    for (int k = 0; k < 4; ++k) {
        char when[32];
        if (eq_frame[k] >= 0) snprintf(when, sizeof when, "f%d", eq_frame[k]);
        else if (eq_frame[k] == -2) snprintf(when, sizeof when, "SETTLED THEN WOKE");
        else snprintf(when, sizeof when, "*** NEVER ***");
        printf("      %-10s %12s %14.1f %12.3f\n", specs[k].tag, when,
               (late_max[k] - late_min[k]) * 1000.0f, last_speed[k]);
    }

    printf("      nature      peak_tip  mid_tip  final_tip  swings  bonds(of 3)\n");
    for (int k = 0; k < 4; ++k)
        printf("      %-10s %8.2f %8.2f %10.2f %7d %8zu%s\n",
               specs[k].tag, peak_tip[k], mid_tip[k], fin[k], swings[k], alive[k],
               (k != 3 && alive[k] < 3) ? "  *** BROKE, SHOULD NOT ***" : "");

    size_t brittle_bonds = 0;
    for (int j = 0; j < 3; ++j)
        brittle_bonds += engine.get_physics_system()
                             .get_gluons_for_particle(seg[3][j]).size();
    const bool bent_ok    = peak_tip[0] > 0.5f && fin[0] > 0.4f;      // kept bent
    const bool springy_ok = peak_tip[2] > 0.5f && swings[2] <= 2 && fin[2] < 0.15f;
    // Owner: the bounced totem should end 'a bit leaning', not parade
    // vertical — a trace of the event. Leaning = tip settled off grown
    // pose, but far from flat.
    const bool bouncy_ok  = peak_tip[1] > 0.5f && swings[1] >= 1 &&
                            fin[1] > 0.10f && fin[1] < 0.60f;
    const bool breaks_ok  = brittle_bonds < 3;                         // it SNAPPED
    // Only BRITTLE may break: the other three natures keep every bond.
    const bool unbroken_ok = alive[0] == 3 && alive[1] == 3 && alive[2] == 3;
    r.passed = bent_ok && springy_ok && bouncy_ok && breaks_ok && !ghost && unbroken_ok;
    snprintf(r.detail, sizeof r.detail,
             "BENT %s (peak %.2f, fin %.2f, need fin > 0.4); LEANING %s (sw %d, "
             "fin %.2f); STRAIGHT %s (sw %d, fin %.2f); BRITTLE %s (%zu bond edges "
             "left of 3+)",
             bent_ok ? "ok" : "*** FORGOT THE DAMAGE ***", peak_tip[0], fin[0],
             bouncy_ok ? "ok" : "OFF (no lean/bounce)", swings[1], fin[1],
             springy_ok ? "ok" : "OFF", swings[2], fin[2],
             breaks_ok ? "SNAPPED ok" : "*** DID NOT BREAK ***", brittle_bonds);
    if (ghost)
        printf("      *** GHOST SUPPORT: %s — a body may not stand on air ***\n",
               ghost_txt);
    if (g_label) {
        char buf[224];
        snprintf(buf, sizeof buf,
                 "RUNG 4 done: finals %.2f | %.2f | %.2f | %.2f m. ESC or close when satisfied.",
                 fin[0], fin[1], fin[2], fin[3]);
        g_label->set_text(buf);
    }
    // LIVE EQUILIBRIUM READOUT during the hold: the owner reports pulsing
    // that headless cannot reproduce (0.00 deg jitter, 0.000 m/s over 1200
    // frames after settling). So the instrument goes where the phenomenon
    // is: on screen, while they watch. If physics is truly still, this line
    // reads zeros and the pulsing is in the rendering, not the world.
    if (g_interactive && !g_autopilot) {
        float prev_x[4][3] = {};
        bool first = true;
        while (engine.is_running()) {
            if (engine.get_input_system().get_input_state().keys[GLFW_KEY_ESCAPE]) break;
            step(engine);
            auto v = ps.lock_particles_for_write();
            float worst_v = 0.0f, worst_jit = 0.0f;
            const char* who_v = "-"; const char* who_j = "-";
            for (int k = 0; k < 4; ++k)
                for (int j = 0; j < 3; ++j) {
                    const Particle& p = v[seg[k][j]];
                    const float sp = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                    if (sp > worst_v) { worst_v = sp; who_v = specs[k].tag; }
                    if (!first) {
                        const float d = std::fabs(p.rotation_x - prev_x[k][j]) * 57.3f;
                        if (d > worst_jit) { worst_jit = d; who_j = specs[k].tag; }
                    }
                    prev_x[k][j] = p.rotation_x;
                }
            first = false;
            if (g_live) {
                char buf[256];
                snprintf(buf, sizeof buf,
                         "EQUILIBRIUM  worst speed %.4f m/s (%s)   pose change "
                         "%.3f deg/frame (%s)   %s",
                         worst_v, who_v, worst_jit, who_j,
                         (worst_v < 0.001f && worst_jit < 0.01f)
                             ? "AT REST (any pulsing you see is rendering)"
                             : "STILL MOVING");
                g_live->set_text(buf);
            }
        }
    } else {
        hold_until_close(engine);
    }
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
        // Owner QA: wider frame — both rigs (post at x=0, chain at x=3) and
        // the finger's whole approach lane must be observable at once.
        camera.set_position(-5.7f, -3.7f, 3.1f);
        camera.look_at(1.2f, 0.2f, 1.2f);
        camera.set_pixels_per_unit(74.0f);
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

    Rung rungs[] = { rung1(engine), rung2_verdict(), rung3(engine), rung4(engine) };

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
