// KG particle-data storage — the physics-coupled slice of KG.
//
// The knowledge graph stores Particle structs so chunks can be reloaded
// without losing particle state (position, velocity, mass, etc.). That
// tie to `particle.h` is the only reason the rest of the KG module has
// to drag rendering/physics headers into translation units that don't
// care about them.
//
// PIMPL: KGParticleDataStore is forward-declared in kg_core.h as a
// shared_ptr member. shared_ptr's destructor is type-erased via the
// control block, so KGCore's default destructor in kg_core.cpp does NOT
// need the complete type of KGParticleDataStore. That lets kg_core.cpp
// and kg_module.cpp stay free of particle.h, which is the prerequisite
// for a headless-only build profile.
//
// Lazy allocation: the store is null until first particle-data write.
// Headless builds that never touch particle storage pay no allocation.

#include "logosphere/physics/physics_solver.h"
#include "logosphere/physics/creation_door.h"   // oriented_bottom_offset
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <set>
#include "logosphere/kg/kg_core.h"
#include "logosphere/kg/kg_module.h"
#include "particle.h"

#include <mutex>
#include <unordered_map>

namespace kg {

// ============================================================================
// PIMPL: concrete particle data store
// ============================================================================

struct KGParticleDataStore {
    std::unordered_map<KGParticleID, Particle> data;
};

// ============================================================================
// KGCore: particle data storage for reload
// ============================================================================

// THE THIRD DOOR. The turtle guard was wired into ParticleSystem::add_particle
// and ::queue_particle_addition, which misses every generator that stores its
// bodies in the KG and lets chunk activation materialise them later —
// rock_generator, fallen_tree_generator, tree_generator, organic_generator,
// planet_generator and collect_tree_specs all take this path. A repo sweep
// found 51 definite below-turtle placements and flagged that the guard could
// not see the generators most likely to offend.
//
// Checked HERE the violation is attributed to the generator that wrote it,
// at the moment it writes it, instead of surfacing later against whichever
// activator happened to load the chunk.
static void kg_assert_above_turtle(const Particle& p) {
    if (p.GetMass() == 0.0f) return;
    // THE ORIENTED BOTTOM, same as the ParticleSystem door and the solver's
    // own turtle pass. z - thickness/2 describes a solid a rotated body does
    // not have: a log laid flat carries its LENGTH on the thickness axis and
    // its DIAMETER in world Z. This door aborting on correctly-placed logs is
    // what pushed the fallen-tree generator into lifting every preset
    // 0.21-0.29 m off the ground (C10). Ask the geometry.
    // Cost guard first (see the ParticleSystem door): the half diagonal
    // bounds the reach in every pose, so a body with that much clearance
    // never needs its exact extent built.
    if (p.z - logosphere::max_bottom_reach(p) >= PhysicsV4::TURTLE_Z - PhysicsV4::SLOP)
        return;
    const float bottom = p.z - logosphere::oriented_bottom_offset(p);
    if (bottom >= PhysicsV4::TURTLE_Z - PhysicsV4::SLOP) return;
    static std::set<std::string> reported;
    char key[128];
    std::snprintf(key, sizeof(key), "%.5f|%.5f", p.z, p.thickness);
    if (!reported.insert(key).second) return;
    std::cerr << "[TURTLE VIOLATION] setKGParticleData: body STORED below the"
              << " world floor. z=" << p.z << " thickness=" << p.thickness
              << " => bottom=" << bottom << " < " << PhysicsV4::TURTLE_Z
              << " (by " << (PhysicsV4::TURTLE_Z - bottom) << " m)."
              << " It will surface at chunk activation, not here." << std::endl;
    // Strict by default, same as the other two doors. TURTLE_LENIENT=1 to warn.
    if (!std::getenv("TURTLE_LENIENT")) std::abort();
}

void KGCore::setKGParticleData(KGParticleID kg_id, const Particle& particle_data) {
    kg_assert_above_turtle(particle_data);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!particle_data_store_) {
        particle_data_store_ = std::make_shared<KGParticleDataStore>();
    }
    particle_data_store_->data[kg_id] = particle_data;
}

Particle KGCore::getKGParticleData(KGParticleID kg_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!particle_data_store_) return Particle{};
    auto it = particle_data_store_->data.find(kg_id);
    if (it != particle_data_store_->data.end()) {
        return it->second;
    }
    return Particle{};
}

bool KGCore::hasKGParticleData(KGParticleID kg_id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!particle_data_store_) return false;
    return particle_data_store_->data.find(kg_id) != particle_data_store_->data.end();
}

// ============================================================================
// KGModule: physics-coupled facade + entity mass summation
// ============================================================================

void KGModule::setKGParticleData(KGParticleID kg_id, const Particle& particle_data) {
    if (!checkEnabled("setKGParticleData")) {
        return;
    }
    core->setKGParticleData(kg_id, particle_data);
}

Particle KGModule::getKGParticleData(KGParticleID kg_id) const {
    if (!checkEnabled("getKGParticleData")) {
        return Particle{};
    }
    return core->getKGParticleData(kg_id);
}

bool KGModule::hasKGParticleData(KGParticleID kg_id) const {
    if (!checkEnabled("hasKGParticleData")) {
        return false;
    }
    return core->hasKGParticleData(kg_id);
}

float KGModule::getEntityMass(EntityID entity_id) const {
    if (!checkEnabled("getEntityMass")) {
        return 0.0f;
    }

    auto kg_particles = getEntityKGParticles(entity_id);
    float total_mass = 0.0f;
    for (KGParticleID kg_id : kg_particles) {
        Particle p = getKGParticleData(kg_id);
        total_mass += p.GetMass();
    }
    return total_mass;
}

} // namespace kg
