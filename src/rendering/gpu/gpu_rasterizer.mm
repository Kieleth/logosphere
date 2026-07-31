// gpu_rasterizer.mm
// Metal GPU rasterization implementation
//
// IMPLEMENTATION NOTES:
// - .mm extension required for Objective-C++ (Metal API is Objective-C)
// - Follows MetalComputeBridge pattern (see metal_compute_bridge.mm)
// - Uses Metal Compute (not render pipeline) for flexibility
// - Matches CPU PixelBuffer format: uint32_t BGRA
//
// PHASE III: Full GPU Rasterization - STEP 1

#include "logosphere/rendering/gpu/gpu_rasterizer.h"
#include <iomanip>
#include "../../particle_geometry_v2.h"  // For Surface struct
#include "../../core/camera_system.h"    // For projection
#include "../../optimization_flags.h"     // For GPU thread group configuration
#include "logosphere/rendering/gpu/metal_compute_bridge.h"        // For LightData struct
#include "logosphere/rendering/vision_memory.h"  // CPU helper for the memory grid
#include "metallib_locator.h"
#include "../../core/telemetry.h"
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <vector>                         // For std::vector
#include <cmath>                          // For std::sqrt
#include <iostream>                       // For std::cerr
#include <iomanip>                        // For std::setprecision
#include <cstdlib>                        // For std::exit
#include <numeric>                        // For std::accumulate
#include <memory>                         // For std::shared_ptr

// Set to 1 for PHASE1_DEBUG shadow buffer diagnostic output
#define GPU_PHASE1_DEBUG 0

// ---------------------------------------------------------------------------
// Metal command buffer helpers: enhanced error reporting + diagnostics
// ---------------------------------------------------------------------------

// Create a command buffer with encoder-level error reporting (macOS 11+).
// On GPU error, this populates NSError.userInfo[MTLCommandBufferEncoderInfoErrorKey]
// with per-encoder fault info, making GPU crashes diagnosable.
static id<MTLCommandBuffer> createTrackedCommandBuffer(id<MTLCommandQueue> queue) {
    MTLCommandBufferDescriptor *desc = [[MTLCommandBufferDescriptor alloc] init];
    desc.errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus;
    id<MTLCommandBuffer> cmd = [queue commandBufferWithDescriptor:desc];
    // Every command buffer in the engine is born here, so hooking the factory
    // is the only way to measure GPU busy vs GPU span without missing one.
    // Named per-pass stages cannot answer it: their durations are not additive.
    const uint64_t tel_frame = ::logosphere::telemetry::frame_index();
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        ::logosphere::telemetry::record_gpu_window(
            tel_frame, cb.GPUStartTime, cb.GPUEndTime);
    }];
    return cmd;
}

// Log detailed error info from a failed command buffer, including per-encoder
// execution status when available (requires createTrackedCommandBuffer).
static void logCommandBufferError(id<MTLCommandBuffer> cb, const char* pass_name,
                                   std::atomic<bool>* device_lost = nullptr) {
    if (cb.status != MTLCommandBufferStatusError) return;

    NSError *error = cb.error;
    std::cerr << "[GPU_ERROR] " << pass_name << " failed: "
              << [[error localizedDescription] UTF8String]
              << " (code=" << error.code << ")" << std::endl;

    // Categorize the error for operational response
    switch (error.code) {
        case MTLCommandBufferErrorTimeout:  // code 2
            std::cerr << "[GPU_ERROR] " << pass_name
                      << ": GPU watchdog timeout (~2-5s). Shader too complex or infinite loop?"
                      << std::endl;
            break;
        case MTLCommandBufferErrorAccessRevoked:  // code 4
            std::cerr << "[GPU_ERROR] " << pass_name
                      << ": GPU access REVOKED. Device lost — cannot recover without re-init."
                      << std::endl;
            if (device_lost) device_lost->store(true, std::memory_order_relaxed);
            break;
        case MTLCommandBufferErrorOutOfMemory:  // code 8
            std::cerr << "[GPU_ERROR] " << pass_name
                      << ": GPU out of memory."
                      << std::endl;
            break;
        default:
            break;
    }

    // Dump encoder-level execution status (available with ErrorOptionEncoderExecutionStatus)
    NSArray *encoderInfos = error.userInfo[MTLCommandBufferEncoderInfoErrorKey];
    if (encoderInfos) {
        for (id<MTLCommandBufferEncoderInfo> info in encoderInfos) {
            const char* status_str = "unknown";
            switch (info.errorState) {
                case MTLCommandEncoderErrorStateCompleted: status_str = "completed"; break;
                case MTLCommandEncoderErrorStateAffected:  status_str = "affected"; break;
                case MTLCommandEncoderErrorStateFaulted:   status_str = "FAULTED"; break;
                case MTLCommandEncoderErrorStatePending:   status_str = "pending"; break;
                case MTLCommandEncoderErrorStateUnknown:   status_str = "unknown"; break;
            }
            std::cerr << "[GPU_ERROR]   Encoder '" << [info.label UTF8String]
                      << "': " << status_str << std::endl;
        }
    }
}

namespace Logosphere {

// Global Metal allocation query for memory leak tests
static void* g_metal_device = nullptr;

size_t get_metal_allocated_bytes() {
    if (!g_metal_device) return 0;
    id<MTLDevice> dev = (__bridge id<MTLDevice>)g_metal_device;
    return dev.currentAllocatedSize;
}

GPURasterizer::GPURasterizer()
    : device_(nullptr)
    , command_queue_(nullptr)
    , compute_pipeline_minimal_(nullptr)
    , compute_pipeline_triangle_(nullptr)
    , compute_pipeline_barycentric_(nullptr)
    , compute_pipeline_with_depth_(nullptr)
    , library_(nullptr)
    , framebuffer_buffer_(nullptr), framebuffer_capacity_(0)
    , depth_buffer_(nullptr), depth_capacity_(0)
    , triangles_buffer_(nullptr), triangles_capacity_(0)
    , lights_buffer_(nullptr), lights_capacity_(0)
    , bvh_nodes_buffer_(nullptr), bvh_nodes_capacity_(0)
    , bvh_triangles_buffer_(nullptr), bvh_triangles_capacity_(0)
    , buffer_semaphore_(nullptr)
    , current_buffer_index_(0)
    , width_(0)
    , height_(0)
    , initialized_(false)
    , timestamp_counter_set_(nullptr)
    , performance_counter_set_(nullptr)
    , counter_sample_buffer_(nullptr)
    , debug_rays_traced_buffer_(nullptr)
    , debug_bvh_nodes_visited_buffer_(nullptr)
    , debug_triangles_tested_buffer_(nullptr)
    , acceleration_structure_(nullptr)
    , accel_scratch_buffer_(nullptr)
    , compute_pipeline_shadows_rt_(nullptr)
    , compute_pipeline_ssao_(nullptr)
    , compute_pipeline_denoise_ssao_(nullptr)
    , compute_pipeline_ddgi_trace_(nullptr)
    , compute_pipeline_ddgi_update_(nullptr)
    , compute_pipeline_denoise_shadow_(nullptr)
    , compute_pipeline_penumbra_blocker_(nullptr)
{
    // Initialize multi-buffered arrays (configurable buffer count)
    for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
        framebuffer_buffer_async_[i] = nullptr;
        depth_buffer_async_[i] = nullptr;
        triangles_buffer_async_[i] = nullptr;
        lights_buffer_async_[i] = nullptr;
        bvh_nodes_buffer_async_[i] = nullptr;
        bvh_triangles_buffer_async_[i] = nullptr;
        tile_indices_buffer_async_[i] = nullptr;
        tile_offsets_buffer_async_[i] = nullptr;
        tile_counts_buffer_async_[i] = nullptr;
        gbuffer_buffer_async_[i] = nullptr;        // Deferred rendering: G-buffer
        shadow_results_buffer_async_[i] = nullptr; // Deferred rendering: shadow results
        light_color_buffer_async_[i] = nullptr;    // Per-pixel light color ratio
        entity_bvh_nodes_buffer_async_[i] = nullptr;
        directional_groups_buffer_async_[i] = nullptr;
        shadow_denoised_buffer_async_[i] = nullptr;
        ssao_results_buffer_async_[i] = nullptr;
        ssao_denoised_buffer_async_[i] = nullptr;
        blocker_distance_buffer_async_[i] = nullptr;
        penumbra_temp_buffer_async_[i] = nullptr;
        jfa_buffer_a_[i] = nullptr;
        jfa_buffer_b_[i] = nullptr;
        jfa_buffer_capacity_[i] = 0;
        transparent_triangles_buffer_async_[i] = nullptr;

        framebuffer_capacity_async_[i] = 0;
        depth_capacity_async_[i] = 0;
        triangles_capacity_async_[i] = 0;
        lights_capacity_async_[i] = 0;
        bvh_nodes_capacity_async_[i] = 0;
        bvh_triangles_capacity_async_[i] = 0;
        tile_indices_capacity_async_[i] = 0;
        tile_offsets_capacity_async_[i] = 0;
        tile_counts_capacity_async_[i] = 0;
        gbuffer_capacity_async_[i] = 0;        // Deferred rendering: G-buffer capacity
        shadow_results_capacity_async_[i] = 0; // Deferred rendering: shadow results capacity
        light_color_capacity_async_[i] = 0;    // Per-pixel light color ratio capacity
        entity_bvh_nodes_capacity_async_[i] = 0;
        directional_groups_capacity_async_[i] = 0;
        shadow_denoised_capacity_async_[i] = 0;
        ssao_results_capacity_async_[i] = 0;
        ssao_denoised_capacity_async_[i] = 0;
        blocker_distance_capacity_async_[i] = 0;
        penumbra_temp_capacity_async_[i] = 0;
        transparent_triangles_capacity_async_[i] = 0;
        transparent_triangle_count_async_[i] = 0;

        // QW4: GPU destination buffers (Private mode) for blit transfers
        triangles_buffer_gpu_async_[i] = nullptr;
        bvh_nodes_buffer_gpu_async_[i] = nullptr;
        bvh_triangles_buffer_gpu_async_[i] = nullptr;

        triangles_capacity_gpu_async_[i] = 0;
        bvh_nodes_capacity_gpu_async_[i] = 0;
        bvh_triangles_capacity_gpu_async_[i] = 0;
    }

    // Initialize cached constant buffers
    width_buffer_ = nullptr;
    height_buffer_ = nullptr;
    clear_color_buffer_ = nullptr;
    triangle_count_buffer_ = nullptr;
    light_count_buffer_ = nullptr;
    bvh_node_count_buffer_ = nullptr;
    bvh_triangle_count_buffer_ = nullptr;
    shadow_width_buffer_ = nullptr;    // Phase 1: Shadow quality buffers
    shadow_height_buffer_ = nullptr;

    temporal_lighting_buffer_ = nullptr;     // Phase 2: Temporal distribution buffers
    temporal_lighting_capacity_ = 0;
    prev_particle_id_buffer_ = nullptr;      // Soft shadow motion detection (ghosting fix)
    prev_particle_id_capacity_ = 0;
    sample_count_buffer_ = nullptr;          // Soft shadow running average convergence
    sample_count_capacity_ = 0;
    frame_index_buffer_ = nullptr;

    // Phase 2: Indirect dispatch buffers - support N-frame temporal
    for (int i = 0; i < MAX_TEMPORAL_FRAMES; i++) {
        pixel_indices_buffers_[i] = nullptr;
    }
    pixel_indices_count_ = 0;

    // Create semaphore for multi-buffering (GPU_BUFFER_SLOTS buffers available initially)
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(Optimizations::GPU_BUFFER_SLOTS);
    buffer_semaphore_ = (__bridge_retained void*)semaphore;
}

GPURasterizer::~GPURasterizer() {
    // Release persistent GPU buffers (STEP 7.5: Memory leak fix)
    if (framebuffer_buffer_) {
        CFBridgingRelease(framebuffer_buffer_);
        framebuffer_buffer_ = nullptr;
    }
    if (depth_buffer_) {
        CFBridgingRelease(depth_buffer_);
        depth_buffer_ = nullptr;
    }
    if (triangles_buffer_) {
        CFBridgingRelease(triangles_buffer_);
        triangles_buffer_ = nullptr;
    }
    if (lights_buffer_) {
        CFBridgingRelease(lights_buffer_);
        lights_buffer_ = nullptr;
    }
    if (bvh_nodes_buffer_) {
        CFBridgingRelease(bvh_nodes_buffer_);
        bvh_nodes_buffer_ = nullptr;
    }
    if (bvh_triangles_buffer_) {
        CFBridgingRelease(bvh_triangles_buffer_);
        bvh_triangles_buffer_ = nullptr;
    }

    // Release cached constant buffers
    if (width_buffer_) {
        CFBridgingRelease(width_buffer_);
        width_buffer_ = nullptr;
    }
    if (height_buffer_) {
        CFBridgingRelease(height_buffer_);
        height_buffer_ = nullptr;
    }
    if (clear_color_buffer_) {
        CFBridgingRelease(clear_color_buffer_);
        clear_color_buffer_ = nullptr;
    }

    // QW2: Release cached count buffers
    if (triangle_count_buffer_) {
        CFBridgingRelease(triangle_count_buffer_);
        triangle_count_buffer_ = nullptr;
    }
    if (light_count_buffer_) {
        CFBridgingRelease(light_count_buffer_);
        light_count_buffer_ = nullptr;
    }
    if (bvh_node_count_buffer_) {
        CFBridgingRelease(bvh_node_count_buffer_);
        bvh_node_count_buffer_ = nullptr;
    }
    if (bvh_triangle_count_buffer_) {
        CFBridgingRelease(bvh_triangle_count_buffer_);
        bvh_triangle_count_buffer_ = nullptr;
    }

    // Phase 1: Release shadow quality buffers
    if (shadow_width_buffer_) {
        CFBridgingRelease(shadow_width_buffer_);
        shadow_width_buffer_ = nullptr;
    }
    if (shadow_height_buffer_) {
        CFBridgingRelease(shadow_height_buffer_);
        shadow_height_buffer_ = nullptr;
    }

    // Phase 2: Release temporal distribution buffers
    if (temporal_lighting_buffer_) {
        CFBridgingRelease(temporal_lighting_buffer_);
        temporal_lighting_buffer_ = nullptr;
    }
    if (prev_particle_id_buffer_) {
        CFBridgingRelease(prev_particle_id_buffer_);
        prev_particle_id_buffer_ = nullptr;
    }
    if (sample_count_buffer_) {
        CFBridgingRelease(sample_count_buffer_);
        sample_count_buffer_ = nullptr;
    }
    if (frame_index_buffer_) {
        CFBridgingRelease(frame_index_buffer_);
        frame_index_buffer_ = nullptr;
    }

    // Phase 2: Release indirect dispatch pixel indices buffers (N-frame temporal)
    for (int i = 0; i < MAX_TEMPORAL_FRAMES; i++) {
        if (pixel_indices_buffers_[i]) {
            CFBridgingRelease(pixel_indices_buffers_[i]);
            pixel_indices_buffers_[i] = nullptr;
        }
    }

    // Release multi-buffered resources (async execution, configurable buffer count)
    for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
        if (framebuffer_buffer_async_[i]) {
            CFBridgingRelease(framebuffer_buffer_async_[i]);
            framebuffer_buffer_async_[i] = nullptr;
        }
        if (depth_buffer_async_[i]) {
            CFBridgingRelease(depth_buffer_async_[i]);
            depth_buffer_async_[i] = nullptr;
        }
        if (triangles_buffer_async_[i]) {
            CFBridgingRelease(triangles_buffer_async_[i]);
            triangles_buffer_async_[i] = nullptr;
        }
        if (lights_buffer_async_[i]) {
            CFBridgingRelease(lights_buffer_async_[i]);
            lights_buffer_async_[i] = nullptr;
        }
        if (bvh_nodes_buffer_async_[i]) {
            CFBridgingRelease(bvh_nodes_buffer_async_[i]);
            bvh_nodes_buffer_async_[i] = nullptr;
        }
        if (bvh_triangles_buffer_async_[i]) {
            CFBridgingRelease(bvh_triangles_buffer_async_[i]);
            bvh_triangles_buffer_async_[i] = nullptr;
        }
        if (tile_indices_buffer_async_[i]) {
            CFBridgingRelease(tile_indices_buffer_async_[i]);
            tile_indices_buffer_async_[i] = nullptr;
        }
        if (tile_offsets_buffer_async_[i]) {
            CFBridgingRelease(tile_offsets_buffer_async_[i]);
            tile_offsets_buffer_async_[i] = nullptr;
        }
        if (tile_counts_buffer_async_[i]) {
            CFBridgingRelease(tile_counts_buffer_async_[i]);
            tile_counts_buffer_async_[i] = nullptr;
        }
        if (gbuffer_buffer_async_[i]) {
            CFBridgingRelease(gbuffer_buffer_async_[i]);
            gbuffer_buffer_async_[i] = nullptr;
        }
        if (shadow_results_buffer_async_[i]) {
            CFBridgingRelease(shadow_results_buffer_async_[i]);
            shadow_results_buffer_async_[i] = nullptr;
        }
        if (light_color_buffer_async_[i]) {
            CFBridgingRelease(light_color_buffer_async_[i]);
            light_color_buffer_async_[i] = nullptr;
        }
        if (entity_bvh_nodes_buffer_async_[i]) {
            CFBridgingRelease(entity_bvh_nodes_buffer_async_[i]);
            entity_bvh_nodes_buffer_async_[i] = nullptr;
        }
        if (directional_groups_buffer_async_[i]) {
            CFBridgingRelease(directional_groups_buffer_async_[i]);
            directional_groups_buffer_async_[i] = nullptr;
        }
        if (shadow_denoised_buffer_async_[i]) {
            CFBridgingRelease(shadow_denoised_buffer_async_[i]);
            shadow_denoised_buffer_async_[i] = nullptr;
        }
        if (transparent_triangles_buffer_async_[i]) {
            CFBridgingRelease(transparent_triangles_buffer_async_[i]);
            transparent_triangles_buffer_async_[i] = nullptr;
        }

        // QW4: Release GPU destination buffers
        if (triangles_buffer_gpu_async_[i]) {
            CFBridgingRelease(triangles_buffer_gpu_async_[i]);
            triangles_buffer_gpu_async_[i] = nullptr;
        }
        if (bvh_nodes_buffer_gpu_async_[i]) {
            CFBridgingRelease(bvh_nodes_buffer_gpu_async_[i]);
            bvh_nodes_buffer_gpu_async_[i] = nullptr;
        }
        if (bvh_triangles_buffer_gpu_async_[i]) {
            CFBridgingRelease(bvh_triangles_buffer_gpu_async_[i]);
            bvh_triangles_buffer_gpu_async_[i] = nullptr;
        }
    }

    // Release GPU performance profiling resources
    if (counter_sample_buffer_) {
        CFBridgingRelease(counter_sample_buffer_);
        counter_sample_buffer_ = nullptr;
    }
    // Counter sets are device-owned, no explicit release needed

    // Release GPU shader instrumentation buffers
    if (debug_rays_traced_buffer_) {
        CFBridgingRelease(debug_rays_traced_buffer_);
        debug_rays_traced_buffer_ = nullptr;
    }
    if (debug_bvh_nodes_visited_buffer_) {
        CFBridgingRelease(debug_bvh_nodes_visited_buffer_);
        debug_bvh_nodes_visited_buffer_ = nullptr;
    }
    if (debug_triangles_tested_buffer_) {
        CFBridgingRelease(debug_triangles_tested_buffer_);
        debug_triangles_tested_buffer_ = nullptr;
    }

    // Release semaphore
    if (buffer_semaphore_) {
        CFBridgingRelease(buffer_semaphore_);
        buffer_semaphore_ = nullptr;
    }

    // Release Metal resources (ARC handles this automatically)
    if (device_) {
        device_ = nullptr;
    }
    if (command_queue_) {
        command_queue_ = nullptr;
    }
    if (compute_pipeline_minimal_) {
        compute_pipeline_minimal_ = nullptr;
    }
    if (compute_pipeline_triangle_) {
        compute_pipeline_triangle_ = nullptr;
    }
    if (compute_pipeline_barycentric_) {
        compute_pipeline_barycentric_ = nullptr;
    }
    if (compute_pipeline_with_depth_) {
        compute_pipeline_with_depth_ = nullptr;
    }
    if (library_) {
        library_ = nullptr;
    }
}

bool GPURasterizer::initialize(int width, int height) {
    // Update dimensions (cheap, always do this)
    if (width_ != width || height_ != height) {
        std::cout << "[GPU_RASTERIZER] Resolution change: " << width_ << "x" << height_
                  << " → " << width << "x" << height << std::endl;
        width_ = width;
        height_ = height;

        // Phase 1: Calculate shadow dimensions at runtime based on framebuffer size
        // WHY: Framebuffer size is runtime-dependent (window size), not compile-time
        // OWNERSHIP: GPURasterizer owns shadow buffer → owns shadow dimensions
        shadow_width_ = (uint32_t)(width * Optimizations::SHADOW_RESOLUTION_SCALE);
        shadow_height_ = (uint32_t)(height * Optimizations::SHADOW_RESOLUTION_SCALE);

        std::cout << "[GPU_RASTERIZER] Phase 1: Shadow resolution calculated: "
                  << shadow_width_ << "×" << shadow_height_
                  << " (scale: " << Optimizations::SHADOW_RESOLUTION_SCALE << ")" << std::endl;

        // CRITICAL FIX: Update cached dimension buffers when resolution changes!
        // These Metal buffers hold width/height values passed to shaders.
        // Without this update, shaders use stale dimensions → rendering artifacts.
        if (initialized_ && width_buffer_ && height_buffer_) {
            id<MTLBuffer> widthBuf = (__bridge id<MTLBuffer>)width_buffer_;
            id<MTLBuffer> heightBuf = (__bridge id<MTLBuffer>)height_buffer_;
            unsigned int width_uint = (unsigned int)width_;
            unsigned int height_uint = (unsigned int)height_;
            memcpy([widthBuf contents], &width_uint, sizeof(unsigned int));
            memcpy([heightBuf contents], &height_uint, sizeof(unsigned int));
            std::cout << "[GPU_RASTERIZER] Updated width/height buffers to "
                      << width_uint << "×" << height_uint << std::endl;
        }
        if (initialized_ && shadow_width_buffer_ && shadow_height_buffer_) {
            id<MTLBuffer> shadowWidthBuf = (__bridge id<MTLBuffer>)shadow_width_buffer_;
            id<MTLBuffer> shadowHeightBuf = (__bridge id<MTLBuffer>)shadow_height_buffer_;
            memcpy([shadowWidthBuf contents], &shadow_width_, sizeof(uint32_t));
            memcpy([shadowHeightBuf contents], &shadow_height_, sizeof(uint32_t));
            std::cout << "[GPU_RASTERIZER] Updated shadow dimension buffers to "
                      << shadow_width_ << "×" << shadow_height_ << std::endl;
        }
    }

    // Only initialize Metal resources once
    if (initialized_) {
        return true;  // Metal resources already initialized
    }

    std::cout << "[GPU_RASTERIZER] Initializing Metal resources for " << width << "x" << height << std::endl;

    // Get Metal device
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Metal not supported on this system");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but Metal not available - ABORTING");
        std::exit(1);
    }
    device_ = (__bridge_retained void*)device;
    g_metal_device = device_;  // For get_metal_allocated_bytes()

    // GPU MEMORY MONITORING: Log device memory budget at init
    // currentAllocatedSize tracks total Metal buffer allocation.
    // recommendedMaxWorkingSetSize is the safe limit before the system pages out.
    std::cout << "[GPU_MEMORY] Device: " << [device.name UTF8String] << std::endl;
    std::cout << "[GPU_MEMORY] Recommended max working set: "
              << (device.recommendedMaxWorkingSetSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "[GPU_MEMORY] Current allocated: "
              << (device.currentAllocatedSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "[GPU_MEMORY] GPU_BUFFER_SLOTS: " << Optimizations::GPU_BUFFER_SLOTS
              << " | GBufferPixel: 32 bytes" << std::endl;

    // Check for Metal Ray Tracing support (Apple6 = M3+ chip)
    // MTLGPUFamilyApple6 supports hardware-accelerated ray tracing
    if (@available(macOS 14.0, *)) {
        supports_raytracing_ = [device supportsFamily:MTLGPUFamilyApple9];
        if (!supports_raytracing_) {
            // Fallback check for Apple8 (M2 with limited RT support)
            supports_raytracing_ = [device supportsFamily:MTLGPUFamilyApple8];
        }
    } else {
        supports_raytracing_ = false;
    }
    std::cout << "[GPU_RASTERIZER] Metal Ray Tracing: "
              << (supports_raytracing_ ? "AVAILABLE (M3+ hardware)" : "NOT AVAILABLE (compute fallback)")
              << std::endl;

    // Create command queue
    id<MTLCommandQueue> commandQueue = [device newCommandQueue];
    if (!commandQueue) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create Metal command queue");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but GPU resources unavailable - ABORTING");
        std::exit(1);
    }
    command_queue_ = (__bridge_retained void*)commandQueue;

    // Load Metal library (default.metallib), resolved relative to the
    // executable — never the CWD (stale cross-tree shader hazard).
    NSError* error = nil;
    id<MTLLibrary> library = nil;

    std::string libPath = logosphere::gpu::locate_metallib();
    if (!libPath.empty()) {
        NSURL* url = [NSURL fileURLWithPath:
                         [NSString stringWithUTF8String:libPath.c_str()]];
        library = [device newLibraryWithURL:url error:&error];
        if (library) {
            NSLog(@"[GPU_RASTERIZER] Metal library: %s", libPath.c_str());
        }
    }

    if (!library) {
        // Last resort: default library (embedded in binary / app bundle)
        library = [device newDefaultLibrary];

            if (!library) {
                NSLog(@"[GPU_RASTERIZER] FATAL: Metal library not found");
                NSLog(@"  Executable-relative search found: %s",
                      libPath.empty() ? "(nothing)" : libPath.c_str());
                NSLog(@"  Tried: [device newDefaultLibrary]");
                if (error) {
                    NSLog(@"  Last error: %@", [error localizedDescription]);
                }
                NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but Metal shaders not compiled - ABORTING");
                NSLog(@"  Make sure Metal shaders are compiled: cmake --build build");
                std::exit(1);
            }
    }
    library_ = (__bridge_retained void*)library;

    // STRICT MODE: All pipelines must initialize or we crash
    // Create compute pipeline for rasterize_minimal kernel (STEP 1)
    id<MTLFunction> minimalFunction = [library newFunctionWithName:@"rasterize_minimal"];
    if (!minimalFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'rasterize_minimal' not found in Metal library");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but required shaders missing - ABORTING");
        std::exit(1);
    }

    id<MTLComputePipelineState> minimalPipeline = [device newComputePipelineStateWithFunction:minimalFunction
                                                                                          error:&error];
    if (!minimalPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create minimal pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but pipeline creation failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_minimal_ = (__bridge_retained void*)minimalPipeline;

    // Create compute pipeline for rasterize_triangle kernel (STEP 2)
    id<MTLFunction> triangleFunction = [library newFunctionWithName:@"rasterize_triangle"];
    if (!triangleFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'rasterize_triangle' not found in Metal library");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but required shaders missing - ABORTING");
        std::exit(1);
    }

    id<MTLComputePipelineState> trianglePipeline = [device newComputePipelineStateWithFunction:triangleFunction
                                                                                           error:&error];
    if (!trianglePipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create triangle pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but pipeline creation failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_triangle_ = (__bridge_retained void*)trianglePipeline;

    // STRICT MODE: All pipelines must initialize or we crash
    // Create compute pipeline for rasterize_triangle_barycentric kernel (STEP 3)
    id<MTLFunction> barycentricFunction = [library newFunctionWithName:@"rasterize_triangle_barycentric"];
    if (!barycentricFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'rasterize_triangle_barycentric' not found in library");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but required shaders missing - ABORTING");
        std::exit(1);
    }

    id<MTLComputePipelineState> barycentricPipeline = [device newComputePipelineStateWithFunction:barycentricFunction
                                                                                              error:&error];
    if (!barycentricPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create barycentric pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but pipeline creation failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_barycentric_ = (__bridge_retained void*)barycentricPipeline;

    // Create compute pipeline for rasterize_triangle_with_depth kernel (STEP 4)
    id<MTLFunction> depthFunction = [library newFunctionWithName:@"rasterize_triangle_with_depth"];
    if (!depthFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'rasterize_triangle_with_depth' not found in library");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but required shaders missing - ABORTING");
        std::exit(1);
    }

    id<MTLComputePipelineState> depthPipeline = [device newComputePipelineStateWithFunction:depthFunction
                                                                                        error:&error];
    if (!depthPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create depth pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but pipeline creation failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_with_depth_ = (__bridge_retained void*)depthPipeline;

    // Initialize STEP 5: batch rasterization pipeline
    id<MTLFunction> batchFunction = [library newFunctionWithName:@"rasterize_triangles_batch"];
    if (!batchFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'rasterize_triangles_batch' not found in library");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but required shaders missing - ABORTING");
        std::exit(1);
    }
    id<MTLComputePipelineState> batchPipeline = [device newComputePipelineStateWithFunction:batchFunction error:&error];
    if (!batchPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create batch pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but pipeline creation failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_batch_ = (__bridge_retained void*)batchPipeline;

    // Initialize STEP 7: lighting integration pipeline (CRITICAL for GPU_RASTERIZATION)
    id<MTLFunction> litFunction = [library newFunctionWithName:@"rasterize_triangles_lit"];
    if (!litFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'rasterize_triangles_lit' not found in library");
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but required shaders missing - ABORTING");
        std::exit(1);
    }
    id<MTLComputePipelineState> litPipeline = [device newComputePipelineStateWithFunction:litFunction error:&error];
    if (!litPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create lit pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] GPU_RASTERIZATION enabled but pipeline creation failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_lit_ = (__bridge_retained void*)litPipeline;

    // Initialize buffer clearing pipeline (STEP 7.5 optimization)
    id<MTLFunction> clearFunction = [library newFunctionWithName:@"clear_framebuffer"];
    if (!clearFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'clear_framebuffer' not found in library");
        std::exit(1);
    }
    id<MTLComputePipelineState> clearPipeline = [device newComputePipelineStateWithFunction:clearFunction error:&error];
    if (!clearPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create clear pipeline: %@", error);
        std::exit(1);
    }
    compute_pipeline_clear_ = (__bridge_retained void*)clearPipeline;

    // Initialize deferred rendering pipelines (3-pass system)
    // Pass 1: G-Buffer rasterization (geometry only)
    id<MTLFunction> gbufferFunction = [library newFunctionWithName:@"rasterize_gbuffer"];
    if (!gbufferFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'rasterize_gbuffer' not found in library");
        NSLog(@"[GPU_RASTERIZER] Deferred rendering pass 1 missing - ABORTING");
        std::exit(1);
    }
    id<MTLComputePipelineState> gbufferPipeline = [device newComputePipelineStateWithFunction:gbufferFunction error:&error];
    if (!gbufferPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create G-buffer pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] Deferred rendering initialization failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_gbuffer_ = (__bridge_retained void*)gbufferPipeline;

    // Pass 2: Shadow rays (coherent BVH traversal)
    id<MTLFunction> shadowsFunction = [library newFunctionWithName:@"trace_shadow_rays_deferred"];
    if (!shadowsFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'trace_shadow_rays_deferred' not found in library");
        NSLog(@"[GPU_RASTERIZER] Deferred rendering pass 2 missing - ABORTING");
        std::exit(1);
    }
    id<MTLComputePipelineState> shadowsPipeline = [device newComputePipelineStateWithFunction:shadowsFunction error:&error];
    if (!shadowsPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create shadows pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] Deferred rendering initialization failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_shadows_ = (__bridge_retained void*)shadowsPipeline;

    // Batched shadow ray kernel for BVH cache coherency (Phase II-B)
    id<MTLComputePipelineState> shadowsBatchedPipeline = nil;
    if (Optimizations::USE_GPU_RAY_BATCHING) {
        id<MTLFunction> shadowsBatchedFunction = [library newFunctionWithName:@"trace_shadow_rays_deferred_batched"];
        if (shadowsBatchedFunction) {
            shadowsBatchedPipeline = [device newComputePipelineStateWithFunction:shadowsBatchedFunction error:&error];
            if (!shadowsBatchedPipeline) {
                NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create batched shadows pipeline: %@", error);
                NSLog(@"[GPU_RASTERIZER] Falling back to regular shadow pipeline");
            } else {
                NSLog(@"[GPU_RASTERIZER] Batched shadow ray kernel loaded (Phase II-B: 8 pixels/thread)");
            }
        } else {
            NSLog(@"[GPU_RASTERIZER] WARNING: Batched kernel 'trace_shadow_rays_deferred_batched' not found");
        }
    }
    compute_pipeline_shadows_batched_ = shadowsBatchedPipeline ? (__bridge_retained void*)shadowsBatchedPipeline : nullptr;

    // Instrumented shadow ray kernel for BVH profiling
    id<MTLComputePipelineState> shadowsInstrumentedPipeline = nil;
    if (Optimizations::PROFILE_BVH_TRAVERSAL) {
        id<MTLFunction> shadowsInstrumentedFunction = [library newFunctionWithName:@"trace_shadow_rays_instrumented"];
        if (shadowsInstrumentedFunction) {
            shadowsInstrumentedPipeline = [device newComputePipelineStateWithFunction:shadowsInstrumentedFunction error:&error];
            if (!shadowsInstrumentedPipeline) {
                NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create instrumented shadows pipeline: %@", error);
                NSLog(@"[GPU_RASTERIZER] Falling back to regular shadow pipeline");
            } else {
                NSLog(@"[GPU_RASTERIZER] Instrumented shadow ray kernel loaded for BVH profiling");
            }
        } else {
            NSLog(@"[GPU_RASTERIZER] WARNING: Instrumented kernel 'trace_shadow_rays_instrumented' not found");
        }
    }
    compute_pipeline_shadows_instrumented_ = shadowsInstrumentedPipeline ? (__bridge_retained void*)shadowsInstrumentedPipeline : nullptr;

    // Metal RT shadow ray kernel (M3+ hardware acceleration)
    if (supports_raytracing_) {
        id<MTLFunction> shadowsRTFunction = [library newFunctionWithName:@"trace_shadows_metal_rt"];
        if (shadowsRTFunction) {
            id<MTLComputePipelineState> shadowsRTPipeline = [device newComputePipelineStateWithFunction:shadowsRTFunction error:&error];
            if (!shadowsRTPipeline) {
                NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create Metal RT shadows pipeline: %@", error);
                NSLog(@"[GPU_RASTERIZER] Falling back to compute-based shadow rays");
            } else {
                compute_pipeline_shadows_rt_ = (__bridge_retained void*)shadowsRTPipeline;
                NSLog(@"[GPU_RASTERIZER] ✓ Metal RT shadow kernel loaded (hardware acceleration enabled)");
            }
        } else {
            NSLog(@"[GPU_RASTERIZER] WARNING: Metal RT kernel 'trace_shadows_metal_rt' not found");
        }
    }

    // Deterministic shadow kernel (1 closest-hit ray/pixel/light, no temporal/PCSS)
    if (supports_raytracing_) {
        id<MTLFunction> shadowsDetFunction = [library newFunctionWithName:@"trace_shadows_deterministic"];
        if (shadowsDetFunction) {
            id<MTLComputePipelineState> shadowsDetPipeline = [device newComputePipelineStateWithFunction:shadowsDetFunction error:&error];
            if (!shadowsDetPipeline) {
                NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create deterministic shadows pipeline: %@", error);
            } else {
                compute_pipeline_shadows_deterministic_ = (__bridge_retained void*)shadowsDetPipeline;
                NSLog(@"[GPU_RASTERIZER] Deterministic shadow kernel loaded (PenumbraMode support)");
            }
        } else {
            NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'trace_shadows_deterministic' not found in library");
        }
    }

    // Penumbra post-process kernels (Pass 2.5)
    {
        id<MTLFunction> blockerFunc = [library newFunctionWithName:@"penumbra_blocker_analysis"];
        if (blockerFunc) {
            id<MTLComputePipelineState> blockerPipeline = [device newComputePipelineStateWithFunction:blockerFunc error:&error];
            if (blockerPipeline) {
                compute_pipeline_penumbra_blocker_ = (__bridge_retained void*)blockerPipeline;
                NSLog(@"[GPU_RASTERIZER] Penumbra blocker analysis kernel loaded (Approach C)");
            } else {
                NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create penumbra blocker pipeline: %@", error);
            }
        } else {
            NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'penumbra_blocker_analysis' not found in library");
        }

        // JFA + Separable Blur pipelines (Tier 2 penumbra)
        auto loadKernel = [&](const char* name) -> void* {
            id<MTLFunction> func = [library newFunctionWithName:[NSString stringWithUTF8String:name]];
            if (!func) { NSLog(@"[GPU_RASTERIZER] WARNING: Kernel '%s' not found", name); return nullptr; }
            id<MTLComputePipelineState> pipe = [device newComputePipelineStateWithFunction:func error:&error];
            if (!pipe) { NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create pipeline '%s': %@", name, error); return nullptr; }
            NSLog(@"[GPU_RASTERIZER] JFA kernel '%s' loaded", name);
            return (__bridge_retained void*)pipe;
        };
        compute_pipeline_jfa_seed_ = loadKernel("penumbra_jfa_seed");
        compute_pipeline_jfa_propagate_ = loadKernel("penumbra_jfa_propagate");
        compute_pipeline_blur_h_ = loadKernel("penumbra_blur_h");
        compute_pipeline_blur_v_ = loadKernel("penumbra_blur_v");
    }

    // Pass 3: Apply lighting (combine G-buffer + shadow results)
    id<MTLFunction> lightingFunction = [library newFunctionWithName:@"apply_lighting_deferred"];
    if (!lightingFunction) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Kernel 'apply_lighting_deferred' not found in library");
        NSLog(@"[GPU_RASTERIZER] Deferred rendering pass 3 missing - ABORTING");
        std::exit(1);
    }
    id<MTLComputePipelineState> lightingPipeline = [device newComputePipelineStateWithFunction:lightingFunction error:&error];
    if (!lightingPipeline) {
        NSLog(@"[GPU_RASTERIZER] FATAL: Failed to create lighting pipeline: %@", error);
        NSLog(@"[GPU_RASTERIZER] Deferred rendering initialization failed - ABORTING");
        std::exit(1);
    }
    compute_pipeline_lighting_ = (__bridge_retained void*)lightingPipeline;


    // Pass 2.7: SSAO (Screen-Space Ambient Occlusion)
    id<MTLFunction> ssaoFunction = [library newFunctionWithName:@"compute_ssao"];
    if (!ssaoFunction) {
        NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'compute_ssao' not found - SSAO disabled");
        compute_pipeline_ssao_ = nullptr;
    } else {
        id<MTLComputePipelineState> ssaoPipeline = [device newComputePipelineStateWithFunction:ssaoFunction error:&error];
        if (!ssaoPipeline) {
            NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create SSAO pipeline: %@", error);
            compute_pipeline_ssao_ = nullptr;
        } else {
            compute_pipeline_ssao_ = (__bridge_retained void*)ssaoPipeline;
            NSLog(@"[GPU_RASTERIZER] SSAO pipeline initialized");
        }
    }

    // Pass 2.8: SSAO Denoise (A-trous bilateral, single channel)
    id<MTLFunction> ssaoDenoiseFunction = [library newFunctionWithName:@"denoise_ssao_atrous"];
    if (!ssaoDenoiseFunction) {
        NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'denoise_ssao_atrous' not found - SSAO denoise disabled");
        compute_pipeline_denoise_ssao_ = nullptr;
    } else {
        id<MTLComputePipelineState> ssaoDenoisePipeline = [device newComputePipelineStateWithFunction:ssaoDenoiseFunction error:&error];
        if (!ssaoDenoisePipeline) {
            NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create SSAO denoise pipeline: %@", error);
            compute_pipeline_denoise_ssao_ = nullptr;
        } else {
            compute_pipeline_denoise_ssao_ = (__bridge_retained void*)ssaoDenoisePipeline;
            NSLog(@"[GPU_RASTERIZER] SSAO denoise pipeline initialized (%d passes)", Optimizations::SSAO_DENOISE_PASSES);
        }
    }

    // Pass 2.5b/c: DDGI (Dynamic Diffuse Global Illumination, probe-based)
    if constexpr (Optimizations::USE_DDGI) {
        id<MTLFunction> ddgiTraceFunc = [library newFunctionWithName:@"ddgi_trace_probes"];
        if (ddgiTraceFunc) {
            id<MTLComputePipelineState> p = [device newComputePipelineStateWithFunction:ddgiTraceFunc error:&error];
            if (p) {
                compute_pipeline_ddgi_trace_ = (__bridge_retained void*)p;
                NSLog(@"[GPU_RASTERIZER] DDGI trace pipeline initialized");
            }
        }
        id<MTLFunction> ddgiUpdateFunc = [library newFunctionWithName:@"ddgi_update_probes"];
        if (ddgiUpdateFunc) {
            id<MTLComputePipelineState> p = [device newComputePipelineStateWithFunction:ddgiUpdateFunc error:&error];
            if (p) {
                compute_pipeline_ddgi_update_ = (__bridge_retained void*)p;
                NSLog(@"[GPU_RASTERIZER] DDGI update pipeline initialized");
            }
        }
    }

    // Pass 2.05: Shadow Denoise (edge-preserving spatial filter on shadow lux)
    id<MTLFunction> denoiseShadowFunction = [library newFunctionWithName:@"denoise_shadow_buffer"];
    if (!denoiseShadowFunction) {
        NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'denoise_shadow_buffer' not found - shadow denoise disabled");
        compute_pipeline_denoise_shadow_ = nullptr;
    } else {
        id<MTLComputePipelineState> denoiseShadowPipeline = [device newComputePipelineStateWithFunction:denoiseShadowFunction error:&error];
        if (!denoiseShadowPipeline) {
            NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create shadow denoise pipeline: %@", error);
            compute_pipeline_denoise_shadow_ = nullptr;
        } else {
            compute_pipeline_denoise_shadow_ = (__bridge_retained void*)denoiseShadowPipeline;
            NSLog(@"[GPU_RASTERIZER] Shadow denoise pipeline initialized");
        }
    }

    // Pass 4: Vision cone post-process (optional fog-of-war effect)
    id<MTLFunction> visionConeFunction = [library newFunctionWithName:@"apply_vision_cone"];
    if (!visionConeFunction) {
        NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'apply_vision_cone' not found - vision cone disabled");
        compute_pipeline_vision_cone_ = nullptr;
    } else {
        id<MTLComputePipelineState> visionConePipeline = [device newComputePipelineStateWithFunction:visionConeFunction error:&error];
        if (!visionConePipeline) {
            NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create vision cone pipeline: %@", error);
            compute_pipeline_vision_cone_ = nullptr;
        } else {
            compute_pipeline_vision_cone_ = (__bridge_retained void*)visionConePipeline;
            NSLog(@"[GPU_RASTERIZER] Vision cone pipeline initialized");
        }
    }

    // Pass 3.5: Forward transparent rendering (optional, behind USE_TRANSPARENCY flag)
    id<MTLFunction> forwardTransparentFunction = [library newFunctionWithName:@"rasterize_transparent_forward"];
    if (!forwardTransparentFunction) {
        NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'rasterize_transparent_forward' not found - transparency disabled");
        compute_pipeline_forward_transparent_ = nullptr;
    } else {
        id<MTLComputePipelineState> forwardTransparentPipeline = [device newComputePipelineStateWithFunction:forwardTransparentFunction error:&error];
        if (!forwardTransparentPipeline) {
            NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create forward transparent pipeline: %@", error);
            compute_pipeline_forward_transparent_ = nullptr;
        } else {
            compute_pipeline_forward_transparent_ = (__bridge_retained void*)forwardTransparentPipeline;
            NSLog(@"[GPU_RASTERIZER] Forward transparent pipeline initialized (Pass 3.5)");
        }
    }

    // RT variant: preferred at dispatch when Metal RT + accel structure are
    // available (the software-BVH walk kernel above stays as the fallback).
    id<MTLFunction> forwardTransparentRTFunction = [library newFunctionWithName:@"rasterize_transparent_forward_rt"];
    if (!forwardTransparentRTFunction) {
        NSLog(@"[GPU_RASTERIZER] WARNING: Kernel 'rasterize_transparent_forward_rt' not found - RT transparency unavailable");
        compute_pipeline_forward_transparent_rt_ = nullptr;
    } else {
        id<MTLComputePipelineState> forwardTransparentRTPipeline = [device newComputePipelineStateWithFunction:forwardTransparentRTFunction error:&error];
        if (!forwardTransparentRTPipeline) {
            NSLog(@"[GPU_RASTERIZER] WARNING: Failed to create RT forward transparent pipeline: %@", error);
            compute_pipeline_forward_transparent_rt_ = nullptr;
        } else {
            compute_pipeline_forward_transparent_rt_ = (__bridge_retained void*)forwardTransparentRTPipeline;
            NSLog(@"[GPU_RASTERIZER] Forward transparent RT pipeline initialized (Pass 3.5, any-hit)");
        }
    }

    // Create vision cone params buffer. Layout MUST stay
    // byte-identical to the VisionConeParams struct in
    // gpu_types.metal (48 base + 16 align + 1024 occlusion mask
    // + 32 memory header = 1120 bytes). static_assert below
    // catches drift at compile time.
    struct VisionConeParamsGPU {
        float    viewer_x, viewer_y, look_direction, half_fov;
        float    range, inner_falloff, darkness;
        uint32_t enabled;
        float    focus_x, focus_y, focus_radius, padding;
        // LOS occlusion mask (mirrors VISION_CONE_OCCLUSION_BINS = 256)
        int32_t  occlusion_count;
        int32_t  occlusion_pad0;
        int32_t  occlusion_pad1;
        int32_t  occlusion_pad2;
        float    occlusion_distance[GPURasterizer::kVisionConeOcclusionBins];
        // Vision-memory grid header. Buffer lives at MTL bind
        // index 5; this is just layout + tuning.
        int32_t  memory_enabled;
        int32_t  memory_width;
        int32_t  memory_height;
        int32_t  memory_pad;
        float    memory_origin_x;
        float    memory_origin_y;
        float    memory_cell_size;
        float    memory_dim;
    };
    static_assert(sizeof(VisionConeParamsGPU) == 1120,
                  "VisionConeParams must stay byte-identical to the Metal struct "
                  "(48 base + 16 align + 1024 occlusion + 32 memory = 1120)");
    VisionConeParamsGPU vision_params = {};
    vision_params.half_fov     = 0.785f;
    vision_params.range        = 20.0f;
    vision_params.inner_falloff = 0.8f;
    vision_params.darkness     = 0.1f;
    vision_params.focus_radius = 3.0f;
    vision_params.occlusion_count = 0;
    vision_params.memory_enabled = 0;     // off by default — opt-in
    id<MTLBuffer> visionParamsBuf = [device newBufferWithBytes:&vision_params
                                                         length:sizeof(vision_params)
                                                        options:MTLResourceStorageModeShared];
    [visionParamsBuf retain];
    vision_cone_params_buffer_ = (__bridge_retained void*)visionParamsBuf;

    // Vision-memory grid buffer — persistent across frames.
    // Allocated up-front at the maximum supported size so the host
    // never has to grow it (and we don't fight ARC over the swap).
    // Game configures the LOGICAL extent + cells via
    // set_vision_memory_extent; if cells_per_side ≤ 256 the buffer
    // is reused. Beyond 256, the host code clamps and warns.
    {
        const size_t max_cells = 256 * 256;
        std::vector<float> zeros(max_cells, 0.0f);
        id<MTLBuffer> visionMemBuf = [device newBufferWithBytes:zeros.data()
                                                          length:max_cells * sizeof(float)
                                                         options:MTLResourceStorageModeShared];
        [visionMemBuf retain];
        vision_memory_buffer_ = (__bridge_retained void*)visionMemBuf;
        vision_memory_buffer_capacity_ = max_cells;
    }

    // Stub dynamic-particle map buffer (1 byte = 0). Same retain
    // pattern. set_dynamic_particle_map() grows it on demand.
    {
        uint8_t zero = 0;
        id<MTLBuffer> dynBuf = [device newBufferWithBytes:&zero
                                                    length:1
                                                   options:MTLResourceStorageModeShared];
        [dynBuf retain];
        dynamic_particle_map_buffer_ = (__bridge_retained void*)dynBuf;
        dynamic_particle_map_capacity_ = 1;
    }

    // Create cached constant buffers to avoid per-frame recreation
    // These buffers rarely change, so we cache them for the lifetime of the rasterizer
    unsigned int width_uint = (unsigned int)width_;
    unsigned int height_uint = (unsigned int)height_;
    id<MTLBuffer> widthBuf = [device newBufferWithBytes:&width_uint
                                                  length:sizeof(unsigned int)
                                                 options:MTLResourceStorageModeShared];
    [widthBuf retain];
    width_buffer_ = (__bridge_retained void*)widthBuf;

    id<MTLBuffer> heightBuf = [device newBufferWithBytes:&height_uint
                                                   length:sizeof(unsigned int)
                                                  options:MTLResourceStorageModeShared];
    [heightBuf retain];
    height_buffer_ = (__bridge_retained void*)heightBuf;

    uint32_t clear_color = 0xFF0A0A0F;  // BGRA(10,10,15,255) - dark background
    id<MTLBuffer> clearColorBuf = [device newBufferWithBytes:&clear_color
                                                       length:sizeof(uint32_t)
                                                      options:MTLResourceStorageModeShared];
    [clearColorBuf retain];
    clear_color_buffer_ = (__bridge_retained void*)clearColorBuf;

    // QW2: Initialize cached constant buffers (4 bytes each)
    // These will be updated via memcpy instead of recreated every frame
    unsigned int init_count = 0;

    id<MTLBuffer> triangleCountBuf = [device newBufferWithBytes:&init_count
                                                          length:sizeof(unsigned int)
                                                         options:MTLResourceStorageModeShared];
    [triangleCountBuf retain];
    triangle_count_buffer_ = (__bridge_retained void*)triangleCountBuf;

    id<MTLBuffer> lightCountBuf = [device newBufferWithBytes:&init_count
                                                       length:sizeof(unsigned int)
                                                      options:MTLResourceStorageModeShared];
    [lightCountBuf retain];
    light_count_buffer_ = (__bridge_retained void*)lightCountBuf;

    id<MTLBuffer> bvhNodeCountBuf = [device newBufferWithBytes:&init_count
                                                         length:sizeof(unsigned int)
                                                        options:MTLResourceStorageModeShared];
    [bvhNodeCountBuf retain];
    bvh_node_count_buffer_ = (__bridge_retained void*)bvhNodeCountBuf;

    id<MTLBuffer> bvhTriangleCountBuf = [device newBufferWithBytes:&init_count
                                                             length:sizeof(unsigned int)
                                                            options:MTLResourceStorageModeShared];
    [bvhTriangleCountBuf retain];
    bvh_triangle_count_buffer_ = (__bridge_retained void*)bvhTriangleCountBuf;

    // Phase 1: Shadow quality - create shadow dimension buffers
    // CRITICAL: Use RUNTIME dimensions (shadow_width_, shadow_height_) calculated above
    // These buffers are passed to shaders - must match actual shadow buffer size!
    // PREVIOUS BUG: Used compile-time Optimizations::SHADOW_WIDTH/HEIGHT (1280×800)
    //               while actual buffer was runtime size (e.g., 1600×1051)
    //               → shaders wrote to wrong region → black screen

    id<MTLBuffer> shadowWidthBuf = [device newBufferWithBytes:&shadow_width_
                                                        length:sizeof(uint32_t)
                                                       options:MTLResourceStorageModeShared];
    [shadowWidthBuf retain];
    shadow_width_buffer_ = (__bridge_retained void*)shadowWidthBuf;

    id<MTLBuffer> shadowHeightBuf = [device newBufferWithBytes:&shadow_height_
                                                         length:sizeof(uint32_t)
                                                        options:MTLResourceStorageModeShared];
    [shadowHeightBuf retain];
    shadow_height_buffer_ = (__bridge_retained void*)shadowHeightBuf;

    // GPU Performance Profiling: Initialize MTLCounterSet API
    // Reference: WWDC20 "Optimize Metal apps and games with GPU counters"
    // Apple Docs: https://developer.apple.com/documentation/metal/gpu_counters_and_counter_sample_buffers
    NSLog(@"[GPU_PROFILING] Enumerating available GPU counter sets...");

    NSArray<id<MTLCounterSet>>* counterSets = [device counterSets];
    if (counterSets && [counterSets count] > 0) {
        NSLog(@"[GPU_PROFILING] Found %lu counter set(s) on device", (unsigned long)[counterSets count]);

        // Find available counter sets
        for (id<MTLCounterSet> counterSet in counterSets) {
            NSString* setName = [counterSet name];
            NSLog(@"[GPU_PROFILING]   Counter set: %@ (%lu counters)",
                  setName, (unsigned long)[[counterSet counters] count]);

            // Look for timestamp counter set (always available on Apple Silicon)
            if ([setName containsString:@"timestamp"] ||
                [setName containsString:@"Timestamp"]) {
                timestamp_counter_set_ = (__bridge void*)counterSet;
                NSLog(@"[GPU_PROFILING]     ✓ Found timestamp counter set");
            }

            // Look for performance counter set (occupancy, bandwidth)
            // May be named "performance", "common", or vendor-specific
            if ([setName containsString:@"performance"] ||
                [setName containsString:@"Performance"] ||
                [setName containsString:@"common"]) {
                performance_counter_set_ = (__bridge void*)counterSet;
                NSLog(@"[GPU_PROFILING]     ✓ Found performance counter set");

                // List available counters in this set
                for (id<MTLCounter> counter in [counterSet counters]) {
                    NSLog(@"[GPU_PROFILING]       - %@", [counter name]);
                }
            }
        }

        // Create counter sample buffer if we found at least one counter set
        if (timestamp_counter_set_ || performance_counter_set_) {
            // Use timestamp counter set if available, otherwise performance
            id<MTLCounterSet> selectedCounterSet = timestamp_counter_set_ ?
                (__bridge id<MTLCounterSet>)timestamp_counter_set_ :
                (__bridge id<MTLCounterSet>)performance_counter_set_;

            MTLCounterSampleBufferDescriptor* desc = [[MTLCounterSampleBufferDescriptor alloc] init];
            desc.counterSet = selectedCounterSet;
            desc.storageMode = MTLStorageModeShared;  // CPU needs to read results
            desc.sampleCount = kCounterSampleCount;   // 4 samples: before/after Pass 2, plus extras

            NSError* counterError = nil;
            id<MTLCounterSampleBuffer> sampleBuffer = [device newCounterSampleBufferWithDescriptor:desc
                                                                                              error:&counterError];
            if (sampleBuffer) {
                counter_sample_buffer_ = (__bridge_retained void*)sampleBuffer;
                NSLog(@"[GPU_PROFILING] ✓ Created counter sample buffer (%d samples)", kCounterSampleCount);
            } else {
                NSLog(@"[GPU_PROFILING] ⚠ Failed to create counter sample buffer: %@",
                      counterError ? [counterError localizedDescription] : @"unknown error");
            }
        } else {
            NSLog(@"[GPU_PROFILING] ⚠ No usable counter sets found (GPU profiling unavailable)");
        }
    } else {
        NSLog(@"[GPU_PROFILING] ⚠ Device does not support counter sets (older Metal version?)");
    }

    // GPU Shader Instrumentation: Allocate atomic counter buffers
    // Each buffer holds a single uint32_t that all threads increment atomically
    NSLog(@"[GPU_PROFILING] Allocating shader instrumentation buffers (atomic counters)...");

    debug_rays_traced_buffer_ = (__bridge_retained void*)[device newBufferWithLength:sizeof(uint32_t)
                                                                             options:MTLResourceStorageModeShared];
    debug_bvh_nodes_visited_buffer_ = (__bridge_retained void*)[device newBufferWithLength:sizeof(uint32_t)
                                                                                   options:MTLResourceStorageModeShared];
    debug_triangles_tested_buffer_ = (__bridge_retained void*)[device newBufferWithLength:sizeof(uint32_t)
                                                                                  options:MTLResourceStorageModeShared];

    if (debug_rays_traced_buffer_ && debug_bvh_nodes_visited_buffer_ && debug_triangles_tested_buffer_) {
        NSLog(@"[GPU_PROFILING] ✓ Created 3 atomic counter buffers (4 bytes each)");
    } else {
        NSLog(@"[GPU_PROFILING] ⚠ Failed to create atomic counter buffers");
    }

    initialized_ = true;
    return true;
}

void GPURasterizer::rasterize_minimal(uint32_t* pixel_buffer) {
    if (!initialized_ || !pixel_buffer) {
        return;  // Not initialized or invalid input
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_minimal_;

    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

    // Create compute encoder
    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];

    // Set compute pipeline
    [computeEncoder setComputePipelineState:pipeline];

    // Create GPU buffer for pixel buffer (BGRA uint32_t)
    size_t bufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> gpuBuffer = [device newBufferWithBytes:pixel_buffer
                                                  length:bufferSize
                                                 options:MTLResourceStorageModeShared];

    // Create GPU buffers for width/height constants
    unsigned int width_uint = (unsigned int)width_;
    unsigned int height_uint = (unsigned int)height_;
    id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                    length:sizeof(unsigned int)
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];

    // Set buffers
    [computeEncoder setBuffer:gpuBuffer offset:0 atIndex:0];
    [computeEncoder setBuffer:widthBuffer offset:0 atIndex:1];
    [computeEncoder setBuffer:heightBuffer offset:0 atIndex:2];

    // Dispatch GPU threads (one thread per pixel)
    MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
    MTLSize threadsPerThreadgroup = MTLSizeMake(
        Optimizations::GPU_THREADS_FORWARD,
        Optimizations::GPU_THREADS_FORWARD,
        1);  // GPU_THREADS_FORWARD × GPU_THREADS_FORWARD threads per group

    [computeEncoder dispatchThreads:threadsPerGrid
              threadsPerThreadgroup:threadsPerThreadgroup];

    // End encoding and commit
    [computeEncoder endEncoding];
    [commandBuffer commit];

    // Wait for GPU to finish (synchronous for STEP 1)
    [commandBuffer waitUntilCompleted];

    // Copy result back to CPU pixel buffer
    memcpy(pixel_buffer, [gpuBuffer contents], bufferSize);
}

void GPURasterizer::rasterize_triangle(uint32_t* pixel_buffer,
                                       const float triangle_verts[6],
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!initialized_ || !pixel_buffer) {
        return;  // Not initialized or invalid input
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_triangle_;

    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

    // Create compute encoder
    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];

    // Set compute pipeline
    [computeEncoder setComputePipelineState:pipeline];

    // Create GPU buffer for framebuffer
    size_t bufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> framebufferBuffer = [device newBufferWithBytes:pixel_buffer
                                                          length:bufferSize
                                                         options:MTLResourceStorageModeShared];

    // Create GPU buffers for width/height
    unsigned int width_uint = (unsigned int)width_;
    unsigned int height_uint = (unsigned int)height_;
    id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                    length:sizeof(unsigned int)
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];

    // Create GPU buffer for triangle vertices
    id<MTLBuffer> vertsBuffer = [device newBufferWithBytes:triangle_verts
                                                    length:6 * sizeof(float)
                                                   options:MTLResourceStorageModeShared];

    // Create color buffer (RGBA float4, 0-1 range)
    float colorData[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    id<MTLBuffer> colorBuffer = [device newBufferWithBytes:colorData
                                                    length:sizeof(colorData)
                                                   options:MTLResourceStorageModeShared];

    // Set buffers
    [computeEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
    [computeEncoder setBuffer:widthBuffer offset:0 atIndex:1];
    [computeEncoder setBuffer:heightBuffer offset:0 atIndex:2];
    [computeEncoder setBuffer:vertsBuffer offset:0 atIndex:3];
    [computeEncoder setBuffer:colorBuffer offset:0 atIndex:4];

    // Dispatch GPU threads (one thread per pixel)
    MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
    MTLSize threadsPerThreadgroup = MTLSizeMake(
        Optimizations::GPU_THREADS_FORWARD,
        Optimizations::GPU_THREADS_FORWARD,
        1);  // GPU_THREADS_FORWARD × GPU_THREADS_FORWARD threads per group

    [computeEncoder dispatchThreads:threadsPerGrid
              threadsPerThreadgroup:threadsPerThreadgroup];

    // End encoding and commit
    [computeEncoder endEncoding];
    [commandBuffer commit];

    // Wait for GPU to finish (synchronous for STEP 2)
    [commandBuffer waitUntilCompleted];

    // Copy result back to CPU pixel buffer
    memcpy(pixel_buffer, [framebufferBuffer contents], bufferSize);
}

void GPURasterizer::rasterize_triangle_barycentric(uint32_t* pixel_buffer,
                                                   const float triangle_verts[6]) {
    if (!initialized_ || !pixel_buffer) {
        return;  // Not initialized or invalid input
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_barycentric_;

    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

    // Create compute encoder
    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];

    // Set compute pipeline
    [computeEncoder setComputePipelineState:pipeline];

    // Create GPU buffer for framebuffer
    size_t bufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> framebufferBuffer = [device newBufferWithBytes:pixel_buffer
                                                          length:bufferSize
                                                         options:MTLResourceStorageModeShared];

    // Create GPU buffers for width/height
    unsigned int width_uint = (unsigned int)width_;
    unsigned int height_uint = (unsigned int)height_;
    id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                    length:sizeof(unsigned int)
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];

    // Create GPU buffer for triangle vertices
    id<MTLBuffer> vertsBuffer = [device newBufferWithBytes:triangle_verts
                                                    length:6 * sizeof(float)
                                                   options:MTLResourceStorageModeShared];

    // Set buffers
    [computeEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
    [computeEncoder setBuffer:widthBuffer offset:0 atIndex:1];
    [computeEncoder setBuffer:heightBuffer offset:0 atIndex:2];
    [computeEncoder setBuffer:vertsBuffer offset:0 atIndex:3];

    // Dispatch GPU threads (one thread per pixel)
    MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
    MTLSize threadsPerThreadgroup = MTLSizeMake(
        Optimizations::GPU_THREADS_FORWARD,
        Optimizations::GPU_THREADS_FORWARD,
        1);  // GPU_THREADS_FORWARD × GPU_THREADS_FORWARD threads per group

    [computeEncoder dispatchThreads:threadsPerGrid
              threadsPerThreadgroup:threadsPerThreadgroup];

    // End encoding and commit
    [computeEncoder endEncoding];
    [commandBuffer commit];

    // Wait for GPU to finish (synchronous for STEP 3)
    [commandBuffer waitUntilCompleted];

    // Copy result back to CPU pixel buffer
    memcpy(pixel_buffer, [framebufferBuffer contents], bufferSize);
}

void GPURasterizer::rasterize_triangle_with_depth(uint32_t* pixel_buffer,
                                                   uint32_t* depth_buffer,
                                                   const float triangle_verts[9],
                                                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!initialized_ || !pixel_buffer || !depth_buffer) {
        return;  // Not initialized or invalid input
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_with_depth_;

    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

    // Create compute encoder
    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];

    // Set compute pipeline
    [computeEncoder setComputePipelineState:pipeline];

    // Create GPU buffer for framebuffer
    size_t framebufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> framebufferBuffer = [device newBufferWithBytes:pixel_buffer
                                                          length:framebufferSize
                                                         options:MTLResourceStorageModeShared];

    // Create GPU buffer for depth buffer
    size_t depthBufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> depthBufferGPU = [device newBufferWithBytes:depth_buffer
                                                        length:depthBufferSize
                                                       options:MTLResourceStorageModeShared];

    // Create GPU buffers for width/height
    unsigned int width_uint = (unsigned int)width_;
    unsigned int height_uint = (unsigned int)height_;
    id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                    length:sizeof(unsigned int)
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];

    // Create GPU buffer for triangle vertices (9 floats: x0,y0,z0, x1,y1,z1, x2,y2,z2)
    id<MTLBuffer> vertsBuffer = [device newBufferWithBytes:triangle_verts
                                                    length:9 * sizeof(float)
                                                   options:MTLResourceStorageModeShared];

    // Create color buffer (RGBA float4, 0-1 range)
    float colorData[4] = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    id<MTLBuffer> colorBuffer = [device newBufferWithBytes:colorData
                                                    length:sizeof(colorData)
                                                   options:MTLResourceStorageModeShared];

    // Set buffers (must match kernel signature order)
    [computeEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
    [computeEncoder setBuffer:depthBufferGPU offset:0 atIndex:1];
    [computeEncoder setBuffer:widthBuffer offset:0 atIndex:2];
    [computeEncoder setBuffer:heightBuffer offset:0 atIndex:3];
    [computeEncoder setBuffer:vertsBuffer offset:0 atIndex:4];
    [computeEncoder setBuffer:colorBuffer offset:0 atIndex:5];

    // Dispatch GPU threads (one thread per pixel)
    MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
    MTLSize threadsPerThreadgroup = MTLSizeMake(
        Optimizations::GPU_THREADS_FORWARD,
        Optimizations::GPU_THREADS_FORWARD,
        1);  // GPU_THREADS_FORWARD × GPU_THREADS_FORWARD threads per group

    [computeEncoder dispatchThreads:threadsPerGrid
              threadsPerThreadgroup:threadsPerThreadgroup];

    // End encoding and commit
    [computeEncoder endEncoding];
    [commandBuffer commit];

    // Wait for GPU to finish (synchronous for STEP 4)
    [commandBuffer waitUntilCompleted];

    // Copy results back to CPU buffers
    memcpy(pixel_buffer, [framebufferBuffer contents], framebufferSize);
    memcpy(depth_buffer, [depthBufferGPU contents], depthBufferSize);
}

void GPURasterizer::rasterize_triangles_batch(uint32_t* pixel_buffer,
                                               uint32_t* depth_buffer,
                                               const TriangleGPU* triangles,
                                               uint32_t triangle_count) {
    if (!initialized_ || !pixel_buffer || !depth_buffer || !triangles || triangle_count == 0) {
        return;  // Invalid input
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_batch_;

    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];

    // Set compute pipeline
    [computeEncoder setComputePipelineState:pipeline];

    // Create GPU buffer for framebuffer
    size_t framebufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> framebufferBuffer = [device newBufferWithBytes:pixel_buffer
                                                          length:framebufferSize
                                                         options:MTLResourceStorageModeShared];

    // Create GPU buffer for depth buffer
    size_t depthBufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> depthBufferGPU = [device newBufferWithBytes:depth_buffer
                                                        length:depthBufferSize
                                                       options:MTLResourceStorageModeShared];

    // Create GPU buffers for width/height
    unsigned int width_uint = (unsigned int)width_;
    unsigned int height_uint = (unsigned int)height_;
    id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                    length:sizeof(unsigned int)
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];

    // Create GPU buffer for triangle array
    size_t trianglesSize = triangle_count * sizeof(TriangleGPU);
    id<MTLBuffer> trianglesBuffer = [device newBufferWithBytes:triangles
                                                         length:trianglesSize
                                                        options:MTLResourceStorageModeShared];

    // Create GPU buffer for triangle count
    id<MTLBuffer> triangleCountBuffer = [device newBufferWithBytes:&triangle_count
                                                            length:sizeof(unsigned int)
                                                           options:MTLResourceStorageModeShared];

    // Set buffers (must match kernel signature order)
    [computeEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
    [computeEncoder setBuffer:depthBufferGPU offset:0 atIndex:1];
    [computeEncoder setBuffer:widthBuffer offset:0 atIndex:2];
    [computeEncoder setBuffer:heightBuffer offset:0 atIndex:3];
    [computeEncoder setBuffer:trianglesBuffer offset:0 atIndex:4];
    [computeEncoder setBuffer:triangleCountBuffer offset:0 atIndex:5];

    // Dispatch GPU threads (one thread per pixel)
    MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
    MTLSize threadsPerThreadgroup = MTLSizeMake(
        Optimizations::GPU_THREADS_FORWARD,
        Optimizations::GPU_THREADS_FORWARD,
        1);  // GPU_THREADS_FORWARD × GPU_THREADS_FORWARD threads per group

    [computeEncoder dispatchThreads:threadsPerGrid
              threadsPerThreadgroup:threadsPerThreadgroup];

    // End encoding and commit
    [computeEncoder endEncoding];
    [commandBuffer commit];

    // Wait for GPU to finish (synchronous for STEP 5)
    [commandBuffer waitUntilCompleted];

    // Copy results back to CPU buffers
    memcpy(pixel_buffer, [framebufferBuffer contents], framebufferSize);
    memcpy(depth_buffer, [depthBufferGPU contents], depthBufferSize);
}

// =========================================================================
// STEP 6: SURFACE UPLOAD - Convert CPU surfaces to GPU triangles
// =========================================================================
void GPURasterizer::convert_surface_to_gpu_triangles(
    const Surface& surface,
    CameraSystem& camera_system,
    uint8_t particle_r, uint8_t particle_g, uint8_t particle_b, uint8_t particle_a,
    std::vector<TriangleGPU>& out_triangles) {

    // Convert color to 0-1 range (GPU expects float colors)
    float r = particle_r / 255.0f;
    float g = particle_g / 255.0f;
    float b = particle_b / 255.0f;
    float a = particle_a / 255.0f;

    // Surface can be triangle (3 vertices) or quad (4 vertices)
    // Quad is split into 2 triangles: (0,1,2) and (0,2,3)

    int num_triangles = (surface.vertex_count == 3) ? 1 : 2;

    for (int tri_idx = 0; tri_idx < num_triangles; ++tri_idx) {
        // Extract triangle vertices based on which triangle we're processing
        float triangle_verts[3][3];

        if (tri_idx == 0) {
            // First triangle: vertices 0, 1, 2
            for (int i = 0; i < 3; ++i) {
                triangle_verts[i][0] = surface.vertices[i][0];
                triangle_verts[i][1] = surface.vertices[i][1];
                triangle_verts[i][2] = surface.vertices[i][2];
            }
        } else {
            // Second triangle (quad only): vertices 0, 2, 3
            triangle_verts[0][0] = surface.vertices[0][0];
            triangle_verts[0][1] = surface.vertices[0][1];
            triangle_verts[0][2] = surface.vertices[0][2];

            triangle_verts[1][0] = surface.vertices[2][0];
            triangle_verts[1][1] = surface.vertices[2][1];
            triangle_verts[1][2] = surface.vertices[2][2];

            triangle_verts[2][0] = surface.vertices[3][0];
            triangle_verts[2][1] = surface.vertices[3][1];
            triangle_verts[2][2] = surface.vertices[3][2];
        }

        // Project triangle to screen space
        auto projected = camera_system.project_triangle(triangle_verts);

        // Skip if triangle is off-screen
        if (!projected.on_screen) {
            continue;
        }

        // Create GPU triangle
        TriangleGPU gpu_tri;

        // Screen coordinates (x, y) and projection-aware depth (z).
        // Using raw world Z is wrong for any projection where the view
        // direction isn't straight-down — under iso, two points at the
        // same world Z but different XY do NOT share a depth value.
        // Route through the projection so the depth buffer stays in
        // one consistent space (see CameraSystem::compute_depth and
        // tests/test_iso_depth_ordering.cpp).
        gpu_tri.x0 = (float)projected.screen_corners[0][0];
        gpu_tri.y0 = (float)projected.screen_corners[0][1];
        gpu_tri.z0 = camera_system.compute_depth(projected.world_corners[0][0],
                                                  projected.world_corners[0][1],
                                                  projected.world_corners[0][2]);
        gpu_tri._padding0 = 0.0f;

        gpu_tri.x1 = (float)projected.screen_corners[1][0];
        gpu_tri.y1 = (float)projected.screen_corners[1][1];
        gpu_tri.z1 = camera_system.compute_depth(projected.world_corners[1][0],
                                                  projected.world_corners[1][1],
                                                  projected.world_corners[1][2]);
        gpu_tri._padding1 = 0.0f;

        gpu_tri.x2 = (float)projected.screen_corners[2][0];
        gpu_tri.y2 = (float)projected.screen_corners[2][1];
        gpu_tri.z2 = camera_system.compute_depth(projected.world_corners[2][0],
                                                  projected.world_corners[2][1],
                                                  projected.world_corners[2][2]);
        gpu_tri._padding2 = 0.0f;

        // Color (0-1 range)
        gpu_tri.r = r;
        gpu_tri.g = g;
        gpu_tri.b = b;
        gpu_tri.a = a;

        out_triangles.push_back(gpu_tri);
    }
}

// STEP 7: Convert Surface to TriangleLit (with lighting data)
void GPURasterizer::convert_surface_to_lit_triangles(
    const Surface& surface,
    CameraSystem& camera_system,
    uint8_t particle_r, uint8_t particle_g, uint8_t particle_b, uint8_t particle_a,
    std::vector<TriangleLit>& out_triangles) {

    // Convert color to 0-1 range (GPU expects float colors)
    float r = particle_r / 255.0f;
    float g = particle_g / 255.0f;
    float b = particle_b / 255.0f;
    float a = particle_a / 255.0f;

    // Projection-aware back-face cull. A triangle is front-facing iff
    // moving from its center along its outward normal takes you CLOSER
    // to the camera (depth decreases). True for any projection whose
    // compute_depth is monotone along the view axis. Without this, the
    // two-sided rasterizer inside-test also rasterizes the object's
    // back hemisphere, whose outward normals point away from overhead
    // light → pixels Lambert-clamp to zero → "black bowl under the
    // body" artifact. See ellipsoid_lighting_test for the repro.
    if (surface.vertex_count >= 3) {
        float face_cx = 0.0f, face_cy = 0.0f, face_cz = 0.0f;
        for (int i = 0; i < surface.vertex_count; ++i) {
            face_cx += surface.vertices[i][0];
            face_cy += surface.vertices[i][1];
            face_cz += surface.vertices[i][2];
        }
        face_cx /= (float)surface.vertex_count;
        face_cy /= (float)surface.vertex_count;
        face_cz /= (float)surface.vertex_count;

        constexpr float kBackfaceEps = 0.01f;  // 1 cm: well above numerical noise
        float depth_center = camera_system.compute_depth(face_cx, face_cy, face_cz);
        float depth_off    = camera_system.compute_depth(
            face_cx + surface.nx * kBackfaceEps,
            face_cy + surface.ny * kBackfaceEps,
            face_cz + surface.nz * kBackfaceEps);
        if (depth_off >= depth_center) {
            return;  // back-facing, drop
        }
    }

    // Surface can be triangle (3 vertices) or quad (4 vertices)
    // Quad is split into 2 triangles: (0,1,2) and (0,2,3)
    int num_triangles = (surface.vertex_count == 3) ? 1 : 2;

    for (int tri_idx = 0; tri_idx < num_triangles; ++tri_idx) {
        // Extract triangle vertices based on which triangle we're processing
        float triangle_verts[3][3];

        if (tri_idx == 0) {
            // First triangle: vertices 0, 1, 2
            for (int i = 0; i < 3; ++i) {
                triangle_verts[i][0] = surface.vertices[i][0];
                triangle_verts[i][1] = surface.vertices[i][1];
                triangle_verts[i][2] = surface.vertices[i][2];
            }
        } else {
            // Second triangle (quad only): vertices 0, 2, 3
            triangle_verts[0][0] = surface.vertices[0][0];
            triangle_verts[0][1] = surface.vertices[0][1];
            triangle_verts[0][2] = surface.vertices[0][2];

            triangle_verts[1][0] = surface.vertices[2][0];
            triangle_verts[1][1] = surface.vertices[2][1];
            triangle_verts[1][2] = surface.vertices[2][2];

            triangle_verts[2][0] = surface.vertices[3][0];
            triangle_verts[2][1] = surface.vertices[3][1];
            triangle_verts[2][2] = surface.vertices[3][2];
        }

        // Project triangle to screen space
        auto projected = camera_system.project_triangle(triangle_verts);

        // Skip if triangle is off-screen
        if (!projected.on_screen) {
            continue;
        }

        // Projection-aware per-vertex depth. Mirrors the CPU path
        // (SurfaceRasterizer::calculate_pixel_depth → CameraSystem::compute_depth).
        // For parallel projections (iso, bird's-eye, cabinet) this is the
        // orthographic view-direction depth, NOT 3D Euclidean distance —
        // see tests/test_iso_depth_ordering.cpp for the contract. Using
        // Euclidean here was the bug that let a low body occlude a higher
        // marker when the camera sat near the scene plane.
        auto calc_depth = [&](int vert_idx) -> float {
            return camera_system.compute_depth(
                projected.world_corners[vert_idx][0],
                projected.world_corners[vert_idx][1],
                projected.world_corners[vert_idx][2]);
        };

        float depth0 = calc_depth(0);
        float depth1 = calc_depth(1);
        float depth2 = calc_depth(2);

        // Create GPU triangle with lighting data
        TriangleLit gpu_tri;

        // Screen coordinates (x, y) and camera-space depth (z)
        gpu_tri.x0 = (float)projected.screen_corners[0][0];
        gpu_tri.y0 = (float)projected.screen_corners[0][1];
        gpu_tri.z0 = depth0;  // Camera-space depth (distance from camera)
        gpu_tri._padding0 = 0.0f;

        gpu_tri.x1 = (float)projected.screen_corners[1][0];
        gpu_tri.y1 = (float)projected.screen_corners[1][1];
        gpu_tri.z1 = depth1;
        gpu_tri._padding1 = 0.0f;

        gpu_tri.x2 = (float)projected.screen_corners[2][0];
        gpu_tri.y2 = (float)projected.screen_corners[2][1];
        gpu_tri.z2 = depth2;
        gpu_tri._padding2 = 0.0f;

        // Base material color (0-1 range)
        gpu_tri.r = r;
        gpu_tri.g = g;
        gpu_tri.b = b;
        gpu_tri.a = a;

        // World-space positions (for lighting interpolation)
        gpu_tri.world_pos0[0] = projected.world_corners[0][0];
        gpu_tri.world_pos0[1] = projected.world_corners[0][1];
        gpu_tri.world_pos0[2] = projected.world_corners[0][2];
        gpu_tri._padding3 = 0.0f;

        gpu_tri.world_pos1[0] = projected.world_corners[1][0];
        gpu_tri.world_pos1[1] = projected.world_corners[1][1];
        gpu_tri.world_pos1[2] = projected.world_corners[1][2];
        gpu_tri._padding4 = 0.0f;

        gpu_tri.world_pos2[0] = projected.world_corners[2][0];
        gpu_tri.world_pos2[1] = projected.world_corners[2][1];
        gpu_tri.world_pos2[2] = projected.world_corners[2][2];
        gpu_tri._padding5 = 0.0f;

        // Surface normal (shared for flat shading)
        gpu_tri.normal[0] = surface.nx;
        gpu_tri.normal[1] = surface.ny;
        gpu_tri.normal[2] = surface.nz;

        // Particle ID (for debug/future features)
        gpu_tri.particle_id = surface.particle_id;

        // Material properties for SSGI
        gpu_tri.roughness = surface.roughness;
        gpu_tri._padding_roughness[0] = 0.0f;
        gpu_tri._padding_roughness[1] = 0.0f;
        gpu_tri._padding_roughness[2] = 0.0f;

        out_triangles.push_back(gpu_tri);
    }
}

// =========================================================================
// STEP 7: LIGHTING INTEGRATION - C++ wrapper for rasterize_triangles_lit
// =========================================================================
void GPURasterizer::rasterize_triangles_lit(uint32_t* pixel_buffer,
                                            uint32_t* depth_buffer,
                                            const TriangleLit* triangles,
                                            uint32_t triangle_count,
                                            const void* lights,
                                            uint32_t light_count,
                                            const void* bvh_nodes,
                                            uint32_t bvh_count,
                                            const void* bvh_triangles,
                                            uint32_t bvh_triangle_count) {
    if (!initialized_ || !pixel_buffer || !depth_buffer || !triangles || triangle_count == 0) {
        return;  // Invalid input
    }

    // CRITICAL: Autorelease pool prevents temporary buffer accumulation
    // Without this, Metal buffers created via newBufferWithBytes accumulate
    // and cause progressive FPS degradation even though memory appears stable
    @autoreleasepool {

    // STEP 7: Full lighting with BVH shadow ray tracing integrated
    // Uses Phase I GPU BVH infrastructure for shadow testing

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_lit_;
    id<MTLComputePipelineState> clearPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_clear_;

    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

    // STEP 7.5: Use persistent buffers (prevent memory leak)
    // Pattern from metal_compute_bridge.mm - only allocate if size changes

    // Framebuffer (persistent)
    size_t framebufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> framebufferBuffer = nil;
    if (!framebuffer_buffer_ || framebuffer_capacity_ < framebufferSize) {
        if (framebuffer_buffer_) {
            CFBridgingRelease(framebuffer_buffer_);
        }
        framebufferBuffer = [device newBufferWithLength:framebufferSize
                                                 options:MTLResourceStorageModeShared];
        if (!framebufferBuffer) {
            std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create framebuffer" << std::endl;
            std::exit(1);
        }
        framebuffer_buffer_ = (__bridge_retained void*)framebufferBuffer;
        framebuffer_capacity_ = framebufferSize;
    } else {
        framebufferBuffer = (__bridge id<MTLBuffer>)framebuffer_buffer_;
    }
    // GPU will clear this buffer - no CPU copy needed

    // Depth buffer (persistent)
    size_t depthBufferSize = width_ * height_ * sizeof(uint32_t);
    id<MTLBuffer> depthBufferGPU = nil;
    if (!depth_buffer_ || depth_capacity_ < depthBufferSize) {
        if (depth_buffer_) {
            CFBridgingRelease(depth_buffer_);
        }
        depthBufferGPU = [device newBufferWithLength:depthBufferSize
                                              options:MTLResourceStorageModeShared];
        if (!depthBufferGPU) {
            std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create depth buffer" << std::endl;
            std::exit(1);
        }
        depth_buffer_ = (__bridge_retained void*)depthBufferGPU;
        depth_capacity_ = depthBufferSize;
    } else {
        depthBufferGPU = (__bridge id<MTLBuffer>)depth_buffer_;
    }
    // GPU will clear this buffer - no CPU copy needed

    // Create GPU buffers for width/height
    unsigned int width_uint = (unsigned int)width_;
    unsigned int height_uint = (unsigned int)height_;
    id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                    length:sizeof(unsigned int)
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];

    // Triangles buffer (persistent)
    size_t trianglesSize = triangle_count * sizeof(TriangleLit);
    id<MTLBuffer> trianglesBuffer = nil;
    if (!triangles_buffer_ || triangles_capacity_ < trianglesSize) {
        if (triangles_buffer_) {
            CFBridgingRelease(triangles_buffer_);
        }
        trianglesBuffer = [device newBufferWithLength:trianglesSize
                                               options:MTLResourceStorageModeShared];
        if (!trianglesBuffer) {
            std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create triangles buffer" << std::endl;
            std::exit(1);
        }
        triangles_buffer_ = (__bridge_retained void*)trianglesBuffer;
        triangles_capacity_ = trianglesSize;
    } else {
        trianglesBuffer = (__bridge id<MTLBuffer>)triangles_buffer_;
    }
    // Copy triangle data into persistent buffer
    memcpy([trianglesBuffer contents], triangles, trianglesSize);

    // Create GPU buffer for triangle count
    id<MTLBuffer> triangleCountBuffer = [device newBufferWithBytes:&triangle_count
                                                            length:sizeof(unsigned int)
                                                           options:MTLResourceStorageModeShared];

    // Lights buffer (persistent)
    id<MTLBuffer> lightsBuffer = nil;
    if (lights && light_count > 0) {
        // LightData is 32 bytes (see gpu_types.metal)
        size_t lightsSize = light_count * 40;  // LightData: pos(12) + strength(4) + radius(4) + size(4) + color(12) + pad(4) = 40
        if (!lights_buffer_ || lights_capacity_ < lightsSize) {
            if (lights_buffer_) {
                CFBridgingRelease(lights_buffer_);
            }
            lightsBuffer = [device newBufferWithLength:lightsSize
                                               options:MTLResourceStorageModeShared];
            if (!lightsBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create lights buffer" << std::endl;
                std::exit(1);
            }
            lights_buffer_ = (__bridge_retained void*)lightsBuffer;
            lights_capacity_ = lightsSize;
        } else {
            lightsBuffer = (__bridge id<MTLBuffer>)lights_buffer_;
        }
        // Copy light data into persistent buffer
        memcpy([lightsBuffer contents], lights, lightsSize);
    } else {
        // No lights - create minimal dummy buffer (one-time allocation)
        if (!lights_buffer_) {
            float dummy = 0.0f;
            lightsBuffer = [device newBufferWithBytes:&dummy
                                               length:sizeof(float)
                                              options:MTLResourceStorageModeShared];
            lights_buffer_ = (__bridge_retained void*)lightsBuffer;
            lights_capacity_ = sizeof(float);
        } else {
            lightsBuffer = (__bridge id<MTLBuffer>)lights_buffer_;
        }
    }

    // Create GPU buffer for light count
    id<MTLBuffer> lightCountBuffer = [device newBufferWithBytes:&light_count
                                                         length:sizeof(unsigned int)
                                                        options:MTLResourceStorageModeShared];

    // BVH nodes buffer (persistent)
    id<MTLBuffer> bvhNodesBuffer = nil;
    id<MTLBuffer> bvhNodeCountBuffer = nil;
    if (bvh_nodes && bvh_count > 0) {
        // BVHNode is 48 bytes (see gpu_types.metal)
        size_t bvhNodesSize = bvh_count * 48;
        if (!bvh_nodes_buffer_ || bvh_nodes_capacity_ < bvhNodesSize) {
            if (bvh_nodes_buffer_) {
                CFBridgingRelease(bvh_nodes_buffer_);
            }
            bvhNodesBuffer = [device newBufferWithLength:bvhNodesSize
                                                 options:MTLResourceStorageModeShared];
            if (!bvhNodesBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create BVH nodes buffer" << std::endl;
                std::exit(1);
            }
            bvh_nodes_buffer_ = (__bridge_retained void*)bvhNodesBuffer;
            bvh_nodes_capacity_ = bvhNodesSize;
        } else {
            bvhNodesBuffer = (__bridge id<MTLBuffer>)bvh_nodes_buffer_;
        }
        // Copy BVH data into persistent buffer
        memcpy([bvhNodesBuffer contents], bvh_nodes, bvhNodesSize);

        bvhNodeCountBuffer = [device newBufferWithBytes:&bvh_count
                                                 length:sizeof(unsigned int)
                                                options:MTLResourceStorageModeShared];
    } else {
        // No BVH - create minimal dummy buffers (one-time allocation)
        if (!bvh_nodes_buffer_) {
            float dummy = 0.0f;
            bvhNodesBuffer = [device newBufferWithBytes:&dummy
                                                 length:sizeof(float)
                                                options:MTLResourceStorageModeShared];
            bvh_nodes_buffer_ = (__bridge_retained void*)bvhNodesBuffer;
            bvh_nodes_capacity_ = sizeof(float);
        } else {
            bvhNodesBuffer = (__bridge id<MTLBuffer>)bvh_nodes_buffer_;
        }
        unsigned int zero = 0;
        bvhNodeCountBuffer = [device newBufferWithBytes:&zero
                                                 length:sizeof(unsigned int)
                                                options:MTLResourceStorageModeShared];
    }

    // BVH triangles buffer (persistent)
    id<MTLBuffer> bvhTrianglesBuffer = nil;
    id<MTLBuffer> bvhTriangleCountBuffer = nil;
    if (bvh_triangles && bvh_triangle_count > 0) {
        // Triangle is 64 bytes (see gpu_types.metal: 48 vertices + 16 color+padding)
        size_t bvhTrianglesSize = bvh_triangle_count * 64;
        if (!bvh_triangles_buffer_ || bvh_triangles_capacity_ < bvhTrianglesSize) {
            if (bvh_triangles_buffer_) {
                CFBridgingRelease(bvh_triangles_buffer_);
            }
            bvhTrianglesBuffer = [device newBufferWithLength:bvhTrianglesSize
                                                     options:MTLResourceStorageModeShared];
            if (!bvhTrianglesBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create BVH triangles buffer" << std::endl;
                std::exit(1);
            }
            bvh_triangles_buffer_ = (__bridge_retained void*)bvhTrianglesBuffer;
            bvh_triangles_capacity_ = bvhTrianglesSize;
        } else {
            bvhTrianglesBuffer = (__bridge id<MTLBuffer>)bvh_triangles_buffer_;
        }
        // Copy triangle data into persistent buffer
        memcpy([bvhTrianglesBuffer contents], bvh_triangles, bvhTrianglesSize);

        // Build Metal RT acceleration structure. Rebuild whenever we've
        // just copied new triangle vertex data — not just when the count
        // changes. Gating only on count let rotating/moving geometry keep
        // stale acceleration structures while the CPU-side BVH had been
        // correctly refit, producing a "shadow doesn't follow the bike
        // when it rotates" symptom that looked like a BVH bug but was
        // actually this RT-structure staleness. Repro in
        // bike_viewer: rotate the motorcycle → old shadow persists.
        if (supports_raytracing_ && compute_pipeline_shadows_rt_) {
            build_acceleration_structure(bvh_triangles, bvh_triangle_count);
        } else if (!supports_raytracing_ || !compute_pipeline_shadows_rt_) {
            static int rt_skip_log = 0;
            if (rt_skip_log < 3) {
                std::cout << "[GPU_RASTERIZER] RT AS skip: supports_rt=" << supports_raytracing_
                          << " pipeline=" << (compute_pipeline_shadows_rt_ ? "OK" : "NULL") << std::endl;
                rt_skip_log++;
            }
        }

        bvhTriangleCountBuffer = [device newBufferWithBytes:&bvh_triangle_count
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];
    } else {
        // No triangles - create minimal dummy buffers (one-time allocation)
        if (!bvh_triangles_buffer_) {
            float dummy = 0.0f;
            bvhTrianglesBuffer = [device newBufferWithBytes:&dummy
                                                     length:sizeof(float)
                                                    options:MTLResourceStorageModeShared];
            bvh_triangles_buffer_ = (__bridge_retained void*)bvhTrianglesBuffer;
            bvh_triangles_capacity_ = sizeof(float);
        } else {
            bvhTrianglesBuffer = (__bridge id<MTLBuffer>)bvh_triangles_buffer_;
        }
        unsigned int zero = 0;
        bvhTriangleCountBuffer = [device newBufferWithBytes:&zero
                                                     length:sizeof(unsigned int)
                                                    options:MTLResourceStorageModeShared];
    }

    // STEP 7.6: GPU-side buffer clearing (eliminate 13.4 MB/frame CPU->GPU memcpy waste)

    // Clear depth buffer using blit encoder. Depth is SIGNED int32 —
    // filling with byte 0x7F yields 0x7F7F7F7F = 2139062143, our
    // "infinity" sentinel per depth_encoding.metal / depth_encoding.h.
    // 0xFF would give -1 as int32, which is NOT "far" — any real depth
    // encoded via encode_depth(d) would lose the first-write shortcut
    // and the whole depth test goes wrong. See the 2026-04-20 fix and
    // tests/test_gbuffer_depth_encoding.cpp.
    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
    [blitEncoder fillBuffer:depthBufferGPU
                      range:NSMakeRange(0, depthBufferSize)
                      value:0x7F];
    [blitEncoder endEncoding];

    // Clear framebuffer using compute shader
    uint32_t clear_color = 0xFF0A0A0F;  // BGRA(10,10,15,255) - dark background
    id<MTLBuffer> clearColorBuffer = [device newBufferWithBytes:&clear_color
                                                         length:sizeof(uint32_t)
                                                        options:MTLResourceStorageModeShared];

    id<MTLComputeCommandEncoder> clearEncoder = [commandBuffer computeCommandEncoder];
    [clearEncoder setComputePipelineState:clearPipeline];
    [clearEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
    [clearEncoder setBuffer:widthBuffer offset:0 atIndex:1];
    [clearEncoder setBuffer:heightBuffer offset:0 atIndex:2];
    [clearEncoder setBuffer:clearColorBuffer offset:0 atIndex:3];

    MTLSize clearThreadsPerGrid = MTLSizeMake(width_, height_, 1);
    MTLSize clearThreadsPerThreadgroup = MTLSizeMake(Optimizations::GPU_THREADS_CLEAR, Optimizations::GPU_THREADS_CLEAR, 1);
    [clearEncoder dispatchThreads:clearThreadsPerGrid
              threadsPerThreadgroup:clearThreadsPerThreadgroup];
    [clearEncoder endEncoding];

    // Now create rasterization encoder
    id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
    [computeEncoder setComputePipelineState:pipeline];

    // Set buffers (must match kernel signature order)
    [computeEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
    [computeEncoder setBuffer:depthBufferGPU offset:0 atIndex:1];
    [computeEncoder setBuffer:widthBuffer offset:0 atIndex:2];
    [computeEncoder setBuffer:heightBuffer offset:0 atIndex:3];
    [computeEncoder setBuffer:trianglesBuffer offset:0 atIndex:4];
    [computeEncoder setBuffer:triangleCountBuffer offset:0 atIndex:5];
    [computeEncoder setBuffer:lightsBuffer offset:0 atIndex:6];
    [computeEncoder setBuffer:lightCountBuffer offset:0 atIndex:7];
    [computeEncoder setBuffer:bvhNodesBuffer offset:0 atIndex:8];          // NEW: BVH nodes
    [computeEncoder setBuffer:bvhNodeCountBuffer offset:0 atIndex:9];      // NEW: BVH count
    [computeEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10];     // NEW: Shadow triangles
    [computeEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11]; // NEW: Triangle count

    // Dispatch GPU threads (one thread per pixel)
    MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
    MTLSize threadsPerThreadgroup = MTLSizeMake(
        Optimizations::GPU_THREADS_FORWARD,
        Optimizations::GPU_THREADS_FORWARD,
        1);  // GPU_THREADS_FORWARD × GPU_THREADS_FORWARD threads per group

    [computeEncoder dispatchThreads:threadsPerGrid
              threadsPerThreadgroup:threadsPerThreadgroup];

    // End encoding and commit
    [computeEncoder endEncoding];
    [commandBuffer commit];

    // Wait for GPU to finish (synchronous for STEP 7)
    [commandBuffer waitUntilCompleted];

    // Copy results back to CPU buffers
    memcpy(pixel_buffer, [framebufferBuffer contents], framebufferSize);
    memcpy(depth_buffer, [depthBufferGPU contents], depthBufferSize);

    } // @autoreleasepool - drain all temporary Metal buffers
}

// =========================================================================
// ASYNC GPU RASTERIZATION (Triple-Buffered CPU/GPU Overlap)
// =========================================================================
// Pattern follows metal_compute_bridge.mm::trace_shadow_rays_batched_bvh_async()

void GPURasterizer::rasterize_triangles_lit_async(
    const TriangleLit* triangles,
    uint32_t triangle_count,
    const void* lights,
    uint32_t light_count,
    const void* bvh_nodes,
    uint32_t bvh_count,
    const void* bvh_triangles,
    uint32_t bvh_triangle_count,
    // Tile binning data (optional - pass nullptr to disable)
    const uint32_t* tile_indices,
    const uint32_t* tile_offsets,
    const uint32_t* tile_counts,
    int tiles_x,
    int tiles_y,
    CompletionCallback callback,
    void* user_data)
{
    @autoreleasepool {
        if (!initialized_ || !triangles || triangle_count == 0) {
            return;
        }

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_lit_;
        id<MTLComputePipelineState> clearPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_clear_;

        // STEP 1: ACQUIRE BUFFER FROM SEMAPHORE (triple-buffering!)
        // Use timeout to prevent system freeze during resolution changes
        dispatch_semaphore_t semaphore = (__bridge dispatch_semaphore_t)buffer_semaphore_;

        // PERFORMANCE DIAGNOSIS: Measure semaphore wait time
        auto wait_start = std::chrono::high_resolution_clock::now();

        // 1.5s timeout — must stay under WindowServer's 3-6s watchdog threshold
        constexpr int64_t TIMEOUT_NS = 1500LL * 1000000LL;  // 1.5 seconds
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, TIMEOUT_NS);
        long wait_result = dispatch_semaphore_wait(semaphore, timeout);
        if (wait_result != 0) {
            std::cerr << "[GPU_RASTERIZER] TIMEOUT acquiring buffer slot (1.5s) - aborting frame" << std::endl;
            return;  // Abort this frame, don't crash the system
        }

        auto wait_duration = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - wait_start).count();

        // CPU STARVATION INVESTIGATION: Always log semaphore wait for 9-11 lights
        // (profiling showed the CPU starving at 10+ lights)
        static int semaphore_log_counter = 0;
        semaphore_log_counter++;
        bool should_log_semaphore = (light_count >= 9 && light_count <= 11) ||
                                   wait_duration > 5.0 ||
                                   (semaphore_log_counter % 60 == 1);

        if (should_log_semaphore) {
            std::cout << "[CPU_TIMING] Frame " << semaphore_log_counter
                      << " | Lights: " << light_count
                      << " | Semaphore wait: " << std::fixed << std::setprecision(2)
                      << wait_duration << "ms"
                      << (wait_duration > 5.0 ? " ⚠️  GPU PRESSURE" : "")
                      << std::endl;
        }

        // GPU MEMORY MONITORING: Log allocation every 120 frames (~2 seconds)
        if (semaphore_log_counter % 120 == 1) {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            size_t allocated_mb = dev.currentAllocatedSize / (1024 * 1024);
            size_t max_mb = dev.recommendedMaxWorkingSetSize / (1024 * 1024);
            float pct = (float)dev.currentAllocatedSize / (float)dev.recommendedMaxWorkingSetSize * 100.0f;
            std::cout << "[GPU_MEMORY] Frame " << semaphore_log_counter
                      << " | Allocated: " << allocated_mb << " MB / " << max_mb << " MB"
                      << " (" << std::fixed << std::setprecision(1) << pct << "%)"
                      << (pct > 75.0f ? " WARNING: HIGH PRESSURE" : "")
                      << std::endl;
        }

        // Get current buffer index and rotate for next call
        const int bufIdx = current_buffer_index_;
        current_buffer_index_ = (current_buffer_index_ + 1) % Optimizations::GPU_BUFFER_SLOTS;

        // STEP 2: Get or create buffers at this index
        size_t framebufferSize = width_ * height_ * sizeof(uint32_t);
        size_t depthBufferSize = width_ * height_ * sizeof(uint32_t);
        size_t trianglesSize = triangle_count * sizeof(TriangleLit);

        // Framebuffer buffer (async set)
        id<MTLBuffer> framebufferBuffer = nil;
        if (!framebuffer_buffer_async_[bufIdx] || framebuffer_capacity_async_[bufIdx] < framebufferSize) {
            if (framebuffer_buffer_async_[bufIdx]) {
                CFBridgingRelease(framebuffer_buffer_async_[bufIdx]);
            }
            framebufferBuffer = [device newBufferWithLength:framebufferSize
                                                     options:MTLResourceStorageModeShared];
            if (!framebufferBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async framebuffer" << std::endl;
                dispatch_semaphore_signal(semaphore);  // Release on error!
                return;
            }
            framebuffer_buffer_async_[bufIdx] = (__bridge_retained void*)framebufferBuffer;
            framebuffer_capacity_async_[bufIdx] = framebufferSize;
        } else {
            framebufferBuffer = (__bridge id<MTLBuffer>)framebuffer_buffer_async_[bufIdx];
        }

        // Depth buffer (async set)
        id<MTLBuffer> depthBufferGPU = nil;
        if (!depth_buffer_async_[bufIdx] || depth_capacity_async_[bufIdx] < depthBufferSize) {
            if (depth_buffer_async_[bufIdx]) {
                CFBridgingRelease(depth_buffer_async_[bufIdx]);
            }
            depthBufferGPU = [device newBufferWithLength:depthBufferSize
                                                  options:MTLResourceStorageModeShared];
            if (!depthBufferGPU) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async depth buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            depth_buffer_async_[bufIdx] = (__bridge_retained void*)depthBufferGPU;
            depth_capacity_async_[bufIdx] = depthBufferSize;
        } else {
            depthBufferGPU = (__bridge id<MTLBuffer>)depth_buffer_async_[bufIdx];
        }

        // Triangles buffer (async set)
        id<MTLBuffer> trianglesBuffer = nil;
        if (!triangles_buffer_async_[bufIdx] || triangles_capacity_async_[bufIdx] < trianglesSize) {
            if (triangles_buffer_async_[bufIdx]) {
                CFBridgingRelease(triangles_buffer_async_[bufIdx]);
            }
            trianglesBuffer = [device newBufferWithLength:trianglesSize
                                                   options:MTLResourceStorageModeShared];
            if (!trianglesBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async triangles buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            triangles_buffer_async_[bufIdx] = (__bridge_retained void*)trianglesBuffer;
            triangles_capacity_async_[bufIdx] = trianglesSize;
        } else {
            trianglesBuffer = (__bridge id<MTLBuffer>)triangles_buffer_async_[bufIdx];
        }
        // Copy triangle data
        memcpy([trianglesBuffer contents], triangles, trianglesSize);

        // Lights buffer (async set)
        id<MTLBuffer> lightsBuffer = nil;
        if (lights && light_count > 0) {
            size_t lightsSize = light_count * 40;  // LightData: pos(12) + strength(4) + radius(4) + size(4) + color(12) + pad(4) = 40
            if (!lights_buffer_async_[bufIdx] || lights_capacity_async_[bufIdx] < lightsSize) {
                if (lights_buffer_async_[bufIdx]) {
                    CFBridgingRelease(lights_buffer_async_[bufIdx]);
                }
                lightsBuffer = [device newBufferWithLength:lightsSize
                                                   options:MTLResourceStorageModeShared];
                if (!lightsBuffer) {
                    std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async lights buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                lights_buffer_async_[bufIdx] = (__bridge_retained void*)lightsBuffer;
                lights_capacity_async_[bufIdx] = lightsSize;
            } else {
                lightsBuffer = (__bridge id<MTLBuffer>)lights_buffer_async_[bufIdx];
            }
            memcpy([lightsBuffer contents], lights, lightsSize);
        } else {
            // Dummy light buffer
            if (!lights_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                lightsBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                  options:MTLResourceStorageModeShared];
                lights_buffer_async_[bufIdx] = (__bridge_retained void*)lightsBuffer;
                lights_capacity_async_[bufIdx] = sizeof(float);
            } else {
                lightsBuffer = (__bridge id<MTLBuffer>)lights_buffer_async_[bufIdx];
            }
        }

        // BVH nodes buffer (async set)
        id<MTLBuffer> bvhNodesBuffer = nil;
        if (bvh_nodes && bvh_count > 0) {
            size_t bvhNodesSize = bvh_count * 48;  // BVHNode is 48 bytes
            if (!bvh_nodes_buffer_async_[bufIdx] || bvh_nodes_capacity_async_[bufIdx] < bvhNodesSize) {
                if (bvh_nodes_buffer_async_[bufIdx]) {
                    CFBridgingRelease(bvh_nodes_buffer_async_[bufIdx]);
                }
                bvhNodesBuffer = [device newBufferWithLength:bvhNodesSize
                                                     options:MTLResourceStorageModeShared];
                if (!bvhNodesBuffer) {
                    std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async BVH nodes buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                bvh_nodes_buffer_async_[bufIdx] = (__bridge_retained void*)bvhNodesBuffer;
                bvh_nodes_capacity_async_[bufIdx] = bvhNodesSize;
            } else {
                bvhNodesBuffer = (__bridge id<MTLBuffer>)bvh_nodes_buffer_async_[bufIdx];
            }
            memcpy([bvhNodesBuffer contents], bvh_nodes, bvhNodesSize);
        } else {
            // Dummy BVH buffer
            if (!bvh_nodes_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                bvhNodesBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                    options:MTLResourceStorageModeShared];
                bvh_nodes_buffer_async_[bufIdx] = (__bridge_retained void*)bvhNodesBuffer;
                bvh_nodes_capacity_async_[bufIdx] = sizeof(float);
            } else {
                bvhNodesBuffer = (__bridge id<MTLBuffer>)bvh_nodes_buffer_async_[bufIdx];
            }
        }

        // BVH triangles buffer (async set)
        id<MTLBuffer> bvhTrianglesBuffer = nil;
        if (bvh_triangles && bvh_triangle_count > 0) {
            size_t bvhTrianglesSize = bvh_triangle_count * 64;  // Triangle is 64 bytes (48 vertices + 16 color+padding)
            if (!bvh_triangles_buffer_async_[bufIdx] || bvh_triangles_capacity_async_[bufIdx] < bvhTrianglesSize) {
                if (bvh_triangles_buffer_async_[bufIdx]) {
                    CFBridgingRelease(bvh_triangles_buffer_async_[bufIdx]);
                }
                bvhTrianglesBuffer = [device newBufferWithLength:bvhTrianglesSize
                                                         options:MTLResourceStorageModeShared];
                if (!bvhTrianglesBuffer) {
                    std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async BVH triangles buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                bvh_triangles_buffer_async_[bufIdx] = (__bridge_retained void*)bvhTrianglesBuffer;
                bvh_triangles_capacity_async_[bufIdx] = bvhTrianglesSize;
            } else {
                bvhTrianglesBuffer = (__bridge id<MTLBuffer>)bvh_triangles_buffer_async_[bufIdx];
            }
            memcpy([bvhTrianglesBuffer contents], bvh_triangles, bvhTrianglesSize);
        } else {
            // Dummy BVH triangles buffer
            if (!bvh_triangles_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                bvhTrianglesBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                        options:MTLResourceStorageModeShared];
                bvh_triangles_buffer_async_[bufIdx] = (__bridge_retained void*)bvhTrianglesBuffer;
                bvh_triangles_capacity_async_[bufIdx] = sizeof(float);
            } else {
                bvhTrianglesBuffer = (__bridge id<MTLBuffer>)bvh_triangles_buffer_async_[bufIdx];
            }
        }

        // Tile binning buffers (async set) - GPU bandwidth optimization
        id<MTLBuffer> tileIndicesBuffer = nil;
        id<MTLBuffer> tileOffsetsBuffer = nil;
        id<MTLBuffer> tileCountsBuffer = nil;

        // Determine total number of tiles needed (for bounds checking in dummy buffers)
        int total_tiles = (tiles_x > 0 && tiles_y > 0) ? (tiles_x * tiles_y) : 0;

        if (tile_indices && tile_offsets && tile_counts && total_tiles > 0) {
            // tile_indices: variable size based on triangle distribution
            size_t tile_indices_size = 0;
            for (int i = 0; i < total_tiles; ++i) {
                tile_indices_size += tile_counts[i];
            }
            tile_indices_size *= sizeof(uint32_t);

            if (tile_indices_size > 0) {
                if (!tile_indices_buffer_async_[bufIdx] || tile_indices_capacity_async_[bufIdx] < tile_indices_size) {
                    if (tile_indices_buffer_async_[bufIdx]) {
                        CFBridgingRelease(tile_indices_buffer_async_[bufIdx]);
                    }
                    tileIndicesBuffer = [device newBufferWithLength:tile_indices_size
                                                            options:MTLResourceStorageModeShared];
                    if (!tileIndicesBuffer) {
                        std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async tile indices buffer" << std::endl;
                        dispatch_semaphore_signal(semaphore);
                        return;
                    }
                    tile_indices_buffer_async_[bufIdx] = (__bridge_retained void*)tileIndicesBuffer;
                    tile_indices_capacity_async_[bufIdx] = tile_indices_size;
                } else {
                    tileIndicesBuffer = (__bridge id<MTLBuffer>)tile_indices_buffer_async_[bufIdx];
                }
                memcpy([tileIndicesBuffer contents], tile_indices, tile_indices_size);
            }

            // tile_offsets: fixed size (one per tile)
            size_t tile_offsets_size = total_tiles * sizeof(uint32_t);
            if (!tile_offsets_buffer_async_[bufIdx] || tile_offsets_capacity_async_[bufIdx] < tile_offsets_size) {
                if (tile_offsets_buffer_async_[bufIdx]) {
                    CFBridgingRelease(tile_offsets_buffer_async_[bufIdx]);
                }
                tileOffsetsBuffer = [device newBufferWithLength:tile_offsets_size
                                                        options:MTLResourceStorageModeShared];
                if (!tileOffsetsBuffer) {
                    std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async tile offsets buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                tile_offsets_buffer_async_[bufIdx] = (__bridge_retained void*)tileOffsetsBuffer;
                tile_offsets_capacity_async_[bufIdx] = tile_offsets_size;
            } else {
                tileOffsetsBuffer = (__bridge id<MTLBuffer>)tile_offsets_buffer_async_[bufIdx];
            }
            memcpy([tileOffsetsBuffer contents], tile_offsets, tile_offsets_size);

            // tile_counts: fixed size (one per tile)
            size_t tile_counts_size = total_tiles * sizeof(uint32_t);
            if (!tile_counts_buffer_async_[bufIdx] || tile_counts_capacity_async_[bufIdx] < tile_counts_size) {
                if (tile_counts_buffer_async_[bufIdx]) {
                    CFBridgingRelease(tile_counts_buffer_async_[bufIdx]);
                }
                tileCountsBuffer = [device newBufferWithLength:tile_counts_size
                                                       options:MTLResourceStorageModeShared];
                if (!tileCountsBuffer) {
                    std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create async tile counts buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                tile_counts_buffer_async_[bufIdx] = (__bridge_retained void*)tileCountsBuffer;
                tile_counts_capacity_async_[bufIdx] = tile_counts_size;
            } else {
                tileCountsBuffer = (__bridge id<MTLBuffer>)tile_counts_buffer_async_[bufIdx];
            }
            memcpy([tileCountsBuffer contents], tile_counts, tile_counts_size);
        } else {
            // No tile binning - create minimal dummy buffers
            if (!tile_indices_buffer_async_[bufIdx]) {
                uint32_t dummy = 0;
                tileIndicesBuffer = [device newBufferWithBytes:&dummy length:sizeof(uint32_t)
                                                       options:MTLResourceStorageModeShared];
                tile_indices_buffer_async_[bufIdx] = (__bridge_retained void*)tileIndicesBuffer;
                tile_indices_capacity_async_[bufIdx] = sizeof(uint32_t);
            } else {
                tileIndicesBuffer = (__bridge id<MTLBuffer>)tile_indices_buffer_async_[bufIdx];
            }

            if (!tile_offsets_buffer_async_[bufIdx]) {
                uint32_t dummy = 0;
                tileOffsetsBuffer = [device newBufferWithBytes:&dummy length:sizeof(uint32_t)
                                                       options:MTLResourceStorageModeShared];
                tile_offsets_buffer_async_[bufIdx] = (__bridge_retained void*)tileOffsetsBuffer;
                tile_offsets_capacity_async_[bufIdx] = sizeof(uint32_t);
            } else {
                tileOffsetsBuffer = (__bridge id<MTLBuffer>)tile_offsets_buffer_async_[bufIdx];
            }

            if (!tile_counts_buffer_async_[bufIdx]) {
                uint32_t dummy = 0;
                tileCountsBuffer = [device newBufferWithBytes:&dummy length:sizeof(uint32_t)
                                                      options:MTLResourceStorageModeShared];
                tile_counts_buffer_async_[bufIdx] = (__bridge_retained void*)tileCountsBuffer;
                tile_counts_capacity_async_[bufIdx] = sizeof(uint32_t);
            } else {
                tileCountsBuffer = (__bridge id<MTLBuffer>)tile_counts_buffer_async_[bufIdx];
            }
        }

        // STEP 3: Create constant buffers (temporary, autoreleased)
        unsigned int width_uint = (unsigned int)width_;
        unsigned int height_uint = (unsigned int)height_;
        id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                        length:sizeof(unsigned int)
                                                       options:MTLResourceStorageModeShared];
        id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                         length:sizeof(unsigned int)
                                                        options:MTLResourceStorageModeShared];
        id<MTLBuffer> triangleCountBuffer = [device newBufferWithBytes:&triangle_count
                                                                length:sizeof(unsigned int)
                                                               options:MTLResourceStorageModeShared];
        id<MTLBuffer> lightCountBuffer = [device newBufferWithBytes:&light_count
                                                             length:sizeof(unsigned int)
                                                            options:MTLResourceStorageModeShared];
        unsigned int bvh_count_uint = (unsigned int)bvh_count;
        id<MTLBuffer> bvhNodeCountBuffer = [device newBufferWithBytes:&bvh_count_uint
                                                               length:sizeof(unsigned int)
                                                              options:MTLResourceStorageModeShared];
        unsigned int bvh_triangle_count_uint = (unsigned int)bvh_triangle_count;
        id<MTLBuffer> bvhTriangleCountBuffer = [device newBufferWithBytes:&bvh_triangle_count_uint
                                                                   length:sizeof(unsigned int)
                                                                  options:MTLResourceStorageModeShared];

        // Tile binning scalar buffers (tiles_x, tiles_y)
        unsigned int tiles_x_uint = (unsigned int)tiles_x;
        unsigned int tiles_y_uint = (unsigned int)tiles_y;
        id<MTLBuffer> tilesXBuffer = [device newBufferWithBytes:&tiles_x_uint
                                                          length:sizeof(unsigned int)
                                                         options:MTLResourceStorageModeShared];
        id<MTLBuffer> tilesYBuffer = [device newBufferWithBytes:&tiles_y_uint
                                                          length:sizeof(unsigned int)
                                                         options:MTLResourceStorageModeShared];

        // STEP 4: Create command buffer
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

        // STEP 5: Clear buffers (blit + compute, same as sync)

        // Clear depth buffer — SIGNED init (see depth_encoding.metal).
        id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
        [blitEncoder fillBuffer:depthBufferGPU
                          range:NSMakeRange(0, depthBufferSize)
                          value:0x7F];  // → 0x7F7F7F7F = DEPTH_INIT_VALUE
        [blitEncoder endEncoding];

        // Clear framebuffer using compute shader
        uint32_t clear_color = 0xFF0A0A0F;  // BGRA(10,10,15,255)
        id<MTLBuffer> clearColorBuffer = [device newBufferWithBytes:&clear_color
                                                             length:sizeof(uint32_t)
                                                            options:MTLResourceStorageModeShared];

        id<MTLComputeCommandEncoder> clearEncoder = [commandBuffer computeCommandEncoder];
        [clearEncoder setComputePipelineState:clearPipeline];
        [clearEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
        [clearEncoder setBuffer:widthBuffer offset:0 atIndex:1];
        [clearEncoder setBuffer:heightBuffer offset:0 atIndex:2];
        [clearEncoder setBuffer:clearColorBuffer offset:0 atIndex:3];

        MTLSize clearThreadsPerGrid = MTLSizeMake(width_, height_, 1);
        MTLSize clearThreadsPerThreadgroup = MTLSizeMake(Optimizations::GPU_THREADS_CLEAR, Optimizations::GPU_THREADS_CLEAR, 1);
        [clearEncoder dispatchThreads:clearThreadsPerGrid
                  threadsPerThreadgroup:clearThreadsPerThreadgroup];
        [clearEncoder endEncoding];

        // STEP 6: Rasterization encoder
        id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
        [computeEncoder setComputePipelineState:pipeline];

        // Set all buffers
        [computeEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
        [computeEncoder setBuffer:depthBufferGPU offset:0 atIndex:1];
        [computeEncoder setBuffer:widthBuffer offset:0 atIndex:2];
        [computeEncoder setBuffer:heightBuffer offset:0 atIndex:3];
        [computeEncoder setBuffer:trianglesBuffer offset:0 atIndex:4];
        [computeEncoder setBuffer:triangleCountBuffer offset:0 atIndex:5];
        [computeEncoder setBuffer:lightsBuffer offset:0 atIndex:6];
        [computeEncoder setBuffer:lightCountBuffer offset:0 atIndex:7];
        [computeEncoder setBuffer:bvhNodesBuffer offset:0 atIndex:8];
        [computeEncoder setBuffer:bvhNodeCountBuffer offset:0 atIndex:9];
        [computeEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10];
        [computeEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11];
        // Tile binning buffers (GPU bandwidth optimization - indices 12-16)
        [computeEncoder setBuffer:tileIndicesBuffer offset:0 atIndex:12];
        [computeEncoder setBuffer:tileOffsetsBuffer offset:0 atIndex:13];
        [computeEncoder setBuffer:tileCountsBuffer offset:0 atIndex:14];
        [computeEncoder setBuffer:tilesXBuffer offset:0 atIndex:15];
        [computeEncoder setBuffer:tilesYBuffer offset:0 atIndex:16];

        // Dispatch GPU threads (one thread per pixel)
        MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
        MTLSize threadsPerThreadgroup = MTLSizeMake(Optimizations::GPU_THREADS_FORWARD, Optimizations::GPU_THREADS_FORWARD, 1);
        [computeEncoder dispatchThreads:threadsPerGrid
                  threadsPerThreadgroup:threadsPerThreadgroup];
        [computeEncoder endEncoding];

        // STEP 7: ADD COMPLETION HANDLER (ASYNC!)
        // Copy buffer references for block capture
        id<MTLBuffer> framebufferCaptured = framebufferBuffer;
        id<MTLBuffer> depthCaptured = depthBufferGPU;
        int w = width_;
        int h = height_;
        CompletionCallback callback_copy = callback;
        void* user_data_copy = user_data;

        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            // GPU FINISHED! (runs on Metal's completion thread)
            logCommandBufferError(cb, "Pass 3 (Apply Lighting)");
            if (cb.status == MTLCommandBufferStatusError) {
                // Skip callback on error — buffer contents are undefined
            } else {
                // Call user's completion callback (on background thread!)
                if (callback_copy) {
                    uint32_t* fb = (uint32_t*)[framebufferCaptured contents];
                    uint32_t* db = (uint32_t*)[depthCaptured contents];
                    void* gb = nullptr;  // Forward rendering doesn't use G-buffer
                    callback_copy(fb, db, gb, w, h, user_data_copy);
                }
            }

            // RELEASE BUFFER BACK TO POOL
            dispatch_semaphore_signal(semaphore);
        }];

        // STEP 8: COMMIT AND RETURN IMMEDIATELY (NO WAIT!)
        [commandBuffer commit];

        // CPU continues immediately - no waitUntilCompleted!
        // GPU processes rays while CPU works on next frame
    }
}

// =========================================================================
// DEFERRED RENDERING (3-Pass Architecture) - ASYNC
// =========================================================================
// ARCHITECTURE: Separates geometry, shadows, lighting into 3 coherent passes
// PERFORMANCE: 19× faster than forward rendering (coherent BVH traversal)
//
// Pass 1: Rasterize G-buffer (geometry data for visible pixels only)
// Pass 2: Trace shadow rays (coherent BVH traversal on visible pixels)
// Pass 3: Apply lighting (simple math: G-buffer + shadow results → framebuffer)

void GPURasterizer::rasterize_triangles_deferred_async(
    const TriangleLit* triangles,
    uint32_t triangle_count,
    const void* lights,
    uint32_t light_count,
    const void* bvh_nodes,
    uint32_t bvh_count,
    const void* bvh_triangles,
    uint32_t bvh_triangle_count,
    // Entity BVH data (optional - pass nullptr to use flat BVH)
    const void* entity_bvh_nodes,
    uint32_t entity_node_count,
    const void* directional_groups,
    uint32_t dir_group_count,
    // Tile binning data (optional - pass nullptr to disable)
    const uint32_t* tile_indices,
    const uint32_t* tile_offsets,
    const uint32_t* tile_counts,
    int tiles_x,
    int tiles_y,
    // Light source mapping (for emissive rendering in Pass 3)
    const uint8_t* is_light_source_map,
    uint32_t map_size,
    // Particle transforms (for pattern rendering in Pass 3)
    const void* particle_transforms,
    uint32_t particle_count,
    CompletionCallback callback,
    void* user_data)
{
    @autoreleasepool {
        // Device-lost guard: stop submitting work to a revoked GPU
        if (gpu_device_lost_.load(std::memory_order_relaxed)) {
            std::cerr << "[GPU_RASTERIZER] Skipping frame — GPU device lost" << std::endl;
            return;
        }

        // Pointer for completion handlers to set on AccessRevoked
        std::atomic<bool>* device_lost_ptr = &gpu_device_lost_;

        // GPU memory leak monitor: log Metal allocation delta per frame
        {
            static int mem_log = 0;
            static size_t prev_alloc = 0;
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            size_t alloc_now = dev.currentAllocatedSize;
            if (mem_log % 60 == 0) {
                size_t delta = alloc_now > prev_alloc ? alloc_now - prev_alloc : 0;
                std::cout << "[GPU_MEM] Frame " << mem_log << ": Metal = "
                          << alloc_now / (1024*1024) << " MB (delta since last log: +"
                          << delta / (1024*1024) << " MB)" << std::endl;
            }
            prev_alloc = alloc_now;
            mem_log++;
        }

        // ITERATION 0: Verify this function is being called
        static int call_count = 0;
        if (call_count < 3 && light_count > 0) {
            std::cout << "[ITER0_ENTRY] rasterize_triangles_deferred_async called! count=" << call_count
                      << " lights=" << light_count << std::endl;
            std::cout.flush();
            call_count++;
        }

        if (!initialized_ || !triangles || triangle_count == 0) {
            return;
        }

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLComputePipelineState> gbufferPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_gbuffer_;
        id<MTLComputePipelineState> shadowsPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_shadows_;
        id<MTLComputePipelineState> lightingPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_lighting_;
        id<MTLComputePipelineState> clearPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_clear_;

        // STEP 1: ACQUIRE BUFFER FROM SEMAPHORE (triple-buffering!)
        // Use timeout to prevent system freeze during resolution changes
        dispatch_semaphore_t semaphore = (__bridge dispatch_semaphore_t)buffer_semaphore_;

        auto wait_start = std::chrono::high_resolution_clock::now();

        // 1.5s timeout — must stay under WindowServer's 3-6s watchdog threshold
        constexpr int64_t TIMEOUT_NS = 1500LL * 1000000LL;  // 1.5 seconds
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, TIMEOUT_NS);
        ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::RenderSlotWait);
        long wait_result = dispatch_semaphore_wait(semaphore, timeout);
        ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::RenderSlotWait);
        if (wait_result != 0) {
            std::cerr << "[GPU_RASTERIZER] TIMEOUT in deferred_full (1.5s) - aborting frame" << std::endl;
            return;  // Abort this frame, don't crash the system
        }

        auto wait_duration = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - wait_start).count();

        // INSTRUMENTATION: Always log GPU wait time + context for spike analysis
        static int frame_counter = 0;
        frame_counter++;

        // Capture light positions for correlation analysis
        std::vector<std::array<float, 3>> light_positions;
        if (lights && light_count > 0) {
            const LightData* light_data = static_cast<const LightData*>(lights);
            for (uint32_t i = 0; i < light_count; ++i) {
                light_positions.push_back({
                    light_data[i].position[0],
                    light_data[i].position[1],
                    light_data[i].position[2]
                });
            }
        }

        // ALWAYS log wait time when profiling is enabled to understand GPU sync
        // This is Path 4E from GPU_OPTIMIZATION_NEXT_STEPS.md
        static int wait_log_counter = 0;
        wait_log_counter++;
        const bool should_log_wait = Optimizations::ENABLE_PROFILING &&
                                     ((wait_log_counter % 60) == 0 || wait_duration > 5.0);

        if (should_log_wait) {
            std::cout << "[GPU_SYNC] Frame " << frame_counter
                      << " | Semaphore wait: " << std::fixed << std::setprecision(2) << wait_duration << "ms"
                      << " | Lights: " << light_count
                      << " | Tris: " << triangle_count
                      << (wait_duration > 30.0 ? " *** SPIKE: GPU not ready! ***" : "")
                      << std::endl;

            // On spike frames, log light positions for pattern analysis
            if (wait_duration > 30.0 && light_count > 0) {
                std::cout << "  [SPIKE_LIGHTS] Positions: ";
                for (uint32_t i = 0; i < light_count; ++i) {
                    std::cout << "L" << i << "=("
                              << std::fixed << std::setprecision(1)
                              << light_positions[i][0] << ","
                              << light_positions[i][1] << ","
                              << light_positions[i][2] << ") ";
                }
                std::cout << std::endl;
            }
        }

        const int bufIdx = current_buffer_index_;
        current_buffer_index_ = (current_buffer_index_ + 1) % Optimizations::GPU_BUFFER_SLOTS;

        // STEP 2: Allocate/get all buffers
        size_t framebufferSize = width_ * height_ * sizeof(uint32_t);
        size_t depthBufferSize = width_ * height_ * sizeof(uint32_t);
        size_t trianglesSize = triangle_count * sizeof(TriangleLit);
        size_t gbufferSize = width_ * height_ * 32;  // 32 bytes per pixel (GBufferPixel: world_pos(12) + normal(12) + base_color(4) + particle_id(4))
        size_t shadowResultsSize = shadow_width_ * shadow_height_ * sizeof(float);  // Phase 1: Reduced resolution shadow buffer
        size_t lightColorSize = shadow_width_ * shadow_height_ * sizeof(float) * 4;  // Per-pixel light color ratio (float4: RGB + pad)

        // Framebuffer buffer (async set)
        id<MTLBuffer> framebufferBuffer = nil;
        if (!framebuffer_buffer_async_[bufIdx] || framebuffer_capacity_async_[bufIdx] < framebufferSize) {
            if (framebuffer_buffer_async_[bufIdx]) {
                CFBridgingRelease(framebuffer_buffer_async_[bufIdx]);
            }
            framebufferBuffer = [device newBufferWithLength:framebufferSize
                                                     options:MTLResourceStorageModeShared];
            if (!framebufferBuffer) {
                std::cerr << "[GPU_DEFERRED] FATAL: Failed to create framebuffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            framebuffer_buffer_async_[bufIdx] = (__bridge_retained void*)framebufferBuffer;
            framebuffer_capacity_async_[bufIdx] = framebufferSize;
        } else {
            framebufferBuffer = (__bridge id<MTLBuffer>)framebuffer_buffer_async_[bufIdx];
        }

        // Depth buffer (async set)
        id<MTLBuffer> depthBufferGPU = nil;
        if (!depth_buffer_async_[bufIdx] || depth_capacity_async_[bufIdx] < depthBufferSize) {
            if (depth_buffer_async_[bufIdx]) {
                CFBridgingRelease(depth_buffer_async_[bufIdx]);
            }
            depthBufferGPU = [device newBufferWithLength:depthBufferSize
                                                  options:MTLResourceStorageModeShared];
            if (!depthBufferGPU) {
                std::cerr << "[GPU_DEFERRED] FATAL: Failed to create depth buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            depth_buffer_async_[bufIdx] = (__bridge_retained void*)depthBufferGPU;
            depth_capacity_async_[bufIdx] = depthBufferSize;
        } else {
            depthBufferGPU = (__bridge id<MTLBuffer>)depth_buffer_async_[bufIdx];
        }

        // G-buffer (async set) - NEW for deferred rendering
        id<MTLBuffer> gbufferBuffer = nil;
        if (!gbuffer_buffer_async_[bufIdx] || gbuffer_capacity_async_[bufIdx] < gbufferSize) {
            if (gbuffer_buffer_async_[bufIdx]) {
                CFBridgingRelease(gbuffer_buffer_async_[bufIdx]);
            }
            gbufferBuffer = [device newBufferWithLength:gbufferSize
                                                options:MTLResourceStorageModeShared];
            if (!gbufferBuffer) {
                std::cerr << "[GPU_DEFERRED] FATAL: Failed to create G-buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            gbuffer_buffer_async_[bufIdx] = (__bridge_retained void*)gbufferBuffer;
            gbuffer_capacity_async_[bufIdx] = gbufferSize;
        } else {
            gbufferBuffer = (__bridge id<MTLBuffer>)gbuffer_buffer_async_[bufIdx];
        }

        // Shadow results buffer (async set) - NEW for deferred rendering
        id<MTLBuffer> shadowResultsBuffer = nil;
        if (!shadow_results_buffer_async_[bufIdx] || shadow_results_capacity_async_[bufIdx] < shadowResultsSize) {
            if (shadow_results_buffer_async_[bufIdx]) {
                CFBridgingRelease(shadow_results_buffer_async_[bufIdx]);
            }
            shadowResultsBuffer = [device newBufferWithLength:shadowResultsSize
                                                       options:MTLResourceStorageModeShared];
            if (!shadowResultsBuffer) {
                std::cerr << "[GPU_DEFERRED] FATAL: Failed to create shadow results buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            shadow_results_buffer_async_[bufIdx] = (__bridge_retained void*)shadowResultsBuffer;
            shadow_results_capacity_async_[bufIdx] = shadowResultsSize;
        } else {
            shadowResultsBuffer = (__bridge id<MTLBuffer>)shadow_results_buffer_async_[bufIdx];
        }

        // Light color ratio buffer (async set) - per-pixel RGB color ratio from deterministic shadow kernel
        id<MTLBuffer> lightColorBuffer = nil;
        if (!light_color_buffer_async_[bufIdx] || light_color_capacity_async_[bufIdx] < lightColorSize) {
            if (light_color_buffer_async_[bufIdx]) {
                CFBridgingRelease(light_color_buffer_async_[bufIdx]);
            }
            lightColorBuffer = [device newBufferWithLength:lightColorSize
                                                    options:MTLResourceStorageModeShared];
            if (!lightColorBuffer) {
                std::cerr << "[GPU_DEFERRED] WARNING: Failed to create light color buffer" << std::endl;
                // Non-fatal: apply_lighting handles nullptr fallback (white light)
            } else {
                light_color_buffer_async_[bufIdx] = (__bridge_retained void*)lightColorBuffer;
                light_color_capacity_async_[bufIdx] = lightColorSize;
            }
        } else {
            lightColorBuffer = (__bridge id<MTLBuffer>)light_color_buffer_async_[bufIdx];
        }

        // Shadow denoise output buffer (pass 2.05)
        id<MTLBuffer> shadowDenoisedBuffer = nil;
        if (!shadow_denoised_buffer_async_[bufIdx] || shadow_denoised_capacity_async_[bufIdx] < shadowResultsSize) {
            if (shadow_denoised_buffer_async_[bufIdx]) {
                CFBridgingRelease(shadow_denoised_buffer_async_[bufIdx]);
            }
            shadowDenoisedBuffer = [device newBufferWithLength:shadowResultsSize
                                                       options:MTLResourceStorageModeShared];
            if (!shadowDenoisedBuffer) {
                std::cerr << "[GPU_DEFERRED] WARNING: Failed to create shadow denoised buffer" << std::endl;
                shadowDenoisedBuffer = shadowResultsBuffer;  // Fallback: no denoise
            } else {
                shadow_denoised_buffer_async_[bufIdx] = (__bridge_retained void*)shadowDenoisedBuffer;
                shadow_denoised_capacity_async_[bufIdx] = shadowResultsSize;
            }
        } else {
            shadowDenoisedBuffer = (__bridge id<MTLBuffer>)shadow_denoised_buffer_async_[bufIdx];
        }


        // SSDO buffers (full resolution, RGB bounce + AO per pixel;
        // element is half4 or float4 per SSDO_HALF_PRECISION)
        constexpr size_t ssdo_elem = Optimizations::SSDO_HALF_PRECISION ? 8 : 16;
        size_t ssaoResultsSize = width_ * height_ * ssdo_elem;
        id<MTLBuffer> ssaoResultsBuffer = nil;
        id<MTLBuffer> ssaoDenoisedBuffer = nil;
        if constexpr (Optimizations::USE_SSAO) {
            if (!ssao_results_buffer_async_[bufIdx] || ssao_results_capacity_async_[bufIdx] < ssaoResultsSize) {
                if (ssao_results_buffer_async_[bufIdx]) CFBridgingRelease(ssao_results_buffer_async_[bufIdx]);
                ssaoResultsBuffer = [device newBufferWithLength:ssaoResultsSize options:MTLResourceStorageModeShared];
                if (ssaoResultsBuffer) {
                    ssao_results_buffer_async_[bufIdx] = (__bridge_retained void*)ssaoResultsBuffer;
                    ssao_results_capacity_async_[bufIdx] = ssaoResultsSize;
                }
            } else {
                ssaoResultsBuffer = (__bridge id<MTLBuffer>)ssao_results_buffer_async_[bufIdx];
            }
            if (!ssao_denoised_buffer_async_[bufIdx] || ssao_denoised_capacity_async_[bufIdx] < ssaoResultsSize) {
                if (ssao_denoised_buffer_async_[bufIdx]) CFBridgingRelease(ssao_denoised_buffer_async_[bufIdx]);
                ssaoDenoisedBuffer = [device newBufferWithLength:ssaoResultsSize options:MTLResourceStorageModeShared];
                if (ssaoDenoisedBuffer) {
                    ssao_denoised_buffer_async_[bufIdx] = (__bridge_retained void*)ssaoDenoisedBuffer;
                    ssao_denoised_capacity_async_[bufIdx] = ssaoResultsSize;
                } else {
                    ssaoDenoisedBuffer = ssaoResultsBuffer;
                }
            } else {
                ssaoDenoisedBuffer = (__bridge id<MTLBuffer>)ssao_denoised_buffer_async_[bufIdx];
            }
        }

        // Phase 2: Temporal lighting history buffer (persistent across frames, NOT per-buffer)
        // This stores the previous frame's lighting results for temporal distribution
        // Only needed for PCSS mode (stochastic shadows with temporal accumulation)
        id<MTLBuffer> temporalLightingBuffer = nil;
        if constexpr (Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::PCSS) {
        if (!temporal_lighting_buffer_ || temporal_lighting_capacity_ < shadowResultsSize) {
            if (temporal_lighting_buffer_) {
                CFBridgingRelease(temporal_lighting_buffer_);
            }

            // Use same storage mode as shadow buffer for consistency
            MTLResourceOptions temporalOptions = Optimizations::USE_GPU_PRIVATE_BUFFERS
                ? MTLResourceStorageModePrivate
                : MTLResourceStorageModeShared;

            temporalLightingBuffer = [device newBufferWithLength:shadowResultsSize
                                                          options:temporalOptions];
            if (!temporalLightingBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create temporal lighting buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            [temporalLightingBuffer retain];
            temporal_lighting_buffer_ = (__bridge void*)temporalLightingBuffer;
            temporal_lighting_capacity_ = shadowResultsSize;

            NSLog(@"[TEMPORAL] Allocated temporal lighting buffer: %zu bytes (%.2f MB)",
                  shadowResultsSize, shadowResultsSize / (1024.0 * 1024.0));

            // CRITICAL: Clear temporal buffer on allocation
            // WHY: When TEMPORAL_FRAME_COUNT changes (e.g., 3→1), buffer retains stale diagonal stripe pattern
            // IMPACT: Prevents ghosting artifacts from previous temporal distribution patterns
            if (temporalOptions == MTLResourceStorageModeShared) {
                // Shared memory: Clear via CPU
                memset([temporalLightingBuffer contents], 0, shadowResultsSize);
                NSLog(@"[TEMPORAL] Cleared temporal buffer via CPU memset");
            } else {
                // Private GPU memory: Must clear via blit encoder in next frame
                // (will be cleared below in buffer clearing section)
                NSLog(@"[TEMPORAL] Will clear temporal buffer via GPU blit in first frame");
            }
        } else {
            temporalLightingBuffer = (__bridge id<MTLBuffer>)temporal_lighting_buffer_;
        }
        } // end PCSS-only temporal buffer


        // Soft shadow motion detection + sample count: Only needed for PCSS mode
        id<MTLBuffer> prevParticleIdBuffer = nil;
        id<MTLBuffer> sampleCountBuffer = nil;
        if constexpr (Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::PCSS) {
        // Previous frame particle IDs (for ghosting fix)
        // Used to detect surface changes at each pixel - if particle_id changed, skip temporal blending
        size_t particleIdBufferSize = shadow_width_ * shadow_height_ * sizeof(uint32_t);
        if (!prev_particle_id_buffer_ || prev_particle_id_capacity_ < particleIdBufferSize) {
            if (prev_particle_id_buffer_) {
                CFBridgingRelease(prev_particle_id_buffer_);
            }

            MTLResourceOptions particleIdOptions = Optimizations::USE_GPU_PRIVATE_BUFFERS
                ? MTLResourceStorageModePrivate
                : MTLResourceStorageModeShared;

            prevParticleIdBuffer = [device newBufferWithLength:particleIdBufferSize
                                                       options:particleIdOptions];
            if (!prevParticleIdBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create prev particle ID buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            [prevParticleIdBuffer retain];
            prev_particle_id_buffer_ = (__bridge void*)prevParticleIdBuffer;
            prev_particle_id_capacity_ = particleIdBufferSize;

            NSLog(@"[SOFT_SHADOW] Allocated prev_particle_id buffer: %zu bytes (%.2f MB)",
                  particleIdBufferSize, particleIdBufferSize / (1024.0 * 1024.0));

            // Clear on allocation
            if (particleIdOptions == MTLResourceStorageModeShared) {
                memset([prevParticleIdBuffer contents], 0xFF, particleIdBufferSize);  // 0xFFFFFFFF = invalid particle ID
            }
        } else {
            prevParticleIdBuffer = (__bridge id<MTLBuffer>)prev_particle_id_buffer_;
        }

        // Soft shadow running average: per-pixel sample count for convergence
        // After SOFT_SHADOW_MAX_SAMPLES frames, weight = 1/16 → oscillation dampened
        size_t sampleCountBufferSize = shadow_width_ * shadow_height_ * sizeof(uint32_t);
        if (!sample_count_buffer_ || sample_count_capacity_ < sampleCountBufferSize) {
            if (sample_count_buffer_) {
                CFBridgingRelease(sample_count_buffer_);
            }

            MTLResourceOptions sampleCountOptions = Optimizations::USE_GPU_PRIVATE_BUFFERS
                ? MTLResourceStorageModePrivate
                : MTLResourceStorageModeShared;

            sampleCountBuffer = [device newBufferWithLength:sampleCountBufferSize
                                                    options:sampleCountOptions];
            if (!sampleCountBuffer) {
                std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create sample count buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            [sampleCountBuffer retain];
            sample_count_buffer_ = (__bridge void*)sampleCountBuffer;
            sample_count_capacity_ = sampleCountBufferSize;

            NSLog(@"[SOFT_SHADOW] Allocated sample_count buffer: %zu bytes (%.2f MB)",
                  sampleCountBufferSize, sampleCountBufferSize / (1024.0 * 1024.0));

            // Clear to 0 on allocation (all pixels start with count=0)
            if (sampleCountOptions == MTLResourceStorageModeShared) {
                memset([sampleCountBuffer contents], 0, sampleCountBufferSize);
            }
        } else {
            sampleCountBuffer = (__bridge id<MTLBuffer>)sample_count_buffer_;
        }
        } // end PCSS-only temporal buffers

        // Blocker distance buffer: per-pixel closest blocker distance (for deterministic penumbra)
        // Needed for all modes except PCSS (which computes penumbra in-kernel via stochastic sampling)
        id<MTLBuffer> blockerDistanceBuffer = nil;
        if constexpr (Optimizations::PENUMBRA_MODE != Optimizations::PenumbraMode::PCSS) {
        if (!blocker_distance_buffer_async_[bufIdx] || blocker_distance_capacity_async_[bufIdx] < shadowResultsSize) {
            if (blocker_distance_buffer_async_[bufIdx]) {
                CFBridgingRelease(blocker_distance_buffer_async_[bufIdx]);
            }
            blockerDistanceBuffer = [device newBufferWithLength:shadowResultsSize
                                                        options:MTLResourceStorageModeShared];
            if (!blockerDistanceBuffer) {
                std::cerr << "[GPU_RASTERIZER] WARNING: Failed to create blocker distance buffer" << std::endl;
            } else {
                blocker_distance_buffer_async_[bufIdx] = (__bridge_retained void*)blockerDistanceBuffer;
                blocker_distance_capacity_async_[bufIdx] = shadowResultsSize;
            }
        } else {
            blockerDistanceBuffer = (__bridge id<MTLBuffer>)blocker_distance_buffer_async_[bufIdx];
        }
        } // end non-PCSS blocker distance buffer

        // Phase 2: TODO[PERF-001] - Indirect dispatch pixel indices
        // Pre-compute temporal pattern pixel indices for optimized dispatch
        // Supports N-frame temporal distribution (2, 3, 4, or 5 frames)
        uint32_t total_pixels = shadow_width_ * shadow_height_;
        uint32_t pixels_per_frame = total_pixels / Optimizations::TEMPORAL_FRAME_COUNT;
        size_t indicesSize = pixels_per_frame * sizeof(uint32_t);

        id<MTLBuffer> pixelIndicesBuffers[MAX_TEMPORAL_FRAMES] = {nil};

        // Only allocate if resolution changed
        if (!pixel_indices_buffers_[0] || pixel_indices_count_ != pixels_per_frame) {
            // Release old buffers
            for (int i = 0; i < MAX_TEMPORAL_FRAMES; i++) {
                if (pixel_indices_buffers_[i]) {
                    CFBridgingRelease(pixel_indices_buffers_[i]);
                    pixel_indices_buffers_[i] = nullptr;
                }
            }

            // Allocate N buffers (one per temporal frame)
            for (int i = 0; i < Optimizations::TEMPORAL_FRAME_COUNT; i++) {
                pixelIndicesBuffers[i] = [device newBufferWithLength:indicesSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
                if (!pixelIndicesBuffers[i]) {
                    std::cerr << "[GPU_RASTERIZER] FATAL: Failed to create pixel indices buffer " << i << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
            }

            // Pre-compute temporal pattern indices (diagonal stripes for N-frame)
            // Each frame gets pixels where (px + py) % N == frame_index
            std::vector<uint32_t*> indices_ptrs(Optimizations::TEMPORAL_FRAME_COUNT);
            std::vector<uint32_t> indices_counts(Optimizations::TEMPORAL_FRAME_COUNT, 0);

            for (int i = 0; i < Optimizations::TEMPORAL_FRAME_COUNT; i++) {
                indices_ptrs[i] = (uint32_t*)[pixelIndicesBuffers[i] contents];
            }

            for (uint32_t py = 0; py < shadow_height_; ++py) {
                for (uint32_t px = 0; px < shadow_width_; ++px) {
                    uint32_t pixel_index = py * shadow_width_ + px;
                    // Diagonal stripe pattern: (px + py) % TEMPORAL_FRAME_COUNT
                    int frame_idx = (px + py) % Optimizations::TEMPORAL_FRAME_COUNT;
                    indices_ptrs[frame_idx][indices_counts[frame_idx]++] = pixel_index;
                }
            }

            // Retain and store buffers
            for (int i = 0; i < Optimizations::TEMPORAL_FRAME_COUNT; i++) {
                [pixelIndicesBuffers[i] retain];
                pixel_indices_buffers_[i] = (__bridge void*)pixelIndicesBuffers[i];
            }
            pixel_indices_count_ = pixels_per_frame;

            NSLog(@"[INDIRECT_DISPATCH] Pre-computed %d pixel index buffers: %u pixels per frame (%.2f MB total)",
                  Optimizations::TEMPORAL_FRAME_COUNT, pixels_per_frame,
                  (indicesSize * Optimizations::TEMPORAL_FRAME_COUNT) / (1024.0 * 1024.0));
        } else {
            // Re-use existing buffers
            for (int i = 0; i < Optimizations::TEMPORAL_FRAME_COUNT; i++) {
                pixelIndicesBuffers[i] = (__bridge id<MTLBuffer>)pixel_indices_buffers_[i];
            }
        }

        // Triangles buffer (async set)
        id<MTLBuffer> trianglesBuffer = nil;
        if (!triangles_buffer_async_[bufIdx] || triangles_capacity_async_[bufIdx] < trianglesSize) {
            if (triangles_buffer_async_[bufIdx]) {
                CFBridgingRelease(triangles_buffer_async_[bufIdx]);
            }
            trianglesBuffer = [device newBufferWithLength:trianglesSize
                                                   options:MTLResourceStorageModeShared];
            if (!trianglesBuffer) {
                std::cerr << "[GPU_DEFERRED] FATAL: Failed to create triangles buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            triangles_buffer_async_[bufIdx] = (__bridge_retained void*)trianglesBuffer;
            triangles_capacity_async_[bufIdx] = trianglesSize;
        } else {
            trianglesBuffer = (__bridge id<MTLBuffer>)triangles_buffer_async_[bufIdx];
        }
        memcpy([trianglesBuffer contents], triangles, trianglesSize);

        // Lights buffer (async set)
        id<MTLBuffer> lightsBuffer = nil;
        if (lights && light_count > 0) {
            size_t lightsSize = light_count * 40;  // LightData: pos(12) + strength(4) + radius(4) + size(4) + color(12) + pad(4) = 40
            if (!lights_buffer_async_[bufIdx] || lights_capacity_async_[bufIdx] < lightsSize) {
                if (lights_buffer_async_[bufIdx]) {
                    CFBridgingRelease(lights_buffer_async_[bufIdx]);
                }
                lightsBuffer = [device newBufferWithLength:lightsSize
                                                    options:MTLResourceStorageModeShared];
                if (!lightsBuffer) {
                    std::cerr << "[GPU_DEFERRED] FATAL: Failed to create lights buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                lights_buffer_async_[bufIdx] = (__bridge_retained void*)lightsBuffer;
                lights_capacity_async_[bufIdx] = lightsSize;
            } else {
                lightsBuffer = (__bridge id<MTLBuffer>)lights_buffer_async_[bufIdx];
            }
            memcpy([lightsBuffer contents], lights, lightsSize);
        } else {
            if (!lights_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                lightsBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                   options:MTLResourceStorageModeShared];
                lights_buffer_async_[bufIdx] = (__bridge_retained void*)lightsBuffer;
                lights_capacity_async_[bufIdx] = sizeof(float);
            } else {
                lightsBuffer = (__bridge id<MTLBuffer>)lights_buffer_async_[bufIdx];
            }
        }

        // Light source mapping buffer (for Pass 3 emissive rendering)
        id<MTLBuffer> lightSourceMapBuffer = nil;
        id<MTLBuffer> mapSizeBuffer = nil;
        if (is_light_source_map && map_size > 0) {
            lightSourceMapBuffer = [device newBufferWithBytes:is_light_source_map
                                                       length:map_size
                                                      options:MTLResourceStorageModeShared];
            mapSizeBuffer = [device newBufferWithBytes:&map_size
                                                 length:sizeof(unsigned int)
                                                options:MTLResourceStorageModeShared];
        }

        // BVH nodes buffer (async set)
        id<MTLBuffer> bvhNodesBuffer = nil;
        if (bvh_nodes && bvh_count > 0) {
            size_t bvhNodesSize = bvh_count * 48;  // BVHNode is 48 bytes
            if (!bvh_nodes_buffer_async_[bufIdx] || bvh_nodes_capacity_async_[bufIdx] < bvhNodesSize) {
                if (bvh_nodes_buffer_async_[bufIdx]) {
                    CFBridgingRelease(bvh_nodes_buffer_async_[bufIdx]);
                }
                bvhNodesBuffer = [device newBufferWithLength:bvhNodesSize
                                                     options:MTLResourceStorageModeShared];
                if (!bvhNodesBuffer) {
                    std::cerr << "[GPU_DEFERRED] FATAL: Failed to create BVH nodes buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                bvh_nodes_buffer_async_[bufIdx] = (__bridge_retained void*)bvhNodesBuffer;
                bvh_nodes_capacity_async_[bufIdx] = bvhNodesSize;
            } else {
                bvhNodesBuffer = (__bridge id<MTLBuffer>)bvh_nodes_buffer_async_[bufIdx];
            }
            memcpy([bvhNodesBuffer contents], bvh_nodes, bvhNodesSize);
        } else {
            if (!bvh_nodes_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                bvhNodesBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                    options:MTLResourceStorageModeShared];
                bvh_nodes_buffer_async_[bufIdx] = (__bridge_retained void*)bvhNodesBuffer;
                bvh_nodes_capacity_async_[bufIdx] = sizeof(float);
            } else {
                bvhNodesBuffer = (__bridge id<MTLBuffer>)bvh_nodes_buffer_async_[bufIdx];
            }
        }

        // BVH triangles buffer (async set)
        id<MTLBuffer> bvhTrianglesBuffer = nil;
        if (bvh_triangles && bvh_triangle_count > 0) {
            size_t bvhTrianglesSize = bvh_triangle_count * 64;  // Triangle is 64 bytes (48 vertices + 16 color+padding)
            if (!bvh_triangles_buffer_async_[bufIdx] || bvh_triangles_capacity_async_[bufIdx] < bvhTrianglesSize) {
                if (bvh_triangles_buffer_async_[bufIdx]) {
                    CFBridgingRelease(bvh_triangles_buffer_async_[bufIdx]);
                }
                bvhTrianglesBuffer = [device newBufferWithLength:bvhTrianglesSize
                                                         options:MTLResourceStorageModeShared];
                if (!bvhTrianglesBuffer) {
                    std::cerr << "[GPU_DEFERRED] FATAL: Failed to create BVH triangles buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                bvh_triangles_buffer_async_[bufIdx] = (__bridge_retained void*)bvhTrianglesBuffer;
                bvh_triangles_capacity_async_[bufIdx] = bvhTrianglesSize;
            } else {
                bvhTrianglesBuffer = (__bridge id<MTLBuffer>)bvh_triangles_buffer_async_[bufIdx];
            }
            memcpy([bvhTrianglesBuffer contents], bvh_triangles, bvhTrianglesSize);

            // Build Metal RT acceleration structure whenever new triangle
            // data is uploaded — not just when the count changes. Gating
            // on count left rotating/moving geometry pointing at a stale
            // acceleration structure; see the gbuffer copy of this
            // comment in the sync path above for full repro details.
            if (supports_raytracing_ && compute_pipeline_shadows_rt_) {
                auto rt_start = std::chrono::high_resolution_clock::now();
                build_acceleration_structure(bvh_triangles, bvh_triangle_count);
                auto rt_end = std::chrono::high_resolution_clock::now();
                double rt_ms = std::chrono::duration<double, std::milli>(rt_end - rt_start).count();
                static int rt_log = 0;
                if (rt_log++ % 60 == 0) {
                    std::cout << "[RT_BUILD] " << bvh_triangle_count << " tris in "
                              << std::fixed << std::setprecision(2) << rt_ms << "ms" << std::endl;
                }
            }
        } else {
            if (!bvh_triangles_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                bvhTrianglesBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                        options:MTLResourceStorageModeShared];
                bvh_triangles_buffer_async_[bufIdx] = (__bridge_retained void*)bvhTrianglesBuffer;
                bvh_triangles_capacity_async_[bufIdx] = sizeof(float);
            } else {
                bvhTrianglesBuffer = (__bridge id<MTLBuffer>)bvh_triangles_buffer_async_[bufIdx];
            }
        }

        // Entity BVH nodes buffer (async set) - for directional culling optimization
        id<MTLBuffer> entityBvhNodesBuffer = nil;
        if (entity_bvh_nodes && entity_node_count > 0) {
            size_t entityNodesSize = entity_node_count * 48;  // EntityBVHNode is 48 bytes
            if (!entity_bvh_nodes_buffer_async_[bufIdx] || entity_bvh_nodes_capacity_async_[bufIdx] < entityNodesSize) {
                if (entity_bvh_nodes_buffer_async_[bufIdx]) {
                    CFBridgingRelease(entity_bvh_nodes_buffer_async_[bufIdx]);
                }
                entityBvhNodesBuffer = [device newBufferWithLength:entityNodesSize
                                                            options:MTLResourceStorageModeShared];
                if (!entityBvhNodesBuffer) {
                    std::cerr << "[GPU_DEFERRED] FATAL: Failed to create entity BVH nodes buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                entity_bvh_nodes_buffer_async_[bufIdx] = (__bridge_retained void*)entityBvhNodesBuffer;
                entity_bvh_nodes_capacity_async_[bufIdx] = entityNodesSize;
            } else {
                entityBvhNodesBuffer = (__bridge id<MTLBuffer>)entity_bvh_nodes_buffer_async_[bufIdx];
            }
            memcpy([entityBvhNodesBuffer contents], entity_bvh_nodes, entityNodesSize);
        } else {
            if (!entity_bvh_nodes_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                entityBvhNodesBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                           options:MTLResourceStorageModeShared];
                entity_bvh_nodes_buffer_async_[bufIdx] = (__bridge_retained void*)entityBvhNodesBuffer;
                entity_bvh_nodes_capacity_async_[bufIdx] = sizeof(float);
            } else {
                entityBvhNodesBuffer = (__bridge id<MTLBuffer>)entity_bvh_nodes_buffer_async_[bufIdx];
            }
        }

        // Directional groups buffer (async set) - for directional culling optimization
        id<MTLBuffer> dirGroupsBuffer = nil;
        if (directional_groups && dir_group_count > 0) {
            size_t dirGroupsSize = dir_group_count * 32;  // DirectionalGroup is 32 bytes
            if (!directional_groups_buffer_async_[bufIdx] || directional_groups_capacity_async_[bufIdx] < dirGroupsSize) {
                if (directional_groups_buffer_async_[bufIdx]) {
                    CFBridgingRelease(directional_groups_buffer_async_[bufIdx]);
                }
                dirGroupsBuffer = [device newBufferWithLength:dirGroupsSize
                                                       options:MTLResourceStorageModeShared];
                if (!dirGroupsBuffer) {
                    std::cerr << "[GPU_DEFERRED] FATAL: Failed to create directional groups buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                directional_groups_buffer_async_[bufIdx] = (__bridge_retained void*)dirGroupsBuffer;
                directional_groups_capacity_async_[bufIdx] = dirGroupsSize;
            } else {
                dirGroupsBuffer = (__bridge id<MTLBuffer>)directional_groups_buffer_async_[bufIdx];
            }
            memcpy([dirGroupsBuffer contents], directional_groups, dirGroupsSize);
        } else {
            if (!directional_groups_buffer_async_[bufIdx]) {
                float dummy = 0.0f;
                dirGroupsBuffer = [device newBufferWithBytes:&dummy length:sizeof(float)
                                                      options:MTLResourceStorageModeShared];
                directional_groups_buffer_async_[bufIdx] = (__bridge_retained void*)dirGroupsBuffer;
                directional_groups_capacity_async_[bufIdx] = sizeof(float);
            } else {
                dirGroupsBuffer = (__bridge id<MTLBuffer>)directional_groups_buffer_async_[bufIdx];
            }
        }

        // Tile binning buffers (async set) - GPU bandwidth optimization
        id<MTLBuffer> tileIndicesBuffer = nil;
        id<MTLBuffer> tileOffsetsBuffer = nil;
        id<MTLBuffer> tileCountsBuffer = nil;

        int total_tiles = (tiles_x > 0 && tiles_y > 0) ? (tiles_x * tiles_y) : 0;

        if (tile_indices && tile_offsets && tile_counts && total_tiles > 0) {
            // Calculate tile_indices size
            size_t tile_indices_size = 0;
            for (int i = 0; i < total_tiles; ++i) {
                tile_indices_size += tile_counts[i];
            }
            tile_indices_size *= sizeof(uint32_t);

            if (tile_indices_size > 0) {
                if (!tile_indices_buffer_async_[bufIdx] || tile_indices_capacity_async_[bufIdx] < tile_indices_size) {
                    if (tile_indices_buffer_async_[bufIdx]) {
                        CFBridgingRelease(tile_indices_buffer_async_[bufIdx]);
                    }
                    tileIndicesBuffer = [device newBufferWithLength:tile_indices_size
                                                            options:MTLResourceStorageModeShared];
                    if (!tileIndicesBuffer) {
                        std::cerr << "[GPU_DEFERRED] FATAL: Failed to create tile indices buffer" << std::endl;
                        dispatch_semaphore_signal(semaphore);
                        return;
                    }
                    tile_indices_buffer_async_[bufIdx] = (__bridge_retained void*)tileIndicesBuffer;
                    tile_indices_capacity_async_[bufIdx] = tile_indices_size;
                } else {
                    tileIndicesBuffer = (__bridge id<MTLBuffer>)tile_indices_buffer_async_[bufIdx];
                }
                memcpy([tileIndicesBuffer contents], tile_indices, tile_indices_size);
            }

            size_t tile_offsets_size = total_tiles * sizeof(uint32_t);
            if (!tile_offsets_buffer_async_[bufIdx] || tile_offsets_capacity_async_[bufIdx] < tile_offsets_size) {
                if (tile_offsets_buffer_async_[bufIdx]) {
                    CFBridgingRelease(tile_offsets_buffer_async_[bufIdx]);
                }
                tileOffsetsBuffer = [device newBufferWithLength:tile_offsets_size
                                                        options:MTLResourceStorageModeShared];
                if (!tileOffsetsBuffer) {
                    std::cerr << "[GPU_DEFERRED] FATAL: Failed to create tile offsets buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                tile_offsets_buffer_async_[bufIdx] = (__bridge_retained void*)tileOffsetsBuffer;
                tile_offsets_capacity_async_[bufIdx] = tile_offsets_size;
            } else {
                tileOffsetsBuffer = (__bridge id<MTLBuffer>)tile_offsets_buffer_async_[bufIdx];
            }
            memcpy([tileOffsetsBuffer contents], tile_offsets, tile_offsets_size);

            size_t tile_counts_size = total_tiles * sizeof(uint32_t);
            if (!tile_counts_buffer_async_[bufIdx] || tile_counts_capacity_async_[bufIdx] < tile_counts_size) {
                if (tile_counts_buffer_async_[bufIdx]) {
                    CFBridgingRelease(tile_counts_buffer_async_[bufIdx]);
                }
                tileCountsBuffer = [device newBufferWithLength:tile_counts_size
                                                       options:MTLResourceStorageModeShared];
                if (!tileCountsBuffer) {
                    std::cerr << "[GPU_DEFERRED] FATAL: Failed to create tile counts buffer" << std::endl;
                    dispatch_semaphore_signal(semaphore);
                    return;
                }
                tile_counts_buffer_async_[bufIdx] = (__bridge_retained void*)tileCountsBuffer;
                tile_counts_capacity_async_[bufIdx] = tile_counts_size;
            } else {
                tileCountsBuffer = (__bridge id<MTLBuffer>)tile_counts_buffer_async_[bufIdx];
            }
            memcpy([tileCountsBuffer contents], tile_counts, tile_counts_size);
        } else {
            // No tile binning - create dummy buffers
            if (!tile_indices_buffer_async_[bufIdx]) {
                uint32_t dummy = 0;
                tileIndicesBuffer = [device newBufferWithBytes:&dummy length:sizeof(uint32_t)
                                                       options:MTLResourceStorageModeShared];
                tile_indices_buffer_async_[bufIdx] = (__bridge_retained void*)tileIndicesBuffer;
                tile_indices_capacity_async_[bufIdx] = sizeof(uint32_t);
            } else {
                tileIndicesBuffer = (__bridge id<MTLBuffer>)tile_indices_buffer_async_[bufIdx];
            }

            if (!tile_offsets_buffer_async_[bufIdx]) {
                uint32_t dummy = 0;
                tileOffsetsBuffer = [device newBufferWithBytes:&dummy length:sizeof(uint32_t)
                                                       options:MTLResourceStorageModeShared];
                tile_offsets_buffer_async_[bufIdx] = (__bridge_retained void*)tileOffsetsBuffer;
                tile_offsets_capacity_async_[bufIdx] = sizeof(uint32_t);
            } else {
                tileOffsetsBuffer = (__bridge id<MTLBuffer>)tile_offsets_buffer_async_[bufIdx];
            }

            if (!tile_counts_buffer_async_[bufIdx]) {
                uint32_t dummy = 0;
                tileCountsBuffer = [device newBufferWithBytes:&dummy length:sizeof(uint32_t)
                                                      options:MTLResourceStorageModeShared];
                tile_counts_buffer_async_[bufIdx] = (__bridge_retained void*)tileCountsBuffer;
                tile_counts_capacity_async_[bufIdx] = sizeof(uint32_t);
            } else {
                tileCountsBuffer = (__bridge id<MTLBuffer>)tile_counts_buffer_async_[bufIdx];
            }
        }

        // STEP 3: Create constant buffers
        unsigned int width_uint = (unsigned int)width_;
        unsigned int height_uint = (unsigned int)height_;
        id<MTLBuffer> widthBuffer = [device newBufferWithBytes:&width_uint
                                                        length:sizeof(unsigned int)
                                                       options:MTLResourceStorageModeShared];
        id<MTLBuffer> heightBuffer = [device newBufferWithBytes:&height_uint
                                                         length:sizeof(unsigned int)
                                                        options:MTLResourceStorageModeShared];
        id<MTLBuffer> triangleCountBuffer = [device newBufferWithBytes:&triangle_count
                                                                length:sizeof(unsigned int)
                                                               options:MTLResourceStorageModeShared];
        id<MTLBuffer> lightCountBuffer = [device newBufferWithBytes:&light_count
                                                             length:sizeof(unsigned int)
                                                            options:MTLResourceStorageModeShared];
        unsigned int bvh_count_uint = (unsigned int)bvh_count;
        id<MTLBuffer> bvhNodeCountBuffer = [device newBufferWithBytes:&bvh_count_uint
                                                               length:sizeof(unsigned int)
                                                              options:MTLResourceStorageModeShared];
        unsigned int bvh_triangle_count_uint = (unsigned int)bvh_triangle_count;
        id<MTLBuffer> bvhTriangleCountBuffer = [device newBufferWithBytes:&bvh_triangle_count_uint
                                                                   length:sizeof(unsigned int)
                                                                  options:MTLResourceStorageModeShared];
        // CRITICAL FIX (GPU Page Fault): Force tiles_x/y to 0 when tile binning disabled
        // Shader checks "tiles_x > 0" to detect tile binning, but can't check buffer validity
        // Without this, shader accesses dummy 4-byte buffer as if it were full array → page fault
        unsigned int tiles_x_uint = (unsigned int)tiles_x;
        unsigned int tiles_y_uint = (unsigned int)tiles_y;
        // std::cout << "[DEFERRED_DEBUG] Tile grid: " << tiles_x_uint << "x" << tiles_y_uint << std::endl;
        id<MTLBuffer> tilesXBuffer = [device newBufferWithBytes:&tiles_x_uint
                                                          length:sizeof(unsigned int)
                                                         options:MTLResourceStorageModeShared];
        id<MTLBuffer> tilesYBuffer = [device newBufferWithBytes:&tiles_y_uint
                                                          length:sizeof(unsigned int)
                                                         options:MTLResourceStorageModeShared];

        // Frame index captured at ENCODE time: completion handlers fire later,
        // on Metal's threads, so they must attribute their sample to the frame
        // that produced it rather than to whatever frame is current on arrival.
        const uint64_t tel_frame = ::logosphere::telemetry::frame_index();

        // STEP 4: Create command buffers
        // =====================================================================
        // Per-pass command buffers ARE the production structure (decision
        // 2026-07-23, GPU_OPT_LEDGER.md item E). Timing handlers are sampled
        // (GPU_PROFILE_SAMPLE_RATE) and near-free. The former flag-off
        // single-buffer path was a frozen pre-deterministic renderer, deleted.
        // =====================================================================

        // Structure to aggregate timing from async completion handlers
        struct PerPassTiming {
            int frame_num;
            uint32_t num_lights;
            double wait_duration;
            double pass1_gpu;
            std::vector<double> shadow_gpu;  // One per light
            double pass3_gpu;
            std::atomic<int> completed_passes{0};
            int total_passes;

            // Callback data (captured once, used in last handler)
            id<MTLBuffer> framebufferBuffer;
            id<MTLBuffer> depthBuffer;
            id<MTLBuffer> gbufferBuffer;  // NEW: G-buffer with particle IDs
            id<MTLBuffer> shadowResultsBuffer;  // PHASE1_DEBUG: Shadow buffer for debugging
            int width;
            int height;
            int shadow_width;   // PHASE1_DEBUG: Shadow buffer dimensions
            int shadow_height;
            CompletionCallback callback;
            void* user_data;
            dispatch_semaphore_t semaphore;
        };

        auto timing = std::make_shared<PerPassTiming>();
        timing->frame_num = frame_counter;
        timing->num_lights = light_count;
        timing->wait_duration = wait_duration;
        timing->shadow_gpu.resize(light_count > 0 ? light_count : 1, 0.0);

        // PHASE1_FIX: Calculate total passes based on batching mode
        bool use_batched = Optimizations::USE_GPU_RAY_BATCHING && compute_pipeline_shadows_batched_;
        if (use_batched || light_count == 0) {
            timing->total_passes = 3;  // Pass 1 + Batched shadow (all lights in one) + Pass 3
        } else {
            timing->total_passes = 2 + light_count;  // Pass 1 + N per-light shadows + Pass 3
        }
        timing->framebufferBuffer = framebufferBuffer;
        timing->depthBuffer = depthBufferGPU;
        timing->gbufferBuffer = gbufferBuffer;  // NEW: G-buffer with particle IDs
        timing->shadowResultsBuffer = shadowResultsBuffer;  // PHASE1_DEBUG: Shadow buffer
        timing->width = width_;
        timing->height = height_;
        timing->shadow_width = shadow_width_;  // PHASE1_DEBUG
        timing->shadow_height = shadow_height_;  // PHASE1_DEBUG
        timing->callback = callback;
        timing->user_data = user_data;
        timing->semaphore = semaphore;

        // COMMAND BUFFER 1: Clear + Pass 1 (G-buffer)
        id<MTLCommandBuffer> cmdPass1 = createTrackedCommandBuffer(commandQueue);

        // STATISTICAL SAMPLING: Only measure every Nth frame to avoid overhead
        // Following PERFORMANCE_RESEARCH.md: "Sample, don't measure everything"
        constexpr int GPU_SUBMIT_SAMPLE_RATE = 60;  // Sample 1 in 60 frames
        const bool should_profile_submit = Optimizations::ENABLE_PROFILING &&
                                          Optimizations::PROFILE_GPU_COMMAND_ENCODING &&
                                          (frame_counter % GPU_SUBMIT_SAMPLE_RATE == 0);

        // CPU encoding time tracking (sampling OR 9-11 lights for starvation investigation)
        bool should_measure_all_encoding = should_profile_submit || (light_count >= 9 && light_count <= 11);
        double total_encode_ms = 0.0;
        double pass1_encode_ms = 0.0;
        std::vector<double> shadow_encode_ms;
        double pass3_encode_ms = 0.0;

        auto encode_start = should_measure_all_encoding ?
                           std::chrono::high_resolution_clock::now() :
                           std::chrono::high_resolution_clock::time_point{};

        // Clear buffers
        id<MTLBlitCommandEncoder> blitEncoder = [cmdPass1 blitCommandEncoder];
        [blitEncoder fillBuffer:depthBufferGPU range:NSMakeRange(0, depthBufferSize) value:0x7F];  // SIGNED init, see depth_encoding.metal
        [blitEncoder fillBuffer:gbufferBuffer range:NSMakeRange(0, gbufferSize) value:0xFF];
        [blitEncoder fillBuffer:shadowResultsBuffer range:NSMakeRange(0, shadowResultsSize) value:0x00];

        // Clear light color buffer (deterministic kernel writes every pixel, so 0x00 is safe —
        // sky pixels get (1,1,1) explicitly, geometry pixels get computed ratios)
        if (lightColorBuffer) {
            [blitEncoder fillBuffer:lightColorBuffer range:NSMakeRange(0, lightColorSize) value:0x00];
        }

        // Clear blocker distance buffer (deterministic penumbra modes only)
        if constexpr (Optimizations::PENUMBRA_MODE != Optimizations::PenumbraMode::PCSS) {
            if (blockerDistanceBuffer) {
                [blitEncoder fillBuffer:blockerDistanceBuffer range:NSMakeRange(0, shadowResultsSize) value:0x00];
            }
        }

        // CRITICAL FIX: Clear temporal buffer when TEMPORAL_FRAME_COUNT=1
        // Only relevant in PCSS mode (deterministic modes don't use temporal buffers)
        if constexpr (Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::PCSS) {
        if (Optimizations::TEMPORAL_FRAME_COUNT == 1 && !Optimizations::USE_SOFT_SHADOWS) {
            [blitEncoder fillBuffer:temporalLightingBuffer range:NSMakeRange(0, shadowResultsSize) value:0x00];
            if (frame_counter <= 3) {
                NSLog(@"[TEMPORAL FIX] Cleared temporal buffer (TEMPORAL_FRAME_COUNT=1, no reuse)");
            }
        } else if (Optimizations::TEMPORAL_FRAME_COUNT == 1 && Optimizations::USE_SOFT_SHADOWS) {
            if (frame_counter <= 3) {
                NSLog(@"[SOFT SHADOWS] Preserving temporal buffer for soft shadow accumulation");
            }
        }
        } // end PCSS-only temporal clearing

        [blitEncoder endEncoding];

        // Clear framebuffer
        // Clear to dark blue (original color)
        uint32_t clear_color = 0xFF0A0A0F;  // BGRA(10,10,15,255) = dark blue
        id<MTLBuffer> clearColorBuffer = [device newBufferWithBytes:&clear_color
                                                             length:sizeof(uint32_t)
                                                            options:MTLResourceStorageModeShared];
        id<MTLComputeCommandEncoder> clearEncoder = [cmdPass1 computeCommandEncoder];
        [clearEncoder setComputePipelineState:clearPipeline];
        [clearEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
        [clearEncoder setBuffer:widthBuffer offset:0 atIndex:1];
        [clearEncoder setBuffer:heightBuffer offset:0 atIndex:2];
        [clearEncoder setBuffer:clearColorBuffer offset:0 atIndex:3];
        MTLSize clearThreadsPerGrid = MTLSizeMake(width_, height_, 1);
        MTLSize clearThreadsPerThreadgroup = MTLSizeMake(Optimizations::GPU_THREADS_CLEAR, Optimizations::GPU_THREADS_CLEAR, 1);
        [clearEncoder dispatchThreads:clearThreadsPerGrid threadsPerThreadgroup:clearThreadsPerThreadgroup];
        [clearEncoder endEncoding];

        // Pass 1: G-buffer
        id<MTLComputeCommandEncoder> gbufferEncoder = [cmdPass1 computeCommandEncoder];
        [gbufferEncoder setComputePipelineState:gbufferPipeline];
        [gbufferEncoder setBuffer:gbufferBuffer offset:0 atIndex:0];
        [gbufferEncoder setBuffer:depthBufferGPU offset:0 atIndex:1];
        [gbufferEncoder setBuffer:widthBuffer offset:0 atIndex:2];
        [gbufferEncoder setBuffer:heightBuffer offset:0 atIndex:3];
        [gbufferEncoder setBuffer:trianglesBuffer offset:0 atIndex:4];
        [gbufferEncoder setBuffer:triangleCountBuffer offset:0 atIndex:5];
        [gbufferEncoder setBuffer:tileIndicesBuffer offset:0 atIndex:6];
        [gbufferEncoder setBuffer:tileOffsetsBuffer offset:0 atIndex:7];
        [gbufferEncoder setBuffer:tileCountsBuffer offset:0 atIndex:8];
        [gbufferEncoder setBuffer:tilesXBuffer offset:0 atIndex:9];
        [gbufferEncoder setBuffer:tilesYBuffer offset:0 atIndex:10];
        if constexpr (Optimizations::RASTER_BBOX_STREAM) {
            // Reject-path bbox stream (set_triangle_bboxes fills this slot
            // pre-dispatch; count must match the triangle list)
            [gbufferEncoder setBuffer:(__bridge id<MTLBuffer>)tri_bbox_buffer_[bufIdx] offset:0 atIndex:11];
        }
        MTLSize threadsPerGrid = MTLSizeMake(width_, height_, 1);
        MTLSize threadsPerThreadgroup = MTLSizeMake(Optimizations::GPU_THREADS_FORWARD, Optimizations::GPU_THREADS_FORWARD, 1);

        // CRITICAL DEBUG: Verify Pass 1 dispatch dimensions
        static int pass1_log = 0;
        if (pass1_log < 3) {
            std::cout << "[PASS1_DISPATCH] threadsPerGrid=(" << threadsPerGrid.width << "x" << threadsPerGrid.height
                      << ") width_=" << width_ << " height_=" << height_
                      << " shadow_width_=" << shadow_width_ << " shadow_height_=" << shadow_height_ << std::endl;
            pass1_log++;
        }

        [gbufferEncoder dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
        [gbufferEncoder endEncoding];

        // DEBUG: Log Pass 1 dispatch
        static int pass1_dispatch_log = 0;
        if (pass1_dispatch_log < 3) {
            std::cout << "[GPU_DISPATCH] Pass 1 (G-Buffer) dispatched: " << threadsPerGrid.width << "x" << threadsPerGrid.height << " threads" << std::endl;
            pass1_dispatch_log++;
        }

        // Measure Pass 1 encoding time (sampling OR 9-11 lights)
        if (should_measure_all_encoding) {
            auto pass1_encode_end = std::chrono::high_resolution_clock::now();
            pass1_encode_ms = std::chrono::duration<double, std::milli>(pass1_encode_end - encode_start).count();

            // CPU STARVATION INVESTIGATION: Log Pass 1 encoding time for 9-11 lights
            if (light_count >= 9 && light_count <= 11) {
                static int pass1_encode_log = 0;
                pass1_encode_log++;
                if (pass1_encode_log % 60 == 1) {
                    std::cout << "[CPU_TIMING] Frame " << pass1_encode_log
                              << " | Lights: " << light_count
                              << " | Pass 1 encode: " << std::fixed << std::setprecision(2)
                              << pass1_encode_ms << "ms"
                              << std::endl;
                }
            }
        }

        auto pass1_commit = std::chrono::high_resolution_clock::now();
        [cmdPass1 addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            logCommandBufferError(cb, "Pass 1 (G-buffer)");
            auto completion = std::chrono::high_resolution_clock::now();
            timing->pass1_gpu = std::chrono::duration<double, std::milli>(completion - pass1_commit).count();
            timing->completed_passes++;

            // GPU TIMESTAMP PROFILING: Measure Pass 1 (G-buffer + Clear) GPU time
            if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                static uint64_t gpu_pass1_frame_counter = 0;
                gpu_pass1_frame_counter++;
                bool should_profile_pass1 = (gpu_pass1_frame_counter % Optimizations::GPU_PROFILE_SAMPLE_RATE == 1);

                if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                    double gpu_pass1_ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                    ::logosphere::telemetry::record_gpu_stage(
                        ::logosphere::telemetry::GpuStage::Pass1GBuffer, gpu_pass1_ms, tel_frame);
                    if (should_profile_pass1) {
                        NSLog(@"[GPU_TIMESTAMP] Frame %llu | Pass 1 (G-buffer+Clear): %.2f ms",
                              gpu_pass1_frame_counter, gpu_pass1_ms);
                    }
                }
            }
        }];
        [cmdPass1 commit];

        // COMMAND BUFFERS 2-N: Shadow passes (one per light OR single-pass all-lights)
        bool used_deterministic_shadows = false;  // Track if deterministic kernel ran (for light_color buffer)
        std::vector<std::chrono::high_resolution_clock::time_point> shadow_commits;
        if (light_count > 0) {
            // ITERATION 7C: Test single-pass all-lights dispatch for batched kernel
            // Hypothesis: Per-light dispatch pattern causes issues with batched kernel
            // Solution: Dispatch ONCE with ALL lights (no per-light loop)
            static int iter7c_log_count = 0;
            bool use_single_pass = Optimizations::USE_GPU_RAY_BATCHING && compute_pipeline_shadows_batched_;

            // DEBUG: Log dispatch path decision
            static int path_log = 0;
            if (path_log < 5) {
                std::cout << "[DISPATCH_PATH] use_single_pass=" << use_single_pass
                          << " USE_GPU_RAY_BATCHING=" << Optimizations::USE_GPU_RAY_BATCHING
                          << " batched_pipeline=" << (compute_pipeline_shadows_batched_ ? "OK" : "NULL")
                          << std::endl;
                path_log++;
            }

            if (use_single_pass && iter7c_log_count < 3) {
                std::cout << "[ITER7C] Using SINGLE-PASS all-lights dispatch (no per-light loop)" << std::endl;
                std::cout << "[ITER7C] Passing " << light_count << " lights to kernel in one dispatch" << std::endl;
                std::cout.flush();
                iter7c_log_count++;
            }

            const LightData* all_lights = static_cast<const LightData*>(lights);

            // Phase 2: Frame counter for temporal distribution
            static uint32_t temporal_frame_index = 0;
            static uint32_t temporal_warmup_frames = 0;

            // Soft shadow jitter counter: cycles 0-15 independently of TEMPORAL_FRAME_COUNT
            // Fixes dot artifacts caused by constant jitter when TEMPORAL_FRAME_COUNT=1
            // (frame_index was always 0 → halton16[0] → same jitter direction every frame)
            static uint32_t soft_shadow_frame_counter = 0;

            const uint32_t WARMUP_DURATION = Optimizations::TEMPORAL_FRAME_COUNT;
            bool is_warmup = (temporal_warmup_frames < WARMUP_DURATION);

            if (is_warmup) {
                temporal_frame_index = 999;  // Special value: forces all pixels to trace during warmup
                temporal_warmup_frames++;
                if (temporal_warmup_frames == WARMUP_DURATION) {
                    NSLog(@"[TEMPORAL] Warmup will complete after this frame (%u total)", WARMUP_DURATION);
                }
            } else {
                if (temporal_frame_index == 999) {
                    temporal_frame_index = 0;  // First normal frame after warmup
                    NSLog(@"[TEMPORAL] Warmup complete, starting checkerboard with frame 0");
                } else {
                    temporal_frame_index = (temporal_frame_index + 1) % Optimizations::TEMPORAL_FRAME_COUNT;
                }
            }

            // Advance soft shadow counter every frame (even during warmup)
            soft_shadow_frame_counter = (soft_shadow_frame_counter + 1) % 16;

            // Write to frame_index_buffer:
            // - During warmup: 999 (sentinel for "trace all pixels")
            // - After warmup: soft_shadow_frame_counter (varying jitter for soft shadows)
            // When TEMPORAL_FRAME_COUNT=1, checkerboard always traces all pixels regardless
            // of frame_index value, so using the soft shadow counter is safe.
            uint32_t buffer_frame_index = is_warmup ? temporal_frame_index : soft_shadow_frame_counter;

            // Create or update frame index buffer
            if (!frame_index_buffer_) {
                frame_index_buffer_ = (__bridge_retained void*)[device newBufferWithBytes:&buffer_frame_index
                                                                                   length:sizeof(uint32_t)
                                                                                  options:MTLResourceStorageModeShared];
                NSLog(@"[TEMPORAL] Created frame index buffer, frame %u (warmup: %s)",
                      buffer_frame_index, is_warmup ? "yes" : "no");
            } else {
                id<MTLBuffer> frameIndexBuf = (__bridge id<MTLBuffer>)frame_index_buffer_;
                memcpy([frameIndexBuf contents], &buffer_frame_index, sizeof(uint32_t));
            }

            id<MTLBuffer> frameIndexBuffer = (__bridge id<MTLBuffer>)frame_index_buffer_;

            // TEMPORAL PROFILING: Statistical sampling following PERFORMANCE_RESEARCH.md
            static uint64_t temporal_frame_counter = 0;
            static uint64_t total_threads_dispatched = 0;
            static uint64_t total_frames_sampled = 0;
            temporal_frame_counter++;

            // Statistical sampling: Log every 60th frame (zero overhead 98.3% of the time)
            bool should_sample_this_frame = (temporal_frame_counter % 60 == 1);  // Frame 1, 61, 121, ...

            if (should_sample_this_frame) {
                total_frames_sampled++;
            }

            if (use_single_pass) {
                // SINGLE-PASS: Dispatch once with ALL lights
                shadow_encode_ms.resize(1, 0.0);  // Only 1 dispatch

                // CPU STARVATION INVESTIGATION: Always measure encoding time for 9-11 lights
                bool should_measure_encoding = should_profile_submit || (light_count >= 9 && light_count <= 11);
                auto shadow_encode_start = should_measure_encoding ?
                                          std::chrono::high_resolution_clock::now() :
                                          std::chrono::high_resolution_clock::time_point{};

                id<MTLCommandBuffer> cmdShadow = createTrackedCommandBuffer(commandQueue);
                auto shadow_commit = std::chrono::high_resolution_clock::now();
                shadow_commits.push_back(shadow_commit);

                // =====================================================================
                // SHADOW KERNEL DISPATCH: Mode-dependent
                // PCSS: temporal blit + stochastic PCSS kernel (24 rays/pixel/light)
                // Deterministic: 1 closest-hit ray/pixel/light, no temporal, no denoise
                // =====================================================================

                id<MTLBuffer> shadowWidthBuffer = (__bridge id<MTLBuffer>)shadow_width_buffer_;
                id<MTLBuffer> shadowHeightBuffer = (__bridge id<MTLBuffer>)shadow_height_buffer_;

                if constexpr (Optimizations::PENUMBRA_MODE != Optimizations::PenumbraMode::PCSS) {
                // =========================================================================
                // DETERMINISTIC PATH: 1 closest-hit ray per pixel per light
                // No temporal accumulation, no stochastic sampling, no denoise needed
                // Outputs: shadow_results (hard shadow lux) + blocker_distance (for penumbra post-process)
                // =========================================================================
                id<MTLComputeCommandEncoder> shadowEncoder = [cmdShadow computeCommandEncoder];

                bool use_deterministic_rt = supports_raytracing_ &&
                                           compute_pipeline_shadows_deterministic_ &&
                                           acceleration_structure_;

                if (use_deterministic_rt) {
                    used_deterministic_shadows = true;
                    id<MTLComputePipelineState> detPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_shadows_deterministic_;
                    [shadowEncoder setComputePipelineState:detPipeline];

                    id<MTLAccelerationStructure> accel = (__bridge id<MTLAccelerationStructure>)acceleration_structure_;
                    [shadowEncoder setAccelerationStructure:accel atBufferIndex:0];
                    [shadowEncoder setBuffer:gbufferBuffer offset:0 atIndex:1];
                    [shadowEncoder setBuffer:shadowResultsBuffer offset:0 atIndex:2];
                    [shadowEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:3];
                    [shadowEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:4];
                    [shadowEncoder setBuffer:widthBuffer offset:0 atIndex:5];
                    [shadowEncoder setBuffer:heightBuffer offset:0 atIndex:6];
                    [shadowEncoder setBuffer:lightsBuffer offset:0 atIndex:7];
                    [shadowEncoder setBuffer:lightCountBuffer offset:0 atIndex:8];
                    [shadowEncoder setBuffer:blockerDistanceBuffer offset:0 atIndex:9];
                    // buffer(10) = shadow_triangles — for Approach B (solid angle)
                    [shadowEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10];
                    // buffer(11) = triangle_count
                    [shadowEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11];
                    // buffer(12) = light_color output (per-pixel RGB color ratio)
                    if (lightColorBuffer) {
                        [shadowEncoder setBuffer:lightColorBuffer offset:0 atIndex:12];
                    }
                    static int det_log = 0;
                    if (det_log < 3) {
                        NSLog(@"[GPU_RASTERIZER] Deterministic shadow kernel: 1 closest-hit ray/pixel/light (PenumbraMode=%d)",
                              (int)Optimizations::PENUMBRA_MODE);
                        det_log++;
                    }
                } else {
                    // Fallback: no RT support — use PCSS batched kernel as hard shadow fallback
                    // (deterministic mode requires Metal RT for closest-hit distance)
                    NSLog(@"[GPU_RASTERIZER] WARNING: Deterministic shadows require Metal RT — falling back to batched kernel");
                    id<MTLComputePipelineState> shadowsBatchedPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_shadows_batched_;
                    [shadowEncoder setComputePipelineState:shadowsBatchedPipeline];
                    [shadowEncoder setBuffer:gbufferBuffer offset:0 atIndex:0];
                    [shadowEncoder setBuffer:shadowResultsBuffer offset:0 atIndex:1];
                    [shadowEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:2];
                    [shadowEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:3];
                    [shadowEncoder setBuffer:widthBuffer offset:0 atIndex:4];
                    [shadowEncoder setBuffer:heightBuffer offset:0 atIndex:5];
                    [shadowEncoder setBuffer:lightsBuffer offset:0 atIndex:6];
                    [shadowEncoder setBuffer:lightCountBuffer offset:0 atIndex:7];
                    [shadowEncoder setBuffer:bvhNodesBuffer offset:0 atIndex:8];
                    [shadowEncoder setBuffer:bvhNodeCountBuffer offset:0 atIndex:9];
                    [shadowEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10];
                    [shadowEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11];
                    [shadowEncoder setBuffer:frameIndexBuffer offset:0 atIndex:12];
                    [shadowEncoder setBuffer:temporalLightingBuffer offset:0 atIndex:13];
                }

                // Dispatch all pixels (no temporal distribution in deterministic mode)
                uint32_t total_pixels = shadow_width_ * shadow_height_;
                uint32_t total_threads = total_pixels;
                MTLSize det_threads_per_grid = MTLSizeMake(total_threads, 1, 1);
                MTLSize det_threads_per_threadgroup = MTLSizeMake(64, 1, 1);
                [shadowEncoder dispatchThreads:det_threads_per_grid threadsPerThreadgroup:det_threads_per_threadgroup];

                static int det_dispatch_log = 0;
                if (det_dispatch_log < 3) {
                    std::cout << "[GPU_DISPATCH] Pass 2 (Deterministic Shadow) dispatched: " << total_threads
                              << " threads" << std::endl;
                    det_dispatch_log++;
                }

                [shadowEncoder endEncoding];
                [cmdShadow addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                    if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                        double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                        static int sc = 0;
                        ::logosphere::telemetry::record_gpu_stage(::logosphere::telemetry::GpuStage::Pass2ShadowRT, ms, tel_frame);
                        if (sc++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2 (Shadow RT): %.2f ms", ms);
                    }
                }];
                [cmdShadow commit];

                // =================================================================
                // PENUMBRA POST-PROCESS (Pass 2.5)
                // Runs after deterministic shadow kernel for modes A, C, A+C
                // Reads hard shadow lux → applies penumbra smoothing → writes back
                // =================================================================
                if constexpr (Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::BLOCKER_MAP ||
                              Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::BLOCKER_GRADIENT) {

                    // Allocate penumbra temp buffer (same size as shadow_results)
                    id<MTLBuffer> penumbraTempBuffer = nil;
                    if (!penumbra_temp_buffer_async_[bufIdx] || penumbra_temp_capacity_async_[bufIdx] < shadowResultsSize) {
                        penumbraTempBuffer = [device newBufferWithLength:shadowResultsSize
                                                                options:MTLResourceStorageModeShared];
                        penumbra_temp_buffer_async_[bufIdx] = (__bridge_retained void*)penumbraTempBuffer;
                        penumbra_temp_capacity_async_[bufIdx] = shadowResultsSize;
                    } else {
                        penumbraTempBuffer = (__bridge id<MTLBuffer>)penumbra_temp_buffer_async_[bufIdx];
                    }

                    // Constant buffers for penumbra kernels
                    id<MTLBuffer> shadowWidthBuf = (__bridge id<MTLBuffer>)shadow_width_buffer_;
                    id<MTLBuffer> shadowHeightBuf = (__bridge id<MTLBuffer>)shadow_height_buffer_;

                    uint32_t penumbra_total_threads = shadow_width_ * shadow_height_;
                    MTLSize penumbra_grid = MTLSizeMake(penumbra_total_threads, 1, 1);
                    MTLSize penumbra_threadgroup = MTLSizeMake(64, 1, 1);

                    // Lambda: dispatch kernel C (blocker analysis)
                    auto dispatch_blocker = [&]() {
                        if (!compute_pipeline_penumbra_blocker_) return;

                        // Blit shadow_results → temp (read source for kernel)
                        id<MTLCommandBuffer> cmdPenumbraC = createTrackedCommandBuffer(commandQueue);
                        id<MTLBlitCommandEncoder> blitC = [cmdPenumbraC blitCommandEncoder];
                        [blitC copyFromBuffer:shadowResultsBuffer sourceOffset:0
                                     toBuffer:penumbraTempBuffer destinationOffset:0 size:shadowResultsSize];
                        [blitC endEncoding];

                        // Compute max light_size from lights data
                        float max_light_size = 0.1f;  // default fallback
                        if (light_count > 0) {
                            const LightData* host_lights = (const LightData*)[lightsBuffer contents];
                            for (uint32_t li = 0; li < light_count; li++) {
                                if (host_lights[li].light_size > max_light_size)
                                    max_light_size = host_lights[li].light_size;
                            }
                        }
                        id<MTLBuffer> lightSizeBuf = [device newBufferWithBytes:&max_light_size
                                                                         length:sizeof(float)
                                                                        options:MTLResourceStorageModeShared];

                        id<MTLComputeCommandEncoder> encC = [cmdPenumbraC computeCommandEncoder];
                        id<MTLComputePipelineState> pipelineC = (__bridge id<MTLComputePipelineState>)compute_pipeline_penumbra_blocker_;
                        [encC setComputePipelineState:pipelineC];
                        [encC setBuffer:penumbraTempBuffer offset:0 atIndex:0];      // shadow_input (read)
                        [encC setBuffer:shadowResultsBuffer offset:0 atIndex:1];     // shadow_output (write)
                        [encC setBuffer:blockerDistanceBuffer offset:0 atIndex:2];   // blocker_distance
                        [encC setBuffer:shadowWidthBuf offset:0 atIndex:3];          // width
                        [encC setBuffer:shadowHeightBuf offset:0 atIndex:4];         // height
                        [encC setBuffer:lightSizeBuf offset:0 atIndex:5];            // light_size
                        [encC dispatchThreads:penumbra_grid threadsPerThreadgroup:penumbra_threadgroup];
                        [encC endEncoding];
                        [cmdPenumbraC addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                            logCommandBufferError(cb, "Pass 2.5 (Penumbra blocker)");
                            if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                                double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                static int pc = 0;
                                if (pc++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2.5 (Penumbra): %.2f ms", ms);
                            }
                        }];
                        [cmdPenumbraC commit];
                        // Serial queue guarantees GPU executes after Pass 2
                    };

                    // Dispatch based on mode
                    if constexpr (Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::BLOCKER_MAP) {
                        dispatch_blocker();
                    } else if constexpr (Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::BLOCKER_GRADIENT) {
                        // JFA + Separable Blur (Tier 2): replaces O(N²) Kernel C
                        if (compute_pipeline_jfa_seed_ && compute_pipeline_jfa_propagate_ &&
                            compute_pipeline_blur_h_ && compute_pipeline_blur_v_) {

                            // Allocate JFA ping-pong buffers
                            id<MTLBuffer> jfaA = nil, jfaB = nil;
                            if (!jfa_buffer_a_[bufIdx] || jfa_buffer_capacity_[bufIdx] < shadowResultsSize) {
                                jfaA = [device newBufferWithLength:shadowResultsSize options:MTLResourceStorageModeShared];
                                jfaB = [device newBufferWithLength:shadowResultsSize options:MTLResourceStorageModeShared];
                                if (jfa_buffer_a_[bufIdx]) CFBridgingRelease(jfa_buffer_a_[bufIdx]);
                                if (jfa_buffer_b_[bufIdx]) CFBridgingRelease(jfa_buffer_b_[bufIdx]);
                                jfa_buffer_a_[bufIdx] = (__bridge_retained void*)jfaA;
                                jfa_buffer_b_[bufIdx] = (__bridge_retained void*)jfaB;
                                jfa_buffer_capacity_[bufIdx] = shadowResultsSize;
                            } else {
                                jfaA = (__bridge id<MTLBuffer>)jfa_buffer_a_[bufIdx];
                                jfaB = (__bridge id<MTLBuffer>)jfa_buffer_b_[bufIdx];
                            }

                            // Compute max light_size
                            float max_light_size = 0.1f;
                            if (light_count > 0) {
                                const LightData* host_lights = (const LightData*)[lightsBuffer contents];
                                for (uint32_t li = 0; li < light_count; li++) {
                                    if (host_lights[li].light_size > max_light_size)
                                        max_light_size = host_lights[li].light_size;
                                }
                            }
                            id<MTLBuffer> lightSizeBuf = [device newBufferWithBytes:&max_light_size
                                                                             length:sizeof(float)
                                                                            options:MTLResourceStorageModeShared];

                            // Both paths finish with the result in jfaA; step 3
                            // below reads it through 'ping'.
                            id<MTLBuffer> ping = jfaA, pong = jfaB;

                            // --- Steps 1+2 MERGED: seed + 6 JFA propagations ---
                            // One command buffer, one encoder, 7 serial dispatches.
                            // Metal orders dispatches within an encoder, which is
                            // exactly the ordering the 7 separate buffers relied on,
                            // so the output is unchanged and 6 boundaries disappear.
                            if constexpr (Optimizations::MERGE_JFA_COMMAND_BUFFERS) {
                                static int jfa_frame_m = 0;
                                const bool log_jfa_m = (jfa_frame_m++ % 60 == 0);
                                int steps_m[] = {32, 16, 8, 4, 2, 1};
                                id<MTLBuffer> ping_m = jfaA, pong_m = jfaB;

                                id<MTLCommandBuffer> cmd = createTrackedCommandBuffer(commandQueue);
                                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];

                                [enc setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_pipeline_jfa_seed_];
                                [enc setBuffer:blockerDistanceBuffer offset:0 atIndex:0];
                                [enc setBuffer:jfaA offset:0 atIndex:1];
                                [enc setBuffer:shadowWidthBuf offset:0 atIndex:2];
                                [enc setBuffer:shadowHeightBuf offset:0 atIndex:3];
                                [enc setBuffer:lightSizeBuf offset:0 atIndex:4];
                                [enc dispatchThreads:penumbra_grid threadsPerThreadgroup:penumbra_threadgroup];

                                [enc setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_pipeline_jfa_propagate_];
                                for (int s = 0; s < 6; s++) {
                                    [enc setBuffer:ping_m offset:0 atIndex:0];
                                    [enc setBuffer:pong_m offset:0 atIndex:1];
                                    [enc setBuffer:shadowWidthBuf offset:0 atIndex:2];
                                    [enc setBuffer:shadowHeightBuf offset:0 atIndex:3];
                                    // setBytes, not a per-step MTLBuffer: the old loop
                                    // allocated 6 four-byte Metal buffers every frame.
                                    [enc setBytes:&steps_m[s] length:sizeof(int) atIndex:4];
                                    [enc dispatchThreads:penumbra_grid threadsPerThreadgroup:penumbra_threadgroup];
                                    id<MTLBuffer> tmp = ping_m; ping_m = pong_m; pong_m = tmp;
                                }
                                [enc endEncoding];

                                if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                                    [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                        if (cb.GPUStartTime <= 0 || cb.GPUEndTime <= 0) return;
                                        double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                        // Seed folded in; recorded under the propagate stage.
                                        ::logosphere::telemetry::record_gpu_stage(
                                            ::logosphere::telemetry::GpuStage::Pass25JfaPropagate, ms, tel_frame);
                                        if (log_jfa_m) NSLog(@"[GPU_TIMESTAMP] Pass 2.5 (JFA seed+6, merged): %.2f ms", ms);
                                    }];
                                }
                                [cmd commit];
                                // 6 swaps from an even count leaves the result in jfaA,
                                // which is what the unmerged path also ends with.
                                ping = jfaA; pong = jfaB;
                            } else {

                            // --- Step 1: Seed ---
                            {
                                id<MTLCommandBuffer> cmd = createTrackedCommandBuffer(commandQueue);
                                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                                [enc setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_pipeline_jfa_seed_];
                                [enc setBuffer:blockerDistanceBuffer offset:0 atIndex:0];
                                [enc setBuffer:jfaA offset:0 atIndex:1];  // Output: penumbra_width
                                [enc setBuffer:shadowWidthBuf offset:0 atIndex:2];
                                [enc setBuffer:shadowHeightBuf offset:0 atIndex:3];
                                [enc setBuffer:lightSizeBuf offset:0 atIndex:4];
                                [enc dispatchThreads:penumbra_grid threadsPerThreadgroup:penumbra_threadgroup];
                                [enc endEncoding];
                                if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                                    [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                        double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                        static int c = 0;
                                        ::logosphere::telemetry::record_gpu_stage(::logosphere::telemetry::GpuStage::Pass25JfaSeed, ms, tel_frame);
                        if (c++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2.5 (JFA seed): %.2f ms", ms);
                                    }];
                                }
                                [cmd commit];
                            }

                            // --- Step 2: JFA propagation (6 passes, ping-pong) ---
                            int steps[] = {32, 16, 8, 4, 2, 1};
                            static int jfa_frame = 0;
                            const bool log_jfa = (jfa_frame++ % 60 == 0);
                            for (int s = 0; s < 6; s++) {
                                id<MTLCommandBuffer> cmd = createTrackedCommandBuffer(commandQueue);
                                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                                [enc setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_pipeline_jfa_propagate_];
                                [enc setBuffer:ping offset:0 atIndex:0];   // Read
                                [enc setBuffer:pong offset:0 atIndex:1];   // Write
                                [enc setBuffer:shadowWidthBuf offset:0 atIndex:2];
                                [enc setBuffer:shadowHeightBuf offset:0 atIndex:3];
                                id<MTLBuffer> stepBuf = [device newBufferWithBytes:&steps[s]
                                                                           length:sizeof(int)
                                                                          options:MTLResourceStorageModeShared];
                                [enc setBuffer:stepBuf offset:0 atIndex:4];
                                [enc dispatchThreads:penumbra_grid threadsPerThreadgroup:penumbra_threadgroup];
                                [enc endEncoding];
                                if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                                    {
                                        const int step_now = steps[s];
                                        const bool log_step = log_jfa;
                                        [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                            if (cb.GPUStartTime <= 0 || cb.GPUEndTime <= 0) return;
                                            double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                            // One record per JFA step; record_gpu_stage
                                            // sums them within the frame.
                                            ::logosphere::telemetry::record_gpu_stage(
                                                ::logosphere::telemetry::GpuStage::Pass25JfaPropagate, ms, tel_frame);
                                            if (log_step) {
                                                NSLog(@"[GPU_TIMESTAMP] Pass 2.5 (JFA step %d): %.2f ms", step_now, ms);
                                            }
                                        }];
                                    }
                                }
                                [cmd commit];
                                // Swap ping-pong
                                id<MTLBuffer> tmp = ping; ping = pong; pong = tmp;
                            }
                            }  // end else (unmerged JFA path)
                            // After 6 passes, 'ping' has the final penumbra_width

                            // --- Step 3: Horizontal blur ---
                            // Save original hard shadow in 'pong' (JFA done, pong is free)
                            // for energy-conserving clamp in both blur passes.
                            {
                                id<MTLCommandBuffer> cmd = createTrackedCommandBuffer(commandQueue);
                                id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
                                // Copy original to temp (the H-blur's read source).
                                // A second copy into 'pong' used to ride along as an
                                // "energy-conserving clamp reference" — neither blur
                                // kernel ever read it (dead since the parameter was
                                // added), so it was a full shadow-buffer copy per
                                // frame for nothing. Removed 2026-07-29.
                                [blit copyFromBuffer:shadowResultsBuffer sourceOffset:0
                                            toBuffer:penumbraTempBuffer destinationOffset:0 size:shadowResultsSize];
                                [blit endEncoding];

                                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                                [enc setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_pipeline_blur_h_];
                                [enc setBuffer:penumbraTempBuffer offset:0 atIndex:0];    // Read: hard shadow
                                [enc setBuffer:shadowResultsBuffer offset:0 atIndex:1];   // Write: H-blurred
                                [enc setBuffer:ping offset:0 atIndex:2];                  // penumbra_width
                                [enc setBuffer:shadowWidthBuf offset:0 atIndex:3];
                                [enc setBuffer:shadowHeightBuf offset:0 atIndex:4];
                                [enc setBuffer:gbufferBuffer offset:0 atIndex:5];
                                if constexpr (Optimizations::PENUMBRA_COMPACT_IDS) {
                                    size_t idSize = (size_t)shadow_width_ * shadow_height_ * sizeof(uint32_t);
                                    if (!penumbra_id_buffer_[bufIdx] || penumbra_id_capacity_[bufIdx] < idSize) {
                                        if (penumbra_id_buffer_[bufIdx]) CFBridgingRelease(penumbra_id_buffer_[bufIdx]);
                                        id<MTLBuffer> idBuf = [device newBufferWithLength:idSize options:MTLResourceStorageModeShared];
                                        penumbra_id_buffer_[bufIdx] = (__bridge_retained void*)idBuf;
                                        penumbra_id_capacity_[bufIdx] = idSize;
                                    }
                                    [enc setBuffer:(__bridge id<MTLBuffer>)penumbra_id_buffer_[bufIdx] offset:0 atIndex:7];
                                }
                                [enc dispatchThreads:penumbra_grid threadsPerThreadgroup:penumbra_threadgroup];
                                [enc endEncoding];
                                [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                    logCommandBufferError(cb, "Pass 2.5 (JFA blur H)");
                                    if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                                        double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                        static int c = 0;
                                        ::logosphere::telemetry::record_gpu_stage(::logosphere::telemetry::GpuStage::Pass25PenumbraBlurH, ms, tel_frame);
                        if (c++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2.5 (Penumbra blurH): %.2f ms", ms);
                                    }
                                }];
                                [cmd commit];
                            }

                            // --- Step 4: Vertical blur ---
                            {
                                id<MTLCommandBuffer> cmd = createTrackedCommandBuffer(commandQueue);
                                id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
                                [blit copyFromBuffer:shadowResultsBuffer sourceOffset:0
                                            toBuffer:penumbraTempBuffer destinationOffset:0 size:shadowResultsSize];
                                [blit endEncoding];

                                id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                                [enc setComputePipelineState:(__bridge id<MTLComputePipelineState>)compute_pipeline_blur_v_];
                                [enc setBuffer:penumbraTempBuffer offset:0 atIndex:0];    // Read: H-blurred
                                [enc setBuffer:shadowResultsBuffer offset:0 atIndex:1];   // Write: final
                                [enc setBuffer:ping offset:0 atIndex:2];                  // penumbra_width
                                [enc setBuffer:shadowWidthBuf offset:0 atIndex:3];
                                [enc setBuffer:shadowHeightBuf offset:0 atIndex:4];
                                if constexpr (Optimizations::PENUMBRA_COMPACT_IDS) {
                                    // Packed id stream written by the H-blur
                                    [enc setBuffer:(__bridge id<MTLBuffer>)penumbra_id_buffer_[bufIdx] offset:0 atIndex:5];
                                } else {
                                    [enc setBuffer:gbufferBuffer offset:0 atIndex:5];
                                }

                                [enc dispatchThreads:penumbra_grid threadsPerThreadgroup:penumbra_threadgroup];
                                [enc endEncoding];
                                [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                    logCommandBufferError(cb, "Pass 2.5 (JFA blur V)");
                                    if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                                        double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                        static int pc = 0;
                                        ::logosphere::telemetry::record_gpu_stage(::logosphere::telemetry::GpuStage::Pass25PenumbraBlurV, ms, tel_frame);
                        if (pc++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2.5 (Penumbra blurV): %.2f ms", ms);
                                    }
                                }];
                                [cmd commit];
                            }
                        } else {
                            // Fallback to old Kernel C if JFA pipelines not available
                            dispatch_blocker();
                        }
                    }

                    static int penumbra_log = 0;
                    if (penumbra_log < 3) {
                        std::cout << "[GPU_DISPATCH] Pass 2.5 (Penumbra Post-Process) dispatched: mode="
                                  << (int)Optimizations::PENUMBRA_MODE << std::endl;
                        penumbra_log++;
                    }


                }

                } else { // PENUMBRA_MODE == PCSS
                // =========================================================================
                // PCSS PATH: Stochastic shadow rays (legacy, unchanged)
                // Temporal pre-fill blit + PCSS kernel (24 rays/pixel/light) + denoise
                // =========================================================================
                if constexpr (!Optimizations::DISABLE_TEMPORAL_BLIT) {
                    if (!is_warmup) {
                        id<MTLBlitCommandEncoder> preFillEncoder = [cmdShadow blitCommandEncoder];
                        [preFillEncoder copyFromBuffer:temporalLightingBuffer
                                           sourceOffset:0
                                               toBuffer:shadowResultsBuffer
                                      destinationOffset:0
                                                   size:shadowResultsSize];
                        [preFillEncoder endEncoding];
                    }
                } else {
                    static int blit_disabled_log = 0;
                    if (blit_disabled_log < 3) {
                        NSLog(@"[GPU_PROFILE] Temporal blit DISABLED for performance measurement (rendering will be dark)");
                        blit_disabled_log++;
                    }
                }

                id<MTLComputeCommandEncoder> shadowEncoder = [cmdShadow computeCommandEncoder];

                // Check if Metal RT is available and ready
                bool use_metal_rt = Optimizations::USE_METAL_RT &&
                                   supports_raytracing_ &&
                                   compute_pipeline_shadows_rt_ &&
                                   acceleration_structure_;

                static int rt_check_log = 0;
                if (rt_check_log < 3) {
                    std::cout << "[GPU_RASTERIZER] RT check: supports=" << supports_raytracing_
                              << " pipeline=" << (compute_pipeline_shadows_rt_ ? "OK" : "NULL")
                              << " accel=" << (acceleration_structure_ ? "OK" : "NULL")
                              << " => use_metal_rt=" << use_metal_rt << std::endl;
                    rt_check_log++;
                }

                if (use_metal_rt) {
                    // =========================================================================
                    // METAL RT PATH: Hardware-accelerated shadow rays (M3+ only)
                    // =========================================================================
                    // Uses RT cores for BVH traversal instead of compute units
                    // Expected speedup: 5-10× vs software BVH traversal
                    static int rt_log = 0;
                    if (rt_log < 3) {
                        NSLog(@"[GPU_RASTERIZER] ⚡ Metal RT: Using hardware ray tracing for shadows");
                        rt_log++;
                    }

                    id<MTLComputePipelineState> shadowsRTPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_shadows_rt_;
                    [shadowEncoder setComputePipelineState:shadowsRTPipeline];

                    // Metal RT kernel buffer bindings (different from batched kernel)
                    id<MTLAccelerationStructure> accel = (__bridge id<MTLAccelerationStructure>)acceleration_structure_;
                    [shadowEncoder setAccelerationStructure:accel atBufferIndex:0];   // Driver-built acceleration structure
                    [shadowEncoder setBuffer:gbufferBuffer offset:0 atIndex:1];       // G-buffer (full res)
                    [shadowEncoder setBuffer:shadowResultsBuffer offset:0 atIndex:2]; // Shadow output (reduced res)
                    [shadowEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:3];   // Shadow width (reduced)
                    [shadowEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:4];  // Shadow height (reduced)
                    [shadowEncoder setBuffer:widthBuffer offset:0 atIndex:5];         // G-buffer width (full)
                    [shadowEncoder setBuffer:heightBuffer offset:0 atIndex:6];        // G-buffer height (full)
                    [shadowEncoder setBuffer:lightsBuffer offset:0 atIndex:7];        // ALL lights
                    [shadowEncoder setBuffer:lightCountBuffer offset:0 atIndex:8];    // Light count
                    [shadowEncoder setBuffer:frameIndexBuffer offset:0 atIndex:9];    // Frame index for temporal
                    [shadowEncoder setBuffer:temporalLightingBuffer offset:0 atIndex:10]; // Temporal buffer
                    // Note: pixel_indices bound below in indirect dispatch setup at index 11
                    [shadowEncoder setBuffer:prevParticleIdBuffer offset:0 atIndex:12];  // Motion detection: prev frame particle IDs
                    [shadowEncoder setBuffer:sampleCountBuffer offset:0 atIndex:14];      // Running average sample count (convergence)

                    // Temporal reprojection: compute camera delta in shadow buffer pixels
                    // This fixes the soft shadow aura bug when camera moves
                    float delta_world_x = shadow_culling_camera_x_ - prev_shadow_camera_x_;
                    float delta_world_y = shadow_culling_camera_y_ - prev_shadow_camera_y_;

                    // Isometric transform: iso_x = (view_x - view_y) * 0.866f
                    // Camera moving +X in world shifts iso_x by -0.866, +Y shifts iso_x by +0.866
                    float delta_iso_x = (-delta_world_x + delta_world_y) * 0.866f;
                    float delta_iso_y = (-delta_world_x - delta_world_y) * 0.5f;

                    // Scale to shadow buffer pixels
                    float scale_factor = shadow_pixels_per_unit_ * ((float)shadow_width_ / (float)width_);
                    float camera_delta[2] = {
                        delta_iso_x * scale_factor,
                        -delta_iso_y * scale_factor  // Y inverted for screen coords
                    };
                    [shadowEncoder setBytes:camera_delta length:sizeof(camera_delta) atIndex:13];
                } else {
                    // =========================================================================
                    // COMPUTE PATH: Software BVH traversal (fallback for M1/M2)
                    // =========================================================================
                    id<MTLComputePipelineState> shadowsBatchedPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_shadows_batched_;
                    [shadowEncoder setComputePipelineState:shadowsBatchedPipeline];

                    [shadowEncoder setBuffer:gbufferBuffer offset:0 atIndex:0];           // G-buffer (full res)
                    [shadowEncoder setBuffer:shadowResultsBuffer offset:0 atIndex:1];     // Shadow output (reduced res)
                    [shadowEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:2];       // Shadow width (reduced)
                    [shadowEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:3];      // Shadow height (reduced)
                    [shadowEncoder setBuffer:widthBuffer offset:0 atIndex:4];             // G-buffer width (full)
                    [shadowEncoder setBuffer:heightBuffer offset:0 atIndex:5];            // G-buffer height (full)
                    [shadowEncoder setBuffer:lightsBuffer offset:0 atIndex:6];            // ALL lights (shifted +2)
                    [shadowEncoder setBuffer:lightCountBuffer offset:0 atIndex:7];        // Full count (shifted +2)
                    [shadowEncoder setBuffer:bvhNodesBuffer offset:0 atIndex:8];          // BVH nodes (shifted +2)
                    [shadowEncoder setBuffer:bvhNodeCountBuffer offset:0 atIndex:9];      // BVH node count (shifted +2)
                    [shadowEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10];     // BVH triangles (shifted +2)
                    [shadowEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11]; // BVH tri count (shifted +2)

                    // Phase 2: Temporal distribution buffers
                    [shadowEncoder setBuffer:frameIndexBuffer offset:0 atIndex:12];        // Current frame index (uint32_t)
                    [shadowEncoder setBuffer:temporalLightingBuffer offset:0 atIndex:13];  // Previous frame lighting results
                }

                // Phase 2: TODO[PERF-001] - INDIRECT DISPATCH OPTIMIZATION
                //
                // SOLUTION IMPLEMENTED: Dispatch only pixels that need tracing
                //   - CPU pre-computed checkerboard indices (frame 0 and frame 1)
                //   - GPU dispatches only traced pixels (840K threads, not 1.68M)
                //   - Shader reads pixel index from buffer (no checkerboard check needed)
                //   - Expected gain: ~2× speedup (50% thread reduction)
                //
                uint32_t total_pixels = shadow_width_ * shadow_height_;
                uint32_t total_threads;  // Will be pixels_per_frame or total_pixels (warmup)

                // Select appropriate pixel indices buffer based on frame index
                id<MTLBuffer> pixelIndicesBuffer;
                if (is_warmup) {
                    // During warmup: trace all pixels (no indirect dispatch)
                    pixelIndicesBuffer = nil;
                    total_threads = total_pixels;
                } else {
                    // Normal operation: use indirect dispatch with pre-computed indices
                    total_threads = pixel_indices_count_;  // 1/N of total pixels (N = TEMPORAL_FRAME_COUNT)

                    // Select correct pixel indices buffer based on current temporal frame
                    int buffer_idx = temporal_frame_index % Optimizations::TEMPORAL_FRAME_COUNT;
                    pixelIndicesBuffer = pixelIndicesBuffers[buffer_idx];

                    // Bind pixel indices buffer (different index for Metal RT vs batched)
                    if (use_metal_rt) {
                        [shadowEncoder setBuffer:pixelIndicesBuffer offset:0 atIndex:11];  // Metal RT: index 11
                    } else {
                        [shadowEncoder setBuffer:pixelIndicesBuffer offset:0 atIndex:14];  // Batched: index 14
                    }
                }

                // GPU PROFILING: Atomic counter buffers (batched kernel only - Metal RT doesn't use them)
                if (!use_metal_rt) {
                    id<MTLBuffer> debugRaysTracedBuffer = (__bridge id<MTLBuffer>)debug_rays_traced_buffer_;
                    id<MTLBuffer> debugBvhNodesVisitedBuffer = (__bridge id<MTLBuffer>)debug_bvh_nodes_visited_buffer_;
                    id<MTLBuffer> debugTrianglesTestedBuffer = (__bridge id<MTLBuffer>)debug_triangles_tested_buffer_;

                    // Zero out counters before dispatch
                    uint32_t zero = 0;
                    memcpy([debugRaysTracedBuffer contents], &zero, sizeof(uint32_t));
                    memcpy([debugBvhNodesVisitedBuffer contents], &zero, sizeof(uint32_t));
                    memcpy([debugTrianglesTestedBuffer contents], &zero, sizeof(uint32_t));

                    // Bind atomic counter buffers to shader
                    [shadowEncoder setBuffer:debugRaysTracedBuffer offset:0 atIndex:15];       // Rays traced
                    [shadowEncoder setBuffer:debugBvhNodesVisitedBuffer offset:0 atIndex:16];  // BVH nodes visited
                    [shadowEncoder setBuffer:debugTrianglesTestedBuffer offset:0 atIndex:17];  // Triangles tested

                    // Entity BVH buffers (for directional culling optimization)
                    [shadowEncoder setBuffer:entityBvhNodesBuffer offset:0 atIndex:18];        // Entity BVH nodes
                    uint32_t entity_count_val = entity_node_count;
                    id<MTLBuffer> entityNodeCountBuffer = [device newBufferWithBytes:&entity_count_val
                                                                               length:sizeof(uint32_t)
                                                                              options:MTLResourceStorageModeShared];
                    [shadowEncoder setBuffer:entityNodeCountBuffer offset:0 atIndex:19];       // Entity node count
                    [shadowEncoder setBuffer:dirGroupsBuffer offset:0 atIndex:20];             // Directional groups
                    uint32_t dir_count_val = dir_group_count;
                    id<MTLBuffer> dirGroupCountBuffer = [device newBufferWithBytes:&dir_count_val
                                                                             length:sizeof(uint32_t)
                                                                            options:MTLResourceStorageModeShared];
                    [shadowEncoder setBuffer:dirGroupCountBuffer offset:0 atIndex:21];         // Directional group count
                    [shadowEncoder setBuffer:prevParticleIdBuffer offset:0 atIndex:22];        // Soft shadow motion detection
                }

                // TEMPORAL PROFILING: Accumulate dispatch counts
                total_threads_dispatched += total_threads;

                if (should_sample_this_frame) {
                    // FIXED: Calculate expected threads correctly
                    // Warmup: all pixels, Normal: pixels_per_frame (total_pixels / TEMPORAL_FRAME_COUNT)
                    uint32_t expected_threads = is_warmup ? total_pixels : pixel_indices_count_;
                    float efficiency = (total_threads == expected_threads) ? 100.0f : (expected_threads * 100.0f / total_threads);
                    NSLog(@"[TEMPORAL_DISPATCH] Frame %llu: Dispatched %u threads | Expected: %u (%.1f%% efficiency) | Warmup: %s | Lights: %u",
                          temporal_frame_counter, total_threads, expected_threads,
                          efficiency, is_warmup ? "YES" : "NO", light_count);
                    if (!is_warmup && total_threads != expected_threads) {
                        NSLog(@"[TEMPORAL_DISPATCH] ⚠️  DISPATCH MISMATCH: Expected %u but dispatched %u", expected_threads, total_threads);
                    }
                }

                MTLSize batched_threads_per_grid = MTLSizeMake(total_threads, 1, 1);
                MTLSize batched_threads_per_threadgroup = MTLSizeMake(64, 1, 1);

                // GPU PROFILING: Counter sampling DISABLED
                // FINDING: Only "timestamp" counter set available (1 counter)
                // - Timestamp counters already accessible via GPUStartTime/GPUEndTime (in use)
                // - Performance counters (occupancy, bandwidth) not available on M4 Max via MTLCounterSet
                // - Conclusion: Use shader instrumentation (atomic counters) for BVH/memory metrics instead
                //
                // if (counter_sample_buffer_ && should_sample_this_frame) {
                //     [shadowEncoder sampleCountersInBuffer:...];  // CRASHES - not supported for timestamp counters
                // }

                [shadowEncoder dispatchThreads:batched_threads_per_grid threadsPerThreadgroup:batched_threads_per_threadgroup];

                // DEBUG: Log Pass 2 dispatch
                static int pass2_dispatch_log = 0;
                if (pass2_dispatch_log < 3) {
                    std::cout << "[GPU_DISPATCH] Pass 2 (Shadow/Lighting) dispatched: " << batched_threads_per_grid.width
                              << " threads [" << (use_metal_rt ? "Metal RT ⚡" : "Compute BVH") << "]" << std::endl;
                    pass2_dispatch_log++;
                }

                [shadowEncoder endEncoding];

                // Update previous camera position for next frame's temporal reprojection
                prev_shadow_camera_x_ = shadow_culling_camera_x_;
                prev_shadow_camera_y_ = shadow_culling_camera_y_;

                [cmdShadow addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                    logCommandBufferError(cb, "Pass 2 (Shadow batched)");
                    auto completion = std::chrono::high_resolution_clock::now();
                    timing->shadow_gpu[0] = std::chrono::duration<double, std::milli>(completion - shadow_commits[0]).count();
                    timing->completed_passes++;

                    // GPU TIMESTAMP PROFILING: Use Metal's hardware timestamps for accurate GPU timing
                    // Statistical sampling every GPU_PROFILE_SAMPLE_RATE frames (zero overhead 98.3% of time)
                    if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                        static uint64_t gpu_profile_frame_counter = 0;
                        gpu_profile_frame_counter++;
                        bool should_profile_gpu = (gpu_profile_frame_counter % Optimizations::GPU_PROFILE_SAMPLE_RATE == 1);

                        if (should_profile_gpu && cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                            // Metal GPU timestamps in seconds (high precision)
                            double gpu_pass2_ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;

                            const char* blit_status = Optimizations::DISABLE_TEMPORAL_BLIT ? "DISABLED" : "enabled";
                            NSLog(@"[GPU_TIMESTAMP] Frame %llu | Pass 2 (Shadow+Blit): %.2f ms | GPU Start: %.6f | GPU End: %.6f | Lights: %u | Blit: %s",
                                  gpu_profile_frame_counter,
                                  gpu_pass2_ms,
                                  cb.GPUStartTime,
                                  cb.GPUEndTime,
                                  light_count,
                                  blit_status
                            );

                            // GPU SHADER INSTRUMENTATION: Read atomic counters
                            // Measure BVH traversal activity during Pass 2
                            id<MTLBuffer> raysBuffer = (__bridge id<MTLBuffer>)debug_rays_traced_buffer_;
                            id<MTLBuffer> nodesBuffer = (__bridge id<MTLBuffer>)debug_bvh_nodes_visited_buffer_;
                            id<MTLBuffer> trisBuffer = (__bridge id<MTLBuffer>)debug_triangles_tested_buffer_;

                            uint32_t rays_traced = *((uint32_t*)[raysBuffer contents]);
                            uint32_t bvh_nodes_visited = *((uint32_t*)[nodesBuffer contents]);
                            uint32_t triangles_tested = *((uint32_t*)[trisBuffer contents]);

                            // Calculate averages per ray (if any rays were traced)
                            double avg_nodes_per_ray = rays_traced > 0 ? (double)bvh_nodes_visited / rays_traced : 0.0;
                            double avg_tris_per_ray = rays_traced > 0 ? (double)triangles_tested / rays_traced : 0.0;

                            NSLog(@"[SHADER_INSTRUMENTATION] Frame %llu | Rays: %u | BVH nodes: %u (%.1f/ray) | Triangles: %u (%.1f/ray)",
                                  gpu_profile_frame_counter,
                                  rays_traced,
                                  bvh_nodes_visited,
                                  avg_nodes_per_ray,
                                  triangles_tested,
                                  avg_tris_per_ray
                            );
                        }
                    }
                }];
                [cmdShadow commit];

                } // end PCSS else block

                if (should_measure_encoding) {
                    auto shadow_encode_end = std::chrono::high_resolution_clock::now();
                    double encode_time = std::chrono::duration<double, std::milli>(
                        shadow_encode_end - shadow_encode_start).count();
                    shadow_encode_ms[0] = encode_time;

                    // CPU STARVATION INVESTIGATION: Log encoding time for 9-11 lights
                    if (light_count >= 9 && light_count <= 11) {
                        static int pass2_encode_log = 0;
                        pass2_encode_log++;
                        if (pass2_encode_log % 60 == 1) {
                            std::cout << "[CPU_TIMING] Frame " << pass2_encode_log
                                      << " | Lights: " << light_count
                                      << " | Pass 2 encode: " << std::fixed << std::setprecision(2)
                                      << encode_time << "ms"
                                      << std::endl;
                        }
                    }
                }
            } else {
                // PER-LIGHT: Original dispatch pattern
                shadow_encode_ms.resize(light_count, 0.0);
                for (uint32_t light_idx = 0; light_idx < light_count; ++light_idx) {
                auto shadow_encode_start = should_profile_submit ?
                                          std::chrono::high_resolution_clock::now() :
                                          std::chrono::high_resolution_clock::time_point{};

                id<MTLCommandBuffer> cmdShadow = createTrackedCommandBuffer(commandQueue);
                auto shadow_commit = std::chrono::high_resolution_clock::now();
                shadow_commits.push_back(shadow_commit);

                // Single light buffer
                LightData single_light = all_lights[light_idx];
                id<MTLBuffer> singleLightBuffer = [device newBufferWithBytes:&single_light
                                                                      length:sizeof(LightData)
                                                                     options:MTLResourceStorageModeShared];
                uint32_t one_light = 1;
                id<MTLBuffer> oneLightCountBuffer = [device newBufferWithBytes:&one_light
                                                                        length:sizeof(uint32_t)
                                                                       options:MTLResourceStorageModeShared];

                id<MTLComputeCommandEncoder> shadowEncoder = [cmdShadow computeCommandEncoder];

                // Use instrumented kernel if BVH profiling is enabled
                bool useInstrumented = Optimizations::PROFILE_BVH_TRAVERSAL &&
                                     compute_pipeline_shadows_instrumented_ &&
                                     (frame_counter % Optimizations::PROFILE_SAMPLE_RATE == 0);

                // Declare instrumentation buffers outside if block for visibility in completion handler
                __block id<MTLBuffer> pixelStatsBuffer = nil;
                __block id<MTLBuffer> globalStatsBuffer = nil;

                if (useInstrumented) {
                    id<MTLComputePipelineState> shadowsInstrumentedPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_shadows_instrumented_;
                    [shadowEncoder setComputePipelineState:shadowsInstrumentedPipeline];

                    // Allocate instrumentation buffers
                    size_t pixelStatsSize = width_ * height_ * 32;  // BVHTraversalStats is 32 bytes
                    size_t globalStatsSize = 32;  // BVHGlobalStats is 32 bytes

                    pixelStatsBuffer = [device newBufferWithLength:pixelStatsSize
                                                          options:MTLResourceStorageModeShared];
                    globalStatsBuffer = [device newBufferWithLength:globalStatsSize
                                                           options:MTLResourceStorageModeShared];

                    // Clear global stats
                    memset([globalStatsBuffer contents], 0, globalStatsSize);

                    // Phase 1: Pass shadow dimensions + G-buffer dimensions to shader
                    id<MTLBuffer> shadowWidthBuffer = (__bridge id<MTLBuffer>)shadow_width_buffer_;
                    id<MTLBuffer> shadowHeightBuffer = (__bridge id<MTLBuffer>)shadow_height_buffer_;

                    // Set all the regular buffers
                    [shadowEncoder setBuffer:gbufferBuffer offset:0 atIndex:0];           // G-buffer (full res)
                    [shadowEncoder setBuffer:shadowResultsBuffer offset:0 atIndex:1];     // Shadow output (reduced res)
                    [shadowEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:2];       // Shadow width (reduced)
                    [shadowEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:3];      // Shadow height (reduced)
                    [shadowEncoder setBuffer:widthBuffer offset:0 atIndex:4];             // G-buffer width (full)
                    [shadowEncoder setBuffer:heightBuffer offset:0 atIndex:5];            // G-buffer height (full)
                    [shadowEncoder setBuffer:singleLightBuffer offset:0 atIndex:6];       // Light (shifted +2)
                    [shadowEncoder setBuffer:oneLightCountBuffer offset:0 atIndex:7];     // Light count (shifted +2)
                    [shadowEncoder setBuffer:bvhNodesBuffer offset:0 atIndex:8];          // BVH nodes (shifted +2)
                    [shadowEncoder setBuffer:bvhNodeCountBuffer offset:0 atIndex:9];      // BVH node count (shifted +2)
                    [shadowEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10];     // BVH triangles (shifted +2)
                    [shadowEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11]; // BVH tri count (shifted +2)

                    // Add instrumentation buffers
                    [shadowEncoder setBuffer:pixelStatsBuffer offset:0 atIndex:12];       // Pixel stats (shifted +2)
                    [shadowEncoder setBuffer:globalStatsBuffer offset:0 atIndex:13];      // Global stats (shifted +2)
                } else {
                    // Regular non-instrumented path
                    // Check if batched version should be used

                    // ITERATION 0: Baseline fact gathering - log kernel selection
                    static int kernel_log_count = 0;
                    if (kernel_log_count < 3 && light_idx == 0) {
                        std::cout << "[ITER0_DEBUG] === PASS 2: Shadow Kernel Selection ===" << std::endl;
                        std::cout << "[ITER0_DEBUG] USE_GPU_RAY_BATCHING = " << Optimizations::USE_GPU_RAY_BATCHING << std::endl;
                        std::cout << "[ITER0_DEBUG] compute_pipeline_shadows_batched_ = " << compute_pipeline_shadows_batched_ << std::endl;
                        std::cout.flush();
                        kernel_log_count++;
                    }

                    if (Optimizations::USE_GPU_RAY_BATCHING && compute_pipeline_shadows_batched_) {
                        // Phase II-B: Batched shadow rays (8 pixels per thread)
                        id<MTLComputePipelineState> shadowsBatchedPipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_shadows_batched_;
                        [shadowEncoder setComputePipelineState:shadowsBatchedPipeline];
                        if (kernel_log_count <= 3 && light_idx == 0) {
                            std::cout << "[ITER0_DEBUG] SELECTED: Batched kernel (8 pixels/thread)" << std::endl;
                            std::cout.flush();
                        }
                    } else {
                        // Regular: 1 pixel per thread
                        [shadowEncoder setComputePipelineState:shadowsPipeline];
                        if (kernel_log_count <= 3 && light_idx == 0) {
                            std::cout << "[ITER0_DEBUG] SELECTED: Regular kernel (1 pixel/thread)" << std::endl;
                            std::cout.flush();
                        }
                    }

                    // Phase 1: Pass shadow dimensions + G-buffer dimensions to shader
                    id<MTLBuffer> shadowWidthBuffer = (__bridge id<MTLBuffer>)shadow_width_buffer_;
                    id<MTLBuffer> shadowHeightBuffer = (__bridge id<MTLBuffer>)shadow_height_buffer_;

                    [shadowEncoder setBuffer:gbufferBuffer offset:0 atIndex:0];           // G-buffer (full res)
                    [shadowEncoder setBuffer:shadowResultsBuffer offset:0 atIndex:1];     // Shadow output (reduced res)
                    [shadowEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:2];       // Shadow width (reduced)
                    [shadowEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:3];      // Shadow height (reduced)
                    [shadowEncoder setBuffer:widthBuffer offset:0 atIndex:4];             // G-buffer width (full)
                    [shadowEncoder setBuffer:heightBuffer offset:0 atIndex:5];            // G-buffer height (full)
                    [shadowEncoder setBuffer:singleLightBuffer offset:0 atIndex:6];       // Light (shifted +2)
                    [shadowEncoder setBuffer:oneLightCountBuffer offset:0 atIndex:7];     // Light count (shifted +2)
                    [shadowEncoder setBuffer:bvhNodesBuffer offset:0 atIndex:8];          // BVH nodes (shifted +2)
                    [shadowEncoder setBuffer:bvhNodeCountBuffer offset:0 atIndex:9];      // BVH node count (shifted +2)
                    [shadowEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10];     // BVH triangles (shifted +2)
                    [shadowEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11]; // BVH tri count (shifted +2)
                }

                // Dispatch with appropriate grid size
                // ITERATION 0: Log dispatch parameters
                static int dispatch_log_count = 0;
                if (dispatch_log_count < 3 && light_idx == 0) {
                    std::cout << "[ITER0_DEBUG] === Dispatch Parameters ===" << std::endl;
                    std::cout << "[ITER0_DEBUG] G-buffer resolution: " << width_ << "x" << height_ << std::endl;
                    std::cout << "[ITER0_DEBUG] Shadow resolution: " << shadow_width_ << "x" << shadow_height_ << " (RUNTIME)" << std::endl;
                    std::cout << "[ITER0_DEBUG] Light count: " << light_count << " (processing light " << light_idx << ")" << std::endl;
                    std::cout.flush();
                    dispatch_log_count++;
                }

                if (Optimizations::USE_GPU_RAY_BATCHING && compute_pipeline_shadows_batched_) {
                    // Phase 1: Batched dispatch at REDUCED shadow resolution
                    uint32_t total_pixels = shadow_width_ * shadow_height_;  // Phase 1: Use shadow resolution
                    uint32_t total_threads = (total_pixels + 7) / 8;  // Round up, 8 pixels per thread
                    MTLSize batched_threads_per_grid = MTLSizeMake(total_threads, 1, 1);
                    MTLSize batched_threads_per_threadgroup = MTLSizeMake(64, 1, 1);  // 64 threads per group

                    if (dispatch_log_count <= 3 && light_idx == 0) {
                        std::cout << "[ITER0_DEBUG] BATCHED DISPATCH:" << std::endl;
                        std::cout << "[ITER0_DEBUG]   total_pixels = " << total_pixels << " (REDUCED from " << (width_ * height_) << ")" << std::endl;
                        std::cout << "[ITER0_DEBUG]   total_threads = " << total_threads << " (pixels / 8)" << std::endl;
                        std::cout << "[ITER0_DEBUG]   grid = (" << total_threads << ", 1, 1)" << std::endl;
                        std::cout << "[ITER0_DEBUG]   threadgroup = (64, 1, 1)" << std::endl;
                        std::cout.flush();
                    }

                    [shadowEncoder dispatchThreads:batched_threads_per_grid threadsPerThreadgroup:batched_threads_per_threadgroup];
                } else {
                    // Phase 1: Regular 2D dispatch at REDUCED shadow resolution
                    MTLSize shadowThreadsPerGrid = MTLSizeMake(shadow_width_, shadow_height_, 1);
                    MTLSize shadowThreadsPerThreadgroup = MTLSizeMake(Optimizations::GPU_THREADS_SHADOW, Optimizations::GPU_THREADS_SHADOW, 1);

                    if (dispatch_log_count <= 3 && light_idx == 0) {
                        std::cout << "[ITER0_DEBUG] REGULAR DISPATCH:" << std::endl;
                        std::cout << "[ITER0_DEBUG]   grid = (" << shadow_width_ << ", " << shadow_height_ << ", 1) [REDUCED RES - RUNTIME]" << std::endl;
                        std::cout << "[ITER0_DEBUG]   threadgroup = (" << Optimizations::GPU_THREADS_SHADOW << ", " << Optimizations::GPU_THREADS_SHADOW << ", 1)" << std::endl;
                        std::cout.flush();
                    }

                    [shadowEncoder dispatchThreads:shadowThreadsPerGrid threadsPerThreadgroup:shadowThreadsPerThreadgroup];
                }
                [shadowEncoder endEncoding];

                // Completion handler with optional instrumentation reporting
                if (useInstrumented) {
                    __block uint32_t lightIndex = light_idx;

                    [cmdShadow addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                        logCommandBufferError(cb, "Pass 2 (Shadow per-light instrumented)");
                        auto completion = std::chrono::high_resolution_clock::now();
                        timing->shadow_gpu[light_idx] = std::chrono::duration<double, std::milli>(completion - shadow_commits[light_idx]).count();

                        // Read and report BVH statistics
                        struct {
                            uint32_t total_nodes_visited;
                            uint32_t total_triangles_tested;
                            uint32_t total_early_terminations;
                            uint32_t max_traversal_depth;
                            uint32_t coherency_score;
                            uint32_t _padding[3];
                        } *globalStats = (decltype(globalStats))[globalStatsBuffer contents];

                        std::cout << "[BVH_PROFILE] Light " << lightIndex << " Statistics:" << std::endl;
                        std::cout << "  Total nodes visited: " << globalStats->total_nodes_visited << std::endl;
                        std::cout << "  Total triangles tested: " << globalStats->total_triangles_tested << std::endl;
                        std::cout << "  Early terminations: " << globalStats->total_early_terminations << std::endl;
                        std::cout << "  Max traversal depth: " << globalStats->max_traversal_depth << std::endl;
                        std::cout << "  Coherency score: " << globalStats->coherency_score << std::endl;

                        // Calculate averages
                        uint32_t totalPixels = width_ * height_;
                        if (totalPixels > 0) {
                            std::cout << "  Avg nodes/pixel: " << (float)globalStats->total_nodes_visited / totalPixels << std::endl;
                            std::cout << "  Avg triangles/pixel: " << (float)globalStats->total_triangles_tested / totalPixels << std::endl;
                        }

                        timing->completed_passes++;
                    }];
                } else {
                    // Regular completion handler
                    [cmdShadow addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                        logCommandBufferError(cb, "Pass 2 (Shadow per-light)");
                        auto completion = std::chrono::high_resolution_clock::now();
                        timing->shadow_gpu[light_idx] = std::chrono::duration<double, std::milli>(completion - shadow_commits[light_idx]).count();
                        timing->completed_passes++;
                    }];
                }
                [cmdShadow commit];

                // Measure shadow encoding time (only when sampling)
                if (should_profile_submit) {
                    auto shadow_encode_end = std::chrono::high_resolution_clock::now();
                    shadow_encode_ms[light_idx] = std::chrono::duration<double, std::milli>(
                        shadow_encode_end - shadow_encode_start).count();
                }
                }  // End for (light_idx)
            }  // End else (per-light path)
        }  // End if (light_count > 0)

        // PASS 2.5 (screen-space GI) retired 2026-07-29: the BVH-indirect
        // and SSGI producers were both compile-time disabled (replaced by
        // SSDO + DDGI) and contributed zeros to Pass 3.

        // Shadow dimension buffers (needed by both pass 2.05 denoise and pass 3)
        id<MTLBuffer> shadowWidthBuffer = (__bridge id<MTLBuffer>)shadow_width_buffer_;
        id<MTLBuffer> shadowHeightBuffer = (__bridge id<MTLBuffer>)shadow_height_buffer_;

        // PASS 2.05: DENOISE SHADOW BUFFER (sync path)
        // Only needed for PCSS mode — deterministic modes produce clean output
        id<MTLBuffer> shadowBufferForPass3Sync = shadowResultsBuffer;
        if constexpr (Optimizations::PENUMBRA_MODE == Optimizations::PenumbraMode::PCSS) {
        if (!Optimizations::SKIP_SHADOW_DENOISE) {
            id<MTLComputePipelineState> denoiseShadowPipelineSync = compute_pipeline_denoise_shadow_
                ? (__bridge id<MTLComputePipelineState>)compute_pipeline_denoise_shadow_ : nil;
            if (denoiseShadowPipelineSync && shadowDenoisedBuffer != shadowResultsBuffer) {
                id<MTLCommandBuffer> cmdDenoiseShadow = createTrackedCommandBuffer(commandQueue);
                id<MTLComputeCommandEncoder> denoiseShadowEncoder = [cmdDenoiseShadow computeCommandEncoder];
                [denoiseShadowEncoder setComputePipelineState:denoiseShadowPipelineSync];
                [denoiseShadowEncoder setBuffer:shadowResultsBuffer offset:0 atIndex:0];
                [denoiseShadowEncoder setBuffer:shadowDenoisedBuffer offset:0 atIndex:1];
                [denoiseShadowEncoder setBuffer:gbufferBuffer offset:0 atIndex:2];
                [denoiseShadowEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:3];
                [denoiseShadowEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:4];
                [denoiseShadowEncoder setBuffer:widthBuffer offset:0 atIndex:5];
                [denoiseShadowEncoder setBuffer:heightBuffer offset:0 atIndex:6];

                MTLSize shadowDenoiseGrid = MTLSizeMake(shadow_width_, shadow_height_, 1);
                NSUInteger sdW = denoiseShadowPipelineSync.threadExecutionWidth;
                NSUInteger sdH = denoiseShadowPipelineSync.maxTotalThreadsPerThreadgroup / sdW;
                MTLSize shadowDenoiseThreadgroup = MTLSizeMake(sdW, sdH, 1);
                [denoiseShadowEncoder dispatchThreads:shadowDenoiseGrid threadsPerThreadgroup:shadowDenoiseThreadgroup];
                [denoiseShadowEncoder endEncoding];
                [cmdDenoiseShadow addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                    logCommandBufferError(cb, "Pass 2.05 (Shadow Denoise)");
                }];
                [cmdDenoiseShadow commit];
                // Serial queue guarantees GPU executes before Pass 3
                shadowBufferForPass3Sync = shadowDenoisedBuffer;
            }
        }
        } // end PCSS-only shadow denoise

        // PASS 2.6 (GI denoise) retired 2026-07-29 with its producer.

        // PASS 2.5b/c: DDGI probe tracing + update (sync path)
        if constexpr (Optimizations::USE_DDGI) {
            id<MTLComputePipelineState> ddgiTrace = compute_pipeline_ddgi_trace_
                ? (__bridge id<MTLComputePipelineState>)compute_pipeline_ddgi_trace_ : nil;
            id<MTLComputePipelineState> ddgiUpdate = compute_pipeline_ddgi_update_
                ? (__bridge id<MTLComputePipelineState>)compute_pipeline_ddgi_update_ : nil;
            id<MTLAccelerationStructure> accel = acceleration_structure_
                ? (__bridge id<MTLAccelerationStructure>)acceleration_structure_ : nil;

            if (ddgiTrace && ddgiUpdate && accel) {
                const uint32_t total_probes = Optimizations::DDGI_GRID_X * Optimizations::DDGI_GRID_Y * Optimizations::DDGI_GRID_Z;
                const uint32_t total_rays = total_probes * Optimizations::DDGI_RAYS_PER_PROBE;
                const uint32_t total_texels = total_probes * 64;

                // Allocate DDGI buffers if needed
                size_t irr_size = total_probes * 64 * sizeof(float) * 4;
                size_t depth_size = total_probes * 64 * sizeof(float) * 2;
                size_t ray_size = total_rays * sizeof(float) * 4;

                id<MTLBuffer> ddgiIrr = nil, ddgiDepth = nil, ddgiRays = nil;
                if (!ddgi_irradiance_buffer_ || ddgi_irradiance_capacity_ < irr_size) {
                    ddgiIrr = [device newBufferWithLength:irr_size options:MTLResourceStorageModeShared];
                    if (ddgiIrr) {
                        memset([ddgiIrr contents], 0, irr_size);
                        if (ddgi_irradiance_buffer_) CFBridgingRelease(ddgi_irradiance_buffer_);
                        ddgi_irradiance_buffer_ = (__bridge_retained void*)ddgiIrr;
                        ddgi_irradiance_capacity_ = irr_size;
                    }
                } else { ddgiIrr = (__bridge id<MTLBuffer>)ddgi_irradiance_buffer_; }

                if (!ddgi_depth_buffer_ || ddgi_depth_capacity_ < depth_size) {
                    ddgiDepth = [device newBufferWithLength:depth_size options:MTLResourceStorageModeShared];
                    if (ddgiDepth) {
                        memset([ddgiDepth contents], 0, depth_size);
                        if (ddgi_depth_buffer_) CFBridgingRelease(ddgi_depth_buffer_);
                        ddgi_depth_buffer_ = (__bridge_retained void*)ddgiDepth;
                        ddgi_depth_capacity_ = depth_size;
                    }
                } else { ddgiDepth = (__bridge id<MTLBuffer>)ddgi_depth_buffer_; }

                if (!ddgi_ray_results_buffer_ || ddgi_ray_results_capacity_ < ray_size) {
                    ddgiRays = [device newBufferWithLength:ray_size options:MTLResourceStorageModeShared];
                    if (ddgiRays) {
                        if (ddgi_ray_results_buffer_) CFBridgingRelease(ddgi_ray_results_buffer_);
                        ddgi_ray_results_buffer_ = (__bridge_retained void*)ddgiRays;
                        ddgi_ray_results_capacity_ = ray_size;
                    }
                } else { ddgiRays = (__bridge id<MTLBuffer>)ddgi_ray_results_buffer_; }

                if (ddgiIrr && ddgiDepth && ddgiRays) {
                    // Grid origin: center the grid on the world
                    float grid_ox = -(Optimizations::DDGI_GRID_X - 1) * Optimizations::DDGI_PROBE_SPACING / 2.0f;
                    float grid_oy = -(Optimizations::DDGI_GRID_Y - 1) * Optimizations::DDGI_PROBE_SPACING / 2.0f;
                    float grid_oz = 0.0f;  // Start at ground level

                    struct { float o[3]; float spacing; uint32_t d[3]; uint32_t rays; uint32_t frame; float hyst; float nbias; float intensity; } ddgi_p = {
                        {grid_ox, grid_oy, grid_oz}, Optimizations::DDGI_PROBE_SPACING,
                        {(uint32_t)Optimizations::DDGI_GRID_X, (uint32_t)Optimizations::DDGI_GRID_Y, (uint32_t)Optimizations::DDGI_GRID_Z},
                        (uint32_t)Optimizations::DDGI_RAYS_PER_PROBE,
                        ddgi_frame_counter_++,
                        Optimizations::DDGI_HYSTERESIS, Optimizations::DDGI_NORMAL_BIAS, Optimizations::DDGI_INTENSITY
                    };

                    id<MTLBuffer> ddgiParamsBuf = [device newBufferWithBytes:&ddgi_p length:sizeof(ddgi_p) options:MTLResourceStorageModeShared];

                    // Pass 2.5b: Trace probes
                    {
                        id<MTLCommandBuffer> cmd = createTrackedCommandBuffer(commandQueue);
                        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                        [enc setComputePipelineState:ddgiTrace];
                        [enc setAccelerationStructure:accel atBufferIndex:0];
                        [enc setBuffer:bvhTrianglesBuffer offset:0 atIndex:1];
                        [enc setBuffer:bvhTriangleCountBuffer offset:0 atIndex:2];
                        [enc setBuffer:lightsBuffer offset:0 atIndex:3];
                        [enc setBuffer:lightCountBuffer offset:0 atIndex:4];
                        [enc setBuffer:ddgiParamsBuf offset:0 atIndex:5];
                        [enc setBuffer:ddgiRays offset:0 atIndex:6];
                        MTLSize grid = MTLSizeMake(total_rays, 1, 1);
                        NSUInteger tw = ddgiTrace.threadExecutionWidth;
                        [enc dispatchThreads:grid threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
                        [enc endEncoding];
                        if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                            [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                static int c = 0;
                                ::logosphere::telemetry::record_gpu_stage(::logosphere::telemetry::GpuStage::Pass25bDdgiTrace, ms, tel_frame);
                        if (c++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2.5b (DDGI trace): %.2f ms", ms);
                            }];
                        }
                        [cmd commit];
                    }

                    // Pass 2.5c: Update probes
                    {
                        id<MTLCommandBuffer> cmd = createTrackedCommandBuffer(commandQueue);
                        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
                        [enc setComputePipelineState:ddgiUpdate];
                        [enc setBuffer:ddgiRays offset:0 atIndex:0];
                        [enc setBuffer:ddgiIrr offset:0 atIndex:1];
                        [enc setBuffer:ddgiDepth offset:0 atIndex:2];
                        [enc setBuffer:ddgiParamsBuf offset:0 atIndex:3];
                        MTLSize grid = MTLSizeMake(total_texels, 1, 1);
                        NSUInteger tw = ddgiUpdate.threadExecutionWidth;
                        [enc dispatchThreads:grid threadsPerThreadgroup:MTLSizeMake(tw, 1, 1)];
                        [enc endEncoding];
                        if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                            [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                static int c = 0;
                                ::logosphere::telemetry::record_gpu_stage(::logosphere::telemetry::GpuStage::Pass25cDdgiUpdate, ms, tel_frame);
                        if (c++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2.5c (DDGI update): %.2f ms", ms);
                            }];
                        }
                        [cmd commit];
                    }
                }
            }
        }

        // PASS 2.7/2.8: SSAO (sync path)
        id<MTLBuffer> ssaoBufferForPass3Sync = nil;
        if constexpr (Optimizations::USE_SSAO) {
            id<MTLComputePipelineState> ssaoPipeline = compute_pipeline_ssao_
                ? (__bridge id<MTLComputePipelineState>)compute_pipeline_ssao_ : nil;
            if (ssaoPipeline) {
                // Reuse persistent SSAO buffers (same as async path)
                constexpr size_t ssdo_elem_sync = Optimizations::SSDO_HALF_PRECISION ? 8 : 16;
                size_t ssaoSize = width_ * height_ * ssdo_elem_sync;
                id<MTLBuffer> ssaoResBuf = nil;
                id<MTLBuffer> ssaoDnBuf = nil;
                if (!ssao_results_buffer_async_[bufIdx] || ssao_results_capacity_async_[bufIdx] < ssaoSize) {
                    if (ssao_results_buffer_async_[bufIdx]) CFBridgingRelease(ssao_results_buffer_async_[bufIdx]);
                    ssaoResBuf = [device newBufferWithLength:ssaoSize options:MTLResourceStorageModeShared];
                    if (ssaoResBuf) { ssao_results_buffer_async_[bufIdx] = (__bridge_retained void*)ssaoResBuf; ssao_results_capacity_async_[bufIdx] = ssaoSize; }
                } else { ssaoResBuf = (__bridge id<MTLBuffer>)ssao_results_buffer_async_[bufIdx]; }
                if (!ssao_denoised_buffer_async_[bufIdx] || ssao_denoised_capacity_async_[bufIdx] < ssaoSize) {
                    if (ssao_denoised_buffer_async_[bufIdx]) CFBridgingRelease(ssao_denoised_buffer_async_[bufIdx]);
                    ssaoDnBuf = [device newBufferWithLength:ssaoSize options:MTLResourceStorageModeShared];
                    if (ssaoDnBuf) { ssao_denoised_buffer_async_[bufIdx] = (__bridge_retained void*)ssaoDnBuf; ssao_denoised_capacity_async_[bufIdx] = ssaoSize; }
                } else { ssaoDnBuf = (__bridge id<MTLBuffer>)ssao_denoised_buffer_async_[bufIdx]; }

                struct SSAOParamsGPU {
                    uint32_t sample_count; float screen_radius; float world_radius;
                    float bias; float intensity; float _padding[3];
                } ssao_params = {
                    (uint32_t)Optimizations::SSAO_SAMPLE_COUNT,
                    Optimizations::SSAO_SCREEN_RADIUS, Optimizations::SSAO_WORLD_RADIUS,
                    Optimizations::SSAO_BIAS, Optimizations::SSAO_INTENSITY, {0,0,0}
                };

                id<MTLCommandBuffer> cmdSSAO = createTrackedCommandBuffer(commandQueue);
                id<MTLComputeCommandEncoder> ssaoEnc = [cmdSSAO computeCommandEncoder];
                [ssaoEnc setComputePipelineState:ssaoPipeline];
                [ssaoEnc setBuffer:gbufferBuffer offset:0 atIndex:0];
                [ssaoEnc setBuffer:ssaoResBuf offset:0 atIndex:1];
                [ssaoEnc setBuffer:widthBuffer offset:0 atIndex:2];
                [ssaoEnc setBuffer:heightBuffer offset:0 atIndex:3];
                id<MTLBuffer> ssaoParamsBuf = [device newBufferWithBytes:&ssao_params length:sizeof(ssao_params) options:MTLResourceStorageModeShared];
                [ssaoEnc setBuffer:ssaoParamsBuf offset:0 atIndex:4];
                // SSDO: pass shadow results for bounce color
                if (shadowBufferForPass3Sync) {
                    [ssaoEnc setBuffer:shadowBufferForPass3Sync offset:0 atIndex:5];
                }
                MTLSize ssaoGrid = MTLSizeMake(width_, height_, 1);
                NSUInteger ssaoW = ssaoPipeline.threadExecutionWidth;
                NSUInteger ssaoH = ssaoPipeline.maxTotalThreadsPerThreadgroup / ssaoW;
                [ssaoEnc dispatchThreads:ssaoGrid threadsPerThreadgroup:MTLSizeMake(ssaoW, ssaoH, 1)];
                [ssaoEnc endEncoding];
                if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                    [cmdSSAO addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                        double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                        static int c = 0;
                        ::logosphere::telemetry::record_gpu_stage(::logosphere::telemetry::GpuStage::Pass27Ssdo, ms, tel_frame);
                        if (c++ % 60 == 0) NSLog(@"[GPU_TIMESTAMP] Pass 2.7 (SSDO): %.2f ms", ms);
                    }];
                }
                [cmdSSAO commit];

                // Denoise
                id<MTLComputePipelineState> ssaoDnPipeline = compute_pipeline_denoise_ssao_
                    ? (__bridge id<MTLComputePipelineState>)compute_pipeline_denoise_ssao_ : nil;
                if (ssaoDnPipeline && ssaoDnBuf) {
                    id<MTLBuffer> ping = ssaoResBuf, pong = ssaoDnBuf;
                    NSUInteger dnW = ssaoDnPipeline.threadExecutionWidth;
                    NSUInteger dnH = ssaoDnPipeline.maxTotalThreadsPerThreadgroup / dnW;
                    static int dn_frame = 0;
                    const bool log_dn = (dn_frame++ % 60 == 0);
                    for (int pass = 0; pass < Optimizations::SSAO_DENOISE_PASSES; pass++) {
                        int step = 1 << pass;
                        id<MTLCommandBuffer> cmdDn = createTrackedCommandBuffer(commandQueue);
                        id<MTLComputeCommandEncoder> enc = [cmdDn computeCommandEncoder];
                        [enc setComputePipelineState:ssaoDnPipeline];
                        [enc setBuffer:ping offset:0 atIndex:0];
                        [enc setBuffer:pong offset:0 atIndex:1];
                        [enc setBuffer:gbufferBuffer offset:0 atIndex:2];
                        [enc setBuffer:widthBuffer offset:0 atIndex:3];
                        [enc setBuffer:heightBuffer offset:0 atIndex:4];
                        id<MTLBuffer> stepBuf = [device newBufferWithBytes:&step length:sizeof(int) options:MTLResourceStorageModeShared];
                        [enc setBuffer:stepBuf offset:0 atIndex:5];
                        [enc dispatchThreads:ssaoGrid threadsPerThreadgroup:MTLSizeMake(dnW, dnH, 1)];
                        [enc endEncoding];
                        if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                            {
                                const int pass_idx = pass;
                                const bool log_pass = log_dn;
                                [cmdDn addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                                    if (cb.GPUStartTime <= 0 || cb.GPUEndTime <= 0) return;
                                    double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                                    ::logosphere::telemetry::record_gpu_stage(
                                        ::logosphere::telemetry::GpuStage::Pass28SsdoDenoise, ms, tel_frame);
                                    if (log_pass) {
                                        NSLog(@"[GPU_TIMESTAMP] Pass 2.8 (SSDO denoise %d): %.2f ms", pass_idx, ms);
                                    }
                                }];
                            }
                        }
                        [cmdDn commit];
                        id<MTLBuffer> tmp = ping; ping = pong; pong = tmp;
                    }
                    ssaoBufferForPass3Sync = ssaoDnBuf;  // odd passes: result in pong=ssaoDnBuf
                } else {
                    ssaoBufferForPass3Sync = ssaoResBuf;
                }
            }
        }

        // COMMAND BUFFER LAST: Pass 3 (Lighting) + Semaphore release
        auto pass3_encode_start = should_measure_all_encoding ?
                                 std::chrono::high_resolution_clock::now() :
                                 std::chrono::high_resolution_clock::time_point{};

        id<MTLCommandBuffer> cmdPass3 = createTrackedCommandBuffer(commandQueue);
        auto pass3_commit = std::chrono::high_resolution_clock::now();

        id<MTLComputeCommandEncoder> lightingEncoder = [cmdPass3 computeCommandEncoder];
        [lightingEncoder setComputePipelineState:lightingPipeline];

        [lightingEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];       // Final framebuffer output (full res)
        [lightingEncoder setBuffer:gbufferBuffer offset:0 atIndex:1];           // G-buffer input (full res)
        [lightingEncoder setBuffer:shadowBufferForPass3Sync offset:0 atIndex:2]; // Shadow results input (denoised or raw, reduced res)
        [lightingEncoder setBuffer:widthBuffer offset:0 atIndex:3];             // Framebuffer width (full)
        [lightingEncoder setBuffer:heightBuffer offset:0 atIndex:4];            // Framebuffer height (full)
        [lightingEncoder setBuffer:shadowWidthBuffer offset:0 atIndex:5];       // Shadow width (reduced)
        [lightingEncoder setBuffer:shadowHeightBuffer offset:0 atIndex:6];      // Shadow height (reduced)
        [lightingEncoder setBuffer:lightsBuffer offset:0 atIndex:7];            // Light sources (shifted +2)
        [lightingEncoder setBuffer:lightCountBuffer offset:0 atIndex:8];        // Number of lights (shifted +2)
        if (lightSourceMapBuffer && mapSizeBuffer) {
            [lightingEncoder setBuffer:lightSourceMapBuffer offset:0 atIndex:9];
            [lightingEncoder setBuffer:mapSizeBuffer offset:0 atIndex:10];
        } else {
            uint8_t dummy_map = 0;
            uint32_t dummy_size = 0;
            id<MTLBuffer> dummyMapBuffer = [device newBufferWithBytes:&dummy_map length:sizeof(uint8_t) options:MTLResourceStorageModeShared];
            id<MTLBuffer> dummySizeBuffer = [device newBufferWithBytes:&dummy_size length:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            [lightingEncoder setBuffer:dummyMapBuffer offset:0 atIndex:9];
            [lightingEncoder setBuffer:dummySizeBuffer offset:0 atIndex:10];
        }

        // Add particle transforms buffer (for local coordinate pattern rendering)
        if (particle_transforms && particle_count > 0) {
            size_t transformsSize = particle_count * 32; // ParticleTransform is 32 bytes
            id<MTLBuffer> transformsBuffer = [device newBufferWithBytes:particle_transforms
                                                                 length:transformsSize
                                                                options:MTLResourceStorageModeShared];
            id<MTLBuffer> particleCountBuffer = [device newBufferWithBytes:&particle_count
                                                                    length:sizeof(uint32_t)
                                                                   options:MTLResourceStorageModeShared];
            [lightingEncoder setBuffer:transformsBuffer offset:0 atIndex:11];
            [lightingEncoder setBuffer:particleCountBuffer offset:0 atIndex:12];
        } else {
            // Provide empty buffers - shader expects them at indices 11 & 12
            uint32_t dummy_count = 0;
            id<MTLBuffer> dummyTransformBuffer = [device newBufferWithLength:32
                                                                     options:MTLResourceStorageModeShared];
            id<MTLBuffer> dummyCountBuffer = [device newBufferWithBytes:&dummy_count
                                                                 length:sizeof(uint32_t)
                                                                options:MTLResourceStorageModeShared];
            [lightingEncoder setBuffer:dummyTransformBuffer offset:0 atIndex:11];
            [lightingEncoder setBuffer:dummyCountBuffer offset:0 atIndex:12];
        }

        // Shadow distance culling parameters (for distance fade in Pass 3)
        // Pass camera position and culling radius from optimization_flags.h
        {
            float shadow_camera_pos[2] = {shadow_culling_camera_x_, shadow_culling_camera_y_};
            float shadow_cull_radius = Optimizations::USE_SHADOW_DISTANCE_CULLING ? Optimizations::SHADOW_CULL_RADIUS : 0.0f;
            float shadow_fade_range[2] = { Optimizations::SHADOW_FADE_START, Optimizations::SHADOW_FADE_END };
            [lightingEncoder setBytes:shadow_camera_pos length:sizeof(shadow_camera_pos) atIndex:13];
            [lightingEncoder setBytes:&shadow_cull_radius length:sizeof(shadow_cull_radius) atIndex:14];
            [lightingEncoder setBytes:shadow_fade_range length:sizeof(shadow_fade_range) atIndex:15];
        }

        // Buffers 16/18/19 retired with the screen-space GI path (2026-07-29).
        // Indices left unused so every other binding keeps its slot.

        // Per-pixel light color ratio (only valid when deterministic shadow kernel wrote it)
        if (used_deterministic_shadows && lightColorBuffer) {
            [lightingEncoder setBuffer:lightColorBuffer offset:0 atIndex:17];
        }

        // SSAO buffer for Pass 3 (sync path)
        if (ssaoBufferForPass3Sync) {
            [lightingEncoder setBuffer:ssaoBufferForPass3Sync offset:0 atIndex:20];
        }

        // DDGI buffers for Pass 3 (sync path)
        if constexpr (Optimizations::USE_DDGI) {
            id<MTLBuffer> ddgiIrr = ddgi_irradiance_buffer_ ? (__bridge id<MTLBuffer>)ddgi_irradiance_buffer_ : nil;
            id<MTLBuffer> ddgiDep = ddgi_depth_buffer_ ? (__bridge id<MTLBuffer>)ddgi_depth_buffer_ : nil;
            if (ddgiIrr && ddgiDep) {
                [lightingEncoder setBuffer:ddgiIrr offset:0 atIndex:21];
                [lightingEncoder setBuffer:ddgiDep offset:0 atIndex:22];
                float grid_ox = -(Optimizations::DDGI_GRID_X - 1) * Optimizations::DDGI_PROBE_SPACING / 2.0f;
                float grid_oy = -(Optimizations::DDGI_GRID_Y - 1) * Optimizations::DDGI_PROBE_SPACING / 2.0f;
                struct { float o[3]; float spacing; uint32_t d[3]; uint32_t rays; uint32_t frame; float hyst; float nbias; float intensity; } ddgi_p = {
                    {grid_ox, grid_oy, 0.0f}, Optimizations::DDGI_PROBE_SPACING,
                    {(uint32_t)Optimizations::DDGI_GRID_X, (uint32_t)Optimizations::DDGI_GRID_Y, (uint32_t)Optimizations::DDGI_GRID_Z},
                    (uint32_t)Optimizations::DDGI_RAYS_PER_PROBE, ddgi_frame_counter_,
                    Optimizations::DDGI_HYSTERESIS, Optimizations::DDGI_NORMAL_BIAS, Optimizations::DDGI_INTENSITY
                };
                [lightingEncoder setBytes:&ddgi_p length:sizeof(ddgi_p) atIndex:23];
            }
        }

        // CRITICAL DEBUG: Verify Pass 3 dispatch dimensions
        static int pass3_log = 0;
        if (pass3_log < 3) {
            std::cout << "[PASS3_DISPATCH] threadsPerGrid=(" << threadsPerGrid.width << "x" << threadsPerGrid.height
                      << ") width_=" << width_ << " height_=" << height_
                      << " shadow_width_=" << shadow_width_ << " shadow_height_=" << shadow_height_ << std::endl;
            pass3_log++;
        }

        [lightingEncoder dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
        [lightingEncoder endEncoding];

        // =========================================================================
        // PASS 3.5: FORWARD TRANSPARENT RENDERING (Optional)
        // =========================================================================
        if constexpr (Optimizations::USE_TRANSPARENCY) {
            uint32_t trans_count = transparent_triangle_count_async_[bufIdx];
            // Prefer the RT-intersector kernel (any-hit occlusion on the
            // driver accel) — the software-BVH walk measured ~13 ms/frame
            // for 96 wing triangles. Fallback: the walk kernel.
            bool use_rt_transparent = supports_raytracing_ &&
                                      acceleration_structure_ != nullptr &&
                                      compute_pipeline_forward_transparent_rt_ != nullptr;
            void* transparent_pipeline_handle = use_rt_transparent
                ? compute_pipeline_forward_transparent_rt_
                : compute_pipeline_forward_transparent_;
            if (trans_count > 0 && transparent_pipeline_handle != nullptr) {
                id<MTLComputePipelineState> transparentPipeline =
                    (__bridge id<MTLComputePipelineState>)transparent_pipeline_handle;
                id<MTLBuffer> transTrianglesBuffer =
                    (__bridge id<MTLBuffer>)transparent_triangles_buffer_async_[bufIdx];

                id<MTLComputeCommandEncoder> transEncoder = [cmdPass3 computeCommandEncoder];
                [transEncoder setComputePipelineState:transparentPipeline];
                if (use_rt_transparent) {
                    id<MTLAccelerationStructure> transAccel =
                        (__bridge id<MTLAccelerationStructure>)acceleration_structure_;
                    [transEncoder setAccelerationStructure:transAccel atBufferIndex:13];
                }

                // Match forward_transparent.metal buffer bindings (indices 0-11)
                [transEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];   // pixel_buffer (read-write)
                [transEncoder setBuffer:depthBufferGPU offset:0 atIndex:1];      // opaque_depth (read-only)
                [transEncoder setBuffer:(__bridge id<MTLBuffer>)width_buffer_ offset:0 atIndex:2];
                [transEncoder setBuffer:(__bridge id<MTLBuffer>)height_buffer_ offset:0 atIndex:3];
                [transEncoder setBuffer:transTrianglesBuffer offset:0 atIndex:4]; // transparent triangles
                [transEncoder setBytes:&trans_count length:sizeof(uint32_t) atIndex:5]; // triangle_count
                [transEncoder setBuffer:lightsBuffer offset:0 atIndex:6];        // lights
                [transEncoder setBuffer:lightCountBuffer offset:0 atIndex:7];    // light_count
                [transEncoder setBuffer:bvhNodesBuffer offset:0 atIndex:8];      // opaque BVH nodes
                [transEncoder setBuffer:bvhNodeCountBuffer offset:0 atIndex:9];  // bvh_node_count
                [transEncoder setBuffer:bvhTrianglesBuffer offset:0 atIndex:10]; // opaque BVH triangles
                [transEncoder setBuffer:bvhTriangleCountBuffer offset:0 atIndex:11]; // bvh_triangle_count

                // Bound the dispatch to the union screen bbox of the
                // transparent triangles. A full-screen dispatch made every
                // pixel loop all transparent triangles — measured ~15 ms
                // for 96 butterfly-wing triangles (2026-07 GPU audit).
                // Triangles are screen-space and the buffer is shared
                // storage, so the host can read them directly.
                const TriangleLit* trans_tris =
                    static_cast<const TriangleLit*>([transTrianglesBuffer contents]);
                float bb_min_x = 1e30f, bb_min_y = 1e30f;
                float bb_max_x = -1e30f, bb_max_y = -1e30f;
                for (uint32_t t = 0; t < trans_count; ++t) {
                    const TriangleLit& tri = trans_tris[t];
                    bb_min_x = std::min({bb_min_x, tri.x0, tri.x1, tri.x2});
                    bb_max_x = std::max({bb_max_x, tri.x0, tri.x1, tri.x2});
                    bb_min_y = std::min({bb_min_y, tri.y0, tri.y1, tri.y2});
                    bb_max_y = std::max({bb_max_y, tri.y0, tri.y1, tri.y2});
                }
                int origin[2] = {
                    std::max(0, (int)std::floor(bb_min_x) - 1),
                    std::max(0, (int)std::floor(bb_min_y) - 1)
                };
                int bb_w = std::min((int)width_,  (int)std::ceil(bb_max_x) + 2) - origin[0];
                int bb_h = std::min((int)height_, (int)std::ceil(bb_max_y) + 2) - origin[1];
                [transEncoder setBytes:origin length:sizeof(origin) atIndex:12];

                if (bb_w > 0 && bb_h > 0) {
                    [transEncoder dispatchThreads:MTLSizeMake(bb_w, bb_h, 1)
                            threadsPerThreadgroup:threadsPerThreadgroup];
                }
                [transEncoder endEncoding];

                static int pass35_log = 0;
                if (pass35_log < 3) {
                    std::cout << "[GPU_DISPATCH] Pass 3.5 (Transparency) dispatched: "
                              << trans_count << " transparent triangles, bbox "
                              << bb_w << "x" << bb_h << " at (" << origin[0]
                              << "," << origin[1] << ")" << std::endl;
                    pass35_log++;
                }
            }
        }

        // =========================================================================
        // PASS 4: VISION CONE POST-PROCESS (Optional)
        // =========================================================================
        // Darkens pixels outside the viewer's field of view
        // Only runs if vision_cone_enabled_ is true and pipeline exists
        static int pass4_check_log = 0;
        if (pass4_check_log < 5) {
            std::cout << "[GPU_DISPATCH] Pass 4 check: enabled=" << vision_cone_enabled_
                      << " pipeline=" << (compute_pipeline_vision_cone_ != nullptr ? "OK" : "NULL") << std::endl;
            pass4_check_log++;
        }
        if (vision_cone_enabled_ && compute_pipeline_vision_cone_ != nullptr) {
            id<MTLComputePipelineState> visionConePipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_vision_cone_;

            // Update vision cone params buffer with current values.
            // Layout MUST match VisionConeParams in gpu_types.metal
            // byte-for-byte (320 B); static_assert in the buffer-init
            // code locks it. Earlier this site had only the 48-byte
            // base struct — memcpy(48) into the 320-byte buffer left
            // occlusion_count zero, so the shader silently skipped
            // the LOS check no matter what the host pushed.
            struct VisionConeParamsGPU {
                float    viewer_x, viewer_y, look_direction, half_fov;
                float    range, inner_falloff, darkness;
                uint32_t enabled;
                float    focus_x, focus_y, focus_radius, padding;
                int32_t  occlusion_count;
                int32_t  occlusion_pad0;
                int32_t  occlusion_pad1;
                int32_t  occlusion_pad2;
                float    occlusion_distance[GPURasterizer::kVisionConeOcclusionBins];
                int32_t  memory_enabled;
                int32_t  memory_width;
                int32_t  memory_height;
                int32_t  memory_pad;
                float    memory_origin_x;
                float    memory_origin_y;
                float    memory_cell_size;
                float    memory_dim;
            };
            VisionConeParamsGPU params = {};
            params.viewer_x = vision_cone_viewer_x_;
            params.viewer_y = vision_cone_viewer_y_;
            params.look_direction = vision_cone_look_direction_;
            params.half_fov = vision_cone_half_fov_;
            params.range = vision_cone_range_;
            params.inner_falloff = vision_cone_inner_falloff_;
            params.darkness = vision_cone_darkness_;
            params.enabled = 1;
            params.focus_x = vision_cone_focus_x_;
            params.focus_y = vision_cone_focus_y_;
            params.focus_radius = vision_cone_focus_radius_;
            params.padding = 0;
            params.occlusion_count = vision_cone_occlusion_count_;
            if (params.occlusion_count > 0) {
                std::memcpy(params.occlusion_distance,
                            vision_cone_occlusion_distance_,
                            sizeof(float) * GPURasterizer::kVisionConeOcclusionBins);
            }
            params.memory_enabled   = vision_memory_enabled_ ? 1 : 0;
            params.memory_width     = vision_memory_width_;
            params.memory_height    = vision_memory_height_;
            params.memory_origin_x  = vision_memory_origin_x_;
            params.memory_origin_y  = vision_memory_origin_y_;
            params.memory_cell_size = vision_memory_cell_size_;
            params.memory_dim       = vision_memory_dim_;

            id<MTLBuffer> visionParamsBuffer = (__bridge id<MTLBuffer>)vision_cone_params_buffer_;
            memcpy([visionParamsBuffer contents], &params, sizeof(params));

            // Create encoder for Pass 4
            id<MTLComputeCommandEncoder> visionEncoder = [cmdPass3 computeCommandEncoder];
            [visionEncoder setComputePipelineState:visionConePipeline];

            // Set buffers
            // buffer(0): pixel_buffer (framebuffer) - same as Pass 3 output
            // buffer(1): gbuffer - same as Pass 1 output
            // buffer(2): vision cone params
            // buffer(3): width
            // buffer(4): height
            [visionEncoder setBuffer:framebufferBuffer offset:0 atIndex:0];
            [visionEncoder setBuffer:gbufferBuffer offset:0 atIndex:1];
            [visionEncoder setBuffer:visionParamsBuffer offset:0 atIndex:2];
            [visionEncoder setBuffer:(__bridge id<MTLBuffer>)width_buffer_ offset:0 atIndex:3];
            [visionEncoder setBuffer:(__bridge id<MTLBuffer>)height_buffer_ offset:0 atIndex:4];
            // Vision-memory grid update (decay + mark) +  upload.
            // Done HERE (inside the dispatch encoder, before commit)
            // so the GPU isn't already reading the buffer when we
            // memcpy into it. Doing this from the host frame loop
            // races with the previous frame's Pass-4 read and
            // produces flashing / GPU hangs.
            if (vision_memory_enabled_ && vision_memory_buffer_ &&
                !vision_memory_data_.empty()) {
                logosphere::rendering::VisionMemoryConfig vmcfg{};
                vmcfg.width         = vision_memory_width_;
                vmcfg.height        = vision_memory_height_;
                vmcfg.origin_x      = vision_memory_origin_x_;
                vmcfg.origin_y      = vision_memory_origin_y_;
                vmcfg.cell_size     = vision_memory_cell_size_;
                vmcfg.decay_seconds = vision_memory_decay_seconds_;
                logosphere::rendering::update_vision_memory(
                    vision_memory_data_.data(), vmcfg,
                    vision_cone_viewer_x_, vision_cone_viewer_y_,
                    vision_cone_look_direction_, vision_cone_half_fov_,
                    vision_cone_range_,
                    (vision_cone_occlusion_count_ > 0) ? vision_cone_occlusion_distance_ : nullptr,
                    vision_cone_occlusion_count_,
                    vision_memory_pending_dt_);
                vision_memory_pending_dt_ = 0.0f;
                id<MTLBuffer> vmBuf = (__bridge id<MTLBuffer>)vision_memory_buffer_;
                std::memcpy([vmBuf contents], vision_memory_data_.data(),
                            vision_memory_data_.size() * sizeof(float));
            }
            // Always bind a non-nil buffer at index 5 — the shader
            // gates on params.memory_enabled, but the slot must be
            // populated regardless (init created a stub 1-cell
            // buffer for the disabled case).
            [visionEncoder setBuffer:(__bridge id<MTLBuffer>)vision_memory_buffer_ offset:0 atIndex:5];

            // Dynamic-particle map at MTL bind index 6 + its size at
            // index 7. Same race-safe pattern: memcpy from CPU
            // mirror INSIDE the dispatch encoder so the GPU isn't
            // already reading the buffer.
            if (dynamic_particle_map_buffer_ &&
                dynamic_particle_map_size_ > 0 &&
                !dynamic_particle_map_data_.empty()) {
                id<MTLBuffer> dynBuf =
                    (__bridge id<MTLBuffer>)dynamic_particle_map_buffer_;
                std::memcpy([dynBuf contents],
                            dynamic_particle_map_data_.data(),
                            dynamic_particle_map_data_.size());
            }
            [visionEncoder setBuffer:(__bridge id<MTLBuffer>)dynamic_particle_map_buffer_
                              offset:0 atIndex:6];
            uint32_t dyn_map_size = dynamic_particle_map_size_;
            [visionEncoder setBytes:&dyn_map_size length:sizeof(uint32_t) atIndex:7];

            // Dispatch - same grid as Pass 3 (full framebuffer resolution)
            [visionEncoder dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
            [visionEncoder endEncoding];

            static int pass4_log = 0;
            if (pass4_log < 3) {
                std::cout << "[GPU_DISPATCH] Pass 4 (Vision Cone) dispatched: "
                          << threadsPerGrid.width << "x" << threadsPerGrid.height << " threads"
                          << " | viewer=(" << vision_cone_viewer_x_ << "," << vision_cone_viewer_y_ << ")"
                          << " | look=" << vision_cone_look_direction_ << " rad"
                          << std::endl;
                pass4_log++;
            }
        }

        // DEBUG: Log Pass 3 dispatch
        static int pass3_dispatch_log = 0;
        if (pass3_dispatch_log < 3) {
            std::cout << "[GPU_DISPATCH] Pass 3 (Apply Lighting) dispatched: " << threadsPerGrid.width << "x" << threadsPerGrid.height << " threads" << std::endl;
            pass3_dispatch_log++;
        }

        // Measure Pass 3 encoding time (sampling OR 9-11 lights)
        if (should_measure_all_encoding) {
            auto pass3_encode_end = std::chrono::high_resolution_clock::now();
            pass3_encode_ms = std::chrono::duration<double, std::milli>(pass3_encode_end - pass3_encode_start).count();

            // CPU STARVATION INVESTIGATION: Log Pass 3 encoding time for 9-11 lights
            if (light_count >= 9 && light_count <= 11) {
                static int pass3_encode_log = 0;
                pass3_encode_log++;
                if (pass3_encode_log % 60 == 1) {
                    std::cout << "[CPU_TIMING] Frame " << pass3_encode_log
                              << " | Lights: " << light_count
                              << " | Pass 3 encode: " << std::fixed << std::setprecision(2)
                              << pass3_encode_ms << "ms"
                              << std::endl;
                }
            }
        }

        [cmdPass3 addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            // UNCONDITIONAL: Verify handler fires
            static int pass3_handler_counter = 0;
            pass3_handler_counter++;
            // DEBUG: Changed % 60 to % 61 to avoid frame 60 trigger (investigating corruption)
            if (pass3_handler_counter % 61 == 0) {
                std::cout << "[PASS3_HANDLER] Called " << pass3_handler_counter << " times" << std::endl;
            }

            auto completion = std::chrono::high_resolution_clock::now();
            timing->pass3_gpu = std::chrono::duration<double, std::milli>(completion - pass3_commit).count();

            // The final lighting pass was declared in the stage enum but never
            // recorded, so every "GPU stage sum" before 2026-07-30 was missing it.
            if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                ::logosphere::telemetry::record_gpu_stage(
                    ::logosphere::telemetry::GpuStage::Pass3Apply,
                    (cb.GPUEndTime - cb.GPUStartTime) * 1000.0, tel_frame);
            }

            // GPU TIMESTAMP PROFILING: Measure Pass 3 (Apply Lighting) GPU time
            if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
                static uint64_t gpu_pass3_frame_counter = 0;
                gpu_pass3_frame_counter++;
                bool should_profile_pass3 = (gpu_pass3_frame_counter % Optimizations::GPU_PROFILE_SAMPLE_RATE == 1);

                if (should_profile_pass3 && cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                    double gpu_pass3_ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                    NSLog(@"[GPU_TIMESTAMP] Frame %llu | Pass 3 (Apply): %.2f ms",
                          gpu_pass3_frame_counter, gpu_pass3_ms);
                }
            }

            int completed = ++timing->completed_passes;
            if (pass3_handler_counter % 61 == 0) {
                std::cout << "[PASS3_HANDLER] completed=" << completed
                          << " total=" << timing->total_passes << std::endl;
            }

            if (completed == timing->total_passes) {
                // All passes complete - log aggregated timing
                static int log_counter = 0;
                log_counter++;
                bool should_log = (log_counter % 61 == 0) || (timing->wait_duration > 20.0);

                // UNCONDITIONAL: Check condition
                if (log_counter % 61 == 0) {
                    std::cout << "[PASS3_HANDLER] Frame=" << timing->frame_num
                              << " should_log=" << should_log
                              << " num_lights=" << timing->num_lights << std::endl;
                }

                if (should_log && timing->num_lights > 0) {
                    std::cout << "[PER_PASS_GPU] Frame=" << timing->frame_num
                              << " Lights=" << timing->num_lights
                              << " | Pass1=" << std::fixed << std::setprecision(1) << timing->pass1_gpu << "ms";

                    for (size_t i = 0; i < timing->shadow_gpu.size(); ++i) {
                        std::cout << " L" << i << "=" << timing->shadow_gpu[i] << "ms";
                    }

                    std::cout << " Pass3=" << timing->pass3_gpu << "ms"
                              << " Total=" << (timing->pass1_gpu + std::accumulate(timing->shadow_gpu.begin(), timing->shadow_gpu.end(), 0.0) + timing->pass3_gpu) << "ms"
                              << std::endl;

#if GPU_PHASE1_DEBUG
                    // PHASE1_DEBUG: Sample buffers to diagnose black screen
                    float max_lux = 0.0f;
                    int max_x = 0, max_y = 0;

                    if (timing->shadowResultsBuffer) {
                        const float* shadow_data = (const float*)[timing->shadowResultsBuffer contents];
                        int shadow_w = timing->shadow_width;
                        int shadow_h = timing->shadow_height;
                        std::cout << "[PHASE1_DEBUG] Shadow buffer: " << shadow_w << "x" << shadow_h << std::endl;

                        // Sample multiple pixels to find any non-zero lux
                        int samples[5][2] = {
                            {shadow_w/2, shadow_h/2},    // Center
                            {shadow_w/4, shadow_h/4},    // Top-left
                            {3*shadow_w/4, shadow_h/4},  // Top-right
                            {shadow_w/4, 3*shadow_h/4},  // Bottom-left
                            {3*shadow_w/4, 3*shadow_h/4} // Bottom-right
                        };

                        for (int i = 0; i < 5; i++) {
                            int sx = samples[i][0];
                            int sy = samples[i][1];
                            int idx = sy * shadow_w + sx;
                            float lux = shadow_data[idx];
                            if (lux > max_lux) {
                                max_lux = lux;
                                max_x = sx;
                                max_y = sy;
                            }
                        }

                        std::cout << "[PHASE1_DEBUG]   Max lux found: " << max_lux << " at (" << max_x << "," << max_y << ")" << std::endl;

                        // If all zeros, scan a larger area
                        if (max_lux < 0.001f) {
                            std::cout << "[PHASE1_DEBUG]   WARNING: All sampled pixels have ~0 lux! Scanning first 1000 pixels..." << std::endl;
                            for (int i = 0; i < std::min(1000, shadow_w * shadow_h); i++) {
                                if (shadow_data[i] > 0.001f) {
                                    std::cout << "[PHASE1_DEBUG]   Found non-zero at pixel " << i << ": " << shadow_data[i] << " lux" << std::endl;
                                    break;
                                }
                            }
                        }
                    }

                    // Sample G-buffer center pixel
                    uint8_t base_b = 0, base_g = 0, base_r = 0, base_a = 0;
                    if (timing->gbufferBuffer) {
                        const uint8_t* gbuffer_data = (const uint8_t*)[timing->gbufferBuffer contents];
                        const int GBUFFER_PIXEL_SIZE = 32;
                        const int BASE_COLOR_OFFSET = 24;
                        int center_x = timing->width / 2;
                        int center_y = timing->height / 2;
                        int center_idx = center_y * timing->width + center_x;
                        const uint8_t* color_ptr = gbuffer_data + (center_idx * GBUFFER_PIXEL_SIZE) + BASE_COLOR_OFFSET;
                        base_b = color_ptr[0];
                        base_g = color_ptr[1];
                        base_r = color_ptr[2];
                        base_a = color_ptr[3];
                        std::cout << "[PHASE1_DEBUG]   G-buffer base color = BGRA("
                                  << (int)base_b << "," << (int)base_g << ","
                                  << (int)base_r << "," << (int)base_a << ")" << std::endl;
                    }

                    // Sample final framebuffer center pixel
                    if (timing->framebufferBuffer) {
                        const uint32_t* fb_data = (const uint32_t*)[timing->framebufferBuffer contents];
                        int center_x = timing->width / 2;
                        int center_y = timing->height / 2;
                        int center_idx = center_y * timing->width + center_x;
                        uint32_t pixel_bgra = fb_data[center_idx];
                        uint8_t b = (pixel_bgra >> 0) & 0xFF;
                        uint8_t g = (pixel_bgra >> 8) & 0xFF;
                        uint8_t r = (pixel_bgra >> 16) & 0xFF;
                        uint8_t a = (pixel_bgra >> 24) & 0xFF;
                        std::cout << "[PHASE1_DEBUG]   Final framebuffer = BGRA("
                                  << (int)b << "," << (int)g << "," << (int)r << "," << (int)a << ")" << std::endl;

                        // Calculate expected tone mapping for max_lux
                        auto tone_map_cpu = [](float lux) -> int {
                            const float shadow_threshold = 10.0f;
                            const float midtone_threshold = 100.0f;
                            const float shadow_rgb_max = 75.0f;
                            const float midtone_rgb_max = 200.0f;

                            if (lux < 0.001f) return 0;
                            if (lux <= shadow_threshold) {
                                return (int)((lux / shadow_threshold) * shadow_rgb_max);
                            } else if (lux <= midtone_threshold) {
                                float ratio = (lux - shadow_threshold) / (midtone_threshold - shadow_threshold);
                                return (int)(shadow_rgb_max + ratio * (midtone_rgb_max - shadow_rgb_max));
                            } else {
                                float excess = lux - midtone_threshold;
                                float compressed = excess * 0.55f;
                                float highlight_range = 255.0f - midtone_rgb_max;
                                return std::min((int)(midtone_rgb_max + std::min(compressed, highlight_range)), 255);
                            }
                        };

                        int brightness_rgb = tone_map_cpu(max_lux);
                        float brightness_factor = brightness_rgb / 255.0f;

                        std::cout << "[PHASE1_DEBUG]   Tone mapping: " << max_lux << " lux → "
                                  << brightness_rgb << " RGB (" << (int)(brightness_factor * 100) << "%)" << std::endl;
                        std::cout << "[PHASE1_DEBUG]   Expected final = BGRA("
                                  << (int)(base_b * brightness_factor) << ","
                                  << (int)(base_g * brightness_factor) << ","
                                  << (int)(base_r * brightness_factor) << ","
                                  << (int)base_a << ")" << std::endl;
                        std::cout << "[PHASE1_DEBUG]   Actual vs Expected: R="
                                  << (int)r << " vs " << (int)(base_r * brightness_factor)
                                  << " | Diff=" << ((int)r - (int)(base_r * brightness_factor)) << std::endl;
                    }
#endif
                }
            }

            logCommandBufferError(cb, "Pass 3 (Apply Lighting, deferred)", device_lost_ptr);
            if (cb.status == MTLCommandBufferStatusError) {
                // Skip callback on error — buffer contents are undefined
            } else {
                if (timing->callback) {
                    uint32_t* fb = (uint32_t*)[timing->framebufferBuffer contents];
                    uint32_t* db = (uint32_t*)[timing->depthBuffer contents];
                    void* gb = [timing->gbufferBuffer contents];  // NEW: G-buffer
                    timing->callback(fb, db, gb, timing->width, timing->height, timing->user_data);
                }
            }

            // CRITICAL: Always signal semaphore, even on error. If we don't,
            // the semaphore permanently loses a count → deadlock after N frames.
            dispatch_semaphore_signal(timing->semaphore);
        }];
        [cmdPass3 commit];

        // Measure Pass 3 encoding time (only when sampling)
        if (should_profile_submit) {
            auto pass3_encode_end = std::chrono::high_resolution_clock::now();
            pass3_encode_ms = std::chrono::duration<double, std::milli>(
                pass3_encode_end - pass3_encode_start).count();
        }

        // LOG CPU ENCODING TIME (statistical sampling - minimal overhead)
        // Only log when we've sampled this frame
        if (should_profile_submit) {
            total_encode_ms = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - encode_start).count();

            // Log the breakdown
            std::cout << "[GPU_SUBMIT_PROFILE] Frame " << frame_counter
                      << " | Total encode: " << std::fixed << std::setprecision(2) << total_encode_ms << "ms"
                      << " | Pass1: " << pass1_encode_ms << "ms"
                      << " | " << shadow_encode_ms.size() << " shadows: "
                      << std::accumulate(shadow_encode_ms.begin(), shadow_encode_ms.end(), 0.0) << "ms"
                      << " | Pass3: " << pass3_encode_ms << "ms"
                      << " | Per-buffer avg: " << (total_encode_ms / (2 + shadow_encode_ms.size())) << "ms"
                      << std::endl;

            // Log warning if encoding is taking too long
            if (total_encode_ms > 30.0) {
                std::cout << "[GPU_SUBMIT_PROFILE] WARNING: Encoding overhead "
                          << total_encode_ms << "ms exceeds 30ms threshold!" << std::endl;
            }
        }

    }
}

// Upload transparent triangle data to the given triple-buffer slot.
// Called from render_pipeline before rasterize_triangles_deferred_async().
void GPURasterizer::set_triangle_bboxes(const int32_t* bboxes, uint32_t triangle_count) {
    if constexpr (!Optimizations::RASTER_BBOX_STREAM) {
        return;
    }
    int idx = current_buffer_index_;
    if (!bboxes || triangle_count == 0) {
        tri_bbox_count_[idx] = 0;
        return;
    }
    @autoreleasepool {
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        size_t dataSize = (size_t)triangle_count * 4 * sizeof(int32_t);
        if (!tri_bbox_buffer_[idx] || tri_bbox_capacity_[idx] < dataSize) {
            if (tri_bbox_buffer_[idx]) CFBridgingRelease(tri_bbox_buffer_[idx]);
            id<MTLBuffer> buf = [device newBufferWithLength:dataSize
                                                    options:MTLResourceStorageModeShared];
            if (!buf) { tri_bbox_count_[idx] = 0; return; }
            tri_bbox_buffer_[idx] = (__bridge_retained void*)buf;
            tri_bbox_capacity_[idx] = dataSize;
        }
        id<MTLBuffer> buf = (__bridge id<MTLBuffer>)tri_bbox_buffer_[idx];
        memcpy([buf contents], bboxes, dataSize);
        tri_bbox_count_[idx] = triangle_count;
    }
}

void GPURasterizer::set_transparent_triangles(
    const TriangleLit* triangles,
    uint32_t triangle_count)
{
    if constexpr (!Optimizations::USE_TRANSPARENCY) {
        return;  // Compile-time dead code when transparency disabled
    }

    // Use current_buffer_index_ — must be called BEFORE rasterize_triangles_deferred_async()
    // which will consume this same slot and then increment.
    int idx = current_buffer_index_;

    if (!triangles || triangle_count == 0) {
        transparent_triangle_count_async_[idx] = 0;
        return;
    }

    @autoreleasepool {
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        size_t dataSize = triangle_count * sizeof(TriangleLit);

        if (!transparent_triangles_buffer_async_[idx] ||
            transparent_triangles_capacity_async_[idx] < dataSize) {
            if (transparent_triangles_buffer_async_[idx]) {
                CFBridgingRelease(transparent_triangles_buffer_async_[idx]);
            }
            id<MTLBuffer> buf = [device newBufferWithLength:dataSize
                                                    options:MTLResourceStorageModeShared];
            if (!buf) {
                std::cerr << "[GPU_RASTERIZER] Failed to create transparent triangles buffer" << std::endl;
                transparent_triangle_count_async_[idx] = 0;
                return;
            }
            transparent_triangles_buffer_async_[idx] = (__bridge_retained void*)buf;
            transparent_triangles_capacity_async_[idx] = dataSize;
        }

        id<MTLBuffer> buf = (__bridge id<MTLBuffer>)transparent_triangles_buffer_async_[idx];
        memcpy([buf contents], triangles, dataSize);
        transparent_triangle_count_async_[idx] = triangle_count;
    }
}

// Wait for all pending GPU work to complete (for safe shutdown)
void GPURasterizer::wait_for_completion() {
    // Checkpoint logging is verbose-gated: this runs on every deletion flush
    // and on every serialized oracle frame; unconditional stdout in that path
    // is measurable cost and pure noise in the logs.
    if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
        std::cout << "[CHECKPOINT 040] GPU_RASTERIZER: wait_for_completion called" << std::endl;
    }

    if (!buffer_semaphore_) {
        return;  // No semaphore, nothing to wait for
    }

    @autoreleasepool {
        if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
            std::cout << "[GPU_RASTERIZER] Waiting for "
                      << Optimizations::GPU_BUFFER_SLOTS << " semaphore slots..." << std::endl;
        }

        dispatch_semaphore_t semaphore = (__bridge dispatch_semaphore_t)buffer_semaphore_;

        // Use timeout instead of DISPATCH_TIME_FOREVER to prevent system freeze
        // 1.0s per slot keeps worst-case total (GPU_BUFFER_SLOTS × 1.0s = 3s)
        // under WindowServer's 3-6s watchdog threshold.
        constexpr int64_t TIMEOUT_NS = 1000LL * 1000000LL;  // 1.0 second in nanoseconds
        int slots_acquired = 0;

        // Acquire all GPU_BUFFER_SLOTS semaphore slots - this ensures all GPU work has completed
        // (When all slots are available, no buffers are in-flight)
        for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
            if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
                std::cout << "[CHECKPOINT 043] GPU_RASTERIZER: Waiting for slot " << i << "..." << std::endl;
            }

            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, TIMEOUT_NS);
            long result = dispatch_semaphore_wait(semaphore, timeout);

            if (result != 0) {
                std::cerr << "[GPU_RASTERIZER ERROR] Timeout waiting for semaphore slot " << i
                          << " - GPU may be stuck! Aborting wait to prevent system freeze." << std::endl;
                // Release slots we already acquired
                for (int j = 0; j < slots_acquired; j++) {
                    dispatch_semaphore_signal(semaphore);
                }
                return;  // Bail out to prevent kernel panic
            }

            slots_acquired++;
            if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
                std::cout << "[CHECKPOINT 044] GPU_RASTERIZER: Slot " << i << " acquired" << std::endl;
            }
        }

        if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
            std::cout << "[CHECKPOINT 045] GPU_RASTERIZER: All slots acquired" << std::endl;
        }

        // NO drawable-pool sleep here (removed 2026-07-29, live-stall RCA).
        // A hardcoded usleep(50000) used to sit at this point, guarding
        // CAMetalLayer.drawableSize changes against in-flight drawables. No
        // caller of this function changes the drawable: the resolution path
        // holds acquire_all_slots() and calls PlatformMacOS::
        // force_drawable_resize(), which owns its own stabilization wait.
        // The sleep only taxed the OTHER callers — deletion flush (every
        // chunk unload: 61-83 ms hitches), shutdown, scene reset, and every
        // serialized headless oracle frame. Anything that mutates the
        // drawable must take the acquire_all_slots() path, not this one.
        // Guarded by test_gpu_wait_no_fixed_sleep.

        // Now safe to release semaphores
        for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
            dispatch_semaphore_signal(semaphore);
        }

        if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
            std::cout << "[CHECKPOINT 046] GPU_RASTERIZER: All GPU work completed and slots released" << std::endl;
        }
    }
}

// Reset all temporal GPU state for scene transitions.
// Clears persistent temporal buffers AND per-slot GI/shadow buffers to prevent
// stale data from bleeding between scenes (checkerboard temporal distribution
// only updates half the pixels per frame, so un-processed pixels retain old data).
// PRECONDITION: wait_for_completion() must have been called first.
void GPURasterizer::reset_temporal_state() {
    std::cout << "[GPU_RASTERIZER] reset_temporal_state: clearing ALL GPU buffers for scene change" << std::endl;

    @autoreleasepool {
        // 1. Release persistent temporal buffers (re-allocated and zeroed on next frame)
        if (temporal_lighting_buffer_) {
            CFBridgingRelease(temporal_lighting_buffer_);
            temporal_lighting_buffer_ = nullptr;
            temporal_lighting_capacity_ = 0;
        }
        if (sample_count_buffer_) {
            CFBridgingRelease(sample_count_buffer_);
            sample_count_buffer_ = nullptr;
            sample_count_capacity_ = 0;
        }
        if (prev_particle_id_buffer_) {
            CFBridgingRelease(prev_particle_id_buffer_);
            prev_particle_id_buffer_ = nullptr;
            prev_particle_id_capacity_ = 0;
        }

        // 2. Zero per-slot shadow buffers (checkerboard temporal distribution
        //    only updates a subset of pixels each frame — un-processed pixels retain
        //    stale data from previous scenes, causing colored artifact bleeding)
        for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
            if (shadow_denoised_buffer_async_[i] && shadow_denoised_capacity_async_[i] > 0) {
                id<MTLBuffer> buf = (__bridge id<MTLBuffer>)shadow_denoised_buffer_async_[i];
                memset([buf contents], 0, shadow_denoised_capacity_async_[i]);
            }
            if (light_color_buffer_async_[i] && light_color_capacity_async_[i] > 0) {
                id<MTLBuffer> buf = (__bridge id<MTLBuffer>)light_color_buffer_async_[i];
                memset([buf contents], 0, light_color_capacity_async_[i]);
            }
        }

        // 3. Force acceleration structure rebuild
        accel_triangle_count_ = 0;
    }

    std::cout << "[GPU_RASTERIZER] reset_temporal_state: complete" << std::endl;
}

// Acquire all semaphore slots (for holding GPU idle during resolution change)
// Returns number of slots acquired, 0 on failure
int GPURasterizer::acquire_all_slots() {
    if (!buffer_semaphore_) {
        return 0;
    }

    dispatch_semaphore_t semaphore = (__bridge dispatch_semaphore_t)buffer_semaphore_;
    // 1.5s per slot — worst case 3 × 1.5s = 4.5s, borderline but with bail-out
    constexpr int64_t TIMEOUT_NS = 1500LL * 1000000LL;  // 1.5 seconds
    int slots_acquired = 0;

    for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, TIMEOUT_NS);
        long result = dispatch_semaphore_wait(semaphore, timeout);

        if (result != 0) {
            std::cerr << "[GPU_RASTERIZER] acquire_all_slots: timeout on slot " << i << std::endl;
            // Release what we acquired
            for (int j = 0; j < slots_acquired; j++) {
                dispatch_semaphore_signal(semaphore);
            }
            return 0;
        }
        slots_acquired++;
    }

    std::cout << "[GPU_RASTERIZER] acquire_all_slots: acquired " << slots_acquired << " slots" << std::endl;
    return slots_acquired;
}

// Release previously acquired slots
void GPURasterizer::release_all_slots(int slots_to_release) {
    if (!buffer_semaphore_ || slots_to_release <= 0) {
        return;
    }

    dispatch_semaphore_t semaphore = (__bridge dispatch_semaphore_t)buffer_semaphore_;
    for (int i = 0; i < slots_to_release; i++) {
        dispatch_semaphore_signal(semaphore);
    }
    std::cout << "[GPU_RASTERIZER] release_all_slots: released " << slots_to_release << " slots" << std::endl;
}

// =========================================================================
// VISION CONE POST-PROCESS API
// =========================================================================

void GPURasterizer::set_vision_cone_enabled(bool enabled) {
    vision_cone_enabled_ = enabled;
    if (enabled) {
        static bool first_enable = true;
        if (first_enable) {
            std::cout << "[GPU_RASTERIZER] Vision cone enabled" << std::endl;
            first_enable = false;
        }
    }
}

void GPURasterizer::set_vision_cone(float viewer_x, float viewer_y, float look_direction,
                                     float fov_radians, float range) {
    vision_cone_viewer_x_ = viewer_x;
    vision_cone_viewer_y_ = viewer_y;
    vision_cone_look_direction_ = look_direction;
    vision_cone_half_fov_ = fov_radians / 2.0f;
    vision_cone_range_ = range;
}

void GPURasterizer::set_vision_cone_style(float inner_falloff, float darkness) {
    vision_cone_inner_falloff_ = inner_falloff;
    vision_cone_darkness_ = darkness;
}

void GPURasterizer::set_vision_cone_focus(float focus_x, float focus_y, float focus_radius) {
    vision_cone_focus_x_ = focus_x;
    vision_cone_focus_y_ = focus_y;
    vision_cone_focus_radius_ = focus_radius;
}

void GPURasterizer::set_vision_cone_occlusion(const float* distances, int count) {
    // Strict count check — the shader assumes a fixed bin count and
    // doing the math with a wrong count silently mis-bins. Reject and
    // disable rather than accept ambiguous input.
    if (!distances || count != kVisionConeOcclusionBins) {
        vision_cone_occlusion_count_ = 0;
        return;
    }
    vision_cone_occlusion_count_ = kVisionConeOcclusionBins;
    std::memcpy(vision_cone_occlusion_distance_, distances,
                sizeof(float) * kVisionConeOcclusionBins);
}

void GPURasterizer::clear_vision_cone_occlusion() {
    vision_cone_occlusion_count_ = 0;
    // Distances stay as-is; shader ignores them when count == 0.
}

void GPURasterizer::set_vision_memory_enabled(bool enabled) {
    vision_memory_enabled_ = enabled;
}

void GPURasterizer::set_vision_memory_extent(float min_x, float min_y,
                                             float max_x, float max_y,
                                             int cells_per_side) {
    if (cells_per_side <= 0) return;
    if (max_x <= min_x || max_y <= min_y) return;

    // Buffer is fixed-size at init (256 × 256 max). Clamp if the
    // game asks for more so we never overflow it.
    const int kMaxCells = 256;
    if (cells_per_side > kMaxCells) {
        std::cerr << "[VISION_MEMORY] requested " << cells_per_side
                  << " cells/side; clamped to " << kMaxCells << std::endl;
        cells_per_side = kMaxCells;
    }

    vision_memory_width_     = cells_per_side;
    vision_memory_height_    = cells_per_side;
    vision_memory_origin_x_  = min_x;
    vision_memory_origin_y_  = min_y;
    vision_memory_cell_size_ = (max_x - min_x) / static_cast<float>(cells_per_side);

    const size_t needed = static_cast<size_t>(vision_memory_width_)
                        * static_cast<size_t>(vision_memory_height_);
    vision_memory_data_.assign(needed, 0.0f);

    // Wipe the GPU buffer's first `needed` floats. The buffer was
    // pre-allocated at max size in init, so no realloc.
    if (vision_memory_buffer_) {
        id<MTLBuffer> buf = (__bridge id<MTLBuffer>)vision_memory_buffer_;
        std::memset([buf contents], 0, needed * sizeof(float));
    }
}

void GPURasterizer::set_vision_memory_decay(float decay_seconds, float memory_dim) {
    if (decay_seconds < 0.001f) decay_seconds = 0.001f;
    if (memory_dim < 0.0f) memory_dim = 0.0f;
    if (memory_dim > 1.0f) memory_dim = 1.0f;
    vision_memory_decay_seconds_ = decay_seconds;
    vision_memory_dim_           = memory_dim;
}

void GPURasterizer::update_vision_memory(float dt) {
    // Just record dt — the actual decay-mark-memcpy happens inside
    // the Pass-4 dispatch (see flush_vision_memory_to_gpu_), where
    // the encoder hasn't been committed yet so writing the GPU
    // buffer is safe. Doing it from the host side directly races
    // with the previous frame's GPU read — manifests as flashing /
    // GPU command-buffer hangs.
    vision_memory_pending_dt_ += dt;
}

bool GPURasterizer::read_latest_framebuffer(uint32_t* out_pixels,
                                             int& out_width, int& out_height) {
    if (!out_pixels) return false;
    // Drain all in-flight GPU work. After this returns, every
    // command buffer we submitted has either completed or been
    // aborted, so the framebuffer slots hold final pixels.
    wait_for_completion();

    // current_buffer_index_ is the NEXT slot to write into. The most
    // recently WRITTEN slot is therefore (current - 1) modulo the
    // pool size; that slot was the target of the last dispatch.
    constexpr int N = Optimizations::GPU_BUFFER_SLOTS;
    int slot = (current_buffer_index_ + N - 1) % N;
    if (!framebuffer_buffer_async_[slot]) return false;
    if (width_ == 0 || height_ == 0) return false;

    id<MTLBuffer> fb = (__bridge id<MTLBuffer>)framebuffer_buffer_async_[slot];
    const void* contents = [fb contents];
    if (!contents) return false;

    const size_t pixel_count = static_cast<size_t>(width_) * static_cast<size_t>(height_);
    std::memcpy(out_pixels, contents, pixel_count * sizeof(uint32_t));
    out_width  = static_cast<int>(width_);
    out_height = static_cast<int>(height_);
    return true;
}

void GPURasterizer::set_dynamic_particle_map(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        dynamic_particle_map_size_ = 0;
        return;
    }
    dynamic_particle_map_data_.assign(data, data + size);
    dynamic_particle_map_size_ = static_cast<uint32_t>(size);
    // Grow GPU buffer if needed. Same persistent-buffer pattern as
    // vision_memory: alloc once, retain, grow on demand. Memcpy from
    // dynamic_particle_map_data_ happens inside the Pass-4 dispatch
    // (race-safe, mirrors the vision_memory path).
    if (size > dynamic_particle_map_capacity_) {
        if (dynamic_particle_map_buffer_) {
            id<MTLBuffer> old = (__bridge_transfer id<MTLBuffer>)dynamic_particle_map_buffer_;
            (void)old;
            dynamic_particle_map_buffer_ = nullptr;
        }
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        // Round capacity up to keep churn down.
        size_t cap = 1024;
        while (cap < size) cap *= 2;
        id<MTLBuffer> buf = [device newBufferWithLength:cap
                                                options:MTLResourceStorageModeShared];
        if (buf) {
            std::memset([buf contents], 0, cap);
            [buf retain];
            dynamic_particle_map_buffer_ = (__bridge_retained void*)buf;
            dynamic_particle_map_capacity_ = cap;
        }
    }
}

void GPURasterizer::set_shadow_culling_camera(float camera_x, float camera_y) {
    shadow_culling_camera_x_ = camera_x;
    shadow_culling_camera_y_ = camera_y;
}

void GPURasterizer::set_shadow_projection_params(float pixels_per_unit) {
    shadow_pixels_per_unit_ = pixels_per_unit;
}

bool GPURasterizer::read_shadow_debug(int screen_x, int screen_y,
                                       uint32_t& out_sample_count,
                                       float& out_temporal_lux,
                                       uint32_t& out_prev_particle_id) const {
    if (!sample_count_buffer_ || !temporal_lighting_buffer_ || !prev_particle_id_buffer_)
        return false;
    if (shadow_width_ == 0 || shadow_height_ == 0) return false;
    // Map screen coords to shadow buffer coords (1:1 when SHADOW_RESOLUTION_SCALE=1.0)
    int sx = (int)((float)screen_x * shadow_width_ / width_);
    int sy = (int)((float)screen_y * shadow_height_ / height_);
    if (sx < 0 || sx >= (int)shadow_width_ || sy < 0 || sy >= (int)shadow_height_)
        return false;
    uint32_t idx = sy * shadow_width_ + sx;
    id<MTLBuffer> scBuf = (__bridge id<MTLBuffer>)sample_count_buffer_;
    id<MTLBuffer> tlBuf = (__bridge id<MTLBuffer>)temporal_lighting_buffer_;
    id<MTLBuffer> ppBuf = (__bridge id<MTLBuffer>)prev_particle_id_buffer_;
    out_sample_count = ((const uint32_t*)[scBuf contents])[idx];
    out_temporal_lux = ((const float*)[tlBuf contents])[idx];
    out_prev_particle_id = ((const uint32_t*)[ppBuf contents])[idx];
    return true;
}

bool GPURasterizer::read_gi_debug(int screen_x, int screen_y,
                                   float& out_gi_r, float& out_gi_g, float& out_gi_b,
                                   float& out_shadow_lux) const {
    out_gi_r = out_gi_g = out_gi_b = 0.0f;
    out_shadow_lux = 0.0f;
    if (width_ == 0 || height_ == 0) return false;
    if (screen_x < 0 || screen_x >= (int)width_ || screen_y < 0 || screen_y >= (int)height_)
        return false;
    // GI channels stay 0: the screen-space GI buffer they reported was
    // retired 2026-07-29 (replaced by SSDO + DDGI). Shadow lux below is
    // still live, which is what this probe is used for in practice.
    // Read shadow_results from the last-used async slot (what Pass 3 read before denoise)
    // Map to shadow coordinates
    int sx = (int)((float)screen_x * shadow_width_ / width_);
    int sy = (int)((float)screen_y * shadow_height_ / height_);
    if (sx >= 0 && sx < (int)shadow_width_ && sy >= 0 && sy < (int)shadow_height_) {
        uint32_t shadow_idx = sy * shadow_width_ + sx;
        // Try denoised buffer first (what Pass 3 actually reads)
        int last_slot = (current_buffer_index_ + Optimizations::GPU_BUFFER_SLOTS - 1) % Optimizations::GPU_BUFFER_SLOTS;
        if (shadow_denoised_buffer_async_[last_slot]) {
            id<MTLBuffer> buf = (__bridge id<MTLBuffer>)shadow_denoised_buffer_async_[last_slot];
            out_shadow_lux = ((const float*)[buf contents])[shadow_idx];
        } else if (shadow_results_buffer_async_[last_slot]) {
            id<MTLBuffer> buf = (__bridge id<MTLBuffer>)shadow_results_buffer_async_[last_slot];
            out_shadow_lux = ((const float*)[buf contents])[shadow_idx];
        }
    }
    return true;
}

bool GPURasterizer::read_ssdo_debug(int screen_x, int screen_y,
                                    float& out_r, float& out_g, float& out_b,
                                    float& out_ao) const {
    out_r = out_g = out_b = 0.0f;
    out_ao = 1.0f;
    if (width_ == 0 || height_ == 0) return false;
    if (screen_x < 0 || screen_x >= (int)width_ || screen_y < 0 || screen_y >= (int)height_)
        return false;
    int last_slot = (current_buffer_index_ + Optimizations::GPU_BUFFER_SLOTS - 1) % Optimizations::GPU_BUFFER_SLOTS;
    void* raw = ssao_denoised_buffer_async_[last_slot]
              ? ssao_denoised_buffer_async_[last_slot]
              : ssao_results_buffer_async_[last_slot];
    if (!raw) return false;
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)raw;
    uint32_t idx = (uint32_t)(screen_y * width_ + screen_x) * 4;
    if constexpr (Optimizations::SSDO_HALF_PRECISION) {
        const __fp16* d = (const __fp16*)[buf contents];
        constexpr float unscale = 1.0f / 1024.0f;  // ssdo_pack range shift
        out_r  = (float)d[idx + 0] * unscale;
        out_g  = (float)d[idx + 1] * unscale;
        out_b  = (float)d[idx + 2] * unscale;
        out_ao = (float)d[idx + 3] * unscale;
    } else {
        const float* d = (const float*)[buf contents];
        out_r = d[idx + 0]; out_g = d[idx + 1]; out_b = d[idx + 2];
        out_ao = d[idx + 3];
    }
    return true;
}

bool GPURasterizer::read_gbuffer_debug(int screen_x, int screen_y,
                                        uint8_t& out_r, uint8_t& out_g, uint8_t& out_b,
                                        uint32_t& out_particle_id) const {
    out_r = out_g = out_b = 0;
    out_particle_id = UINT32_MAX;
    if (width_ == 0 || height_ == 0) return false;
    if (screen_x < 0 || screen_x >= (int)width_ || screen_y < 0 || screen_y >= (int)height_)
        return false;
    // Read from most-recently-used gbuffer slot
    int last_slot = (current_buffer_index_ + Optimizations::GPU_BUFFER_SLOTS - 1) % Optimizations::GPU_BUFFER_SLOTS;
    if (!gbuffer_buffer_async_[last_slot]) return false;
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)gbuffer_buffer_async_[last_slot];
    // GBufferPixel = 32 bytes: float3 world_pos(12), float3 normal(12), uchar4 base_color(4), uint particle_id(4)
    const uint8_t* data = (const uint8_t*)[buf contents];
    uint32_t pixel_idx = screen_y * width_ + screen_x;
    const uint8_t* pixel = data + pixel_idx * 32;
    // base_color at offset 24 (12+12), BGRA format
    out_b = pixel[24];
    out_g = pixel[25];
    out_r = pixel[26];
    // particle_id at offset 28 (24+4)
    memcpy(&out_particle_id, pixel + 28, sizeof(uint32_t));
    return true;
}

bool GPURasterizer::read_gpu_framebuffer(int screen_x, int screen_y,
                                          uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) const {
    out_r = out_g = out_b = 0;
    if (width_ == 0 || height_ == 0) return false;
    if (screen_x < 0 || screen_x >= (int)width_ || screen_y < 0 || screen_y >= (int)height_)
        return false;
    int last_slot = (current_buffer_index_ + Optimizations::GPU_BUFFER_SLOTS - 1) % Optimizations::GPU_BUFFER_SLOTS;
    if (!framebuffer_buffer_async_[last_slot]) return false;
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)framebuffer_buffer_async_[last_slot];
    uint32_t pixel_idx = screen_y * width_ + screen_x;
    uint32_t pixel_val = ((const uint32_t*)[buf contents])[pixel_idx];
    out_b = pixel_val & 0xFF;
    out_g = (pixel_val >> 8) & 0xFF;
    out_r = (pixel_val >> 16) & 0xFF;
    return true;
}

// =============================================================================
// METAL RT: BUILD ACCELERATION STRUCTURE (Phase 1.2)
// =============================================================================
// Build hardware-accelerated acceleration structure from shadow triangles
// Replaces software BVH with Apple Silicon ray tracing hardware
//
// INPUT:
//   triangles: Array of ShadowTriangle (v0, v1, v2 per triangle, 48 bytes each)
//   triangle_count: Number of triangles
//
// RETURNS: true on success, false on failure
//
// NOTE: Requires M3+ chip (supports_raytracing_ must be true)

bool GPURasterizer::build_acceleration_structure(const void* triangles, uint32_t triangle_count) {
    // Check if RT is available
    if (!supports_raytracing_) {
        std::cerr << "[GPU_RASTERIZER] RT not available - cannot build acceleration structure" << std::endl;
        return false;
    }

    // Check for valid input
    if (!triangles || triangle_count == 0) {
        std::cerr << "[GPU_RASTERIZER] Invalid triangle data for acceleration structure" << std::endl;
        return false;
    }

    // Get Metal device
    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLCommandQueue> commandQueue = (__bridge id<MTLCommandQueue>)command_queue_;

    // Check if device supports acceleration structures
    if (@available(macOS 14.0, *)) {
        // === STEP 1: Convert ShadowTriangle array to vertex buffer ===
        // ShadowTriangle format: { v0[3], pad, v1[3], pad, v2[3], pad, color, pad3 } = 64 bytes
        // We need to extract just the vertex positions into a packed buffer

        // Reuse vertex buffer if capacity sufficient, else reallocate
        size_t vertex_buffer_size = triangle_count * 3 * sizeof(float) * 3;
        id<MTLBuffer> vertexBuffer = nil;
        if (accel_vertex_buffer_ && accel_vertex_capacity_ >= vertex_buffer_size) {
            vertexBuffer = (__bridge id<MTLBuffer>)accel_vertex_buffer_;
        } else {
            vertexBuffer = [device newBufferWithLength:vertex_buffer_size
                                              options:MTLResourceStorageModeShared];
            if (!vertexBuffer) {
                std::cerr << "[GPU_RASTERIZER] Failed to create vertex buffer for AS" << std::endl;
                return false;
            }
            if (accel_vertex_buffer_) CFBridgingRelease(accel_vertex_buffer_);
            accel_vertex_buffer_ = (__bridge_retained void*)vertexBuffer;
            accel_vertex_capacity_ = vertex_buffer_size;
        }

        // Copy vertex data from ShadowTriangle array to packed vertex buffer
        float* vertices = (float*)[vertexBuffer contents];
        const uint8_t* src = (const uint8_t*)triangles;

        for (uint32_t i = 0; i < triangle_count; i++) {
            // ShadowTriangle layout: v0[3], pad(4), v1[3], pad(4), v2[3], pad(4), color(4), pad(12) = 64 bytes
            const float* v0 = (const float*)(src + i * 64 + 0);   // offset 0
            const float* v1 = (const float*)(src + i * 64 + 16);  // offset 16 (after v0 + pad)
            const float* v2 = (const float*)(src + i * 64 + 32);  // offset 32 (after v1 + pad)

            // Write to packed vertex buffer (9 floats per triangle)
            size_t base = i * 9;
            vertices[base + 0] = v0[0]; vertices[base + 1] = v0[1]; vertices[base + 2] = v0[2];
            vertices[base + 3] = v1[0]; vertices[base + 4] = v1[1]; vertices[base + 5] = v1[2];
            vertices[base + 6] = v2[0]; vertices[base + 7] = v2[1]; vertices[base + 8] = v2[2];
        }

        // === STEP 2: Create geometry descriptor ===
        MTLAccelerationStructureTriangleGeometryDescriptor* geometryDesc =
            [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
        geometryDesc.vertexBuffer = vertexBuffer;
        geometryDesc.vertexBufferOffset = 0;
        geometryDesc.vertexStride = sizeof(float) * 3;  // Packed float3
        geometryDesc.triangleCount = triangle_count;

        // === STEP 3: Create primitive acceleration structure descriptor ===
        MTLPrimitiveAccelerationStructureDescriptor* accelDesc =
            [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        accelDesc.geometryDescriptors = @[geometryDesc];
        if constexpr (Optimizations::USE_RT_AS_REFIT) {
            accelDesc.usage = MTLAccelerationStructureUsageRefit;
        }

        // Item C: refit instead of rebuild when eligible — same triangle
        // count (topology unchanged; the vertex buffer above already
        // carries this frame's positions), an existing refit-capable AS,
        // and the periodic full rebuild not due. Refit cost is a
        // fraction of a full build; hit results are identical.
        bool do_refit = false;
        if constexpr (Optimizations::USE_RT_AS_REFIT) {
            accel_frames_since_full_build_++;
            do_refit = acceleration_structure_ != nullptr &&
                       accel_supports_refit_ &&
                       triangle_count == accel_triangle_count_ &&
                       accel_frames_since_full_build_ < Optimizations::AS_FULL_REBUILD_INTERVAL;
        }

        // === STEP 4: Get size requirements ===
        MTLAccelerationStructureSizes sizes = [device accelerationStructureSizesWithDescriptor:accelDesc];

        // === STEP 5: Reuse or allocate acceleration structure ===
        // CRITICAL: Reuse the existing accel struct when size is sufficient.
        // Allocating a new one every frame leaked ~500KB/rebuild * 745 rebuilds = 384MB.
        id<MTLAccelerationStructure> accelStruct = nil;
        if (acceleration_structure_ && accel_struct_capacity_ >= sizes.accelerationStructureSize) {
            accelStruct = (__bridge id<MTLAccelerationStructure>)acceleration_structure_;
        } else {
            accelStruct = [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
            if (!accelStruct) {
                std::cerr << "[GPU_RASTERIZER] Failed to create acceleration structure" << std::endl;
                return false;
            }
            if (acceleration_structure_) CFRelease(acceleration_structure_);
            acceleration_structure_ = (__bridge_retained void*)accelStruct;
            accel_struct_capacity_ = sizes.accelerationStructureSize;
            // A freshly allocated AS is empty — it cannot be refitted.
            do_refit = false;
            accel_supports_refit_ = false;
        }

        // === STEP 6: Reuse or allocate scratch buffer ===
        if (sizes.buildScratchBufferSize > accel_scratch_capacity_) {
            id<MTLBuffer> scratchBuffer = [device newBufferWithLength:sizes.buildScratchBufferSize
                                                              options:MTLResourceStorageModePrivate];
            if (!scratchBuffer) {
                std::cerr << "[GPU_RASTERIZER] Failed to create scratch buffer for AS build" << std::endl;
                return false;
            }
            if (accel_scratch_buffer_) CFBridgingRelease(accel_scratch_buffer_);
            accel_scratch_buffer_ = (__bridge_retained void*)scratchBuffer;
            accel_scratch_capacity_ = sizes.buildScratchBufferSize;
        }
        id<MTLBuffer> scratchBuffer = (__bridge id<MTLBuffer>)accel_scratch_buffer_;

        // === STEP 7: Build or refit the acceleration structure ===
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        id<MTLAccelerationStructureCommandEncoder> encoder =
            [commandBuffer accelerationStructureCommandEncoder];

        if (do_refit) {
            [encoder refitAccelerationStructure:accelStruct
                                     descriptor:accelDesc
                                    destination:accelStruct
                                  scratchBuffer:scratchBuffer
                            scratchBufferOffset:0];
        } else {
            [encoder buildAccelerationStructure:accelStruct
                                     descriptor:accelDesc
                                  scratchBuffer:scratchBuffer
                            scratchBufferOffset:0];
            if constexpr (Optimizations::USE_RT_AS_REFIT) {
                accel_supports_refit_ = true;   // built with Refit usage
                accel_frames_since_full_build_ = 0;
            }
        }
        [encoder endEncoding];

        // Item C measurement: attribute the per-frame AS build/refit cost
        // (same sampling cadence as the pass timestamps).
        if constexpr (Optimizations::ENABLE_GPU_TIMESTAMP_PROFILING) {
            static int as_ts_pc = 0;
            if (as_ts_pc++ % Optimizations::GPU_PROFILE_SAMPLE_RATE == 0) {
                const bool was_refit = do_refit;
                [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
                    if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
                        NSLog(@"[GPU_TIMESTAMP] AS %s: %.2f ms",
                              was_refit ? "refit" : "build",
                              (cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
                    }
                }];
            }
        }

        [commandBuffer commit];
        // NOTE: no waitUntilCompleted. Both the AS build and subsequent
        // shadow-ray dispatches go on the same command_queue_, so Metal
        // sequences them — the shadow kernel can't run until this build
        // completes. Blocking the CPU here was 20 ms/frame (~30 FPS
        // cap) once we started rebuilding every frame for rotating
        // geometry. Dropping the block takes the game loop from 31 to
        // ~80 FPS on an M4 Max with the bike_viewer scene.
        accel_triangle_count_ = triangle_count;

        // Log rebuilds sparingly (every ~5 s of play) instead of on
        // every frame — with per-frame rebuilds active this would
        // flood the console otherwise.
        static int as_build_log = 0;
        if (as_build_log++ % 300 == 0) {
            std::cout << "[GPU_RASTERIZER] Built Metal RT acceleration structure: "
                      << triangle_count << " triangles, "
                      << sizes.accelerationStructureSize / 1024 << " KB" << std::endl;
        }

        return true;
    } else {
        std::cerr << "[GPU_RASTERIZER] macOS 14.0+ required for acceleration structures" << std::endl;
        return false;
    }
}

} // namespace Logosphere
