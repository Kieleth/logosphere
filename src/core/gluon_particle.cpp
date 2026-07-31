#include "logosphere/physics/gluon_particle.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// VECTOR MATH HELPERS (since math_types.h Vec3 doesn't have these)
// ============================================================================

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

// ============================================================================
// FORCE DECOMPOSITION
// ============================================================================

ForceDecomposition decompose_force(const Vec3& force, const Vec3& axis, const Vec3& moment_arm) {
    ForceDecomposition result;

    // Normalize axis to ensure proper projection
    Vec3 normalized_axis = axis.normalized();

    // Axial component: projection of force onto axis
    // Positive = tension (pulling apart), Negative = compression (pushing together)
    result.axial_force = dot(force, normalized_axis);

    // Shear component: force perpendicular to axis
    Vec3 axial_component = normalized_axis * result.axial_force;
    result.shear_force = force - axial_component;

    // Torsional moment: torque around the axis
    // τ = r × F, then project onto axis
    Vec3 torque = cross(moment_arm, force);
    result.torsional_moment = dot(torque, normalized_axis);

    return result;
}

bool should_gluon_break(const GluonProperties& props, const ForceDecomposition& forces) {
    // Check tensile limit (pulling apart)
    if (forces.axial_force > 0 && forces.axial_force > props.tensile_limit) {
        return true;  // Pulled apart
    }

    // Check compressive limit (pushing together)
    if (forces.axial_force < 0 && std::abs(forces.axial_force) > props.compressive_limit) {
        return true;  // Crushed
    }

    // Check shear limit (sideways force)
    float shear_magnitude = forces.shear_force.length();
    if (shear_magnitude > props.shear_limit) {
        return true;  // Bent/sheared
    }

    // Check torsion limit (twisting)
    if (std::abs(forces.torsional_moment) > props.torsion_limit) {
        return true;  // Twisted off
    }

    // Check accumulated damage (fatigue failure)
    if (props.accumulated_damage >= 1.0f) {
        return true;  // Fatigue failure
    }

    return false;  // Connection holds
}

float calculate_stress_level(const GluonProperties& props, const ForceDecomposition& forces) {
    float max_stress = 0.0f;

    // Calculate stress ratio for each force type
    if (forces.axial_force > 0) {
        // Tensile stress
        max_stress = std::max(max_stress, forces.axial_force / props.tensile_limit);
    } else {
        // Compressive stress
        max_stress = std::max(max_stress, std::abs(forces.axial_force) / props.compressive_limit);
    }

    // Shear stress
    float shear_magnitude = forces.shear_force.length();
    max_stress = std::max(max_stress, shear_magnitude / props.shear_limit);

    // Torsional stress
    max_stress = std::max(max_stress, std::abs(forces.torsional_moment) / props.torsion_limit);

    // Include accumulated damage
    max_stress = std::max(max_stress, props.accumulated_damage);

    // Clamp to [0, 1] range (can exceed 1.0 when breaking)
    return std::min(max_stress, 1.0f);
}

// ============================================================================
// FACTORY METHOD FOR NAIL
// ============================================================================

GluonParticle GluonParticle::CreateNail(const Vec3& pos, const Vec3& axis) {
    GluonParticle nail;

    // Position and orientation
    nail.x = pos.x;
    nail.y = pos.y;
    nail.z = pos.z;
    nail.gluon_props.position = pos;
    nail.gluon_props.primary_axis = axis.normalized();

    // Calculate perpendicular axis for shear
    Vec3 up = {0, 0, 1};
    if (std::abs(dot(axis, up)) > 0.99f) {
        up = {1, 0, 0};  // Use different axis if nail is vertical
    }
    nail.gluon_props.secondary_axis = cross(axis, up).normalized();

    // Nail properties (from implementation plan)
    nail.gluon_props.type = GluonType::NAIL;

    // Stiffness: very hard to compress/pull, easier to bend
    nail.gluon_props.axial_stiffness = 200000.0f;     // Very stiff along nail
    nail.gluon_props.shear_stiffness = 5000.0f;       // Can bend with force
    nail.gluon_props.torsional_stiffness = 100.0f;    // Easy to rotate around

    // Break thresholds: hard to pull out, easier to bend/break
    nail.gluon_props.tensile_limit = 5000.0f;         // Hard to pull out
    nail.gluon_props.compressive_limit = 50000.0f;    // Very hard to push through
    nail.gluon_props.shear_limit = 2000.0f;           // Can bend/break sideways
    nail.gluon_props.torsion_limit = 500.0f;          // Twists relatively easily

    // Rotation: allows spinning around nail axis
    nail.gluon_props.allows_axial_rotation = true;
    nail.gluon_props.axial_friction = 10.0f;          // Some resistance

    nail.gluon_props.accumulated_damage = 0.0f;

    return nail;
}

// Placeholder implementations for other types (to be implemented later)
GluonParticle GluonParticle::CreateKneeJoint(const Vec3& pos, const Vec3& bend_axis) {
    // For now, just return a nail
    return CreateNail(pos, bend_axis);
}

GluonParticle GluonParticle::CreateShoulderJoint(const Vec3& pos) {
    // For now, just return a nail
    return CreateNail(pos, {0, 0, 1});
}

GluonParticle GluonParticle::CreateCement(const Vec3& pos) {
    // For now, just return a nail
    return CreateNail(pos, {0, 0, 1});
}

GluonParticle GluonParticle::CreateGlue(const Vec3& pos, float viscosity) {
    // For now, just return a nail
    (void)viscosity; // Unused parameter
    return CreateNail(pos, {0, 0, 1});
}

GluonParticle GluonParticle::CreateMagicField(const Vec3& pos, float radius) {
    // For now, just return a nail
    (void)radius; // Unused parameter
    return CreateNail(pos, {0, 0, 1});
}