// =============================================================================
// SCENE: THE JAMMED CLUMP MAY NOT SLEEP — G-67 / G-68 / G-48 (owner order
// 2026-09-01: "I'd like to TDD all this first of any change")
// =============================================================================
// The frame-collapse RCA (LEDGER 2026-09-01, GEDANKEN-67) found Eden at
// 3.4 s of physics per frame because ~900 quiet bodies could never
// sleep: stones born INSIDE strata tiles, held awake by G-48's sleep
// veto on contact overlap, dragging 30,000 contact rows through an
// exhausted 32-iteration budget every substep. Before any fix, the
// four mechanisms in that chain get a born-red assert each, on a REAL
// stage (stone tile on the turtle, stone rubble, a wood trunk with
// leaves), headless and windowed from this one file.
//
// The four cases, each derived in its G record:
//   A  THE STONE BORN IN THE FLOOR (G-67, INV-4, INV-2, INV-31): Eden's
//      recipe verbatim - rubble centred at the tile's own centre height.
//   B  THE JAMMED CLUMP (G-67): two leaves bonded to a trunk, overlapping
//      each other by 2 cm at rest by construction. Quiet from birth;
//      nothing will ever move them; they must sleep. INV-2/INV-4 waived
//      by name (the structural band, owner decision (a) on the board).
//   C  THE INTERLOCKED STACK (G-48's own case, the guard against
//      over-correcting G-67): a cube born 2 cm into the cube under it,
//      free to separate. Must NOT sleep while the overlap still shrinks,
//      must separate, then sleep. Expected green today.
//   D  THE FLOOR ARRIVES AFTER THE STONE (G-68, INV-4): the stone sleeps
//      on the turtle; a tile is then created around it. Nothing may be
//      born inside anything; the stone ends ON the tile, both asleep.
//
// THE COST WITNESS on every case (G-67): once the stage is quiet, the
// solver leaves by convergence, never by exhausting its budget - read
// from PhysicsSystem::last_solve(), the record the level-2 trace writes.
//
// FULL-STATE NARRATION (per case, assert-or-waive by DOF):
//   tile:    z at 0.15 (INV-2 stands on the turtle), no spin (INV-34),
//            asleep at the end (INV-31). x/y: waived, nothing pushes it.
//   rubble:  A: z must LEAVE the tile (INV-2) - today it stays at 0.15;
//            D: z ends at the tile top + half thickness (0.43). Spin:
//            INV-34. Asleep at the end: INV-31. x/y: waived (no lateral
//            load; a lateral escape is measured, not asserted).
//   trunk:   z 0.90 (stands), asleep. Leaves: overlap 2 cm by design
//            (waived INV-2/INV-4), asleep (G-67), bonds intact (INV-14).
//   cubes:   C: lower z 0.55, upper z ends at 1.05 (INV-2 separated),
//            first sleep frame has overlap under tolerance (G-48),
//            both asleep at the end.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace scene_jammed {

constexpr float DT = 1.0f / 60.0f;
constexpr float MU = 0.5f;               // explicit on every body
constexpr float OVERLAP_TOL = 0.002f;    // INV-2's 2x SLOP bar (scene_limits)
constexpr float SPIN_NOISE = 0.05f;      // rad/s (INV-34)
constexpr float SPEED_MAX = 10.0f;       // m/s (INV-11)
constexpr float HEIGHT_TOL = 0.01f;      // m, stands-at-height bar

// THE STAGE: one stone strata tile, Eden's own (4 x 4 x 0.3 m, STONE
// 2500 kg/m^3 = 12,000 kg), centre z 0.15, top z 0.30, on the turtle.
constexpr float TILE_L = 4.0f, TILE_T = 0.3f, TILE_Z = 0.15f;
constexpr float TILE_TOP = TILE_Z + TILE_T * 0.5f;   // 0.30
// Eden's rubble recipe (examples/eden/src/main.cpp): rock.z = 0.15,
// dims 0.15 + (i%5)*0.08 by 0.15 + (i%4)*0.06 by 0.1 + (i%3)*0.08 -
// the deepest measured pair was 0.31 x 0.21 x 0.26 at z 0.13-0.15.
constexpr float RUB_X = 0.31f, RUB_Y = 0.21f, RUB_Z = 0.26f;

struct BodySpec {
    float sx, sy, sz;
    float x, y, z;
    Materials::Type mat;
    const char* label;
    bool late = false;     // D: created at Case::late_frame, not at build
};

