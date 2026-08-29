// =============================================================================
// SCENE: THE LIMITS CAMPAIGN — G-57..G-62 (owner ruling 2026-08-28)
// =============================================================================
// "Study/understand better the limitations of what we have first...
// to understand what we're missing from the engine itself."
//
// One parameterized scene: a list of boxes (dims, material, EXPLICIT
// friction, birth spin/velocity), stepped identically by the headless
// drivers and the window. Every case's expectation is derived in its
// GEDANKEN record; the bands here restate that arithmetic.
//
// ALL LIMITS INSTRUMENTS PROBE THE TRIO WORLD (FRICTION_TWIST +
// WARM_LEARN + MANIFOLD_SPAN). The drivers set the levers themselves
// before engine init — the campaign question is where the CANDIDATE
// DEFAULT breaks, so the world under test is fixed, not ambient.
//
// THE FRICTION ANCHOR (campaign finding, 2026-08-28): every particle
// is born with friction = 0.5 (particle_core.h) and SetMaterial never
// writes it — every measured baseline in the battery, R1's 0.217 s
// stop included, is mu = 0.5 physics, not STONE's nominal 0.4. This
// scene sets friction EXPLICITLY on every body; nothing here relies
// on the default. Bands scale from the measured anchor.
// =============================================================================
#pragma once

#include "core/argus.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace scene_limits {

constexpr float DT = 1.0f / 60.0f;
constexpr float MU_BASE  = 0.5f;    // the engine's actual baseline (see header)
constexpr float R1_STOP  = 0.217f;  // measured grindstone anchor (G-55, mu 0.5, L 1)
// G-55's stop band at the anchor (mu 0.5, L 1 m). Scaled per case by
// (MU_BASE/mu) — the grindstone law is 1/mu — and by L (alpha ~ 1/L).
constexpr float BAND_LO = 0.2f, BAND_HI = 0.6f;
constexpr float SPIN_NOISE = 0.05f;   // rad/s (INV-24 class)
constexpr float SPEED_MAX  = 10.0f;   // m/s (INV-3)
constexpr float LZ_BAND    = 1.05f;   // peak |L_z| <= initial * this

struct BodySpec {
    float sx, sy, sz;        // dims (width=x, height=y, thickness=z)
    float x, y, z;           // birth centre
    Materials::Type mat;
    float mu;                // explicit friction, always
    float wz0 = 0.0f;        // birth spin about +Z
    float vx0 = 0.0f;        // birth velocity along X (the nudge)
    const char* label = "body";
};

struct StopBand  { int body; float lo, hi; };          // spin-death window (s)
struct HeightRef { int body; float z_static, tol; };   // INV-4 stand
struct Case {
    const char* name;                  // for the panel and the log
    const char* gid;                   // the law every assert cites
    std::vector<BodySpec> bodies;
    int run_frames = 300;
    std::vector<StopBand>  stop_bands;
    std::vector<HeightRef> heights;
    std::vector<int> still;            // must NOT be dragged: peak |wz| < SPIN_NOISE
    bool must_sleep = false;           // every body is_at_rest at the end
    int   slide_body = -1;             // travelled distance assert (the nudge case)
    float slide_lo = 0, slide_hi = 0;  // metres along X
    const char* demo1 = "";            // window: DEMONSTRATING line
    const char* demo2 = "";            // window: WATCH line
    const char* waiver = nullptr;      // narrated DOF deliberately not asserted
};

// Set the trio BEFORE engine/physics construction. The levers are
// latched with function-local statics at first use inside the solver,
// so this must run before the first physics.update().
inline void set_trio_world() {
    setenv("FRICTION_TWIST", "1", 1);
    setenv("WARM_LEARN", "1", 1);
    setenv("MANIFOLD_SPAN", "1", 1);
}

struct Scene {
    logosphere::Argus argus;
    std::vector<int> ids;
    std::vector<BodySpec> specs;

