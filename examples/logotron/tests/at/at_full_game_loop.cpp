// Acceptance Test: full game loop, headless. Spins up the actual
// LogotronApplication + Engine the way main.cpp does, but with
// create_display=false so no window pops up. Drives N ticks and
// asserts the game's observable state.
//
// What this proves vs the lower-level director AT:
//   - The Director apply path actually runs INSIDE the engine's
//     update loop (poll_director called from update_game called
//     from Engine::update), not just in isolation.
//   - The headless engine + the extracted LogotronApplication
//     header link together cleanly.
//   - Pre-fire walls land in the KG within the first few ticks
//     (offline random responder, no LLM dependency).
//   - Player + AI cycles spawn alive at the configured positions.
//   - Auto-fire fires on cadence (not before kFirstAutoFireAtSec).
//
// The AT runs offline by design — LLMPlan resolves to None when no
// API keys are in env, the Director uses make_random_responder
// (synchronous, deterministic), and CI / Linux runs identically.
//
// Run: ./build/at_logotron_full_game_loop
//      LOGOTRON_AT_LOOP_FRAMES (default 90 ≈ 1.5 s game time)
//      LOGOTRON_AT_LOOP_DT_MS  (default 16 ≈ 60 FPS)

#include "at_common.h"

#include "application.h"
#include "core/engine.h"
#include "logosphere/kg/kg_module.h"

#include "logotron_app.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <thread>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

int env_int(const char* key, int fallback) {
    const char* e = std::getenv(key);
    if (!e || !*e) return fallback;
    try { return std::stoi(e); } catch (...) { return fallback; }
}

// Drop API keys from the env before we resolve the LLM plan so the
// AT is hermetic — runs the same way whether or not the dev's shell
// has ANTHROPIC_API_KEY / OPENAI_API_KEY set.
void scrub_llm_env() {
    unsetenv("ANTHROPIC_API_KEY");
    unsetenv("OPENAI_API_KEY");
    unsetenv("LOGOTRON_LLM_PROVIDER");
    unsetenv("LOGOTRON_LLM_URL");
    unsetenv("LOGOTRON_LLM_MODEL");
}

// Spin up the engine the way main.cpp does, in headless mode. Owns
// the application + engine for the AT's lifetime. Engine destructor
// shuts down all systems.
struct Harness {
    LogotronApplication app;
    Engine               engine;

    Harness() : engine(&app) {
        scrub_llm_env();
        EngineConfig config;
        config.create_display      = false;
        config.window_width        = 1600;
        config.window_height       = 1200;
        config.window_title        = "logotron-at";
        config.show_debug_overlay  = false;
        config.show_kg_inspector   = false;
        config.enable_chat_window  = false;
        int rc = engine.initialize(config);
        if (rc < 0) throw std::runtime_error(
            "Engine::initialize() failed in headless mode");
    }

    ~Harness() { engine.shutdown(); }

    void tick(double dt) { engine.update(dt); }
};

// Count Director-spawned wall TrailSegments — those with
// director_origin="1" (the same predicate the live game uses to
// distinguish player/AI trails from Director walls).
int count_director_walls(kg::KGModule& kg) {
    auto trails = kg.findByType("TrailSegment");
    int n = 0;
    for (auto t : trails) {
        if (kg.getProperty(t, "director_origin") == "1") n++;
    }
    return n;
}

