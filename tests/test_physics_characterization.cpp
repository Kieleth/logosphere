// =============================================================================
// PHYSICS CHARACTERIZATION: the net that makes refactoring apply_all_forces safe
// =============================================================================
// WHY. `apply_all_forces` is 98% of physics cost and scales O(n^1.38) (kit
// study S18). It is itself only 30 lines: an O(n) gravity loop and one call to
// `solve_contacts_v3`, which is 1,981 lines holding broad phase, contact
// detection and the constraint solver as a single function. That is what has to
// be disentangled before anything can be fixed, and disentangling it must not
// change what it computes.
//
// The existing physics guards (logosphere-physics-guards) test locomotion and
// interactions: walk progress, stance foot, gluon lifecycle. None of them pin
// the SOLVER's arithmetic on a pile of contacting bodies, which is exactly the
// code a refactor here would disturb.
//
// LAWS (assert-protocol migration, 2026-08-21):
//   INV-27 deterministic-given-seeds. The A-vs-A control is INV-27 stated
//          exactly: the same scene stepped the same number of times in the
//          same build produces bit-identical state. INV-27's own record calls
//          this test out by name and calls reproducibility "the instrument
//          every other invariant is measured with".
//   INV-14 the gluon-coverage check: the bonded chain is held at its target
//          distance, so the pinned checksum actually covers the bond path.
//   hygiene the pinned baseline checksum is a REFACTOR NET, not a law: it says
//          the arithmetic did not move, and any deliberate physics change is
//          expected to move it.
//
// WHAT THIS DOES. Builds a fixed many-body pile, steps it a fixed number of
// frames, and reduces every particle's position and velocity to a checksum.
// Refactor, re-run, compare. It is a characterization test, not a correctness
// test: it does not claim the current numbers are RIGHT, only that they are
// what the engine does today. That is the property a refactor must preserve.
//
// DETERMINISM IS CHECKED FIRST, AND IS NOT ASSUMED. The scene is run TWICE in
// one process and the two runs must agree bit for bit. If physics is not
// reproducible, a golden checksum is noise and this test says so instead of
// handing back a number that drifts. This session spent a lot of effort on
// exactly that failure mode; an A-vs-A control comes first, always.
//
// ON EXACTNESS. The comparison is bit-exact by intent. A pure disentangle
// (lifting blocks into functions, no reordering of arithmetic) is bit-identical
// under a fixed compiler. If a step changes the last bits, that is information:
// the refactor reassociated something, and the report prints the magnitude so
// it can be judged rather than silently accepted.
//
//   ./build-release/logosphere-tests --test test_physics_characterization --no-head
//   LOGOSPHERE_PHYS_BASELINE=<hex>   fail unless the checksum matches this
//
// BASELINE HISTORY. Re-pin only for a DELIBERATE behaviour change, never to
// silence a surprise:
//   31f8a744adc8870b  first pin, narrower scene (no gluons, position+velocity only)
//   7b597182f47cffed  scene widened: gluon chain added, angular and rest state sampled
//   7120ff46938a7b0d  carried through the inertia-tensor and gluon-identity
//                     work; held bit-identical across the debug-rot deletion,
//                     the orphan-model deletion and both material commits
//   41d9cb760d6eed9d  the split-impulse POSITION pass got a SLOP tolerance.
//                     It corrected any non-zero error, so below ~1 um it was
//                     chasing float residue: 1955 lifts of 290 nm on a scene
//                     AT REST, each doing unpaid work against gravity. With
//                     the tolerance, test_settling_wiggle reads d_TOTAL = 0
//                     exactly — the engine's first energy-neutral scene.
//   db2d1f7ad9d245bf  position repair reaches sleeping bodies (only KINEMATIC
//                     is immovable to it). Should have been pinned with that
//                     commit and was not; recorded here on discovery.
//   5962bcfd9ad272b8  split impulse corrected: a speculative contact's
//                     NEGATIVE bias is an approach-speed LIMIT, not geometry,
//                     and stays in the velocity solve. Zeroing it made a
//                     speculative row a hard stop and left a dropped box
//                     resting on 80 mm of air (battery scenario 2, 0.680 m).
//   1b701dcda5d29d8b  SPLIT IMPULSE MADE UNIVERSAL. Position bias no longer
//                     enters the velocity solve for ANY row; a separate
//                     position pass repairs geometry with a pseudo-velocity
//                     that is discarded. Solver output legitimately moved and
//                     this pin is deliberate. Same run: gluon chain gaps
//                     tightened from 0.5498..0.5502 to 0.5499..0.5500.
//   16af12523829b082  effective-mass guard returns 0 for an immovable pair
//                     instead of 1 (kit study S22): the solver now converges on
//                     tiled floors, so its output legitimately moved
// =============================================================================

#include "../src/core/engine.h"
#include "logosphere/physics/physics_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cmath>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Chain particle ids, so the run can prove the gluon path actually executed
// rather than merely having been configured.
static std::vector<int> g_chain_ids;

