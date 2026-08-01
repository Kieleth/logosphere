#include "sphere_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <unordered_map>

namespace ParticleGeometryV2 {

namespace {

// Icosahedron topology: 12 vertices on a sphere, 20 equilateral-triangle
// faces. Vertex coordinates use the golden ratio phi; each vertex sits at
// distance sqrt(1 + phi^2) from the origin, so we normalize to unit length
// before scaling per-axis.
//
// Reference: any standard computer-graphics treatment of the regular
// icosahedron (the 12 vertices are the permutations of (0, ±1, ±phi)).

constexpr float kPhi = 1.6180339887498949f;  // (1 + sqrt(5)) / 2

struct V3 { float x, y, z; };

const V3 kIcoVerticesRaw[12] = {
    {-1.0f,  kPhi,  0.0f},
    { 1.0f,  kPhi,  0.0f},
    {-1.0f, -kPhi,  0.0f},
    { 1.0f, -kPhi,  0.0f},
    { 0.0f, -1.0f,  kPhi},
    { 0.0f,  1.0f,  kPhi},
    { 0.0f, -1.0f, -kPhi},
    { 0.0f,  1.0f, -kPhi},
    { kPhi,  0.0f, -1.0f},
    { kPhi,  0.0f,  1.0f},
    {-kPhi,  0.0f, -1.0f},
    {-kPhi,  0.0f,  1.0f},
};

const int kIcoFaces[20][3] = {
    {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10},  {0, 10, 11},
    {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6},  {7, 1, 8},
    {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},   {3, 8, 9},
    {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},   {9, 8, 1},
};

// Build unit-length icosahedron vertices once.
std::vector<Vec3> make_unit_vertices() {
    std::vector<Vec3> v;
    v.reserve(12);
    for (const auto& r : kIcoVerticesRaw) {
        float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        v.emplace_back(r.x / len, r.y / len, r.z / len);
    }
    return v;
}

// Subdivide a triangle by splitting each edge's midpoint (normalized to
// the unit sphere) and replacing the triangle with 4 sub-triangles. A
// midpoint cache keyed on unordered vertex-pair indices prevents
// duplicate vertices at shared edges.
struct EdgeKey {
    int a, b;
    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
};
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const noexcept {
        return std::hash<long long>{}(
            (long long)k.a * 1000003LL + (long long)k.b);
    }
};

int midpoint_unit(int i, int j,
                  std::vector<V3>& unit_vertices,
                  std::unordered_map<EdgeKey, int, EdgeKeyHash>& cache) {
    EdgeKey key{ std::min(i, j), std::max(i, j) };
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const auto& va = unit_vertices[i];
    const auto& vb = unit_vertices[j];
    V3 m{ (va.x + vb.x) * 0.5f, (va.y + vb.y) * 0.5f, (va.z + vb.z) * 0.5f };
    float len = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
    if (len > 1e-6f) { m.x /= len; m.y /= len; m.z /= len; }
    int idx = static_cast<int>(unit_vertices.size());
    unit_vertices.push_back(m);
    cache[key] = idx;
    return idx;
}

// The unit icosphere at a given subdivision level is the same mesh for every
// sphere in the world: only radius and transform differ. Subdividing it used
// to run per sphere, per call, twice a frame — two unordered_map builds and
// ~160 sqrt each time, to reproduce numbers that never change. Build it once
// per level and hand out a const reference.
struct UnitIcosphere {
    std::vector<V3>                 vertices;   // on the unit sphere
    std::vector<std::array<int, 3>> faces;
};
// Caching the built unit faces here was tried on 2026-07-30 and reverted:
// it trades 320 cross products and 320 sqrt for a 320-element 24 KB vector
// copy, which measured as a wash. What remains in this path is memory
// traffic, not arithmetic. See kit study S10.

const UnitIcosphere& unit_icosphere(int subdivisions) {
    // Levels 0..4 (20 .. 5120 triangles). Built on first use, then read-only,
    // which is what makes the bare statics safe under the render workers.
    static constexpr int kMaxLevel = 4;
    if (subdivisions < 0) subdivisions = 0;
    if (subdivisions > kMaxLevel) subdivisions = kMaxLevel;

    static std::array<UnitIcosphere, kMaxLevel + 1> cache = [] {
        std::array<UnitIcosphere, kMaxLevel + 1> built;
        for (int lvl_target = 0; lvl_target <= kMaxLevel; ++lvl_target) {
            UnitIcosphere& u = built[lvl_target];
            u.vertices.reserve(12);
            for (const auto& r : kIcoVerticesRaw) {
                float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
                u.vertices.push_back({ r.x / len, r.y / len, r.z / len });
            }
            u.faces.reserve(20);
            for (const auto& f : kIcoFaces) u.faces.push_back({ f[0], f[1], f[2] });

            for (int lvl = 0; lvl < lvl_target; ++lvl) {
                std::unordered_map<EdgeKey, int, EdgeKeyHash> mid;
                std::vector<std::array<int, 3>> next;
                next.reserve(u.faces.size() * 4);
                for (const auto& f : u.faces) {
                    int m01 = midpoint_unit(f[0], f[1], u.vertices, mid);
                    int m12 = midpoint_unit(f[1], f[2], u.vertices, mid);
                    int m20 = midpoint_unit(f[2], f[0], u.vertices, mid);
                    next.push_back({ f[0], m01, m20 });
                    next.push_back({ f[1], m12, m01 });
                    next.push_back({ f[2], m20, m12 });
                    next.push_back({ m01, m12, m20 });
                }
                u.faces.swap(next);
            }
        }
        return built;
    }();
    return cache[subdivisions];
}

// Populate a GeometryV2-shaped structure with a subdivided icosphere
// scaled per-axis. subdivisions = 0 → 20 tris, each level quadruples.
void populate_icosphere(std::vector<Vec3>& out_vertices,
                        std::vector<Face>& out_faces,
                        float sx, float sy, float sz,
                        int subdivisions = 0) {
    const UnitIcosphere& unit = unit_icosphere(subdivisions);
    const std::vector<V3>& unit_v = unit.vertices;
    const std::vector<std::array<int, 3>>& faces_idx = unit.faces;

    // Scale unit vertices per-axis to build the final mesh.
    out_vertices.clear();
    out_vertices.reserve(unit_v.size());
    for (const auto& u : unit_v) {
        out_vertices.emplace_back(u.x * sx, u.y * sy, u.z * sz);
    }

    out_faces.clear();
    out_faces.reserve(faces_idx.size());
    for (const auto& f : faces_idx) {
        Face tri;
        tri.vertex_indices = { f[0], f[1], f[2] };
        tri.uvs = { UV(0.0f, 0.0f), UV(1.0f, 0.0f), UV(0.5f, 1.0f) };
        const auto& a = out_vertices[f[0]];
        const auto& b = out_vertices[f[1]];
        const auto& c = out_vertices[f[2]];
        Vec3 ab(b.x - a.x, b.y - a.y, b.z - a.z);
        Vec3 ac(c.x - a.x, c.y - a.y, c.z - a.z);
        Vec3 n(ab.y * ac.z - ab.z * ac.y,
               ab.z * ac.x - ab.x * ac.z,
               ab.x * ac.y - ab.y * ac.x);
        float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-6f) { n.x /= len; n.y /= len; n.z /= len; }
        tri.normal = n;
        out_faces.push_back(tri);
    }
}

