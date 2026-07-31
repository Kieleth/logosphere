// metal_compute_bridge.mm
// Metal GPU compute implementation for shadow ray tracing
//
// OBJECTIVE-C++ BRIDGE: This file mixes Objective-C (Metal API) with C++
// File extension .mm tells compiler to use Objective-C++ mode
//
// EDUCATIONAL NOTES:
// - @autoreleasepool: Manages Objective-C memory (like RAII in C++)
// - id<Protocol>: Objective-C for "pointer to object conforming to Protocol"
// - [object method:arg]: Objective-C method call syntax
// - __bridge: Cast between C++ void* and Objective-C id without ownership change

#include "logosphere/rendering/gpu/metal_compute_bridge.h"
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <iostream>
#include <cstring>  // For memcpy
#include "../../lighting_metrics.h"  // For GPU profiling integration
#include "../../optimization_flags.h"  // For GPU_BUFFER_SLOTS configuration
#include "metallib_locator.h"

namespace Logosphere {

// =========================================================================
// CONSTRUCTOR / DESTRUCTOR
// =========================================================================

MetalComputeBridge::MetalComputeBridge()
    : device_(nullptr)
    , command_queue_(nullptr)
    , lighting_queue_(nullptr)
    , compute_pipeline_(nullptr)
    , library_(nullptr)
    , bvh_buffer_(nullptr)
    , bvh_compute_pipeline_(nullptr)
    , lighting_compute_pipeline_(nullptr)
    , pixels_buffer_(nullptr)
    , lights_buffer_(nullptr)
    , lighting_output_buffer_(nullptr)
    , pixels_buffer_capacity_(0)
    , lights_buffer_capacity_(0)
    , lighting_output_buffer_capacity_(0)
    , raycount_buffer_(nullptr)
    , buffer_semaphore_(nullptr)
    , current_buffer_index_(0)
    , initialized_(false)
{
    // Initialize multi-buffered arrays (configurable buffer count)
    for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
        rays_buffer_[i] = nullptr;
        triangles_buffer_[i] = nullptr;
        results_buffer_[i] = nullptr;
        rays_buffer_capacity_[i] = 0;
        triangles_buffer_capacity_[i] = 0;
        results_buffer_capacity_[i] = 0;
    }

    // Create semaphore for multi-buffering (GPU_BUFFER_SLOTS buffers available initially)
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(Optimizations::GPU_BUFFER_SLOTS);
    buffer_semaphore_ = (__bridge_retained void*)semaphore;
}

MetalComputeBridge::~MetalComputeBridge() {
    @autoreleasepool {
        // Release Metal resources (manual reference counting - no ARC)
        // We need to explicitly call [obj release] or use CFRelease

        // Release multi-buffered resources (configurable buffer count)
        for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
            if (results_buffer_[i]) {
                CFBridgingRelease(results_buffer_[i]);
                results_buffer_[i] = nullptr;
            }
            if (triangles_buffer_[i]) {
                CFBridgingRelease(triangles_buffer_[i]);
                triangles_buffer_[i] = nullptr;
            }
            if (rays_buffer_[i]) {
                CFBridgingRelease(rays_buffer_[i]);
                rays_buffer_[i] = nullptr;
            }
        }

        if (raycount_buffer_) {
            CFBridgingRelease(raycount_buffer_);
            raycount_buffer_ = nullptr;
        }

        if (bvh_buffer_) {
            id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)bvh_buffer_;
            [buffer release];
            bvh_buffer_ = nullptr;
        }

        if (bvh_compute_pipeline_) {
            CFBridgingRelease(bvh_compute_pipeline_);
            bvh_compute_pipeline_ = nullptr;
        }

        // Release lighting compute resources
        if (lighting_compute_pipeline_) {
            CFBridgingRelease(lighting_compute_pipeline_);
            lighting_compute_pipeline_ = nullptr;
        }

        if (pixels_buffer_) {
            CFBridgingRelease(pixels_buffer_);
            pixels_buffer_ = nullptr;
        }

        if (lights_buffer_) {
            CFBridgingRelease(lights_buffer_);
            lights_buffer_ = nullptr;
        }

        if (lighting_output_buffer_) {
            CFBridgingRelease(lighting_output_buffer_);
            lighting_output_buffer_ = nullptr;
        }

        if (compute_pipeline_) {
            id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_;
            [pipeline release];
            compute_pipeline_ = nullptr;
        }

        if (library_) {
            id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;
            [library release];
            library_ = nullptr;
        }

        if (lighting_queue_) {
            id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)lighting_queue_;
            [queue release];
            lighting_queue_ = nullptr;
        }

        if (command_queue_) {
            id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
            [queue release];
            command_queue_ = nullptr;
        }

        if (device_) {
            id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
            [device release];
            device_ = nullptr;
        }

        // Release semaphore
        if (buffer_semaphore_) {
            CFBridgingRelease(buffer_semaphore_);
            buffer_semaphore_ = nullptr;
        }
    }
}

// =========================================================================
// INITIALIZATION
// =========================================================================

bool MetalComputeBridge::initialize() {
    @autoreleasepool {
        // STEP 1: Get default Metal device
        // MTLCreateSystemDefaultDevice() returns the GPU (or fallback to integrated)
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();

        if (!device) {
            std::cerr << "[METAL ERROR] No Metal-capable device found" << std::endl;
            std::cerr << "  This Mac may not support Metal (macOS 10.11+ required)" << std::endl;
            return false;
        }

        std::cout << "[METAL INIT " << (void*)this << "] Initialized device: "
                  << [device.name UTF8String] << std::endl;

        // Store device (retain for C++ ownership - manual reference counting)
        [device retain];
        device_ = (__bridge void*)device;

        // STEP 2: Create command queue
        // Command queue manages execution of GPU commands
        // Like a queue of work submitted to the GPU
        id<MTLCommandQueue> queue = [device newCommandQueue];

        if (!queue) {
            std::cerr << "[METAL ERROR] Failed to create command queue" << std::endl;
            return false;
        }

        [queue retain];
        command_queue_ = (__bridge void*)queue;

        // STEP 2b: Create separate command queue for lighting (avoids deadlock in async callbacks)
        id<MTLCommandQueue> lightingQueue = [device newCommandQueue];
        if (!lightingQueue) {
            std::cerr << "[METAL ERROR] Failed to create lighting command queue" << std::endl;
            return false;
        }
        [lightingQueue retain];
        lighting_queue_ = (__bridge void*)lightingQueue;
        std::cout << "[METAL INIT] Created separate lighting command queue (avoids async deadlock)" << std::endl;

        // STEP 3: Load shader library
        if (!load_shader_library()) {
            return false;
        }

        // STEP 4: Create compute pipeline
        if (!create_compute_pipeline()) {
            return false;
        }

        initialized_ = true;
        std::cout << "[METAL] GPU compute bridge initialized successfully" << std::endl;
        return true;
    }
}

bool MetalComputeBridge::load_shader_library() {
    @autoreleasepool {
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;

        // METAL LIBRARY LOADING:
        // Option 1: Load from .metallib file (what we use)
        // Option 2: Compile from source at runtime (slower, for development)
        // Option 3: Use default library (if compiled into app bundle)

        // default.metallib resolved relative to the executable — never the
        // CWD (stale cross-tree shader hazard).
        NSError* error = nil;
        id<MTLLibrary> library = nil;

        std::string libPath = logosphere::gpu::locate_metallib();
        if (!libPath.empty()) {
            NSURL* url = [NSURL fileURLWithPath:
                             [NSString stringWithUTF8String:libPath.c_str()]];
            library = [device newLibraryWithURL:url error:&error];
            if (library) {
                std::cout << "[METAL] Metal library: " << libPath << std::endl;
            }
        }

        if (!library) {
            library = [device newDefaultLibrary];

            if (!library) {
                std::cerr << "[METAL ERROR] Failed to load shader library" << std::endl;
                std::cerr << "  Executable-relative search found: "
                          << (libPath.empty() ? "(nothing)" : libPath) << std::endl;
                std::cerr << "  Tried: [device newDefaultLibrary]" << std::endl;
                    if (error) {
                        std::cerr << "  Last error: " << [[error localizedDescription] UTF8String] << std::endl;
                    }
                    std::cerr << "  Make sure Metal shaders were compiled (cmake --build build --target metal_shaders)" << std::endl;
                    return false;
                }
                std::cout << "[METAL] Loaded default shader library" << std::endl;
            }

        [library retain];
        library_ = (__bridge void*)library;
        return true;
    }
}

