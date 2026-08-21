// =============================================================================
// NOTHING IS BORN INSIDE ANYTHING (INV-30, INV-4, G-48 — owner ruling R8)
// =============================================================================
// THE LAW THIS TEST ENFORCES.
//
// INV-30: an external writer may not hand the solver a state it would never
// have produced — "no frame begins with an overlap beyond SLOP ... created by
// a non-solver writer". INV-4: a generated structure arrives with "no overlap
// beyond slop" before a single solver iteration. G-48 carries the derivation.
//
// Owner ruling R8, 2026-08-21, verbatim: "the generator should NOT put things
// overlapping... I'd not fix for penetration, I'd just avoid it fully...
// enforce nothing that is created, ever, can coexist — particles are always,
// 100% guaranteed not to overlap in 3d, ever. Solve that, and do not care
// about that in our physics engine, reducing complexity, and just simply
// refuse to run."
//
// That ruling SUPERSEDES the 2026-08-02 position this file used to carry, that
// minimal overlap is accepted because rejecting a colliding branch dropped the
// tree from 149 bodies to 59. The trade that position saw was between refusing
// a placement and accepting a bad one. The third option it missed is to make
// the placement search see the whole world before it decides.
//
// WHAT IS ASSERTED, AND THROUGH WHAT.
//
// Every verdict here comes from the CREATION DOOR itself
// (ParticleSystem::inspect_creation_overlaps), which is the same geometry, the
// same SLOP threshold and the same narrow phase the armed door uses at every
// spawn. The test does not carry a private overlap formula: a check written
// twice is a check that eventually disagrees with itself, and this file used
// to hold exactly that — an axis-aligned box test that described a tilted
// branch as a solid it does not have.
//
//   PART 1  the standard foliage scene: nothing overlaps at frame zero.
//   PART 2  the fallen-tree presets: every log RESTS ON its ground, measured
//           on the ORIENTED bottom, because a laid-flat log's world-Z extent
//           is its DIAMETER and not its length (test_collision_bounds_rotation
//           pins the same arithmetic on a 2.2 m log spanning 0.70 m).
//   PART 3  the control: a body placed inside another on purpose IS seen.
//           Without it, "clean" and "blind" are the same output.
//
// MODE. The door is armed strict by default and aborts the process, which no
// in-process assert can catch, so this file runs under
// LOGOSPHERE_CREATION_LENIENT=1 and reads the verdict directly. The STRICT
// path is exercised by every other test in the suite and by every Eden run;
// what is asserted here is the VERDICT, which is what the abort branches on.
//
//   ./build/logosphere-tests --test test_no_overlap_at_creation --no-head
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/particle.h"
#include "logosphere/physics/creation_door.h"
#include "logosphere/physics/physics_solver.h"
#include "logosphere/worldgen/fallen_tree_generator.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// The engine's own geometric-error tolerance. Not a number chosen here:
// PhysicsV4::SLOP is what the solver calls "float residue, not geometry", and
// reusing it is what makes ABUTTING legal (adjacent floor tiles, the shared
// joint point of two bonded segments) while PENETRATING is not.
constexpr float SLOP = PhysicsV4::SLOP;

int checks_run = 0;
int checks_failed = 0;

void check(bool ok, const char* law, const std::string& claim) {
    checks_run++;
    if (!ok) checks_failed++;
    printf("  [%s] %-8s %s\n", ok ? "V" : "X", law, claim.c_str());
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

void dump_violations(const std::vector<logosphere::CreationOverlap>& v,
                     size_t max_shown) {
    size_t shown = 0;
    for (const auto& o : v) {
        if (shown++ >= max_shown) {
            printf("      ... and %zu more\n", v.size() - max_shown);
            break;
        }
        printf("      %s\n", logosphere::creation_violation_text(o).c_str());
    }
}

}  // namespace

