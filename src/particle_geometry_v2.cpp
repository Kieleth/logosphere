#include "particle_geometry_v2.h"
#include "logosphere/rendering/rasterization_math.h"  // For 3D math primitives
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <cassert>
#include "uv_cache.h"
#include "uv_coherence_cache.h"

namespace ParticleGeometryV2 {

// ============================================================================
// Vec3 Implementation - A Point or Direction in 3D Space
// ============================================================================
//
// Vec3 represents the fundamental building block of 3D geometry: a triplet of
// numbers that can mean either a position in space (x,y,z coordinates) or a
// direction/velocity (dx,dy,dz components). The meaning depends on context.
//
// Why we need this: Every vertex, every normal, every movement in 3D requires
// us to track three values together. This class keeps them organized and
// provides the mathematical operations we need.
//
// Note: Constructors are defined inline in the header file

Vec3 Vec3::operator+(const Vec3& other) const {
    // Adding vectors: if they're positions, this gives midpoint logic
    // If they're velocities, this combines movements
    return Vec3(x + other.x, y + other.y, z + other.z);
}

Vec3 Vec3::operator-(const Vec3& other) const {
    // Subtracting positions gives the direction from one to another
    // Subtracting velocities gives the change in velocity
    return Vec3(x - other.x, y - other.y, z - other.z);
}

Vec3 Vec3::operator*(float scale) const {
    // Scaling is uniform in all dimensions - no distortion
    // Negative scales flip the direction
    return Vec3(x * scale, y * scale, z * scale);
}

Vec3 Vec3::operator/(float divisor) const {
    // Division by scalar - essential for averaging positions
    // Example: center of 4 vertices = (v1 + v2 + v3 + v4) / 4
    // We check for near-zero to avoid infinity/NaN
    if (std::abs(divisor) < 0.0001f) {
        return *this;  // Can't divide by zero, return unchanged
    }
    return Vec3(x / divisor, y / divisor, z / divisor);
}

float Vec3::length() const {
    // Pythagorean theorem in 3D: distance = sqrt(x² + y² + z²)
    // This gives us the magnitude of a vector
    return std::sqrt(x*x + y*y + z*z);
}

Vec3 Vec3::normalized() const {
    // A normalized vector has length 1 but points in the same direction
    // This is crucial for normals (perpendicular directions) and any time
    // we care about direction but not magnitude
    float len = length();
    if (len > 0.0001f) {  // Avoid divide by zero for tiny vectors
        return Vec3(x/len, y/len, z/len);
    }
    return *this;  // Can't normalize a zero vector - return as-is
}

float Vec3::dot(const Vec3& other) const {
    // Dot product tells us how "aligned" two vectors are:
    // - Positive: pointing in similar directions
    // - Zero: perpendicular
    // - Negative: pointing in opposite directions
    // Also gives us the length of one vector projected onto another
    return x * other.x + y * other.y + z * other.z;
}

Vec3 Vec3::cross(const Vec3& other) const {
    // Cross product gives a vector perpendicular to both inputs
    // The magnitude equals the area of the parallelogram they span
    // Right-hand rule: curl fingers from first to second, thumb points result
    return Vec3(
        y * other.z - z * other.y,
        z * other.x - x * other.z,
        x * other.y - y * other.x
    );
}

Vec3 Vec3::rotated(float roll, float pitch, float yaw) const {
    // =========================================================================
    // Rotate a vector by Euler angles (in radians)
    // =========================================================================
    // This is how we orient things in 3D space. Think of it like this:
    // - Roll: Tilt your head side to side (rotation around X)
    // - Pitch: Nod your head up and down (rotation around Y)  
    // - Yaw: Shake your head "no" (rotation around Z)
    //
    // We apply rotations in X->Y->Z order (industry standard)
    // Each rotation happens in the frame of the previous rotation
    // This is called "intrinsic" or "local" rotations
    //
    // The math uses rotation matrices, but we expand them for clarity
    Vec3 result = *this;
    
    // X rotation (roll) - rotates in the YZ plane
    if (roll != 0) {
        float cos_r = std::cos(roll);
        float sin_r = std::sin(roll);
        // X component stays the same, Y and Z rotate
        float new_y = result.y * cos_r - result.z * sin_r;
        float new_z = result.y * sin_r + result.z * cos_r;
        result.y = new_y;
        result.z = new_z;
    }
    
    // Y rotation (pitch) - rotates in the XZ plane
    if (pitch != 0) {
        float cos_p = std::cos(pitch);
        float sin_p = std::sin(pitch);
        // Y component stays the same, X and Z rotate
        float new_x = result.x * cos_p + result.z * sin_p;
        float new_z = -result.x * sin_p + result.z * cos_p;
        result.x = new_x;
        result.z = new_z;
    }
    
    // Z rotation (yaw) - rotates in the XY plane
    if (yaw != 0) {
        float cos_y = std::cos(yaw);
        float sin_y = std::sin(yaw);
        // Z component stays the same, X and Y rotate
        float new_x = result.x * cos_y - result.y * sin_y;
        float new_y = result.x * sin_y + result.y * cos_y;
        result.x = new_x;
        result.y = new_y;
    }
    
    return result;
}

// ============================================================================
// Face Implementation - A Polygon in 3D Space
// ============================================================================
//
// A face is a flat polygon defined by vertices. In our system, faces are
// typically quads (4 vertices) but the code supports any convex polygon.
// Each face knows its vertices, how to map textures (UV), and which way
// it's facing (normal).

void Face::compute_normal(const std::vector<Vec3>& vertices) {
    // To find which way a face is pointing, we use the cross product trick:
    // Take any two edges of the polygon and cross them. The result points
    // perpendicular to the face.
    //
    // Why first 3 vertices? Any 3 non-collinear points define a plane.
    // We assume vertices are given in a consistent winding order.
    
    if (vertex_indices.size() < 3) {
        // Can't compute normal from less than 3 points - default to "up"
        normal = Vec3(0, 0, 1);
        return;
    }
    
    // Get first 3 vertices of the face
    const Vec3& v0 = vertices[vertex_indices[0]];
    const Vec3& v1 = vertices[vertex_indices[1]];
    const Vec3& v2 = vertices[vertex_indices[2]];
    
    // Create two edges from v0 to v1 and v0 to v2
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    
    // Cross product gives perpendicular vector
    // The direction depends on winding order (clockwise vs counter-clockwise)
    normal = edge1.cross(edge2).normalized();
}

Vec3 Face::get_center(const std::vector<Vec3>& vertices) const {
    // The center of a face is simply the average of all its vertices
    // This is the centroid - the "balance point" of the polygon
    Vec3 center(0, 0, 0);
    
    if (vertex_indices.empty()) return center;
    
    // Sum all vertex positions
    for (int idx : vertex_indices) {
        center = center + vertices[idx];
    }
    
    // Divide by count to get average
    return center * (1.0f / vertex_indices.size());
}

// ============================================================================
// Transform Implementation - Position and Orientation in Space
// ============================================================================
//
// A Transform tells us where something is (position) and how it's rotated.
// Think of it as the "pose" of an object. Every particle has a transform
// that places its local geometry into the world.
//
// Why Euler angles? They're intuitive (pitch/yaw/roll) even though they
// have issues like gimbal lock. For our particle system, they're sufficient.
//
// IMPORTANT: Rotation order matters! Different orders give different results.
// We use X->Y->Z (pitch->yaw->roll) which is common in games.
// This is "intrinsic" rotation - each axis rotates in the object's local frame.

Transform::Transform() 
    : position(0, 0, 0), rotation_x(0), rotation_y(0), rotation_z(0) {}

Transform::Transform(const Vec3& pos, float rx, float ry, float rz)
    : position(pos), rotation_x(rx), rotation_y(ry), rotation_z(rz) {}

Vec3 Transform::transform_point(const Vec3& local_point) const {
    // This function converts a point from "local space" (relative to the object)
    // to "world space" (absolute position in the scene).
    //
    // The process:
    // 1. Start with local coordinates (as if object is at origin, unrotated)
    // 2. Apply rotations to orient the point
    // 3. Add position to place it in the world
    //
    // Why this order? Rotation happens around the object's center (origin in
    // local space). If we translated first, rotation would orbit around world origin.
    
    Vec3 p = local_point;
    
    // Apply rotations in X-Y-Z order (Euler convention)
    // Each rotation uses the previous frame's axes, not world axes
    
    // X rotation (pitch) - like nodding your head
    if (rotation_x != 0) {
        float cos_x = std::cos(rotation_x);
        float sin_x = std::sin(rotation_x);
        // X rotation leaves X unchanged, rotates Y-Z plane
        float new_y = p.y * cos_x - p.z * sin_x;
        float new_z = p.y * sin_x + p.z * cos_x;
        p.y = new_y;
        p.z = new_z;
    }
    
    // Y rotation (yaw) - like shaking your head "no"
    if (rotation_y != 0) {
        float cos_y = std::cos(rotation_y);
        float sin_y = std::sin(rotation_y);
        // Y rotation leaves Y unchanged, rotates X-Z plane
        float new_x = p.x * cos_y + p.z * sin_y;
        float new_z = -p.x * sin_y + p.z * cos_y;
        p.x = new_x;
        p.z = new_z;
    }
    
    // Z rotation (yaw) - rotating around vertical axis
    // Uses CLOCKWISE rotation to match movement convention:
    //   rotation_z = 0 → facing North (+Y)
    //   rotation_z = 90° → facing East (+X)
    //   rotation_z = 180° → facing South (-Y)
    if (rotation_z != 0) {
        float cos_z = std::cos(rotation_z);
        float sin_z = std::sin(rotation_z);
        // Z rotation leaves Z unchanged, rotates X-Y plane (clockwise from above)
        float new_x = p.x * cos_z + p.y * sin_z;
        float new_y = -p.x * sin_z + p.y * cos_z;
        p.x = new_x;
        p.y = new_y;
    }
    
    // Finally, translate to world position
    return p + position;
}

Vec3 Transform::transform_direction(const Vec3& local_dir) const {
    // Directions (like normals) only rotate, they don't translate
    // A normal pointing "up" in local space needs to be rotated to
    // know which way "up" is after the object is rotated
    //
    // This is identical to transform_point except no translation at the end
    
    Vec3 d = local_dir;
    
    if (rotation_x != 0) {
        float cos_x = std::cos(rotation_x);
        float sin_x = std::sin(rotation_x);
        float new_y = d.y * cos_x - d.z * sin_x;
        float new_z = d.y * sin_x + d.z * cos_x;
        d.y = new_y;
        d.z = new_z;
    }
    
    if (rotation_y != 0) {
        float cos_y = std::cos(rotation_y);
        float sin_y = std::sin(rotation_y);
        float new_x = d.x * cos_y + d.z * sin_y;
        float new_z = -d.x * sin_y + d.z * cos_y;
        d.x = new_x;
        d.z = new_z;
    }
    
    // Z rotation (yaw) - clockwise to match movement convention
    if (rotation_z != 0) {
        float cos_z = std::cos(rotation_z);
        float sin_z = std::sin(rotation_z);
        float new_x = d.x * cos_z + d.y * sin_z;
        float new_y = -d.x * sin_z + d.y * cos_z;
        d.x = new_x;
        d.y = new_y;
    }

    return d;
}

// ============================================================================
// GeometryV2 Base Class - The Shape of Things
// ============================================================================
//
// GeometryV2 stores the actual 3D shape: where are the vertices? how are they
// connected into faces? This is the "blueprint" that gets placed in the world
// using a Transform.
//
// Key insight: We store geometry in "local space" - as if the object is at the
// origin and unrotated. This lets us reuse the same geometry for multiple
// particles, each with their own transform.

std::vector<Vec3> GeometryV2::get_world_vertices(const Transform& transform) const {
    // Convert all vertices from local to world space
    // This is what actually "places" the geometry in the scene
    std::vector<Vec3> world_vertices;
    world_vertices.reserve(local_vertices.size());  // Pre-allocate for efficiency
    
    for (const auto& v : local_vertices) {
        world_vertices.push_back(transform.transform_point(v));
    }
    
    return world_vertices;
}

void GeometryV2::get_world_vertices_into(const Transform& transform,
                                         std::vector<Vec3>& out) const {
    out.clear();
    out.reserve(local_vertices.size());
    for (const auto& v : local_vertices) {
        out.push_back(transform.transform_point(v));
    }
}

std::vector<Face> GeometryV2::get_world_faces(const Transform& transform) const {
    // Faces need their normals transformed to world space
    // The vertices themselves aren't stored in Face, just indices
    std::vector<Face> world_faces = faces;  // Copy face structure
    
    // Transform each face normal to world space
    for (auto& face : world_faces) {
        // Normals are directions, so we use transform_direction
        face.normal = transform.transform_direction(face.normal).normalized();
    }
    
    return world_faces;
}

// Extract the actual 3D positions of a face's vertices
// This gives you the corners of the polygon in world space
std::vector<Vec3> GeometryV2::get_face_vertices_world(int face_index, const Transform& transform) const {
    std::vector<Vec3> vertices;
    
    if (face_index < 0 || face_index >= (int)faces.size()) {
        return vertices;  // Invalid face index
    }
    
    const Face& face = faces[face_index];
    
    // Transform each vertex of this face to world space
    for (int vertex_idx : face.vertex_indices) {
        Vec3 world_pos = transform.transform_point(local_vertices[vertex_idx]);
        vertices.push_back(world_pos);
    }
    
    return vertices;
}

// ============================================================================
// CubeGeometryV2 - A Specific Shape: The Cube
// ============================================================================
//
// A cube has 8 vertices and 6 faces. It's the simplest 3D solid that fully
// encloses space. We define it centered at the origin with a given size.

CubeGeometryV2::CubeGeometryV2(float size) {
    float half = size / 2.0f;
    
    // Define the 8 corners of the cube
    // We number them systematically: binary counting in XYZ
    // vertex 0 = (0,0,0) in binary = (-,-,-) = left, back, bottom
    // vertex 1 = (0,0,1) in binary = (-,-,+) = left, back, top
    // etc.
    //
    // This systematic approach makes it easier to reason about faces
    
    // Bottom face vertices (z = -half):
    local_vertices.push_back(Vec3(-half, -half, -half));  // 0: left-back-bottom
    local_vertices.push_back(Vec3( half, -half, -half));  // 1: right-back-bottom
    local_vertices.push_back(Vec3( half,  half, -half));  // 2: right-front-bottom
    local_vertices.push_back(Vec3(-half,  half, -half));  // 3: left-front-bottom
    
    // Top face vertices (z = +half):
    local_vertices.push_back(Vec3(-half, -half,  half));  // 4: left-back-top
    local_vertices.push_back(Vec3( half, -half,  half));  // 5: right-back-top
    local_vertices.push_back(Vec3( half,  half,  half));  // 6: right-front-top
    local_vertices.push_back(Vec3(-half,  half,  half));  // 7: left-front-top
    
    // Define the 6 faces
    // Each face lists vertices in counter-clockwise order when viewed from outside
    // This consistent winding ensures normals point outward
    
    faces.resize(6);
    
    // Face 0: +Y face (front - points in +Y direction)
    // Looking at the cube from +Y, we see vertices 3,2,6,7
    faces[0].vertex_indices = {3, 2, 6, 7};
    faces[0].uvs = {UV(0,0), UV(1,0), UV(1,1), UV(0,1)};  // Standard UV mapping
    faces[0].normal = Vec3(0, 1, 0);
    
    // Face 1: -Y face (back - points in -Y direction)
    // Looking at the cube from -Y, we see vertices 1,0,4,5
    faces[1].vertex_indices = {1, 0, 4, 5};
    faces[1].uvs = {UV(0,0), UV(1,0), UV(1,1), UV(0,1)};
    faces[1].normal = Vec3(0, -1, 0);
    
    // Face 2: +Z face (top - points in +Z direction)
    faces[2].vertex_indices = {7, 6, 5, 4};
    faces[2].uvs = {UV(0,0), UV(1,0), UV(1,1), UV(0,1)};
    faces[2].normal = Vec3(0, 0, 1);
    
    // Face 3: -Z face (bottom - points in -Z direction)
    faces[3].vertex_indices = {0, 1, 2, 3};
    faces[3].uvs = {UV(0,0), UV(1,0), UV(1,1), UV(0,1)};
    faces[3].normal = Vec3(0, 0, -1);
    
    // Face 4: +X face (right - points in +X direction)
    faces[4].vertex_indices = {2, 1, 5, 6};
    faces[4].uvs = {UV(0,0), UV(1,0), UV(1,1), UV(0,1)};
    faces[4].normal = Vec3(1, 0, 0);
    
    // Face 5: -X face (left - points in -X direction)
    faces[5].vertex_indices = {0, 3, 7, 4};
    faces[5].uvs = {UV(0,0), UV(1,0), UV(1,1), UV(0,1)};
    faces[5].normal = Vec3(-1, 0, 0);
}

// ============================================================================
// Utility Functions - Bridging to Other Systems
// ============================================================================

// Convert our clean geometry representation to the legacy Surface format
// This is temporary - eventually all systems should use GeometryV2 directly
std::vector<Surface> geometry_to_surfaces(const GeometryV2& geometry, const Transform& transform) {
    std::vector<Surface> surfaces;
    
    auto world_vertices = geometry.get_world_vertices(transform);
    auto world_faces = geometry.get_world_faces(transform);
    
    for (size_t i = 0; i < world_faces.size(); i++) {
        const Face& face = world_faces[i];
        
        // Calculate face center
        Vec3 center = face.get_center(world_vertices);
        
        // Calculate dimensions for triangles or quads
        // We support both triangles (3 vertices) and quads (4 vertices)
        int num_vertices = face.vertex_indices.size();
        assert((num_vertices == 3 || num_vertices == 4) && "Only triangles and quads are supported!");
        
        Vec3 v0 = world_vertices[face.vertex_indices[0]];
        Vec3 v1 = world_vertices[face.vertex_indices[1]];
        Vec3 v2 = world_vertices[face.vertex_indices[2]];
        
        // Measure edge lengths
        float edge01 = (v1 - v0).length();
        float edge12 = (v2 - v1).length();
        float edge20 = (v0 - v2).length();
        
        // For a triangle, use the longest edge as width
        float max_width = std::max({edge01, edge12, edge20});
        
        // Calculate the area using cross product
        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec3 cross = edge1.cross(edge2);
        float area = cross.length() * 0.5f;
        
        // Height = 2 * area / base
        float max_height = 0;
        if (max_width > 0) {
            max_height = 2.0f * area / max_width;
        }
        
        // Fill in Surface struct
        Surface surf;
        surf.x = center.x;
        surf.y = center.y;
        surf.z = center.z;
        surf.nx = face.normal.x;
        surf.ny = face.normal.y;
        surf.nz = face.normal.z;
        surf.width = max_width;
        surf.height = max_height;
        surf.area = max_width * max_height;
        surf.surface_index = i;
        
        // =====================================================================
        // CRITICAL: Store actual vertex positions!
        // =====================================================================
        // This is the whole point of V2 - we have explicit vertices!
        // No more tangent space computation needed.
        surf.vertex_count = std::min((int)face.vertex_indices.size(), Surface::MAX_VERTICES);
        for (int v = 0; v < surf.vertex_count; v++) {
            const Vec3& vertex = world_vertices[face.vertex_indices[v]];
            surf.vertices[v][0] = vertex.x;
            surf.vertices[v][1] = vertex.y;
            surf.vertices[v][2] = vertex.z;
        }
        
        surfaces.push_back(surf);
    }
    
    return surfaces;
}

// Extract the vertices of a face as a simple array
// This is useful for systems that need raw vertex data
void extract_face_vertices(const GeometryV2& geometry, 
                           int face_index,
                           const Transform& transform,
                           float vertices_out[][3],
                           int max_vertices) {
    // Use the public accessors instead of direct member access
    const auto& faces = geometry.get_faces();
    const auto& local_vertices = geometry.get_local_vertices();
    
    if (face_index < 0 || face_index >= (int)faces.size()) {
        return;
    }
    
    const Face& face = faces[face_index];
    int num_to_copy = std::min((int)face.vertex_indices.size(), max_vertices);
    
    // Transform each vertex and store in the output array
    for (int i = 0; i < num_to_copy; i++) {
        Vec3 world_pos = transform.transform_point(
            local_vertices[face.vertex_indices[i]]
        );
        vertices_out[i][0] = world_pos.x;
        vertices_out[i][1] = world_pos.y;
        vertices_out[i][2] = world_pos.z;
    }
}

} // namespace ParticleGeometryV2

