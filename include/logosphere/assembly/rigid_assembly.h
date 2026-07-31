#ifndef LOGOSPHERE_ASSEMBLY_RIGID_ASSEMBLY_H
#define LOGOSPHERE_ASSEMBLY_RIGID_ASSEMBLY_H

// Rigid multi-particle assemblies.
//
// An entity that wants to render as N particles held in a fixed
// arrangement (a motorcycle body + two wheels, a drone frame + four
// rotors, a turret base + barrel) declares itself a RigidAssembly:
// one world pose (x, y, z, yaw) plus a list of BodyPart entries
// describing each particle's LOCAL-frame offset and local yaw.
//
// ============================================================================
// CONVENTION (matches the rest of the engine — see particle_core.h,
// particle_geometry_v2.cpp, particle_dynamics_system)
// ============================================================================
//   Local +Y = forward (facing direction at rest)
//   Local +X = starboard / right
//   Local +Z = up
//   world_yaw = 0  → assembly faces +Y (north)
//   world_yaw increases CLOCKWISE viewed from +Z (compass)
//       yaw =  0       → facing +Y (north)
//       yaw = +π/2     → facing +X (east)
//       yaw = +π       → facing -Y (south)
//       yaw = -π/2     → facing -X (west)
//
// The engine's Transform rotates rotation_z CW by the same rule. We
// pick the SAME convention here so position math and geometry rotation
// agree naturally — no sign-flip compensation. Picking the math-CCW
// convention instead (and compensating with a negate) used to cause
// the motorcycle body and wheels to rotate in opposite directions.
// See tests/test_rigid_assembly.cpp for the contract.
//
// v1 supports yaw-only rotation about world Z (the common case for
// ground vehicles, arena pieces, top-down entities). Pitch and roll
// come later as extra fields on the assembly — the composition rule
// will generalize to quaternions / matrices without changing the
// call site API.
//
// Headless-testable: the pure math in this header depends only on
// particle_types.h and materials.h. The sync / dump functions that
// talk to ParticleSystem and KGModule live in rigid_assembly.cpp.

#include <cmath>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "logosphere/kg/kg_types.h"
#include "materials.h"
#include "particle_types.h"

// Forward-declare so the header stays headless — the full definition
// is only needed by rigid_assembly.cpp.
class ParticleSystem;
namespace kg { class KGModule; }

namespace logosphere::assembly {

// A single particle in an assembly, described in the entity's local
// frame (assembly origin, +X = forward, +Z = up).
struct BodyPart {
    // Optional label — shows up in dumps so a multi-part assembly is
    // readable ("body", "front_wheel", "rear_wheel"). Not required for
    // correctness.
    std::string name;

    ParticleShape shape = ParticleShape::BOX;

    // Local-frame offset from the assembly's origin.
    float local_x = 0.0f;
    float local_y = 0.0f;
    float local_z = 0.0f;

    // Local yaw (radians around local Z) applied ON TOP of the
    // assembly's world yaw. Use 0 for parts that ride the assembly as
    // rigid children; non-zero when the part has its own orientation
    // (e.g., a turret barrel that rotates independently of the base).
    float local_yaw = 0.0f;

    // Dimensions. For BOX and ELLIPSOID we use (width, height, thickness)
    // along the local-frame axes. For SPHERE we use `size` as diameter.
    float width     = 1.0f;
    float height    = 1.0f;
    float thickness = 1.0f;
    float size      = 1.0f;

    // Appearance.
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    // Material + emission. is_light_source / emission_strength let an
    // assembly carry its own lighting (a drone with navigation lights,
    // a ship with portholes), avoiding the need for a separate light
    // entity.
    Materials::Type material_type = Materials::Type::STONE;
    bool  is_light_source   = false;
    float emission_strength = 0.0f;
    float emission_radius   = 0.0f;

    // Vision-memory exclusion (mirrors Particle::is_dynamic). Set
    // true on assemblies that move around — bikes, NPCs, vehicles
    // — so the renderer's Pass-4 vision cone doesn't leave a faint
    // trailing copy of them in the "what I just saw" memory grid.
    // Default false matches Particle's default: static parts persist
    // in memory.
    bool  is_dynamic = false;
};

struct RigidAssembly {
    kg::EntityID entity = kg::INVALID_ENTITY;

    // World pose.
    float world_x   = 0.0f;
    float world_y   = 0.0f;
    float world_z   = 0.0f;
    float world_yaw = 0.0f;   // radians around world Z