    int build(ParticleSystem& ps, const std::vector<BodySpec>& s,
              float x_off = 0.0f) {
        specs = s;
        ids.clear();
        for (const BodySpec& b : specs) {
            Particle p{};
            p.shape = ParticleShape::BOX;
            p.width = b.sx; p.height = b.sy; p.thickness = b.sz;
            p.size = std::fmax(b.sx, std::fmax(b.sy, b.sz));
            p.x = x_off + b.x; p.y = b.y; p.z = b.z;
            p.omega_z = b.wz0;
            p.vx = b.vx0;
            const bool hot = (b.wz0 != 0.0f) || (b.vx0 != 0.0f);
            p.r = hot ? 0.95f : 0.55f;
            p.g = 0.6f;
            p.b = hot ? 0.3f : 0.8f;
            p.a = 1.0f;
            p.SetMaterial(b.mat);
            p.friction = b.mu;            // explicit, never the default
            ids.push_back(ps.queue_particle_addition(p));
        }
        ps.flush_pending_particles();
        for (size_t i = 0; i < ids.size(); ++i)
            argus.watch(ids[i], specs[i].label);
        return (int)ids.size();
    }

    // TELEPORT LAW: same resets as scene_torsion_rungs::rearm.
    void rearm(ParticleSystem& ps, PhysicsSystem& physics, float x_off = 0.0f) {
        auto parts = ps.lock_particles_for_write();
        for (size_t i = 0; i < ids.size(); ++i) {
            Particle& p = parts[ids[i]];
            const BodySpec& b = specs[i];
            p.x = x_off + b.x; p.y = b.y; p.z = b.z;
            p.vx = b.vx0; p.vy = p.vz = 0.0f;
            p.omega_x = p.omega_y = 0.0f;
            p.omega_z = b.wz0;
            p.rotation_x = p.rotation_y = p.rotation_z = 0.0f;
            p.rotation_q = logosphere::Quat::identity();
            p.is_at_rest = false;
            p.frames_at_rest = 0;
            p.low_velocity_frames = 0;
            p.quiet_growth_run = 0;
            p.rest_quiet_sq = 1e9f;
            physics.forget_body((size_t)ids[i]);
            argus.reset_milestones(ids[i]);
        }
    }

    void step(ParticleSystem& ps, PhysicsSystem& physics, int frame) {
        ps.update_bvh();
        physics.update(DT);
        argus.observe(ps, frame);
    }

    float z(ParticleSystem& ps, int i) const {
        return ps.lock_particles_for_read()[ids[i]].z;
    }
    float x(ParticleSystem& ps, int i) const {
        return ps.lock_particles_for_read()[ids[i]].x;
    }
    float wz(ParticleSystem& ps, int i) const {
        return ps.lock_particles_for_read()[ids[i]].omega_z;
    }
    float spin(ParticleSystem& ps, int i) const {
        const Particle& p = ps.lock_particles_for_read()[ids[i]];
        return std::sqrt(p.omega_x*p.omega_x + p.omega_y*p.omega_y
                       + p.omega_z*p.omega_z);
    }
    bool asleep(ParticleSystem& ps, int i) const {
        return ps.lock_particles_for_read()[ids[i]].is_at_rest;
    }
    float total_Lz(ParticleSystem& ps) const {
        auto parts = ps.lock_particles_for_read();
        float L = 0.0f;
        for (int id : ids) {
            const Particle& p = parts[id];
            L += p.GetMomentOfInertia() * p.omega_z;
        }
        return L;
    }
};

// ---------------------------------------------------------------------------
// THE CASE TABLES — geometry + bands in ONE place (criterion b: the
// window must read exactly what the headless asserts read).
// Derivations live in the G records; comments restate the arithmetic.
// ---------------------------------------------------------------------------

inline BodySpec cube(float L, float x, float z, Materials::Type m, float mu,
                     float wz0, const char* label, float vx0 = 0.0f) {
    return BodySpec{L, L, L, x, 0.0f, z, m, mu, wz0, vx0, label};
}

