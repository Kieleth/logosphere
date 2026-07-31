#ifndef LIGHTING_METRICS_H
#define LIGHTING_METRICS_H

#include <chrono>
#include <cstdio>
#include "optimization_flags.h"

// Global lighting metrics for profiling
// This is a temporary solution for profiling without changing all function signatures
struct LightingMetrics {
    // Timing accumulators (reset each frame)
    double uv_to_world_time = 0.0;      // Time converting UV to 3D position
    double intensity_calc_time = 0.0;    // Time calculating light contribution
    double shadow_ray_time = 0.0;        // Time tracing shadow rays
    double surface_fetch_time = 0.0;     // Time getting surface data
    double tone_mapping_time = 0.0;      // Time in tone mapping
    double color_calc_time = 0.0;        // Time calculating final RGB
    int ray_count = 0;                   // Number of shadow rays cast
    int lighting_calls = 0;               // Number of get_surface_color_at calls
    
    // BVH-specific metrics
    int bvh_rays_traced = 0;             // Number of rays traced through BVH
    int linear_rays_traced = 0;          // Number of rays using linear search
    int bvh_nodes_visited = 0;           // Total BVH nodes visited
    double bvh_build_time = 0.0;         // Time to build/rebuild BVH
    bool bvh_enabled = false;            // Whether BVH is active
    bool bvh_built = false;              // Whether BVH is built
    
    // COMPREHENSIVE BVH METRICS - Added for deep analysis
    int bvh_total_nodes = 0;             // Total nodes in BVH tree
    int bvh_leaf_nodes = 0;               // Number of leaf nodes
    int bvh_max_depth = 0;                // Maximum depth of tree
    float bvh_avg_nodes_per_ray = 0.0f;  // Average nodes visited per ray
    int bvh_aabb_tests = 0;               // Number of ray-AABB tests
    int bvh_aabb_hits = 0;                // Number of AABB hits
    int bvh_leaf_tests = 0;               // Number of leaf node tests
    int bvh_max_stack_depth = 0;          // Maximum traversal stack depth
    int bvh_empty_leaf_hits = 0;          // Leaf nodes with no intersection
    
    // UV Coherence Cache metrics
    uint64_t uv_coherent_hits = 0;       // Adjacent pixels using delta calculation
    uint64_t uv_coherent_misses = 0;     // Pixels needing full calculation
    float uv_coherence_rate = 0.0f;      // Hit rate percentage
    
    // GRANULAR PROFILING: Separate shadow vs lighting timing
    // Only populated when ENABLE_PROFILING && statistical sampling
    double shadow_test_time = 0.0;       // Time in is_ray_blocked (separate from traversal)
    double bvh_traverse_time = 0.0;      // Time in BVH traversal specifically
    double light_calc_time = 0.0;        // Time in calculate_light_contribution
    double surface_transform_time = 0.0; // Time transforming surfaces to world space
    int shadow_rays_blocked = 0;         // How many rays were blocked
    int shadow_rays_clear = 0;           // How many rays were clear
    
    // BATCH PROFILING (statistical sampling, no per-ray timing)
    uint32_t total_batches = 0;          // Total shadow ray batches processed
    uint32_t sampled_batches = 0;        // Number of batches we actually timed
    double batch_time_accumulated = 0.0; // Accumulated time for sampled batches
    uint32_t rays_per_batch_sum = 0;     // Sum of rays in sampled batches
    
    // MEMORY ALLOCATION TRACKING
    uint32_t frame_allocations = 0;      // Number of allocations this frame
    size_t frame_bytes_allocated = 0;    // Bytes allocated this frame
    uint32_t vector_resizes = 0;         // Number of vector reallocations
    
    // SIMD COHERENCE METRICS
    uint32_t simd_packets_processed = 0;  // Total SIMD packets (4 rays each)
    uint32_t simd_rays_coherent[8] = {0}; // Rays still coherent at BVH depth 0-7
    float simd_coherence_rate = 0.0f;     // Average coherence across all levels

    // GPU COMPUTE METRICS (following PERFORMANCE_RESEARCH.md principles)
    uint32_t gpu_dispatches = 0;          // Number of GPU kernel dispatches (count only, no timing)
    uint32_t gpu_rays_dispatched = 0;     // Total rays sent to GPU
    double gpu_kernel_time = 0.0;         // GPU kernel execution time (from Metal hardware timestamps)
    double gpu_total_time = 0.0;          // Total GPU time including overhead (from Metal timestamps)
    double gpu_upload_bytes = 0.0;        // Bytes uploaded to GPU (BVH + rays + triangles)
    double gpu_download_bytes = 0.0;      // Bytes downloaded from GPU (results)
    
    // Frame-level profiling control
    uint32_t frame_number = 0;
    bool should_profile = false;

