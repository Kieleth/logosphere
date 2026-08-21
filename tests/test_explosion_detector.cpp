// =============================================================================
// DOES THE ENGINE NOTICE ITS OWN EXPLOSIONS? (issue #42)
// =============================================================================
// Two world-scale blowups in one week, both discovered by a person watching a
// screen, and one of them no longer reproduces. The detector this test guards
// is the engine saying it ITSELF, as a rate-limited [EXPLOSION WARNING] that
// names the offending body.
//
// LAWS (assert-protocol migration, 2026-08-21). INV-11 is the law and this
// file is its MECHANISM under test: the detector is INV-11's runtime
// verification, so what is being checked here is that the instrument works,
// not that any particular scene is explosion-free (TEST_AUDIT says exactly
// that in its gaps field). Per check:
//   quiet control    hygiene — a tripwire that cries wolf gets disabled, and
//                    a disabled tripwire is the state this file exists to end.
//                    Nothing below it is evidence until it reads zero.
//   detonation       INV-11: a body past the speed ceiling is a failure, and
//                    the warning must NAME it (the S22 lesson).
//   collective       INV-3: six bodies each under the per-body ceiling gained
//                    megajoules in one frame. Energy created is energy
//                    created whether or not any single body looks extreme;
//                    that is why INV-11 carries an energy signal at all.
//   kill switch      hygiene (engine gate contract, not a physics law): the
//                    always-on cost argument depends on off meaning off.
//
// THE ORDER OF THE CHECKS IS THE POINT.
//
//   1  QUIET CONTROL FIRST. A resting scene runs 180 frames and the detector
//      must stay silent. A tripwire that cries wolf gets turned off, and a
//      turned-off tripwire is the situation this file exists to end. This is
//      also the sensitivity control's other half: if both halves were noisy,
//      "it fired on the explosion" would mean nothing.
//   2  DETONATION. A body is given 100 m/s, far beyond anything a legitimate
//      mechanism produces (settled canopy 1.2 m/s, Baumgarte ejection capped
//      at 4, a 50 m fall 31). The detector must fire, and must NAME that
//      exact particle. Injected rather than provoked, deliberately: the real
//      explosion cannot currently be summoned, and the detector's contract is
//      about the STATE of the world, not about which bug produced it.
//   3  KILL SWITCH. Disabled, the same detonation must produce nothing, and
//      the scan must not run. Off has to mean off, or the cost argument for
//      always-on is a lie.
//
//   ./build-release/logosphere-tests --test test_explosion_detector --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include <cstdio>
#include <vector>

namespace X = ::logosphere::expdet;

namespace {

// A 3x3 kinematic floor and one resting box: the quietest world the engine
// can hold, borrowed from the battery. Minimal on purpose; scenery is bodies.
int build_quiet_scene(Engine& engine) {
    auto& ps = engine.get_particle_system();
    for (int c = -1; c <= 1; ++c)
        for (int d = -1; d <= 1; ++d) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)c; p.y = (float)d; p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = p.g = p.b = 0.5f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
    Particle b = {};
    b.shape = ParticleShape::BOX;
    b.x = 0; b.y = 0; b.z = 0.6f;
    b.width = b.height = b.thickness = 1.0f; b.size = 1.0f;
    b.r = 0.8f; b.g = 0.4f; b.b = 0.3f; b.a = 1.0f;
    b.SetMaterial(Materials::Type::STONE);
    const int id = engine.add_particle(b);
    ps.flush_pending_particles();
    return id;
}

}  // namespace

