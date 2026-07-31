// Serpent locomotion subsystem — see header for design intent.
//
// Owns parsing + autonomy + per-frame movement. Data lives inside Impl;
// dynamics calls update() each frame (via Engine wiring) and the
// register/unregister/notify_swap accessors for lifecycle.

#include "logosphere/animation/serpent_locomotion.h"

#include "core/engine.h"
#include "core/input_system.h"
#include "core/particle_system.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>

namespace logosphere::animation {

struct SerpentLocomotion::Impl {
    Engine* engine = nullptr;
    bool initialized = false;
    std::vector<SerpentParts> serpent_entities;

    // Mouse-control hooks. Engine wires these in initialize(); headless
    // tests leave them null (serpent then only wanders autonomously).
    // Decouples SerpentLocomotion from RenderSystem + InputSystem at link
    // time so logosphere_dynamics doesn't drag rendering into headless
    // builds.
    std::function<void(int, int, float, float&, float&)> screen_to_world;
    std::function<void(float&, float&)> mouse_pos;
    ParticleSystem* ps = nullptr;
};

SerpentLocomotion::SerpentLocomotion()
    : impl_(std::make_unique<Impl>()) {}

SerpentLocomotion::~SerpentLocomotion() = default;

bool SerpentLocomotion::initialize(Engine* engine) {
    if (!engine) {
        std::cerr << "[SerpentLocomotion] initialize: null engine pointer\n";
        return false;
    }
    impl_->engine = engine;
    impl_->ps = &engine->get_particle_system();
    impl_->initialized = true;
    // Mouse-control hooks (screen_to_world + mouse_pos) installed by
    // serpent_locomotion_engine.cpp via set_input_hooks(). Keeps
    // RenderSystem + InputSystem references out of this translation
    // unit so logosphere_dynamics has no rendering link-time dep.
    std::cout << "[SerpentLocomotion] initialized\n";
    return true;
}

void SerpentLocomotion::set_input_hooks(
        std::function<void(int, int, float, float&, float&)> screen_to_world,
        std::function<void(float&, float&)> mouse_pos) {
    impl_->screen_to_world = std::move(screen_to_world);
    impl_->mouse_pos = std::move(mouse_pos);
}

void SerpentLocomotion::shutdown() {
    if (!impl_->initialized) return;
    impl_->serpent_entities.clear();
    impl_->engine = nullptr;
    impl_->initialized = false;
}

size_t SerpentLocomotion::entity_count() const {
    return impl_->serpent_entities.size();
}

void SerpentLocomotion::register_serpent(kg::EntityID entity_id) {
    if (!impl_->initialized || !impl_->engine) {
        std::cerr << "[SerpentLocomotion] Cannot register: not initialized" << std::endl;
        return;
    }
    SerpentParts parts;
    if (!parse_serpent_parts(entity_id, parts)) {
        std::cerr << "[SerpentLocomotion] Failed to parse serpent parts for entity " << entity_id << std::endl;
        return;
    }
    impl_->serpent_entities.push_back(parts);
    std::cout << "[SerpentLocomotion] Registered serpent " << entity_id << " for movement" << std::endl;
}

void SerpentLocomotion::unregister_entity(kg::EntityID entity_id) {
    auto it = std::remove_if(
        impl_->serpent_entities.begin(),
        impl_->serpent_entities.end(),
        [entity_id](const SerpentParts& parts) {
            return parts.entity_id == entity_id;
        }
    );
    impl_->serpent_entities.erase(it, impl_->serpent_entities.end());
}

void SerpentLocomotion::notify_particle_swap(size_t old_idx, size_t new_idx) {
    auto fix = [&](unsigned int& id) {
        if (id == old_idx) id = static_cast<unsigned int>(new_idx);
    };
    for (auto& parts : impl_->serpent_entities) {
        fix(parts.head);
        fix(parts.tail);
        for (auto& seg : parts.body_segments) fix(seg);
    }
}

void SerpentLocomotion::update(double delta_time, kg::EntityID input_target) {
    if (!impl_->initialized || !impl_->engine) return;
    if (impl_->serpent_entities.empty()) return;
    for (auto& parts : impl_->serpent_entities) {
        update_serpent_movement(parts, delta_time, input_target);
    }
}

bool SerpentLocomotion::parse_serpent_parts(kg::EntityID entity_id, SerpentParts& out_parts) {
    if (!impl_->engine) return false;
    auto& kg_mod = impl_->engine->get_kg();
    auto& ps = impl_->engine->get_particle_system();

    out_parts.entity_id = entity_id;

    auto head_str = kg_mod.getProperty(entity_id, "head_particle");
    auto segments_str = kg_mod.getProperty(entity_id, "body_segments");
    auto spacing_str = kg_mod.getProperty(entity_id, "segment_spacing");

    if (!head_str.empty()) out_parts.head = std::stoi(head_str);
    if (!spacing_str.empty()) out_parts.segment_spacing = std::stof(spacing_str);

    if (!segments_str.empty()) {
        std::stringstream ss(segments_str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                out_parts.body_segments.push_back(std::stoi(token));
            }
        }
    }

