#include "triangle_cube_geometry.h"
#include "optimization_flags.h"
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <algorithm>

namespace ParticleGeometryV2 {

// ============================================================================
// GEOMETRY CACHE - Phase 1 Optimization
// ============================================================================
// Cache transformed vertices for identical (size, rotation) combinations
// Most particles share size=1.0, rotation=(0,0,0) - cache hit rate ~95%
// Expected impact: 2-3× speedup in collect_surfaces (12.2ms → 4-6ms at 32K)

struct GeometryCacheKey {
    float size;
    float rx, ry, rz;

    bool operator==(const GeometryCacheKey& other) const {
        // Use epsilon comparison for floating point
        const float eps = 0.0001f;
        return std::abs(size - other.size) < eps &&
               std::abs(rx - other.rx) < eps &&
               std::abs(ry - other.ry) < eps &&
               std::abs(rz - other.rz) < eps;
    }
};

// Hash function for cache key
struct GeometryCacheKeyHash {
    std::size_t operator()(const GeometryCacheKey& k) const {
        // Simple hash combining all fields
        // Quantize to 0.0001 precision to ensure identical hashes for near-equal values
        int size_i = static_cast<int>(k.size * 10000);
        int rx_i = static_cast<int>(k.rx * 10000);
        int ry_i = static_cast<int>(k.ry * 10000);
        int rz_i = static_cast<int>(k.rz * 10000);

        std::size_t h1 = std::hash<int>{}(size_i);
        std::size_t h2 = std::hash<int>{}(rx_i);
        std::size_t h3 = std::hash<int>{}(ry_i);
        std::size_t h4 = std::hash<int>{}(rz_i);

        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

// Thread-local cache to avoid contention in multi-threaded collect_surfaces
thread_local std::unordered_map<GeometryCacheKey, std::vector<Vec3>, GeometryCacheKeyHash> g_geometry_cache;

// Cache statistics (thread-local to avoid contention)
thread_local size_t g_cache_hits = 0;
thread_local size_t g_cache_misses = 0;
thread_local size_t g_cache_lookups = 0;

// ============================================================================
// TRIANGLE CUBE GEOMETRY IMPLEMENTATION
// ============================================================================
// Following the directive from CPU_RENDERING_TRIANGLE.md:
// "This change must be surgical - remove ALL quad-based calculations"
//
// Each cube face is split into 2 triangles using a consistent diagonal.
// This ensures no cracks between triangles and proper UV mapping.
// ============================================================================

TriangleCubeGeometry::TriangleCubeGeometry(float size) {
    float half = size / 2.0f;
    
    // Define the 8 corners of the cube (same as CubeGeometryV2)
    // Using the same systematic numbering: binary counting in XYZ
    local_vertices.push_back(Vec3(-half, -half, -half));  // 0: left-back-bottom
    local_vertices.push_back(Vec3( half, -half, -half));  // 1: right-back-bottom
    local_vertices.push_back(Vec3( half,  half, -half));  // 2: right-front-bottom
    local_vertices.push_back(Vec3(-half,  half, -half));  // 3: left-front-bottom
    local_vertices.push_back(Vec3(-half, -half,  half));  // 4: left-back-top
    local_vertices.push_back(Vec3( half, -half,  half));  // 5: right-back-top
    local_vertices.push_back(Vec3( half,  half,  half));  // 6: right-front-top
    local_vertices.push_back(Vec3(-half,  half,  half));  // 7: left-front-top
    
    // =========================================================================
    // CRITICAL: Convert each quad face to 2 triangles
    // =========================================================================
    // We split each quad along the diagonal from vertex 0 to vertex 2.
    // This is consistent and prevents T-junctions (cracks between triangles).
    //
    // Quad vertices: [0,1,2,3] becomes:
    // Triangle 1: [0,1,2] (bottom-right triangle)
    // Triangle 2: [0,2,3] (top-left triangle)
    // =========================================================================
    
    // Face 0: +Y face (front - points in +Y direction)
    // Quad [3,2,6,7] becomes two triangles
    Face tri1;
    tri1.vertex_indices = {3, 2, 6};  // Triangle 1
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, 1, 0);
    faces.push_back(tri1);
    
    Face tri2;
    tri2.vertex_indices = {3, 6, 7};  // Triangle 2
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, 1, 0);
    faces.push_back(tri2);
    
