#include "celestial_system.h"
#include "../core/particle_system.h"
#include "../particle.h"
#include "../core/game_time.h"
#include "logosphere/kg/kg_module.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace Celestial {

// Constants
constexpr double SECONDS_PER_DAY = 86400.0;  // 24 * 60 * 60
constexpr float PI = 3.14159265359f;
constexpr float DEG_TO_RAD = PI / 180.0f;

// Forward declare interpolation helpers (defined below, used in calculate_position)
static float interpolate_float_keyframes(const std::vector<Keyframe<float>>& keyframes, float phase);
static Color interpolate_color_keyframes(const std::vector<Keyframe<Color>>& keyframes, float phase);
static Color lerp_color(const Color& a, const Color& b, float t);

CelestialBodyConfig make_moon(const MoonSpec& spec) {
    CelestialBodyConfig moon;
    moon.name = "moon";
    moon.orbit.period_days = 1.0f;
    moon.orbit.phase_offset = spec.phase_offset;
    moon.orbit.distance = spec.distance;
    moon.orbit.inclination_deg = spec.inclination_deg;
    moon.visual_radius = spec.size;
    moon.cast_shadows = true;
    moon.dim_below_horizon = true;
    moon.color_curve = {{0.0f, {spec.r, spec.g, spec.b, 1.0f}}};
    // Emission keyed to the moon's OWN phase: zenith at 0.5 (= world
    // midnight when anti-phase), dark through the day hours. Base:
    // cinematic full moon ~400 lux at the default distance.
    // lux_at_ground = E / d^2  =>  E = lux * d^2
    float peak = 400.0f * spec.distance * spec.distance * spec.brightness;
    moon.emission_curve = {
        {0.00f, 0.0f},
        {0.28f, 0.0f},
        {0.35f, peak * 0.35f},
        {0.50f, peak},
        {0.65f, peak * 0.35f},
        {0.72f, 0.0f},
        {1.00f, 0.0f},
    };
    return moon;
}

void CelestialSystem::initialize(ParticleSystem* particle_system, kg::KGModule* kg_module) {
    particle_system_ = particle_system;
    kg_module_ = kg_module;
}

int CelestialSystem::add_body(const CelestialBodyConfig& config) {
    if (!particle_system_) {
        std::cerr << "[CelestialSystem] ERROR: Not initialized with particle system" << std::endl;
        return -1;
    }

    CelestialBody body;
    body.config = config;

    // Create KG entity for this celestial body (for Entity BVH grouping)
    if (kg_module_) {
        body.entity_id = kg_module_->createEntity("CelestialBody");
        kg_module_->setProperty(body.entity_id, "name", config.name);
        kg_module_->setProperty(body.entity_id, "type", "celestial");
    }

    // Create light particle for this body
    Particle p;
    p.is_light_source = true;
    p.SetMaterial(Materials::Type::LIGHT);  // Celestial bodies float - not subject to physics
    p.size = config.visual_radius;
    p.reflectivity = 0.0f;  // Lights don't reflect
    p.emission_radius = 1000.0f;  // Large radius for celestial bodies

    // Initial position will be calculated on first update
    p.x = 0.0f;
    p.y = 0.0f;
    p.z = config.orbit.distance;  // Start above

    // Initial color from first keyframe
    if (!config.color_curve.empty()) {
        const auto& c = config.color_curve[0].value;
        p.r = c.r;
        p.g = c.g;
        p.b = c.b;
    }

    // Initial emission from first keyframe
    if (!config.emission_curve.empty()) {
        p.emission_strength = config.emission_curve[0].value;
    }

    // Create particle bound to entity
    body.particle_id = particle_system_->add_particle_to_entity(p, kg_module_, body.entity_id);

    int index = static_cast<int>(bodies_.size());
    bodies_.push_back(body);

    std::cout << "[CelestialSystem] Added body '" << config.name
              << "' with particle_id=" << body.particle_id
              << " entity_id=" << body.entity_id << std::endl;

    return index;
}

void CelestialSystem::remove_body(int index) {
    if (index < 0 || index >= static_cast<int>(bodies_.size())) {
        return;
    }

    // Mark particle for deletion (ParticleSystem handles cleanup)
    // Note: ParticleSystem doesn't have a remove method, particles persist
    // We could set emission_strength to 0 to effectively disable it

    bodies_.erase(bodies_.begin() + index);
}

