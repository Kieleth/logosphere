#include "logosphere/effects/assembly_visibility.h"

#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"

namespace Logosphere::Effects {

AssemblyVisibilitySnapshot hide_assembly(ParticleSystem& particles,
                                         const kg::KGModule& kg,
                                         kg::EntityID entity_id) {
    AssemblyVisibilitySnapshot snap;
    auto kg_particles = kg.getEntityKGParticles(entity_id);
    if (kg_particles.empty()) return snap;

    auto write = particles.lock_particles_for_write();
    auto& vec  = write.get_particles();
    snap.entries.reserve(kg_particles.size());
    for (kg::KGParticleID kg_id : kg_particles) {
        kg::RenderIndex idx = kg.getRenderIndex(kg_id);
        if (idx < 0 || static_cast<size_t>(idx) >= vec.size()) continue;
        snap.entries.emplace_back(static_cast<int>(idx), vec[idx].a);
        vec[idx].a = 0.0f;
    }
    return snap;
}

void restore_assembly(ParticleSystem& particles,
                      const AssemblyVisibilitySnapshot& snapshot) {
    if (snapshot.entries.empty()) return;
    auto write = particles.lock_particles_for_write();
    auto& vec  = write.get_particles();
    for (const auto& [idx, prior_alpha] : snapshot.entries) {
        if (idx < 0 || static_cast<size_t>(idx) >= vec.size()) continue;
        vec[idx].a = prior_alpha;
    }
}

}  // namespace Logosphere::Effects
