// ============================================================================
// GRASS BENDS, IT DOES NOT MOW (issue #47)
// ============================================================================
// THE LAW. Walking through grass bends it. It does not sever it. A blade may
// lean, may be pushed flat, may stay flat — but the pass of a leg must not
// leave a trail of detached blades behind it.
//
// THE DEFECT, measured on the walk gate with its drifter named:
//   worst drifter P277: d = (0.00, -2.52, -0.50) m
//     bonds still attached  0
//     grass bonds in world: 614 -> 605  (NINE TORE in one pass)
// The displacement is purely along the walk axis, and the blade has no bonds
// left, so it was not dragged while attached: it was severed and then carried.
//
// WHY. A blade's root is pinned and its top is pushed by a rotation-blind AABB
// that cannot slide. The bond is a DISTANCE constraint, so the only way it can
// absorb that push is to STRETCH, and it reaches OrganicGluon's 2.0x rest
// ratio and snaps. OrganicSpec already carries the mechanism that would let it
// bend instead — `gluon_quat_drive`, documented as "blades hold their grown
// pose and bend by ROTATING" — and it is false on every species that exists.
// Commit 29ead13 said it in its title a long time ago: the blade must bend by
// rotating, not by shearing.
//
// THE REDUCTION, AND WHAT IT FAILED TO REPRODUCE. A kinematic sweeper driven
// through the patch instead of a humanoid: a leg brushing a blade is a moving
// body pushing on it, so the rig around the leg ought not to matter.
//
// IT DOES MATTER. This test is GREEN and was green on its first run, and it
// stays green at 1.2, 2.4, 4.0 and 8.0 m/s — 6.7x walking pace — with 223
// bonds before and 223 after every time. So a single moving box does not tear
// grass at any speed, and the walk gate's nine tears need something Eva has
// that this does not. Candidates, none yet tested:
//   - her legs are DYNAMIC and quat-driven, not kinematic: a driven joint can
//     push with the whole rig's authority behind it, and can keep pushing
//     against a blade that is not yielding
//   - heel-strike spawns pin gluons mid-pass
//   - a blade can be PINCHED between two of her parts (shin and foot, or the
//     two legs closing) and stretched by their relative motion. One box cannot
//     pinch anything.
//
// So this file is NOT the red for the mowing defect. It is a guard: the
// invariant is real and currently holds, and if a future change starts
// severing grass under a plain moving body it will say so. The real red has
// to be built on whichever of those three it turns out to be.
//
// TWO ASSERTIONS (both currently satisfied):
//   1  NOTHING TORE. The bond count after the pass equals the bond count
//      before it.
//   2  EVERY BODY IS STILL ATTACHED. Same union-find invariant
//      test_plants_are_rooted uses: every component of the bond graph
//      contains a KINEMATIC body. A severed blade is a component with no root.
//
//   ./build-release/logosphere-tests --test test_grass_bends_not_tears --no-head
// ============================================================================

#include "generated/earth_ontology_registry.h"
#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/kg/ontology_registry.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <vector>

namespace {

struct DisjointSet {
    std::vector<size_t> parent;
    explicit DisjointSet(size_t n) : parent(n) {
        for (size_t i = 0; i < n; ++i) parent[i] = i;
    }
    size_t find(size_t a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    }
    void join(size_t a, size_t b) { a = find(a); b = find(b); if (a != b) parent[b] = a; }
};

// How many blade bodies sit in a bond-graph component containing no KINEMATIC
// body — i.e. how much grass is no longer attached to the ground.
size_t count_adrift(Engine& engine, const std::vector<size_t>& ids) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    std::map<size_t, size_t> slot;
    for (size_t k = 0; k < ids.size(); ++k) slot[ids[k]] = k;

    DisjointSet ds(ids.size());
    std::vector<bool> rooted(ids.size(), false);
    {
        auto v = ps.lock_particles_for_write();
        for (size_t k = 0; k < ids.size(); ++k) {
            if (ids[k] < v.size())
                rooted[k] = v[ids[k]].solver_mode == ParticleSolverMode::KINEMATIC;
            for (const GluonConstraintBase* g : physics.get_gluons_for_particle(ids[k])) {
                if (!g) continue;
                auto ia = slot.find(g->particle_a), ib = slot.find(g->particle_b);
                if (ia == slot.end() || ib == slot.end()) continue;
                ds.join(ia->second, ib->second);
            }
        }
    }
    std::map<size_t, bool> comp_rooted;
    std::map<size_t, size_t> comp_size;
    for (size_t k = 0; k < ids.size(); ++k) {
        const size_t r = ds.find(k);
        comp_size[r]++;
        comp_rooted[r] = comp_rooted.count(r) ? (comp_rooted[r] || rooted[k]) : rooted[k];
    }
    size_t adrift = 0;
    for (const auto& [root, is_rooted] : comp_rooted)
        if (!is_rooted) adrift += comp_size[root];
    return adrift;
}

} // namespace