void CelestialSystem::update(double game_time_seconds) {
    static int frame_count = 0;
    for (auto& body : bodies_) {
        calculate_position(body, game_time_seconds);
        update_particle(body);

        // Debug output every 60 frames (~1 second)
        if (frame_count % 60 == 0) {
            std::cout << "[Celestial] " << body.config.name
                      << " pos=(" << body.x << "," << body.y << "," << body.z << ")"
                      << " phase=" << body.current_phase
                      << " emission=" << body.current_emission
                      << " visible=" << body.is_visible << std::endl;
        }
    }
    frame_count++;
}

void CelestialSystem::calculate_position(CelestialBody& body, double game_time_seconds) {
    const auto& orbit = body.config.orbit;

    // Calculate orbital phase (0.0 to 1.0)
    double period_seconds = orbit.period_days * SECONDS_PER_DAY;
    double orbit_progress = std::fmod(game_time_seconds / period_seconds + orbit.phase_offset, 1.0);
    body.current_phase = static_cast<float>(orbit_progress);

    // Angle in radians (0 = east, rotates counter-clockwise as viewed from above)
    float angle = body.current_phase * 2.0f * PI;

    // Calculate position on orbit
    float distance = orbit.distance;

    // Apply eccentricity (elliptical orbit)
    if (orbit.eccentricity > 0.0f) {
        // Simple ellipse: r = a(1-e²)/(1+e·cos(θ))
        float e = orbit.eccentricity;
        distance = orbit.distance * (1.0f - e * e) / (1.0f + e * std::cos(angle));
    }

    // ========================================================
    // COORDINATE SYSTEM (from docs/ARCHITECTURE.md):
    //   X = East(+) / West(-)
    //   Y = North(+) / South(-)
    //   Z = Up(+) / Down(-)
    //
    // SUN PATH (northern hemisphere):
    //   Midnight: North (+Y), underground (-Z)
    //   Sunrise:  East (+X), horizon (Z=0)
    //   Noon:     South (-Y), highest (+Z)
    //   Sunset:   West (-X), horizon (Z=0)
    // ========================================================

    // Azimuth: full rotation per day (angle increases with time)
    // angle=0 → midnight, angle=π/2 → sunrise, angle=π → noon, angle=3π/2 → sunset
    float circle_x = std::sin(angle);   // 0 at midnight/noon, +1 at sunrise, -1 at sunset
    float circle_y = std::cos(angle);   // +1 at midnight, -1 at noon, 0 at sunrise/sunset

    // Tilt Y axis into Z (sun arcs overhead, not just around horizon)
    // inclination_deg = how high sun gets at noon (45° = halfway to zenith)
    float incl = orbit.inclination_deg * DEG_TO_RAD;

    body.x = distance * circle_x;                        // East(+) at sunrise, West(-) at sunset
    body.y = distance * circle_y * std::cos(incl);       // North(+) at midnight, South(-) at noon
    body.z = -distance * circle_y * std::sin(incl);      // Down(-) at midnight, Up(+) at noon

    // Handle parent body (for moons orbiting planets, etc.)
    if (orbit.parent_body_index >= 0 &&
        orbit.parent_body_index < static_cast<int>(bodies_.size())) {
        const auto& parent = bodies_[orbit.parent_body_index];
        body.x += parent.x;
        body.y += parent.y;
        body.z += parent.z;
    }

    // Interpolate color based on phase
    body.current_color = interpolate_color_keyframes(body.config.color_curve, body.current_phase);

    // Interpolate emission based on phase
    body.current_emission = interpolate_float_keyframes(body.config.emission_curve, body.current_phase);

    // Check visibility (above horizon)
    body.is_visible = body.z >= body.config.horizon_z;

    // Apply below-horizon dimming
    if (!body.is_visible && body.config.dim_below_horizon) {
        // Fade out smoothly below horizon
        float below = body.config.horizon_z - body.z;
        float fade = std::max(0.0f, 1.0f - below / 20.0f);  // Fade over 20 units
        body.current_emission *= fade;
    }
}

