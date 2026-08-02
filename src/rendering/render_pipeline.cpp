#include "logosphere/rendering/render_pipeline.h"
#include "core/telemetry.h"
#include <cstring>
#include <cstdlib>
#include <climits>
#include "../optimization_flags.h"
#include "../debug_control.h"
#include "../surface_cache.h"
#include "logosphere/rendering/triangle_bvh.h"              // For TriangleBVH (shadow BVH acceleration)
#include "logosphere/rendering/gpu/metal_compute_bridge.h"  // For ShadowTriangle type
#include "object_id.h"                 // For ObjectID encoding in picking pass
#include "logosphere/kg/kg_module.h"           // For entity grouping (particle → entity_id)
#include <algorithm>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <set>
#include <limits>
#include <unordered_map>
#include <cmath>
#include <thread>  // For parallel surface collection
#include <atomic>  // For flicker debug tracking

// Timing types (file scope for instrumentation)
using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

// =========================================================================
// SPATIAL SORTING HELPERS (BVH Cache Coherence Optimization)
// Morton codes (Z-order curve) for 3D spatial locality
// =========================================================================

/**
 * Expand a 10-bit integer into 30 bits by inserting 2 zeros after each bit
 * Used for Morton code calculation (interleaving X/Y/Z coordinates)
 *
 * Example: 0b1111111111 (10 bits) → 0b001001001001001001001001001001 (30 bits)
 * This allows interleaving 3 coordinates into a single 30-bit Morton code
 */
