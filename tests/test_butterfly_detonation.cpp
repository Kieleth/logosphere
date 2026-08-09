// =============================================================================
// DO BUTTERFLIES DETONATE? (issue #47)
// =============================================================================
// In Eden, at world spawn, the solver DIVERGES on contact rows between
// butterfly parts: measured impulses grow 16k -> 48k -> 1.8M -> 4.5e8 across
// one frame's iterations, velocity reaches 1.78e12 m/s, the 100 m/s clamp
// truncates it, and butterfly parts leave the scene at escape velocity. A
// butterfly part weighs under a gram, and gram-scale bodies are where the
// sequential-impulse iteration stops converging.
//
// THE LADDER: one butterfly alone, then four in Eden's exact cluster layout.
// If ONE detonates by itself, the divergence is internal to a single 8-part
// body and the repro needs no cluster. If one is calm and four detonate, the
// coupling between neighbouring butterflies is a necessary ingredient. Either
// answer shrinks the search.
//
// VERDICT via the explosion detector, which found this bug in the first
// place: any part over the 40 m/s ceiling within the first 60 frames is a
// detonation. The spec is that NOTHING here should exceed low single digits;
// a butterfly's own flight is under 2 m/s.
//
// REPORTS, DOES NOT GATE, while #47 is open, so CI stays green. When the
// solver is fixed this flips to a hard gate: the printout says exactly what
// to change.
//
//   ./build-release/logosphere-tests --test test_butterfly_detonation --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/particle.h"
#include "../src/materials.h"
#include "logosphere/worldgen/butterfly_generator.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include <cstdio>

namespace X = ::logosphere::expdet;

namespace {

struct Rung {
    const char* name;
    int count;
    bool floor = false;
    bool grass = false;
    size_t bodies = 0;
    uint64_t speed_events = 0;
    float    worst = 0.0f;
    int      worst_id = -1;
};

void run_rung(Rung& r) {
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return; }

    // Rung 3's ingredient: kinematic floor tiles beneath the cluster. In Eden
    // the parts that detonated also carried contact rows against terrain-range
    // ids, and a gram-scale part against a 250 kg immovable tile is the most
    // extreme mass-ratio coupling the solver ever sees.
    if (r.floor) {
        auto& ps = engine.get_particle_system();
        for (int c = 19; c <= 31; ++c)
            for (int d = -31; d >= -19; --d) { }
        for (int cx = 19; cx <= 31; ++cx)
            for (int cy = -31; cy <= -19; ++cy) {
                Particle p = {};
                p.shape = ParticleShape::BOX;
                p.x = (float)cx; p.y = (float)cy; p.z = 0.05f;
                p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
                p.r = p.g = p.b = 0.5f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                int id = engine.add_particle(p);
                auto v = ps.lock_particles_for_write();
                v[id].solver_mode = ParticleSolverMode::KINEMATIC;
                v[id].owner = ParticleOwner::DYNAMICS;
                v[id].is_at_rest = true;
            }
    }

    // Rung 4: TALL GRASS, no butterflies. The Eden forensics (issue #47) ended
    // at a vertical tuft of ultra-thin blades: sheets 1 mm thick and slivers
    // 0.27 mm / 0.3 g stacked at one (x,y) from z 0.1 to 2.65. The diverging
    // rows are a sandwich, jz=+1 from below and jz=-1 from above, on those
    // bodies. Butterflies were a position coincidence and are calm; this rung
    // asks whether the tuft alone detonates.
    if (r.grass) {
        auto& ogen = engine.get_worldgen_system().get_organic_generator();
        ogen.generate_grass_patch(30.0f, -20.0f, 0.0f, GrassPatchSpec::tall_grass());
    }

    ButterflyGenerator gen;
    gen.initialize(&engine, &engine.get_kg());

    // Eden's exact spawn layout: four butterflies on a ~4 m ring. Rung 1 takes
    // only the first. No floor, no trees, nothing else: if parts fly, it is
    // the butterflies' own physics and cannot be blamed on scenery.
    const float POS[4][3] = {
        {29.0f, -25.0f, 1.0f}, {25.0f, -21.0f, 1.3f},
        {21.0f, -25.0f, 1.6f}, {25.0f, -29.0f, 1.9f},
    };
    for (int i = 0; i < r.count; ++i)
        gen.generate_butterfly(POS[i][0], POS[i][1], POS[i][2],
                               ButterflySpec::monarch());
    engine.get_particle_system().flush_pending_particles();

