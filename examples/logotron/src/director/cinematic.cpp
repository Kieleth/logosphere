#include "director/cinematic.h"

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/effects/assembly_visibility.h"
#include "logosphere/kg/kg_module.h"

namespace logotron::director {

namespace {
// Camera dolly tuning. Distance/tilt picked to read as "studio
// inspection shot" — close enough to feel intimate, high enough
// that the floor and Program both fit in the frame.
constexpr float kDollyDistance    = 3.5f;   // metres from focus point
constexpr float kDollyTiltDeg     = 25.0f;  // above horizontal
constexpr float kEngageEaseSecs   = 0.7f;   // §20 phase 2 budget
constexpr float kReleaseEaseSecs  = 0.4f;   // §20 phase 7 wrap

// Where the Program stands relative to the focus point. KISS:
// 0.6 m to the +X side of the bike so the camera (looking from
// roughly the south at 25° tilt) gets both bike footprint and
// Program in frame. Big TODO: orient toward camera once the
// transform branch lands and the disk gets a facing direction.
constexpr float kProgramOffsetX = 0.6f;
constexpr float kProgramOffsetY = 0.0f;
constexpr float kProgramOffsetZ = 0.0f;
}  // namespace

void begin(Engine& engine, kg::KGModule& kg,
           const CinematicInputs& inputs,
           CinematicState& state) {
    if (state.active) return;
    state.active = true;

    // 1. Pause game time. Renderer + camera dolly keep playing on
    //    real time.
    engine.set_cinematic_pause(true);

    // 2. Camera dollies to the focus point.
    engine.get_camera_director().focus_on(
        inputs.center_x, inputs.center_y, inputs.center_z,
        kDollyDistance, kDollyTiltDeg, kEngageEaseSecs);

    // 3. Hide the player's bike (KISS placeholder for derez).
    state.bike_snapshot = Logosphere::Effects::hide_assembly(
        engine.get_particle_system(), kg, inputs.player_bike_entity);

    // 4. Pop the Program in next to the bike. KISS — no rez-in,
    //    no overhead-disk pose.
    state.program = spawn(engine,
                          inputs.center_x + kProgramOffsetX,
                          inputs.center_y + kProgramOffsetY,
                          inputs.center_z + kProgramOffsetZ);
}

void end(Engine& engine, CinematicState& state) {
    if (!state.active) return;
    state.active = false;

    // Reverse order of begin().

    // Despawn Program (KISS — instantaneous; Big TODO: vertical
    // dissolve out).
    despawn(engine, state.program);
    state.program = ProgramActorHandle{};

    // Restore bike alpha (KISS — instantaneous; Big TODO:
    // redeploy-from-disk transform).
    Logosphere::Effects::restore_assembly(
        engine.get_particle_system(), state.bike_snapshot);
    state.bike_snapshot = {};

    // Camera eases back to its prior pose.
    engine.get_camera_director().release(kReleaseEaseSecs);

    // Resume game time. Physics, AI, dynamics start ticking again.
    engine.set_cinematic_pause(false);
}

}  // namespace logotron::director
