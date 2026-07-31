#ifndef PARTICLE_GEOMETRY_V2_H
#define PARTICLE_GEOMETRY_V2_H

#include <vector>
#include <array>
#include <initializer_list>

// =============================================================================
// Surface - A piece of geometry in world space
// =============================================================================
// Represents a single face/surface of a particle with explicit vertex storage.
// This is the output of the geometry system - ready for rendering and physics.
struct Surface {
    // Position in world space (center of surface)
    float x, y, z;
    
    // Normal vector (direction surface faces)
    float nx, ny, nz;
    
    // Dimensions of the surface (for bounding box, collision, etc.)
    float width = 1.0f;   // Size along first tangent axis
    float height = 1.0f;  // Size along second tangent axis
    float area;           // Total surface area
    
    // =========================================================================
    // VERTEX DATA - The actual corners of this surface in world space
    // =========================================================================
    // This is the KEY improvement from V2 geometry!
    // Instead of computing corners from normal+tangent space, we store them.
    // For a quad: [0]=bottom-left, [1]=bottom-right, [2]=top-right, [3]=top-left
    // UV mapping: [0]=(0,0), [1]=(1,0), [2]=(1,1), [3]=(0,1)
    static constexpr int MAX_VERTICES = 4;  // Most surfaces are quads
    float vertices[MAX_VERTICES][3] = {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};  // Initialize to zeros
    int vertex_count = 4;                   // How many vertices (3 for triangle, 4 for quad)
    
    // Material properties
    float roughness = 0.5f;     // 0=mirror, 1=matte
    float metallic = 0.0f;      // 0=dielectric, 1=metal
    float reflectance = 0.5f;   // Base reflectivity
    
    // Parent particle (which particle owns this surface)
    int particle_id = -1;
    
    // Surface identifier within particle
    int surface_index = -1;
    
    // =========================================================================
    // GEOMETRY METHODS - Handle UV mapping and interpolation
    // =========================================================================
    // Convert UV coordinates (0-1) to world position on this surface
    // Handles triangles, quads, or any polygon shape
    void get_world_position_at_uv(float u, float v,
                                  float& out_x, float& out_y, float& out_z) const;
};

// =============================================================================
// Icosphere LOD control
// =============================================================================
// How many triangles a SPHERE / ELLIPSOID particle emits:
//   0 = 20, 1 = 80, 2 = 320, 3 = 1280, 4 = 5120.
// Default is Optimizations::SPHERE_SUBDIVISIONS, overridden at startup by
// LOGOSPHERE_SPHERE_LOD, and settable at runtime so a quality comparison can
// flip levels on a live scene instead of rebuilding. Read on the render
// workers, so the storage is atomic.
namespace logosphere {
void set_sphere_lod(int level);
int  get_sphere_lod();
}

// =============================================================================
// PARTICLE GEOMETRY V2 - Explicit Vertex-Based Geometry System
// =============================================================================
// This represents a fundamental shift in how we think about particle geometry.
// Instead of generating geometry procedurally from normals and dimensions,
// we store explicit vertices and faces. This gives us complete control over
// the exact shape and allows for consistent transformations.
//
// Key Concepts:
// - Vertices are stored in LOCAL SPACE (relative to particle center)
// - Faces connect vertices to form surfaces
// - Transforms convert from local to world space
// - UV coordinates map textures/lighting onto faces
// =============================================================================

namespace ParticleGeometryV2 {

// =============================================================================
// Vec3 - A point or vector in 3D space
// =============================================================================
// Represents either:
// - A position (x,y,z coordinates of a point)
// - A direction (vector from origin)
// - A displacement (difference between two points)
struct Vec3 {
    float x, y, z;
    
    // Constructors
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    
    // Vector operations
    Vec3 operator+(const Vec3& other) const;
    Vec3 operator-(const Vec3& other) const;
    Vec3 operator*(float scalar) const;
    Vec3 operator/(float scalar) const;
    