    // PIPELINE PHASE TIMING (high-level breakdown)
    double binning_time = 0.0;               // Time binning surfaces to tiles
    double rasterization_time = 0.0;         // Time rasterizing pixels (tiles)
    double lighting_calculation_time = 0.0;  // Time applying lighting to pixels
    
    // High-resolution timing helpers
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<double, std::milli>;
    
    // Reset per-frame counters but keep timing breakdown for UI display
    void reset() {
        // Increment frame counter
        frame_number++;
        
        // Determine if we should profile this frame (statistical sampling)
        should_profile = Optimizations::ENABLE_PROFILING && 
                        Optimizations::PROFILE_LEVEL >= Optimizations::PROFILE_SYSTEMS &&
                        (frame_number % Optimizations::PROFILE_SAMPLE_RATE == 0);
        
        // Reset only per-frame counters
        ray_count = 0;
        lighting_calls = 0;
        // Reset BVH metrics (but not enabled/built flags)
        bvh_rays_traced = 0;
        linear_rays_traced = 0;
        bvh_nodes_visited = 0;
        bvh_aabb_tests = 0;
        bvh_aabb_hits = 0;
        bvh_leaf_tests = 0;
        bvh_empty_leaf_hits = 0;
        // Don't reset tree structure metrics (total_nodes, depth, etc)
        // Reset UV coherence metrics
        uv_coherent_hits = 0;
        uv_coherent_misses = 0;
        
        // Reset granular profiling counters
        shadow_rays_blocked = 0;
        shadow_rays_clear = 0;
        
        // Reset batch metrics
        total_batches = 0;
        sampled_batches = 0;
        batch_time_accumulated = 0.0;
        rays_per_batch_sum = 0;
        
        // Reset memory tracking
        frame_allocations = 0;
        frame_bytes_allocated = 0;
        vector_resizes = 0;
        
        // Reset SIMD coherence
        simd_packets_processed = 0;
        for (int i = 0; i < 8; i++) simd_rays_coherent[i] = 0;

        // Reset GPU metrics (counts always, timing only when profiling)
        gpu_dispatches = 0;
        gpu_rays_dispatched = 0;
        gpu_upload_bytes = 0.0;
        gpu_download_bytes = 0.0;

        // Only reset timing if we're about to profile
        if (should_profile) {
            gpu_kernel_time = 0.0;
            gpu_total_time = 0.0;
            shadow_test_time = 0.0;
            bvh_traverse_time = 0.0;
            light_calc_time = 0.0;
            surface_transform_time = 0.0;
            // Also reset the old timings for fresh measurement
            uv_to_world_time = 0.0;
            intensity_calc_time = 0.0;
            shadow_ray_time = 0.0;
            surface_fetch_time = 0.0;
            tone_mapping_time = 0.0;
            color_calc_time = 0.0;
            // Reset pipeline phase timings
            binning_time = 0.0;
            rasterization_time = 0.0;
            lighting_calculation_time = 0.0;
        }
    }
    
    // Print GPU-specific profiling report (when GPU rendering path is active)
    void print_gpu_report() const {
        if (!should_profile || !Optimizations::ENABLE_PROFILING) return;
        if (!Optimizations::USE_GPU_RASTERIZATION) return;  // Sanity check

        printf("\n[GPU PROFILING] Frame %u:\n", frame_number);
        printf("  GPU Dispatches:\n");
        printf("    Count: %u dispatches\n", gpu_dispatches);
        printf("    Rays: %u total\n", gpu_rays_dispatched);
        if (gpu_dispatches > 0) {
            printf("    Avg rays/dispatch: %.1f\n",
                   gpu_rays_dispatched / (float)gpu_dispatches);
        }

        // Note: Semaphore wait timing logged separately in metal_compute_bridge.mm
        // Note: Tile binning timing logged separately in render_pipeline.cpp
        // Note: GPU submit timing logged separately in render_pipeline.cpp

        printf("  Data Transfer:\n");
        printf("    Upload: %.1f KB\n", gpu_upload_bytes / 1024.0);
        printf("    Download: %.1f KB\n", gpu_download_bytes / 1024.0);
    }

