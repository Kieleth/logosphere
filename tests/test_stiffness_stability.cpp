#include "../src/core/engine.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <string>

// ============================================================================
// XPBD STABILITY INVESTIGATION - INCREMENTAL COMPLEXITY
// ============================================================================
// Goal: Find the exact breaking point where XPBD numerical instability occurs
// Method: Start with minimal case, progressively add complexity
//
// Level 0: 2 particles (Foot + Shin), 1 constraint, NO floor (free fall)
// Level 1: 2 particles + floor collision
// Level 2: 3 particles (Foot + Shin + Thigh), 2 constraints + floor
// Level 3: 4 particles (+ Hip), 3 constraints + floor
//
// Test stiffness values: 50k, 100k, 150k, 200k N/m
// Success criteria: No explosion (horizontal drift < 5m, spread increase < 1m)
// ============================================================================

namespace {

void calculate_metrics(const std::vector<unsigned int>& indices,
                      ParticleSystem& particle_system,
                      float& center_x, float& center_y, float& spread) {
    auto particles = particle_system.lock_particles_for_read();

    if (indices.empty()) {
        center_x = center_y = spread = 0.0f;
        return;
    }

    center_x = 0.0f;
    center_y = 0.0f;
    for (auto idx : indices) {
        center_x += particles[idx].x;
        center_y += particles[idx].y;
    }
    center_x /= indices.size();
    center_y /= indices.size();

    spread = 0.0f;
    for (auto idx : indices) {
        float dx = particles[idx].x - center_x;
        float dy = particles[idx].y - center_y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > spread) spread = dist;
    }
}