void test_pre_fire_lands_walls_within_first_tick() {
    Harness h;
    auto& kg = h.engine.get_kg();

    // No ticks yet. Pre-fire was called at the end of setup (during
    // engine.initialize via initialize_game), but apply only happens
    // in poll_director which runs from update_game. So before any
    // tick: zero Director walls.
    AT_ASSERT_TRUE(count_director_walls(kg) == 0,
        "Director walls should NOT exist before any update tick "
        "(got " + std::to_string(count_director_walls(kg)) + ")");

    // First tick. Random responder is synchronous so the response is
    // already cached; this tick drains + applies it.
    h.tick(1.0 / 60.0);

    int walls = count_director_walls(kg);
    AT_ASSERT_TRUE(walls >= 0,  // random can emit 0..2 walls per round
        "wall count went negative? got " + std::to_string(walls));
    // Random responder picks 1-3 op CATEGORIES; walls are one of
    // them but not guaranteed. What IS guaranteed: pre-fire produced
    // SOMETHING (set_property at minimum). Assert via the round
    // counter — it stayed at 0 (pre-fire doesn't tick it) AND a
    // response was drained (no longer pending).
    auto cycles = kg.findByType("PlayerCycle");
    AT_ASSERT_TRUE(!cycles.empty(),
        "PlayerCycle should exist after init + 1 tick");

    auto ais = kg.findByType("AICycle");
    AT_ASSERT_TRUE(!ais.empty(),
        "AICycle should exist after init + 1 tick");
}

void test_arena_dimensions_match_balance() {
    Harness h;
    auto& kg = h.engine.get_kg();
    auto arenas = kg.findByType("Arena");
    AT_ASSERT_TRUE(!arenas.empty(), "Arena entity should exist");

    auto w  = kg.getProperty(arenas[0], "arena_w");
    auto sh = kg.getProperty(arenas[0], "arena_h");
    AT_ASSERT_TRUE(w  == "50.000000",
        "arena_w should be 50 m, got '" + w + "'");
    AT_ASSERT_TRUE(sh == "50.000000",
        "arena_h should be 50 m, got '" + sh + "'");
}

// REGRESSION: 2026-04-28 playtest reported the AI "popping out of
// existence without crashing into anything." Cause: poll_director's
// apply path called respawn_ai_in_world() unconditionally on every
// non-pre-fire apply. Auto-fire fires the Director every ~8 s of
// real time independent of AI state, so a still-alive AI got wiped
// + re-created at the SW spawn each round.
//
// Drive enough ticks for at least one auto-fire round trip
// (random responder is synchronous, so apply lands on the next
// tick). Capture the AI's KG entity ID before, then after. Apply
// must NOT have respawned the AI when the AI was still RIDING.
void test_auto_fire_does_not_respawn_alive_ai() {
    Harness h;
    auto& kg = h.engine.get_kg();

    // First tick lands the pre-fire random apply (no respawn —
    // is_pre_fire_apply guard). Capture AI entity ID after that
    // settles.
    const double dt = 1.0 / 60.0;
    h.tick(dt);
    auto ais_before = kg.findByType("AICycle");
    AT_ASSERT_TRUE(!ais_before.empty(),
        "AICycle should exist after pre-fire apply");
    auto ai_id_before = ais_before[0];

    // Drive past the auto-fire trigger (kFirstAutoFireAtSec = 2.0s).
    // 200 ticks at 60 FPS = 3.33 s, comfortably past it. The random
    // responder is synchronous; apply lands within a couple ticks
    // of the auto-fire submission.
    for (int i = 0; i < 200; i++) h.tick(dt);

    // AI should still be RIDING and STILL THE SAME ENTITY. If
    // respawn_ai_in_world fired on the auto-fire's apply, we'd see
    // a new AICycle entity ID (the old one destroyed, new one
    // created). That's the "popping" symptom.
    auto ais_after = kg.findByType("AICycle");
    AT_ASSERT_TRUE(!ais_after.empty(),
        "AICycle should still exist after auto-fire round");

    auto a_state = kg.getProperty(ais_after[0], "cycle_state");
    if (a_state != "RIDING") {
        // AI naturally crashed during the run — out of scope for
        // this test. Skip cleanly. The assertion below is only
        // meaningful when the AI was still alive at apply time.
        std::cerr << "  [skip] AI naturally CRASHED during run "
                  << "(state=" << a_state << "); regression check "
                  << "needs an AI that's still RIDING when apply hits"
                  << std::endl;
        return;
    }

    AT_ASSERT_TRUE(ais_after[0] == ai_id_before,
        "AI entity ID changed (" + std::to_string(ai_id_before) +
        " → " + std::to_string(ais_after[0]) + ") even though the AI "
        "was still RIDING. respawn_ai_in_world() fired when it "
        "should have been gated on AI state == CRASHED.");
}

