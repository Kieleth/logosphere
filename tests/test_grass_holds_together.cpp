// =============================================================================
// GRASS MUST BE BONDED, AND MUST NOT DETONATE (issue #47, fix 1)
// =============================================================================
// THE DESIGN, as the owner stated it: grass is joined by gluons. A blade of
// tall grass is 2-5 stacked particles, and stacked bodies that belong together
// are BONDED, the same way a tree's leaves are bonded to its branches.
//
// THE DEFECT THIS CAGES: OrganicGenerator stored bare particle records in the
// KG and never created a single gluon, so chunk activation materialised every
// blade as a tower of FREE ultra-thin plates (0.27-1 mm, sub-gram) held
// together by nothing. The solver then had to maintain every interface with
// contact rows alone, forever, and on that opposing-manifold sandwich the
// sequential-impulse iteration DIVERGES: measured 16k -> 4.5e8 in one frame,
// 1.78e12 m/s before the clamp, grass shrapnel at ~100 m/s. That shrapnel,
// sweeping through canopies, is what issue #38 looks like from a camera.
//
// WHAT THIS TEST DRIVES: the REAL path. generate_grass_patch stores the patch
// in the KG; activate_entity_now materialises it exactly the way chunk
// activation does, including gluon recreation via create_gluon_from_kg. An
// earlier isolation attempt called a generator and asserted on an empty world;
// this one refuses to assert anything until bodies exist.
//
// THREE ASSERTIONS, in dependency order, each naming its law
// (assert-protocol migration, 2026-08-21):
//   1  hygiene: BODIES EXIST after activation, or everything below is
//      vacuous. This guards the measurement, not a law.
//   2  INV-4: GLUONS EXIST after activation. A structure is born at rest
//      with its bonds in place; a blade materialised as free plates was
//      never born as a structure at all, and INV-9 has nothing to derive a
//      force law for. It was ZERO before the fix: the KG had no gluon
//      records to recreate.
//   3  INV-11 (no detonation) and INV-1 (nothing leaves the world / a plant
//      reaches something immobile): the explosion detector stays silent for
//      180 frames and no blade particle ends up further than 2 m from where
//      activation placed it. A bonded blade may lean or settle; it may not
//      leave.
//
//   ./build-release/logosphere-tests --test test_grass_holds_together --no-head
// =============================================================================

#include "generated/earth_ontology_registry.h"
#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/particle.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/kg/ontology_registry.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace X = ::logosphere::expdet;

