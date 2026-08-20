#ifndef PARTICLE_TYPES_H
#define PARTICLE_TYPES_H

#include <cstdint>

// =============================================================================
// ODOR TYPES — entity-based smell emission for creature senses.
// Particles emit odors that creatures detect without line-of-sight; different
// creature types react differently to different odor types.
// =============================================================================
enum class OdorType : uint8_t {
    NONE = 0,           // No odor (rocks, walls, most objects)
    LIVING_FLESH,       // Living humans, animals (what undead want)
    FRESH_MEAT,         // Freshly killed, uncooked meat
    BLOOD,              // Open wounds, blood pools, fresh kills
    DECAY,              // Rotting undead (shamblers emit this)
    ROTTEN,             // Meat that's too far gone
    SMOKE,              // Fire, torches (environmental)
};

// =============================================================================
// PARTICLE SHAPE — BOX uses (width, height, thickness). SPHERE uses `size`
// as diameter. ELLIPSOID uses (width, height, thickness) as per-axis diameters.
//
// Minimum visible dimension: 0.15 m — below that a particle projects to
// sub-pixel size and the rasterizer culls the degenerate triangles.
// =============================================================================
enum class ParticleShape {
    BOX,
    SPHERE,     // `size` is diameter; radius = size / 2 on all axes
    ELLIPSOID,  // (width, height, thickness) are per-axis diameters
};

// =============================================================================
// PARTICLE OWNERSHIP — which system has authority over this particle.
//
// Priority: ANIMATION > DYNAMICS > PHYSICS.
//
// owner == DYNAMICS:
//   physics skips gravity / gluon constraints / impulse application but STILL
//   detects collisions and emits CollisionEvents.
//
// owner == ANIMATION:
//   FK system controls position via joint angles; dynamics skips shape
//   maintenance. The animation system must restore DYNAMICS when done.
// =============================================================================
enum class ParticleOwner : uint8_t {
    PHYSICS = 0,  // Default: physics owns gravity, impulses, gluons
    DYNAMICS,     // Dynamics owns motion; physics only detects collisions
    ANIMATION,    // Animation owns positions via FK
    STATIC,       // Reserved — only the turtle world boundary is truly immovable
};

// =============================================================================
// PARTICLE SOLVER MODE — physics-layer authority over integration.
//
// Generic mechanism: tells the physics solver whether to integrate this
// particle's state, treat it as infinite mass (external writer owns position),
// or skip it entirely. No game-layer coupling — any system can set this.
//
// DYNAMIC:    physics integrates position + velocity + rotation every step.
//             Gravity, gluons, contact impulses all apply normally. This is
//             the default for any newly-spawned particle.
// KINEMATIC:  physics treats as infinite mass (inv_mass = 0). Position is
//             written externally (by whoever wrote it last — animation,
//             dynamics, scripted movement, etc.). Collisions still detected
//             and contacts generated; impulses from other bodies are
//             absorbed (no bounce back). The solver does not integrate.
// STATIC:     reserved. Only the turtle world boundary is truly immovable.
//             Physics solver handles the turtle specially.
//
// The `owner` enum above is a game-layer concept ("which system wrote this
// particle last"). `solver_mode` is the physics-layer concept. They map
// but are not identical — an ANIMATION-owned particle is typically
// KINEMATIC, but the physics solver only needs to see KINEMATIC to know
// not to integrate.
// =============================================================================
enum class ParticleSolverMode : uint8_t {
    DYNAMIC = 0,  // Default: physics integrates this particle
    KINEMATIC,    // An external writer OWNS this body's position RIGHT NOW
                  // (animation, locomotion, a driver). It is a transient
                  // authority, not a label: whoever sets it must release it
                  // when they stop driving, or the body can never fall,
                  // tip or ragdoll again. NOT a way to make scenery
                  // immovable — see INV-1 and the substrate direction.
    // STATIC was here until 2026-08-14. It was accepted from the KG
    // (entity_physical_state.cpp) and handled by NOTHING in the solver:
    // a body set STATIC fell silently. Owner order: eradicate, legacy.
};

#endif  // PARTICLE_TYPES_H
