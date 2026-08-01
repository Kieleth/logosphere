// ThresholdApp: a side-view platformer built on Logosphere's real physics
// and animation systems -- HumanoidLocomotion gait, real gravity, a real
// jump, a particle-based level with a collapsing floor, and a
// sword-fighting guard driven by FK animation. See examples/threshold's
// design notes (plan) for why this exists alongside examples/pop, which
// deliberately avoids all of this.
//
// The "side view" comes from camera placement, not a different physics
// plane: the engine's native axes are untouched (X horizontal, Y depth,
// Z up). The player moves along X and jumps along Z; Y is held fixed
// (level.h's kFixedDepthY) and the camera looks straight down the Y axis.
#pragma once

#include "application.h"
#include "logosphere/damage/damage_system.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_types.h"

#include <cstdint>
#include <optional>

class Engine;

namespace threshold {

class ThresholdApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }

    void initialize_game(void* engine_ptr) override;
    void update_game(float dt) override;

    const char* get_app_name() const override { return "Threshold"; }

private:
    void setup_camera();
    void update_input(float dt);
    void update_camera();
    void update_loose_tile();
    void update_hazards();
    void update_guard();
    void update_sword_prop();
    void update_exit();

    Engine* engine_ = nullptr;

    kg::EntityID prince_entity_ = kg::INVALID_ENTITY;
    int prince_hips_id_ = -1;
    bool jump_was_pressed_ = false;   // edge-detect: try_jump() fires once per press, not per held frame

    int loose_tile_id_ = -1;
    bool loose_tile_woken_ = false;

    kg::EntityID guard_entity_ = kg::INVALID_ENTITY;
    int guard_hips_id_ = -1;
    int guard_right_hand_id_ = -1;
    int guard_right_forearm_id_ = -1;
    bool swing_hit_landed_ = false;   // "already hit this swing" -- one hit per clip play

    int sword_prop_id_ = -1;

    bool reached_exit_ = false;

    // Spikes: no other entity in this level can trigger the spike
    // profile's volume events, so any entered==true event for
    // spike_profile_id_ is read as "the Prince hit the spikes" -- see
    // scene.h's spawn_spikes and update_hazards.
    uint32_t spike_profile_id_ = 0;
    // Constructed in initialize_game() -- EventReader binds to the
    // engine's event log at read time, and the engine doesn't exist yet
    // when ThresholdApp itself is constructed.
    std::optional<logosphere::EventReader<logosphere::onto::VolumeEvent>> spike_reader_;
    DamageSystem damage_system_;
};

} // namespace threshold