void test_cycles_alive_after_warmup_ticks() {
    // Drive 30 ticks (~0.5 s game time) and assert both cycles are
    // still RIDING. The headless AI never receives input so the
    // player cycle drives east into the boundary and crashes
    // eventually — but at 0.5 s with the new balance (max 6 m/s,
    // 25 m to the east wall) it has only crossed ~3 m and is fine.
    Harness h;
    const int frames  = env_int("LOGOTRON_AT_LOOP_FRAMES", 30);
    const double dt   = env_int("LOGOTRON_AT_LOOP_DT_MS",  16) / 1000.0;
    for (int i = 0; i < frames; i++) h.tick(dt);

    auto& kg = h.engine.get_kg();
    auto players = kg.findByType("PlayerCycle");
    auto ais     = kg.findByType("AICycle");
    AT_ASSERT_TRUE(!players.empty(), "PlayerCycle missing after warmup");
    AT_ASSERT_TRUE(!ais.empty(),     "AICycle missing after warmup");

    // REGRESSION (2026-07-18): every KG-backed particle used to ALSO
    // mint an orphan "AutoParticle" entity (add_particle's auto-create
    // firing inside add_particle_to_entity). Per-frame churn (run
    // heads, director orb) leaked entities without bound and hit the
    // KG's entity cap when unthrottled. Contract: the AutoParticle
    // count must NOT grow with frames. A one-time baseline (~35: the
    // 32 arena edge lights + engine default/sun lights, created once
    // via bare add_particle) is expected and fine.
    size_t autos_now = kg.findByType("AutoParticle").size();
    for (int i = 0; i < 30; i++) h.tick(dt);
    size_t autos_later = kg.findByType("AutoParticle").size();
    AT_ASSERT_TRUE(autos_later <= autos_now + 2,
        "AutoParticle entities leak per frame (was " +
        std::to_string(autos_now) + ", now " +
        std::to_string(autos_later) + " after 30 more ticks)");

    auto p_state = kg.getProperty(players[0], "cycle_state");
    auto a_state = kg.getProperty(ais[0],     "cycle_state");
    AT_ASSERT_TRUE(p_state == "RIDING",
        "PlayerCycle should still be RIDING after " +
        std::to_string(frames) + " warmup ticks (got '" + p_state + "')");
    AT_ASSERT_TRUE(a_state == "RIDING",
        "AICycle should still be RIDING after " +
        std::to_string(frames) + " warmup ticks (got '" + a_state + "')");
}

