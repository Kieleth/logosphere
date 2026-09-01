// =============================================================================
// THE GENERATED ROCK — does a bonded aggregate STAY where it settled?
// =============================================================================
// A ten-box gluon-bonded boulder falls 3 m onto a slab, settles, and is then
// left alone for eight seconds with nothing touching it. Nothing in the scene
// can push it sideways: the floor is flat, every contact normal measured is
// vertical, and no other body exists. So the centre of mass must not move.
//
// This test is a BORN-RED TDD FLAG under the single law (owner ruling
// 2026-09-01: keep it red, do not fix the physics here, do not weaken a
// threshold). Its job is now to say WHERE the drift comes from, not merely
// that it exists. Under the pre-flip default the same scene drifts 0.0468 m
// (6 % under the 0.05 m law); in the single-law world it drifts 0.1652 m.
// The step/creep classifier, the direction, the contact census and the sleep
// census below exist to name the mechanism.
//
//   ./build/logosphere-tests --test test_physics_rock --no-head
//   INTERACTIVE=1 ./build/logosphere-tests --test test_physics_rock
//
// -----------------------------------------------------------------------------
// FULL-STATE NARRATION (assert-or-waive per degree of freedom, physics skill
// 2026-08-19). Cast: the boulder aggregate (10 bonded boxes), the floor slab,
// two wood cubes. Phases: FALL (frame 0 .. settle), REST (the 480 frames
// after, with no external input at all).
//
//   AGGREGATE CENTRE OF MASS
//     x, y   FALL: falls straight, no lateral force exists.  REST: must not
//            move at all. ASSERTED as part of total drift [INV-34]; the
//            horizontal component is measured and printed separately, and
//            asserted on its own [INV-21] — a horizontal displacement with
//            only vertical contact normals is momentum from nowhere.
//     z      FALL: falls to slab top + half height.  REST: constant.
//            ASSERTED as part of total drift [INV-34]; printed separately.
//     ABOVE THE TURTLE at all times. ASSERTED [INV-1].
//
//   AGGREGATE VELOCITY
//     vx,vy,vz  FALL: gravity only.  REST: zero to the settle threshold.
//            ASSERTED via the settle gate (max box speed < 5 cm/s for 30
//            consecutive frames) and via Argus peak speed during REST
//            [INV-34]. The absolute ceiling is the explosion detector's
//            [INV-11], asserted through expdet::stats().
//
//   PER-BOX ORIENTATION
//     rot_x/y/z  FALL: free.  REST: constant — a resting aggregate does not
//            turn. WAIVED from a hard assert: the aggregate settles on
//            whatever face it lands on, so no absolute angle is predictable.
//            The DERIVATIVE is asserted instead (peak |omega| during REST).
//
//   PER-BOX ANGULAR VELOCITY
//     omega  REST: zero. ASSERTED via Argus peak_spin during REST [INV-34].
//
//   q-vs-EULER COHERENCE
//     ASSERTED via Argus divergence, sharp band [INV-33 finiteness of the
//     orientation ledgers is the precondition; the two-band contract is
//     G-21]. Tagged hygiene where no INV names it.
//
//   BONDS (9 OrganicGluons, aggregate cohesion)
//     REST: no bond tears, no box walks away from the core. ASSERTED via the
//     live gluon count and via Argus separation core<->box [INV-14].
//
//   CONTACTS (boulder boxes against the slab)
//     normal   flat ground, flat slab: every normal vertical. ASSERTED
//              [INV-12].
//     penetration  bounded by SLOP in steady state. ASSERTED [INV-2].
//     normal impulse  NOT REACHABLE: CollisionEvent carries penetration,
//              normal, contact point and relative_velocity, but no impulse.
//              WAIVED, and reported as a missing instrument.
//
//   SLEEP
//     REST: the aggregate falls asleep and stays asleep; a body that keeps
//     waking itself with nothing touching it is the drift's engine.
//     MEASURED and printed per frame; asserted as hygiene only — no registry
//     law says "an untouched settled aggregate must be asleep by frame N"
//     (INV-34 says rest is reached, not that sleep is its marker).
//
//   THE TWO CUBES
//     z above the slab and drift < 5 cm each. ASSERTED [INV-2] [INV-34].
//     x, y: WAIVED, they land where they are dropped and nothing pushes
//     them; the drift assert is on z only, as it always was.
// =============================================================================

#include "../src/core/engine.h"
#include "../src/core/entity_telemetry.h"
#include "../src/core/argus.h"
#include "../src/core/explosion_detector.h"
#include "generated/physics_constants.h"
#include "logosphere/worldgen/physics_rock_generator.h"
#include <iostream>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <cstdlib>
#include <algorithm>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The laws this test enforces, in one place, so the verdict lines and the
// audit row read the same words.
// ---------------------------------------------------------------------------
namespace {

int g_failures = 0;

void check(bool ok, const char* law, const std::string& what) {
    std::printf("  %s %-8s %s\n", ok ? "[PASS]" : "[FAIL]", law, what.c_str());
    if (!ok) g_failures++;
}

// The lever world this run is measuring. The test sets NO lever: the ambient
// world is the world under test (owner order 2026-09-01, "the default is the
// world under test"). It only says which world it was.
std::string lever_world() {
    static const char* names[] = {"FRICTION_TWIST", "WARM_LEARN",
                                  "MANIFOLD_SPAN", "SINGLE_LAW",
                                  "CONTACT_TORQUE"};
    std::string s;
    for (const char* n : names) {
        const char* v = std::getenv(n);
        if (v) { s += (s.empty() ? "" : " "); s += n; s += "="; s += v; }
    }
    return s.empty() ? "default (no levers set)" : s;
}

}  // namespace

