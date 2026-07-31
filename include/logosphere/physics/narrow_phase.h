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

// Swept AABB vs static AABB.
// `a_start`, `a_end`: moving AABB at t=0 and t=1
// `b`: static target AABB
// On hit, fills out_t (0..1 = fraction of motion), and out_normal (axis unit vector
// pointing from b toward a at time of impact).
// Returns false if no collision along the motion path.
bool swept_aabb_vs_aabb(const AABB6& a_start, const AABB6& a_end, const AABB6& b,
                       float& out_t, float& out_nx, float& out_ny, float& out_nz);

#endif // NARROW_PHASE_H