    // Print granular profiling report (CPU lighting path only)
    void print_granular_report() const {
        if (!should_profile || !Optimizations::ENABLE_PROFILING) return;

        // When GPU rendering is active, print GPU report instead
        if (Optimizations::USE_GPU_RASTERIZATION) {
            print_gpu_report();
            return;
        }

        // CPU LIGHTING PATH - Only relevant when GPU rendering is disabled
        // Adjust for parallel thread accumulation
        // When USE_PARALLEL_TILES is on, timing accumulates across worker threads + main thread
        float thread_divisor = 1.0f;
        if (Optimizations::USE_PARALLEL_TILES && !Optimizations::MAIN_THREAD_NO_RENDER) {
            thread_divisor = Optimizations::WORKER_THREAD_COUNT + 1;  // Workers + main thread
        } else if (Optimizations::USE_PARALLEL_TILES) {
            thread_divisor = Optimizations::WORKER_THREAD_COUNT;      // Workers only
        }

        printf("\n[GRANULAR LIGHTING PROFILING - CPU PATH] Frame %u:\n", frame_number);
        printf("  Pipeline Phases:\n");
        printf("    Binning: %.2fms\n", binning_time);
        printf("    Rasterization: %.2fms\n", rasterization_time);
        printf("    Lighting calculation: %.2fms\n", lighting_calculation_time);
        double total_pipeline = binning_time + rasterization_time + lighting_calculation_time;
        printf("    Total: %.2fms\n", total_pipeline);
        printf("  Shadow Testing:\n");
        printf("    Total time: %.2fms (wall clock: %.2fms)\n", 
               shadow_test_time, shadow_test_time / thread_divisor);
        printf("    BVH traversal: %.2fms\n", bvh_traverse_time);
        printf("    Rays: %d tested, %d blocked (%.1f%%)\n", 
               shadow_rays_blocked + shadow_rays_clear,
               shadow_rays_blocked,
               (shadow_rays_blocked + shadow_rays_clear) > 0 ?
                   100.0f * shadow_rays_blocked / (shadow_rays_blocked + shadow_rays_clear) : 0.0f);
        
        // BVH Deep Analysis
        if (bvh_rays_traced > 0) {
            printf("  BVH Analysis:\n");
            printf("    Tree: %d nodes (%d leaves), depth %d\n", 
                   bvh_total_nodes, bvh_leaf_nodes, bvh_max_depth);
            printf("    Traversal: %.1f nodes/ray average\n", 
                   bvh_nodes_visited / (float)bvh_rays_traced);
            printf("    AABB tests: %d total, %d hits (%.1f%% hit rate)\n",
                   bvh_aabb_tests, bvh_aabb_hits,
                   bvh_aabb_tests > 0 ? 100.0f * bvh_aabb_hits / bvh_aabb_tests : 0.0f);
            printf("    Leaf tests: %d, empty: %d\n", bvh_leaf_tests, bvh_empty_leaf_hits);
            printf("    Time per ray: %.2f ns\n", 
                   bvh_traverse_time * 1000000.0 / bvh_rays_traced);
        }
        
        printf("  Lighting Calculation:\n");
        printf("    Light contribution: %.2fms\n", light_calc_time);
        printf("    Surface transform: %.2fms\n", surface_transform_time);
        printf("    UV→World: %.2fms\n", uv_to_world_time);
        printf("  Total Rasterization Breakdown:\n");
        // Use wall clock time for percentages to avoid impossible math
        float adjusted_shadow_time = shadow_test_time / thread_divisor;
        float total_lighting_time = adjusted_shadow_time + light_calc_time;
        printf("    Shadow rays: %.2fms (%.1f%%)\n",
               adjusted_shadow_time,
               total_lighting_time > 0 ?
                   100.0f * adjusted_shadow_time / total_lighting_time : 0.0f);
        printf("    Lighting: %.2fms (%.1f%%)\n",
               light_calc_time,
               total_lighting_time > 0 ?
                   100.0f * light_calc_time / total_lighting_time : 0.0f);

        // GPU metrics (if GPU is active)
        if (gpu_dispatches > 0) {
            printf("  GPU Compute:\n");
            printf("    Dispatches: %u (%u rays total)\n", gpu_dispatches, gpu_rays_dispatched);
            printf("    GPU kernel time: %.2fms\n", gpu_kernel_time);
            printf("    Upload: %.1f KB, Download: %.1f KB\n",
                   gpu_upload_bytes / 1024.0, gpu_download_bytes / 1024.0);
            if (gpu_rays_dispatched > 0) {
                printf("    Time per ray: %.2f ns\n",
                       gpu_kernel_time * 1000000.0 / gpu_rays_dispatched);
            }
        }
    }
    
    // Singleton access
    static LightingMetrics& get() {
        static LightingMetrics instance;
        return instance;
    }
};

// RAII timer for automatic timing - ONLY active during profiling frames
class ScopedTimer {
public:
    ScopedTimer(double& accumulator) 
        : accumulator_(accumulator)
        , active_(LightingMetrics::get().should_profile) {
        if (active_) {
            start_ = LightingMetrics::Clock::now();
        }
    }
    
    ~ScopedTimer() {
        if (active_) {
            auto end = LightingMetrics::Clock::now();
            accumulator_ += LightingMetrics::Duration(end - start_).count();
        }
    }
    
private:
    double& accumulator_;
    LightingMetrics::TimePoint start_;
    bool active_;
};