// Helper to wait for SPACE key press with visual feedback
static void wait_for_space(Engine& engine, const std::string& title, const std::vector<std::string>& messages) {
    auto* ui = engine.get_ui_system();
    auto& input = engine.get_input_system();

    std::cout << "  [INTERACTIVE] " << title << " - waiting for SPACE..." << std::endl;

    const auto& initial_state = input.get_input_state();
    bool was_pressed = initial_state.keys[GLFW_KEY_SPACE];
    bool detected_press = false;

    while (!detected_press) {
        engine.get_platform()->poll_events();
        engine.render();

        int y = 10;
        ui->draw_text(10, y, title, 255, 255, 0);
        y += 40;

        for (const auto& msg : messages) {
            ui->draw_text(10, y, msg, 200, 200, 200);
            y += 25;
        }

        ui->draw_text(10, y + 20, "Press SPACE to continue...", 150, 150, 255);
        engine.present();

        const auto& state = input.get_input_state();
        bool is_pressed = state.keys[GLFW_KEY_SPACE];

        if (!was_pressed && is_pressed) {
            detected_press = true;
        }
        was_pressed = is_pressed;
    }

    std::cout << "  [INTERACTIVE] SPACE pressed!" << std::endl;
}

// Helper: Create floor particle
static int create_floor(Engine& engine, float size = 50.0f) {
    Particle floor = {};
    floor.x = 0.0f;
    floor.y = 0.0f;
    floor.z = 0.025f;  // bottom on the turtle, top unchanged at 0.05
    floor.shape = ParticleShape::BOX;
    floor.width = size;
    floor.height = size;
    floor.thickness = 0.05f;
    floor.SetMaterial(Materials::Type::HEAVY_STATIC);  // Heavy - effectively immovable
    floor.r = 0.3f;
    floor.g = 0.3f;
    floor.b = 0.3f;
    floor.a = 1.0f;
    return engine.add_particle(floor);
}

// Helper: Create light particle
static void create_light(Engine& engine) {
    Particle light = {};
    light.x = 0.0f; light.y = -10.0f; light.z = 10.0f;
    light.size = 2.0f;
    light.SetMaterial(Materials::Type::LIGHT);  // Massive - gravity negligible
    light.is_light_source = true;
    light.emission_strength = 100000.0f;
    light.emission_radius = 500.0f;
    light.r = 1.0f; light.g = 1.0f; light.b = 1.0f;
    engine.add_particle(light);
}

// Helper: Calculate center of mass for a rock
static void get_rock_com(ParticleSystem& ps, const PhysicsRockResult& rock, float& x, float& y, float& z) {
    auto particles = ps.lock_particles_for_read();
    float total_mass = 0.0f;
    x = y = z = 0.0f;

    for (int id : rock.box_ids) {
        const Particle& p = particles[id];
        x += p.x * p.GetMass();
        y += p.y * p.GetMass();
        z += p.z * p.GetMass();
        total_mass += p.GetMass();
    }

    if (total_mass > 0.0f) {
        x /= total_mass;
        y /= total_mass;
        z /= total_mass;
    }
}

// Helper: Check for NaN in rock particles (INV-33: finite state)
static bool check_nan(ParticleSystem& ps, const PhysicsRockResult& rock) {
    auto particles = ps.lock_particles_for_read();
    for (int id : rock.box_ids) {
        const Particle& p = particles[id];
        if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z) ||
            std::isinf(p.x) || std::isinf(p.y) || std::isinf(p.z)) {
            return true;
        }
    }
    return false;
}

// Lowest bottom face among the boulder's boxes (INV-1: the turtle).
static float min_bottom_z(ParticleSystem& ps, const PhysicsRockResult& rock) {
    auto particles = ps.lock_particles_for_read();
    float lo = 1e9f;
    for (int id : rock.box_ids)
        lo = std::min(lo, particles[id].z - particles[id].thickness * 0.5f);
    return lo;
}

// ---------------------------------------------------------------------------
// THE CONTACT CENSUS — the interactions, not just the outcome (physics skill
// directive 2026-08-15). Every frame, every contact event that touches a
// boulder box is classified: who against whom, which way the normal points,
// how deep, how fast the pair is closing.
// ---------------------------------------------------------------------------
struct ContactCensus {
    long long frames_with_contacts = 0;
    long long events = 0;
    long long horizontal_normals = 0;   // |n_x| or |n_y| dominant
    long long vs_floor = 0;             // boulder box against the slab
    long long vs_sibling = 0;           // boulder box against boulder box
    float worst_penetration = 0.0f;
    int   worst_penetration_frame = -1;
    float worst_approach = 0.0f;        // most negative relative_velocity
    int   max_events_in_a_frame = 0;
    int   max_events_frame = -1;

    void observe(const std::vector<CollisionEvent>& evs,
                 const std::vector<int>& box_ids, int floor_id, int frame) {
        int here = 0;
        for (const auto& e : evs) {
            const bool a_box = std::find(box_ids.begin(), box_ids.end(),
                                         (int)e.particle_a) != box_ids.end();
            const bool b_box = std::find(box_ids.begin(), box_ids.end(),
                                         (int)e.particle_b) != box_ids.end();
            if (!a_box && !b_box) continue;
            here++;
            events++;
            if (a_box && b_box) vs_sibling++;
            else if ((int)e.particle_a == floor_id ||
                     (int)e.particle_b == floor_id) vs_floor++;
            const float ax = std::fabs(e.normal_x), ay = std::fabs(e.normal_y);
            const float az = std::fabs(e.normal_z);
            if (!(az > ax && az > ay)) horizontal_normals++;
            if (e.penetration > worst_penetration) {
                worst_penetration = e.penetration;
                worst_penetration_frame = frame;
            }
            if (e.relative_velocity < worst_approach)
                worst_approach = e.relative_velocity;
        }
        if (here > 0) frames_with_contacts++;
        if (here > max_events_in_a_frame) {
            max_events_in_a_frame = here;
            max_events_frame = frame;
        }
    }
};