inline uint32_t expand_bits(uint32_t v) {
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

/**
 * Compute Morton code (Z-order curve index) for 3D position
 * Interleaves bits from X, Y, Z coordinates to create space-filling curve
 *
 * WHY: Nearby points in 3D space have nearby Morton codes
 * HOW: Expand each coordinate to 10 bits, interleave as: Z Y X Z Y X ...
 * RESULT: 30-bit code where spatial locality → numerical locality
 *
 * @param x, y, z: World coordinates (expected range: -100 to +100)
 * @return 30-bit Morton code (0 to ~1 billion)
 */
inline uint32_t morton_code_3d(float x, float y, float z) {
    // World bounds (adjust if your world is different)
    const float world_size = 200.0f;      // -100 to +100 range
    const uint32_t grid_size = 1024;      // 10-bit precision (2^10)

    // Normalize coordinates to [0, grid_size-1]
    auto clamp = [](float val, uint32_t min_val, uint32_t max_val) -> uint32_t {
        if (val < min_val) return min_val;
        if (val > max_val) return max_val;
        return static_cast<uint32_t>(val);
    };

    uint32_t gx = clamp((x + 100.0f) / world_size * grid_size, 0, grid_size - 1);
    uint32_t gy = clamp((y + 100.0f) / world_size * grid_size, 0, grid_size - 1);
    uint32_t gz = clamp((z + 100.0f) / world_size * grid_size, 0, grid_size - 1);

    // Interleave bits: ZYXZYXZYX... (X is LSB)
    return (expand_bits(gx) << 2) | (expand_bits(gy) << 1) | expand_bits(gz);
}

/**
 * RenderPipeline Implementation
 *
 * The complete rendering pipeline from particles to pixels.
 * Each stage is cleanly separated for maintainability and optimization.
 */

RenderPipeline::RenderPipeline()
    : debug_mode_(false)
    , show_lights_as_white_(false)
    , last_frame_stats_{}
    , surface_rasterizer_()
    , gpu_rasterizer_() {
}

// ============================================================================
// ASYNC GPU_PREP IMPLEMENTATION (2025-01-24)
// ============================================================================
// Prepare GPU data for deferred rendering - writes to indexed buffers
// This function can run async to overlap with GPU execution
//
// Prepares (all written to buffer_index):
// - gpu_triangles_[buffer_index]: Lit triangles for G-buffer
// - shadow_triangles_[buffer_index]: Shadow geometry from all particles
// - shadow_bvh_[buffer_index]: BVH acceleration structure
// - light_data_[buffer_index]: Packed light positions/properties
// - is_light_source_map_[buffer_index]: Entity ID → is_light mapping
//
// PERFORMANCE: This ~14ms CPU work can overlap with ~31ms GPU execution
// ============================================================================

// Minimum frames between shadow-BVH rebuilds. Default from
// Optimizations::SHADOW_BVH_MIN_REBUILD_FRAMES, overridable at runtime with
// LOGOSPHERE_BVH_REBUILD_FRAMES so the quality/perf trade can be swept without
// a rebuild. Read once.
// DIAGNOSTIC GATES: suppress either shadow acceleration structure at dispatch
// so a test can prove which one the Metal kernel actually traverses.
// shadow_rays_deferred.metal takes the entity path when
// (entity_node_count > 0 && dir_group_count > 0), and reaches the flat
// TriangleBVH only via `else if (bvh_node_count > 0)`. Zeroing a count here is
// exactly what the kernel branches on, so it selects the path without touching
// shader code. Both default true: production behaviour is unchanged.
static std::atomic<bool> g_pass_flat_shadow_bvh{true};
static std::atomic<bool> g_pass_entity_shadow_bvh{true};

namespace logosphere {
void set_flat_shadow_bvh_enabled(bool on)   { g_pass_flat_shadow_bvh.store(on, std::memory_order_relaxed); }
void set_entity_shadow_bvh_enabled(bool on) { g_pass_entity_shadow_bvh.store(on, std::memory_order_relaxed); }
bool get_flat_shadow_bvh_enabled()   { return g_pass_flat_shadow_bvh.load(std::memory_order_relaxed); }
bool get_entity_shadow_bvh_enabled() { return g_pass_entity_shadow_bvh.load(std::memory_order_relaxed); }
}  // namespace logosphere

// Async GPU prep. Seeded from the compile-time flag so behaviour is unchanged
// until something explicitly asks otherwise; see render_pipeline.h for why the
// default is what it is.
static std::atomic<bool> g_async_gpu_prep{[] {
    if (const char* e = std::getenv("LOGOSPHERE_ASYNC_PREP")) return std::strcmp(e, "0") != 0;
    return Optimizations::USE_ASYNC_GPU_PREP;
}()};

namespace logosphere {
void set_async_gpu_prep(bool on) { g_async_gpu_prep.store(on, std::memory_order_relaxed); }
bool get_async_gpu_prep()        { return g_async_gpu_prep.load(std::memory_order_relaxed); }
}  // namespace logosphere

// Analytic smooth sphere normals; see render_pipeline.h for what it is for.
static std::atomic<bool> g_smooth_spheres{[] {
    if (const char* e = std::getenv("LOGOSPHERE_SMOOTH_SPHERES")) return std::strcmp(e, "0") != 0;
    return true;   // ON since 2026-08-01; see SPHERE_SUBDIVISIONS and kit S17
}()};

namespace logosphere {
void set_smooth_sphere_normals(bool on) { g_smooth_spheres.store(on, std::memory_order_relaxed); }
bool get_smooth_sphere_normals()        { return g_smooth_spheres.load(std::memory_order_relaxed); }
}  // namespace logosphere

static std::atomic<size_t> g_shadow_bvh_rebuild_frames{[] {
    if (const char* e = std::getenv("LOGOSPHERE_BVH_REBUILD_FRAMES")) {
        long v = std::atol(e);
        if (v >= 1 && v <= 240) return static_cast<size_t>(v);
    }
    return Optimizations::SHADOW_BVH_MIN_REBUILD_FRAMES;
}()};

namespace logosphere {
void set_shadow_bvh_rebuild_frames(size_t n) {
    if (n < 1) n = 1;
    if (n > 240) n = 240;
    g_shadow_bvh_rebuild_frames.store(n, std::memory_order_relaxed);
}
size_t get_shadow_bvh_rebuild_frames() {
    return g_shadow_bvh_rebuild_frames.load(std::memory_order_relaxed);
}
}  // namespace logosphere

static size_t shadow_bvh_min_rebuild_frames() {
    return g_shadow_bvh_rebuild_frames.load(std::memory_order_relaxed);
}

// The thread that drives frames, latched on first use. A prep WORKER is not it.
static std::thread::id      g_snapshot_home_thread{};
static std::atomic<uint64_t> g_kg_snapshot_offthread{0};

namespace logosphere {
uint64_t render_kg_snapshots_off_main_thread() {
    return g_kg_snapshot_offthread.load(std::memory_order_relaxed);
}
void reset_render_kg_snapshot_guard() {
    g_kg_snapshot_offthread.store(0, std::memory_order_relaxed);
    g_snapshot_home_thread = std::thread::id{};
}
}  // namespace logosphere

void RenderPipeline::snapshot_entity_ids(int slot, size_t particle_count) {
    // MECHANICAL GUARD for issue #31, rather than a comment asking future code
    // to behave. The bug was a live KG read on a prep worker resolved against a
    // particle array captured a frame earlier. Reading the KG here from any
    // thread but the one that drives frames means that skew is back, whatever
    // the call path looks like. A test asserts this counter stays zero through
    // an async run with heavy particle churn, which is exactly the condition
    // the original equivalence test could not reach.
    if (g_snapshot_home_thread == std::thread::id{}) {
        g_snapshot_home_thread = std::this_thread::get_id();
    } else if (std::this_thread::get_id() != g_snapshot_home_thread) {
        g_kg_snapshot_offthread.fetch_add(1, std::memory_order_relaxed);
    }

    if (kg_module_) {
        kg_module_->snapshotRenderIndexToEntity(shadow_entity_ids_[slot], particle_count);
    } else {
        shadow_entity_ids_[slot].assign(particle_count, 0);
    }
}

void RenderPipeline::prepare_gpu_data(
    int buffer_index,
    const std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
    const std::vector<Particle>& particles,
    CameraSystem& camera_system,
    bool kg_snapshot_taken
) {
    auto prep_start = Clock::now();
    ::logosphere::telemetry::ScopedPhase _prep_all(::logosphere::telemetry::Phase::RenderPrep);

    // Get references to this buffer's data structures
    auto& gpu_triangles = gpu_triangles_[buffer_index];
    auto& gpu_transparent_triangles = gpu_transparent_triangles_[buffer_index];
    auto& shadow_triangles = shadow_triangles_[buffer_index];
    auto& shadow_bvh = shadow_bvh_[buffer_index];
    auto& light_data = light_data_[buffer_index];
    auto& is_light_source_map = is_light_source_map_[buffer_index];
    auto& is_dynamic_map      = is_dynamic_map_[buffer_index];
    auto& particle_transforms = particle_transforms_[buffer_index];

    // =========================================================================
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::PrepTriangles);
    // STEP 1: Convert culled surfaces to GPU lit triangles
    // =========================================================================
    auto gpu_tri_start = Clock::now();

    gpu_triangles.clear();
    gpu_triangles.reserve(surfaces.size() * 2);  // Most surfaces are quads = 2 triangles
    if constexpr (Optimizations::USE_TRANSPARENCY) {
        gpu_transparent_triangles.clear();
        gpu_transparent_triangles.reserve(surfaces.size() / 4);  // Few transparent surfaces expected
    }

    // Analytic smooth shading applies to SPHERES only: the normal is exact
    // there (normalize(p - centre)) and nothing else has a single centre that
    // works. Returns nullptr for everything else, which keeps flat shading.
    const bool smooth_spheres = ::logosphere::get_smooth_sphere_normals();
    float sphere_centre[3];
    auto sphere_centre_for = [&](const auto& sd) -> const float* {
        if (!smooth_spheres) return nullptr;
        if (sd.particle_index < 0 || sd.particle_index >= (int)particles.size()) return nullptr;
        const Particle& p = particles[sd.particle_index];
        if (p.shape != ParticleShape::SPHERE) return nullptr;
        sphere_centre[0] = p.x; sphere_centre[1] = p.y; sphere_centre[2] = p.z;
        return sphere_centre;
    };

    for (const auto& surface : surfaces) {
        // Get particle color from SurfaceData (already stored as 0-1 floats)
        uint8_t r = static_cast<uint8_t>(surface.particle_r * 255.0f);
        uint8_t g = static_cast<uint8_t>(surface.particle_g * 255.0f);
        uint8_t b = static_cast<uint8_t>(surface.particle_b * 255.0f);
        uint8_t a = static_cast<uint8_t>(surface.particle_a * 255.0f);

        // Entity hover highlighting (debug mode only)
        // Use -1 check to allow Entity 0 to be highlighted
        if (hovered_entity_id_ >= 0 && surface.particle_index >= 0 && surface.particle_index < (int)particles.size()) {
            const Particle& particle = particles[surface.particle_index];
            if (static_cast<int>(particle.entity_id) == hovered_entity_id_) {
                // Apply subtle cyan glow
                r = std::min(255, r + 15);
                g = std::min(255, g + 45);
                b = std::min(255, b + 50);
            }
        }

         // Route transparent surfaces to separate buffer for forward pass
        if constexpr (Optimizations::USE_TRANSPARENCY) {
            // Fully invisible surfaces (alpha ~ 0, e.g. foot-pin anchors)
            // render as nothing — skip them everywhere.
            if (surface.particle_a <= 0.004f) {
                continue;
            }
            if (surface.particle_a < 1.0f) {
                Logosphere::GPURasterizer::convert_surface_to_lit_triangles(
                    surface.surface, camera_system, r, g, b, a,
                    gpu_transparent_triangles, sphere_centre_for(surface));
                continue;  // Don't add to opaque batch
            }
        }

        Logosphere::GPURasterizer::convert_surface_to_lit_triangles(
            surface.surface,
            camera_system,
            r, g, b, a,
            gpu_triangles,
            sphere_centre_for(surface));
    }
    // Sort transparent triangles back-to-front (painter's algorithm) for correct blending
    if constexpr (Optimizations::USE_TRANSPARENCY) {
        if (!gpu_transparent_triangles.empty()) {
            std::sort(gpu_transparent_triangles.begin(), gpu_transparent_triangles.end(),
                [](const Logosphere::GPURasterizer::TriangleLit& a,
                   const Logosphere::GPURasterizer::TriangleLit& b) {
                    // Average screen-space depth (z0+z1+z2)/3 — higher depth = farther = draw first
                    float depth_a = (a.z0 + a.z1 + a.z2);
                    float depth_b = (b.z0 + b.z1 + b.z2);
                    return depth_a > depth_b;  // Back-to-front
                });
        }
    }

    auto gpu_tri_ms = Duration(Clock::now() - gpu_tri_start).count();

    // =========================================================================
    LOGO_COUNT_N(::logosphere::telemetry::Counter::TrianglesBuilt, gpu_triangles.size());
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::PrepTriangles);
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::PrepShadowTris);
    // STEP 2: Build shadow triangles from particles (with optional distance culling)
    // =========================================================================
    // When USE_SHADOW_DISTANCE_CULLING is enabled:
    // - Only generate shadow triangles for particles within SHADOW_CULL_RADIUS
    // - Distant particles fade to black via GPU shader (apply_lighting_deferred.metal)
    // - Creates "darkness beyond the campfire" effect
    auto shadow_tri_start = Clock::now();

    shadow_triangles.clear();
    shadow_triangles.reserve(particles.size() * 12);  // 12 triangles per cube

    // Entity ID tracking for entity BVH grouping
    auto& triangle_entity_ids = triangle_entity_ids_[buffer_index];
    triangle_entity_ids.clear();
    triangle_entity_ids.reserve(particles.size() * 12);

    // Distance culling parameters (from optimization_flags.h)
    // FIX: Use look-at target, not camera position (isometric camera is offset from scene)
    const float shadow_cull_radius_sq = Optimizations::SHADOW_CULL_RADIUS * Optimizations::SHADOW_CULL_RADIUS;
    float cam_x, cam_y;
    camera_system.get_look_at_target(cam_x, cam_y);

    // ISOMETRIC BACKFACE CULLING: Get camera forward direction for shadow triangle culling
    // In isometric projection, view direction is fixed - faces pointing away from camera
    // can never cast visible shadows, so we cull them at build time (not runtime)
    float forward_x, forward_y, forward_z;
    camera_system.get_forward_vector(forward_x, forward_y, forward_z);

    // Per-particle shadow triangle count tracking (for dirty-refit optimization)
    auto& ptri_start = particle_tri_start_[buffer_index];
    auto& ptri_count = particle_tri_count_[buffer_index];
    ptri_start.assign(particles.size(), 0);
    ptri_count.assign(particles.size(), 0);

    // Render index -> owning entity, snapshotted ONCE under a single KG lock.
    // This used to be a getEntityByRenderIndex() call per particle inside the
    // worker loop below. That accessor locks a recursive_mutex and does two
    // hash lookups, and 14 workers contended on it every frame: measured
    // 4.51 ms of a 6.88 ms prep_shadow_tris at 16,383 particles, 275 ns per
    // call against roughly 25 uncontended. Unmapped slots stay INVALID_ENTITY
    // (== 0), which is exactly what the per-index accessor returned on a miss,
    // so the entity grouping is unchanged.
    // ISSUE #31: only refresh when the caller has NOT already done it. Async
    // prep fills this on the main thread beside the particle snapshot, because
    // reading the live KG from here against frame N-1's particles resolves
    // every index through a mapping that swap-and-pop deletion has since
    // rewritten.
    if (!kg_snapshot_taken) {
        snapshot_entity_ids(buffer_index, particles.size());
    }
    const std::vector<kg::EntityID>& entity_ids = shadow_entity_ids_[buffer_index];

    // PARALLEL SHADOW TRIANGLE GENERATION
    if constexpr (Optimizations::USE_PARALLEL_SURFACE_COLLECTION) {
        const int num_threads = Optimizations::WORKER_THREAD_COUNT;
        std::vector<std::vector<Logosphere::ShadowTriangle>> thread_triangles(num_threads);
        std::vector<std::vector<kg::EntityID>> thread_entity_ids(num_threads);
        // Per-thread particle triangle counts (particle_idx → count within thread)
        std::vector<std::vector<int>> thread_ptri_counts(num_threads);

        // Pre-allocate thread-local storage
        for (int t = 0; t < num_threads; ++t) {
            size_t chunk = (particles.size() / num_threads) + 1;
            thread_triangles[t].reserve(chunk * 12);
            thread_entity_ids[t].reserve(chunk * 12);
            thread_ptri_counts[t].assign(particles.size(), 0);
        }

        // Worker function (shadow triangle generation)
        auto worker_func = [&, cam_x, cam_y, shadow_cull_radius_sq, forward_x, forward_y, forward_z](int thread_id) {
            size_t start = (particles.size() * thread_id) / num_threads;
            size_t end = (particles.size() * (thread_id + 1)) / num_threads;
            auto& my_triangles = thread_triangles[thread_id];
            auto& my_entity_ids = thread_entity_ids[thread_id];
            auto& my_ptri_counts = thread_ptri_counts[thread_id];

            // Pre-allocate temp buffer for GetShadowTriangles
            std::vector<float> temp_vertices;
            temp_vertices.reserve(12 * 9);  // 12 triangles x 9 floats

            for (size_t i = start; i < end; ++i) {
                const auto& particle = particles[i];
                if (particle.is_light_source) continue;
                // Transparent particles don't cast shadows (they're rendered in forward pass)
                if constexpr (Optimizations::USE_TRANSPARENCY) {
                    if (particle.a < 1.0f) continue;
                }

                // DISTANCE CULLING: Skip shadow triangles for distant particles
                if constexpr (Optimizations::USE_SHADOW_DISTANCE_CULLING) {
                    float dx = particle.x - cam_x;
                    float dy = particle.y - cam_y;
                    if (dx*dx + dy*dy > shadow_cull_radius_sq) continue;
                }

                // Entity id for BVH grouping — read from the frame snapshot,
                // lock-free. See snapshotRenderIndexToEntity above.
                kg::EntityID entity_id = entity_ids[i];

                // FAST PATH: Get shadow triangles directly (no Surface overhead)
                temp_vertices.clear();
                particle.GetShadowTriangles(temp_vertices);

                // DEBUG: Log DYNAMICS particles' shadow triangle generation
                if (particle.owner == ParticleOwner::DYNAMICS) {
                    static int dyn_log = 0;
                    if (dyn_log < 10) {
                        printf("[DYN_SHADOW] p[%zu] pos=(%.2f,%.2f,%.2f) sz=(%.3f,%.3f,%.3f) a=%.2f light=%d tris=%zu tri0=(%.2f,%.2f,%.2f)\n",
                            i, particle.x, particle.y, particle.z,
                            particle.width, particle.height, particle.thickness,
                            particle.a, particle.is_light_source,
                            temp_vertices.size() / 9,
                            temp_vertices.size() >= 3 ? temp_vertices[0] : 0.0f,
                            temp_vertices.size() >= 3 ? temp_vertices[1] : 0.0f,
                            temp_vertices.size() >= 3 ? temp_vertices[2] : 0.0f);
                        dyn_log++;
                    }
                }

                int tri_count_for_particle = 0;

                // Copy vertices to shadow triangles (9 floats per triangle).
                //
                // A per-triangle normal used to be computed here, six
                // subtractions and a cross product, and then never read. It was
                // left over from an attempt at build-time back-face culling,
                // which is the WRONG frame of reference for shadows: rays run
                // from shaded points toward lights in every direction, so a
                // triangle facing away from the camera still occludes. The
                // correct per-ray cull already happens on the GPU, in
                // shadow_rays_deferred.metal stage 2, which skips directional
                // groups whose average normal faces away from THAT ray. Every
                // face is required input for it, which is what the line below
                // means. Deleted 2026-07-31: 196,596 dead cross products a frame.
                for (size_t v = 0; v < temp_vertices.size(); v += 9) {
                    // Shadow triangles need all faces (no backface cull)
                    my_triangles.emplace_back(
                        temp_vertices[v+0], temp_vertices[v+1], temp_vertices[v+2],  // v0
                        temp_vertices[v+3], temp_vertices[v+4], temp_vertices[v+5],  // v1
                        temp_vertices[v+6], temp_vertices[v+7], temp_vertices[v+8],  // v2
                        static_cast<uint8_t>(particle.b * 255),  // BGRA order
                        static_cast<uint8_t>(particle.g * 255),
                        static_cast<uint8_t>(particle.r * 255),
                        static_cast<uint8_t>(particle.a * 255)
                    );
                    my_entity_ids.push_back(entity_id);
                    tri_count_for_particle++;
                }
                my_ptri_counts[i] = tri_count_for_particle;
            }
        };

        // Launch worker threads
        std::vector<std::thread> workers;
        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back(worker_func, t);
        }

        // Wait for completion
        for (auto& worker : workers) {
            worker.join();
        }

        // Merge thread-local triangles and entity IDs
        // Track per-thread triangle offsets for particle→triangle mapping
        int global_offset = 0;
        for (int t = 0; t < num_threads; ++t) {
            size_t t_start = (particles.size() * t) / num_threads;
            size_t t_end = (particles.size() * (t + 1)) / num_threads;

            // Record particle tri starts within the merged array
            for (size_t i = t_start; i < t_end; ++i) {
                ptri_count[i] = thread_ptri_counts[t][i];
                ptri_start[i] = global_offset;
                global_offset += ptri_count[i];
            }

            shadow_triangles.insert(shadow_triangles.end(), thread_triangles[t].begin(), thread_triangles[t].end());
            triangle_entity_ids.insert(triangle_entity_ids.end(), thread_entity_ids[t].begin(), thread_entity_ids[t].end());
        }
    } else {
        // SERIAL FALLBACK
        std::vector<float> temp_vertices;
        temp_vertices.reserve(12 * 9);

        for (size_t particle_idx = 0; particle_idx < particles.size(); ++particle_idx) {
            const auto& particle = particles[particle_idx];
            if (particle.is_light_source) continue;

            // DISTANCE CULLING: Skip shadow triangles for distant particles
            if constexpr (Optimizations::USE_SHADOW_DISTANCE_CULLING) {
                float dx = particle.x - cam_x;
                float dy = particle.y - cam_y;
                if (dx*dx + dy*dy > shadow_cull_radius_sq) continue;
            }

            // Entity id for BVH grouping — from the frame snapshot, lock-free.
            kg::EntityID entity_id = entity_ids[particle_idx];

            temp_vertices.clear();
            particle.GetShadowTriangles(temp_vertices);

            ptri_start[particle_idx] = static_cast<int>(shadow_triangles.size());
            int tri_count_for_particle = 0;

            // Same dead normal computation as the parallel path above; see the
            // comment there for why build-time back-face culling is the wrong
            // frame of reference for shadow casters.
            for (size_t v = 0; v < temp_vertices.size(); v += 9) {
                // Shadow triangles need all faces (no backface cull)
                shadow_triangles.emplace_back(
                    temp_vertices[v+0], temp_vertices[v+1], temp_vertices[v+2],
                    temp_vertices[v+3], temp_vertices[v+4], temp_vertices[v+5],
                    temp_vertices[v+6], temp_vertices[v+7], temp_vertices[v+8],
                    static_cast<uint8_t>(particle.b * 255),  // BGRA order
                    static_cast<uint8_t>(particle.g * 255),
                    static_cast<uint8_t>(particle.r * 255),
                    static_cast<uint8_t>(particle.a * 255)
                );
                triangle_entity_ids.push_back(entity_id);
                tri_count_for_particle++;
            }
            ptri_count[particle_idx] = tri_count_for_particle;
        }
    }

    auto shadow_tri_ms = Duration(Clock::now() - shadow_tri_start).count();

    // =========================================================================
    // DIRTY PARTICLE DETECTION: Compare positions to previous frame
    // =========================================================================
    auto& prev_pos = prev_positions_[buffer_index];
    auto& prev_rot = prev_rotations_[buffer_index];
    std::vector<int> dirty_triangle_indices;
    bool have_dirty_list = false;

    // Only compute dirty list if prev poses exist and triangle count is stable
    if (prev_pos.size() == particles.size() * 3 &&
        prev_rot.size() == particles.size() * 3 &&
        shadow_triangles.size() == last_triangle_count_[buffer_index]) {
        constexpr float POS_EPSILON_SQ = 1e-12f;  // Sub-millimeter movement threshold
        constexpr float ROT_EPSILON_SQ = 1e-10f;  // Sub-microradian threshold

        for (size_t i = 0; i < particles.size(); ++i) {
            float dx = particles[i].x - prev_pos[i * 3 + 0];
            float dy = particles[i].y - prev_pos[i * 3 + 1];
            float dz = particles[i].z - prev_pos[i * 3 + 2];
            float drx = particles[i].rotation_x - prev_rot[i * 3 + 0];
            float dry = particles[i].rotation_y - prev_rot[i * 3 + 1];
            float drz = particles[i].rotation_z - prev_rot[i * 3 + 2];
            bool moved   = dx * dx + dy * dy + dz * dz > POS_EPSILON_SQ;
            bool rotated = drx*drx + dry*dry + drz*drz > ROT_EPSILON_SQ;
            if (moved || rotated) {
                // Position OR orientation changed — shadow triangles are
                // stale; mark the particle's entire triangle range dirty.
                int start = ptri_start[i];
                int count = ptri_count[i];
                for (int t = 0; t < count; ++t) {
                    dirty_triangle_indices.push_back(start + t);
                }
            }
        }
        have_dirty_list = true;
    }

    // Update previous poses for next frame
    prev_pos.resize(particles.size() * 3);
    prev_rot.resize(particles.size() * 3);
    for (size_t i = 0; i < particles.size(); ++i) {
        prev_pos[i * 3 + 0] = particles[i].x;
        prev_pos[i * 3 + 1] = particles[i].y;
        prev_pos[i * 3 + 2] = particles[i].z;
        prev_rot[i * 3 + 0] = particles[i].rotation_x;
        prev_rot[i * 3 + 1] = particles[i].rotation_y;
        prev_rot[i * 3 + 2] = particles[i].rotation_z;
    }

    // =========================================================================
    LOGO_COUNT_N(::logosphere::telemetry::Counter::ShadowTrianglesBuilt, shadow_triangles.size());
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::PrepShadowTris);
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::PrepBVH);
    // STEP 3: Build/refit shadow BVH
    // =========================================================================
    // THE SEAM. Shadow rays trace against ONE structure, and which one decides
    // whether these CPU trees are worth building. Under HardwareRT the driver
    // owns the acceleration structure, trace_shadows_deterministic binds it at
    // buffer(0), and neither bvh_nodes nor entity_bvh_nodes is bound at all —
    // so building them is pure waste (2.16 ms of a 21.7 ms Eden frame, up to
    // 97 ms on a single frame in a spawning scene). Under SoftwareBVH the
    // batched kernel walks them at buffer(8)/(9) and they are load-bearing.
    // NOTE: this is NOT ParticleSystem::shadow_bvh_, a different BVH over
    // particles that physics and animation query. That one is untouched.
    const bool software_shadow_accel =
        gpu_rasterizer_.shadow_accel_backend() == Logosphere::ShadowAccelBackend::SoftwareBVH;

    // Declared out here: the entity-BVH region below reads it, and under
    // HardwareRT nothing sets it (no rebuild is ever needed).
    bool triangle_count_changed = false;

    auto bvh_start = Clock::now();
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::PrepBvhTriangle);
    if (software_shadow_accel) {

    bool& bvh_built = bvh_built_[buffer_index];
    size_t& last_triangle_count = last_triangle_count_[buffer_index];
    size_t& frames_since_rebuild = frames_since_rebuild_[buffer_index];

    // OPTION F: Distinguish real geometry change from culling flicker.
    // Small count drift (a few particles crossing the shadow cull radius due to
    // floating-point noise) should NOT trigger a full rebuild. Only treat count
    // changes > 1% (or > 100 triangles) as "real" geometry changes requiring rebuild.
    // Smaller drift falls through to refit(), which tolerates stale trailing leaves.
    size_t count_now = shadow_triangles.size();
    size_t count_delta = count_now > last_triangle_count
        ? count_now - last_triangle_count
        : last_triangle_count - count_now;
    size_t rebuild_threshold = std::max(size_t(100), count_now / 100);
    triangle_count_changed = (count_delta >= rebuild_threshold);

    // BVH quality optimization: Periodic rebuild to maintain quality
    bool quality_degraded = false;
    if constexpr (Optimizations::USE_BVH_QUALITY_REBUILD) {
        if (Optimizations::BVH_REBUILD_STRATEGY == Optimizations::BVHRebuildStrategy::TIME_BASED ||
            Optimizations::BVH_REBUILD_STRATEGY == Optimizations::BVHRebuildStrategy::BOTH) {
            quality_degraded = (frames_since_rebuild >= Optimizations::BVH_REBUILD_INTERVAL);
        }
        // Quality-based strategy will be added in Phase 2
    }

    // QUALITY LEVER: hold off incremental rebuilds. A steady spawn rate outruns
    // the relative threshold above while the scene is small, which rebuilt every
    // frame at up to 97 ms. Defer to at most one rebuild per N frames; a refit
    // covers the frames in between, so bodies spawned since the last rebuild are
    // missing from the BVH (they cast no shadow) until it comes round. A change
    // larger than SHADOW_BVH_FORCE_REBUILD_FRACTION bypasses the hold entirely,
    // so teleports and scene swaps are never served a stale tree.
    if (triangle_count_changed && !quality_degraded && bvh_built && !shadow_triangles.empty()) {
        const size_t min_frames = shadow_bvh_min_rebuild_frames();
        const bool huge_change =
            count_delta >= static_cast<size_t>(count_now * Optimizations::SHADOW_BVH_FORCE_REBUILD_FRACTION);
        if (min_frames > 1 && !huge_change && frames_since_rebuild < min_frames) {
            triangle_count_changed = false;   // refit this frame, rebuild later
        }
    }

    if (!bvh_built || shadow_triangles.empty() || triangle_count_changed || quality_degraded) {
        // Full rebuild (first frame, empty scene, significant count change, or quality degraded)
        if (!shadow_triangles.empty()) {
            shadow_bvh.build(shadow_triangles);
            bvh_built = true;
            last_triangle_count = shadow_triangles.size();

            // Reset quality tracking
            size_t old_frames = frames_since_rebuild;
            frames_since_rebuild = 0;

            // Log rebuild events (excluding first frame) - always log since rebuilds are rare
            if constexpr (Optimizations::ENABLE_PROFILING) {
                if (old_frames > 0) {
                    auto rebuild_ms = Duration(Clock::now() - bvh_start).count();
                    const char* reason = triangle_count_changed ? "particle count changed" :
                                       quality_degraded ? "quality degraded (time-based)" :
                                       "unknown";
                    std::cout << "    [BVH_REBUILD] Triggered: " << reason
                              << " (frames since last: " << old_frames << ")"
                              << " - " << shadow_triangles.size() << " tris in " << rebuild_ms << "ms" << std::endl;
                }
            }
        } else {
            shadow_bvh.clear();
            bvh_built = false;
            last_triangle_count = 0;
            frames_since_rebuild = 0;
        }
    } else {
        // Fast refit path. Triangle count may have drifted by <1% due to culling
        // flicker at the shadow cull radius; refit() tolerates stale trailing
        // leaves (it skips invalid indices). Dirty refit only when count is
        // strictly stable (its index assumptions require exact match).
        bool count_stable = (count_now == last_triangle_count);
        if (count_stable && have_dirty_list && !dirty_triangle_indices.empty() &&
            dirty_triangle_indices.size() < shadow_triangles.size() / 2) {
            shadow_bvh.refit_dirty(shadow_triangles, dirty_triangle_indices);
        } else {
            shadow_bvh.refit(shadow_triangles);
        }
        frames_since_rebuild++;
    }

    // Always update last_triangle_count so next frame can detect dirty vs rebuild.
    // (Previously only set during rebuild, causing perpetual rebuilds when count drifts.)
    last_triangle_count = shadow_triangles.size();

    }  // if (software_shadow_accel)
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::PrepBvhTriangle);
    auto bvh_ms = Duration(Clock::now() - bvh_start).count();
    static int dirty_log_counter = 0;
    if (++dirty_log_counter % 60 == 0) {
        if (have_dirty_list) {
            printf("[BVH_DIRTY] dirty_tris=%zu/%zu (%.1f%%) bvh=%.1fms\n",
                   dirty_triangle_indices.size(), shadow_triangles.size(),
                   100.0 * dirty_triangle_indices.size() / std::max(shadow_triangles.size(), size_t(1)),
                   bvh_ms);
        } else {
            printf("[BVH_DIRTY] no dirty list (count changed or first frame) bvh=%.1fms\n", bvh_ms);
        }
    }

    // =========================================================================
    // STEP 3b: Build Entity BVH (directional culling optimization)
    // =========================================================================
    // Group triangles by entity_id for proper entity-level BVH traversal
    // This enables both entity AABB culling AND directional culling per entity
    auto& entity_bvh = entity_bvh_[buffer_index];
    auto& entity_data = entity_triangle_data_[buffer_index];
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::PrepBvhEntity);

    if (software_shadow_accel && !shadow_triangles.empty() && triangle_count_changed) {
        entity_data.clear();

        // Group triangles by entity_id using unordered_map for O(1) lookup
        std::unordered_map<kg::EntityID, size_t> entity_to_index;

        for (size_t tri_idx = 0; tri_idx < shadow_triangles.size(); ++tri_idx) {
            kg::EntityID entity_id = triangle_entity_ids[tri_idx];

            // Find or create EntityTriangleData for this entity
            auto it = entity_to_index.find(entity_id);
            size_t data_idx;
            if (it == entity_to_index.end()) {
                data_idx = entity_data.size();
                entity_data.emplace_back();
                entity_data.back().entity_id = entity_id;
                entity_data.back().is_dynamic = false;  // TODO: Track dynamic entities
                // Initialize AABB to first triangle
                const auto& tri = shadow_triangles[tri_idx];
                entity_data.back().bbox = AABB(
                    std::min({tri.v0[0], tri.v1[0], tri.v2[0]}),
                    std::min({tri.v0[1], tri.v1[1], tri.v2[1]}),
                    std::min({tri.v0[2], tri.v1[2], tri.v2[2]}),
                    std::max({tri.v0[0], tri.v1[0], tri.v2[0]}),
                    std::max({tri.v0[1], tri.v1[1], tri.v2[1]}),
                    std::max({tri.v0[2], tri.v1[2], tri.v2[2]})
                );
                entity_to_index[entity_id] = data_idx;
            } else {
                data_idx = it->second;
            }

            // Add triangle to entity and expand AABB
            auto& entity = entity_data[data_idx];
            entity.triangles.push_back(shadow_triangles[tri_idx]);

            // Expand AABB to include this triangle
            const auto& tri = shadow_triangles[tri_idx];
            entity.bbox.min_x = std::min(entity.bbox.min_x, std::min({tri.v0[0], tri.v1[0], tri.v2[0]}));
            entity.bbox.min_y = std::min(entity.bbox.min_y, std::min({tri.v0[1], tri.v1[1], tri.v2[1]}));
            entity.bbox.min_z = std::min(entity.bbox.min_z, std::min({tri.v0[2], tri.v1[2], tri.v2[2]}));
            entity.bbox.max_x = std::max(entity.bbox.max_x, std::max({tri.v0[0], tri.v1[0], tri.v2[0]}));
            entity.bbox.max_y = std::max(entity.bbox.max_y, std::max({tri.v0[1], tri.v1[1], tri.v2[1]}));
            entity.bbox.max_z = std::max(entity.bbox.max_z, std::max({tri.v0[2], tri.v1[2], tri.v2[2]}));
        }

        // Build EntityBVH from grouped entities
        entity_bvh.build(entity_data);

        if constexpr (Optimizations::ENABLE_PROFILING) {
            static int entity_bvh_log_count = 0;
            if (entity_bvh_log_count++ % 100 == 0) {
                std::cout << "    [ENTITY_BVH] " << entity_data.size() << " entities, "
                          << shadow_triangles.size() << " triangles (rebuild)" << std::endl;

                // Detailed breakdown: count triangles in entity_id=0 vs bound entities
                size_t unbound_tris = 0;
                size_t largest_entity_tris = 0;
                kg::EntityID largest_entity_id = 0;
                for (const auto& ed : entity_data) {
                    if (ed.entity_id == 0) {
                        unbound_tris = ed.triangles.size();
                    }
                    if (ed.triangles.size() > largest_entity_tris) {
                        largest_entity_tris = ed.triangles.size();
                        largest_entity_id = ed.entity_id;
                    }
                }
                std::cout << "    [ENTITY_BVH] Unbound (id=0): " << unbound_tris << " tris, "
                          << "Largest entity: id=" << largest_entity_id << " with " << largest_entity_tris << " tris" << std::endl;
                // Show triangles per directional group distribution (key performance metric)
                auto groups = entity_bvh.get_groups();
                auto group_count = entity_bvh.get_group_count();
                if (group_count > 0) {
                    int max_tris = 0, min_tris = INT_MAX;
                    int groups_over_100 = 0, groups_over_1000 = 0;
                    for (int g = 0; g < group_count; ++g) {
                        int tc = groups[g].tri_count;
                        max_tris = std::max(max_tris, tc);
                        min_tris = std::min(min_tris, tc);
                        if (tc > 100) groups_over_100++;
                        if (tc > 1000) groups_over_1000++;
                    }
                    std::cout << "    [ENTITY_BVH] Group sizes: min=" << min_tris
                              << " max=" << max_tris << " avg=" << shadow_triangles.size() / group_count
                              << " (over100: " << groups_over_100 << ", over1000: " << groups_over_1000 << ")" << std::endl;
                }
            }
        }
    } else if (software_shadow_accel && !shadow_triangles.empty() && entity_bvh.is_ready()) {
        // Same triangle count but positions may have changed - refit AABBs
        // This avoids expensive SAH tree rebuild when only positions update

        // Build entity → new AABB map from updated triangle positions
        std::unordered_map<kg::EntityID, AABB> entity_new_aabbs;

        for (size_t tri_idx = 0; tri_idx < shadow_triangles.size(); ++tri_idx) {
            kg::EntityID entity_id = triangle_entity_ids[tri_idx];
            const auto& tri = shadow_triangles[tri_idx];

            auto it = entity_new_aabbs.find(entity_id);
            if (it == entity_new_aabbs.end()) {
                entity_new_aabbs[entity_id] = AABB(
                    std::min({tri.v0[0], tri.v1[0], tri.v2[0]}),
                    std::min({tri.v0[1], tri.v1[1], tri.v2[1]}),
                    std::min({tri.v0[2], tri.v1[2], tri.v2[2]}),
                    std::max({tri.v0[0], tri.v1[0], tri.v2[0]}),
                    std::max({tri.v0[1], tri.v1[1], tri.v2[1]}),
                    std::max({tri.v0[2], tri.v1[2], tri.v2[2]})
                );
            } else {
                it->second.expand(tri.v0[0], tri.v0[1], tri.v0[2]);
                it->second.expand(tri.v1[0], tri.v1[1], tri.v1[2]);
                it->second.expand(tri.v2[0], tri.v2[1], tri.v2[2]);
            }
        }

        // Refit each entity's AABB in the BVH
        for (const auto& [entity_id, new_aabb] : entity_new_aabbs) {
            entity_bvh.refit_entity(entity_id, new_aabb);
        }
    }

    // =========================================================================
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::PrepBvhEntity);
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::PrepBVH);
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::PrepLights);
    // STEP 4: Pack light data (48 bytes per light, must match gpu_types.metal)
    // =========================================================================
    auto light_pack_start = Clock::now();
    light_data.clear();

    for (const auto& particle : particles) {
        if (particle.is_light_source) {
            float pos[3] = {particle.x, particle.y, particle.z};
            float strength = particle.emission_strength;
            float radius = particle.emission_radius;

            // Light size = max of the particle's visible extents, used
            // for area-light approximation in the soft-shadow shader.
            // BOX and ELLIPSOID use explicit per-axis dimensions;
            // SPHERE is isotropic (size IS the diameter).
            float light_size = particle.size;
            if (particle.shape == ParticleShape::BOX ||
                particle.shape == ParticleShape::ELLIPSOID) {
                light_size = std::max({particle.width, particle.height, particle.thickness, particle.size});
            }

            float color[3] = {particle.r, particle.g, particle.b};
            float padding = 0.0f;

            light_data.insert(light_data.end(), (uint8_t*)pos, (uint8_t*)pos + sizeof(pos));               // 12 bytes
            light_data.insert(light_data.end(), (uint8_t*)&strength, (uint8_t*)&strength + sizeof(float));  // 4 bytes
            light_data.insert(light_data.end(), (uint8_t*)&radius, (uint8_t*)&radius + sizeof(float));      // 4 bytes
            light_data.insert(light_data.end(), (uint8_t*)&light_size, (uint8_t*)&light_size + sizeof(float)); // 4 bytes
            light_data.insert(light_data.end(), (uint8_t*)color, (uint8_t*)color + sizeof(color));          // 12 bytes
            light_data.insert(light_data.end(), (uint8_t*)&padding, (uint8_t*)&padding + sizeof(float));    // 4 bytes
            // Total: 48 bytes per light
        }
    }

    auto light_pack_ms = Duration(Clock::now() - light_pack_start).count();

    // =========================================================================
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::PrepLights);
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::PrepTransforms);
    // STEP 5: Build emissive map (particle_index → is_emissive)
    // =========================================================================
    // Despite the legacy variable name, this map is the SHADER'S
    // self-emissive predicate, not "is in scene lights array."
    // Both is_light_source AND is_self_emissive flag a particle as
    // self-emissive. The scene-lights array (light_data_) is built
    // separately and only includes is_light_source particles —
    // self-emissive walls glow without paying scene-light cost.
    // See the self-emissive field design notes.
    auto light_map_start = Clock::now();
    // CRITICAL: Index by particle array index (matches G-buffer particle_id), NOT entity_id!
    is_light_source_map.clear();
    is_dynamic_map.clear();

    if (!particles.empty()) {
        is_light_source_map.resize(particles.size(), 0);
        is_dynamic_map.resize(particles.size(), 0);

        for (size_t i = 0; i < particles.size(); ++i) {
            const auto& p = particles[i];
            if (p.is_light_source || p.is_self_emissive) {
                is_light_source_map[i] = 1;
            }
            if (p.is_dynamic) {
                is_dynamic_map[i] = 1;
            }
        }
    }

    auto light_map_ms = Duration(Clock::now() - light_map_start).count();

    // =========================================================================
    // STEP 6: Build particle transforms (particle_index → center + primary_axis)
    // =========================================================================
    auto transforms_start = Clock::now();
    // ParticleTransform is 32 bytes: center(12) + padding(4) + primary_axis(12) + pattern_id(1) + padding(3)
    // CRITICAL: Index by particle array index (matches G-buffer particle_id), NOT entity_id!
    constexpr size_t TRANSFORM_SIZE = 32;
    particle_transforms.clear();

    if (!particles.empty()) {
        particle_transforms.resize(particles.size() * TRANSFORM_SIZE, 0);

        for (size_t i = 0; i < particles.size(); ++i) {
            const auto& particle = particles[i];

            // Compute center (particle position)
            float center[3] = {particle.x, particle.y, particle.z};

            // Compute primary axis: longest dimension in local space, transformed by rotation
            // For BOX: width=X, height=Y, thickness=Z
            float local_axis[3] = {0.0f, 0.0f, 1.0f};  // Default Z-up

            if (particle.width >= particle.height && particle.width >= particle.thickness) {
                local_axis[0] = 1.0f; local_axis[1] = 0.0f; local_axis[2] = 0.0f;
            } else if (particle.height >= particle.thickness) {
                local_axis[0] = 0.0f; local_axis[1] = 1.0f; local_axis[2] = 0.0f;
            }

            // Apply particle rotation to get world-space primary axis
            // Rotation order: X, Y, Z (same as particle_geometry_v2.cpp)
            float cx = std::cos(particle.rotation_x), sx = std::sin(particle.rotation_x);
            float cy = std::cos(particle.rotation_y), sy = std::sin(particle.rotation_y);
            float cz = std::cos(particle.rotation_z), sz = std::sin(particle.rotation_z);

            // Rotate around X
            float y1 = local_axis[1] * cx - local_axis[2] * sx;
            float z1 = local_axis[1] * sx + local_axis[2] * cx;
            local_axis[1] = y1; local_axis[2] = z1;

            // Rotate around Y
            float x2 = local_axis[0] * cy + local_axis[2] * sy;
            float z2 = -local_axis[0] * sy + local_axis[2] * cy;
            local_axis[0] = x2; local_axis[2] = z2;

            // Rotate around Z (CLOCKWISE to match particle_geometry_v2.cpp)
            // Convention: rotation_z = 0 → facing North (+Y)
            float x3 = local_axis[0] * cz + local_axis[1] * sz;
            float y3 = -local_axis[0] * sz + local_axis[1] * cz;
            local_axis[0] = x3; local_axis[1] = y3;

            // Normalize the axis (should already be unit, but safety)
            float len = std::sqrt(local_axis[0]*local_axis[0] + local_axis[1]*local_axis[1] + local_axis[2]*local_axis[2]);
            if (len > 0.001f) {
                local_axis[0] /= len; local_axis[1] /= len; local_axis[2] /= len;
            }

            // Pack into transform buffer indexed by particle array index
            // Layout: center(12) + padding(4) + primary_axis(12) + pattern_id(1) + padding(3) = 32 bytes
            uint8_t* dst = particle_transforms.data() + i * TRANSFORM_SIZE;
            std::memcpy(dst + 0, center, 12);         // center[3] at offset 0
            std::memcpy(dst + 16, local_axis, 12);    // primary_axis[3] at offset 16 (after center + padding)
            dst[28] = particle.pattern_id;            // pattern_id at offset 28
        }
    }

    auto transforms_ms = Duration(Clock::now() - transforms_start).count();
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::PrepTransforms);

    // =========================================================================
    // LOG PROFILING (only when slow or every 60 frames)
    // =========================================================================
    static int prep_frame = 0;
    prep_frame++;

    double total_prep_ms = Duration(Clock::now() - prep_start).count();

    // Always log breakdown every 60 frames for performance analysis
    if (total_prep_ms > 20.0 || (prep_frame % 60) == 0) {
        printf("[GPU_PREP] f=%d | gpu_tri=%.1fms(%zu) shadow_tri=%.1fms(%zu) "
               "bvh=%.1fms lights=%.1fms(%zu) map=%.1fms xform=%.1fms(%zu) "
               "TOTAL=%.1fms\n",
               prep_frame,
               gpu_tri_ms, gpu_triangles.size(),
               shadow_tri_ms, shadow_triangles.size(),
               bvh_ms,
               light_pack_ms, light_data.size() / 40,
               light_map_ms,
               transforms_ms, particles.size(),
               total_prep_ms);
    }

    if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
        if (total_prep_ms > 20.0 || (prep_frame % 60) == 0) {
            std::cout << "[ASYNC_PREP] Buffer " << buffer_index << " Frame " << prep_frame
                      << " | GPU tris: " << gpu_tri_ms << "ms (" << gpu_triangles.size() << ")"
                      << " | Shadow tris: " << shadow_tri_ms << "ms (" << shadow_triangles.size() << ")"
                      << " | BVH: " << bvh_ms << "ms"
                      << " | TOTAL: " << total_prep_ms << "ms"
                      << std::endl;
        }
    }
}

