#ifndef PHYSICS_FLAGS_H
#define PHYSICS_FLAGS_H

// =========================================================================
// PHYSICS v2 FLAGS - Configuration for clean Newtonian physics
// See docs/PHYSICS_v2.md for design philosophy
//
// MAXIMA DIRECTIVA: Newtonian classical physics only (F=ma, v=at, x=½at²)
//
// IMPORTANT: When changing these flags, rebuild the entire project
// =========================================================================

namespace PhysicsSolver {

    // =========================================================================
    // COLLISION FORENSICS - Track collision detection and resolution
    // Enable logging to debug collision behavior in the 6-phase pipeline
    //
    // ⚠️ WARNING: ENABLE_FORENSIC_LOGGING causes severe performance degradation!
    //    - Generates millions of log lines per second with many particles
    //    - stdout blocking makes engine appear frozen/hanging
    //    - Only enable for isolated debugging of specific particles
    //    - Set FORENSIC_PARTICLE_COUNT to track specific particles, not all
    // =========================================================================
    constexpr bool ENABLE_FORENSIC_LOGGING = false;    // Master switch for all forensic logs (DISABLED - causes severe perf issues)

    // 6-Phase pipeline logging
    constexpr bool LOG_PROPOSED_POSITION = false;       // Log proposed positions (Phase 3)
    constexpr bool LOG_COLLISION_DETECTION = false;     // Log when collisions are detected (Phase 4)
    constexpr bool LOG_COLLISION_RESPONSE = false;      // Log impulse application (Phase 5)
    constexpr bool LOG_COMMIT_POSITION = false;         // Log position updates (Phase 6)

    // =========================================================================
    // TIME SYSTEM FORENSICS - Track delta time flow through the engine
    // Enable logging to debug time_scale and dt=0 issues
    // =========================================================================
    constexpr bool LOG_TIME_SYSTEM = false;             // Log TimeSystem::tick() calls
    constexpr bool LOG_ENGINE_UPDATE_DT = false;        // Log dt passed to Engine::update()

    // =========================================================================
    // FORENSIC PARTICLE TRACKING - Which particles to log
    // =========================================================================
    // Set FORENSIC_PARTICLE_COUNT = 0 to track ALL dynamic particles (mass > 0)
    // Otherwise specify particle IDs in FORENSIC_PARTICLE_IDS array
    constexpr int FORENSIC_PARTICLE_COUNT = 5;          // Track first 5 tree particles
    constexpr int FORENSIC_PARTICLE_IDS[] = {1, 2, 3, 4, 5};  // Trunk + first branches

    // =========================================================================
    // CONTACT DETECTION TOLERANCES
    // =========================================================================
    // Resting contact tolerance - particles within this gap are considered "touching"
    // This handles:
    // - Numerical precision errors
    // - Small transient penetrations from discrete timestep
    // - Floating-point rounding
    // Critical for detecting resting contacts where v_rel = 0 (stacked particles)
    constexpr float RESTING_CONTACT_TOLERANCE = 0.001f;  // 1mm

}

#endif // PHYSICS_FLAGS_H
