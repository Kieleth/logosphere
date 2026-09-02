// =============================================================================
// SCENE: THE STONE AND THE SLAB — G-67 / G-68 / G-48 (owner orders
// 2026-09-01: "TDD all this first of any change", then, watching the
// first window: "argus each, these tests are mostly wrong, they do
// nothing, and nothing moves/checks anything").
// =============================================================================
// The frame-collapse RCA (GEDANKEN-67) found Eden at 3.4 s of physics per
// frame because ~900 quiet bodies could never sleep: stones born INSIDE
// strata tiles, held awake by G-48's sleep veto on contact overlap,
// dragging 30,000 contact rows through an exhausted budget every substep.
// Before any fix, the chain gets four EXPERIMENTS on one real stage (a
// stone strata tile on the turtle, stone rubble, both at Eden's own
// sizes and masses), each with a staged event, a control beside it, and
// every interaction read through Argus and the contact stream.
//
//   A  THE STONE BORN IN THE FLOOR (G-67). Two identical stones: one
//      DROPPED onto the tile from 1.5 m (strikes at ~4.6 m/s, settles,
//      sleeps), one born at the tile's own centre height, Eden's recipe.
//      The control lives; the buried one holds the tile and the solver
//      awake forever.
//   B  THE SLAB LANDS ON THE STONE (G-67's inverse). A stone sleeps on
//      the ground; a 12-ton tile is dropped on it from 0.8 m and must
//      come to rest tilted on the stone with one edge on the ground,
//      the stone uncrushed (INV-1). A second tile dropped beside it
//      lands flat. The repair's mass law, seen from the other side.
//   C  THE STRUCK STACK (G-48, the guard). Nothing is born in overlap
//      (INV-37, so the interlock cannot be a birth): a 1 m cube is
//      DROPPED 0.5 m onto the cube under it, the impact's transient
//      penetration is the repair G-48 protects, and the pair may sleep
//      only once separated again; a touching stack beside it sleeps
//      at once.
//   D  THE FLOOR ARRIVES AFTER THE STONE (G-68). Two stones sleep on a
//      REAL floor (a base tile on the turtle - no demo stands on the
//      bare boundary, owner rule 2026-08-28); at frame 60 a second
//      layer arrives: one tile THROUGH the stones, one tile BESIDE
//      them. OWNER DECREE 2026-09-01 (INV-37): "under no circumstances,
//      any creation of particles should be allowed to overlap in
//      space with another" - the door REFUSES the tile through the
//      stones and ADMITS the tile beside them; the stones stay asleep
//      on the base, nothing moves. Likewise A's buried stone is
//      refused, never born: the tile keeps its one dropped stone.
//
// THE COST WITNESS on every case (G-67): once the stage is quiet the
// solver leaves by convergence, never by exhausting its budget, read
// from PhysicsSystem::last_solve().
//
// FULL-STATE NARRATION (assert or waive, per DOF, per case):
//   tile:   z (INV-2 stands / INV-7 is not lifted by a stone's repair),
//           rotation_y (B: the derived tilt; elsewhere zero), speed
//           (Argus peak), asleep. x/y waived: nothing pushes sideways.
//   stone:  z (rests ON the tile or the ground), min z over the run
//           (INV-1: never pressed into the world), the strike
//           (first contact frame and relative speed from the contact
//           stream), Argus peak speed, spin (INV-34), asleep.
//   cubes:  the strike (contact stream), peak transient overlap (latched),
//           separation (Argus) back to 1.00, first sleep only after the
//           repair, asleep. Rotation waived: a centred stack has no torque.
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
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace scene_jammed {

constexpr float DT = 1.0f / 60.0f;
constexpr float G = 9.81f;
constexpr float MU = 0.5f;               // explicit on every body
constexpr float OVERLAP_TOL = 0.002f;    // INV-2's 2x SLOP bar
constexpr float SPIN_NOISE = 0.05f;      // rad/s (INV-34)
constexpr float SPEED_MAX = 10.0f;       // m/s (INV-11)
constexpr float HEIGHT_TOL = 0.01f;      // m
constexpr float STILL = 0.02f;           // m/s: a 12-ton floor that "moves" less is still
constexpr int   SLEEP_GRACE = 90;        // frames after the last event to be asleep

// THE STAGE: Eden's strata tile (4 x 4 x 0.3 m, STONE 2500 kg/m^3 =
// 12,000 kg) on the turtle: centre z 0.15, top 0.30.
constexpr float TILE_L = 4.0f, TILE_T = 0.3f, TILE_Z = 0.15f;
constexpr float TILE_TOP = TILE_Z + TILE_T * 0.5f;
// Eden's rubble recipe (examples/eden/src/main.cpp): the deepest pair
// measured in the census was 0.31 x 0.21 x 0.26 at z 0.13-0.15.
constexpr float RUB_X = 0.31f, RUB_Y = 0.21f, RUB_Z = 0.26f;
constexpr float RUB_ON_TILE = TILE_TOP + RUB_Z * 0.5f;   // 0.43
constexpr float RUB_ON_GROUND = RUB_Z * 0.5f;            // 0.13

inline float fall_speed(float h) { return std::sqrt(2.0f * G * h); }
inline int   fall_frames(float h) { return (int)std::ceil(std::sqrt(2.0f * h / G) / DT); }

struct BodySpec {
    float sx, sy, sz;
    float x, y, z;
    Materials::Type mat;
    const char* label;
    bool late = false;     // D: created at Case::late_frame, not at build
};

struct Strike {             // an instrumented impact: who hits whom, how fast
    int a, b;
    int by_frame;           // must have happened by this frame
    float v_lo, v_hi;       // relative speed at first contact, m/s
};
struct Height { int body; float z; };
struct Still  { int body; };                // Argus peak speed < STILL
struct Tilt   { int body; float lo, hi; };  // |rotation_y| band, rad
struct Sep    { int a, b; float lo, hi; };  // Argus separation band at the end
struct VSep   { int a, b; float lo, hi; };  // vertical separation band (Argus z's): rests ON

struct Case {
    const char* name;
    const char* gid;
    std::vector<BodySpec> bodies;
    int run_frames = 600;
    int late_frame = -1;
    std::vector<Strike> strikes;
    std::vector<Height> heights;            // INV-2: rests at
    std::vector<Still>  stills;             // INV-7: not moved by a repair
    std::vector<Tilt>   tilts;
    std::vector<Sep>    seps;
    std::vector<VSep>   vseps;
    std::vector<int>    ground_pinned;      // INV-1: min z >= birth z - tol
    std::vector<int>    sleep_by_event;     // INV-31: asleep within SLEEP_GRACE of last strike
    int repair_a = -1, repair_b = -1;       // G-48: first sleep only after separation >= repair_done
    float repair_done = 0.0f;
    int born_overlap_pair_a = -1, born_overlap_pair_b = -1;   // INV-37 (creation frame)
    int refused = -1;                       // INV-37: this creation must be REFUSED (absent)
    std::vector<int> admitted;              // INV-37: these creations must be BORN (on stage)
    float refusal_depth = -1.0f;            // INV-37: the door's own reading of the refused depth (m)
    bool rest_overlap_asserted = true;      // INV-2 at the end (axis-aligned measure)
    int manifold_pair_a = -1, manifold_pair_b = -1;   // INV-2 through the engine's own contact (tilted bodies)
    const char* waiver = nullptr;
    const char* demo1 = "";
    const char* demo2 = "";
};

struct Verdict { std::string text; bool ok; };

struct Scene {
    logosphere::Argus argus;
    std::vector<int> ids;
    std::vector<BodySpec> specs;
    // Latches (one source for the headless asserts and the live panel).
    float birth_overlap = 0.0f;
    // THE DOOR'S OWN RECORD (feat/creation-door): what it refused and how deep.
    bool  door_refused = false;
    float door_depth = 0.0f;
    std::map<std::pair<int,int>, int>   first_contact_frame;
    std::map<std::pair<int,int>, float> first_contact_speed;
    std::map<std::pair<int,int>, float> last_penetration;   // the manifold's own, this frame
    std::vector<int>   first_sleep_frame;
    std::vector<float> min_z;
    float peak_overlap = 0.0f;           // the transient the repair must undo
    float repair_sep_at_first_sleep = -1.0f;
    int   last_event_frame = 0;
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
        ids[i] = ps.queue_particle_addition(p);
        // The door judges at queue time (it hands back -1 for a refused
        // body); latch its record here, before the next birth overwrites it.
        if (ps.last_creation_refusal().refused) {
            door_refused = true;
            door_depth = ps.last_creation_refusal().depth;
        }
        return ids[i];
    }

    void reset_latches() {
        first_contact_frame.clear(); first_contact_speed.clear(); last_penetration.clear();
        first_sleep_frame.assign(specs.size(), -1);
        min_z.assign(specs.size(), 1e9f);
        peak_overlap = 0.0f;
        repair_sep_at_first_sleep = -1.0f;
        last_event_frame = 0; frames_run = 0;
    }

    int build(ParticleSystem& ps, PhysicsSystem& physics, const Case& c,
              float x_off = 0.0f) {
        (void)physics;
        specs = c.bodies;
        ids.assign(specs.size(), -1);
        for (size_t i = 0; i < specs.size(); ++i)
            if (!specs[i].late) create(ps, (int)i, x_off);
        ps.flush_pending_particles();
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] >= 0) argus.watch(ids[i], specs[i].label);
        reset_latches();
        birth_overlap = worst_overlap(ps).pen;
        return (int)ids.size();
    }

    // D: the floor arrives after the stone (a chunk streaming in around a
    // body that already exists). Same creation path as build().
    void create_late(ParticleSystem& ps, float x_off, int frame) {
        for (size_t i = 0; i < specs.size(); ++i)
            if (specs[i].late && ids[i] < 0) create(ps, (int)i, x_off);
        ps.flush_pending_particles();
        for (size_t i = 0; i < ids.size(); ++i)
            if (specs[i].late) argus.watch(ids[i], specs[i].label);
        birth_overlap = std::fmax(birth_overlap, worst_overlap(ps).pen);
        last_event_frame = frame;
    }

    // TELEPORT LAW (scene_limits::rearm): the window replays a case.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics, float x_off) {
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
        reset_latches();
    }

    void set_frozen(ParticleSystem& ps, bool frozen) {
        auto parts = ps.lock_particles_for_write();
        for (int id : ids)
            if (id >= 0)
                parts[id].solver_mode = frozen ? ParticleSolverMode::KINEMATIC
                                               : ParticleSolverMode::DYNAMIC;
    }

    int index_of(size_t pid) const {
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == (int)pid) return (int)i;
        return -1;
    }

    // ONE step for both drivers (the timestep trap). Latches the strikes
    // from the contact stream, the first sleep, the minimum height.
    void step(ParticleSystem& ps, PhysicsSystem& physics, const Case& c,
              int frame, float x_off = 0.0f) {
        if (frame == c.late_frame) create_late(ps, x_off, frame);
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
        frames_run = frame + 1;
        for (auto& kv : last_penetration) kv.second = 0.0f;
        for (const CollisionEvent& e : physics.get_collision_events()) {
            const int a = index_of(e.particle_a), b = index_of(e.particle_b);
            if (a < 0 || b < 0 || a == b) continue;
            const std::pair<int,int> k{std::min(a, b), std::max(a, b)};
            last_penetration[k] = std::fmax(last_penetration[k], e.penetration);
            if (!first_contact_frame.count(k)) {
                first_contact_frame[k] = frame;
                first_contact_speed[k] = std::fabs(e.relative_velocity);
                last_event_frame = std::max(last_event_frame, frame);
            }
        }
        peak_overlap = std::fmax(peak_overlap, worst_overlap(ps).pen);
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] < 0) continue;
            min_z[i] = std::fmin(min_z[i], z(ps, (int)i));
            if (first_sleep_frame[i] < 0 && asleep(ps, (int)i)) {
                first_sleep_frame[i] = frame;
                // G-48 reads the STRUCK body's first sleep: the lower cube
                // sleeping at f9 while the upper is still in the air says
                // nothing about the repair.
                if (c.repair_a >= 0 && repair_sep_at_first_sleep < 0.0f &&
                    (int)i == c.repair_b)
                    repair_sep_at_first_sleep =
                        argus.separation(ids[c.repair_a], ids[c.repair_b]);
            }
        }
    }

    float z(ParticleSystem& ps, int i) const {
        return ids[i] < 0 ? -1.0f : ps.lock_particles_for_read()[ids[i]].z;
    }
    float rot_y(ParticleSystem& ps, int i) const {
        return ids[i] < 0 ? 0.0f : ps.lock_particles_for_read()[ids[i]].rotation_y;
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
            if (alive(ps, (int)i) && !asleep(ps, (int)i)) return false;
        return true;
    }
    int asleep_count(ParticleSystem& ps) const {
        int n = 0;
        for (size_t i = 0; i < ids.size(); ++i) n += asleep(ps, (int)i);
        return n;
    }
    // The bodies ON STAGE: a refused creation is not one (INV-37), so
    // every "x of y" reads y from here, never from the case table.
    int alive_count(ParticleSystem& ps) const {
        int n = 0;
        for (size_t i = 0; i < ids.size(); ++i) n += alive(ps, (int)i);
        return n;
    }
    // A body's height for the readout: "refused" when it is not on stage.
    std::string z_text(ParticleSystem& ps, int i) const {
        char b[32];
        if (!alive(ps, (int)i)) return "refused";
        std::snprintf(b, sizeof b, "%.3f", z(ps, i));
        return b;
    }
    float peak_speed(int i) const { return ids[i] < 0 ? 0.0f : argus.peak_speed(ids[i]); }
    // A refused creation leaves no body on stage (INV-37): no id, or an id
    // the flush dropped (mass 0 / beyond the live range).
    bool alive(ParticleSystem& ps, int i) const {
        if (ids[i] < 0) return false;
        auto v = ps.lock_particles_for_read();
        return (size_t)ids[i] < v.size() && v[ids[i]].GetMass() > 0.0f;
    }
    float separation(int a, int b) const {
        return (ids[a] < 0 || ids[b] < 0) ? -1.0f : argus.separation(ids[a], ids[b]);
    }
    // Vertical separation from Argus' latest states: "rests ON" for a
    // body that is not above the other's centre.
    float vseparation(int a, int b) const {
        if (ids[a] < 0 || ids[b] < 0) return -1.0f;
        const logosphere::Argus::State* pa = argus.latest(ids[a]);
        const logosphere::Argus::State* pb = argus.latest(ids[b]);
        return (pa && pb) ? std::fabs(pa->z - pb->z) : -1.0f;
    }
    int   strike_frame(int a, int b) const {
        auto it = first_contact_frame.find({std::min(a, b), std::max(a, b)});
        return it == first_contact_frame.end() ? -1 : it->second;
    }
    float contact_penetration(int a, int b) const {
        auto it = last_penetration.find({std::min(a, b), std::max(a, b)});
        return it == last_penetration.end() ? 0.0f : it->second;
    }
    float strike_speed(int a, int b) const {
        auto it = first_contact_speed.find({std::min(a, b), std::max(a, b)});
        return it == first_contact_speed.end() ? 0.0f : it->second;
    }

    // INV-2 measured directly: axis-aligned on birth dims. Exact for the
    // unrotated bodies; for B's tilted slab it is conservative by the
    // tilt (7 deg: a few mm on a 4 m slab), stated in the assert text.
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
inline BodySpec tile(float x, float z = TILE_Z, const char* label = "tile",
                     bool late = false) {
    return BodySpec{TILE_L, TILE_L, TILE_T, x, 0.0f, z,
                    Materials::Type::STONE, label, late};
}
inline BodySpec rubble(float x, float z, const char* label) {
    return BodySpec{RUB_X, RUB_Y, RUB_Z, x, 0.0f, z, Materials::Type::STONE, label};
}

