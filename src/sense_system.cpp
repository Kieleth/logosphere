// SenseSystem - BVH-based creature perception
//
// Algorithm (ray-based vision cone):
// 1. Cast N rays spread across the FOV angle
// 2. Each ray returns the particle_id of what it hits (or -1)
// 3. Collect unique particle_ids from all hits
// 4. Build SenseTarget entries for each visible entity
// 5. Sort by distance (nearest first)

#include "sense_system.h"
#include "logosphere/physics/bvh.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <limits>
#include <iostream>
#include <cstdlib>  // For rand()

SenseResult SenseSystem::cast_vision_cone(
    const SenseConfig& config,
    float viewer_x, float viewer_y, float viewer_z,
    float facing_angle,
    const std::vector<Particle>& particles,
    const BVH& bvh,
    const std::vector<int>& target_ids,
    const std::vector<int>& exclude_ids
) const {
    SenseResult result;

    // Effective range accounting for vision quality
    float effective_range = config.vision_range * config.vision_quality;
    float half_fov = (config.fov_degrees * 0.5f) * (M_PI / 180.0f);  // radians

    // Completely blind?
    if (config.vision_quality <= 0.0f ||
        (config.left_eye_blind && config.right_eye_blind)) {
        return result;  // Empty - can't see anything
    }

    // Adjust FOV for eye damage
    float start_angle = -half_fov;
    float end_angle = half_fov;
    if (config.left_eye_blind) start_angle = 0.0f;   // Can only see right half
    if (config.right_eye_blind) end_angle = 0.0f;    // Can only see left half

    int ray_count = config.ray_count;
    if (ray_count < 1) ray_count = 8;

    // Debug logging (every N frames)
    static int sense_debug_frame = 0;
    bool verbose = (sense_debug_frame++ % 60 == 0);

    // Collect unique particle hits
    std::set<int> hit_particles;

    // Cast rays across the FOV at multiple vertical angles
    // pitch_angles: 0 = horizontal, negative = looking down
    // More pitch angles ensure small ground objects (z=0.1-0.3m) are detected
    // at typical detection ranges (3-6m from eye height ~1.7m)
    float pitch_angles[] = {0.0f, -0.15f, -0.25f, -0.37f, -0.5f, -0.65f};
    //                      0°,   -9°,    -14°,   -21°,   -29°,  -37° down
    // -21° pitch hits z≈0.15m at 4m distance from 1.7m eye height
    int num_pitches = 6;

    for (int i = 0; i < ray_count; ++i) {
        // Spread rays evenly across horizontal FOV
        float t = (ray_count == 1) ? 0.5f : static_cast<float>(i) / (ray_count - 1);
        float ray_angle = facing_angle + start_angle + t * (end_angle - start_angle);

        // Cast rays at multiple pitch angles to detect ground objects
        for (int p = 0; p < num_pitches; ++p) {
            float pitch = pitch_angles[p];

            // Direction vector with pitch
            // facing_angle: 0 = +Y (north), PI/2 = +X (east)
            float cos_pitch = std::cos(pitch);
            float dir_x = std::sin(ray_angle) * cos_pitch;
            float dir_y = std::cos(ray_angle) * cos_pitch;
            float dir_z = std::sin(pitch);  // Negative = downward

            // Cast ray and get what it hits
            int hit_pid = bvh.trace_ray_hit(
                viewer_x, viewer_y, viewer_z,
                dir_x, dir_y, dir_z,
                effective_range,
                particles,
                exclude_ids
            );

            if (hit_pid >= 0) {
                hit_particles.insert(hit_pid);

                if (verbose && p == 0) {  // Only log horizontal rays to reduce spam
                    std::cout << "[SENSE] Ray " << i << " angle=" << ray_angle
                              << " hit particle_id=" << hit_pid << std::endl;
                }
            }
        }
    }

    // If we have specific target_ids, filter hits to only those
    if (!target_ids.empty()) {
        std::set<int> target_set(target_ids.begin(), target_ids.end());
        std::set<int> filtered;
        for (int pid : hit_particles) {
            if (target_set.count(pid) > 0) {
                filtered.insert(pid);
            }
        }
        hit_particles = filtered;
    }

    // Build SenseTarget for each unique hit
    for (int pid : hit_particles) {
        if (pid < 0 || pid >= static_cast<int>(particles.size())) continue;

        const Particle& p = particles[pid];

        float dx = p.x - viewer_x;
        float dy = p.y - viewer_y;
        float dz = p.z - viewer_z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        float angle_to_target = std::atan2(dx, dy);
        float angle_offset = normalize_angle(angle_to_target - facing_angle);

        SenseTarget target;
        target.particle_id = pid;
        target.x = p.x;
        target.y = p.y;
        target.z = p.z;
        target.distance = dist;
        target.angle_offset = angle_offset;
        result.visible_targets.push_back(target);

        if (verbose) {
            std::cout << "[SENSE] VISIBLE: pid=" << pid
                      << " dist=" << dist << std::endl;
        }
    }

    // Sort by distance (nearest first)
    std::sort(result.visible_targets.begin(), result.visible_targets.end(),
        [](const SenseTarget& a, const SenseTarget& b) {
            return a.distance < b.distance;
        });

    return result;
}

