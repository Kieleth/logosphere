#ifndef NARROW_PHASE_H
#define NARROW_PHASE_H

// ============================================================================
// NARROW PHASE: SAT + Sutherland-Hodgman Face Clipping (box-box);
//               analytic handlers for sphere-sphere / sphere-box
// ============================================================================
// Computes contact manifolds for overlapping particle pairs.
// Shape-dispatched: AABB-AABB, SPHERE-SPHERE, SPHERE-AABB. ELLIPSOID falls
// back to its enclosing AABB for now (conservative over-reporting).
//
// Output normal convention: unit vector pointing from body B toward body A
// (so `pa += normal * impulse` pushes A off B). Matches narrow_phase_aabb
// and turtle-contact conventions elsewhere in physics_system_v4.
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
