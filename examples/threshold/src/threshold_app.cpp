#include "threshold_app.h"

#include "level.h"
#include "scene.h"

#include "core/engine.h"
#include "core/input_system.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles
#include "projection_system.h"

#include <cmath>
#include <iostream>

namespace threshold {

void ThresholdApp::initialize_game(void* engine_ptr) {
    engine_ = static_cast<Engine*>(engine_ptr);

    engine_->create_sun();
    build_level(*engine_);
    loose_tile_id_ = spawn_loose_tile(*engine_);
    spike_profile_id_ = spawn_spikes(*engine_);
    spike_reader_.emplace(engine_->get_event_bus().volume().create_reader());

    auto prince = spawn_humanoid(*engine_, kPrinceStartX);
    prince_entity_ = prince.entity_id;
    prince_hips_id_ = prince.hips_id;
    std::cout << "[threshold] Prince spawned at x=" << kPrinceStartX
              << " hips_id=" << prince_hips_id_ << "\n";

    damage_system_.set_event_bus(&engine_->get_event_bus());
    damage_system_.set_kg(&engine_->get_kg());
    damage_system_.register_entity(prince_entity_, 100.0f);
    damage_system_.on_death = [](kg::EntityID, DamageType) {
        std::cout << "[threshold] the Prince has fallen\n";
    };

    // The guard: physics-driven and registered with HumanoidLocomotion
    // exactly like the Prince (so it gets the same auto-registered
    // walk/idle/punch/kick FK clips for free), but held in a ready
    // on-guard stance rather than left in the default arms-at-sides
    // idle. It's immune to the spikes by construction, not by profile
    // masking: the spike hazard sits at kGapMinX..kGapMaxX and the
    // guard spawns at kGuardStartX, well past it, so their volumes
    // never overlap (see update_hazards' single-player VolumeEvent
    // assumption in spawn_spikes).
    auto guard = spawn_humanoid(*engine_, kGuardStartX);
    guard_entity_ = guard.entity_id;
    guard_hips_id_ = guard.hips_id;
    guard_right_hand_id_ = guard.right_hand_id;
    guard_right_forearm_id_ = guard.right_forearm_id;
    engine_->get_humanoid_locomotion().register_fk_animation(
        guard_hips_id_, "guard_pose", create_fk_guard_pose_right());
    engine_->get_humanoid_locomotion().register_fk_animation(
        guard_hips_id_, "sword_swing", create_fk_sword_swing_right(0.5f));
    engine_->get_humanoid_locomotion().play_fk_animation(guard_hips_id_, "guard_pose");
    std::cout << "[threshold] guard spawned at x=" << kGuardStartX
              << " hips_id=" << guard_hips_id_ << "\n";

    if (guard_right_hand_id_ >= 0) {
        sword_prop_id_ = spawn_sword(*engine_);
    }

    setup_camera();
}

void ThresholdApp::setup_camera() {
    auto& cam = engine_->get_camera_system();
    auto projection = ProjectionFactory::create(ProjectionFactory::Type::Cabinet);
    projection->configure(/*angle_degrees=*/0.0f, /*depth_ratio=*/0.0f, 0.0f);
    cam.set_projection_system(std::move(projection));
    cam.set_pixels_per_unit(32.0f);
    update_camera();
}

void ThresholdApp::update_camera() {
    if (prince_hips_id_ < 0) return;
    auto view = engine_->get_particle_system().lock_particles_for_read();
    const float prince_x = view[prince_hips_id_].x;
    auto& cam = engine_->get_camera_system();
    cam.set_position(prince_x, kFixedDepthY - 15.0f, 1.6f);
    cam.look_at(prince_x, kFixedDepthY, 1.0f);
}

void ThresholdApp::update_input(float /*dt*/) {
    if (prince_hips_id_ < 0) return;

    auto& input = engine_->get_input_system();
    const auto& state = input.get_input_state();
    const bool left = state.keys[GLFW_KEY_A];
    const bool right = state.keys[GLFW_KEY_D];

    float local_x = 0.0f;
    if (right) local_x += 1.0f;
    if (left) local_x -= 1.0f;

    auto& loco = engine_->get_humanoid_locomotion();
    const float walk_speed = loco.get_max_walk_speed(prince_hips_id_);
    loco.set_target_velocity(prince_hips_id_, local_x * walk_speed, 0.0f);

    const bool jump_pressed = state.keys[GLFW_KEY_SPACE];
    if (jump_pressed && !jump_was_pressed_) {
        const bool jumped = loco.try_jump(prince_hips_id_, 1.2f);
        std::cout << "[threshold] jump " << (jumped ? "OK" : "ignored (not grounded)") << "\n";
    }
    jump_was_pressed_ = jump_pressed;
}

void ThresholdApp::update_loose_tile() {
    if (loose_tile_woken_ || loose_tile_id_ < 0 || prince_hips_id_ < 0) return;

    auto view = engine_->get_particle_system().lock_particles_for_read();
    const float prince_x = view[prince_hips_id_].x;
    // Wake it a little before the Prince actually reaches it -- the tile
    // needs a few frames to visibly start falling before he's standing
    // on it, matching how the loose tile is supposed to read: cross it
    // fast (unhurt/full speed) and you're past before it lets go.
    if (prince_x < kLooseTileX - 0.5f) return;

    engine_->get_physics_system().wake_particle(static_cast<size_t>(loose_tile_id_));
    loose_tile_woken_ = true;
    std::cout << "[threshold] loose tile woken\n";
}

void ThresholdApp::update_hazards() {
    if (!spike_reader_ || prince_entity_ == kg::INVALID_ENTITY) return;

    for (const auto& ev : spike_reader_->drain()) {
        if (!ev.entered.value_or(false)) continue;   // only care about entry, not exit
        if (ev.medium_profile.value_or(0) != static_cast<int32_t>(spike_profile_id_)) continue;

        // VolumeEvent doesn't carry which particle triggered it -- this
        // level has exactly one player and one hazard profile, so any
        // entry event on the spike profile can only be the Prince (see
        // scene.h's spawn_spikes).
        constexpr float kSpikeDamage = 25.0f;
        damage_system_.apply_to_body_part(prince_entity_, "left_leg", kSpikeDamage, DamageType::Pierce);
        std::cout << "[threshold] the Prince hit the spikes, hp=" << damage_system_.get_hp(prince_entity_) << "\n";
    }
}

void ThresholdApp::update_guard() {
    if (guard_hips_id_ < 0 || prince_hips_id_ < 0) return;

    auto& loco = engine_->get_humanoid_locomotion();
    float dx;
    {
        auto view = engine_->get_particle_system().lock_particles_for_read();
        dx = std::abs(view[prince_hips_id_].x - view[guard_hips_id_].x);
    }

    // Sword's reach -- the same shape as pop's in_sword_range, just driven
    // off real FK animation state instead of a tick counter.
    constexpr float kSwordRange = 2.0f;
    const bool in_range = dx < kSwordRange;
    const bool swinging = loco.is_fk_animation_playing(guard_hips_id_);

    if (in_range && !swinging) {
        loco.play_fk_animation(guard_hips_id_, "sword_swing");
        swing_hit_landed_ = false;
        return;
    }

    // One hit per swing: only while the clip is playing, only once, and
    // only if the Prince is still in range when it lands (walking away
    // mid-swing dodges it).
    if (swinging && !swing_hit_landed_ && in_range) {
        constexpr float kSwordDamage = 20.0f;
        damage_system_.apply_to_body_part(prince_entity_, "torso", kSwordDamage, DamageType::Slash);
        swing_hit_landed_ = true;
        std::cout << "[threshold] the guard's blade lands, prince hp="
                  << damage_system_.get_hp(prince_entity_) << "\n";
    }
}

void ThresholdApp::update_sword_prop() {
    if (sword_prop_id_ < 0 || guard_right_hand_id_ < 0 || guard_right_forearm_id_ < 0) return;

    auto view = engine_->get_particle_system().lock_particles_for_write();
    const Particle& hand = view[guard_right_hand_id_];
    const Particle& forearm = view[guard_right_forearm_id_];

    const float dx = hand.x - forearm.x;
    const float dz = hand.z - forearm.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) return;   // forearm/hand coincident this frame -- skip rather than divide by zero
    const float ux = dx / len, uz = dz / len;

