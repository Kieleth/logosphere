// =============================================================================
// SCENE: THE CREATION DOOR — nothing is born inside anything (INV-37).
// =============================================================================
// Owner decree 2026-09-01: "under no circumstances, any creation of particles
// should be allowed to overlap in space with another... we should have an
// assert/except for any creation-overlapping moment." Owner ruling 2026-09-02
// on the shape: "fresh at the choke point, with the incremental BVH cost
// measured on the headless bench before it ships, in TDD please."
//
// THIS SCENE IS THE DOOR'S OWN CONTRACT, at the granularity the field
// witnesses (test_jammed_sleep A and D, test_no_overlap_at_creation) cannot
// reach: every entry path, every shape pair, the same-batch case, the
// oriented case, the bond that must not be attached to a body that was never
// born, and the turtle door still standing beside it. Written once, for two
// drivers (test_creation_door, test_creation_door_visual): the births, the
// beats they land on, the stepping and the evaluator live here.
//
// EVERY CASE IS A LECTURE WITH CONTRAST ON STAGE (owner standard 2026-08-20):
// each birth is a BEAT (one every BIRTH_GAP frames) on one real stage, Eden's
// 12-ton strata tile on the turtle, and every refused birth stands beside an
// admitted twin so the eye sees the difference the door makes: the stone ON
// the tile beside the stone IN the tile, the sphere on the box beside the
// sphere in it, the cube beside the rotated bar beside the cube through it,
// the part on its parent beside the part inside its parent.
//
//   A  EDEN'S RECIPE, THE DIRECT PATH. The tile; a stone ON it (born, stands,
//      sleeps); the same stone at the tile's centre height, Eden's census
//      recipe: REFUSED 260 mm deep, the tile named as the blocker, the body
//      absent, the sentence a [PHYSICS REFUSED] add_particle(...) line.
//   B  TOUCHING IS NOT OVERLAPPING. A first body into an empty stage; a cube
//      born exactly touching the tile (gap 0); a cube born clear, 0.5 m up,
//      that falls and lands; a cube born half a SLOP deep — geometric error,
//      not geometry, by the solver's own constant (INV-29). All four born.
//   C  THE QUEUED PATH. The stone in the tile queued: refused, no index
//      handed out, the flush creates nothing; the next queued body is told
//      the live count, not the live count plus a ghost (GEDANKEN-69), and
//      lands there.
//   D  ONE BATCH, TWO NEWBORNS. Two cubes queued before either exists, the
//      second 500 mm into the first: refused against the batch, named by the
//      index the first will take; one body survives the flush.
//   E  SPHERES THROUGH THE SAME DOOR. A box on the tile; a sphere born with
//      0.3 m of itself inside the box: refused; a sphere resting exactly on
//      the box's face: born; a sphere 0.1 m into that one: refused; a sphere
//      exactly touching it: born.
//   F  THE ROTATED BAR IS ITS OBB, NEVER ITS SLAB (INV-12). A 2 m bar turned
//      45 degrees (clockwise viewed from +Z, the engine convention); a cube
//      inside the bar's world-axis slab but clear of the bar: born; a cube
//      through the bar's real waist: refused. The other sign swaps them.
//   G  A STRUCTURE IS NOT AN EXEMPT CLASS. An anchor cube; a nailed part
//      placed INSIDE it by the placement law pb = pa + offset_a - offset_b:
//      refused, 750 mm deep, and NO gluon is created for a body that was
//      never born; the same part nailed ON the anchor: born, bonded, rides.
//
// THE READINGS (instruments, not stage experiments; both drivers take them
// on a private world): the cost on 1,600 abutting tiles, reported with its
// births/candidates/exact tests/microseconds, asserted only for "all
// created"; the turtle door still standing under TURTLE_LENIENT.
//
// THE RED. `CREATION_DOOR=0` is the kill switch (INV-36's pattern: a
// diagnostic for A/B, never a shipping mode). Under it this scene is the
// pre-door world and every refusal line is RED BY DESIGN; under the default
// it is all green.
//
// FULL-STATE NARRATION (assert or waive, per DOF): every admitted body:
// born (its index, its presence), stands at its rest height (z), asleep at
// the end (except the spheres: two touching spheres trade a contact nudge
// and a resting sphere's sleep is the judge's business, waived by name);
// x/y and rotation waived: nothing pushes sideways on a flat tile. Every
// refused body: absent, the blocker named, the depth read in mm where the
// derivation is written beside it. Nothing about what the SOLVER does
// afterwards beyond standing and sleeping: the solver's side of INV-37 (a
// world with no births in overlap converges) is test_jammed_sleep's.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/physics/creation_door.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace scene_door {

constexpr float DT = 1.0f / 60.0f;
constexpr float MU = 0.5f;                       // explicit on every body
constexpr float SLOP = PhysicsV4::SLOP;          // the door's own threshold (INV-29)
constexpr float DEPTH_TOL = 0.002f;              // m: the refusal's depth reading
constexpr float HEIGHT_TOL = 0.01f;              // m: the stands-at bar
constexpr int   BIRTH_GAP = 20;                  // frames between a case's beats
constexpr int   RUN_FRAMES = 360;                // frames per case, headless

