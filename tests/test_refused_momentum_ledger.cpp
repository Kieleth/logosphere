// =============================================================================
// THE REFUSAL LEDGER IS COMPLETE, OR IT IS A LIE
// =============================================================================
// When a body cannot take momentum — an external writer owns it — the
// solver stops the striker anyway and the share the braced body should
// have taken goes somewhere. Since 2026-08-14 it is BOOKED
// (PhysicsSystem::record_refused_impulse) so the body's owner can decide
// what a shove means. That ledger is about to be consumed by policy
// (knockback, stagger, ragdoll), which makes its COMPLETENESS the whole
// question: a drain that receives a fraction of the truth is worse than
// no drain at all, because the fraction looks like an answer.
//
// WHAT THIS CAUGHT (F1 RCA, 2026-08-14): the ledger booked 2.5% of the
// truth. A KINEMATIC target refused 1357.8 kg*m/s and the book held
// 33.6. Two doors spent momentum outside the booking loop — the warm
// start (physics_system_v4.cpp, cached impulses applied before the
// iterations) and the entire friction block, which booked nothing at
// all. Both are fixed; this test is what keeps them fixed.
//
// THE MEASUREMENT. One striker, one braced target, airborne so no floor
// row can muddy the accounting. The striker's momentum change IS the
// momentum the contact moved; the target takes none of it. So the book
// must hold what the striker lost, to within the tolerance a converging
// solver leaves. No expected value is invented anywhere: the striker's
// own delta is the truth the ledger is checked against.
//
// Run: ./build/test_refused_momentum_ledger
// =============================================================================

#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) failures++;
}

}  // namespace

int main() {
    printf("\n=== THE REFUSAL LEDGER: complete, or a lie ===\n");

    ParticleSystem ps;
    PhysicsSystem physics;
    if (!physics.initialize(ps)) { printf("  [FAIL] init\n"); return 1; }

    auto spawn = [&](float x, float z, float size, Materials::Type mat,
                     float vx) {
        Particle p{};
        p.x = x; p.y = 0.0f; p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = p.height = p.thickness = size;
        p.size = size;
        p.vx = vx;
        p.SetMaterial(mat);
        int id = ps.queue_particle_addition(p);
        ps.flush_pending_particles();
        return id;
    };

    // Airborne, well clear of the turtle: the only contact in this world
    // is the one under test.
    const int striker = spawn(-1.2f, 6.0f, 0.4f, Materials::Type::STONE, 9.0f);
    const int target  = spawn( 0.0f, 6.0f, 0.6f, Materials::Type::STONE, 0.0f);
    {
        auto v = ps.lock_particles_for_write();
        v[target].solver_mode = ParticleSolverMode::KINEMATIC;   // braced
        v[target].owner = ParticleOwner::DYNAMICS;
    }

    float m_striker = 0.0f, vx0 = 0.0f;
    {
        auto v = ps.lock_particles_for_read();
        m_striker = v[striker].GetMass();
        vx0 = v[striker].vx;
    }

    float booked_x = 0.0f, booked_y = 0.0f, booked_z = 0.0f;
    for (int f = 0; f < 40; ++f) {
        ps.update_bvh();
        physics.update(1.0 / 60.0);
        float jx, jy, jz;
        const size_t n = ps.lock_particles_for_read().size();
        for (size_t pid = 0; pid < n; ++pid)
            if (physics.take_refused_impulse(pid, jx, jy, jz)) {
                booked_x += jx; booked_y += jy; booked_z += jz;
            }
    }

    float vx1 = 0.0f, target_vx = 0.0f;
    {
        auto v = ps.lock_particles_for_read();
        vx1 = v[striker].vx;
        target_vx = v[target].vx;
    }

    const float lost = m_striker * (vx0 - vx1);   // what the contact moved
    const float ratio = (lost > 0.01f) ? (booked_x / lost) : 0.0f;

    printf("  [measure] striker %.1f kg: vx %.2f -> %.2f, momentum lost "
           "%.1f kg*m/s\n", m_striker, vx0, vx1, lost);
    printf("  [measure] target vx %.4f (braced: must not move)\n", target_vx);
    printf("  [measure] ledger holds %.1f along the strike "
           "(%.1f%% of the truth)\n", booked_x, 100.0f * ratio);

    check(std::fabs(target_vx) < 1e-4f,
          "the braced body did not move (it really refused)");
    check(lost > 1.0f, "the strike really landed");
    check(ratio > 0.90f,
          "the ledger holds what the braced body refused (>90%)");

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "LEDGER COMPLETE" : "LEDGER INCOMPLETE", failures);
    return failures == 0 ? 0 : 1;
}