// Shared triangle emitter — the bridge function in particle_geometry_v2.cpp
// is written against GeometryV2; these helpers keep our ctor lean and the
// implementation mirrors FlatParticleGeometry's to_surfaces shape.
Surface make_triangle_surface(const Vec3& v0, const Vec3& v1, const Vec3& v2,
                              const Vec3& normal) {
    Surface s;
    s.vertex_count = 3;
    s.vertices[0][0] = v0.x; s.vertices[0][1] = v0.y; s.vertices[0][2] = v0.z;
    s.vertices[1][0] = v1.x; s.vertices[1][1] = v1.y; s.vertices[1][2] = v1.z;
    s.vertices[2][0] = v2.x; s.vertices[2][1] = v2.y; s.vertices[2][2] = v2.z;
    s.x = (v0.x + v1.x + v2.x) / 3.0f;
    s.y = (v0.y + v1.y + v2.y) / 3.0f;
    s.z = (v0.z + v1.z + v2.z) / 3.0f;
    s.nx = normal.x; s.ny = normal.y; s.nz = normal.z;
    // No area: the field is write-only on Surface (see
    // FlatParticleGeometry::create_triangle_surface for the full note). This
    // used to cost a cross product and a sqrt per surface, and an icosphere at
    // subdivision 2 is 320 surfaces per sphere.
    return s;
}

