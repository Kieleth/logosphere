#ifndef LOGOTRON_DIRECTOR_CINEMATIC_H
#define LOGOTRON_DIRECTOR_CINEMATIC_H

// Master Control cinematic — sequences the engine primitives
// (Engine::set_cinematic_pause, CameraDirector dolly,
// hide_assembly, program_actor) into the show that plays around
// each Director firing. See examples/logotron/GAME_DESIGN.md §20.
//
// KISS for A.5. The full §20 timeline is seven phases; we ship the
// shell with the four that matter and big-TODO the others:
//
//   begin() — pause game time; camera dollies in on the player
//             bike; bike "vanishes" (alpha=0 hide, KISS placeholder
//             for derez-to-disk); a Tron Program (humanoid) pops in
//             at the bike's position.
//   end()   — Program pops out; bike alpha restored; camera
//             releases back to the gameplay framing; game resumes.
//
// Between begin and end, gameplay is paused but the renderer + the
// camera dolly + (later) any cinematic-driven effects keep
// playing. The Director's LLM call runs in this gap; the
// cinematic IS the loading screen.
//
// Not in this KISS pass (Big TODO §20):
//   - Phase 3's derez-to-disk transform (waiting on the parallel
//     transforms branch). Today the bike just goes invisible.
//   - Phase 4's Program "lift the disk overhead" pose (waiting on
//     the same). Today the Program just stands.
//   - Phase 6 per-mutation rez-in plays. Mutations apply
//     synchronously today; visual playback is Phase D of the plan.
//   - Phase 5's minimum-300ms hold for fast LLM responses (the
//     game already gates the apply on kTrailFadeDuration, which
//     gives roughly the same effect today).

#include "director/program_actor.h"
#include "logosphere/effects/assembly_visibility.h"
#include "logosphere/kg/kg_types.h"

class Engine;
namespace kg { class KGModule; }

namespace logotron::director {

// State the cinematic carries between begin and end. Owned by the
// caller (typically the Logotron app), passed back to end().
struct CinematicState {
    bool                                            active = false;
    Logosphere::Effects::AssemblyVisibilitySnapshot bike_snapshot;
    ProgramActorHandle                              program;
};

// Inputs the cinematic needs to compose its beats. Center coords
// are typically the player bike's hips; the cinematic dollies the
// camera to that point and spawns the Program right there.
struct CinematicInputs {
    kg::EntityID player_bike_entity = kg::INVALID_ENTITY;
    float        center_x           = 0.0f;
    float        center_y           = 0.0f;
    float        center_z           = 0.5f;
};

// Begin the show. Pauses, dollies, hides, spawns Program. Safe
// to call when state.active is already true (no-op).
void begin(Engine& engine, kg::KGModule& kg,
           const CinematicInputs& inputs,
           CinematicState& state);

// End the show. Despawns Program, restores bike, releases camera,
// resumes. Safe to call when state.active is false (no-op).
void end(Engine& engine, CinematicState& state);

}  // namespace logotron::director

#endif  // LOGOTRON_DIRECTOR_CINEMATIC_H