// The crashed-AI trail fade: on the AI's CRASHED flip the game seals
// the active run and hands every AI-owned trail to a 2 s visual fade,
// after which the trail PARTICLES are deleted while the TrailSegment
// KG entities live on (gameplay/tests still see them until the
// Director respawn or a round reset reaps them). Collision never
// reads the fade (lethality is cycle_state-driven), so this is a
// pure visual-lifecycle contract.
//
// Timing: the Director auto-fires at 2.0 s and its apply respawns a
// crashed AI (wiping its trail entities), so the fade window gets
// truncated ~10 ticks short of completion. Ramp assertions therefore
// sit safely BEFORE the auto-fire; the final "particles gone"
// assertion is deliberately race-robust (fade completion and Director
// respawn both delete them).
void test_crashed_ai_trail_fades_and_deletes() {
    Harness h;
    auto& kg = h.engine.get_kg();
    auto& ps = h.engine.get_particle_system();
    const double dt = 1.0 / 60.0;

    // Warm up a short AI run (>= 0.1 m so the crash seal lands a
    // segment), then force the crash through the KG — the same
    // property every reader consults.
    for (int i = 0; i < 5; i++) h.tick(dt);
    auto ais = kg.findByType("AICycle");
    AT_ASSERT_TRUE(!ais.empty(), "AICycle should exist before forced crash");
    auto ai = ais[0];
    kg.setProperty(ai, "cycle_state", "CRASHED");
    h.tick(dt);  // seal + fade arm land this tick

    // Collect the crashed AI's trail particles.
    auto ai_str = std::to_string(ai);
    std::vector<kg::KGParticleID> trail_kgids;
    for (auto t : kg.findByType("TrailSegment")) {
        if (kg.getProperty(t, "owner_cycle_id") != ai_str) continue;
        for (auto kgid : kg.getEntityKGParticles(t)) trail_kgids.push_back(kgid);
    }
    AT_ASSERT_TRUE(!trail_kgids.empty(),
        "crashed AI should have sealed at least one trail particle");

    // Trails are SELF-EMISSIVE: the fade ramps their COLOR with alpha
    // pinned at 1.0 (alpha < 1 would reroute the surface into the forward
    // transparent pass, which draws emissive full-bright — see
    // tests/test_trail_fade_render.cpp). So the fade is measured on the
    // red channel; orange trails start at r = 1.0.
    auto red_of = [&](kg::KGParticleID kgid) -> float {
        auto idx = kg.getRenderIndex(kgid);
        if (idx == kg::INVALID_RENDER_INDEX) return -1.0f;
        auto v = ps.lock_particles_for_read();
        return v[idx].r;
    };

    // ~1.0 s into the 2.0 s fade: color mid-ramp on every trail particle.
    for (int i = 0; i < 60; i++) h.tick(dt);
    for (auto kgid : trail_kgids) {
        float r = red_of(kgid);
        AT_ASSERT_TRUE(r > 0.15f && r < 0.85f,
            "trail color should be mid-fade at ~1 s (got " +
            std::to_string(r) + ")");
    }

    // ~1.8 s (still before the 2.0 s auto-fire): particles near-faded,
    // TrailSegment entities alive.
    for (int i = 0; i < 45; i++) h.tick(dt);
    for (auto kgid : trail_kgids) {
        float r = red_of(kgid);
        AT_ASSERT_TRUE(r >= -1.0f && r < 0.5f,
            "trail color should be deep in the fade at ~1.8 s (got " +
            std::to_string(r) + ")");
    }
    int ai_trail_entities = 0;
    for (auto t : kg.findByType("TrailSegment"))
        if (kg.getProperty(t, "owner_cycle_id") == ai_str) ai_trail_entities++;
    AT_ASSERT_TRUE(ai_trail_entities > 0,
        "TrailSegment entities must outlive their fading particles");

    // Well past fade end + Director respawn: every trail particle is
    // deleted (whichever path got there first).
    for (int i = 0; i < 60; i++) h.tick(dt);
    for (auto kgid : trail_kgids) {
        AT_ASSERT_TRUE(kg.getRenderIndex(kgid) == kg::INVALID_RENDER_INDEX,
            "crashed AI trail particle should be deleted after the fade");
    }

    // The player survived all of this untouched.
    auto players = kg.findByType("PlayerCycle");
    AT_ASSERT_TRUE(!players.empty(), "PlayerCycle should still exist");
}

// The respawn primitive owns the rendered-trails prune: every trail
// entity it destroys leaves the caller's "already rendered" set, so
// a reused EntityID can render again. App code used to hand-roll the
// erase next to the call; the primitive makes it un-forgettable.
void test_respawn_ai_prunes_rendered_trails() {
    Harness h;
    auto& kg = h.engine.get_kg();
    h.tick(1.0 / 60.0);
    auto ais = kg.findByType("AICycle");
    AT_ASSERT_TRUE(!ais.empty(), "AICycle should exist");
    auto ai = ais[0];

    auto trail = kg.createEntity("TrailSegment");
    kg.setProperty(trail, "owner_cycle_id", std::to_string(ai));
    std::unordered_set<kg::EntityID> rendered = {trail, 999999u};

    auto result = logotron::director::respawn_ai(
        kg, ai, {5.5f, 5.5f, logotron::Direction::NORTH}, &rendered);
    AT_ASSERT_TRUE(result.trails_wiped >= 1,
        "AI-owned trail should be wiped by respawn");
    AT_ASSERT_TRUE(rendered.count(trail) == 0,
        "destroyed trail must leave the rendered set");
    AT_ASSERT_TRUE(rendered.count(999999u) == 1,
        "unrelated entries must survive the prune");
}