struct State {
    // x,y,z, vx,vy,vz, rotation_z, omega_x,omega_y,omega_z, is_at_rest
    // per particle, in index order.
    //
    // ANGULAR AND REST STATE ARE SAMPLED DELIBERATELY. The first version of
    // this net took position and velocity only, which left rotation, angular
    // velocity and the at-rest flag free to change under a refactor without
    // the checksum noticing. A large part of solve_contacts_v3 writes exactly
    // those: the angular constraint rows, the gluon angular coupling, and
    // wake_particle_with_propagation.
    std::vector<float> v;
    uint64_t checksum = 0;
    float chain_min_gap = 0.0f, chain_max_gap = 0.0f;   // gluon sensitivity
};

// FNV-1a over the raw bits. Bit-exact by design: see the header note.
uint64_t hash_bits(const std::vector<float>& v) {
    uint64_t h = 1469598103934665603ull;
    for (float f : v) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        for (int b = 0; b < 4; ++b) {
            h ^= (bits >> (b * 8)) & 0xFF;
            h *= 1099511628211ull;
        }
    }
    return h;
}

// Deterministic by construction: no RNG, no time-dependent placement, fixed
// integer lattice perturbed by a fixed closed-form offset.
void build_pile(Engine& engine) {
    auto& ps = engine.get_particle_system();
    std::vector<int>& chain_ids = g_chain_ids;
    chain_ids.clear();

    const int FLOOR = 22;
    for (int r = 0; r < FLOOR; ++r)
        for (int c = 0; c < FLOOR; ++c) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)(c - FLOOR / 2); p.y = (float)(r - FLOOR / 2); p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }

    // Dynamic bodies stacked in a lattice so they fall, collide, and settle
    // into a pile. Contacts are the point: that is what apply_all_forces spends
    // its time on and what a refactor is most likely to disturb.
    for (int layer = 0; layer < 6; ++layer)
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 7; ++c) {
                Particle p = {};
                const bool sphere = ((layer + r + c) % 3) == 0;
                p.shape = sphere ? ParticleShape::SPHERE : ParticleShape::BOX;
                p.x = (c - 3) * 1.30f + 0.07f * (float)((layer * 7 + r) % 5);
                p.y = (r - 3) * 1.30f + 0.05f * (float)((layer * 5 + c) % 4);
                p.z = 2.0f + layer * 1.45f;
                p.width = p.height = p.thickness = 0.9f; p.size = 0.9f;
                p.r = 0.8f; p.g = 0.4f; p.b = 0.3f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                engine.add_particle(p);
            }

    // A GLUON-BONDED CHAIN, WITH ANGULAR COUPLING ON.
    //
    // Without this the net was blind to a fifth of solve_contacts_v3: the gluon
    // constraint build (lines 1062-1471) never executes when no gluon exists,
    // so every extraction there would have been verified by a checksum that
    // could not see it. The chain is deliberately long enough to exercise the
    // multi-hop distance propagation, and angular coupling is ON so the angular
    // constraint rows and the rotation/omega state they write are covered too.
    //
    // It also lands the chain ONTO the pile rather than in free space, so gluon
    // constraints and contact constraints are solved in the same substep and
    // fight each other, which is the interaction a naive extraction is most
    // likely to reorder.
    for (int i = 0; i < 8; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = -6.0f + i * 0.55f; p.y = 5.5f; p.z = 6.0f + i * 0.30f;
        p.width = p.height = p.thickness = 0.5f; p.size = 0.5f;
        p.r = 0.3f; p.g = 0.7f; p.b = 0.9f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        chain_ids.push_back(engine.add_particle(p));
    }
    ps.flush_pending_particles();

    auto& physics = engine.get_physics_system();
    for (size_t i = 1; i < chain_ids.size(); ++i) {
        auto g = std::make_unique<NailGluon>();
        g->offset_a = Vec3{0.0f, 0.0f, 0.0f};
        g->offset_b = Vec3{0.0f, 0.0f, 0.0f};
        g->target_distance = 0.55f;
        g->stiffness = 20000.0f;
        g->damping = 200.0f;
        g->breaking_force = 1e9f;          // never breaks: breaking is a
                                            // separate behaviour needing its
                                            // own test, not this net
        g->enable_angular_constraint = true;
        g->angular_stiffness = 800.0f;
        g->angular_damping = 40.0f;
        g->rotate_offsets = true;
        physics.add_gluon_between(static_cast<size_t>(chain_ids[i]),
                                  static_cast<size_t>(chain_ids[i - 1]),
                                  std::move(g));
    }
}