    // Face 1: -Y face (back - points in -Y direction)
    // Quad [1,0,4,5] becomes two triangles
    tri1.vertex_indices = {1, 0, 4};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, -1, 0);
    faces.push_back(tri1);
    
    tri2.vertex_indices = {1, 4, 5};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, -1, 0);
    faces.push_back(tri2);
    
    // Face 2: +Z face (top - points in +Z direction)
    // Quad [7,6,5,4] becomes two triangles
    tri1.vertex_indices = {7, 6, 5};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, 0, 1);
    faces.push_back(tri1);
    
    tri2.vertex_indices = {7, 5, 4};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, 0, 1);
    faces.push_back(tri2);
    
    // Face 3: -Z face (bottom - points in -Z direction)
    // Quad [0,1,2,3] becomes two triangles
    tri1.vertex_indices = {0, 1, 2};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, 0, -1);
    faces.push_back(tri1);
    
    tri2.vertex_indices = {0, 2, 3};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, 0, -1);
    faces.push_back(tri2);
    
    // Face 4: +X face (right - points in +X direction)
    // Quad [2,1,5,6] becomes two triangles
    tri1.vertex_indices = {2, 1, 5};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(1, 0, 0);
    faces.push_back(tri1);
    
    tri2.vertex_indices = {2, 5, 6};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(1, 0, 0);
    faces.push_back(tri2);
    
    // Face 5: -X face (left - points in -X direction)
    // Quad [0,3,7,4] becomes two triangles
    tri1.vertex_indices = {0, 3, 7};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(-1, 0, 0);
    faces.push_back(tri1);
    
    tri2.vertex_indices = {0, 7, 4};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(-1, 0, 0);
    faces.push_back(tri2);
}

