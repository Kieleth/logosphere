#ifndef GAME_TIME_H
#define GAME_TIME_H

#include <string>
#include <vector>
#include <cstdint>

// Timeline identifier for multiverse/branching support
using timeline_id = uint64_t;

// GameTime - Central time authority for the entire game
//
// Design: Static singleton providing single source of truth for time
// All systems query GameTime for current time, ensuring synchronization
// Supports time scale, time travel, and timeline branching
//
// Usage:
//   GameTime::advance(delta_seconds);           // Main loop advances time
//   double now = GameTime::get_current_time();  // Systems query time
//   GameTime::set_time_scale(2.0);              // Fast-forward (2x speed)
//   GameTime::jump_to(future_time);             // Time travel
class GameTime {
public:
    // === Time Queries ===

    // Get current game time (seconds since world creation)
    static double get_current_time();

    // Get current timeline (for branching/multiverse)
    static timeline_id get_current_timeline();

    // Get time scale multiplier (1.0 = normal, 2.0 = 2x speed, 0.0 = paused)
    static double get_time_scale();

    // === Time Manipulation ===

    // Advance time by delta (called from main loop)
    // delta is real-world seconds, scaled by time_scale
    static void advance(double delta_seconds);

    // Set time directly (for save/load or time travel)
    // If target_time < current_time, triggers timeline branching (if enabled)
    static void set_time(double target_time);

    // Jump to specific time (time travel)
    // Alias for set_time, more explicit for gameplay usage
    static void jump_to(double target_time);

    // Set time scale (0.0 = paused, 1.0 = normal, 2.0 = 2x speed)
    static void set_time_scale(double scale);

    // Pause time (equivalent to set_time_scale(0.0))
    static void pause();

    // Resume time (equivalent to set_time_scale(1.0))
    static void resume();

    // === Timeline Management (for multiverse/branching) ===

    // Create new timeline branch (for time travel paradoxes)
    // Returns new timeline_id
    static timeline_id create_timeline_branch(timeline_id parent_timeline,
                                               double branch_time,
                                               const std::string& reason);

    // Switch to different timeline
    static void set_timeline(timeline_id timeline);

    // Get parent timeline (returns 0 for root timeline)
    static timeline_id get_parent_timeline(timeline_id timeline);

    // Get time when timeline branched from parent
    static double get_branch_time(timeline_id timeline);

    // Get all available timelines
    static std::vector<timeline_id> get_all_timelines();

    // === Calendar Functions ===

    // Convert time to game calendar units
    static int get_day(double time);        // Day number since world creation
    static int get_year(double time);       // Year number since world creation
    static int get_season(double time);     // 0=spring, 1=summer, 2=autumn, 3=winter
    static double get_day_fraction(double time);  // 0.0=midnight, 0.5=noon, 1.0=midnight

    // Calendar constants (configurable)
    static constexpr double SECONDS_PER_DAY = 86400.0;     // 24 hours
    static constexpr double SECONDS_PER_YEAR = 31536000.0; // 365 days
    static constexpr int DAYS_PER_YEAR = 365;

    // === Initialization ===

    // Initialize GameTime system (called once at engine startup)
    static void initialize(double initial_time = 0.0);

    // Reset to initial state (for testing)
    static void reset();

private:
    // Static state (single source of truth)
    static double current_time_;        // Seconds since world creation
    static double time_scale_;          // Time multiplier (1.0 = normal)
    static timeline_id current_timeline_;  // Active timeline ID
    static timeline_id next_timeline_id_;  // Counter for generating timeline IDs

    // Timeline metadata
    struct TimelineInfo {
        timeline_id id;
        timeline_id parent_id;
        double branch_time;
        std::string branch_reason;
    };
    static std::vector<TimelineInfo> timelines_;
};

#endif // GAME_TIME_H