// G-58 THE ICE RINK: the grindstone law is 1/mu; the band scales by
// (MU_BASE/mu). Ice 0.05 -> stop ~2.2 s in [2.0, 6.0]; rubber 0.8 ->
// ~0.14 s in [0.125, 0.375]. The nudged icy cube decelerates at
// mu*g = 0.49 m/s^2: 1 m/s -> stop in ~2.0 s after ~1.02 m, then sleeps.
inline std::vector<Case> cases_ice() {
    std::vector<Case> v;
    v.push_back({"ICE: spinner at mu 0.05", "G-58",
        {cube(1, 0, 0.5f, Materials::Type::STONE, 0.05f, 3.0f, "ice spinner")},
        540,
        {{0, BAND_LO * (MU_BASE/0.05f), BAND_HI * (MU_BASE/0.05f)}},
        {{0, 0.5f, 0.02f}}, {}, false, -1, 0, 0,
        "DEMONSTRATING: the same spinner on ICE (G-58).",
        "WATCH: 10x less grip, 10x longer spin - it must turn ~2 s."});
    v.push_back({"RUBBER: spinner at mu 0.8", "G-58",
        {cube(1, 0, 0.5f, Materials::Type::STONE, 0.8f, 3.0f, "rubber spinner")},
        300,
        {{0, BAND_LO * (MU_BASE/0.8f), BAND_HI * (MU_BASE/0.8f)}},
        {{0, 0.5f, 0.02f}}, {}, false, -1, 0, 0,
        "DEMONSTRATING: the same spinner on RUBBER (G-58).",
        "WATCH: more grip than stone - dead in a blink, no drama."});
    v.push_back({"ICE: the nudged glide", "G-58",
        {cube(1, 0, 0.5f, Materials::Type::STONE, 0.05f, 0.0f, "glider", 1.0f)},
        300,
        {}, {{0, 0.5f, 0.02f}}, {}, true, 0, 0.75f, 1.35f,
        "DEMONSTRATING: a cube shoved 1 m/s across ICE (G-58).",
        "WATCH: it glides ~1 m, slows at mu*g, stops, and SLEEPS."});
    return v;
}

// G-57 THE ANVIL LADDER: pure density ratios at identical geometry.
// Statics are trivial on paper at ANY ratio; the question is the
// solver's convergence knee. No spin: no rotation may be invented.
inline std::vector<Case> cases_anvil() {
    auto pair = [](Materials::Type top, const char* name,
                   const char* d2) {
        return Case{name, "G-57",
            {cube(1, 0, 0.5f, Materials::Type::WOOD_SOFT, MU_BASE, 0, "supporter"),
             cube(1, 0, 1.5f, top, MU_BASE, 0, "anvil")},
            300, {},
            {{0, 0.5f, 0.02f}, {1, 1.5f, 0.02f}}, {}, true, -1, 0, 0,
            "DEMONSTRATING: a heavy block on a light one (G-57).", d2};
    };
    std::vector<Case> v;
    v.push_back(pair(Materials::Type::STONE,
        "ANVIL 5:1 (stone on softwood)",
        "WATCH: 5x its weight - the wood must hold, still, asleep."));
    v.push_back(pair(Materials::Type::IRON,
        "ANVIL 15.6:1 (iron on softwood)",
        "WATCH: 15x its weight - same law, heavier truth."));
    v.push_back(pair(Materials::Type::GOLD,
        "ANVIL 38.6:1 (gold on softwood)",
        "WATCH: 38x its weight - where solvers usually start lying."));
    Case extreme{"ANVIL 96.5:1 (gold on leaves)", "G-57",
        {cube(1, 0, 0.5f, Materials::Type::LEAVES, MU_BASE, 0, "leaves"),
         cube(1, 0, 1.5f, Materials::Type::GOLD, MU_BASE, 0, "gold anvil")},
        300, {},
        {{0, 0.5f, 0.02f}, {1, 1.5f, 0.02f}}, {}, true, -1, 0, 0,
        "DEMONSTRATING: gold on a bale of leaves (G-57).",
        "WATCH: 96x its weight. Either it holds, or we found the knee."};
    v.push_back(extreme);
    return v;
}

// G-59 THE SIZE LADDER: alpha ~ 1/L, so the stop band scales by L.
// Height tolerance scales with the body (floored at 2 mm).
inline std::vector<Case> cases_size() {
    auto rung = [](float L, int frames, const char* name, const char* d2) {
        return Case{name, "G-59",
            {cube(L, 0, L * 0.5f, Materials::Type::STONE, MU_BASE, 3.0f,
                  "spinner")},
            frames,
            {{0, BAND_LO * L, BAND_HI * L}},
            {{0, L * 0.5f, std::fmax(0.002f, 0.02f * L)}},
            {}, false, -1, 0, 0,
            "DEMONSTRATING: the same law at another size (G-59).", d2};
    };
    std::vector<Case> v;
    v.push_back(rung(0.1f, 300, "SIZE: the 10 cm die",
        "WATCH: a die stops in a flash - stop time scales with size."));
    v.push_back(rung(3.0f, 300, "SIZE: the 3 m boulder",
        "WATCH: a boulder turns for a full second before dying."));
    return v;
}