bool test_grass_bends_not_tears() {
    printf("\n=== GRASS BENDS, IT DOES NOT MOW (issue #47) ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  engine init failed\n  FAIL\n"); return false; }
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
    auto& physics = engine.get_physics_system();

    // A floor to stand the grass on.
    for (int cx = -3; cx <= 3; ++cx)
        for (int cy = -6; cy <= 6; ++cy) {
            Particle t = {};
            t.shape = ParticleShape::BOX;
            t.x = (float)cx; t.y = (float)cy; t.z = 0.05f;
            t.width = t.height = 1.0f; t.thickness = 0.1f; t.size = 1.0f;
            t.SetMaterial(Materials::Type::STONE);
            const int id = engine.add_particle(t);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }

    const size_t before_bodies = [&]{ auto v = ps.lock_particles_for_write(); return v.size(); }();

    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    kg::EntityID patch = ogen.generate_grass_patch(0.0f, 0.0f, 0.10f,
                                                   GrassPatchSpec::tall_grass());
    scene.activate_entity_now(patch);
    ps.flush_pending_particles();

    std::vector<size_t> grass;
    {
        auto v = ps.lock_particles_for_write();
        for (size_t i = before_bodies; i < v.size(); ++i) grass.push_back(i);
    }
    const size_t bonds_before  = physics.get_total_gluon_count();
    const size_t adrift_before = count_adrift(engine, grass);

    // THE SWEEPER: a shin-sized kinematic box driven through the patch at
    // walking pace. Kinematic because a leg's motion is commanded, not
    // negotiated — the same reason a humanoid's legs are quat-driven.
    Particle shin = {};
    shin.shape = ParticleShape::BOX;
    shin.x = 0.0f; shin.y = -4.0f; shin.z = 0.45f;
    shin.width = 0.12f; shin.height = 0.12f; shin.thickness = 0.80f; shin.size = 0.12f;
    shin.r = 0.8f; shin.g = 0.6f; shin.b = 0.5f; shin.a = 1.0f;
    shin.SetMaterial(Materials::Type::FLESH);
    const int sweeper = engine.add_particle(shin);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[sweeper].solver_mode = ParticleSolverMode::KINEMATIC;
        v[sweeper].owner = ParticleOwner::DYNAMICS;
        v[sweeper].is_at_rest = false;
    }

    // 8 m at 1.2 m/s, straight through the middle of the patch.
    // SWEEP_SPEED overrides the pace. A walking BODY moves at 1.2 m/s but a
    // swinging FOOT moves several times that, so "walking speed" is the wrong
    // number for the thing that actually hits a blade.
    const char* sp_env = std::getenv("SWEEP_SPEED");
    const float SPEED = sp_env ? (float)std::atof(sp_env) : 1.2f;
    constexpr float DT = 1.0f / 60.0f;
    const int FRAMES = (int)(8.0f / (SPEED * DT));
    for (int f = 0; f < FRAMES; ++f) {
        { auto v = ps.lock_particles_for_write();
          v[sweeper].y += SPEED * DT; v[sweeper].vy = SPEED; }
        engine.update(DT);
    }
    // Let it settle after the pass: bending is allowed, leaving is not.
    for (int f = 0; f < 180; ++f) engine.update(DT);

    const size_t bonds_after  = physics.get_total_gluon_count();
    const size_t adrift_after = count_adrift(engine, grass);
    const size_t tore = (bonds_before > bonds_after) ? (bonds_before - bonds_after) : 0;

    printf("  %-46s %6zu\n", "blade bodies in the patch", grass.size());
    printf("  %-46s %6zu -> %zu\n", "bonds, before -> after the pass",
           bonds_before, bonds_after);
    printf("  %-46s %6zu   (want 0)\n", "BONDS TORN by one pass", tore);
    printf("  %-46s %6zu   (want %zu)\n", "bodies adrift from any root",
           adrift_after, adrift_before);

    const bool nothing_tore = (tore == 0);
    const bool still_held   = (adrift_after <= adrift_before);
    printf("\n");
    if (!nothing_tore) {
        printf("  *** THE GRASS WAS MOWED. *** One pass of a shin severed %zu bonds.\n"
               "  A blade can only absorb a sideways push by STRETCHING, because its\n"
               "  bond is a distance constraint and its root is pinned, so it reaches\n"
               "  the 2.0x tear ratio and snaps. OrganicSpec::gluon_quat_drive exists\n"
               "  for exactly this — 'blades hold their grown pose and bend by\n"
               "  ROTATING' — and is false on every species in the tree.\n", tore);
    } else if (!still_held) {
        printf("  *** BLADES CAME LOOSE. *** No bond broke, but %zu bodies ended in a\n"
               "  component with no root, so something else detached them.\n", adrift_after);
    } else {
        printf("  IT BENT. One pass at %.1f m/s, nothing severed, every body still\n"
               "  reaches the ground through its bonds.\n", SPEED);
    }

    const bool pass = nothing_tore && still_held;
    printf("\n  %s\n", pass ? "PASS" : "FAIL (walking through grass is mowing it)");
    engine.shutdown();
    return pass;
}