// THE STAGE: Eden's strata tile (4 x 4 x 0.3 m, STONE 2500 kg/m^3 =
// 12,000 kg) on the turtle: centre z 0.15, top 0.30. Eden's rubble recipe
// (examples/eden/src/main.cpp): the census' deepest pair was 0.31 x 0.21 x
// 0.26 born at z 0.13, the tile's centre height.
constexpr float TILE_L = 4.0f, TILE_T = 0.3f, TILE_Z = 0.15f;
constexpr float TILE_TOP = TILE_Z + TILE_T * 0.5f;
constexpr float RUB_X = 0.31f, RUB_Y = 0.21f, RUB_Z = 0.26f;
constexpr float RUB_IN_TILE = 0.13f;
constexpr float RUB_ON_TILE = TILE_TOP + RUB_Z * 0.5f;
// The stone spans 0.00..0.26 against a tile spanning 0.00..0.30: the
// shallowest separating axis is Z, (0.30 + 0.26)/2 - |0.13 - 0.15| = 0.26.
constexpr float RUB_IN_TILE_DEPTH = (TILE_T + RUB_Z) * 0.5f - (TILE_Z - RUB_IN_TILE);

enum class Shape  { BOX, SPHERE };
enum class Path   { DIRECT, QUEUED, BONDED };
enum class Expect { ADMIT, REFUSE };

inline const char* door_name(Path p) {
    return p == Path::QUEUED ? "queue_particle_addition" : "add_particle";
}
inline const char* path_name(Path p) {
    return p == Path::DIRECT ? "DIRECT" : p == Path::QUEUED ? "QUEUED" : "BONDED";
}

struct BodySpec {
    Shape shape;
    float sx, sy, sz;          // box dims; a sphere's diameter in all three
    float x, y, z;
    float rot_z;               // radians, clockwise viewed from +Z
    Materials::Type mat;
    const char* label;
};
inline BodySpec box_(float sx, float sy, float sz, float x, float y, float z,
                     const char* label, float rot_z = 0.0f) {
    return BodySpec{Shape::BOX, sx, sy, sz, x, y, z, rot_z, Materials::Type::STONE, label};
}
inline BodySpec sphere_(float d, float x, float y, float z, const char* label) {
    return BodySpec{Shape::SPHERE, d, d, d, x, y, z, 0.0f, Materials::Type::STONE, label};
}
inline BodySpec tile(const char* label = "tile") {
    return box_(TILE_L, TILE_L, TILE_T, 0.0f, 0.0f, TILE_Z, label);
}
inline BodySpec rubble(float x, float z, const char* label) {
    return box_(RUB_X, RUB_Y, RUB_Z, x, 0.0f, z, label);
}

// ONE BIRTH: a body, the door path it takes, the beat it lands on, what the
// law expects of it and why, and the numbers that make the verdict a
// measurement (the blocker, the derived depth, the rest height).
struct Birth {
    BodySpec body;
    Path   path;
    Expect expect;
    int    beat = 0;            // the birth happens at frame beat * BIRTH_GAP
    const char* law = "INV-37";
    const char* why = "";
    bool   flush_after = true;  // QUEUED: flush the batch after this birth
    int    blocker = -1;        // REFUSE: birth index of the body it must name (-1: not asserted)
    float  depth = -1.0f;       // REFUSE: derived depth, m (-1: not asserted)
    int    anchor = -1;         // BONDED: birth index of the parent
    float  ax = 0, ay = 0, az = 0, bx = 0, by = 0, bz = 0;   // BONDED: attachment offsets
    float  breaking_force = 0.0f;                              // BONDED: the nail's declared threshold
    float  rest_z = -1.0f;      // ADMIT: stands at this height at the end (-1: waived)
    bool   sleeps = true;       // ADMIT: asleep at the end (false: waived in `why`)
};

inline Birth admit(BodySpec b, Path path, int beat, const char* why, float rest_z,
                   bool sleeps = true) {
    Birth k; k.body = b; k.path = path; k.expect = Expect::ADMIT; k.beat = beat;
    k.why = why; k.rest_z = rest_z; k.sleeps = sleeps;
    return k;
}
inline Birth refuse(BodySpec b, Path path, int beat, const char* why, int blocker,
                    float depth = -1.0f) {
    Birth k; k.body = b; k.path = path; k.expect = Expect::REFUSE; k.beat = beat;
    k.why = why; k.blocker = blocker; k.depth = depth;
    return k;
}
// A nailed part placed by the placement law pb = pa + offset_a - offset_b
// (the way the generators place parts). The spec's position is that law's
// answer, so the stage and the teleport (replay) agree with the engine.
inline Birth nailed(const BodySpec& anchor, int anchor_birth, BodySpec part,
                    float ax, float ay, float az, float bx, float by, float bz,
                    float breaking_force, Expect expect, int beat, const char* why,
                    float depth = -1.0f) {
    part.x = anchor.x + ax - bx; part.y = anchor.y + ay - by; part.z = anchor.z + az - bz;
    Birth k; k.body = part; k.path = Path::BONDED; k.expect = expect; k.beat = beat;
    k.why = why; k.anchor = anchor_birth;
    k.ax = ax; k.ay = ay; k.az = az; k.bx = bx; k.by = by; k.bz = bz;
    k.breaking_force = breaking_force;
    if (expect == Expect::REFUSE) { k.blocker = anchor_birth; k.depth = depth; }
    else k.rest_z = part.z;
    return k;
}