    // Dot product - measures alignment between vectors
    float dot(const Vec3& other) const;
    
    // Cross product - creates perpendicular vector
    Vec3 cross(const Vec3& other) const;
    
    // Length of the vector
    float length() const;
    
    // Normalized version (unit vector)
    Vec3 normalized() const;
    
    // Rotate this vector by Euler angles
    Vec3 rotated(float roll, float pitch, float yaw) const;
};

// =============================================================================
// UV - Texture/Parameter Coordinate
// =============================================================================
// UV coordinates map a 2D parameter space onto a 3D surface.
// Think of it like wrapping gift paper (2D) onto a box (3D).
// U goes left-to-right (0 to 1), V goes bottom-to-top (0 to 1).
struct UV {
    float u, v;
    UV() : u(0), v(0) {}
    UV(float u_, float v_) : u(u_), v(v_) {}
};

// =============================================================================
// Face - A surface defined by vertices
// =============================================================================
// A face connects vertices to form a renderable surface.
// For a quad (4-sided face), we store 4 vertex indices.
// UV coordinates define how textures/lighting map onto the face.
// Fixed-capacity inline storage. A face is a triangle or a quad, never more,
// but `vertex_indices` and `uvs` were std::vectors: two heap allocations per
// face. An icosphere at subdivision 2 is 320 faces, so constructing one cost
// 640 allocations, and the render path constructed one per sphere per call,
// twice per frame. Inline storage removes the allocation at the type level.
// The interface matches the std::vector subset the call sites use, so nothing
// downstream changes.
template <typename T, int N>
struct SmallArray {
    T   items[N];
    int count = 0;

    SmallArray() = default;
    SmallArray(std::initializer_list<T> il) { assign(il); }
    SmallArray& operator=(std::initializer_list<T> il) { assign(il); return *this; }
    void assign(std::initializer_list<T> il) {
        count = 0;
        for (const T& v : il) { if (count < N) items[count++] = v; }
    }
    void push_back(const T& v) { if (count < N) items[count++] = v; }
    void clear()               { count = 0; }
    size_t size()  const       { return static_cast<size_t>(count); }
    bool   empty() const       { return count == 0; }
    T&       operator[](size_t i)       { return items[i]; }
    const T& operator[](size_t i) const { return items[i]; }
    T*       begin()       { return items; }
    T*       end()         { return items + count; }
    const T* begin() const { return items; }
    const T* end()   const { return items + count; }
};

struct Face {
    // Indices into the vertex array (which vertices form this face)
    // Order matters for winding (counter-clockwise when viewed from outside)
    SmallArray<int, 4> vertex_indices;  // 3 for a triangle, 4 for a quad

    // UV coordinates for each vertex (0-1 range)
    // These define the 2D parameterization of the surface
    SmallArray<UV, 4> uvs;  // One UV per vertex

    // Normal vector for the face (perpendicular to surface)
    Vec3 normal;

    // Constructor for a quad face
    Face() {}
    Face(const std::vector<int>& indices, const Vec3& norm)
        : normal(norm) {
        for (int i : indices) vertex_indices.push_back(i);
        // Default UVs for a quad: standard texture mapping
        uvs = {UV(0,0), UV(1,0), UV(1,1), UV(0,1)};
    }
    
    // Compute normal from vertices (if not provided)
    void compute_normal(const std::vector<Vec3>& vertices);
    
    // Get center point of face
    Vec3 get_center(const std::vector<Vec3>& vertices) const;
};

// =============================================================================
// Transform - Position and orientation in world space
// =============================================================================
// Defines how to transform vertices from local space to world space.
// This is the "pose" of an object - where it is and how it's rotated.
//
// Coordinate spaces explained:
// - LOCAL SPACE: Object at origin, unrotated (like a blueprint)
// - WORLD SPACE: Object positioned and rotated in the scene
// - Transform converts: LOCAL -> WORLD
//
// Rotation order matters! We use X->Y->Z (pitch->yaw->roll) convention.
struct Transform {
    Vec3 position;      // Where the particle center is in world space
    float rotation_x;   // Rotation around X axis (pitch, in radians)
    float rotation_y;   // Rotation around Y axis (yaw, in radians)  
    float rotation_z;   // Rotation around Z axis (roll, in radians)
    