struct Bond { int a, b; float ax, ay, az; float bx, by, bz; float rest; };

struct Case {
    const char* name;
    const char* gid;
    std::vector<BodySpec> bodies;
    std::vector<Bond> bonds;
    int run_frames = 600;
    int late_frame = -1;               // D: when the late body is created
    int rubble = -1;                   // the body whose z is the story
    float rubble_z_final = -1.0f;      // where it must end (INV-2), <0 = none
    int stack_upper = -1;              // C: the interlocked cube
    float stack_upper_z_final = -1.0f;
    bool birth_overlap_asserted = true;   // INV-4 at the creation frame
    bool rest_overlap_asserted = true;    // INV-2 at the end
    const char* waiver = nullptr;
    const char* demo1 = "";
    const char* demo2 = "";
};

struct Verdict { std::string text; bool ok; };

struct Scene {
    logosphere::Argus argus;
    std::vector<int> ids;               // -1 until created (late bodies)
    std::vector<BodySpec> specs;
    // Latches the evaluator reads (one source for headless and window).
    float birth_overlap = 0.0f;         // worst overlap right after a creation flush
    float first_sleep_overlap = -1.0f;  // overlap at the frame the first body slept
    int   first_sleep_frame = -1;
    int   frames_run = 0;

    static void paint(Particle& p, Materials::Type m) {
        switch (m) {
            case Materials::Type::WOOD_SOFT: p.r = 0.72f; p.g = 0.55f; p.b = 0.35f; break;
            case Materials::Type::LEAVES:    p.r = 0.42f; p.g = 0.62f; p.b = 0.32f; break;
            case Materials::Type::STONE:
            default:                         p.r = 0.55f; p.g = 0.55f; p.b = 0.58f; break;
        }
        p.a = 1.0f;
    }

    int create(ParticleSystem& ps, int i, float x_off) {
        const BodySpec& b = specs[i];
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.width = b.sx; p.height = b.sy; p.thickness = b.sz;
        p.size = std::fmax(b.sx, std::fmax(b.sy, b.sz));
        p.x = x_off + b.x; p.y = b.y; p.z = b.z;
        paint(p, b.mat);
        p.SetMaterial(b.mat);
        p.friction = MU;
        const int id = ps.queue_particle_addition(p);
        ids[i] = id;
        return id;
    }

    void bond(ParticleSystem& ps, PhysicsSystem& physics, const Bond& bd) {
        (void)ps;
        auto g = std::make_unique<OrganicGluon>();
        g->offset_a = {bd.ax, bd.ay, bd.az};
        g->offset_b = {bd.bx, bd.by, bd.bz};
        g->target_distance = bd.rest;      // born at strain 1.0 (INV-4)
        g->rotate_offsets = true;
        g->contact_area = 1e-3f;
        g->stiffness = 5000.0f;
        g->damping = 100.0f;
        g->enable_angular_constraint = true;
        g->angular_stiffness = 50.0f;
        g->angular_damping = 5.0f;
        physics.add_gluon_between(ids[bd.a], ids[bd.b], std::move(g));
    }

