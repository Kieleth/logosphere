// ============================================================================
// LIGHT BODY RINGING — a bond between unequal masses must not amplify
// ============================================================================
// THE LAW. Hit a bonded pair once and it rings down. A constraint may store
// energy and return it, but it may not manufacture more than it was given. If
// a pair is stable at equal masses and unstable at unequal ones, the mass
// ratio is doing something the physics does not license.
//
// WHY THIS EXISTS. TEAR_DEBUG on the walk gate, 17 tears in one pass:
//
//   [TEAR] P274<->P275 strain=2.09x | a vel=(0,-26.05,..) m=0.00238
//                                   | b vel=(0,-29.10,..) m=0.03454
//   bodies at tear:  mean |vy| 6.26 m/s, max 29.10 m/s
//   mass ratio:      mean 7.7x, worst 22.8x
//
// Eva walks at 1.2 m/s and the blades reach 29 m/s with the sign flipping
// between samples. They are not dragged, they RING, and the ringing stretches
// the bond past its 2.0x tear ratio. A kinematic box swept through the same
// patch at 8 m/s tears nothing, because it does not excite this.
//
// THE REDUCTION, built from the measured numbers rather than invented: a
// rooted chain at grass scale — rest 0.06 m, light end 0.00238 kg — swept
// across mass ratios from 1x to 1044x. One impulse in, then nobody touches it.
//
//   ./build/logosphere-tests --test test_light_body_ringing --no-head
//   SINGLE_LAW=1 ...     the exact-manifold-LCP world (BORN-RED here, below)
//   RING_RATIO=1044 RING_TRACE=1 TEAR_DEBUG=1 ...   one ratio, per-frame
//
// ============================================================================
// BORN-RED TDD FLAG (owner ruling, 2026-09-01)
// ============================================================================
// Under SINGLE_LAW=1 the 500x and 1044x ratios TEAR. The owner ruled this test
// a born-red flag: it stays red, the physics is NOT to be fixed here, and no
// threshold or scene parameter moves. This file was converted to physics-TDD
// form (narration, Argus, law-tagged asserts, printed measures) so the red says
// WHAT it is, not just THAT it is. The measures below are what changed; the
// scene and both verdict conditions are untouched.
//
// ============================================================================
// FULL-STATE NARRATION — assert or waive, per degree of freedom
// ============================================================================
// THE CAST, per ratio: root (KINEMATIC anchor, 0.00238 kg), light (0.00238 kg),
// heavy (0.00238 x ratio kg); bonds R-L and L-H, OrganicGluon, rest 0.06 m,
// tear ratio 2.0x. The only external inputs in the whole run are gravity and
// ONE velocity write.
//
// PHASE A — SETTLE, 60 frames. Gravity only; nobody touches anything.
//   root position    fixed at spawn. ASSERTED (INV-7: KINEMATIC bodies take no
//                    momentum, so the anchor cannot move; it is also the frame
//                    every separation below is measured in).
//   root velocity    zero throughout. ASSERTED (INV-7).
//   root orientation/omega  nothing in this experiment drives them. WAIVED,
//                    watched by Argus and printed.
//   light/heavy position  where they hang is the bonds' business, not an
//                    absolute the test can name. WAIVED as an absolute;
//                    asserted as the RELATIVE quantity (strain) below. The one
//                    absolute that IS a law: nothing below the turtle plane —
//                    ASSERTED (INV-1), measured through the turtle site's own
//                    down-reach (probe::body_bottom: oriented bounds for a
//                    rotated box, raw thickness otherwise). Measuring it as
//                    z - thickness/2 on a tumbling box reads -0.027 m and is a
//                    measurement bug, not a violation; the correct extent reads
//                    -0.0009 m at worst.
//   light/heavy velocity  a chain born at rest with nothing touching it should
//                    not accelerate itself. MEASURED and printed (settle peak
//                    speed). WAIVED from a hard bound: gravity is a sanctioned
//                    input and this chain hangs sideways off its anchor, so a
//                    nonzero settle speed is licensed motion, not a defect.
//   bond strain      1.0x at frame zero, before a solver iteration. ASSERTED
//                    (INV-4: born at rest). Through the settle it must stay
//                    under its tear ratio — ASSERTED (INV-14).
//   bond count       2 -> 2. ASSERTED (INV-14).
//
// PHASE B — THE PUSH, one frame. heavy.vy := 1.2 m/s. Nothing else, ever again.
//
// PHASE C — FREE RUN, 600 frames. Gravity and the bonds. Nobody touches it.
//   peak speed       must ring DOWN: worst speed in the second half at or below
//                    the worst in the first. ASSERTED (INV-34: a scene with no
//                    external input settles and stays settled).
//   bond strain      must stay under the tear ratio. ASSERTED (INV-14) over
//                    OBSERVED frames — and the number is printed with its bond,
//                    frame, dist and rest, so a tear can be read as LICENSED
//                    (genuinely stretched, defect upstream) or MANUFACTURED
//                    (tore at rest). KNOWN BLIND SPOT, stated rather than
//                    papered over: the witness samples once per engine.update()
//                    and the tear fires inside a substep, so the observed peak
//                    is a LOWER BOUND on the strain at the tear. The test prints
//                    the frame the count fell and the last strain it could see;
//                    TEAR_DEBUG=1 prints the crossing itself (measured 2.121x
//                    on P0<->P1 at 1044x under SINGLE_LAW: dist 0.1273 m against
//                    rest 0.0600 m, so THAT tear was licensed).
//   bond count       2 -> 2. ASSERTED (INV-14).
//   speed ceiling    the explosion detector is the law's own instrument.
//                    ASSERTED silent (INV-11).
//   finiteness       every tracked DOF finite at all times. ASSERTED (INV-33).
//   light/heavy omega  the angular constraint drives them; no rung of this
//                    experiment names a value they must reach. WAIVED, watched
//                    by Argus (peak |omega| printed for both bodies).
//   q-vs-Euler       no rung here turns a body far enough for the two ledgers
//                    to matter. WAIVED, watched (Argus divergence printed).
//   total mechanical energy  MEASURED via the engine's own ledger
//                    (ENERGY_LEDGER=1) and NOT asserted: that ledger flags
//                    ENERGY CREATED in this scene under BOTH worlds (503 of 605
//                    substeps with the lever off, 123 of 273 with it on), so it
//                    is a pre-existing condition of the bond model here, not a
//                    discriminator for this red. Named in the report as owed.
// ============================================================================

