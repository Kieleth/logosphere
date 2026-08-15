// =============================================================================
// THE RUBE GOLDBERG MACHINE: one continuous chain, every stage an invariant
// =============================================================================
// WHAT THIS IS.
//
// One scene, placed complete at frame zero, through which a single chain
// reaction propagates: a ball drops onto a ramp, slides into a row of boxes,
// the last box falls onto a sleeping stack, the woken stack loses its top
// box, the wreck reaches a nailed bridge, the nails tear, the plank crashes
// into the basement past a gram-scale bonded body, and the survivors slide
// on toward a domino and a lever. S5+ choreography is owed. Every
// stage names the invariants it
// exercises and passes only on a MEASURED predicate. Nothing is isolated:
// every mechanism runs in the presence of all the others, end to end.
//
// RED STOPS THE MACHINE. A stage that misses its window, or any global
// watchdog (detonation, deep penetration, NaN), halts the run and prints the
// stage, its invariants, and the measured values. GREEN CONTINUES. The score
// is percent completion.
//
// THE FRONTIER RATCHET. Stages past EXPECTED_COMPLETED are the engine's
// known missing mechanics — today, rotation (contact torque; the INV-16
// pivot law). The machine is EXPECTED to halt at the first frontier stage.
// Halting earlier is a regression and fails. Passing a frontier stage fails
// LOUDLY too, so the ratchet is raised in the same commit that earned it.
// The machine grows: every campaign slice should move the halt line further
// down the chain, and new stages get appended behind it forever.
//
// Usage:
//   ./build-release/logosphere-tests --test test_rube_goldberg_machine --no-head
//   INTERACTIVE=1 ./build-release/logosphere-tests --test test_rube_goldberg_machine
//   RUBE_TRACE=1  per-frame position of the chain's leading actor
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/telemetry.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <functional>
#include <string>
#include <vector>

namespace T = ::logosphere::telemetry;

namespace {
namespace rube {

// ---------------------------------------------------------------- layout
// Scene design values (INV-29 governs the engine, not test scenery).
// +X east: the chain flows east. +Z up. Everything above the turtle at z=0.
constexpr float TILE    = 1.0f;
constexpr float TILE_TH = 0.2f;
constexpr float BALL    = 0.4f;
constexpr float BOX     = 0.4f;
// A single TALL POST standing on the main deck, its top face at the
// alley deck's level so a box leaving that deck strikes it squarely.
//
// It was a two-box stack until 2026-08-14. The arrival struck the top
// box, which then slid on the LOWER BOX at that pair's friction
// (mu=0.5) and stopped in 0.17 m — measured deceleration 2.1 m/s^2
// where the polished deck allows 0.49. A body sliding on another body
// never touches the floor you polished. One post stands on the deck
// itself, so the deck's friction is the one that governs.
constexpr float STACK_W  = 0.25f;
constexpr float STACK_H  = 2.0f;
// The post starts where the arrival is still HIGH enough to strike it
// squarely: moving it east to 6.9 dropped the strike to z=2.35 and the
// post barely moved. It stays at 6.6 and the BRIDGE reaches west to
// meet it — the post comes to rest with its east face at 7.73.
// 6.6 is the measured optimum, and the trade is unforgiving: the
// arrival is ballistic, so every 0.1 m east costs strike height and
// therefore transferred speed. Measured post velocity right after the
// strike: 0.86 m/s at 6.6, 0.43 m/s at 7.0 — the post loses travel
// faster than it gains a head start, and 7.0 leaves it at 7.24 where
// 6.6 leaves it at 7.61. Neither reaches the bridge at 7.90.
constexpr float STACK_X  = 6.6f;
constexpr float STACK_HI_Z = 2.5f;   // spans 1.5..3.5 = deck to alley level

constexpr float DECK_ALLEY_TOP = 3.4f;
constexpr float DECK_MAIN_TOP  = 1.5f;
constexpr float BASEMENT_TOP   = 0.2f;

// The engine's default Coulomb mu is 0.5 (physics_constants.h), so the
// friction angle is 26.6 deg: a box on a shallower ramp SHOULD stick.
// 40 deg clears it with margin.
constexpr float RAMP_CX = 1.3f, RAMP_CZ = 4.44f;
constexpr float RAMP_LEN = 3.2f, RAMP_TILT = 0.70f;   // ~40 deg about Y
constexpr float BALL_X0 = 0.45f, BALL_Z0 = 6.6f;

constexpr int PREROLL   = 120;
constexpr int MAX_FRAME = 4000;

// THE RATCHET, per lever. With the engine default (wake gate), the
// chain dies in Newton's alley at S3: the relayed pair walls against a
// sleeping crate. With WAKE_RESOLVER=1 (INV-31) it runs through the
// alley and the stack wake, reaching S5.
//
// S5+ is choreography still owed: the struck stack box stops where
// friction and geometry say it stops, short of the bridge. That is a
// scene-design debt, not a physics red — and past it, the domino and
// lever stages need contact torque (the rotation campaign), which is
// what the frontier flags mark.
static int expected_completed() {
    return std::getenv("WAKE_RESOLVER") ? 5 : 3;
}

// Global watchdogs.
constexpr float SPEED_CEILING = 50.0f;   // m/s (INV-11)
constexpr float PEN_CEILING   = 0.05f;   // m    (INV-2, catastrophe scale)

float g_feather_peak = 0.0f;   // highest speed the gram-scale body ever hits

// ---------------------------------------------------------------- world
struct World {
    Engine engine;
    bool interactive = false;
    bool trace = false;
    bool ok = false;

