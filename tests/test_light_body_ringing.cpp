// ============================================================================
// LIGHT BODY RINGING — a bond between unequal masses must not amplify
// ============================================================================
// THE LAW. Hit a bonded pair once and it rings down. A constraint may store
// energy and return it, but it may not manufacture more than it was given. If
// a pair is stable at equal masses and unstable at unequal ones, the mass
// ratio is doing something the physics does not license.
//
// WHY THIS EXISTS. TEAR_DEBUG on the walk gate, 17 tears in one pass:
//
//   [TEAR] P274<->P275 strain=2.09x | a vel=(0,-26.05,..) m=0.00238
//                                   | b vel=(0,-29.10,..) m=0.03454
//   bodies at tear:  mean |vy| 6.26 m/s, max 29.10 m/s
//   mass ratio:      mean 7.7x, worst 22.8x
//
// Eva walks at 1.2 m/s and the blades reach 29 m/s with the sign flipping
// between samples. They are not dragged, they RING, and the ringing stretches
// the bond past its 2.0x tear ratio. A kinematic box swept through the same
// patch at 8 m/s tears nothing, because it does not excite this.
//
// THE REDUCTION, built from the measured numbers rather than invented: a
// rooted chain at grass scale — rest 0.06 m, light end 0.00238 kg — swept
// across mass ratios from 1x to 25x. One impulse in, then nobody touches it.
//
// TWO ASSERTIONS per ratio:
//   1  IT RINGS DOWN. Peak speed in the second half of the run is below the
//      peak in the first half. Energy returned, not manufactured.
//   2  IT SURVIVES. No bond tears. A pair that shakes itself apart with
//      nothing touching it has already failed assertion 1 catastrophically.
//
//   ./build-release/logosphere-tests --test test_light_body_ringing --no-head
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

struct Ring {
    float peak_early = 0.0f;   // worst speed, first half
    float peak_late  = 0.0f;   // worst speed, second half
    size_t bonds_before = 0, bonds_after = 0;
};