void RenderPipeline::render(
    const ParticleSystem& particle_system,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    SparseObjectMap& object_map,
    CameraSystem& camera_system,
    LightSystem& light_system,
    IPixelShader& pixel_shader,
    EngineMetrics* metrics) {
    
    // High-resolution timing for profiling
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::milli>;
    
    // Reset statistics
    last_frame_stats_ = {};
    
    // Reset per-frame counters if metrics provided
    if (metrics) {
        metrics->surfaces_culled = 0;
        metrics->surfaces_projected = 0;
        metrics->surfaces_rendered = 0;
        metrics->pixels_tested = 0;
        metrics->pixels_rejected = 0;
        metrics->pixels_drawn = 0;
        
        // Reset overdraw metrics
        metrics->pixels_shaded = 0;
        metrics->pixels_depth_rejected = 0;
        metrics->pixels_visible = 0;
        metrics->overdraw_factor = 0.0;

        // Reset detailed rasterization metrics (from single-pass renderer)
        metrics->pixels_edge_tested = 0;
        metrics->pixels_inside = 0;
        metrics->pixels_depth_tested = 0;
        metrics->pixels_depth_passed = 0;

        // Reset BVH traversal metrics (populated after render from LightingMetrics)
        metrics->bvh_aabb_tests = 0;
        metrics->bvh_aabb_hits = 0;
        metrics->bvh_nodes_visited = 0;
        metrics->bvh_leaf_tests = 0;
        metrics->bvh_rays_traced = 0;

        // Reset timing accumulators
        metrics->render_culling_time = 0.0;
        metrics->render_projection_time = 0.0;
        metrics->render_sorting_time = 0.0;
        metrics->render_rasterization_time = 0.0;
    }
    
    // DEBUG: Pipeline trace
    static int call_count = 0;
    bool debug = debug_mode_ && (call_count++ < 3);

    if (debug) {
        std::cout << "\n=== RENDER PIPELINE TRACE ===" << std::endl;
    }

    // THREAD SAFETY: Use ReadView for automatic lock management
    // Lock acquired on construction, released when view goes out of scope (RAII)
    // No manual lock/unlock needed - impossible to forget
    auto particles_view = particle_system.lock_particles_for_read();
    last_frame_stats_.total_particles = particles_view.size();

    if (debug) {
        std::cout << "1. Found " << particles_view.size() << " particles" << std::endl;
    }

    // ===== STAGE 1: SURFACE COLLECTION =====
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::RenderCollect);
    // GPU OPTIMIZATION: Skip synchronous wait when using GPU rasterization
    // GPU copies all data into Metal buffers during submission (triple-buffered)
    // Only CPU worker threads need protection, and they're not used in GPU mode
    if (!Optimizations::USE_GPU_RASTERIZATION) {
        // CPU path: Wait for worker threads to finish using surface_cache_ from previous frame
        surface_rasterizer_.wait_for_workers_completion();
    }
    // NOTE: Particle deletion safety is handled separately in Engine::render() before flush

    // Clear and reuse the surface cache to avoid allocations
    // Safe now - no GPU callbacks or workers are accessing it
    surface_cache_.clear();

    // NOTE: SurfaceCache prebuild stays at Stage 5 for rasterization/lighting
    // Don't prebuild here - it would process ALL 87K particles vs ~8K visible

    // ===== PRE-CULLING: Entity-based or Spatial Grid =====
    // Two strategies for reducing particle iteration:
    // 1. Entity-based: Test entity AABBs, iterate visible entities
    // 2. Spatial Grid: Query 10m cells in frustum, get particles directly
    std::vector<bool> entity_culled;
    std::vector<int> visible_particle_indices;
    int entities_culled = 0;
    int entities_visible = 0;

    if (Optimizations::USE_SPATIAL_GRID_CULLING) {
        // ===== SPATIAL GRID CULLING (Two-Tier Chunk Streaming Phase 1) =====
        // Build/rebuild grid when particle count changes significantly
        const auto& particles = particles_view.get();
        size_t current_count = particles.size();

        // Rebuild if particle count changed by more than 10% or never built
        bool need_rebuild = (last_grid_particle_count_ == 0) ||
                           (current_count > last_grid_particle_count_ * 1.1f) ||
                           (current_count < last_grid_particle_count_ * 0.9f);

        if (need_rebuild) {
            render_spatial_grid_.clear();
            for (size_t i = 0; i < particles.size(); ++i) {
                const auto& p = particles[i];
                render_spatial_grid_.insert(static_cast<int>(i), p.x, p.y);
            }
            last_grid_particle_count_ = current_count;

            if constexpr (Optimizations::ENABLE_PROFILING) {
                static int rebuild_count = 0;
                ++rebuild_count;
                std::cout << "[SPATIAL_GRID] Rebuilt grid with " << current_count
                          << " particles, " << render_spatial_grid_.get_cell_count()
                          << " cells (rebuild #" << rebuild_count << ")" << std::endl;
            }
        }

        // Query frustum to get visible particle indices directly
        visible_particle_indices = render_spatial_grid_.query_frustum(
            camera_system,
            pixel_buffer.width(),
            pixel_buffer.height(),
            100.0f  // max render distance
        );

        if constexpr (Optimizations::ENABLE_PROFILING) {
            static int grid_frame_count = 0;
            ++grid_frame_count;
            if (grid_frame_count == 10 || grid_frame_count % 60 == 0) {
                std::cout << "[SPATIAL_GRID] Query returned " << visible_particle_indices.size()
                          << " particles from " << render_spatial_grid_.get_cell_count()
                          << " cells" << std::endl;
            }
        }
    } else if (Optimizations::USE_ENTITY_FRUSTUM_CULLING) {
        // ===== ENTITY-LEVEL PRE-CULLING (Original Path) =====
        // Test entity AABBs before per-particle culling (~7K entities vs 87K particles)
        // If entity is off-screen, skip ALL its particles (12x fewer frustum tests)
        pre_cull_entities(particles_view.get(), camera_system,
                         pixel_buffer.width(), pixel_buffer.height(),
                         entity_culled, entities_culled, entities_visible);

        // Build flattened list of visible particle indices
        // This allows collect_surfaces to iterate ONLY visible particles (10x fewer iterations)
        const auto& entity_indices = particle_system.get_all_entity_particle_indices();
        size_t estimated_visible = particles_view.size() / 8;  // ~12% visible typically
        visible_particle_indices.reserve(estimated_visible);

        for (size_t eid = 0; eid < entity_culled.size(); ++eid) {
            if (!entity_culled[eid] && eid < entity_indices.size()) {
                for (int idx : entity_indices[eid]) {
                    visible_particle_indices.push_back(idx);
                }
            }
        }
    }

    if constexpr (Optimizations::ENABLE_PROFILING) {
        auto collect_start = Clock::now();

        collect_surfaces(particles_view.get(), surface_cache_, camera_system,
                        pixel_buffer.width(), pixel_buffer.height(), depth_buffer,
                        entity_culled, visible_particle_indices);

        auto collect_ms = Duration(Clock::now() - collect_start).count();
        static int collect_frame_count = 0;
        ++collect_frame_count;
        if (collect_frame_count == 10 || collect_frame_count % 60 == 0) {
            std::cout << "[CPU_PROFILE] collect_surfaces: " << collect_ms
                      << "ms (frame " << collect_frame_count << ", "
                      << particles_view.size() << " particles → "
                      << surface_cache_.size() << " surfaces)"
                      << " [visible_indices=" << visible_particle_indices.size() << "]"
                      << std::endl;
        }
    } else {
        collect_surfaces(particles_view.get(), surface_cache_, camera_system,
                        pixel_buffer.width(), pixel_buffer.height(), depth_buffer,
                        entity_culled, visible_particle_indices);
    }

    last_frame_stats_.total_surfaces = surface_cache_.size();

    if (debug) {
        std::cout << "2. Collected " << surface_cache_.size() << " surfaces" << std::endl;
    }

    LOGO_COUNT_N(::logosphere::telemetry::Counter::ParticlesVisible, visible_particle_indices.size());
    LOGO_COUNT_N(::logosphere::telemetry::Counter::SurfacesCollected, surface_cache_.size());
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::RenderCollect);
    // ===== STAGE 2: CULLING =====
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::RenderCull);
    auto cull_start = Clock::now();
    cull_surfaces(surface_cache_, camera_system, metrics);

    if (metrics) {
        auto cull_end = Clock::now();
        metrics->render_culling_time = Duration(cull_end - cull_start).count();

        if constexpr (Optimizations::ENABLE_PROFILING) {
            static int cull_frame_count = 0;
            ++cull_frame_count;
            if (cull_frame_count == 10 || cull_frame_count % 60 == 0) {
                std::cout << "[CPU_PROFILE] cull_surfaces: " << metrics->render_culling_time
                          << "ms (frame " << cull_frame_count << ")" << std::endl;
            }
        }
    }

    if (debug) {
        std::cout << "3. After culling: " << surface_cache_.size() << " surfaces remain" << std::endl;
    }

    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::RenderCull);
    // ===== STAGE 3: DISTANCE CALCULATION =====
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::RenderDistances);
    if constexpr (Optimizations::ENABLE_PROFILING) {
        auto distance_start = Clock::now();

        calculate_distances(surface_cache_, camera_system);

        auto distance_ms = Duration(Clock::now() - distance_start).count();
        static int distance_frame_count = 0;
        ++distance_frame_count;
        if (distance_frame_count == 10 || distance_frame_count % 60 == 0) {  // First at frame 10, then every 60
            std::cout << "[CPU_PROFILE] calculate_distances: " << distance_ms << "ms (frame " << distance_frame_count << ")" << std::endl;
        }
    } else {
        calculate_distances(surface_cache_, camera_system);
    }
    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::RenderDistances);

    // ===== STAGE 4: SORTING =====
    ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::RenderSort);
    auto sort_start = Clock::now();
    sort_surfaces(surface_cache_, metrics);

    if (metrics) {
        auto sort_end = Clock::now();
        metrics->render_sorting_time = Duration(sort_end - sort_start).count();

        if constexpr (Optimizations::ENABLE_PROFILING) {
            static int sort_frame_count = 0;
            ++sort_frame_count;
            if (sort_frame_count == 10 || sort_frame_count % 60 == 0) {
                std::cout << "[CPU_PROFILE] sort_surfaces: " << metrics->render_sorting_time
                          << "ms (frame " << sort_frame_count << ")" << std::endl;
            }
        }
    }

    ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::RenderSort);
    // ===== STAGE 5: RASTERIZATION =====
    // SurfaceCache already pre-built at Stage 1 (before collect_surfaces)
    auto raster_start = Clock::now();
    rasterize_surfaces(surface_cache_, pixel_buffer, depth_buffer, object_map,
                      camera_system, light_system, pixel_shader,
                      particles_view.get(),
                      particle_system.get_shadow_bvh(), metrics);
    
    if (metrics) {
        auto raster_end = Clock::now();
        metrics->render_rasterization_time = Duration(raster_end - raster_start).count();
        
        // Calculate overdraw metrics
        metrics->pixels_visible = metrics->pixels_drawn;  // Pixels that passed depth test
        if (metrics->pixels_visible > 0) {
            metrics->overdraw_factor = static_cast<double>(metrics->pixels_shaded) / 
                                      static_cast<double>(metrics->pixels_visible);
        }
        
        // Output overdraw metrics every 60 frames (once per second at 60 FPS)
        static int frame_count = 0;
        if (++frame_count % 60 == 0) {
            std::cout << "[OVERDRAW METRICS] "
                     << "Shaded: " << metrics->pixels_shaded
                     << " | Visible: " << metrics->pixels_visible
                     << " | Rejected: " << metrics->pixels_depth_rejected  
                     << " | Overdraw: " << std::fixed << std::setprecision(2) 
                     << metrics->overdraw_factor << "x" << std::endl;
        }
    }
    
    if (debug) {
        std::cout << "4. Drew " << last_frame_stats_.pixels_drawn << " pixels" << std::endl;
        std::cout << "=== END RENDER PIPELINE ===" << std::endl;
    }

    // Lock automatically released here when particles_view goes out of scope (RAII)
}