// G-60 THE UNEQUAL FOOTPRINT: the twist brake must read the CONTACT
// PATCH, not either body's own face. Big-on-small brakes at patch 0.4
// (stop ~0.54 s, x2.5 the equal case); small-on-big at its own 0.4
// face (~0.087 s). Both stands are statics-derivable (COM inside).
inline std::vector<Case> cases_footprint() {
    std::vector<Case> v;
    // The pedestal's rotation is WAIVED, not asserted: its anchor
    // (turtle friction, N = 26.1 kN) against the drag (top joint,
    // N = 24.5 kN) is a 1.07 torque ratio - marginal by arithmetic,
    // nothing like R3's 2:1, so a 160 kg pedestal under a 2500 kg
    // grinder being wrenched around is physical. (First registration
    // asserted "not dragged" off R3 intuition - derivation corrected
    // 2026-08-28, measured peak |wz| 2.23 booked in the G-60 record.)
    v.push_back({"FOOTPRINT: big spinner on small base", "G-60",
        {cube(0.4f, 0, 0.2f, Materials::Type::STONE, MU_BASE, 0, "small base"),
         cube(1, 0, 0.9f, Materials::Type::STONE, MU_BASE, 3.0f, "big spinner")},
        300,
        {{1, BAND_LO / 0.4f, BAND_HI / 0.4f}},
        {{0, 0.2f, 0.02f}, {1, 0.9f, 0.02f}},
        {}, false, -1, 0, 0,
        "DEMONSTRATING: a big spinner on a small pedestal (G-60).",
        "WATCH: less patch, less brake - it spins 2.5x longer.",
        "the pedestal's rotation (anchor/drag torque 1.07, marginal - "
        "either outcome physical; measured: dragged)"});
    v.push_back({"FOOTPRINT: small spinner on big base", "G-60",
        {cube(1, 0, 0.5f, Materials::Type::STONE, MU_BASE, 0, "big base"),
         cube(0.4f, 0, 1.2f, Materials::Type::STONE, MU_BASE, 3.0f,
              "small spinner")},
        300,
        {{1, BAND_LO * 0.4f, BAND_HI * 0.4f}},
        {{0, 0.5f, 0.02f}, {1, 1.2f, 0.02f}},
        {0}, false, -1, 0, 0,
        "DEMONSTRATING: a small spinner on a big table (G-60).",
        "WATCH: tiny patch, tiny inertia - dead almost instantly."});
    return v;
}

// G-62 THE SLIP JOINT: the top cube carries mu 0.05, so ONLY its
// joint is icy (combined = min). It spins ~10x longer than R1 and
// passes ~10x less torsion down: the cubes below must not turn.
inline std::vector<Case> cases_slipjoint() {
    std::vector<Case> v;
    v.push_back({"SLIP JOINT: icy top on a grippy column", "G-62",
        {cube(1, 0, 0.5f, Materials::Type::STONE, MU_BASE, 0, "bottom"),
         cube(1, 0, 1.5f, Materials::Type::STONE, MU_BASE, 0, "middle"),
         cube(1, 0, 2.5f, Materials::Type::STONE, 0.05f, 3.0f, "icy spinner")},
        540,
        {{2, BAND_LO * (MU_BASE/0.05f), BAND_HI * (MU_BASE/0.05f)}},
        {{0, 0.5f, 0.02f}, {1, 1.5f, 0.02f}, {2, 2.5f, 0.02f}},
        {0, 1}, false, -1, 0, 0,
        "DEMONSTRATING: a greased bearing in a clamped stack (G-62).",
        "WATCH: the icy top spins ~2 s; the column below never turns."});
    return v;
}

