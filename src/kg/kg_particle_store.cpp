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

void KGCore::setKGParticleData(KGParticleID kg_id, const Particle& particle_data) {
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