std::vector<Surface> TriangleCubeGeometry::to_surfaces(const Transform& transform) const {
    std::vector<Surface> surfaces;

    std::vector<Vec3> world_vertices;

    if constexpr (Optimizations::USE_GEOMETRY_CACHE) {
        // =========================================================================
        // PHASE 1 OPTIMIZATION: Geometry Caching with Fast Path
        // =========================================================================
        // Cache rotated vertices for identical (size, rotation) combinations
        // Only translate position for cache hits - eliminates 168 ops per particle

        g_cache_lookups++;

        // Extract cube size from local_vertices (they're centered at ±half_size)
        float half_size = local_vertices[1].x;  // Right vertex has +half_size
        float size = half_size * 2.0f;

        // FAST PATH: Identity transform (no rotation, default size)
        // This is the most common case - skip hash altogether
        static thread_local std::vector<Vec3> identity_cache;
        if (transform.rotation_x == 0.0f && transform.rotation_y == 0.0f && transform.rotation_z == 0.0f && size == 1.0f) {
            if (identity_cache.empty()) {
                // First time: compute and cache identity vertices
                Transform identity(Vec3(0, 0, 0), 0, 0, 0);
                identity_cache.reserve(local_vertices.size());
                for (const auto& v : local_vertices) {
                    identity_cache.push_back(identity.transform_point(v));
                }
            }

            // Use cached identity vertices, just translate
            world_vertices.reserve(identity_cache.size());
            for (const auto& v : identity_cache) {
                world_vertices.push_back(v + transform.position);
            }
            g_cache_hits++;

            // Log cache stats every 10000 lookups
            if constexpr (Optimizations::ENABLE_PROFILING) {
                if (g_cache_lookups % 10000 == 0) {
                    float hit_rate = (g_cache_hits * 100.0f) / g_cache_lookups;
                    std::cout << "[GEOM_CACHE] Lookups: " << g_cache_lookups
                              << " | Hits: " << g_cache_hits
                              << " | Misses: " << g_cache_misses
                              << " | Hit rate: " << hit_rate << "%"
                              << " | Cache size: " << g_geometry_cache.size() << std::endl;
                }
            }
        } else {
            // GENERAL CASE: Use hash map for other transforms
            // Create cache key
            GeometryCacheKey cache_key{size, transform.rotation_x, transform.rotation_y, transform.rotation_z};

            auto it = g_geometry_cache.find(cache_key);

            if (it != g_geometry_cache.end()) {
                // CACHE HIT: Use cached rotated vertices, just add position offset
                g_cache_hits++;

                const std::vector<Vec3>& rotated_vertices = it->second;
                world_vertices.reserve(rotated_vertices.size());

                for (const auto& v : rotated_vertices) {
                    world_vertices.push_back(v + transform.position);
                }
            } else {
                // CACHE MISS: Full transform and store rotated vertices (without position)
                g_cache_misses++;

                // First, get rotated vertices (without translation)
                Transform rotation_only(Vec3(0, 0, 0), transform.rotation_x, transform.rotation_y, transform.rotation_z);
                std::vector<Vec3> rotated_vertices;
                rotated_vertices.reserve(local_vertices.size());

                for (const auto& v : local_vertices) {
                    rotated_vertices.push_back(rotation_only.transform_point(v));
                }

                // Cache the rotated vertices
                g_geometry_cache[cache_key] = rotated_vertices;

                // Now apply translation for world vertices
                world_vertices.reserve(rotated_vertices.size());
                for (const auto& v : rotated_vertices) {
                    world_vertices.push_back(v + transform.position);
                }
            }

            // Log cache stats for general case every 10000 lookups
            if constexpr (Optimizations::ENABLE_PROFILING) {
                if (g_cache_lookups % 10000 == 0) {
                    float hit_rate = (g_cache_hits * 100.0f) / g_cache_lookups;
                    std::cout << "[GEOM_CACHE] Lookups: " << g_cache_lookups
                              << " | Hits: " << g_cache_hits
                              << " | Misses: " << g_cache_misses
                              << " | Hit rate: " << hit_rate << "%"
                              << " | Cache size: " << g_geometry_cache.size() << std::endl;
                }
            }
        }
    } else {
        // NO CACHE: Direct transform (baseline behavior)
        world_vertices = get_world_vertices(transform);
    }

    // Each face becomes a triangle surface
    for (size_t i = 0; i < faces.size(); ++i) {
        const Face& face = faces[i];

        // Get the 3 vertices of this triangle
        Vec3 v0 = world_vertices[face.vertex_indices[0]];
        Vec3 v1 = world_vertices[face.vertex_indices[1]];
        Vec3 v2 = world_vertices[face.vertex_indices[2]];

        // Transform normal to world space
        Vec3 world_normal = transform.transform_direction(face.normal);

        // Create a triangle surface
        Surface surf = create_triangle_surface(v0, v1, v2, world_normal, -1);

        surfaces.push_back(surf);
    }

    return surfaces;
}