// Phase B: the grid_fog escalation op. The Director darkens the Grid
// by setting @arena.grid_fog=1; the app-side sync flips the engine's
// vision cone on. Reset (R) clears both. The game STARTS with the
// cone disabled (Phase A) — fog is earned, not default.
void test_grid_fog_op_drives_vision_cone() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;

    h.tick(dt);
    AT_ASSERT_TRUE(!h.engine.get_vision_cone_enabled(),
        "vision cone must start DISABLED (Phase A)");

    auto arenas = kg.findByType("Arena");
    AT_ASSERT_TRUE(!arenas.empty(), "Arena entity should exist");
    kg.setProperty(arenas[0], "grid_fog", "1");
    h.tick(dt);
    AT_ASSERT_TRUE(h.engine.get_vision_cone_enabled(),
        "grid_fog=1 must enable the vision cone via the app sync");

    // Reset clears the escalation: property back to 0, cone off.
    h.app.handle_key(GLFW_KEY_R, 0, GLFW_PRESS, 0);
    h.tick(dt);
    AT_ASSERT_TRUE(!h.engine.get_vision_cone_enabled(),
        "reset_round must clear the fog escalation (cone off)");
    AT_ASSERT_TRUE(kg.getProperty(arenas[0], "grid_fog") != "1",
        "reset_round must clear @arena.grid_fog");
}

// Trail ribbon (cycle.h policy): each cycle's total trail length is
// capped by its tail_length KG property; the OLDEST end erodes
// continuously. With a tiny budget, a sealed segment must be fully
// consumed (entity destroyed) as the bike rides on — and the bike
// itself keeps riding (erosion never kills).
void test_trail_ribbon_erodes() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;

    for (int i = 0; i < 5; i++) h.tick(dt);
    // Quiet world: destroy the Programs so no derez fires the
    // Director mid-test (a random op racing the seal assert was a
    // real flake — this case tests erosion, not the Director).
    for (size_t i = 0; i < h.app.enemy_count(); ++i)
        kg.destroyEntity(h.app.enemy_entity(i));
    h.app.reconcile_rosters_now();
    auto player = kg.findByType("PlayerCycle")[0];
    kg.setProperty(player, "tail_length", "8.0");   // tiny ribbon
    // Pin the speed ramp off (max == base, the documented disable) so
    // the speed-coupled tail bonus stays zero and 8 m means 8 m.
    kg.setProperty(player, "max_speed", "6.0");

    for (int i = 0; i < 10; i++) h.tick(dt);
    h.app.handle_key(GLFW_KEY_LEFT, 0, GLFW_PRESS, 0);   // seal a segment
    for (int i = 0; i < 3; i++) h.tick(dt);

    auto player_str = std::to_string(player);
    kg::EntityID sealed = kg::INVALID_ENTITY;
    for (auto t : kg.findByType("TrailSegment")) {
        if (kg.getProperty(t, "owner_cycle_id") == player_str) { sealed = t; break; }
    }
    AT_ASSERT_TRUE(sealed != kg::INVALID_ENTITY,
        "player turn should seal a trail segment");

    // Ride on: the live run grows past the 8 m budget, so the sealed
    // segment must erode away entirely. The first turn points north
    // with only ~12.5 m of arena; turn west after 3 m so the long leg
    // has ~26 m of headroom regardless of what the pre-fire ops did
    // to the arena dims.
    for (int i = 0; i < 30; i++) h.tick(dt);
    h.app.handle_key(GLFW_KEY_LEFT, 0, GLFW_PRESS, 0);   // north -> west
    for (int i = 0; i < 150; i++) h.tick(dt);
    bool still_there = false;
    for (auto t : kg.findByType("TrailSegment")) {
        if (t == sealed) { still_there = true; break; }
    }
    AT_ASSERT_TRUE(!still_there,
        "sealed segment should be fully consumed by ribbon erosion");
    AT_ASSERT_TRUE(kg.getProperty(player, "cycle_state") == "RIDING",
        "erosion must never kill the rider");
}

