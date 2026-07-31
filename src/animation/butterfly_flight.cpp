// Butterfly flight subsystem.

#include "logosphere/animation/butterfly_flight.h"

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace logosphere::animation {

namespace {
// Mirrors ParticleDynamicsSystem::calculate_sinusoidal_offset /
// check_random_probability — small enough to inline rather than call
// across modules through the dynamics handle.
inline float calc_sinusoidal_offset(float phase, float amplitude, float phase_offset) {
    return amplitude * std::sin(phase + phase_offset);
}
inline bool check_random_probability(float chance_per_second, double dt) {
    float threshold = chance_per_second * static_cast<float>(dt);
    return (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) < threshold;
}
}  // namespace

struct ButterflyFlight::Impl {
    Engine* engine = nullptr;
    bool initialized = false;
    std::vector<ButterflyParts> butterfly_entities;
};

ButterflyFlight::ButterflyFlight()
    : impl_(std::make_unique<Impl>()) {}

ButterflyFlight::~ButterflyFlight() = default;

bool ButterflyFlight::initialize(Engine* engine) {
    if (!engine) {
        std::cerr << "[ButterflyFlight] initialize: null engine pointer\n";
        return false;
    }
    impl_->engine = engine;
    impl_->initialized = true;
    std::cout << "[ButterflyFlight] initialized\n";
    return true;
}

void ButterflyFlight::shutdown() {
    if (!impl_->initialized) return;
    impl_->butterfly_entities.clear();
    impl_->engine = nullptr;
    impl_->initialized = false;
}

size_t ButterflyFlight::entity_count() const {
    return impl_->butterfly_entities.size();
}

void ButterflyFlight::register_butterfly(kg::EntityID entity_id) {
    if (!impl_->initialized || !impl_->engine) {
        std::cerr << "[ButterflyFlight] Cannot register: not initialized" << std::endl;
        return;
    }
    ButterflyParts parts;
    if (!parse_butterfly_parts(entity_id, parts)) {
        std::cerr << "[ButterflyFlight] Failed to parse butterfly parts for entity " << entity_id << std::endl;
        return;
    }
    impl_->butterfly_entities.push_back(parts);
    std::cout << "[ButterflyFlight] Registered butterfly " << entity_id << " for flight" << std::endl;
}

void ButterflyFlight::unregister_entity(kg::EntityID entity_id) {
    auto it = std::remove_if(
        impl_->butterfly_entities.begin(),
        impl_->butterfly_entities.end(),
        [entity_id](const ButterflyParts& parts) {
            return parts.entity_id == entity_id;
        }
    );
    impl_->butterfly_entities.erase(it, impl_->butterfly_entities.end());
}

void ButterflyFlight::notify_particle_swap(size_t old_idx, size_t new_idx) {
    auto fix = [&](unsigned int& id) {
        if (id == old_idx) id = static_cast<unsigned int>(new_idx);
    };
    auto fix_vec = [&](std::vector<unsigned int>& v) {
        for (auto& id : v) fix(id);
    };
    for (auto& parts : impl_->butterfly_entities) {
        fix(parts.head);
        fix(parts.thorax);
        fix(parts.abdomen);
        fix(parts.left_wing);
        fix(parts.right_wing);
        fix_vec(parts.all_thorax_segments);
        fix_vec(parts.all_left_wings);
        fix_vec(parts.all_right_wings);
        fix(parts.root.particle_id);
        fix(parts.root.previous_particle_id);
    }
}

void ButterflyFlight::update(double delta_time) {
    if (!impl_->initialized || !impl_->engine) return;
    if (impl_->butterfly_entities.empty()) return;
    for (auto& parts : impl_->butterfly_entities) {
        update_butterfly_flight(parts, delta_time);
    }
}

bool ButterflyFlight::parse_butterfly_parts(kg::EntityID entity_id, ButterflyParts& out_parts) {
    if (!impl_->engine) return false;
    auto& kg_mod = impl_->engine->get_kg();
    auto& ps = impl_->engine->get_particle_system();

    out_parts.entity_id = entity_id;

    auto head_str = kg_mod.getProperty(entity_id, "head_particle");
    auto thorax_str = kg_mod.getProperty(entity_id, "thorax_particle");
    auto abdomen_str = kg_mod.getProperty(entity_id, "abdomen_particle");
    auto left_wing_str = kg_mod.getProperty(entity_id, "left_wing_particle");
    auto right_wing_str = kg_mod.getProperty(entity_id, "right_wing_particle");

    auto frequency_str = kg_mod.getProperty(entity_id, "wing_beat_frequency");
    auto amplitude_str = kg_mod.getProperty(entity_id, "wing_beat_amplitude");
    auto speed_str = kg_mod.getProperty(entity_id, "flight_speed");
    auto glide_prob_str = kg_mod.getProperty(entity_id, "glide_probability");
    auto wing_span_str = kg_mod.getProperty(entity_id, "wing_span");

    if (!head_str.empty()) out_parts.head = std::stoi(head_str);
    if (!thorax_str.empty()) out_parts.thorax = std::stoi(thorax_str);
    if (!abdomen_str.empty()) out_parts.abdomen = std::stoi(abdomen_str);
    if (!left_wing_str.empty()) out_parts.left_wing = std::stoi(left_wing_str);
    if (!right_wing_str.empty()) out_parts.right_wing = std::stoi(right_wing_str);

    for (int i = 0; i < 10; i++) {
        auto thorax_i_str = kg_mod.getProperty(entity_id, "thorax_" + std::to_string(i));
        auto left_wing_i_str = kg_mod.getProperty(entity_id, "left_wing_" + std::to_string(i));
        auto right_wing_i_str = kg_mod.getProperty(entity_id, "right_wing_" + std::to_string(i));
        if (thorax_i_str.empty()) break;
        out_parts.all_thorax_segments.push_back(std::stoi(thorax_i_str));
        if (!left_wing_i_str.empty()) out_parts.all_left_wings.push_back(std::stoi(left_wing_i_str));
        if (!right_wing_i_str.empty()) out_parts.all_right_wings.push_back(std::stoi(right_wing_i_str));
    }

    if (!frequency_str.empty()) out_parts.wing_beat_frequency = std::stof(frequency_str);
    if (!amplitude_str.empty()) out_parts.wing_beat_amplitude = std::stof(amplitude_str);
    if (!speed_str.empty()) out_parts.flight_speed = std::stof(speed_str);
    if (!glide_prob_str.empty()) out_parts.glide_probability = std::stof(glide_prob_str);
    if (!wing_span_str.empty()) out_parts.wing_span = std::stof(wing_span_str);

    if (out_parts.thorax != 0) {
        auto particles_view = ps.lock_particles_for_read();
        const Particle& thorax = particles_view[out_parts.thorax];
        out_parts.world_x = thorax.x;
        out_parts.world_y = thorax.y;
        out_parts.world_z = thorax.z;
        out_parts.base_altitude = thorax.z;
    }

    out_parts.flight_direction = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f * static_cast<float>(M_PI);
    out_parts.turn_timer = 2.0f + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 3.0f;

    if (out_parts.head == 0 || out_parts.thorax == 0 ||
        out_parts.left_wing == 0 || out_parts.right_wing == 0) {
        std::cerr << "[ButterflyFlight] Butterfly missing required particles for entity " << entity_id << std::endl;
        return false;
    }

    std::cout << "[ButterflyFlight] Parsed butterfly " << entity_id
              << " - Segments: " << out_parts.all_thorax_segments.size()
              << ", Wings: " << out_parts.all_left_wings.size() + out_parts.all_right_wings.size()
              << ", Frequency: " << out_parts.wing_beat_frequency << " Hz"
              << ", Flight speed: " << out_parts.flight_speed << " m/s" << std::endl;
    return true;
}

void ButterflyFlight::update_butterfly_flight(ButterflyParts& parts, double delta_time) {
    if (parts.thorax == 0) return;

    auto& ps = impl_->engine->get_particle_system();
    auto particles_view = ps.lock_particles_for_write();

    Particle& thorax = particles_view[parts.thorax];

    parts.world_x = thorax.x;
    parts.world_y = thorax.y;
    parts.world_z = thorax.z;

    if (!parts.is_gliding) {
        if (check_random_probability(parts.glide_probability, delta_time)) {
            parts.is_gliding = true;
            parts.glide_timer = 0.5f + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 1.5f;
        }
    } else {
        parts.glide_timer -= static_cast<float>(delta_time);
        if (parts.glide_timer <= 0.0f) {
            parts.is_gliding = false;
        }
    }

    if (!parts.is_gliding) {
        parts.wing_phase += 2.0f * static_cast<float>(M_PI) * parts.wing_beat_frequency * static_cast<float>(delta_time);
        parts.wing_phase = fmodf(parts.wing_phase, 2.0f * static_cast<float>(M_PI));
    } else {
        parts.wing_phase = static_cast<float>(M_PI) / 2.0f;
    }

    // Wing offset retained for future use (currently unused by Z control,
    // see disabled block below).
    [[maybe_unused]] float wing_offset_z =
        calc_sinusoidal_offset(parts.wing_phase, parts.wing_beat_amplitude, 0.0f);

    const float CLIMB_RATE = 0.3f;
    const float DESCENT_RATE = 0.2f;
    const float ALTITUDE_MIN = 0.1f;
    const float ALTITUDE_MAX = 20.0f;

    if (!parts.is_gliding) {
        parts.altitude_velocity = CLIMB_RATE;
    } else {
        parts.altitude_velocity = -DESCENT_RATE;
    }

    parts.base_altitude += parts.altitude_velocity * static_cast<float>(delta_time);
    if (parts.base_altitude < ALTITUDE_MIN) {
        parts.base_altitude = ALTITUDE_MIN;
        parts.altitude_velocity = 0.0f;
    } else if (parts.base_altitude > ALTITUDE_MAX) {
        parts.base_altitude = ALTITUDE_MAX;
        parts.altitude_velocity = 0.0f;
    }

    // PHYSICS-003: Z control disabled — physics owns Z via gravity/ground.

    parts.turn_timer -= static_cast<float>(delta_time);
    if (parts.turn_timer <= 0.0f) {
        parts.flight_direction = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f * static_cast<float>(M_PI);
        parts.turn_timer = 2.0f + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 3.0f;
    }

    float movement_speed = parts.is_gliding ? parts.flight_speed * 0.5f : parts.flight_speed;
    float dx = cosf(parts.flight_direction) * movement_speed * static_cast<float>(delta_time);
    float dy = sinf(parts.flight_direction) * movement_speed * static_cast<float>(delta_time);

    if (parts.head != 0) {
        Particle& head = particles_view[parts.head];
        head.x += dx;
        head.y += dy;
    }

    for (unsigned int thorax_id : parts.all_thorax_segments) {
        particles_view[thorax_id].x += dx;
        particles_view[thorax_id].y += dy;
    }

    if (parts.abdomen != 0) {
        Particle& abdomen = particles_view[parts.abdomen];
        abdomen.x += dx;
        abdomen.y += dy;
    }

    for (unsigned int left_wing_id : parts.all_left_wings) {
        particles_view[left_wing_id].x += dx;
        particles_view[left_wing_id].y += dy;
    }
    for (unsigned int right_wing_id : parts.all_right_wings) {
        particles_view[right_wing_id].x += dx;
        particles_view[right_wing_id].y += dy;
    }

    if (parts.head != 0) particles_view[parts.head].rotation_z = parts.flight_direction;
    for (unsigned int thorax_id : parts.all_thorax_segments) {
        particles_view[thorax_id].rotation_z = parts.flight_direction;
    }
    if (parts.abdomen != 0) particles_view[parts.abdomen].rotation_z = parts.flight_direction;

    ps.mark_bvh_dirty();
}

}  // namespace logosphere::animation
