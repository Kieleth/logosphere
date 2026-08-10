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
#include <limits>

namespace PhysicsV4 {

// ============================================================================
// SOLVER PARAMETERS
// ============================================================================

constexpr int    SOLVER_ITERATIONS = 32;      // Increased for deep constraint chains (stacked floors)

// Outer substep count per render frame. The full physics pipeline
// (forces + contacts + gluon bias + integration) runs SOLVER_SUBSTEPS
// times with dt/SOLVER_SUBSTEPS each substep. Each substep rebuilds
// constraints against current state, so the position-bias correction
// applies more authority per frame at the same per-substep iteration
// count. The motivating problem: gluon position bias is dimensionally
// proportional (bias = BETA × separation), so closing a 33 mm/frame
// hip-motion lag at 60 Hz needs more update opportunities than 1
// frame can supply at BETA = 1.0 without crossing into chain
// instability (see April rehab probes — BETA up to 4.0 blew up).
// Substepping multiplies authority without changing BETA, at linear
// CPU cost.
constexpr int    SOLVER_SUBSTEPS   = 4;
constexpr float  SLOP              = 0.001f;  // 1mm allowed penetration
constexpr float  BETA              = 0.4f;    // Position correction strength (0.4 for stable stacks)
// Cap on the contact push-out speed Baumgarte may request (V4.14).
// bias = BETA*pen/dt is a position correction expressed at velocity
// level; uncapped, a deep false penetration (SAT axis flip between
// rotated boxes) becomes a ballistic ejection — a wedged boulder
// left the world at 72 m/s. Deep overlaps resolve over frames.
constexpr float  MAX_BIAS_VELOCITY = 4.0f;    // m/s
// MECHANISM A (issue #47): a contact may STOP an approach, never amplify it.
// Every contact row's impulse budget is effective_mass * (|approach speed| +
// this cushion), instead of infinity. The cushion is what sustains STATIC
// support: a resting row has zero approach speed but must still carry weight,
// m*g*dt per tick is 0.16 m/s-equivalent, so 4.0 funds a ~25-body stack of
// equals. Rows are rebuilt every frame with fresh speeds, so a sustained load
// re-earns its budget each tick. What this forbids is exactly the disease
// measured on the single-blade study: a 9 m/s footstep handing a 2-gram sheet
// an impulse worth 675 m/s. Capture-bounded, that same row can give the sheet
// at most (9 + 4) m/s: carried, not fired. It is also a global brake on the
// divergence class, since energy-from-nothing is precisely what it removes.
constexpr float  CONTACT_CAPTURE_CUSHION = 4.0f;   // m/s
// MECHANISM B1 (issue #47): the gluon twin of MAX_BIAS_VELOCITY. Gluon rows
// request bias = GLUON_POSITION_BETA * separation, which is dimensionally a
// velocity with no cap: the single-blade study measured a bond stretched to
// 434 m requesting a 434 m/s closing speed on a 2-gram body. A stretched bond
// may pull itself home at a bounded speed; separation beyond that resolves
// over frames, exactly the V4.14 argument for contacts.
constexpr float  GLUON_MAX_BIAS_VELOCITY = 4.0f;   // m/s
// MECHANISM B1, angular twin. Angular rows compute bias = 0.4 * error / dt,
// so an angle error at the pi wrap requests 0.4*pi*120 = 151 rad/s with an
// infinite impulse budget: the walk-through-grass gate's worst detonation
// measured exactly 151.2 m/s. A joint may correct toward its target at a
// bounded rate; larger errors resolve over frames (V4.14, third verse).
constexpr float  MAX_ANGULAR_BIAS_VELOCITY = 4.0f; // rad/s
// Wake-on-strain. is_at_rest is a solver optimization and must never make a
// strained bond invisible: rotate a KINEMATIC parent and its sleeping child
// used to ignore the moved anchor entirely (rotation ladder, rung 1). Any
// gluon whose positional error exceeds this wakes both DYNAMIC endpoints at
// row build. Threshold sits above settled-structure residuals (~6 mm on the
// grass-holds gate) so resting forests stay asleep, and far below any real
// strain.
constexpr float  GLUON_WAKE_STRAIN = 0.02f;        // m
constexpr float  GRAVITY           = 9.8f;    // m/s²
constexpr float  FRICTION_COEFFICIENT = 0.5f; // Coulomb friction coefficient (0.5 = moderate grip)
constexpr float  CONTACT_MARGIN    = 0.08f;   // 80mm Z margin for speculative contacts
constexpr float  CONTACT_MARGIN_XY = 0.005f;  // 5mm XY margin to prevent edge touch jitter
constexpr int    CONTACT_PERSISTENCE_FRAMES = 0;  // DISABLED - Keep contacts alive for N frames after separation

// ============================================================================
// UNIFIED CONSTRAINT SOLVER FLAGS (2025-12-17)
// ============================================================================
// Problem: Gluon position projection and contact velocity solver operate in
// separate phases. Position projection moves particles → contact solver detects
// penetration → adds velocity → position projection moves back → OSCILLATION.
//
// Solution: Unify both in the Jacobian solver. Add gentle position bias to
// gluon constraints so contacts and gluons find equilibrium together.
//
// Flags allow A/B testing of old vs new behavior.
// ============================================================================
constexpr bool   ENABLE_GLUON_POSITION_PROJECTION = false;  // Hard path stays off; unified Jacobian alone for now
constexpr bool   ENABLE_GLUON_JACOBIAN_POSITION_BIAS = true;  // V4.10 unified solver: gluons enforce distance via position bias
// With the /dt restored (one correction law), BETA is a true Baumgarte
// gain: the FRACTION of position error corrected per substep. 1.0 means
// "erase the whole error this step", which rings (measured: 113 swings on
// a bond that should sway once). Contacts have used 0.4 all along; bonds
// now use the same discipline, softer because chains stack corrections.
constexpr float  GLUON_POSITION_BETA = 0.2f;

// ============================================================================
// THE TURTLE - World Boundary Collision Plane
// ============================================================================
// "See the TURTLE of enormous girth! On his shell he holds the earth."
//                                            — Terry Pratchett, Small Gods
//
// Every particle simulation needs a bottom. The Turtle is our ONE exception
// to "everything is particles" - a collision plane at z_min that supports
// the entire world.
//
// Properties:
//   - Position: z = TURTLE_Z (below all terrain)
//   - Mass: Infinite (never moves)
//   - Normal: (0, 0, 1) always pointing up
//   - Restitution: 0 (no bounce off bedrock)
//   - Friction: Uses particle's friction coefficient
//
// This replaces the need for kinematic floor tiles. Floor particles can now
// be regular heavy particles gluoned together, supported ultimately by the
// Turtle at the bottom of the world.
// ============================================================================
constexpr float  TURTLE_Z = 0.0f;             // World floor at z=0 (floor tiles sit on Turtle)
constexpr float  TURTLE_RESTITUTION = 0.0f;   // No bounce (bedrock)

// ============================================================================
// FRAME-GATED DAMPING (2025-12-11)
// ============================================================================
// Problem: Gluon-connected particles never reached equilibrium. Solver and
// position projection injected tiny velocities each frame, causing perpetual
// oscillation (0.01-0.3 m/s). Hard clamping caused instability in gluon chains.
//
// Solution: Track consecutive frames at low velocity, only damp after 1 second.
// Real physics (rock hits tree) varies velocity and resets counter.
// Numerical oscillation stays consistently low and triggers damping.
//
// Results (test_rest_diagnostics with 5 trees, 393 particles):
//   Before: 10% at rest, max velocity 0.84 m/s (unstable, growing)
//   After:  98% at rest, max velocity 0.016 m/s (stable)
//
// See: physics_system_v4.cpp:integrate_positions() for implementation
// ============================================================================
// V4.7: Aligned thresholds to prevent oscillation
// Problem: Particles oscillated 0.1-0.35 m/s, never triggering 0.5 m/s threshold
// Solution: Lower threshold to match wake threshold (0.2 m/s) + shorter wait
constexpr float  DAMPING_VELOCITY_THRESHOLD = 0.4f;   // Count frames below this (V4.7: was 0.5)
constexpr float  DAMPING_VELOCITY_THRESHOLD_SQ = DAMPING_VELOCITY_THRESHOLD * DAMPING_VELOCITY_THRESHOLD;
constexpr int    DAMPING_FRAMES_REQUIRED = 30;        // Start damping after 0.5s (V4.7: was 60)
constexpr float  DAMPING_FACTOR = 0.90f;              // 10% velocity reduction per frame (V4.7: was 5%)
constexpr float  ZERO_VELOCITY_SQ = 0.000001f;        // Hard clamp below 1mm/s

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
    float compliance;           // Inverse stiffness. 0 = rigid (contacts). >0 = soft (animation gluons).
    float bias;                 // Velocity bias for position correction