// ============================================================================
// Surface Methods - UV interpolation for any polygon shape
// ============================================================================

void Surface::get_world_position_at_uv(float u, float v, 
                                       float& out_x, float& out_y, float& out_z) const {
    // =========================================================================
    // Convert UV to world position using stored vertices
    // =========================================================================
    // This handles any polygon shape - triangle, quad, etc.
    // The key insight: we interpolate based on what we have.
    
    // NO COUNTING IN HOT PATH - this is called for every pixel!
    // Frame metrics removed for performance
    
    // =========================================================================
    // OPTIMIZATION: UV COHERENCE CACHE
    // WHY: UV→World calculations can be cached for adjacent pixels
    // HOW: Cache last UV calc + gradients, use deltas for adjacent pixels
    // IMPACT: For triangles, less benefit than quads but still worthwhile
    // =========================================================================
    if (Optimizations::USE_SCANLINE_COHERENCE) {
        // Try coherent calculation for adjacent pixels
        if (UVCoherenceCache::get().try_coherent_calculation(
                this, u, v, out_x, out_y, out_z)) {
            // NO COUNTING IN HOT PATH
            return;  // Success! Used delta calculation
        }
        // Fall through to full calculation
    }
    
    // =========================================================================
    // FAILED OPTIMIZATION: UV→WORLD CACHE
    // Result: 2 FPS slower despite 66% hit rate
    // Reason: Cache overhead > computation cost for our small calculations
    // =========================================================================
    if (Optimizations::USE_UV_CACHE) {
        // Try cache first
        if (UVCache::get().lookup(this, u, v, out_x, out_y, out_z)) {
            return;  // Cache hit! Skip all computation
        }
        // Cache miss - compute and store result below
    }
    
    // NO TIMING IN HOT PATH - this is called for every pixel!
    // Statistical sampling removed for performance
    
    // We only support triangles now - assert this
    assert(vertex_count == 3 && "Only triangle surfaces are supported!");
    
    // =====================================================================
    // TRIANGLE: Barycentric interpolation (direct calculation, no iteration!)
    // =====================================================================
    // For a triangle, UV maps to barycentric coordinates
    // u goes along edge 0->1, v goes toward vertex 2
    // Barycentric: (1-u-v) * v0 + u * v1 + v * v2
    
    float w = 1.0f - u - v;  // Weight for vertex 0
    if (w < 0) w = 0;        // Clamp to triangle
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    
    // DEBUG: Disabled - barycentric math is correct, issue is with triangle splitting

    // Direct barycentric interpolation using primitives
    // Much faster than the old quad bilinear (16 multiplies + 9 adds)!
    out_x = RasterizationMath::barycentric_interpolate(vertices[0][0], vertices[1][0], vertices[2][0], w, u, v);
    out_y = RasterizationMath::barycentric_interpolate(vertices[0][1], vertices[1][1], vertices[2][1], w, u, v);
    out_z = RasterizationMath::barycentric_interpolate(vertices[0][2], vertices[1][2], vertices[2][2], w, u, v);
    
    // =========================================================================
    // CACHE STORE: Save computed result for future lookups
    // =========================================================================
    if (Optimizations::USE_UV_CACHE) {
        UVCache::get().store(this, u, v, out_x, out_y, out_z);
    }
    
    // Store for coherence cache (needs vertex data)
    if (Optimizations::USE_SCANLINE_COHERENCE) {
        UVCoherenceCache::get().store_computation(
            this, u, v, out_x, out_y, out_z, vertices);
    }
}