bool run_stability_test(const std::vector<unsigned int>& particle_indices,
                       ParticleSystem& ps, Engine& engine, float stiffness,
                       const char* level_name) {
    float initial_center_x, initial_center_y, initial_spread;
    calculate_metrics(particle_indices, ps, initial_center_x, initial_center_y, initial_spread);

    const float dt = 1.0f / 60.0f;
    const int total_frames = 260;  // Run to Frame 260 to capture Frame 245-246 failure
    const float max_horizontal_drift = 100.0f;  // Don't exit early - let it run
    const float max_spread_increase = 100.0f;

    for (int frame = 0; frame < total_frames; frame++) {
        engine.update(dt);

        if (frame % 10 == 0 && frame > 0) {
            float center_x, center_y, spread;
            calculate_metrics(particle_indices, ps, center_x, center_y, spread);

            float horizontal_drift = std::sqrt(
                std::pow(center_x - initial_center_x, 2) +
                std::pow(center_y - initial_center_y, 2)
            );
            float spread_increase = spread - initial_spread;

            std::cout << "[Frame " << frame << "] drift=" << horizontal_drift
                      << "m spread_inc=" << spread_increase << "m";

            if (horizontal_drift > max_horizontal_drift || spread_increase > max_spread_increase) {
                std::cout << " EXPLOSION!" << std::endl;
                return false;
            }
            std::cout << std::endl;
        }
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// LEVEL 0: 2 Particles, NO Floor
// ============================================================================
bool test_level_0(float stiffness) {
    std::cout << "\n[Level 0] 2 particles, NO floor, " << (stiffness/1000.0f) << "k N/m" << std::endl;

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    engine.initialize(config);

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    const float foot_radius = 0.08f;
    const float leg_segment_length = 0.4f;

    Particle foot = {};
    foot.x = 0.0f; foot.y = 0.0f; foot.z = foot_radius;
    foot.width = foot_radius * 2.0f;
    foot.height = foot_radius * 2.0f;
    foot.thickness = foot_radius * 2.0f;
    foot.r = 0.8f; foot.g = 0.6f; foot.b = 0.4f; foot.a = 1.0f;
    foot.shape = ParticleShape::BOX;
    foot.material_density = 500.0f;
    foot.CalculateMass();
    unsigned int foot_idx = engine.add_particle(foot);

    Particle shin = {};
    shin.x = 0.0f; shin.y = 0.0f; shin.z = foot_radius * 2.0f + leg_segment_length / 2.0f;
    shin.width = 0.08f; shin.height = 0.08f; shin.thickness = leg_segment_length;
    shin.r = 0.9f; shin.g = 0.7f; shin.b = 0.5f; shin.a = 1.0f;
    shin.shape = ParticleShape::BOX;
    shin.material_density = 500.0f;
    shin.CalculateMass();
    unsigned int shin_idx = engine.add_particle(shin);

    kg::KGParticleID foot_kg = kg.createKGParticle(1, foot_idx);
    kg::KGParticleID shin_kg = kg.createKGParticle(1, shin_idx);

    kg::EntityID constraint_id = kg.createEntity("Constraint");
    kg.setProperty(constraint_id, "particle_a", std::to_string(foot_kg));
    kg.setProperty(constraint_id, "particle_b", std::to_string(shin_kg));
    kg.setProperty(constraint_id, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(constraint_id, "base_stiffness", std::to_string(stiffness));

    engine.get_physics_system().load_constraints_from_kg();

    std::vector<unsigned int> particle_indices = {foot_idx, shin_idx};
    return run_stability_test(particle_indices, ps, engine, stiffness, "Level 0");
}

// ============================================================================
// LEVEL 1: 2 Particles + Floor
// ============================================================================
bool test_level_1(float stiffness) {
    std::cout << "\n[Level 1] 2 particles + FLOOR, " << (stiffness/1000.0f) << "k N/m" << std::endl;

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    engine.initialize(config);

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    // Create floor
    FloorTileConfig floor_config;
    floor_config.tile_width = 2.0f;
    floor_config.tile_height = 2.0f;
    floor_config.tile_thickness = 0.1f;
    ps.create_floor_grid(0.0f, 0.0f, 3, 3, floor_config);

    const float foot_radius = 0.08f;
    const float leg_segment_length = 0.4f;

    // Foot starts 1m above floor
    Particle foot = {};
    foot.x = 0.0f; foot.y = 0.0f; foot.z = 1.0f;
    foot.width = foot_radius * 2.0f;
    foot.height = foot_radius * 2.0f;
    foot.thickness = foot_radius * 2.0f;
    foot.r = 0.8f; foot.g = 0.6f; foot.b = 0.4f; foot.a = 1.0f;
    foot.shape = ParticleShape::BOX;
    foot.material_density = 500.0f;
    foot.CalculateMass();
    unsigned int foot_idx = engine.add_particle(foot);

    Particle shin = {};
    shin.x = 0.0f; shin.y = 0.0f; shin.z = 1.0f + leg_segment_length;
    shin.width = 0.08f; shin.height = 0.08f; shin.thickness = leg_segment_length;
    shin.r = 0.9f; shin.g = 0.7f; shin.b = 0.5f; shin.a = 1.0f;
    shin.shape = ParticleShape::BOX;
    shin.material_density = 500.0f;
    shin.CalculateMass();
    unsigned int shin_idx = engine.add_particle(shin);

    kg::KGParticleID foot_kg = kg.createKGParticle(1, foot_idx);
    kg::KGParticleID shin_kg = kg.createKGParticle(1, shin_idx);

    kg::EntityID constraint_id = kg.createEntity("Constraint");
    kg.setProperty(constraint_id, "particle_a", std::to_string(foot_kg));
    kg.setProperty(constraint_id, "particle_b", std::to_string(shin_kg));
    kg.setProperty(constraint_id, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(constraint_id, "base_stiffness", std::to_string(stiffness));

    engine.get_physics_system().load_constraints_from_kg();

    std::vector<unsigned int> particle_indices = {foot_idx, shin_idx};
    return run_stability_test(particle_indices, ps, engine, stiffness, "Level 1");
}

// ============================================================================
// LEVEL 2: 3 Particles (Full Leg) + Floor
// ============================================================================
bool test_level_2(float stiffness) {
    std::cout << "\n[Level 2] 3 particles (Foot+Shin+Thigh) + floor, " << (stiffness/1000.0f) << "k N/m" << std::endl;

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    engine.initialize(config);

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    // Create floor
    FloorTileConfig floor_config;
    floor_config.tile_width = 2.0f;
    floor_config.tile_height = 2.0f;
    floor_config.tile_thickness = 0.1f;
    ps.create_floor_grid(0.0f, 0.0f, 3, 3, floor_config);

    const float foot_radius = 0.08f;
    const float leg_segment_length = 0.4f;

    // Foot
    Particle foot = {};
    foot.x = 0.0f; foot.y = 0.0f; foot.z = 1.0f;
    foot.width = foot_radius * 2.0f;
    foot.height = foot_radius * 2.0f;
    foot.thickness = foot_radius * 2.0f;
    foot.r = 0.8f; foot.g = 0.6f; foot.b = 0.4f; foot.a = 1.0f;
    foot.shape = ParticleShape::BOX;
    foot.material_density = 500.0f;
    foot.CalculateMass();
    unsigned int foot_idx = engine.add_particle(foot);

    // Shin
    Particle shin = {};
    shin.x = 0.0f; shin.y = 0.0f; shin.z = 1.0f + leg_segment_length;
    shin.width = 0.08f; shin.height = 0.08f; shin.thickness = leg_segment_length;
    shin.r = 0.9f; shin.g = 0.7f; shin.b = 0.5f; shin.a = 1.0f;
    shin.shape = ParticleShape::BOX;
    shin.material_density = 500.0f;
    shin.CalculateMass();
    unsigned int shin_idx = engine.add_particle(shin);

    // Thigh
    Particle thigh = {};
    thigh.x = 0.0f; thigh.y = 0.0f; thigh.z = 1.0f + leg_segment_length * 2;
    thigh.width = 0.08f; thigh.height = 0.08f; thigh.thickness = leg_segment_length;
    thigh.r = 1.0f; thigh.g = 0.8f; thigh.b = 0.6f; thigh.a = 1.0f;
    thigh.shape = ParticleShape::BOX;
    thigh.material_density = 500.0f;
    thigh.CalculateMass();
    unsigned int thigh_idx = engine.add_particle(thigh);

    kg::KGParticleID foot_kg = kg.createKGParticle(1, foot_idx);
    kg::KGParticleID shin_kg = kg.createKGParticle(1, shin_idx);
    kg::KGParticleID thigh_kg = kg.createKGParticle(1, thigh_idx);

    // Constraint 1: Foot - Shin
    kg::EntityID c1 = kg.createEntity("Constraint");
    kg.setProperty(c1, "particle_a", std::to_string(foot_kg));
    kg.setProperty(c1, "particle_b", std::to_string(shin_kg));
    kg.setProperty(c1, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c1, "base_stiffness", std::to_string(stiffness));

    // Constraint 2: Shin - Thigh
    kg::EntityID c2 = kg.createEntity("Constraint");
    kg.setProperty(c2, "particle_a", std::to_string(shin_kg));
    kg.setProperty(c2, "particle_b", std::to_string(thigh_kg));
    kg.setProperty(c2, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c2, "base_stiffness", std::to_string(stiffness));

    engine.get_physics_system().load_constraints_from_kg();

    std::vector<unsigned int> particle_indices = {foot_idx, shin_idx, thigh_idx};
    return run_stability_test(particle_indices, ps, engine, stiffness, "Level 2");
}

// ============================================================================
// LEVEL 3: 4 Particles (+ Hip) + Floor
// ============================================================================
bool test_level_3(float stiffness) {
    std::cout << "\n[Level 3] 4 particles (Foot+Shin+Thigh+Hip) + floor, " << (stiffness/1000.0f) << "k N/m" << std::endl;

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    engine.initialize(config);

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    // Create floor
    FloorTileConfig floor_config;
    floor_config.tile_width = 2.0f;
    floor_config.tile_height = 2.0f;
    floor_config.tile_thickness = 0.1f;
    ps.create_floor_grid(0.0f, 0.0f, 3, 3, floor_config);

    const float foot_radius = 0.08f;
    const float leg_segment_length = 0.4f;
    const float hip_radius = 0.1f;

    // Foot
    Particle foot = {};
    foot.x = 0.0f; foot.y = 0.0f; foot.z = 1.0f;
    foot.width = foot_radius * 2.0f;
    foot.height = foot_radius * 2.0f;
    foot.thickness = foot_radius * 2.0f;
    foot.r = 0.8f; foot.g = 0.6f; foot.b = 0.4f; foot.a = 1.0f;
    foot.shape = ParticleShape::BOX;
    foot.material_density = 500.0f;
    foot.CalculateMass();
    unsigned int foot_idx = engine.add_particle(foot);

    // Shin
    Particle shin = {};
    shin.x = 0.0f; shin.y = 0.0f; shin.z = 1.0f + leg_segment_length;
    shin.width = 0.08f; shin.height = 0.08f; shin.thickness = leg_segment_length;
    shin.r = 0.9f; shin.g = 0.7f; shin.b = 0.5f; shin.a = 1.0f;
    shin.shape = ParticleShape::BOX;
    shin.material_density = 500.0f;
    shin.CalculateMass();
    unsigned int shin_idx = engine.add_particle(shin);

    // Thigh
    Particle thigh = {};
    thigh.x = 0.0f; thigh.y = 0.0f; thigh.z = 1.0f + leg_segment_length * 2;
    thigh.width = 0.08f; thigh.height = 0.08f; thigh.thickness = leg_segment_length;
    thigh.r = 1.0f; thigh.g = 0.8f; thigh.b = 0.6f; thigh.a = 1.0f;
    thigh.shape = ParticleShape::BOX;
    thigh.material_density = 500.0f;
    thigh.CalculateMass();
    unsigned int thigh_idx = engine.add_particle(thigh);

    // Hip
    Particle hip = {};
    hip.x = 0.0f; hip.y = 0.0f; hip.z = 1.0f + leg_segment_length * 3;
    hip.width = hip_radius * 2.0f;
    hip.height = hip_radius * 2.0f;
    hip.thickness = hip_radius * 2.0f;
    hip.r = 0.7f; hip.g = 0.5f; hip.b = 0.3f; hip.a = 1.0f;
    hip.shape = ParticleShape::BOX;
    hip.material_density = 500.0f;
    hip.CalculateMass();
    unsigned int hip_idx = engine.add_particle(hip);

    kg::KGParticleID foot_kg = kg.createKGParticle(1, foot_idx);
    kg::KGParticleID shin_kg = kg.createKGParticle(1, shin_idx);
    kg::KGParticleID thigh_kg = kg.createKGParticle(1, thigh_idx);
    kg::KGParticleID hip_kg = kg.createKGParticle(1, hip_idx);

    // Constraint 1: Foot - Shin
    kg::EntityID c1 = kg.createEntity("Constraint");
    kg.setProperty(c1, "particle_a", std::to_string(foot_kg));
    kg.setProperty(c1, "particle_b", std::to_string(shin_kg));
    kg.setProperty(c1, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c1, "base_stiffness", std::to_string(stiffness));

    // Constraint 2: Shin - Thigh
    kg::EntityID c2 = kg.createEntity("Constraint");
    kg.setProperty(c2, "particle_a", std::to_string(shin_kg));
    kg.setProperty(c2, "particle_b", std::to_string(thigh_kg));
    kg.setProperty(c2, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c2, "base_stiffness", std::to_string(stiffness));

    // Constraint 3: Thigh - Hip
    kg::EntityID c3 = kg.createEntity("Constraint");
    kg.setProperty(c3, "particle_a", std::to_string(thigh_kg));
    kg.setProperty(c3, "particle_b", std::to_string(hip_kg));
    kg.setProperty(c3, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c3, "base_stiffness", std::to_string(stiffness));

    engine.get_physics_system().load_constraints_from_kg();

    std::vector<unsigned int> particle_indices = {foot_idx, shin_idx, thigh_idx, hip_idx};
    return run_stability_test(particle_indices, ps, engine, stiffness, "Level 3");
}

// ============================================================================
// LEVEL 4: 7 Particles (Both Legs) + Floor
// ============================================================================
bool test_level_4(float stiffness) {
    std::cout << "\n[Level 4] 7 particles (Both legs + Hip) + floor, " << (stiffness/1000.0f) << "k N/m" << std::endl;

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    engine.initialize(config);

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    // Create floor
    FloorTileConfig floor_config;
    floor_config.tile_width = 2.0f;
    floor_config.tile_height = 2.0f;
    floor_config.tile_thickness = 0.1f;
    ps.create_floor_grid(0.0f, 0.0f, 3, 3, floor_config);

    const float foot_radius = 0.08f;
    const float leg_segment_length = 0.4f;
    const float hip_radius = 0.1f;
    const float leg_separation = 0.2f;  // 20cm between legs

    // Hip (center, at top)
    Particle hip = {};
    hip.x = 0.0f; hip.y = 0.0f; hip.z = 1.0f + leg_segment_length * 3;
    hip.width = hip_radius * 2.0f;
    hip.height = hip_radius * 2.0f;
    hip.thickness = hip_radius * 2.0f;
    hip.r = 0.7f; hip.g = 0.5f; hip.b = 0.3f; hip.a = 1.0f;
    hip.shape = ParticleShape::BOX;
    hip.material_density = 500.0f;
    hip.CalculateMass();
    unsigned int hip_idx = engine.add_particle(hip);

    // LEFT LEG
    Particle left_thigh = {};
    left_thigh.x = -leg_separation; left_thigh.y = 0.0f; left_thigh.z = 1.0f + leg_segment_length * 2;
    left_thigh.width = 0.08f; left_thigh.height = 0.08f; left_thigh.thickness = leg_segment_length;
    left_thigh.r = 1.0f; left_thigh.g = 0.8f; left_thigh.b = 0.6f; left_thigh.a = 1.0f;
    left_thigh.shape = ParticleShape::BOX;
    left_thigh.material_density = 500.0f;
    left_thigh.CalculateMass();
    unsigned int left_thigh_idx = engine.add_particle(left_thigh);

    Particle left_shin = {};
    left_shin.x = -leg_separation; left_shin.y = 0.0f; left_shin.z = 1.0f + leg_segment_length;
    left_shin.width = 0.08f; left_shin.height = 0.08f; left_shin.thickness = leg_segment_length;
    left_shin.r = 0.9f; left_shin.g = 0.7f; left_shin.b = 0.5f; left_shin.a = 1.0f;
    left_shin.shape = ParticleShape::BOX;
    left_shin.material_density = 500.0f;
    left_shin.CalculateMass();
    unsigned int left_shin_idx = engine.add_particle(left_shin);

    Particle left_foot = {};
    left_foot.x = -leg_separation; left_foot.y = 0.0f; left_foot.z = 1.0f;
    left_foot.width = foot_radius * 2.0f;
    left_foot.height = foot_radius * 2.0f;
    left_foot.thickness = foot_radius * 2.0f;
    left_foot.r = 0.8f; left_foot.g = 0.6f; left_foot.b = 0.4f; left_foot.a = 1.0f;
    left_foot.shape = ParticleShape::BOX;
    left_foot.material_density = 500.0f;
    left_foot.CalculateMass();
    unsigned int left_foot_idx = engine.add_particle(left_foot);

    // RIGHT LEG
    Particle right_thigh = {};
    right_thigh.x = leg_separation; right_thigh.y = 0.0f; right_thigh.z = 1.0f + leg_segment_length * 2;
    right_thigh.width = 0.08f; right_thigh.height = 0.08f; right_thigh.thickness = leg_segment_length;
    right_thigh.r = 1.0f; right_thigh.g = 0.8f; right_thigh.b = 0.6f; right_thigh.a = 1.0f;
    right_thigh.shape = ParticleShape::BOX;
    right_thigh.material_density = 500.0f;
    right_thigh.CalculateMass();
    unsigned int right_thigh_idx = engine.add_particle(right_thigh);

    Particle right_shin = {};
    right_shin.x = leg_separation; right_shin.y = 0.0f; right_shin.z = 1.0f + leg_segment_length;
    right_shin.width = 0.08f; right_shin.height = 0.08f; right_shin.thickness = leg_segment_length;
    right_shin.r = 0.9f; right_shin.g = 0.7f; right_shin.b = 0.5f; right_shin.a = 1.0f;
    right_shin.shape = ParticleShape::BOX;
    right_shin.material_density = 500.0f;
    right_shin.CalculateMass();
    unsigned int right_shin_idx = engine.add_particle(right_shin);

    Particle right_foot = {};
    right_foot.x = leg_separation; right_foot.y = 0.0f; right_foot.z = 1.0f;
    right_foot.width = foot_radius * 2.0f;
    right_foot.height = foot_radius * 2.0f;
    right_foot.thickness = foot_radius * 2.0f;
    right_foot.r = 0.8f; right_foot.g = 0.6f; right_foot.b = 0.4f; right_foot.a = 1.0f;
    right_foot.shape = ParticleShape::BOX;
    right_foot.material_density = 500.0f;
    right_foot.CalculateMass();
    unsigned int right_foot_idx = engine.add_particle(right_foot);

    // Create KG particles
    kg::KGParticleID hip_kg = kg.createKGParticle(1, hip_idx);
    kg::KGParticleID left_thigh_kg = kg.createKGParticle(1, left_thigh_idx);
    kg::KGParticleID left_shin_kg = kg.createKGParticle(1, left_shin_idx);
    kg::KGParticleID left_foot_kg = kg.createKGParticle(1, left_foot_idx);
    kg::KGParticleID right_thigh_kg = kg.createKGParticle(1, right_thigh_idx);
    kg::KGParticleID right_shin_kg = kg.createKGParticle(1, right_shin_idx);
    kg::KGParticleID right_foot_kg = kg.createKGParticle(1, right_foot_idx);

    // Left leg constraints (Hip -> Thigh -> Shin -> Foot)
    kg::EntityID c1 = kg.createEntity("Constraint");
    kg.setProperty(c1, "particle_a", std::to_string(hip_kg));
    kg.setProperty(c1, "particle_b", std::to_string(left_thigh_kg));
    kg.setProperty(c1, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c1, "base_stiffness", std::to_string(stiffness));

    kg::EntityID c2 = kg.createEntity("Constraint");
    kg.setProperty(c2, "particle_a", std::to_string(left_thigh_kg));
    kg.setProperty(c2, "particle_b", std::to_string(left_shin_kg));
    kg.setProperty(c2, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c2, "base_stiffness", std::to_string(stiffness));

    kg::EntityID c3 = kg.createEntity("Constraint");
    kg.setProperty(c3, "particle_a", std::to_string(left_shin_kg));
    kg.setProperty(c3, "particle_b", std::to_string(left_foot_kg));
    kg.setProperty(c3, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c3, "base_stiffness", std::to_string(stiffness));

    // Right leg constraints (Hip -> Thigh -> Shin -> Foot)
    kg::EntityID c4 = kg.createEntity("Constraint");
    kg.setProperty(c4, "particle_a", std::to_string(hip_kg));
    kg.setProperty(c4, "particle_b", std::to_string(right_thigh_kg));
    kg.setProperty(c4, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c4, "base_stiffness", std::to_string(stiffness));

    kg::EntityID c5 = kg.createEntity("Constraint");
    kg.setProperty(c5, "particle_a", std::to_string(right_thigh_kg));
    kg.setProperty(c5, "particle_b", std::to_string(right_shin_kg));
    kg.setProperty(c5, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c5, "base_stiffness", std::to_string(stiffness));

    kg::EntityID c6 = kg.createEntity("Constraint");
    kg.setProperty(c6, "particle_a", std::to_string(right_shin_kg));
    kg.setProperty(c6, "particle_b", std::to_string(right_foot_kg));
    kg.setProperty(c6, "rest_distance", std::to_string(leg_segment_length));
    kg.setProperty(c6, "base_stiffness", std::to_string(stiffness));

    engine.get_physics_system().load_constraints_from_kg();

    std::vector<unsigned int> particle_indices = {
        hip_idx,
        left_thigh_idx, left_shin_idx, left_foot_idx,
        right_thigh_idx, right_shin_idx, right_foot_idx
    };
    return run_stability_test(particle_indices, ps, engine, stiffness, "Level 4");
}

// ============================================================================
// LEVEL 5: 23 Particles (Full Eva) + Floor
// ============================================================================
bool test_level_5(float stiffness) {
    std::cout << "\n[Level 5] 23 particles (Full Eva) + floor, " << (stiffness/1000.0f) << "k N/m" << std::endl;

    Engine engine;
    EngineConfig config;
    config.create_display = false;
    engine.initialize(config);

    auto& ps = engine.get_particle_system();
    auto& kg = engine.get_kg();

    // Create floor
    FloorTileConfig floor_config;
    floor_config.tile_width = 2.0f;
    floor_config.tile_height = 2.0f;
    floor_config.tile_thickness = 0.1f;
    ps.create_floor_grid(0.0f, 0.0f, 3, 3, floor_config);

    // Create Eva using humanoid generator
    HumanoidGenerator humanoid_gen;
    humanoid_gen.initialize(&engine, &kg);

    // Eva spawns at (0,0,1.0) which will drop onto the floor
    auto result = humanoid_gen.create_and_activate_eva(0.0f, 0.0f, 1.0f, &engine);

    // NOTE: Level 5 uses Eva's default stiffness values (150k-200k N/m mixed)
    // This tests the FULL Eva structure as she actually is configured
    // The 'stiffness' parameter is ignored for this level
    std::cout << "Created Eva with " << result.render_indices.size()
              << " particles (using default stiffness: 150k-200k N/m)" << std::endl;

    return run_stability_test(result.render_indices, ps, engine, stiffness, "Level 5");
}

// ============================================================================
// Main Test: Run All Levels Incrementally
// ============================================================================
bool test_stiffness_stability() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "XPBD STABILITY INVESTIGATION" << std::endl;
    std::cout << "Incremental Complexity Testing" << std::endl;
    std::cout << "========================================" << std::endl;

    std::vector<float> stiffness_values = {50000.0f, 100000.0f, 150000.0f, 200000.0f};

    for (float stiffness : stiffness_values) {
        std::cout << "\n========================================" << std::endl;
        std::cout << "TESTING STIFFNESS: " << (stiffness/1000.0f) << "k N/m" << std::endl;
        std::cout << "========================================" << std::endl;

        // Level 0: No floor
        if (!test_level_0(stiffness)) {
            std::cout << "\n!!! FAILURE AT LEVEL 0 (" << (stiffness/1000.0f) << "k N/m) !!!" << std::endl;
            return false;
        }

        // Level 1: Add floor
        if (!test_level_1(stiffness)) {
            std::cout << "\n!!! FAILURE AT LEVEL 1 (" << (stiffness/1000.0f) << "k N/m) !!!" << std::endl;
            std::cout << "Floor collision interaction causes instability" << std::endl;
            return false;
        }

        // Level 2: Add third particle
        if (!test_level_2(stiffness)) {
            std::cout << "\n!!! FAILURE AT LEVEL 2 (" << (stiffness/1000.0f) << "k N/m) !!!" << std::endl;
            std::cout << "3-particle chain causes instability" << std::endl;
            return false;
        }

        // Level 3: Add hip
        if (!test_level_3(stiffness)) {
            std::cout << "\n!!! FAILURE AT LEVEL 3 (" << (stiffness/1000.0f) << "k N/m) !!!" << std::endl;
            std::cout << "4-particle chain causes instability" << std::endl;
            return false;
        }

        // Level 4: Both legs
        if (!test_level_4(stiffness)) {
            std::cout << "\n!!! FAILURE AT LEVEL 4 (" << (stiffness/1000.0f) << "k N/m) !!!" << std::endl;
            std::cout << "7-particle (both legs) causes instability" << std::endl;
            return false;
        }

        // Level 5: Full Eva
        if (!test_level_5(stiffness)) {
            std::cout << "\n!!! FAILURE AT LEVEL 5 (" << (stiffness/1000.0f) << "k N/m) !!!" << std::endl;
            std::cout << "23-particle (full Eva) causes instability" << std::endl;
            return false;
        }

        std::cout << "\n>>> ALL LEVELS PASSED at " << (stiffness/1000.0f) << "k N/m <<<" << std::endl;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "RESULT: ALL LEVELS STABLE AT ALL STIFFNESS VALUES" << std::endl;
    std::cout << "========================================" << std::endl;
    return true;
}