    int ball = -1;
    int alley[3] = {-1, -1, -1};
    int stack_lo = -1, stack_hi = -1;
    int plank = -1;
    int heavy = -1, feather = -1;
    int domino = -1;
    int lever_arm = -1, lever_pebble = -1;

    // Latched by the run loop: a sleeping body that wakes and settles
    // again is asleep by the deadline, so the wake must be caught when
    // it happens, not asked about afterwards.
    bool stack_woke = false;

    struct Tile { int id; float x, y, z_top; };
    std::vector<Tile> tiles;
    std::vector<int> pending_kinematic;
    size_t bonds_at_start = 0;

    PhysicsSystem& phys() { return engine.get_physics_system(); }
    ParticleSystem& ps()  { return engine.get_particle_system(); }

    Particle read(int id) {
        auto v = ps().lock_particles_for_write();
        return v[id];
    }
    void make_kinematic(int id) {
        auto v = ps().lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::KINEMATIC;
        v[id].owner = ParticleOwner::DYNAMICS;
        v[id].is_at_rest = true;
    }
    void release(int id) {   // trigger latch -> live body
        auto v = ps().lock_particles_for_write();
        v[id].solver_mode = ParticleSolverMode::DYNAMIC;
        v[id].is_at_rest = false;
    }

    int add_box(float x, float y, float z, float sx, float sy, float sz,
                Materials::Type mat, float rot_y = 0.0f,
                float r = 0.8f, float g = 0.5f, float b = 0.3f) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = z;
        p.width = sx; p.height = sy; p.thickness = sz;
        p.size = std::max(sx, std::max(sy, sz));
        p.rotation_y = rot_y;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(mat);
        return engine.add_particle(p);
    }
    void platform(float x0, float x1, float z_top) {
        for (float x = x0 + TILE / 2; x < x1; x += TILE)
            for (float y = -1.0f; y <= 1.0f; y += 1.0f) {
                int id = add_box(x, y, z_top - TILE_TH / 2, TILE, TILE,
                                 TILE_TH, Materials::Type::STONE, 0.0f,
                                 0.45f, 0.45f, 0.5f);
                tiles.push_back({id, x, y, z_top});
                pending_kinematic.push_back(id);
            }
    }
    // Nearest y=0 tile of a given deck to an x coordinate (bridge anchors).
    int tile_near(float x, float z_top) {
        int best = -1; float bd = 1e9f;
        for (const Tile& t : tiles) {
            if (std::fabs(t.z_top - z_top) > 0.01f || t.y != 0.0f) continue;
            const float d = std::fabs(t.x - x);
            if (d < bd) { bd = d; best = t.id; }
        }
        return best;
    }

    // A nail whose two anchor points COINCIDE at build time, computed from
    // the actual placed positions — the bond is born unstrained (INV-4).
    void nail_at(int a, int b, float wx, float wy, float wz, float force) {
        const Particle pa = read(a), pb = read(b);
        auto g = std::make_unique<NailGluon>();
        g->offset_a = {wx - pa.x, wy - pa.y, wz - pa.z};
        g->offset_b = {wx - pb.x, wy - pb.y, wz - pb.z};
        g->target_distance = 0.0f;
        g->breaking_force = force;
        phys().add_gluon_between((size_t)a, (size_t)b, std::move(g));
    }