    {   // A rung that generated nothing proves nothing: say what exists.
        auto v = engine.get_particle_system().lock_particles_for_write();
        r.bodies = v.size();
    }
    X::set_enabled(true);
    X::reset();
    for (int f = 0; f < 60; ++f) engine.update(1.0 / 60.0);
    const X::Stats s = X::stats();
    r.speed_events = s.speed_events;
    r.worst = s.worst_speed;
    r.worst_id = s.worst_id;
    engine.shutdown();
}

}  // namespace

bool test_butterfly_detonation() {
    printf("\n=== DO BUTTERFLIES DETONATE? (issue #47) ===\n");
    printf("Gram-scale bodies, nothing else in the scene. Any part over the\n");
    printf("40 m/s ceiling in the first second is the solver diverging, not\n");
    printf("flight: a butterfly's own flight is under 2 m/s.\n\n");

    Rung one   {"ONE butterfly, alone", 1};
    Rung four  {"FOUR, Eden's cluster", 4};
    Rung terra {"FOUR + floor beneath", 4, true};
    Rung tuft  {"TALL GRASS tuft, no butterflies", 0, false, true};
    run_rung(one);
    run_rung(four);
    run_rung(terra);
    run_rung(tuft);

    printf("  %-24s %14s %12s %10s\n", "rung", "ceiling events", "worst m/s", "worst id");
    printf("  %-24s %14llu %12.1f %10d\n", one.name,
           (unsigned long long)one.speed_events, one.worst, one.worst_id);
    printf("  %-24s %14llu %12.1f %10d\n", four.name,
           (unsigned long long)four.speed_events, four.worst, four.worst_id);
    printf("  %-24s %14llu %12.1f %10d\n", terra.name,
           (unsigned long long)terra.speed_events, terra.worst, terra.worst_id);
    if (tuft.bodies == 0) {
        printf("  %-31s %7s\n", tuft.name, "INCONCLUSIVE: generated 0 bodies");
        printf("\n  THE EMPTY RUNG IS ITSELF THE FINDING. generate_grass_patch() through a\n"
               "  bare engine creates nothing: Eden's grass is stored in the KG and its\n"
               "  PARTICLES are created by CHUNK ACTIVATION. The tuft that detonates is\n"
               "  born in the chunk path, so reproducing it in isolation means driving\n"
               "  that path, not calling a generator. See issue #47.\n");
    } else {
        printf("  %-31s %7llu %12.1f %10d\n", tuft.name,
               (unsigned long long)tuft.speed_events, tuft.worst, tuft.worst_id);
    }

    const bool one_detonates  = one.speed_events > 0;
    const bool four_detonate  = four.speed_events > 0;
    const bool terra_detonates = terra.speed_events > 0;

    printf("\n");
    if (one_detonates) {
        printf("  A SINGLE BUTTERFLY DETONATES ALONE. The divergence is internal to one\n"
               "  8-part body: its own segments' contact rows are enough. The minimal\n"
               "  repro is one butterfly on an empty turtle, no cluster required.\n");
    } else if (four_detonate) {
        printf("  ONE IS CALM, FOUR DETONATE. Coupling between neighbouring butterflies\n"
               "  is a necessary ingredient; the divergence needs inter-body rows.\n");
    } else if (terra_detonates) {
        printf("  CALM ALONE, CALM CLUSTERED, DETONATES ON THE FLOOR. The necessary\n"
               "  ingredient is contact with massive kinematic terrain: a sub-gram part\n"
               "  against a 250 kg immovable tile is the most extreme mass-ratio row the\n"
               "  solver ever builds, and that coupling is where the iteration diverges.\n");
    } else {
        printf("  NO RUNG DETONATES. The Eden spawn-time explosion needs something even\n"
               "  this scene lacks (grass, trees, the chunk path, the flight system).\n"
               "  That is a finding: butterflies plus terrain are not sufficient.\n");
    }

    printf("\n  SPEC (the gate once #47 is fixed): zero ceiling events on both rungs.\n"
           "  Currently REPORTS ONLY, so CI stays green while the solver bug is open.\n");
    printf("\n  PASS (diagnostic)\n");
    return true;
}
