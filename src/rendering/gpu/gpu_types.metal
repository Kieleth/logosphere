#ifndef GPU_TYPES_METAL
#define GPU_TYPES_METAL

// =========================================================================
// GPU TYPE DEFINITIONS - Single Source of Truth
// =========================================================================
// All data structures shared across Metal GPU shaders
// CRITICAL: Must match C++ layouts in metal_compute_bridge.h exactly
//
// ALIGNMENT RULES:
// - Use packed_float3 (12 bytes) NOT float3 (16 bytes padded)
// - Add explicit _padding fields to match C++ alignas(16)
// - Total struct size must be multiple of 16 bytes
//
// LESSON LEARNED (2025-10-06):
// Metal's float3 is automatically padded to 16 bytes by compiler
// CPU's float array[3] is 12 bytes
// Mismatch caused zero lighting bug - use packed_float3!

// =========================================================================
// SHADOW RAY TRACING TYPES
// =========================================================================

// Ray for shadow testing
// Matches: metal_compute_bridge.h:ShadowRay
struct Ray {
    packed_float3 origin;     // Ray origin in world space (12 bytes)
    float max_distance;       // Maximum ray distance to light (4 bytes)
                              // CRITICAL: Prevents "mirror shadow" bug
                              // See shadow_ray.metal:61-116 for explanation
    packed_float3 direction;  // Ray direction, must be normalized (12 bytes)
    float _padding1;          // Explicit padding for 16-byte alignment (4 bytes)
    // Total: 32 bytes
};

// Triangle geometry (3 vertices + base color for indirect lighting)
// Matches: metal_compute_bridge.h:ShadowTriangle
struct Triangle {
    packed_float3 v0;  // Vertex 0 (12 bytes)
    float _padding0;   // 4 bytes
    packed_float3 v1;  // Vertex 1 (12 bytes)
    float _padding1;   // 4 bytes
    packed_float3 v2;  // Vertex 2 (12 bytes)
    float _padding2;   // 4 bytes
    uchar4 base_color; // Surface color BGRA (4 bytes) - for indirect ray hit color lookup
    float _padding3[3]; // 12 bytes padding to reach 64 bytes (16-byte aligned)
    // Total: 64 bytes
};

// BVH acceleration structure node
// Matches: triangle_bvh.h:GPUBVHNode
struct BVHNode {
    packed_float3 bbox_min;  // Bounding box minimum (12 bytes)
    float _pad0;             // 4 bytes
    packed_float3 bbox_max;  // Bounding box maximum (12 bytes)
    float _pad1;             // 4 bytes
    int left_child;          // Left child node index, -1 if leaf (4 bytes)
    int right_child;         // Right child node index, -1 if leaf (4 bytes)
    int triangle_idx;        // Triangle index if leaf, -1 if internal (4 bytes)
    int _pad2;               // 4 bytes
    // Total: 48 bytes
};

// =========================================================================
// LIGHTING TYPES
// =========================================================================

// Pixel data for lighting calculation
// Matches: metal_compute_bridge.h:PixelData
struct PixelData {
    packed_float3 position;  // World space position (12 bytes)
    float _padding0;         // 4 bytes
    packed_float3 normal;    // World space surface normal (12 bytes)
    float _padding1;         // 4 bytes
    // Total: 32 bytes
};

// Light source data
// Matches: metal_compute_bridge.h:LightData
struct LightData {
    packed_float3 position;      // Light position in world space (12 bytes)
    float emission_strength;     // Light intensity in lumens (4 bytes)
    float emission_radius;       // Maximum light distance (4 bytes)
    float light_size;            // Physical size of light source in meters (4 bytes)
    packed_float3 emission_color; // RGB color (0-1, default white) (12 bytes)
    float _padding;              // 4 bytes to reach 48-byte total
    // Total: 48 bytes
};

