// =============================================================================
// KNOCKBACK: a shove against a braced body is not a shove into the void
// =============================================================================
// A humanoid under animation is KINEMATIC while its clip drives it, which
// means the solver may not move it — inverse mass 0. Until 2026-08-14 the
// solver therefore computed the contact impulse, stopped the striker with
// it, and DROPPED the half the humanoid should have taken. A boulder could
// hit a person square in the chest and nothing whatsoever happened, which
// is why TransformationEffect::KNOCKBACK had no implementation to speak of
// and why test_humanoid_impact asserted a displacement that was
// structurally always zero.
//
// The law being tested: momentum a body's authority refuses is BOOKED, not
// destroyed (PhysicsSystem::record_refused_impulse), and the writer holding
// that authority drains it and decides what a push means. For a humanoid,
// the locomotion system is that writer, and the decision is: absorb it into
// the body's world position — a stagger.
//
// Three rungs:
//   R1  physics books the momentum a KINEMATIC body refuses
//   R2  the humanoid's writer drains it and the body actually moves
//   R3  direction is honoured: hit from the west, stagger east
//
// STATE: RED, and what it measured is more interesting than the rungs.
//
// A boulder thrown at this humanoid's chest at 8 m/s passes STRAIGHT
// THROUGH at a constant 7.99 m/s. Not deflected, not slowed. And the
// reason is not the KINEMATIC pin this file was written to test:
//
//   right after registration: chest solver_mode = DYNAMIC(0),
//   owner = DYNAMICS(1), mass = 15.625 kg, is_quat_driven = 1
//
// Only the HIPS are KINEMATIC. The torso and limbs are DYNAMIC — and
// inert anyway, because physics exempts quat-driven bodies from
// gravity and response (physics_system_v4.cpp:608 and the sibling
// reads at :2005, :2015, :3224, :3229, :3393, :3404, all gated on
// `is_quat_driven && owner`). Those are the seven owner-reads task #43
// is meant to remove.
//
// So "who may move this body" has THREE answers in this engine, not
// one: solver_mode, is_at_rest, and is_quat_driven+owner. The third is
// an unnamed pin — it takes a body out of physics entirely, with no
// release path and no mention in the KINEMATIC audit, because it does
// not use the KINEMATIC enum at all. It is why a person cannot be hit.
//
// This test stays red until that is resolved. Fixing it by exempting
// the exemption would be an if-statement edge fix; the mechanism owed
// is a single answer to "who owns this body's motion right now",
// which is the same law INV-31 and the KINEMATIC ruling are converging
// on from the other side.
//
// Run: ./build/test_humanoid_knockback
// =============================================================================

#include "core/particle_system.h"
#include "core/particle_tracer.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) failures++;
}

struct World {
    ParticleSystem ps;
    kg::OntologyRegistry registry;
    kg::KGModule kg{registry};
    PhysicsSystem physics;
    ParticleTracer tracer;
    ParticleDynamicsSystem dyn;
    logosphere::animation::HumanoidLocomotion humanoid;
    int hips_id = -1;

    bool build(float hips_z) {
        if (!physics.initialize(ps) || !dyn.initialize_headless(ps) ||
            !humanoid.initialize_headless(ps, physics, kg, dyn, tracer))
            return false;
        auto spawn = [&](float x, float y, float z, float size,
                         Materials::Type mat) {
            Particle p{};
            p.x = x; p.y = y; p.z = z;
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = size;
            p.size = size;
            p.SetMaterial(mat);
            int id = ps.queue_particle_addition(p);
            ps.flush_pending_particles();
            return id;
        };
        hips_id    = spawn(0.0f, 0.0f, hips_z, 0.2f, Materials::Type::FLESH);
        int abdomen = spawn(0.0f, 0.0f, hips_z + 0.15f, 0.2f, Materials::Type::FLESH);
        int chest   = spawn(0.0f, 0.0f, hips_z + 0.35f, 0.25f, Materials::Type::FLESH);
        int neck    = spawn(0.0f, 0.0f, hips_z + 0.50f, 0.1f, Materials::Type::FLESH);
        int head    = spawn(0.0f, 0.0f, hips_z + 0.62f, 0.2f, Materials::Type::FLESH);
        std::vector<int> torso = { hips_id, abdomen, chest, neck, head };
        auto limb = [&](float sx, float z0) {
            return std::vector<int>{
                spawn(sx, 0.0f, z0, 0.12f, Materials::Type::FLESH),
                spawn(sx, 0.0f, z0 + 0.35f, 0.12f, Materials::Type::FLESH),
                spawn(sx, 0.0f, z0 + 0.70f, 0.12f, Materials::Type::FLESH) };
        };
        std::vector<int> ll = limb(-0.12f, hips_z - 0.85f);
        std::vector<int> rl = limb( 0.12f, hips_z - 0.85f);
        std::vector<int> la = limb(-0.28f, hips_z - 0.10f);
        std::vector<int> ra = limb( 0.28f, hips_z - 0.10f);
        humanoid.register_humanoid_direct(hips_id, ll, rl, la, ra, torso,
                                          250.0f, 500.0f, kg::INVALID_ENTITY);
        return true;
    }

