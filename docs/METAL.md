# Metal GPU Rendering: Lessons and Architecture

## Rendering Pipeline (Deferred, per frame)

```
Pass 1:   G-buffer rasterization (compute shader, box particles to world_pos + normal + color + ID)
Pass 2:   Shadow rays (Metal RT closest-hit, 1 ray/pixel/light, outputs lux + blocker_distance)
Pass 2.5: Penumbra post-process (blocker neighborhood search + Gaussian blur on shadow buffer)
Pass 2.5: GI indirect rays (16 cosine-weighted hemisphere rays/pixel, Metal RT closest-hit)
Pass 2.6: GI denoise (5x5 bilateral edge-preserving filter)
Pass 3:   Apply lighting (composite: shadow lux + GI + base color + tone mapping)
Present:  MPS bilinear scale (render res to drawable res) + presentDrawable
```

Each pass is a separate MTLCommandBuffer on a serial queue. GPU timestamps via `cb.GPUStartTime`/`cb.GPUEndTime` in completion handlers.

## Key Lesson: Software BVH vs Metal RT Hardware Acceleration (2026-04-07)

### The Problem
GI indirect rays took 29ms/frame. The kernel (`compute_indirect_rays`) traced 16 rays per pixel through a CPU-built BVH using manual stack-based traversal in the shader: 30 lines of while-loop, stack push/pop, AABB tests, triangle intersection.

### The Discovery
The shadow kernel (`trace_shadows_deterministic`) already used Metal RT hardware acceleration (`primitive_acceleration_structure` + `intersector<triangle_data>`) on THE SAME triangle data. The GI kernel was never migrated when Metal RT was added for shadows.

### The Fix
Replaced the software BVH traversal with 5 lines:
```metal
intersector<triangle_data> gi_intersector;
gi_intersector.accept_any_intersection(false);  // closest-hit for color lookup
ray gi_ray;
gi_ray.origin = ray_origin;
gi_ray.direction = world_dir;
gi_ray.min_distance = 0.001f;
gi_ray.max_distance = params.max_distance;
auto result = gi_intersector.intersect(gi_ray, accel);
if (result.type != intersection_type::none) {
    closest_t = result.distance;
    closest_tri_idx = (int)result.primitive_id;
}
```

CPU dispatch: bind `acceleration_structure_` at buffer index 8 instead of `bvhNodesBuffer`. Shift subsequent buffer indices down by 2 (removed bvh_nodes and bvh_node_count).

### Result
29ms to 0.75ms. 39x speedup. Zero visual difference.

### Why It Works
Metal RT uses dedicated hardware intersection units on Apple Silicon. The M4 Max can sustain ~1 billion ray-triangle intersections per second. Software BVH traversal in a compute shader has divergent branching (stack push/pop), random memory access patterns (node traversal), and no hardware acceleration for AABB/triangle tests. The hardware intersector handles all of this in fixed-function silicon.

### Key Takeaway
If you're doing ray tracing through a BVH in a compute shader on Apple Silicon, check if Metal RT `primitive_acceleration_structure` + `intersector<>` can replace it. The acceleration structure build from vertex data is a one-time cost per frame (~2ms for 4764 triangles). The per-ray intersection speedup is 10-40x.

## Metal RT API Reference

### Acceleration Structure Build (CPU side, gpu_rasterizer.mm)
```objc
// Vertex buffer: packed float3 per vertex, 9 floats per triangle
MTLAccelerationStructureTriangleGeometryDescriptor* geometryDesc = ...;
geometryDesc.vertexBuffer = vertexBuffer;
geometryDesc.vertexStride = sizeof(float) * 3;
geometryDesc.triangleCount = triangle_count;

MTLPrimitiveAccelerationStructureDescriptor* accelDesc = ...;
accelDesc.geometryDescriptors = @[geometryDesc];

// Build (async on command buffer)
[encoder buildAccelerationStructure:accelStruct descriptor:accelDesc scratchBuffer:scratch ...];
```

### Intersection (GPU shader side)
```metal
// Kernel parameter
primitive_acceleration_structure accel [[buffer(N)]]

// In kernel
intersector<triangle_data> inter;
inter.accept_any_intersection(false);  // false = closest-hit, true = any-hit (faster for shadows)

ray r;
r.origin = ...;
r.direction = ...;
r.min_distance = 0.001f;
r.max_distance = max_dist;

auto result = inter.intersect(r, accel);
// result.type: intersection_type::none or intersection_type::triangle
// result.distance: hit distance
// result.primitive_id: triangle index in the vertex buffer (maps to original triangle array)
```