    int build(ParticleSystem& ps, PhysicsSystem& physics, const Case& c,
              float x_off = 0.0f) {
        specs = c.bodies;
        ids.assign(specs.size(), -1);
        for (size_t i = 0; i < specs.size(); ++i)
            if (!specs[i].late) create(ps, (int)i, x_off);
        ps.flush_pending_particles();
        for (const Bond& bd : c.bonds) bond(ps, physics, bd);
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] >= 0) argus.watch(ids[i], specs[i].label);
        birth_overlap = worst_overlap(ps).pen;
        first_sleep_overlap = -1.0f; first_sleep_frame = -1; frames_run = 0;
        return (int)ids.size();
    }

    // D: the floor arrives after the stone (a chunk streaming in around
    // a body that already exists). Same creation path as build().
    void create_late(ParticleSystem& ps, float x_off) {
        for (size_t i = 0; i < specs.size(); ++i)
            if (specs[i].late && ids[i] < 0) create(ps, (int)i, x_off);
        ps.flush_pending_particles();
        for (size_t i = 0; i < ids.size(); ++i)
            if (specs[i].late) argus.watch(ids[i], specs[i].label);
        birth_overlap = std::fmax(birth_overlap, worst_overlap(ps).pen);
    }

    // TELEPORT LAW (scene_limits::rearm): the window replays a case.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics, const Case& c,
               float x_off) {
        auto parts = ps.lock_particles_for_write();
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] < 0) continue;
            Particle& p = parts[ids[i]];
            const BodySpec& b = specs[i];
            p.x = x_off + b.x; p.y = b.y; p.z = b.z;
            p.vx = p.vy = p.vz = 0.0f;
            p.omega_x = p.omega_y = p.omega_z = 0.0f;
            p.rotation_x = p.rotation_y = p.rotation_z = 0.0f;
            p.rotation_q = logosphere::Quat::identity();
            p.is_at_rest = false;
            p.frames_at_rest = 0;
            p.low_velocity_frames = 0;
            p.quiet_growth_run = 0;
            p.rest_quiet_sq = 1e9f;
            p.solver_mode = ParticleSolverMode::DYNAMIC;
            physics.forget_body((size_t)ids[i]);
            argus.reset_milestones(ids[i]);
        }
        (void)c;
        first_sleep_overlap = -1.0f; first_sleep_frame = -1; frames_run = 0;
    }

    void set_frozen(ParticleSystem& ps, bool frozen) {
        auto parts = ps.lock_particles_for_write();
        for (int id : ids)
            if (id >= 0)
                parts[id].solver_mode = frozen ? ParticleSolverMode::KINEMATIC
                                               : ParticleSolverMode::DYNAMIC;
    }

    // ONE step for both drivers (the timestep trap).
    void step(ParticleSystem& ps, PhysicsSystem& physics, const Case& c,
              int frame, float x_off = 0.0f) {
        if (frame == c.late_frame) create_late(ps, x_off);
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
        frames_run = frame + 1;
        if (first_sleep_frame < 0) {
            for (size_t i = 0; i < ids.size(); ++i)
                if (ids[i] >= 0 && asleep(ps, (int)i)) {
                    first_sleep_frame = frame;
                    first_sleep_overlap = worst_overlap(ps).pen;
                    break;
                }
        }
    }

    float z(ParticleSystem& ps, int i) const {
        return ids[i] < 0 ? -1.0f : ps.lock_particles_for_read()[ids[i]].z;
    }
    float spin(ParticleSystem& ps, int i) const {
        if (ids[i] < 0) return 0.0f;
        const Particle& p = ps.lock_particles_for_read()[ids[i]];
        return std::sqrt(p.omega_x*p.omega_x + p.omega_y*p.omega_y
                       + p.omega_z*p.omega_z);
    }
    bool asleep(ParticleSystem& ps, int i) const {
        return ids[i] >= 0 && ps.lock_particles_for_read()[ids[i]].is_at_rest;
    }
    bool all_asleep(ParticleSystem& ps) const {
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] >= 0 && !asleep(ps, (int)i)) return false;
        return true;
    }
    int asleep_count(ParticleSystem& ps) const {
        int n = 0;
        for (size_t i = 0; i < ids.size(); ++i) n += asleep(ps, (int)i);
        return n;
    }

    // INV-2 measured directly (scene_limits::worst_overlap): axis-aligned
    // on birth dims; exact here, nothing rotates on this stage.
    struct Overlap { float pen = 0.0f; int a = -1, b = -1; };
    Overlap worst_overlap(ParticleSystem& ps) const {
        Overlap w;
        auto parts = ps.lock_particles_for_read();
        const int n = (int)ids.size();
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b) {
                if (ids[a] < 0 || ids[b] < 0) continue;
                const Particle& pa = parts[ids[a]];
                const Particle& pb = parts[ids[b]];
                const float d[3]  = {pa.x - pb.x, pa.y - pb.y, pa.z - pb.z};
                const float ha[3] = {specs[a].sx, specs[a].sy, specs[a].sz};
                const float hb[3] = {specs[b].sx, specs[b].sy, specs[b].sz};
                float pen = 1e9f;
                for (int ax = 0; ax < 3; ++ax)
                    pen = std::fmin(pen, (ha[ax] + hb[ax]) * 0.5f
                                             - std::fabs(d[ax]));
                if (pen > w.pen) { w.pen = pen; w.a = a; w.b = b; }
            }
        return w;
    }
};

// ---------------------------------------------------------------------------
// THE CASE TABLE. Geometry and bars in one place; derivations in G-67/G-68.
// ---------------------------------------------------------------------------
inline BodySpec tile(bool late = false) {
    return BodySpec{TILE_L, TILE_L, TILE_T, 0.0f, 0.0f, TILE_Z,
                    Materials::Type::STONE, "tile", late};
}
inline BodySpec rubble(float z, const char* label = "rubble") {
    return BodySpec{RUB_X, RUB_Y, RUB_Z, 0.0f, 0.0f, z,
                    Materials::Type::STONE, label};
}

