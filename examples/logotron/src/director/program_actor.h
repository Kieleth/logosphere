#ifndef LOGOTRON_DIRECTOR_PROGRAM_ACTOR_H
#define LOGOTRON_DIRECTOR_PROGRAM_ACTOR_H

// program_actor — the Tron "Program" humanoid that pops in next to
// the player's bike during the Master Control pause cinematic.
//
// Logotron-side. Composes the engine's HumanoidGenerator + KG
// destroy primitives. Game-specific characterisation: the Program
// is a Tron-vernacular Program (humanoid form, glowing
// circuitry-style colors). When transforms ship the spawn will
// gain a vertical-dissolve rez-in and a "lift the disk overhead"
// pose; right now it just pops into existence and pops out again.
// Big TODO marker on both ends.
//
// Usage from cinematic state machine:
//   auto handle = ProgramActor::spawn(engine, x, y, z);
//   ... hold while LLM thinks ...
//   ProgramActor::despawn(engine, handle);

#include "logosphere/kg/kg_types.h"

class Engine;

namespace logotron::director {

struct ProgramActorHandle {
    kg::EntityID root_entity = kg::INVALID_ENTITY;
    bool valid() const { return root_entity != kg::INVALID_ENTITY; }
};

// Spawn the Program at (x, y, z). Returns a handle the caller
// keeps to despawn later. KISS: instantaneous appearance.
// Big TODO: vertical rez-in dissolve (pixels-from-floor sweep)
// once the transforms branch lands. Big TODO: "lift disk
// overhead" pose for the held disk.
ProgramActorHandle spawn(Engine& engine, float x, float y, float z);

// Despawn the Program. Wipes every particle owned by the rig
// (recursive HAS_PART traversal) and destroys the body part
// entities. KISS: instantaneous disappearance. Big TODO: vertical
// dissolve.
void despawn(Engine& engine, ProgramActorHandle handle);

} // namespace logotron::director

#endif  // LOGOTRON_DIRECTOR_PROGRAM_ACTOR_H