// ---------------------------------------------------------------------------
// THE DRIFT TRACE — per-frame centre of mass, so a step and a creep cannot
// look the same. A step is one frame carrying most of the total; a creep is
// every frame carrying a slice of it.
// ---------------------------------------------------------------------------
struct DriftSample {
    int frame;
    float cx, cy, cz;
    float step;       // |com(f) - com(f-1)|
    float drift;      // |com(f) - com(settled)|
    float drift_h;    // horizontal component of that
    float drift_z;    // signed vertical component
    int   asleep;     // boxes reporting is_at_rest
};

// ============================================================================
// Test: Large Boulder Stability
// ============================================================================
bool test_physics_rock_boulder(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Physics Rock: Large Boulder Stability";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "  WORLD: " << lever_world() << std::endl;
    std::cout << "  DEMONSTRATING: a bonded aggregate, settled and then left "
                 "alone on flat ground, must not move. [INV-34]" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {"Boulder will fall from z=3m", "Should settle and stay stable"});
    }

    // Clear any previous state
    ps.clear_particles();
    physics.clear_gluons();

    // Setup scene
    create_light(engine);
    int floor_id = create_floor(engine);

    // Create physics rock generator
    PhysicsRockGenerator generator;
    generator.initialize(&engine);

    // Generate large boulder at height (will fall)
    std::cout << "\n[STEP 1] Generating large boulder at z=3m..." << std::endl;
    PhysicsRockResult boulder = generator.generate_rock(0.0f, 0.0f, 3.0f, PhysicsRockSpec::large_boulder());

    std::cout << "  Core ID: " << boulder.core_id << std::endl;
    std::cout << "  Total boxes: " << boulder.total_boxes << std::endl;
    const size_t gluons_at_birth = physics.get_total_gluon_count();
    std::cout << "  Gluons at birth: " << gluons_at_birth << std::endl;

    // ARGUS — the witness. The asserts below read the same queries this
    // narration prints (physics skill: one source, no drift between what is
    // checked and what is seen).
    logosphere::Argus argus;
    argus.set_capacity(1200);
    for (size_t i = 0; i < boulder.box_ids.size(); ++i)
        argus.watch(boulder.box_ids[i],
                    (boulder.box_ids[i] == boulder.core_id)
                        ? "rock/core"
                        : ("rock/box" + std::to_string(i)));

    // Enable telemetry for all boulder boxes
    auto& telemetry = engine.get_physics_system().get_telemetry();
    telemetry.set_enabled(true);
    for (int id : boulder.box_ids) {
        telemetry.track_particle(static_cast<unsigned int>(id));
    }
    std::cout << "  Tracking " << boulder.box_ids.size() << " boulder particles for telemetry" << std::endl;

    logosphere::expdet::reset();   // INV-11: this scene's own ledger

    // Phase 1: Let it settle. Adaptive — wait until max box speed is under
    // threshold for N consecutive frames, or hit a hard time cap. A 10-box
    // gluon-bonded boulder falling from z=3m needs more than the old fixed
    // 2s budget; the fixed budget previously hid real settling as "drift".
    std::cout << "\n[STEP 2] Settling phase (adaptive)..." << std::endl;
    const double dt = 1.0 / 60.0;
    constexpr float SETTLE_SPEED_THRESHOLD = 0.05f;   // 5 cm/s
    constexpr int   SETTLE_STABLE_FRAMES   = 30;      // 0.5 s of quiet
    constexpr int   SETTLE_MAX_FRAMES      = 600;     // 10 s hard cap
    constexpr float DRIFT_LIMIT            = 0.05f;   // the law under test

    ContactCensus fall_census;
    int stable_streak = 0;
    int frame = 0;
    for (; frame < SETTLE_MAX_FRAMES; frame++) {
        engine.update(dt);
        argus.observe(ps, frame);
        fall_census.observe(physics.get_collision_events(), boulder.box_ids,
                            floor_id, frame);

        if (check_nan(ps, boulder)) {
            std::cout << "  ❌ FAIL: NaN detected during settling at frame " << frame << std::endl;
            return false;
        }

        float max_speed = 0.0f;
        {
            const auto& particles = ps.lock_particles_for_read();
            for (int id : boulder.box_ids) {
                if (id < 0 || (size_t)id >= particles.size()) continue;
                const auto& p = particles[id];
                float s = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                if (s > max_speed) max_speed = s;
            }
        }

        if (max_speed < SETTLE_SPEED_THRESHOLD) stable_streak++;
        else                                    stable_streak = 0;

        if (frame % 30 == 0) {
            float x, y, z;
            get_rock_com(ps, boulder, x, y, z);
            std::cout << "  [t=" << std::fixed << std::setprecision(2) << (frame * dt)
                      << "s] COM: (" << std::setprecision(3) << x << ", " << y << ", " << z << ")"
                      << " max_speed=" << std::setprecision(3) << max_speed << " m/s"
                      << " stable=" << stable_streak
                      << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - SETTLING", 255, 255, 0);
            engine.present();
        }

        if (stable_streak >= SETTLE_STABLE_FRAMES) {
            std::cout << "  [settled at frame " << frame << " = " << (frame * dt) << "s]\n";
            break;
        }
    }
    const int settle_frame = frame;
    if (frame >= SETTLE_MAX_FRAMES) {
        std::cout << "  ⚠ settle timeout: hit " << SETTLE_MAX_FRAMES << " frame cap without full quiescence\n";
    }

    // Record settled position
    float settled_x, settled_y, settled_z;
    get_rock_com(ps, boulder, settled_x, settled_y, settled_z);
    std::cout << "\n[STEP 3] Settled position: (" << settled_x << ", " << settled_y << ", " << settled_z << ")" << std::endl;

    // Separation core<->box at settle: the aggregate's shape, so a bond that
    // stretches during REST is visible as geometry, not only as a count.
    std::vector<float> sep_at_settle;
    for (int id : boulder.box_ids)
        sep_at_settle.push_back(argus.separation(boulder.core_id, id));
    const size_t gluons_at_settle = physics.get_total_gluon_count();

    // Phase 2: Check for drift (8 seconds) with NOTHING touching the rock.
    std::cout << "\n[STEP 4] Drift test (8 seconds, no external input)..." << std::endl;

    ContactCensus rest_census;
    std::vector<DriftSample> trace;
    trace.reserve(480);
    float max_drift = 0.0f;
    int drift_frame = -1;
    float prev_x = settled_x, prev_y = settled_y, prev_z = settled_z;
    float peak_rest_speed = 0.0f, peak_rest_spin = 0.0f;
    int first_over_limit = -1;
    int all_asleep_frames = 0, any_asleep_frames = 0;

    for (int f = 0; f < 480; f++) {
        engine.update(dt);
        argus.observe(ps, settle_frame + 1 + f);
        rest_census.observe(physics.get_collision_events(), boulder.box_ids,
                            floor_id, f);

        if (check_nan(ps, boulder)) {
            std::cout << "  ❌ FAIL: NaN detected during drift test at frame " << f << std::endl;
            return false;
        }

        float x, y, z;
        get_rock_com(ps, boulder, x, y, z);

        DriftSample s;
        s.frame = f;
        s.cx = x; s.cy = y; s.cz = z;
        s.step = std::sqrt((x - prev_x) * (x - prev_x) +
                           (y - prev_y) * (y - prev_y) +
                           (z - prev_z) * (z - prev_z));
        s.drift_h = std::sqrt((x - settled_x) * (x - settled_x) +
                              (y - settled_y) * (y - settled_y));
        s.drift_z = z - settled_z;
        s.drift = std::sqrt(s.drift_h * s.drift_h + s.drift_z * s.drift_z);
        s.asleep = 0;
        {
            const auto& particles = ps.lock_particles_for_read();
            for (int id : boulder.box_ids) {
                const auto& p = particles[id];
                if (p.is_at_rest) s.asleep++;
                const float sp = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                if (sp > peak_rest_speed) peak_rest_speed = sp;
                const float w = std::sqrt(p.omega_x*p.omega_x +
                                          p.omega_y*p.omega_y +
                                          p.omega_z*p.omega_z);
                if (w > peak_rest_spin) peak_rest_spin = w;
            }
        }
        if (s.asleep == (int)boulder.box_ids.size()) all_asleep_frames++;
        if (s.asleep > 0) any_asleep_frames++;
        trace.push_back(s);
        prev_x = x; prev_y = y; prev_z = z;

        if (s.drift > max_drift) { max_drift = s.drift; drift_frame = f; }
        if (first_over_limit < 0 && s.drift >= DRIFT_LIMIT) first_over_limit = f;

        if (f % 60 == 0) {
            std::cout << "  [t=" << std::fixed << std::setprecision(1) << (2.0 + f * dt)
                      << "s] drift=" << std::setprecision(4) << s.drift << "m"
                      << " (h=" << s.drift_h << " z=" << s.drift_z << ")"
                      << " step=" << s.step
                      << " asleep=" << s.asleep << "/" << boulder.box_ids.size()
                      << " max=" << max_drift << "m" << std::endl;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - DRIFT TEST", 255, 255, 0);
            engine.present();
        }
    }

    // ------------------------------------------------------------------
    // [STEP 5] WHERE DOES THE DRIFT COME FROM? Step or creep, and which way.
    // ------------------------------------------------------------------
    std::cout << "\n[STEP 5] Drift shape:" << std::endl;
    std::vector<DriftSample> by_step = trace;
    std::sort(by_step.begin(), by_step.end(),
              [](const DriftSample& a, const DriftSample& b) {
                  return a.step > b.step; });
    const float biggest_step = by_step.empty() ? 0.0f : by_step[0].step;
    float sum_steps = 0.0f;
    int rising_frames = 0;
    for (size_t i = 0; i < trace.size(); ++i) {
        sum_steps += trace[i].step;
        if (i > 0 && trace[i].drift > trace[i - 1].drift) rising_frames++;
    }
    std::cout << "  path length (sum of per-frame steps): " << std::setprecision(4)
              << sum_steps << " m over " << trace.size() << " frames" << std::endl;
    std::cout << "  net displacement: " << max_drift << " m; frames where drift grew: "
              << rising_frames << "/" << (int)trace.size() << std::endl;
    std::cout << "  five biggest single-frame steps:" << std::endl;
    for (int i = 0; i < 5 && i < (int)by_step.size(); ++i)
        std::cout << "    f" << by_step[i].frame << "  step=" << std::setprecision(5)
                  << by_step[i].step << " m  drift-then=" << by_step[i].drift
                  << " m" << std::endl;
    // Step, creep or oscillation: three different diseases, and a single
    // "max drift" number cannot tell them apart. A STEP puts most of the
    // displacement in one frame. An OSCILLATION swings the aggregate back
    // and forth (INV-34's "sustained oscillation of an untouched body is an
    // energy source wearing a steady state's clothes"). A CREEP is neither.
    float dz_lo = 1e9f, dz_hi = -1e9f;
    int   dz_sign_flips = 0;
    float prev_dz_delta = 0.0f;
    for (size_t i = 1; i < trace.size(); ++i) {
        if (trace[i].frame >= 120) {
            dz_lo = std::min(dz_lo, trace[i].drift_z);
            dz_hi = std::max(dz_hi, trace[i].drift_z);
        }
        const float d = trace[i].drift_z - trace[i - 1].drift_z;
        if (d * prev_dz_delta < 0.0f) dz_sign_flips++;
        if (d != 0.0f) prev_dz_delta = d;
    }
    const float dz_span = (dz_hi > dz_lo) ? (dz_hi - dz_lo) : 0.0f;
    const bool is_step = (max_drift > 1e-6f) && (biggest_step > 0.25f * max_drift);
    const bool is_osc  = !is_step && (max_drift > 1e-6f) &&
                         (dz_span > 0.25f * max_drift) && dz_sign_flips > 20;
    std::cout << "  vertical swing after f120: [" << std::setprecision(5)
              << dz_lo << ", " << dz_hi << "] m, span " << dz_span
              << " m, " << dz_sign_flips << " direction changes" << std::endl;
    std::cout << "  VERDICT (hygiene classifier): the drift is "
              << (is_step ? "a STEP (one frame carries >25% of it)"
                          : is_osc ? "an OSCILLATION (a vertical swing wider "
                                     "than a quarter of the drift, with many "
                                     "direction changes)"
                                   : "a CREEP (no single frame carries >25%, "
                                     "no sustained swing)")
              << std::endl;
    const DriftSample& last = trace.back();
    const float az = std::atan2(last.cy - settled_y, last.cx - settled_x);
    std::cout << "  direction at the end: dx=" << std::setprecision(5)
              << (last.cx - settled_x) << " dy=" << (last.cy - settled_y)
              << " dz=" << (last.cz - settled_z)
              << "  azimuth=" << std::setprecision(2) << (az * 57.2957795f)
              << " deg   horizontal share="
              << (last.drift > 1e-9f ? 100.0f * last.drift_h / last.drift : 0.0f)
              << "%" << std::endl;
    if (first_over_limit >= 0)
        std::cout << "  crossed the 0.05 m law at frame " << first_over_limit
                  << " (t=" << std::setprecision(2)
                  << (2.0 + first_over_limit * dt) << "s)" << std::endl;

    // ------------------------------------------------------------------
    // [STEP 6] Contact census — the interactions the outcome came from.
    // ------------------------------------------------------------------
    auto print_census = [](const char* phase, const ContactCensus& c) {
        std::cout << "  " << phase << ": events=" << c.events
                  << " frames_with_contacts=" << c.frames_with_contacts
                  << " vs_floor=" << c.vs_floor
                  << " vs_sibling=" << c.vs_sibling
                  << " horizontal_normals=" << c.horizontal_normals
                  << " worst_pen=" << std::setprecision(5) << c.worst_penetration
                  << " m (f" << c.worst_penetration_frame << ")"
                  << " worst_approach=" << c.worst_approach << " m/s"
                  << " busiest frame f" << c.max_events_frame
                  << " (" << c.max_events_in_a_frame << " events)" << std::endl;
    };
    std::cout << "\n[STEP 6] Contact census (PhysicsSystem::get_collision_events):" << std::endl;
    print_census("FALL", fall_census);
    print_census("REST", rest_census);
    std::cout << "  NOTE: normal IMPULSE is not on CollisionEvent (penetration,"
                 " normal, point, relative_velocity only) — the load each"
                 " contact carries is not observable from a test today."
              << std::endl;
    std::cout << "  NOTE: vs_sibling is 0 BY DESIGN — the solver skips "
                 "contacts inside one bonded structure (physics_system_v4.cpp,"
                 " the union-find over the gluon graph), so boxes of this "
                 "boulder may interpenetrate freely and INV-2 cannot see it. "
                 "Only the boulder-against-slab overlap is measurable here."
              << std::endl;

    // Legacy telemetry summary — kept because it counts the same contacts a
    // second way (a cross-check on the census above).
    int total_contacts = 0;
    int vertical_contacts = 0;
    int horizontal_contacts = 0;
    int corner_contacts = 0;
    float sum_nx = 0, sum_ny = 0;

    for (int id : boulder.box_ids) {
        const auto* buf = telemetry.query(static_cast<unsigned int>(id));
        if (!buf || buf->empty()) continue;

        for (size_t i = 0; i < buf->size(); i++) {
            const auto& tframe = buf->at(i);
            for (const auto& c : tframe.contacts) {
                total_contacts++;
                if (c.is_corner_contact) corner_contacts++;

                float abs_nz = std::abs(c.normal_z);
                float abs_nx = std::abs(c.normal_x);
                float abs_ny = std::abs(c.normal_y);

                if (abs_nz > abs_nx && abs_nz > abs_ny) {
                    vertical_contacts++;
                } else {
                    horizontal_contacts++;
                    sum_nx += c.normal_x;
                    sum_ny += c.normal_y;
                }

                if (horizontal_contacts <= 20 && !(abs_nz > abs_nx && abs_nz > abs_ny)) {
                    std::cout << "    f" << tframe.frame_number
                              << " box" << id << " P" << c.particle_a << "<>P" << c.particle_b
                              << " n=(" << std::fixed << std::setprecision(3)
                              << c.normal_x << "," << c.normal_y << "," << c.normal_z << ")"
                              << " pen=" << std::setprecision(4) << c.penetration
                              << (c.is_corner_contact ? " CORNER" : "")
                              << std::endl;
                }
            }
        }
    }

    std::cout << "\n  Telemetry cross-check:" << std::endl;
    std::cout << "    Total contacts: " << total_contacts << std::endl;
    std::cout << "    Vertical (Z dominant): " << vertical_contacts << std::endl;
    std::cout << "    Horizontal (X/Y dominant): " << horizontal_contacts << std::endl;
    std::cout << "    Corner contacts: " << corner_contacts << std::endl;
    if (horizontal_contacts > 0) {
        std::cout << "    Net horizontal normal: (" << sum_nx << ", " << sum_ny << ")" << std::endl;
        std::cout << "    This is the direction the solver pushes the boulder" << std::endl;
    }

    // ------------------------------------------------------------------
    // [STEP 7] The aggregate itself: bonds, spin, coherence, turtle.
    // ------------------------------------------------------------------
    const size_t gluons_at_end = physics.get_total_gluon_count();
    float worst_sep_change = 0.0f;
    int   worst_sep_box = -1;
    for (size_t i = 0; i < boulder.box_ids.size(); ++i) {
        const float now = argus.separation(boulder.core_id, boulder.box_ids[i]);
        const float ch = std::fabs(now - sep_at_settle[i]);
        if (ch > worst_sep_change) { worst_sep_change = ch; worst_sep_box = boulder.box_ids[i]; }
    }
    float worst_div = 0.0f;
    for (int id : boulder.box_ids)
        worst_div = std::max(worst_div, argus.peak_divergence(id, false));
    const float lowest = min_bottom_z(ps, boulder);
    const auto ex = logosphere::expdet::stats();

    std::cout << "\n[STEP 7] Aggregate state:" << std::endl;
    std::cout << "  gluons  birth=" << gluons_at_birth << " settle=" << gluons_at_settle
              << " end=" << gluons_at_end << std::endl;
    std::cout << "  worst core<->box separation change during REST: "
              << std::setprecision(6) << worst_sep_change << " m (box "
              << worst_sep_box << ")" << std::endl;
    for (size_t i = 0; i < boulder.box_ids.size(); ++i) {
        if (boulder.box_ids[i] == boulder.core_id) continue;
        const float now = argus.separation(boulder.core_id, boulder.box_ids[i]);
        std::printf("    core<->P%-3d  settle %.4f m  end %.4f m  delta %+.4f m\n",
                    boulder.box_ids[i], sep_at_settle[i], now,
                    now - sep_at_settle[i]);
    }
    std::cout << "  REST peak speed " << std::setprecision(5) << peak_rest_speed
              << " m/s, peak |omega| " << peak_rest_spin << " rad/s" << std::endl;
    std::cout << "  SLEEP: all ten boxes asleep on " << all_asleep_frames
              << "/480 REST frames; at least one asleep on "
              << any_asleep_frames << "/480" << std::endl;
    std::cout << "  worst q-vs-Euler divergence (sharp band) " << worst_div
              << " rad" << std::endl;
    std::cout << "  lowest bottom face " << lowest << " m (turtle at 0)" << std::endl;
    std::cout << "  explosion detector: speed_warnings=" << ex.speed_warnings
              << " energy_warnings=" << ex.energy_warnings
              << " escape_warnings=" << ex.escape_warnings
              << " worst_speed=" << ex.worst_speed << " m/s (P" << ex.worst_id
              << ")" << std::endl;
    argus.dump(std::cout, 3);

    // ------------------------------------------------------------------
    // [STEP 8] THE ASSERTS. Every line names the law it enforces.
    // ------------------------------------------------------------------
    std::cout << "\n[STEP 8] Asserts:" << std::endl;
    const int before = g_failures;

    check(max_drift < DRIFT_LIMIT, "[INV-34]",
          "settled and STAYS settled: aggregate drift " +
          std::to_string(max_drift) + " m < 0.05 m");
    check(trace.back().drift_h < DRIFT_LIMIT, "[INV-21]",
          "no horizontal walk: with every contact normal vertical, a "
          "sideways displacement is momentum from a position repair "
          "(final |dxy| " + std::to_string(trace.back().drift_h) + " m)");
    check(horizontal_contacts == 0, "[INV-12]",
          "flat ground gives flat normals: horizontal-normal contacts " +
          std::to_string(horizontal_contacts));
    check(rest_census.horizontal_normals == 0, "[INV-12]",
          "same claim through get_collision_events during REST: " +
          std::to_string(rest_census.horizontal_normals));
    check(rest_census.worst_penetration < PhysicsV4::SLOP, "[INV-2]",
          "steady-state overlap under SLOP: worst " +
          std::to_string(rest_census.worst_penetration) + " m");
    check(lowest > -PhysicsV4::SLOP, "[INV-1]",
          "the turtle is absolute: lowest AXIS-ALIGNED bottom face " +
          std::to_string(lowest) + " m (a rotated box reaches lower than "
          "z - thickness/2, so this bound is the weak one: INV-12 forbids "
          "reading a body as its slab and this measure does exactly that)");
    check(rest_census.events > 0, "hygiene",
          "the REST contact census is not vacuous: " +
          std::to_string(rest_census.events) + " events over " +
          std::to_string(rest_census.frames_with_contacts) + " frames");
    check(gluons_at_end == gluons_at_settle, "[INV-14]",
          "no bond tears with nothing pulling: gluons " +
          std::to_string(gluons_at_settle) + " -> " +
          std::to_string(gluons_at_end));
    // CENTRE-to-CENTRE, and that word is the caveat. INV-14 defines strain
    // attachment-point-to-attachment-point, and a box that merely ROTATES
    // about its attachment moves its centre without straining its bond at
    // all. So this is a SHAPE measure, not a strain measure, and it is
    // tagged hygiene rather than claiming a law it does not measure. The
    // real quantity is not reachable: INV-28 records that the
    // attachment-point math exists as four hand-copies with no exported
    // helper, and a test computing it would be the fifth.
    check(worst_sep_change < 0.01f, "hygiene",
          "the aggregate keeps its shape (centre-to-centre): worst "
          "core<->box separation change " +
          std::to_string(worst_sep_change) + " m");
    check(peak_rest_spin < 0.05f, "[INV-34]",
          "a resting aggregate does not turn: REST peak |omega| " +
          std::to_string(peak_rest_spin) + " rad/s");
    check(ex.energy_warnings == 0 && ex.speed_warnings == 0 &&
          ex.escape_warnings == 0, "[INV-11]",
          "the explosion detector stays silent through the whole scene");
    check(!check_nan(ps, boulder), "[INV-33]",
          "every position and velocity is finite at the end");
    check(worst_div < 0.05f, "hygiene",
          "one body, one orientation: worst q-vs-Euler divergence " +
          std::to_string(worst_div) + " rad (G-21 sharp band)");

    const bool pass = (g_failures == before);

    if (is_interactive) {
        wait_for_space(engine, pass ? "PASS" : "FAIL", {"Max drift: " + std::to_string(max_drift) + "m"});
    }

    return pass;
}