bool MetalComputeBridge::create_compute_pipeline() {
    @autoreleasepool {
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;

        // STEP 1: Get compute function from library
        // Function name must match kernel function in shadow_ray.metal
        NSString* functionName = @"trace_shadow_ray";
        id<MTLFunction> computeFunction = [library newFunctionWithName:functionName];

        if (!computeFunction) {
            std::cerr << "[METAL ERROR] Failed to find compute function: "
                      << [functionName UTF8String] << std::endl;
            std::cerr << "  Check that shadow_ray.metal contains kernel function with this name" << std::endl;
            return false;
        }

        std::cout << "[METAL] Found compute function: " << [functionName UTF8String] << std::endl;

        // STEP 2: Create compute pipeline state
        // This compiles the function for the specific GPU
        NSError* error = nil;
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:computeFunction error:&error];

        if (!pipeline) {
            std::cerr << "[METAL ERROR] Failed to create compute pipeline" << std::endl;
            if (error) {
                std::cerr << "  Error: " << [[error localizedDescription] UTF8String] << std::endl;
            }
            return false;
        }

        // Log pipeline info
        std::cout << "[METAL] Created compute pipeline:" << std::endl;
        std::cout << "  Max total threads per threadgroup: "
                  << pipeline.maxTotalThreadsPerThreadgroup << std::endl;
        std::cout << "  Thread execution width: "
                  << pipeline.threadExecutionWidth << std::endl;

        [pipeline retain];
        compute_pipeline_ = (__bridge void*)pipeline;
        return true;
    }
}

// =========================================================================
// SHADOW RAY TRACING
// =========================================================================

float MetalComputeBridge::trace_shadow_ray(
    const ShadowRay& ray,
    const ShadowTriangle& triangle)
{
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] trace_shadow_ray called before initialize()" << std::endl;
            return 1.0f;  // Return "lit" as safe default
        }

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLComputePipelineState> pipeline = (__bridge id<MTLComputePipelineState>)compute_pipeline_;

        // STEP 1: Create buffers for input/output data
        // Metal buffers are GPU-accessible memory
        // We create 3 buffers: ray (input), triangle (input), result (output)

        // Buffer sizes must match struct sizes (with padding!)
        const NSUInteger raySize = sizeof(ShadowRay);         // 32 bytes
        const NSUInteger triangleSize = sizeof(ShadowTriangle); // 48 bytes
        const NSUInteger resultSize = sizeof(float);           // 4 bytes

        // Create buffers with MTLResourceStorageModeShared
        // Shared = CPU and GPU can both access (slower but simpler for MVP)
        // Future optimization: Use MTLResourceStorageModePrivate for GPU-only buffers
        id<MTLBuffer> rayBuffer =
            [device newBufferWithLength:raySize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> triangleBuffer =
            [device newBufferWithLength:triangleSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> resultBuffer =
            [device newBufferWithLength:resultSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        if (!rayBuffer || !triangleBuffer || !resultBuffer) {
            std::cerr << "[METAL ERROR] Failed to create GPU buffers" << std::endl;
            return 1.0f;
        }

        // STEP 2: Upload data to GPU
        // memcpy into buffer contents (CPU → GPU memory)
        std::memcpy([rayBuffer contents], &ray, raySize);
        std::memcpy([triangleBuffer contents], &triangle, triangleSize);

        // Initialize result to 1.0 (default = lit)
        float initialResult = 1.0f;
        std::memcpy([resultBuffer contents], &initialResult, resultSize);

        // STEP 3: Create command buffer
        // Command buffer = batch of GPU commands to execute atomically
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];

        if (!commandBuffer) {
            std::cerr << "[METAL ERROR] Failed to create command buffer" << std::endl;
            return 1.0f;
        }

        // STEP 4: Create compute command encoder
        // Encoder = writes GPU commands into command buffer
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];

        if (!encoder) {
            std::cerr << "[METAL ERROR] Failed to create compute encoder" << std::endl;
            return 1.0f;
        }

        // STEP 5: Set compute pipeline and buffers
        // Tell GPU which shader to run and which buffers to use
        [encoder setComputePipelineState:pipeline];

        // Bind buffers to shader buffer indices (must match [[buffer(N)]] in .metal)
        [encoder setBuffer:rayBuffer offset:0 atIndex:0];      // [[buffer(0)]]
        [encoder setBuffer:triangleBuffer offset:0 atIndex:1]; // [[buffer(1)]]
        [encoder setBuffer:resultBuffer offset:0 atIndex:2];   // [[buffer(2)]]

        // STEP 6: Dispatch compute threads
        // For MVP: dispatch exactly 1 thread (overkill for GPU but validates pipeline)
        // Thread organization: (width, height, depth)
        MTLSize gridSize = MTLSizeMake(1, 1, 1);  // 1 thread total

        // Threadgroup size: how many threads execute together
        // For 1 thread, threadgroup size is also 1
        MTLSize threadgroupSize = MTLSizeMake(1, 1, 1);

        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];

        // STEP 7: Finish encoding and submit
        [encoder endEncoding];

        // Commit command buffer to queue (start GPU execution)
        [commandBuffer commit];

        // STEP 8: Wait for completion (synchronous for MVP)
        // In production, we'd use callbacks or double-buffering
        // For Phase I, we just block until GPU finishes
        [commandBuffer waitUntilCompleted];

        // Check for errors
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            std::cerr << "[METAL ERROR] Command buffer execution failed" << std::endl;
            if (commandBuffer.error) {
                std::cerr << "  Error: "
                          << [[commandBuffer.error localizedDescription] UTF8String]
                          << std::endl;
            }
            return 1.0f;
        }

        // STEP 9: Read result back from GPU
        // GPU → CPU memory
        float result;
        std::memcpy(&result, [resultBuffer contents], resultSize);

        return result;
    }
}

// =========================================================================
// MULTI-TRIANGLE SHADOW RAY (Phase I-B)
// =========================================================================

