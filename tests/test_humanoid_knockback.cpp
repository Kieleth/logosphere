// =============================================================================
// KNOCKBACK: a shove against a braced body is not a shove into the void
// =============================================================================
// A humanoid under animation is KINEMATIC while its clip drives it, which
// means the solver may not move it — inverse mass 0. Until 2026-08-14 the
// solver therefore computed the contact impulse, stopped the striker with
// it, and DROPPED the half the humanoid should have taken. A boulder could
// hit a person square in the chest and nothing whatsoever happened, which
// is why TransformationEffect::KNOCKBACK had no implementation to speak of
// and why test_humanoid_impact asserted a displacement that was
// structurally always zero.
//
// The law being tested: momentum a body's authority refuses is BOOKED, not
// destroyed (PhysicsSystem::record_refused_impulse), and the writer holding
// that authority drains it and decides what a push means. For a humanoid,
// the locomotion system is that writer, and the decision is: absorb it into
// the body's world position — a stagger.
//
// Three rungs:
//   R1  physics books the momentum a KINEMATIC body refuses
//   R2  the humanoid's writer drains it and the body actually moves
//   R3  direction is honoured: hit from the west, stagger east
//
// STATE: RED, and what it measured is more interesting than the rungs.
//
// CORRECTED 2026-08-19. This paragraph used to say the boulder passed
// straight through at a constant 7.99 m/s, not deflected and not slowed.
// That number came from the draft that omitted `update_bvh()` (see the
// note in the R1 loop), which could not form a box-box contact at all,
// and it survived the fix in prose. Measured now, through Argus and at
// closest approach rather than as a run-wide minimum: the boulder closes
// on the chest at 8.011 m/s, comes within 0.3178 m of it against a
// 0.3250 m half-extent sum, and leaves the contact at 6.487 m/s having
// entered it at 7.491. The strike lands and physics books it (R1 green,
// four bookings totalling ~46 N*s along +x).
//
// What is still red is everything downstream of the booking, and the
// reason is not the KINEMATIC pin this file was written to test:
//
//   right after registration: chest solver_mode = DYNAMIC(0),
//   owner = DYNAMICS(1), mass = 15.625 kg, is_quat_driven = 1
//
// Only the HIPS are KINEMATIC. The torso and limbs are DYNAMIC — and
// inert anyway, because physics exempts quat-driven bodies from
// gravity and response (physics_system_v4.cpp:608 and the sibling
// reads at :2005, :2015, :3224, :3229, :3393, :3404, all gated on
// `is_quat_driven && owner`). Those are the seven owner-reads task #43
// is meant to remove.
//
// So "who may move this body" has THREE answers in this engine, not
// one: solver_mode, is_at_rest, and is_quat_driven+owner. The third is
// an unnamed pin — it takes a body out of physics entirely, with no
// release path and no mention in the KINEMATIC audit, because it does
// not use the KINEMATIC enum at all. It is why a person cannot be hit.
//
// This test stays red until that is resolved. Fixing it by exempting
// the exemption would be an if-statement edge fix; the mechanism owed
// is a single answer to "who owns this body's motion right now",
// which is the same law INV-31 and the KINEMATIC ruling are converging
// on from the other side.
//
// Run: ./build/test_humanoid_knockback
// =============================================================================