// ===== ENTITY PRE-CULLING =====
// Compute entity AABBs and frustum test them before per-particle culling
// Returns a vector where entity_culled[entity_id] = true if entity is off-screen
void RenderPipeline::pre_cull_entities(
    const std::vector<Particle>& particles,
    CameraSystem& camera_system,
    int render_width,
    int render_height,
    std::vector<bool>& entity_culled,
    int& entities_culled_count,
    int& entities_visible_count) {

    // Phase 1: Scan particles to find entity AABBs
    // entity_bounds[entity_id] = {min_x, min_y, min_z, max_x, max_y, max_z}
    struct EntityBounds {
        float min_x = std::numeric_limits<float>::max();
        float min_y = std::numeric_limits<float>::max();
        float min_z = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float max_y = std::numeric_limits<float>::lowest();
        float max_z = std::numeric_limits<float>::lowest();
        bool has_particles = false;
    };

    // Find max entity_id to size the vectors
    uint32_t max_entity_id = 0;
    for (const auto& p : particles) {
        if (p.entity_id > max_entity_id) {
            max_entity_id = p.entity_id;
        }
    }

    // Build entity bounds
    std::vector<EntityBounds> bounds(max_entity_id + 1);
    for (const auto& p : particles) {
        if (p.entity_id == 0) continue;  // Skip particles without entity

        auto& b = bounds[p.entity_id];
        b.has_particles = true;

        // Get particle half-extents. SPHERE is isotropic (size is its
        // diameter). BOX and ELLIPSOID use explicit per-axis dimensions
        // as diameters.
        bool isotropic = (p.shape == ParticleShape::SPHERE);
        float hw = (isotropic ? p.size : p.width)     * 0.5f;
        float hh = (isotropic ? p.size : p.height)    * 0.5f;
        float hd = (isotropic ? p.size : p.thickness) * 0.5f;

        // Expand bounds to include this particle
        b.min_x = std::min(b.min_x, p.x - hw);
        b.min_y = std::min(b.min_y, p.y - hh);
        b.min_z = std::min(b.min_z, p.z - hd);
        b.max_x = std::max(b.max_x, p.x + hw);
        b.max_y = std::max(b.max_y, p.y + hh);
        b.max_z = std::max(b.max_z, p.z + hd);
    }

    // Phase 2: Frustum test each entity AABB
    entity_culled.resize(max_entity_id + 1, false);
    entities_culled_count = 0;
    entities_visible_count = 0;

    const float ppu = camera_system.get_pixels_per_unit();
    const int margin = std::max(50, static_cast<int>(ppu * 2.0f));

    for (uint32_t eid = 1; eid <= max_entity_id; ++eid) {
        const auto& b = bounds[eid];
        if (!b.has_particles) continue;

        // Project AABB corners to screen and check if any is visible
        // For efficiency, just project center and use diagonal as radius
        float center_x = (b.min_x + b.max_x) * 0.5f;
        float center_y = (b.min_y + b.max_y) * 0.5f;
        float center_z = (b.min_z + b.max_z) * 0.5f;

        float half_diag = 0.5f * std::sqrt(
            (b.max_x - b.min_x) * (b.max_x - b.min_x) +
            (b.max_y - b.min_y) * (b.max_y - b.min_y) +
            (b.max_z - b.min_z) * (b.max_z - b.min_z)
        );

        int screen_x, screen_y;
        camera_system.world_to_screen(center_x, center_y, center_z, screen_x, screen_y);

        // Conservative screen radius (accounts for isometric projection)
        int screen_radius = static_cast<int>(half_diag * ppu * 1.5f);

        // Check if entity AABB is completely off-screen
        if (screen_x + screen_radius < -margin ||
            screen_x - screen_radius >= render_width + margin ||
            screen_y + screen_radius < -margin ||
            screen_y - screen_radius >= render_height + margin) {
            entity_culled[eid] = true;
            entities_culled_count++;
        } else {
            entities_visible_count++;
        }
    }

}

bool RenderPipeline::CachedSurfaces::matches(const Particle& p) const {
    // Exact float compare on purpose: any change at all must regenerate, and
    // an unmoved particle reproduces its fields bit-for-bit.
    return valid &&
           shape == static_cast<int>(p.shape) &&
           x == p.x && y == p.y && z == p.z &&
           rx == p.rotation_x && ry == p.rotation_y && rz == p.rotation_z &&
           w == p.width && h == p.height && t == p.thickness && size == p.size;
}

void RenderPipeline::CachedSurfaces::store_key(const Particle& p) {
    shape = static_cast<int>(p.shape);
    x = p.x; y = p.y; z = p.z;
    rx = p.rotation_x; ry = p.rotation_y; rz = p.rotation_z;
    w = p.width; h = p.height; t = p.thickness; size = p.size;
    valid = true;
}