float MetalComputeBridge::trace_shadow_ray_multi(
    const ShadowRay& ray,
    const ShadowTriangle* triangles,
    int triangle_count)
{
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] trace_shadow_ray_multi called before initialize()" << std::endl;
            return 1.0f;  // Return "lit" as safe default
        }

        if (triangle_count <= 0) {
            // No triangles to test - ray is unoccluded
            return 1.0f;
        }

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;

        // STEP 1: Get multi-triangle compute function
        // Note: We create pipeline on-demand instead of storing it
        // This keeps initialization simple (KISS principle)
        NSString* functionName = @"trace_shadow_ray_multi_triangle";
        id<MTLFunction> computeFunction = [library newFunctionWithName:functionName];

        if (!computeFunction) {
            std::cerr << "[METAL ERROR] Failed to find compute function: "
                      << [functionName UTF8String] << std::endl;
            return 1.0f;
        }

        // Create compute pipeline for this function
        NSError* error = nil;
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:computeFunction error:&error];

        if (!pipeline) {
            std::cerr << "[METAL ERROR] Failed to create multi-triangle compute pipeline" << std::endl;
            if (error) {
                std::cerr << "  Error: " << [[error localizedDescription] UTF8String] << std::endl;
            }
            return 1.0f;
        }

        // STEP 2: Create buffers for input/output data
        const NSUInteger raySize = sizeof(ShadowRay);  // 32 bytes
        const NSUInteger trianglesSize = sizeof(ShadowTriangle) * triangle_count;  // 48 * N bytes
        const NSUInteger countSize = sizeof(uint32_t);  // 4 bytes
        const NSUInteger resultSize = sizeof(float);    // 4 bytes

        id<MTLBuffer> rayBuffer =
            [device newBufferWithLength:raySize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> trianglesBuffer =
            [device newBufferWithLength:trianglesSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> countBuffer =
            [device newBufferWithLength:countSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> resultBuffer =
            [device newBufferWithLength:resultSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        if (!rayBuffer || !trianglesBuffer || !countBuffer || !resultBuffer) {
            std::cerr << "[METAL ERROR] Failed to create GPU buffers for multi-triangle" << std::endl;
            return 1.0f;
        }

        // STEP 3: Upload data to GPU
        std::memcpy([rayBuffer contents], &ray, raySize);
        std::memcpy([trianglesBuffer contents], triangles, trianglesSize);

        uint32_t count_u32 = static_cast<uint32_t>(triangle_count);
        std::memcpy([countBuffer contents], &count_u32, countSize);

        // Initialize result to 1.0 (default = lit)
        float initialResult = 1.0f;
        std::memcpy([resultBuffer contents], &initialResult, resultSize);

        // STEP 4: Create command buffer and encoder
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        if (!commandBuffer) {
            std::cerr << "[METAL ERROR] Failed to create command buffer" << std::endl;
            return 1.0f;
        }

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (!encoder) {
            std::cerr << "[METAL ERROR] Failed to create compute encoder" << std::endl;
            return 1.0f;
        }

        // STEP 5: Set pipeline and bind buffers
        [encoder setComputePipelineState:pipeline];

        // Bind buffers to match shader signature:
        // trace_shadow_ray_multi_triangle(
        //     device const Ray* ray [[buffer(0)]],
        //     device const Triangle* triangles [[buffer(1)]],
        //     constant uint& triangle_count [[buffer(2)]],
        //     device float* result [[buffer(3)]]
        // )
        [encoder setBuffer:rayBuffer offset:0 atIndex:0];       // [[buffer(0)]]
        [encoder setBuffer:trianglesBuffer offset:0 atIndex:1]; // [[buffer(1)]]
        [encoder setBuffer:countBuffer offset:0 atIndex:2];     // [[buffer(2)]]
        [encoder setBuffer:resultBuffer offset:0 atIndex:3];    // [[buffer(3)]]

        // STEP 6: Dispatch compute threads
        // Still using 1 thread for single ray (same as single-triangle version)
        MTLSize gridSize = MTLSizeMake(1, 1, 1);
        MTLSize threadgroupSize = MTLSizeMake(1, 1, 1);

        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];

        // STEP 7: Submit and wait
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        // Check for errors
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            std::cerr << "[METAL ERROR] Multi-triangle command buffer execution failed" << std::endl;
            if (commandBuffer.error) {
                std::cerr << "  Error: "
                          << [[commandBuffer.error localizedDescription] UTF8String]
                          << std::endl;
            }
            return 1.0f;
        }

        // STEP 8: Read result
        float result;
        std::memcpy(&result, [resultBuffer contents], resultSize);

        return result;
    }
}

// =========================================================================
// PARALLEL SHADOW RAYS (Phase I-C)
// =========================================================================

