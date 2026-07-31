#ifndef LOGOSPHERE_ANIMATION_SERPENT_LOCOMOTION_H
#define LOGOSPHERE_ANIMATION_SERPENT_LOCOMOTION_H

// Serpent locomotion subsystem.
//
// Owns the serpent autonomy + kinematic gait: parse serpent parts from
// the KG, drive the head with sinusoidal wandering plus follow-the-
// leader body chain. Lives outside src/core/ so dynamics stays
// generic.
//
// Mirrors the layout of HumanoidLocomotion: per-instance Impl in the
// cpp, friend-accessed dynamics state for the owning container, an
// `update_pre_physics` slot Engine drives each frame.

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/particle_system.h"
#include "logosphere/dynamics/kinematic_root.h"
#include "logosphere/kg/kg_types.h"

class Engine;
class ParticleDynamicsSystem;

namespace logosphere::animation {

struct SerpentParts {
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    unsigned int head = 0;
    std::vector<unsigned int> body_segments;  // head → tail order
    unsigned int tail = 0;

    // Position history kept for potential future use (currently unused).
    static const int HISTORY_SIZE = 100;
    std::vector<std::pair<float, float>> position_history;
    int history_write_index = 0;

    // Movement state.
    float world_x = 0, world_y = 0, world_z = 0;
    float head_rotation = 0.0f;
    float movement_phase = 0.0f;

    // Autonomous wandering.
    float wander_direction = 0.0f;
    float turn_timer = 0.0f;
    float wander_speed = 0.5f;

    // Distance-based serpentine oscillation (frame-rate independent).
    float distance_since_turn = 0.0f;
    float current_turn_target = 0.0f;
    float turn_distance = 0.35f;

    float segment_spacing = 0.1f;

    logosphere::KinematicRoot root;
};

class SerpentLocomotion;

// Engine adapter — defined in serpent_locomotion_engine.cpp inside the
// full logosphere library. Wires Engine's RenderSystem + InputSystem
// into the SerpentLocomotion mouse-control hooks. Headless tests skip
// this call.
void serpent_install_engine_input_hooks(SerpentLocomotion& serpent, Engine* engine);

class SerpentLocomotion {
public:
    SerpentLocomotion();
    ~SerpentLocomotion();

    SerpentLocomotion(const SerpentLocomotion&) = delete;
    SerpentLocomotion& operator=(const SerpentLocomotion&) = delete;

    bool initialize(Engine* engine);

    // Install mouse-control hooks. Engine's adapter (in
    // serpent_locomotion_engine.cpp, linked into the full logosphere
    // library) provides them; headless tests leave them unset and
    // fall back to autonomous wandering.
    void set_input_hooks(
        std::function<void(int, int, float, float&, float&)> screen_to_world,
        std::function<void(float&, float&)> mouse_pos);
    void shutdown();

    // Per-frame update: run autonomy + advance serpent movement.
    // Engine calls this after dynamics's update() so input target +
    // physics state are current.
    void update(double delta_time, kg::EntityID input_target);

    // Register / unregister.
    void register_serpent(kg::EntityID entity_id);
    void unregister_entity(kg::EntityID entity_id);

    // Particle-swap notification — chunk streaming may move particle
    // indices; serpent caches them so it must remap.
    void notify_particle_swap(size_t old_idx, size_t new_idx);

    // Container size for dynamics's metrics.
    size_t entity_count() const;

private:
    bool parse_serpent_parts(kg::EntityID entity_id, SerpentParts& out_parts);
    void update_serpent_movement(SerpentParts& parts, double delta_time,
                                 kg::EntityID input_target);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace logosphere::animation

#endif  // LOGOSPHERE_ANIMATION_SERPENT_LOCOMOTION_H