bool test_no_overlap_at_creation() {
    printf("\n=== NOTHING IS BORN INSIDE ANYTHING (INV-30, INV-4, G-48) ===\n");
    printf("A particle is a body: it occupies space and it collides. Two bodies\n");
    printf("created in the same place is not a tight fit, it is an impossible\n");
    printf("world, and the solver's only correct response is to throw them apart.\n");
    printf("Every verdict below comes from the creation door itself.\n\n");

    // The door aborts by design; a test needs its verdict, not its exit code.
    setenv("LOGOSPHERE_CREATION_LENIENT", "1", 1);

    checks_run = 0;
    checks_failed = 0;

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) {
        printf("  ERROR: engine init failed\n");
        unsetenv("LOGOSPHERE_CREATION_LENIENT");
        return false;
    }
    ParticleSystem& ps = engine.get_particle_system();

    // ========================================================================
    // PART 1 — THE STANDARD FOLIAGE SCENE
    // ========================================================================
    printf("  --- PART 1: the standard foliage scene, frame zero ---\n");

    PhysicsTreeGenerator gen;
    gen.initialize(&engine);
    TreeSpec spec;
    spec.random_seed = 12345;

    PhysicsTreeResult tree = gen.generate_tree_with_roots(0.0f, 0.0f, 0.0f, spec);
    ps.flush_pending_particles();
    // NO engine.update() ANYWHERE IN THIS TEST. The moment physics runs it
    // starts repairing the damage, and the question here is what was handed to
    // it, not what it managed to salvage.

    const logosphere::CreationVerdict world = ps.inspect_creation_overlaps({});

    printf("  [measure] tree bodies: trunk 1, branches %zu, leaves %zu, roots %zu\n",
           tree.branch_ids.size(), tree.leaf_ids.size(), tree.root_ids.size());
    printf("  [measure] audit: %zu bodies, %zu BVH candidates, %zu exact pair tests,"
           " %.0f us\n",
           world.bodies_audited, world.candidates, world.pairs_tested, world.micros);
    printf("  [measure] contact-bearing overlaps deeper than SLOP=%.4f m: %zu"
           " (deepest %.4f m)\n",
           SLOP, world.violations.size(), world.deepest());
    printf("  [measure] structural overlaps (one bonded body, no contact row):"
           " %zu (deepest %.4f m)\n",
           world.structural.size(), world.deepest_structural());
    if (!world.clean()) {
        printf("      CONTACT-BEARING PAIRS, as the door reports them:\n");
        dump_violations(world.violations, 12);
    }
    if (!world.structural.empty()) {
        printf("      STRUCTURAL PAIRS (a sample; the whole crown crosses itself):\n");
        dump_violations(world.structural, 4);
    }

    // Hygiene: a check that looked at nothing reports clean. Prove it looked.
    check(world.bodies_audited >= 100, "hygiene",
          fmt("the audit saw the whole tree: %zu bodies audited (>= 100)",
              world.bodies_audited));
    check(world.pairs_tested > 0, "hygiene",
          fmt("the BVH handed back real neighbours: %zu exact pair tests (> 0)",
              world.pairs_tested));

    // THE LAW.
    check(world.clean(), "INV-30",
          fmt("no external writer hands the solver an overlap beyond SLOP: "
              "%zu contact-bearing pairs (deepest %.4f m), want 0",
              world.violations.size(), world.deepest()));
    check(world.clean(), "INV-4",
          fmt("the generated structure is born at rest, no overlap beyond slop: "
              "%zu contact-bearing pairs, want 0", world.violations.size()));

    // BORN RED, AND IT STAYS RED UNTIL A GENERATOR CAN PASS IT.
    // R8 is literal: "particles are always, 100% guaranteed not to overlap in
    // 3d, ever." The tree's crown crosses ITSELF — branches through sibling
    // branches, leaves through branches — and the engine tolerates it only
    // because bonded_components() withholds the contact row. That is exactly
    // the creation-overlap complexity R8 orders out of the engine, and it
    // cannot be met by a threshold: it is a crown-generator redesign. The
    // number below is the size of that job, measured on every run so it can
    // only shrink. Never weaken this; it goes green when the generator does.
    check(world.structural.empty(), "G-48",
          fmt("nothing coexists, INCLUDING inside one bonded structure: "
              "%zu structural pairs (deepest %.4f m), want 0 — OWNER DECISION "
              "OPEN, the crown generator draws them crossing on purpose",
              world.structural.size(), world.deepest_structural()));

    // ========================================================================
    // PART 2 — THE FALLEN-TREE PRESETS REST ON THEIR GROUND
    // ========================================================================
    // A fallen log is laid horizontal by rotation_y = pi/2, so the axis its
    // LENGTH lives on points sideways and its world-Z half-extent is half its
    // DIAMETER. Offsetting the segment centre by half the segment LENGTH
    // therefore hovers the whole log above the ground it is drawn resting on.
    // Same arithmetic as test_collision_bounds_rotation: a laid-flat 2.2 m log
    // spans 0.70 m in world Z, which is its diameter.
    printf("\n  --- PART 2: the fallen-tree presets rest ON their ground ---\n");

    FallenTreeGenerator fallen;
    fallen.initialize(&engine, &engine.get_kg());

    struct Preset { const char* name; FallenTreeSpec spec; };
    const Preset presets[] = {
        {"fallen_trunk",  FallenTreeSpec::fallen_trunk()},
        {"fallen_log",    FallenTreeSpec::fallen_log()},
        {"fallen_branch", FallenTreeSpec::fallen_branch()},
        {"twig",          FallenTreeSpec::twig()},
    };

    // Spread the presets apart so this part measures GROUND CONTACT and
    // nothing else; whether two logs may share a metre of forest floor is
    // part 1's question, asked of the tree.
    float px = 200.0f;
    for (const Preset& preset : presets) {
        const float ground_z = 0.0f;
        // SCOPE BY DELTA, not by entity id. "FallenTree" is not in the
        // ontology, so createEntityAtPosition is REJECTED and every generator
        // here lands its particles on entity 0 alongside the tree's. Reading
        // getEntityKGParticles(e) therefore hands back the whole world and the
        // four presets all measure the same body. That KG rejection is a
        // separate defect and is not this test's subject; taking the particles
        // that appeared during THIS call measures exactly the bodies this
        // preset created.
        const size_t before = engine.get_kg().getEntityKGParticles(0).size();
        fallen.generate_fallen_tree(px, 0.0f, ground_z, preset.spec);
        px += 60.0f;

        auto all_kg = engine.get_kg().getEntityKGParticles(0);
        std::vector<kg::KGParticleID> kg_particles(all_kg.begin() + before,
                                                   all_kg.end());
        float worst_lift_err = 0.0f;  // + centre too high, - too low
        float worst_gap = 0.0f;       // + hovering, - buried (door's own reading)
        float worst_span_err = 0.0f;
        float worst_seam = 0.0f;      // + gap between segments, - overlap
        int   worst_seg = -1;
        float prev_x = 0, prev_y = 0, prev_z = 0, prev_half_len = 0;
        for (size_t i = 0; i < kg_particles.size(); ++i) {
            const Particle p = engine.get_kg().getKGParticleData(kg_particles[i]);

            // INDEPENDENT ARITHMETIC, no door involved. The log is laid flat,
            // so its centre must sit exactly half its (tapered) diameter above
            // the ground. width carries the diameter; thickness carries the
            // LENGTH, and reading that one instead is the whole bug.
            const float lift_err = (p.z - ground_z) - p.width * 0.5f;

            // THROUGH THE PRODUCTION PATH. The door's oriented bottom must
            // agree with that arithmetic, or the two describe different solids.
            const logosphere::CreationBody b =
                logosphere::describe_creation_body((int)i, p);
            const float gap = b.world_min_z - ground_z;
            const float span = 2.0f * (p.z - b.world_min_z);
            const float span_err = span - p.width;

            // SEGMENTS ABUT. Centres are one segment length apart along the
            // log axis and each is one segment long, so the faces meet at
            // exactly zero: no gap to see through, no overlap to eject.
            if (i > 0) {
                const float dx = p.x - prev_x, dy = p.y - prev_y, dz = p.z - prev_z;
                const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float seam = d - (prev_half_len + p.thickness * 0.5f);
                if (std::fabs(seam) > std::fabs(worst_seam)) worst_seam = seam;
            }
            prev_x = p.x; prev_y = p.y; prev_z = p.z;
            prev_half_len = p.thickness * 0.5f;

            if (std::fabs(lift_err) > std::fabs(worst_lift_err)) {
                worst_lift_err = lift_err; worst_seg = (int)i;
            }
            if (std::fabs(gap) > std::fabs(worst_gap)) worst_gap = gap;
            if (std::fabs(span_err) > std::fabs(worst_span_err)) worst_span_err = span_err;
        }

        printf("  [measure] %-14s len %.2f dia %.2f segs %d -> %zu segments,"
               " worst centre-lift error %+.4f m (seg %d), oriented bottom gap"
               " %+.4f m, Z-span error %+.4f m, worst seam %+.4f m\n",
               preset.name, preset.spec.length, preset.spec.diameter,
               preset.spec.num_segments, kg_particles.size(),
               worst_lift_err, worst_seg, worst_gap, worst_span_err, worst_seam);

        check(kg_particles.size() == (size_t)preset.spec.num_segments, "hygiene",
              fmt("%s produced the %d segments it declares, and this loop is "
                  "measuring THEM: %zu found", preset.name,
                  preset.spec.num_segments, kg_particles.size()));
        check(std::fabs(worst_lift_err) <= SLOP, "INV-4",
              fmt("%s is born resting on its ground, centre at half its DIAMETER "
                  "and not half its length: worst error %+.4f m, want <= %.4f",
                  preset.name, worst_lift_err, SLOP));
        check(std::fabs(worst_span_err) <= SLOP, "INV-30",
              fmt("%s spans its DIAMETER in world Z: the door's oriented extent "
                  "is off by %+.4f m, want <= %.4f",
                  preset.name, worst_span_err, SLOP));
        check(std::fabs(worst_gap) <= SLOP, "INV-30",
              fmt("%s: the door's own oriented bottom sits on the ground, "
                  "%+.4f m off, want <= %.4f", preset.name, worst_gap, SLOP));
        check(std::fabs(worst_seam) <= SLOP, "G-48",
              fmt("%s segments ABUT and do not interpenetrate: worst seam "
                  "%+.4f m, want |seam| <= %.4f",
                  preset.name, worst_seam, SLOP));
    }

    // ========================================================================
    // PART 3 — THE CONTROL: the door is not blind
    // ========================================================================
    // Run the answer out the other way. A clean verdict from a check that
    // cannot see anything looks exactly like a clean verdict from a legal
    // world, and only this tells them apart.
    printf("\n  --- PART 3: control, a body placed inside another IS seen ---\n");

    Particle host;
    host.shape = ParticleShape::BOX;
    host.width = host.height = host.thickness = 1.0f;
    host.x = -500.0f; host.y = 0.0f; host.z = 0.5f;
    host.material_density = 600.0f;
    const int host_id = ps.queue_particle_addition(host);

    Particle intruder = host;
    intruder.z = 0.5f + 0.25f;      // a quarter of a metre INSIDE the host
    const int intruder_id = ps.queue_particle_addition(intruder);
    // Through the real spawn path: the flush runs the ARMED door, which
    // under LENIENT prints the refusal it would otherwise abort on.
    ps.flush_pending_particles();

    const logosphere::CreationVerdict ctrl =
        ps.inspect_creation_overlaps({intruder_id});

    bool caught = false;
    float caught_depth = 0.0f;
    for (const auto& v : ctrl.violations) {
        const bool pair = (v.a.index == intruder_id && v.b.index == host_id) ||
                          (v.a.index == host_id && v.b.index == intruder_id);
        if (pair) { caught = true; caught_depth = v.depth; }
    }
    printf("  [measure] control pair P%d inside P%d: door reports %zu violation(s),"
           " depth %.4f m (geometry says 0.7500)\n",
           intruder_id, host_id, ctrl.violations.size(), caught_depth);

    check(caught, "hygiene",
          fmt("the door SEES a deliberate 0.75 m overlap it was not told about: "
              "reported %.4f m", caught_depth));
    check(std::fabs(caught_depth - 0.75f) <= SLOP, "hygiene",
          fmt("and measures it, not merely flags it: %.4f m vs the 0.7500 m the "
              "placement puts there", caught_depth));

    // ========================================================================
    printf("\n  %d/%d checks pass.\n", checks_run - checks_failed, checks_run);
    if (checks_failed == 0) {
        printf("  CLEAN. Every body of every generated structure is clear of every\n"
               "  other by at least SLOP, so the solver is handed a legal world and\n"
               "  has nothing to repair on frame one.\n");
    } else {
        printf("  %d CHECK(S) RED. A generator is placing bodies inside other bodies\n"
               "  or above the ground they are drawn resting on. Fix the PLACEMENT:\n"
               "  the engine carries no tolerance for creation overlap (R8).\n",
               checks_failed);
    }

    unsetenv("LOGOSPHERE_CREATION_LENIENT");
    engine.shutdown();
    return checks_failed == 0;
}