void MetalComputeBridge::trace_shadow_rays_parallel(
    const ShadowRay* rays,
    int ray_count,
    const ShadowTriangle* triangles,
    int triangle_count,
    float* results)
{
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] trace_shadow_rays_parallel called before initialize()" << std::endl;
            return;
        }

        if (ray_count <= 0 || triangle_count <= 0) {
            return;
        }

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;

        // STEP 1: Get parallel rays compute function
        NSString* functionName = @"trace_shadow_rays_parallel";
        id<MTLFunction> computeFunction = [library newFunctionWithName:functionName];

        if (!computeFunction) {
            std::cerr << "[METAL ERROR] Failed to find compute function: "
                      << [functionName UTF8String] << std::endl;
            return;
        }

        // Create compute pipeline for this function
        NSError* error = nil;
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:computeFunction error:&error];

        if (!pipeline) {
            std::cerr << "[METAL ERROR] Failed to create parallel rays compute pipeline" << std::endl;
            if (error) {
                std::cerr << "  Error: " << [[error localizedDescription] UTF8String] << std::endl;
            }
            return;
        }

        // STEP 2: Create buffers for input/output data
        const NSUInteger raysSize = sizeof(ShadowRay) * ray_count;        // 32 * N bytes
        const NSUInteger trianglesSize = sizeof(ShadowTriangle) * triangle_count;  // 48 * M bytes
        const NSUInteger rayCountSize = sizeof(uint32_t);                 // 4 bytes
        const NSUInteger triangleCountSize = sizeof(uint32_t);            // 4 bytes
        const NSUInteger resultsSize = sizeof(float) * ray_count;         // 4 * N bytes

        id<MTLBuffer> raysBuffer =
            [device newBufferWithLength:raysSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> trianglesBuffer =
            [device newBufferWithLength:trianglesSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> rayCountBuffer =
            [device newBufferWithLength:rayCountSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> triangleCountBuffer =
            [device newBufferWithLength:triangleCountSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> resultsBuffer =
            [device newBufferWithLength:resultsSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        if (!raysBuffer || !trianglesBuffer || !rayCountBuffer ||
            !triangleCountBuffer || !resultsBuffer) {
            std::cerr << "[METAL ERROR] Failed to create GPU buffers for parallel rays" << std::endl;
            return;
        }

        // STEP 3: Upload data to GPU
        std::memcpy([raysBuffer contents], rays, raysSize);
        std::memcpy([trianglesBuffer contents], triangles, trianglesSize);

        uint32_t ray_count_u32 = static_cast<uint32_t>(ray_count);
        uint32_t triangle_count_u32 = static_cast<uint32_t>(triangle_count);
        std::memcpy([rayCountBuffer contents], &ray_count_u32, rayCountSize);
        std::memcpy([triangleCountBuffer contents], &triangle_count_u32, triangleCountSize);

        // Initialize results to 1.0 (default = lit)
        for (int i = 0; i < ray_count; i++) {
            ((float*)[resultsBuffer contents])[i] = 1.0f;
        }

        // STEP 4: Create command buffer and encoder
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        if (!commandBuffer) {
            std::cerr << "[METAL ERROR] Failed to create command buffer" << std::endl;
            return;
        }

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (!encoder) {
            std::cerr << "[METAL ERROR] Failed to create compute encoder" << std::endl;
            return;
        }

        // STEP 5: Set pipeline and bind buffers
        [encoder setComputePipelineState:pipeline];

        // Bind buffers to match shader signature:
        // trace_shadow_rays_parallel(
        //     device const Ray* rays [[buffer(0)]],
        //     device const Triangle* triangles [[buffer(1)]],
        //     constant uint& ray_count [[buffer(2)]],
        //     constant uint& triangle_count [[buffer(3)]],
        //     device float* results [[buffer(4)]]
        // )
        [encoder setBuffer:raysBuffer offset:0 atIndex:0];              // [[buffer(0)]]
        [encoder setBuffer:trianglesBuffer offset:0 atIndex:1];         // [[buffer(1)]]
        [encoder setBuffer:rayCountBuffer offset:0 atIndex:2];          // [[buffer(2)]]
        [encoder setBuffer:triangleCountBuffer offset:0 atIndex:3];     // [[buffer(3)]]
        [encoder setBuffer:resultsBuffer offset:0 atIndex:4];           // [[buffer(4)]]

        // STEP 6: Dispatch compute threads (TRUE PARALLELISM!)
        // KEY DIFFERENCE: Dispatch N threads (one per ray), not 1 thread!

        // Grid size: Total number of threads to dispatch
        MTLSize gridSize = MTLSizeMake(ray_count, 1, 1);

        // Threadgroup size: How many threads execute together
        // Metal recommendation: Multiple of SIMD width (32 on Apple Silicon)
        // We use 64 threads per threadgroup (2 SIMD groups)
        NSUInteger threadsPerGroup = 64;

        // Ensure threadgroup size doesn't exceed GPU limits
        NSUInteger maxThreadsPerGroup = pipeline.maxTotalThreadsPerThreadgroup;
        if (threadsPerGroup > maxThreadsPerGroup) {
            threadsPerGroup = maxThreadsPerGroup;
        }

        MTLSize threadgroupSize = MTLSizeMake(threadsPerGroup, 1, 1);

        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];

        // STEP 7: Submit and wait
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        // Check for errors
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            std::cerr << "[METAL ERROR] Parallel rays command buffer execution failed" << std::endl;
            if (commandBuffer.error) {
                std::cerr << "  Error: "
                          << [[commandBuffer.error localizedDescription] UTF8String]
                          << std::endl;
            }
            return;
        }

        // STEP 8: Read results back to CPU
        std::memcpy(results, [resultsBuffer contents], resultsSize);
    }
}

// =========================================================================
// BATCHED SHADOW RAYS (Phase II-A)
// =========================================================================

void MetalComputeBridge::trace_shadow_rays_batched(
    const ShadowRay* rays,
    int ray_count,
    const ShadowTriangle* triangles,
    int triangle_count,
    float* results)
{
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] trace_shadow_rays_batched called before initialize()" << std::endl;
            return;
        }

        if (ray_count <= 0 || triangle_count <= 0) {
            return;
        }

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;

        // STEP 1: Get batched rays compute function
        NSString* functionName = @"trace_shadow_rays_batched";
        id<MTLFunction> computeFunction = [library newFunctionWithName:functionName];

        if (!computeFunction) {
            std::cerr << "[METAL ERROR] Failed to find compute function: "
                      << [functionName UTF8String] << std::endl;
            return;
        }

        // Create compute pipeline for this function
        NSError* error = nil;
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:computeFunction error:&error];

        if (!pipeline) {
            std::cerr << "[METAL ERROR] Failed to create batched rays compute pipeline" << std::endl;
            if (error) {
                std::cerr << "  Error: " << [[error localizedDescription] UTF8String] << std::endl;
            }
            return;
        }

        // STEP 2: Create buffers (same as parallel version)
        const NSUInteger raysSize = sizeof(ShadowRay) * ray_count;
        const NSUInteger trianglesSize = sizeof(ShadowTriangle) * triangle_count;
        const NSUInteger rayCountSize = sizeof(uint32_t);
        const NSUInteger triangleCountSize = sizeof(uint32_t);
        const NSUInteger resultsSize = sizeof(float) * ray_count;

        id<MTLBuffer> raysBuffer =
            [device newBufferWithLength:raysSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> trianglesBuffer =
            [device newBufferWithLength:trianglesSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> rayCountBuffer =
            [device newBufferWithLength:rayCountSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> triangleCountBuffer =
            [device newBufferWithLength:triangleCountSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        id<MTLBuffer> resultsBuffer =
            [device newBufferWithLength:resultsSize
                                options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        if (!raysBuffer || !trianglesBuffer || !rayCountBuffer ||
            !triangleCountBuffer || !resultsBuffer) {
            std::cerr << "[METAL ERROR] Failed to create GPU buffers for batched rays" << std::endl;
            return;
        }

        // STEP 3: Upload data to GPU
        std::memcpy([raysBuffer contents], rays, raysSize);
        std::memcpy([trianglesBuffer contents], triangles, trianglesSize);

        uint32_t ray_count_u32 = static_cast<uint32_t>(ray_count);
        uint32_t triangle_count_u32 = static_cast<uint32_t>(triangle_count);
        std::memcpy([rayCountBuffer contents], &ray_count_u32, rayCountSize);
        std::memcpy([triangleCountBuffer contents], &triangle_count_u32, triangleCountSize);

        // Initialize results to 1.0 (default = lit)
        for (int i = 0; i < ray_count; i++) {
            ((float*)[resultsBuffer contents])[i] = 1.0f;
        }

        // STEP 4: Create command buffer and encoder
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        if (!commandBuffer) {
            std::cerr << "[METAL ERROR] Failed to create command buffer" << std::endl;
            return;
        }

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (!encoder) {
            std::cerr << "[METAL ERROR] Failed to create compute encoder" << std::endl;
            return;
        }

        // STEP 5: Set pipeline and bind buffers
        [encoder setComputePipelineState:pipeline];

        // Bind buffers (same layout as parallel version)
        [encoder setBuffer:raysBuffer offset:0 atIndex:0];              // [[buffer(0)]]
        [encoder setBuffer:trianglesBuffer offset:0 atIndex:1];         // [[buffer(1)]]
        [encoder setBuffer:rayCountBuffer offset:0 atIndex:2];          // [[buffer(2)]]
        [encoder setBuffer:triangleCountBuffer offset:0 atIndex:3];     // [[buffer(3)]]
        [encoder setBuffer:resultsBuffer offset:0 atIndex:4];           // [[buffer(4)]]

        // STEP 6: Dispatch compute threads (BATCHED!)
        // KEY DIFFERENCE: Each thread processes 8 rays, not 1 ray!
        // Grid size = number of batches, not number of rays

        const int RAYS_PER_BATCH = 8;
        int num_batches = (ray_count + RAYS_PER_BATCH - 1) / RAYS_PER_BATCH;  // Round up

        // Grid size: Total number of threads (one per batch)
        MTLSize gridSize = MTLSizeMake(num_batches, 1, 1);

        // Threadgroup size: 64 threads (8 batches × 8 rays = 64 rays processed per threadgroup)
        NSUInteger threadsPerGroup = 64;

        // Ensure threadgroup size doesn't exceed GPU limits
        NSUInteger maxThreadsPerGroup = pipeline.maxTotalThreadsPerThreadgroup;
        if (threadsPerGroup > maxThreadsPerGroup) {
            threadsPerGroup = maxThreadsPerGroup;
        }

        MTLSize threadgroupSize = MTLSizeMake(threadsPerGroup, 1, 1);

        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];

        // STEP 7: Submit and wait
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        // Check for errors
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            std::cerr << "[METAL ERROR] Batched rays command buffer execution failed" << std::endl;
            if (commandBuffer.error) {
                std::cerr << "  Error: "
                          << [[commandBuffer.error localizedDescription] UTF8String]
                          << std::endl;
            }
            return;
        }

        // STEP 8: Read results back to CPU
        std::memcpy(results, [resultsBuffer contents], resultsSize);
    }
}

// =========================================================================
// BVH UPLOAD (Phase II-B)
// =========================================================================

void MetalComputeBridge::upload_bvh(const void* bvh_nodes, int node_count) {
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] upload_bvh called before initialize()" << std::endl;
            return;
        }

        if (!bvh_nodes || node_count <= 0) {
            std::cerr << "[METAL ERROR] upload_bvh called with invalid parameters" << std::endl;
            return;
        }

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;

        // Calculate buffer size (48 bytes per node - aligned for GPU)
        const NSUInteger bufferSize = node_count * 48;  // GPUBVHNode is 48 bytes

        // Release old buffer if it exists
        if (bvh_buffer_) {
            id<MTLBuffer> oldBuffer = (__bridge id<MTLBuffer>)bvh_buffer_;
            [oldBuffer release];
            bvh_buffer_ = nullptr;
        }

        // Create persistent buffer for BVH nodes
        // MTLResourceStorageModeShared = CPU and GPU can both access
        // This is the same pattern as triangle upload
        id<MTLBuffer> buffer = [device newBufferWithLength:bufferSize
                                                   options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        if (!buffer) {
            std::cerr << "[METAL ERROR] Failed to create BVH buffer ("
                      << bufferSize << " bytes)" << std::endl;
            return;
        }

        // Upload BVH nodes to GPU
        std::memcpy([buffer contents], bvh_nodes, bufferSize);

        // Store buffer (retain for persistence)
        [buffer retain];
        bvh_buffer_ = (__bridge void*)buffer;
    }
}