// ============================================================================
// Test: Two Cubes Stability (simple collision test)
// ============================================================================
// NARRATION. Two 30 cm wood cubes dropped 1.5 m onto the same slab, 6 m
// apart, never touching each other.
//   z          settles at slab top + half the cube, then constant.
//              ASSERTED (above the floor, and drift < 5 cm) [INV-2][INV-34].
//   x, y       nothing pushes them. WAIVED from assert (the historical
//              contract is z only), MEASURED and printed below so a lateral
//              walk cannot hide.
//   omega      zero at rest. ASSERTED [INV-34].
//   contacts   one flat face on the slab: vertical normals only.
//              ASSERTED [INV-12].
// ============================================================================
bool test_physics_rock_cubes(Engine& engine, bool is_interactive) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto* ui = engine.get_ui_system();

    std::string test_name = "Physics Rock: Cube Collision Test";

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║  " << test_name << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "  WORLD: " << lever_world() << std::endl;
    std::cout << "  DEMONSTRATING: a single unbonded box, settled on flat "
                 "ground, holds its height and does not walk. [INV-34]" << std::endl;

    if (is_interactive) {
        wait_for_space(engine, test_name, {"Two cubes will fall", "Should settle on floor"});
    }

    // Clear any previous state
    ps.clear_particles();
    physics.clear_gluons();

    // Setup scene
    create_light(engine);
    int floor_id = create_floor(engine);

    // Create two simple cubes
    std::cout << "\n[STEP 1] Generating two cubes..." << std::endl;

    Particle cube1 = {};
    cube1.x = -3.0f; cube1.y = 0.0f; cube1.z = 1.5f;
    cube1.shape = ParticleShape::BOX;
    cube1.width = 0.3f; cube1.height = 0.3f; cube1.thickness = 0.3f;
    cube1.SetMaterial(Materials::Type::WOOD_HARD);
    cube1.r = 0.5f; cube1.g = 0.5f; cube1.b = 0.5f; cube1.a = 1.0f;
    int cube1_id = engine.add_particle(cube1);
    std::cout << "  Cube1: id=" << cube1_id << " at z=" << cube1.z << std::endl;

    Particle cube2 = {};
    cube2.x = 3.0f; cube2.y = 0.0f; cube2.z = 1.5f;
    cube2.shape = ParticleShape::BOX;
    cube2.width = 0.3f; cube2.height = 0.3f; cube2.thickness = 0.3f;
    cube2.SetMaterial(Materials::Type::WOOD_HARD);
    cube2.r = 0.5f; cube2.g = 0.5f; cube2.b = 0.5f; cube2.a = 1.0f;
    int cube2_id = engine.add_particle(cube2);
    std::cout << "  Cube2: id=" << cube2_id << " at z=" << cube2.z << std::endl;

    logosphere::Argus argus;
    argus.set_capacity(600);
    argus.watch(cube1_id, "cube1");
    argus.watch(cube2_id, "cube2");

    // Settle phase
    std::cout << "\n[STEP 2] Settling (2 seconds)..." << std::endl;
    const double dt = 1.0 / 60.0;
    ContactCensus settle_census;
    std::vector<int> cube_ids = {cube1_id, cube2_id};

    for (int frame = 0; frame < 120; frame++) {
        engine.update(dt);
        argus.observe(ps, frame);
        settle_census.observe(physics.get_collision_events(), cube_ids,
                              floor_id, frame);

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - SETTLING", 255, 255, 0);
            engine.present();
        }
    }

    // Check settled positions
    float z1, z2, x1_0, y1_0, x2_0, y2_0;
    {
        auto particles = ps.lock_particles_for_read();
        z1 = particles[cube1_id].z;
        z2 = particles[cube2_id].z;
        x1_0 = particles[cube1_id].x; y1_0 = particles[cube1_id].y;
        x2_0 = particles[cube2_id].x; y2_0 = particles[cube2_id].y;
    }
    std::cout << "  Cube1 settled at z=" << z1 << std::endl;
    std::cout << "  Cube2 settled at z=" << z2 << std::endl;

    // Drift test
    std::cout << "\n[STEP 3] Drift test (5 seconds)..." << std::endl;
    float settled_z1 = z1, settled_z2 = z2;
    float max_drift1 = 0.0f, max_drift2 = 0.0f;
    float max_lat1 = 0.0f, max_lat2 = 0.0f;
    int asleep_frames = 0;
    ContactCensus cube_census;

    for (int frame = 0; frame < 300; frame++) {
        engine.update(dt);
        argus.observe(ps, 120 + frame);
        cube_census.observe(physics.get_collision_events(), cube_ids, floor_id,
                            frame);

        {
            auto particles = ps.lock_particles_for_read();
            float drift1 = std::abs(particles[cube1_id].z - settled_z1);
            float drift2 = std::abs(particles[cube2_id].z - settled_z2);
            if (drift1 > max_drift1) max_drift1 = drift1;
            if (drift2 > max_drift2) max_drift2 = drift2;
            const float l1 = std::hypot(particles[cube1_id].x - x1_0,
                                        particles[cube1_id].y - y1_0);
            const float l2 = std::hypot(particles[cube2_id].x - x2_0,
                                        particles[cube2_id].y - y2_0);
            max_lat1 = std::max(max_lat1, l1);
            max_lat2 = std::max(max_lat2, l2);
            if (particles[cube1_id].is_at_rest &&
                particles[cube2_id].is_at_rest) asleep_frames++;
        }

        if (is_interactive) {
            engine.get_platform()->poll_events();
            engine.render();
            ui->draw_text(10, 10, test_name + " - DRIFT TEST", 255, 255, 0);
            engine.present();
        }
    }

    // Results
    std::cout << "\n[STEP 4] Measures:" << std::endl;
    std::cout << "  Cube1: z=" << z1 << " z-drift=" << max_drift1
              << " m lateral=" << max_lat1 << " m spin(peak)="
              << argus.peak_spin(cube1_id) << " rad/s" << std::endl;
    std::cout << "  Cube2: z=" << z2 << " z-drift=" << max_drift2
              << " m lateral=" << max_lat2 << " m spin(peak)="
              << argus.peak_spin(cube2_id) << " rad/s" << std::endl;
    std::cout << "  contacts SETTLE: events=" << settle_census.events
              << " horizontal_normals=" << settle_census.horizontal_normals
              << " worst_pen=" << settle_census.worst_penetration << " m"
              << std::endl;
    std::cout << "  contacts REST:   events=" << cube_census.events
              << " horizontal_normals=" << cube_census.horizontal_normals
              << " worst_pen=" << cube_census.worst_penetration << " m"
              << std::endl;
    std::cout << "  both cubes asleep for " << asleep_frames
              << "/300 REST frames  <-- the CONTROL for the boulder case, "
                 "which never sleeps at all" << std::endl;

    std::cout << "\n[STEP 5] Asserts:" << std::endl;
    const int before = g_failures;
    check(z1 > 0.0f, "[INV-1]",
          "cube1 stands above the turtle (z " + std::to_string(z1) + ")");
    check(z2 > 0.0f, "[INV-1]",
          "cube2 stands above the turtle (z " + std::to_string(z2) + ")");
    check(max_drift1 < 0.05f, "[INV-34]",
          "cube1 holds its height: z-drift " + std::to_string(max_drift1) + " m");
    check(max_drift2 < 0.05f, "[INV-34]",
          "cube2 holds its height: z-drift " + std::to_string(max_drift2) + " m");
    // The REST census is EMPTY here, and that is the finding, not a gap: a
    // sleeping body reports no contacts. So the normal and penetration
    // claims are made against the SETTLE census, which has events, and the
    // emptiness of the REST census is asserted as what it means.
    check(settle_census.events > 0, "hygiene",
          "the SETTLE contact census is not vacuous: " +
          std::to_string(settle_census.events) + " events");
    check(settle_census.horizontal_normals == 0, "[INV-12]",
          "a flat box on a flat slab makes vertical normals only (" +
          std::to_string(settle_census.horizontal_normals) + " horizontal "
          "of " + std::to_string(settle_census.events) + ")");
    check(settle_census.worst_penetration < PhysicsV4::SLOP, "[INV-2]",
          "the cube never sinks into the slab: worst overlap " +
          std::to_string(settle_census.worst_penetration) + " m");
    check(asleep_frames > 0, "[INV-34]",
          "rest is REACHED: both cubes asleep for " +
          std::to_string(asleep_frames) + " of 300 REST frames");

    const bool all_pass = (g_failures == before);

    if (is_interactive) {
        wait_for_space(engine, all_pass ? "ALL PASS" : "SOME FAILED", {});
    }

    return all_pass;
}

