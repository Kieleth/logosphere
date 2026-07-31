// Logosphere - PlayerController
//
// Handles player-specific input: mouse look, WASD movement, abilities.
// Separates player control from ParticleDynamicsSystem which handles
// generic humanoid physics for both players and NPCs.
//
// Usage:
//   auto& pc = engine.get_player_controller();
//   pc.set_controlled_entity(player_entity, hips_id);
//   pc.on_punch = [&]() { engine.get_humanoid_locomotion().play_fk_animation(hips, "punch_r"); };

#pragma once

#include "logosphere/kg/kg_types.h"
#include <functional>

class Engine;

class PlayerController {
public:
    PlayerController();

    // Initialize with engine reference (called by Engine::initialize)
    void initialize(Engine* engine);

    // Set the controlled entity (call once at setup)
    void set_controlled_entity(kg::EntityID entity_id, int hips_id);
    void clear_controlled_entity();

    // Called each frame by Engine::update()
    void update(float dt);

    // Configuration: override speeds (0 = use dynamics system derived speeds)
    void set_walk_speed(float speed) { walk_speed_override_ = speed; }
    void set_run_speed(float speed) { run_speed_override_ = speed; }
    void set_hand_particle(size_t hand_id) { hand_id_ = hand_id; }

    // Ability callbacks
    std::function<void()> on_punch;

    // Query state
    kg::EntityID get_controlled_entity() const { return entity_id_; }
    int get_hips_id() const { return hips_id_; }
    bool has_controlled_entity() const { return entity_id_ != kg::INVALID_ENTITY; }

private:
    Engine* engine_;
    kg::EntityID entity_id_ = kg::INVALID_ENTITY;
    int hips_id_ = -1;
    size_t hand_id_ = 0;

    // Speed overrides (0 = read from dynamics system limits)
    float walk_speed_override_ = 0.0f;
    float run_speed_override_ = 0.0f;

    // Movement modifiers (body-relative)
    static constexpr float STRAFE_MULT = 0.7f;
    static constexpr float BACKWARD_MULT = 0.6f;

    // Input state tracking for edge detection
    bool space_was_pressed_ = false;

    // Internal update functions
    void update_mouse_look();   // Mouse -> body rotation via set_look_at_target
    void update_movement();     // WASD -> body-relative velocity via dynamics
    void update_abilities();    // SPACE -> punch callback
};
