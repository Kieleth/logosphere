#pragma once

// =============================================================================
// EXPLOSION DETECTOR — always on, rate-limited, names the body (issue #42)
// =============================================================================
// WHY THIS EXISTS. Two world-scale explosions were found in one week, both by
// a human noticing something on a screen, both loud in the physics from the
// first frame, and one of them no longer reproduces on demand. An intermittent
// blowup that cannot be summoned is exactly what a permanent tripwire is for:
// the engine has every number needed to say "I have exploded" and, until this
// file, said nothing.
//
// WHAT COUNTS AS BANANAS. Three signals, each physical, none game-specific:
//
//   SPEED    a body faster than any legitimate mechanism can make it.
//            Calibration from this engine's own constants: a settled canopy
//            peaks at 1.2 m/s, the solver's Baumgarte ejection is capped at
//            MAX_BIAS_VELOCITY = 4 m/s per contact, a 50 m free fall reaches
//            31 m/s. The default ceiling of 40 m/s clears every one of those
//            and fires only on compounding blowups (the wedged-boulder bug
//            measured 72 m/s).
//   ENERGY   total kinetic energy jumping in one frame. Gravity feeds KE in
//            smoothly; only the solver can create it from nothing. A jump
//            needs BOTH a ratio (default 5x) and an absolute rise (default
//            10 kJ), because a ratio alone fires on the first pebble moving
//            in a quiet world and an absolute alone misses small scenes.
//            This is the signal that catches "many bodies moderately fast",
//            which a per-body ceiling cannot see: 100 floor tiles at 3 m/s
//            is a 100 kJ spike and zero ceiling violations.
//   ESCAPE   a body outside any place the world puts things: far below the
//            turtle (which enforce_turtle_boundary should make impossible,
//            so reaching it means the boundary failed) or absurdly far out.
//
// THE ELEMENT IS NAMED, ALWAYS. Every warning carries the worst body's id,
// speed, position and mass. A max with no owner cannot be sanity-checked;
// that lesson cost a week on S22 and a debugging detour on S23.
//
// RATE-LIMITED. An explosion produces thousands of violations per frame. One
// WARNING per class per ~60 physics frames, carrying the count of everything
// suppressed since the last one, or the log becomes the place the signal
// hides.
//
// ALWAYS COMPILED IN, ON BY DEFAULT. The scan is one pass over particle
// velocities at the end of the physics frame, a few tens of microseconds at
// 19k bodies, below the measurement noise floor (kit S1). Off-switch is
// LOGOSPHERE_EXPLOSION_DETECTOR=0; thresholds via
// LOGOSPHERE_EXPLOSION_SPEED=<m/s>. Instrumentation that must be switched on
// rots; this one is on when nobody remembers it exists, which is the moment
// it is for.
//
// MAIN-THREAD ONLY, fed from the end of PhysicsSystem::update. Particle-free
// on purpose (plain floats), so it lives in logosphere_core next to
// physics_trace and every profile links it.
// =============================================================================

#include <cstdint>

namespace logosphere {
namespace expdet {

bool enabled();                    // env-seeded once; cheap after that
void set_enabled(bool on);         // tests
void set_speed_ceiling(float ms);  // tests; env: LOGOSPHERE_EXPLOSION_SPEED

// One frame = begin, N samples, end. end_frame() evaluates and warns.
void begin_frame();
void sample(int id, float vx, float vy, float vz,
            float mass, float x, float y, float z);
void end_frame();

// Queryable so a test can assert "it fired" / "it stayed silent" instead of
// grepping stdout. Counts are cumulative WARNINGS EMITTED (post rate-limit);
// events_* are raw per-body violations including suppressed ones.
struct Stats {
    uint64_t speed_warnings = 0, energy_warnings = 0, escape_warnings = 0;
    uint64_t speed_events = 0, escape_events = 0;
    float    worst_speed = 0.0f;   // fastest body ever seen, m/s
    int      worst_id = -1;        // and who it was
    double   ke_total = 0.0;       // last completed frame, joules
};
Stats stats();
void  reset();                     // zero everything; tests run back to back

}  // namespace expdet
}  // namespace logosphere
