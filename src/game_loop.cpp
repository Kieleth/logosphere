#include "game_loop.h"
#include "debug_control.h"
#include <thread>
#include <iostream>

GameLoop::GameLoop()
    : is_running_(false)
    , vsync_enabled_(true)
    , frame_cap_enabled_(true)
    , show_metrics_(false)
    , target_fps_(60)
    , target_frame_time_(1.0 / 60.0)
    , delta_time_(0.0)
    , frame_time_(0.0)
    , fps_accumulator_(0.0)
    , frame_count_(0)
    , current_fps_(0.0)
{
}

GameLoop::~GameLoop() {
    // Nothing to clean up
}

void GameLoop::run(std::function<bool()> should_continue,
                   std::function<void()> poll_events,
                   std::function<void(double)> update,
                   std::function<void()> render,
                   std::function<void()> present) {
    
    DEBUG_LOG("GameLoop: Starting main loop...");
    is_running_ = true;
    
    // Initialize timing
    last_frame_time_ = Clock::now();
    fps_update_time_ = last_frame_time_;
    
    while (is_running_ && should_continue()) {
        current_frame_time_ = Clock::now();
        
        // Calculate delta time
        Duration frame_duration = current_frame_time_ - last_frame_time_;
        delta_time_ = frame_duration.count();
        last_frame_time_ = current_frame_time_;
        
        // Cap delta time to prevent huge jumps during debugging
        if (delta_time_ > 0.1) {
            delta_time_ = 0.1;
        }
        
        // Poll events
        if (poll_events) {
            poll_events();
        }
        
        // Update
        if (update) {
            update(delta_time_);
        }
        
        // Render
        if (render) {
            render();
        }
        
        // Present
        if (present) {
            present();
        }
        
        // Calculate frame time
        auto frame_end = Clock::now();
        Duration frame_time_duration = frame_end - current_frame_time_;
        frame_time_ = frame_time_duration.count();
        
        // Frame rate regulation
        if (frame_cap_enabled_ && !vsync_enabled_) {
            regulate_frame_rate();
        }
        
        // FPS calculation
        calculate_fps();
        
        // Performance metrics
        if (show_metrics_) {
            DEBUG_LOG("FPS: " << current_fps_ 
                     << " | Frame: " << (frame_time_ * 1000) << "ms"
                     << " | Delta: " << (delta_time_ * 1000) << "ms");
        }
    }
    
    DEBUG_LOG("GameLoop: Main loop ended.");
    is_running_ = false;
}

void GameLoop::set_target_fps(int fps) {
    target_fps_ = fps;
    target_frame_time_ = 1.0 / fps;
}

void GameLoop::regulate_frame_rate() {
    if (frame_time_ < target_frame_time_) {
        double sleep_time = target_frame_time_ - frame_time_;
        std::this_thread::sleep_for(std::chrono::microseconds(
            static_cast<long>(sleep_time * 1000000)
        ));
    }
}

void GameLoop::calculate_fps() {
    frame_count_++;
    fps_accumulator_ += delta_time_;
    
    // Update FPS every second
    Duration fps_duration = current_frame_time_ - fps_update_time_;
    if (fps_duration.count() >= 1.0) {
        current_fps_ = frame_count_ / fps_accumulator_;
        frame_count_ = 0;
        fps_accumulator_ = 0.0;
        fps_update_time_ = current_frame_time_;
    }
}