struct Case {
    const char* name;
    const char* gid;
    const char* demo1;
    const char* demo2;
    std::vector<Birth> births;
    int run_frames = RUN_FRAMES;
    const char* waiver = nullptr;
};

struct Verdict { std::string text; bool ok; bool pending = false; };

// What the door answered for one birth, latched the moment it answered
// (the next birth overwrites last_creation_refusal()).
struct Outcome {
    bool   attempted = false;
    bool   replayed = false;       // this run teleported a standing body (SPACE)
    bool   created_now = false;    // this run put a new body on stage (read after the beat's flush)
    int    id = -1;                // live index, -1 when refused
    int    predicted = -1;         // QUEUED: the index the queue promised
    size_t live_before = 0;
    size_t gluons_before = 0, gluons_after = 0;
    logosphere::CreationRefusal rec;
};

struct Scene {
    logosphere::Argus argus;
    std::vector<Outcome> out;
    std::vector<int> first_sleep_frame;
    size_t live_at_arm = 0, refusals_at_arm = 0;
    int    frames_run = 0;
    float  x_off = 0.0f;

    static void paint(Particle& p, Materials::Type m) {
        switch (m) {
            case Materials::Type::WOOD_SOFT: p.r = 0.72f; p.g = 0.55f; p.b = 0.35f; break;
            case Materials::Type::STONE:
            default:                         p.r = 0.55f; p.g = 0.55f; p.b = 0.58f; break;
        }
        p.a = 1.0f;
    }

    Particle make(const BodySpec& b) const {
        Particle p{};
        p.shape = b.shape == Shape::BOX ? ParticleShape::BOX : ParticleShape::SPHERE;
        p.width = b.sx; p.height = b.sy; p.thickness = b.sz;
        p.size = std::fmax(b.sx, std::fmax(b.sy, b.sz));
        p.x = x_off + b.x; p.y = b.y; p.z = b.z;
        p.rotation_z = b.rot_z;
        paint(p, b.mat);
        p.SetMaterial(b.mat);
        p.friction = MU;
        return p;
    }

    // Arm a case: a fresh run of its beats. Bodies already on stage (a
    // replay) keep their ids and are teleported back at their beat.
    void arm(ParticleSystem& ps, const Case& c, float off) {
        x_off = off;
        if (out.size() != c.births.size()) out.assign(c.births.size(), Outcome{});
        for (Outcome& o : out) { o.attempted = false; o.replayed = false; o.created_now = false; }
        first_sleep_frame.assign(c.births.size(), -1);
        live_at_arm = ps.count();
        refusals_at_arm = ps.creation_door_stats().refusals;
        frames_run = 0;
    }

    bool alive(ParticleSystem& ps, int i) const {
        if (out[i].id < 0) return false;
        auto v = ps.lock_particles_for_read();
        return (size_t)out[i].id < v.size() && v[out[i].id].GetMass() > 0.0f;
    }
    bool asleep(ParticleSystem& ps, int i) const {
        return alive(ps, i) && ps.lock_particles_for_read()[out[i].id].is_at_rest;
    }
    float z(ParticleSystem& ps, int i) const {
        return alive(ps, i) ? ps.lock_particles_for_read()[out[i].id].z : -1.0f;
    }
    float peak_speed(int i) const { return out[i].id < 0 ? 0.0f : argus.peak_speed(out[i].id); }
    std::string z_text(ParticleSystem& ps, int i) const {
        char b[32];
        if (!alive(ps, i)) return "absent";
        std::snprintf(b, sizeof b, "%.3f", z(ps, i));
        return b;
    }

    // TELEPORT LAW (scene_limits::rearm): a replay returns a standing body
    // to its birth pose, voids its history, and the solver forgets it.
    void teleport(ParticleSystem& ps, PhysicsSystem& physics, int i, const BodySpec& b) {
        auto parts = ps.lock_particles_for_write();
        Particle& p = parts[out[i].id];
        p.x = x_off + b.x; p.y = b.y; p.z = b.z;
        p.vx = p.vy = p.vz = 0.0f;
        p.omega_x = p.omega_y = p.omega_z = 0.0f;
        p.rotation_x = p.rotation_y = 0.0f; p.rotation_z = b.rot_z;
        p.rotation_q = logosphere::Quat::from_euler(0.0f, 0.0f, b.rot_z);
        p.is_at_rest = false;
        p.frames_at_rest = 0;
        p.low_velocity_frames = 0;
        p.quiet_growth_run = 0;
        p.rest_quiet_sq = 1e9f;
        p.solver_mode = ParticleSolverMode::DYNAMIC;
        physics.forget_body((size_t)out[i].id);
        argus.reset_milestones(out[i].id);
    }

