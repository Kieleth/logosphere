// =============================================================================
// THE CREATION DOOR — NOTHING IS BORN INSIDE ANYTHING (INV-37)
// =============================================================================
// Owner decree 2026-09-01: "under no circumstances, any creation of particles
// should be allowed to overlap in space with another... we should have an
// assert/except for any creation-overlapping moment." Owner ruling 2026-09-02
// on the shape: "fresh at the choke point, with the incremental BVH cost
// measured on the headless bench before it ships, in TDD please."
//
// THIS FILE IS THE DOOR'S OWN CONTRACT, at the granularity the field
// witnesses (test_jammed_sleep A and D, test_no_overlap_at_creation) cannot
// reach: every entry path, every shape pair, the same-batch case, the
// oriented case, the bond that must not be attached to a body that was never
// born, and the turtle door still standing beside it.
//
// THE RED. `CREATION_DOOR=0` is the kill switch (INV-36's pattern: a
// diagnostic for A/B, never a shipping mode). Under it this file is the
// pre-door world and every refusal case fails; under the default it passes.
// Run both:
//     ./build/test_creation_door                # the door
//     CREATION_DOOR=0 ./build/test_creation_door # the world before it
//
// WHAT IS NOT ASSERTED HERE, by name: nothing about what the SOLVER does
// afterwards. This file is about the moment of birth. The solver's side of
// INV-37 (a world with no births in overlap converges and sleeps) is
// test_jammed_sleep's.
// =============================================================================

#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/creation_door.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& law, const std::string& text) {
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s: %s\n", ok ? "[PASS]" : "[FAIL]", law.c_str(), text.c_str());
}

Particle box(float x, float y, float z, float w, float h, float t,
             Materials::Type m = Materials::Type::STONE) {
    Particle p{};
    p.shape = ParticleShape::BOX;
    p.x = x; p.y = y; p.z = z;
    p.width = w; p.height = h; p.thickness = t;
    p.size = std::fmax(w, std::fmax(h, t));
    p.r = 0.6f; p.g = 0.6f; p.b = 0.6f; p.a = 1.0f;
    p.SetMaterial(m);
    p.friction = 0.5f;
    return p;
}

Particle sphere(float x, float y, float z, float d,
                Materials::Type m = Materials::Type::STONE) {
    Particle p{};
    p.shape = ParticleShape::SPHERE;
    p.x = x; p.y = y; p.z = z;
    p.size = d;
    p.width = p.height = p.thickness = d;
    p.r = 0.6f; p.g = 0.6f; p.b = 0.6f; p.a = 1.0f;
    p.SetMaterial(m);
    return p;
}

// The direct path: ParticleSystem::add_particle, reached through the public
// entity-bound wrapper. No flush anywhere near it (case e).
int add_direct(ParticleSystem& ps, const Particle& p) {
    return ps.add_particle_to_entity(p, nullptr, kg::INVALID_ENTITY);
}

size_t live(ParticleSystem& ps) { return ps.count(); }

// SLOP is the engine's own geometric-error constant, not a number this test
// invented (INV-29). Everything below is stated as a multiple of it.
constexpr float SLOP = PhysicsV4::SLOP;         // 0.001 m

}  // namespace

