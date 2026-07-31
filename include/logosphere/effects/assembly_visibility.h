#ifndef LOGOSPHERE_EFFECTS_ASSEMBLY_VISIBILITY_H
#define LOGOSPHERE_EFFECTS_ASSEMBLY_VISIBILITY_H

// Assembly visibility — KISS placeholder for the eventual
// transform-based derez/redeploy effect.
//
// HUGE TODO. The Master Control cinematic (logotron / GAME_DESIGN.md
// §20) calls for a Tron-faithful "bike collapses into a glowing
// disk, humanoid lifts disk overhead, disk re-deploys into bike"
// sequence. That needs particle-level animated transforms (parts
// converging to centroid, flattening to a vertical disk facing
// the humanoid). Particle transforms are being designed in a
// parallel branch and aren't ready yet.
//
// To unblock the cinematic shell we ship this stub: hide an
// assembly's particles for the duration of the cutscene by
// zeroing their alpha, snapshot the prior alpha so the redeploy
// restores them exactly. Visually the bike "vanishes" instead of
// derezzing. The Program (humanoid) "pops in" instead of the
// disk-and-lift sequence. Same beats, simpler frames.
//
// When the transforms branch lands, replace `hide_assembly` with
// `derez_to_disk(... vertical_disk_facing_xy)` and
// `restore_assembly` with `redeploy_from_disk(...)`. Same callers,
// same shape — just real visuals.
//
// Generic engine effect. Lives under include/logosphere/effects/
// because any game with a rigid assembly might want to make it
// "go" briefly for a cutscene.

#include "logosphere/kg/kg_types.h"

#include <cstdint>
#include <utility>
#include <vector>

class ParticleSystem;
namespace kg { class KGModule; }

namespace Logosphere::Effects {

// Snapshot returned by hide_assembly; pass back to restore_assembly
// to bring the assembly back exactly as it was. Opaque to callers.
struct AssemblyVisibilitySnapshot {
    // pair: particle_index, prior_alpha
    std::vector<std::pair<int, float>> entries;
};

// Set every particle owned by `entity_id` (via KG's HAS_PART
// relation) to alpha=0. Returns a snapshot the caller passes to
// restore_assembly() later. Snapshot is empty if the entity owns
// no particles. The KG is the source of truth for the entity →
// particle mapping (matches how the rest of the engine finds an
// assembly's particles).
AssemblyVisibilitySnapshot hide_assembly(ParticleSystem& particles,
                                         const kg::KGModule& kg,
                                         kg::EntityID entity_id);

// Restore alphas captured by hide_assembly. Particles that no
// longer exist (post-restructure / GC) are skipped silently.
void restore_assembly(ParticleSystem& particles,
                      const AssemblyVisibilitySnapshot& snapshot);

}  // namespace Logosphere::Effects

#endif  // LOGOSPHERE_EFFECTS_ASSEMBLY_VISIBILITY_H
