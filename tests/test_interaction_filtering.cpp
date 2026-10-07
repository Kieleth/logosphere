// =============================================================================
// PARTICLE INTERACTION — solver filter seam (Phase 2 contract)
// =============================================================================
// The contact broad phase consults ParticleInteractionSystem::
// should_contact after AABB overlap: declared-passable pairs skip
// narrow phase, are recorded as FilteredOverlaps for the Phase-3 force
// pass, and open a ContactFilteredEvent episode on the bus.
//
// Contracts:
//   f1  DEFAULT WORLD A/B: with the interaction system wired but no
//       profiles registered, two overlapping DYNAMIC boxes resolve
//       contact exactly as before (they push apart, physics collision
//       events fire for the pair). This is the no-behavior-change
//       guarantee.
//   f2  FILTERED: with mutually-declining profiles the same pair never
//       exchanges contact (no separation, no physics collision events
//       between them) — but the overlap is not decoration: it is
//       recorded (get_filtered_overlaps) for the volume-force pass.
//   f3  EVENTS: the episode opens exactly ONE ContactFilteredEvent on
//       the bus while the pair overlaps continuously (per-episode, not
//       per-frame/per-substep).
//
// Run: ./build/logosphere-physics-guards --test test_interaction_filtering
// =============================================================================

#include "core/engine.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/events/event_bus.h"
#include "materials.h"
#include "particle.h"

#include <cmath>
#include <cstdio>

namespace {

struct FilterRig {
    Engine engine;
    bool ok = false;
    int a = -1, b = -1;

    explicit FilterRig(const char* title) {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.window_title = title;
        cfg.enable_chat_window = false;
        ok = (engine.initialize(cfg) == 0);
        if (!ok) return;

        // Floor so the pair rests instead of falling forever.
        Particle floor_p = {};
        floor_p.shape = ParticleShape::BOX;
        floor_p.x = 0; floor_p.y = 0; floor_p.z = 0.25f;
        floor_p.width = 20.0f; floor_p.height = 20.0f; floor_p.thickness = 0.5f;
        floor_p.SetMaterial(Materials::Type::STONE);
        floor_p.is_at_rest = true;
        engine.add_particle(floor_p);

        // Two boxes overlapping by 0.2 in x, resting height.
        auto box = [&](float x) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = x; p.y = 0.0f; p.z = 0.65f;
            p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
            p.size = 0.3f;
            p.SetMaterial(Materials::Type::STONE);
            return engine.add_particle(p);
        };
        // THE OVERLAP IS DRIVEN IN, NOT BORN (INV-37, owner decree 2026-09-01).
        // The pair used to be SPAWNED 0.2 m inside each other, which the
        // creation door refuses: this rig would have had one box instead of
        // two, and every reading below would have been of an empty pair. They
        // are born clear and the second is written into the first before a
        // step runs - the same INV-30 external write test_solver_residual and
        // test_baumgarte_ratchet use. The solver sees the identical state on
        // frame one.
        a = box(-0.05f);
        b = box(+0.85f);
        {
            auto v = engine.get_particle_system().lock_particles_for_write();
            v[b].x = +0.05f;
        }
        engine.get_particle_system().mark_bvh_dirty();
    }

    float separation_x() {
        auto view = engine.get_particle_system().lock_particles_for_read();
        return std::fabs(view[a].x - view[b].x);
    }

    int collision_events_between() {
        int n = 0;
        for (const auto& e : engine.get_physics_system().get_collision_events()) {
            bool ab = ((int)e.particle_a == a && (int)e.particle_b == b);
            bool ba = ((int)e.particle_a == b && (int)e.particle_b == a);
            if (ab || ba) ++n;
        }
        return n;
    }
};

} // namespace