void RenderPipeline::collect_surfaces(
    const std::vector<Particle>& particles,
    std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
    CameraSystem& camera_system,
    int render_width,
    int render_height,
    DepthBuffer& depth_buffer,
    const std::vector<bool>& entity_culled,
    const std::vector<int>& visible_particle_indices) {

    // OPTIMIZATION: If visible_particle_indices provided, iterate ONLY those
    // This skips 90% of particles (those in culled entities) entirely
    const bool use_visible_only = !visible_particle_indices.empty();

    if constexpr (Optimizations::USE_RENDER_SURFACE_CACHE) {
        // Size once, before any worker touches it: workers index disjoint
        // slots, so growth during the parallel section would be a data race.
        if (surface_geom_cache_.size() < particles.size()) {
            surface_geom_cache_.resize(particles.size());
        }
    }

    // PARALLEL SURFACE COLLECTION: Split particles across threads
    // Each thread generates surfaces independently, then merge results
    // Uses std::thread (no OpenMP needed - same pattern as tile_thread_pool.cpp)
    if constexpr (Optimizations::USE_PARALLEL_SURFACE_COLLECTION) {
        // Per-thread storage to avoid contention
        const int num_threads = Optimizations::WORKER_THREAD_COUNT;

        // PHASE 1.3: Use vector instead of deque for better cache locality
        // and to enable reserve() pre-allocation
        using SurfaceContainer = typename std::conditional<
            Optimizations::USE_SURFACE_ALLOC_BATCHING,
            std::vector<SurfaceRasterizer::SurfaceData>,
            std::deque<SurfaceRasterizer::SurfaceData>
        >::type;

        std::vector<SurfaceContainer> thread_surfaces(num_threads);

        // Per-thread statistics
        std::vector<int> thread_frustum_culled(num_threads, 0);
        std::vector<int> thread_occlusion_culled(num_threads, 0);
        std::vector<int> thread_processed(num_threads, 0);
        std::vector<int> thread_surfaces_culled(num_threads, 0);

        // ZOOM-AWARE FRUSTUM MARGIN:
        // At higher zoom (more pixels_per_unit), particles appear larger on screen.
        // A 1-unit particle at ppu=30 is ~30px, at ppu=100 is ~100px.
        // The margin must account for the largest particle being partially on-screen
        // while its center is off-screen. Scale by pixels_per_unit with reasonable bounds.
        const float ppu = camera_system.get_pixels_per_unit();
        const int margin = std::max(50, static_cast<int>(ppu * 2.0f));  // 2x particle size margin

        // Azimuth-dependent culling probe directions are per-frame
        // constants; computed once here, read by every worker.
        const float cull_az = camera_system.get_view_azimuth();
        const float cull_az_c = std::cos(cull_az);
        const float cull_az_s = std::sin(cull_az);

        // Lambda: Thread worker function
        auto worker_func = [&](int thread_id) {
            // OPTIMIZATION: If visible_particle_indices provided, iterate ONLY those
            // This skips 90% of iteration entirely (particles in culled entities)
            size_t item_count = use_visible_only ? visible_particle_indices.size() : particles.size();
            size_t start = (item_count * thread_id) / num_threads;
            size_t end = (item_count * (thread_id + 1)) / num_threads;

            auto& my_surfaces = thread_surfaces[thread_id];

            // PHASE 1.3: Pre-allocate surface storage to avoid repeated reallocations
            // Each particle generates up to 12 triangles (6 cube faces × 2 triangles/face)
            // Conservative estimate: 80% of particles pass culling
            if constexpr (Optimizations::USE_SURFACE_ALLOC_BATCHING) {
                size_t particles_this_thread = end - start;
                size_t estimated_surfaces = (particles_this_thread * 12 * 80) / 100;
                my_surfaces.reserve(estimated_surfaces);
            }
            int& my_frustum_culled = thread_frustum_culled[thread_id];
            int& my_occlusion_culled = thread_occlusion_culled[thread_id];
            int& my_processed = thread_processed[thread_id];
            int& my_surfaces_culled = thread_surfaces_culled[thread_id];

            for (size_t iter = start; iter < end; ++iter) {
                // Get actual particle index (direct or via visible_particle_indices)
                size_t i = use_visible_only ? static_cast<size_t>(visible_particle_indices[iter]) : iter;
                const auto& particle = particles[i];

                // ===== ENTITY-LEVEL FRUSTUM CULLING =====
                // SKIP if using visible_particle_indices (already filtered)
                // Only check if falling back to full iteration
                if (!use_visible_only) {
                    if constexpr (Optimizations::USE_ENTITY_FRUSTUM_CULLING) {
                        kg::EntityID eid = particle.entity_id;
                        if (eid < entity_culled.size() && entity_culled[eid]) {
                            my_frustum_culled++;  // Count as frustum-culled
                            continue;
                        }
                    }
                }

                // ========================================================
                // FLICKER DEBUG: Track specific particle IDs through pipeline
                // ========================================================
                // Track particles 1-25 (humanoid range) to detect render issues
                bool track_this_particle = (i >= 1 && i <= 25);
                static std::atomic<int> flicker_frame{0};
                static std::atomic<int> tracked_rendered{0};
                static std::atomic<int> tracked_culled{0};
                if (thread_id == 0 && i == start) {
                    // Reset counters at start of first thread's work
                    flicker_frame++;
                    if (flicker_frame % 30 == 0) {
                        std::cout << "[RENDER_TRACK] frame=" << flicker_frame.load()
                                  << " humanoid_particles rendered=" << tracked_rendered.load()
                                  << " culled=" << tracked_culled.load() << std::endl;
                    }
                    tracked_rendered = 0;
                    tracked_culled = 0;
                }

                // ===== PHASE 1.2 OPTIMIZATION: CACHE PROJECTIONS =====
                // Cache screen coordinates from frustum test to reuse in hierarchical test
                // Eliminates 1 redundant world_to_screen call per particle (20 ops saved)
                int cached_screen_x = -1, cached_screen_y = -1;
                bool screen_pos_cached = false;

                // ===== FRUSTUM CULLING =====
                if (Optimizations::USE_FRUSTUM_CULLING && !particle.is_light_source) {
                    // Check if we can use the fast path for no-rotation particles
                    // (no rotation means AABB = bounding sphere, skip 5/6 projections)
                    bool use_kinematic_fast_path = false;
                    if constexpr (Optimizations::USE_KINEMATIC_FAST_CULLING) {
                        use_kinematic_fast_path =
                            particle.rotation_x == 0.0f &&
                            particle.rotation_y == 0.0f &&
                            particle.rotation_z == 0.0f;
                    }

                    if (use_kinematic_fast_path) {
                        // ===== FAST PATH: Kinematic particles with no rotation =====
                        // No rotation means AABB = bounding sphere, skip 5/6 projections
                        camera_system.world_to_screen(particle.x, particle.y, particle.z,
                                                     cached_screen_x, cached_screen_y);
                        screen_pos_cached = true;

                        // Screen margin from the particle's largest half-
                        // extent. BOX and ELLIPSOID use explicit per-axis
                        // dimensions; SPHERE uses size as its diameter.
                        bool isotropic = (particle.shape == ParticleShape::SPHERE);
                        float half_size = isotropic
                            ? particle.size * 0.5f
                            : std::max({particle.width, particle.height, particle.thickness}) * 0.5f;
                        // 1.5x multiplier accounts for isometric diagonal projection
                        int screen_margin = static_cast<int>(ppu * half_size * 1.5f);

                        // Simple AABB check
                        if (cached_screen_x + screen_margin < -margin ||
                            cached_screen_x - screen_margin >= render_width + margin ||
                            cached_screen_y + screen_margin < -margin ||
                            cached_screen_y - screen_margin >= render_height + margin) {
                            my_frustum_culled++;
                            if (track_this_particle) tracked_culled++;
                            continue;
                        }
                        // On-screen: fall through to geometry generation
                    } else {
                        // ===== SLOW PATH: Rotation-safe bounding sphere =====
                        // Use a bounding sphere that encompasses all possible rotations of the particle.
                        // The sphere radius is the half-diagonal of the bounding box: sqrt(w² + h² + d²) / 2
                        // This ensures rotated particles are never incorrectly culled.

                        // Get actual particle dimensions. SPHERE is
                        // isotropic (size = diameter); BOX and ELLIPSOID
                        // use explicit per-axis dimensions.
                        float w, h, d;
                        if (particle.shape == ParticleShape::SPHERE) {
                            w = h = d = particle.size;
                        } else {
                            w = particle.width;
                            h = particle.height;
                            d = particle.thickness;
                        }

                        // Bounding sphere radius = half-diagonal of box (rotation-safe)
                        float half_diagonal = 0.5f * std::sqrt(w*w + h*h + d*d);

                        // Project center to screen
                        camera_system.world_to_screen(particle.x, particle.y, particle.z,
                                                     cached_screen_x, cached_screen_y);
                        screen_pos_cached = true;

                        // Project points on bounding sphere to find max screen radius
                        // In isometric projection, DIAGONAL directions project further than axis-aligned!
                        // iso_x = (x - y) * 0.866 → maximized by (1,-1,0) diagonal
                        // iso_y = (x + y) * 0.5 + z * h → maximized by (1,1,1) diagonal
                        int edge_x, edge_y;
                        int screen_radius = 0;

                        // Diagonal offset for XY plane: half_diagonal / sqrt(2)
                        float diag_xy = half_diagonal * 0.7071f;  // 1/sqrt(2)
                        // Diagonal offset for XYZ: half_diagonal / sqrt(3)
                        float diag_xyz = half_diagonal * 0.5774f;  // 1/sqrt(3)

                        // The extremal probe directions are frame
                        // properties, not world properties: under an
                        // azimuth orbit they are the classic diagonals
                        // rotated back into world space (CCW by a).
                        // Trig hoisted to per-frame (cull_az_*).
                        // R_ccw(a) * (1, -1): screen_x maximizer
                        const float ex_x = cull_az_c + cull_az_s, ex_y = cull_az_s - cull_az_c;
                        // R_ccw(a) * (1, 1): screen_y maximizer (with z)
                        const float ey_x = cull_az_c - cull_az_s, ey_y = cull_az_s + cull_az_c;

                        // Check rotated (1, -1, 0) diagonal - maximizes screen_x
                        camera_system.world_to_screen(particle.x + ex_x * diag_xy, particle.y + ex_y * diag_xy, particle.z,
                                                     edge_x, edge_y);
                        screen_radius = std::max(screen_radius, std::abs(edge_x - cached_screen_x));
                        screen_radius = std::max(screen_radius, std::abs(edge_y - cached_screen_y));

                        // Check rotated (-1, 1, 0) diagonal - maximizes negative screen_x
                        camera_system.world_to_screen(particle.x - ex_x * diag_xy, particle.y - ex_y * diag_xy, particle.z,
                                                     edge_x, edge_y);
                        screen_radius = std::max(screen_radius, std::abs(edge_x - cached_screen_x));
                        screen_radius = std::max(screen_radius, std::abs(edge_y - cached_screen_y));

                        // Check rotated (1, 1, 1) diagonal - maximizes screen_y (toward top)
                        camera_system.world_to_screen(particle.x + ey_x * diag_xyz, particle.y + ey_y * diag_xyz, particle.z + diag_xyz,
                                                     edge_x, edge_y);
                        screen_radius = std::max(screen_radius, std::abs(edge_x - cached_screen_x));
                        screen_radius = std::max(screen_radius, std::abs(edge_y - cached_screen_y));

                        // Check rotated (-1, -1, -1) diagonal - maximizes screen_y (toward bottom)
                        camera_system.world_to_screen(particle.x - ey_x * diag_xyz, particle.y - ey_y * diag_xyz, particle.z - diag_xyz,
                                                     edge_x, edge_y);
                        screen_radius = std::max(screen_radius, std::abs(edge_x - cached_screen_x));
                        screen_radius = std::max(screen_radius, std::abs(edge_y - cached_screen_y));

                        // Also check Z-axis for tall objects
                        camera_system.world_to_screen(particle.x, particle.y, particle.z + half_diagonal,
                                                     edge_x, edge_y);
                        screen_radius = std::max(screen_radius, std::abs(edge_x - cached_screen_x));
                        screen_radius = std::max(screen_radius, std::abs(edge_y - cached_screen_y));

                        // Check if bounding circle is completely outside viewport (with margin)
                        // Margin ensures particles just off-screen still render for smooth scrolling
                        if (cached_screen_x + screen_radius < -margin || cached_screen_x - screen_radius >= render_width + margin ||
                            cached_screen_y + screen_radius < -margin || cached_screen_y - screen_radius >= render_height + margin) {
                            my_frustum_culled++;
                            if (track_this_particle) tracked_culled++;
                            continue; // Skip this particle entirely
                        }
                    }
                }

                my_processed++;

                // ===== OCCLUSION CULLING =====
                // Test if particle is completely hidden behind other geometry
                // NOTE: This requires depth buffer from previous frame to work properly
                // On first frame or when depth buffer is empty, skip occlusion culling
                static int frame_count = 0;
                if (thread_id == 0) frame_count++;  // Only thread 0 increments

                // Skip occlusion for floor-level particles (z < 0.5m have nothing behind them)
                // NOTE: Previously used is_kinematic flag, now use position heuristic
                bool skip_occlusion = Optimizations::USE_KINEMATIC_FAST_CULLING && (particle.z < 0.5f);
                if (Optimizations::USE_OCCLUSION_CULLING && !particle.is_light_source &&
                    !skip_occlusion && frame_count > 1) {

                    // OPTIMIZATION: Test center point first using cached frustum projection
                    // If center is visible, skip all 8 expensive corner projections.
                    // Depth must be computed with the SAME metric the rasterizer wrote
                    // into the depth buffer — use the projection-aware CameraSystem
                    // helper instead of raw particle.z so iso/bird's-eye/cabinet all
                    // compare apples to apples.
                    bool center_visible = false;
                    if (screen_pos_cached &&
                        cached_screen_x >= 0 && cached_screen_x < render_width &&
                        cached_screen_y >= 0 && cached_screen_y < render_height) {
                        float center_depth = depth_buffer.get_depth(cached_screen_x, cached_screen_y);
                        float my_depth = camera_system.compute_depth(particle.x, particle.y, particle.z);
                        if (my_depth <= center_depth || center_depth >= std::numeric_limits<float>::max()) {
                            center_visible = true;  // Center visible, skip corner testing
                        }
                    }

                    // Only test 8 corners if center point failed depth test
                    if (!center_visible) {
                        // Test 8 corners of the particle's actual bounding box.
                        // SPHERE is isotropic (size = diameter). BOX and
                        // ELLIPSOID use explicit per-axis dimensions.
                        float half_w, half_h, half_d;
                        if (particle.shape == ParticleShape::SPHERE) {
                            half_w = half_h = half_d = particle.size * 0.5f;
                        } else {
                            half_w = particle.width * 0.5f;
                            half_h = particle.height * 0.5f;
                            half_d = particle.thickness * 0.5f;
                        }
                        bool any_corner_visible = false;
                        bool any_corner_on_screen = false;

                        for (int corner = 0; corner < 8; corner++) {
                            float corner_x = particle.x + ((corner & 1) ? half_w : -half_w);
                            float corner_y = particle.y + ((corner & 2) ? half_h : -half_h);
                            float corner_z = particle.z + ((corner & 4) ? half_d : -half_d);

                            int screen_x, screen_y;
                            camera_system.world_to_screen(corner_x, corner_y, corner_z,
                                                         screen_x, screen_y);

                            if (screen_x >= 0 && screen_x < render_width &&
                                screen_y >= 0 && screen_y < render_height) {
                                any_corner_on_screen = true;

                                float existing_depth = depth_buffer.get_depth(screen_x, screen_y);
                                // Projection-aware depth: matches the metric the
                                // rasterizer writes via SurfaceRasterizer::calculate_pixel_depth.
                                float corner_depth = camera_system.compute_depth(corner_x, corner_y, corner_z);

                                if (corner_depth <= existing_depth || existing_depth >= std::numeric_limits<float>::max()) {
                                    any_corner_visible = true;
                                    break;
                                }
                            }
                        }

                        // Only cull if we tested at least one corner and none were visible
                        if (any_corner_on_screen && !any_corner_visible) {
                            my_occlusion_culled++;
                            my_processed--;
                            if (track_this_particle) tracked_culled++;
                            continue;
                        }
                    }
                }

                // Track successfully rendered humanoid particles
                if (track_this_particle) tracked_rendered++;

                // Get surfaces for this particle into a REUSED buffer. This
                // used to be `auto particle_surfaces = particle.GetSurfaces();`
                // — a heap allocation per particle per frame, on the path that
                // study S7 showed dominates CPU render cost. The buffer is
                // thread_local so each worker keeps its own capacity.
                // NOTE: geometry cache still not used here - would require
                // prebuild of ALL particles; lighting/BVH have their own.
                static thread_local std::vector<Surface> scratch_surfaces;
                const std::vector<Surface>* surfaces_ptr;
                if constexpr (Optimizations::USE_RENDER_SURFACE_CACHE) {
                    // Reuse geometry while the particle has not moved. Threads
                    // own disjoint particle indices, and the vector is sized
                    // before the parallel section, so per-index access needs no
                    // lock. The key check is what makes index reuse under
                    // swap-and-pop safe.
                    auto& entry = surface_geom_cache_[i];
                    if (!entry.matches(particle)) {
                        particle.GetSurfacesInto(entry.surfaces);
                        entry.store_key(particle);
                    } else {
                        LOGO_COUNT(::logosphere::telemetry::Counter::SurfaceCacheHit);
                    }
                    surfaces_ptr = &entry.surfaces;
                } else {
                    particle.GetSurfacesInto(scratch_surfaces);
                    surfaces_ptr = &scratch_surfaces;
                }
                const std::vector<Surface>& particle_surfaces = *surfaces_ptr;

                // Add all surfaces from this particle (thread-local storage)
                for (size_t surf_idx = 0; surf_idx < particle_surfaces.size(); ++surf_idx) {
                    // Back-face reject BEFORE the 144-byte SurfaceData exists.
                    // Same predicate cull_surfaces applied one pass later, same
                    // light-source exemption, so the surviving set and its order
                    // are unchanged. Culling here cannot be folded into the
                    // geometry cache: the cache is keyed on the particle
                    // transform and the camera moves independently of it.
                    if constexpr (Optimizations::CULL_SURFACES_AT_GENERATION) {
                        if (!particle.is_light_source) {
                            const Surface& s = particle_surfaces[surf_idx];
                            if (!camera_system.should_render_surface(
                                    s.x, s.y, s.z, s.nx, s.ny, s.nz, s.width, s.height)) {
                                LOGO_COUNT(::logosphere::telemetry::Counter::SurfacesCulled);
                                my_surfaces_culled++;
                                continue;
                            }
                        }
                    }
                    SurfaceRasterizer::SurfaceData data;
                    data.surface = particle_surfaces[surf_idx];
                    data.particle_r = particle.r;
                    data.particle_g = particle.g;
                    data.particle_b = particle.b;
                    data.particle_a = particle.a;
                    data.is_light_source = particle.is_light_source;
                    data.emission_strength = particle.emission_strength;
                    // CRITICAL: Store array index in G-buffer for transform/pattern lookup
                    // Used by is_light_source_map and particle_transforms in deferred lighting pass
                    // Must match indexing used in STEP 5 and STEP 6 of prepare_async_data()
                    data.surface.particle_id = static_cast<int>(i);
                    data.particle_index = static_cast<int>(i);
                    data.surface_index = static_cast<int>(surf_idx);
                    data.closest_distance = 0.0f; // Will be calculated later

                    my_surfaces.push_back(data);
                }
            } // End particle loop
        }; // End lambda

        // Spawn threads
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back(worker_func, t);
        }

        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        // MERGE: Combine all thread results into main surfaces deque
        int particles_frustum_culled = 0;
        int particles_occlusion_culled = 0;
        int particles_processed = 0;
        int surfaces_backface_culled = 0;

        for (int t = 0; t < num_threads; ++t) {
            // Merge surfaces
            surfaces.insert(surfaces.end(),
                           std::make_move_iterator(thread_surfaces[t].begin()),
                           std::make_move_iterator(thread_surfaces[t].end()));

            // Sum statistics
            particles_frustum_culled += thread_frustum_culled[t];
            particles_occlusion_culled += thread_occlusion_culled[t];
            particles_processed += thread_processed[t];
            surfaces_backface_culled += thread_surfaces_culled[t];
        }
        // The debug overlay reads this through EngineMetrics. cull_surfaces
        // used to own it; with the reject moved upstream, this is the source.
        if constexpr (Optimizations::CULL_SURFACES_AT_GENERATION) {
            last_frame_stats_.surfaces_culled = surfaces_backface_culled;
        }

        // Output merged statistics
        static int frame_counter = 0;
        if ((frame_counter++ % 60 == 0)) {
            if (Optimizations::USE_FRUSTUM_CULLING || Optimizations::USE_OCCLUSION_CULLING) {
                std::cout << "[CULLING STATS] Particles: " << particles.size()
                          << " | Frustum: " << particles_frustum_culled
                          << " | Occlusion: " << particles_occlusion_culled
                          << " | Processed: " << particles_processed
                          << " | Surfaces: " << surfaces.size() << std::endl;
            }
        }

        return; // Parallel path done
    } // End if constexpr parallel

    // FALLBACK: Serial path (USE_PARALLEL_SURFACE_COLLECTION = false)
    // Original serial implementation (kept for fallback)
    // Note: Rest of function continues below with original serial code
}

