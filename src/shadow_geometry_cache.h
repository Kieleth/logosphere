#ifndef SHADOW_GEOMETRY_CACHE_H
#define SHADOW_GEOMETRY_CACHE_H

#include <vector>
#include <cmath>
#include <unordered_map>
#include <mutex>
#include <cstdint>

// ============================================================================
// SHADOW GEOMETRY TEMPLATE CACHE
// ============================================================================
// PURPOSE: Eliminate per-frame geometry object creation for shadow triangles
// STRATEGY: Pre-cache local vertex positions for common particle shapes
//
// PERFORMANCE IMPACT:
// - Before: Create TriangleCubeGeometry every frame (~1-2ms overhead)
// - After: Lookup cached template (minimal overhead)
// - Expected: Reduce object creation cost, then async for parallelization
// ============================================================================

namespace ShadowGeometryCache {

// Template key for caching geometry
struct TemplateKey {
    enum class ShapeType : uint8_t {
        CUBE,
        BOX
    };

    ShapeType shape;
    float param1;  // CUBE: size, BOX: width
    float param2;  // BOX: height (unused for CUBE)
    float param3;  // BOX: thickness (unused for CUBE)

    bool operator==(const TemplateKey& other) const {
        const float eps = 0.0001f;
        return shape == other.shape &&
               std::abs(param1 - other.param1) < eps &&
               std::abs(param2 - other.param2) < eps &&
               std::abs(param3 - other.param3) < eps;
    }
};

// Hash function for TemplateKey
struct TemplateKeyHash {
    std::size_t operator()(const TemplateKey& k) const {
        int p1 = static_cast<int>(k.param1 * 10000);
        int p2 = static_cast<int>(k.param2 * 10000);
        int p3 = static_cast<int>(k.param3 * 10000);
        int shape = static_cast<int>(k.shape);

        std::size_t h1 = std::hash<int>{}(p1);
        std::size_t h2 = std::hash<int>{}(p2);
        std::size_t h3 = std::hash<int>{}(p3);
        std::size_t h4 = std::hash<int>{}(shape);

        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

// Cached geometry template (local space vertices)
struct GeometryTemplate {
    std::vector<float> local_vertices;  // Flat array: x0,y0,z0, x1,y1,z1, ...
    int vertex_count;                   // Total vertices (e.g., 8 for cube)

    // Triangle indices (groups of 3 vertex indices)
    std::vector<uint16_t> triangle_indices;  // e.g., [0,1,2, 0,2,3, ...]
    int triangle_count;                      // Total triangles (e.g., 12 for cube)
};

// ============================================================================
// CACHE API
// ============================================================================

// Get or create geometry template (thread-safe)
const GeometryTemplate* get_template(const TemplateKey& key);

// Pre-populate cache with common templates (called at startup)
void initialize_common_templates();

// Clear cache (for cleanup)
void clear_cache();

// Statistics (for profiling)
struct CacheStats {
    size_t template_count;
    size_t total_hits;
    size_t total_misses;
};

CacheStats get_stats();

} // namespace ShadowGeometryCache

#endif // SHADOW_GEOMETRY_CACHE_H
