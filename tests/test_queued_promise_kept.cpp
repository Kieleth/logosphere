// =============================================================================
// A QUEUED BIRTH'S PROMISE (GEDANKEN-76, touches INV-37 / INV-11)
// =============================================================================
// queue_particle_addition hands back a PREDICTED index: live count plus the
// queue's length, judged by the creation door so a refusal never shifts it.
// What does shift it is a DIRECT add_particle before the flush: that birth
// takes the promised index for itself, and every promise handed out before it
// points at a stranger. Eden's five spirit lights orbited a trunk, a branch
// and three leaves at 45 m/s for this reason (night 2026-09-04, journal 17).
//
// Case A (the contract as written today): queue one body, birth one directly,
//   flush, read what stands at the promised index. RED by construction until
//   the contract is fixed one way or another (owner ruling, GEDANKEN-76).
// Case B (the promise honoured): queue, flush, then birth directly. GREEN -
//   the discipline Eden now follows at init.
//
// Run: ./build/test_queued_promise_kept
// =============================================================================
#include <cstdio>
#include <cmath>
#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"

static Particle stone_cube(float x, float y, float z) {
    Particle p = {};
    p.shape = ParticleShape::BOX;
    p.width = p.height = p.thickness = 0.1f;
    p.size = 0.1f;
    p.x = x; p.y = y; p.z = z;
    p.SetMaterial(Materials::Type::STONE);
    return p;
}

int main() {
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { std::printf("engine init failed\n"); return 1; }
    auto& ps = engine.get_particle_system();
    int red = 0;
    std::printf("\n=== A QUEUED BIRTH'S PROMISE (GEDANKEN-76) ===\n");

    // ---- Case A: a direct birth between the queue and its flush ----
    {
        const size_t breaks_before = ps.promise_breaks();
        const int promised = ps.queue_particle_addition(stone_cube(0.0f, 0.0f, 10.0f));
        const int direct   = engine.add_particle(stone_cube(5.0f, 0.0f, 10.0f));
        ps.flush_pending_particles();
        float x_at_promise = NAN;
        {
            auto v = ps.lock_particles_for_read();
            if (promised >= 0 && static_cast<size_t>(promised) < v.size())
                x_at_promise = v[static_cast<size_t>(promised)].x;
        }
        const bool kept = std::fabs(x_at_promise - 0.0f) < 1e-4f;
        std::printf("  [%s] [G-76 A] the queued body stands at its promised index P%d "
                    "(found x=%.1f, the queued body's x=0.0, the direct birth P%d's x=5.0)\n",
                    kept ? "PASS" : "FAIL", promised, x_at_promise, direct);
        red += !kept;
        const size_t breaks = ps.promise_breaks() - breaks_before;
        std::printf("  [%s] [G-76 A] direct births over outstanding promises: %zu "
                    "(a kept contract reads 0)\n", breaks == 0 ? "PASS" : "FAIL", breaks);
        red += (breaks != 0);
    }

    // ---- Case B: flushed before the direct birth (Eden's discipline) ----
    {
        const size_t breaks_before = ps.promise_breaks();
        const int promised = ps.queue_particle_addition(stone_cube(0.0f, 5.0f, 10.0f));
        ps.flush_pending_particles();
        const int direct = engine.add_particle(stone_cube(5.0f, 5.0f, 10.0f));
        float x_at_promise = NAN, y_at_promise = NAN;
        {
            auto v = ps.lock_particles_for_read();
            if (promised >= 0 && static_cast<size_t>(promised) < v.size()) {
                x_at_promise = v[static_cast<size_t>(promised)].x;
                y_at_promise = v[static_cast<size_t>(promised)].y;
            }
        }
        const bool kept = std::fabs(x_at_promise) < 1e-4f && std::fabs(y_at_promise - 5.0f) < 1e-4f;
        std::printf("  [%s] [G-76 B] flushed before the direct birth P%d, the promise P%d "
                    "names its body (found x=%.1f y=%.1f)\n",
                    kept ? "PASS" : "FAIL", direct, promised, x_at_promise, y_at_promise);
        red += !kept;
        const size_t breaks = ps.promise_breaks() - breaks_before;
        std::printf("  [%s] [G-76 B] direct births over outstanding promises: %zu\n",
                    breaks == 0 ? "PASS" : "FAIL", breaks);
        red += (breaks != 0);
    }
    std::printf("  %s (%d red)\n", red ? "[FAIL]" : "[PASS]", red);
    return red ? 1 : 0;
}
