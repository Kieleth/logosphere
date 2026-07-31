#include "flat_particle_geometry.h"
#include <cmath>

namespace ParticleGeometryV2 {

// ============================================================================
// FLAT PARTICLE GEOMETRY IMPLEMENTATION
// ============================================================================
// Following the same triangle-based pattern as TriangleCubeGeometry.
// Each face is split into 2 triangles using a consistent diagonal.
// ============================================================================

FlatParticleGeometry::FlatParticleGeometry(float width, float height, float thickness) {
    float half_width = width / 2.0f;
    float half_height = height / 2.0f;
    float half_thickness = thickness / 2.0f;

    // Define the 8 corners of the rectangular prism
    // Using the same systematic numbering: binary counting in XYZ
    local_vertices.push_back(Vec3(-half_width, -half_height, -half_thickness));  // 0: left-back-bottom
    local_vertices.push_back(Vec3( half_width, -half_height, -half_thickness));  // 1: right-back-bottom
    local_vertices.push_back(Vec3( half_width,  half_height, -half_thickness));  // 2: right-front-bottom
    local_vertices.push_back(Vec3(-half_width,  half_height, -half_thickness));  // 3: left-front-bottom
    local_vertices.push_back(Vec3(-half_width, -half_height,  half_thickness));  // 4: left-back-top
    local_vertices.push_back(Vec3( half_width, -half_height,  half_thickness));  // 5: right-back-top
    local_vertices.push_back(Vec3( half_width,  half_height,  half_thickness));  // 6: right-front-top
    local_vertices.push_back(Vec3(-half_width,  half_height,  half_thickness));  // 7: left-front-top

    // =========================================================================
    // Convert each quad face to 2 triangles
    // =========================================================================
    // Quad vertices: [0,1,2,3] becomes:
    // Triangle 1: [0,1,2] (bottom-right triangle)
    // Triangle 2: [0,2,3] (top-left triangle)
    // =========================================================================

    // Face 0: +Y face (front - points in +Y direction)
    Face tri1;
    tri1.vertex_indices = {3, 2, 6};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, 1, 0);
    faces.push_back(tri1);

    Face tri2;
    tri2.vertex_indices = {3, 6, 7};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, 1, 0);
    faces.push_back(tri2);

    // Face 1: -Y face (back - points in -Y direction)
    tri1.vertex_indices = {1, 0, 4};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, -1, 0);
    faces.push_back(tri1);

    tri2.vertex_indices = {1, 4, 5};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, -1, 0);
    faces.push_back(tri2);

    // Face 2: +Z face (top - points in +Z direction)
    tri1.vertex_indices = {7, 6, 5};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, 0, 1);
    faces.push_back(tri1);

    tri2.vertex_indices = {7, 5, 4};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, 0, 1);
    faces.push_back(tri2);

    // Face 3: -Z face (bottom - points in -Z direction)
    tri1.vertex_indices = {0, 1, 2};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(0, 0, -1);
    faces.push_back(tri1);

    tri2.vertex_indices = {0, 2, 3};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(0, 0, -1);
    faces.push_back(tri2);

    // Face 4: +X face (right - points in +X direction)
    tri1.vertex_indices = {2, 1, 5};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(1, 0, 0);
    faces.push_back(tri1);

    tri2.vertex_indices = {2, 5, 6};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(1, 0, 0);
    faces.push_back(tri2);

    // Face 5: -X face (left - points in -X direction)
    tri1.vertex_indices = {0, 3, 7};
    tri1.uvs = {UV(0,0), UV(1,0), UV(1,1)};
    tri1.normal = Vec3(-1, 0, 0);
    faces.push_back(tri1);

    tri2.vertex_indices = {0, 7, 4};
    tri2.uvs = {UV(0,0), UV(1,1), UV(0,1)};
    tri2.normal = Vec3(-1, 0, 0);
    faces.push_back(tri2);
}

std::vector<Surface> FlatParticleGeometry::to_surfaces(const Transform& transform) const {
    std::vector<Surface> surfaces;
    to_surfaces_into(transform, surfaces);
    return surfaces;
}

// Appending variant: caller owns the buffer so a hot loop reuses one
// allocation instead of heap-allocating per particle per frame.
void FlatParticleGeometry::to_surfaces_into(const Transform& transform,
                                            std::vector<Surface>& surfaces) const {
    // Transform vertices to world space. The by-value get_world_vertices()
    // heap-allocates on every call, and this runs once per box per frame on
    // the render path and again on the shadow path. Reuse one buffer.
    thread_local std::vector<Vec3> world_vertices;
    get_world_vertices_into(transform, world_vertices);
    surfaces.reserve(surfaces.size() + faces.size());

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
}

Surface FlatParticleGeometry::create_triangle_surface(const Vec3& v0, const Vec3& v1, const Vec3& v2,
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

    // Store the 3 vertices of the triangle
    surf.vertex_count = 3;
    surf.vertices[0][0] = v0.x; surf.vertices[0][1] = v0.y; surf.vertices[0][2] = v0.z;
    surf.vertices[1][0] = v1.x; surf.vertices[1][1] = v1.y; surf.vertices[1][2] = v1.z;
    surf.vertices[2][0] = v2.x; surf.vertices[2][1] = v2.y; surf.vertices[2][2] = v2.z;

    // Calculate surface area using cross product
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    Vec3 cross = edge1.cross(edge2);
    surf.area = cross.length() / 2.0f;  // Triangle area = |cross product| / 2

    // Calculate approximate width and height from edge lengths
    float len1 = edge1.length();
    float len2 = edge2.length();
    surf.width = std::max(len1, len2);
    surf.height = std::min(len1, len2);

    surf.particle_id = particle_id;
    surf.surface_index = -1;

    return surf;
}

// OPTIMIZATION: Direct vertex output for shadow triangles
// Bypasses Surface struct creation - outputs raw triangle vertices
// Expected speedup: 5-10x vs to_surfaces() (eliminates normal/UV/Surface overhead)
void FlatParticleGeometry::to_shadow_vertices(const Transform& transform, std::vector<float>& out_vertices) const {
    // Get transformed vertices into a reused buffer, not a fresh allocation.
    thread_local std::vector<Vec3> world_vertices;
    get_world_vertices_into(transform, world_vertices);

    // Reserve space for triangles × 9 floats
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