#include "../src/core/argus.h"
#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "generated/physics_constants.h"
#include "logosphere/physics/physics_system.h"
#include "scenes/probe_bond_strain.h"

#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

struct Ring {
    // What the bond actually got (derived, not what the recipe asked for).
    float m_light = 0, m_heavy = 0;
    float stiffness = 0, damping = 0, breaking = 0, tear_limit = 0;

    // PHASE A
    float born_strain = 0.0f;          // INV-4
    float settle_peak_speed = 0.0f;    // measured, waived
    float settle_peak_strain = 0.0f;   // INV-14

    // PHASE C
    float peak_early = 0.0f;           // worst speed, first half
    float peak_late  = 0.0f;           // worst speed, second half
    size_t bonds_before = 0, bonds_after = 0;

    // The tear law's own measure, worst over the whole run.
    probe::StrainWatch strain;

    // WHERE THE TEAR HAPPENED, and what the witness could still see one frame
    // earlier. The witness samples once per engine.update(); the tear fires
    // INSIDE a substep, and the bond is gone before the next observation. So
    // "peak strain observed" is a LOWER BOUND on the strain at the tear, and
    // this pair of numbers says how big the blind spot was.
    int   tear_frame = -1;
    float strain_before_tear = 0.0f;
    int   strain_before_tear_frame = -1;

    // Per-DOF witnesses
    float root_drift = 0.0f, root_peak_speed = 0.0f;    // INV-7
    float lowest_bottom = 1e9f;                          // INV-1
    bool  all_finite = true;                             // INV-33
    float peak_spin_light = 0.0f, peak_spin_heavy = 0.0f;   // waived, watched
    float peak_div_light = 0.0f, peak_div_heavy = 0.0f;     // waived, watched

    // INV-11, from the detector itself
    bool     expdet_on = false;
    uint64_t expdet_warnings = 0;
    float    expdet_worst_speed = 0.0f;
    int      expdet_worst_id = -1;
};

