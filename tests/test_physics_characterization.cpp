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
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct State {
    std::vector<float> v;      // x,y,z,vx,vy,vz per particle, in index order
    uint64_t checksum = 0;
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
    ps.flush_pending_particles();
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
        }
    }
    s.checksum = hash_bits(s.v);
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

    printf("  bodies sampled       %zu  (%zu floats)\n", a.v.size() / 6, a.v.size());
    printf("  run A checksum       %016llx\n", (unsigned long long)a.checksum);
    printf("  run B checksum       %016llx\n", (unsigned long long)b.checksum);

    bool ok = true;

    // (1) DETERMINISM FIRST. Without this a golden checksum is meaningless.
    if (a.v.size() != b.v.size()) {
        printf("\n  FAIL: the two runs produced different particle counts (%zu vs %zu).\n"
               "        Physics is not reproducible, so no checksum can guard a refactor.\n",
               a.v.size() / 6, b.v.size() / 6);
        return false;
    }
    if (a.checksum != b.checksum) {
        double maxd = 0.0; size_t ndiff = 0;
        for (size_t i = 0; i < a.v.size(); ++i) {
            const double d = std::fabs((double)a.v[i] - (double)b.v[i]);
            if (d > 0.0) ++ndiff;
            if (d > maxd) maxd = d;
        }
        printf("\n  FAIL: PHYSICS IS NOT DETERMINISTIC. Two identical runs disagree on\n"
               "        %zu of %zu values, max delta %.9g.\n"
               "        A characterization checksum cannot guard a refactor until this is\n"
               "        understood. Likely causes: unordered iteration over a hash container,\n"
               "        thread-count-dependent accumulation, or uninitialised state.\n",
               ndiff, a.v.size(), maxd);
        return false;
    }
    printf("\n  determinism OK: two independent runs agree bit for bit.\n");

    // (2) Optional pin. Supply the checksum from before a refactor and this
    // fails if the refactor changed what the solver computes.
    if (const char* want = std::getenv("LOGOSPHERE_PHYS_BASELINE")) {
        const uint64_t expect = std::strtoull(want, nullptr, 16);
        if (expect != a.checksum) {
            printf("  FAIL: checksum %016llx does not match the pinned baseline %016llx.\n"
                   "        The refactor CHANGED WHAT PHYSICS COMPUTES. That is the thing\n"
                   "        this test exists to catch. Either it is not a pure refactor, or\n"
                   "        arithmetic got reassociated; both need a decision, not a re-pin.\n",
                   (unsigned long long)a.checksum, (unsigned long long)expect);
            ok = false;
        } else {
            printf("  baseline OK: solver output is unchanged (%016llx).\n",
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