inline std::vector<Case> cases() {
    std::vector<Case> v;
    // A. Eden's stone, Eden's tile: rubble centre = tile centre.
    {
        Case c;
        c.name = "A THE STONE BORN IN THE FLOOR";
        c.gid = "G-67";
        c.bodies = {tile(), rubble(TILE_Z)};
        c.rubble = 1;
        c.rubble_z_final = TILE_TOP + RUB_Z * 0.5f;   // 0.43: on the tile
        c.demo1 = "DEMONSTRATING: a stone born inside a floor tile (Eden's "
                  "recipe) must be born ON it (INV-4), leave it (INV-2), sleep";
        c.demo2 = "WATCH: rubble z (0.15 -> 0.43?), asleep count, the solver's exit";
        v.push_back(c);
    }
    // B. The clump: trunk on the tile, two leaves bonded to its top,
    //    overlapping each other 2 cm by construction (leaves at y +-0.24,
    //    half-height 0.25). Quiet from birth. Nothing pushes.
    {
        Case c;
        c.name = "B THE JAMMED CLUMP";
        c.gid = "G-67";
        const float trunk_h = 1.2f, trunk_z = TILE_TOP + trunk_h * 0.5f; // 0.90
        const float top_z = trunk_z + trunk_h * 0.5f;                     // 1.50
        c.bodies = {tile(),
                    BodySpec{0.3f, 0.3f, trunk_h, 0.0f, 0.0f, trunk_z,
                             Materials::Type::WOOD_SOFT, "trunk"},
                    BodySpec{0.5f, 0.5f, 0.06f, 0.0f, +0.24f, top_z,
                             Materials::Type::LEAVES, "leaf+"},
                    BodySpec{0.5f, 0.5f, 0.06f, 0.0f, -0.24f, top_z,
                             Materials::Type::LEAVES, "leaf-"}};
        // Attachment: trunk top centre <-> leaf centre, rest = 0.24 m
        // (born at strain 1.0, INV-4's bond clause).
        c.bonds = {Bond{1, 2, 0, 0, trunk_h * 0.5f, 0, 0, 0, 0.24f},
                   Bond{1, 3, 0, 0, trunk_h * 0.5f, 0, 0, 0, 0.24f}};
        c.birth_overlap_asserted = false;
        c.rest_overlap_asserted = false;
        c.waiver = "INV-2/INV-4 on the clump: the leaves overlap 2 cm BY "
                   "DESIGN (the structural band, owner decision (a) on the "
                   "board) - measured, not asserted; G-67 asks only that "
                   "it SLEEPS";
        c.demo1 = "DEMONSTRATING: a bonded clump at rest in its own overlap is "
                  "jammed, not repairing - it must fall asleep (G-67)";
        c.demo2 = "WATCH: asleep count reaching 4/4, bonds 2 -> 2, exit kind";
        v.push_back(c);
    }
    // C. G-48's stack: the upper cube born 2 cm into the lower, free to
    //    separate. The repair must run to the end before sleep.
    {
        Case c;
        c.name = "C THE INTERLOCKED STACK (G-48 guard)";
        c.gid = "G-48";
        const float L = 0.5f;
        const float lower_z = TILE_TOP + L * 0.5f;          // 0.55
        const float upper_z = lower_z + L - 0.02f;          // 1.03: 2 cm in
        c.bodies = {tile(),
                    BodySpec{L, L, L, 0.0f, 0.0f, lower_z,
                             Materials::Type::STONE, "lower"},
                    BodySpec{L, L, L, 0.0f, 0.0f, upper_z,
                             Materials::Type::STONE, "upper"}};
        c.stack_upper = 2;
        c.stack_upper_z_final = lower_z + L;                // 1.05
        c.birth_overlap_asserted = false;
        c.waiver = "INV-4 at birth: the 2 cm interlock IS the experiment "
                   "(G-48's own case); the repair, not the birth, is under "
                   "test";
        c.demo1 = "DEMONSTRATING: a cube born 2 cm into another must NOT sleep "
                  "until separated (G-48), then separate (INV-2) and sleep";
        c.demo2 = "WATCH: upper z 1.03 -> 1.05, first-sleep overlap, exit kind";
        v.push_back(c);
    }
    // D. The stone first, on the turtle; the tile arrives at frame 60
    //    around it (chunk streaming).
    {
        Case c;
        c.name = "D THE FLOOR ARRIVES AFTER THE STONE";
        c.gid = "G-68";
        c.bodies = {tile(/*late=*/true), rubble(RUB_Z * 0.5f)};   // bottom on the turtle
        c.late_frame = 60;
        c.rubble = 1;
        c.rubble_z_final = TILE_TOP + RUB_Z * 0.5f;   // 0.43
        c.demo1 = "DEMONSTRATING: a tile created around a sleeping stone may not "
                  "be born through it (INV-4/G-68); the stone ends ON the tile";
        c.demo2 = "WATCH: frame 60 - the tile appears; rubble z 0.13 -> 0.43?";
        v.push_back(c);
    }
    return v;
}