bool finite3(float a, float b, float c) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}

// Grass-scale geometry, taken from the tear log: rest 0.06 m, light end
// 0.00238 kg. Only the heavy end's mass changes across the sweep.
Ring run_ratio(float ratio) {
    Ring r;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) return r;

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    constexpr float REST = 0.06f;
    constexpr float LIGHT_KG = 0.00238f;

    // Size chosen so density x volume lands on the target mass; the solver
    // reads mass, and the shape only has to be plausible for a blade segment.
    auto make = [&](float z, float kg) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = 0.0f; p.y = 0.0f; p.z = z;
        p.width = 0.04f; p.height = 0.003f; p.thickness = REST;
        p.size = 0.04f;
        p.r = 0.3f; p.g = 0.7f; p.b = 0.3f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::WOOD_SOFT);
        const int id = engine.add_particle(p);
        ps.flush_pending_particles();
        auto v = ps.lock_particles_for_write();
        v[id].SetMass(kg);
        return id;
    };

    // Root, then light, then heavy: the shape a blade actually has, a thin
    // segment carrying a leaf many times its own mass.
    const int root  = make(0.10f, LIGHT_KG);
    const int light = make(0.10f + REST, LIGHT_KG);
    const int heavy = make(0.10f + 2.0f * REST, LIGHT_KG * ratio);
    {
        auto v = ps.lock_particles_for_write();
        v[root].solver_mode = ParticleSolverMode::KINEMATIC;
        v[root].owner = ParticleOwner::DYNAMICS;
        v[root].is_at_rest = true;
    }

    auto bond = [&](int a, int b) {
        auto g = std::make_unique<OrganicGluon>();
        g->offset_a = {0, 0, 0}; g->offset_b = {0, 0, 0};
        g->target_distance = REST;
        g->rotate_offsets = false;
        g->contact_area = 1e-4f;
        g->stiffness = 5000.0f;      // the organic recipe
        g->damping = 100.0f;
        g->enable_angular_constraint = true;
        g->angular_stiffness = 50.0f;
        g->angular_damping = 5.0f;
        physics.add_gluon_between(a, b, std::move(g));
    };
    bond(root, light);
    bond(light, heavy);

    // THE WITNESS. The asserts below and every printed measure read these
    // same queries, so what is checked and what is seen cannot drift apart.
    logosphere::Argus argus;
    argus.watch(root,  "root");
    argus.watch(light, "light");
    argus.watch(heavy, "heavy");
    const std::vector<size_t> cast = {(size_t)root, (size_t)light, (size_t)heavy};

    float root_x0 = 0, root_y0 = 0, root_z0 = 0;
    {
        auto v = ps.lock_particles_for_read();
        r.m_light = v[light].GetMass();
        r.m_heavy = v[heavy].GetMass();
        root_x0 = v[root].x; root_y0 = v[root].y; root_z0 = v[root].z;
    }
    // What the bond ACTUALLY carries after the doors derived it — the recipe
    // above asks for 5000/100, and the energy ledger's strain term is read
    // against whatever landed, not what was asked for.
    for (const GluonConstraintBase* g : physics.get_gluons_for_particle(light)) {
        if (!g) continue;
        r.stiffness = g->stiffness; r.damping = g->damping;
        r.tear_limit = g->max_strain_ratio();
        auto v = ps.lock_particles_for_read();
        r.breaking = g->calculate_breaking_force(v[g->particle_a], v[g->particle_b]);
        break;
    }

    // INV-11's instrument, zeroed so this ratio's run is the only thing in it.
    logosphere::expdet::reset();
    r.expdet_on = logosphere::expdet::enabled();

    // Every observation the asserts read, taken the same way in every phase.
    int frame = 0;
    size_t prev_bonds = (size_t)-1;
    float  prev_peak_strain = 0.0f;
    auto observe = [&](Ring& out) {
        argus.observe(ps, frame);
        const probe::BondCensus c = probe::census(ps, physics, cast);
        out.strain.take(c, frame);
        if (prev_bonds != (size_t)-1 && c.bonds < prev_bonds && out.tear_frame < 0) {
            out.tear_frame = frame;
            out.strain_before_tear = prev_peak_strain;
            out.strain_before_tear_frame = frame - 1;
        }
        prev_bonds = c.bonds;
        prev_peak_strain = c.peak_strain;
        auto v = ps.lock_particles_for_read();
        for (int id : {root, light, heavy}) {
            if ((size_t)id >= v.size()) continue;
            const Particle& p = v[id];
            if (!finite3(p.x, p.y, p.z) || !finite3(p.vx, p.vy, p.vz) ||
                !finite3(p.omega_x, p.omega_y, p.omega_z) ||
                !finite3(p.rotation_x, p.rotation_y, p.rotation_z))
                out.all_finite = false;                       // INV-33
            out.lowest_bottom = std::fmin(out.lowest_bottom,
                                          probe::body_bottom(p));      // INV-1
        }
        const Particle& rp = v[root];
        out.root_drift = std::fmax(out.root_drift,
            std::sqrt((rp.x-root_x0)*(rp.x-root_x0) + (rp.y-root_y0)*(rp.y-root_y0)
                    + (rp.z-root_z0)*(rp.z-root_z0)));                 // INV-7
        out.root_peak_speed = std::fmax(out.root_peak_speed,
            std::sqrt(rp.vx*rp.vx + rp.vy*rp.vy + rp.vz*rp.vz));       // INV-7
        return c;
    };

    // ---- PHASE A: born, then settle ------------------------------------
    {   // INV-4: at frame zero, before a single solver iteration.
        const probe::BondCensus c0 = probe::census(ps, physics, cast);
        r.born_strain = c0.peak_strain;
        r.bonds_before = physics.get_total_gluon_count();
    }
    for (int f = 0; f < 60; ++f) {
        engine.update(1.0 / 60.0);
        observe(r);
        r.settle_peak_strain = r.strain.peak_strain;
        r.settle_peak_speed = std::fmax(
            r.settle_peak_speed,
            std::fmax(argus.peak_speed(light), argus.peak_speed(heavy)));
        frame++;
    }
    r.bonds_before = physics.get_total_gluon_count();

    // ---- PHASE B: ONE push, at the speed Eva's leg actually travels -----
    {
        auto v = ps.lock_particles_for_write();
        v[heavy].vy = 1.2f;
        v[heavy].is_at_rest = false;
        v[light].is_at_rest = false;
    }

    // ---- PHASE C: hands off --------------------------------------------
    // RING_FRAMES bounds the run so a diverging ratio cannot hang the suite.
    // RING_TRACE prints per-frame state: this is how a ratio that "does not
    // return" gets separated into diverging-and-slow versus genuinely stuck.
    const char* fr_env = std::getenv("RING_FRAMES");
    const int FRAMES = fr_env ? std::atoi(fr_env) : 600;
    static const bool trace = std::getenv("RING_TRACE") != nullptr;
    for (int f = 0; f < FRAMES; ++f) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        engine.update(1.0 / 60.0);
        const double frame_ms = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0).count();
        const probe::BondCensus c = observe(r);
        if (trace) {
            auto v = ps.lock_particles_for_read();
            printf("    [RING r=%.1f f%03d] %7.2f ms | light v=(%.2f,%.2f,%.2f) "
                   "z=%.4f | heavy v=(%.2f,%.2f,%.2f) z=%.4f | dist=%.4f "
                   "| bonds %zu worst strain %.4fx\n",
                   ratio, f, frame_ms,
                   v[light].vx, v[light].vy, v[light].vz, v[light].z,
                   v[heavy].vx, v[heavy].vy, v[heavy].vz, v[heavy].z,
                   std::sqrt(std::pow(v[heavy].x - v[light].x, 2) +
                             std::pow(v[heavy].y - v[light].y, 2) +
                             std::pow(v[heavy].z - v[light].z, 2)),
                   c.bonds, c.peak_strain);
        }
        float worst = 0.0f;
        {
            auto v = ps.lock_particles_for_read();
            for (int id : {light, heavy}) {
                if ((size_t)id >= v.size()) continue;
                const Particle& p = v[id];
                worst = std::fmax(worst, std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz));
            }
        }
        if (f < FRAMES / 2) r.peak_early = std::fmax(r.peak_early, worst);
        else                r.peak_late  = std::fmax(r.peak_late,  worst);
        frame++;
    }
    r.bonds_after = physics.get_total_gluon_count();

    // The waived-but-watched DOFs, printed so the waiver is a visible decision.
    r.peak_spin_light = argus.peak_spin(light);
    r.peak_spin_heavy = argus.peak_spin(heavy);
    r.peak_div_light  = argus.peak_divergence(light, false);
    r.peak_div_heavy  = argus.peak_divergence(heavy, false);

    const logosphere::expdet::Stats st = logosphere::expdet::stats();
    r.expdet_warnings = st.speed_warnings + st.energy_warnings + st.escape_warnings;
    r.expdet_worst_speed = st.worst_speed;
    r.expdet_worst_id = st.worst_id;

    engine.shutdown();
    return r;
}