State run_once(int frames) {
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    State s;
    if (engine.initialize(cfg) != 0) return s;

    build_pile(engine);
    for (int f = 0; f < frames; ++f) engine.update(1.0 / 60.0);

    auto& ps = engine.get_particle_system();
    {
        auto v = ps.lock_particles_for_write();
        s.v.reserve(v.size() * 6);
        for (size_t i = 0; i < v.size(); ++i) {
            s.v.push_back(v[i].x);  s.v.push_back(v[i].y);  s.v.push_back(v[i].z);
            s.v.push_back(v[i].vx); s.v.push_back(v[i].vy); s.v.push_back(v[i].vz);
            s.v.push_back(v[i].rotation_z);
            s.v.push_back(v[i].omega_x); s.v.push_back(v[i].omega_y); s.v.push_back(v[i].omega_z);
            s.v.push_back(v[i].is_at_rest ? 1.0f : 0.0f);
        }
    }
    s.checksum = hash_bits(s.v);

    // SENSITIVITY: did the gluon path actually run? The chain is bonded at
    // target_distance 0.55. If the gluon constraints were never built, the
    // bodies would fall independently and the gaps would scatter. A bigger
    // scene is not by itself proof of bigger coverage.
    {
        auto v = ps.lock_particles_for_write();
        float lo = 1e9f, hi = 0.0f;
        for (size_t i = 1; i < g_chain_ids.size(); ++i) {
            const auto& a = v[g_chain_ids[i - 1]];
            const auto& b = v[g_chain_ids[i]];
            const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < lo) lo = d;
            if (d > hi) hi = d;
        }
        s.chain_min_gap = lo; s.chain_max_gap = hi;
    }

    engine.shutdown();
    return s;
}

}  // namespace

bool test_physics_characterization() {
    printf("\n=== PHYSICS CHARACTERIZATION (refactor net for apply_all_forces) ===\n");

    const int FRAMES = 150;   // long enough for the pile to fall, collide, settle

    const State a = run_once(FRAMES);
    const State b = run_once(FRAMES);

    if (a.v.empty() || b.v.empty()) { printf("  ERROR: engine init failed\n"); return false; }

    printf("  bodies sampled       %zu  (%zu floats)\n", a.v.size() / 11, a.v.size());
    printf("  run A checksum       %016llx\n", (unsigned long long)a.checksum);
    printf("  run B checksum       %016llx\n", (unsigned long long)b.checksum);

    bool ok = true;

    // (1) DETERMINISM FIRST. Without this a golden checksum is meaningless.
    if (a.v.size() != b.v.size()) {
        printf("\n  FAIL INV-27: the two runs produced different particle counts (%zu vs %zu).\n"
               "        Physics is not reproducible, so no checksum can guard a refactor.\n",
               a.v.size() / 11, b.v.size() / 11);
        return false;
    }
    if (a.checksum != b.checksum) {
        double maxd = 0.0; size_t ndiff = 0;
        for (size_t i = 0; i < a.v.size(); ++i) {
            const double d = std::fabs((double)a.v[i] - (double)b.v[i]);
            if (d > 0.0) ++ndiff;
            if (d > maxd) maxd = d;
        }
        printf("\n  FAIL INV-27: PHYSICS IS NOT DETERMINISTIC. Two identical runs disagree on\n"
               "        %zu of %zu values, max delta %.9g.\n"
               "        A characterization checksum cannot guard a refactor until this is\n"
               "        understood. Likely causes: unordered iteration over a hash container,\n"
               "        thread-count-dependent accumulation, or uninitialised state.\n",
               ndiff, a.v.size(), maxd);
        return false;
    }
    printf("\n  INV-27 determinism OK: two independent runs agree bit for bit.\n");

    // The gluon half of solve_contacts_v3 (lines 1062-1471, ~21% of it) only
    // executes when a gluon exists. Without this check a green checksum would
    // say nothing about that region, which is exactly the hole the first
    // version of this net had.
    printf("  gluon chain gaps     %.4f to %.4f m  (bonded at 0.550)\n",
           a.chain_min_gap, a.chain_max_gap);
    if (a.chain_min_gap < 0.30f || a.chain_max_gap > 0.90f) {
        printf("  FAIL INV-14: the bonded chain did not hold together, so the gluon\n"
               "        constraint path is NOT being exercised and every gluon-region\n"
               "        extraction would be verified by a checksum that cannot see it.\n");
        ok = false;
    } else {
        printf("  INV-14 gluon coverage OK: the chain is held at its target distance, so the\n"
               "                     gluon constraint build is live in this net.\n");
    }

    // (2) Optional pin. Supply the checksum from before a refactor and this
    // fails if the refactor changed what the solver computes.
    if (const char* want = std::getenv("LOGOSPHERE_PHYS_BASELINE")) {
        const uint64_t expect = std::strtoull(want, nullptr, 16);
        if (expect != a.checksum) {
            printf("  FAIL hygiene (refactor net, not a law): checksum %016llx does not match the pinned baseline %016llx.\n"
                   "        The refactor CHANGED WHAT PHYSICS COMPUTES. That is the thing\n"
                   "        this test exists to catch. Either it is not a pure refactor, or\n"
                   "        arithmetic got reassociated; both need a decision, not a re-pin.\n",
                   (unsigned long long)a.checksum, (unsigned long long)expect);
            ok = false;
        } else {
            printf("  hygiene baseline OK: solver output is unchanged (%016llx).\n",
                   (unsigned long long)a.checksum);
        }
    } else {
        printf("\n  No baseline pinned. Before refactoring, capture this run's value:\n"
               "    export LOGOSPHERE_PHYS_BASELINE=%016llx\n"
               "  then re-run after every step and expect it to hold.\n",
               (unsigned long long)a.checksum);
    }

    printf("\n  %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