    if (!out_parts.body_segments.empty()) {
        out_parts.tail = out_parts.body_segments.back();
    }

    if (out_parts.head != 0) {
        auto particles_view = ps.lock_particles_for_read();
        const Particle& head = particles_view[out_parts.head];
        out_parts.world_x = head.x;
        out_parts.world_y = head.y;
        out_parts.world_z = head.z;
    }

    out_parts.wander_direction = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f * static_cast<float>(M_PI);
    out_parts.turn_timer = 2.0f + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 3.0f;
    out_parts.wander_speed = 0.5f;

    out_parts.distance_since_turn = 0.0f;
    float initial_turn = 0.52f + (static_cast<float>(rand()) / RAND_MAX) * 0.53f;
    out_parts.current_turn_target = (rand() % 2 == 0) ? initial_turn : -initial_turn;
    out_parts.turn_distance = 0.35f;

    out_parts.position_history.clear();
    out_parts.position_history.reserve(SerpentParts::HISTORY_SIZE);

    float init_dir = out_parts.wander_direction;
    float dir_x = cosf(init_dir);
    float dir_y = sinf(init_dir);
    for (int i = 0; i < SerpentParts::HISTORY_SIZE; i++) {
        float offset = i * (out_parts.segment_spacing / 10.0f);
        float trail_x = out_parts.world_x - dir_x * offset;
        float trail_y = out_parts.world_y - dir_y * offset;
        out_parts.position_history.push_back(std::make_pair(trail_x, trail_y));
    }
    out_parts.history_write_index = 0;

    if (out_parts.head == 0) {
        std::cerr << "[SerpentLocomotion] No head particle found for serpent entity " << entity_id << std::endl;
        return false;
    }

    std::cout << "[SerpentLocomotion] Parsed serpent " << entity_id
              << " - Head: " << out_parts.head
              << ", Segments: " << out_parts.body_segments.size()
              << ", Spacing: " << out_parts.segment_spacing << std::endl;

    return true;
}