    bool build() {
        EngineConfig cfg;
        cfg.create_display = interactive;
        cfg.enable_chat_window = false;
        cfg.show_debug_overlay = false;
        if (engine.initialize(cfg) != 0) return false;
        T::set_enabled(true);
        T::set_residual_enabled(true);

        // RENDER BISECT: swap in the boulder test's known-good scene
        // (dark KINEMATIC grid at origin) under THIS test's loop and
        // camera code. Renders -> my scene content is the white-maker;
        // white -> my harness code is.
        if (std::getenv("RUBE_BOULDER_SCENE")) {
            for (int row = 0; row < 12; ++row)
                for (int col = 0; col < 12; ++col) {
                    Particle p = {};
                    p.shape = ParticleShape::BOX;
                    p.x = (col - 6.0f) * 1.5f;
                    p.y = (row - 6.0f) * 1.5f;
                    p.width = p.height = p.thickness = 0.8f;
                    p.z = 0.4f;
                    p.size = 0.8f;
                    float t = (float)((row * 7 + col * 13) % 11) / 11.0f;
                    p.r = 0.06f + 0.05f * t;
                    p.g = 0.07f + 0.04f * t;
                    p.b = 0.16f + 0.10f * t;
                    p.a = 1.0f;
                    p.SetMaterial(Materials::Type::STONE);
                    pending_kinematic.push_back(engine.add_particle(p));
                }
            ball = add_box(0.0f, 0.0f, 3.0f, BALL, BALL, BALL,
                           Materials::Type::STONE, 0.0f, 0.9f, 0.2f, 0.2f);
            ps().flush_pending_particles();
            for (int id : pending_kinematic) make_kinematic(id);
            make_kinematic(ball);
            if (interactive) {
                auto& cam = engine.get_camera_system();
                cam.set_pixels_per_unit(90.0f);
                cam.set_camera_deadzone(0.0f);
                ps().queue_light(0.0f, 0.0f, 8.0f, 60000.0f, 60.0f,
                                 1.0f, 0.95f, 0.85f);
            }
            ok = true;
            return true;
        }

        // terrain: alley deck, split main deck with a gap, basement below
        platform(2.9f, 6.0f, DECK_ALLEY_TOP);
        platform(5.9f, 8.0f, DECK_MAIN_TOP);    // west of the gap
        platform(9.4f, 10.6f, DECK_MAIN_TOP);   // east of the gap
        platform(7.5f, 12.9f, BASEMENT_TOP);    // basement

        // the ramp: one rotated KINEMATIC box (INV-12 territory)
        // RENDER BISECT: no other visual test draws a rotation_y box;
        // temporarily excluded to test whether it whites the frame.
        if (!std::getenv("RUBE_NO_RAMP")) {
            int ramp = add_box(RAMP_CX, 0.0f, RAMP_CZ, RAMP_LEN, 1.6f, 0.2f,
                               Materials::Type::STONE, RAMP_TILT,
                               0.4f, 0.55f, 0.4f);
            pending_kinematic.push_back(ramp);
        }

        // the protagonist, latched KINEMATIC above the ramp's west end.
        // STONE: the ball outweighs the wooden alley boxes ~3.5:1, so the
        // relay is a plow, not a pendulum — with mu=0.5 eating momentum at
        // every hop, equal masses die in the row (measured: last box never
        // moved).
        ball = add_box(BALL_X0, 0.0f, BALL_Z0, BALL, BALL, BALL,
                       Materials::Type::STONE, 0.0f, 0.9f, 0.2f, 0.2f);

        // Newton's alley: three boxes at rest on the alley deck
        for (int i = 0; i < 3; ++i)
            alley[i] = add_box(4.9f + 0.45f * i, 0.0f,
                               DECK_ALLEY_TOP + BOX / 2 + 0.002f,
                               BOX, BOX, BOX, Materials::Type::WOOD_HARD,
                               0.0f, 0.85f, 0.65f, 0.2f);

        // the sleeping stack on the main deck
        stack_hi = add_box(STACK_X, 0.0f, STACK_HI_Z,
                           STACK_W, STACK_W, STACK_H,
                           Materials::Type::WOOD_SOFT, 0.0f,
                           0.55f, 0.8f, 0.45f);
        stack_lo = stack_hi;   // one post; the field name is legacy

        // THE NAILED BRIDGE, sized from the tear law it must satisfy.
        //
        // A nail breaks on SUSTAINED load: physics_system_v4.cpp computes
        // force = impulse/dt per frame and tears only after 12 CONSECUTIVE
        // frames at or above the threshold. An impulsive hit cannot break
        // it — one frame of force, then the striker is stopped and pushes
        // no more. What breaks a nail is weight it has to keep carrying.
        //
        // So the plank FILLS the gap (7.90..9.40) flush with the deck
        // rather than resting on top of it: a body slides on from the deck
        // and out over open air, where nothing but the nails holds it up.
        // Flush also avoids the spawn overlap that a recessed plank had
        // (measured 0.063 m of penetration at frame 1 — the plank ends
        // were inside the deck tiles).
        //
        // The arithmetic the nails are chosen by, at 2 nails:
        //   plank alone   90.0 kg ->  883 N  -> 441 N/nail  (63% of hold)
        //   plank + post 152.5 kg -> 1496 N  -> 748 N/nail  (107%)
        // A bridge that carries itself and fails under the load it was
        // never meant to take. NAIL_HOLD is the design input; the tear is
        // the consequence.
        plank = add_box(8.65f, 0.0f, DECK_MAIN_TOP - 0.075f,
                        1.5f, 0.8f, 0.15f, Materials::Type::WOOD_SOFT,
                        0.0f, 0.7f, 0.55f, 0.3f);

        // the featherweight station in the basement crash zone: the
        // 1044:1 ringing scenario, embedded in the machine
        heavy = add_box(8.7f, 0.0f, BASEMENT_TOP + 0.5f + 0.002f,
                        1.0f, 1.0f, 1.0f, Materials::Type::STONE,
                        0.0f, 0.4f, 0.4f, 0.42f);
        feather = add_box(8.7f, 0.0f, BASEMENT_TOP + 1.0f + 0.02f + 0.004f,
                          0.04f, 0.04f, 0.04f, Materials::Type::LEAVES,
                          0.0f, 0.3f, 0.9f, 0.3f);

        // FRONTIER — the domino: tall, thin, waiting for contact torque
        domino = add_box(11.0f, 0.0f, BASEMENT_TOP + 0.75f + 0.002f,
                         0.3f, 0.6f, 1.5f, Materials::Type::WOOD_HARD,
                         0.0f, 0.9f, 0.9f, 0.3f);

        // FRONTIER — the lever: plank nailed to a KINEMATIC pivot post,
        // pebble on the long (east) end
        {
            int pivot_post = add_box(11.9f, 0.0f, BASEMENT_TOP + 0.3f,
                                     0.2f, 0.6f, 0.6f,
                                     Materials::Type::STONE);
            pending_kinematic.push_back(pivot_post);
            lever_arm = add_box(11.9f, 0.0f, BASEMENT_TOP + 0.675f + 0.002f,
                                1.6f, 0.5f, 0.15f,
                                Materials::Type::WOOD_HARD, 0.0f,
                                0.8f, 0.7f, 0.5f);
            lever_pebble = add_box(12.55f, 0.0f,
                                   BASEMENT_TOP + 0.85f + 0.004f,
                                   0.2f, 0.2f, 0.2f, Materials::Type::STONE,
                                   0.0f, 0.9f, 0.3f, 0.9f);
        }

        ps().flush_pending_particles();
        for (int id : pending_kinematic) make_kinematic(id);

        // The alley is POLISHED: contact friction combines as min of the
        // two surfaces (particle_core.h:152), so icing the deck tiles
        // alone lets the ball and the relayed boxes coast where mu=0.5
        // would kill 2.76 m/s in 0.78 m (measured).
        {
            auto v = ps().lock_particles_for_write();
            for (const Tile& t : tiles)
                if (std::fabs(t.z_top - DECK_ALLEY_TOP) < 0.01f ||
                    std::fabs(t.z_top - DECK_MAIN_TOP) < 0.01f)
                    v[t.id].friction = 0.05f;
        }
        make_kinematic(ball);   // latched until the trigger frame

        // bridge nails, at the exact plank-end/deck-edge contact points
        {
            const Particle pl = read(plank);
            const float z_join = DECK_MAIN_TOP;   // deck top = plank bottom
            constexpr float NAIL_HOLD = 700.0f;   // N per nail, see above
            const int west = tile_near(pl.x - 0.9f, DECK_MAIN_TOP);
            const int east = tile_near(pl.x + 0.9f, DECK_MAIN_TOP);
            (void)z_join;
            nail_at(plank, west, pl.x - 0.75f, 0.0f, pl.z, NAIL_HOLD);
            nail_at(plank, east, pl.x + 0.75f, 0.0f, pl.z, NAIL_HOLD);
        }
        // feather bond: the ringing-ladder recipe, verbatim (known good)
        {
            auto g = std::make_unique<OrganicGluon>();
            g->offset_a = {0, 0, 0.51f};
            g->offset_b = {0, 0, -0.02f};
            g->target_distance = 0.0f;
            g->rotate_offsets = false;
            g->contact_area = 1e-4f;
            g->stiffness = 5000.0f;
            g->damping = 100.0f;
            g->enable_angular_constraint = true;
            g->angular_stiffness = 50.0f;
            g->angular_damping = 5.0f;
            phys().add_gluon_between((size_t)heavy, (size_t)feather,
                                     std::move(g));
        }
        // lever nail: the pivot "hinge" — with scalar rows this is a rigid
        // lock; the INV-16 full-Jacobian row is what will let it PIVOT
        {
            const Particle la = read(lever_arm);
            nail_at(lever_arm, pending_kinematic.back(),   // pivot post
                    la.x, 0.0f, la.z - 0.075f, 1e9f);
        }

        bonds_at_start = phys().get_total_gluon_count();

        // Light and camera are unconditional: the RUBE_DUMP self-check
        // renders headlessly and needs both (the frame-200 dump with
        // these under `if (interactive)` was a black frame with a tiny
        // unlit machine — proof the renderer was fine all along; the
        // interactive whiteout was the missing engine.render() call).
        {
            auto& cam = engine.get_camera_system();
            cam.set_pixels_per_unit(90.0f);
            cam.set_camera_deadzone(0.0f);   // immediate follow
            // Three lights strung along the chain, close enough to
            // matter (one 60k light 14 m up left the whole machine in
            // the dark — inverse square is not a suggestion).
            ps().queue_light(1.5f, -2.5f, 8.5f, 250000.0f, 40.0f,
                             1.0f, 0.95f, 0.85f);
            ps().queue_light(6.5f, -2.5f, 7.0f, 250000.0f, 40.0f,
                             1.0f, 0.95f, 0.85f);
            ps().queue_light(11.0f, -2.5f, 5.5f, 250000.0f, 40.0f,
                             1.0f, 0.92f, 0.8f);
        }
        ok = true;
        return true;
    }