int main() {
    std::printf("\n=== THE CREATION DOOR: nothing is born inside anything (INV-37) ===\n");
    std::printf("  SLOP = %.4f m (physics_constants.h). Touching is not overlapping.\n",
                SLOP);
    std::printf("  WORLD: %s\n\n",
                logosphere::creation_door_enabled()
                    ? "DEFAULT — the door is armed"
                    : "CREATION_DOOR=0 — the door is KILLED (the pre-door world; "
                      "every refusal case below is expected to FAIL)");

    // -----------------------------------------------------------------------
    // (b) A LEGAL BIRTH PASSES UNCHANGED — separated, and exactly touching.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        const int floor_id = add_direct(ps, box(0, 0, 0.25f, 4.0f, 4.0f, 0.5f));
        check(floor_id == 0 && live(ps) == 1, "INV-37",
              "a first body into an empty world is created (index " +
                  std::to_string(floor_id) + ")");

        // Touching: the cube's bottom face exactly on the floor's top face.
        const int on_top = add_direct(ps, box(0, 0, 0.5f + 0.25f, 0.5f, 0.5f, 0.5f));
        check(on_top == 1 && live(ps) == 2, "INV-37",
              "a body born TOUCHING (gap 0) is created — touching is not "
              "overlapping (index " + std::to_string(on_top) + ")");

        // Separated by a metre.
        const int apart = add_direct(ps, box(2.0f, 0, 1.5f, 0.5f, 0.5f, 0.5f));
        check(apart == 2 && live(ps) == 3, "INV-37",
              "a body born clear of everything is created");

        // Half a SLOP deep: geometric error, not geometry. Legal by the same
        // constant the solver uses to decide a contact is not a real error.
        const int within = add_direct(ps,
            box(2.0f, 0, 1.5f + 0.5f - 0.5f * SLOP, 0.5f, 0.5f, 0.5f));
        check(within == 3 && live(ps) == 4, "INV-37",
              "a body overlapping by SLOP/2 is created — the threshold is the "
              "engine's own SLOP, no new constant (INV-29)");
    }

    // -----------------------------------------------------------------------
    // (a) + (d) THE DIRECT PATH REFUSES, and says who and how deep.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        add_direct(ps, box(0, 0, 0.15f, 4.0f, 4.0f, 0.3f));    // Eden's tile
        const size_t before = live(ps);

        // Eden's own recipe: a stone born at the tile's centre height.
        const Particle stone = box(0, 0, 0.13f, 0.31f, 0.21f, 0.26f);
        const int id = add_direct(ps, stone);

        check(id < 0, "INV-37",
              "the direct path REFUSES a body born inside a live one (returned " +
                  std::to_string(id) + ", the invalid id)");
        check(live(ps) == before, "INV-37",
              "the refused body is NOT in the live array (" +
                  std::to_string(live(ps)) + " bodies, was " +
                  std::to_string(before) + ")");

        const logosphere::CreationRefusal& r = ps.last_creation_refusal();
        check(r.refused, "INV-37", "the caller can read the refusal");
        check(r.blocker.index == 0, "INV-37",
              "the refusal NAMES the body it hit: P" +
                  std::to_string(r.blocker.index));
        // The stone spans 0.00..0.26 against a tile top of 0.30: the shallowest
        // separating axis is Z, 0.26 m deep. That is the derivation, and the
        // door must report it, in millimetres, not a boolean.
        check(std::fabs(r.depth - 0.26f) < 0.002f, "INV-37",
              "the refusal carries the DEPTH: " +
                  std::to_string(r.depth * 1000.0f) + " mm (derived 260 mm)");
        check(r.text.find("[PHYSICS REFUSED] add_particle") != std::string::npos,
              "INV-37",
              "the line is a [PHYSICS REFUSED] add_particle(...) naming both "
              "bodies");
        check(ps.creation_door_stats().refusals == 1, "hygiene",
              "the door counts its refusals (" +
                  std::to_string(ps.creation_door_stats().refusals) + ")");
    }

    // -----------------------------------------------------------------------
    // (d) THE QUEUED PATH REFUSES TOO, and does not hand out an index for a
    //     body that will never exist.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        add_direct(ps, box(0, 0, 0.15f, 4.0f, 4.0f, 0.3f));

        const int queued = ps.queue_particle_addition(box(0, 0, 0.13f, 0.31f, 0.21f, 0.26f));
        check(queued < 0, "INV-37",
              "queue_particle_addition REFUSES a body born inside a live one "
              "(returned " + std::to_string(queued) + ")");

        ps.flush_pending_particles();
        check(live(ps) == 1, "INV-37",
              "the flush creates nothing: " + std::to_string(live(ps)) +
                  " live bodies");

        // And the prediction stays honest for whoever queues next: a refused
        // body must not be counted in the index the NEXT caller is given,
        // because generators bond by that number (GEDANKEN-69).
        const int next = ps.queue_particle_addition(box(0, 0, 0.30f + 0.13f,
                                                        0.31f, 0.21f, 0.26f));
        check(next == 1, "INV-37",
              "the next queued body is told index 1, not 2 — a refusal is not "
              "counted in a predicted index (got " + std::to_string(next) + ")");
        ps.flush_pending_particles();
        check(live(ps) == 2 && next == 1, "hygiene",
              "and that is where it actually landed");
    }

    // -----------------------------------------------------------------------
    // (c) TWO NEWBORNS IN ONE BATCH: the second is refused.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        const int a = ps.queue_particle_addition(box(0, 0, 0.5f, 1.0f, 1.0f, 1.0f));
        const int b = ps.queue_particle_addition(box(0.5f, 0, 0.5f, 1.0f, 1.0f, 1.0f));
        check(a == 0, "INV-37", "the first of the batch is accepted");
        check(b < 0, "INV-37",
              "the SECOND body of the SAME batch, overlapping the first by "
              "500 mm, is refused before either exists (returned " +
                  std::to_string(b) + ")");
        ps.flush_pending_particles();
        check(live(ps) == 1, "INV-37",
              "one body survives the batch, not two (" +
                  std::to_string(live(ps)) + ")");
    }

    // -----------------------------------------------------------------------
    // (e) A HEADLESS RUN THAT NEVER FLUSHES still refuses on the direct path.
    //     This is why the door is at the push_back and not at the flush: the
    //     chunk paths, entity activation and the generators are direct adds,
    //     and flush_pending_particles is only called from Engine::render and
    //     ChunkSystem — neither of which a headless test reaches.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        PhysicsSystem physics;
        physics.initialize(ps);
        add_direct(ps, box(0, 0, 0.5f, 2.0f, 2.0f, 1.0f));
        const int buried = add_direct(ps, box(0, 0, 0.5f, 0.4f, 0.4f, 0.4f));
        check(buried < 0 && live(ps) == 1, "INV-37",
              "no flush is ever called and the birth is still refused");
        physics.shutdown();
    }

    // -----------------------------------------------------------------------
    // (f) SPHERE-BOX and SPHERE-SPHERE go through the same door.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        add_direct(ps, box(0, 0, 0.5f, 2.0f, 2.0f, 1.0f));
        // Sphere of diameter 0.4 centred 0.1 below the box's top face:
        // 0.3 m of it is inside.
        const int s_in = add_direct(ps, sphere(0, 0, 0.9f, 0.4f));
        check(s_in < 0, "INV-37", "a SPHERE born inside a BOX is refused");
        // Resting exactly on the top face: touching, legal.
        const int s_on = add_direct(ps, sphere(0, 0, 1.0f + 0.2f, 0.4f));
        check(s_on >= 0, "INV-37",
              "a SPHERE resting exactly on the box's face is created");
        // A second sphere overlapping the first by 0.1 m.
        const int s2 = add_direct(ps, sphere(0.3f, 0, 1.2f, 0.4f));
        check(s2 < 0, "INV-37", "a SPHERE born inside a SPHERE is refused");
        const int s3 = add_direct(ps, sphere(0.4f, 0, 1.2f, 0.4f));
        check(s3 >= 0, "INV-37",
              "two spheres exactly touching (centres one diameter apart) are "
              "both created");
    }

    // -----------------------------------------------------------------------
    // (g) THE ROTATED BOX IS JUDGED AS ITS OBB, NEVER AS ITS SLAB (INV-12).
    //     A 2 m x 0.2 m x 0.2 m bar turned 45 degrees about Z reaches
    //     1.0*cos45 + 0.1*cos45 = 0.778 m along BOTH world axes, so its
    //     axis-aligned slab is a 1.56 m square with a 0.2 m body inside it.
    //     rotation_z is CLOCKWISE viewed from +Z (the engine convention), so
    //     local +X (the 2 m length) points along u = (+0.707, -0.707) and the
    //     bar's waist runs along v = (+0.707, +0.707).
    //       - a 0.3 m cube at (0.6, 0.6) is 0.849 m out along v: INSIDE the
    //         slab, CLEAR of the bar. The axis-aligned reading would refuse a
    //         legal birth; the oriented one must let it through.
    //       - a 0.3 m cube at 0.5*u = (0.354, -0.354) is straight through the
    //         bar's waist and must be refused.
    //     This pins the rotation convention as well as the solid: with the
    //     other sign the two cases swap.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        Particle bar = box(0, 0, 1.0f, 2.0f, 0.2f, 0.2f);
        bar.rotation_z = 0.7853981634f;               // 45 deg, engine CW
        const int b0 = add_direct(ps, bar);
        check(b0 == 0, "INV-12", "a rotated bar is created");

        // Clear of the real solid, inside its axis-aligned bound.
        Particle clear_of_it = box(0.6f, 0.6f, 1.0f, 0.3f, 0.3f, 0.2f);
        const int c = add_direct(ps, clear_of_it);
        check(c >= 0, "INV-12",
              "a body inside the rotated bar's world-axis SLAB but clear of "
              "the bar itself is CREATED — the door reads the oriented solid, "
              "never the slab");

        // Straight through the bar's waist, along its own axis.
        Particle through = box(0.354f, -0.354f, 1.0f, 0.3f, 0.3f, 0.2f);
        const int t = add_direct(ps, through);
        check(t < 0, "INV-12",
              "a body through the rotated bar's real waist is refused");
    }

    // -----------------------------------------------------------------------
    // (h) THE BOND IS NOT ATTACHED TO A BODY THAT WAS NEVER BORN.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        PhysicsSystem physics;
        physics.initialize(ps);
        const int anchor = add_direct(ps, box(0, 0, 0.5f, 1.0f, 1.0f, 1.0f));
        const size_t gluons_before = physics.get_total_gluon_count();

        auto g = std::make_unique<NailGluon>();
        g->offset_a = {0.0f, 0.0f, 0.0f};      // both centres: B lands ON A
        g->offset_b = {0.0f, 0.0f, 0.0f};
        g->stiffness = 1.0e6f;
        g->damping = 1.0e3f;
        g->breaking_force = 700.0f;
        const size_t b = physics.add_particle_with_gluon_to(
            static_cast<size_t>(anchor), box(0, 0, 0.5f, 0.5f, 0.5f, 0.5f),
            std::move(g), /*use_config_position=*/false);

        check(b == static_cast<size_t>(-1), "INV-37",
              "add_particle_with_gluon_to REFUSES a bonded part placed inside "
              "its parent — a bonded structure is not an exempt class");
        check(live(ps) == 1, "INV-37", "and the part is not in the world");
        check(physics.get_total_gluon_count() == gluons_before, "INV-37",
              "and NO gluon was created for it (" +
                  std::to_string(physics.get_total_gluon_count()) + " gluons, was " +
                  std::to_string(gluons_before) + ")");
        physics.shutdown();
    }

    // -----------------------------------------------------------------------
    // (h, second half) THE TURTLE DOOR STILL STANDS. The creation door is its
    // twin, not its replacement: a body legal in every neighbour's eyes and
    // below the world floor must still be caught. Asserted through the lever
    // (TURTLE_LENIENT downgrades the abort) so this file cannot kill itself
    // proving that the other door aborts.
    // -----------------------------------------------------------------------
    {
        const bool lenient = std::getenv("TURTLE_LENIENT") != nullptr;
        if (!lenient) {
            std::printf("  [SKIP] INV-1/turtle: the turtle door ABORTS by design; "
                        "re-run with TURTLE_LENIENT=1 to see it counted here "
                        "(scripts/physics_sweep.py runs both)\n");
            ++checks;
        } else {
            ParticleSystem ps;
            const int under = add_direct(ps, box(20.0f, 20.0f, -1.0f, 0.5f, 0.5f, 0.5f));
            check(under >= 0, "INV-1",
                  "under TURTLE_LENIENT the below-floor body is reported and "
                  "still created — the turtle door is untouched by INV-37");
        }
    }

    // -----------------------------------------------------------------------
    // THE COST, on the smallest world that can show it: a 40 x 40 grid of
    // touching tiles, all legal, all through the door. Reported, not
    // asserted — the bar is the headless Eden bench, and a number in a unit
    // test that fails on a busy machine is a flake, not a law.
    // -----------------------------------------------------------------------
    {
        ParticleSystem ps;
        const int N = 40;
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
                add_direct(ps, box(i * 1.0f, j * 1.0f, 0.15f, 1.0f, 1.0f, 0.3f));
        const logosphere::CreationDoorStats& s = ps.creation_door_stats();
        std::printf("  [measure] %d touching tiles: %zu births, %zu refused, "
                    "%zu candidates, %zu exact tests, %.3f ms total "
                    "(%.2f us/birth)\n",
                    N * N, s.births, s.refusals, s.candidates, s.exact_tests,
                    s.micros / 1000.0, s.births ? s.micros / s.births : 0.0);
        check(live(ps) == static_cast<size_t>(N * N), "INV-37",
              "all " + std::to_string(N * N) +
                  " abutting tiles are created (touching is not overlapping): " +
                  std::to_string(live(ps)));
    }

    std::printf("\n  %d of %d checks pass%s\n", checks - failures, checks,
                failures ? "  <-- RED" : "");
    if (!logosphere::creation_door_enabled() && failures > 0) {
        std::printf("  (expected: CREATION_DOOR=0 is the world before the door)\n");
    }
    return failures == 0 ? 0 : 1;
}