Surface TriangleCubeGeometry::create_triangle_surface(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                                      const Vec3& normal, int particle_id) const {
    Surface surf;
    
    // Set center as average of vertices
    surf.x = (v0.x + v1.x + v2.x) / 3.0f;
    surf.y = (v0.y + v1.y + v2.y) / 3.0f;
    surf.z = (v0.z + v1.z + v2.z) / 3.0f;
    
    // Set normal
    surf.nx = normal.x;
    surf.ny = normal.y;
    surf.nz = normal.z;
    
    // CRITICAL: Set vertex_count to 3 for triangle!
    surf.vertex_count = 3;
    
    // Store the 3 vertices
    surf.vertices[0][0] = v0.x;
    surf.vertices[0][1] = v0.y;
    surf.vertices[0][2] = v0.z;
    
    surf.vertices[1][0] = v1.x;
    surf.vertices[1][1] = v1.y;
    surf.vertices[1][2] = v1.z;
    
    surf.vertices[2][0] = v2.x;
    surf.vertices[2][1] = v2.y;
    surf.vertices[2][2] = v2.z;
    
    // Calculate dimensions (for bounding box)
    float min_x = std::min({v0.x, v1.x, v2.x});
    float max_x = std::max({v0.x, v1.x, v2.x});
    float min_y = std::min({v0.y, v1.y, v2.y});
    float max_y = std::max({v0.y, v1.y, v2.y});
    float min_z = std::min({v0.z, v1.z, v2.z});
    float max_z = std::max({v0.z, v1.z, v2.z});
    
    surf.width = max_x - min_x;
    surf.height = std::max(max_y - min_y, max_z - min_z);
    
    // No area: write-only field, see FlatParticleGeometry::create_triangle_surface.
    
    // Set material properties (defaults)
    surf.roughness = 0.5f;
    surf.metallic = 0.0f;
    surf.reflectance = 0.5f;

    // Set particle ID
    surf.particle_id = particle_id;

    return surf;
}

// OPTIMIZATION: Direct vertex output for shadow triangles
// Bypasses Surface struct creation - outputs raw triangle vertices
// Expected speedup: 5-10x vs to_surfaces() (eliminates normal/UV/Surface overhead)
void TriangleCubeGeometry::to_shadow_vertices(const Transform& transform, std::vector<float>& out_vertices) const {
    // Get transformed vertices (use same caching logic as to_surfaces if enabled)
    std::vector<Vec3> world_vertices;

    if constexpr (Optimizations::USE_GEOMETRY_CACHE) {
        // Build cache key from transform (same as to_surfaces)
        GeometryCacheKey cache_key{
            local_vertices[1].x * 2.0f,  // size (derived from half-size)
            transform.rotation_x,
            transform.rotation_y,
            transform.rotation_z
        };

        g_cache_lookups++;

        auto it = g_geometry_cache.find(cache_key);
        if (it != g_geometry_cache.end()) {
            // CACHE HIT: Apply translation to cached rotated vertices
            g_cache_hits++;
            const std::vector<Vec3>& rotated_vertices = it->second;
            world_vertices.reserve(rotated_vertices.size());
            for (const auto& v : rotated_vertices) {
                world_vertices.push_back(v + transform.position);
            }
        } else {
            // CACHE MISS: Full transform
            g_cache_misses++;
            Transform rotation_only(Vec3(0, 0, 0), transform.rotation_x, transform.rotation_y, transform.rotation_z);
            std::vector<Vec3> rotated_vertices;
            rotated_vertices.reserve(local_vertices.size());

            for (const auto& v : local_vertices) {
                rotated_vertices.push_back(rotation_only.transform_point(v));
            }

            g_geometry_cache[cache_key] = rotated_vertices;

            world_vertices.reserve(rotated_vertices.size());
            for (const auto& v : rotated_vertices) {
                world_vertices.push_back(v + transform.position);
            }
        }
    } else {
        // NO CACHE: Direct transform
        world_vertices = get_world_vertices(transform);
    }

    // Reserve space for 12 triangles × 9 floats = 108 floats
    out_vertices.reserve(out_vertices.size() + faces.size() * 9);

    // Output triangle vertices directly (9 floats per triangle)
    for (const Face& face : faces) {
        const Vec3& v0 = world_vertices[face.vertex_indices[0]];
        const Vec3& v1 = world_vertices[face.vertex_indices[1]];
        const Vec3& v2 = world_vertices[face.vertex_indices[2]];

        // Triangle vertex 0
        out_vertices.push_back(v0.x);
        out_vertices.push_back(v0.y);
        out_vertices.push_back(v0.z);

        // Triangle vertex 1
        out_vertices.push_back(v1.x);
        out_vertices.push_back(v1.y);
        out_vertices.push_back(v1.z);

        // Triangle vertex 2
        out_vertices.push_back(v2.x);
        out_vertices.push_back(v2.y);
        out_vertices.push_back(v2.z);
    }
}

} // namespace ParticleGeometryV2