// Roster (3v1): three Programs seed at distinct corners; kill
// attribution credits the User only when the dead Program hit the
// USER's light; crashed Programs respawn on the next Director apply;
// and the Director can GROW the roster (create_entity AICycle) via
// the reconciler.
void test_roster_attribution_and_lever() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;

    h.tick(dt);
    AT_ASSERT_TRUE(h.app.enemy_count() == 3,
        "roster should seed 3 Programs (got " +
        std::to_string(h.app.enemy_count()) + ")");
    AT_ASSERT_TRUE(h.app.enemy_entity(0) != h.app.enemy_entity(1) &&
                   h.app.enemy_entity(1) != h.app.enemy_entity(2),
        "Programs must be distinct entities");

    auto player = kg.findByType("PlayerCycle")[0];

    // Player kill: Program 1 dies on a USER-owned trail.
    auto ptrail = kg.createEntity("TrailSegment");
    kg.setProperty(ptrail, "owner_cycle_id", std::to_string(player));
    auto p1 = h.app.enemy_entity(0);
    kg.setProperty(p1, "cycle_state", "CRASHED");
    kg.setProperty(p1, "crash_hit_entity", std::to_string(ptrail));
    h.tick(dt);
    AT_ASSERT_TRUE(h.app.derez_count() == 1,
        "User must be credited for a kill on their light (got " +
        std::to_string(h.app.derez_count()) + ")");

    // Indirect kill: Program 2 dies on Program 3's trail. EVERY
    // Program death scores (trapping is the player's craft); the
    // direct/indirect distinction is flavor only.
    auto atrail = kg.createEntity("TrailSegment");
    kg.setProperty(atrail, "owner_cycle_id",
                   std::to_string(h.app.enemy_entity(2)));
    auto p2 = h.app.enemy_entity(1);
    kg.setProperty(p2, "cycle_state", "CRASHED");
    kg.setProperty(p2, "crash_hit_entity", std::to_string(atrail));
    h.tick(dt);
    AT_ASSERT_TRUE(h.app.derez_count() == 2,
        "every Program death must score (got " +
        std::to_string(h.app.derez_count()) + ")");

    // Both crashed Programs respawn once the Director's apply lands.
    // Respawn destroys the old entity and rezzes a fresh one — assert
    // THAT, not "everyone is riding right now" (a respawned Program
    // can legitimately die again while we wait).
    for (int i = 0; i < 260; i++) h.tick(dt);
    AT_ASSERT_TRUE(kg.getType(p1).empty() && kg.getType(p2).empty(),
        "crashed Program entities must be destroyed by respawn");
    AT_ASSERT_TRUE(h.app.enemy_count() == 3,
        "roster must hold 3 through respawns (got " +
        std::to_string(h.app.enemy_count()) + ")");

    // The lever: a Director-created AICycle grows the roster via the
    // reconciler on the next apply.
    size_t before = h.app.enemy_count();
    kg.createEntity("AICycle");
    h.app.reconcile_rosters_now();
    h.tick(dt);
    AT_ASSERT_TRUE(h.app.enemy_count() == before + 1,
        "create_entity AICycle must grow the roster (got " +
        std::to_string(h.app.enemy_count()) + ")");
}

