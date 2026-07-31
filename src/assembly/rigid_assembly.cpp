#include "logosphere/assembly/rigid_assembly.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>

#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"
#include "particle.h"

namespace logosphere::assembly {

namespace {

// Build one Particle from a BodyPart + assembly pose. Both the
// assembly's position math (see rigid_assembly.h) and the engine's
// Transform for particle.rotation_z rotate CLOCKWISE around world Z —
// the compass convention used by facing_angle and the humanoid yaw
// cascade. Because both sides agree, no sign flip is needed here.
Particle build_particle(const RigidAssembly& a, const BodyPart& bp) {
    auto s = build_part_snapshot(a, bp);
    Particle p = {};
    p.shape       = s.shape;
    p.x = s.x; p.y = s.y; p.z = s.z;
    p.rotation_z  = s.rotation_z;
    p.width       = s.width;
    p.height      = s.height;
    p.thickness   = s.thickness;
    p.size        = s.size;
    p.r = s.r; p.g = s.g; p.b = s.b; p.a = s.a;
    p.is_light_source   = s.is_light_source;
    p.emission_strength = s.emission_strength;
    p.emission_radius   = s.emission_radius;
    p.is_dynamic        = s.is_dynamic;
    p.SetMaterial(bp.material_type);
    p.owner      = ParticleOwner::STATIC;
    p.is_at_rest = true;
    return p;
}

float wrap_angle(float a) {
    const float two_pi = 6.283185307179586f;
    a = std::fmod(a, two_pi);
    if (a >  3.14159265f) a -= two_pi;
    if (a < -3.14159265f) a += two_pi;
    return a;
}

}  // namespace

void sync_rigid_assembly(kg::KGModule& kg, ParticleSystem& ps,
                         const RigidAssembly& a) {
    if (a.entity == kg::INVALID_ENTITY) return;

    // Delete the previous frame's particles AND prune the per-entity
    // KG-particle list. If we only call delete_particles_immediate
    // the entity leaks N particle IDs per frame (see earlier bike
    // viewer diagnostics).
    auto kg_particles = kg.getEntityKGParticles(a.entity);
    std::vector<int> idxs;
    for (auto kgid : kg_particles) {
        int idx = static_cast<int>(kg.getRenderIndex(kgid));
        if (idx >= 0) idxs.push_back(idx);
    }
    if (!idxs.empty()) ps.delete_particles_immediate(idxs);
    for (auto kgid : kg_particles) kg.destroyKGParticle(kgid);

    for (const auto& bp : a.parts) {
        Particle p = build_particle(a, bp);
        ps.add_particle_to_entity(p, &kg, a.entity);
    }
}

void sync_rigid_assemblies(kg::KGModule& kg, ParticleSystem& ps,
                           const std::vector<RigidAssembly>& assemblies) {
    for (const auto& a : assemblies) sync_rigid_assembly(kg, ps, a);
}

std::vector<PartInspection>
inspect_assembly(const RigidAssembly& a,
                 const kg::KGModule& kg, const ParticleSystem& ps) {
    std::vector<PartInspection> out;
    if (a.entity == kg::INVALID_ENTITY) return out;

    // We match by index: the i-th kg_particle corresponds to the i-th
    // BodyPart because sync_rigid_assembly emits parts in order.
    auto kg_particles = kg.getEntityKGParticles(a.entity);
    const size_t n = std::min(a.parts.size(), kg_particles.size());
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const BodyPart& bp = a.parts[i];
        int idx = static_cast<int>(kg.getRenderIndex(kg_particles[i]));

        PartInspection row{};
        row.part_index = i;
        row.name       = bp.name;
        row.shape      = bp.shape;
        row.local_x    = bp.local_x;
        row.local_y    = bp.local_y;
        row.local_z    = bp.local_z;
        row.local_yaw  = bp.local_yaw;

        part_world_position(a, bp, row.expected_world_x,
                             row.expected_world_y, row.expected_world_z);
        row.expected_world_yaw = part_world_yaw(a, bp);

        if (idx < 0) {
            row.actual_world_x = 0.0f;
            row.actual_world_y = 0.0f;
            row.actual_world_z = 0.0f;
            row.actual_world_yaw = 0.0f;
            row.coherent = false;
            row.max_position_error = std::numeric_limits<float>::infinity();
            row.yaw_error = std::numeric_limits<float>::infinity();
        } else {
            Particle live = ps.get_particle_copy(static_cast<size_t>(idx));
            row.actual_world_x = live.x;
            row.actual_world_y = live.y;
            row.actual_world_z = live.z;
            row.actual_world_yaw = live.rotation_z;
            float dx = std::fabs(row.actual_world_x - row.expected_world_x);
            float dy = std::fabs(row.actual_world_y - row.expected_world_y);
            float dz = std::fabs(row.actual_world_z - row.expected_world_z);
            row.max_position_error = std::max({dx, dy, dz});
            row.yaw_error = std::fabs(
                wrap_angle(row.actual_world_yaw - row.expected_world_yaw));
            const float kPosEps = 1e-4f;
            const float kYawEps = 1e-4f;
            row.coherent = (row.max_position_error < kPosEps &&
                            row.yaw_error < kYawEps);
        }
        out.push_back(row);
    }
    return out;
}

void dump_assembly(std::ostream& out,
                   const RigidAssembly& a,
                   const kg::KGModule& kg, const ParticleSystem& ps) {
    const float rad2deg = 57.29577951308232f;
    out << "[assembly] entity=" << a.entity
        << " world=(" << a.world_x << ", " << a.world_y << ", " << a.world_z
        << ") yaw=" << a.world_yaw
        << " rad (" << (a.world_yaw * rad2deg) << "°)"
        << " parts=" << a.parts.size() << std::endl;

    auto rows = inspect_assembly(a, kg, ps);
    for (const auto& r : rows) {
        out << "  [" << r.part_index << "] "
            << (r.name.empty() ? "(unnamed)" : r.name)
            << " shape=" << static_cast<int>(r.shape)
            << " local=(" << r.local_x << ", " << r.local_y << ", " << r.local_z
            << ") local_yaw=" << r.local_yaw
            << std::endl
            << "       expected_world=(" << r.expected_world_x << ", "
            << r.expected_world_y << ", " << r.expected_world_z
            << ") yaw=" << r.expected_world_yaw << std::endl
            << "       actual_world=("   << r.actual_world_x   << ", "
            << r.actual_world_y   << ", " << r.actual_world_z
            << ") yaw=" << r.actual_world_yaw
            << "  coherent=" << (r.coherent ? "YES" : "NO")
            << " pos_err=" << r.max_position_error
            << " yaw_err=" << r.yaw_error
            << std::endl;
    }
}

}  // namespace logosphere::assembly
