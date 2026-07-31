#include "shadow_geometry_cache.h"
#include "triangle_cube_geometry.h"
#include "flat_particle_geometry.h"
#include <iostream>

namespace ShadowGeometryCache {

// Thread-safe cache storage
static std::unordered_map<TemplateKey, GeometryTemplate, TemplateKeyHash> g_cache;
static std::mutex g_cache_mutex;
static CacheStats g_stats = {0, 0, 0};

// ============================================================================
// CACHE IMPLEMENTATION
// ============================================================================

const GeometryTemplate* get_template(const TemplateKey& key) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);

    // Check if template exists
    auto it = g_cache.find(key);
    if (it != g_cache.end()) {
        g_stats.total_hits++;
        return &it->second;
    }

    // Cache miss - create new template
    g_stats.total_misses++;

    GeometryTemplate new_template;

    // Build geometry based on shape type
    if (key.shape == TemplateKey::ShapeType::CUBE) {
        // Create TriangleCubeGeometry to extract vertices and indices
        ParticleGeometryV2::TriangleCubeGeometry cube(key.param1);

        // Extract local vertices (8 corners)
        const auto& local_verts = cube.get_local_vertices();
        new_template.vertex_count = static_cast<int>(local_verts.size());
        new_template.local_vertices.reserve(new_template.vertex_count * 3);

        for (const auto& v : local_verts) {
            new_template.local_vertices.push_back(v.x);
            new_template.local_vertices.push_back(v.y);
            new_template.local_vertices.push_back(v.z);
        }

        // Extract triangle indices (12 triangles × 3 indices)
        const auto& faces = cube.get_faces();
        new_template.triangle_count = static_cast<int>(faces.size());
        new_template.triangle_indices.reserve(new_template.triangle_count * 3);

        for (const auto& face : faces) {
            for (int idx : face.vertex_indices) {
                new_template.triangle_indices.push_back(static_cast<uint16_t>(idx));
            }
        }

    } else {  // BOX
        // Create FlatParticleGeometry to extract vertices and indices
        ParticleGeometryV2::FlatParticleGeometry box(key.param1, key.param2, key.param3);

        // Extract local vertices (8 corners)
        const auto& local_verts = box.get_local_vertices();
        new_template.vertex_count = static_cast<int>(local_verts.size());
        new_template.local_vertices.reserve(new_template.vertex_count * 3);

        for (const auto& v : local_verts) {
            new_template.local_vertices.push_back(v.x);
            new_template.local_vertices.push_back(v.y);
            new_template.local_vertices.push_back(v.z);
        }

        // Extract triangle indices (12 triangles × 3 indices)
        const auto& faces = box.get_faces();
        new_template.triangle_count = static_cast<int>(faces.size());
        new_template.triangle_indices.reserve(new_template.triangle_count * 3);

        for (const auto& face : faces) {
            for (int idx : face.vertex_indices) {
                new_template.triangle_indices.push_back(static_cast<uint16_t>(idx));
            }
        }
    }

    // Store template in cache
    auto result = g_cache.emplace(key, std::move(new_template));
    g_stats.template_count = g_cache.size();

    return &result.first->second;
}

void initialize_common_templates() {
    std::cout << "[ShadowGeometryCache] Pre-populating common templates...\n";

    // Common cube sizes
    get_template({TemplateKey::ShapeType::CUBE, 1.0f, 0.0f, 0.0f});
    get_template({TemplateKey::ShapeType::CUBE, 0.5f, 0.0f, 0.0f});
    get_template({TemplateKey::ShapeType::CUBE, 2.0f, 0.0f, 0.0f});

    // Common box dimensions (floor tiles, walls, etc.)
    get_template({TemplateKey::ShapeType::BOX, 10.0f, 10.0f, 0.1f});  // Floor tile
    get_template({TemplateKey::ShapeType::BOX, 1.0f, 1.0f, 2.0f});    // Wall segment
    get_template({TemplateKey::ShapeType::BOX, 5.0f, 5.0f, 0.2f});    // Platform

    std::cout << "[ShadowGeometryCache] Initialized " << g_stats.template_count << " templates\n";
}

void clear_cache() {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    g_cache.clear();
    g_stats = {0, 0, 0};
}

CacheStats get_stats() {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    return g_stats;
}

} // namespace ShadowGeometryCache
