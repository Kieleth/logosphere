// ============================================================================
// ROTATED BOX-BOX NARROW PHASE — the contact matches the geometry
// ============================================================================
// Before this path existed, every box-box contact normal was a world axis:
// the walk-through-grass canary measured (0,-1,0) and (0,0,-1) as the ONLY
// normals between feet and tilted blade segments, and test_settling_flat's
// tipped plate collided as its upright slab and rested on 276 mm of air.
//
// Part 1 exercises narrow_phase_obb directly, no engine: measured normals,
// point counts, penetrations, against hand-computed geometry. Includes the
// CONTROL (unrotated boxes must reproduce the AABB path's answer) so a
// vacuous pass cannot hide.
//
// Part 2 drops a quarter-turned plate onto a support box inside the real
// engine and measures the air underneath where its ROTATED shape says its
// lowest point is. The support is tall enough that the turtle plane (which
// still reads world-Z extents) stays out of the picture; that boundary is
// tracked separately.
//
// LAWS (assert-protocol migration, 2026-08-21): this file is INV-12 end to
// end — a rotated box collides as a rotated box, rests flush on its true
// faces, and no bound under-covers the body it stands for. Normal DIRECTION
// checks cite INV-25 (one documented sign, A toward B). The control block
// cites G-19: for a body that never turned, the oriented path must reproduce
// the old answer exactly, which is the migration's own safety property. The
// oblique-normal case cites INV-6: the normal comes from the contact geometry
// and never from a world axis.
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/narrow_phase.h"

#include <cmath>
#include <cstdio>

namespace {

int checks_run = 0, checks_failed = 0;

void check(bool ok, const char* what) {
    checks_run++;
    if (!ok) checks_failed++;
    printf("      %-58s %s\n", what, ok ? "ok" : "*** FAIL ***");
}

Particle box_particle(float x, float y, float z,
                      float w, float h, float t,
                      float rx, float ry, float rz) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y; p.z = z;
    p.width = w; p.height = h; p.thickness = t; p.size = w;
    p.rotation_x = rx; p.rotation_y = ry; p.rotation_z = rz;
    return p;
}

} // namespace

