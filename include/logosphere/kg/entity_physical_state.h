#ifndef ENTITY_PHYSICAL_STATE_H
#define ENTITY_PHYSICAL_STATE_H

#include "logosphere/kg/kg_types.h"

class ParticleSystem;
class PhysicsSystem;
namespace kg { class KGModule; }

namespace logosphere {

class EventBus;

// Ontology levers -> the bodies that carry them.
//
// The knowledge graph is the only surface an agent can reach: it can
// create entities, set properties, and relate them, and nothing else.
// Until something reads those properties back out, they are a record
// of intent rather than a world that changed. This is that reader.
//
// It follows the shape ParticleInteractionSystem already established:
// the KG declares, a system resolves the declaration onto particles,
// and physics reads plain particle fields. Physics never learns that
// an ontology exists.
//
// Two levers today, both previously unreachable from the KG:
//
//   solver_authority   who owns a body's position. The only honest
//                      way to make something immovable. A structure
//                      held up by stillness collapses the moment
//                      something walks on it - is_at_rest is a solver
//                      optimisation, and deliberately has no lever.
//
//   BONDED_TO          cement between two bodies, at bond_strength.
//
// Levers reach every particle the entity owns, following HAS_PART, so
// one setting covers a whole articulated body rather than its hips.
class EntityPhysicalState {
public:
    void initialize(ParticleSystem* particles, PhysicsSystem* physics,
                    kg::KGModule* kg);

    // Resolve every lever this entity declares onto its particles.
    // Safe before activation (writes the KG-stored particle data) and
    // after (writes the live particle). Returns particles touched.
    int apply(kg::EntityID entity);

    // Re-resolve automatically whenever a lever property changes, so
    // an agent's set_property lands in the world immediately rather
    // than at the next materialisation.
    void watch(EventBus& bus);

    // Names an agent may set. Kept here so the watcher and any
    // caller agree on what counts as a lever.
    static bool is_lever(const std::string& property);

private:
    int apply_solver_authority(kg::EntityID entity);
    int apply_bonds(kg::EntityID entity);

    ParticleSystem* particles_ = nullptr;
    PhysicsSystem* physics_ = nullptr;
    kg::KGModule* kg_ = nullptr;
};

}  // namespace logosphere

#endif  // ENTITY_PHYSICAL_STATE_H