    // A boulder thrown at the chest from the given direction.
    int throw_boulder(float from_x, float from_y, float z,
                      float vx, float vy) {
        Particle b{};
        b.x = from_x; b.y = from_y; b.z = z;
        b.shape = ParticleShape::BOX;
        b.width = b.height = b.thickness = 0.4f;
        b.size = 0.4f;
        b.vx = vx; b.vy = vy;
        b.SetMaterial(Materials::Type::STONE);
        int id = ps.queue_particle_addition(b);
        ps.flush_pending_particles();
        return id;
    }
};

}  // namespace

int main() {
    printf("\n=== KNOCKBACK: the momentum a braced body refuses ===\n");

    // ---- R1: physics books what it cannot deliver ----------------------
    {
        World w;
        if (!w.build(1.0f)) { printf("  [FAIL] init\n"); return 1; }
        // Short flight, aimed high: over 1 m at 8 m/s the boulder drops
        // ~8 cm, so it must be launched above the chest to arrive AT it.
        // (First draft threw from 2 m at 6 m/s and the boulder sailed
        // under the ribs — 0.54 m of drop — hitting nothing.)
        {
            auto v = w.ps.lock_particles_for_read();
            printf("  [measure] right after registration: chest mode=%d "
                   "owner=%d | hips mode=%d owner=%d\n",
                   (int)v[w.hips_id + 2].solver_mode,
                   (int)v[w.hips_id + 2].owner,
                   (int)v[w.hips_id].solver_mode,
                   (int)v[w.hips_id].owner);
            printf("  [measure] chest mass=%.3f kg quat_driven=%d | "
                   "boulder-to-be mass check pending\n",
                   v[w.hips_id + 2].GetMass(),
                   (int)v[w.hips_id + 2].is_quat_driven);
        }
        const int boulder = w.throw_boulder(-1.0f, 0.0f, 1.44f, 8.0f, 0.0f);
        float jx = 0, jy = 0, jz = 0;
        bool booked = false;
        float boulder_vx0 = 8.0f, boulder_vx_min = 8.0f;
        for (int f = 0; f < 90 && !booked; ++f) {
            w.physics.update(1.0 / 60.0);
            {
                auto v = w.ps.lock_particles_for_read();
                if (v[boulder].vx < boulder_vx_min) boulder_vx_min = v[boulder].vx;
                if (getenv("KB_PROBE") && f >= 6 && f <= 14) {
                    const auto& ch = v[w.hips_id + 2];
                    printf("      [f%d] boulder x=%.3f vx=%.2f | chest "
                           "mode=%d x=%.4f vx=%.4f rest=%d\n",
                           f, v[boulder].x, v[boulder].vx,
                           (int)ch.solver_mode, ch.x, ch.vx,
                           (int)ch.is_at_rest);
                }
            }
            // Drain EVERY body in the world: which part the boulder
            // catches is the scene's business, not this rung's.
            const size_t n = w.ps.lock_particles_for_read().size();
            for (size_t pid = 0; pid < n; ++pid)
                if (w.physics.take_refused_impulse(pid, jx, jy, jz)) {
                    booked = true;
                    printf("  [measure] refused momentum booked on P%zu: "
                           "(%.2f, %.2f, %.2f) N*s\n", pid, jx, jy, jz);
                    break;
                }
        }
        printf("  [measure] boulder vx %.2f -> %.2f (it %s the humanoid)\n",
               boulder_vx0, boulder_vx_min,
               boulder_vx_min < boulder_vx0 - 0.5f ? "HIT" : "MISSED");
        check(booked, "R1: a strike on a KINEMATIC body is BOOKED, "
                      "not dropped");
        check(booked && std::fabs(jx) > 0.1f,
              "R1: and it points along the strike (+x)");
    }

    // ---- R2/R3: the writer drains it and the body moves ----------------
    // The locomotion system owns this body's position, so it is the one
    // that must answer the push. R2 asks whether it moved at all; R3 asks
    // whether it moved the RIGHT WAY.
    {
        World w;
        if (!w.build(1.0f)) { printf("  [FAIL] init\n"); return 1; }
        const float x0 = w.ps.lock_particles_for_read()[w.hips_id].x;
        w.throw_boulder(-1.0f, 0.0f, 1.44f, 8.0f, 0.0f);
        for (int f = 0; f < 120; ++f) {
            w.humanoid.update_pre_physics(1.0 / 60.0);
            w.physics.update(1.0 / 60.0);
            w.humanoid.update_post_physics(1.0 / 60.0);
        }
        const float x1 = w.ps.lock_particles_for_read()[w.hips_id].x;
        const float moved = x1 - x0;
        printf("  [measure] hips x: %.3f -> %.3f (moved %+.3f m)\n",
               x0, x1, moved);
        check(std::fabs(moved) > 0.02f,
              "R2: the struck humanoid is displaced by the hit");
        check(moved > 0.0f,
              "R3: struck from the west, it staggers EAST");
    }

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "KNOCKBACK OK" : "KNOCKBACK INCOMPLETE", failures);
    return failures == 0 ? 0 : 1;
}
