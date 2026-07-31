#ifndef TRIANGLE_CUBE_GEOMETRY_H
#define TRIANGLE_CUBE_GEOMETRY_H

#include "particle_geometry_v2.h"
#include <vector>

// ============================================================================
// TRIANGLE-BASED CUBE GEOMETRY - The Future of Logosphere Rendering
// ============================================================================
// PURPOSE: Replace quad-based surfaces with triangles for performance
// WHY: Eliminate Newton-Raphson iteration, use direct barycentric coordinates
// IMPACT: Expected 20-30% speedup in UV calculation
//
// This is a SURGICAL implementation - we create triangles directly, no quads!
// Each cube face becomes 2 triangles instead of 1 quad.
// ============================================================================

namespace ParticleGeometryV2 {

class TriangleCubeGeometry : public GeometryV2 {
public:
    // Create a cube with given size using ONLY triangles
    explicit TriangleCubeGeometry(float size);

    // Convert to Surface format for rendering
    // Each face generates 2 triangle surfaces (12 total for a cube)
    std::vector<Surface> to_surfaces(const Transform& transform) const;

    // OPTIMIZATION: Direct vertex output for shadow triangles
    // Outputs raw triangle vertices (9 floats per triangle: v0.xyz, v1.xyz, v2.xyz)
    // Bypasses Surface struct creation entirely (5-10x faster than to_surfaces)
    // Expected: 12 triangles × 9 floats = 108 floats for a cube
    void to_shadow_vertices(const Transform& transform, std::vector<float>& out_vertices) const;

private:
    // Helper to create a triangle surface from 3 vertices
    Surface create_triangle_surface(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                   const Vec3& normal, int particle_id) const;
};

} // namespace ParticleGeometryV2

#endif // TRIANGLE_CUBE_GEOMETRY_H