void RenderPipeline::cull_surfaces(
    std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
    CameraSystem& camera_system,
    EngineMetrics* metrics) {
    
    if constexpr (Optimizations::CULL_SURFACES_AT_GENERATION) {
        // Already rejected at birth in collect_surfaces. Nothing survives here
        // that would fail the test, so the pass reduces to its bookkeeping.
        LOGO_COUNT_N(::logosphere::telemetry::Counter::SurfacesDrawn, surfaces.size());
        last_frame_stats_.surfaces_projected = surfaces.size();
        if (metrics) metrics->surfaces_culled = last_frame_stats_.surfaces_culled;
        return;
    }

    // Remove surfaces that shouldn't be rendered
    auto new_end = std::remove_if(surfaces.begin(), surfaces.end(),
        [&camera_system, metrics, this](const SurfaceRasterizer::SurfaceData& data) {
            // Light sources bypass culling - we want to see them from all angles
            if (data.is_light_source) {
                return false; // Always keep light source surfaces
            }
            
            bool should_render = camera_system.should_render_surface(
                data.surface.x, data.surface.y, data.surface.z,
                data.surface.nx, data.surface.ny, data.surface.nz,
                data.surface.width, data.surface.height
            );
            
            if (!should_render) {
                if (metrics) metrics->surfaces_culled++;
                last_frame_stats_.surfaces_culled++;
                return true; // Remove this surface
            }
            
            return false; // Keep this surface
        });
    
    surfaces.erase(new_end, surfaces.end());
    LOGO_COUNT_N(::logosphere::telemetry::Counter::SurfacesDrawn, surfaces.size());
    last_frame_stats_.surfaces_projected = surfaces.size();
}

void RenderPipeline::calculate_distances(
    std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
    CameraSystem& camera_system) {
    
    // Get camera position for distance calculations
    float cam_x, cam_y, cam_z;
    camera_system.get_position(cam_x, cam_y, cam_z);
    
    // Calculate distance for each surface
    for (auto& surf_data : surfaces) {
        const Surface& surface = surf_data.surface;

        // Find closest vertex to camera
        float min_distance = std::numeric_limits<float>::max();
        for (int v = 0; v < surface.vertex_count; ++v) {
            float dx = surface.vertices[v][0] - cam_x;
            float dy = surface.vertices[v][1] - cam_y;
            float dz = surface.vertices[v][2] - cam_z;
            float distance = std::sqrt(dx*dx + dy*dy + dz*dz);
            min_distance = std::min(min_distance, distance);
        }
        
        surf_data.closest_distance = min_distance;
    }
}

void RenderPipeline::sort_surfaces(
    std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
    EngineMetrics* metrics) {
    
    // Skip sorting when using depth buffer
    if (Optimizations::USE_DEPTH_BUFFER) {
        // With depth buffer, rendering order doesn't matter
        return;
    }
    
    // Painter's algorithm: sort back-to-front
    std::sort(surfaces.begin(), surfaces.end(),
        [](const SurfaceRasterizer::SurfaceData& a, const SurfaceRasterizer::SurfaceData& b) {
            return a.closest_distance > b.closest_distance;
        });
}