bool test_explosion_detector() {
    printf("\n=== DOES THE ENGINE NOTICE ITS OWN EXPLOSIONS? (issue #42) ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    const int box = build_quiet_scene(engine);

    // ---- 1. QUIET CONTROL -------------------------------------------------
    X::set_enabled(true);
    X::reset();
    for (int f = 0; f < 180; ++f) engine.update(1.0 / 60.0);
    const X::Stats quiet = X::stats();

    printf("  %-44s warnings: speed %llu, energy %llu, escape %llu; worst %.2f m/s\n",
           "hygiene QUIET: box resting on a floor, 180 frames",
           (unsigned long long)quiet.speed_warnings,
           (unsigned long long)quiet.energy_warnings,
           (unsigned long long)quiet.escape_warnings, quiet.worst_speed);

    const bool quiet_ok = quiet.speed_warnings == 0 && quiet.energy_warnings == 0 &&
                          quiet.escape_warnings == 0;
    if (!quiet_ok) {
        printf("\n  *** hygiene: FALSE ALARM. ***\n"
               "  A box sitting still on a floor triggered the detector. A tripwire that\n"
               "  cries wolf gets disabled, and disabled is the state this exists to end.\n"
               "  Its thresholds are wrong; nothing below can be trusted until this is 0.\n");
        printf("\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    // ---- 2. DETONATION ----------------------------------------------------
    X::reset();
    {
        auto v = engine.get_particle_system().lock_particles_for_write();
        // STRAIGHT UP, deliberately. The first version kicked the box
        // HORIZONTALLY at 100 m/s and the detector read nothing, because the
        // box sits on floor tiles: within that same frame it slammed into a
        // tile-seam contact and the solver, correctly, stopped it like a wall.
        // The detonation was real and the world un-detonated it before the
        // scan. Scenery is bodies; upward there is nothing to hit.
        v[box].vz = 100.0f;                    // nothing legitimate does this
        v[box].is_at_rest = false;
    }
    for (int f = 0; f < 5; ++f) engine.update(1.0 / 60.0);
    const X::Stats bang = X::stats();

    printf("  %-44s warnings: speed %llu, energy %llu; worst %.1f m/s on particle %d\n",
           "INV-11 DETONATION: particle kicked to 100 m/s",
           (unsigned long long)bang.speed_warnings,
           (unsigned long long)bang.energy_warnings,
           bang.worst_speed, bang.worst_id);

    const bool fired = bang.speed_warnings >= 1;
    const bool named = bang.worst_id == box;
    if (!fired || !named) {
        printf("\n  *** INV-11: THE DETECTOR IS BLIND. ***\n"
               "  A body at 100 m/s, 2.5x the ceiling, produced %llu warnings and the\n"
               "  worst body was recorded as %d when it should be %d. An explosion\n"
               "  detector that misses a 100 m/s body will miss everything, and one that\n"
               "  cannot name the body cannot be sanity-checked (the S22 lesson).\n",
               (unsigned long long)bang.speed_warnings, bang.worst_id, box);
        printf("\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    // ---- 2b. COLLECTIVE DETONATION ----------------------------------------
    // Six bodies kicked to 25 m/s at once: every one is BELOW the 40 m/s
    // ceiling, so the speed signal cannot see this. The energy signal must,
    // because a 4+ MJ rise spread over six bodies is what an actual explosion
    // looks like, and it is exactly the case a per-body ceiling is blind to.
    // (A SINGLE body's injection must NOT fire it: that is a throw, and the
    // battery's projectile scenario proved it false-alarmed before the
    // rise > 2x fastest-body condition existed.)
    // QUIET THE WORLD FIRST. The detonated box from phase 2 is still flying at
    // ~98 m/s carrying ~12 MJ; left alone it keeps the speed signal firing and
    // its KE poisons the ratio baseline, so the collective rise reads as 1.3x
    // and is (correctly) suppressed. First version of this phase did exactly
    // that and failed both assertions: a test phase inherits its predecessor's
    // world, and this file has no more right to skip that lesson than a scene
    // has to skip the scenery-is-bodies one.
    {
        auto v = engine.get_particle_system().lock_particles_for_write();
        v[box].vx = v[box].vy = v[box].vz = 0.0f;
    }
    std::vector<int> swarm;
    {
        auto& ps2 = engine.get_particle_system();
        for (int k = 0; k < 6; ++k) {
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = -8.0f + k * 3.0f; p.y = 8.0f; p.z = 5.0f;   // airborne, apart
            p.width = p.height = p.thickness = 1.0f; p.size = 1.0f;
            p.r = 0.6f; p.g = 0.6f; p.b = 0.2f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            swarm.push_back(engine.add_particle(p));
        }
        ps2.flush_pending_particles();
    }
    engine.update(1.0 / 60.0);                 // settle
    X::reset();
    // THREE establishing updates, not one. Physics ticks at 30 Hz under 60 Hz
    // engine updates, so a single update can land on a zero-tick frame and
    // leave the detector's previous-frame KE unknown; the injection spike then
    // arrives as "first frame ever" and is swallowed by the no-baseline guard.
    // Found by printing the accumulators: KE 4,321 kJ recorded, zero warnings.
    for (int f = 0; f < 3; ++f) engine.update(1.0 / 60.0);
    {
        auto v = engine.get_particle_system().lock_particles_for_write();
        for (int id : swarm) { v[id].vz = 25.0f; v[id].is_at_rest = false; }
    }
    for (int f = 0; f < 5; ++f) engine.update(1.0 / 60.0);
    const X::Stats coll = X::stats();
    printf("  %-44s warnings: speed %llu, energy %llu; worst %.1f m/s, KE %.1f kJ\n",
           "INV-3 COLLECTIVE: 6 bodies at 25 m/s (under ceiling)",
           (unsigned long long)coll.speed_warnings,
           (unsigned long long)coll.energy_warnings,
           coll.worst_speed, coll.ke_total / 1000.0);
    const bool collective = coll.energy_warnings >= 1 && coll.speed_warnings == 0;
    if (!collective) {
        printf("\n  *** INV-3/INV-11: THE COLLECTIVE SIGNAL FAILED. ***\n"
               "  Six bodies below the ceiling gained megajoules in one frame and the\n"
               "  energy signal reported %llu warnings (speed reported %llu, must be 0).\n"
               "  This is the exact shape of the Eden explosions: many bodies, none\n"
               "  individually extreme, and a per-body ceiling blind to all of it.\n",
               (unsigned long long)coll.energy_warnings,
               (unsigned long long)coll.speed_warnings);
        printf("\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    // ---- 3. KILL SWITCH ---------------------------------------------------
    X::set_enabled(false);
    X::reset();
    {
        auto v = engine.get_particle_system().lock_particles_for_write();
        v[box].vx = 100.0f;
        v[box].is_at_rest = false;
    }
    for (int f = 0; f < 5; ++f) engine.update(1.0 / 60.0);
    const X::Stats off = X::stats();
    const bool off_ok = off.speed_warnings == 0 && off.speed_events == 0 &&
                        off.worst_id == -1;
    printf("  %-44s warnings: %llu, events: %llu, worst id: %d\n",
           "hygiene KILL SWITCH: same detonation, detector off",
           (unsigned long long)off.speed_warnings,
           (unsigned long long)off.speed_events, off.worst_id);
    X::set_enabled(true);   // leave the tripwire armed for whoever runs next

    printf("\n");
    const bool pass = quiet_ok && fired && named && collective && off_ok;
    if (pass) {
        printf("  ARMED. Silent through 180 quiet frames, fired within 5 frames of a\n"
               "  detonation, named the exact body, and off means off. From this commit\n"
               "  the engine reports its own explosions instead of waiting for a human\n"
               "  to notice one on a screen.\n");
    } else if (!off_ok) {
        printf("  *** hygiene: OFF DOES NOT MEAN OFF — the scan ran while disabled. The always-on\n"
               "  cost argument depends on the gate being real.\n");
    }
    printf("\n  %s\n", pass ? "PASS" : "FAIL");
    engine.shutdown();
    return pass;
}