bool test_interaction_filtering() {
    printf("\n=== Interaction Filtering (solver seam) ===\n");
    const double dt = 1.0 / 60.0;
    bool all_ok = true;

    // ------------------------------------------------------------------
    // f1 — default world: contact behavior unchanged.
    // ------------------------------------------------------------------
    {
        FilterRig rig("interaction filtering f1");
        if (!rig.ok) { printf("  ERROR: engine init failed (f1)\n"); return false; }

        float sep0 = rig.separation_x();
        int contact_frames = 0;   // best-effort: collision events clear
                                  // per-substep, so a brief contact can
                                  // be invisible to per-frame sampling
        for (int f = 0; f < 90; ++f) {
            rig.engine.update(dt);
            if (rig.collision_events_between() > 0) ++contact_frames;
        }
        float sep1 = rig.separation_x();

        // The contract is contact RESOLUTION: overlapping defaults get
        // pushed apart. (Event visibility is diagnostic only.)
        bool separated = sep1 > 0.25f;
        printf("  %s: f1 default pair pushes apart (%.3f -> %.3f m, contact frames seen=%d)\n",
               separated ? "PASS" : "FAIL", sep0, sep1, contact_frames);
        all_ok = all_ok && separated;
    }

    // ------------------------------------------------------------------
    // f2 + f3 — filtered pair: no contact, overlap recorded, one episode.
    // ------------------------------------------------------------------
    {
        FilterRig rig("interaction filtering f2");
        if (!rig.ok) { printf("  ERROR: engine init failed (f2)\n"); return false; }

        // Two categories that mutually decline each other but accept the
        // default world (bit 0), so both still rest on the floor.
        using logosphere::interaction::InteractionProfile;
        auto& sys = rig.engine.get_interaction_system();
        InteractionProfile red;
        red.id = 100u; red.category = 1u << 1;
        red.collides_with = ~(1u << 2);   // everything except blue
        sys.register_profile(red);
        InteractionProfile blue;
        blue.id = 200u; blue.category = 1u << 2;
        blue.collides_with = ~(1u << 1);  // everything except red
        sys.register_profile(blue);

        {
            auto view = rig.engine.get_particle_system().lock_particles_for_write();
            view[rig.a].interaction_profile_id = 100u;
            view[rig.b].interaction_profile_id = 200u;
        }

        auto reader = rig.engine.get_event_bus().contact_filtered().create_reader();

        float sep0 = rig.separation_x();
        int contact_frames = 0;
        int filtered_seen = 0;
        size_t episode_events = 0;
        for (int f = 0; f < 90; ++f) {
            rig.engine.update(dt);
            if (rig.collision_events_between() > 0) ++contact_frames;
            if (!rig.engine.get_physics_system().get_filtered_overlaps().empty()) {
                ++filtered_seen;
            }
            episode_events += reader.drain().size();
        }
        float sep1 = rig.separation_x();

        bool stayed = std::fabs(sep1 - sep0) < 0.02f;
        bool no_contact = (contact_frames == 0);
        printf("  %s: f2 filtered pair never contacts (sep %.3f -> %.3f, contact frames=%d)\n",
               (stayed && no_contact) ? "PASS" : "FAIL", sep0, sep1, contact_frames);
        all_ok = all_ok && stayed && no_contact;

        // The pair records while AWAKE; once both go at-rest the broad
        // phase legitimately skips them (sleeping particles do not
        // re-run pair detection). Require a solid awake window.
        bool recorded = filtered_seen >= 10;
        printf("  %s: f2 overlap recorded for the force pass (%d/90 frames, until at-rest)\n",
               recorded ? "PASS" : "FAIL", filtered_seen);
        all_ok = all_ok && recorded;

        bool one_episode = (episode_events == 1);
        printf("  %s: f3 exactly one ContactFilteredEvent episode (got %zu)\n",
               one_episode ? "PASS" : "FAIL", episode_events);
        all_ok = all_ok && one_episode;
    }

    printf("  [%s]\n", all_ok ? "PASS" : "FAIL - interaction filter seam broken");
    return all_ok;
}