// FULL-STATE NARRATION (assert-or-waive, per DOF, owner directive
// 2026-08-19), observed through Argus so the asserts and the log read
// one source.
//
//   BOULDER (DYNAMIC, thrown west to east at the chest)
//     x, vx         — ASSERTED. And HOW they are measured is the repair
//                     this file most needed: the old readout took the
//                     MINIMUM vx over 90 frames and called it the
//                     strike, so a boulder that missed entirely, landed
//                     on the turtle downrange and was braked by floor
//                     friction still printed "it HIT the humanoid". The
//                     velocity is now sampled AT CLOSEST APPROACH, and
//                     closest approach is itself asserted against the
//                     two bodies' half-extents.
//     y, vy         — ASSERTED at zero: the throw is along +X alone, so
//                     a boulder that curved out of the sagittal plane
//                     never had the strike this test claims.
//     z, vz         — WAIVED: it is thrown on a ballistic arc on
//                     purpose (the launch is aimed high precisely
//                     because it drops ~8 cm over the span). Printed.
//     omega         — WAIVED, watched. D2 owns contact torque and this
//                     file is not its ladder; a nonzero value would be
//                     news and the dump would show it.
//     coherence     — ASSERTED (G-23).
//
//   HUMANOID (hips KINEMATIC, chest DYNAMIC but quat-pinned)
//     hips x        — ASSERTED (R2/R3): displaced, and displaced EAST.
//     hips y        — ASSERTED unmoved: a stagger from a due-west strike
//                     has no north component. Without it, R2 would pass
//                     on a humanoid that slid sideways.
//     hips z        — ASSERTED against an UNSTRUCK TWIN, not against
//                     zero. The struck hips descend 7.6 cm over the run
//                     and the twin descends exactly the same: that is
//                     the idle pose settling onto the turtle, a
//                     different front, and asserting "must not sink"
//                     would have measured it and called it knockback.
//                     What IS mandatory is that the strike adds nothing
//                     vertical, and that is what is checked.
//     chest x       — ASSERTED to follow the hips. The chest is where
//                     the strike lands, so a chest that moved while the
//                     hips did not (or the reverse) is a body coming
//                     apart, not a stagger.
//     omega         — WAIVED, watched: this whole rig is animation-
//                     driven and its rotations are the clip's, not the
//                     solver's.
//
//   RELATIVE (Argus)
//     separation    — ASSERTED: the boulder reaches the CHEST. This is
//                     the assert the audit's gaps field has been asking
//                     for since 2026-08-14.
//     approach      — ASSERTED: it was closing at its throw speed, so
//                     the contact under test is the one that was aimed.

#include "core/argus.h"
#include "core/particle_system.h"
#include "core/particle_tracer.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) failures++;
}

struct World {
    ParticleSystem ps;
    kg::OntologyRegistry registry;
    kg::KGModule kg{registry};
    PhysicsSystem physics;
    ParticleTracer tracer;
    ParticleDynamicsSystem dyn;
    logosphere::animation::HumanoidLocomotion humanoid;
    int hips_id = -1;

    bool build(float hips_z) {
        if (!physics.initialize(ps) || !dyn.initialize_headless(ps) ||
            !humanoid.initialize_headless(ps, physics, kg, dyn, tracer))
            return false;
        auto spawn = [&](float x, float y, float z, float size,
                         Materials::Type mat) {
            Particle p{};
            p.x = x; p.y = y; p.z = z;
            p.shape = ParticleShape::BOX;
            p.width = p.height = p.thickness = size;
            p.size = size;
            p.SetMaterial(mat);
            int id = ps.queue_particle_addition(p);
            ps.flush_pending_particles();
            return id;
        };
        hips_id    = spawn(0.0f, 0.0f, hips_z, 0.2f, Materials::Type::FLESH);
        int abdomen = spawn(0.0f, 0.0f, hips_z + 0.15f, 0.2f, Materials::Type::FLESH);
        int chest   = spawn(0.0f, 0.0f, hips_z + 0.35f, 0.25f, Materials::Type::FLESH);
        int neck    = spawn(0.0f, 0.0f, hips_z + 0.50f, 0.1f, Materials::Type::FLESH);
        int head    = spawn(0.0f, 0.0f, hips_z + 0.62f, 0.2f, Materials::Type::FLESH);
        std::vector<int> torso = { hips_id, abdomen, chest, neck, head };
        auto limb = [&](float sx, float z0) {
            return std::vector<int>{
                spawn(sx, 0.0f, z0, 0.12f, Materials::Type::FLESH),
                spawn(sx, 0.0f, z0 + 0.35f, 0.12f, Materials::Type::FLESH),
                spawn(sx, 0.0f, z0 + 0.70f, 0.12f, Materials::Type::FLESH) };
        };
        std::vector<int> ll = limb(-0.12f, hips_z - 0.85f);
        std::vector<int> rl = limb( 0.12f, hips_z - 0.85f);
        std::vector<int> la = limb(-0.28f, hips_z - 0.10f);
        std::vector<int> ra = limb( 0.28f, hips_z - 0.10f);
        humanoid.register_humanoid_direct(hips_id, ll, rl, la, ra, torso,
                                          250.0f, 500.0f, kg::INVALID_ENTITY);
        return true;
    }