    // Constructors
    Transform();
    Transform(const Vec3& pos, float rx, float ry, float rz);
    
    // Transform a point from local space to world space
    // Steps: 1) Apply rotations, 2) Add position
    Vec3 transform_point(const Vec3& local_point) const;
    
    // Transform a direction (like a normal) - rotation only, no translation
    // Directions don't have position, only orientation
    Vec3 transform_direction(const Vec3& local_dir) const;
};

// =============================================================================
// GeometryV2 - Base class for explicit geometry
// =============================================================================
// Stores vertices and faces that define a 3D shape.
// All vertices are in LOCAL SPACE (relative to origin).
// Use Transform to place the geometry in world space.
//
// Why explicit vertices instead of generating from normals?
// 1. No ambiguity - we know EXACTLY where each corner is
// 2. Rotation is simple - just transform the vertices
// 3. UV mapping is clear - each vertex has explicit UV coords
// 4. Extensible - any shape works (pyramids, hexagons, etc.)
class GeometryV2 {
protected:
    std::vector<Vec3> local_vertices;  // All vertices in local space
    std::vector<Face> faces;           // All faces (surfaces)
    
public:
    // Virtual destructor for inheritance
    virtual ~GeometryV2() = default;
    
    // Get vertices and faces (read-only access)
    const std::vector<Vec3>& get_local_vertices() const { return local_vertices; }
    const std::vector<Face>& get_faces() const { return faces; }
    
    // Transform all vertices from local to world space
    std::vector<Vec3> get_world_vertices(const Transform& transform) const;

    // Same, into a caller-owned buffer. The by-value form heap-allocates on
    // every call, and the emitters call it once per particle per frame.
    void get_world_vertices_into(const Transform& transform,
                                 std::vector<Vec3>& out) const;

    // Transform faces (mainly updates normals to world space).
    // COPIES every face. Prefer get_faces() + transform_direction on the one
    // normal you need; this exists for callers that genuinely want the copy.
    std::vector<Face> get_world_faces(const Transform& transform) const;
    
    // Get the world-space vertices of a specific face
    std::vector<Vec3> get_face_vertices_world(int face_index, const Transform& transform) const;
};

// =============================================================================
// CubeGeometryV2 - A cube with explicit vertices
// =============================================================================
// Defines a cube centered at origin with given size.
// Has 8 vertices (corners) and 6 faces (sides).
//
// Vertex numbering (looking from +Z):
//     4 ---- 7
//    /|     /|
//   5 ---- 6 |
//   | 0 ---| 3
//   |/     |/
//   1 ---- 2
//
// This is systematic: binary XYZ where 0=negative, 1=positive
// Vertex 0 = (0,0,0) binary = (-X,-Y,-Z)
// Vertex 6 = (1,1,1) binary = (+X,+Y,+Z)
class CubeGeometryV2 : public GeometryV2 {
public:
    // Create a cube with given size (edge length)
    explicit CubeGeometryV2(float size);
};

// =============================================================================
// Bridge Functions - Convert V2 to Legacy Systems
// =============================================================================
// These functions help us transition from the old Surface-based system
// to the new vertex-based system. Eventually these will be removed.

// Convert our clean geometry to the old Surface format
std::vector<Surface> geometry_to_surfaces(const GeometryV2& geometry, 
                                         const Transform& transform);

// Extract raw vertex data for systems that need it
void extract_face_vertices(const GeometryV2& geometry,
                          int face_index,
                          const Transform& transform,
                          float vertices_out[][3],
                          int max_vertices);

} // namespace ParticleGeometryV2

#endif // PARTICLE_GEOMETRY_V2_H