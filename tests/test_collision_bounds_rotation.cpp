// ============================================================================
// COLLISION BOUNDS vs ROTATION: the table that keeps the comments honest
// ============================================================================
// Two comments in this tree asserted, in two different wordings, that physics
// "ignores rotation" when computing collision extents. Both were unfalsifiable
// prose: nothing in the suite could tell a reader whether they were still
// true. They were not. The OBB work (3fe107e, 0eb4b67, both 2026-08-12) made
// rotated BOX bounds oriented at every site, and no comment moved.
//
// On dates, precisely: the claim became false on 2026-08-12. How long the
// older comment stood before that cannot be read from this repo, whose history
// starts at the squashed publish commit 6e9847d (2026-07-30); it dates itself
// to a "2024-12 debugging session" and that is its own word, not git's.
//
// This test is the falsifier. It pins, line by line, WHICH shapes get
// oriented treatment and which stay axis-aligned, so the canonical table in
// include/logosphere/physics/narrow_phase.h cannot go stale in silence: change
// the dispatch and this test goes red before the comment can lie.
//
// It is deliberately bounds-level and engine-free. The rotated box-box contact
// itself (normals, manifold points, penetrations) is test_rotated_box_contact.
//
// INV-12 (true-geometry-contacts) is the law under all of it. Part 5 used to
// pin the one place the code broke it; that pair was fixed on 2026-08-19 and
// part 5 now pins the correct answer, including the deep case.
// ============================================================================

#include "particle.h"
#include "logosphere/physics/narrow_phase.h"
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>

namespace {

int checks_run = 0, checks_failed = 0;

void check(bool ok, const char* what) {
    checks_run++;
    if (!ok) checks_failed++;
    printf("      %-62s %s\n", what, ok ? "ok" : "*** FAIL ***");
}

Particle box(float x, float y, float z,
             float w, float h, float t,
             float rx, float ry, float rz) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y; p.z = z;
    p.width = w; p.height = h; p.thickness = t; p.size = w;
    p.rotation_x = rx; p.rotation_y = ry; p.rotation_z = rz;
    return p;
}

float z_extent(const Particle& p) {
    const AABB6 w = aabb_of_obb(obb_of_box_particle(p, p.z));
    return w.max_z - w.min_z;
}

} // namespace