    // A boulder thrown at the chest from the given direction.
    int throw_boulder(float from_x, float from_y, float z,
                      float vx, float vy) {
        Particle b{};
        b.x = from_x; b.y = from_y; b.z = z;
        b.shape = ParticleShape::BOX;
        b.width = b.height = b.thickness = 0.4f;
        b.size = 0.4f;
        b.vx = vx; b.vy = vy;
        b.SetMaterial(Materials::Type::STONE);
        int id = ps.queue_particle_addition(b);
        ps.flush_pending_particles();
        return id;
    }
};

}  // namespace

int main() {
    printf("\n=== KNOCKBACK: the momentum a braced body refuses ===\n");

    // ---- R1: physics books what it cannot deliver ----------------------
    {
        World w;
        if (!w.build(1.0f)) { printf("  [FAIL] init\n"); return 1; }
        // Short flight, aimed high: over 1 m at 8 m/s the boulder drops
        // ~8 cm, so it must be launched above the chest to arrive AT it.
        // (First draft threw from 2 m at 6 m/s and the boulder sailed
        // under the ribs — 0.54 m of drop — hitting nothing.)
        {
            auto v = w.ps.lock_particles_for_read();
            printf("  [measure] right after registration: chest mode=%d "
                   "owner=%d | hips mode=%d owner=%d\n",
                   (int)v[w.hips_id + 2].solver_mode,
                   (int)v[w.hips_id + 2].owner,
                   (int)v[w.hips_id].solver_mode,
                   (int)v[w.hips_id].owner);
            printf("  [measure] chest mass=%.3f kg quat_driven=%d | "
                   "boulder-to-be mass check pending\n",
                   v[w.hips_id + 2].GetMass(),
                   (int)v[w.hips_id + 2].is_quat_driven);
        }
        const int boulder = w.throw_boulder(-1.0f, 0.0f, 1.44f, 8.0f, 0.0f);
        const int chest = w.hips_id + 2;
        // THE WITNESS. It is here to answer one question the old readout
        // could not: did the boulder ever REACH the chest? A velocity
        // minimum sampled over the whole run cannot, because a boulder
        // that sails past and lands on the turtle is braked by floor
        // friction and reads exactly like one that was stopped by ribs.
        logosphere::Argus argus;
        argus.watch(boulder, "boulder");
        argus.watch(chest, "chest");
        argus.watch(w.hips_id, "hips");
        // Half-extent sum along the strike axis: boulder 0.4, chest 0.25.
        const float REACH = 0.5f * (0.4f + 0.25f);
        float min_sep = 1e9f, peak_approach = 0.0f;
        float vx_at_contact = 8.0f, vx_before_contact = 8.0f;
        float boulder_lateral = 0.0f, boulder_div = 0.0f;
        int   contact_frame = -1;
        float jx = 0, jy = 0, jz = 0;
        bool booked = false;
        float boulder_vx0 = 8.0f;
        for (int f = 0; f < 90; ++f) {
            // THE BROAD PHASE NEEDS A BVH. Without this call
            // bvh->is_ready() is false (physics_system_v4.cpp:1004), the
            // candidate list is empty every frame, and NO box-box contact
            // can exist in this process at all. The first draft of this
            // test omitted it and "measured" a boulder passing through a
            // chest: it lost 0.00074 m/s crossing the full span, which is
            // air drag. A harness that cannot form the event under test
            // manufactures findings.
            w.ps.update_bvh();
            w.physics.update(1.0 / 60.0);
            argus.observe(w.ps, f);
            {   // Sample the strike AT the strike, from the witness.
                const float sep = argus.separation(boulder, chest);
                const float app = argus.approach_speed(boulder, chest);
                if (app > peak_approach) peak_approach = app;
                if (sep >= 0.0f && sep < min_sep) {
                    min_sep = sep;
                    contact_frame = f;
                    const logosphere::Argus::State* prev = argus.previous(boulder);
                    if (prev) vx_before_contact = prev->vx;
                }
                if (const logosphere::Argus::State* s = argus.latest(boulder)) {
                    if (contact_frame == f) vx_at_contact = s->vx;
                    const float ly = std::fabs(s->y);
                    if (ly > boulder_lateral) boulder_lateral = ly;
                }
                const float dv = argus.divergence(boulder);
                if (dv > boulder_div) boulder_div = dv;
            }
            {
                auto v = w.ps.lock_particles_for_read();
                if (getenv("KB_PROBE") && f >= 6 && f <= 14) {
                    const auto& ch = v[w.hips_id + 2];
                    printf("      [f%d] boulder x=%.3f vx=%.2f | chest "
                           "mode=%d x=%.4f vx=%.4f rest=%d\n",
                           f, v[boulder].x, v[boulder].vx,
                           (int)ch.solver_mode, ch.x, ch.vx,
                           (int)ch.is_at_rest);
                }
            }
            // Drain EVERY body and ACCUMULATE. A humanoid books refusals
            // constantly from its own structure — its thigh resting on
            // its KINEMATIC hips books -14.23 N*s downward every frame,
            // which is true and is not a shove. What this rung asks is
            // whether the STRIKE was booked, so it sums the along-strike
            // (+x) component and ignores the structural -z traffic.
            const size_t n = w.ps.lock_particles_for_read().size();
            for (size_t pid = 0; pid < n; ++pid) {
                float ax, ay, az;
                if (w.physics.take_refused_impulse(pid, ax, ay, az)) {
                    if (ax > 0.5f) {
                        jx += ax; jy += ay; jz += az;
                        booked = true;
                        printf("  [measure] strike booked on P%zu: "
                               "(%.2f, %.2f, %.2f) N*s\n", pid, ax, ay, az);
                    }
                }
            }
        }
        const bool reached = min_sep < REACH + 0.02f;
        printf("  [argus] closest the boulder came to the CHEST: %.4f m at "
               "frame %d (their faces meet at %.4f)\n",
               min_sep, contact_frame, REACH);
        printf("  [argus] peak closing speed on the chest %.3f m/s "
               "(thrown at %.3f)\n", peak_approach, boulder_vx0);
        printf("  [argus] boulder vx across the contact: %.3f -> %.3f "
               "(it %s the humanoid)\n",
               vx_before_contact, vx_at_contact, reached ? "REACHED" : "MISSED");
        printf("  [note] that verdict is GEOMETRIC now. The old readout took\n"
               "         the minimum vx over the whole run, which a boulder\n"
               "         that sailed past and was braked by the turtle's\n"
               "         friction downrange satisfies just as well.\n");
        printf("  [argus] boulder lateral drift %.2e m, divergence %.6f rad\n",
               boulder_lateral, boulder_div);
        check(reached,
              "R1: the boulder actually REACHED the chest. Geometry, not a "
              "velocity statistic: closest approach against the two bodies' "
              "half-extents.");
        check(peak_approach > 0.9f * boulder_vx0,
              "R1: and it was closing on the chest at its throw speed, so "
              "the contact under test is the one that was aimed");
        check(boulder_lateral < 1e-3f,
              "R1: the throw stayed in the sagittal plane (a boulder that "
              "curved out of it never had this strike)");
        check(booked, "R1: a strike on a KINEMATIC body is BOOKED, "
                      "not dropped");
        check(booked && std::fabs(jx) > 0.1f,
              "R1: and it points along the strike (+x)");
        check(boulder_div < 0.01f,
              "R1: one body, one orientation, for the striker (G-23)");
    }

    // ---- R2/R3: the writer drains it and the body moves ----------------
    // The locomotion system owns this body's position, so it is the one
    // that must answer the push. R2 asks whether it moved at all; R3 asks
    // whether it moved the RIGHT WAY.
    {
        World w;
        if (!w.build(1.0f)) { printf("  [FAIL] init\n"); return 1; }
        float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f, chest_x0 = 0.0f;
        {
            auto v = w.ps.lock_particles_for_read();
            x0 = v[w.hips_id].x; y0 = v[w.hips_id].y; z0 = v[w.hips_id].z;
            chest_x0 = v[w.hips_id + 2].x;
        }
        const int boulder2 = w.throw_boulder(-1.0f, 0.0f, 1.44f, 8.0f, 0.0f);
        const int chest2 = w.hips_id + 2;
        logosphere::Argus argus;
        argus.watch(w.hips_id, "hips");
        argus.watch(chest2, "chest");
        argus.watch(boulder2, "boulder");
        const float REACH2 = 0.5f * (0.4f + 0.25f);
        float min_sep2 = 1e9f;
        float hips_lateral = 0.0f, hips_sink = 0.0f;
        for (int f = 0; f < 120; ++f) {
            w.humanoid.update_pre_physics(1.0 / 60.0);
            w.ps.update_bvh();
            w.physics.update(1.0 / 60.0);
            w.humanoid.update_post_physics(1.0 / 60.0);
            argus.observe(w.ps, f);
            const float sep = argus.separation(boulder2, chest2);
            if (sep >= 0.0f && sep < min_sep2) min_sep2 = sep;
            if (const logosphere::Argus::State* s = argus.latest(w.hips_id)) {
                const float ly = std::fabs(s->y - y0);
                if (ly > hips_lateral) hips_lateral = ly;
                const float dz = std::fabs(s->z - z0);
                if (dz > hips_sink) hips_sink = dz;
            }
        }
        const logosphere::Argus::State* hs = argus.latest(w.hips_id);
        const logosphere::Argus::State* cs = argus.latest(chest2);
        const float x1 = hs ? hs->x : x0;
        const float moved = x1 - x0;
        const float chest_x = cs ? cs->x : chest_x0;
        const float chest_moved = chest_x - chest_x0;
        printf("  [argus] the boulder reached %.4f m of the chest "
               "(faces meet at %.4f): the strike %s\n",
               min_sep2, REACH2, min_sep2 < REACH2 + 0.02f ? "landed"
                                                          : "NEVER HAPPENED");
        printf("  [measure] hips x: %.3f -> %.3f (moved %+.3f m) | "
               "chest x %+.3f -> %+.3f (moved %+.3f m)\n",
               x0, x1, moved, chest_x0, chest_x, chest_moved);
        printf("  [argus] hips lateral %.2e m, hips vertical %.2e m (see the control below)\n",
               hips_lateral, hips_sink);
        printf("\n  the witness's last two frames:\n");
        argus.dump(std::cout, 2);
        printf("\n");
        check(min_sep2 < REACH2 + 0.02f,
              "R2 fixture: the strike this rung is about actually landed "
              "(without this, R2's red could just be a boulder that missed)");
        check(std::fabs(moved) > 0.02f,
              "R2: the struck humanoid is displaced by the hit");
        check(moved > 0.0f,
              "R3: struck from the west, it staggers EAST");
        check(std::fabs(chest_moved - moved) < 0.02f,
              "R3: and the chest goes with the hips. The strike lands on "
              "the chest, so a chest that moves while the hips do not (or "
              "the reverse) is a body coming apart, not a stagger.");
        check(hips_lateral < 0.01f,
              "R3: a due-west strike produces no north component");

        // THE VERTICAL NEEDS A CONTROL BEFORE IT CAN BE ASSERTED. The
        // struck hips descend 7.6 cm over the run, and the honest
        // question is whether the STRIKE did that. An unstruck twin,
        // same rig, same frames, no boulder, answers it. Asserting
        // "it must not sink" without this control would be measuring
        // the idle pose's own ground settle and calling it knockback.
        World c;
        if (!c.build(1.0f)) { printf("  [FAIL] control init\n"); return 1; }
        logosphere::Argus cargus;
        cargus.watch(c.hips_id, "hips_control");
        for (int f = 0; f < 120; ++f) {
            c.humanoid.update_pre_physics(1.0 / 60.0);
            c.ps.update_bvh();
            c.physics.update(1.0 / 60.0);
            c.humanoid.update_post_physics(1.0 / 60.0);
            cargus.observe(c.ps, f);
        }
        const logosphere::Argus::State* ch = cargus.latest(c.hips_id);
        const float cz = ch ? ch->z : z0;
        const float cx = ch ? ch->x : x0;
        const float struck_z = hs ? hs->z : z0;
        printf("  [measure] CONTROL, same rig with no boulder: hips end at "
               "x %+.4f z %.4f (struck twin: x %+.4f z %.4f)\n",
               cx, cz, x1, struck_z);
        printf("  [note] the 7.6 cm descent is the idle pose settling onto\n"
               "         the turtle, not the strike: the unstruck twin does\n"
               "         it too. That is why the vertical claim below is\n"
               "         about the DIFFERENCE and not about zero.\n");
        check(std::fabs(struck_z - cz) < 0.005f,
              "R3: the strike adds no vertical motion. Measured against an "
              "unstruck twin, not against zero: the rig's own idle settle "
              "is 7.6 cm and belongs to a different front.");
        check(std::fabs(cx - x0) < 1e-4f,
              "R3 control: an unstruck humanoid does not wander east on "
              "its own, so R2's red is the strike's absence and not a "
              "drift this test cannot see");
    }

    printf("\n  %s (%d failures)\n",
           failures == 0 ? "KNOCKBACK OK" : "KNOCKBACK INCOMPLETE", failures);
    return failures == 0 ? 0 : 1;
}
