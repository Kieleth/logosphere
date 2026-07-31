// =============================================================================
// DEFERRED DELETION INTEGRITY — queued deletions survive swap-and-pop
// =============================================================================
// pending_deletions_ stores raw particle indices stamped at queue time,
// but the array is compacted by swap-and-pop (last index moves into the
// removed slot). Any swap between queue time and flush time makes a
// queued index point at a DIFFERENT particle: the flush then deletes the
// wrong particle (silent), double-deletes, or fires out of range.
//
// Found 2026-07-13 in the Eden RCA: walking Eva churns foot-plant pin
// anchors while chunk streaming deletes tiles; stale queue entries
// deleted live floor tiles and lights (visible artifacts), killed a pin
// anchor twice ([PIN_LOST] x3 on anchor=3824), and fired 4 out-of-range
// errors ("Tried to remove invalid index 4196 (size=4123)").
//
// Contracts (all RED before the swap-safe queue fix):
//   a6a  an entry whose target got SWAPPED (tail moved into a removed
//        slot) is remapped and still deletes the intended particle.
//   a6b  an entry whose target was deleted through another path is
//        DROPPED — the particle now occupying that slot survives.
//   a6c  duplicate entries for one particle delete it exactly once.
//   a6d  chunk-scale batch: queued set + interleaved direct removals
//        leaves exactly the intended survivors.
//
// Identity is asserted by SIGNATURE (unique x position), never by index.
//
// Run: ./build/logosphere-tests --test test_deferred_deletion_integrity --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <cmath>
#include <set>
#include <vector>

namespace {

struct EngineBox {
    Engine engine;
    bool ok = false;
    EngineBox(const char* title) {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.window_title = title;
        cfg.enable_chat_window = false;
        ok = (engine.initialize(cfg) == 0);
    }
};

// Spawn `n` inert particles with signature x = base_sig + i. KINEMATIC +
// DYNAMICS-owned so no integrator touches them; the test never calls
// engine.update, so positions are stable identity tags.
std::vector<int> spawn_signed(Engine& engine, int n, int base_sig) {
    std::vector<int> ids;
    ids.reserve(n);
    for (int i = 0; i < n; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = static_cast<float>(base_sig + i);
        p.y = 0.0f;
        p.z = 5.0f;
        p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
        p.size = 0.3f;
        p.r = 0.5f; p.g = 0.5f; p.b = 0.5f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        int id = engine.add_particle(p);
        {
            auto view = engine.get_particle_system().lock_particles_for_write();
            view[id].solver_mode = ParticleSolverMode::KINEMATIC;
            view[id].owner = ParticleOwner::DYNAMICS;
        }
        ids.push_back(id);
    }
    return ids;
}

// Collect the signatures present in [lo, hi).
std::multiset<int> collect_signatures(ParticleSystem& ps, int lo, int hi) {
    std::multiset<int> sigs;
    auto view = ps.lock_particles_for_read();
    for (const auto& p : view.get()) {
        int sig = static_cast<int>(std::lround(p.x));
        if (sig >= lo && sig < hi && std::fabs(p.z - 5.0f) < 0.5f) {
            sigs.insert(sig);
        }
    }
    return sigs;
}

bool check_signatures(const char* label, ParticleSystem& ps,
                      int lo, int hi, const std::multiset<int>& expected) {
    auto got = collect_signatures(ps, lo, hi);
    if (got == expected) {
        printf("  PASS: %s (%zu survivors, all intended)\n", label, got.size());
        return true;
    }
    printf("  FAIL: %s\n", label);
    for (int s : expected) {
        if (got.count(s) < expected.count(s)) {
            printf("        missing signature %d (wrongly deleted)\n", s);
        }
    }
    for (int s : got) {
        if (expected.count(s) < got.count(s)) {
            printf("        unexpected survivor %d (should be deleted)\n", s);
        }
    }
    return false;
}

} // namespace