// ---------------------------------------------------------------------------
// THE EVALUATOR: one list of law-tagged verdicts, read by the headless
// driver (printed) and by the window (the live panel), from the same
// helpers and latches. Every DOF of the narration is here or waived.
// ---------------------------------------------------------------------------
inline std::vector<Verdict> evaluate(ParticleSystem& ps, PhysicsSystem& physics,
                                     const Scene& s, const Case& c) {
    std::vector<Verdict> v;
    char t[224];
    const int n = (int)s.ids.size();
    const PhysicsSystem::SolveStats& st = physics.last_solve();
    const bool exhausted = std::strcmp(st.exit, "iteration_budget_exhausted") == 0;

    // INV-4: nothing is born inside anything (the creation frame's overlap).
    if (c.birth_overlap_asserted) {
        std::snprintf(t, sizeof t, "%s/INV-4: nothing is BORN inside anything "
                      "(creation overlap %.1f mm <= %.0f mm)", c.gid,
                      s.birth_overlap * 1000.0f, OVERLAP_TOL * 1000.0f);
        v.push_back({t, s.birth_overlap <= OVERLAP_TOL});
    }
    // INV-2: no standing interpenetration at the end.
    {
        const Scene::Overlap w = s.worst_overlap(ps);
        if (c.rest_overlap_asserted) {
            std::snprintf(t, sizeof t, "%s/INV-2: no standing interpenetration "
                          "(worst %.1f mm%s%s%s < %.0f mm)", c.gid, w.pen * 1000.0f,
                          w.a >= 0 ? ", " : "", w.a >= 0 ? c.bodies[w.a].label : "",
                          w.a >= 0 ? (std::string("<->") + c.bodies[w.b].label).c_str() : "",
                          OVERLAP_TOL * 1000.0f);
            v.push_back({t, w.pen < OVERLAP_TOL});
        }
    }
    // The story body's height (INV-2): out of the floor / on the tile.
    if (c.rubble >= 0 && c.rubble_z_final > 0.0f) {
        const float zr = s.z(ps, c.rubble);
        std::snprintf(t, sizeof t, "%s/INV-2: the rubble rests ON the tile "
                      "(z %.3f vs %.2f, tol %.2f)", c.gid, zr, c.rubble_z_final,
                      HEIGHT_TOL);
        v.push_back({t, std::fabs(zr - c.rubble_z_final) < HEIGHT_TOL});
    }
    if (c.stack_upper >= 0) {
        const float zu = s.z(ps, c.stack_upper);
        std::snprintf(t, sizeof t, "%s/INV-2: the upper cube is pushed OUT "
                      "(z %.3f vs %.2f, tol %.2f)", c.gid, zu, c.stack_upper_z_final,
                      HEIGHT_TOL);
        v.push_back({t, std::fabs(zu - c.stack_upper_z_final) < HEIGHT_TOL});
        // G-48: no sleep while the repair is still running.
        std::snprintf(t, sizeof t, "%s: nobody sleeps mid-repair (first sleep at "
                      "f%d with overlap %.1f mm < %.0f mm)", c.gid,
                      s.first_sleep_frame, s.first_sleep_overlap * 1000.0f,
                      OVERLAP_TOL * 1000.0f);
        v.push_back({t, s.first_sleep_frame >= 0 &&
                        s.first_sleep_overlap < OVERLAP_TOL});
    }
    // The tile stands on the turtle (INV-2), every case.
    {
        const float zt = s.z(ps, 0);
        const bool present = s.ids[0] >= 0;
        std::snprintf(t, sizeof t, "%s/INV-2: the tile stands on the turtle "
                      "(z %.3f vs %.2f)", c.gid, zt, TILE_Z);
        v.push_back({t, present && std::fabs(zt - TILE_Z) < HEIGHT_TOL});
    }
    // INV-34: nothing spins on this stage.
    {
        float worst = 0.0f;
        for (int i = 0; i < n; ++i) worst = std::fmax(worst, s.spin(ps, i));
        std::snprintf(t, sizeof t, "%s/INV-34: no spin is invented (worst %.4f "
                      "< %.2f)", c.gid, worst, SPIN_NOISE);
        v.push_back({t, worst < SPIN_NOISE});
    }
    // INV-14: the clump's bonds survive (B only has bonds).
    if (!c.bonds.empty()) {
        const size_t live = physics.get_total_gluon_count();
        std::snprintf(t, sizeof t, "%s/INV-14: every bond survives (%zu of %zu)",
                      c.gid, live, c.bonds.size());
        v.push_back({t, live == c.bonds.size()});
    }
    // INV-31 / G-67: the quiet stage falls ASLEEP.
    {
        std::snprintf(t, sizeof t, "%s/INV-31: the quiet stage falls ASLEEP "
                      "(%d of %d bodies at rest after %d frames)", c.gid,
                      s.asleep_count(ps), n, s.frames_run);
        v.push_back({t, s.all_asleep(ps)});
    }
    // G-67 THE COST WITNESS: the solver leaves by a door, not by the wall.
    {
        std::snprintf(t, sizeof t, "%s: the solver CONVERGES at rest (last exit "
                      "'%s', %d iterations, %d rows)", c.gid, st.exit,
                      st.iterations, st.rows);
        v.push_back({t, !exhausted});
    }
    // INV-11: speeds bounded.
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
            if (s.ids[i] >= 0) pk = std::fmax(pk, s.argus.peak_speed(s.ids[i]));
        std::snprintf(t, sizeof t, "INV-11: speeds stay bounded (peak %.3f < %.0f)",
                      pk, SPEED_MAX);
        v.push_back({t, pk < SPEED_MAX});
    }
    return v;
}