// =========================================================================
// STATISTICAL BATCH PROFILER
// Following PERFORMANCE_RESEARCH.md: Sample batches, not individual operations
// No timing in hot paths - only sample 1% of batches
// =========================================================================
class BatchProfiler {
public:
    // Profile a batch of shadow rays with statistical sampling
    // NO CLOCK in hot path unless we're sampling this batch
    template<typename Func>
    static void ProfileBatch(size_t ray_count, Func&& process_batch) {
        auto& metrics = LightingMetrics::get();
        metrics.total_batches++;
        
        // Only time if we're in BATCH profiling level AND this is a sample
        if (Optimizations::PROFILE_LEVEL >= Optimizations::PROFILE_BATCHES &&
            metrics.total_batches % Optimizations::BATCH_SAMPLE_RATE == 0) {
            
            // This batch is being sampled - time it
            auto start = LightingMetrics::Clock::now();
            process_batch();
            auto end = LightingMetrics::Clock::now();
            
            metrics.sampled_batches++;
            metrics.batch_time_accumulated += LightingMetrics::Duration(end - start).count();
            metrics.rays_per_batch_sum += ray_count;
        } else {
            // Not sampling - just run without timing
            process_batch();
        }
    }
    
    // Track memory allocations (cheap counter, no timing)
    static void TrackAllocation(size_t bytes) {
        if (Optimizations::TRACK_MEMORY_ALLOCS) {
            auto& metrics = LightingMetrics::get();
            metrics.frame_allocations++;
            metrics.frame_bytes_allocated += bytes;
        }
    }
    
    // Track SIMD coherence at different BVH levels
    static void TrackCoherence(int bvh_depth, int rays_still_coherent) {
        if (Optimizations::TRACK_SIMD_COHERENCE && bvh_depth < 8) {
            auto& metrics = LightingMetrics::get();
            metrics.simd_rays_coherent[bvh_depth] += rays_still_coherent;
            if (bvh_depth == 0) {
                metrics.simd_packets_processed++;
            }
        }
    }
    
    // Get average batch time (only from sampled batches)
    static double GetAverageBatchTime() {
        auto& metrics = LightingMetrics::get();
        if (metrics.sampled_batches > 0) {
            return metrics.batch_time_accumulated / metrics.sampled_batches;
        }
        return 0.0;
    }
    
    // Get average rays per batch
    static double GetAverageRaysPerBatch() {
        auto& metrics = LightingMetrics::get();
        if (metrics.sampled_batches > 0) {
            return static_cast<double>(metrics.rays_per_batch_sum) / metrics.sampled_batches;
        }
        return 0.0;
    }
};

// =========================================================================
// PROFILING MACROS
// These compile to nothing when ENABLE_PROFILING is false
// For hot path use - zero overhead in production
// NOTE: Using if constexpr for compile-time optimization
// =========================================================================

// Define a preprocessor constant based on the constexpr value
// This is evaluated at compile time
#if 1  // Change to 0 to disable all profiling at compile time

    // Timing macros - automatically sample-controlled via should_profile
    // Note: These create scoped timers that last for the function scope
    #define PROFILE_SHADOW_TEST() \
        ScopedTimer _shadow_timer(LightingMetrics::get().shadow_test_time)
    #define PROFILE_BVH_TRAVERSE() \
        ScopedTimer _bvh_timer(LightingMetrics::get().bvh_traverse_time)
    #define PROFILE_LIGHT_CALC() \
        ScopedTimer _light_timer(LightingMetrics::get().light_calc_time)
    #define PROFILE_SURFACE_TRANSFORM() \
        ScopedTimer _surface_timer(LightingMetrics::get().surface_transform_time)
    
    // Counting macros - always active when profiling enabled (cheap increments)
    #define COUNT_SHADOW_RAY() LightingMetrics::get().ray_count++
    #define COUNT_SHADOW_BLOCKED() LightingMetrics::get().shadow_rays_blocked++
    #define COUNT_SHADOW_CLEAR() LightingMetrics::get().shadow_rays_clear++
    #define COUNT_BVH_NODE() LightingMetrics::get().bvh_nodes_visited++
    #define COUNT_LIGHTING_CALL() LightingMetrics::get().lighting_calls++

#else

    // All macros compile to nothing in release builds
    #define PROFILE_SHADOW_TEST()
    #define PROFILE_BVH_TRAVERSE()
    #define PROFILE_LIGHT_CALC()
    #define PROFILE_SURFACE_TRANSFORM()
    
    #define COUNT_SHADOW_RAY()
    #define COUNT_SHADOW_BLOCKED()
    #define COUNT_SHADOW_CLEAR()
    #define COUNT_BVH_NODE()
    #define COUNT_LIGHTING_CALL()

#endif

#endif // LIGHTING_METRICS_H