// G-61 THE PARTICLE FLOOR: the four-cube spin tower on a 3x3 raft of
// stone tiles, feet straddling a SEAM. Physics identical to the
// turtle case; the engine hazards are the seam, warm cache on nine
// more interfaces, and 98 kN through 0.2 m-thin bodies.
inline std::vector<Case> cases_floor() {
    Case c;
    c.name = "PARTICLE FLOOR: the tower on tiles, feet on a seam";
    c.gid = "G-61";
    for (int tx = -1; tx <= 1; ++tx)
        for (int ty = -1; ty <= 1; ++ty)
            c.bodies.push_back(BodySpec{1, 1, 0.2f, (float)tx, (float)ty, 0.1f,
                                        Materials::Type::STONE, MU_BASE,
                                        0, 0, "tile"});
    for (int i = 0; i < 4; ++i)
        c.bodies.push_back(cube(1, 0.5f, 0.7f + (float)i,
                                Materials::Type::STONE, MU_BASE,
                                i == 1 ? 3.0f : 0.0f,
                                i == 1 ? "spinner" : "tower"));
    c.run_frames = 300;
    for (int i = 0; i < 9; ++i) c.heights.push_back({i, 0.1f, 0.02f});
    for (int i = 0; i < 4; ++i) c.heights.push_back({9 + i, 0.7f + (float)i, 0.02f});
    c.demo1 = "DEMONSTRATING: the spin tower on REAL ground (G-61).";
    c.demo2 = "WATCH: tiles, a seam under the feet, 98 kN through them.";
    return {c};
}