    float accumulated_impulse;  // Total impulse applied (for clamping)

    // SPLIT IMPULSE: the position pass's own accumulator. Kept separate from
    // accumulated_impulse so position repair never enters the body's momentum
    // and never contaminates the warm-start cache.
    float accumulated_pseudo_impulse = 0.0f;
    float min_impulse;          // Clamp min (0 for contacts)
    float max_impulse;          // Clamp max (breaking force for gluons)

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
        , jz(1.0f)              // Default to Z axis
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
        , angular_limit(3.14159f)  // Default: no limit (π = 180°)
    {}
};

// ============================================================================
// CONSTRAINT TYPES
// ============================================================================

enum class ConstraintType {
    CONTACT,  // Non-penetration (can only push apart)
    GLUON     // Fixed distance (can push or pull, may break)
};

// ============================================================================
// WARM STARTING (V4.3) - Persist impulses across frames
// ============================================================================
// Problem: Baumgarte bias converts position error → velocity injection.
// Solver rediscovers equilibrium from scratch every frame, overshooting.
// Result: Perpetual oscillation at 0.2-0.3 m/s instead of rest.
//
// Solution: Cache impulses by contact identity. Resting contacts retain
// their equilibrium impulse across frames. Solver starts near solution,
// doesn't overshoot, no oscillation.
//
// Standard technique used by Box2D, Bullet, PhysX.
// See: docs/PHYSICS_ITERATIVE_RESOLVER_V4.3.md
// ============================================================================
constexpr bool ENABLE_WARM_STARTING = true;