void CelestialSystem::update_particle(CelestialBody& body) {
    if (!particle_system_ || body.particle_id < 0) {
        return;
    }

    // Get mutable access to particle using safe API
    auto write_view = particle_system_->lock_particles_for_write();
    auto& particles = write_view.get_particles();

    if (body.particle_id >= static_cast<int>(particles.size())) {
        std::cerr << "[CELESTIAL ERROR] particle_id " << body.particle_id
                  << " >= particles.size() " << particles.size() << std::endl;
        return;
    }

    auto& p = particles[body.particle_id];

    // DEBUG: Verify we're updating a light source
    static int verify_count = 0;
    if (verify_count++ % 300 == 0) {  // Log every 5 seconds at 60fps
        std::cout << "[CELESTIAL VERIFY] " << body.config.name
                  << " updating particle_id=" << body.particle_id
                  << " is_light_source=" << p.is_light_source
                  << " current_pos=(" << p.x << "," << p.y << "," << p.z << ")"
                  << " emission=" << p.emission_strength << std::endl;
        if (!p.is_light_source) {
            std::cerr << "[CELESTIAL ERROR] Particle " << body.particle_id
                      << " is NOT a light source! Something corrupted the reference!" << std::endl;
        }
    }

    // Update position
    p.x = body.x;
    p.y = body.y;
    p.z = body.z;

    // Update color
    p.r = body.current_color.r;
    p.g = body.current_color.g;
    p.b = body.current_color.b;

    // Update emission
    p.emission_strength = body.current_emission;
}

const CelestialBody* CelestialSystem::get_body(int index) const {
    if (index < 0 || index >= static_cast<int>(bodies_.size())) {
        return nullptr;
    }
    return &bodies_[index];
}

CelestialBody* CelestialSystem::get_body_mutable(int index) {
    if (index < 0 || index >= static_cast<int>(bodies_.size())) {
        return nullptr;
    }
    return &bodies_[index];
}

int CelestialSystem::find_body(const std::string& name) const {
    for (size_t i = 0; i < bodies_.size(); ++i) {
        if (bodies_[i].config.name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool CelestialSystem::is_any_body_above_horizon() const {
    for (const auto& body : bodies_) {
        if (body.is_visible) {
            return true;
        }
    }
    return false;
}

float CelestialSystem::get_total_ambient_light() const {
    float total = 0.0f;
    for (const auto& body : bodies_) {
        if (body.is_visible) {
            total += body.current_emission;
        }
    }
    return total;
}

// Static helper: interpolate float keyframes
static float interpolate_float_keyframes(const std::vector<Keyframe<float>>& keyframes, float phase) {
    if (keyframes.empty()) {
        return 0.0f;
    }
    if (keyframes.size() == 1) {
        return keyframes[0].value;
    }

    // Find surrounding keyframes
    const Keyframe<float>* prev = &keyframes.back();
    const Keyframe<float>* next = &keyframes.front();

    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].time > phase) {
            next = &keyframes[i];
            prev = (i > 0) ? &keyframes[i - 1] : &keyframes.back();
            break;
        }
        prev = &keyframes[i];
        next = (i + 1 < keyframes.size()) ? &keyframes[i + 1] : &keyframes.front();
    }

    // Calculate interpolation factor
    float range = next->time - prev->time;
    if (range < 0) range += 1.0f;  // Wrap around
    float local_phase = phase - prev->time;
    if (local_phase < 0) local_phase += 1.0f;
    float t = (range > 0.0001f) ? local_phase / range : 0.0f;

    // Linear interpolation
    return prev->value + t * (next->value - prev->value);
}

// Static helper: lerp between two colors
static Color lerp_color(const Color& a, const Color& b, float t) {
    return Color{
        a.r + t * (b.r - a.r),
        a.g + t * (b.g - a.g),
        a.b + t * (b.b - a.b),
        a.a + t * (b.a - a.a)
    };
}

// Static helper: interpolate color keyframes
static Color interpolate_color_keyframes(const std::vector<Keyframe<Color>>& keyframes, float phase) {
    if (keyframes.empty()) {
        return Color{1.0f, 1.0f, 1.0f, 1.0f};
    }
    if (keyframes.size() == 1) {
        return keyframes[0].value;
    }

    // Find surrounding keyframes
    const Keyframe<Color>* prev = &keyframes.back();
    const Keyframe<Color>* next = &keyframes.front();

    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (keyframes[i].time > phase) {
            next = &keyframes[i];
            prev = (i > 0) ? &keyframes[i - 1] : &keyframes.back();
            break;
        }
        prev = &keyframes[i];
        next = (i + 1 < keyframes.size()) ? &keyframes[i + 1] : &keyframes.front();
    }

    // Calculate interpolation factor
    float range = next->time - prev->time;
    if (range < 0) range += 1.0f;
    float local_phase = phase - prev->time;
    if (local_phase < 0) local_phase += 1.0f;
    float t = (range > 0.0001f) ? local_phase / range : 0.0f;

    return lerp_color(prev->value, next->value, t);
}

} // namespace Celestial