void SerpentLocomotion::update_serpent_movement(SerpentParts& parts, double delta_time,
                                                 kg::EntityID input_target) {
    if (parts.head == 0) return;

    auto& ps = impl_->engine->get_particle_system();
    auto particles_view = ps.lock_particles_for_write();

    Particle& head = particles_view[parts.head];

    // Mouse-control path requires the screen→world callback (Engine
    // wires this in production; headless tests leave it null). When
    // either the callback or the input lookup is missing, the serpent
    // falls back to autonomous wandering — same as if no input target
    // were set.
    bool is_controlled = (input_target == parts.entity_id)
                         && impl_->screen_to_world
                         && impl_->mouse_pos;

    if (is_controlled) {
        float mx_screen = 0.0f, my_screen = 0.0f;
        impl_->mouse_pos(mx_screen, my_screen);

        float mouse_world_x = 0.0f, mouse_world_y = 0.0f;
        impl_->screen_to_world(static_cast<int>(mx_screen),
                               static_cast<int>(my_screen),
                               head.z, mouse_world_x, mouse_world_y);

        float dx = mouse_world_x - head.x;
        float dy = mouse_world_y - head.y;
        parts.head_rotation = atan2f(dy, dx);
    } else {
        parts.turn_timer -= static_cast<float>(delta_time);
        if (parts.turn_timer <= 0.0f) {
            parts.wander_direction = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f * static_cast<float>(M_PI);
            parts.turn_timer = 2.0f + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 3.0f;
        }

        float distance_traveled = parts.wander_speed * static_cast<float>(delta_time);
        parts.distance_since_turn += distance_traveled;

        if (parts.distance_since_turn >= parts.turn_distance) {
            float min_turn = 0.52f;
            float max_turn = 1.05f;
            float random_turn = min_turn + (static_cast<float>(rand()) / RAND_MAX) * (max_turn - min_turn);
            parts.current_turn_target = (parts.current_turn_target > 0) ? -random_turn : random_turn;
            parts.distance_since_turn = 0.0f;
        }

        float target_heading = parts.wander_direction + parts.current_turn_target;
        float heading_diff = target_heading - parts.head_rotation;
        while (heading_diff > static_cast<float>(M_PI)) heading_diff -= 2.0f * static_cast<float>(M_PI);
        while (heading_diff < -static_cast<float>(M_PI)) heading_diff += 2.0f * static_cast<float>(M_PI);

        const float ROTATION_SPEED = 4.0f;
        float max_rotation = ROTATION_SPEED * static_cast<float>(delta_time);
        if (fabsf(heading_diff) > max_rotation) {
            parts.head_rotation += (heading_diff > 0 ? max_rotation : -max_rotation);
        } else {
            parts.head_rotation = target_heading;
        }

        float dx = cosf(parts.head_rotation) * parts.wander_speed * static_cast<float>(delta_time);
        float dy = sinf(parts.head_rotation) * parts.wander_speed * static_cast<float>(delta_time);
        head.x += dx;
        head.y += dy;
    }

    head.rotation_z = parts.head_rotation;
    parts.world_x = head.x;
    parts.world_y = head.y;
    parts.world_z = head.z;

    if (!parts.body_segments.empty()) {
        float prev_x = head.x;
        float prev_y = head.y;

        for (size_t i = 0; i < parts.body_segments.size(); i++) {
            Particle& segment = particles_view[parts.body_segments[i]];

            float dx = prev_x - segment.x;
            float dy = prev_y - segment.y;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist > 0.001f) {
                float dir_x = dx / dist;
                float dir_y = dy / dist;

                float ideal_x = prev_x - dir_x * parts.segment_spacing;
                float ideal_y = prev_y - dir_y * parts.segment_spacing;

                float move_dx = ideal_x - segment.x;
                float move_dy = ideal_y - segment.y;
                float move_dist = sqrtf(move_dx * move_dx + move_dy * move_dy);

                if (move_dist > 0.001f) {
                    const float FOLLOW_SPEED = 3.0f;
                    float max_move = FOLLOW_SPEED * static_cast<float>(delta_time);
                    if (move_dist > max_move) {
                        segment.x += (move_dx / move_dist) * max_move;
                        segment.y += (move_dy / move_dist) * max_move;
                    } else {
                        segment.x = ideal_x;
                        segment.y = ideal_y;
                    }
                }

                segment.rotation_z = atan2f(dir_y, dir_x);
            }

            prev_x = segment.x;
            prev_y = segment.y;
        }
    }

    ps.mark_bvh_dirty();
}

}  // namespace logosphere::animation