    // ONE BIRTH through its door. The verdict is latched here, once.
    void attempt(ParticleSystem& ps, PhysicsSystem& physics, const Case& c, int i) {
        const Birth& k = c.births[i];
        Outcome& o = out[i];
        if (o.id >= 0 && alive(ps, i)) {           // replay: the body returns to its pose
            o.attempted = true;
            o.replayed = true;
            teleport(ps, physics, i, k.body);
            return;
        }
        o = Outcome{};
        o.attempted = true;
        o.live_before = ps.count();
        o.gluons_before = physics.get_total_gluon_count();
        const Particle p = make(k.body);
        switch (k.path) {
            case Path::DIRECT:
                o.id = ps.add_particle_to_entity(p, nullptr, kg::INVALID_ENTITY);
                o.rec = ps.last_creation_refusal();
                break;
            case Path::QUEUED:
                o.predicted = ps.queue_particle_addition(p);
                o.rec = ps.last_creation_refusal();
                if (k.flush_after) ps.flush_pending_particles();
                o.id = o.predicted;
                break;
            case Path::BONDED: {
                const int a = out[k.anchor].id;
                if (a < 0) { o.id = -1; break; }
                auto g = std::make_unique<NailGluon>();
                g->offset_a = {k.ax, k.ay, k.az};
                g->offset_b = {k.bx, k.by, k.bz};
                g->stiffness = 1.0e6f;
                g->damping = 1.0e3f;
                g->breaking_force = k.breaking_force;
                const size_t r = physics.add_particle_with_gluon_to(
                    (size_t)a, p, std::move(g), /*use_config_position=*/false);
                o.id = r == static_cast<size_t>(-1) ? -1 : (int)r;
                o.rec = ps.last_creation_refusal();
                break;
            }
        }
        o.gluons_after = physics.get_total_gluon_count();
    }

    // ONE step for both drivers (the timestep trap): the beat's births, the
    // world's step, the witness, the first sleep.
    void step(ParticleSystem& ps, PhysicsSystem& physics, const Case& c, int frame) {
        for (size_t i = 0; i < c.births.size(); ++i)
            if (c.births[i].beat * BIRTH_GAP == frame) attempt(ps, physics, c, (int)i);
        // A queued body stands only after its batch's flush, so the beat's
        // presence is read once the beat is over, never per attempt.
        for (size_t i = 0; i < c.births.size(); ++i) {
            if (c.births[i].beat * BIRTH_GAP != frame || out[i].id < 0 || !alive(ps, (int)i)) continue;
            if (!out[i].replayed) out[i].created_now = true;
            argus.watch(out[i].id, c.births[i].body.label);
        }
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
        frames_run = frame + 1;
        for (size_t i = 0; i < out.size(); ++i)
            if (first_sleep_frame[i] < 0 && asleep(ps, (int)i)) first_sleep_frame[i] = frame;
    }

    // The counts the case-level lines read.
    int created_this_run(const Case& c) const {
        int n = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (c.births[i].expect == Expect::ADMIT && out[i].created_now) ++n;
        return n;
    }
    int refusals_expected_this_run(const Case& c) const {
        int n = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (c.births[i].expect == Expect::REFUSE && out[i].attempted && out[i].id < 0) ++n;
        return n;
    }
    bool all_attempted() const {
        for (const Outcome& o : out) if (!o.attempted) return false;
        return true;
    }
    int pending_count() const {
        int n = 0;
        for (const Outcome& o : out) if (!o.attempted) ++n;
        return n;
    }
};