    // The grip is at the hand; the blade extends half its length beyond
    // it, along the forearm->hand direction.
    constexpr float kBladeLength = 0.6f;
    Particle& sword = view[sword_prop_id_];
    sword.x = hand.x + ux * (kBladeLength * 0.5f);
    sword.y = hand.y;
    sword.z = hand.z + uz * (kBladeLength * 0.5f);
    // rotation_y rotates local (x,0,z) to (x*cos+z*sin, -x*sin+z*cos)
    // (particle_geometry_v2.cpp) -- the blade's local +X tip needs to
    // land on (ux, uz), so solve cos=ux, -sin=uz for the angle.
    sword.rotation_y = std::atan2(-uz, ux);
}

void ThresholdApp::update_exit() {
    if (reached_exit_ || prince_hips_id_ < 0) return;

    float prince_x;
    {
        auto view = engine_->get_particle_system().lock_particles_for_read();
        prince_x = view[prince_hips_id_].x;
    }
    if (prince_x < kExitX) return;

    reached_exit_ = true;
    std::cout << "[threshold] the Prince reaches the threshold -- hp="
              << damage_system_.get_hp(prince_entity_) << "\n";
}

void ThresholdApp::update_game(float dt) {
    if (!engine_) return;
    update_input(dt);
    update_camera();
    update_loose_tile();
    update_hazards();
    update_guard();
    update_sword_prop();
    update_exit();
}

} // namespace threshold