// Grass-scale geometry, taken from the tear log: rest 0.06 m, light end
// 0.00238 kg. Only the heavy end's mass changes across the sweep.
Ring run_ratio(float ratio) {
    Ring r;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) return r;

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    constexpr float REST = 0.06f;
    constexpr float LIGHT_KG = 0.00238f;

    // Size chosen so density x volume lands on the target mass; the solver
    // reads mass, and the shape only has to be plausible for a blade segment.
    auto make = [&](float z, float kg) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = z;
        p.width = 0.04f; p.height = 0.003f; p.thickness = REST;
        p.size = 0.04f;
        p.r = 0.3f; p.g = 0.7f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::WOOD_SOFT);
        const int id = engine.add_particle(p);
        ps.flush_pending_particles();
        auto v = ps.lock_particles_for_write();
        v[id].SetMass(kg);
        return id;
    };

    // Root, then light, then heavy: the shape a blade actually has, a thin
    // segment carrying a leaf many times its own mass.
    const int root  = make(0.10f, LIGHT_KG);
    const int light = make(0.10f + REST, LIGHT_KG);
    const int heavy = make(0.10f + 2.0f * REST, LIGHT_KG * ratio);
    {
        auto v = ps.lock_particles_for_write();
        v[root].solver_mode = ParticleSolverMode::KINEMATIC;
        v[root].owner = ParticleOwner::DYNAMICS;
        v[root].is_at_rest = true;
    }

    auto bond = [&](int a, int b) {
        auto g = std::make_unique<OrganicGluon>();
        g->offset_a = {0, 0, 0}; g->offset_b = {0, 0, 0};
        g->target_distance = REST;
        g->rotate_offsets = false;
        g->contact_area = 1e-4f;
        g->stiffness = 5000.0f;      // the organic recipe
        g->damping = 100.0f;
        g->enable_angular_constraint = true;
        g->angular_stiffness = 50.0f;
        g->angular_damping = 5.0f;
        physics.add_gluon_between(a, b, std::move(g));
    };
    bond(root, light);
    bond(light, heavy);

    for (int f = 0; f < 60; ++f) engine.update(1.0 / 60.0);   // settle
    r.bonds_before = physics.get_total_gluon_count();

    // ONE push, at the speed Eva's leg actually travels. Then hands off.
    {
        auto v = ps.lock_particles_for_write();
        v[heavy].vy = 1.2f;
        v[heavy].is_at_rest = false;
        v[light].is_at_rest = false;
    }

    // RING_FRAMES bounds the run so a diverging ratio cannot hang the suite.
    // RING_TRACE prints per-frame state: this is how a ratio that "does not
    // return" gets separated into diverging-and-slow versus genuinely stuck.
    const char* fr_env = std::getenv("RING_FRAMES");
    const int FRAMES = fr_env ? std::atoi(fr_env) : 600;
    static const bool trace = std::getenv("RING_TRACE") != nullptr;
    for (int f = 0; f < FRAMES; ++f) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        engine.update(1.0 / 60.0);
        const double frame_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0).count();
        if (trace) {
            auto v = ps.lock_particles_for_write();
            printf("    [RING r=%.1f f%03d] %7.2f ms | light v=(%.2f,%.2f,%.2f) "
                   "z=%.4f | heavy v=(%.2f,%.2f,%.2f) z=%.4f | dist=%.4f\n",
                   ratio, f, frame_ms,
                   v[light].vx, v[light].vy, v[light].vz, v[light].z,
                   v[heavy].vx, v[heavy].vy, v[heavy].vz, v[heavy].z,
                   std::sqrt(std::pow(v[heavy].x - v[light].x, 2) +
                             std::pow(v[heavy].y - v[light].y, 2) +
                             std::pow(v[heavy].z - v[light].z, 2)));
        }
        float worst = 0.0f;
        {
            auto v = ps.lock_particles_for_write();
            for (int id : {light, heavy}) {
                if ((size_t)id >= v.size()) continue;
                const Particle& p = v[id];
                worst = std::fmax(worst, std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz));
            }
        }
        if (f < FRAMES / 2) r.peak_early = std::fmax(r.peak_early, worst);
        else                r.peak_late  = std::fmax(r.peak_late,  worst);
    }
    r.bonds_after = physics.get_total_gluon_count();
    engine.shutdown();
    return r;
}

} // namespace

bool test_light_body_ringing() {
    printf("\n=== LIGHT BODY RINGING: a bond must not amplify (issue #47) ===\n\n");
    printf("  grass scale: rest 0.060 m, light end 0.00238 kg, one 1.2 m/s push\n\n");
    printf("  %-8s %12s %12s %10s %s\n",
           "ratio", "peak early", "peak late", "bonds", "verdict");
    printf("  %s\n", "------------------------------------------------------------");

    int failures = 0;
    std::vector<float> ratios = {1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 22.8f, 25.0f};
    if (const char* only = std::getenv("RING_RATIO")) ratios = { (float)std::atof(only) };
    for (float ratio : ratios) {
        const Ring r = run_ratio(ratio);
        const bool tore = r.bonds_after < r.bonds_before;
        const bool rang_down = r.peak_late <= r.peak_early;
        const bool ok = rang_down && !tore;
        if (!ok) failures++;
        printf("  %6.1fx %12.3f %12.3f %6zu->%zu  %s\n",
               ratio, r.peak_early, r.peak_late, r.bonds_before, r.bonds_after,
               tore ? "*** TORE ***" : (rang_down ? "rings down" : "*** GROWING ***"));
    }

    printf("\n  %d ratio(s) failed\n", failures);
    const bool pass = (failures == 0);
    printf("\n  %s\n", pass ? "PASS"
        : "FAIL (a bond between unequal masses is manufacturing energy)");
    return pass;
}