// ---------------------------------------------------------------------------
// THE CASE TABLE. Geometry and derivations in one place.
// ---------------------------------------------------------------------------
inline std::vector<Case> cases() {
    std::vector<Case> v;
    {
        Case c{"A  EDEN'S RECIPE, THE DIRECT PATH", "INV-37",
               "DEMONSTRATING: Eden's stone born INSIDE its 12-ton tile is REFUSED, 260 mm deep, the tile named;",
               "beside it the same stone born ON the tile is born, stands and sleeps. Direct path, no flush."};
        c.births = {
            admit(tile(), Path::DIRECT, 0, "the first body into an empty stage", TILE_Z),
            admit(rubble(+0.8f, RUB_ON_TILE, "stone-on"), Path::DIRECT, 1,
                  "Eden's stone resting ON the tile", RUB_ON_TILE),
            refuse(rubble(-0.8f, RUB_IN_TILE, "stone-in"), Path::DIRECT, 2,
                   "Eden's stone at the tile's centre height: the census recipe", 0, RUB_IN_TILE_DEPTH),
        };
        v.push_back(c);
    }
    {
        Case c{"B  TOUCHING IS NOT OVERLAPPING", "INV-37",
               "DEMONSTRATING: a cube born exactly touching (gap 0), a cube born clear that falls and lands,",
               "and a cube born half a SLOP deep are all BORN: the threshold is the solver's own SLOP (INV-29)."};
        const float H = 0.5f;
        c.births = {
            admit(tile(), Path::DIRECT, 0, "a first body into an empty stage", TILE_Z),
            admit(box_(H, H, H, -1.2f, 0.0f, TILE_TOP + H * 0.5f, "touching"), Path::DIRECT, 1,
                  "born TOUCHING the tile, gap 0", TILE_TOP + H * 0.5f),
            admit(box_(H, H, H, +1.2f, 0.0f, TILE_TOP + H * 0.5f + 0.5f, "clear"), Path::DIRECT, 2,
                  "born CLEAR of everything, 0.5 m up: falls, lands", TILE_TOP + H * 0.5f),
            admit(box_(H, H, H, 0.0f, 0.0f, TILE_TOP + H * 0.5f - 0.5f * SLOP, "slop/2"), Path::DIRECT, 3,
                  "born SLOP/2 into the tile: geometric error, not geometry", TILE_TOP + H * 0.5f),
        };
        v.push_back(c);
    }
    {
        Case c{"C  THE QUEUED PATH", "INV-37",
               "DEMONSTRATING: the stone in the tile QUEUED is refused with no index handed out, the flush",
               "creates nothing; the next queued body is told the live count, not a ghost's (GEDANKEN-69)."};
        c.births = {
            admit(tile(), Path::DIRECT, 0, "the stage", TILE_Z),
            refuse(rubble(-0.8f, RUB_IN_TILE, "stone-in"), Path::QUEUED, 1,
                   "queued into the tile: refused at the queue, before any flush", 0, RUB_IN_TILE_DEPTH),
            admit(rubble(+0.8f, RUB_ON_TILE, "stone-next"), Path::QUEUED, 2,
                  "queued next: promised the live count, lands there", RUB_ON_TILE),
        };
        v.push_back(c);
    }
    {
        Case c{"D  ONE BATCH, TWO NEWBORNS", "INV-37",
               "DEMONSTRATING: two cubes queued in ONE batch, the second 500 mm into the first, is refused",
               "before either exists, named by the index the first will take; one body survives the flush."};
        const float H = 1.0f;
        Birth first = admit(box_(H, H, H, -0.25f, 0.0f, TILE_TOP + H * 0.5f, "cube-1"), Path::QUEUED, 1,
                            "the first of the batch, queued, not flushed", TILE_TOP + H * 0.5f);
        first.flush_after = false;
        c.births = {
            admit(tile(), Path::DIRECT, 0, "the stage", TILE_Z),
            first,
            refuse(box_(H, H, H, +0.25f, 0.0f, TILE_TOP + H * 0.5f, "cube-2"), Path::QUEUED, 1,
                   "the second of the SAME batch, 500 mm into the first", 1, 0.5f),
        };
        v.push_back(c);
    }
    {
        Case c{"E  SPHERES THROUGH THE SAME DOOR", "INV-37",
               "DEMONSTRATING: a sphere with 0.3 m of itself inside a box is refused, one resting on the",
               "box's face is born; a sphere 0.1 m into that one is refused, one exactly touching it is born.",
        };
        c.waiver = "sphere sleep waived: two touching spheres trade a contact nudge; the door's case is the birth";
        const float D = 0.4f, BOX_H = 1.0f;
        const float box_z = TILE_TOP + BOX_H * 0.5f, box_top = TILE_TOP + BOX_H;
        c.births = {
            admit(tile(), Path::DIRECT, 0, "the stage", TILE_Z),
            admit(box_(2.0f, 2.0f, BOX_H, 0.0f, 0.0f, box_z, "box"), Path::DIRECT, 1,
                  "a box on the tile", box_z),
            refuse(sphere_(D, 0.0f, 0.0f, box_top - 0.1f, "sphere-in"), Path::DIRECT, 2,
                   "a SPHERE born inside a BOX: 0.3 m of it inside", 1),
            admit(sphere_(D, 0.0f, 0.0f, box_top + D * 0.5f, "sphere-on"), Path::DIRECT, 3,
                  "a SPHERE resting exactly on the box's face", box_top + D * 0.5f, false),
            refuse(sphere_(D, 0.3f, 0.0f, box_top + D * 0.5f, "sphere-in-2"), Path::DIRECT, 4,
                   "a SPHERE born 0.1 m into the first sphere", 3, 0.1f),
            admit(sphere_(D, 0.4f, 0.0f, box_top + D * 0.5f, "sphere-touch"), Path::DIRECT, 5,
                  "two spheres exactly touching (centres one diameter apart)", box_top + D * 0.5f, false),
        };
        v.push_back(c);
    }
    {
        Case c{"F  THE ROTATED BAR IS ITS OBB, NEVER ITS SLAB (INV-12)", "INV-12",
               "DEMONSTRATING: a 2 m bar turned 45 deg clockwise; a cube INSIDE its world-axis slab but clear",
               "of the bar is born; a cube through the bar's real waist is refused. The other sign swaps them."};
        // rotation_z is CLOCKWISE viewed from +Z: local +X (the 2 m length)
        // points along u = (+0.707, -0.707); the waist runs along v = (+0.707,
        // +0.707). A 0.3 m cube at (0.6, 0.6) is 0.849 m out along v: inside
        // the 1.56 m slab, clear of the 0.2 m bar. A cube at 0.5*u is
        // straight through the waist.
        const float T = 0.2f, bar_z = TILE_TOP + T * 0.5f;
        c.births = {
            admit(tile(), Path::DIRECT, 0, "the stage", TILE_Z),
            admit(box_(2.0f, T, T, 0.0f, 0.0f, bar_z, "bar", 0.7853981634f), Path::DIRECT, 1,
                  "a bar turned 45 deg, clockwise viewed from +Z", bar_z),
            admit(box_(0.3f, 0.3f, T, 0.6f, 0.6f, bar_z, "beside"), Path::DIRECT, 2,
                  "inside the bar's world-axis SLAB, 0.85 m clear of the bar itself", bar_z),
            refuse(box_(0.3f, 0.3f, T, 0.354f, -0.354f, bar_z, "through"), Path::DIRECT, 3,
                   "straight through the bar's real waist", 1),
        };
        v.push_back(c);
    }
    {
        Case c{"G  A STRUCTURE IS NOT AN EXEMPT CLASS", "INV-37",
               "DEMONSTRATING: a nailed part placed INSIDE its anchor by the placement law is refused and",
               "NO gluon is created for a body never born; the same part nailed ON the anchor is born and rides."};
        const float A = 1.0f, PART = 0.5f;
        const BodySpec anchor = box_(A, A, A, 0.0f, 0.0f, TILE_TOP + A * 0.5f, "anchor");
        // A 0.5 m cube centred in a 1 m cube: (0.5 + 0.25) - 0 = 0.75 on every axis.
        c.births = {
            admit(tile(), Path::DIRECT, 0, "the stage", TILE_Z),
            admit(anchor, Path::DIRECT, 1, "the anchor on the tile", anchor.z),
            nailed(anchor, 1, box_(PART, PART, PART, 0, 0, 0, "part-in"),
                   0, 0, 0, 0, 0, 0, 1.0e4f, Expect::REFUSE, 2,
                   "offsets both at the centres: the part lands INSIDE its anchor", (A + PART) * 0.5f),
            nailed(anchor, 1, box_(PART, PART, PART, 0, 0, 0, "part-on"),
                   0, 0, A * 0.5f, 0, 0, -PART * 0.5f, 1.0e4f, Expect::ADMIT, 3,
                   "anchor top to part bottom: the part lands ON its anchor, bonded"),
        };
        v.push_back(c);
    }
    return v;
}