// ---------------------------------------------------------------------------
// THE SHARED RUNNER — one evaluator for the headless drivers and the
// window (criterion b). Prints measurements first, then every assert
// naming its law. Returns the failure count.
// ---------------------------------------------------------------------------
inline int run_case(ParticleSystem& ps, PhysicsSystem& physics,
                    const Case& c, bool trace) {
    Scene scene;
    const int n = scene.build(ps, c.bodies);
    int failures = 0;
    char buf[192];
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", what);
        if (!ok) failures++;
    };

    const float L0 = scene.total_Lz(ps);
    float peak_absL = std::fabs(L0);
    std::vector<float> stop_time(n, -1.0f);
    std::vector<float> peak_abs_wz(n, 0.0f);
    const float x0_slide = c.slide_body >= 0 ? scene.x(ps, c.slide_body) : 0.0f;

    for (int f = 0; f < c.run_frames; ++f) {
        scene.step(ps, physics, f);
        peak_absL = std::fmax(peak_absL, std::fabs(scene.total_Lz(ps)));
        for (int i = 0; i < n; ++i) {
            peak_abs_wz[i] = std::fmax(peak_abs_wz[i],
                                       std::fabs(scene.wz(ps, i)));
            if (stop_time[i] < 0.0f && scene.spin(ps, i) < SPIN_NOISE)
                stop_time[i] = (float)(f + 1) * DT;
        }
        if (trace && (f < 30 || f % 30 == 0)) {
            std::printf("  [f%03d]", f);
            for (int i = 0; i < n && i < 6; ++i)
                std::printf("  wz%d %+7.3f z%d %7.4f", i, scene.wz(ps, i),
                            i, scene.z(ps, i));
            std::printf("\n");
        }
    }

    std::printf("\n-- %s --\n", c.name);
    for (int i = 0; i < n; ++i)
        std::printf("  [measure] %-12s z %.4f  spin %.4f  peak|wz| %.4f  "
                    "stop %.3f  peak speed %.4f%s\n",
                    c.bodies[i].label, scene.z(ps, i), scene.spin(ps, i),
                    peak_abs_wz[i], stop_time[i],
                    scene.argus.peak_speed(scene.ids[i]),
                    scene.asleep(ps, i) ? "  [asleep]" : "");
    if (L0 != 0.0f)
        std::printf("  [measure] L_z initial %.1f  peak %.1f  final %.1f\n",
                    L0, peak_absL, scene.total_Lz(ps));

    for (const StopBand& b : c.stop_bands) {
        std::snprintf(buf, sizeof(buf),
                      "%s: %s dies at the derived rate (stop %.3f s in "
                      "[%.2f, %.2f])", c.gid, c.bodies[b.body].label,
                      stop_time[b.body], b.lo, b.hi);
        check(stop_time[b.body] >= b.lo && stop_time[b.body] <= b.hi, buf);
    }
    // Nothing may keep or invent rotation (G-48/INV-24 class), every case.
    {
        bool all_dead = true;
        float worst = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float s = scene.spin(ps, i);
            if (s >= SPIN_NOISE) all_dead = false;
            worst = std::fmax(worst, s);
        }
        std::snprintf(buf, sizeof(buf),
                      "%s/INV-24: every spin is dead at the end "
                      "(worst %.4f < %.2f)", c.gid, worst, SPIN_NOISE);
        check(all_dead, buf);
    }
    if (L0 != 0.0f) {
        std::snprintf(buf, sizeof(buf),
                      "%s/INV-3: L_z is never created (peak %.1f <= "
                      "initial %.1f x %.2f)", c.gid, peak_absL,
                      std::fabs(L0), LZ_BAND);
        check(peak_absL <= std::fabs(L0) * LZ_BAND, buf);
    }
    for (int i : c.still) {
        std::snprintf(buf, sizeof(buf),
                      "%s: %s is NOT dragged (peak |wz| %.4f < %.2f)",
                      c.gid, c.bodies[i].label, peak_abs_wz[i], SPIN_NOISE);
        check(peak_abs_wz[i] < SPIN_NOISE, buf);
    }
    for (const HeightRef& h : c.heights) {
        std::snprintf(buf, sizeof(buf),
                      "%s/INV-4: %s stands at its static height "
                      "(%.4f vs %.2f, tol %.3f)", c.gid,
                      c.bodies[h.body].label, scene.z(ps, h.body),
                      h.z_static, h.tol);
        check(std::fabs(scene.z(ps, h.body) - h.z_static) < h.tol, buf);
    }
    if (c.slide_body >= 0) {
        const float dist = scene.x(ps, c.slide_body) - x0_slide;
        std::snprintf(buf, sizeof(buf),
                      "%s: the glide travels mu*g's distance "
                      "(%.3f m in [%.2f, %.2f])", c.gid, dist,
                      c.slide_lo, c.slide_hi);
        check(dist >= c.slide_lo && dist <= c.slide_hi, buf);
    }
    if (c.waiver)
        std::printf("  [WAIVED] %s\n", c.waiver);
    if (c.must_sleep) {
        bool all = true;
        for (int i = 0; i < n; ++i) all = all && scene.asleep(ps, i);
        std::snprintf(buf, sizeof(buf),
                      "%s/INV-24: the world falls ASLEEP (all %d bodies "
                      "at rest)", c.gid, n);
        check(all, buf);
    }
    for (int i = 0; i < n; ++i) {
        const float pk = scene.argus.peak_speed(scene.ids[i]);
        if (pk >= SPEED_MAX) {
            std::snprintf(buf, sizeof(buf),
                          "INV-3: %s speeds stayed bounded (peak %.4f)",
                          c.bodies[i].label, pk);
            check(false, buf);
        }
    }
    return failures;
}

// A driver: fresh engine per case (no cross-case contamination).
inline int run_all(const char* title, const std::vector<Case>& cases) {
    set_trio_world();
    static const bool trace = std::getenv("ARGUS_TRACE") != nullptr;
    const char* only_env = std::getenv("LIMITS_CASE");
    const int only = only_env ? std::atoi(only_env) : -1;
    std::printf("\n=== %s ===\n", title);
    std::printf("  WORLD: trio (FRICTION_TWIST+WARM_LEARN+MANIFOLD_SPAN, "
                "set by the test) - probing the candidate default\n");
    int failures = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        if (only >= 0 && (int)i != only) continue;
        ParticleSystem ps;
        PhysicsSystem physics;
        if (!physics.initialize(ps)) { std::printf("  [FAIL] init\n"); return 1; }
        failures += run_case(ps, physics, cases[i], trace);
        physics.shutdown();
    }
    std::printf("\n  %s (%d failures)\n",
                failures == 0 ? "EVERY CASE ANSWERS ITS LAW"
                              : "RED where informative: the limit map is the "
                                "point (booked, not hidden)",
                failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace scene_limits
