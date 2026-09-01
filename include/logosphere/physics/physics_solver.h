#ifndef PHYSICS_SOLVER_H
#define PHYSICS_SOLVER_H

/**
 * ============================================================================
 * PHYSICS SOLVER V4 - Sequential Impulse Constraint Solver
 * ============================================================================
 *
 * ONE SENTENCE: Iteratively apply impulses to stop particles from penetrating.
 *
 * THE ALGORITHM (4 steps):
 *   1. PREDICT - Where would particles go if nothing collided?
 *   2. DETECT  - Which predictions cause overlap?
 *   3. CORRECT - Apply impulses to fix velocities (iterate N times)
 *   4. COMMIT  - Move particles using corrected velocities
 *
 * WHY IT WORKS:
 *   Each iteration improves the solution. After N iterations, all constraints
 *   are approximately satisfied. No special ordering needed - just iterate.
 *
 * EXAMPLE (Ball hits floor):
 *   Ball: mass=1kg, vz=-2m/s, z=0.01m
 *   Floor: kinematic, z=0
 *
 *   Iteration 1:
 *     v_rel = -2 - 0 = -2 m/s (approaching)
 *     impulse = 1kg × 2m/s = 2 kg⋅m/s
 *     Ball.vz = -2 + 2 = 0 m/s (stopped)
 *
 *   Result: Ball stops at floor.
 *
 * REFERENCE: docs/PHYSICS_ITERATIVE_RESOLVER_V4.md
 * ============================================================================
 */

#include <cstddef>
#include <cstdint>   // uint8_t in Constraint; GCC does not include it transitively
#include <limits>

// The INV-29 constants registry: every solver tuning constant is an engine
// INPUT declared in schema/physics.yaml (constants_registry subset) and
// generated into this header, in this same PhysicsV4 namespace, each with
// its unit and the RCA that earned it. Edit the schema, regenerate, commit
// both. Constant-work and code-work are separate efforts by decree: never
// retune a value in the commit that changes the code consuming it.
#include "generated/physics_constants.h"

namespace PhysicsV4 {

// Derived, not an input: kept beside its base so the derivation cannot
// drift from it. The registry holds declared values only.
constexpr float  DAMPING_VELOCITY_THRESHOLD_SQ = DAMPING_VELOCITY_THRESHOLD * DAMPING_VELOCITY_THRESHOLD;

// ============================================================================
// CONSTRAINT
// ============================================================================
/**
 * A constraint says: "These two particles should not approach along the Jacobian."
 *
 * VEC3 JACOBIAN FORMULATION:
 *   - Jacobian (jx, jy, jz): Unit vector direction of the constraint
 *   - body_a gets impulse in +jacobian direction
 *   - body_b gets impulse in -jacobian direction (reaction)
 *
 * RELATIVE VELOCITY:
 *   v_rel = dot(jacobian, v_a - v_b)
 *         = jx*(va.x - vb.x) + jy*(va.y - vb.y) + jz*(va.z - vb.z)
 *
 * IMPULSE APPLICATION:
 *   v_a += jacobian * (impulse / mass_a)
 *   v_b -= jacobian * (impulse / mass_b)
 *
 * CONTACT: Jacobian = separation normal (axis of minimum penetration)
 *          min_impulse = 0 (can only push apart)
 *
 * GLUON:   Three constraints per gluon (X, Y, Z axes)
 *          Each constrains one velocity component to match
 *          Can push or pull, may break at max force.
 *
 * EXAMPLE (Ball at z=0.5 collides with floor at z=0):
 *   jacobian = (0, 0, 1) - push apart in Z
 *   v_rel = 0*(vx_diff) + 0*(vy_diff) + 1*(vz_diff) = vz_ball - vz_floor
 *   If v_rel < 0: approaching, apply impulse
 *
 * WHY VEC3 (not single axis)?
 *   - Gluons need ALL axes constrained (particle velocities must match)
 *   - Future: Rotated contacts have non-axis-aligned normals
 *   - Uniform formulation: same math for all constraint types
 */
struct Constraint {
    size_t body_a;              // First particle index
    size_t body_b;              // Second particle index

    float jx, jy, jz;           // Jacobian: direction of constraint (unit vector)

    float effective_mass;       // 1/(1/ma + 1/mb + compliance/dt²) - how impulse distributes
    // Share of the pair's effective mass this row carries: 1/num_points for
    // a multi-point manifold, 1 otherwise. N same-normal rows describe a
    // rank-1 error; each solving it with FULL effective mass over-corrects
    // by xN per sweep, which is a pump against extreme mass ratios (a walker
    // foot flicking 0.03 kg leaves metres). Splitting is the standard
    // manifold treatment: the rows sum to one correction.
    float eff_mass_share = 1.0f;
    float compliance;           // Inverse stiffness. 0 = rigid (contacts). >0 = soft (animation gluons).
    float bias;                 // Velocity bias for position correction