// ============================================================================
// Main Entry Point
// ============================================================================
bool test_physics_rock() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║     PHYSICS ROCK TEST - Gluon-Based Rock Generation         ║" << std::endl;
    std::cout <<   "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "\nTest Overview:" << std::endl;
    std::cout << "  Test 1: Large boulder stability (BORN-RED under the single law)" << std::endl;
    std::cout << "  Test 2: Cube collision test" << std::endl;
    std::cout << "\nAcceptance: a settled body left alone does not move (< 5 cm)" << std::endl;
    std::cout << "WORLD: " << lever_world() << std::endl;

    g_failures = 0;

    // Single engine for all tests
    Engine engine;
    EngineConfig config;

    const char* interactive_env = std::getenv("INTERACTIVE");
    bool is_interactive = (interactive_env != nullptr && std::string(interactive_env) == "1");

    config.create_display = is_interactive;
    config.window_width = 1600;
    config.window_height = 1200;
    config.window_title = "Physics Rock Test";
    config.enable_chat_window = false;

    std::cout << "\n[MODE] " << (is_interactive ? "INTERACTIVE" : "HEADLESS") << std::endl;
    if (!is_interactive) {
        std::cout << "[MODE] Set INTERACTIVE=1 for visual mode" << std::endl;
    }

    if (engine.initialize(config) != 0) {
        std::cerr << "❌ FAIL: Engine initialization failed" << std::endl;
        return false;
    }

    auto& camera = engine.get_camera_system();
    camera.set_position(0.0f, -15.0f, 10.0f);

    bool all_pass = true;

    // Test 1: Large boulder
    all_pass &= test_physics_rock_boulder(engine, is_interactive);

    // Test 2: Cube collision
    all_pass &= test_physics_rock_cubes(engine, is_interactive);

    std::cout << "\n══════════════════════════════════════════════════════════════" << std::endl;
    std::cout << "ASSERTS: " << g_failures << " red" << std::endl;
    if (all_pass) {
        std::cout << "✅ ALL PHYSICS ROCK TESTS PASSED" << std::endl;
    } else {
        std::cout << "❌ SOME PHYSICS ROCK TESTS FAILED" << std::endl;
    }
    std::cout << "══════════════════════════════════════════════════════════════\n" << std::endl;

    return all_pass;
}
