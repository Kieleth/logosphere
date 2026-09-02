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
//   E  THE STRATA STACK (G-69). Eden's own floor: bedrock (12 t), a 6 t
//      layer on it, a 2.4 t dirt layer on that, born touching. Each
//      pair must stand within 2 mm of each other (INV-2, the manifold's
//      own reading) and sleep. Measured on the door branch: 6-13 mm of
//      compaction at rest, and the sleep gate rightly refuses to sleep
//      a penetrating stack - the census population behind Eden's frame.
//
//   F  THE TILED FLOOR (G-69). Eden's floor with its SEAMS: 2 x 2 tiles
//      per layer, three layers, born touching edge to edge and face to
//      face. Every contact the engine builds must read under 2 mm - the
//      seams between coplanar neighbours included - and the floor must
//      sleep. Measured in Eden: seam rows reading 4.0 m of "penetration"
//      at frame 0, a dirt tile sinking 46 mm into the layer below within
//      two frames, 85 bonds born strained past the wake strain.
//
//   G  THE GENTLE WAKE (G-69). The tiled floor of F is left to fall
//      asleep, then a 2.5 kg pebble is dropped on one dirt tile. A tile
//      woken on a SLEEPING floor must be held by it (INV-31: a sleeper's
//      interactions are priced with its true mass) - it must not sink,
//      the contact it builds must read under 2 mm, and the floor must
//      fall back asleep. Eden's dirt tiles wake with their supports
//      asleep and fall through them; the census read them 73 mm deep.
//
//   H  THE AWAKE SIBLING (G-69's phantom). G's floor, born asleep at
//      Eden's stored heights, but the CENTRE dirt tile is born AWAKE at
//      its true rest height, 7 mm below its eight sleeping siblings -
//      the state of Eden's dirt tile the moment after it wakes and
//      falls. Every contact it builds must read under 2 mm: the
//      surface-continuity merge must not manufacture a coplanar
//      sibling's overlap (Eden's canary: 4 m along x, 94 mm along z,
//      then a 20 mm shove into the layer below).
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
#include <memory>
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
    bool born_asleep = false;   // G: as a streamed chunk births its tiles - at rest, where stored
};

// A bond between two bodies: attachment offsets, rest length. rest 0 with
// offsets at the shared face is the strata generator's edge-to-edge recipe
// (strata_floor_generator.cpp bond_tiles), born at strain 1.0 (INV-4).
struct Bond { int a, b; float ax, ay, az; float bx, by, bz; float rest; };
inline Bond edge_bond(const std::vector<BodySpec>& bs, int a, int b) {
    const BodySpec& A = bs[a]; const BodySpec& B = bs[b];
    float dx = B.x - A.x, dy = B.y - A.y, dz = B.z - A.z;
    const float d = std::sqrt(dx*dx + dy*dy + dz*dz);
    dx /= d; dy /= d; dz /= d;
    const float ha = std::fabs(dx) * A.sx * 0.5f + std::fabs(dy) * A.sy * 0.5f + std::fabs(dz) * A.sz * 0.5f;
    const float hb = std::fabs(dx) * B.sx * 0.5f + std::fabs(dy) * B.sy * 0.5f + std::fabs(dz) * B.sz * 0.5f;
    return Bond{a, b, dx * ha, dy * ha, dz * ha, -dx * hb, -dy * hb, -dz * hb, 0.0f};
}