    // Parts, in render order. Order is stable across frames so the
    // inspector can assume "index i = part i".
    std::vector<BodyPart> parts;
};

// =============================================================================
// Pure-math helpers — inline so they can be used in headless tests
// without depending on ParticleSystem.
// =============================================================================

// Compute a part's world-space position from the assembly pose plus
// the part's local-frame offset. Rotation is CLOCKWISE around world Z
// (compass convention — matches facing_angle and the engine Transform
// rotation_z). At yaw=0, local +Y maps straight to world +Y (north).
// At yaw=+π/2, local +Y maps to world +X (east).
inline void part_world_position(const RigidAssembly& a, const BodyPart& p,
                                float& wx, float& wy, float& wz) {
    const float c = std::cos(a.world_yaw);
    const float s = std::sin(a.world_yaw);
    wx = a.world_x + c * p.local_x + s * p.local_y;
    wy = a.world_y - s * p.local_x + c * p.local_y;
    wz = a.world_z + p.local_z;
}

// Compose world + local yaw. Kept as a function (not a + in the call
// site) so pitch/roll can layer on without callers having to know.
inline float part_world_yaw(const RigidAssembly& a, const BodyPart& p) {
    return a.world_yaw + p.local_yaw;
}

// Plain snapshot of the particle-space fields that sync_rigid_assembly
// will write for a given (assembly, part) pair. This is the SAME math
// sync uses — exposing it as a pure function gives headless tests a
// way to verify the full particle output (position, yaw, extents,
// color) without needing a ParticleSystem.
//
// Kept as a separate struct (not Particle directly) so the header does
// not need to include the Particle definition — keeping assembly
// headless-safe.
struct PartParticleSnapshot {
    ParticleShape shape;
    float x, y, z;
    float rotation_z;        // yaw around world Z (v1)
    float width, height, thickness;
    float size;
    float r, g, b, a;
    bool  is_light_source;
    float emission_strength;
    float emission_radius;
    bool  is_dynamic;        // mirrors BodyPart::is_dynamic
};

inline PartParticleSnapshot
build_part_snapshot(const RigidAssembly& a, const BodyPart& bp) {
    PartParticleSnapshot s{};
    s.shape = bp.shape;
    part_world_position(a, bp, s.x, s.y, s.z);
    s.rotation_z = part_world_yaw(a, bp);
    s.width      = bp.width;
    s.height     = bp.height;
    s.thickness  = bp.thickness;
    s.size       = bp.size;
    s.r = bp.r; s.g = bp.g; s.b = bp.b; s.a = bp.a;
    s.is_light_source   = bp.is_light_source;
    s.emission_strength = bp.emission_strength;
    s.emission_radius   = bp.emission_radius;
    s.is_dynamic        = bp.is_dynamic;
    return s;
}

// =============================================================================
// Sync + introspection — implemented in rigid_assembly.cpp (full engine).
// =============================================================================

// Rebuild all particles for one assembly. Deletes the previous frame's
// particles, prunes stale KG-particle mappings on the entity, then
// adds fresh particles at the computed world poses.
//
// Caller is responsible for tracking the RigidAssembly itself (it is
// plain data; the engine owns no assembly registry yet). Typical
// usage: one RigidAssembly per game entity held by the game; update
// pose each frame, then call sync_rigid_assembly.
void sync_rigid_assembly(kg::KGModule& kg, ParticleSystem& ps,
                         const RigidAssembly& assembly);

// Vector form for bulk sync.
void sync_rigid_assemblies(kg::KGModule& kg, ParticleSystem& ps,
                           const std::vector<RigidAssembly>& assemblies);

// Introspection row produced by inspect_assembly. Tells you what the
// engine EXPECTS a part to be at (from the pose + local offset) and
// what the particle ACTUALLY is at (from the live ParticleSystem).
// `coherent` is true when expected ≈ actual within a small epsilon.
struct PartInspection {
    size_t   part_index;
    std::string name;
    ParticleShape shape;
    // From BodyPart
    float local_x, local_y, local_z;
    float local_yaw;
    // Computed from assembly pose
    float expected_world_x, expected_world_y, expected_world_z;
    float expected_world_yaw;
    // Observed on the live particle
    float actual_world_x, actual_world_y, actual_world_z;
    float actual_world_yaw;
    bool  coherent;
    float max_position_error;   // max(|expected - actual|) over xyz
    float yaw_error;             // |expected_yaw - actual_yaw| (wrapped)
};

std::vector<PartInspection>
inspect_assembly(const RigidAssembly& a,
                 const kg::KGModule& kg, const ParticleSystem& ps);

// Pretty-print everything useful about an assembly to `out`:
//   - entity id, world pose
//   - each part's name, shape, local offset, local yaw
//   - computed expected world pose per part
//   - observed actual world pose per part
//   - coherent ✓ / ✗ flag plus numeric error
void dump_assembly(std::ostream& out,
                   const RigidAssembly& a,
                   const kg::KGModule& kg, const ParticleSystem& ps);

}  // namespace logosphere::assembly

#endif  // LOGOSPHERE_ASSEMBLY_RIGID_ASSEMBLY_H
