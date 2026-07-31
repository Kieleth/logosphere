#pragma once

#include <chrono>
#include <functional>

// GameLoop: Main game loop implementation with timing control
//
// Following principles from successful refactoring:
// - Single responsibility: ONLY manages the game loop timing
// - Clean interface for update/render callbacks
// - No knowledge of engine internals
//
// This was extracted from Engine.cpp where the game loop was mixed
// with engine lifecycle and system management.
//
// IMPORTANT: GameLoop is provided as a convenience for applications,
// but applications are free to implement their own loops.

class GameLoop {
public:
    GameLoop();
    ~GameLoop();
    
    // Core loop control
    void run(std::function<bool()> should_continue,     // Returns false to stop
             std::function<void()> poll_events,          // Platform event polling
             std::function<void(double)> update,         // Update with delta time
             std::function<void()> render,               // Render frame
             std::function<void()> present);             // Present to screen
    
    void stop() { is_running_ = false; }
    bool is_running() const { return is_running_; }
    
    // Timing configuration
    void set_target_fps(int fps);
    void set_vsync(bool enabled) { vsync_enabled_ = enabled; }
    void set_frame_cap(bool enabled) { frame_cap_enabled_ = enabled; }
    
    // Frame timing access
    double get_delta_time() const { return delta_time_; }
    double get_frame_time() const { return frame_time_; }
    double get_current_fps() const { return current_fps_; }
    
    // Performance monitoring
    void set_show_metrics(bool show) { show_metrics_ = show; }
    
private:
    // Timing types
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double>;
    
    // Frame rate regulation
    void regulate_frame_rate();
    void calculate_fps();
    
    // State
    bool is_running_;
    bool vsync_enabled_;
    bool frame_cap_enabled_;
    bool show_metrics_;
    
    // Timing
    int target_fps_;
    double target_frame_time_;
    TimePoint last_frame_time_;
    TimePoint current_frame_time_;
    double delta_time_;
    double frame_time_;
    
    // FPS calculation
    double fps_accumulator_;
    int frame_count_;
    TimePoint fps_update_time_;
    double current_fps_;
};