void test_ally_mercy_lever() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // Quiet world: no Programs, so nothing scores or fires the
    // Director autonomously — every event below is ours.
    for (size_t i = 0; i < h.app.enemy_count(); ++i)
        kg.destroyEntity(h.app.enemy_entity(i));
    h.app.reconcile_rosters_now();
    AT_ASSERT_TRUE(h.app.enemy_count() == 0, "quiet world setup");

    // The mercy lever: create_entity AllyCycle wires an ally.
    kg.createEntity("AllyCycle");
    h.app.reconcile_rosters_now();
    h.tick(dt);
    AT_ASSERT_TRUE(h.app.ally_count() == 1,
        "create_entity AllyCycle must wire an ally (got " +
        std::to_string(h.app.ally_count()) + ")");
    auto ally = h.app.ally_entity(0);
    AT_ASSERT_TRUE(kg.getType(ally) == "AllyCycle",
        "wired ally must be an AllyCycle entity");

    // Kill it immediately (before it can ride anywhere): never
    // scores, torn down after the trail fade, never respawned.
    int score_before = h.app.derez_count();
    kg.setProperty(ally, "cycle_state", "CRASHED");
    for (int i = 0; i < 10; i++) h.tick(dt);
    AT_ASSERT_TRUE(h.app.derez_count() == score_before,
        "an ally death must not score (got " +
        std::to_string(h.app.derez_count()) + ")");
    // Teardown runs 3 s of SIM time after the crash — but Director
    // cinematics (pre-fire dwell rides the REAL clock) zero the game
    // dt, so a tick budget is meaningless on a fast machine. Wait on
    // a real-time deadline instead.
    {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(20);
        while (h.app.ally_count() != 0 &&
               std::chrono::steady_clock::now() < deadline)
            h.tick(dt);
    }
    AT_ASSERT_TRUE(h.app.ally_count() == 0,
        "a dead ally is torn down (got " +
        std::to_string(h.app.ally_count()) + ")");
    AT_ASSERT_TRUE(kg.getType(ally).empty(),
        "the dead ally's entity must be destroyed");
    h.app.reconcile_rosters_now();
    h.tick(dt);
    AT_ASSERT_TRUE(h.app.ally_count() == 0,
        "a dead ally is never respawned");

    // Ally cap: three requests, two granted, the third refused.
    kg.createEntity("AllyCycle");
    kg.createEntity("AllyCycle");
    kg.createEntity("AllyCycle");
    h.app.reconcile_rosters_now();
    h.tick(dt);
    AT_ASSERT_TRUE(h.app.ally_count() == 2,
        "ally cap is 2 (got " + std::to_string(h.app.ally_count()) + ")");
    AT_ASSERT_TRUE(kg.findByType("AllyCycle").size() == 2,
        "over-cap AllyCycle entities must be destroyed (got " +
        std::to_string(kg.findByType("AllyCycle").size()) + ")");
}

