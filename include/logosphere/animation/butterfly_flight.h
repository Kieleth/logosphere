#ifndef LOGOSPHERE_ANIMATION_BUTTERFLY_FLIGHT_H
#define LOGOSPHERE_ANIMATION_BUTTERFLY_FLIGHT_H

// Butterfly flight subsystem.
//
// Owns butterfly-specific animation policy: parse butterfly parts
// from KG, drive sinusoidal wing beats, gliding transitions, vertical
// flight (climb-while-beating, descend-while-gliding), autonomous
// horizontal direction changes.
//
// Mirrors the layout of HumanoidLocomotion / SerpentLocomotion.

#include <memory>
#include <vector>

#include "core/particle_system.h"
#include "logosphere/dynamics/kinematic_root.h"
#include "logosphere/kg/kg_types.h"

class Engine;

namespace logosphere::animation {

struct ButterflyParts {
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    unsigned int head = 0;
    unsigned int thorax = 0;
    unsigned int abdomen = 0;
    unsigned int left_wing = 0;
    unsigned int right_wing = 0;

    std::vector<unsigned int> all_thorax_segments;
    std::vector<unsigned int> all_left_wings;
    std::vector<unsigned int> all_right_wings;

    // Flight behavior parameters from KG.
    float wing_beat_frequency = 8.0f;
    float wing_beat_amplitude = 0.08f;
    float flight_speed = 1.0f;
    float glide_probability = 0.1f;
    float wing_span = 0.125f;

    // Animation state.
    float wing_phase = 0.0f;
    bool is_gliding = false;
    float glide_timer = 0.0f;
    float flight_direction = 0.0f;
    float turn_timer = 0.0f;

    float altitude_velocity = 0.0f;
    // Height above the GROUND, not above zero. There is no floor at
    // zero: the ground is particles, it is layered, and it moves.
    float clearance = 1.0f;          // current height over the surface
    float preferred_clearance = 1.5f; // the band this one likes to hold
    float surface_z = 0.0f;           // last surface found beneath it
    // A sustained turn reads as a circle; a random new heading reads
    // as a flit. Real butterflies do both.
    float turn_rate = 0.0f;
    float circle_timer = 0.0f;
    // Every particle this butterfly owns, so flight can move the whole
    // body in Z as one thing.
    std::vector<unsigned int> all_particles;

    float world_x = 0, world_y = 0, world_z = 0;

    logosphere::KinematicRoot root;
};

class ButterflyFlight {
public:
    ButterflyFlight();
    ~ButterflyFlight();

    ButterflyFlight(const ButterflyFlight&) = delete;
    ButterflyFlight& operator=(const ButterflyFlight&) = delete;

    bool initialize(Engine* engine);
    void shutdown();

    // Per-frame update.
    void update(double delta_time);

    void register_butterfly(kg::EntityID entity_id);
    void unregister_entity(kg::EntityID entity_id);

    void notify_particle_swap(size_t old_idx, size_t new_idx);

    size_t entity_count() const;

private:
    bool parse_butterfly_parts(kg::EntityID entity_id, ButterflyParts& out_parts);
    void update_butterfly_flight(ButterflyParts& parts, double delta_time);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace logosphere::animation

#endif  // LOGOSPHERE_ANIMATION_BUTTERFLY_FLIGHT_H