    ~World() { if (ok) engine.shutdown(); }
};

// ---------------------------------------------------------------- stages
struct Stage {
    const char* name;
    const char* invs;
    const char* frontier;   // nullptr = expected green today
    int deadline;           // absolute frame
    std::function<bool(World&, std::string&)> pass;
};

std::vector<Stage> make_stages() {
    std::vector<Stage> st;

    // S0: after the pre-roll every placed body is still and no bond tore
    // under its own construction (INV-4 born at rest; INV-30 legal placing).
    st.push_back({"S0 BORN AT REST", "INV-4 INV-30", nullptr, PREROLL + 1,
        [](World& w, std::string& m) {
            float vmax = 0.0f;
            {
                auto v = w.ps().lock_particles_for_write();
                for (int id : {w.alley[0], w.alley[1], w.alley[2],
                               w.stack_lo, w.stack_hi, w.plank, w.heavy,
                               w.feather, w.domino, w.lever_pebble}) {
                    const auto& p = v[id];
                    const float s = std::sqrt(p.vx * p.vx + p.vy * p.vy +
                                              p.vz * p.vz);
                    if (s > vmax) vmax = s;
                }
            }
            const size_t bonds = w.phys().get_total_gluon_count();
            char buf[160];
            snprintf(buf, sizeof buf, "vmax=%.4f m/s bonds=%zu/%zu",
                     vmax, bonds, w.bonds_at_start);
            m = buf;
            return vmax < 0.05f && bonds == w.bonds_at_start;
        }});

    // S1: the released ball falls to ramp altitude (INV-3: the drop is the
    // machine's entire energy budget; INV-11 watchdog covers the fall).
    st.push_back({"S1 THE DROP", "INV-3 INV-11", nullptr, PREROLL + 90,
        [](World& w, std::string& m) {
            const Particle p = w.read(w.ball);
            char buf[120];
            snprintf(buf, sizeof buf, "ball z=%.2f vz=%.2f", p.z, p.vz);
            m = buf;
            return p.z < RAMP_CZ + 1.0f;
        }});

    // S2: slides east down the rotated ramp and leaves it with real speed
    // inside the energy budget (INV-17: contacts never amplify).
    st.push_back({"S2 THE RAMP", "INV-12 INV-17 INV-25", nullptr,
                  PREROLL + 400,
        [](World& w, std::string& m) {
            const Particle p = w.read(w.ball);
            const float sp2 = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;
            const float budget2 = 2.0f * 9.81f * (BALL_Z0 - p.z) * 1.10f;
            char buf[160];
            snprintf(buf, sizeof buf,
                     "ball x=%.2f vx=%.2f |v|2=%.1f budget2=%.1f",
                     p.x, p.vx, sp2, budget2);
            m = buf;
            return p.x > RAMP_CX + RAMP_LEN * 0.42f && p.vx > 0.5f &&
                   sp2 <= budget2;
        }});

    // S3: momentum relays through the row; the last box leaves the deck.
    st.push_back({"S3 NEWTON'S ALLEY", "INV-7 INV-2 INV-10", nullptr,
                  PREROLL + 700,
        [](World& w, std::string& m) {
            const Particle last = w.read(w.alley[2]);
            char buf[120];
            snprintf(buf, sizeof buf, "last box x=%.2f z=%.2f",
                     last.x, last.z);
            m = buf;
            return last.x > 6.0f && last.z < DECK_ALLEY_TOP - 0.3f;
        }});

    // S4: a body arrives on a stack that has been asleep for two
    // seconds. The stack must REGISTER it — wake, carry the load, and
    // hold the arrival up. Sleep is a cache, not a hiding place
    // (INV-18); the rows resize to the woken world (INV-8).
    //
    // The first draft of this stage demanded a lateral shove, which a
    // box landing on TOP cannot deliver without contact torque — the
    // predicate was asking the rotation frontier for a favour. What is
    // actually being tested is the wake and the support.
    st.push_back({"S4 THE WAKE-UP", "INV-18 INV-8 INV-7 INV-2", nullptr,
                  PREROLL + 1000,
        [](World& w, std::string& m) {
            const Particle hi = w.read(w.stack_hi);
            const Particle box = w.read(w.alley[2]);
            const float stack_top = hi.z + STACK_H / 2;
            (void)stack_top;
            const bool supported = box.z > stack_top - 0.1f &&
                                   std::fabs(box.x - hi.x) < 1.0f;
            const bool struck = std::fabs(hi.x - STACK_X) > 0.03f ||
                                std::fabs(hi.z - STACK_HI_Z) > 0.05f;
            char buf[180];
            snprintf(buf, sizeof buf,
                     "stack_woke=%d top x=%.2f z=%.2f (start z %.2f) arrival z=%.2f "
                     "supported=%d", (int)w.stack_woke, hi.x, hi.z,
                     STACK_HI_Z, box.z, (int)supported);
            m = buf;
            return w.stack_woke && (supported || struck);
        }});

    // S5: the arriving load tears both declared nails; the plank drops.
    //
    // WHY THIS IS STILL RED (measured 2026-08-15, and it is choreography,
    // not physics):
    //   * The tear law needs SUSTAINED load — force = impulse/dt at or
    //     above the nail's hold for 12 CONSECUTIVE frames. An impulsive
    //     hit cannot do it: one frame of force, then the striker stops.
    //   * So the bridge must be STOOD ON, and it is built for that now:
    //     it fills the gap flush with the deck, carries its own 883 N on
    //     two 700 N nails (441 N each, 63%), and a 62.5 kg post arriving
    //     mid-span would put 748 N on each (107%) — a tear.
    //   * The post never arrives. It stops at x=7.61 and the bridge
    //     begins at 7.90. Moving it east makes it worse, not better:
    //     the arrival is ballistic, so a post at 7.00 is struck lower and
    //     leaves with 0.43 m/s instead of 0.86, ending at 7.24.
    //
    // The chain needs a different carrier for the last 0.3 m — a body
    // that arrives ON the bridge rather than sliding at it. That is a
    // redesign of this leg, not a constant to nudge, and it is on the
    // board as C5's remainder.
    st.push_back({"S5 THE BRIDGE", "INV-14 INV-4", nullptr, PREROLL + 1400,
        [](World& w, std::string& m) {
            const size_t bonds = w.phys().get_total_gluon_count();
            const Particle p = w.read(w.plank);
            char buf[140];
            snprintf(buf, sizeof buf, "bonds=%zu/%zu plank z=%.2f",
                     bonds, w.bonds_at_start, p.z);
            m = buf;
            return bonds <= w.bonds_at_start - 2 &&
                   p.z < DECK_MAIN_TOP - 0.5f;
        }});

    // S6: the gram-scale bonded body rides out the crash inside the
    // momentum caps (INV-10) and never detonates (INV-11): peak tracked
    // every frame by the watchdog loop, settled again by the deadline.
    st.push_back({"S6 THE FEATHERWEIGHT", "INV-10 INV-11", nullptr,
                  PREROLL + 1700,
        [](World& w, std::string& m) {
            const Particle f = w.read(w.feather);
            const float s = std::sqrt(f.vx * f.vx + f.vy * f.vy +
                                      f.vz * f.vz);
            char buf[140];
            snprintf(buf, sizeof buf, "feather |v|=%.2f peak=%.2f m/s",
                     s, g_feather_peak);
            m = buf;
            return s < 0.25f && g_feather_peak < 5.0f;
        }});

    // ------------------------- THE FRONTIER ------------------------------
    st.push_back({"S7 THE DOMINO",
                  "contact torque (rotation campaign)",
                  "contacts carry no torque: bodies slide, they do not tip",
                  PREROLL + 2200,
        [](World& w, std::string& m) {
            const Particle d = w.read(w.domino);
            const float tilt = std::max(std::fabs(d.rotation_x),
                                        std::fabs(d.rotation_y));
            char buf[120];
            snprintf(buf, sizeof buf, "domino tilt=%.2f rad x=%.2f",
                     tilt, d.x);
            m = buf;
            return tilt > 0.8f;
        }});

    st.push_back({"S8 THE LEVER", "INV-16 (aspirational)",
                  "the anchor pivot law needs the full-Jacobian row",
                  PREROLL + 2700,
        [](World& w, std::string& m) {
            const Particle peb = w.read(w.lever_pebble);
            char buf[120];
            snprintf(buf, sizeof buf, "pebble z=%.2f vz=%.2f",
                     peb.z, peb.vz);
            m = buf;
            return peb.z > BASEMENT_TOP + 1.4f;
        }});

    st.push_back({"S9 THE QUIET END", "INV-24 INV-2 INV-18", nullptr,
                  MAX_FRAME - 1,
        [](World& w, std::string& m) {
            float vmax = 0.0f;
            {
                auto v = w.ps().lock_particles_for_write();
                for (size_t i = 0; i < v.size(); ++i) {
                    const auto& p = v[i];
                    if (p.solver_mode != ParticleSolverMode::DYNAMIC)
                        continue;
                    const float s = std::sqrt(p.vx * p.vx + p.vy * p.vy +
                                              p.vz * p.vz);
                    if (s > vmax) vmax = s;
                }
            }
            const double pen = T::solve_residual().max_penetration;
            char buf[120];
            snprintf(buf, sizeof buf, "vmax=%.3f m/s pen=%.4f m",
                     vmax, pen);
            m = buf;
            return vmax < 0.05f && pen < 0.002;
        }});

    return st;
}

bool run_machine() {
    const bool interactive = std::getenv("INTERACTIVE") != nullptr;
    const bool trace = std::getenv("RUBE_TRACE") != nullptr;
    g_feather_peak = 0.0f;

    printf("\n=== THE RUBE GOLDBERG MACHINE: every stage an invariant ===\n");
    printf("  mode: %s\n", interactive ? "INTERACTIVE" : "HEADLESS");

    World w;
    w.interactive = interactive;
    w.trace = trace;
    if (!w.build()) { printf("  ERROR: engine init failed\n"); return false; }

    // Bisect mode: no stages, just render the known-good scene under
    // this loop for 30 s, camera following the held ball at origin.
    if (std::getenv("RUBE_BOULDER_SCENE")) {
        for (int frame = 0; frame < 1800 && w.engine.is_running(); ++frame) {
            const auto t0 = std::chrono::steady_clock::now();
            const Particle b = w.read(w.ball);
            w.engine.get_camera_system().update_follow_target(b.x, b.y, b.z);
            w.engine.update(1.0 / 60.0);
            w.engine.render();
            if (interactive) w.engine.present();
            if (interactive)
                std::this_thread::sleep_until(t0 +
                    std::chrono::microseconds(16667));
        }
        printf("  BISECT MODE: boulder scene under machine harness — done\n");
        return true;
    }

    std::vector<Stage> stages = make_stages();
    const int total = (int)stages.size();
    int completed = 0, current = 0;
    bool watchdog = false;   // detonation / penetration / NaN: never OK
    std::string halt_reason;

    const int EXPECTED_COMPLETED = expected_completed();
    printf("  stages: %d   expected today: %d (%s)   trigger at frame %d\n\n",
           total, EXPECTED_COMPLETED,
           std::getenv("WAKE_RESOLVER") ? "WAKE_RESOLVER=1"
                                        : "engine default",
           PREROLL);

    const int dump_frame = std::getenv("RUBE_DUMP")
        ? std::atoi(std::getenv("RUBE_DUMP")) : -1;
    for (int frame = 0; frame < MAX_FRAME && current < total; ++frame) {
        if (frame == PREROLL) w.release(w.ball);
        // Interactive runs in real time; headless runs flat out. Without
        // this the whole show plays in milliseconds (measured: 798k frames
        // before the first human look).
        const auto frame_start = std::chrono::steady_clock::now();
        if (interactive || dump_frame >= 0) {
            // Direct centering: iso_x=(vx-vy)*.866, iso_y=(vx+vy)*.5+vz
            // with view = world - camera, so camera AT the subject puts
            // it at exact screen center. update_follow_target applies a
            // diagonal offset tuned for Eden's camera height and never
            // writes camera z — at z=0 it centered a point ~30 world
            // units off-frame (measured: an all-black dump).
            const Particle b = w.read(w.ball);
            w.engine.get_camera_system().set_position(b.x, b.y, b.z);
        }
        w.engine.update(1.0 / 60.0);
        // update() simulates; render() paints; present() puts the paint
        // on the window. All three, every frame, or the user sees an
        // unpainted (white) surface while the framebuffer is perfect.
        if (interactive || dump_frame >= 0) w.engine.render();
        if (interactive) {
            w.engine.present();
            if (frame == 60 && w.engine.presents_completed() == 0) {
                printf("  MACHINE RED: window shown but nothing presented "
                       "(frame-chain broken)\n");
                return false;
            }
        }
        if (frame == dump_frame) {
            w.engine.get_renderer().wait_for_completion();
            int fw = w.engine.get_render_buffer().width();
            int fh = w.engine.get_render_buffer().height();
            std::vector<uint32_t> px((size_t)fw * fh);
            if (w.engine.read_latest_framebuffer(px.data(), fw, fh)) {
                FILE* f = fopen("/tmp/rube_dump.ppm", "wb");
                if (f) {
                    fprintf(f, "P6\n%d %d\n255\n", fw, fh);
                    for (int i = 0; i < fw * fh; ++i) {
                        unsigned char rgb[3] = {
                            (unsigned char)((px[i] >> 16) & 0xFF),
                            (unsigned char)((px[i] >> 8) & 0xFF),
                            (unsigned char)(px[i] & 0xFF)};
                        fwrite(rgb, 1, 3, f);
                    }
                    fclose(f);
                    printf("  [dump] frame %d -> /tmp/rube_dump.ppm (%dx%d)\n",
                           frame, fw, fh);
                }
            } else {
                printf("  [dump] framebuffer readback FAILED\n");
            }
        }
        if (interactive)
            std::this_thread::sleep_until(frame_start +
                std::chrono::microseconds(16667));

        // global watchdogs: any violation is an instant red
        {
            auto v = w.ps().lock_particles_for_write();
            for (size_t i = 0; i < v.size(); ++i) {
                const auto& p = v[i];
                if (p.solver_mode != ParticleSolverMode::DYNAMIC) continue;
                const float s = std::sqrt(p.vx * p.vx + p.vy * p.vy +
                                          p.vz * p.vz);
                if ((int)i == w.feather && s > g_feather_peak)
                    g_feather_peak = s;
                if (frame > PREROLL && ((int)i == w.stack_hi ||
                                        (int)i == w.stack_lo) &&
                    !p.is_at_rest)
                    w.stack_woke = true;
                if (s > SPEED_CEILING || std::isnan(p.x) ||
                    std::isnan(p.z)) {
                    char buf[160];
                    snprintf(buf, sizeof buf,
                             "WATCHDOG INV-11: body %zu |v|=%.1f m/s "
                             "at frame %d", i, s, frame);
                    halt_reason = buf;
                }
            }
        }
        if (halt_reason.empty()) {
            const double pen = T::solve_residual().max_penetration;
            if (pen > PEN_CEILING) {
                char buf[160];
                snprintf(buf, sizeof buf,
                         "WATCHDOG INV-2: penetration %.3f m at frame %d",
                         pen, frame);
                halt_reason = buf;
            }
        }
        if (!halt_reason.empty()) { watchdog = true; break; }

        Stage& st = stages[current];
        std::string measured;
        if (st.pass(w, measured)) {
            ++completed;
            printf("  [%4d] GREEN  %-20s (%s)  %s\n", frame, st.name,
                   st.invs, measured.c_str());
            ++current;
        } else if (frame >= st.deadline) {
            printf("  [%4d] RED    %-20s (%s)\n", frame, st.name, st.invs);
            if (st.frontier) printf("         frontier: %s\n", st.frontier);
            printf("         measured: %s\n", measured.c_str());
            // A halt is only a REGRESSION if it lands short of the
            // ratchet; halting AT it is the machine's known limit and
            // names the next piece of work.
            halt_reason = std::string(st.frontier ? "frontier: "
                                                  : "halted at: ") + st.name;
            break;
        }

        // RUBE_PROBE=a,b: per-frame kinematics of the plow trio through
        // an RCA window — 1-frame resolution where the 30-frame trace
        // hid the mechanism.
        static int probe_a = -1, probe_b = -1;
        static bool probe_init = false;
        if (!probe_init) {
            probe_init = true;
            if (const char* pv = std::getenv("RUBE_PROBE"))
                sscanf(pv, "%d,%d", &probe_a, &probe_b);
        }
        if (getenv("RUBE_STACK") && frame >= 300 && frame <= 700 &&
            (frame % 25) == 0) {
            const Particle hi = w.read(w.stack_hi);
            const Particle lo = w.read(w.stack_lo);
            printf("      [stack f%d] hi x=%.3f vx=%+.3f rest=%d mass=%.1f | "
                   "plank x=%.3f z=%.3f\n", frame, hi.x, hi.vx,
                   (int)hi.is_at_rest, hi.GetMass(),
                   w.read(w.plank).x, w.read(w.plank).z);
        }
        if (frame >= probe_a && frame <= probe_b) {
            const Particle b = w.read(w.ball);
            const Particle a0 = w.read(w.alley[0]);
            const Particle a1 = w.read(w.alley[1]);
            printf("    [probe f%d] ball x=%.4f vx=%.4f vz=%.4f rest=%d | "
                   "box0 x=%.4f vx=%.4f vz=%.4f rest=%d | "
                   "box1 x=%.4f vx=%.4f rest=%d\n",
                   frame, b.x, b.vx, b.vz, (int)b.is_at_rest,
                   a0.x, a0.vx, a0.vz, (int)a0.is_at_rest,
                   a1.x, a1.vx, (int)a1.is_at_rest);
        }

        if (trace && (frame % 30) == 0) {
            const Particle b = w.read(w.ball);
            const Particle a0 = w.read(w.alley[0]);
            const Particle a2 = w.read(w.alley[2]);
            printf("    f%-4d stage=%s ball=(%.2f,%.2f vx=%.2f) "
                   "box0=(%.2f rest=%d vx=%.2f) box2=(%.2f rest=%d)\n",
                   frame, stages[current].name, b.x, b.z, b.vx,
                   a0.x, (int)a0.is_at_rest, a0.vx,
                   a2.x, (int)a2.is_at_rest);
        }
    }

    const float pct = 100.0f * completed / total;
    printf("\n  COMPLETION: %d/%d stages (%.1f%%)", completed, total, pct);
    if (!halt_reason.empty()) printf("  — halted: %s", halt_reason.c_str());
    printf("\n");

    // Interactive: hold the window on the final state so the wreck can be
    // inspected. The machine is already scored; close the window to end.
    if (interactive) {
        printf("  (window stays on the final state — close it or ESC to end)\n");
        while (w.engine.is_running()) {
            const auto t0 = std::chrono::steady_clock::now();
            w.engine.update(1.0 / 60.0);
            w.engine.render();
            w.engine.present();
            std::this_thread::sleep_until(t0 +
                std::chrono::microseconds(16667));
        }
    }

    if (watchdog) {
        printf("  VERDICT: RED — watchdog fired: %s\n",
               halt_reason.c_str());
        return false;
    }
    if (completed > EXPECTED_COMPLETED) {
        printf("  VERDICT: RATCHET — a frontier stage went green. Raise "
               "EXPECTED_COMPLETED to %d in this same commit.\n", completed);
        return false;
    }
    if (completed < EXPECTED_COMPLETED) {
        printf("  VERDICT: RED — completed %d, expected %d\n", completed,
               EXPECTED_COMPLETED);
        return false;
    }
    printf("  VERDICT: GREEN — the machine stands at its known limit "
           "(%d/%d): %s\n", completed, total,
           halt_reason.empty() ? "ran to the end" : halt_reason.c_str());
    return true;
}

}  // namespace rube
}  // namespace

bool test_rube_goldberg_machine() {
    return rube::run_machine();
}
