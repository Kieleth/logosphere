// =============================================================================
// ONE BLADE, ONE HUMAN: what truly happens at contact
// =============================================================================
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_single_blade_contact --no-head
//
// THE OWNER'S REDUCTION, verbatim intent: "just put a single blade of grass
// that the human hits, and let's study what truly happens at contact, then we
// decide." The decision this study feeds, in the owner's words: grass should
// bend because of the physics the human applies to it, the blade's gluons
// should CARRY that bending, and properties should decide the character:
// stiff grass springs back fast, soft grass recovers slowly, some stays flat.
// Organically varied, but from properties, not scripts.
//
// So this file is a measurement, not a fix. One bonded blade, grown tall
// enough to meet her thigh. Eva walks straight through it once. Every bond of
// the blade is tracked INDIVIDUALLY, every frame, through three phases:
//
//   APPROACH   she is not touching it. Bonds at rest: the baseline, and the
//              proof the instrument reads zero when nothing happens.
//   CONTACT    her body meets the blade. Per frame: how many contacts, what
//              relative speed, how far each bond stretches, how far the TIP
//              deflects from where it grew. This is "what truly happens".
//   RECOVERY   she is past. The tip's return is timed: peak deflection, and
//              the half-life of its decay. TODAY's recovery comes from
//              OrganicGluon stiffness/damping alone; the half-life number is
//              what a future per-species property would tune, and a blade
//              that never returns is today's version of "stays flat".
//
// Headless prints the whole timeline as a table plus the recovery analysis.
// Interactive shows the encounter close-up with live per-phase labels.
// Diagnostic PASS: the study is the deliverable. Self-checks only: the blade
// must exist, be bonded, and actually get hit, or the run says so and fails.
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "../src/ui/ui_system.h"
#include "../src/ui/widgets.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/organic_spec.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <array>
#include <map>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <GLFW/glfw3.h>
#endif

namespace X = ::logosphere::expdet;

