#ifndef CELESTIAL_SYSTEM_H
#define CELESTIAL_SYSTEM_H

#include <vector>
#include <string>
#include <cstdint>
#include "logosphere/kg/kg_types.h"

// Forward declarations
class ParticleSystem;
namespace kg { class KGModule; }

namespace Celestial {

// Color with alpha for blending
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// Keyframe for interpolation (time is fraction of orbit period, 0.0-1.0)
template<typename T>
struct Keyframe {
    float time;  // 0.0 = start of period, 1.0 = end
    T value;
};

// Orbit parameters - supports circular and elliptical orbits
struct OrbitParams {
    float period_days = 1.0f;      // How many game-days for one orbit
    float phase_offset = 0.0f;     // Starting phase (0.0-1.0)
    float distance = 200.0f;       // Distance from origin (semi-major axis)
    float eccentricity = 0.0f;     // 0.0 = circular, 0.0-1.0 = elliptical
    float inclination_deg = 0.0f;  // Tilt from horizontal plane
    float longitude_deg = 0.0f;    // Rotation around vertical axis

    // For bodies orbiting other bodies (moons)
    int parent_body_index = -1;    // -1 = orbits origin, else index of parent
};

// Configuration for a single celestial body
struct CelestialBodyConfig {
    std::string name = "body";
    OrbitParams orbit;

    // Color keyframes (interpolated based on orbit phase)
    // Default: single white color
    std::vector<Keyframe<Color>> color_curve = {{0.0f, {1.0f, 1.0f, 1.0f, 1.0f}}};

    // Emission keyframes in lumens (interpolated based on orbit phase)
    // Default: constant emission
    std::vector<Keyframe<float>> emission_curve = {{0.0f, 100000000.0f}};

    // Visual properties
    float visual_radius = 5.0f;    // Size of the light particle
    bool cast_shadows = true;      // Whether this body casts shadows

    // Below horizon behavior
    float horizon_z = 0.0f;        // Z level considered "horizon"
    bool dim_below_horizon = true; // Fade out when below horizon
};

// ---------------------------------------------------------------------------
// Moon factory (engine preset). Earth's moon by default; exotic moons
// (blood red, viridian, multiple) via the spec. Games consume this
// instead of hand-rolling configs so every world's moons behave the
// same: anti-phase with the sun (zenith at world midnight), shining
// only at night, light scaled from a cinematic full-moon baseline.
// ---------------------------------------------------------------------------
struct MoonSpec {
    // Surface/glow color. Default: Earth silver.
    float r = 0.93f, g = 0.93f, b = 0.88f;
    // 1.0 = Earth full moon (cinematic ~400 lux); 0.2 pale sliver,
    // 3.0 an impossible bright giant.
    float brightness = 1.0f;
    float size = 5.0f;             // visual radius of the far particle (m)
    // 0.5 = exactly anti-phase with the sun (up at night). Stagger
    // additional moons around it.
    float phase_offset = 0.5f;
    float distance = 380.0f;       // far particle, never in frame
    // The ECLIPTIC: moons ride the sun's orbital plane (inclination
    // 60, longitude 0 — a circle proven to never project into the
    // iso window by the off-camera sweep test). In an orthographic
    // camera, ANY orbit crossing the view axis flashes through
    // screen center regardless of distance — sharing the sun's
    // plane is the guarantee, staggering by phase the variety.
    float inclination_deg = 60.0f;
};

CelestialBodyConfig make_moon(const MoonSpec& spec = {});

// Runtime state of a celestial body
struct CelestialBody {
    CelestialBodyConfig config;
    int particle_id = -1;              // ID in ParticleSystem
    kg::EntityID entity_id = 0;        // KG entity for BVH grouping

    // Current state (updated each frame)
    float current_phase = 0.0f;    // 0.0-1.0 position in orbit
    float x = 0.0f, y = 0.0f, z = 0.0f;  // World position
    Color current_color;
    float current_emission = 0.0f;
    bool is_visible = true;        // Above horizon?
};

// CelestialSystem - manages any number of celestial bodies
class CelestialSystem {
public:
    CelestialSystem() = default;

    // Initialize with particle system and KG module references
    void initialize(ParticleSystem* particle_system, kg::KGModule* kg_module);

    // Add a celestial body, returns index
    int add_body(const CelestialBodyConfig& config);

    // Remove a body by index
    void remove_body(int index);

    // Update all bodies based on current game time (in seconds)
    void update(double game_time_seconds);

    // Getters
    size_t body_count() const { return bodies_.size(); }
    const CelestialBody* get_body(int index) const;
    CelestialBody* get_body_mutable(int index);

    // Find body by name (returns -1 if not found)
    int find_body(const std::string& name) const;

    // Query helpers
    bool is_any_body_above_horizon() const;
    float get_total_ambient_light() const;  // Sum of all visible body emissions

private:
    ParticleSystem* particle_system_ = nullptr;
    kg::KGModule* kg_module_ = nullptr;
    std::vector<CelestialBody> bodies_;

    // Helper functions
    void calculate_position(CelestialBody& body, double game_time_seconds);
    void update_particle(CelestialBody& body);

    // Note: interpolation helpers are static functions in .cpp
};

} // namespace Celestial

#endif // CELESTIAL_SYSTEM_H