// A driver: fresh engine per case (no cross-case contamination).
inline int run_all(const char* title) {
    const std::vector<Case> cs = cases();
    const char* only_env = std::getenv("JAMMED_CASE");
    const int only = only_env ? std::atoi(only_env) : -1;
    std::printf("\n=== %s ===\n", title);
    std::printf("  WORLD: DEFAULT = the single law (INV-36); "
                "SLEEP_LAW_OFF=1 is the diagnostic switch (never a fix)\n");
    int failures = 0;
    for (size_t i = 0; i < cs.size(); ++i) {
        if (only >= 0 && (int)i != only) continue;
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene s;
        const Case& c = cs[i];
        s.build(ps, physics, c);
        for (int f = 0; f < c.run_frames; ++f) s.step(ps, physics, c, f);
        std::printf("\n-- %s --\n", c.name);
        for (size_t b = 0; b < c.bodies.size(); ++b)
            std::printf("  [measure] %-8s z %.4f  spin %.4f  peak speed %.4f%s\n",
                        c.bodies[b].label, s.z(ps, (int)b), s.spin(ps, (int)b),
                        s.ids[b] >= 0 ? s.argus.peak_speed(s.ids[b]) : 0.0f,
                        s.asleep(ps, (int)b) ? "  [asleep]" : "");
        const Scene::Overlap w = s.worst_overlap(ps);
        std::printf("  [measure] birth overlap %.1f mm, end overlap %.1f mm, "
                    "first sleep f%d at %.1f mm, solver exit '%s' (%d iters, "
                    "%d rows)\n", s.birth_overlap * 1000.0f, w.pen * 1000.0f,
                    s.first_sleep_frame, s.first_sleep_overlap * 1000.0f,
                    physics.last_solve().exit, physics.last_solve().iterations,
                    physics.last_solve().rows);
        if (c.waiver) std::printf("  [WAIVED] %s\n", c.waiver);
        for (const Verdict& vd : evaluate(ps, physics, s, c)) {
            std::printf("  %s %s\n", vd.ok ? "[PASS]" : "[FAIL]", vd.text.c_str());
            if (!vd.ok) failures++;
        }
        physics.shutdown();
    }
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "EVERY CASE ANSWERS ITS LAW"
                              : "BORN RED where the chain is broken (TDD "
                                "flags, owner order 2026-09-01)",
                failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace scene_jammed