### CPU Binding
```objc
// Bind acceleration structure (NOT a regular buffer)
[encoder setAccelerationStructure:accel atBufferIndex:N];

// Triangle data still needed for color/normal lookup after hit
[encoder setBuffer:trianglesBuffer offset:0 atIndex:N+1];
```

### any-hit vs closest-hit
- `accept_any_intersection(true)`: Returns the FIRST hit, not the closest. Much faster (can skip BVH subtrees). Use for shadow occlusion queries ("is anything blocking the light?").
- `accept_any_intersection(false)`: Traverses full BVH to find the closest surface. Required when you need to read the hit surface's properties (color, normal). Use for GI bounces.

## GI Temporal Accumulation

### Architecture
Per-pixel EMA (Exponential Moving Average) with a persistent `gi_temporal` buffer:
- Frame 0: no history, output raw sample
- Frames 1-64: blend `mix(history, current, blend_factor)` where blend ramps from 50% down to 5%
- Frame 64+: frozen (Halton sequence repeats, no new information)

### Camera Movement
On camera pan, the temporal buffer is screen-space keyed (pixel index). Camera movement shifts world positions under fixed pixel indices, creating ghost artifacts. Fix: detect camera movement via `shadow_culling_camera_x/y` delta, clear temporal buffer and reset frame counter.

### Noise Management
16 rays at 200x intensity = visible Monte Carlo noise on early frames. Mitigations:
- Per-ray radiance clamped to 50 (prevents firefly dots from hot surfaces near lights)
- 8-frame fade-in: `gi_fade = clamp(frame_index / 8.0, 0, 1)` suppresses noisy output until convergence builds
- 5x5 bilateral spatial denoiser (edge-preserving, normal-weighted)

## Penumbra System

### Before: BLOCKER_GRADIENT Kernel C (20ms)
For each pixel: read blocker_distance from shadow pass. Shadowed pixels compute penumbra_width from blocker distance. Lit pixels scan a 65x65 neighborhood to find nearby shadow influence. Then 2D Gaussian blur with computed width. Two O(N²) operations per pixel.

### After: JFA + Separable Blur (0.6ms, 32x speedup, 2026-04-07)
1. **Seed kernel**: `blocker_distance * light_size * BLOCKER_PENUMBRA_SCALE` → per-pixel penumbra_width
2. **JFA propagation**: 6 passes (step 32,16,8,4,2,1), 9-tap each. Distance-attenuated: `effective = neighbor_pw - dist`
3. **Separable blur H+V**: 1D Gaussian with G-buffer `particle_id` edge detection (prevents cross-object bleeding)

Key tuning: `BLOCKER_PENUMBRA_SCALE = 2.0` (was 10.0, caused over-blur with large blockers like trees).

## Performance Profiling

### GPU Timestamps
```objc
[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
    if (cb.GPUStartTime > 0 && cb.GPUEndTime > 0) {
        double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
        NSLog(@"[GPU_TIMESTAMP] Pass X: %.2f ms", ms);
    }
}];
```
Each pass needs its own command buffer for independent timing. Sampled every 60 frames to reduce overhead.

### Metal HUD
`MTL_HUD_ENABLED=1 ./build/eden/eden` shows real GPU time, FPS, and memory. The CPU-side "pipeline total" measurement is WRONG because it measures encode time, not execution time. Always use the Metal HUD or GPU timestamps for real GPU cost.

### CPU vs GPU Timing Trap
CPU-side measurements (chrono around dispatch) show encode time (~1-3ms). Actual GPU execution (87ms) is invisible to the CPU because command buffers execute asynchronously. The Metal HUD "GPU" line shows true execution time. GPU timestamps in completion handlers also show true time.

### Shader Compilation Trap
Metal shader compilation errors are SILENT at runtime. The build system compiles .metal to .air to .metallib. If compilation fails, the old .metallib is used. Always check `make` output for `penumbra.air Error` or similar. A "working" shader may be running stale code.

## Buffer Management

### Double Buffering
2 buffer slots (`GPU_BUFFER_SLOTS = 2`, reduced from 3 on 2025-03-07 to save ~250 MB GPU memory), indexed by frame. Semaphore prevents CPU from getting more than 1 frame ahead of GPU. Each slot has its own shadow results, G-buffer, GI results, etc.

### Resolution Scaling
Shadow resolution can differ from G-buffer resolution via `SHADOW_RESOLUTION_SCALE`. Pass 3 does bilinear upsampling when they differ. Currently 1.0 (full resolution).

### Memory (398 particles, 1600x1051)
- G-buffer: 60MB (36 bytes/pixel)
- Framebuffer: 6.7MB
- Shadow results: 6.7MB
- GI results: 26.8MB (float4/pixel)
- Total: ~2.6GB reported by Metal HUD (includes triple buffering overhead)