// ---------------------------------------------------------------------------
// THE EVALUATOR: one source for the headless asserts and the live panel.
// ---------------------------------------------------------------------------
inline std::vector<Verdict> evaluate(ParticleSystem& ps, PhysicsSystem& physics,
                                     const Scene& s, const Case& c) {
    (void)physics;
    std::vector<Verdict> v;
    char t[300];
    for (size_t i = 0; i < c.births.size(); ++i) {
        const Birth& k = c.births[i];
        const Outcome& o = s.out[i];
        const char* L = k.body.label;
        const bool pend = !o.attempted;
        auto push = [&](bool ok) { v.push_back({t, ok && !pend, pend}); };
        if (k.expect == Expect::ADMIT) {
            if (pend)
                std::snprintf(t, sizeof t, "%s: %s is BORN on the %s path (%s) - beat %d awaits",
                              k.law, L, path_name(k.path), k.why, k.beat);
            else
                std::snprintf(t, sizeof t, "%s: %s is BORN on the %s path (%s): P%d, %s",
                              k.law, L, path_name(k.path), k.why, o.id,
                              s.alive(ps, (int)i) ? "on stage" : "ABSENT");
            push(o.id >= 0 && s.alive(ps, (int)i));
            if (k.path == Path::QUEUED) {
                std::snprintf(t, sizeof t, "%s: the queue promised the live count (%zu): told %d, landed at P%d",
                              k.law, o.live_before, o.predicted, o.id);
                push(o.created_now ? (o.predicted == (int)o.live_before && s.alive(ps, (int)i))
                                   : s.alive(ps, (int)i));
            }
            if (k.path == Path::BONDED) {
                std::snprintf(t, sizeof t, "%s: and ONE gluon was created for it (%zu, was %zu)",
                              k.law, o.gluons_after, o.gluons_before);
                push(o.created_now ? o.gluons_after == o.gluons_before + 1 : s.alive(ps, (int)i));
            }
            if (k.rest_z >= 0.0f) {
                std::snprintf(t, sizeof t, "INV-2: %s stands at z %.3f (now %s)", L, k.rest_z,
                              s.z_text(ps, (int)i).c_str());
                push(s.alive(ps, (int)i) && std::fabs(s.z(ps, (int)i) - k.rest_z) < HEIGHT_TOL);
            }
            if (k.sleeps) {
                std::snprintf(t, sizeof t, "INV-18: %s sleeps (first sleep f%d, peak speed %.2f m/s)", L,
                              s.first_sleep_frame[i], s.peak_speed((int)i));
                push(s.asleep(ps, (int)i));
            }
        } else {
            const std::string want = std::string("[PHYSICS REFUSED] ") + door_name(k.path);
            if (pend)
                std::snprintf(t, sizeof t, "%s: %s is REFUSED on the %s path, absent (%s) - beat %d awaits",
                              k.law, L, path_name(k.path), k.why, k.beat);
            else
                std::snprintf(t, sizeof t, "%s: %s is REFUSED on the %s path, absent (%s): returned %d, %s",
                              k.law, L, path_name(k.path), k.why,
                              k.path == Path::QUEUED ? o.predicted : o.id,
                              o.rec.refused ? "the record reads refused" : "NO refusal recorded");
            push(o.id < 0 && !s.alive(ps, (int)i) && o.rec.refused &&
                 o.rec.text.find(want) != std::string::npos);
            if (k.blocker >= 0) {
                const int want_p = s.out[k.blocker].id;
                if (k.depth >= 0.0f)
                    std::snprintf(t, sizeof t, "%s: the refusal NAMES P%d (%s) and reads the depth: "
                                  "%.0f mm (derived %.0f mm)", k.law, want_p, c.births[k.blocker].body.label,
                                  o.rec.depth * 1000.0f, k.depth * 1000.0f);
                else
                    std::snprintf(t, sizeof t, "%s: the refusal NAMES P%d (%s): hit P%d, %.0f mm deep",
                                  k.law, want_p, c.births[k.blocker].body.label,
                                  o.rec.blocker.index, o.rec.depth * 1000.0f);
                push(o.rec.refused && o.rec.blocker.index == want_p &&
                     (k.depth < 0.0f || std::fabs(o.rec.depth - k.depth) < DEPTH_TOL));
            }
            if (k.path == Path::BONDED) {
                std::snprintf(t, sizeof t, "%s: and NO gluon was created for it (%zu, was %zu)",
                              k.law, o.gluons_after, o.gluons_before);
                push(o.gluons_after == o.gluons_before);
            }
        }
    }
    // The case's own bookkeeping: the live count grew by exactly the admitted
    // births, none for the refused; the door counted every refusal.
    const int grew = (int)ps.count() - (int)s.live_at_arm;
    const int want_grow = s.created_this_run(c);
    std::snprintf(t, sizeof t, "INV-37: the live count grew by exactly the admitted births: +%d (expected +%d)",
                  grew, want_grow);
    v.push_back({t, s.all_attempted() && grew == want_grow, !s.all_attempted()});
    const int counted = (int)(ps.creation_door_stats().refusals - s.refusals_at_arm);
    const int want_ref = s.refusals_expected_this_run(c);
    std::snprintf(t, sizeof t, "hygiene: the door counted its refusals: %d (expected %d)", counted, want_ref);
    v.push_back({t, s.all_attempted() && counted == want_ref, !s.all_attempted()});
    return v;
}

