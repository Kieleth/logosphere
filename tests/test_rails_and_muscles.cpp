// =============================================================================
// RAILS AND MUSCLES (INV-40) - the headless half
// =============================================================================
// G-81 the harness, G-82 a muscle under weight, G-83 a replanted foot is a
// new rail. Scene and stepping in tests/scenes/scene_rails_and_muscles.h,
// shared with the window. BORN RED by the law's registration (2026-09-08):
// the writer still moves every drive child by hand after the solver, the
// gravity pass exempts any body wearing the drive children's flags, and no
// writer declares a jump. Predictions are read from the code; this run
// measures them.
//
//   ./build/test_rails_and_muscles                         numbers
//   INV40_STEP=n ./build/test_rails_and_muscles            with n deletion steps applied
//   KINEMATIC_LEDGER=1 ./build/test_rails_and_muscles     the G-83 ledger half
//   INTERACTIVE=1 ./build/test_rails_and_muscles_visual   window
// =============================================================================
// FULL-STATE NARRATION (assert or waive, per DOF).
//   MUSCLES (20 drive children, DYNAMIC): position, velocity, orientation,
//     angular velocity: ASSERTED as 'no hand but the solver's' through the
//     tracer (every record is an outside write: G-81). Their pose against the
//     clip: WAIVED here, the drive walk tests hold it; the walk gauge below
//     keeps this test from passing by leaving the limbs behind.
//   HIPS (rail): forward progress ASSERTED at the drive walk gauge's bar;
//     the sites that write it are NARRATED and WAIVED: a rail's writer may
//     have several internal steps, the law asks for one mechanism.
//   ANCHORS (rails, born on the first plant): jumps ASSERTED as declared
//     (G-83); the ledger speed after a jump ASSERTED quiet, a claim made for
//     KINEMATIC_LEDGER=1 (by default the ledger reads zero and the assert is
//     vacuous, said so on the line).
//   THE BODY (every rig particle): every nail's two attachment points
//     ASSERTED together within 10 SLOP over the run (INV-28 / INV-22); every
//     bone ASSERTED within its standing reach + 0.15 m of the hips (G-81:
//     the rows carry the body; a limb that walks off is a red line, not a
//     thing only the eye catches). Owner ask, 2026-09-08: "add a check to
//     make sure all parts of the body are where they are supposed to be".
//   BOX A (DYNAMIC, drive-child flags, unsupported): z ASSERTED as falling
//     (G-82); x, y, orientation: no lateral force, no torque; WAIVED.
//   BOX B (KINEMATIC): z ASSERTED as still (INV-1 as rewritten); rest WAIVED.
//   ARM (DYNAMIC drive child on a nail): orientation ASSERTED against its
//     target (INV-13), separation from the post ASSERTED (the nail is rigid);
//     x, y, z otherwise follow the nail; WAIVED.
#include "scenes/scene_rails_and_muscles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
using namespace scene_rails_and_muscles;
namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what.c_str());
    if (!ok) failures++;
}
}
int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const bool ledger = std::getenv("KINEMATIC_LEDGER") != nullptr;
    std::printf("\n=== rails and muscles (INV-40) %s INV40_STEP=%d ===\n", ledger ? "[KINEMATIC_LEDGER=1]" : "[default]", Scene::inv40_step());
    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "rails and muscles";
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) != 0) { std::printf("  [FAIL] init\n"); return 1; }
    Scene scene;
    scene.build(engine);
    std::printf("  cast: %zu muscles (drive children), hips P%d as the rail; station B at x=%.1f\n",
                scene.muscles.size(), scene.hips, B_X);
    const bool diag = std::getenv("RAILS_DIAG") != nullptr;
    for (int f = 0; f < RUN_FRAMES; ++f) {
        scene.step(engine, f);
        if (diag && f < 120 && f % 5 == 0) {           // the head: rows, contacts, hands, motion
            auto& tracer = engine.get_particle_tracer();
            auto& physics = engine.get_physics_system();
            const int head = scene.eva.head_id;
            float hx = 0, hy = 0, hz = 0, hv = 0; int mode = -1;
            {
                auto v = engine.get_particle_system().lock_particles_for_read();
                if (head >= 0 && (size_t)head < v.size()) {
                    const Particle& p = v[head]; hx = p.x; hy = p.y; hz = p.z;
                    hv = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz); mode = (int)p.solver_mode;
                }
            }
            const auto gl = physics.get_gluons_for_particle((size_t)head);
            if (f == 10) {   // the head's cast, once: who is nailed to it, who rides it, and their modes
                std::printf("  [head cast] nailed to P%d:", head);
                for (const auto* g : gl) if (g) std::printf(" P%zu", g->particle_a == (size_t)head ? g->particle_b : g->particle_a);
                if (const auto* parts = engine.get_humanoid_locomotion().get_humanoid_parts(scene.hips)) {
                    auto v = engine.get_particle_system().lock_particles_for_read();
                    std::printf("\n  [head cast] rides the head:");
                    for (unsigned int c : parts->head_child_particles)
                        std::printf(" P%u(%s,q%d,o%d)", c, v[c].solver_mode == ParticleSolverMode::KINEMATIC ? "KIN" : "DYN", (int)v[c].is_quat_driven, (int)v[c].owner);
                }
                std::printf("\n");
            }
            std::string contacts;
            int nc = 0;
            for (const auto& e : physics.get_collision_events()) {
                if ((int)e.particle_a != head && (int)e.particle_b != head) continue;
                ++nc;
                if (nc <= 4) {
                    char b[96]; std::snprintf(b, sizeof(b), " [%zu<>%zu pen %.4f n(%.2f,%.2f,%.2f)]", e.particle_a, e.particle_b, e.penetration, e.normal_x, e.normal_y, e.normal_z);
                    contacts += b;
                }
            }
            std::string hands_on_head;
            for (const auto& r : tracer.records()) if (r.particle_id == head && Scene::is_state_field(r.field)) { hands_on_head += " "; hands_on_head += r.site; }
            std::printf("  [head f%3d] P%d mode %d pos (%.3f,%.3f,%.3f) |v| %.3f gluons %zu contacts %d%s hands:%s\n",
                        f, head, mode, hx, hy, hz, hv, gl.size(), nc, contacts.c_str(), hands_on_head.empty() ? " none" : hands_on_head.c_str());
        }
        if (diag && (f < 3 || f == 20 || f == 60)) {    // who is traced, and every record this frame
            auto& tracer = engine.get_particle_tracer();
            std::printf("  [diag f%d] muscles:", f);
            for (int id : scene.muscles) std::printf(" %d%s", id, tracer.is_traced(id) ? "" : "(untraced)");
            std::printf("\n  [diag f%d] eva legs L:", f);
            for (int id : scene.eva.left_leg_ids) std::printf(" %d", id);
            std::printf(" R:"); for (int id : scene.eva.right_leg_ids) std::printf(" %d", id);
            std::printf(" hips %d\n", scene.hips);
            {   // the live bones' modes, and the hierarchy's own child ids
                auto v = engine.get_particle_system().lock_particles_for_read();
                std::printf("  [diag f%d] leg modes:", f);
                for (int id : scene.eva.left_leg_ids) {
                    const Particle& p = v[id];
                    std::printf(" %d:%s/q%d/o%d", id, p.solver_mode == ParticleSolverMode::KINEMATIC ? "KIN" : (p.solver_mode == ParticleSolverMode::DYNAMIC ? "DYN" : "STA"), (int)p.is_quat_driven, (int)p.owner);
                }
                std::printf("\n");
                if (const auto* parts = engine.get_humanoid_locomotion().get_humanoid_parts(scene.hips)) {
                    std::printf("  [diag f%d] hierarchy children:", f);
                    for (const auto& j : parts->joint_hierarchy.joints) std::printf(" %s=%u", j.name.c_str(), j.child_particle);
                    std::printf("\n  [diag f%d] anchors L %d R %d engaged %d\n", f, parts->left_plant_anchor_id, parts->right_plant_anchor_id, parts->plant_anchor_particle_id);
                }
            }
            std::map<std::string, int> by;
            for (const auto& r : tracer.records()) by[std::to_string(r.particle_id) + " " + r.site]++;
            std::printf("  [diag f%d] records this frame: %zu\n", f, tracer.records().size());
            for (const auto& [k, n] : by) std::printf("      %s x%d\n", k.c_str(), n);
        }
        if (f % 30 == 29) {
            const auto* A = scene.argus.latest(scene.box_a);
            std::printf("  [f%3d] fwd %+6.3f back %2d | hands %5d recs, %3d frames (%s) | replants %d declared %d loud %d ledger %.2f m/s"
                        " | A z %.3f drop %.3f | B drift %.4f | arm err %.4f sep drift %.5f | body: gap %.4f (%s) reach over %+.3f (%s)\n",
                        f, scene.forward, scene.backward_frames, scene.hand_records, scene.frames_with_hands,
                        scene.hands_summary().c_str(), scene.replants, scene.replants_declared, scene.replants_ledger_loud,
                        scene.replant_ledger_speed_max, A ? A->z : 0.0f, scene.a_drop_max, scene.b_drift_max,
                        scene.arm_err_max, scene.arm_sep_drift_max,
                        scene.joint_gap_max, scene.joint_gap_worst.c_str(), scene.reach_over_max, scene.reach_worst.c_str());
        }
    }
    std::printf("\n  [measure] drive children: %zu, of which %d STALE (no joint names them)", scene.muscles.size(), scene.muscles_stale);
    std::printf("\n  [measure] hands on muscles: %d records over %d of %d frames; sites: %s\n",
                scene.hand_records, scene.frames_with_hands, RUN_FRAMES, scene.hands_summary(8).c_str());
    std::string rail;
    for (const auto& [site, n] : scene.rail_hands) rail += (rail.empty() ? "" : ", ") + site + " " + std::to_string(n);
    std::printf("  [measure] hands on the hips rail (narrated, waived): %s\n", rail.empty() ? "none" : rail.c_str());
    std::printf("  [measure] walk: forward %.3f of %.3f m, %d backward frames\n",
                scene.forward, RUN_FRAMES * DT * WALK_SPEED, scene.backward_frames);
    std::printf("  [measure] anchors: %d replants, %d declared, %d loud in the ledger (max %.2f m/s, bar %.2f)\n",
                scene.replants, scene.replants_declared, scene.replants_ledger_loud, scene.replant_ledger_speed_max, JUMP_LEDGER_MAX);
    std::printf("  [measure] the body: %zu bones; worst nail gap %.4f m at %s (frame %d, bar %.4f); worst reach over standing + %.2f m: %+.3f m at %s\n",
                scene.rig.size(), scene.joint_gap_max, scene.joint_gap_worst.c_str(), scene.joint_gap_frame, JOINT_GAP_MAX,
                REACH_SLACK, scene.reach_over_max, scene.reach_worst.c_str());
    std::printf("  [measure] worst nails: %s\n", scene.nails_summary().c_str());
    std::printf("  [measure] box A drop max %.3f m (bar > %.3f by frame %d); box B drift %.4f m; arm pose err max %.4f rad, sep drift %.5f m\n\n",
                scene.a_drop_max, FALL_MIN, FALL_FRAMES, scene.b_drift_max, scene.arm_err_max, scene.arm_sep_drift_max);

    check(Scene::muscles_live(scene.muscles_stale),                   "hygiene/INV-35: the engine's drive children are live bodies (ids follow the swaps)");
    check(Scene::hands_off(scene.hand_records),                       "INV-40/INV-35/G-81: no hand but the solver's on any muscle, any frame");
    check(Scene::walks(scene.forward, scene.walk_frames),             "G-81 gauge (drive walk bar): the limbs follow the rail and she walks");
    check(Scene::holds_together(scene.joint_gap_max),                 "INV-28/INV-22: every nail of the body holds its two attachment points together (gap <= 10 SLOP)");
    check(Scene::whole(scene.reach_over_max),                         "G-81/INV-22: every bone is within its standing reach of the hips (the body is whole)");
    check(Scene::fell(scene.a_drop_max),                              "INV-40/INV-15/G-82: a DYNAMIC body wearing the muscles' flags FALLS");
    check(Scene::stayed(scene.b_drift_max),                           "INV-1/G-82: a rail stays where its writer prescribes");
    check(Scene::holds(scene.arm_err_max, scene.arm_sep_drift_max),   "INV-13/G-82: the driven arm holds its commanded pose on its nail");
    check(Scene::declared(scene.replants, scene.replants_declared),   "INV-40/G-83: every replant is DECLARED by its writer (rail.jump)");
    check(Scene::ledger_quiet(scene.replants, scene.replants_ledger_loud),
          std::string("INV-39/INV-11/G-83: the ledger never reads a replant as a velocity") + (ledger ? "" : " [default: no ledger, vacuous]"));

    std::printf("\n  %s\n", failures == 0 ? "TWO RAILS AND TWENTY MUSCLES" : "RED: INV-40");
    engine.shutdown();
    return failures == 0 ? 0 : 1;
}