bool test_single_blade_contact() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    printf("\n=== ONE BLADE, ONE HUMAN: THE CONTACT, STUDIED ===\n");
    printf("  mode: %s\n", interactive ? "INTERACTIVE (ESC quits)" : "HEADLESS");

    EngineConfig cfg;
    cfg.create_display = interactive;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  ERROR: engine init failed\n"); return false; }

    {   // ontology, the sanctioned way
        kg::OntologyRegistry reg;
        reg.addEntityType("Grass", "Plant", false);
        reg.addAncestors("Grass", {"Plant", "LivingEntity", "WorldEntity", "Entity"});
        engine.get_kg().extendOntology(reg);
    }

    auto& ps = engine.get_particle_system();
    // Just enough floor for her four steps. Ground is a subject: no floor, no walk.
    for (int x = -1; x <= 1; ++x)
        for (int y = -2; y <= 8; ++y) {   // long enough that she never
                                           // walks off the world's edge: the
                                           // first version ended at y=4 and her
                                           // own body detonated on the rim
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = (float)x; p.y = (float)y; p.z = 0.05f;
            p.width = p.height = 1.0f; p.thickness = 0.1f; p.size = 1.0f;
            p.r = 0.35f; p.g = 0.32f; p.b = 0.28f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            int id = engine.add_particle(p);
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
        }
    ps.queue_light(-2.0f, 0.0f, 10.0f, 350000.0f, 40.0f, 1.0f, 0.96f, 0.9f);
    ps.flush_pending_particles();

    // ONE blade, on her line, tall enough to meet her thigh (spec height up
    // from the default 0.4 so the encounter is unmissable both to her body
    // and to the camera).
    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    std::set<size_t> blade_ids;
    size_t blade_gluons = 0;
    {
        const size_t b0 = [&] { auto v = ps.lock_particles_for_write(); return v.size(); }();
        const size_t g0 = engine.get_physics_system().get_total_gluon_count();
        OrganicSpec spec = OrganicSpec::grass_blade();
        spec.height = 1.0f;            // thigh-high, one clear subject
        spec.random_seed = 4242;       // deterministic study
        // x = 0.10, IN A LEG'S PATH. At x=0 the 9 mm stem sits exactly on her
        // centreline and she STRADDLES it: legs swing at x = +/-0.10 and the
        // first version recorded zero contacts because none happened. A study
        // of a contact begins by arranging for the contact to exist.
        kg::EntityID blade = ogen.generate_grass(0.10f, 1.5f, 0.1f, spec);
        scene.activate_entity_now(blade);
        ps.flush_pending_particles();
        blade_gluons = engine.get_physics_system().get_total_gluon_count() - g0;
        auto v = ps.lock_particles_for_write();
        for (size_t i = b0; i < v.size(); ++i) blade_ids.insert(i);
    }

    // The blade's anatomy, captured at rest: every bond with its rest length,
    // and the TIP (highest particle) with its grown position.
    // Orientation instrumentation. A particle's long axis is local +Z rotated
    // by its Euler triple (generation guarantees this: direction_to_euler maps
    // local +Z onto the grown segment direction, X-then-Y rotation, CCW —
    // organic_generator.cpp:345-349). The SHEAR angle of a bond is how far the
    // parent-to-child direction has rotated away from the parent's own axis,
    // relative to the grown baseline. A blade that BENDS keeps shear near zero
    // (pose rotates with geometry). A blade that only TRANSLATES converts all
    // bending into shear, because physics never writes rotation_x/y of a
    // default DYNAMIC particle and contacts carry no torque at all.
    auto axis_of = [](const Particle& p) {
        // local +Z through R = Rz(-rz) * Ry(ry) * Rx(rx), X-then-Y-then-Z
        // (Z clockwise per engine convention; stems carry rz ~ 0 anyway).
        const float cx = std::cos(p.rotation_x), sx = std::sin(p.rotation_x);
        const float cy = std::cos(p.rotation_y), sy = std::sin(p.rotation_y);
        const float cz = std::cos(-p.rotation_z), sz = std::sin(-p.rotation_z);
        // v = (0,0,1) -> Rx -> (0,-sx,cx) -> Ry -> (cy*0+sy*cx, -sx, -sy*0+cy*cx)
        float x = sy * cx, y = -sx, z = cy * cx;
        // -> Rz
        const float x2 = cz * x - sz * y, y2 = sz * x + cz * y;
        return std::array<float,3>{x2, y2, z};
    };
    auto angle_between = [](std::array<float,3> u, std::array<float,3> v2) {
        const float du = std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
        const float dv = std::sqrt(v2[0]*v2[0]+v2[1]*v2[1]+v2[2]*v2[2]);
        if (du < 1e-9f || dv < 1e-9f) return 0.0f;
        float c = (u[0]*v2[0]+u[1]*v2[1]+u[2]*v2[2]) / (du*dv);
        c = std::fmax(-1.0f, std::fmin(1.0f, c));
        return std::acos(c);
    };

    struct Bond { size_t a, b; float rest; float base_shear; };
    std::vector<Bond> bonds;
    struct GrownPose { float rx, ry, rz; };
    std::map<size_t, GrownPose> grown_pose;
    size_t tip_id = 0; float tip_x0 = 0, tip_y0 = 0, tip_z0 = -1.0f;
    {
        auto v = ps.lock_particles_for_write();
        std::set<std::pair<size_t,size_t>> seen;
        for (size_t id : blade_ids) {
            grown_pose[id] = {v[id].rotation_x, v[id].rotation_y, v[id].rotation_z};
            if (v[id].z > tip_z0) { tip_z0 = v[id].z; tip_id = id; tip_x0 = v[id].x; tip_y0 = v[id].y; }
            for (const auto* g : engine.get_physics_system().get_gluons_for_particle(id)) {
                auto key = std::minmax(g->particle_a, g->particle_b);
                if (!seen.insert(key).second) continue;
                const Particle& pa = v[g->particle_a];
                const Particle& pb = v[g->particle_b];
                const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
                // particle_a is the parent (generation bonds parent -> child).
                const float base = angle_between(axis_of(pa), {dx, dy, dz});
                bonds.push_back({g->particle_a, g->particle_b,
                                 std::sqrt(dx*dx + dy*dy + dz*dz), base});
            }
        }
    }
    printf("  blade: %zu particles, %zu gluons, %zu bonds tracked, tip P%zu at (%.2f, %.2f, %.2f)\n",
           blade_ids.size(), blade_gluons, bonds.size(), tip_id, tip_x0, tip_y0, tip_z0);
    if (blade_ids.empty() || bonds.empty()) {
        printf("\n  *** NO BONDED BLADE (%zu particles, %zu bonds): nothing to study.\n\n  FAIL\n",
               blade_ids.size(), bonds.size());
        engine.shutdown();
        return false;
    }

    HumanoidGenerator hgen;
    hgen.initialize(&engine, &engine.get_kg());
    HumanoidSpec hspec = HumanoidSpec::hunter();
    hspec.facing_angle = 0.0f;
    auto eva = hgen.generate_humanoid_physics(0.0f, -1.0f, 0.1f, -1, hspec, false);
    std::set<size_t> eva_ids;
    {
        auto add = [&](const std::vector<int>& v2) { for (int i : v2) eva_ids.insert((size_t)i); };
        eva_ids.insert((size_t)eva.hips_id);
        add(eva.left_leg_ids); add(eva.right_leg_ids);
        add(eva.left_arm_ids); add(eva.right_arm_ids); add(eva.torso_ids);
    }
    auto& loco = engine.get_humanoid_locomotion();
    loco.register_humanoid_direct(eva.hips_id,
                                  eva.left_leg_ids, eva.right_leg_ids,
                                  eva.left_arm_ids, eva.right_arm_ids,
                                  eva.torso_ids, 150.0f, 600.0f);
    loco.reset_humanoid_position(eva.hips_id);
    loco.set_volitional(eva.hips_id, true);
    loco.set_target_velocity(eva.hips_id, 0.0f, 1.2f);

    if (interactive) {
        auto& camera = engine.get_camera_system();
        camera.set_position(-4.0f, 0.0f, 2.2f);
        camera.look_at(0.0f, 1.5f, 0.7f);      // the blade, waist height
        camera.set_pixels_per_unit(140.0f);    // close: one blade is the frame
    }
    ui::Label *l_title = nullptr, *l_live = nullptr, *l_bonds = nullptr;
    if (auto* uis = engine.get_ui_system()) {
        auto mk = [&](int y, uint8_t r, uint8_t g, uint8_t b) {
            auto* L = new ui::Label("", "");
            L->set_position(24, y); L->set_size(1500, 22); L->set_color(r, g, b);
            uis->add_widget(L);
            return L;
        };
        l_title = mk(24, 255, 255, 255);
        l_live  = mk(54, 120, 220, 255);
        l_bonds = mk(84, 110, 235, 130);
    }

    X::set_enabled(true);
    X::reset();

    // The encounter. Phases detected from data, never scripted: CONTACT is the
    // first frame an Eva-vs-blade collision event exists; RECOVERY starts when
    // contacts have been absent for 30 straight frames.
    const int FRAMES = 600;
    enum Phase { APPROACH, CONTACT, RECOVERY };
    Phase phase = APPROACH;
    int contact_start = -1, recovery_start = -1, frames_since_contact = 0;
    uint64_t contacts_total = 0;
    double worst_rel_speed = 0.0;
    double peak_tip = 0.0, peak_bond = 0.0, worst_blade_speed = 0.0;
    double peak_shear = 0.0, peak_pose_rot = 0.0;
    int    peak_tip_frame = -1;
    double tip_deflect = 0.0;
    int half_life_frames = -1;

    // Timeline rows for the headless table: sampled every 15 frames plus
    // every phase change.
    struct Row { int f; Phase ph; float eva_y; double tip, bond, shear, pose; uint64_t c; };
    std::vector<Row> table;

    for (int f = 0; f < FRAMES; ++f) {
        engine.update(1.0 / 60.0);

        float eva_y;
        {
            auto v = ps.lock_particles_for_write();
            eva_y = v[eva.hips_id].y;
            const float dx = v[tip_id].x - tip_x0, dy = v[tip_id].y - tip_y0,
                        dz = v[tip_id].z - tip_z0;
            tip_deflect = std::sqrt(dx*dx + dy*dy + dz*dz);
        }

        uint64_t contacts_now = 0;
        for (const auto& ev : engine.get_physics_system().get_collision_events()) {
            const bool a_eva = eva_ids.count(ev.particle_a) > 0;
            const bool b_eva = eva_ids.count(ev.particle_b) > 0;
            const bool a_bl  = blade_ids.count(ev.particle_a) > 0;
            const bool b_bl  = blade_ids.count(ev.particle_b) > 0;
            if ((a_eva && b_bl) || (b_eva && a_bl)) {
                contacts_now++;
                worst_rel_speed = std::fmax(worst_rel_speed,
                                            (double)std::fabs(ev.relative_velocity));
            }
        }
        contacts_total += contacts_now;

        double bond_stretch = 0.0, shear_now = 0.0, pose_rot_now = 0.0;
        {
            // Torn bonds drift arbitrarily; shear is only meaningful on the
            // joints that still exist. Re-enumerate the survivors each frame.
            std::set<std::pair<size_t,size_t>> alive;
            for (size_t id : blade_ids)
                for (const auto* g : engine.get_physics_system().get_gluons_for_particle(id))
                    alive.insert(std::minmax(g->particle_a, g->particle_b));

            auto v = ps.lock_particles_for_write();
            for (const Bond& b : bonds) {
                if (b.a >= v.size() || b.b >= v.size()) continue;
                const float dx = v[b.b].x - v[b.a].x, dy = v[b.b].y - v[b.a].y,
                            dz = v[b.b].z - v[b.a].z;
                bond_stretch = std::fmax(bond_stretch,
                    std::fabs(std::sqrt(dx*dx + dy*dy + dz*dz) - b.rest));
                if (alive.count(std::minmax(b.a, b.b))) {
                    const float sh = angle_between(axis_of(v[b.a]), {dx, dy, dz});
                    shear_now = std::fmax(shear_now,
                                          (double)std::fabs(sh - b.base_shear));
                }
            }
            // How much has any segment's POSE actually rotated since growth?
            for (const auto& [id, g0] : grown_pose) {
                if (id >= v.size()) continue;
                Particle ref;  // grown pose replayed through the same axis math
                ref.rotation_x = g0.rx; ref.rotation_y = g0.ry; ref.rotation_z = g0.rz;
                const auto now = axis_of(v[id]);
                const auto was = axis_of(ref);
                pose_rot_now = std::fmax(pose_rot_now,
                                         (double)angle_between(was, now));
            }
            // Worst BLADE speed: her limbs legitimately swing at gait speed,
            // so the fired/carried verdict must read the blade alone.
            for (size_t id : blade_ids) {
                if (id >= v.size()) continue;
                const double sp = std::sqrt((double)v[id].vx * v[id].vx +
                                            (double)v[id].vy * v[id].vy +
                                            (double)v[id].vz * v[id].vz);
                worst_blade_speed = std::fmax(worst_blade_speed, sp);
            }
        }

        // Phase machine, from data.
        Phase before = phase;
        if (phase == APPROACH && contacts_now > 0) { phase = CONTACT; contact_start = f; }
        if (phase == CONTACT) {
            frames_since_contact = contacts_now ? 0 : frames_since_contact + 1;
            if (frames_since_contact >= 30) { phase = RECOVERY; recovery_start = f; }
        }
        if (phase != APPROACH) {
            if (tip_deflect > peak_tip) { peak_tip = tip_deflect; peak_tip_frame = f; }
            peak_bond = std::fmax(peak_bond, bond_stretch);
            peak_shear = std::fmax(peak_shear, shear_now);
            peak_pose_rot = std::fmax(peak_pose_rot, pose_rot_now);
        }
        if (phase == RECOVERY && half_life_frames < 0 && peak_tip > 0.02 &&
            tip_deflect <= peak_tip * 0.5)
            half_life_frames = f - recovery_start;

        if (f % 15 == 0 || phase != before)
            table.push_back({f, phase, eva_y, tip_deflect, bond_stretch,
                             shear_now, pose_rot_now, contacts_now});

        if (interactive) {
            static const char* PH[3] = {"APPROACH", "CONTACT", "RECOVERY"};
            if (l_title) {
                char buf[192];
                snprintf(buf, sizeof(buf),
                         "ONE BLADE vs ONE HUMAN   phase: %s   (bend from physics; bonds carry it)",
                         PH[phase]);
                l_title->set_text(buf);
            }
            if (l_live) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "f %d  Eva y=%5.2f  contacts now %llu  worst rel speed %4.1f m/s  detector %llu",
                         f, eva_y, (unsigned long long)contacts_now, worst_rel_speed,
                         (unsigned long long)X::stats().speed_events);
                l_live->set_text(buf);
            }
            if (l_bonds) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "tip deflection %5.3f m (peak %5.3f)   worst bond stretch %5.3f m (peak %5.3f)",
                         tip_deflect, peak_tip, bond_stretch, peak_bond);
                l_bonds->set_text(buf);
            }
            engine.render();
            engine.present();
            const auto& input = engine.get_input_system();
            if (input.get_input_state().keys[GLFW_KEY_ESCAPE]) break;
            if (!engine.is_running()) break;
        }
    }
    loco.set_target_velocity(eva.hips_id, 0.0f, 0.0f);

    // ---- the study, printed --------------------------------------------------
    static const char* PH[3] = {"APPROACH", "CONTACT", "RECOVERY"};
    printf("\n  %6s %-9s %7s %10s %10s %9s %9s %9s\n",
           "frame", "phase", "eva_y", "tip_defl", "bond_str",
           "shear_deg", "pose_deg", "contacts");
    for (const Row& r : table)
        printf("  %6d %-9s %7.2f %10.3f %10.3f %9.1f %9.1f %9llu\n",
               r.f, PH[r.ph], r.eva_y, r.tip, r.bond,
               r.shear * 180.0 / M_PI, r.pose * 180.0 / M_PI,
               (unsigned long long)r.c);

    const X::Stats s = X::stats();
    printf("\n  THE ENCOUNTER, SUMMARISED\n");
    printf("  %-46s %d\n", "first contact at frame", contact_start);
    printf("  %-46s %llu (worst relative speed %.1f m/s)\n", "total contact events",
           (unsigned long long)contacts_total, worst_rel_speed);
    printf("  %-46s %.3f m at frame %d\n", "peak tip deflection", peak_tip, peak_tip_frame);
    printf("  %-46s %.3f m\n", "peak bond stretch (the gluons carrying it)", peak_bond);
    printf("  %-46s %.1f deg\n", "peak SHEAR (geometry rotated away from pose)",
           peak_shear * 180.0 / M_PI);
    printf("  %-46s %.1f deg\n", "peak POSE rotation (any segment, any axis)",
           peak_pose_rot * 180.0 / M_PI);
    if (recovery_start >= 0) {
        printf("  %-46s frame %d\n", "recovery began", recovery_start);
        if (half_life_frames >= 0)
            printf("  %-46s %d frames (%.2f s): today's spring-back, set by\n"
                   "  %-46s stiffness 5000 / damping 100 alone\n",
                   "tip deflection half-life", half_life_frames, half_life_frames / 60.0, "");
        else
            printf("  %-46s NOT within the run: today's version of a blade\n"
                   "  %-46s that 'stays flat' (final deflection %.3f m)\n",
                   "tip half-life", "", tip_deflect);
    } else {
        printf("  recovery never began: contact never ended within %d frames.\n", FRAMES);
    }
    printf("  %-46s %llu (worst %.1f m/s, worst id P%d %s)\n", "detector events",
           (unsigned long long)s.speed_events, s.worst_speed, s.worst_id,
           s.worst_id >= 0 && eva_ids.count((size_t)s.worst_id) ? "= EVA PART"
           : (s.worst_id >= 0 && blade_ids.count((size_t)s.worst_id) ? "= BLADE PART" : "= other"));

    printf("\n  WHAT THIS FEEDS (the owner's design): bend character should come from\n"
           "  properties: stiff springs back fast, soft recovers slowly, some stays\n"
           "  flat, organically varied. The numbers above are the BASELINE those\n"
           "  properties would move. Decide the mechanism on this data.\n");

    // How many bonds survived her passage? A torn joint is a pair whose
    // gluon no longer exists; count survivors against the start roster.
    size_t bonds_alive = 0;
    {
        std::set<std::pair<size_t,size_t>> alive;
        for (size_t id : blade_ids)
            for (const auto* g : engine.get_physics_system().get_gluons_for_particle(id))
                alive.insert(std::minmax(g->particle_a, g->particle_b));
        for (const Bond& b : bonds)
            if (alive.count(std::minmax(b.a, b.b))) bonds_alive++;
    }
    printf("  %-46s %zu of %zu%s\n", "bonds still holding at the end",
           bonds_alive, bonds.size(),
           bonds_alive == bonds.size() ? "" : "  (the rest TORE)");

    // ====================================================================
    // THE GATE (owner: A+B). The study measured what happens today: a 2 g
    // sheet leaves at 675 m/s, bonds stretch to 6 m, nothing bends, nothing
    // recovers. These assertions are the owner's design, encoded; they are
    // RED until mechanisms A (contact bounded by capture) and B (bonds that
    // bend, tear, and remember) exist.
    // ====================================================================
    const bool got_hit   = contact_start >= 0;
    // A blade piece may be carried at roughly her contact speed; it may not
    // be FIRED. 3x the worst relative contact speed is carrying with slack.
    const bool no_fling  = worst_blade_speed <= std::fmax(3.0 * worst_rel_speed, 6.0);
    // Bending means the tip MOVED but stayed on the plant: deflection real
    // (> 5 cm) and bounded (< 1.5 m, its own height and a half).
    const bool bends     = peak_tip > 0.05 && peak_tip < 1.5;
    // The bonds carry the bend without becoming slingshots: stretch stays
    // under 3x the longest rest length (~0.1 m), not 60x.
    const bool carries   = peak_bond < 0.3;
    // And the world does not detonate over a footstep.
    const bool calm      = s.speed_events == 0;
    // The owner's observation, encoded (2026-08-09): "blades moved but not
    // rotate". A blade keeps its entity and its gluon placement via ROTATION
    // and translation. Bending must live in the pose, not in shear: the bond
    // direction may not rotate away from the parent's own axis by more than
    // 15 degrees, and when the geometry really bends the pose must have
    // turned too. RED today: physics never writes rotation_x/y of a default
    // particle, contacts carry no torque, so ALL bending is shear.
    const bool rotates   = peak_shear < (15.0 * M_PI / 180.0) &&
                           (peak_tip < 0.15 ||
                            peak_pose_rot > (10.0 * M_PI / 180.0));

    printf("\n  THE GATE (A+B, the owner's design)\n");
    printf("  %-52s %s\n", "contact happened", got_hit ? "yes" : "*** NO ***");
    printf("  %-52s %s (worst %.1f vs contact %.1f m/s)\n", "carried, not fired",
           no_fling ? "yes" : "*** FIRED ***", worst_blade_speed, worst_rel_speed);
    printf("  %-52s %s (peak tip %.3f m)\n", "bends, stays on the plant",
           bends ? "yes" : "*** NO BEND REGIME ***", peak_tip);
    printf("  %-52s %s (peak stretch %.3f m)\n", "bonds carry it, no slingshot",
           carries ? "yes" : "*** SLINGSHOT ***", peak_bond);
    printf("  %-52s %s (%llu events)\n", "no detonation",
           calm ? "yes" : "*** DETONATED ***", (unsigned long long)s.speed_events);
    printf("  %-52s %s (shear %.1f deg, pose %.1f deg)\n",
           "bends by ROTATING, not by shearing",
           rotates ? "yes" : "*** TRANSLATED, NEVER ROTATED ***",
           peak_shear * 180.0 / M_PI, peak_pose_rot * 180.0 / M_PI);

    const bool pass = got_hit && no_fling && bends && carries && calm && rotates;
    printf("\n  %s\n", pass ? "PASS" : "FAIL (RED until A+B exist; the study above is why)");
    engine.shutdown();
    return pass;
}