// =========================================================================
// BATCHED SHADOW RAYS WITH BVH (Phase II-B)
// =========================================================================

void MetalComputeBridge::trace_shadow_rays_batched_bvh(
    const ShadowRay* rays,
    int ray_count,
    const ShadowTriangle* triangles,
    int triangle_count,
    float* results)
{
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] trace_shadow_rays_batched_bvh called before initialize()" << std::endl;
            return;
        }

        if (ray_count <= 0 || triangle_count <= 0) {
            return;
        }

        if (!bvh_buffer_) {
            std::cerr << "[METAL ERROR] trace_shadow_rays_batched_bvh called but BVH not uploaded!" << std::endl;
            return;
        }

        // GPU PROFILING: Get metrics singleton (PERFORMANCE_RESEARCH.md: count operations, no timing overhead)
        LightingMetrics& metrics = LightingMetrics::get();
        metrics.gpu_dispatches++;  // Count dispatches (zero overhead)
        metrics.gpu_rays_dispatched += ray_count;  // Count rays

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;

        // STEP 1: Get or create cached BVH compute pipeline (OPTIMIZED: create once, reuse)
        id<MTLComputePipelineState> pipeline = nil;

        if (!bvh_compute_pipeline_) {
            // First call - create and cache pipeline
            NSString* functionName = @"trace_shadow_rays_batched_bvh";
            id<MTLFunction> computeFunction = [library newFunctionWithName:functionName];

            if (!computeFunction) {
                std::cerr << "[METAL ERROR] Failed to find compute function: "
                          << [functionName UTF8String] << std::endl;
                return;
            }

            NSError* error = nil;
            pipeline = [device newComputePipelineStateWithFunction:computeFunction error:&error];

            if (!pipeline) {
                std::cerr << "[METAL ERROR] Failed to create BVH compute pipeline" << std::endl;
                if (error) {
                    std::cerr << "  Error: " << [[error localizedDescription] UTF8String] << std::endl;
                }
                return;
            }

            // Cache for future calls (PERFORMANCE: eliminates 5-10ms per frame)
            [pipeline retain];
            bvh_compute_pipeline_ = (__bridge void*)pipeline;
            std::cout << "[GPU OPTIMIZE] BVH compute pipeline cached" << std::endl;
        } else {
            // Reuse cached pipeline
            pipeline = (__bridge id<MTLComputePipelineState>)bvh_compute_pipeline_;
        }

        // STEP 2: Get or create persistent buffers (OPTIMIZED: allocate once, resize if needed)
        const NSUInteger raysSize = sizeof(ShadowRay) * ray_count;
        const NSUInteger trianglesSize = sizeof(ShadowTriangle) * triangle_count;
        const NSUInteger rayCountSize = sizeof(uint32_t);
        const NSUInteger resultsSize = sizeof(float) * ray_count;

        // Use buffer index 0 (double-buffering requires async, separate optimization)
        const int bufIdx = 0;

        // Rays buffer
        id<MTLBuffer> raysBuffer = nil;
        if (!rays_buffer_[bufIdx] || rays_buffer_capacity_[bufIdx] < raysSize) {
            // Create or resize
            if (rays_buffer_[bufIdx]) {
                CFBridgingRelease(rays_buffer_[bufIdx]);
            }
            raysBuffer = [device newBufferWithLength:raysSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!raysBuffer) {
                std::cerr << "[METAL ERROR] Failed to create rays buffer" << std::endl;
                return;
            }
            [raysBuffer retain];
            rays_buffer_[bufIdx] = (__bridge void*)raysBuffer;
            rays_buffer_capacity_[bufIdx] = raysSize;
        } else {
            raysBuffer = (__bridge id<MTLBuffer>)rays_buffer_[bufIdx];
        }

        // Triangles buffer
        id<MTLBuffer> trianglesBuffer = nil;
        if (!triangles_buffer_[bufIdx] || triangles_buffer_capacity_[bufIdx] < trianglesSize) {
            if (triangles_buffer_[bufIdx]) {
                CFBridgingRelease(triangles_buffer_[bufIdx]);
            }
            trianglesBuffer = [device newBufferWithLength:trianglesSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!trianglesBuffer) {
                std::cerr << "[METAL ERROR] Failed to create triangles buffer" << std::endl;
                return;
            }
            [trianglesBuffer retain];
            triangles_buffer_[bufIdx] = (__bridge void*)trianglesBuffer;
            triangles_buffer_capacity_[bufIdx] = trianglesSize;
        } else {
            trianglesBuffer = (__bridge id<MTLBuffer>)triangles_buffer_[bufIdx];
        }

        // Results buffer
        id<MTLBuffer> resultsBuffer = nil;
        if (!results_buffer_[bufIdx] || results_buffer_capacity_[bufIdx] < resultsSize) {
            if (results_buffer_[bufIdx]) {
                CFBridgingRelease(results_buffer_[bufIdx]);
            }
            resultsBuffer = [device newBufferWithLength:resultsSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!resultsBuffer) {
                std::cerr << "[METAL ERROR] Failed to create results buffer" << std::endl;
                return;
            }
            [resultsBuffer retain];
            results_buffer_[bufIdx] = (__bridge void*)resultsBuffer;
            results_buffer_capacity_[bufIdx] = resultsSize;
        } else {
            resultsBuffer = (__bridge id<MTLBuffer>)results_buffer_[bufIdx];
        }

        // Ray count buffer (small, create once)
        id<MTLBuffer> rayCountBuffer = nil;
        if (!raycount_buffer_) {
            rayCountBuffer = [device newBufferWithLength:rayCountSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!rayCountBuffer) {
                std::cerr << "[METAL ERROR] Failed to create ray count buffer" << std::endl;
                return;
            }
            [rayCountBuffer retain];
            raycount_buffer_ = (__bridge void*)rayCountBuffer;
        } else {
            rayCountBuffer = (__bridge id<MTLBuffer>)raycount_buffer_;
        }

        // STEP 3: Upload data to GPU (buffers reused, only data changes)
        std::memcpy([raysBuffer contents], rays, raysSize);
        std::memcpy([trianglesBuffer contents], triangles, trianglesSize);

        uint32_t ray_count_u32 = static_cast<uint32_t>(ray_count);
        std::memcpy([rayCountBuffer contents], &ray_count_u32, rayCountSize);

        // Initialize results buffer to 1.0 (lit)
        float* results_ptr = (float*)[resultsBuffer contents];
        for (int i = 0; i < ray_count; i++) {
            results_ptr[i] = 1.0f;
        }

        // STEP 4: Create command buffer and encoder
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        if (!commandBuffer) {
            std::cerr << "[METAL ERROR] Failed to create command buffer" << std::endl;
            return;
        }

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (!encoder) {
            std::cerr << "[METAL ERROR] Failed to create compute encoder" << std::endl;
            return;
        }

        // STEP 5: Set pipeline and bind buffers
        [encoder setComputePipelineState:pipeline];

        // Bind buffers: rays, BVH, triangles, ray_count, results
        id<MTLBuffer> bvhBuffer = (__bridge id<MTLBuffer>)bvh_buffer_;
        [encoder setBuffer:raysBuffer offset:0 atIndex:0];         // [[buffer(0)]]
        [encoder setBuffer:bvhBuffer offset:0 atIndex:1];          // [[buffer(1)]] BVH!
        [encoder setBuffer:trianglesBuffer offset:0 atIndex:2];    // [[buffer(2)]]
        [encoder setBuffer:rayCountBuffer offset:0 atIndex:3];     // [[buffer(3)]]
        [encoder setBuffer:resultsBuffer offset:0 atIndex:4];      // [[buffer(4)]]

        // STEP 6: Dispatch compute threads
        const int RAYS_PER_BATCH = 8;
        int num_batches = (ray_count + RAYS_PER_BATCH - 1) / RAYS_PER_BATCH;

        MTLSize gridSize = MTLSizeMake(num_batches, 1, 1);

        NSUInteger threadsPerGroup = 64;
        NSUInteger maxThreadsPerGroup = pipeline.maxTotalThreadsPerThreadgroup;
        if (threadsPerGroup > maxThreadsPerGroup) {
            threadsPerGroup = maxThreadsPerGroup;
        }

        MTLSize threadgroupSize = MTLSizeMake(threadsPerGroup, 1, 1);

        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];

        // STEP 7: Submit and wait
        [encoder endEncoding];

        // Track upload bytes (always, zero overhead)
        metrics.gpu_upload_bytes += raysSize + trianglesSize;
        metrics.gpu_download_bytes += resultsSize;

        // GPU PROFILING: Use Metal hardware timestamps (only when profiling, zero overhead otherwise)
        if (metrics.should_profile) {
            // Enable GPU capture for this command buffer
            commandBuffer.label = @"Shadow BVH Traversal";
        }

        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        // Check for errors
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            std::cerr << "[METAL ERROR] BVH command buffer execution failed" << std::endl;
            if (commandBuffer.error) {
                std::cerr << "  Error: "
                          << [[commandBuffer.error localizedDescription] UTF8String]
                          << std::endl;
            }
            return;
        }

        // GPU PROFILING: Collect hardware timestamps (only when profiling)
        if (metrics.should_profile) {
            // Metal provides GPU start/end time via hardware counters (zero overhead)
            CFTimeInterval gpu_start = commandBuffer.GPUStartTime;
            CFTimeInterval gpu_end = commandBuffer.GPUEndTime;
            if (gpu_start > 0 && gpu_end > 0) {
                double kernel_time_ms = (gpu_end - gpu_start) * 1000.0;
                metrics.gpu_kernel_time += kernel_time_ms;
                metrics.gpu_total_time += kernel_time_ms;
            }
        }

        // STEP 8: Read results back to CPU
        std::memcpy(results, [resultsBuffer contents], resultsSize);
    }
}