inline std::vector<Case> cases() {
    std::vector<Case> v;
    // A. Two identical stones on Eden's tile: one dropped (the control),
    //    one born at the tile's centre height (Eden's recipe).
    //    Drop: 1.5 - 0.43 = 1.07 m of fall, 4.58 m/s at the strike, 28 frames.
    {
        Case c;
        c.name = "A THE STONE BORN IN THE FLOOR";
        c.gid = "G-67";
        const float drop_z = 1.5f;
        c.bodies = {tile(0.0f), rubble(-1.2f, drop_z, "dropped"),
                    rubble(+1.2f, TILE_Z, "buried")};
        const float h = drop_z - RUB_ON_TILE;
        c.strikes = {Strike{1, 0, fall_frames(h) + 12,
                            fall_speed(h) * 0.75f, fall_speed(h) * 1.2f}};
        c.heights = {Height{1, RUB_ON_TILE}, Height{0, TILE_Z}};
        c.stills = {Still{0}};
        c.sleep_by_event = {1};
        c.born_overlap_pair_a = 0; c.born_overlap_pair_b = 2;
        c.refused = 2;
        c.refusal_depth = TILE_T * 0.5f + RUB_Z * 0.5f;          // 0.28: tile half + rubble half
        c.demo1 = "DEMONSTRATING: the dropped stone strikes, settles and sleeps; the "
                  "stone BORN in the tile (Eden's recipe) is REFUSED at creation (INV-37)";
        c.demo2 = "WATCH: the strike speed, 'buried' on stage or not, the tile LIFTED, "
                  "asleep 1/3, exit";
        v.push_back(c);
    }
    // B. The slab lands on the stone. Stone on the ground at x +0.3 (off
    //    centre so the slab tips to -x deterministically); the tile born
    //    at z 0.95 (bottom 0.80) falls 0.54 m onto the stone top (0.26):
    //    3.25 m/s at the strike, 20 frames. At rest: middle on the stone,
    //    the -x edge on the ground: tilt atan(0.26/2.3) = 0.113 rad,
    //    centre z ~ 0.26 + 0.15 cos(tilt) - 0.3 sin(tilt)... = 0.37-0.42.
    //    The control tile beside it lands flat and sleeps.
    {
        Case c;
        c.name = "B THE SLAB LANDS ON THE STONE";
        c.gid = "G-67";
        const float slab_z = 0.95f;
        c.bodies = {rubble(+0.3f, RUB_ON_GROUND, "stone"),
                    tile(0.0f, slab_z, "slab"),
                    tile(+5.0f, slab_z, "control")};
        const float h = slab_z - TILE_T * 0.5f - RUB_Z;
        c.strikes = {Strike{1, 0, fall_frames(h) + 12,
                            fall_speed(h) * 0.7f, fall_speed(h) * 1.2f}};
        c.tilts = {Tilt{1, 0.09f, 0.14f}, Tilt{2, 0.0f, 0.01f}};
        c.heights = {Height{2, TILE_Z}};
        c.ground_pinned = {0};
        c.rest_overlap_asserted = false;        // the slab is tilted: the AABB measure lies by the tilt
        c.manifold_pair_a = 0; c.manifold_pair_b = 1;
        c.sleep_by_event = {1, 2};
        c.waiver = "slab z at rest: implied by the tilt band and the stone's "
                   "height (the two bars above); not asserted twice. Stone x: "
                   "waived, the 12-ton strike may shove it a few cm";
        c.demo1 = "DEMONSTRATING: a 12-ton slab dropped on a stone comes to rest "
                  "TILTED on it (INV-2), the stone uncrushed (INV-1), both asleep";
        c.demo2 = "WATCH: the strike at ~3.2 m/s, slab tilt ~0.11 rad, control flat, "
                  "the stone's min z";
        v.push_back(c);
    }
    // C. G-48's guard, born legal (INV-37): the upper 1 m cube (2,500 kg)
    //    is DROPPED 0.5 m onto the lower one - 3.13 m/s at the strike, 19
    //    frames. The impact's transient penetration is the repair G-48
    //    protects: the pair may sleep only once its separation is back at
    //    1.00. A touching stack beside it sleeps at once.
    {
        Case c;
        c.name = "C THE STRUCK STACK (G-48 guard)";
        c.gid = "G-48";
        const float L = 1.0f, drop = 0.5f;
        const float lower_z = TILE_TOP + L * 0.5f;          // 0.80
        c.bodies = {tile(0.0f),
                    BodySpec{L, L, L, -1.2f, 0.0f, lower_z, Materials::Type::STONE, "lower"},
                    BodySpec{L, L, L, -1.2f, 0.0f, lower_z + L + drop, Materials::Type::STONE, "upper"},
                    BodySpec{L, L, L, +1.2f, 0.0f, lower_z, Materials::Type::STONE, "ctl-lower"},
                    BodySpec{L, L, L, +1.2f, 0.0f, lower_z + L, Materials::Type::STONE, "ctl-upper"}};
        c.strikes = {Strike{2, 1, fall_frames(drop) + 12,
                            fall_speed(drop) * 0.7f, fall_speed(drop) * 1.2f}};
        c.seps = {Sep{1, 2, L - OVERLAP_TOL, L + 0.01f}, Sep{3, 4, L - OVERLAP_TOL, L + 0.01f}};
        c.heights = {Height{2, lower_z + L}, Height{4, lower_z + L}, Height{0, TILE_Z}};
        c.stills = {Still{0}};
        c.repair_a = 1; c.repair_b = 2; c.repair_done = L - OVERLAP_TOL;
        c.sleep_by_event = {2, 3, 4};
        c.born_overlap_pair_a = 1; c.born_overlap_pair_b = 2;   // born legal, asserted
        c.waiver = "Cube rotation waived: a centred stack carries no torque. The "
                   "impact's peak transient overlap is measured, not bounded "
                   "(INV-2's transit clause is owed its own derivation)";
        c.demo1 = "DEMONSTRATING: a cube dropped on another may sleep only once the "
                  "impact's penetration is repaired (G-48); the touching stack sleeps at once";
        c.demo2 = "WATCH: the strike at ~3.1 m/s, peak transient overlap, separation back "
                  "to 1.00, first sleep frame, asleep 5/5";
        v.push_back(c);
    }
    // D. Two stones asleep on a base tile; at frame 60 a second layer
    //    arrives: one tile THROUGH the stones (its z-range 0.30-0.60
    //    against the stones' 0.30-0.56: 260 mm of overlap, refused) and
    //    one tile BESIDE them (x 6, clear of everything: admitted).
    {
        Case c;
        c.name = "D THE FLOOR ARRIVES AFTER THE STONE";
        c.gid = "G-68";
        const float layer2_z = TILE_TOP + TILE_T * 0.5f;   // 0.45: a second layer
        c.bodies = {tile(0.0f, TILE_Z, "base"),
                    rubble(0.0f, RUB_ON_TILE, "under"),
                    rubble(+0.9f, RUB_ON_TILE, "beside"),
                    tile(0.0f, layer2_z, "through", /*late=*/true),
                    tile(+6.0f, TILE_Z, "admitted", /*late=*/true)};
        c.late_frame = 60;
        // Argus separations: the stones rest ON the base (0.15 + 0.13).
        const float on_base = TILE_T * 0.5f + RUB_Z * 0.5f;
        c.vseps = {VSep{0, 1, on_base - HEIGHT_TOL, on_base + HEIGHT_TOL},
                   VSep{0, 2, on_base - HEIGHT_TOL, on_base + HEIGHT_TOL}};
        c.heights = {Height{0, TILE_Z}, Height{4, TILE_Z}};
        c.stills = {Still{0}, Still{1}, Still{2}};
        c.born_overlap_pair_a = 3; c.born_overlap_pair_b = 1;
        c.refused = 3;
        c.refusal_depth = TILE_T * 0.5f + RUB_Z * 0.5f - (layer2_z - RUB_ON_TILE);   // 0.26
        c.admitted = {4};
        c.demo1 = "DEMONSTRATING: a layer arriving THROUGH sleeping stones is REFUSED, "
                  "a tile arriving BESIDE them is born (INV-37/G-68); nothing moves";
        c.demo2 = "WATCH: frame 60 - one tile appears at x 6, none through the stones; "
                  "stones stay on the base, asleep 4/4 on stage";
        v.push_back(c);
    }
    return v;
}

