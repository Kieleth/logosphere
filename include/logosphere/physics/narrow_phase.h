#ifndef NARROW_PHASE_H
#define NARROW_PHASE_H

// ============================================================================
// NARROW PHASE: SAT + Sutherland-Hodgman face clipping (box-box, axis-aligned
//               and oriented); analytic handlers for sphere-sphere, sphere-box
// ============================================================================
// Computes contact manifolds for overlapping particle pairs.
//
// Output normal convention: unit vector pointing from body B toward body A
// (so `pa += normal * impulse` pushes A off B). Matches narrow_phase_aabb
// and turtle-contact conventions elsewhere in physics_system_v4. INV-25.
//
// ----------------------------------------------------------------------------
// WHERE ROTATION IS APPLIED, AND WHERE IT IS NOT
// ----------------------------------------------------------------------------
// This block is the canonical answer. Point at it; do not restate it locally.
// Two worldgen comments restated it and both rotted: they went on asserting
// that physics "ignores rotation" after the OBB work landed on 2026-08-12
// (3fe107e narrow phase, 0eb4b67 turtle down-reach) and made it false. Prose
// nobody can falsify is how that happens, so every line below is an asserted
// check in tests/test_collision_bounds_rotation.cpp, the last one included.
// Change the dispatch and that test goes red before this table can lie.
//
//   BOX rotated past the epsilon: ORIENTED, at every site.
//     bounds        aabb_of_obb(obb_of_box_particle(...)). Broad-phase pair
//                   test (physics_system_v4.cpp:1027 body i, :1073 body j),
//                   BVH leaf (bvh.cpp:41), turtle contact down-reach (:896),
//                   turtle boundary clamp (:4811).
//     narrow phase  narrow_phase_obb: 15-axis SAT plus reference-face
//                   clipping. Dispatched at physics_system_v4.cpp:1341 when
//                   EITHER side of a box-box pair is rotated; the unrotated
//                   side degenerates to identity axes, so mixed pairs are
//                   exact too.
//     measured      a 2.2 m log laid flat by rotation_y = pi/2 bounds to a
//                   0.70 m world-Z span, its diameter, not its `thickness`.
//
//   BOX under the epsilon: AXIS-ALIGNED, and bit-identical to the pre-OBB
//     arithmetic on purpose, so unrotated worlds did not move when the
//     oriented path landed. BOX_ROTATION_EPS = 1e-4 rad; the quaternion twin
//     QUAT_UNROTATED_EPS = 1e-6 on 1 - |w|, about 3 mrad. One predicate,
//     box_particle_is_rotated, and nobody re-derives it.
//
//   SPHERE: rotation-invariant by shape. Radius is size/2, and the sphere
//     handlers never read width/height/thickness.
//
//   ELLIPSOID: AXIS-ALIGNED. Conservative per-axis AABB, orientation dropped.
//     Not an oriented ellipsoid and not claimed to be.
//
//   SPHERE vs BOX: AXIS-ALIGNED, and this one is a live defect, not a design
//     choice. narrow_phase_particle_pair builds the box side with
//     aabb_of_box_particle, which never reads rotation, so a sphere meets a
//     ROTATED box as its upright bounding slab and gets a world axis back for
//     a normal. Measured: on one 30-degree ramp a box is correctly told
//     (0, -0.5, 0.866) and a sphere is told (0, 0, 1), a flat shelf with
//     nothing to slide down. INV-12 broken, live. Board front F2, "a sphere
//     will not slide a ramp that a cube slides". Whoever fixes it corrects
//     this paragraph and the pinned check in the test, in the same commit.
// ============================================================================

#include "logosphere/physics/contact_manifold.h"

struct Particle;  // fwd

struct AABB6 {
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
};

// Compute contact manifold for two overlapping AABBs.
// Returns true if contact exists (manifold populated), false if separated.
// margin: speculative contact distance (contacts within this gap are included)
bool narrow_phase_aabb(const AABB6& a, const AABB6& b,
                       size_t id_a, size_t id_b,
                       float margin,
                       ContactManifold& out);

// Sphere-vs-sphere. Centers + radii. Normal points from B toward A.
// Contact point sits on A's surface in the -normal direction.
bool narrow_phase_sphere_sphere(
    float ax, float ay, float az, float ra,
    float bx, float by, float bz, float rb,
    size_t id_a, size_t id_b,
    float margin,
    ContactManifold& out);

// Sphere (A) vs axis-aligned box (B). Closest-point-on-box-to-sphere.
// Handles the "sphere center inside box" degenerate case by picking the
// shallowest face for the normal. Normal points from B toward A.
bool narrow_phase_sphere_aabb(
    float cx, float cy, float cz, float r,
    const AABB6& box,
    size_t id_a, size_t id_b,
    float margin,
    ContactManifold& out);

// Dispatcher: inspects a.shape + b.shape and picks the right handler.
// Used by physics_system_v4 for pairs where at least one side is not a
// BOX. BOX-BOX keeps going through narrow_phase_aabb directly because
// that path has extra surface-merging logic for adjacent static tiles.
bool narrow_phase_particle_pair(
    const Particle& a, const Particle& b,
    size_t id_a, size_t id_b,
    float margin,
    ContactManifold& out);

// ============================================================================
// ORIENTED BOXES
// ============================================================================
// An oriented box: center, three unit local axes expressed in world space,
// half-extent along each local axis. Local axis 0/1/2 carries the particle's
// width/height/thickness — the same triple the AABB path reads, no longer
// pinned to world X/Y/Z.
struct OBB {
    float c[3];        // world center
    float axis[3][3];  // axis[i] = local axis i as a world-space unit vector
    float half[3];     // half extents: width/2, height/2, thickness/2
};

// True when the particle carries an orientation the axis-aligned fast path
// cannot represent. Sub-milliradian rotations are treated as unrotated.
bool box_particle_is_rotated(const Particle& p);

// Build the oriented box of a BOX particle. `z_override` replaces p.z so
// callers can pass the solver's predicted Z. Orientation source matches the
// gluon anchor code: rotation_q when the particle is quat-driven, otherwise
// the Euler triple composed X then Y then Z (Z clockwise-from-+Z, the engine
// convention — Quat::from_euler owns that composition).
OBB obb_of_box_particle(const Particle& p, float z_override);

// Tight world-axis bounds of an oriented box. Per world axis k the extent is
// sum_i half[i] * |axis[i][k]| — exact for a box, conservative never loose.
AABB6 aabb_of_obb(const OBB& o);

// SAT (15 axes) + reference-face clipping for oriented boxes. Same manifold
// contract as narrow_phase_aabb: normal from B toward A, 1-4 points with
// per-point penetration, `margin` admits speculative near-contacts. Face
// contacts clip the incident face against the reference face's side planes;
// edge-edge contacts report the single closest point between the two edges.
bool narrow_phase_obb(const OBB& a, const OBB& b,
                      size_t id_a, size_t id_b,
                      float margin,
                      ContactManifold& out);

// Swept AABB vs static AABB.
// `a_start`, `a_end`: moving AABB at t=0 and t=1
// `b`: static target AABB
// On hit, fills out_t (0..1 = fraction of motion), and out_normal (axis unit vector
// pointing from b toward a at time of impact).
// Returns false if no collision along the motion path.
bool swept_aabb_vs_aabb(const AABB6& a_start, const AABB6& a_end, const AABB6& b,
                       float& out_t, float& out_nx, float& out_ny, float& out_nz);

#endif // NARROW_PHASE_H
