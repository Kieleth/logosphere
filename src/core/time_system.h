#ifndef TIME_SYSTEM_H
#define TIME_SYSTEM_H

#include <functional>  // for std::function (Stage 2: tick_fixed_physics callback)

/**
 * TimeSystem - Centralized time management for FPS-independent gameplay
 *
 * Separates real time (wall clock, FPS) from game time (simulation).
 * Provides time scaling for pause, slow-motion, fast-forward.
 *
 * Single source of truth for all time queries in the engine.
 */
class TimeSystem {
public:
    TimeSystem();
    ~TimeSystem() = default;

    // === Per-Frame Update ===
    // Called once per frame by Engine::update()
    // Updates all time counters based on real elapsed time
    void tick(double real_delta_time);

    // === Fixed Timestep Update (Stage 2: New method) ===
    // Ticks physics at fixed rate (60 Hz) regardless of framerate
    // Callback will be called 0-N times per frame depending on accumulated time
    // Example: tick_fixed_physics([](double dt) { physics.update(dt); });
    void tick_fixed_physics(std::function<void(double)> physics_callback);

    // === Game Time Queries (affected by time_scale) ===
    // Use these for gameplay, movement, physics, animations
    double get_game_delta_time() const { return game_delta_time_; }
    double get_total_game_time() const { return total_game_time_; }

    // === Real Time Queries (never affected by time_scale) ===
    // Use these for FPS display, profiling, real-world timing
    double get_real_delta_time() const { return real_delta_time_; }
    double get_total_real_time() const { return total_real_time_; }
    double get_fps() const { return fps_; }

    // === Fixed Timestep Queries ===
    // Physics always ticks at this rate (Stage 1: Added, not used yet)
    double get_physics_timestep() const { return PHYSICS_TIMESTEP; }
    double get_interpolation_alpha() const { return interpolation_alpha_; }

    // === Time Control ===
    // Control simulation speed without affecting render rate
    void set_time_scale(float scale);      // 0.0=pause, 0.5=slow, 1.0=normal, 2.0=fast
    float get_time_scale() const { return time_scale_; }

    void pause();                          // Set scale to 0, save previous
    void resume();                         // Restore saved scale
    void reset();                          // Reset all counters to zero

private:
    // Real time tracking (wall clock)
    double real_delta_time_ = 0.0;         // Actual time elapsed this frame
    double total_real_time_ = 0.0;         // Total wall clock time

    // FPS calculation
    double fps_ = 0.0;                     // Current frames per second
    int frame_count_ = 0;                  // Frames since last FPS update
    double fps_accumulator_ = 0.0;         // Time accumulated for FPS calc

    // Game time tracking (affected by time_scale)
    float time_scale_ = 1.0f;              // Time scaling factor
    double game_delta_time_ = 0.0;         // Scaled time for this frame
    double total_game_time_ = 0.0;         // Total scaled simulation time

    // Pause/resume state
    float saved_time_scale_ = 1.0f;        // Previous scale before pause

    // Fixed timestep physics (Stage 1: Data only, no behavior change yet)
    static constexpr double PHYSICS_TIMESTEP = 1.0 / 30.0;  // 30 Hz (0.0333s) - sufficient for narrative RPG
    double physics_accumulator_ = 0.0;     // Time accumulated for next physics tick
    double interpolation_alpha_ = 0.0;     // For smooth rendering (0.0 to 1.0)

    // Internal helpers
    void calculate_fps();
};

#endif // TIME_SYSTEM_H