// Appending variant: the caller owns the buffer, so a hot loop can reuse
// one allocation across every particle instead of heap-allocating per
// particle per frame. See the performance research notes study S7:
// render cost tracks SURFACES, and this path runs once per surface.
void emit_surfaces_into(const GeometryV2& g, const Transform& t,
                        std::vector<Surface>& out) {
    // get_world_faces() used to be called here. Its whole body is
    // `std::vector<Face> world_faces = faces;` followed by rotating each
    // normal — a full copy of every face (320 of them for an icosphere) to
    // change one Vec3 apiece. Read the local faces and rotate the normal in
    // place instead: same values, no copy, no allocation.
    thread_local std::vector<Vec3> world_verts;
    g.get_world_vertices_into(t, world_verts);
    const auto& local_faces = g.get_faces();
    out.reserve(out.size() + local_faces.size());
    for (const auto& f : local_faces) {
        if (f.vertex_indices.size() < 3) continue;
        const auto& a = world_verts[f.vertex_indices[0]];
        const auto& b = world_verts[f.vertex_indices[1]];
        const auto& c = world_verts[f.vertex_indices[2]];
        out.push_back(make_triangle_surface(
            a, b, c, t.transform_direction(f.normal).normalized()));
    }
}

std::vector<Surface> emit_surfaces(const GeometryV2& g, const Transform& t) {
    std::vector<Surface> surfaces;
    emit_surfaces_into(g, t, surfaces);
    return surfaces;
}

void emit_shadow_vertices(const GeometryV2& g, const Transform& t,
                          std::vector<float>& out) {
    // Shadow triangles are positions only, so the face normals this used to
    // copy 320 of were never even read. Same fix as emit_surfaces_into.
    thread_local std::vector<Vec3> world_verts;
    g.get_world_vertices_into(t, world_verts);
    const auto& local_faces = g.get_faces();
    out.reserve(out.size() + local_faces.size() * 9);
    for (const auto& f : local_faces) {
        if (f.vertex_indices.size() < 3) continue;
        const auto& a = world_verts[f.vertex_indices[0]];
        const auto& b = world_verts[f.vertex_indices[1]];
        const auto& c = world_verts[f.vertex_indices[2]];
        out.push_back(a.x); out.push_back(a.y); out.push_back(a.z);
        out.push_back(b.x); out.push_back(b.y); out.push_back(b.z);
        out.push_back(c.x); out.push_back(c.y); out.push_back(c.z);
    }
}

} // namespace

// ============================================================================
// SphereGeometry
// ============================================================================

SphereGeometry::SphereGeometry(float radius, int subdivisions) {
    populate_icosphere(local_vertices, faces, radius, radius, radius, subdivisions);
}

std::vector<Surface> SphereGeometry::to_surfaces(const Transform& t) const {
    return emit_surfaces(*this, t);
}

void SphereGeometry::to_surfaces_into(const Transform& t, std::vector<Surface>& out) const {
    emit_surfaces_into(*this, t, out);
}

void SphereGeometry::to_shadow_vertices(const Transform& t,
                                        std::vector<float>& out) const {
    emit_shadow_vertices(*this, t, out);
}

// ============================================================================
// EllipsoidGeometry
// ============================================================================

EllipsoidGeometry::EllipsoidGeometry(float hw, float hh, float ht, int subdivisions) {
    populate_icosphere(local_vertices, faces, hw, hh, ht, subdivisions);
}

std::vector<Surface> EllipsoidGeometry::to_surfaces(const Transform& t) const {
    return emit_surfaces(*this, t);
}

void EllipsoidGeometry::to_surfaces_into(const Transform& t, std::vector<Surface>& out) const {
    emit_surfaces_into(*this, t, out);
}

void EllipsoidGeometry::to_shadow_vertices(const Transform& t,
                                           std::vector<float>& out) const {
    emit_shadow_vertices(*this, t, out);
}

} // namespace ParticleGeometryV2