// =========================================================================
// ASYNC BATCHED SHADOW RAYS WITH BVH (Phase III: Triple-Buffered Async)
// =========================================================================

void MetalComputeBridge::trace_shadow_rays_batched_bvh_async(
    const ShadowRay* rays,
    int ray_count,
    const ShadowTriangle* triangles,
    int triangle_count,
    CompletionCallback callback,
    void* user_data)
{
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] trace_shadow_rays_batched_bvh_async called before initialize()" << std::endl;
            return;
        }

        if (ray_count <= 0 || triangle_count <= 0) {
            return;
        }

        if (!bvh_buffer_) {
            std::cerr << "[METAL ERROR] trace_shadow_rays_batched_bvh_async called but BVH not uploaded!" << std::endl;
            return;
        }

        // GPU PROFILING: Count operations
        LightingMetrics& metrics = LightingMetrics::get();
        metrics.gpu_dispatches++;
        metrics.gpu_rays_dispatched += ray_count;

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
        id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;

        // STEP 1: Get or create cached BVH compute pipeline
        id<MTLComputePipelineState> pipeline = nil;
        if (!bvh_compute_pipeline_) {
            NSString* functionName = @"trace_shadow_rays_batched_bvh";
            id<MTLFunction> computeFunction = [library newFunctionWithName:functionName];
            if (!computeFunction) {
                std::cerr << "[METAL ERROR] Failed to find compute function" << std::endl;
                return;
            }
            NSError* error = nil;
            pipeline = [device newComputePipelineStateWithFunction:computeFunction error:&error];
            if (error) {
                std::cerr << "[METAL ERROR] Pipeline creation: "
                          << [[error localizedDescription] UTF8String] << std::endl;
                return;
            }
            [pipeline retain];
            bvh_compute_pipeline_ = (__bridge void*)pipeline;
        } else {
            pipeline = (__bridge id<MTLComputePipelineState>)bvh_compute_pipeline_;
        }

        // STEP 2: ACQUIRE BUFFER FROM SEMAPHORE (triple-buffering!)
        dispatch_semaphore_t semaphore = (__bridge dispatch_semaphore_t)buffer_semaphore_;

        // DIAGNOSTIC: Time semaphore acquisition (detects GPU backpressure)
        auto sem_start = std::chrono::high_resolution_clock::now();

        // 5-second timeout to prevent kernel panic during GPU stalls
        constexpr int64_t TIMEOUT_NS = 5LL * 1000000000LL;
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, TIMEOUT_NS);
        long wait_result = dispatch_semaphore_wait(semaphore, timeout);
        if (wait_result != 0) {
            std::cerr << "[METAL_BRIDGE] TIMEOUT in trace_shadow_rays (5s) - aborting" << std::endl;
            return;  // Abort, don't crash the system
        }
        auto sem_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - sem_start).count();

        // Log ALWAYS for first 10 calls to verify code executes, then only on significant waits or ray count changes
        static int sem_log_count = 0;
        static int last_ray_count = 0;
        if (sem_log_count < 10 || sem_ms > 1.0 || ray_count != last_ray_count) {
            std::cout << "[GPU_SEMAPHORE_WAIT] " << sem_ms << "ms (rays=" << ray_count << ")" << std::endl;
            sem_log_count++;
            last_ray_count = ray_count;
        }

        // Get current buffer index and rotate for next call
        const int bufIdx = current_buffer_index_;
        current_buffer_index_ = (current_buffer_index_ + 1) % Optimizations::GPU_BUFFER_SLOTS;

        // STEP 3: Get or create buffers at this index
        const NSUInteger raysSize = sizeof(ShadowRay) * ray_count;
        const NSUInteger trianglesSize = sizeof(ShadowTriangle) * triangle_count;
        const NSUInteger rayCountSize = sizeof(uint32_t);
        const NSUInteger resultsSize = sizeof(float) * ray_count;

        // Rays buffer
        id<MTLBuffer> raysBuffer = nil;
        if (!rays_buffer_[bufIdx] || rays_buffer_capacity_[bufIdx] < raysSize) {
            if (rays_buffer_[bufIdx]) {
                CFBridgingRelease(rays_buffer_[bufIdx]);
            }
            raysBuffer = [device newBufferWithLength:raysSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!raysBuffer) {
                std::cerr << "[METAL ERROR] Failed to create rays buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);  // Release on error!
                return;
            }
            [raysBuffer retain];
            rays_buffer_[bufIdx] = (__bridge void*)raysBuffer;
            rays_buffer_capacity_[bufIdx] = raysSize;
        } else {
            raysBuffer = (__bridge id<MTLBuffer>)rays_buffer_[bufIdx];
        }

        // Triangles buffer
        id<MTLBuffer> trianglesBuffer = nil;
        if (!triangles_buffer_[bufIdx] || triangles_buffer_capacity_[bufIdx] < trianglesSize) {
            if (triangles_buffer_[bufIdx]) {
                CFBridgingRelease(triangles_buffer_[bufIdx]);
            }
            trianglesBuffer = [device newBufferWithLength:trianglesSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!trianglesBuffer) {
                std::cerr << "[METAL ERROR] Failed to create triangles buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            [trianglesBuffer retain];
            triangles_buffer_[bufIdx] = (__bridge void*)trianglesBuffer;
            triangles_buffer_capacity_[bufIdx] = trianglesSize;
        } else {
            trianglesBuffer = (__bridge id<MTLBuffer>)triangles_buffer_[bufIdx];
        }

        // Results buffer
        id<MTLBuffer> resultsBuffer = nil;
        if (!results_buffer_[bufIdx] || results_buffer_capacity_[bufIdx] < resultsSize) {
            if (results_buffer_[bufIdx]) {
                CFBridgingRelease(results_buffer_[bufIdx]);
            }
            resultsBuffer = [device newBufferWithLength:resultsSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!resultsBuffer) {
                std::cerr << "[METAL ERROR] Failed to create results buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            [resultsBuffer retain];
            results_buffer_[bufIdx] = (__bridge void*)resultsBuffer;
            results_buffer_capacity_[bufIdx] = resultsSize;
        } else {
            resultsBuffer = (__bridge id<MTLBuffer>)results_buffer_[bufIdx];
        }

        // Ray count buffer (single, small, persistent)
        id<MTLBuffer> rayCountBuffer = nil;
        if (!raycount_buffer_) {
            rayCountBuffer = [device newBufferWithLength:rayCountSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!rayCountBuffer) {
                std::cerr << "[METAL ERROR] Failed to create ray count buffer" << std::endl;
                dispatch_semaphore_signal(semaphore);
                return;
            }
            [rayCountBuffer retain];
            raycount_buffer_ = (__bridge void*)rayCountBuffer;
        } else {
            rayCountBuffer = (__bridge id<MTLBuffer>)raycount_buffer_;
        }

        // STEP 4: Upload data to GPU buffers
        std::memcpy([raysBuffer contents], rays, raysSize);
        std::memcpy([trianglesBuffer contents], triangles, trianglesSize);
        uint32_t ray_count_u32 = static_cast<uint32_t>(ray_count);
        std::memcpy([rayCountBuffer contents], &ray_count_u32, rayCountSize);
        std::memset([resultsBuffer contents], 0, resultsSize);

        // STEP 5: Create command buffer and encoder
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];

        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:raysBuffer offset:0 atIndex:0];
        [encoder setBuffer:(__bridge id<MTLBuffer>)bvh_buffer_ offset:0 atIndex:1];
        [encoder setBuffer:trianglesBuffer offset:0 atIndex:2];
        [encoder setBuffer:rayCountBuffer offset:0 atIndex:3];
        [encoder setBuffer:resultsBuffer offset:0 atIndex:4];

        // STEP 6: Dispatch GPU work
        const int RAYS_PER_BATCH = 8;
        int num_batches = (ray_count + RAYS_PER_BATCH - 1) / RAYS_PER_BATCH;
        MTLSize gridSize = MTLSizeMake(num_batches, 1, 1);
        NSUInteger threadsPerGroup = 64;
        NSUInteger maxThreadsPerGroup = pipeline.maxTotalThreadsPerThreadgroup;
        if (threadsPerGroup > maxThreadsPerGroup) {
            threadsPerGroup = maxThreadsPerGroup;
        }
        MTLSize threadgroupSize = MTLSizeMake(threadsPerGroup, 1, 1);

        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];
        [encoder endEncoding];

        // Track upload bytes
        metrics.gpu_upload_bytes += raysSize + trianglesSize;
        metrics.gpu_download_bytes += resultsSize;

        // STEP 7: ADD COMPLETION HANDLER (ASYNC!)
        // Copy callback data to heap (stack will be gone when handler runs)
        float* results_copy = new float[ray_count];
        CompletionCallback callback_copy = callback;
        void* user_data_copy = user_data;
        int buffer_idx_copy = bufIdx;  // Copy for block capture

        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            // GPU FINISHED! This runs on Metal's completion thread (background thread)

            // Check for errors
            if (cb.status == MTLCommandBufferStatusError) {
                std::cerr << "[METAL ERROR] Async command buffer execution failed" << std::endl;
                if (cb.error) {
                    std::cerr << "  Error: " << [[cb.error localizedDescription] UTF8String] << std::endl;
                }
            } else {
                // Copy results from GPU buffer
                std::memcpy(results_copy, [resultsBuffer contents], resultsSize);

                // Call user's completion callback (on background thread!)
                if (callback_copy) {
                    callback_copy(results_copy, ray_count, user_data_copy);
                }
            }

            // Clean up heap-allocated data
            delete[] results_copy;

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
// GPU LIGHTING COMPUTE (Option B)
// =========================================================================