void RenderPipeline::rasterize_surfaces(
    const std::deque<SurfaceRasterizer::SurfaceData>& surfaces,
    PixelBuffer& pixel_buffer,
    DepthBuffer& depth_buffer,
    SparseObjectMap& object_map,
    CameraSystem& camera_system,
    LightSystem& light_system,
    IPixelShader& pixel_shader,
    const std::vector<Particle>& particles,
    const BVH* shadow_bvh,
    EngineMetrics* metrics) {

    int pixels_drawn = 0;

    // GPU Rasterization Path (Phase III)
    if (Optimizations::USE_GPU_RASTERIZATION) {
        // GPURasterizer::initialize is idempotent: early-exits if the
        // (width, height) matches its last configured resolution. Call
        // unconditionally so every RenderPipeline instance gets its
        // own rasterizer up to date. The previous function-local
        // static cache was incorrect for multi-Engine-per-process
        // (it was shared across instances; the 2nd Engine's rasterizer
        // saw cached-hit and never got initialized).
        if (!gpu_rasterizer_.initialize(pixel_buffer.width(),
                                        pixel_buffer.height())) {
            std::cerr << "[GPU_RASTERIZATION] FATAL: Failed to initialize GPU rasterizer" << std::endl;
            std::exit(1);
        }

        // =========================================================================
        // ASYNC GPU_PREP: Overlap CPU prep with GPU execution
        // =========================================================================
        // STRATEGY:
        // - Frame N: Wait for prep N to complete → dispatch GPU with buffer N
        // - Frame N: Launch async prep N+1 while GPU renders frame N
        // - Frame N+1: Use pre-prepared data (should be ready)
        //
        // Triple-buffering ensures writer ≠ reader (no race conditions)
        // =========================================================================

        int buffer_idx;

        if (::logosphere::get_async_gpu_prep()) {
            // First frame initialization (also triggers after reset_temporal_state)
            if (first_frame_) {
                // First frame: prepare synchronously in buffer 0
                buffer_idx = 0;
                prepare_gpu_data(buffer_idx, surfaces, particles, camera_system);
                render_buffer_index_.store(0, std::memory_order_release);
                prep_ready_.store(true, std::memory_order_release);
                first_frame_ = false;

                if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
                    static bool logged = false;
                    if (!logged) {
                        std::cout << "[ASYNC_PREP] Initialized - first frame prepared synchronously" << std::endl;
                        logged = true;
                    }
                }
            } else {
                // =========================================================
                // ASYNC BUFFER HANDOFF WITH TIMEOUT FALLBACK
                // =========================================================
                // Normal case: async prep finishes before GPU, grab new buffer.
                // Slow case: BVH rebuild (580ms for 1M tris), timeout after 500ms
                //            and reuse previous buffer to avoid stall.
                //
                // WHY 500ms: Long enough for typical frame prep (<100ms).
                //            Short enough to detect actual hangs.
                //            Vision cone stays in sync (geometry matches).
                // =========================================================
                ::logosphere::telemetry::ScopedPhase _wait(
                    ::logosphere::telemetry::Phase::RenderPrepWait);
                std::unique_lock<std::mutex> lock(prep_mutex_);

                bool ready = prep_ready_.load(std::memory_order_acquire) ||
                             prep_cv_.wait_for(lock, std::chrono::milliseconds(500),
                                 [this]{ return prep_ready_.load(std::memory_order_acquire); });

                if (ready) {
                    buffer_idx = render_buffer_index_.load(std::memory_order_acquire);
                    last_good_buffer_idx_ = buffer_idx;  // Track last successful buffer
                    skip_next_async_prep_ = false;       // OK to start new async prep
                } else {
                    // Prep not ready (slow BVH rebuild) - reuse previous buffer
                    buffer_idx = last_good_buffer_idx_;
                    skip_next_async_prep_ = true;        // DON'T start new prep - old one still running!
                }
            }

        } else {
            // SYNC MODE: Always use buffer 0, call synchronously
            buffer_idx = 0;
            prepare_gpu_data(buffer_idx, surfaces, particles, camera_system);
        }

        // Create local references to prepared data (for GPU dispatch)
        auto& gpu_triangles = gpu_triangles_[buffer_idx];
        auto& gpu_transparent_triangles = gpu_transparent_triangles_[buffer_idx];
        auto& shadow_triangles = shadow_triangles_[buffer_idx];
        auto& shadow_triangle_bvh = shadow_bvh_[buffer_idx];
        auto& entity_bvh = entity_bvh_[buffer_idx];  // Entity-level BVH with directional culling
        auto& light_data = light_data_[buffer_idx];
        auto& is_light_source_map = is_light_source_map_[buffer_idx];
        auto& is_dynamic_map      = is_dynamic_map_[buffer_idx];
        auto& particle_transforms = particle_transforms_[buffer_idx];

        // Calculate derived values from prepared data
        uint32_t light_count = light_data.size() / 40;  // 40 bytes per light (matches LightData struct: 3*4 + 4 + 4 + 4 + 3*4 + 4)
        // Publish to telemetry here: this is where the count is actually
        // known. Without it every metrics record says lights=0 and a
        // light-scaling study is impossible.
        ::logosphere::telemetry::set_light_count(light_count);
        uint32_t max_entity_id = is_light_source_map.empty() ? 0 : is_light_source_map.size() - 1;
        uint32_t particle_count = particle_transforms.empty() ? 0 : static_cast<uint32_t>(particle_transforms.size() / 32);  // 32 bytes per ParticleTransform

        // Data is now prepared! Continue with tile binning and GPU dispatch
        //
        // OLD PREP CODE REMOVED: Now in prepare_gpu_data()
        // - GPU triangles conversion: gpu_triangles
        // - Shadow triangles generation: shadow_triangles
        // - BVH build/refit: shadow_triangle_bvh
        // - Light data packing: light_data
        // - Light source mapping: is_light_source_map

        // ===== TILE-BASED BINNING (GPU Bandwidth Optimization) =====
        // Pre-assign triangles to screen tiles to reduce GPU bandwidth
        // Without binning: 1.68M pixels × 7,200 triangles = 12.1B tests (1,900 GB bandwidth)
        // With binning: ~100M tests (16 GB bandwidth) - 120x reduction

        std::vector<uint32_t> tile_indices;   // Flattened triangle indices
        std::vector<uint32_t> tile_offsets;   // Offset into tile_indices per tile
        std::vector<uint32_t> tile_counts;    // Triangle count per tile
        int tiles_x = 0, tiles_y = 0;         // Tile grid dimensions

        if (Optimizations::USE_GPU_TILE_BINNING && !gpu_triangles.empty()) {
            // System-level timing (once per frame - acceptable per PERFORMANCE_RESEARCH.md)
            auto binning_start = Clock::now();

::logosphere::telemetry::ScopedPhase _bin(::logosphere::telemetry::Phase::PrepBinning);
            const int TILE_SIZE = Optimizations::GPU_BINNING_TILE_SIZE;
            tiles_x = (pixel_buffer.width() + TILE_SIZE - 1) / TILE_SIZE;
            tiles_y = (pixel_buffer.height() + TILE_SIZE - 1) / TILE_SIZE;
            const int total_tiles = tiles_x * tiles_y;

            // Safety check: Limit tile count to prevent excessive memory usage
            const int MAX_TILES = 10000;  // ~3200x3200 at 64x64 tiles
            std::vector<std::vector<uint32_t>> tile_triangle_lists;  // Final merged result

            if (total_tiles > MAX_TILES) {
                std::cerr << "[TILE_BINNING] WARNING: Tile count " << total_tiles
                          << " exceeds maximum " << MAX_TILES << ", disabling binning for this frame" << std::endl;
            } else {
                // PARALLEL TILE BINNING OPTIMIZATION
                // Each thread processes a subset of triangles independently
                // Expected speedup: 4x with 4 threads (12,000 triangles × 425 tiles = 5.1M tests)

                const int num_threads = Optimizations::WORKER_THREAD_COUNT;
                const uint32_t triangles_per_thread = (gpu_triangles.size() + num_threads - 1) / num_threads;

                // Per-thread tile lists (each thread writes independently, no mutex needed)
                std::vector<std::vector<std::vector<uint32_t>>> thread_tile_lists(num_threads);
                for (int t = 0; t < num_threads; ++t) {
                    thread_tile_lists[t].resize(total_tiles);
                }

                // Parallel binning: Each thread processes a range of triangles
                #pragma omp parallel for num_threads(num_threads) if(gpu_triangles.size() > 100)
                for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
                    uint32_t tri_start = thread_id * triangles_per_thread;
                    uint32_t tri_end = std::min(tri_start + triangles_per_thread, (uint32_t)gpu_triangles.size());

                    for (uint32_t tri_idx = tri_start; tri_idx < tri_end; ++tri_idx) {
                        const auto& tri = gpu_triangles[tri_idx];

                        // Compute triangle bounding box in screen space
                        float min_x = std::min({tri.x0, tri.x1, tri.x2});
                        float max_x = std::max({tri.x0, tri.x1, tri.x2});
                        float min_y = std::min({tri.y0, tri.y1, tri.y2});
                        float max_y = std::max({tri.y0, tri.y1, tri.y2});

                        // Skip off-screen triangles
                        if (max_x < 0.0f || max_y < 0.0f ||
                            min_x >= pixel_buffer.width() || min_y >= pixel_buffer.height()) {
                            continue;
                        }

                        // Convert to tile coordinates with clamping
                        int tile_min_x = std::max(0, static_cast<int>(min_x) / TILE_SIZE);
                        int tile_max_x = std::min(tiles_x - 1, static_cast<int>(max_x) / TILE_SIZE);
                        int tile_min_y = std::max(0, static_cast<int>(min_y) / TILE_SIZE);
                        int tile_max_y = std::min(tiles_y - 1, static_cast<int>(max_y) / TILE_SIZE);

                        // Safety: Ensure tile coordinates are valid
                        if (tile_min_x < 0 || tile_max_x >= tiles_x ||
                            tile_min_y < 0 || tile_max_y >= tiles_y) {
                            continue;
                        }

                        // Add triangle index to all overlapping tiles (thread-local, no contention)
                        for (int ty = tile_min_y; ty <= tile_max_y; ++ty) {
                            for (int tx = tile_min_x; tx <= tile_max_x; ++tx) {
                                int tile_idx = ty * tiles_x + tx;
                                if (tile_idx >= 0 && tile_idx < total_tiles) {
                                    thread_tile_lists[thread_id][tile_idx].push_back(tri_idx);
                                }
                            }
                        }
                    }
                }

                // Merge thread-local results into final tile lists
                tile_triangle_lists.resize(total_tiles);
                for (int tile_idx = 0; tile_idx < total_tiles; ++tile_idx) {
                    // Count total triangles for this tile across all threads
                    size_t total_count = 0;
                    for (int t = 0; t < num_threads; ++t) {
                        total_count += thread_tile_lists[t][tile_idx].size();
                    }

                    // Reserve and merge
                    tile_triangle_lists[tile_idx].reserve(total_count);
                    for (int t = 0; t < num_threads; ++t) {
                        auto& thread_list = thread_tile_lists[t][tile_idx];
                        tile_triangle_lists[tile_idx].insert(
                            tile_triangle_lists[tile_idx].end(),
                            thread_list.begin(),
                            thread_list.end()
                        );
                    }
                }
            }

            // Flatten tile lists into single buffer for GPU (only if binning succeeded)
            // Format: tile_offsets[tile_idx] → index into tile_indices
            //         tile_counts[tile_idx] → number of triangles for this tile
            //         tile_indices[offset...offset+count] → triangle indices for this tile
            if (!tile_triangle_lists.empty() && total_tiles <= MAX_TILES) {
                tile_offsets.reserve(total_tiles);
                tile_counts.reserve(total_tiles);

                // First pass: compute offsets and counts
                uint32_t current_offset = 0;
                for (const auto& tile_list : tile_triangle_lists) {
                    tile_offsets.push_back(current_offset);
                    tile_counts.push_back(static_cast<uint32_t>(tile_list.size()));
                    current_offset += static_cast<uint32_t>(tile_list.size());
                }

                // Second pass: flatten all triangle indices
                tile_indices.reserve(current_offset);
                for (const auto& tile_list : tile_triangle_lists) {
                    tile_indices.insert(tile_indices.end(), tile_list.begin(), tile_list.end());
                }

                // System-level timing output (counts are cheap!)
                uint32_t total_assignments = current_offset;
                auto binning_ms = Duration(Clock::now() - binning_start).count();

                // Frame-based logging (follows PERFORMANCE_RESEARCH.md best practices)
                static int binning_frame_count = 0;
                static int last_particle_count_binning = 0;
                bool should_log = false;

                // Log when slow (>5ms) OR particle count changes (always)
                if (binning_ms > 5.0 || (int)particles.size() != last_particle_count_binning) {
                    should_log = true;
                    last_particle_count_binning = particles.size();
                }

                // PROFILING: Also log every N frames when flag enabled (diagnose bottlenecks)
                if constexpr (Optimizations::ENABLE_PROFILING && Optimizations::PROFILE_TILE_BINNING) {
                    if ((++binning_frame_count % Optimizations::PROFILE_SAMPLE_RATE) == 0) {
                        should_log = true;
                    }
                }

                if (should_log) {
                    float avg_triangles_per_tile = total_assignments / static_cast<float>(total_tiles);
                    std::cout << "[TILE_BINNING] " << binning_ms << "ms - "
                              << gpu_triangles.size() << " render tris → "
                              << total_assignments << " assignments ("
                              << std::fixed << std::setprecision(1) << avg_triangles_per_tile << " avg/tile)"
                              << std::endl;
                }
            }
        } else {
            // No binning - pass empty tile data (GPU will fall back to naive approach)
            // This happens when USE_GPU_TILE_BINNING=false or no triangles
        }

        // Get native buffer
        auto& native_buffer = pixel_buffer.get_native_buffer();

        // Convert depth buffer from float to uint32_t for GPU
        // GPU uses uint32_t depth buffer (0 = far, UINT32_MAX = near)
        static std::vector<uint32_t> gpu_depth_buffer;

        // DEBUG: Track reallocation to detect use-after-free
        static int resize_frame = 0;
        resize_frame++;
        void* old_ptr = gpu_depth_buffer.empty() ? nullptr : gpu_depth_buffer.data();
        size_t old_capacity = gpu_depth_buffer.capacity();

        gpu_depth_buffer.resize(pixel_buffer.width() * pixel_buffer.height());

        if (old_ptr != nullptr && gpu_depth_buffer.data() != old_ptr) {
            std::cout << "[REALLOC_DETECTED] frame=" << resize_frame
                      << " old_ptr=" << old_ptr
                      << " new_ptr=" << (void*)gpu_depth_buffer.data()
                      << " old_cap=" << old_capacity
                      << " new_cap=" << gpu_depth_buffer.capacity()
                      << std::endl;
        }

        // GPU will clear depth buffer - no CPU clear needed
        // (Removed wasteful std::fill - GPU blit encoder clears it)

        // REMOVED BUG: Premature flag reset caused race condition
        // The flag is reset in mark_frame_presented() AFTER actual presentation
        // Resetting here at the START of render() caused present() to always see false
        // gpu_frame_ready_.store(false, std::memory_order_release);

        // Allocate context for callback (will be freed by callback)
        // CRITICAL: Include pointer to RenderPipeline for callback synchronization
        struct CallbackContextWithSync {
            uint32_t* cpu_framebuffer;
            uint32_t* cpu_depth;
            SparseObjectMap* object_map;  // NEW: For GPU object picking
            int w;
            int h;
            RenderPipeline* pipeline;  // NEW: For accessing mutex and flag
        };

        CallbackContextWithSync* callback_ctx = new CallbackContextWithSync{
            native_buffer.data(),
            gpu_depth_buffer.data(),
            &object_map,  // NEW: Pass object_map for GPU object picking
            pixel_buffer.width(),
            pixel_buffer.height(),
            this  // Pass RenderPipeline pointer for synchronization
        };

        // Rasterize with GPU lighting + BVH shadows (ASYNC EXECUTION)
        // Use async method to enable CPU/GPU overlap
        // Results are written to shared memory buffers accessible via completion callback
        ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::RenderSubmit);
        auto submit_start = Clock::now();

        // Choose rendering architecture: Forward (inline shadows) vs Deferred (3-pass)
        if (Optimizations::USE_DEFERRED_RENDERING) {
            static int path_log_count = 0;
            if (path_log_count < 3 && light_count > 0) {
                std::cout << "[ITER0_PIPELINE] Using DEFERRED rendering path, light_count=" << light_count << std::endl;
                std::cout.flush();
                path_log_count++;
            }
            // DEFERRED RENDERING (3-pass architecture)
            // Pass 1: G-buffer rasterization (geometry only)
            // Pass 2: Shadow rays (coherent BVH traversal - 80-90% warp efficiency)
            // Pass 3: Apply lighting (simple math: G-buffer + shadows → framebuffer)
            // PERFORMANCE: 19× faster at 250 particles (10,000ms → 515ms)

            // Set look-at target position for shadow distance fade (Pass 3)
            // Use get_look_at_target() which returns the ACTUAL player/target position.
            // This is set by both look_at() and update_follow_target().
            // camera.get_position() would return the camera's 3D offset position, not
            // where the player is - which would cause the fade to be off-center.
            {
                float target_x, target_y;
                camera_system.get_look_at_target(target_x, target_y);
                gpu_rasterizer_.set_shadow_culling_camera(target_x, target_y);
                gpu_rasterizer_.set_view_azimuth(camera_system.get_view_azimuth());
                gpu_rasterizer_.set_shadow_projection_params(camera_system.get_pixels_per_unit());

                // Per-particle "is_dynamic" map for the vision-cone
                // memory blend. Same indexing as is_light_source_map
                // (particle_id). Built each frame from
                // Particle::is_dynamic in prepare_gpu_data().
                gpu_rasterizer_.set_dynamic_particle_map(
                    is_dynamic_map.empty() ? nullptr : is_dynamic_map.data(),
                    is_dynamic_map.size());
            }

            // DEBUG: Log entity BVH stats once every 200 frames
            {
                static int gpu_ebvh_log = 0;
                if (gpu_ebvh_log++ % 200 == 0 && entity_bvh.is_ready()) {
                    std::cout << "[GPU ENTITY_BVH] nodes=" << entity_bvh.get_node_count()
                              << " groups=" << entity_bvh.get_group_count() << std::endl;
                }
            }

            // EMPTY SCENE FIX: If no triangles to render, signal frame ready immediately
            // This prevents infinite wait in engine.cpp's while(!is_frame_ready()) loop
            // but still allow the rest of the pipeline (async prep, stats) to run
            if (gpu_triangles.empty()) {
                gpu_frame_ready_.store(true, std::memory_order_release);
                delete callback_ctx;  // Clean up allocated context
                // Don't return - continue to async prep section below
            } else {
            // Upload transparent triangles BEFORE deferred async dispatch
            // (must be before rasterize_triangles_deferred_async which increments buffer index)
            if constexpr (Optimizations::USE_TRANSPARENCY) {
                gpu_rasterizer_.set_transparent_triangles(
                    gpu_transparent_triangles.empty() ? nullptr : gpu_transparent_triangles.data(),
                    (uint32_t)gpu_transparent_triangles.size());
            }
            if constexpr (Optimizations::RASTER_BBOX_STREAM) {
                // Reject-path bbox stream: same floats, same casts as the
                // kernel's per-pixel bbox ((int) trunc of float min/max),
                // computed once per triangle instead of once per pixel.
                static std::vector<int32_t> tri_bboxes;
                tri_bboxes.resize(gpu_triangles.size() * 4);
                for (size_t i = 0; i < gpu_triangles.size(); ++i) {
                    const auto& t = gpu_triangles[i];
                    tri_bboxes[i * 4 + 0] = (int32_t)std::min(t.x0, std::min(t.x1, t.x2));
                    tri_bboxes[i * 4 + 1] = (int32_t)std::min(t.y0, std::min(t.y1, t.y2));
                    tri_bboxes[i * 4 + 2] = (int32_t)std::max(t.x0, std::max(t.x1, t.x2));
                    tri_bboxes[i * 4 + 3] = (int32_t)std::max(t.y0, std::max(t.y1, t.y2));
                }
                gpu_rasterizer_.set_triangle_bboxes(
                    tri_bboxes.empty() ? nullptr : tri_bboxes.data(),
                    (uint32_t)gpu_triangles.size());
            }
            // Normal case: dispatch GPU work
            gpu_rasterizer_.rasterize_triangles_deferred_async(
                gpu_triangles.data(),
                (uint32_t)gpu_triangles.size(),
                light_data.empty() ? nullptr : light_data.data(),
                light_count,
                (shadow_triangle_bvh.is_ready() && g_pass_flat_shadow_bvh.load(std::memory_order_relaxed))
                    ? shadow_triangle_bvh.get_nodes() : nullptr,
                (shadow_triangle_bvh.is_ready() && g_pass_flat_shadow_bvh.load(std::memory_order_relaxed))
                    ? shadow_triangle_bvh.get_node_count() : 0,
                shadow_triangles.empty() ? nullptr : shadow_triangles.data(),
                (uint32_t)shadow_triangles.size(),
                // Entity BVH data (for directional culling)
                (entity_bvh.is_ready() && g_pass_entity_shadow_bvh.load(std::memory_order_relaxed))
                    ? entity_bvh.get_nodes() : nullptr,
                (entity_bvh.is_ready() && g_pass_entity_shadow_bvh.load(std::memory_order_relaxed))
                    ? (uint32_t)entity_bvh.get_node_count() : 0,
                (entity_bvh.is_ready() && g_pass_entity_shadow_bvh.load(std::memory_order_relaxed))
                    ? entity_bvh.get_groups() : nullptr,
                (entity_bvh.is_ready() && g_pass_entity_shadow_bvh.load(std::memory_order_relaxed))
                    ? (uint32_t)entity_bvh.get_group_count() : 0,
                // Tile binning data (pass empty if binning disabled)
                tile_indices.empty() ? nullptr : tile_indices.data(),
                tile_offsets.empty() ? nullptr : tile_offsets.data(),
                tile_counts.empty() ? nullptr : tile_counts.data(),
                tiles_x,
                tiles_y,
                // Light source mapping (for emissive rendering)
                is_light_source_map.empty() ? nullptr : is_light_source_map.data(),
                max_entity_id + 1,
                // Particle transforms (for local coordinate pattern rendering)
                particle_transforms.empty() ? nullptr : particle_transforms.data(),
                particle_count,
                // Completion callback: Copy results from GPU shared memory to CPU buffers
                // CRITICAL: This runs on Metal's background thread - must synchronize!
                [](uint32_t* framebuffer, uint32_t* depth_buffer, void* gbuffer,
                   int width, int height, void* user_data) {
                    CallbackContextWithSync* ctx = static_cast<CallbackContextWithSync*>(user_data);



                    // LOCK: Prevent main thread from reading pixel_buffer while we write
                    auto lock_start = std::chrono::high_resolution_clock::now();
                    {
                        std::lock_guard<std::mutex> lock(ctx->pipeline->get_frame_mutex());

                        // Copy results from GPU shared memory to CPU buffers
                        size_t framebuffer_size = ctx->w * ctx->h * sizeof(uint32_t);
                        size_t depth_size = ctx->w * ctx->h * sizeof(uint32_t);

                        auto memcpy_start = std::chrono::high_resolution_clock::now();
                        memcpy(ctx->cpu_framebuffer, framebuffer, framebuffer_size);
                        memcpy(ctx->cpu_depth, depth_buffer, depth_size);
                        auto memcpy_duration = std::chrono::duration<double, std::milli>(
                            std::chrono::high_resolution_clock::now() - memcpy_start).count();

                        // DEBUG: Sample tree pixels AFTER Pass 3 to verify lighting was applied
                        static int final_fb_debug = 0;
                        if (final_fb_debug < 1 && ctx->w >= 1024 && ctx->h >= 700) {
                            // Sample positions from TREE_DETAIL output: NORTH@(677,455), SOUTH@(1023,654), EAST@(1023,455), WEST@(677,654)
                            int sample_positions[4][2] = {{677,455}, {1023,654}, {1023,455}, {677,654}};
                            const char* tree_names[4] = {"NORTH", "SOUTH", "EAST", "WEST"};
                            uint32_t* fb = ctx->cpu_framebuffer;

                            std::cout << "[FINAL_FB_DEBUG] Sampling tree pixels AFTER Pass 3 completion:" << std::endl;
                            for (int i = 0; i < 4; i++) {
                                int px = sample_positions[i][0];
                                int py = sample_positions[i][1];
                                if (px < (int)ctx->w && py < (int)ctx->h) {
                                    uint32_t pixel = fb[py * ctx->w + px];
                                    int r = (pixel >> 16) & 0xFF;
                                    int g = (pixel >> 8) & 0xFF;
                                    int b = pixel & 0xFF;
                                    std::cout << "[FINAL_FB_DEBUG]   " << tree_names[i] << " @(" << px << "," << py
                                              << "): RGB(" << r << "," << g << "," << b << ")" << std::endl;
                                }
                            }
                            // Also sample center (floor)
                            int center_px = ctx->w / 2;
                            int center_py = ctx->h / 2;
                            uint32_t center_pixel = fb[center_py * ctx->w + center_px];
                            int cr = (center_pixel >> 16) & 0xFF;
                            int cg = (center_pixel >> 8) & 0xFF;
                            int cb = center_pixel & 0xFF;
                            std::cout << "[FINAL_FB_DEBUG]   CENTER @(" << center_px << "," << center_py
                                      << "): RGB(" << cr << "," << cg << "," << cb << ")" << std::endl;
                            final_fb_debug++;
                        }

                        // NEW: Populate object_map from G-buffer particle IDs (deferred rendering only)
                        // G-buffer layout: 32 bytes/pixel (see gbuffer_types.metal:23-28)
                        // Layout: world_pos(12) + normal(12) + base_color(4) + particle_id(4)
                        // particle_id offset: 28 bytes
                        double object_map_duration = 0.0;
                        if (gbuffer != nullptr) {
                            // PERFORMANCE: Extract object IDs from G-buffer for object picking
                            // This scans all pixels - only logged once per run for verification
                            static bool logged_extraction = false;
                            if (!logged_extraction) {
                                std::cout << "[RENDER_PIPELINE] G-buffer object extraction enabled (resolution " << ctx->w << "x" << ctx->h << ")" << std::endl;
                                logged_extraction = true;
                            }

                            auto object_map_start = std::chrono::high_resolution_clock::now();
                            ctx->object_map->clear();

                            const uint8_t* gbuffer_bytes = static_cast<const uint8_t*>(gbuffer);
                            const int GBUFFER_PIXEL_SIZE = 32;
                            const int PARTICLE_ID_OFFSET = 28;
                            constexpr uint32_t GBUFFER_SKY_ID = 0xFFFFFFFF;  // UINT_MAX = sky/background

                            for (int y = 0; y < ctx->h; ++y) {
                                for (int x = 0; x < ctx->w; ++x) {
                                    int pixel_idx = y * ctx->w + x;
                                    int gbuffer_byte_idx = pixel_idx * GBUFFER_PIXEL_SIZE + PARTICLE_ID_OFFSET;

                                    // Read particle_id (uint32_t at offset 28)
                                    uint32_t particle_id = *reinterpret_cast<const uint32_t*>(
                                        gbuffer_bytes + gbuffer_byte_idx
                                    );

                                    // Skip sky pixels (UINT_MAX means background)
                                    if (particle_id != GBUFFER_SKY_ID) {
                                        // CRITICAL: Encode particle_id using ObjectID system
                                        // GPU G-buffer stores raw particle_id (e.g., 4753)
                                        // Must encode as ((particle_id+1) << 16) for click detection
                                        int encoded_id = ObjectID::encode_particle_surface(particle_id, 0);
                                        ctx->object_map->set_object(x, y, encoded_id);
                                    }
                                }
                            }

                            object_map_duration = std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() - object_map_start).count();
                        }

                        if constexpr (Optimizations::ENABLE_VERBOSE_FRAME_LOGS) {
                            // SYNC INVESTIGATION: Log callback timing breakdown
                            auto lock_duration = std::chrono::duration<double, std::milli>(
                                std::chrono::high_resolution_clock::now() - lock_start).count();

                            static int callback_timing_counter = 0;
                            callback_timing_counter++;
                            bool should_log_callback = (callback_timing_counter % 60 == 0) || (lock_duration > 5.0);

                            if (should_log_callback) {
                                std::cout << "[CALLBACK_TIMING] Frame " << callback_timing_counter
                                          << " | Total: " << std::fixed << std::setprecision(2) << lock_duration << "ms"
                                          << " | memcpy: " << memcpy_duration << "ms"
                                          << " | object_map: " << object_map_duration << "ms"
                                          << " | pixels: " << (ctx->w * ctx->h);
                                if (lock_duration > 5.0) {
                                    std::cout << " *** SLOW CALLBACK ***";
                                }
                                std::cout << std::endl;
                            }
                        }
                    }
                    // UNLOCK: Safe for main thread to read now

                    // Signal that frame is ready to present
                    ctx->pipeline->gpu_frame_ready_.store(true, std::memory_order_release);

                    // Free the context (allocated with new in caller)
                    delete ctx;
                },
                callback_ctx  // User data: Context for callback (will be deleted by callback)
            );
            }  // end else (gpu_triangles not empty)
        } else {
            // FORWARD RENDERING (inline shadows in rasterization pass)
            // Single pass: Rasterize + Shadow rays + Lighting (all inline)
            // ISSUE: Thread divergence on BVH traversal (5-10% warp efficiency)
            gpu_rasterizer_.rasterize_triangles_lit_async(
                gpu_triangles.data(),
                (uint32_t)gpu_triangles.size(),
                light_data.empty() ? nullptr : light_data.data(),
                light_count,
                (shadow_triangle_bvh.is_ready() && g_pass_flat_shadow_bvh.load(std::memory_order_relaxed))
                    ? shadow_triangle_bvh.get_nodes() : nullptr,
                (shadow_triangle_bvh.is_ready() && g_pass_flat_shadow_bvh.load(std::memory_order_relaxed))
                    ? shadow_triangle_bvh.get_node_count() : 0,
                shadow_triangles.empty() ? nullptr : shadow_triangles.data(),
                (uint32_t)shadow_triangles.size(),
                // Tile binning data (pass empty if binning disabled)
                tile_indices.empty() ? nullptr : tile_indices.data(),
                tile_offsets.empty() ? nullptr : tile_offsets.data(),
                tile_counts.empty() ? nullptr : tile_counts.data(),
                tiles_x,
                tiles_y,
                // Completion callback: Copy results from GPU shared memory to CPU buffers
                // CRITICAL: This runs on Metal's background thread - must synchronize!
                [](uint32_t* framebuffer, uint32_t* depth_buffer, void* gbuffer,
                   int width, int height, void* user_data) {
                    CallbackContextWithSync* ctx = static_cast<CallbackContextWithSync*>(user_data);

                    // LOCK: Prevent main thread from reading pixel_buffer while we write
                    {
                        std::lock_guard<std::mutex> lock(ctx->pipeline->get_frame_mutex());

                        // Copy results from GPU shared memory to CPU buffers
                        size_t framebuffer_size = ctx->w * ctx->h * sizeof(uint32_t);
                        size_t depth_size = ctx->w * ctx->h * sizeof(uint32_t);

                        memcpy(ctx->cpu_framebuffer, framebuffer, framebuffer_size);
                        memcpy(ctx->cpu_depth, depth_buffer, depth_size);

                        // NEW: Populate object_map from G-buffer particle IDs
                        // Forward rendering passes nullptr (no G-buffer), so this is skipped
                        if (gbuffer != nullptr) {
                            ctx->object_map->clear();

                            const uint8_t* gbuffer_bytes = static_cast<const uint8_t*>(gbuffer);
                            const int GBUFFER_PIXEL_SIZE = 32;
                            const int PARTICLE_ID_OFFSET = 28;
                            constexpr uint32_t GBUFFER_SKY_ID = 0xFFFFFFFF;  // UINT_MAX = sky/background

                            for (int y = 0; y < ctx->h; ++y) {
                                for (int x = 0; x < ctx->w; ++x) {
                                    int pixel_idx = y * ctx->w + x;
                                    int gbuffer_byte_idx = pixel_idx * GBUFFER_PIXEL_SIZE + PARTICLE_ID_OFFSET;

                                    // Read particle_id (uint32_t at offset 28)
                                    uint32_t particle_id = *reinterpret_cast<const uint32_t*>(
                                        gbuffer_bytes + gbuffer_byte_idx
                                    );

                                    // Skip sky pixels (UINT_MAX means background)
                                    if (particle_id != GBUFFER_SKY_ID) {
                                        // CRITICAL: Encode particle_id using ObjectID system
                                        // GPU G-buffer stores raw particle_id (e.g., 4753)
                                        // Must encode as ((particle_id+1) << 16) for click detection
                                        ctx->object_map->set_object(x, y, ObjectID::encode_particle_surface(particle_id, 0));
                                    }
                                }
                            }
                        }
                    }
                    // UNLOCK: Safe for main thread to read now

                    // Signal that frame is ready to present
                    ctx->pipeline->gpu_frame_ready_.store(true, std::memory_order_release);

                    // Free the context (allocated with new in caller)
                    delete ctx;
                },
                callback_ctx  // User data: Context for callback (will be deleted by callback)
            );
        }
        auto submit_ms = Duration(Clock::now() - submit_start).count();
        ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::RenderSubmit);

        // FRAME TIMING SUMMARY: GPU submit timing
        // NOTE: Prep timing (gpu_tri, shadow_tri, bvh) is now logged inside prepare_gpu_data()
        static int frame_summary_count = 0;
        frame_summary_count++;

        // Log GPU submit time when slow or periodically
        if (submit_ms > 5.0 || (frame_summary_count % 60) == 0) {
            std::cout << "[FRAME_SUBMIT] Frame " << frame_summary_count
                      << " | GPU Submit: " << std::fixed << std::setprecision(2) << submit_ms << "ms"
                      << " | Triangles: " << gpu_triangles.size() << " render, " << shadow_triangles.size() << " shadow"
                      << std::endl;
        }

        // =====================================================================
        // ASYNC GPU_PREP: Launch prep for next frame while GPU renders current
        // =====================================================================
        if (::logosphere::get_async_gpu_prep()) {
            // Don't start new prep if previous one is still running (non-blocking fallback case)
            if (skip_next_async_prep_) {
                // Previous prep still running - let it finish, don't spawn another thread
                // Next frame will pick up the completed buffer
            } else {
            ::logosphere::telemetry::ScopedPhase _handoff(
                ::logosphere::telemetry::Phase::RenderHandoff);
            // Rotate to next buffer slot for async prep
            int next_prep_idx = (buffer_idx + 1) % PREP_BUFFER_SLOTS;

            // Mark prep as not ready yet
            prep_ready_.store(false, std::memory_order_release);

            // CRITICAL: Copy surfaces AND particles — the detached thread outlives
            // the caller's scope, so references become dangling after render returns.
            // SWAP, do not copy. `surfaces` is surface_cache_ and is dead from
            // here to the end of this function, so the worker can simply take
            // ownership of the filled deque and hand back a spare that already
            // owns its blocks. O(1) instead of 103,914 element constructions,
            // and next frame's collect_surfaces still reuses the allocations.
            ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::HandoffSurfaces);
            surface_pool_[next_prep_idx].swap(surface_cache_);
            ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::HandoffSurfaces);
            ::logosphere::telemetry::phase_begin(::logosphere::telemetry::Phase::HandoffParticles);
            auto particles_copy = particles;
            // ISSUE #31: the render-index to entity mapping MUST be captured
            // here, next to the particle snapshot. Taken later, from inside the
            // worker, it reads the KG as of frame N while the worker holds
            // frame N-1's particles; chunk streaming deletes in bulk by
            // swap-and-pop, so every survivor has a new render index in between
            // and the mapping resolves each particle to the wrong entity. That
            // is the foliage-renders-black bug. It writes into the worker's own
            // slot, so no copy is added and nothing is shared.
            snapshot_entity_ids(next_prep_idx, particles_copy.size());
            ::logosphere::telemetry::phase_end(::logosphere::telemetry::Phase::HandoffParticles);

            // Launch async worker to prepare next frame's data
            // This runs in background while GPU renders current frame
            // MOVE into the closure. Capturing these by value copied the whole
            // frame's input a SECOND time: 103,914 surfaces and 19,104
            // particles were duplicated once into the locals above and again
            // into the lambda. Measured 2.06 ms of the 4.57 ms handoff, for
            // nothing. The locals are dead after this point, so a move is safe.
            std::thread prep_worker([this, next_prep_idx,
                                     particles_copy = std::move(particles_copy),
                                     &camera_system]() {
                // Surfaces come from the pool slot this worker owns; only the
                // particle snapshot still needs carrying into the closure.
                prepare_gpu_data(next_prep_idx, surface_pool_[next_prep_idx],
                                 particles_copy, camera_system,
                                 /*kg_snapshot_taken=*/true);

                // Signal completion
                {
                    std::lock_guard<std::mutex> lock(prep_mutex_);
                    render_buffer_index_.store(next_prep_idx, std::memory_order_release);
                    prep_ready_.store(true, std::memory_order_release);
                }
                prep_cv_.notify_one();
            });

            // Detach worker (runs independently)
            prep_worker.detach();
            }  // end else (not skipping async prep)
        }

        // FULL ASYNC MODE: GPU renders frame N while CPU prepares frame N+1
        // Triple-buffering handles synchronization via semaphore and completion callback
        // Results are copied asynchronously in the completion handler
        // Shutdown safety: Engine::shutdown() calls wait_for_gpu_completion() before cleanup
        //
        // NOTE: Synchronous validation mode (for debugging):
        // Uncomment the line below to wait for GPU completion before continuing
        // gpu_rasterizer_.wait_for_completion();

        pixels_drawn = gpu_triangles.size() * 100;  // Rough estimate

    } else {
        // CPU Rasterization Path
        pixels_drawn = surface_rasterizer_.rasterize_surfaces(
            surfaces, pixel_buffer, depth_buffer, object_map,
            camera_system, light_system, pixel_shader,
            particles, shadow_bvh,
            pixel_buffer.width(), pixel_buffer.height(), metrics);
    }

    last_frame_stats_.surfaces_rendered = surfaces.size();
    last_frame_stats_.pixels_drawn = pixels_drawn;

    if (metrics) {
        metrics->surfaces_rendered = surfaces.size();
        metrics->pixels_drawn = pixels_drawn;
    }
}

