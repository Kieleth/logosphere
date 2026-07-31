#ifndef SPHERE_GEOMETRY_H
#define SPHERE_GEOMETRY_H

#include "particle_geometry_v2.h"
#include <vector>

// ============================================================================
// SPHERE + ELLIPSOID GEOMETRY — icosahedron-derived triangle mesh
// ============================================================================
// Adds sphere and ellipsoid shapes to the engine's particle renderer. Both
// use the same icosahedron topology (12 vertices, 20 faces); the sphere
// scales all axes uniformly, the ellipsoid scales each axis independently.
//
// This parallels FlatParticleGeometry (for BOX) and TriangleCubeGeometry.
// The engine's renderer is shape-agnostic past GetSurfaces(), so these new
// shapes flow through the same triangle pipeline as boxes.
//
// Triangle count: 20 per particle (base icosahedron, no subdivision). Cheap
// enough for hundreds of concurrent spheres; upgradeable to subdivision-1
// (80 triangles) later if needed.
// ============================================================================

namespace ParticleGeometryV2 {

// Triangle counts by subdivision level:
//   0 →  20  (base icosahedron — faceted, D20 look)
//   1 →  80
//   2 → 320  (reads as smooth at typical game distances)
//   3 → 1280
class SphereGeometry : public GeometryV2 {
public:
    // Level-0 is the legacy call site and keeps the old triangle count.
    explicit SphereGeometry(float radius, int subdivisions = 0);

    std::vector<Surface> to_surfaces(const Transform& transform) const;
    void to_surfaces_into(const Transform& transform, std::vector<Surface>& out) const;
    void to_shadow_vertices(const Transform& transform,
                            std::vector<float>& out_vertices) const;
};

class EllipsoidGeometry : public GeometryV2 {
public:
    EllipsoidGeometry(float half_width, float half_height, float half_thickness,
                      int subdivisions = 0);

    std::vector<Surface> to_surfaces(const Transform& transform) const;
    void to_surfaces_into(const Transform& transform, std::vector<Surface>& out) const;
    void to_shadow_vertices(const Transform& transform,
                            std::vector<float>& out_vertices) const;
};

} // namespace ParticleGeometryV2

#endif // SPHERE_GEOMETRY_H
