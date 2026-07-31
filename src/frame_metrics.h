#ifndef FRAME_METRICS_H
#define FRAME_METRICS_H

#include <cstdint>
#include <chrono>
#include <iostream>

// Frame-based metrics accumulation
// Count operations during the frame, measure timing only at frame boundaries
// This gives us performance data with minimal overhead

class FrameMetrics {
public:
    // Operation counters (cheap - just increments)
    uint32_t lighting_calls = 0;
    uint32_t shadow_rays = 0;
    uint32_t bvh_traversals = 0;
    uint32_t pixels_processed = 0;
    uint32_t surfaces_rendered = 0;
    uint32_t particles_rendered = 0;
    
    // Frame timing (measured once per frame)
    double frame_time_ms = 0.0;
    double render_time_ms = 0.0;
    double lighting_time_ms = 0.0;
    double physics_time_ms = 0.0;
    
    // Frame counter for sampling
    uint32_t frame_number = 0;
    
    // Sampling rate - how often to report metrics
    static constexpr uint32_t REPORT_INTERVAL = 60;  // Every 60 frames (1 second at 60fps)
    
    // Averaged metrics for UI display (updated every REPORT_INTERVAL)
    struct DisplayMetrics {
        double avg_frame_time = 0.0;
        double avg_render_time = 0.0;
        double avg_lighting_time = 0.0;
        double fps = 0.0;
        uint32_t avg_lighting_calls = 0;
        uint32_t avg_shadow_rays = 0;
        uint32_t avg_pixels = 0;
        double lighting_percent = 0.0;
        double render_percent = 0.0;
        double us_per_lighting = 0.0;
    } display;
    
private:
    // Accumulated totals for averaging
    uint64_t total_lighting_calls = 0;
    uint64_t total_shadow_rays = 0;
    uint64_t total_bvh_traversals = 0;
    uint64_t total_pixels = 0;
    
    double total_frame_time = 0.0;
    double total_render_time = 0.0;
    double total_lighting_time = 0.0;
    
    // Timing helpers
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    
    TimePoint frame_start_time;
    TimePoint render_start_time;
    TimePoint lighting_start_time;
    
public:
    // Singleton access
    static FrameMetrics& Get() {
        static FrameMetrics instance;
        return instance;
    }
    
    // Frame lifecycle
    void StartFrame() {
        frame_start_time = Clock::now();
        
        // Reset per-frame counters
        lighting_calls = 0;
        shadow_rays = 0;
        bvh_traversals = 0;
        pixels_processed = 0;
        surfaces_rendered = 0;
        particles_rendered = 0;
    }
    
    void EndFrame() {
        auto frame_end = Clock::now();
        frame_time_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start_time).count();
        
        // Accumulate totals
        total_frame_time += frame_time_ms;
        total_lighting_calls += lighting_calls;
        total_shadow_rays += shadow_rays;
        total_bvh_traversals += bvh_traversals;
        total_pixels += pixels_processed;
        
        frame_number++;
        
        // Report periodically
        if (frame_number % REPORT_INTERVAL == 0) {
            ReportMetrics();
        }
    }
    
    // Subsystem timing helpers
    void StartRender() {
        render_start_time = Clock::now();
    }
    
    void EndRender() {
        auto render_end = Clock::now();
        render_time_ms = std::chrono::duration<double, std::milli>(render_end - render_start_time).count();
        total_render_time += render_time_ms;
    }
    
    void StartLighting() {
        lighting_start_time = Clock::now();
    }
    
    void EndLighting() {
        auto lighting_end = Clock::now();
        lighting_time_ms = std::chrono::duration<double, std::milli>(lighting_end - lighting_start_time).count();
        total_lighting_time += lighting_time_ms;
    }
    
    // Report accumulated metrics
    void ReportMetrics() {
        if (frame_number == 0) return;
        
        // Calculate averages
        display.avg_frame_time = total_frame_time / REPORT_INTERVAL;
        display.avg_render_time = total_render_time / REPORT_INTERVAL;
        display.avg_lighting_time = total_lighting_time / REPORT_INTERVAL;
        display.fps = 1000.0 / display.avg_frame_time;
        
        display.avg_lighting_calls = total_lighting_calls / REPORT_INTERVAL;
        display.avg_shadow_rays = total_shadow_rays / REPORT_INTERVAL;
        display.avg_pixels = total_pixels / REPORT_INTERVAL;
        
        // Calculate percentages
        if (display.avg_frame_time > 0) {
            display.render_percent = (display.avg_render_time / display.avg_frame_time) * 100.0;
            display.lighting_percent = (display.avg_lighting_time / display.avg_frame_time) * 100.0;
        }
        
        // Calculate per-operation timing
        if (display.avg_lighting_calls > 0) {
            display.us_per_lighting = (display.avg_lighting_time * 1000.0) / display.avg_lighting_calls;
        }
        
        // Console output removed - metrics are shown in UI instead
        
        // Reset accumulators for next interval
        total_frame_time = 0.0;
        total_render_time = 0.0;
        total_lighting_time = 0.0;
        total_lighting_calls = 0;
        total_shadow_rays = 0;
        total_bvh_traversals = 0;
        total_pixels = 0;
    }
    
    // Quick inline counters (very low overhead)
    inline void CountLightingCall() { lighting_calls++; }
    inline void CountShadowRay() { shadow_rays++; }
    inline void CountBVHTraversal() { bvh_traversals++; }
    inline void CountPixel() { pixels_processed++; }
    inline void CountSurface() { surfaces_rendered++; }
    inline void CountParticle() { particles_rendered++; }
};

// Convenience macros for minimal typing
#define FRAME_COUNT_LIGHTING FrameMetrics::Get().CountLightingCall()
#define FRAME_COUNT_SHADOW_RAY FrameMetrics::Get().CountShadowRay()
#define FRAME_COUNT_BVH FrameMetrics::Get().CountBVHTraversal()
#define FRAME_COUNT_PIXEL FrameMetrics::Get().CountPixel()

#endif // FRAME_METRICS_H