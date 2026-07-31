#ifndef PARTICLE_H
#define PARTICLE_H

// Facade header — particle.h has been split into focused headers so consumers
// that only need a subset don't transitively pull the rest. New code should
// include the specific header it needs; this facade exists so the ~200 files
// already including particle.h keep working.
//
//   particle_types.h    — OdorType, ParticleShape, ParticleOwner enums
//   particle_core.h     — Particle struct
//   particle_memory.h   — MemoryParticle, MemoryLayer
//   particle_vision.h   — ViewPlane
//   particle_lighting.h — LightRay, SurfaceLighting, ParticleLighting

#include "particle_types.h"
#include "particle_core.h"
#include "particle_memory.h"
#include "particle_vision.h"
#include "particle_lighting.h"

#endif  // PARTICLE_H