void RenderPipeline::wait_for_gpu_completion() {
    // Wait for GPU to finish all pending work (async rasterization)
    // CRITICAL: Must call before shutdown or resolution change
    // Delegates to GPU rasterizer which manages Metal command buffers
    gpu_rasterizer_.wait_for_completion();
}

void RenderPipeline::reset_temporal_state() {
    // Flush GPU, then clear all temporal buffers for scene transitions
    gpu_rasterizer_.wait_for_completion();
    gpu_rasterizer_.reset_temporal_state();
    
    // Force next frame to prepare GPU data synchronously with current scene.
    // Without this, the first frame after a scene change would use stale async
    // prep data from the previous scene (including wrong shadow triangles/colors).
    first_frame_ = true;
    prep_ready_.store(false, std::memory_order_release);
    skip_next_async_prep_ = false;
    last_good_buffer_idx_ = 0;
}

int RenderPipeline::acquire_all_gpu_slots() {
    return gpu_rasterizer_.acquire_all_slots();
}

void RenderPipeline::release_all_gpu_slots(int slots) {
    gpu_rasterizer_.release_all_slots(slots);
}

// =========================================================================
// VISION CONE POST-PROCESS (Pass 4)
// =========================================================================

void RenderPipeline::set_vision_cone_enabled(bool enabled) {
    gpu_rasterizer_.set_vision_cone_enabled(enabled);
}

bool RenderPipeline::get_vision_cone_enabled() const {
    return gpu_rasterizer_.get_vision_cone_enabled();
}

void RenderPipeline::set_vision_cone(float viewer_x, float viewer_y, float look_direction,
                                      float fov_radians, float range) {
    gpu_rasterizer_.set_vision_cone(viewer_x, viewer_y, look_direction, fov_radians, range);
}

void RenderPipeline::set_vision_cone_style(float inner_falloff, float darkness) {
    gpu_rasterizer_.set_vision_cone_style(inner_falloff, darkness);
}

void RenderPipeline::set_vision_cone_focus(float focus_x, float focus_y, float focus_radius) {
    gpu_rasterizer_.set_vision_cone_focus(focus_x, focus_y, focus_radius);
}

void RenderPipeline::set_vision_cone_occlusion(const float* distances, int count) {
    gpu_rasterizer_.set_vision_cone_occlusion(distances, count);
}

void RenderPipeline::clear_vision_cone_occlusion() {
    gpu_rasterizer_.clear_vision_cone_occlusion();
}

void RenderPipeline::set_vision_memory_enabled(bool enabled) {
    gpu_rasterizer_.set_vision_memory_enabled(enabled);
}

void RenderPipeline::set_vision_memory_extent(float min_x, float min_y,
                                              float max_x, float max_y,
                                              int cells_per_side) {
    gpu_rasterizer_.set_vision_memory_extent(min_x, min_y, max_x, max_y, cells_per_side);
}

void RenderPipeline::set_vision_memory_decay(float decay_seconds, float memory_dim) {
    gpu_rasterizer_.set_vision_memory_decay(decay_seconds, memory_dim);
}

void RenderPipeline::update_vision_memory(float dt) {
    gpu_rasterizer_.update_vision_memory(dt);
}