bool test_deferred_deletion_integrity() {
    printf("\n=== Deferred Deletion Integrity (swap-safe queue) ===\n");

    bool all_ok = true;

    // ------------------------------------------------------------------
    // a6a — remap on swap: queued tail target survives a mid-array
    //       removal and still dies at flush.
    // ------------------------------------------------------------------
    {
        EngineBox box("deletion integrity a6a");
        if (!box.ok) { printf("  ERROR: engine init failed (a6a)\n"); return false; }
        auto& ps = box.engine.get_particle_system();

        auto ids = spawn_signed(box.engine, 10, 1000);   // sigs 1000..1009
        ps.queue_particle_deletion(static_cast<size_t>(ids[9]), 0);  // tail, sig 1009
        ps.remove_particle(static_cast<size_t>(ids[2]));             // sig 1002; tail swaps into its slot
        ps.flush_safe_deletions(1000);

        std::multiset<int> expected = {1000, 1001, 1003, 1004, 1005, 1006, 1007, 1008};
        all_ok &= check_signatures("a6a queued entry remapped across swap", ps, 1000, 1010, expected);
    }

    // ------------------------------------------------------------------
    // a6b — drop on delete: target removed via another path; the
    //       particle swapped into its old slot must survive the flush.
    // ------------------------------------------------------------------
    {
        EngineBox box("deletion integrity a6b");
        if (!box.ok) { printf("  ERROR: engine init failed (a6b)\n"); return false; }
        auto& ps = box.engine.get_particle_system();

        auto ids = spawn_signed(box.engine, 10, 2000);   // sigs 2000..2009
        ps.queue_particle_deletion(static_cast<size_t>(ids[5]), 0);  // sig 2005
        ps.remove_particle(static_cast<size_t>(ids[5]));             // deleted directly; sig 2009 swaps in
        ps.flush_safe_deletions(1000);

        std::multiset<int> expected = {2000, 2001, 2002, 2003, 2004, 2006, 2007, 2008, 2009};
        all_ok &= check_signatures("a6b stale entry dropped, swapped-in survivor lives", ps, 2000, 2010, expected);
    }

    // ------------------------------------------------------------------
    // a6c — duplicates: two entries for one particle, one death.
    // ------------------------------------------------------------------
    {
        EngineBox box("deletion integrity a6c");
        if (!box.ok) { printf("  ERROR: engine init failed (a6c)\n"); return false; }
        auto& ps = box.engine.get_particle_system();

        auto ids = spawn_signed(box.engine, 10, 3000);   // sigs 3000..3009
        ps.queue_particle_deletion(static_cast<size_t>(ids[7]), 0);  // sig 3007
        ps.queue_particle_deletion(static_cast<size_t>(ids[7]), 0);  // duplicate (pin re-destroy class)
        ps.flush_safe_deletions(1000);

        std::multiset<int> expected = {3000, 3001, 3002, 3003, 3004, 3005, 3006, 3008, 3009};
        all_ok &= check_signatures("a6c duplicate entries kill exactly once", ps, 3000, 3010, expected);
    }

    // ------------------------------------------------------------------
    // a6d — chunk-scale batch with interleaved removals (the
    //       "invalid index 4196 (size=4123)" class).
    // ------------------------------------------------------------------
    {
        EngineBox box("deletion integrity a6d");
        if (!box.ok) { printf("  ERROR: engine init failed (a6d)\n"); return false; }
        auto& ps = box.engine.get_particle_system();

        auto ids = spawn_signed(box.engine, 30, 4000);   // sigs 4000..4029
        for (int i = 25; i < 30; ++i) {                  // queue sigs 4025..4029
            ps.queue_particle_deletion(static_cast<size_t>(ids[i]), 0);
        }
        ps.remove_particle(static_cast<size_t>(ids[10]));  // sig 4010; queued tail swaps in
        ps.remove_particle(static_cast<size_t>(ids[11]));  // sig 4011; next queued tail swaps in
        ps.flush_safe_deletions(1000);

        std::multiset<int> expected;
        for (int s = 4000; s <= 4009; ++s) expected.insert(s);
        for (int s = 4012; s <= 4024; ++s) expected.insert(s);
        all_ok &= check_signatures("a6d batch integrity across interleaved removals", ps, 4000, 4030, expected);
    }

    printf("\n  %s\n", all_ok ? "[PASS]" : "[FAIL - deferred deletion queue corrupts across swaps]");
    return all_ok;
}
