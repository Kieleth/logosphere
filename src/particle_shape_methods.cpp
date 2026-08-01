// Particle::GetSurfaces and Particle::GetShadowTriangles — shape-branched
// geometry dispatchers. Extracted from core/particle_system.cpp so the
// pure-geometry path does not drag ParticleSystem (or the rest of the core
// TU) into the headless build profile.

#include "particle_core.h"

#include "particle_geometry_v2.h"
#include "triangle_cube_geometry.h"
#include "flat_particle_geometry.h"
#include "sphere_geometry.h"
#include "optimization_flags.h"

#include <cstdlib>
#include <atomic>

// Icosphere subdivision level for SPHERE / ELLIPSOID particles.
// 0 = 20 triangles (faceted D20), 1 = 80, 2 = 320, 3 = 1280.
// Default lives in Optimizations::SPHERE_SUBDIVISIONS. LOGOSPHERE_SPHERE_LOD
// overrides it at runtime so quality and cost can be compared without a
// rebuild — read once, so every particle in a run agrees.
namespace {
std::atomic<int> g_sphere_lod{[] {
    if (const char* env = std::getenv("LOGOSPHERE_SPHERE_LOD")) {
        const int v = std::atoi(env);
        if (v >= 0 && v <= 4) return v;
    }
    return Optimizations::SPHERE_SUBDIVISIONS;
}()};
}  // namespace

namespace logosphere {
void set_sphere_lod(int level) {
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    g_sphere_lod.store(level, std::memory_order_relaxed);
}
int get_sphere_lod() { return g_sphere_lod.load(std::memory_order_relaxed); }
}  // namespace logosphere

static int sphere_subdivisions() {
    return g_sphere_lod.load(std::memory_order_relaxed);
}

// Appending variant. The render path calls this once per visible particle
// per frame, so returning a fresh vector each time was a heap allocation
// per particle per frame. Callers pass a reusable buffer instead; capacity
// survives across particles and frames. Study S7 established that render
// cost tracks SURFACES, which is what makes this path worth the care.
void Particle::GetSurfacesInto(std::vector<Surface>& out) const {
    out.clear();
    ParticleGeometryV2::Transform transform(
        ParticleGeometryV2::Vec3(x, y, z),
        rotation_x, rotation_y, rotation_z);

    if (shape == ParticleShape::SPHERE) {
        ParticleGeometryV2::SphereGeometry sphere_geom(size * 0.5f, sphere_subdivisions());
        sphere_geom.to_surfaces_into(transform, out);
    } else if (shape == ParticleShape::ELLIPSOID) {
        ParticleGeometryV2::EllipsoidGeometry ellip_geom(
            width * 0.5f, height * 0.5f, thickness * 0.5f, sphere_subdivisions());
        ellip_geom.to_surfaces_into(transform, out);
    } else if (shape == ParticleShape::BOX) {
        // One box per thread, re-pointed rather than rebuilt. The constructor
        // reallocates ~9 times filling 8 vertices and 12 faces; this ran once
        // per visible particle per frame. See FlatParticleGeometry::resize.
        thread_local ParticleGeometryV2::FlatParticleGeometry flat_geom(1.0f, 1.0f, 1.0f);
        flat_geom.resize(width, height, thickness);
        flat_geom.to_surfaces_into(transform, out);
    } else {
        // Cube fallback still allocates internally; wrap for correctness and
        // revisit if profiling shows this shape is hot.
        ParticleGeometryV2::TriangleCubeGeometry cube_geom(size);
        auto v = cube_geom.to_surfaces(transform);
        out.insert(out.end(), v.begin(), v.end());
    }
}

std::vector<Surface> Particle::GetSurfaces() const {
    ParticleGeometryV2::Transform transform(
        ParticleGeometryV2::Vec3(x, y, z),
        rotation_x, rotation_y, rotation_z);

    std::vector<Surface> surfaces;
    if (shape == ParticleShape::SPHERE) {
        ParticleGeometryV2::SphereGeometry sphere_geom(size * 0.5f, sphere_subdivisions());
        surfaces = sphere_geom.to_surfaces(transform);
    } else if (shape == ParticleShape::ELLIPSOID) {
        ParticleGeometryV2::EllipsoidGeometry ellip_geom(
            width * 0.5f, height * 0.5f, thickness * 0.5f, sphere_subdivisions());
        surfaces = ellip_geom.to_surfaces(transform);
    } else if (shape == ParticleShape::BOX) {
        ParticleGeometryV2::FlatParticleGeometry flat_geom(width, height, thickness);
        surfaces = flat_geom.to_surfaces(transform);
    } else {
        ParticleGeometryV2::TriangleCubeGeometry cube_geom(size);
        surfaces = cube_geom.to_surfaces(transform);
    }

    for (auto& surf : surfaces) {
        surf.roughness   = this->roughness;
        surf.reflectance = this->reflectivity;
    }
    return surfaces;
}

// Fast path for shadow triangle generation: raw float triples, no Surface
// struct overhead. ~5–10× faster than GetSurfaces() when the caller only
// needs triangles.
void Particle::GetShadowTriangles(std::vector<float>& out_vertices) const {
    ParticleGeometryV2::Transform transform(
        ParticleGeometryV2::Vec3(x, y, z),
        rotation_x, rotation_y, rotation_z);

    if (shape == ParticleShape::SPHERE) {
        ParticleGeometryV2::SphereGeometry sphere_geom(size * 0.5f, sphere_subdivisions());
        sphere_geom.to_shadow_vertices(transform, out_vertices);
    } else if (shape == ParticleShape::ELLIPSOID) {
        ParticleGeometryV2::EllipsoidGeometry ellip_geom(
            width * 0.5f, height * 0.5f, thickness * 0.5f, sphere_subdivisions());
        ellip_geom.to_shadow_vertices(transform, out_vertices);
    } else if (shape == ParticleShape::BOX) {
        // Same reused instance as the render path; see GetSurfacesInto.
        thread_local ParticleGeometryV2::FlatParticleGeometry flat_geom(1.0f, 1.0f, 1.0f);
        flat_geom.resize(width, height, thickness);
        flat_geom.to_shadow_vertices(transform, out_vertices);
    } else {
        ParticleGeometryV2::TriangleCubeGeometry cube_geom(size);
        cube_geom.to_shadow_vertices(transform, out_vertices);
    }
}
