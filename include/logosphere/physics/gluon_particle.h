#ifndef GLUON_PARTICLE_H
#define GLUON_PARTICLE_H

#include "particle.h"
#include "math_types.h"
#include <vector>

// ============================================================================
// GLUON PARTICLE SYSTEM
// ============================================================================
// Special non-rendering particles that model connections between regular particles
// Avoids numerical instability of spring-based constraints
// See docs/gluon_particle_proposal.md and docs/gluon_implementation_plan.md
//
// Like gluons in quantum chromodynamics hold quarks together,
// our gluon particles hold visible particles together

// Connection type determines physics behavior
enum class GluonType {
    NAIL,       // Rigid connection with directional stiffness (hard to compress, easier to bend)
    JOINT,      // Rotational connection with angle limits (knee, elbow, shoulder)
    CEMENT,     // Brittle connection (strong but breaks suddenly)
    GLUE,       // Viscous connection (resists fast motion, allows slow deformation)
    MAGIC       // Force field connection (distance-based attraction with visual effect)
};

// Gluon properties with directional physics
// CRITICAL: Everything must be breakable with enough force applied in the right direction
struct GluonProperties {
    // ========================================================================
    // POSITION & ORIENTATION
    // ========================================================================
    Vec3 position;          // Gluon position in world space
    Vec3 primary_axis;      // Main connection axis (e.g., nail direction)
    Vec3 secondary_axis;    // Secondary axis (defines rotation plane)

    // ========================================================================
    // DIRECTIONAL STIFFNESS (N/m)
    // ========================================================================
    // Different resistance to different types of deformation
    float axial_stiffness;      // Along primary axis (compression/tension)
    float shear_stiffness;      // Perpendicular to axis (bending)
    float torsional_stiffness;  // Rotation around axis (twisting)

    // ========================================================================
    // DIRECTIONAL BREAK THRESHOLDS (N)
    // ========================================================================
    // Force limits before connection breaks
    float tensile_limit;        // Pull apart force limit
    float compressive_limit;    // Push together force limit
    float shear_limit;          // Sideways force limit
    float torsion_limit;        // Twisting force limit

    // ========================================================================
    // ROTATION CONSTRAINTS (for JOINT type)
    // ========================================================================
    bool allows_axial_rotation;     // Can spin around primary axis?
    float axial_friction;           // Resistance to axial rotation

    Vec3 rotation_axis;             // Which axis joint bends around
    float rotation_min;             // Minimum rotation angle (radians)
    float rotation_max;             // Maximum rotation angle (radians)
    float rotation_damping;         // Joint shock absorption

    // Break thresholds for rotations (joints)
    float rotation_break_negative;  // Break if bent too far backward (hyperextension)
    float rotation_break_positive;  // Break if bent too far forward
    float lateral_rotation_break;   // Break if twisted sideways (ACL tear scenario)

    // ========================================================================
    // STATE TRACKING
    // ========================================================================
    float accumulated_damage;   // Fatigue over time (0 = new, 1.0 = about to break)
    Vec3 current_force;         // Current force for stress analysis
    Vec3 current_torque;        // Current torque for rotation analysis

    // Connection targets
    std::vector<size_t> bound_particles;  // Indices of particles this gluon connects
    GluonType type;                       // Connection type

    // ========================================================================
    // SPECIAL PROPERTIES
    // ========================================================================
    // For GLUE/CEMENT types
    float yield_rate;           // Rate of plastic deformation (0 = no yield, 1 = instant)
    float viscosity;            // Resistance to motion (0 = none, 100 = honey-like)

    // For MAGIC type
    float field_radius;         // Area of effect for force field
    float field_falloff;        // 1.0 = linear, 2.0 = inverse square
    float transparency;         // Visual effect transparency (0 = invisible, 1 = opaque)
};

// Gluon particle extends regular particle with connection properties
struct GluonParticle : public Particle {
    // Inherited from Particle:
    // - Position (x, y, z)
    // - Velocity (vx, vy, vz)
    // - Mass (tiny, almost massless)
    // - Forces (fx, fy, fz)

    // Gluon-specific properties
    GluonProperties gluon_props;

    // Gluon particles have special physics rules
    bool is_gluon = true;              // Identifies this as a gluon particle
    bool allows_penetration = true;     // Can overlap other particles
    bool is_rendered = false;           // Invisible by default

    // Initialize gluon with sensible defaults
    GluonParticle() {
        // Low density - mass will auto-calculate from volume and density
        material_density = 100.0f;  // Low density

        // Not rendered
        is_light_source = false;
        a = 0.0f;  // Fully transparent

        // Default to nail-like properties
        gluon_props.type = GluonType::NAIL;
        gluon_props.axial_stiffness = 200000.0f;    // Very stiff axially
        gluon_props.shear_stiffness = 5000.0f;      // Bendable
        gluon_props.torsional_stiffness = 100.0f;   // Easy to twist

        gluon_props.tensile_limit = 5000.0f;        // Hard to pull apart
        gluon_props.compressive_limit = 50000.0f;   // Very hard to compress
        gluon_props.shear_limit = 2000.0f;          // Can bend/break sideways
        gluon_props.torsion_limit = 500.0f;         // Twists relatively easily

        gluon_props.allows_axial_rotation = true;
        gluon_props.axial_friction = 10.0f;

        gluon_props.accumulated_damage = 0.0f;
    }

    // Factory methods for different gluon types
    static GluonParticle CreateNail(const Vec3& pos, const Vec3& axis);
    static GluonParticle CreateKneeJoint(const Vec3& pos, const Vec3& bend_axis);
    static GluonParticle CreateShoulderJoint(const Vec3& pos);
    static GluonParticle CreateCement(const Vec3& pos);
    static GluonParticle CreateGlue(const Vec3& pos, float viscosity);
    static GluonParticle CreateMagicField(const Vec3& pos, float radius);
};

// ============================================================================
// FORCE DECOMPOSITION HELPERS
// ============================================================================
// Decompose force into axial and shear components relative to connection axis

struct ForceDecomposition {
    float axial_force;      // Component along axis (compression if negative, tension if positive)
    Vec3 shear_force;       // Component perpendicular to axis
    float torsional_moment; // Torque around axis
};

// Decompose a net force into directional components
ForceDecomposition decompose_force(const Vec3& force, const Vec3& axis, const Vec3& moment_arm);

// Check if gluon should break based on current forces
bool should_gluon_break(const GluonProperties& props, const ForceDecomposition& forces);

// Calculate stress level (0.0 = no stress, 1.0 = at breaking point)
float calculate_stress_level(const GluonProperties& props, const ForceDecomposition& forces);

#endif // GLUON_PARTICLE_H