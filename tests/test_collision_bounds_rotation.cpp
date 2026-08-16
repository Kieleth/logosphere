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
// INV-12 (true-geometry-contacts) is the law under all of it, including the
// one place the code still breaks it. See part 5.
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
    // 5. PINNED GAP: SPHERE vs a ROTATED BOX IS STILL AXIS-ALIGNED.
    //
    // narrow_phase_particle_pair builds the box side with
    // aabb_of_box_particle (src/core/narrow_phase.cpp), which reads
    // width/height/thickness as world extents and never looks at rotation.
    // So a sphere meets a rotated ramp as the ramp's upright bounding slab and
    // the normal it gets back is a world axis. That is INV-12 broken, live,
    // and it is board front F2 ("a sphere will not slide a ramp that a cube
    // slides").
    //
    // The assertion below pins the WRONG behaviour on purpose. It is the
    // tripwire for the canonical comment: the day someone routes this pair
    // through an oriented handler, this check fails, and whoever makes it fail
    // has to come here and correct the table. That is the whole point: the
    // previous comments rotted precisely because nothing could fail when they
    // stopped being true.
    // ------------------------------------------------------------------
    printf("    [5] PINNED GAP: sphere vs rotated box (INV-12 open, board F2)\n");
    {
        const float tilt = 30.0f * (float)M_PI / 180.0f;
        Particle ramp = box(0, 0, 0, 4.0f, 4.0f, 0.4f, tilt, 0, 0);

        // The ramp's ORIENTED top at the centre column is well below its
        // world-axis bounding slab's top, so a sphere placed between the two
        // is touching nothing real. Rotation-blind bounds report a contact.
        const AABB6 oriented = aabb_of_obb(obb_of_box_particle(ramp, ramp.z));
        const float slab_top = ramp.z + ramp.thickness * 0.5f;
        printf("        oriented top %.4f   rotation-blind slab top %.4f\n",
               oriented.max_z, slab_top);
        check(oriented.max_z > slab_top + 0.1f,
              "the oriented box reaches well past the raw slab");

        Particle ball = {};
        ball.shape = ParticleShape::SPHERE;
        ball.size = 0.3f;
        ball.x = 0.0f; ball.y = 0.0f;
        ball.z = slab_top + ball.size * 0.5f - 0.01f;   // 10 mm into the SLAB

        ContactManifold m;
        const bool hit = narrow_phase_particle_pair(ball, ramp, 0, 1, 0.0f, m);
        check(hit, "sphere contacts the ramp's world-axis slab");
        if (hit) {
            printf("        normal: (%.4f, %.4f, %.4f)\n",
                   m.normal_x, m.normal_y, m.normal_z);
            check(std::fabs(m.normal_z) > 0.999f,
                  "normal is +Z exactly: a flat shelf, NOT the 30-degree slope");
            check(std::fabs(m.normal_y) < 1e-3f,
                  "no lateral component, so nothing drives the sphere downhill");
        }
    }

    printf("\n  %d checks, %d failed\n", checks_run, checks_failed);
    printf("=== %s ===\n\n", checks_failed == 0 ? "PASS" : "FAIL");
    return checks_failed == 0 ? 0 : 1;
}