// ---------------------------------------------------------------------------
// THE EVALUATOR: law-tagged verdicts from Argus, the contact stream and the
// latches - read by the headless driver (printed) and the window (the
// live panel). Every narrated DOF is here or waived in the case.
// ---------------------------------------------------------------------------
inline std::vector<Verdict> evaluate(ParticleSystem& ps, PhysicsSystem& physics,
                                     const Scene& s, const Case& c) {
    std::vector<Verdict> v;
    char t[240];
    const int n = (int)s.ids.size();
    const PhysicsSystem::SolveStats& st = physics.last_solve();
    const bool exhausted = std::strcmp(st.exit, "iteration_budget_exhausted") == 0;
    auto L = [&](int i) { return c.bodies[i].label; };

    // THE STRIKES (instrument the interaction): who hit whom, when, how fast.
    for (const Strike& k : c.strikes) {
        const int f = s.strike_frame(k.a, k.b);
        const float vs = s.strike_speed(k.a, k.b);
        std::snprintf(t, sizeof t, "hygiene: %s STRIKES %s by f%d at %.1f-%.1f m/s "
                      "(f%d, %.2f m/s)", L(k.a), L(k.b), k.by_frame, k.v_lo, k.v_hi,
                      f, vs);
        v.push_back({t, f >= 0 && f <= k.by_frame && vs >= k.v_lo && vs <= k.v_hi});
    }
    // INV-37 (owner decree 2026-09-01): no creation overlaps anything - the
    // creation frame's overlap, and the offending body REFUSED (absent).
    if (c.born_overlap_pair_a >= 0) {
        std::snprintf(t, sizeof t, "%s/INV-37: no creation overlaps anything "
                      "(%s vs %s at creation: %.0f mm <= %.0f mm)", c.gid,
                      L(c.born_overlap_pair_b), L(c.born_overlap_pair_a),
                      s.birth_overlap * 1000.0f, OVERLAP_TOL * 1000.0f);
        v.push_back({t, s.birth_overlap <= OVERLAP_TOL});
    }
    if (c.refused >= 0) {
        const bool on_stage = s.alive(ps, c.refused);
        std::snprintf(t, sizeof t, "%s/INV-37: the door REFUSES %s (%s)", c.gid,
                      L(c.refused), on_stage ? "it is on stage" : "absent, as ruled");
        v.push_back({t, !on_stage});
    }
    if (c.refused >= 0 && c.refusal_depth > 0.0f) {
        std::snprintf(t, sizeof t, "%s/INV-37: the door's own reading names the depth "
                      "(refused=%d, %.0f mm vs the derived %.0f mm)", c.gid,
                      (int)s.door_refused, s.door_depth * 1000.0f,
                      c.refusal_depth * 1000.0f);
        v.push_back({t, s.door_refused &&
                        std::fabs(s.door_depth - c.refusal_depth) < 0.02f});
    }
    for (int i : c.admitted) {
        const bool on_stage = s.alive(ps, i);
        std::snprintf(t, sizeof t, "%s/INV-37: the door ADMITS %s, born clear of everything "
                      "(%s)", c.gid, L(i), on_stage ? "on stage" : "MISSING");
        v.push_back({t, on_stage});
    }
    // INV-2: no standing interpenetration at the end.
    if (c.rest_overlap_asserted) {
        const Scene::Overlap w = s.worst_overlap(ps);
        std::snprintf(t, sizeof t, "%s/INV-2: no standing interpenetration (worst "
                      "%.1f mm%s%s%s < %.0f mm)", c.gid, w.pen * 1000.0f,
                      w.a >= 0 ? ", " : "", w.a >= 0 ? L(w.a) : "",
                      w.a >= 0 ? (std::string("<->") + L(w.b)).c_str() : "",
                      OVERLAP_TOL * 1000.0f);
        v.push_back({t, w.pen < OVERLAP_TOL});
    }
    // INV-2 through the engine's own manifold (a tilted pair the AABB measure
    // cannot judge): the contact's penetration at the end.
    if (c.manifold_pair_a >= 0) {
        const float pen = s.contact_penetration(c.manifold_pair_a, c.manifold_pair_b);
        std::snprintf(t, sizeof t, "%s/INV-2: %s<->%s contact penetration at rest "
                      "%.1f mm < %.0f mm (the manifold's own reading)", c.gid,
                      L(c.manifold_pair_a), L(c.manifold_pair_b), pen * 1000.0f,
                      OVERLAP_TOL * 1000.0f);
        v.push_back({t, pen < OVERLAP_TOL});
    }
    // INV-2: rests at the derived height.
    for (const Height& h : c.heights) {
        const float zz = s.z(ps, h.body);
        std::snprintf(t, sizeof t, "%s/INV-2: %s rests at z %.2f (%.3f, tol %.2f)",
                      c.gid, L(h.body), h.z, zz, HEIGHT_TOL);
        v.push_back({t, s.ids[h.body] >= 0 && std::fabs(zz - h.z) < HEIGHT_TOL});
    }
    // INV-7: a floor is not moved by a stone's repair (Argus peak speed).
    for (const Still& st_ : c.stills) {
        const float pk = s.peak_speed(st_.body);
        std::snprintf(t, sizeof t, "%s/INV-7: %s is NOT moved by anyone's repair "
                      "(peak speed %.3f < %.2f m/s)", c.gid, L(st_.body), pk, STILL);
        v.push_back({t, pk < STILL});
    }
    // The derived tilt (B): rotation_y band.
    for (const Tilt& ti : c.tilts) {
        const float ry = std::fabs(s.rot_y(ps, ti.body));
        std::snprintf(t, sizeof t, "%s/INV-2: %s rests with |rot_y| in [%.2f, %.2f] "
                      "(%.3f rad)", c.gid, L(ti.body), ti.lo, ti.hi, ry);
        v.push_back({t, ry >= ti.lo && ry <= ti.hi});
    }
    // Argus separation bands (C): the repair completes.
    for (const Sep& sp : c.seps) {
        const float d = s.separation(sp.a, sp.b);
        std::snprintf(t, sizeof t, "%s/INV-2: %s<->%s separation in [%.3f, %.3f] "
                      "(Argus %.3f m)", c.gid, L(sp.a), L(sp.b), sp.lo, sp.hi, d);
        v.push_back({t, d >= sp.lo && d <= sp.hi});
    }
    for (const VSep& sp : c.vseps) {
        const float d = s.vseparation(sp.a, sp.b);
        std::snprintf(t, sizeof t, "%s/INV-2: %s rests ON %s (vertical separation %.3f "
                      "in [%.3f, %.3f], Argus)", c.gid, L(sp.b), L(sp.a), d, sp.lo, sp.hi);
        v.push_back({t, d >= sp.lo && d <= sp.hi});
    }
    // G-48: nobody sleeps mid-repair.
    if (c.repair_a >= 0) {
        const int f = s.first_sleep_frame[c.repair_b];
        std::snprintf(t, sizeof t, "%s: %s sleeps only once the strike's penetration "
                      "is repaired (first sleep f%d at separation %.3f >= %.3f)", c.gid,
                      L(c.repair_b), f, s.repair_sep_at_first_sleep, c.repair_done);
        v.push_back({t, f >= 0 && s.repair_sep_at_first_sleep >= c.repair_done});
    }
    // INV-1: the ground is absolute - a stone is never pressed into it.
    for (int i : c.ground_pinned) {
        std::snprintf(t, sizeof t, "%s/INV-1: %s is never pressed into the ground "
                      "(min z %.4f >= %.4f)", c.gid, L(i), s.min_z[i],
                      c.bodies[i].z - OVERLAP_TOL);
        v.push_back({t, s.min_z[i] >= c.bodies[i].z - OVERLAP_TOL});
    }
    // INV-31: the bodies with an event sleep within the grace after it.
    for (int i : c.sleep_by_event) {
        const int f = s.first_sleep_frame[i];
        const int due = s.last_event_frame + SLEEP_GRACE;
        std::snprintf(t, sizeof t, "%s/INV-31: %s sleeps within %d frames of the "
                      "last event (asleep at f%d, due f%d)", c.gid, L(i), SLEEP_GRACE,
                      f, due);
        v.push_back({t, f >= 0 && f <= due});
    }
    // INV-34: nothing keeps spinning.
    {
        float worst = 0.0f;
        for (int i = 0; i < n; ++i) worst = std::fmax(worst, s.spin(ps, i));
        std::snprintf(t, sizeof t, "%s/INV-34: every spin is dead at the end (worst "
                      "%.4f < %.2f)", c.gid, worst, SPIN_NOISE);
        v.push_back({t, worst < SPIN_NOISE});
    }
    // INV-31 / G-67: the whole stage falls ASLEEP.
    {
        std::snprintf(t, sizeof t, "%s/INV-31: the quiet stage falls ASLEEP (%d of %d "
                      "on stage after %d frames)", c.gid, s.asleep_count(ps),
                      s.alive_count(ps), s.frames_run);
        v.push_back({t, s.all_asleep(ps)});
    }
    // G-67 THE COST WITNESS.
    {
        std::snprintf(t, sizeof t, "%s: the solver CONVERGES at rest (exit '%s', %d "
                      "iterations, %d rows)", c.gid, st.exit, st.iterations, st.rows);
        v.push_back({t, !exhausted});
    }
    // INV-11: speeds bounded.
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::fmax(pk, s.peak_speed(i));
        std::snprintf(t, sizeof t, "INV-11: speeds stay bounded (peak %.2f < %.0f)",
                      pk, SPEED_MAX);
        v.push_back({t, pk < SPEED_MAX});
    }
    return v;
}