struct Strike {             // an instrumented impact: who hits whom, how fast
    int a, b;
    int by_frame;           // must have happened by this frame
    float v_lo, v_hi;       // relative speed at first contact, m/s
};
struct Height { int body; float z; };
struct Still  { int body; };                // Argus peak speed < STILL
struct Fall   { int body; float v_max; };   // Argus peak speed <= the fall it was born to make
struct Keep   { int body; float z_min; };   // never dips below this (a woken tile must not sink)
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
    std::vector<Bond>   bonds;
    std::vector<Height> heights;            // INV-2: rests at
    std::vector<Still>  stills;             // INV-7: not moved by a repair
    std::vector<Fall>   falls;              // INV-3: moved only by its own fall
    std::vector<Keep>   keeps;              // INV-31/INV-2: a supported body never sinks
    int asleep_by_frame = -1;               // hygiene: the stage must be asleep before the event
    bool tight_heights = false;             // heights judged at INV-2's bar, not the 10 mm stand bar
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
    bool rest_overlap_asserted = true;      // INV-2 at the end (axis-aligned measure)
    int manifold_pair_a = -1, manifold_pair_b = -1;   // INV-2 through the engine's own contact (tilted bodies)
    std::vector<std::pair<int,int>> manifold_pairs;   // more of the same (stacks)
    bool all_contacts_asserted = false;               // INV-2 over EVERY contact the engine built
    bool phantom_watch = false;                       // INV-2 over every contact at EVERY frame (the seam phantom)
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
    float birth_bond_gap = 0.0f;         // worst |attachment gap - rest| at birth (INV-4)
    std::map<std::pair<int,int>, int>   first_contact_frame;
    std::map<std::pair<int,int>, float> first_contact_speed;
    std::map<std::pair<int,int>, float> last_penetration;   // the manifold's own, this frame
    std::vector<int>   first_sleep_frame;
    std::vector<float> min_z;
    float peak_overlap = 0.0f;           // the transient the repair must undo
    float peak_contact_pen = 0.0f;       // the deepest reading any contact ever gave (the phantom's trace)
    int   peak_contact_frame = -1, peak_contact_a = -1, peak_contact_b = -1;
    float repair_sep_at_first_sleep = -1.0f;
    int   last_event_frame = 0;
    int   frames_run = 0;
    int   all_asleep_frame = -1;         // first frame every body on stage slept

    static void paint(Particle& p, Materials::Type m) {
        switch (m) {
            case Materials::Type::WOOD_SOFT: p.r = 0.72f; p.g = 0.55f; p.b = 0.35f; break;
            case Materials::Type::LEAVES:    p.r = 0.42f; p.g = 0.62f; p.b = 0.32f; break;
            case Materials::Type::DIRT:      p.r = 0.45f; p.g = 0.33f; p.b = 0.22f; break;
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
        p.is_at_rest = b.born_asleep;
        ids[i] = ps.queue_particle_addition(p);
        return ids[i];
    }

    void bond(PhysicsSystem& physics, const Bond& bd) {
        if (ids[bd.a] < 0 || ids[bd.b] < 0) return;
        auto g = std::make_unique<OrganicGluon>();
        g->offset_a = {bd.ax, bd.ay, bd.az};
        g->offset_b = {bd.bx, bd.by, bd.bz};
        g->target_distance = bd.rest;
        const BodySpec& A = specs[bd.a];
        // the strata recipe: the shared face is the contact area
        g->contact_area = std::fabs(bd.ax) > 0.0f ? A.sy * A.sz
                        : std::fabs(bd.ay) > 0.0f ? A.sx * A.sz : A.sx * A.sy;
        physics.add_gluon_between(ids[bd.a], ids[bd.b], std::move(g));
    }
    void reset_latches() {
        first_contact_frame.clear(); first_contact_speed.clear(); last_penetration.clear();
        first_sleep_frame.assign(specs.size(), -1);
        all_asleep_frame = -1;
        min_z.assign(specs.size(), 1e9f);
        peak_overlap = 0.0f;
        peak_contact_pen = 0.0f; peak_contact_frame = -1; peak_contact_a = peak_contact_b = -1;
        repair_sep_at_first_sleep = -1.0f;
        last_event_frame = 0; frames_run = 0;
    }

    int build(ParticleSystem& ps, PhysicsSystem& physics, const Case& c,
              float x_off = 0.0f) {
        specs = c.bodies;
        ids.assign(specs.size(), -1);
        for (size_t i = 0; i < specs.size(); ++i)
            if (!specs[i].late) create(ps, (int)i, x_off);
        ps.flush_pending_particles();
        for (const Bond& bd : c.bonds) bond(physics, bd);
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] >= 0) argus.watch(ids[i], specs[i].label);
        reset_latches();
        birth_overlap = worst_overlap(ps).pen;
        birth_bond_gap = 0.0f;
        {
            auto v = ps.lock_particles_for_read();
            for (const Bond& bd : c.bonds) {
                if (ids[bd.a] < 0 || ids[bd.b] < 0) continue;
                const Particle& A = v[ids[bd.a]]; const Particle& B = v[ids[bd.b]];
                const float gx = (A.x + bd.ax) - (B.x + bd.bx);
                const float gy = (A.y + bd.ay) - (B.y + bd.by);
                const float gz = (A.z + bd.az) - (B.z + bd.bz);
                birth_bond_gap = std::fmax(birth_bond_gap,
                                           std::fabs(std::sqrt(gx*gx + gy*gy + gz*gz) - bd.rest));
            }
        }
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
            // The window replays a case the way headless builds it: a body
            // born asleep (G) is re-armed asleep, or the two modes stop
            // being reflections (measured: the window woke G's tiles and
            // read 1 red where headless read 19).
            p.is_at_rest = b.born_asleep;
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
            if (e.penetration > peak_contact_pen) {
                peak_contact_pen = e.penetration; peak_contact_frame = frame;
                peak_contact_a = k.first; peak_contact_b = k.second;
            }
            if (!first_contact_frame.count(k)) {
                first_contact_frame[k] = frame;
                first_contact_speed[k] = std::fabs(e.relative_velocity);
                last_event_frame = std::max(last_event_frame, frame);
            }
        }
        peak_overlap = std::fmax(peak_overlap, worst_overlap(ps).pen);
        if (all_asleep_frame < 0 && all_asleep(ps)) all_asleep_frame = frame;
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
    // The deepest contact the engine currently reports among the cast.
    float worst_contact_penetration(int* pa = nullptr, int* pb = nullptr) const {
        float w = 0.0f;
        for (const auto& kv : last_penetration)
            if (kv.second > w) { w = kv.second; if (pa) *pa = kv.first.first; if (pb) *pb = kv.first.second; }
        return w;
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

// F's layers are born a few mm above rest (Eden's births): the rest z is
// the born z minus that gap, by layer index (4-7 layer 2, 8-11 dirt).
inline float born_gap_of(float, int k) { return k >= 8 ? 0.007f : (k >= 4 ? 0.002f : 0.0f); }

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
        c.admitted = {4};
        c.demo1 = "DEMONSTRATING: a layer arriving THROUGH sleeping stones is REFUSED, "
                  "a tile arriving BESIDE them is born (INV-37/G-68); nothing moves";
        c.demo2 = "WATCH: frame 60 - one tile appears at x 6, none through the stones; "
                  "stones stay on the base, asleep 4/4 on stage";
        v.push_back(c);
    }
    // E. Eden's floor, three layers born touching on the turtle. The
    //    stack must stand within INV-2's bar at every interface and sleep.
    {
        Case c;
        c.name = "E THE STRATA STACK";
        c.gid = "G-69";
        const float l2_t = 0.15f, l3_t = 0.10f;
        const float l2_z = TILE_TOP + l2_t * 0.5f;              // 0.375
        const float l3_z = TILE_TOP + l2_t + l3_t * 0.5f;       // 0.50
        c.bodies = {tile(0.0f, TILE_Z, "bedrock"),
                    BodySpec{TILE_L, TILE_L, l2_t, 0.0f, 0.0f, l2_z, Materials::Type::STONE, "layer2"},
                    BodySpec{TILE_L, TILE_L, l3_t, 0.0f, 0.0f, l3_z, Materials::Type::DIRT, "dirt"}};
        c.rest_overlap_asserted = false;             // the manifold reads each interface
        c.manifold_pairs = {{0, 1}, {1, 2}};
        c.heights = {Height{0, TILE_Z}, Height{1, l2_z}, Height{2, l3_z}};
        c.stills = {Still{0}, Still{1}, Still{2}};
        c.born_overlap_pair_a = 0; c.born_overlap_pair_b = 1;    // born touching: 0 mm
        c.demo1 = "DEMONSTRATING: three floor layers born touching must STAND within 2 mm "
                  "of each other (INV-2) and sleep - Eden's floor sinks 6-13 mm and never sleeps";
        c.demo2 = "WATCH: the two interface penetrations (the manifold's own reading), "
                  "asleep 3/3, the solver's exit";
        v.push_back(c);
    }
    // F. Eden's floor with its seams: 2 x 2 tiles per layer, three layers,
    //    born touching. The grid is where Eden's compaction was measured.
    {
        Case c;
        c.name = "F THE TILED FLOOR (seams, bonds, a staggered layer)";
        c.gid = "G-69";
        const float l2_t = 0.15f, l3_t = 0.10f;
        // Born the way Eden births them (census f0): layer 2 two millimetres
        // above rest, the dirt layer seven, so the layers FALL onto each
        // other on frame 0 and the straddling dirt tiles strike four tiles at once.
        const float born_gap[3] = {0.0f, 0.002f, 0.007f};
        const float lz[3] = {TILE_Z, TILE_TOP + l2_t * 0.5f, TILE_TOP + l2_t + l3_t * 0.5f};
        const float lt[3] = {TILE_T, l2_t, l3_t};
        const Materials::Type lm[3] = {Materials::Type::STONE, Materials::Type::STONE,
                                       Materials::Type::DIRT};
        static const char* names[12] = {"b00","b10","b01","b11","l00","l10","l01","l11",
                                        "d00","d10","d01","d11"};
        int k = 0;
        // Eden's dirt layer straddles the layer below (the census: one dirt
        // tile in contact with four layer-2 tiles): the top grid is offset
        // by half a tile in x and y.
        for (int layer = 0; layer < 3; ++layer)
            for (int iy = 0; iy < 2; ++iy)
                for (int ix = 0; ix < 2; ++ix) {
                    const float stagger = (layer == 2) ? TILE_L * 0.5f : 0.0f;
                    c.bodies.push_back(BodySpec{TILE_L, TILE_L, lt[layer],
                                                (ix - 0.5f) * TILE_L + stagger,
                                                (iy - 0.5f) * TILE_L + stagger,
                                                lz[layer] + born_gap[layer], lm[layer], names[k]});
                    c.heights.push_back(Height{k, lz[layer]});
                    // bedrock is born on the turtle and must not move; the
                    // layers above fall their gap and no more (2x band for
                    // the strike's rebound)
                    if (layer == 0) c.stills.push_back(Still{k});
                    else c.falls.push_back(Fall{k, 2.0f * fall_speed(born_gap[layer]) + STILL});
                    ++k;
                }
        // The strata generator bonds every cardinal neighbour in a layer.
        for (int layer = 0; layer < 3; ++layer) {
            const int o = layer * 4;
            c.bonds.push_back(edge_bond(c.bodies, o + 0, o + 1));
            c.bonds.push_back(edge_bond(c.bodies, o + 2, o + 3));
            c.bonds.push_back(edge_bond(c.bodies, o + 0, o + 2));
            c.bonds.push_back(edge_bond(c.bodies, o + 1, o + 3));
        }
        c.rest_overlap_asserted = false;       // the AABB measure reads seams as 0 anyway
        c.all_contacts_asserted = true;        // the engine's own rows, seams included
        c.born_overlap_pair_a = 0; c.born_overlap_pair_b = 4;   // born clear: 0 mm
        c.demo1 = "DEMONSTRATING: a tiled floor born a few mm apart settles within 2 mm at "
                  "EVERY contact (INV-2), seams and straddles included, and sleeps";
        c.demo2 = "WATCH: the worst contact the engine reports (seams included), every "
                  "tile at its height, asleep 12/12, the exit";
        v.push_back(c);
    }
    // G. A 3 x 3 grid per layer (the phantom needs a neighbour of j that lies
    //    beyond i), asleep; then a pebble wakes the CENTRE dirt tile at frame
    //    120 while its eight neighbours sleep.
    {
        Case c;
        c.name = "G THE GENTLE WAKE (a floor born asleep in the air)";
        c.gid = "G-69";
        c.run_frames = 420;
        c.late_frame = 120;
        c.asleep_by_frame = 100;
        const float l2_t = 0.15f, l3_t = 0.10f;
        const float born_gap[3] = {0.0f, 0.002f, 0.007f};
        const float lz[3] = {TILE_Z, TILE_TOP + l2_t * 0.5f, TILE_TOP + l2_t + l3_t * 0.5f};
        const float lt[3] = {TILE_T, l2_t, l3_t};
        const Materials::Type lm[3] = {Materials::Type::STONE, Materials::Type::STONE,
                                       Materials::Type::DIRT};
        static char gnames[27][8];
        // The heights assert the REST height: a tile born asleep in the air
        // that never wakes stays there (its own INV-2 red); the woken centre
        // and whatever it wakes must settle to rest.
        int k = 0;
        for (int layer = 0; layer < 3; ++layer)
            for (int iy = 0; iy < 3; ++iy)
                for (int ix = 0; ix < 3; ++ix) {
                    std::snprintf(gnames[k], sizeof gnames[k], "%c%d%d", "bld"[layer], ix, iy);
                    // Eden's chunk store births its tiles ASLEEP where it stored
                    // them - a few mm above their supports (census f0: layer 2 at
                    // 0.377, dirt at 0.507) - and they hang there until woken.
                    c.bodies.push_back(BodySpec{TILE_L, TILE_L, lt[layer],
                                                (ix - 1.0f) * TILE_L, (iy - 1.0f) * TILE_L,
                                                lz[layer] + born_gap[layer], lm[layer], gnames[k],
                                                /*late=*/false, /*born_asleep=*/layer > 0});
                    c.heights.push_back(Height{k, lz[layer]});
                    if (layer == 0) c.stills.push_back(Still{k});
                    else {
                        c.falls.push_back(Fall{k, 2.0f * fall_speed(born_gap[layer]) + STILL});
                        c.keeps.push_back(Keep{k, lz[layer] - OVERLAP_TOL});
                    }
                    ++k;
                }
        // Eden bonds only its bedrock (the census: bedrock 2-4 bonds, the
        // layers above 0): a woken dirt tile's neighbours stay ASLEEP,
        // which is the merge's precondition.
        {
            const int o = 0;
            for (int iy = 0; iy < 3; ++iy)
                for (int ix = 0; ix < 3; ++ix) {
                    const int a = o + iy * 3 + ix;
                    if (ix < 2) c.bonds.push_back(edge_bond(c.bodies, a, a + 1));
                    if (iy < 2) c.bonds.push_back(edge_bond(c.bodies, a, a + 3));
                }
        }
        c.rest_overlap_asserted = false;
        c.all_contacts_asserted = true;
        // The waker: a 0.5 m stone (312 kg) dropped 0.5 m - heavy enough that
        // the wake transfer (m/(m+M))*v = 0.33 m/s clears the threshold a
        // 2.5 kg pebble could not (measured: threshold-blocked, the floor
        // never woke). In Eden the wakers are the trees and grass standing
        // on the tiles, awake from birth on strained bonds.
        const float peb = 0.50f;
        const float dirt_top = TILE_TOP + l2_t + l3_t;                 // 0.55
        const int D_CENTRE = 18 + 4;                                   // d11
        c.bodies.push_back(BodySpec{peb, peb, peb, 0.0f, 0.0f, dirt_top + born_gap[2] + peb * 0.5f + 0.5f,
                                    Materials::Type::STONE, "stone", /*late=*/true});
        const int P = (int)c.bodies.size() - 1;
        c.strikes = {Strike{P, D_CENTRE, 120 + fall_frames(0.5f) + 12,
                            fall_speed(0.5f) * 0.7f, fall_speed(0.5f) * 1.2f}};
        c.heights.push_back(Height{P, dirt_top + peb * 0.5f});
        // No tile may SLEEP IN THE AIR: every upper tile's rest height within
        // INV-2's bar (Eden's f0 census: asleep 2-7 mm above their supports).
        c.tight_heights = true;
        c.sleep_by_event = {D_CENTRE, P};
        c.born_overlap_pair_a = P; c.born_overlap_pair_b = D_CENTRE;
        c.demo1 = "DEMONSTRATING: a tile woken on a SLEEPING floor is held by it (INV-31): "
                  "the pebble strikes, the dirt tile must not sink, the floor sleeps again";
        c.demo2 = "WATCH: frame 120 - the pebble; d00's min z, the worst contact reading, "
                  "asleep 13/13 again";
        v.push_back(c);
    }
    // H. G's floor with the centre dirt tile born AWAKE at its rest height
    //    among sleeping siblings 7 mm higher: the merge's precondition.
    {
        Case c = v.back();                      // start from G
        c.name = "H THE AWAKE SIBLING (the seam phantom)";
        c.gid = "G-69";
        c.run_frames = 300;
        c.late_frame = -1;                      // no stone: the tile is the event
        c.bodies.pop_back();                    // drop G's stone
        c.strikes.clear(); c.heights.pop_back();
        c.tight_heights = false;                // G already carries the sleepers' red
        c.born_overlap_pair_a = -1;
        const int D_CENTRE = 18 + 4;
        BodySpec& centre = c.bodies[D_CENTRE];
        centre.born_asleep = false;
        centre.z = TILE_TOP + 0.15f + 0.10f * 0.5f;   // 0.500, its true rest
        c.keeps.clear();
        c.keeps.push_back(Keep{D_CENTRE, centre.z - OVERLAP_TOL});
        c.falls.clear();
        c.stills.clear();
        c.stills.push_back(Still{D_CENTRE});      // nothing pushes it: it must not move at all
        // No phantom watch through the event stream: CollisionEvent carries the
        // manifold's clamped depth, not the row's - it read 0.0 while the tile
        // was written 20.7 mm up with zero velocity. The height line is the flag.
        for (Height& h : c.heights) if (h.body == D_CENTRE) h.z = centre.z;
        c.sleep_by_event = {D_CENTRE};
        c.asleep_by_frame = -1;
        c.demo1 = "DEMONSTRATING: a tile awake among sleeping siblings 7 mm higher builds NO "
                  "phantom seam row (INV-2) and is not shoved into the layer below";
        c.demo2 = "WATCH: the worst contact the engine reports for d11, its min z, the "
                  "solver's exit, asleep count";
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
    {
        std::vector<std::pair<int,int>> mp = c.manifold_pairs;
        if (c.manifold_pair_a >= 0) mp.insert(mp.begin(), {c.manifold_pair_a, c.manifold_pair_b});
        for (const auto& pr : mp) {
            const float pen = s.contact_penetration(pr.first, pr.second);
            std::snprintf(t, sizeof t, "%s/INV-2: %s<->%s contact penetration at rest "
                          "%.1f mm < %.0f mm (the manifold's own reading)", c.gid,
                          L(pr.first), L(pr.second), pen * 1000.0f, OVERLAP_TOL * 1000.0f);
            v.push_back({t, pen < OVERLAP_TOL});
        }
    }
    if (c.all_contacts_asserted) {
        int pa = -1, pb = -1;
        const float w = s.worst_contact_penetration(&pa, &pb);
        std::snprintf(t, sizeof t, "%s/INV-2: every contact the engine builds reads under "
                      "%.0f mm, seams included (worst %.1f mm%s%s%s)", c.gid,
                      OVERLAP_TOL * 1000.0f, w * 1000.0f, pa >= 0 ? ", " : "",
                      pa >= 0 ? L(pa) : "", pa >= 0 ? (std::string("<->") + L(pb)).c_str() : "");
        v.push_back({t, w < OVERLAP_TOL});
    }
    if (c.phantom_watch) {
        std::snprintf(t, sizeof t, "%s/INV-2: no contact EVER reads over %.0f mm (deepest %.1f mm "
                      "at f%d%s%s%s)", c.gid, OVERLAP_TOL * 1000.0f, s.peak_contact_pen * 1000.0f,
                      s.peak_contact_frame, s.peak_contact_a >= 0 ? ", " : "",
                      s.peak_contact_a >= 0 ? L(s.peak_contact_a) : "",
                      s.peak_contact_a >= 0 ? (std::string("<->") + L(s.peak_contact_b)).c_str() : "");
        v.push_back({t, s.peak_contact_pen < OVERLAP_TOL});
    }
    // INV-2: rests at the derived height.
    for (const Height& h : c.heights) {
        const float zz = s.z(ps, h.body);
        const float tol = c.tight_heights ? OVERLAP_TOL : HEIGHT_TOL;
        std::snprintf(t, sizeof t, "%s/INV-2: %s rests at z %.3f (%.3f, tol %.3f)%s",
                      c.gid, L(h.body), h.z, zz, tol,
                      c.tight_heights ? " - a sleeper in the air is not at rest (INV-31)" : "");
        v.push_back({t, s.ids[h.body] >= 0 && std::fabs(zz - h.z) < tol});
    }
    // INV-7: a floor is not moved by a stone's repair (Argus peak speed).
    for (const Still& st_ : c.stills) {
        const float pk = s.peak_speed(st_.body);
        std::snprintf(t, sizeof t, "%s/INV-7: %s is NOT moved by anyone's repair "
                      "(peak speed %.3f < %.2f m/s)", c.gid, L(st_.body), pk, STILL);
        v.push_back({t, pk < STILL});
    }
    // INV-3: a body born a few mm above rest falls that much and no more.
    for (const Fall& fl : c.falls) {
        const float pk = s.peak_speed(fl.body);
        std::snprintf(t, sizeof t, "%s/INV-3: %s moves only by its own fall (peak speed "
                      "%.3f <= %.3f m/s)", c.gid, L(fl.body), pk, fl.v_max);
        v.push_back({t, pk <= fl.v_max});
    }
    for (const Keep& kp : c.keeps) {
        std::snprintf(t, sizeof t, "%s/INV-31: %s never sinks below its rest (min z %.4f >= "
                      "%.4f)", c.gid, L(kp.body), s.min_z[kp.body], kp.z_min);
        v.push_back({t, s.min_z[kp.body] >= kp.z_min});
    }
    if (c.asleep_by_frame >= 0) {
        std::snprintf(t, sizeof t, "hygiene: the floor was ASLEEP before the event (all asleep "
                      "by f%d, needed f%d)", s.all_asleep_frame, c.asleep_by_frame);
        v.push_back({t, s.all_asleep_frame >= 0 && s.all_asleep_frame <= c.asleep_by_frame});
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
    // INV-4's bond clause and INV-14: born at strain 1.0, and every bond survives.
    if (!c.bonds.empty()) {
        std::snprintf(t, sizeof t, "%s/INV-4: every bond is born at rest (worst attachment "
                      "gap %.1f mm < %.0f mm)", c.gid, s.birth_bond_gap * 1000.0f,
                      OVERLAP_TOL * 1000.0f);
        v.push_back({t, s.birth_bond_gap < OVERLAP_TOL});
        // The case's OWN bonds (the window holds every case's floor at once).
        size_t live = 0;
        for (const Bond& bd : c.bonds)
            if (s.ids[bd.a] >= 0 && s.ids[bd.b] >= 0 &&
                physics.get_gluon((size_t)s.ids[bd.a], (size_t)s.ids[bd.b]) != nullptr)
                ++live;
        std::snprintf(t, sizeof t, "%s/INV-14: every bond survives (%zu of %zu)", c.gid,
                      live, c.bonds.size());
        v.push_back({t, live == c.bonds.size()});
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