void MetalComputeBridge::compute_lighting(
    const PixelData* pixels,
    int pixel_count,
    const LightData* lights,
    int light_count,
    const float* shadow_results,
    float* lighting_output)
{
    std::cout << "[GPU LIGHTING ENTRY] pixels=" << pixel_count << " lights=" << light_count << "\n" << std::flush;

    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "[METAL ERROR] compute_lighting called before initialize()" << std::endl;
            return;
        }

        if (pixel_count <= 0 || light_count <= 0) {
            std::cout << "[GPU LIGHTING] Early return: pixel_count=" << pixel_count << " light_count=" << light_count << "\n" << std::flush;
            return;
        }

        std::cout << "[GPU LIGHTING] Creating buffers...\n" << std::flush;

        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)lighting_queue_;  // Use separate queue!
        id<MTLLibrary> library = (__bridge id<MTLLibrary>)library_;

        // STEP 1: Get or create cached lighting compute pipeline
        id<MTLComputePipelineState> pipeline = nil;

        if (!lighting_compute_pipeline_) {
            // First call - create and cache pipeline
            NSString* functionName = @"compute_lighting";
            id<MTLFunction> computeFunction = [library newFunctionWithName:functionName];

            if (!computeFunction) {
                std::cerr << "[METAL ERROR] Failed to find compute function: "
                          << [functionName UTF8String] << std::endl;
                return;
            }

            NSError* error = nil;
            pipeline = [device newComputePipelineStateWithFunction:computeFunction error:&error];

            if (!pipeline) {
                std::cerr << "[METAL ERROR] Failed to create lighting compute pipeline" << std::endl;
                if (error) {
                    std::cerr << "  Error: " << [[error localizedDescription] UTF8String] << std::endl;
                }
                return;
            }

            // Cache for future calls
            [pipeline retain];
            lighting_compute_pipeline_ = (__bridge void*)pipeline;
            std::cout << "[GPU OPTIMIZE] Lighting compute pipeline cached" << std::endl;
        } else {
            // Reuse cached pipeline
            pipeline = (__bridge id<MTLComputePipelineState>)lighting_compute_pipeline_;
        }

        // STEP 2: Get or create persistent buffers
        const NSUInteger pixelsSize = sizeof(PixelData) * pixel_count;
        const NSUInteger lightsSize = sizeof(LightData) * light_count;
        const NSUInteger shadowResultsSize = sizeof(float) * pixel_count * light_count;
        const NSUInteger lightingOutputSize = sizeof(float) * pixel_count;
        const NSUInteger pixelCountSize = sizeof(uint32_t);
        const NSUInteger lightCountSize = sizeof(uint32_t);

        // Pixels buffer
        id<MTLBuffer> pixelsBuffer = nil;
        if (!pixels_buffer_ || pixels_buffer_capacity_ < pixelsSize) {
            if (pixels_buffer_) {
                CFBridgingRelease(pixels_buffer_);
            }
            pixelsBuffer = [device newBufferWithLength:pixelsSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!pixelsBuffer) {
                std::cerr << "[METAL ERROR] Failed to create pixels buffer" << std::endl;
                return;
            }
            [pixelsBuffer retain];
            pixels_buffer_ = (__bridge void*)pixelsBuffer;
            pixels_buffer_capacity_ = pixelsSize;
        } else {
            pixelsBuffer = (__bridge id<MTLBuffer>)pixels_buffer_;
        }

        // Lights buffer
        id<MTLBuffer> lightsBuffer = nil;
        if (!lights_buffer_ || lights_buffer_capacity_ < lightsSize) {
            if (lights_buffer_) {
                CFBridgingRelease(lights_buffer_);
            }
            lightsBuffer = [device newBufferWithLength:lightsSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!lightsBuffer) {
                std::cerr << "[METAL ERROR] Failed to create lights buffer" << std::endl;
                return;
            }
            [lightsBuffer retain];
            lights_buffer_ = (__bridge void*)lightsBuffer;
            lights_buffer_capacity_ = lightsSize;
        } else {
            lightsBuffer = (__bridge id<MTLBuffer>)lights_buffer_;
        }

        // Lighting output buffer
        id<MTLBuffer> lightingOutputBuffer = nil;
        if (!lighting_output_buffer_ || lighting_output_buffer_capacity_ < lightingOutputSize) {
            if (lighting_output_buffer_) {
                CFBridgingRelease(lighting_output_buffer_);
            }
            lightingOutputBuffer = [device newBufferWithLength:lightingOutputSize options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
            if (!lightingOutputBuffer) {
                std::cerr << "[METAL ERROR] Failed to create lighting output buffer" << std::endl;
                return;
            }
            [lightingOutputBuffer retain];
            lighting_output_buffer_ = (__bridge void*)lightingOutputBuffer;
            lighting_output_buffer_capacity_ = lightingOutputSize;
        } else {
            lightingOutputBuffer = (__bridge id<MTLBuffer>)lighting_output_buffer_;
        }

        // Shadow results buffer (temporary, not cached)
        id<MTLBuffer> shadowResultsBuffer = [device newBufferWithLength:shadowResultsSize
                                                                 options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
        if (!shadowResultsBuffer) {
            std::cerr << "[METAL ERROR] Failed to create shadow results buffer" << std::endl;
            return;
        }

        // Pixel and light count buffers (small, create each time)
        id<MTLBuffer> pixelCountBuffer = [device newBufferWithLength:pixelCountSize
                                                              options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];
        id<MTLBuffer> lightCountBuffer = [device newBufferWithLength:lightCountSize
                                                              options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked];

        if (!pixelCountBuffer || !lightCountBuffer) {
            std::cerr << "[METAL ERROR] Failed to create count buffers" << std::endl;
            return;
        }

        // STEP 3: Upload data to GPU
        std::memcpy([pixelsBuffer contents], pixels, pixelsSize);
        std::memcpy([lightsBuffer contents], lights, lightsSize);
        std::memcpy([shadowResultsBuffer contents], shadow_results, shadowResultsSize);

        uint32_t pixel_count_u32 = static_cast<uint32_t>(pixel_count);
        uint32_t light_count_u32 = static_cast<uint32_t>(light_count);
        std::memcpy([pixelCountBuffer contents], &pixel_count_u32, pixelCountSize);
        std::memcpy([lightCountBuffer contents], &light_count_u32, lightCountSize);

        // Initialize output to 0.0
        std::memset([lightingOutputBuffer contents], 0, lightingOutputSize);

        // STEP 4: Create command buffer and encoder
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        if (!commandBuffer) {
            std::cerr << "[METAL ERROR] Failed to create command buffer" << std::endl;
            return;
        }

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (!encoder) {
            std::cerr << "[METAL ERROR] Failed to create compute encoder" << std::endl;
            return;
        }

        // STEP 5: Set pipeline and bind buffers
        [encoder setComputePipelineState:pipeline];

        // Bind buffers to match shader signature:
        // compute_lighting(
        //     device const PixelData* pixels [[buffer(0)]],
        //     device const LightData* lights [[buffer(1)]],
        //     device const float* shadow_results [[buffer(2)]],
        //     device float* lighting_output [[buffer(3)]],
        //     constant uint& pixel_count [[buffer(4)]],
        //     constant uint& light_count [[buffer(5)]]
        // )
        [encoder setBuffer:pixelsBuffer offset:0 atIndex:0];
        [encoder setBuffer:lightsBuffer offset:0 atIndex:1];
        [encoder setBuffer:shadowResultsBuffer offset:0 atIndex:2];
        [encoder setBuffer:lightingOutputBuffer offset:0 atIndex:3];
        [encoder setBuffer:pixelCountBuffer offset:0 atIndex:4];
        [encoder setBuffer:lightCountBuffer offset:0 atIndex:5];

        // STEP 6: Dispatch compute threads (one thread per pixel)
        MTLSize gridSize = MTLSizeMake(pixel_count, 1, 1);

        // Threadgroup size: 64 threads per group
        NSUInteger threadsPerGroup = 64;
        NSUInteger maxThreadsPerGroup = pipeline.maxTotalThreadsPerThreadgroup;
        if (threadsPerGroup > maxThreadsPerGroup) {
            threadsPerGroup = maxThreadsPerGroup;
        }

        MTLSize threadgroupSize = MTLSizeMake(threadsPerGroup, 1, 1);

        std::cout << "[GPU LIGHTING] Dispatching threads: " << pixel_count << " (groups: " << threadsPerGroup << ")\n" << std::flush;
        [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];

        // STEP 7: Submit and wait
        std::cout << "[GPU LIGHTING] Ending encoder...\n" << std::flush;
        [encoder endEncoding];
        std::cout << "[GPU LIGHTING] Committing command buffer...\n" << std::flush;
        [commandBuffer commit];
        std::cout << "[GPU LIGHTING] Waiting for GPU completion...\n" << std::flush;
        [commandBuffer waitUntilCompleted];
        std::cout << "[GPU LIGHTING] GPU completed!\n" << std::flush;

        // Check for errors
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            std::cerr << "[METAL ERROR] Lighting command buffer execution failed" << std::endl;
            if (commandBuffer.error) {
                std::cerr << "  Error: "
                          << [[commandBuffer.error localizedDescription] UTF8String]
                          << std::endl;
            }
            return;
        }

        // STEP 8: Download lighting results
        std::memcpy(lighting_output, [lightingOutputBuffer contents], lightingOutputSize);
    }
}