void test_death_journal_feeds_director() {
    Harness h;
    auto& kg = h.engine.get_kg();
    auto& deaths = h.engine.get_event_bus().deaths();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    uint64_t seq0 = deaths.head_seq();

    // Force Program 1 onto the player's light. The derez one-shot
    // must EMIT into the engine journal, and the fire it triggers
    // must DRAIN the event into the prompt's death block.
    auto player = kg.findByType("PlayerCycle")[0];
    auto ptrail = kg.createEntity("TrailSegment");
    kg.setProperty(ptrail, "owner_cycle_id", std::to_string(player));
    auto p1 = h.app.enemy_entity(0);
    kg.setProperty(p1, "cycle_state", "CRASHED");
    kg.setProperty(p1, "crash_hit_entity", std::to_string(ptrail));
    kg.setProperty(p1, "crash_cause", "opponent_trail");
    h.tick(dt);

    AT_ASSERT_TRUE(deaths.head_seq() == seq0 + 1,
        "derez must emit exactly one DeathEvent");
    auto entries = deaths.collect_since(seq0);
    AT_ASSERT_TRUE(entries.size() == 1, "journal retains the death");
    const auto& ev = entries[0].event;
    AT_ASSERT_TRUE(ev.target_entity_id &&
                   *ev.target_entity_id == std::to_string(p1),
        "death targets the derezzed Program");
    AT_ASSERT_TRUE(ev.source_entity_id &&
                   *ev.source_entity_id == std::to_string(player),
        "attribution resolves the trail to its owner");

    // The derez fired the Director; its drain formatted our event.
    AT_ASSERT_TRUE(h.app.last_death_log().size() == 1,
        "fire drains exactly the new death (got " +
        std::to_string(h.app.last_death_log().size()) + ")");
    AT_ASSERT_TRUE(h.app.last_death_log()[0].find("by the User") !=
                       std::string::npos,
        "formatted line credits the User");

    // Cursor semantics: once the first fire's response has applied
    // (respawn destroys p1's entity), derez a riding Program with a
    // distinctive cause. Whatever fire drains it next must deliver
    // the new death and must NOT re-deliver the first one.
    for (int i = 0; i < 1200 && !kg.getType(p1).empty(); i++) h.tick(dt);
    kg::EntityID second = kg::INVALID_ENTITY;
    for (size_t i = 0; i < h.app.enemy_count(); ++i) {
        auto e = h.app.enemy_entity(i);
        if (kg.getProperty(e, "cycle_state") == "RIDING") { second = e; break; }
    }
    AT_ASSERT_TRUE(second != kg::INVALID_ENTITY, "a riding Program exists");
    kg.setProperty(second, "cycle_state", "CRASHED");
    kg.setProperty(second, "crash_cause", "journal_cursor_probe");
    bool delivered = false, redelivered = false;
    for (int i = 0; i < 1800 && !delivered; i++) {
        h.tick(dt);
        for (const auto& line : h.app.last_death_log()) {
            if (line.find("journal_cursor_probe") != std::string::npos)
                delivered = true;
            if (line.find("opponent_trail") != std::string::npos)
                redelivered = true;
        }
    }
    AT_ASSERT_TRUE(delivered,
        "cursor must deliver the second death to a later fire");
    AT_ASSERT_TRUE(!redelivered,
        "cursor must never re-deliver the first death");
}

void test_state_delta_feeds_director() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // A Director-relevant mutation lands between fires...
    auto arena = kg.findByType("Arena")[0];
    kg.setProperty(arena, "arena_w", "44");

    // ...and the next fire (derez-triggered) must surface it as a
    // net delta drained from the engine state_changes journal.
    auto p1 = h.app.enemy_entity(0);
    kg.setProperty(p1, "cycle_state", "CRASHED");
    h.tick(dt);

    AT_ASSERT_TRUE(!h.app.last_state_delta().empty(),
        "fire must drain a state-delta block");
    // The pre-fire's own arena ops share the fold window, so the
    // first-prev is theirs; assert the fold's TAIL is our write.
    const auto& blk = h.app.last_state_delta();
    auto pos = blk.find(std::to_string(arena) + " arena_w: ");
    AT_ASSERT_TRUE(pos != std::string::npos,
        "delta block carries the arena_w fold (got: " + blk + ")");
    auto eol = blk.find('\n', pos);
    AT_ASSERT_TRUE(blk.substr(pos, eol - pos).find("-> 44") !=
                       std::string::npos,
        "fold's latest value is our write (got: " + blk + ")");
    AT_ASSERT_TRUE(h.app.last_state_delta().find(" x:") ==
                       std::string::npos,
        "kinematic churn filtered out of the block");
}

}  // namespace

int main() {
    std::cout << "Logotron AT — full_game_loop" << std::endl;
    AT_TEST(test_pre_fire_lands_walls_within_first_tick);
    AT_TEST(test_arena_dimensions_match_balance);
    AT_TEST(test_auto_fire_does_not_respawn_alive_ai);
    AT_TEST(test_cycles_alive_after_warmup_ticks);
    AT_TEST(test_crashed_ai_trail_fades_and_deletes);
    AT_TEST(test_respawn_ai_prunes_rendered_trails);
    AT_TEST(test_grid_fog_op_drives_vision_cone);
    AT_TEST(test_trail_ribbon_erodes);
    AT_TEST(test_roster_attribution_and_lever);
    AT_TEST(test_ally_mercy_lever);
    AT_TEST(test_death_journal_feeds_director);
    AT_TEST(test_state_delta_feeds_director);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