    float accumulated_impulse;  // Total impulse applied (for clamping)

    // SPLIT IMPULSE: the position pass's own accumulator. Kept separate from
    // accumulated_impulse so position repair never enters the body's momentum
    // and never contaminates the warm-start cache.
    float accumulated_pseudo_impulse = 0.0f;
    float min_impulse;          // Clamp min (0 for contacts)
    float max_impulse;          // Clamp max (breaking force for gluons)
    // G-63: the row's build-time approach speed. G-52's warm-store
    // law (cache what SUSTAINS, forget what CAPTURES) recovered the
    // approach from the capture cap; under the single law the cap is
    // infinite, so the row carries the approach itself.
    float build_approach = 0.0f;

    // How far the bodies overlap when this row was built, in metres. Positive
    // is interpenetration, negative is a gap.
    //
    // MEASUREMENT ONLY, and it is stored because it cannot be recovered from
    // `bias`. Baumgarte turns a position error into a velocity TARGET, and the
    // solve drives v_rel to that target, so a row can be perfectly satisfied at
    // the velocity level while the bodies are still deeply inside each other.
    // Measured 2026-08-02: two boxes spawned 40% overlapped report a velocity
    // residual of exactly zero. Inverting bias to recover penetration does not
    // work either, because it is clamped to MAX_BIAS_VELOCITY on deep overlaps
    // and zeroed inside the slop band, and both destroy the value.
    //
    // Defaulted, because rows are built field by field and gluons never set it.
    float penetration = 0.0f;

    // =========================================================================
    // TURTLE CONTACT (V4.3) - World boundary collision
    // =========================================================================
    // When is_turtle_contact=true, body_b is ignored (Turtle has no particle).
    // The Turtle is an infinite-mass collision plane at z=TURTLE_Z.
    // Impulse is only applied to body_a (the particle hitting the Turtle).
    bool  is_turtle_contact;    // True when colliding with world boundary (Turtle)

    // =========================================================================
    // FRICTION DATA (V4.1) - Only used for contact constraints
    // =========================================================================
    bool  is_contact;           // True for contacts (has friction), false for gluons
    float friction_impulse_t1;  // Accumulated friction impulse along tangent1
    float friction_impulse_t2;  // Accumulated friction impulse along tangent2
    // Tangent directions computed from normal: if normal=Z, t1=X, t2=Y

    // =========================================================================
    // ANGULAR CONSTRAINT DATA (V4.2) - Integrated angular/linear solving
    // =========================================================================
    // When is_angular=true, this constraint couples rotation_z between bodies.
    // Angular constraints solve: ω_rel = ω_b - ω_a → target (usually 0)
    // They also include Baumgarte bias to correct rotation drift.
    //
    // DEAD ZONE: No restoring impulse within ±angular_limit. Beyond limit,
    // bias pushes rotation back to the boundary.
    //
    // WHY INTEGRATED: Angular and linear constraints must solve together.
    // Otherwise, rotation changes after position projection causes "door hinge"
    // effect where particles rotate around their edge instead of center.
    // =========================================================================
    bool  is_angular;           // True for angular constraints (rotation coupling)
    // World-axis this angular row acts on. 0=X, 1=Y, 2=Z. Default 2 so
    // pre-quaternion code that builds a single scalar angular row
    // continues to drive omega_z exactly as before. When
    // angular_axis_vec_len > 0 the solver instead uses
    // angular_axis_x/y/z as a world-space unit axis and applies the
    // impulse along the full vector — used by the quaternion-drive
    // path so a combined pose converges as one Rodrigues rotation
    // rather than three independent per-axis rows.
    uint8_t angular_axis_idx = 2;
    // ANCHOR TORQUE (rung 3): world-space lever arms from each body's centre
    // to the bond anchor. When set, the linear impulse also torques any
    // quaternion-driven body (omega += (r x J*impulse) / I) — the transducer
    // from linear force to rotation. Without it a pushed chain can only
    // shear: nothing in the solver converted force into spin.
    bool  apply_anchor_torque = false;
    // THE PIVOT LAW. A body constrained at an anchor rotates ABOUT THAT
    // ANCHOR, not about its centre. Two consequences, both encoded:
    //   (1) the row's inertia is the anchor-axis inertia (parallel axis
    //       theorem: I_com + m * r_perp^2), stored here per body;
    //   (2) an angular impulse must carry dv = -(dw x r) so the anchor
    //       point does not move (applied in the angular solve).
    // Without (2), the drive rotated about the centre, which displaced the
    // anchor, which the anchor rows immediately undid: measured +3.3 rad/s
    // of drive against -3.3 rad/s of anchor correction, net 0.001, forever
    // (the blade that would not stand up). Pure mechanics: no ownership, no
    // material type, no gravity direction.
    float pivot_inertia_a = 0.0f;   // 0 = no anchor, use centre inertia
    float pivot_inertia_b = 0.0f;
    float anchor_rax = 0.0f, anchor_ray = 0.0f, anchor_raz = 0.0f;
    float anchor_rbx = 0.0f, anchor_rby = 0.0f, anchor_rbz = 0.0f;