int main() {
    printf("\n=== COLLISION BOUNDS vs ROTATION ===\n");

    // ------------------------------------------------------------------
    // 1. A ROTATED BOX GETS ORIENTED BOUNDS.
    //
    // The fallen-log segment is the case that motivated this: a 2.2 m long,
    // 0.7 m thick trunk piece laid flat by rotation_y = pi/2. Its `thickness`
    // field carries the LENGTH, so a rotation-blind bound would claim a 2.2 m
    // world-Z span for a log that is 0.7 m tall.
    // ------------------------------------------------------------------
    printf("    [1] rotated box: bounds follow the orientation\n");
    {
        const float diameter = 0.7f, length = 2.2f;
        Particle log_seg = box(0, 0, 1.0f, diameter, diameter, length,
                               0.0f, (float)M_PI / 2.0f, 0.6f /* ~34 deg heading */);

        check(box_particle_is_rotated(log_seg), "laid-flat log reads as rotated");

        const float ze = z_extent(log_seg);
        printf("        z extent: %.4f m   (thickness field = %.4f m)\n", ze, length);
        check(std::fabs(ze - diameter) < 1e-3f,
              "world-Z extent is the DIAMETER, not the thickness field");
        check(ze < length - 1e-3f,
              "and it is strictly smaller than the rotation-blind answer");

        // The length has to reappear on the horizontal axes: rotating a box
        // moves extent between axes, it does not delete it.
        const AABB6 w = aabb_of_obb(obb_of_box_particle(log_seg, log_seg.z));
        const float span_xy = std::max(w.max_x - w.min_x, w.max_y - w.min_y);
        check(span_xy > length * 0.5f,
              "the length reappears horizontally (extent moved, not lost)");
    }

    // A quarter-turned plate: the classic. thickness 0.1, height 1.0, tipped
    // about X so the tall axis becomes the vertical one.
    printf("    [1b] quarter-turned plate: height becomes the down-reach\n");
    {
        Particle plate = box(0, 0, 2.0f, 1.0f, 1.0f, 0.1f,
                             (float)M_PI / 2.0f, 0, 0);
        const float ze = z_extent(plate);
        printf("        z extent: %.4f m   (thickness field = %.4f m)\n", ze, 0.1f);
        check(std::fabs(ze - 1.0f) < 1e-3f, "world-Z extent is the HEIGHT (1.0 m)");
    }

    // The consequence for FallenTreeGenerator, measured rather than argued.
    // It places a segment centre at world_z + seg_len_z/2, an offset derived
    // when bounds were rotation-blind and the segment's world-Z half-extent
    // really was half its length. Oriented bounds make the half-extent half
    // the DIAMETER, so the same offset now lifts every preset off the ground
    // by exactly the amount the old comment records as its burial depth,
    // sign flipped. Reported, not fixed: correcting the offset is a
    // behaviour change and belongs to whoever owns that generator.
    printf("    [1c] fallen-log placement offset, per preset\n");
    {
        struct Preset { const char* name; float length; int segs; float diameter; };
        const Preset presets[] = {
            {"fallen_trunk",  6.0f, 5, 0.80f},
            {"fallen_log",    2.5f, 3, 0.50f},
            {"fallen_branch", 1.5f, 2, 0.25f},
            {"twig",          0.6f, 1, 0.18f},
        };
        bool all_float = true;
        for (const Preset& ps : presets) {
            const float seg_len_z = (ps.length / ps.segs) * 1.1f;
            const float seg_z     = seg_len_z * 0.5f;      // world_z = 0
            Particle seg = box(0, 0, seg_z, ps.diameter, ps.diameter, seg_len_z,
                               0.0f, (float)M_PI / 2.0f, 0.0f);
            const AABB6 w = aabb_of_obb(obb_of_box_particle(seg, seg.z));
            printf("        %-14s oriented bottom %+.4f m (blind bottom %+.4f m)\n",
                   ps.name, w.min_z, seg_z - seg_len_z * 0.5f);
            if (w.min_z <= 1e-4f) all_float = false;
        }
        check(all_float,
              "every preset's oriented bottom sits ABOVE the ground it targets");
    }

    // ------------------------------------------------------------------
    // 2. AN UNROTATED BOX KEEPS THE RAW ARITHMETIC, TO THE BIT.
    //
    // This is a promise, not an approximation: the axis-aligned fast path
    // exists so that unrotated worlds are bit-identical to the pre-OBB engine.
    // Exact float equality is the assertion on purpose.
    // ------------------------------------------------------------------
    printf("    [2] unrotated box: bounds are the raw extents, bit-identical\n");
    {
        Particle p = box(1.25f, -3.5f, 0.545f, 0.4f, 0.6f, 0.9f, 0, 0, 0);
        check(!box_particle_is_rotated(p), "reads as unrotated");

        const AABB6 w = aabb_of_obb(obb_of_box_particle(p, p.z));
        check(w.min_z == p.z - p.thickness * 0.5f &&
              w.max_z == p.z + p.thickness * 0.5f, "z bounds exactly z +/- t/2");
        check(w.min_x == p.x - p.width * 0.5f &&
              w.max_x == p.x + p.width * 0.5f,     "x bounds exactly x +/- w/2");
        check(w.min_y == p.y - p.height * 0.5f &&
              w.max_y == p.y + p.height * 0.5f,    "y bounds exactly y +/- h/2");
    }

    // ------------------------------------------------------------------
    // 3. WHERE THE LINE IS. Rotation below the epsilon is not rotation.
    // ------------------------------------------------------------------
    printf("    [3] the epsilon line (BOX_ROTATION_EPS = %g rad)\n",
           (double)PhysicsV4::BOX_ROTATION_EPS);
    {
        Particle under = box(0, 0, 0, 1, 1, 0.2f, 0, 0,
                             PhysicsV4::BOX_ROTATION_EPS * 0.5f);
        Particle over  = box(0, 0, 0, 1, 1, 0.2f, 0, 0,
                             PhysicsV4::BOX_ROTATION_EPS * 2.0f);
        check(!box_particle_is_rotated(under), "half an epsilon is unrotated");
        check(box_particle_is_rotated(over),   "two epsilons is rotated");
    }

    // ------------------------------------------------------------------
    // 4. BOX vs BOX WITH ONE SIDE ROTATED USES THE ORIENTED HANDLER, AND
    //     ITS NORMAL COMES FROM THE GEOMETRY, NOT FROM A WORLD AXIS.
    //
    // A 30-degree ramp with a small box resting on its slope. The contact
    // normal must be the slope normal. A rotation-blind bound would answer
    // with a world axis and the box would sit on a phantom horizontal shelf.
    // ------------------------------------------------------------------
    printf("    [4] box on a 30-degree ramp: normal is the slope's\n");
    {
        const float tilt = 30.0f * (float)M_PI / 180.0f;
        Particle ramp = box(0, 0, 0, 4.0f, 4.0f, 0.4f, tilt, 0, 0);
        check(box_particle_is_rotated(ramp), "ramp reads as rotated");

        // Rest a small cube on the ramp's upper face, at the ramp centre.
        // Face centre is half the ramp thickness out along the tilted +Z,
        // plus half the cube, minus 5 mm so they overlap.
        const float nz = std::cos(tilt);
        const float ny = -std::sin(tilt);      // engine tilt sign, measured below
        const float out = 0.4f * 0.5f + 0.3f * 0.5f - 0.005f;
        Particle cube = box(0, ny * out, nz * out, 0.3f, 0.3f, 0.3f, tilt, 0, 0);

        ContactManifold m;
        const bool hit = narrow_phase_obb(obb_of_box_particle(cube, cube.z),
                                          obb_of_box_particle(ramp, ramp.z),
                                          0, 1, 0.02f, m);
        check(hit, "contact detected");
        if (hit) {
            printf("        normal: (%.4f, %.4f, %.4f)\n",
                   m.normal_x, m.normal_y, m.normal_z);
            // The slope normal is 30 degrees off vertical: |nz| = cos30 = 0.866
            // and |ny| = sin30 = 0.5. A world-axis answer would be (0,0,1).
            check(std::fabs(std::fabs(m.normal_z) - std::cos(tilt)) < 0.02f,
                  "normal z-component is cos(30), not 1");
            check(std::fabs(std::fabs(m.normal_y) - std::sin(tilt)) < 0.02f,
                  "normal has the sin(30) lateral component (not axis-locked)");
        }
    }

    // ------------------------------------------------------------------
    // 5. SPHERE vs a ROTATED BOX: ORIENTED, as of 2026-08-19.
    //
    // This section used to pin the WRONG answer on purpose, as a tripwire for
    // the canonical table: narrow_phase_particle_pair built the box side with
    // aabb_of_box_particle, which never reads rotation, so a sphere met a
    // tilted ramp as an upright slab and got a world-axis normal back.
    //
    // THE TRIPWIRE WORKED. The pair was routed through narrow_phase_sphere_obb
    // and these two checks went red the same minute, which is what brought
    // whoever made the change here to correct the table. That is the entire
    // reason the section existed, and it is why the assertions below are now
    // the RIGHT answer rather than a description of a defect.
    //
    // What the gap actually was, measured before the fix
    // (tests/test_ramp_race.cpp): on a 40 degree ramp a sphere fell 1.907 m
    // THROUGH the tilted face and came to rest on the horizontal top of the
    // unrotated box, permanently, which is INV-2 as well as INV-12. It was not
    // a wrong normal on the right surface; it was the wrong solid.
    // ------------------------------------------------------------------
    printf("    [5] sphere vs rotated box: oriented (INV-12 closed, F2 fixed)\n");
    {
        const float tilt = 30.0f * (float)M_PI / 180.0f;
        Particle ramp = box(0, 0, 0, 4.0f, 4.0f, 0.4f, tilt, 0, 0);

        // The oriented box still reaches well past the rotation-blind slab.
        // That difference is exactly what used to be ignored.
        const AABB6 oriented = aabb_of_obb(obb_of_box_particle(ramp, ramp.z));
        const float slab_top = ramp.z + ramp.thickness * 0.5f;
        printf("        oriented top %.4f   rotation-blind slab top %.4f\n",
               oriented.max_z, slab_top);
        check(oriented.max_z > slab_top + 0.1f,
              "the oriented box reaches well past the raw slab");

        // A sphere resting just above the ramp's REAL face at the centre
        // column, where the face passes through the box centre.
        Particle ball = {};
        ball.shape = ParticleShape::SPHERE;
        ball.size = 0.3f;
        ball.x = 0.0f; ball.y = 0.0f;
        ball.z = slab_top + ball.size * 0.5f - 0.01f;

        ContactManifold m;
        const bool hit = narrow_phase_particle_pair(ball, ramp, 0, 1, 0.0f, m);
        check(hit, "sphere contacts the ramp");
        if (hit) {
            printf("        normal: (%.4f, %.4f, %.4f)\n",
                   m.normal_x, m.normal_y, m.normal_z);
            // The SAME normal part 4 measures for a box on the same ramp.
            // A sphere and a cube on one slope must meet one surface.
            check(std::fabs(m.normal_z - std::cos(tilt)) < 1e-3f,
                  "normal z-component is cos(30), the slope's own");
            check(std::fabs(std::fabs(m.normal_y) - std::sin(tilt)) < 1e-3f,
                  "normal carries the sin(30) lateral component, so gravity "
                  "has something to drive the sphere downhill with");
        }

        // And the deep case, which the axis-aligned path could not express:
        // a sphere whose centre is INSIDE the tilted box must be pushed out
        // along one of the box's OWN axes, never along a world axis.
        Particle deep = ball;
        deep.z = ramp.z;                     // dead centre, fully inside
        ContactManifold dm;
        if (narrow_phase_particle_pair(deep, ramp, 0, 1, 0.0f, dm)) {
            printf("        deep normal: (%.4f, %.4f, %.4f)\n",
                   dm.normal_x, dm.normal_y, dm.normal_z);
            check(std::fabs(std::fabs(dm.normal_z) - std::cos(tilt)) < 1e-3f,
                  "a sphere inside the box exits along the box's own axis, "
                  "not along world Z");
        }
    }

    printf("\n  %d checks, %d failed\n", checks_run, checks_failed);
    printf("=== %s ===\n\n", checks_failed == 0 ? "PASS" : "FAIL");
    return checks_failed == 0 ? 0 : 1;
}
