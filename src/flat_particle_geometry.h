#ifndef FLAT_PARTICLE_GEOMETRY_H
#define FLAT_PARTICLE_GEOMETRY_H

#include "particle_geometry_v2.h"
#include <vector>

// ============================================================================
// FLAT PARTICLE GEOMETRY - Thin Rectangular Surfaces
// ============================================================================
// PURPOSE: Create flat rectangular surfaces with explicit dimensions
// USE CASES: Floor tiles, wall panels, platforms, signs, carpets, etc.
//
// This is a thin rectangular prism with full dimensional control.
// Uses the same triangle-based approach as TriangleCubeGeometry.
// ============================================================================

namespace ParticleGeometryV2 {

class FlatParticleGeometry : public GeometryV2 {
public:
    // Create a flat rectangular surface with explicit dimensions
    // width: X dimension (horizontal extent)
    // height: Y dimension (depth extent)
    // thickness: Z dimension (vertical height)
    explicit FlatParticleGeometry(float width, float height, float thickness);

    // Convert to Surface format for rendering
    // Each face generates 2 triangle surfaces (12 total)
    std::vector<Surface> to_surfaces(const Transform& transform) const;
    // Appends into a caller-owned buffer; no per-call allocation.
    void to_surfaces_into(const Transform& transform, std::vector<Surface>& out) const;

    // OPTIMIZATION: Direct vertex output for shadow triangles
    // Outputs raw triangle vertices (9 floats per triangle: v0.xyz, v1.xyz, v2.xyz)
    // Bypasses Surface struct creation entirely (5-10x faster than to_surfaces)
    void to_shadow_vertices(const Transform& transform, std::vector<float>& out_vertices) const;

private:
    // Helper to create a triangle surface from 3 vertices
    Surface create_triangle_surface(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                                   const Vec3& normal, int particle_id) const;
};

} // namespace ParticleGeometryV2

#endif // FLAT_PARTICLE_GEOMETRY_H