    // FACE FRICTION (G-55, lever FRICTION_TWIST=1, default off). Rows
    // of a FACE manifold carry linear-only tangent friction (corner
    // anchors overbrake a spinning face 1.85x against the face
    // integral), the tangent pair is clamped as a vector (circular
    // cone), and ONE row per manifold — the twist carrier — solves
    // torsional Coulomb friction about the contact normal, limited by
    // mu * N_total * twist_r (twist_r = TWIST_RADIUS_FACTOR * patch
    // side, the mean radius of a uniformly pressed square).
    bool  face_friction = false;
    bool  twist_carrier = false;
    float twist_r = 0.0f;
    float twist_impulse = 0.0f;

    float angular_axis_x = 0.0f;
    float angular_axis_y = 0.0f;
    float angular_axis_z = 0.0f;
    float angular_axis_vec_len = 0.0f;  // 0 => use axis_idx path
    float effective_inertia;    // 1/(1/Ia + 1/Ib) - angular impulse distribution
    float angular_bias;         // Rotation correction bias (Baumgarte)
    float accumulated_angular_impulse; // Total angular impulse applied
    float min_angular_impulse;  // Clamp min (usually -infinity for gluons)
    float max_angular_impulse;  // Clamp max (usually +infinity)
    float angular_limit;        // Dead zone: no impulse within ±limit (radians)

    // Initialize with defaults
    Constraint()
        : body_a(0)
        , body_b(0)
        , jx(0.0f)
        , jy(0.0f)
        // Was 1.0f ("Default to Z axis") — a row nobody finished building
        // silently pushed along +Z: an axis bias on top of a phantom
        // (fallback inventory E1). Every real builder sets its jacobian
        // explicitly; an unset row is now INERT, not secretly vertical.
        , jz(0.0f)
        , effective_mass(0.0f)
        , compliance(0.0f)
        , bias(0.0f)
        , accumulated_impulse(0.0f)
        , min_impulse(0.0f)
        , max_impulse(std::numeric_limits<float>::infinity())
        , is_turtle_contact(false)
        , is_contact(false)
        , friction_impulse_t1(0.0f)
        , friction_impulse_t2(0.0f)
        , is_angular(false)
        , effective_inertia(0.0f)
        , angular_bias(0.0f)
        , accumulated_angular_impulse(0.0f)
        , min_angular_impulse(-std::numeric_limits<float>::infinity())
        , max_angular_impulse(std::numeric_limits<float>::infinity())
        , angular_limit(ANGULAR_LIMIT_UNLIMITED)  // Default: no limit (π = 180°)
    {}
};

// ============================================================================
// CONSTRAINT TYPES
// ============================================================================

enum class ConstraintType {
    CONTACT,  // Non-penetration (can only push apart)
    GLUON     // Fixed distance (can push or pull, may break)
};

// ENABLE_WARM_STARTING, ENABLE_SPLIT_IMPULSE, POSITION_ITERATIONS: moved to
// the INV-29 registry (schema/physics.yaml, SolverLevers / SolverSchedule)
// with their histories. Mechanism details: docs/PHYSICS_ITERATIVE_RESOLVER_
// V4.3.md (warm starting), V4.4 notes above integrate_positions (split
// impulse).

/**
 * ContactKey - Unique identifier for a contact constraint
 *
 * Used to match contacts across frames for warm starting.
 * Two contacts are the same if they involve the same particle pair
 * on the same axis (or same type like turtle contact).
 */
struct ContactKey {
    size_t particle_a;      // First particle
    size_t particle_b;      // Second particle (or same as a for turtle)
    int contact_type;       // 0=turtle, 1=box-X, 2=box-Y, 3=box-Z

    bool operator==(const ContactKey& other) const {
        return particle_a == other.particle_a &&
               particle_b == other.particle_b &&
               contact_type == other.contact_type;
    }
};

/**
 * ContactKeyHash - Hash function for ContactKey
 *
 * Combines particle IDs and contact type into a single hash.
 * Used by std::unordered_map for cached_impulses_.
 */
struct ContactKeyHash {
    size_t operator()(const ContactKey& k) const {
        // Golden ratio mixing - prevents collisions from XOR patterns
        size_t h = k.particle_a;
        h ^= k.particle_b * 0x9e3779b9;
        h ^= static_cast<size_t>(k.contact_type) * 0x1b873593;
        return h;
    }
};

} // namespace PhysicsV4

#endif // PHYSICS_SOLVER_H