int failures = 0;
void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void check(bool ok, const char* fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("    %s %s\n", ok ? "[PASS]" : "[FAIL]", msg);
    if (!ok) failures++;
}

} // namespace

bool test_light_body_ringing() {
    printf("\n=== LIGHT BODY RINGING: a bond must not amplify (issue #47) ===\n\n");
    printf("  grass scale: rest 0.060 m, light end 0.00238 kg, one 1.2 m/s push,\n");
    printf("  then nobody touches it. Only the heavy end's mass changes.\n");
    printf("  Observed through Argus + the bond-strain probe (the tear law's own\n");
    printf("  measure); every assert names the law it enforces.\n");

    failures = 0;
    // Extended to 1044x: the tree's leaf bonds run 111.661 kg against a
    // 0.107 kg leaf, three orders of magnitude past where this sweep
    // originally stopped and called the problem settled.
    std::vector<float> ratios = {1.0f, 10.0f, 25.0f, 100.0f, 250.0f, 500.0f, 1044.0f};
    if (const char* only = std::getenv("RING_RATIO")) ratios = { (float)std::atof(only) };

    struct Row { float ratio, early, late, strain; size_t before, after; };
    std::vector<Row> table;

    for (float ratio : ratios) {
        const Ring r = run_ratio(ratio);
        const bool tore = r.bonds_after < r.bonds_before;
        const bool rang_down = r.peak_late <= r.peak_early;

        printf("\n  -- ratio %.1fx  light %.5f kg, heavy %.5f kg; bond k=%.1f N/m "
               "c=%.1f Ns/m F_break=%.3f N tear %.2fx --\n",
               ratio, r.m_light, r.m_heavy, r.stiffness, r.damping,
               r.breaking, r.tear_limit);
        printf("    [measure] A born strain        %.4fx   (frame 0, before any solver iteration)\n",
               r.born_strain);
        printf("    [measure] A settle peak speed  %.4f m/s over 60 frames, nobody touched it\n",
               r.settle_peak_speed);
        printf("    [measure] A settle peak strain %.4fx\n", r.settle_peak_strain);
        printf("    [measure] B push               heavy vy := 1.200 m/s, one frame\n");
        printf("    [measure] C peak speed         early %.4f  late %.4f m/s\n",
               r.peak_early, r.peak_late);
        printf("    [measure] C peak strain        %.4fx at f%d on P%zu<->P%zu "
               "(dist %.4f m, rest %.4f m, limit %.2fx)\n",
               r.strain.peak_strain, r.strain.peak_frame, r.strain.peak_a,
               r.strain.peak_b, r.strain.peak_dist, r.strain.peak_rest,
               r.strain.peak_limit);
        printf("    [measure] C bonds              %zu -> %zu (min live during run %zu)\n",
               r.bonds_before, r.bonds_after, r.strain.min_bonds);
        printf("    [measure] C root               drift %.6f m, peak |v| %.6f m/s\n",
               r.root_drift, r.root_peak_speed);
        printf("    [measure] C lowest down-reach  %.6f m (turtle plane %.3f, slop %.4f;\n"
               "                                  the turtle site's own extent — oriented "
               "bounds for a rotated box)\n",
               r.lowest_bottom, PhysicsV4::TURTLE_Z, PhysicsV4::SLOP);
        if (r.tear_frame >= 0)
            printf("    [measure] C TEAR              bond count fell at f%d; last strain the "
                   "witness could see was %.4fx at f%d.\n"
                   "                                  The tear fires inside a substep and the "
                   "bond is gone before the next observation, so that is a LOWER BOUND: "
                   "TEAR_DEBUG=1 prints the crossing.\n",
                   r.tear_frame, r.strain_before_tear, r.strain_before_tear_frame);
        printf("    [measure] C detector           %llu warning(s), worst body P%d at %.3f m/s "
               "(ceiling %.1f, detector %s)\n",
               (unsigned long long)r.expdet_warnings, r.expdet_worst_id,
               r.expdet_worst_speed, PhysicsV4::EXPDET_SPEED_CEILING,
               r.expdet_on ? "on" : "OFF");
        printf("    [waive]   omega: peak |omega| light %.4f heavy %.4f rad/s — no rung "
               "of this experiment names a value they must reach\n",
               r.peak_spin_light, r.peak_spin_heavy);
        printf("    [waive]   q-vs-Euler: peak divergence light %.5f heavy %.5f rad — "
               "nothing here turns a body far enough for the two ledgers to part\n",
               r.peak_div_light, r.peak_div_heavy);

        check(r.born_strain <= 1.0f + 1e-3f,
              "INV-4: the chain is born at strain 1.0 (measured %.4fx)",
              r.born_strain);
        check(r.root_drift <= 1e-6f && r.root_peak_speed <= 1e-6f,
              "INV-7: the KINEMATIC anchor takes no momentum (drift %.2e m, "
              "peak |v| %.2e m/s)", r.root_drift, r.root_peak_speed);
        check(r.all_finite, "INV-33: every tracked DOF stays finite");
        check(r.lowest_bottom >= PhysicsV4::TURTLE_Z - PhysicsV4::SLOP,
              "INV-1: nothing reaches below the turtle plane beyond slop "
              "(lowest down-reach %.6f m, floor %.6f m)",
              r.lowest_bottom, PhysicsV4::TURTLE_Z - PhysicsV4::SLOP);
        check(r.expdet_warnings == 0,
              "INV-11: the explosion detector stays silent (%llu warning(s), "
              "worst body %.3f m/s vs ceiling %.1f)",
              (unsigned long long)r.expdet_warnings, r.expdet_worst_speed,
              PhysicsV4::EXPDET_SPEED_CEILING);
        check(r.strain.peak_strain < r.strain.peak_limit,
              "INV-14: no OBSERVED frame stands a bond at or past its tear ratio "
              "(worst %.4fx of %.2fx, f%d, P%zu<->P%zu, dist %.4f m vs rest %.4f m; "
              "the witness samples once per update, the tear fires inside a substep)",
              r.strain.peak_strain, r.strain.peak_limit, r.strain.peak_frame,
              r.strain.peak_a, r.strain.peak_b, r.strain.peak_dist,
              r.strain.peak_rest);
        check(!tore,
              "INV-14: every bond survives one 1.2 m/s push (%zu -> %zu)",
              r.bonds_before, r.bonds_after);
        check(rang_down,
              "INV-34: it RINGS DOWN — peak speed in the second half at or "
              "below the first (%.4f -> %.4f m/s)", r.peak_early, r.peak_late);

        table.push_back({ratio, r.peak_early, r.peak_late,
                         r.strain.peak_strain, r.bonds_before, r.bonds_after});
    }

    printf("\n  %-8s %12s %12s %12s %10s %s\n",
           "ratio", "peak early", "peak late", "peak strain", "bonds", "verdict");
    printf("  %s\n",
           "---------------------------------------------------------------------------");
    for (const Row& t : table) {
        const bool tore = t.after < t.before;
        printf("  %6.1fx %12.3f %12.3f %11.3fx %6zu->%zu  %s\n",
               t.ratio, t.early, t.late, t.strain, t.before, t.after,
               tore ? "*** TORE ***"
                    : (t.late <= t.early ? "rings down" : "*** GROWING ***"));
    }

    printf("\n  %d assertion(s) failed\n", failures);
    const bool pass = (failures == 0);
    printf("\n  %s\n", pass ? "PASS"
        : "FAIL (a bond between unequal masses is manufacturing energy)");
    return pass;
}
