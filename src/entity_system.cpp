#include "entity_system.h"
#include "logosphere/kg/kg_types.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "core/engine.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include <iostream>
#include <algorithm>

// ============================================================================
// PHYSICS-BASED CONSTRAINT STIFFNESS REFERENCE
// ============================================================================
// These values are based on real material properties and biomechanics.
// The code should work with these realistic values, not tuned arbitrary numbers.
//
// XPBD Stiffness Formula: k (N/m) = Force / Displacement
// - Higher stiffness = stiffer connection (less stretch under load)
// - Lower stiffness = more compliant (more stretch under load)
//
// REFERENCE STIFFNESS VALUES (N/m):
//
// 1. RIGID STRUCTURES (Minimal deflection):
//    - Steel beams/columns: 1,000,000 - 10,000,000 N/m
//    - Concrete structures: 500,000 - 2,000,000 N/m
//    - Wood beams (oak): 100,000 - 500,000 N/m
//    - Bone (human femur): 200,000 - 400,000 N/m
//
// 2. HUMANOID SKELETAL JOINTS (Load-bearing):
//    - Spine segments: 50,000 - 150,000 N/m (supports torso weight)
//    - Hip joints: 100,000 - 300,000 N/m (supports full body weight)
//    - Knee joints: 80,000 - 200,000 N/m (high load during standing/walking)
//    - Ankle joints: 60,000 - 150,000 N/m (weight transfer to ground)
//    - Shoulder joints: 30,000 - 80,000 N/m (arm weight only)
//    - Elbow joints: 20,000 - 60,000 N/m (forearm/hand weight)
//
// 3. SOFT TISSUES & TENDONS:
//    - Tendons (Achilles): 10,000 - 50,000 N/m (elastic, store energy)
//    - Ligaments (ACL): 5,000 - 30,000 N/m (flexible stabilizers)
//    - Muscle fibers: 1,000 - 10,000 N/m (highly elastic)
//    - Skin/fascia: 100 - 1,000 N/m (very compliant)
//
// 4. MECHANICAL SPRINGS:
//    - Car suspension: 10,000 - 50,000 N/m (absorb road bumps)
//    - Door closer: 50 - 500 N/m (gentle resistance)
//    - Mattress springs: 100 - 1,000 N/m (comfort)
//
// PRACTICAL EXAMPLE: 70kg Human Standing
// - Total weight: 686N (70kg × 9.8m/s²)
// - If ankle joint has k=100,000 N/m:
//   Compression = F/k = 686N / 100,000 N/m = 0.007m (7mm) ✓ Realistic
// - If ankle joint has k=1,000 N/m:
//   Compression = F/k = 686N / 1,000 N/m = 0.686m (68cm) ✗ Collapse!
//
// CURRENT IMPLEMENTATION ISSUES (2025-01-27):
// - Eva (70kg humanoid) uses k=100,000 N/m for all joints
// - During free-fall (no floor collision), constraints too weak → collapse
// - Need: Either higher stiffness OR proper floor collision support
// - Realistic solution: k=150,000-300,000 N/m for load-bearing joints
//
// CODE INTEGRATION:
// - humanoid_generator.cpp: Use joint-specific stiffness (not single value)
// - physics_system.cpp: Ensure XPBD solver handles high stiffness without instability
// - Test with floor collision to validate load-bearing behavior
// ============================================================================

// DEPRECATED: Direct position manipulation fights physics constraints
// Use setEntityVelocity() instead - see docs/PHYSICS_AND_ANIMATIONS.md
void EntitySystem::moveEntity(
    kg::EntityID entity_id,
    float dx, float dy, float dz) {  // WORLD SPACE deltas: dx=East/West, dy=North/South, dz=Up/Down

    if (!kg_module_ || entity_id == kg::INVALID_ENTITY) {
        return;  // No KG module or invalid entity
    }

    // Get all particles that belong to this entity (recursively via relationships)
    auto kg_particles = kg_module_->getEntityKGParticlesRecursive(entity_id);
    std::vector<kg::RenderIndex> entity_particles;
    for (kg::KGParticleID kg_id : kg_particles) {
        kg::RenderIndex render_idx = kg_module_->getRenderIndex(kg_id);
        if (render_idx != kg::INVALID_RENDER_INDEX) {
            entity_particles.push_back(render_idx);
        }
    }

    // THREAD SAFETY: Use WriteView for automatic exclusive lock
    // Lock acquired on construction, released when view goes out of scope
    auto particles_view = particle_system_.lock_particles_for_write();

    // Move each particle by the same amount IN WORLD SPACE
    bool any_moved = false;
    static int move_count = 0;
    for (kg::RenderIndex pid : entity_particles) {
        if (pid < particles_view.size()) {
            float old_x = particles_view[pid].x;
            float old_y = particles_view[pid].y;

            particles_view[pid].x += dx;  // World X (East/West)
            particles_view[pid].y += dy;  // World Y (North/South)
            particles_view[pid].z += dz;  // World Z (Up/Down)
            any_moved = true;

            // Debug first particle movement every 60 frames
            if (pid == entity_particles[0] && move_count % 60 == 0) {
                std::cout << "[ENTITY_SYSTEM] Moved entity " << entity_id << " particle[" << pid
                          << "] from (" << old_x << "," << old_y << ") to ("
                          << particles_view[pid].x << "," << particles_view[pid].y << ")"
                          << " delta=(" << dx << "," << dy << "," << dz << ")"
                          << " [" << entity_particles.size() << " total particles]" << std::endl;
            }
        }
    }
    move_count++;

    // Mark BVH dirty if any particles actually moved
    if (any_moved && (dx != 0.0f || dy != 0.0f || dz != 0.0f)) {
        particle_system_.mark_bvh_dirty();
    }

}