bool test_grass_holds_together() {
    printf("\n=== GRASS MUST BE BONDED, AND MUST NOT DETONATE (issue #47) ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    // The engine ontology has no Grass; Eden's schema extends it. A test is a
    // game for this purpose and extends it the sanctioned way (the
    // test_ontology_extension pattern). Without this every createEntity call
    // is REJECTED and returns 0, and the first version of this test spent its
    // failure on plumbing instead of physics.
    {
        kg::OntologyRegistry reg;
        // The union-merged registry validates parents (malleus H2 made real):
        // a runtime extension must declare the chain it claims.
        // The CANONICAL grass vocabulary: the generated earth-pack
        // registry. Hand-rolled fixture registries drifted from the
        // generator's real writes 16 keys deep (2026-08-14).
        (void)reg;
        engine.get_kg().extendOntology(earth::ontology::registry());
    }

    auto& ps = engine.get_particle_system();
    const size_t bodies_before = [&] {
        auto v = ps.lock_particles_for_write();
        return v.size();
    }();
    const size_t gluons_before = engine.get_physics_system().get_total_gluon_count();

    // Store the patch in the KG, then materialise it the way chunk activation
    // does. This is the path Eden's grass actually takes.
    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    kg::EntityID patch = ogen.generate_grass_patch(0.0f, 0.0f, 0.0f,
                                                   GrassPatchSpec::tall_grass());
    scene.activate_entity_now(patch);
    ps.flush_pending_particles();

    size_t bodies_after = 0;
    std::vector<float> x0, y0, z0;
    std::vector<size_t> blade_ids;
    {
        auto v = ps.lock_particles_for_write();
        bodies_after = v.size();
        for (size_t i = bodies_before; i < v.size(); ++i) {
            blade_ids.push_back(i);
            x0.push_back(v[i].x); y0.push_back(v[i].y); z0.push_back(v[i].z);
        }
    }
    const size_t gluons_after = engine.get_physics_system().get_total_gluon_count();
    const size_t new_bodies = bodies_after - bodies_before;
    const size_t new_gluons = gluons_after - gluons_before;

    printf("  %-44s %8zu\n", "bodies materialised by activation", new_bodies);
    printf("  %-44s %8zu\n", "gluons materialised by activation", new_gluons);

    // ---- 1. bodies exist, or nothing below means anything ------------------
    if (new_bodies == 0) {
        printf("\n  *** hygiene: NOTHING MATERIALISED. ***\n"
               "  The patch was stored and activated and produced zero bodies, so this\n"
               "  test cannot say anything about bonding or detonation. The activation\n"
               "  path is broken or has moved; fix the plumbing before reading physics.\n");
        printf("\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    // ---- 2. the fix's contract: the blades are BONDED ----------------------
    const bool bonded = new_gluons > 0;
    if (!bonded) {
        printf("\n  *** INV-4: THE GRASS IS UNBONDED. ***\n"
               "  %zu bodies materialised with ZERO gluons between them. Every blade is a\n"
               "  tower of free plates held by nothing, and the solver must hold every\n"
               "  interface with contact rows alone, forever. That is the sandwich the\n"
               "  sequential-impulse iteration diverges on (16k -> 4.5e8 in one frame,\n"
               "  measured on issue #47), and grass shrapnel at ~100 m/s is the result.\n"
               "  The design says blades are gluon-bonded; the KG contains no gluons.\n",
               new_bodies);
        printf("\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    // ---- 3. it holds together and stays calm -------------------------------
    X::set_enabled(true);
    X::reset();
    for (int f = 0; f < 180; ++f) engine.update(1.0 / 60.0);
    const X::Stats s = X::stats();

    double max_drift = 0.0;
    {
        auto v = ps.lock_particles_for_write();
        for (size_t k = 0; k < blade_ids.size(); ++k) {
            const size_t i = blade_ids[k];
            if (i >= v.size()) continue;
            const double dx = v[i].x - x0[k], dy = v[i].y - y0[k], dz = v[i].z - z0[k];
            max_drift = std::fmax(max_drift, std::sqrt(dx*dx + dy*dy + dz*dz));
        }
    }

    printf("  %-44s %8llu\n", "INV-11: ceiling events (must be 0)",
           (unsigned long long)s.speed_events);
    printf("  %-44s %8.2f\n", "worst body speed seen (m/s)", s.worst_speed);
    printf("  %-44s %8.3f\n", "INV-1: max drift from placement (m)", max_drift);

    const bool calm = s.speed_events == 0;
    const bool held = max_drift < 2.0;   // lean and settle yes, leave no
    printf("\n");
    if (calm && held) {
        printf("  INV-4/INV-11/INV-1: BONDED AND CALM. %zu bodies, %zu gluons, 180 frames, nothing over the\n"
               "  ceiling and nothing further than %.2f m from where it grew. A blade may\n"
               "  lean; it may not leave.\n", new_bodies, new_gluons, max_drift);
    } else if (!calm) {
        printf("  *** INV-11: STILL DETONATING. *** Bonds exist but %llu ceiling events fired\n"
               "  (worst %.1f m/s). Bonding alone did not remove the divergence; the\n"
               "  solver half of issue #47 is doing the damage even on bonded grass.\n",
               (unsigned long long)s.speed_events, s.worst_speed);
    } else {
        printf("  *** INV-1: THE BLADES LEFT. *** No ceiling events, but a body drifted %.2f m\n"
               "  from placement. The bonds exist and are too weak, or the plant has no\n"
               "  root and toppled off the world.\n", max_drift);
    }

    const bool pass = bonded && calm && held;
    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    engine.shutdown();
    return pass;
}