// The live readout: the numbers at the moment they matter, per case,
// from the same latches the asserts read.
inline std::string readout(ParticleSystem& ps, PhysicsSystem& physics,
                           const Scene& s, const Case& c) {
    char b[240];
    const PhysicsSystem::SolveStats& st = physics.last_solve();
    if (c.repair_a < 0 && !c.strikes.empty()) {
        const Strike& k = c.strikes[0];
        std::snprintf(b, sizeof b,
                      "%s strike f%d @ %.2f m/s | %s z %s  %s z %s | asleep %d/%d on stage | "
                      "exit %s (%d it, %d rows)", c.bodies[k.a].label,
                      s.strike_frame(k.a, k.b), s.strike_speed(k.a, k.b),
                      c.bodies[0].label, s.z_text(ps, 0).c_str(),
                      c.bodies[c.bodies.size() - 1].label,
                      s.z_text(ps, (int)c.bodies.size() - 1).c_str(), s.asleep_count(ps),
                      s.alive_count(ps), st.exit, st.iterations, st.rows);
    } else if (c.repair_a >= 0) {
        std::snprintf(b, sizeof b,
                      "strike f%d @ %.2f m/s | peak overlap %.1f mm | separation %.3f "
                      "(first sleep f%d at %.3f) | asleep %d/%d on stage | exit %s (%d it, %d rows)",
                      s.strike_frame(c.repair_a, c.repair_b), s.strike_speed(c.repair_a, c.repair_b),
                      s.peak_overlap * 1000.0f, s.separation(c.repair_a, c.repair_b),
                      s.first_sleep_frame[c.repair_b], s.repair_sep_at_first_sleep,
                      s.asleep_count(ps), s.alive_count(ps), st.exit, st.iterations, st.rows);
    } else {
        std::snprintf(b, sizeof b,
                      "%s z %s  %s z %s | born overlap %.0f mm | asleep %d/%d on stage | "
                      "exit %s (%d it, %d rows)", c.bodies[1].label, s.z_text(ps, 1).c_str(),
                      c.bodies[0].label, s.z_text(ps, 0).c_str(), s.birth_overlap * 1000.0f,
                      s.asleep_count(ps), s.alive_count(ps), st.exit, st.iterations,
                      st.rows);
    }
    return b;
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
        for (size_t b = 0; b < c.bodies.size(); ++b) {
            if (!s.alive(ps, (int)b)) {
                std::printf("  [measure] %-9s REFUSED at creation (not on stage)\n",
                            c.bodies[b].label);
                continue;
            }
            std::printf("  [measure] %-9s z %.4f (min %.4f)  rot_y %+.3f  spin %.4f  "
                        "peak speed %.3f  first sleep f%d%s\n", c.bodies[b].label,
                        s.z(ps, (int)b), s.min_z[b], s.rot_y(ps, (int)b),
                        s.spin(ps, (int)b), s.peak_speed((int)b), s.first_sleep_frame[b],
                        s.asleep(ps, (int)b) ? "  [asleep]" : "");
        }
        for (const auto& kv : s.first_contact_frame)
            std::printf("  [measure] strike %s<->%s at f%d, %.2f m/s\n",
                        c.bodies[kv.first.first].label, c.bodies[kv.first.second].label,
                        kv.second, s.strike_speed(kv.first.first, kv.first.second));
        std::printf("  [measure] %s\n", readout(ps, physics, s, c).c_str());
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