// PHYSICS-DRIVEN: Set velocity on all entity particles
// Physics will integrate velocity → position, gluons maintain structure
// See docs/PHYSICS_AND_ANIMATIONS.md
void EntitySystem::setEntityVelocity(
    kg::EntityID entity_id,
    float vx, float vy, float vz) {  // Velocity in m/s

    if (!kg_module_ || entity_id == kg::INVALID_ENTITY) {
        return;
    }

    // Get all particles that belong to this entity
    auto kg_particles = kg_module_->getEntityKGParticlesRecursive(entity_id);
    std::vector<kg::RenderIndex> entity_particles;
    for (kg::KGParticleID kg_id : kg_particles) {
        kg::RenderIndex render_idx = kg_module_->getRenderIndex(kg_id);
        if (render_idx != kg::INVALID_RENDER_INDEX) {
            entity_particles.push_back(render_idx);
        }
    }

    auto particles_view = particle_system_.lock_particles_for_write();

    // Set velocity on all particles - physics will integrate
    static int vel_count = 0;
    for (kg::RenderIndex pid : entity_particles) {
        if (pid < particles_view.size()) {
            particles_view[pid].vx = vx;
            particles_view[pid].vy = vy;
            particles_view[pid].vz = vz;

            // Debug first particle every 60 frames
            if (pid == entity_particles[0] && vel_count % 60 == 0) {
                std::cout << "[ENTITY_SYSTEM] Set entity " << entity_id
                          << " velocity=(" << vx << "," << vy << "," << vz << ") m/s"
                          << " [" << entity_particles.size() << " particles]" << std::endl;
            }
        }
    }
    vel_count++;
}

void EntitySystem::moveParticleOrEntity(int particle_id, float dx, float dy, float dz) {
    // Check if this particle is part of an entity
    kg::EntityID entity_id = getEntityForParticle(particle_id);
    
    if (entity_id != kg::INVALID_ENTITY) {
        // Particle is part of entity - move the whole entity
        moveEntity(entity_id, dx, dy, dz);
    } else {
        // THREAD SAFETY: Use WriteView for automatic exclusive lock
        auto particles_view = particle_system_.lock_particles_for_write();

        // Just a single particle - move it alone
        if (particle_id >= 0 && particle_id < static_cast<int>(particles_view.size())) {
            particles_view[particle_id].x += dx;
            particles_view[particle_id].y += dy;
            particles_view[particle_id].z += dz;

            // Mark BVH dirty if particle actually moved
            if (dx != 0.0f || dy != 0.0f || dz != 0.0f) {
                particle_system_.mark_bvh_dirty();
            }
        }
    }
}

bool EntitySystem::isPartOfEntity(int particle_id) const {
    return getEntityForParticle(particle_id) != kg::INVALID_ENTITY;
}

kg::EntityID EntitySystem::getEntityForParticle(int particle_id) const {
    if (!kg_module_) {
        return kg::INVALID_ENTITY;
    }

    auto entities = kg_module_->getEntitiesByRenderIndex(particle_id);
    return entities.empty() ? kg::INVALID_ENTITY : entities[0];
}

int EntitySystem::removeEntity(kg::EntityID entity_id, bool destroy_kg_entity) {
    if (!kg_module_ || entity_id == kg::INVALID_ENTITY) {
        return 0;
    }

    // 1. Get all particle render indices for this entity
    auto kg_particles = kg_module_->getEntityKGParticlesRecursive(entity_id);
    std::vector<kg::RenderIndex> render_indices;
    for (kg::KGParticleID kg_id : kg_particles) {
        kg::RenderIndex render_idx = kg_module_->getRenderIndex(kg_id);
        if (render_idx != kg::INVALID_RENDER_INDEX) {
            render_indices.push_back(render_idx);
        }
    }

    if (render_indices.empty()) {
        std::cout << "[ENTITY_SYSTEM] No particles found for entity " << entity_id << std::endl;
        return 0;
    }

    // 2. Unregister from humanoid locomotion (try each particle — only hips matters).
    //    Reached via the dynamics system's engine pointer; the public
    //    unregister_humanoid delegator on ParticleDynamicsSystem was retired in B8.
    if (dynamics_system_) {
        Engine* engine = dynamics_system_->get_engine();
        if (engine) {
            for (kg::RenderIndex idx : render_indices) {
                engine->get_humanoid_locomotion().unregister_humanoid(static_cast<int>(idx));
            }
        }
    }

    // 3. Sort render indices in DESCENDING order to handle swap-and-pop correctly
    //    ParticleSystem swaps removed particle with last particle, invalidating indices.
    //    By removing highest indices first, lower indices remain valid.
    std::sort(render_indices.begin(), render_indices.end(), std::greater<kg::RenderIndex>());

    // 4. Remove particles in descending order
    int removed_count = 0;
    for (kg::RenderIndex idx : render_indices) {
        particle_system_.remove_particle(static_cast<int>(idx));
        removed_count++;
    }

    std::cout << "[ENTITY_SYSTEM] Removed " << removed_count << " particles for entity " << entity_id << std::endl;

    // 5. Optionally destroy entity from KG (cleans up KG mappings)
    if (destroy_kg_entity) {
        kg_module_->destroyEntity(entity_id);
    }

    return removed_count;
}