// The live readout: the numbers at the moment they matter, from the same
// latches the evaluator reads.
inline std::string readout(ParticleSystem& ps, const Scene& s, const Case& c) {
    char b[300];
    int born = 0, refused = 0, last = -1;
    for (size_t i = 0; i < s.out.size(); ++i) {
        if (!s.out[i].attempted) continue;
        if (s.out[i].id >= 0 && s.alive(ps, (int)i)) ++born;
        else { ++refused; last = (int)i; }
    }
    if (last >= 0 && s.out[last].rec.refused)
        std::snprintf(b, sizeof b, "born %d, refused %d | last refusal: %s %.0f mm into P%d (%s)",
                      born, refused, c.births[last].body.label, s.out[last].rec.depth * 1000.0f,
                      s.out[last].rec.blocker.index, s.out[last].rec.blocker.shape);
    else
        std::snprintf(b, sizeof b, "born %d, refused %d | live bodies %zu", born, refused, ps.count());
    return b;
}

// ---------------------------------------------------------------------------
// THE READINGS: instruments on a private world, taken by both drivers.
// ---------------------------------------------------------------------------
struct Reading { std::string text; bool ok; bool skipped; };

inline int add_direct(ParticleSystem& ps, const Particle& p) {
    return ps.add_particle_to_entity(p, nullptr, kg::INVALID_ENTITY);
}