bool test_rotated_box_contact() {
    printf("\n=== ROTATED BOX-BOX NARROW PHASE ===\n");

    // ------------------------------------------------------------------
    // Part 1a — CONTROL: unrotated boxes through the OBB path must agree
    // with the AABB path. A small box resting 5 mm into a slab's top.
    // ------------------------------------------------------------------
    printf("    [1a] control: unrotated box on slab, OBB vs AABB path\n");
    {
        Particle a = box_particle(0, 0, 0.545f, 0.4f, 0.4f, 0.9f, 0, 0, 0);
        Particle b = box_particle(0, 0, 0.05f, 1.0f, 1.0f, 0.1f, 0, 0, 0);
        check(!box_particle_is_rotated(a) && !box_particle_is_rotated(b),
              "hygiene: control boxes read as unrotated (a vacuous pass cannot hide)");

        ContactManifold m_obb, m_aabb;
        const bool hit_obb = narrow_phase_obb(
            obb_of_box_particle(a, a.z), obb_of_box_particle(b, b.z),
            0, 1, 0.08f, m_obb);
        const AABB6 aa{-0.2f, 0.2f, -0.2f, 0.2f, 0.095f, 0.995f};
        const AABB6 bb{-0.5f, 0.5f, -0.5f, 0.5f, 0.0f, 0.1f};
        const bool hit_aabb = narrow_phase_aabb(aa, bb, 0, 1, 0.08f, m_aabb);

        printf("      [measure] OBB n=(%.3f,%.3f,%.3f) pts=%d pen=%.4f | "
               "AABB n=(%.3f,%.3f,%.3f) pts=%d pen=%.4f\n",
               m_obb.normal_x, m_obb.normal_y, m_obb.normal_z,
               m_obb.num_points, hit_obb ? m_obb.points[0].penetration : -1.0f,
               m_aabb.normal_x, m_aabb.normal_y, m_aabb.normal_z,
               m_aabb.num_points, hit_aabb ? m_aabb.points[0].penetration : -1.0f);
        check(hit_obb && hit_aabb, "hygiene: both paths detect the contact");
        check(std::fabs(m_obb.normal_x - m_aabb.normal_x) < 1e-4f &&
              std::fabs(m_obb.normal_y - m_aabb.normal_y) < 1e-4f &&
              std::fabs(m_obb.normal_z - m_aabb.normal_z) < 1e-4f,
              "INV-25 + G-19: normals agree, B toward A (+Z). A body that never turned must behave bit-identically under the oriented path");
        check(m_obb.num_points == 4, "INV-12: face manifold carries 4 points");
        check(std::fabs(m_obb.points[0].penetration - 0.005f) < 1e-3f,
              "INV-2: penetration ~5 mm, as constructed");
    }

    // ------------------------------------------------------------------
    // Part 1b — a quarter-turned plate (rotation_x = 90 deg) meeting a
    // slab top. Its local Y (height 0.05) now spans world Z: contact is
    // the plate's thin face, normal +Z, NOT the upright slab's fat side.
    // ------------------------------------------------------------------
    printf("    [1b] quarter-turned plate on slab top\n");
    {
        // Plate: W=0.40 H=0.05 T=0.60, tipped 90 deg about X, center 20 mm
        // over the slab top => lowest point 0.020 - 0.025 = -0.005 (5 mm in).
        Particle a = box_particle(0, 0, 0.12f, 0.40f, 0.05f, 0.60f,
                                  (float)(M_PI / 2.0), 0, 0);
        Particle b = box_particle(0, 0, 0.05f, 1.0f, 1.0f, 0.1f, 0, 0, 0);
        check(box_particle_is_rotated(a), "hygiene: tipped plate reads as rotated");

        ContactManifold m;
        const bool hit = narrow_phase_obb(
            obb_of_box_particle(a, a.z), obb_of_box_particle(b, b.z),
            0, 1, 0.08f, m);
        printf("      [measure] n=(%.3f,%.3f,%.3f) pts=%d pen[0]=%.4f face=%d\n",
               m.normal_x, m.normal_y, m.normal_z, m.num_points,
               hit ? m.points[0].penetration : -1.0f, (int)m.is_face_contact);
        check(hit, "INV-12: contact detected");
        check(m.normal_z > 0.99f, "INV-12/INV-25: normal is +Z (B toward A), from GEOMETRY not from a world-up vector");
        check(m.is_face_contact, "INV-12: face contact, not corner");
        check(m.num_points == 4, "INV-12: 4 clipped points");
        float worst_pen = -1.0f;
        for (int i = 0; i < m.num_points; ++i)
            worst_pen = std::fmax(worst_pen, m.points[i].penetration);
        check(std::fabs(worst_pen - 0.005f) < 1e-3f,
              "INV-12: deepest point ~5 mm — the ROTATED extent, not the 0.3 m bounding slab");
    }

    // ------------------------------------------------------------------
    // Part 1c — a 30-degree-tipped plate digging its low edge into the
    // slab. Minimum-translation axis is the slab's top face: normal +Z,
    // and every contact point sits near the plate's LOW edge, not at its
    // unrotated footprint.
    // ------------------------------------------------------------------
    printf("    [1c] 30-degree-tipped plate, low edge in the slab\n");
    {
        const float rx = (float)(30.0 * M_PI / 180.0);
        // Plate 0.4 x 0.05 x 0.6, tipped 30 deg about X. Down-reach of the
        // rotated shape below its center: 0.5*(T*cos(30) + H*sin(30)) =
        // 0.2723 (at rx=0 this is T/2, the upright case — the control that
        // caught a wrong formula here). Center at slab_top + reach - 0.005
        // => the LOW edge digs exactly 5 mm into the slab.
        const float down = 0.5f * (0.60f * std::cos(rx) + 0.05f * std::sin(rx));
        Particle a = box_particle(0, 0, 0.1f + down - 0.005f,
                                  0.40f, 0.05f, 0.60f, rx, 0, 0);
        Particle b = box_particle(0, 0, 0.05f, 2.0f, 2.0f, 0.1f, 0, 0, 0);

        ContactManifold m;
        const bool hit = narrow_phase_obb(
            obb_of_box_particle(a, a.z), obb_of_box_particle(b, b.z),
            0, 1, 0.08f, m);
        printf("      [measure] n=(%.3f,%.3f,%.3f) pts=%d face=%d\n",
               m.normal_x, m.normal_y, m.normal_z, m.num_points,
               (int)m.is_face_contact);
        check(hit, "INV-12: contact detected");
        check(m.normal_z > 0.99f && std::fabs(m.normal_x) < 1e-3f &&
              std::fabs(m.normal_y) < 1e-3f,
              "INV-12/INV-25: normal +Z, the slab face the plate actually rests on");
        // rotation_x = +30 deg tips local +Z toward -Y, so the plate's LOW
        // long edge swings to +Y (corner y = H/2*cos + T/2*sin = +0.128).
        // Every point deeper than the margin band must sit on that side,
        // and the deepest must be the constructed 5 mm.
        bool low_edge_side = true;
        float deepest = -1.0f;
        for (int i = 0; i < m.num_points; ++i) {
            deepest = std::fmax(deepest, m.points[i].penetration);
            if (m.points[i].penetration > 1e-4f && m.points[i].py < 0.0f)
                low_edge_side = false;
            printf("        point %d: (%.3f, %.3f, %.3f) pen %.4f\n",
                   i, m.points[i].px, m.points[i].py, m.points[i].pz,
                   m.points[i].penetration);
        }
        check(low_edge_side, "INV-12: penetrating points cluster at the low (+Y) edge, where the tilted shape is");
        check(std::fabs(deepest - 0.005f) < 2e-3f, "INV-2: deepest ~5 mm as constructed");
    }

    // ------------------------------------------------------------------
    // Part 1d — lateral push: a yawed blade against a box face must give
    // a normal with a LATERAL component matching the blade's yaw, the
    // component the axis-locked path could never produce.
    // ------------------------------------------------------------------
    printf("    [1d] yawed thin blade vs box face: oblique normal\n");
    {
        const float yaw = (float)(30.0 * M_PI / 180.0);
        // Blade: thin wide plate yawed 30 deg about Z (engine yaw is CW
        // from +Z, so local +Y maps to (sin 30, cos 30, 0) = (0.5, 0.866)).
        Particle blade = box_particle(0, 0, 0.5f, 0.30f, 0.02f, 1.0f,
                                      0, 0, yaw);
        // Box centered along the blade's face normal, 2 mm into the face:
        // distance = blade H/2 + box half - 0.002 = 0.108. That makes the
        // blade's thin-axis overlap (2 mm) the unambiguous SAT minimum —
        // every box-face overlap is 45+ mm by construction.
        Particle boxp = box_particle(0.108f * 0.5f, 0.108f * 0.866f, 0.5f,
                                     0.20f, 0.20f, 0.20f, 0, 0, 0);
        ContactManifold m;
        const bool hit = narrow_phase_obb(
            obb_of_box_particle(blade, blade.z),
            obb_of_box_particle(boxp, boxp.z),
            0, 1, 0.08f, m);
        printf("      [measure] n=(%.3f,%.3f,%.3f) pts=%d face=%d\n",
               m.normal_x, m.normal_y, m.normal_z, m.num_points,
               (int)m.is_face_contact);
        check(hit, "INV-12: contact detected");
        const bool oblique = std::fabs(m.normal_x) > 0.1f &&
                             std::fabs(m.normal_y) > 0.1f;
        check(oblique, "INV-12/INV-6: normal has BOTH lateral components — it is not locked to a world axis");
        // The blade's face normal is +-(sin(yaw), cos(yaw), 0) with CW yaw;
        // whichever sign, |nx/ny| must equal tan(30 deg) = 0.577.
        if (std::fabs(m.normal_y) > 1e-4f) {
            const float ratio = std::fabs(m.normal_x / m.normal_y);
            printf("      [measure] |nx/ny| = %.4f (tan 30 = 0.5774)\n", ratio);
            check(std::fabs(ratio - 0.5774f) < 0.01f,
                  "INV-12: normal direction equals the blade's yaw");
        }
    }

    // ------------------------------------------------------------------
    // Part 1e — separation: a rotated plate whose WORLD span would touch
    // but whose oriented shape clears the slab must report NO contact.
    // The fat-box path faked a contact here.
    // ------------------------------------------------------------------
    printf("    [1e] rotated plate clear of the slab: no phantom contact\n");
    {
        // Quarter-turned plate, lowest point 150 mm above the slab top
        // (well past the 80 mm speculative margin). Its unrotated slab
        // form (T=0.6) would OVERLAP: 0.275 - 0.3 = -0.025 < 0.1.
        Particle a = box_particle(0, 0, 0.275f, 0.40f, 0.05f, 0.60f,
                                  (float)(M_PI / 2.0), 0, 0);
        Particle b = box_particle(0, 0, 0.05f, 1.0f, 1.0f, 0.1f, 0, 0, 0);
        ContactManifold m;
        const bool hit = narrow_phase_obb(
            obb_of_box_particle(a, a.z), obb_of_box_particle(b, b.z),
            0, 1, 0.08f, m);
        printf("      [measure] hit=%d (unrotated slab form WOULD overlap)\n",
               (int)hit);
        check(!hit, "INV-12: no phantom contact where the rotated shape has 150 mm of air (the fat-box path faked one)");
    }

    // ------------------------------------------------------------------
    // Part 2 — the engine settles a quarter-turned plate ON a support
    // box, resting where its rotated shape says. Support top at 1.0 m
    // keeps the turtle plane's world-Z clamp (unchanged, tracked
    // separately) out of reach: plate bottom by RAW extents stays > 0.
    // ------------------------------------------------------------------
    printf("    [2] engine: quarter-turned plate settles on its thin face\n");
    {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.enable_chat_window = false;
        cfg.show_debug_overlay = false;
        Engine engine;
        if (engine.initialize(cfg) != 0) {
            printf("  engine init failed\n  FAIL\n");
            return false;
        }
        auto& ps = engine.get_particle_system();

        // Support: a stout box, top face at z = 1.0.
        Particle s = box_particle(0, 0, 0.5f, 1.2f, 1.2f, 1.0f, 0, 0, 0);
        s.SetMaterial(Materials::Type::STONE);
        const int support = engine.add_particle(s);
        ps.flush_pending_particles();
        {
            auto v = ps.lock_particles_for_write();
            v[support].solver_mode = ParticleSolverMode::KINEMATIC;
            v[support].owner = ParticleOwner::DYNAMICS;
            v[support].is_at_rest = true;
        }

        // The settling-flat plate, quarter-turned, dropped from 1.5 m.
        constexpr float W = 0.40f, H = 0.05f, T = 0.60f;
        Particle p = box_particle(0, 0, 1.5f, W, H, T,
                                  (float)(M_PI / 2.0), 0, 0);
        p.SetMaterial(Materials::Type::WOOD_SOFT);
        const int plate = engine.add_particle(p);
        ps.flush_pending_particles();

        for (int f = 0; f < 600; ++f) engine.update(1.0 / 60.0);

        float z = 0, rx = 0, speed = 0;
        {
            auto v = ps.lock_particles_for_write();
            z = v[plate].z;
            rx = v[plate].rotation_x;
            speed = std::sqrt(v[plate].vx * v[plate].vx +
                              v[plate].vy * v[plate].vy +
                              v[plate].vz * v[plate].vz);
        }
        const float support_top = 1.0f;
        // Down-reach of the rotated plate about X: contributions from local
        // Y (H) and local Z (T) spans. At rx = 90 deg this is H/2.
        const float reach = 0.5f * (std::fabs(std::sin(rx)) * H +
                                    std::fabs(std::cos(rx)) * T);
        const float air = (z - reach) - support_top;
        printf("      [measure] plate z %.4f  rotation_x %.1f deg  "
               "air %.0f mm  speed %.4f m/s\n",
               z, rx * 57.2958f, air * 1000.0f, speed);
        check(std::fabs(air) < 0.010f, "INV-12: rests ON the support where its rotated shape says (|air| < 10 mm)");
        check(speed < 0.02f, "PROPOSED REST-IS-REACHED: and is quiet");
        engine.shutdown();
    }

    printf("\n    checks: %d run, %d failed\n", checks_run, checks_failed);
    const bool pass = checks_failed == 0;
    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    return pass;
}