// Wait for all pending GPU work to complete (for safe shutdown)
void MetalComputeBridge::wait_for_completion() {
    if (!buffer_semaphore_) {
        std::cout << "[METAL] No semaphore, skipping wait\n";
        return;  // No semaphore, nothing to wait for
    }

    @autoreleasepool {
        std::cout << "[METAL] Waiting for all GPU work to complete...\n" << std::flush;

        // Bridge the semaphore pointer
        dispatch_semaphore_t semaphore = (__bridge dispatch_semaphore_t)buffer_semaphore_;

        if (!semaphore) {
            std::cerr << "[METAL ERROR] Semaphore bridge returned null!\n" << std::flush;
            return;
        }

        std::cout << "[METAL] Semaphore bridged successfully, waiting for all " << Optimizations::GPU_BUFFER_SLOTS << " slots...\n" << std::flush;

        // Use timeout instead of DISPATCH_TIME_FOREVER to prevent system freeze
        constexpr int64_t TIMEOUT_NS = 5LL * 1000000000LL;  // 5 seconds
        int slots_acquired = 0;

        // Acquire all GPU_BUFFER_SLOTS semaphore slots - this ensures all GPU work has completed
        // (When all slots are available, no buffers are in-flight)
        for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, TIMEOUT_NS);
            long result = dispatch_semaphore_wait(semaphore, timeout);

            if (result != 0) {
                std::cerr << "[METAL ERROR] Timeout waiting for slot " << (i + 1)
                          << " - GPU may be stuck! Aborting to prevent freeze.\n" << std::flush;
                // Release slots we acquired
                for (int j = 0; j < slots_acquired; j++) {
                    dispatch_semaphore_signal(semaphore);
                }
                return;
            }

            slots_acquired++;
            std::cout << "[METAL] Got slot " << (i + 1) << "...\n" << std::flush;
        }

        std::cout << "[METAL] Got all " << Optimizations::GPU_BUFFER_SLOTS << " slots, releasing them back...\n" << std::flush;
        // Release them back immediately
        for (int i = 0; i < Optimizations::GPU_BUFFER_SLOTS; i++) {
            dispatch_semaphore_signal(semaphore);
        }

        std::cout << "[METAL] All GPU work completed\n" << std::flush;
    }
}

} // namespace Logosphere
