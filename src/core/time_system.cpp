#include "time_system.h"
#include "logosphere/physics/physics_flags.h"  // for forensic logging
#include <algorithm>  // for std::max
#include <iostream>   // for std::cout (Stage 4 debug logging)

TimeSystem::TimeSystem() {
    // All members initialized via initializer list in header
}

void TimeSystem::tick(double real_delta_time) {
    // FORENSICS: Log time system state (timing-based, no frames)
    if constexpr (PhysicsSolver::ENABLE_FORENSIC_LOGGING && PhysicsSolver::LOG_TIME_SYSTEM) {
        std::cout << "[TIME_FORENSICS] "
                  << "real_dt=" << real_delta_time
                  << "s, time_scale=" << time_scale_
                  << ", game_dt=" << (real_delta_time * time_scale_) << "s"
                  << std::endl;
    }

    // Update real time (wall clock)
    real_delta_time_ = real_delta_time;
    total_real_time_ += real_delta_time;

    // Calculate game time (scaled by time_scale)
    game_delta_time_ = real_delta_time * time_scale_;
    total_game_time_ += game_delta_time_;

    // Update FPS
    calculate_fps();
}

void TimeSystem::set_time_scale(float scale) {
    // Clamp to non-negative (negative time doesn't make sense)
    time_scale_ = std::max(0.0f, scale);
}

void TimeSystem::pause() {
    // Save current scale so we can resume
    saved_time_scale_ = time_scale_;
    time_scale_ = 0.0f;
}

void TimeSystem::resume() {
    // Restore previous scale
    time_scale_ = saved_time_scale_;
}

void TimeSystem::reset() {
    // Reset all counters to zero
    real_delta_time_ = 0.0;
    total_real_time_ = 0.0;
    game_delta_time_ = 0.0;
    total_game_time_ = 0.0;
    fps_ = 0.0;
    frame_count_ = 0;
    fps_accumulator_ = 0.0;
    // The physics accumulator is time OWED to the next fixed step.
    // Leaving it set means a reset system resumes part-way through a
    // substep and takes a different number of them on its first frame
    // than a fresh one would, so the same inputs give different physics
    // depending on what ran before the reset. Reset has to mean reset.
    physics_accumulator_ = 0.0;
}

void TimeSystem::calculate_fps() {
    // Update FPS counter every second
    frame_count_++;
    fps_accumulator_ += real_delta_time_;

    if (fps_accumulator_ >= 1.0) {
        // Calculate FPS over the last second
        fps_ = frame_count_ / fps_accumulator_;

        // Reset for next second
        frame_count_ = 0;
        fps_accumulator_ = 0.0;
    }
}

// ============================================================================
// STAGE 2: Fixed Timestep Implementation
// ============================================================================
// This is the "Fix Your Timestep" algorithm - physics ticks at fixed rate
// regardless of rendering framerate.
//
// Key insight: Accumulate frame time, then step physics in fixed chunks.
// Leftover time carries forward to next frame.
//
// Example at 30 FPS (0.033s per frame):
//   Frame 1: accumulator = 0.033s → tick physics twice (2×0.0167s), leftover = 0s
//   Frame 2: accumulator = 0.033s → tick physics twice (2×0.0167s), leftover = 0s
//
// Example at 120 FPS (0.0083s per frame):
//   Frame 1: accumulator = 0.0083s → no physics tick, carry forward
//   Frame 2: accumulator = 0.0166s → no physics tick, carry forward
//   Frame 3: accumulator = 0.025s  → tick physics once, leftover = 0.0083s
// ============================================================================
void TimeSystem::tick_fixed_physics(std::function<void(double)> physics_callback) {
    // Add scaled game time to accumulator (respects pause/slow-motion)
    physics_accumulator_ += game_delta_time_;

    // Spiral of death prevention: if game runs slower than physics can keep up,
    // limit physics steps to avoid infinite loop. Better to run in slow-motion
    // than to freeze entirely.
    const int MAX_PHYSICS_STEPS = 5;
    int steps_taken = 0;

    // DEBUG: Log physics steps (Stage 4 verification)
    static int frame_count_debug = 0;
    bool should_log = (frame_count_debug++ < 5);  // Log first 5 frames

    // Tick physics in fixed PHYSICS_TIMESTEP chunks
    while (physics_accumulator_ >= PHYSICS_TIMESTEP && steps_taken < MAX_PHYSICS_STEPS) {
        // Call physics update with FIXED timestep (always 0.0167s at 60 Hz)
        physics_callback(PHYSICS_TIMESTEP);

        // Remove one physics tick worth of time from accumulator
        physics_accumulator_ -= PHYSICS_TIMESTEP;

        steps_taken++;
    }

    if (should_log && steps_taken > 0) {
        std::cout << "[FIXED_TIMESTEP] Frame game_delta=" << game_delta_time_
                  << "s, physics_steps=" << steps_taken
                  << " (each " << PHYSICS_TIMESTEP << "s), leftover=" << physics_accumulator_ << "s"
                  << std::endl;
    }

    // Calculate interpolation factor for smooth rendering
    // Alpha ranges from 0.0 (just after physics tick) to 1.0 (just before next tick)
    // Renderer can use this to interpolate between current and previous physics state
    //
    // Example: If accumulator = 0.0083s and PHYSICS_TIMESTEP = 0.0167s:
    //   alpha = 0.0083 / 0.0167 = 0.5 (halfway between ticks)
    //   render_pos = lerp(prev_physics_pos, current_physics_pos, 0.5)
    interpolation_alpha_ = physics_accumulator_ / PHYSICS_TIMESTEP;
}
