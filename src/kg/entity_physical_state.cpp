#include "logosphere/kg/entity_physical_state.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/physics/physics_system.h"
#include "core/particle_system.h"
#include "particle.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace logosphere {

void EntityPhysicalState::initialize(ParticleSystem* particles,
                                     PhysicsSystem* physics,
                                     kg::KGModule* kg) {
    particles_ = particles;
    physics_ = physics;
    kg_ = kg;
}

bool EntityPhysicalState::is_lever(const std::string& property) {
    return property == "solver_authority" || property == "bond_strength";
}

int EntityPhysicalState::apply(kg::EntityID entity) {
    if (!kg_ || entity == kg::INVALID_ENTITY) return 0;
    int touched = apply_solver_authority(entity);
    touched += apply_bonds(entity);
    return touched;
}

int EntityPhysicalState::apply_solver_authority(kg::EntityID entity) {
    const std::string value = kg_->getProperty(entity, "solver_authority");
    if (value.empty()) return 0;   // unset means DYNAMIC; leave it alone

    ParticleSolverMode mode;
    if (value == "KINEMATIC")   mode = ParticleSolverMode::KINEMATIC;
    else if (value == "STATIC") mode = ParticleSolverMode::STATIC;
    else if (value == "DYNAMIC") mode = ParticleSolverMode::DYNAMIC;
    else {
        std::cerr << "[EntityPhysicalState] entity " << entity
                  << " has unknown solver_authority '" << value << "'"
                  << std::endl;
        return 0;
    }

    // HAS_PART, so one setting covers a whole articulated body rather
    // than whichever particle happens to be the entity's own.
    auto ids = kg_->getEntityKGParticlesRecursive(entity, "HAS_PART");
    int touched = 0;
    for (kg::KGParticleID pid : ids) {
        kg::RenderIndex idx = kg_->getRenderIndex(pid);
        if (idx != kg::INVALID_RENDER_INDEX && particles_) {
            // Live body.
            auto view = particles_->lock_particles_for_write();
            auto& all = view.get_particles();
            if (idx < all.size()) {
                all[idx].solver_mode = mode;
                // A body that just became movable must not stay
                // asleep, or it hangs in the air until something
                // touches it.
                if (mode == ParticleSolverMode::DYNAMIC)
                    all[idx].is_at_rest = false;
                ++touched;
            }
        } else if (kg_->hasKGParticleData(pid)) {
            // Still deferred: the setting travels with it into the
            // world when it activates.
            Particle p = kg_->getKGParticleData(pid);
            p.solver_mode = mode;
            if (mode == ParticleSolverMode::DYNAMIC) p.is_at_rest = false;
            kg_->setKGParticleData(pid, p);
            ++touched;
        }
    }
    return touched;
}

int EntityPhysicalState::apply_bonds(kg::EntityID entity) {
    auto partners = kg_->getRelated(entity, "BONDED_TO");
    if (partners.empty()) return 0;

    const std::string strength_s = kg_->getProperty(entity, "bond_strength");
    float stiffness = 90000.0f;   // rigid cement unless told otherwise
    if (!strength_s.empty()) {
        try { stiffness = std::stof(strength_s); } catch (...) {}
    }

    auto mine = kg_->getEntityKGParticlesRecursive(entity, "HAS_PART");
    if (mine.empty()) return 0;

    int made = 0;
    for (kg::EntityID other : partners) {
        auto theirs = kg_->getEntityKGParticlesRecursive(other, "HAS_PART");
        if (theirs.empty()) continue;

        // Cement where the bodies actually meet: the closest pair.
        // Bonding every pair would be thousands of joins describing
        // one physical fact.
        kg::KGParticleID best_a = mine.front(), best_b = theirs.front();
        float best_d2 = 1e30f;
        bool have_positions = false;
        for (kg::KGParticleID a : mine) {
            Particle pa;
            if (!kg_->hasKGParticleData(a)) continue;
            pa = kg_->getKGParticleData(a);
            for (kg::KGParticleID b : theirs) {
                if (!kg_->hasKGParticleData(b)) continue;
                Particle pb = kg_->getKGParticleData(b);
                float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best_d2) {
                    best_d2 = d2; best_a = a; best_b = b;
                    have_positions = true;
                }
            }
        }
        if (!have_positions) continue;

        kg::KGGluonID gluon = kg_->createKGGluon(entity, best_a, best_b);
        kg::KGGluonData data;
        data.type = kg::KGGluonType::NAIL;
        data.target_distance = std::sqrt(best_d2);
        data.stiffness = stiffness;
        data.breaking_force = stiffness * 0.55f;
        kg_->setKGGluonData(gluon, data);

        // Live on both ends: hand it to the solver now. Otherwise it
        // waits for activation, which for an already-built body never
        // comes again.
        kg::RenderIndex ra = kg_->getRenderIndex(best_a);
        kg::RenderIndex rb = kg_->getRenderIndex(best_b);
        if (physics_ && ra != kg::INVALID_RENDER_INDEX &&
            rb != kg::INVALID_RENDER_INDEX) {
            auto nail = std::make_unique<NailGluon>();
            nail->target_distance = data.target_distance;
            nail->stiffness = data.stiffness;
            nail->damping = data.damping;
            nail->breaking_force = data.breaking_force;
            physics_->add_gluon_between(ra, rb, std::move(nail));
        }
        ++made;
    }
    return made;
}

void EntityPhysicalState::watch(EventBus& bus) {
    bus.state_changes().subscribe(
        [this](const logosphere::ontology::WorldEvent& evt) {
            if (!evt.target_entity_id) return;
            // setProperty carries {property, value, prev}; only a
            // lever is worth re-resolving.
            for (size_t i = 0; i < evt.payload_keys.size() &&
                               i < evt.payload_values.size(); ++i) {
                if (evt.payload_keys[i] != "property") continue;
                if (!is_lever(evt.payload_values[i])) return;
                try {
                    apply(static_cast<kg::EntityID>(
                        std::stoul(*evt.target_entity_id)));
                } catch (...) {}
                return;
            }
        });
}

}  // namespace logosphere
