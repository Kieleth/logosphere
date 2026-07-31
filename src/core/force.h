#ifndef FORCE_H
#define FORCE_H

#include "particle.h"

// ============================================================================
// Force Base Class - Strategy Pattern for Physics Forces
// ============================================================================
// See docs/PHYSICS_v2.md "Force Registry Architecture"
//
// DESIGN: Each force is a modular strategy that can be added/removed at runtime
// PHYSICS: Follows superposition principle (F_net = ΣF_i)
// BENEFITS:
//   - Parallelizable (each particle independent)
//   - Testable (each force isolated)
//   - Extensible (add new forces without modifying integration code)

class Force {
public:
    virtual ~Force() = default;

    // Apply force to particle (modifies fx, fy, fz accumulators)
    // IMPORTANT: Use += not = to respect superposition principle
    virtual void apply(Particle& p, float dt) = 0;

    // Does this force affect this particle?
    // Optimization: Skip force application if not applicable
    virtual bool affects(const Particle& p) const = 0;
};

// ============================================================================
// Gravity Force - Uniform gravitational field
// ============================================================================
// PHYSICS: F = ma where a = g (gravitational acceleration)
// MATH: If mass=0, then F=0 automatically (kinematic particles)
//
// Examples:
//   Earth:    GravityForce(0, 0, -9.8)
//   Moon:     GravityForce(0, 0, -1.6)
//   Sideways: GravityForce(-9.8, 0, 0)  // Puzzle level
//   Zero-G:   GravityForce(0, 0, 0)     // Space level

class GravityForce : public Force {
private:
    float gx_;  // Gravity vector (m/s²)
    float gy_;
    float gz_;

public:
    GravityForce(float x, float y, float z)
        : gx_(x), gy_(y), gz_(z) {}

    void apply(Particle& p, float dt) override {
        // F = ma
        // If mass=0 (floating/light), then F=0 automatically - no conditionals needed!
        float mass = p.GetMass();
        p.fx += mass * gx_;
        p.fy += mass * gy_;
        p.fz += mass * gz_;
    }

    bool affects(const Particle& p) const override {
        // Only dynamic particles are affected
        // Floating particles (mass=0) return false here (optimization)
        return p.GetMass() > 0.0f;
    }

    // Change gravity at runtime (moon level, rotating gravity, etc.)
    void set_gravity(float x, float y, float z) {
        gx_ = x;
        gy_ = y;
        gz_ = z;
    }

    // Get current gravity vector (for tests)
    float get_gx() const { return gx_; }
    float get_gy() const { return gy_; }
    float get_gz() const { return gz_; }
};

#endif // FORCE_H