// DDGI parameters (Dynamic Diffuse Global Illumination)
struct DDGIParams {
    packed_float3 grid_origin;    // World-space origin of probe grid (12 bytes)
    float probe_spacing;          // Distance between probes (4 bytes)
    packed_uint3 grid_dimensions; // Probes in X, Y, Z (12 bytes)
    uint rays_per_probe;          // Rays per probe per frame (4 bytes)
    uint frame_index;             // For deterministic rotation (4 bytes)
    float hysteresis;             // Temporal blend 0.02-0.05 (4 bytes)
    float normal_bias;            // Self-intersection avoidance (4 bytes)
    float intensity;              // Indirect lighting multiplier (4 bytes)
    // Total: 48 bytes
};

// SSAO parameters (matches C++ struct in gpu_rasterizer.mm)
struct SSAOParams {
    uint sample_count;       // Hemisphere samples per pixel
    float screen_radius;     // Screen-space sample radius (pixels)
    float world_radius;      // Max world-space occlusion distance (meters)
    float bias;              // Self-occlusion prevention distance
    float intensity;         // AO darkening strength
    float _padding[3];       // Pad to 32 bytes
};

// =========================================================================
// RASTERIZATION TYPES (GPU_RENDERING_REFACTOR_II.md Phase II)
// =========================================================================

// Projected triangle for GPU rasterization
// Matches: camera_system.h:ProjectedTriangle (simplified for GPU)
struct ProjectedTriangle {
    float2 screen_corners[3];  // 2D screen positions (24 bytes)
    float _padding0[2];        // Alignment (8 bytes)
    float3 world_corners[3];   // 3D world positions (48 bytes)
    int2 bbox_min;             // Bounding box min (8 bytes)
    int2 bbox_max;             // Bounding box max (8 bytes)
    packed_float3 normal;      // Surface normal (12 bytes)
    float _padding1;           // Alignment (4 bytes)
    // Total: 112 bytes (aligned to 16)
};

// Edge coefficients for GPU
// Matches: rasterization_math.h:EdgeCoefficients
struct EdgeCoefficientsGPU {
    float a, b, c;             // Edge equation coefficients (12 bytes)
    float _padding;            // Alignment (4 bytes)
    // Total: 16 bytes
};

// =========================================================================
// STEP 5: MULTIPLE TRIANGLES
// =========================================================================

// Triangle for GPU rasterization with depth buffer
// Contains screen-space 2D coords + depth for each vertex, plus color
struct TriangleGPU {
    // Vertex 0 (screen x, screen y, depth z)
    float x0, y0, z0;          // 12 bytes
    float _padding0;           // 4 bytes

    // Vertex 1
    float x1, y1, z1;          // 12 bytes
    float _padding1;           // 4 bytes

    // Vertex 2
    float x2, y2, z2;          // 12 bytes
    float _padding2;           // 4 bytes

    // Color (RGBA 0-1 range)
    float r, g, b, a;          // 16 bytes

    // Total: 64 bytes (aligned to 16)
};

// =========================================================================
// STEP 7: LIGHTING INTEGRATION
// =========================================================================

// Extended triangle with lighting data for GPU rasterization + lighting
// Contains everything from TriangleGPU plus world positions and normals
struct TriangleLit {
    // Screen-space vertices (x, y, depth_z)
    float x0, y0, z0;          // 12 bytes
    float smooth_enable;       // 4 bytes - 0 = flat shading, 1 = analytic smooth

    float x1, y1, z1;          // 12 bytes
    float _padding1;           // 4 bytes

    float x2, y2, z2;          // 12 bytes
    float _padding2;           // 4 bytes

    // Base color (RGBA 0-1 range) - material color before lighting
    float r, g, b, a;          // 16 bytes

    // World-space positions for lighting calculation
    packed_float3 world_pos0;  // 12 bytes
    float _padding3;           // 4 bytes

    packed_float3 world_pos1;  // 12 bytes
    float _padding4;           // 4 bytes

    packed_float3 world_pos2;  // 12 bytes
    float _padding5;           // 4 bytes

    // Surface normal (shared across triangle, for flat shading)
    packed_float3 normal;      // 12 bytes
    int particle_id;           // 4 bytes (for debug/future features)
    // Total so far: 160 bytes