// THE COST, on the smallest world that can show it: a 40 x 40 grid of
// touching tiles, all legal, all through the door. Reported, asserted only
// for "all created": the bar is the headless Eden bench, and a number in a
// unit test that fails on a busy machine is a flake, not a law.
inline Reading cost_reading() {
    ParticleSystem ps;
    Scene s;
    const int N = 40;
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
            add_direct(ps, s.make(box_(1.0f, 1.0f, 0.3f, i * 1.0f, j * 1.0f, 0.15f, "grid")));
    const logosphere::CreationDoorStats& st = ps.creation_door_stats();
    char b[300];
    std::snprintf(b, sizeof b, "INV-37: %d touching tiles all created (%zu live): %zu births, %zu refused, "
                  "%zu candidates, %zu exact tests, %.3f ms (%.2f us/birth)",
                  N * N, ps.count(), st.births, st.refusals, st.candidates, st.exact_tests,
                  st.micros / 1000.0, st.births ? st.micros / st.births : 0.0);
    return {b, ps.count() == (size_t)(N * N), false};
}

// THE TURTLE DOOR STILL STANDS. The creation door is its twin, not its
// replacement: a body legal in every neighbour's eyes and below the world
// floor must still be caught. Asserted through the lever (TURTLE_LENIENT
// downgrades the abort) so this scene cannot kill itself proving that the
// other door aborts.
inline Reading turtle_reading() {
    if (!std::getenv("TURTLE_LENIENT"))
        return {"INV-1/turtle: the turtle door ABORTS by design; TURTLE_LENIENT=1 counts it here "
                "(scripts/physics_sweep.py runs both)", true, true};
    ParticleSystem ps;
    Scene s;
    const int under = add_direct(ps, s.make(box_(0.5f, 0.5f, 0.5f, 20.0f, 20.0f, -1.0f, "under")));
    char b[200];
    std::snprintf(b, sizeof b, "INV-1: under TURTLE_LENIENT the below-floor body is reported and still "
                  "created (P%d): the turtle door is untouched by INV-37", under);
    return {b, under >= 0, false};
}

inline const char* world_line() {
    return logosphere::creation_door_enabled()
        ? "WORLD: DEFAULT - the door is armed (CREATION_DOOR=0 is the pre-door world, red by design)"
        : "WORLD: CREATION_DOOR=0 - the door is KILLED: the pre-door world, every refusal line RED BY DESIGN";
}

// ---------------------------------------------------------------------------
// THE HEADLESS RUN: every case on a fresh world, then the readings.
// ---------------------------------------------------------------------------
inline int run_all(const char* title) {
    const std::vector<Case> cs = cases();
    const char* only_env = std::getenv("DOOR_CASE");
    const int only = only_env ? std::atoi(only_env) : -1;
    std::printf("\n=== %s ===\n", title);
    std::printf("  SLOP = %.4f m (physics_constants.h). Touching is not overlapping.\n", SLOP);
    std::printf("  %s\n", world_line());
    int failures = 0, checks = 0;
    for (size_t i = 0; i < cs.size(); ++i) {
        if (only >= 0 && (int)i != only) continue;
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        Scene s;
        const Case& c = cs[i];
        s.arm(ps, c, 0.0f);
        for (int f = 0; f < c.run_frames; ++f) s.step(ps, physics, c, f);
        std::printf("\n-- %s --\n", c.name);
        for (size_t b = 0; b < c.births.size(); ++b) {
            const Outcome& o = s.out[b];
            if (o.id < 0 || !s.alive(ps, (int)b))
                std::printf("  [measure] %-12s %-7s %s%s\n", c.births[b].body.label,
                            path_name(c.births[b].path),
                            o.rec.refused ? "REFUSED: " : "absent, no refusal recorded",
                            o.rec.refused ? o.rec.text.c_str() : "");
            else
                std::printf("  [measure] %-12s %-7s P%d z %.4f  peak speed %.3f  first sleep f%d%s\n",
                            c.births[b].body.label, path_name(c.births[b].path), o.id,
                            s.z(ps, (int)b), s.peak_speed((int)b), s.first_sleep_frame[b],
                            s.asleep(ps, (int)b) ? "  [asleep]" : "");
        }
        std::printf("  [measure] %s\n", readout(ps, s, c).c_str());
        if (c.waiver) std::printf("  [WAIVED] %s\n", c.waiver);
        for (const Verdict& vd : evaluate(ps, physics, s, c)) {
            ++checks;
            std::printf("  %s %s\n", vd.ok ? "[PASS]" : "[FAIL]", vd.text.c_str());
            if (!vd.ok) failures++;
        }
        physics.shutdown();
    }
    std::printf("\n-- THE READINGS --\n");
    for (const Reading& r : {cost_reading(), turtle_reading()}) {
        ++checks;
        std::printf("  %s %s\n", r.skipped ? "[SKIP]" : r.ok ? "[PASS]" : "[FAIL]", r.text.c_str());
        if (!r.ok) failures++;
    }
    std::printf("\n  %d of %d checks pass%s\n", checks - failures, checks, failures ? "  <-- RED" : "");
    if (!logosphere::creation_door_enabled() && failures > 0)
        std::printf("  (expected: CREATION_DOOR=0 is the world before the door)\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace scene_door