// ============================================================================
// SPLIT IMPULSE (V4.4) - Separate velocity solving from position correction
// ============================================================================
// Problem: Baumgarte bias mixes position correction into velocity solving.
// When warm start overshoots, solver cannot undo it (contacts are one-sided).
// Result: Oscillation that persists and grows through cached impulses.
//
// Solution: Split Impulse (Box2D technique)
//   - Velocity solver: impulse = -v_rel * mass (NO bias, pure velocity)
//   - Position correction: Direct position adjustment (enforce_turtle_boundary)
//
// Contacts can only PUSH apart. By removing bias from velocity solver:
//   - Warm start applies equilibrium impulse (pure velocity)
//   - Solver adds/removes delta to match relative velocity
//   - No energy injection from position correction
//   - Position fix happens AFTER via direct correction
//
// When enabled: bias is NOT used in contact impulse calculation.
// Position correction for Turtle: enforce_turtle_boundary() (already exists)
// Position correction for boxes: enforce_contact_boundary() (TODO if needed)
// ============================================================================
constexpr bool ENABLE_SPLIT_IMPULSE = true;

// Iterations of the split-impulse POSITION pass. Position repair converges far
// faster than the velocity solve because it has no momentum to fight, so this
// is deliberately much smaller than SOLVER_ITERATIONS (32). Raise it only with
// a measurement showing residual overlap that more sweeps actually close.
constexpr int POSITION_ITERATIONS = 8;

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
