// SenseSystem - Configurable creature perception via BVH raycasting
//
// Provides sight (and future sound/smell) for NPCs and player.
// Vision cone is cast via BVH rays for occlusion-aware detection.
//
// Design: Core sensing in logosphere (reusable), game config in logomancers.

#ifndef SENSE_SYSTEM_H
#define SENSE_SYSTEM_H

#include <vector>
#include <cmath>
#include "particle.h"

// Forward declaration - BVH defined in bvh.h
class BVH;

// Configuration for creature senses
// All parameters are tunable per-creature based on perception attributes
struct SenseConfig {
    // Vision parameters
    float fov_degrees = 90.0f;      // Field of view (configurable per creature)
    float vision_range = 20.0f;     // Max sight distance in world units
    int ray_count = 8;              // Rays per update (accuracy vs performance)

    // Quality/Impairments (0.0 = blind, 1.0 = perfect vision)
    float vision_quality = 1.0f;    // Multiplier on range
    bool left_eye_blind = false;    // Only see right half of FOV
    bool right_eye_blind = false;   // Only see left half of FOV

    // Defaults for common archetypes
    static SenseConfig zombie() {
        return SenseConfig{60.0f, 10.0f, 4, 0.7f, false, false};
    }
    static SenseConfig human() {
        // 24 rays across 90° FOV = 3.75° spacing
        // Can detect ~0.3m objects at 5m distance reliably
        return SenseConfig{90.0f, 30.0f, 24, 1.0f, false, false};
    }
    static SenseConfig eagle() {
        return SenseConfig{120.0f, 100.0f, 16, 1.0f, false, false};
    }
};

// =============================================================================
// SMELL CONFIGURATION AND RESULTS
// =============================================================================

// Configuration for smell-based detection
// Unlike vision, smell is omnidirectional but probabilistic
struct SmellConfig {
    float sensitivity = 1.0f;           // Multiplier on detection (0.5 = half as good)
    float state_factor = 1.0f;          // Based on creature state (SCAN=1.0, WALK=0.3)
    std::vector<OdorType> attracted_to; // Which odors trigger detection

    // Shambler default: attracted to living flesh, fresh meat, blood
    static SmellConfig shambler() {
        SmellConfig cfg;
        cfg.sensitivity = 1.0f;
        cfg.attracted_to = {OdorType::LIVING_FLESH, OdorType::FRESH_MEAT, OdorType::BLOOD};
        return cfg;
    }
};

// Result from a smell check
struct SmellResult {
    bool detected = false;              // Did we smell something?
    int source_particle_id = -1;        // Which particle we smelled
    OdorType type = OdorType::NONE;     // What kind of odor
    float distance = 0.0f;              // Distance to source
    float direction = 0.0f;             // Angle to source (radians, 0 = +Y/north)
    float direction_error = 0.0f;       // Uncertainty in direction (based on Discernment)
};

// A detected target from sensing
struct SenseTarget {
    int particle_id;        // Which particle was seen
    float x, y, z;          // World position
    float distance;         // Distance from viewer
    float angle_offset;     // Angle from facing direction (radians)
};

// Result from a sense query
struct SenseResult {
    std::vector<SenseTarget> visible_targets;

    // Convenience helpers
    bool any_visible() const { return !visible_targets.empty(); }

    const SenseTarget* nearest() const {
        if (visible_targets.empty()) return nullptr;
        const SenseTarget* best = &visible_targets[0];
        for (const auto& t : visible_targets) {
            if (t.distance < best->distance) best = &t;
        }
        return best;
    }

    const SenseTarget* find_by_id(int particle_id) const {
        for (const auto& t : visible_targets) {
            if (t.particle_id == particle_id) return &t;
        }
        return nullptr;
    }
};

// Main sensing system - casts vision cones via BVH
class SenseSystem {
public:
    // Cast vision cone from viewer position/facing
    // Returns all visible targets from target_ids (or all particles if empty)
    //
    // Parameters:
    //   config: Vision parameters (FOV, range, rays)
    //   viewer_x/y/z: Viewer's eye position in world
    //   facing_angle: Direction viewer is looking (radians, 0 = +Y north)
    //   particles: All particles in the world
    //   bvh: BVH for occlusion testing
    //   target_ids: Which particle IDs to check (empty = check all)
    //   exclude_ids: Particle IDs to exclude (self + body parts)
    //
    SenseResult cast_vision_cone(
        const SenseConfig& config,
        float viewer_x, float viewer_y, float viewer_z,
        float facing_angle,
        const std::vector<Particle>& particles,
        const BVH& bvh,
        const std::vector<int>& target_ids = {},
        const std::vector<int>& exclude_ids = {}
    ) const;

    // Single-target visibility check (for PROBE during avoidance)
    // Returns true if can see from viewer to target position
    bool can_see_point(
        float viewer_x, float viewer_y, float viewer_z,
        float target_x, float target_y, float target_z,
        const std::vector<Particle>& particles,
        const BVH& bvh,
        const std::vector<int>& exclude_ids = {}
    ) const;

    // Smell-based detection (omnidirectional, probabilistic)
    // Checks all particles for odors the creature is attracted to.
    // Returns the strongest/nearest detected odor source.
    //
    // Parameters:
    //   config: Smell sensitivity and which odors to react to
    //   sniffer_x/y/z: Creature's position
    //   discernment: Creature's Discernment (0.5-2.0, affects direction precision)
    //   particles: All particles in the world
    //   exclude_ids: Particle IDs to exclude (self + body parts)
    //
    SmellResult check_smell(
        const SmellConfig& config,
        float sniffer_x, float sniffer_y, float sniffer_z,
        float discernment,
        const std::vector<Particle>& particles,
        const std::vector<int>& exclude_ids = {}
    ) const;

private:
    // Normalize angle to [-PI, PI]
    static float normalize_angle(float angle) {
        while (angle > M_PI) angle -= 2.0f * M_PI;
        while (angle < -M_PI) angle += 2.0f * M_PI;
        return angle;
    }

    // Check if angle is within FOV (accounting for eye damage)
    static bool is_in_fov(float angle_offset, float half_fov,
                          bool left_blind, bool right_blind) {
        if (std::abs(angle_offset) > half_fov) return false;
        if (left_blind && angle_offset < 0) return false;   // Left = negative angles
        if (right_blind && angle_offset > 0) return false;  // Right = positive angles
        return true;
    }
};

#endif // SENSE_SYSTEM_H