    // Material properties for SSGI
    float roughness;           // 4 bytes - 0=mirror, 1=matte
    packed_float3 smooth_center; // 12 bytes - sphere centre (was padding)
};

// =========================================================================
// INSTRUMENTATION TYPES - Performance profiling (2025-10-17)
// =========================================================================

// Per-pixel statistics for BVH traversal profiling
// Captures detailed metrics about shadow ray traversal patterns
struct BVHTraversalStats {
    uint nodes_visited;        // Total BVH nodes accessed
    uint nodes_aabb_tested;    // Nodes that passed AABB test
    uint triangles_tested;     // Triangle intersection tests performed
    uint max_stack_depth;      // Maximum traversal stack depth reached
    uint cache_line_changes;   // Estimated cache line transitions
    uint early_terminations;   // Rays that hit and terminated early
    uint rays_traced;          // Number of rays from this pixel
    uint _padding;             // Alignment to 32 bytes
    // Total: 32 bytes
};

// Aggregated statistics for entire dispatch
struct BVHGlobalStats {
    metal::atomic_uint total_nodes_visited;
    metal::atomic_uint total_triangles_tested;
    metal::atomic_uint total_early_terminations;
    metal::atomic_uint max_traversal_depth;
    metal::atomic_uint coherency_score;  // Adjacent rays visiting same nodes
    uint _padding[3];
    // Total: 32 bytes
};

// =========================================================================
// ENTITY BVH TYPES (Entity-level culling with directional groups)
// =========================================================================
// Two-level hierarchy:
//   Level 1: EntityBVH nodes - coarse culling by entity AABB
//   Level 2: DirectionalGroups - triangles grouped by facing direction
//
// Key optimization: Directional culling skips ~50% of triangles per entity
// because surfaces facing AWAY from ray origin are occluded by entity's
// own opaque geometry.

// EntityBVH node for entity-level culling
// Matches: entity_bvh.h:EntityBVHNode
struct EntityBVHNode {
    packed_float3 bbox_min;     // Entity AABB minimum (12 bytes)
    int left_child;             // Left child index, -1 if leaf (4 bytes)
    packed_float3 bbox_max;     // Entity AABB maximum (12 bytes)
    int right_child;            // Right child index, -1 if leaf (4 bytes)
    int entity_id;              // KG EntityID for skip checks (4 bytes)
    int dir_group_start;        // Index into DirectionalGroup array (4 bytes)
    int dir_group_count;        // Number of directional groups (4 bytes)
    int _padding;               // Alignment to 48 bytes (4 bytes)
    // Total: 48 bytes
};

// Directional group - triangles grouped by facing direction
// Matches: entity_bvh.h:DirectionalGroup
struct DirectionalGroup {
    packed_float3 avg_normal;   // Average normal of this group (12 bytes)
    int tri_start;              // Index into triangle array (4 bytes)
    int tri_count;              // Number of triangles in group (4 bytes)
    int _padding[3];            // Alignment to 32 bytes (12 bytes)
    // Total: 32 bytes
};

// =========================================================================
// VISION CONE POST-PROCESS (Pass 4)
// =========================================================================
// Parameters for vision cone effect - darkens pixels outside viewer's FOV
// Used for fog-of-war and stealth game mechanics
//
// The vision cone is defined in 2D (X-Y plane) since games typically
// use top-down or isometric views where vertical FOV is less important

// Number of angular bins for the LOS occlusion mask. Each bin spans
// (2 * half_fov) / VISION_CONE_OCCLUSION_BINS radians of the cone.
// 256 bins across a 180° cone ≈ 0.7° per bin — fine enough that
// thin walls grazing parallel to the ray still get sampled. (At
// 64 bins the angular gap between sampled rays is ~2.8°, so a
// 15 cm wall 20 m away — angular extent ~0.4° — could fall between
// two bin centers and produce visible "see-through" slivers.)
// Mirrored as a constexpr in the engine header so the C++ struct
// stays byte-identical (now 48 + 16 + 1024 = 1088 bytes).
#define VISION_CONE_OCCLUSION_BINS 256