bool SenseSystem::can_see_point(
    float viewer_x, float viewer_y, float viewer_z,
    float target_x, float target_y, float target_z,
    const std::vector<Particle>& particles,
    const BVH& bvh,
    const std::vector<int>& exclude_ids
) const {
    // Simple point-to-point visibility check (no FOV constraints)
    // Used for PROBE state during obstacle avoidance
    // Use trace_shadow_ray_exclude to skip ALL body particles
    bool blocked = bvh.trace_shadow_ray_exclude(
        viewer_x, viewer_y, viewer_z,
        target_x, target_y, target_z,
        particles,
        exclude_ids
    );
    return !blocked;  // can_see = not blocked
}

SmellResult SenseSystem::check_smell(
    const SmellConfig& config,
    float sniffer_x, float sniffer_y, float sniffer_z,
    float discernment,
    const std::vector<Particle>& particles,
    const std::vector<int>& exclude_ids
) const {
    // Smell Detection Algorithm:
    // 1. Loop through all particles looking for odor emitters
    // 2. Check if odor type is in creature's attracted_to list
    // 3. Calculate detection probability based on distance and state
    // 4. Roll for detection, track strongest/nearest source
    // 5. Apply direction precision error based on reflexes

    SmellResult result;

    // Build exclude set for O(1) lookup
    std::set<int> exclude_set(exclude_ids.begin(), exclude_ids.end());

    // Track best candidate (nearest detected odor)
    float best_distance = std::numeric_limits<float>::max();
    int best_particle_id = -1;
    OdorType best_type = OdorType::NONE;
    float best_direction = 0.0f;

    // Debug logging (every N frames)
    static int smell_debug_frame = 0;
    bool verbose = (smell_debug_frame++ % 60 == 0);

    for (size_t i = 0; i < particles.size(); ++i) {
        int pid = static_cast<int>(i);

        // Skip excluded particles (self, body parts)
        if (exclude_set.count(pid) > 0) continue;

        const Particle& p = particles[i];

        // Skip particles with no odor
        if (p.odor_type == OdorType::NONE || p.odor_radius <= 0.0f) continue;

        // Check if creature is attracted to this odor type
        bool attracted = false;
        for (OdorType type : config.attracted_to) {
            if (p.odor_type == type) {
                attracted = true;
                break;
            }
        }
        if (!attracted) continue;

        // Calculate distance
        float dx = p.x - sniffer_x;
        float dy = p.y - sniffer_y;
        float dz = p.z - sniffer_z;
        float distance = std::sqrt(dx*dx + dy*dy + dz*dz);

        // Outside odor radius?
        if (distance > p.odor_radius) continue;

        // Calculate detection probability
        // P(smell) = intensity × (1 - (dist/radius)²) × state_factor × sensitivity
        float dist_ratio = distance / p.odor_radius;
        float distance_factor = 1.0f - (dist_ratio * dist_ratio);  // Quadratic falloff
        float probability = p.odor_intensity * distance_factor * config.state_factor * config.sensitivity;

        // Clamp probability to [0, 1]
        probability = std::max(0.0f, std::min(1.0f, probability));

        // Roll for detection (simple random)
        float roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);

        if (verbose) {
            std::cout << "[SMELL] pid=" << pid << " type=" << static_cast<int>(p.odor_type)
                      << " dist=" << distance << "/" << p.odor_radius
                      << " prob=" << probability << " roll=" << roll << std::endl;
        }

        if (roll > probability) continue;  // Failed detection roll

        // Detection successful! Track if it's the nearest
        if (distance < best_distance) {
            best_distance = distance;
            best_particle_id = pid;
            best_type = p.odor_type;

            // Calculate direction to source (0 = +Y/north)
            best_direction = std::atan2(dx, dy);
        }
    }

    // If we detected something, build result
    if (best_particle_id >= 0) {
        result.detected = true;
        result.source_particle_id = best_particle_id;
        result.type = best_type;
        result.distance = best_distance;
        result.direction = best_direction;

        // Direction precision based on FORGED Discernment
        // Base error ±22.5° at Discernment 1.0 (average)
        // Formula: base_error / discernment
        // Discernment 0.5 → ±45° (impaired, hemisphere)
        // Discernment 1.0 → ±22.5° (average, quadrant)
        // Discernment 2.0 → ±11.25° (exceptional, precise)
        float base_error_degrees = 22.5f;
        float clamped_discernment = std::max(0.3f, std::min(2.5f, discernment));
        float error_degrees = base_error_degrees / clamped_discernment;

        float error_radians = error_degrees * (M_PI / 180.0f);
        result.direction_error = error_radians;

        // Apply random error to perceived direction
        float random_error = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * error_radians;
        result.direction = normalize_angle(result.direction + random_error);

        if (verbose) {
            std::cout << "[SMELL] DETECTED! pid=" << best_particle_id
                      << " type=" << static_cast<int>(best_type)
                      << " dist=" << best_distance
                      << " dir=" << result.direction << " (±" << error_degrees << "° @ discernment=" << discernment << ")" << std::endl;
        }
    }

    return result;
}