struct VisionConeParams {
    float viewer_x;        // Viewer world position X (4 bytes)
    float viewer_y;        // Viewer world position Y (4 bytes)
    float look_direction;  // Direction viewer is facing (radians, 0 = +Y/North) (4 bytes)
    float half_fov;        // Half of field of view angle (radians) (4 bytes)
    float range;           // Maximum vision distance (meters) (4 bytes)
    float inner_falloff;   // Start of edge softness (0.0-1.0 of half_fov) (4 bytes)
    float darkness;        // How dark outside cone (0.0 = black, 1.0 = fully visible) (4 bytes)
    uint enabled;          // 0 = disabled (pass-through), 1 = enabled (4 bytes)
    float focus_x;         // Focus point world X (mouse position) (4 bytes)
    float focus_y;         // Focus point world Y (mouse position) (4 bytes)
    float focus_radius;    // Radius of sharp focus area in meters (4 bytes)
    float padding;         // Alignment padding (4 bytes)
    // --- LOS occlusion mask. The CPU raycasts N rays from the
    //     viewer across the cone and writes the distance-to-nearest-
    //     occluder into occlusion_distance[bin]. The shader maps a
    //     pixel's bearing back to a bin and darkens the pixel if its
    //     distance from the viewer exceeds the bin's value.
    //     occlusion_count = 0 disables the mask entirely (cone falls
    //     back to angle+range only — legacy behavior).
    int occlusion_count;   // 0 or VISION_CONE_OCCLUSION_BINS (4 bytes)
    int occlusion_pad0;    // (4 bytes)
    int occlusion_pad1;    // (4 bytes)
    int occlusion_pad2;    // (4 bytes)
    float occlusion_distance[VISION_CONE_OCCLUSION_BINS];  // 1024 bytes
    // --- Vision-memory grid (world-space "what I just saw" buffer).
    //     Buffer itself lives at MTL bind index 5; this struct
    //     just carries its layout + tuning. memory_enabled = 0
    //     disables the sample-and-blend block in the shader.
    int   memory_enabled;       // 0/1 (4 bytes)
    int   memory_width;         // grid columns (4 bytes)
    int   memory_height;        // grid rows (4 bytes)
    int   memory_pad;           // (4 bytes) align to 16
    float memory_origin_x;      // world coord of cell (0,0)'s SW corner (4 bytes)
    float memory_origin_y;      // (4 bytes)
    float memory_cell_size;     // world meters per cell side (4 bytes)
    float memory_dim;           // brightness multiplier for memory hits (4 bytes)
    // Total: 48 + 16 + 1024 + 32 = 1120 bytes (aligned)
};

// =========================================================================
// PARTICLE TRANSFORM DATA (For local-coordinate pattern calculation)
// =========================================================================

struct ParticleTransform {
    packed_float3 center;       // Particle center in world space (12 bytes)
    float _padding0;            // 4 bytes
    packed_float3 primary_axis; // Primary axis in world space (12 bytes)
    uint8_t pattern_id;         // Material pattern: 0=none, 1=wood, 2=stone (1 byte)
    uint8_t _padding1[3];       // Alignment padding (3 bytes)
    // Total: 32 bytes
};

// =========================================================================
// SSGI PARAMETERS (Screen-Space Global Illumination)
// =========================================================================
// Configuration for Pass 2.5 indirect lighting calculation
// gi_results buffer: float4 per pixel (RGB indirect + padding)
//
// Physics model: Lambertian diffuse + roughness-based spread
// - Rough surfaces (roughness=1.0) spread light widely (matte)
// - Smooth surfaces (roughness=0.0) have tighter reflection lobes

struct SSGIParams {
    uint sample_count;       // 4 bytes - Number of nearby pixels to sample (default: 16)
    float sample_radius;     // 4 bytes - Screen-space sampling radius in pixels (default: 32)
    float max_distance_sq;   // 4 bytes - Max world-space distance squared (default: 25 = 5m²)
    float gi_intensity;      // 4 bytes - Overall GI contribution strength (default: 0.3)
    // Total: 16 bytes (aligned)
};

#endif // GPU_TYPES_METAL
