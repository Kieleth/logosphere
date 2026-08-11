// Logotron application — extracted from main.cpp so headless ATs can
// instantiate the same class the live game runs. main.cpp now contains
// only the entry point + main loop; everything game-side lives here.
//
// The class is header-only (methods inline) for now. If compile time
// becomes a problem we split into .cpp; today the only TUs that include
// this are main.cpp and the AT executables.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <GLFW/glfw3.h>

#include "application.h"
#include "core/engine.h"
#include "logosphere/events/journal_render.h"
#include "logosphere/kg/kg_query.h"
#include "core/camera_system.h"
#include "core/particle_system.h"
#include "logosphere/core/kg_parse.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/assembly/rigid_assembly.h"
#include "logotron_ontology_registry.h"
#include "particle.h"
#include "walls.h"
#include "arena.h"
#include "cycle.h"
#include "round.h"
#include "ai/ai_instrument.h"
#include "ai/decide.h"
#include "ai/head.h"
#include "ai/perception.h"
#include "ai/personality.h"
#include "director/cinematic.h"
#include "director/director.h"
#include "director/director_parser.h"
#include "director/narrative_hint.h"
#include "director/playback_registrations.h"
#include "director/random_director.h"
#include "director/respawn_ai.h"
#include "director/symbolic_refs.h"
#include "logosphere/kg/kg_ops_apply.h"
#include "logosphere/kg/ontology_serialize.h"
#include "logosphere/kg/ontology_validator.h"
#include "llm_plan.h"
#include "logosphere/llm/llm_system_http.h"
#include "logosphere/telemetry/session.h"
#include "hud/speed_dashboard.h"
#include "hud/sight_occluders.h"
#include "ui/ui_system.h"
#include "ui/text_window.h"

namespace {

// The arena spans (0, 0) to (kArenaW, kArenaH) in world coords.
// Centered on the world origin for symmetric camera framing.
//
// Sized for survivable human play: 50 m × 50 m gives the player ~12 s
// of straight-line travel at max bike speed before they have to turn,
// vs ~8 s at the original 40 × 40. The Director's auto-fire cadence
// (8 s real time) gets at least one mutation per round, and the
// pre-fire random shaping always ships >0 walls before the first
// input. Bounds match the schema's arena_w / arena_h range [16, 60].
constexpr float kArenaW = 50.0f;
constexpr float kArenaH = 50.0f;
constexpr float kPillarHeight  = 1.0f;   // pillar tallness (z thickness)
constexpr float kPillarLong  = 0.50f; // extent along the cycle's travel direction
constexpr float kPillarThin  = 0.10f; // extent perpendicular — sleek, almost blade-like
constexpr float kPillarTall  = 0.40f; // wall height

// Bike body vertical center — matches spawn_motorcycle's default
// (wheel_radius 0.26 + body_height 0.48 / 2 = 0.50). The camera aims
// here so the rider stays framed when zooming.
constexpr float kBikeLookZ   = 0.50f;

float arena_to_world_x(float ax) { return ax - kArenaW * 0.5f; }
float arena_to_world_y(float ay) { return ay - kArenaH * 0.5f; }

// Set the §18 speed-model knobs on a freshly spawned cycle. Stand-in
// for what the Weirden Director will eventually own — for now we
// configure both player and AI symmetrically. base_speed stays at
// the spawn default (kCycleSpeed = 5 m/s); the cycle ramps to `max`
// at `ramp` m/s² along straight runs and snaps back to base on every
// turn. Set max == base or ramp == 0 to disable the ramp.
void configure_speed_model(kg::KGModule& kg, kg::EntityID cycle,
                           float max_speed, float ramp_rate) {
    if (cycle == kg::INVALID_ENTITY) return;
    kg.setProperty(cycle, "max_speed",       std::to_string(max_speed));
    kg.setProperty(cycle, "speed_ramp_rate", std::to_string(ramp_rate));
}

} // namespace

class LogotronApplication : public Logosphere::IApplication {
public:
    // One enemy Program: everything that used to be the ai_* scalar
    // singletons, per rider. The roster is data — the Director can
    // grow or shrink it (create_entity AICycle / destroy_entity)
    // through the reconciler; kDefaultEnemyCount (cycle.h) seeds it.
    struct Rider {
        kg::EntityID entity      = kg::INVALID_ENTITY;
        kg::EntityID active_run  = kg::INVALID_ENTITY;
        logotron::ai::HeadState head{};
        logotron::ai::Personality personality =
            logotron::ai::default_personality();
        int   frame_count = 0;
        float crashed_at = -1.0f;       // elapsed_ stamp; -1 = riding
        bool  announced_crashed = false;
        bool  fade_armed = false;
        bool  crash_noted = false;
        int   hue = 0;                  // kEnemyPalette index
        logosphere::assembly::RigidAssembly motorcycle;
    };
    std::vector<Rider> enemies_;
    // Programs riding FOR the User (AllyCycle) — Director-granted,
    // never respawned. hue -1 renders the ally teal. Cap 2.
    std::vector<Rider> allies_;

    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();

        kg.extendOntology(logotron::ontology::registry());
        std::cout << "[logotron] KG ontology extended with logotron types." << std::endl;

        // Open a telemetry session and register Logotron's AI
        // instrument. The Session's ctor creates
        // ~/.logosphere/sessions/<build_sha>/<launch_utc>_N/ and
        // writes session.json with the build metadata. Every round's
        // AI decisions get appended under that folder. See
        // include/logosphere/telemetry/session.h for the contract.
        telemetry_session_ = std::make_unique<logosphere::telemetry::Session>();
        ai_instrument_ = static_cast<logotron::ai::AIInstrument*>(
            telemetry_session_->register_instrument(
                std::make_unique<logotron::ai::AIInstrument>()));
        // First round starts immediately after spawn below; tag it.
        ai_instrument_->begin_round(
            logotron::ai::default_personality().name);

        arena_entity_ = kg.createEntity("Arena");
        kg.setProperty(arena_entity_, "arena_w", std::to_string(kArenaW));
        kg.setProperty(arena_entity_, "arena_h", std::to_string(kArenaH));
        std::cout << "[logotron] Arena entity created: id=" << arena_entity_
                  << " size=" << kArenaW << "x" << kArenaH << " m" << std::endl;

        // Declarative crashed-trail fade: one engine TransformationRule,
        // armed at the AI-crash seal (replaces the old per-frame
        // sync_crashed_trail_fade walk).
        trail_fade_rule_ = kg.createEntity("TransformationRule");
        kg.setProperty(trail_fade_rule_, "trigger", "on_timer");
        kg.setProperty(trail_fade_rule_, "effect", "fade_out");
        kg.setProperty(trail_fade_rule_, "duration_s",
                       std::to_string(kTrailFadeDuration));
        kg.setProperty(trail_fade_rule_, "name", "crashed_trail_fade");
        engine_->get_interaction_system().load_rules_from_kg(kg);

        // Subscribe to the engine death journal: one cursor for the
        // Director, drained at each fire (see emit_death_event).
        death_reader_ = engine_->get_event_bus().deaths().create_reader();
        state_reader_ =
            engine_->get_event_bus().state_changes().create_reader();

        // Spawn player and AI in arena-local coords (centered to
        // world coords inside the visualization layer). They start
        // on perpendicular trajectories.
        player_entity_ = logotron::spawn_cycle(
            kg, "PlayerCycle",
            kArenaW * 0.5f, kArenaH * 0.5f,
            logotron::Direction::EAST);
        configure_speed_model(kg, player_entity_, logotron::kCycleMaxSpeed,
                              logotron::kCycleRampRate);
        player_motorcycle_ = make_motorcycle(spawn_motorcycle(kg), true);
        seed_enemies(kg);
        std::cout << "[logotron] You are CYAN (entity=" << player_entity_
                  << ") at center facing EAST." << std::endl;
        std::cout << "[logotron] " << enemies_.size()
                  << " enemy Program(s) on the Grid." << std::endl;
        std::cout << "[logotron] Controls: LEFT/RIGHT or A/D to turn, "
                     "R = restart, ESC = quit."
                  << std::endl;

        // Floor first so the arena is visible from frame zero.
        floor_entity_ = spawn_floor(kg);

        // Tron boundary: the blue glow comes from the arena EDGES,
        // not from overhead. Four corner lights at ground level +
        // thin neon strips along the four perimeter walls.
        spawn_arena_boundary(kg);

        // Camera: iso-ish view, closer to the arena (zoom step is
        // user-controlled via mouse scroll).
        // Zoom: bump pixels-per-unit so the bike reads clearly.
        // 24 px/unit fits the 50 m arena in ~1200 px of viewport so
        // the player can see the AI's trail growing across the Grid
        // and react with hands instead of luck. Zooms back IN with
        // mouse scroll for tactical close-ups.
        engine_->get_camera_system().set_pixels_per_unit(24.0f);
        apply_camera();

        auto plan = logotron::plan_llm_from_env();
        std::cout << "[logotron] LLM plan: " << logotron::llm_mode_name(plan.mode);
        if (plan.mode != logotron::LLMPlan::Mode::None) {
            std::cout << " model=" << plan.model;
        }
        std::cout << std::endl;
        llm_configured_ = (plan.mode != logotron::LLMPlan::Mode::None);

        // Weirden Director responder. Three paths:
        //   - LLM configured: wrap LLMSystemHTTP::submit_request.
        //   - No key:         deterministic random fallback so the
        //                     game still feels alive offline.
        // Either way, Director::fire/poll is the only API the rest of
        // the loop uses.
        if (llm_configured_) {
            llm_ = std::make_unique<Logosphere::LLMSystemHTTP>();
            bool init_ok = false;
            switch (plan.mode) {
                case logotron::LLMPlan::Mode::OpenAI:
                    init_ok = llm_->initialize_openai(plan.api_key, plan.model);
                    break;
                case logotron::LLMPlan::Mode::Anthropic:
                    init_ok = llm_->initialize_anthropic(plan.api_key, plan.model);
                    break;
                case logotron::LLMPlan::Mode::MLX:
                    init_ok = llm_->initialize_mlx(plan.url, plan.model);
                    break;
                case logotron::LLMPlan::Mode::None:
                    break;
            }
            if (!init_ok) {
                std::cerr << "[logotron] LLM init failed: "
                          << llm_->get_last_error()
                          << " — falling back to random Director" << std::endl;
                llm_.reset();
                llm_configured_ = false;
            }
        }
        if (llm_configured_) {
            Logosphere::LLMSystemHTTP* llm_ptr = llm_.get();
            director_.set_responder(
                [llm_ptr](const std::string& system_prompt,
                           const std::string& user_prompt,
                           std::function<void(std::string)> done) {
                    // Push the (large, static) system prompt through
                    // the LLMSystemHTTP narrative path. That path uses
                    // build_anthropic_request_with_system, which puts
                    // the system block at root level — the only shape
                    // Anthropic prompt caching can attach to. Setting
                    // it every call is cheap (just stashes the string
                    // on the LLM system); the cache itself is keyed on
                    // the system content, so unchanged content hits
                    // the server-side cache regardless of how many
                    // times we re-set it locally.
                    llm_ptr->set_narrative_system_prompt(system_prompt);
                    llm_ptr->submit_request(
                        user_prompt, /*max_tokens=*/600,
                        [done](const std::string& /*p*/,
                               const std::string& response,
                               void* /*user_data*/) {
                            done(response);
                        });
                });
            director_brain_desc_ = std::string(logotron::llm_mode_name(plan.mode)) +
                                   " " + plan.model;
            std::cout << "[logotron] Weirden Director armed (LLM responder)"
                      << std::endl;
        } else {
            director_.set_responder(
                logotron::director::make_random_responder(
                    /*seed=*/0, [this]() {
                        logotron::director::RandomDirectorContext ctx;
                        ctx.arena_w = live_arena_w();
                        ctx.arena_h = live_arena_h();
                        ctx.escalation_tier = escalation_tier();
                        return ctx;
                    }));
            director_brain_desc_.clear();  // empty = random fallback
            std::cout << "[logotron] Weirden Director armed (random fallback)"
                      << std::endl;
        }

        // Phase D — register Logotron's per-KGOp visual rez-in
        // plays with the engine's MutationPlaybackRegistry.
        // Idempotent; safe to call once per app init.
        logotron::director::register_playback_handlers(*engine_);

        // Player head state — mirrors the AI's rider model. Mouse X
        // drives `target_yaw` relative to the bike's heading (clamped
        // ±kMaxHeadOffsetRad so the player can't look fully backwards).
        // Spawn yaw = EAST, matching the cycle spawn direction.
        player_head_.current_yaw = 1.5707963f;  // EAST in engine convention
        player_head_.target_yaw  = 1.5707963f;
        player_head_.max_rate    = kPlayerHeadMaxRate;

        // Vision cone — player-driven fog-of-war. The cone origin
        // tracks the player's bike; its orientation tracks the rider's
        // HEAD yaw (mouse-steered). Range + FOV + darkness tuned to
        // read as "rider's field of view" without blacking out the
        // playfield. See §11 Visual style + §16 AI philosophy.
        //
        // Starts DISABLED (playtest 2026-07-18: opening in fog is
        // disorienting, and the cone's Pass-4 foveal blur + memory
        // ghost smeared the trails). The fog is now a Director
        // ESCALATION: at high tiers the Master Control darkens the
        // Grid (Phase B wires the grid_fog op). Style/focus/memory
        // params below stay configured so enabling is one switch.
        engine_->set_vision_cone_enabled(false);
        // Hard cutoff: darkness=0 means anything outside the cone or
        // behind a wall goes pitch black. Inner sharp until 70% of
        // half-FOV; the remaining 30% is a soft falloff so the cone
        // edge isn't a knife-cut line.
        engine_->set_vision_cone_style(0.70f, 0.0f);
        engine_->set_vision_cone_focus(0.0f, 0.0f, 4.0f);

        // Vision memory — pixels the cone has touched stay dimly
        // visible after the head moves off them, fading over a few
        // seconds. World-space buffer covering the whole arena
        // (centered on world origin per `arena_to_world_x/y`). 256x256
        // = 64 KB = ~0.16 m / cell, fine for a soft fade.
        const float half_w = kArenaW * 0.5f;
        const float half_h = kArenaH * 0.5f;
        engine_->set_vision_memory_extent(-half_w, -half_h, half_w, half_h, 256);
        // Memory holds full strength for the first ~10 s after the
        // cone touched a cell, then linearly decays to black.
        // Configurable per-game; the engine has no opinion on the
        // sweet spot.
        engine_->set_vision_memory_decay(/*decay_seconds=*/10.0f, /*memory_dim=*/0.45f);
        engine_->set_vision_memory_enabled(true);

        // Tron-style speed dashboard, bottom-right corner. Owned by
        // the application (raw pointer is given to UISystem which
        // does NOT take ownership; see ui_system.cpp:2061). Position
        // reads the live framebuffer dims so it stays anchored if
        // the window resizes.
        speed_dash_ = std::make_unique<logotron::hud::SpeedDashboard>();
        const int rw = engine_->get_resolution_manager().get_render_width();
        const int rh = engine_->get_resolution_manager().get_render_height();
        const int dash_w = 280, dash_h = 110, margin = 24;
        speed_dash_->set_bounds(rw - dash_w - margin, rh - dash_h - margin,
                                dash_w, dash_h);
        engine_->get_ui_system()->add_widget(speed_dash_.get());

        // Master Control Program log: top-center text window where the
        // Director's narration lands. Magenta accent so it reads as
        // "the third party" alongside the cyan (player) and orange
        // (Program) palette. Voice only; mechanics go in the ledger.
        weirden_log_ = std::make_unique<TextWindow>("MASTER CONTROL", "weirden_log");
        const int log_w = 520, log_h = 100;
        weirden_log_->set_bounds((rw - log_w) / 2, 20, log_w, log_h);
        weirden_log_->set_max_lines(4);
        weirden_log_->set_background_alpha(190);
        weirden_log_->add_line("The Grid awaits its next duel.", 220, 130, 240);
        engine_->get_ui_system()->add_widget(weirden_log_.get());

        // Director ledger: top-left debug window listing every
        // mutation the Director applies, with the actual coordinates
        // / before-after values so the player can verify the world
        // really changed. Pale magenta on a darker background.
        director_ledger_ = std::make_unique<TextWindow>(
            "DIRECTOR LEDGER", "director_ledger");
        const int ledger_w = 420, ledger_h = 220;
        director_ledger_->set_bounds(20, 20, ledger_w, ledger_h);
        director_ledger_->set_max_lines(16);
        director_ledger_->set_background_alpha(180);
        director_ledger_->add_line("(no mutations yet)", 160, 160, 180);
        engine_->get_ui_system()->add_widget(director_ledger_.get());

        // Run score: derezzes this run + best streak this session.
        // Small, top-right, always current (update_score_hud rewrites
        // it on every change). This is the progression readout the
        // 2026-07-18 playtest found missing.
        score_hud_ = std::make_unique<TextWindow>("RUN", "score_hud");
        const int score_w = 210, score_h = 64;
        score_hud_->set_bounds(rw - score_w - 20, 20, score_w, score_h);
        score_hud_->set_max_lines(2);
        score_hud_->set_background_alpha(180);
        engine_->get_ui_system()->add_widget(score_hud_.get());
        update_score_hud();

        // Startup splash: which Director brain is live (real LLM with
        // provider+model, or the offline random fallback), plus the
        // controls. Dismissed by any key or after kSplashSecs.
        splash_ = std::make_unique<TextWindow>("ENTER THE GRID", "splash");
        const int sp_w = 470, sp_h = 96;
        splash_->set_bounds(rw - sp_w - 24, rh - sp_h - 24, sp_w, sp_h);
        splash_->set_max_lines(4);
        splash_->set_background_alpha(200);
        if (!director_brain_desc_.empty()) {
            splash_->add_line("MASTER CONTROL ONLINE: " + director_brain_desc_,
                              120, 255, 160);
        } else {
            splash_->add_line("MASTER CONTROL: random fallback (no API key)",
                              255, 170, 60);
        }
        splash_->add_line("Derez the Program. Every kill escalates.", 160, 220, 255);
        splash_->add_line("Steer: arrows / A D   Reset: R   Quit: ESC",
                          160, 220, 255);
        engine_->get_ui_system()->add_widget(splash_.get());

        // Interactive runs open on the splash stage with the game
        // frozen; headless (ATs, --headless) skips straight to play.
        // Headless is not a mode: it is display_ == nullptr.
        if (engine_->get_display() != nullptr) {
            splash_active_ = true;
            spawn_splash_logo(kg);
            // The splash owns the screen: game HUD comes back on SPACE.
            if (weirden_log_)     weirden_log_->set_visible(false);
            if (director_ledger_) director_ledger_->set_visible(false);
            if (score_hud_)       score_hud_->set_visible(false);
            if (speed_dash_)      speed_dash_->set_visible(false);
        } else {
            splash_->set_visible(false);
        }

        // Pre-fire the Director RIGHT NOW, before the player can move.
        // This way the LLM round trip overlaps with the player's first
        // few seconds of orientation (figuring out controls, looking
        // around) instead of stealing the front of the round. By the
        // time they've taken a few turns, the cinematic lands and the
        // opening world is already authored. is_setup=true keeps the
        // round / death counters at zero so the next real fire is
        // still labelled "duel 1".
        std::cerr << "[logotron] pre-firing Director (setup)" << std::endl;
        fire_director(engine_->get_kg(), /*is_setup=*/true);
    }

    void update_game(float dt) override {
        if (!engine_ || player_entity_ == kg::INVALID_ENTITY) return;
        auto& kg = engine_->get_kg();

        // Splash state: the Grid waits. Simulation frozen (elapsed_
        // does not advance, cycles do not move, the Director does not
        // fire), camera parked on the LOGOTRON wordmark stage far from
        // the arena. Any key tears it down and starts the ride.
        if (splash_active_) {
            splash_clock_ += dt;
            animate_splash_logo();
            return;
        }
        elapsed_ += dt;

        // Player mouse-look — aim the rider's head at the WORLD point
        // under the cursor. Implementation notes:
        //   1. We invert-project the cursor through the iso camera to
        //      get a world-space target on the bike's horizontal plane
        //      (z = kBikeLookZ). This is what the engine's
        //      render_system.screen_to_world_at_z is for; reusing it
        //      means we inherit any future camera/zoom changes for
        //      free.
        //   2. Desired world yaw = atan2(dx, dy) in ENGINE convention
        //      (yaw=0 → +Y). This matches the cycle's facing-angle
        //      rule pinned in docs/ARCHITECTURE.md; atan2(y, x) would silently
        //      break the cone orientation.
        //   3. We clamp the OFFSET from the bike's heading to
        //      ±kMaxHeadOffsetRad (not the absolute world yaw), so
        //      the forbidden 90° arc directly behind the rider stays
        //      truly unlookable even as the bike turns.
        //   4. step_head rate-limits the catch-up so fast cursor
        //      jerks still take real time to execute, matching the
        //      AI's embodied-head contract.
        {
            const auto& in_st = engine_->get_input_system().get_input_state();
            auto pc_for_head = logotron::read_cycle(kg, player_entity_);
            float bike_world_x = arena_to_world_x(pc_for_head.x);
            float bike_world_y = arena_to_world_y(pc_for_head.y);

            float target_wx = bike_world_x;
            float target_wy = bike_world_y;
            engine_->get_coord_transformer().screen_to_world_at_z(
                static_cast<int>(in_st.mouse_x),
                static_cast<int>(in_st.mouse_y),
                kBikeLookZ,
                target_wx, target_wy);

            float bike_yaw = 0.0f;
            switch (pc_for_head.direction) {
                case logotron::Direction::NORTH: bike_yaw = 0.0f;        break;
                case logotron::Direction::EAST:  bike_yaw = 1.5707963f;  break;
                case logotron::Direction::SOUTH: bike_yaw = 3.14159265f; break;
                case logotron::Direction::WEST:  bike_yaw = -1.5707963f; break;
            }

            float dx = target_wx - bike_world_x;
            float dy = target_wy - bike_world_y;
            float desired_yaw = bike_yaw;
            if (dx * dx + dy * dy > 1e-6f) {
                desired_yaw = std::atan2(dx, dy);  // engine: yaw=0 is +Y
            }

            float offset = logotron::ai::shortest_arc_delta(bike_yaw, desired_yaw);
            if (offset >  kMaxHeadOffsetRad) offset =  kMaxHeadOffsetRad;
            if (offset < -kMaxHeadOffsetRad) offset = -kMaxHeadOffsetRad;

            player_head_.target_yaw = bike_yaw + offset;
            logotron::ai::step_head(player_head_, dt);

            engine_->set_vision_cone(bike_world_x, bike_world_y,
                                     player_head_.current_yaw,
                                     kPlayerVisionFovRad,
                                     kPlayerVisionRange);
            engine_->set_vision_cone_focus(bike_world_x, bike_world_y, 4.0f);

            // LOS occlusion mask. Raycast 64 bins across the cone
            // against trails + opponent active runs (NOT our own
            // active run — see sight_occluders.cpp). Player's bike
            // position uses ARENA coords; the engine cone is in
            // WORLD coords; the helper raycasts in arena coords and
            // we just push the same numbers to the engine because
            // distances are translation-invariant — only the magnitude
            // matters per bin.
            float occlusion[logotron::hud::kVisionConeBins];
            logotron::hud::compute_vision_cone_occlusion(
                kg, player_entity_,
                pc_for_head.x, pc_for_head.y,
                player_head_.current_yaw,
                kPlayerVisionFovRad * 0.5f,
                kPlayerVisionRange,
                elapsed_,                       // game clock — drives the
                                                 // CRASHED-owner + age filters
                occlusion);
            engine_->set_vision_cone_occlusion(
                occlusion, logotron::hud::kVisionConeBins);

            // Vision memory: decay every cell + re-mark whatever the
            // cone is currently illuminating. Reads back the cone +
            // occlusion params we just pushed, so it stays in sync
            // with what the shader is about to render this frame.
            engine_->update_vision_memory(dt);
        }

        if (!round_over_) {
            // AI decision tick — every kAiDecidePeriodFrames frames
            // (~6 decisions/sec at 60 FPS). Builds a PerceivedWorld
            // through the perception layer (no direct KG reads in
            // tactic code per GAME_DESIGN.md §16), runs the blended
            // personality, applies drive + look outputs.
            for (size_t ri = 0; ri < enemies_.size(); ++ri) {
                Rider& rd = enemies_[ri];
                rd.frame_count++;
                auto ai_cyc = logotron::read_cycle(kg, rd.entity);
                if (ai_cyc.state != logotron::CycleState::RIDING) {
                    logotron::ai::step_head(rd.head, dt);
                    continue;
                }
                // Cadence staggered by rider index so N perception
                // builds never land on the same frame.
                if ((rd.frame_count + static_cast<int>(ri)) %
                        kAiDecidePeriodFrames == 0) {
                    auto pw = logotron::ai::build_perceived_world(
                        kg, rd.entity, rd.head,
                        kAiConeFovRad, kAiConeRange,
                        kArenaW, kArenaH);
                    auto d = logotron::ai::decide(rd.personality, pw);
                    // Telemetry stays single-stream: rider 0 only
                    // (expanding AIInstrument is out of scope).
                    if (ri == 0 && ai_instrument_) {
                        ai_instrument_->record_decision(
                            elapsed_, pw, d.drive, d.head_target_yaw);
                    }
                    rd.head.target_yaw = d.head_target_yaw;
                    if (d.drive != ai_cyc.direction) {
                        // Seal + re-read + write, matching the
                        // player's arrow-key path exactly. freeze_run
                        // mutates run_start in the KG so we MUST
                        // re-read after it or the stale pre-freeze
                        // value clobbers the new one (that was the
                        // "weird tail" visual bug). spawn_time stamps
                        // the new segment for the lifetime system.
                        // turn() (not raw direction =) snaps current
                        // speed back to base and restarts the ramp —
                        // the speed model's contract per §18.
                        logotron::freeze_run_at(kg, rd.entity, elapsed_);
                        auto ai_after = logotron::read_cycle(kg, rd.entity);
                        logotron::turn(ai_after, d.drive);
                        logotron::write_cycle(kg, rd.entity, ai_after);
                    }
                }
                // Advance head rotation every frame regardless of
                // decision cadence — the swivel is visible motion.
                logotron::ai::step_head(rd.head, dt);
            }
            // Allies drive with the same machinery but hunt ONLY
            // enemy Programs — never the User they ride for.
            static const std::vector<const char*> kAllyRivals = {"AICycle"};
            for (size_t ri = 0; ri < allies_.size(); ++ri) {
                Rider& rd = allies_[ri];
                rd.frame_count++;
                auto cyc = logotron::read_cycle(kg, rd.entity);
                if (cyc.state != logotron::CycleState::RIDING) {
                    logotron::ai::step_head(rd.head, dt);
                    continue;
                }
                if ((rd.frame_count + static_cast<int>(ri) + 3) %
                        kAiDecidePeriodFrames == 0) {
                    auto pw = logotron::ai::build_perceived_world(
                        kg, rd.entity, rd.head,
                        kAiConeFovRad, kAiConeRange,
                        kArenaW, kArenaH, kAllyRivals);
                    auto d = logotron::ai::decide(rd.personality, pw);
                    rd.head.target_yaw = d.head_target_yaw;
                    if (d.drive != cyc.direction) {
                        logotron::freeze_run_at(kg, rd.entity, elapsed_);
                        auto after = logotron::read_cycle(kg, rd.entity);
                        logotron::turn(after, d.drive);
                        logotron::write_cycle(kg, rd.entity, after);
                    }
                }
                logotron::ai::step_head(rd.head, dt);
            }

            // Continuous integration: feed the engine's wall-clock dt
            // straight to the cycle stepper. No fixed-period
            // accumulator — Logosphere is a continuous engine. The
            // `_at` variant passes the game clock so expired trails
            // (age > kTrailLifetime) are treated as non-lethal.
            logotron::step_cycle_in_kg_with_collision_at(
                kg, player_entity_, kArenaW, kArenaH, dt, elapsed_);
            for (auto& rd : enemies_) {
                logotron::step_cycle_in_kg_with_collision_at(
                    kg, rd.entity, kArenaW, kArenaH, dt, elapsed_);
            }
            for (auto& rd : allies_) {
                logotron::step_cycle_in_kg_with_collision_at(
                    kg, rd.entity, kArenaW, kArenaH, dt, elapsed_);
            }

            // Crash-observer MUST run before end_round so crash
            // records land in the meta. One-shot per cycle per round.
            {
                auto note_if_crashed =
                    [&](kg::EntityID cycle_id, const std::string& label,
                        bool& already_noted) {
                    if (already_noted) return;
                    auto c = logotron::read_cycle(kg, cycle_id);
                    if (c.state != logotron::CycleState::CRASHED) return;
                    logotron::ai::AIInstrument::CrashRecord cr;
                    cr.label = label;
                    cr.cause = kg.getProperty(cycle_id, "crash_cause");
                    cr.x = kg_parse::to_float(
                        kg.getProperty(cycle_id, "crash_x"), "crash_x", cycle_id)
                        .value_or(c.x);
                    cr.y = kg_parse::to_float(
                        kg.getProperty(cycle_id, "crash_y"), "crash_y", cycle_id)
                        .value_or(c.y);
                    cr.at_t = elapsed_;
                    cr.hit_age = kg_parse::to_float(
                        kg.getProperty(cycle_id, "crash_hit_age"),
                        "crash_hit_age", cycle_id).value_or(0.0f);
                    auto hit_s = kg.getProperty(cycle_id, "crash_hit_entity");
                    if (!hit_s.empty()) {
                        try { cr.hit_entity = std::stoll(hit_s); } catch (...) {}
                    }
                    if (ai_instrument_) ai_instrument_->note_crash(cr);
                    std::cerr << "[logotron] " << label << " crashed — "
                              << (cr.cause.empty() ? "?" : cr.cause)
                              << " @ (" << cr.x << ", " << cr.y << ")";
                    if (cr.hit_age > 0.0f)
                        std::cerr << " age=" << cr.hit_age << "s";
                    std::cerr << std::endl;
                    already_noted = true;
                };
                for (size_t ri = 0; ri < enemies_.size(); ++ri) {
                    note_if_crashed(enemies_[ri].entity,
                                    "program_" + std::to_string(ri + 1),
                                    enemies_[ri].crash_noted);
                }
                note_if_crashed(player_entity_, "player", player_crash_noted_);
            }

            // Director real-time clock ticks every frame regardless
            // of round state. The cinematic dwell timer needs it to
            // keep advancing even after a round ends, otherwise the
            // cinematic gets stuck mid-show on a player-crash frame.
            director_real_clock_ += engine_->get_time_system().get_real_delta_time();

            // Per-rider derez events. Each enemy flipping to CRASHED
            // fires exactly once: kill attribution decides whether the
            // USER scored (hit the player's trail or cycle) or a
            // Program took out another Program (respawn, no credit).
            bool any_new_derez = false;
            for (size_t ri = 0; ri < enemies_.size(); ++ri) {
                Rider& rd = enemies_[ri];
                if (rd.announced_crashed) continue;
                auto rc = logotron::read_cycle(kg, rd.entity);
                if (rc.state != logotron::CycleState::CRASHED) continue;
                rd.announced_crashed = true;
                any_new_derez = true;
                // EVERY Program death scores (playtest 2026-07-24:
                // trapping an enemy until it eats its own wall IS the
                // player's kill — the classic Tron skill). The direct
                // vs indirect distinction survives only as flavor and
                // in the Director's death log.
                bool direct_kill = crash_credits_player(kg, rd.entity);
                emit_death_event(kg, rd.entity,
                                 "program_" + std::to_string(ri + 1),
                                 rd.personality.name,
                                 direct_kill ? "user" : "grid");
                ++derez_count_;
                if (derez_count_ > best_derez_streak_)
                    best_derez_streak_ = derez_count_;
                if (weirden_log_) {
                    weirden_log_->add_line(
                        direct_kill
                            ? "PROGRAM DEREZZED on your light. Kill " +
                                  std::to_string(derez_count_) + " this run."
                            : "PROGRAM TRAPPED and derezzed. Kill " +
                                  std::to_string(derez_count_) + " this run.",
                        120, 255, 160);
                    int t = escalation_tier();
                    if (t == kTierPersonalityAt)
                        weirden_log_->add_line(
                            "ESCALATION II: the Programs learn.", 255, 170, 60);
                    else if (t == kTierHardShrinkAt)
                        weirden_log_->add_line(
                            "ESCALATION III: the lattice tightens.", 255, 170, 60);
                    else if (t == kTierFogAt)
                        weirden_log_->add_line(
                            "ESCALATION IV: the light is mine to give.", 255, 170, 60);
                }
                update_score_hud();
            }
            if (any_new_derez) {
                std::cerr << "[logotron] Program derezzed, Weirden firing"
                          << std::endl;
                fire_director(kg);
            }
            bool player_dead = false;
            {
                auto pc = logotron::read_cycle(kg, player_entity_);
                player_dead = pc.state != logotron::CycleState::RIDING;
            }
            if (!player_dead) {
                // Auto-fire on cadence so the Director keeps shaping
                // the world even when both bikes are still alive.
                // Drives on real time so a long cinematic pause
                // doesn't push the next firing past the player's
                // patience. No-op if a Director request is already
                // in flight (director_pending_).
                if (!director_pending_ &&
                    !director_pending_apply_.has_value() &&
                    !cinematic_state_.active &&
                    director_real_clock_ >= next_auto_fire_at_) {
                    std::cerr << "[logotron] auto-fire (clock="
                              << director_real_clock_ << "s)" << std::endl;
                    next_auto_fire_at_ = static_cast<float>(director_real_clock_) +
                                          kAutoFireIntervalSec;
                    fire_director(kg);
                }
            } else {
                std::cerr << "[logotron] ROUND OVER — player derezzed"
                          << " (press R to restart)" << std::endl;
                round_over_ = true;
                emit_death_event(kg, player_entity_, "player", "", "grid");
                // Death overrides theater: never leave a cinematic
                // holding the camera over a corpse.
                if (cinematic_state_.active)
                    logotron::director::end(*engine_, cinematic_state_);
                // Death gets an unmissable screen, not a log line
                // (playtest 2026-07-23: the "press R" hint scrolled
                // away and the player thought the game hung).
                if (weirden_log_) {
                    weirden_log_->add_line(
                        "USER DEREZZED. " + std::to_string(derez_count_) +
                        " Program(s) taken down this run.",
                        255, 120, 120);
                }
                if (splash_) {
                    splash_->clear();
                    splash_->add_line("USER DEREZZED.", 255, 120, 120);
                    splash_->add_line(
                        "Run: " + std::to_string(derez_count_) +
                        " derez(s).  Best: " +
                        std::to_string(best_derez_streak_) + ".",
                        200, 200, 200);
                    splash_->add_line("SPACE or R: re-enter the Grid.",
                                      120, 255, 160);
                    splash_->add_line("ESC: leave the Grid.", 160, 160, 180);
                    splash_->set_visible(true);
                }
                update_score_hud();
                if (ai_instrument_) {
                    ai_instrument_->end_round(
                        "player_derezzed", elapsed_ - round_started_at_);
                }
            }
        }

        // On the frame the AI transitions to CRASHED, seal its active
        // run into a TrailSegment (so the fade covers it too) and
        // stamp the crash time. The bike particles are intentionally
        // NOT cleared here — sync_one_motorcycle's AI-crashed branch
        // skips the assembly re-sync, leaving the last RIDING pose on
        // screen. Player has a different treatment (TODO).
        for (auto* squad : {&enemies_, &allies_}) {
            for (auto& rd : *squad) {
                auto rc = logotron::read_cycle(kg, rd.entity);
                if (rc.state == logotron::CycleState::CRASHED &&
                    rd.crashed_at < 0.0f) {
                    logotron::freeze_run_at(kg, rd.entity, elapsed_);
                    rd.crashed_at = elapsed_;
                }
            }
        }

        // Drain any Weirden response that landed since last frame.
        // When ready, mutations apply and the AI cycle respawns. The
        // player keeps riding throughout.
        poll_director(kg);

        // Fairness clamp: AI max_speed must never exceed the
        // player's max_speed. The Director (random fallback rolling
        // [6, 14] AND the LLM emitting set_property max_speed=12+
        // ops) routinely tries to crank AI faster than the User —
        // playtest 2026-05-07: "I hate weird advantages, fair
        // competition." Re-clamp every tick so the cap holds
        // regardless of who or when the bump came in.
        clamp_ai_speed_to_player(kg);
        sync_grid_fog_from_kg(kg);

        // Visualization sync runs even after round-over so any
        // last-tick trail still gets a particle.
        sync_trail_particles(kg);
        sync_active_runs(kg);
        erode_trails(kg);
        sync_motorcycles(kg);
        for (auto* squad : {&enemies_, &allies_}) {
            for (auto& rd : *squad) {
                if (rd.crashed_at >= 0.0f && !rd.fade_armed) {
                    arm_crashed_trail_fade(kg, rd.entity);
                    rd.fade_armed = true;
                }
            }
        }
        // Ally lifecycle runs even post-round (like the fades above):
        // mourned once, never scored, never respawned, torn down after
        // the trail fade plays (an ally is a gift, not a guarantee).
        for (auto& rd : allies_) {
            if (rd.announced_crashed) continue;
            auto rc = logotron::read_cycle(kg, rd.entity);
            if (rc.state != logotron::CycleState::CRASHED) continue;
            rd.announced_crashed = true;
            emit_death_event(kg, rd.entity, "ally", rd.personality.name,
                             "grid");
            if (weirden_log_)
                weirden_log_->add_line(
                    "Your ally Program is derezzed.", 120, 200, 255);
        }
        for (auto it = allies_.begin(); it != allies_.end();) {
            if (it->crashed_at >= 0.0f &&
                (elapsed_ - it->crashed_at) > kTrailFadeDuration + 1.0f) {
                teardown_rider(kg, *it);
                it = allies_.erase(it);
            } else {
                ++it;
            }
        }
        update_director_orb(kg);
        follow_player(kg);

        // Push live speed numbers to the dashboard. Cheap pure-data
        // call — the widget owns no game state, just renders what we
        // give it. Reads the same Cycle struct the stepper writes,
        // so values are always one frame fresh.
        if (speed_dash_) {
            auto pc = logotron::read_cycle(kg, player_entity_);
            if (pc.current_speed > top_speed_this_run_)
                top_speed_this_run_ = pc.current_speed;   // Director metric
            speed_dash_->set_state(pc.current_speed, pc.max_speed,
                                   pc.base_speed, pc.time_since_turn);
        }
    }

    void apply_camera() {
        if (!engine_) return;
        auto& cam = engine_->get_camera_system();
        // Engine uses a 2.5D IsometricProjection where screen-center is
        // the CAMERA POSITION, not the look_at target. So to keep the
        // bike centered we just pin the camera to the bike's world
        // coords. (See tests/test_iso_camera_centering.cpp for the
        // locked-in contract.)
        cam.set_position(camera_target_x_, camera_target_y_, kBikeLookZ);
        // look_at is decorative in this projection — it only updates
        // shadow-culling state. Keep it synced to the camera position
        // so downstream distance tests still see a coherent target.
        cam.look_at(camera_target_x_, camera_target_y_, kBikeLookZ);
    }

    void follow_player(kg::KGModule& kg) {
        if (player_entity_ == kg::INVALID_ENTITY) return;
        auto c = logotron::read_cycle(kg, player_entity_);
        float target_x = arena_to_world_x(c.x);
        float target_y = arena_to_world_y(c.y);
        // No smoothing — snap the camera to the bike each frame so the
        // bike is unambiguously centered. Smoothing can come back once
        // framing is confirmed.
        camera_target_x_ = target_x;
        camera_target_y_ = target_y;
        apply_camera();
    }

    bool handle_mouse_scroll(double /*xoffset*/, double yoffset) override {
        // Zoom in the iso projection means changing pixels-per-world-unit.
        auto& cam = engine_->get_camera_system();
        cam.adjust_zoom(static_cast<float>(yoffset) * 2.0f);
        return true;
    }

    // Create a Motorcycle entity and pin default shape params. The
    // Build a motorcycle RigidAssembly. Shape parameters are hard-coded
    // here (game policy); the engine primitive owns the transform math.
    logosphere::assembly::RigidAssembly make_motorcycle(kg::EntityID entity,
                                                        bool is_player) {
        namespace la = logosphere::assembly;
        const float body_len   = 1.80f;
        const float body_w     = 0.55f;
        const float body_h     = 0.32f;
        const float wheel_r    = 0.26f;
        const float wheel_t    = 0.14f;
        const float wheel_offs = 0.70f;
        const float body_z     = wheel_r + body_h * 0.5f;

        la::RigidAssembly a;
        a.entity = entity;

        // Engine convention: local +Y = forward. Body long along Y.
        la::BodyPart body;
        body.name = "body";
        body.shape = ParticleShape::ELLIPSOID;
        body.local_z = body_z;
        body.width     = body_w;    // X (sideways)
        body.height    = body_len;  // Y (forward) — long axis
        body.thickness = body_h;    // Z (up)
        if (is_player) { body.r = 0.15f; body.g = 0.95f; body.b = 1.00f; }
        else           { body.r = 1.00f; body.g = 0.45f; body.b = 0.05f; }

        la::BodyPart wheel_front;
        wheel_front.name = "wheel_front";
        wheel_front.shape = ParticleShape::ELLIPSOID;
        wheel_front.local_y = +wheel_offs;  // forward end
        wheel_front.local_z = wheel_r;
        wheel_front.width     = wheel_t;        // axle along X
        wheel_front.height    = 2.0f * wheel_r; // diameter along Y
        wheel_front.thickness = 2.0f * wheel_r; // diameter along Z
        if (is_player) { wheel_front.r = 0.06f; wheel_front.g = 0.14f; wheel_front.b = 0.18f; }
        else           { wheel_front.r = 0.25f; wheel_front.g = 0.10f; wheel_front.b = 0.02f; }

        la::BodyPart wheel_rear = wheel_front;
        wheel_rear.name = "wheel_rear";
        wheel_rear.local_y = -wheel_offs;   // rear end

        // Canopy: flattened ellipsoid sitting on top of the body, biased
        // forward where the rider would sit. Brighter than the body so
        // it reads as a windshield/cockpit even at distance. Sits fully
        // above body top (body_z + body_h/2) with its own half-height.
        const float canopy_w = body_w * 0.55f;
        const float canopy_l = body_len * 0.42f;
        const float canopy_h = 0.18f;
        la::BodyPart canopy;
        canopy.name = "canopy";
        canopy.shape = ParticleShape::ELLIPSOID;
        canopy.local_y = body_len * 0.10f;    // bias forward over the rider
        canopy.local_z = body_z + body_h * 0.5f + canopy_h * 0.5f;
        canopy.width     = canopy_w;
        canopy.height    = canopy_l;
        canopy.thickness = canopy_h;
        if (is_player) { canopy.r = 0.60f; canopy.g = 0.98f; canopy.b = 1.00f; }
        else           { canopy.r = 1.00f; canopy.g = 0.75f; canopy.b = 0.25f; }

        // Tail fin: thin vertical blade at the rear, rising above body.
        // Width (X) is small — the fin is a blade, not a slab. Reads as
        // the Tron light-cycle rear wedge and doubles as a rear marker.
        la::BodyPart fin;
        fin.name = "tail_fin";
        fin.shape = ParticleShape::ELLIPSOID;
        fin.local_y = -body_len * 0.40f;     // near rear tip
        fin.local_z = body_z + body_h * 0.5f + 0.18f;
        fin.width     = 0.05f;                // thin blade (X)
        fin.height    = body_len * 0.28f;     // forward-axis chord (Y)
        fin.thickness = 0.32f;                // vertical span (Z)
        if (is_player) { fin.r = 0.25f; fin.g = 0.95f; fin.b = 1.00f; }
        else           { fin.r = 1.00f; fin.g = 0.55f; fin.b = 0.10f; }

        // Rider particle — the embodied driver. This is the entity
        // whose cone of vision the AI tactics read (see
        // GAME_DESIGN.md §16). Its local_yaw is the head rotation
        // RELATIVE to the bike; the AI / game-layer drives it every
        // frame. Sits slightly ABOVE the canopy top so it reads as
        // "person poking out of the bubble" in iso view.
        // Small enough to not dominate the silhouette.
        la::BodyPart rider;
        rider.name = "rider";
        rider.shape = ParticleShape::ELLIPSOID;
        rider.local_y = body_len * 0.05f;     // just forward of body center
        rider.local_z = canopy.local_z + canopy.thickness * 0.5f + 0.10f;
        rider.width     = 0.16f;               // shoulders (sideways)
        rider.height    = 0.18f;               // fore-aft (tiny bump in travel axis)
        rider.thickness = 0.22f;               // vertical — taller than wide so it reads as a head/bust
        rider.local_yaw = 0.0f;                // head aligned with bike at spawn
        if (is_player) { rider.r = 0.92f; rider.g = 0.80f; rider.b = 0.65f; }  // warm skin
        else           { rider.r = 0.25f; rider.g = 0.15f; rider.b = 0.20f; }  // dark helm

        a.parts = { body, wheel_front, wheel_rear, canopy, fin, rider };
        // Mark every bike part as dynamic so the vision-cone memory
        // grid (engine Pass-4) doesn't ghost the bike at world cells
        // it has previously moved through. Trails / floor / arena
        // walls keep the default (false) and DO get the memory blend.
        for (auto& part : a.parts) part.is_dynamic = true;
        return a;
    }

    // Index of the rider part inside the assembly's parts[] vector.
    // Kept as a named constant rather than scattered magic numbers so
    // head-rotation wiring stays robust if we re-order parts. If any
    // part insertion above changes, this constant must update too.
    static constexpr size_t kRiderPartIndex = 5;

    kg::EntityID spawn_motorcycle(kg::KGModule& kg) {
        return kg.createEntity("Motorcycle");
    }

    // Update the assembly pose from the rider's live cycle state, then
    // hand off to the engine primitive for rendering.
    void sync_one_motorcycle(kg::KGModule& kg,
                             kg::EntityID cycle_entity,
                             logosphere::assembly::RigidAssembly& assembly,
                             const logotron::ai::HeadState& head,
                             bool leave_on_crash) {
        if (cycle_entity == kg::INVALID_ENTITY ||
            assembly.entity == kg::INVALID_ENTITY) return;
        auto& ps = engine_->get_particle_system();

        auto c = logotron::read_cycle(kg, cycle_entity);
        if (c.state != logotron::CycleState::RIDING) {
            // Opponent (AI) crash treatment: leave the bike particles
            // where they crashed. Skipping the sync call means the
            // last RIDING-frame assembly particles remain in the KG
            // and keep rendering at that pose — no re-sync, no clear.
            // This is a placeholder until the proper Tron explosion
            // lands (the explosion-effect design notes).
            if (leave_on_crash) {
                return;
            }
            // Player crash keeps the original "hide" flow for now;
            // the player death sequence is a different design.
            logosphere::assembly::RigidAssembly cleared;
            cleared.entity = assembly.entity;
            cleared.parts.clear();
            logosphere::assembly::sync_rigid_assembly(kg, ps, cleared);
            return;
        }

        // Map cycle compass direction to assembly yaw using the engine
        // convention (docs/ARCHITECTURE.md): yaw=0 → facing +Y (north), +π/2 → +X
        // (east), ±π → -Y (south), -π/2 → -X (west). The bike body's
        // long (forward) axis is local +Y, so getting this right is
        // what aligns the body with the travel direction. Previous
        // mapping had EAST→0 which rendered the bike perpendicular to
        // its trail.
        const float kPi = 3.14159265358979323846f;
        float yaw = 0.0f;
        switch (c.direction) {
            case logotron::Direction::NORTH: yaw = 0.0f;        break;
            case logotron::Direction::EAST:  yaw = kPi * 0.5f;  break;
            case logotron::Direction::SOUTH: yaw = kPi;         break;
            case logotron::Direction::WEST:  yaw = -kPi * 0.5f; break;
        }
        assembly.world_x = arena_to_world_x(c.x);
        assembly.world_y = arena_to_world_y(c.y);
        assembly.world_z = 0.0f;
        assembly.world_yaw = yaw;

        // Rider head local yaw: both riders are embodied. The AI's
        // head is tactic-steered; the player's head is mouse-steered
        // (see update_game mouse-look block). In either case, the
        // world-space head yaw minus the bike yaw gives the local
        // offset the assembly applies on top of `world_yaw`.
        if (assembly.parts.size() > kRiderPartIndex) {
            assembly.parts[kRiderPartIndex].local_yaw =
                head.current_yaw - yaw;
        }

        logosphere::assembly::sync_rigid_assembly(kg, ps, assembly);
    }

    void sync_motorcycles(kg::KGModule& kg) {
        sync_one_motorcycle(kg, player_entity_, player_motorcycle_,
                            player_head_, /*leave_on_crash=*/false);
        for (auto* squad : {&enemies_, &allies_}) {
            for (auto& rd : *squad) {
                sync_one_motorcycle(kg, rd.entity, rd.motorcycle,
                                    rd.head, /*leave_on_crash=*/true);
            }
        }
    }

    void spawn_arena_boundary(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();

        // Self-illuminating neon strips on all four edges. Each
        // strip is its own visible box AND a light source — the
        // walls ARE the lights.
        auto strip = [&](float cx, float cy, float w, float h) {
            auto e = kg.createEntity("ArenaWall");
            kg.setProperty(e, "grid_x", "-999");  // not collidable
            kg.setProperty(e, "grid_y", "-999");
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = cx; p.y = cy; p.z = 0.40f;
            p.width = w; p.height = h; p.thickness = 0.80f;
            p.r = 0.20f; p.g = 0.80f; p.b = 1.0f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            p.owner = ParticleOwner::STATIC;
            p.is_at_rest = true;
            p.is_light_source    = true;
            p.emission_strength  = 4.0f;
            p.emission_radius    = 15.0f;
            ps.add_particle_to_entity(p, &kg, e);
            boundary_entities_.push_back(e);
        };
        const float half_w = kArenaW * 0.5f;
        const float half_h = kArenaH * 0.5f;
        const float strip_t = 0.35f;
        strip(0.0f,  half_h, kArenaW, strip_t); // south edge
        strip(0.0f, -half_h, kArenaW, strip_t); // north edge
        strip(-half_w, 0.0f, strip_t, kArenaH); // west edge
        strip( half_w, 0.0f, strip_t, kArenaH); // east edge

        // Discrete light points along each edge (8 per edge) to
        // fill in the neon wash. The strip particles' own emission
        // covers close-in; these reach further into the arena.
        const int lights_per_edge = 8;
        for (int i = 0; i < lights_per_edge; i++) {
            float t = (i + 0.5f) / lights_per_edge;
            float xs = -half_w + t * kArenaW;
            float ys = -half_h + t * kArenaH;
            auto edge_light = [&](float x, float y) {
                ps.queue_light(x, y, 1.0f,
                               120000.0f, 25.0f,
                               0.30f, 0.75f, 1.0f);
            };
            edge_light(xs,  half_h);
            edge_light(xs, -half_h);
            edge_light(-half_w, ys);
            edge_light( half_w, ys);
        }

        // Subtle floor grid: thin faint lines every 5 m in both
        // axes. Dim enough to read as "grid painted on floor" under
        // the brighter trails and wall glow.
        const float grid_step = 5.0f;
        const float grid_thickness = 0.05f;
        // Grid lines are 0.05 thick, so a centre at 0.02 leaves them 5 mm under
    // the floor. Half their own thickness sits them exactly on it.
    const float grid_z = 0.025f;
        for (float ax = -half_w; ax <= half_w + 0.01f; ax += grid_step) {
            auto e = kg.createEntity("ArenaWall");
            kg.setProperty(e, "grid_x", "-999");
            kg.setProperty(e, "grid_y", "-999");
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = ax; p.y = 0.0f; p.z = grid_z;
            p.width = grid_thickness; p.height = kArenaH; p.thickness = 0.05f;
            p.r = 0.10f; p.g = 0.25f; p.b = 0.45f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            p.owner = ParticleOwner::STATIC;
            p.is_at_rest = true;
            ps.add_particle_to_entity(p, &kg, e);
            boundary_entities_.push_back(e);
        }
        for (float ay = -half_h; ay <= half_h + 0.01f; ay += grid_step) {
            auto e = kg.createEntity("ArenaWall");
            kg.setProperty(e, "grid_x", "-999");
            kg.setProperty(e, "grid_y", "-999");
            Particle p = {};
            p.shape = ParticleShape::BOX;
            p.x = 0.0f; p.y = ay; p.z = grid_z;
            p.width = kArenaW; p.height = grid_thickness; p.thickness = 0.05f;
            p.r = 0.10f; p.g = 0.25f; p.b = 0.45f; p.a = 1.0f;
            p.SetMaterial(Materials::Type::STONE);
            p.owner = ParticleOwner::STATIC;
            p.is_at_rest = true;
            ps.add_particle_to_entity(p, &kg, e);
            boundary_entities_.push_back(e);
        }
    }

    kg::EntityID spawn_floor(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        auto floor = kg.createEntity("Arena");
        Particle p = {};
        p.shape = ParticleShape::BOX;
        // A 0.1-thick floor centred on zero has its underside at -0.05.
        // The arena rests ON the turtle; half its own thickness does that.
        p.x = 0.0f; p.y = 0.0f; p.z = 0.05f;
        p.width  = kArenaW;
        p.height = kArenaH;
        p.thickness = 0.1f;
        p.r = 0.04f; p.g = 0.06f; p.b = 0.12f; p.a = 1.0f;  // dark Tron blue
        p.SetMaterial(Materials::Type::STONE);
        p.owner = ParticleOwner::STATIC;
        p.is_at_rest = true;
        ps.add_particle_to_entity(p, &kg, floor);
        return floor;
    }

    // Wall styling moved to walls.h so headless ATs can verify
    // the resulting Particle properties (is_light_source, emission,
    // color, geometry) without dragging the full game class + GLFW
    // into the test TU. These methods stay as forwarders so
    // existing call sites compile unchanged.
    static void set_wall_color(Particle& p, bool is_player, bool is_director = false,
                               int hue = 0) {
        logotron::set_wall_color(p, is_player, is_director, hue);
    }
    static void set_wall_geometry(Particle& p, float sx, float sy,
                                  float ex, float ey) {
        logotron::set_wall_geometry(p, sx, sy, ex, ey);
    }

    // Master Control "thinking" orb. Pulsing cyan emissive sphere
    // floating above the player's bike while the Director's request
    // is in flight. Visible feedback: when the orb appears, the LLM
    // is cooking the next mutation. When it vanishes, the cinematic
    // is about to begin (or already has). KISS pattern: re-spawn each
    // frame with a sinusoidal pulse on size + emission, same as the
    // active-run heads.
    void update_director_orb(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();

        auto despawn = [&]() {
            if (director_orb_entity_ == kg::INVALID_ENTITY) return;
            auto kg_parts = kg.getEntityKGParticles(director_orb_entity_);
            std::vector<int> idxs;
            for (auto kgid : kg_parts) {
                int idx = static_cast<int>(kg.getRenderIndex(kgid));
                if (idx >= 0) idxs.push_back(idx);
            }
            if (!idxs.empty()) ps.delete_particles_immediate(idxs);
            kg.destroyEntity(director_orb_entity_);
            director_orb_entity_ = kg::INVALID_ENTITY;
        };

        // No active request, OR cinematic is up (bike about to vanish):
        // make sure the orb is gone.
        if (!director_pending_ || cinematic_state_.active) {
            despawn();
            return;
        }

        // We always despawn + respawn so the per-frame pulse is just a
        // fresh particle. Same tradeoff sync_active_runs makes for the
        // bike-trail head; particle creation is cheap enough.
        despawn();

        // Pulse parameters. Period ~0.8 s real time so the orb feels
        // alive without being seizure-fast. Size and emission both
        // breathe in lock-step.
        float t = static_cast<float>(director_real_clock_) * 7.85f;  // 2π/0.8
        float pulse = 0.5f + 0.5f * std::sin(t);  // 0..1
        float size  = 0.45f + 0.20f * pulse;       // 0.45..0.65 m
        float emit  = 1.5f  + 2.5f  * pulse;       // 1.5..4.0

        director_orb_entity_ = kg.createEntity("Cycle");
        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        p.x = player_motorcycle_.world_x;
        p.y = player_motorcycle_.world_y;
        // Hover ~1.6 m above the bike. Tall enough to read clearly
        // over the rider, low enough to stay visually attached.
        p.z = player_motorcycle_.world_z + 1.6f;
        p.width = p.height = p.thickness = size;
        p.size = size;
        // Master Control magenta — same hue as the HUD voice so the
        // colour cue reads as "the third party is acting." (Cyan was
        // the player's colour; reusing it would conflate.)
        p.r = 0.85f; p.g = 0.30f; p.b = 1.00f; p.a = 1.0f;
        p.emission_strength = emit;
        p.emission_radius   = 4.0f;
        p.is_dynamic = true;
        p.is_at_rest = true;
        p.owner = ParticleOwner::STATIC;
        ps.add_particle_to_entity(p, &kg, director_orb_entity_);
    }

    // Frozen trail segments: one particle per run, created once when
    // the run is sealed (direction change or crash). Rendered as a
    // single long thin rectangle spanning the run's endpoints — no
    // tile gridding.
    void sync_trail_particles(kg::KGModule& kg) {
        auto trails = kg.findByType("TrailSegment");
        if (trails.empty()) return;
        auto& ps = engine_->get_particle_system();

        for (auto t : trails) {
            if (rendered_trails_.count(t)) continue;

            auto sx_s = kg.getProperty(t, "start_x");
            auto sy_s = kg.getProperty(t, "start_y");
            auto ex_s = kg.getProperty(t, "end_x");
            auto ey_s = kg.getProperty(t, "end_y");
            auto owner_str = kg.getProperty(t, "owner_cycle_id");
            if (sx_s.empty() || ex_s.empty()) continue;
            float sx = kg_parse::to_float(sx_s, "start_x", t).value_or(0.0f);
            float sy = kg_parse::to_float(sy_s, "start_y", t).value_or(0.0f);
            float ex = kg_parse::to_float(ex_s, "end_x",   t).value_or(0.0f);
            float ey = kg_parse::to_float(ey_s, "end_y",   t).value_or(0.0f);

            bool is_player = (owner_str == std::to_string(player_entity_));
            bool is_director = (kg.getProperty(t, "director_origin") == "1");

            Particle p = {};
            set_wall_geometry(p,
                arena_to_world_x(sx), arena_to_world_y(sy),
                arena_to_world_x(ex), arena_to_world_y(ey));
            set_wall_color(p, is_player, is_director,
                           rider_hue_for(owner_entity(owner_str)));

            ps.add_particle_to_entity(p, &kg, t);
            rendered_trails_.insert(t);
        }
    }

    // Active run visualization: for each cycle, draw one thin
    // rectangle from run_start to the cycle's current position. The
    // particle is recreated each frame (delete + add) because the
    // segment grows continuously with the cycle. One particle per
    // cycle, so this is cheap.
    void sync_active_runs(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        auto remove_head = [&](kg::EntityID head) {
            if (head == kg::INVALID_ENTITY) return;
            auto kg_parts = kg.getEntityKGParticles(head);
            std::vector<int> idxs;
            for (auto kgid : kg_parts) {
                int idx = static_cast<int>(kg.getRenderIndex(kgid));
                if (idx >= 0) idxs.push_back(idx);
            }
            if (!idxs.empty()) ps.delete_particles_immediate(idxs);
            kg.destroyEntity(head);
        };

        auto draw_head = [&](kg::EntityID owner, bool is_player,
                             int hue) -> kg::EntityID {
            if (owner == kg::INVALID_ENTITY) return kg::INVALID_ENTITY;
            auto c = logotron::read_cycle(kg, owner);
            // Stop drawing an active-run head once the cycle crashes.
            // On the crash frame the update_game path has already
            // called freeze_run, promoting what used to be the live
            // segment into a TrailSegment — so the visual doesn't
            // disappear, it just transitions to the fade system.
            if (c.state != logotron::CycleState::RIDING) return kg::INVALID_ENTITY;
            kg::EntityID head = kg.createEntity("Cycle");
            Particle p = {};
            set_wall_geometry(p,
                arena_to_world_x(c.run_start_x), arena_to_world_y(c.run_start_y),
                arena_to_world_x(c.x),           arena_to_world_y(c.y));
            // Active-run head: per-frame churn — see
            // walls.h::style_active_run_head for why this can NOT
            // be a light source.
            logotron::style_active_run_head(p, is_player, hue);
            ps.add_particle_to_entity(p, &kg, head);
            return head;
        };

        remove_head(player_active_run_);
        player_active_run_ = draw_head(player_entity_, true, 0);
        for (auto* squad : {&enemies_, &allies_}) {
            for (auto& rd : *squad) {
                remove_head(rd.active_run);
                rd.active_run = draw_head(rd.entity, false, rd.hue);
            }
        }
    }

    // Opponent-crash trail fade, declarative: the frame the AI-crash
    // seal lands, every AI-owned trail particle is handed to the
    // engine's fade_out TransformationRule (alpha ramp over
    // kTrailFadeDuration, then triple-buffer-safe deferred deletion;
    // TrailSegment entities stay so gameplay logic / tests see the
    // trail). Player-owned trails are left alone — player death gets
    // a different treatment later.
    //
    // Collision is untouched by the fade: lethality flips on the
    // owner's cycle_state == CRASHED (arena.cpp), so visible ==
    // lethal holds through the ramp. The earlier "per-segment
    // lifetime fade" that broke that invariant stays dead.
    // === Splash scene =====================================================
    // Everything on the splash stage is made of ENGINE PARTICLES: the
    // wordmark, the PRESS SPACE call-to-action, and the faint endless
    // grid are all the same light-trail boxes the bikes leave
    // (walls.h estelas). This is the game-side prototype of the
    // particle-native UI direction (the particle-UI design
    // notes): interface as matter, subject to the
    // same engine as the world. Skipped entirely headless.
    static constexpr float kSplashStageY   = 200.0f;  // world y, void territory
    static constexpr float kSplashUnit     = 1.5f;    // wordmark grid cell, meters
    static constexpr float kSplashLetterPitch = 6.5f; // wordmark letter advance
    static constexpr float kSplashBirthGap = 0.10f;   // s between stroke rez-ins

    struct SplashStroke {
        kg::EntityID ent = kg::INVALID_ENTITY;
        float r = 0, g = 0, b = 0;   // full-brightness base color
        float birth = 0;             // splash_clock_ time this stroke rezzes
        int   letter = 0;            // pulse phase group
    };

    // Blocky per-letter strokes on a 3x5 grid, axis-aligned only — a
    // light cycle cannot draw a diagonal, it staircases (see R's leg).
    struct SplashSeg { float x1, y1, x2, y2; };
    static const std::vector<SplashSeg>* splash_glyph(char c) {
        static const std::map<char, std::vector<SplashSeg>> kGlyphs = {
            {'L', {{0,4,0,0},{0,0,2,0}}},
            {'O', {{0,4,2,4},{2,4,2,0},{2,0,0,0},{0,0,0,4}}},
            {'G', {{2,4,0,4},{0,4,0,0},{0,0,2,0},{2,0,2,2},{2,2,1,2}}},
            {'T', {{0,4,2,4},{1,4,1,0}}},
            {'R', {{0,0,0,4},{0,4,2,4},{2,4,2,2},{2,2,0,2},
                   {1,2,1,1},{1,1,2,1},{2,1,2,0}}},
            {'N', {{0,0,0,4},{0,4,2,4},{2,4,2,0}}},
            {'P', {{0,0,0,4},{0,4,2,4},{2,4,2,2},{2,2,0,2}}},
            {'E', {{0,0,0,4},{0,4,2,4},{0,2,1,2},{0,0,2,0}}},
            {'S', {{2,4,0,4},{0,4,0,2},{0,2,2,2},{2,2,2,0},{2,0,0,0}}},
            {'A', {{0,0,0,4},{0,4,2,4},{2,4,2,0},{0,2,2,2}}},
            {'C', {{2,4,0,4},{0,4,0,0},{0,0,2,0}}},
        };
        auto it = kGlyphs.find(c);
        return it == kGlyphs.end() ? nullptr : &it->second;
    }

    // Lay a word out of estela strokes. Letters advance by `pitch`,
    // strokes rez in sequentially starting at `birth0`. Returns the
    // birth time after the last stroke (so words can chain).
    float spawn_splash_word(kg::KGModule& kg, const char* text,
                            float center_x, float base_y,
                            float unit, float pitch, float birth0,
                            float cr, float cg, float cb,
                            bool split_palette = false) {
        auto& ps = engine_->get_particle_system();
        const size_t n = std::strlen(text);
        const float word_w = pitch * (n - 1) + 2.0f * unit;
        const float x0 = center_x - word_w * 0.5f;
        float birth = birth0;
        for (size_t li = 0; li < n; ++li) {
            const auto* glyph = splash_glyph(text[li]);
            if (!glyph) continue;   // space / unknown
            // split_palette: first half cyan (the player's light),
            // second half orange (the Program's) — the wordmark rule.
            float r = cr, g = cg, b = cb;
            if (split_palette && li >= n / 2) { r = 1.00f; g = 0.45f; b = 0.05f; }
            for (const auto& seg : *glyph) {
                SplashStroke st;
                st.letter = static_cast<int>(li);
                st.birth = birth;
                birth += kSplashBirthGap;
                st.r = r; st.g = g; st.b = b;
                st.ent = kg.createEntity("TrailSegment");
                Particle p{};
                logotron::set_wall_geometry(
                    p,
                    x0 + li * pitch + seg.x1 * unit, base_y + seg.y1 * unit,
                    x0 + li * pitch + seg.x2 * unit, base_y + seg.y2 * unit);
                logotron::set_wall_color(p, /*is_player=*/true,
                                         /*is_director=*/false);
                p.r = p.g = p.b = 0.0f;   // born dark; rez-in ramps it up
                ps.add_particle_to_entity(p, &kg, st.ent);
                splash_strokes_.push_back(st);
            }
        }
        return birth;
    }

    // The faint, endless Tron floor: dim blue lines every 10 m across
    // the whole visible stage. Static (no per-frame writes), spawned
    // at final color, torn down with the rest of the stage.
    void spawn_splash_grid(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        const float ext = 140.0f;   // beyond every screen edge
        for (float o = -ext; o <= ext; o += 10.0f) {
            for (int axis = 0; axis < 2; ++axis) {
                auto ent = kg.createEntity("TrailSegment");
                splash_static_ents_.push_back(ent);
                Particle p{};
                float sx = axis == 0 ? -ext : o;
                float ex = axis == 0 ?  ext : o;
                float sy = axis == 0 ? o : kSplashStageY - ext;
                float ey = axis == 0 ? o : kSplashStageY + ext;
                if (axis == 0) { sy = ey = kSplashStageY + o; sx = -ext; ex = ext; }
                logotron::set_wall_geometry(p, sx, sy, ex, ey);
                p.z = 0.02f;             // floor plane, under the letters
                p.thickness = 0.04f;
                p.is_self_emissive = true;
                p.emission_strength = 0.7f;
                p.r = 0.05f; p.g = 0.12f; p.b = 0.22f;  // very faint blue
                p.a = 1.0f;
                ps.add_particle_to_entity(p, &kg, ent);
            }
        }
    }

    void spawn_splash_logo(kg::KGModule& kg) {
        spawn_splash_grid(kg);
        // LOGOTRON: big, palette split cyan/orange.
        float next = spawn_splash_word(kg, "LOGOTRON", 0.0f,
                                       kSplashStageY - 2.5f * kSplashUnit,
                                       kSplashUnit, kSplashLetterPitch,
                                       /*birth0=*/0.0f,
                                       0.15f, 0.95f, 1.00f,
                                       /*split_palette=*/true);
        // PRESS SPACE: small, all cyan, rezzes after the wordmark.
        spawn_splash_word(kg, "PRESS SPACE", 0.0f,
                          kSplashStageY - 14.0f,
                          /*unit=*/0.55f, /*pitch=*/2.3f,
                          next + 0.4f,
                          0.15f, 0.95f, 1.00f);
        // Moving lights: three faint sources that sweep and orbit the
        // wordmark, throwing real illumination across the letters and
        // the stage grid (the strokes glow but do not illuminate —
        // these do). Positions animate in animate_splash_logo.
        auto& ps = engine_->get_particle_system();
        struct { float r, g, b; int mode; float phase; } specs[] = {
            {0.30f, 0.90f, 1.00f, 0, 0.0f},   // cyan sweeper
            {1.00f, 0.55f, 0.15f, 1, 0.0f},   // orange orbiter
            {0.80f, 0.85f, 1.00f, 2, 3.1f},   // pale counter-orbiter
        };
        for (const auto& sp : specs) {
            SplashLight sl;
            sl.mode = sp.mode;
            sl.phase = sp.phase;
            sl.ent = kg.createEntity("LightSource");
            ps.create_light_for_entity(0.0f, kSplashStageY, 2.5f,
                                       /*strength=*/1.1f, /*radius=*/16.0f,
                                       sp.r, sp.g, sp.b, &kg, sl.ent);
            splash_lights_.push_back(sl);
        }

        // Park the camera on the stage; the arena is nowhere in frame.
        auto& cam = engine_->get_camera_system();
        cam.set_pixels_per_unit(26.0f);
        cam.set_position(0.0f, kSplashStageY, logotron::kWallTrailZ);
        cam.look_at(0.0f, kSplashStageY, logotron::kWallTrailZ);
    }

    void animate_splash_logo() {
        if (!engine_ || splash_strokes_.empty()) return;
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();
        auto view = ps.lock_particles_for_write();
        // Moving lights: a sweeper tracing the word left to right and
        // two slow counter-orbiters. Faint by design — the effect is
        // the glint traveling across the letters and grid.
        for (auto& sl : splash_lights_) {
            auto kgids = kg.getEntityKGParticles(sl.ent);
            if (kgids.empty()) continue;
            auto idx = kg.getRenderIndex(kgids[0]);
            if (idx == kg::INVALID_RENDER_INDEX || idx >= view.size()) continue;
            float t = splash_clock_ + sl.phase;
            if (sl.mode == 0) {
                float span = 70.0f;
                view[idx].x = -span * 0.5f + std::fmod(t * 11.0f, span);
                view[idx].y = kSplashStageY - 1.0f;
            } else {
                float dir = sl.mode == 1 ? 1.0f : -1.0f;
                view[idx].x = 26.0f * std::cos(dir * t * 0.45f);
                view[idx].y = kSplashStageY + 9.0f * std::sin(dir * t * 0.45f);
            }
        }
        for (const auto& st : splash_strokes_) {
            auto kgids = kg.getEntityKGParticles(st.ent);
            if (kgids.empty()) continue;
            auto idx = kg.getRenderIndex(kgids[0]);
            if (idx == kg::INVALID_RENDER_INDEX || idx >= view.size()) continue;
            float t = splash_clock_ - st.birth;
            float ramp = t <= 0.0f ? 0.0f : std::min(1.0f, t / 0.35f);
            // Estela breathing: emissive brightness IS the color
            // (fade RCA lesson), phase-offset per letter.
            float pulse = 0.80f + 0.20f * std::sin(splash_clock_ * 2.6f -
                                                   st.letter * 0.7f);
            float k = ramp * pulse;
            view[idx].r = st.r * k;
            view[idx].g = st.g * k;
            view[idx].b = st.b * k;
        }
    }

    void end_splash() {
        if (!splash_active_) return;
        splash_active_ = false;
        auto& kg = engine_->get_kg();
        auto& ps = engine_->get_particle_system();
        std::vector<int> idxs;
        auto collect = [&](kg::EntityID ent) {
            for (auto kgid : kg.getEntityKGParticles(ent)) {
                auto ri = kg.getRenderIndex(kgid);
                if (ri != kg::INVALID_RENDER_INDEX)
                    idxs.push_back(static_cast<int>(ri));
            }
        };
        for (const auto& st : splash_strokes_) collect(st.ent);
        for (auto ent : splash_static_ents_) collect(ent);
        for (const auto& sl : splash_lights_) collect(sl.ent);
        if (!idxs.empty()) ps.delete_particles_immediate(idxs);
        for (const auto& st : splash_strokes_) kg.destroyEntity(st.ent);
        for (auto ent : splash_static_ents_) kg.destroyEntity(ent);
        for (const auto& sl : splash_lights_) kg.destroyEntity(sl.ent);
        splash_strokes_.clear();
        splash_static_ents_.clear();
        splash_lights_.clear();
        if (splash_) splash_->set_visible(false);
        if (weirden_log_)     weirden_log_->set_visible(true);
        if (director_ledger_) director_ledger_->set_visible(true);
        if (score_hud_)       score_hud_->set_visible(true);
        if (speed_dash_)      speed_dash_->set_visible(true);
        // Hand the camera back to the game (follow_player re-centers
        // on the bike next frame).
        engine_->get_camera_system().set_pixels_per_unit(24.0f);
        std::cerr << "[logotron] splash dismissed — entering the Grid"
                  << std::endl;
    }

    // Trail ribbon erosion (cycle.h policy): keep each cycle's total
    // trail length within its tail_length budget by continuously
    // consuming the OLDEST end — the classic light-cycle recede.
    // Rewrites KG start coords, so collision shortens in lockstep
    // with the visual by construction. Director walls are ownerless
    // and never enter a ribbon. Runs every frame; segment counts are
    // tens at most.
    void erode_trails(kg::KGModule& kg) {
        auto& ps = engine_->get_particle_system();
        std::vector<kg::EntityID> cycles = {player_entity_};
        for (const auto& rd : enemies_) cycles.push_back(rd.entity);
        for (const auto& rd : allies_) cycles.push_back(rd.entity);
        for (auto owner : cycles) {
            if (owner == kg::INVALID_ENTITY) continue;
            float budget = logotron::kTrailDefaultTailMeters;
            auto raw = kg.getProperty(owner, "tail_length");
            if (!raw.empty()) {
                float v = std::strtof(raw.c_str(), nullptr);
                if (v > 0.0f) budget = v;
            }
            // Speed-coupled wake: a fast bike drags a longer tail.
            // The turn that dumps speed also collapses the surplus.
            {
                auto cc = logotron::read_cycle(kg, owner);
                float bonus = (cc.current_speed - logotron::kCycleBaseSpeed) *
                              logotron::kTailSpeedCoupling;
                if (bonus > 0.0f) budget += bonus;
                if (budget > 200.0f) budget = 200.0f;   // schema ceiling
            }
            struct Seg { kg::EntityID ent; float sx, sy, ex, ey, len, spawn; };
            std::vector<Seg> segs;
            auto owner_str = std::to_string(owner);
            for (auto t : kg.findByType("TrailSegment")) {
                if (kg.getProperty(t, "owner_cycle_id") != owner_str) continue;
                auto sxs = kg.getProperty(t, "start_x");
                auto sys = kg.getProperty(t, "start_y");
                auto exs = kg.getProperty(t, "end_x");
                auto eys = kg.getProperty(t, "end_y");
                if (sxs.empty() || exs.empty()) continue;
                Seg sg; sg.ent = t;
                sg.sx = std::strtof(sxs.c_str(), nullptr);
                sg.sy = std::strtof(sys.c_str(), nullptr);
                sg.ex = std::strtof(exs.c_str(), nullptr);
                sg.ey = std::strtof(eys.c_str(), nullptr);
                sg.len = std::fabs(sg.ex - sg.sx) + std::fabs(sg.ey - sg.sy);
                auto sp = kg.getProperty(t, "spawn_time");
                sg.spawn = sp.empty() ? 0.0f : std::strtof(sp.c_str(), nullptr);
                segs.push_back(sg);
            }
            std::sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) {
                return a.spawn == b.spawn ? a.ent < b.ent : a.spawn < b.spawn;
            });
            float total = 0.0f;
            for (const auto& sg : segs) total += sg.len;
            auto c = logotron::read_cycle(kg, owner);
            if (c.state == logotron::CycleState::RIDING) {
                total += std::fabs(c.x - c.run_start_x) +
                         std::fabs(c.y - c.run_start_y);
            }
            float over = total - budget;
            for (auto& sg : segs) {
                if (over <= 0.001f) break;
                if (sg.len <= over + 0.001f) {
                    // Whole segment consumed.
                    over -= sg.len;
                    std::vector<int> idxs;
                    for (auto kgid : kg.getEntityKGParticles(sg.ent)) {
                        auto ri = kg.getRenderIndex(kgid);
                        if (ri != kg::INVALID_RENDER_INDEX)
                            idxs.push_back(static_cast<int>(ri));
                    }
                    if (!idxs.empty()) ps.delete_particles_immediate(idxs);
                    kg.destroyEntity(sg.ent);
                    rendered_trails_.erase(sg.ent);
                } else {
                    // Recede the oldest end by the overage.
                    float f = over / sg.len;
                    float nsx = sg.sx + (sg.ex - sg.sx) * f;
                    float nsy = sg.sy + (sg.ey - sg.sy) * f;
                    kg.setProperty(sg.ent, "start_x", std::to_string(nsx));
                    kg.setProperty(sg.ent, "start_y", std::to_string(nsy));
                    auto kgids = kg.getEntityKGParticles(sg.ent);
                    if (!kgids.empty()) {
                        auto ri = kg.getRenderIndex(kgids[0]);
                        if (ri != kg::INVALID_RENDER_INDEX) {
                            auto view = ps.lock_particles_for_write();
                            if (ri < view.size()) {
                                logotron::set_wall_geometry(
                                    view[ri],
                                    arena_to_world_x(nsx), arena_to_world_y(nsy),
                                    arena_to_world_x(sg.ex), arena_to_world_y(sg.ey));
                            }
                        }
                    }
                    over = 0.0f;
                }
            }
        }
    }

    // Pick the AI's next spawn: four inward-facing corner specs built
    // from LIVE arena dims (game policy as data, not a hardcoded
    // point — playtest 2026-07-23: "enemies spawn always in the same
    // place"). Chooses the corner farthest from the player so a fresh
    // Program never rezzes into the User's lap.
    static float dir_to_yaw(logotron::Direction d) {
        switch (d) {
            case logotron::Direction::NORTH: return 0.0f;
            case logotron::Direction::EAST:  return 1.5707963f;
            case logotron::Direction::SOUTH: return 3.14159265f;
            case logotron::Direction::WEST:  return -1.5707963f;
        }
        return 0.0f;
    }

    void seat_rider_head(kg::KGModule& kg, Rider& rd) {
        auto c = logotron::read_cycle(kg, rd.entity);
        float yaw = dir_to_yaw(c.direction);
        rd.head.current_yaw = yaw;
        rd.head.target_yaw  = yaw;
        rd.head.max_rate    = rd.personality.head_max_rate;
        rd.frame_count      = 0;
    }

    Rider spawn_rider(kg::KGModule& kg,
                      const logotron::director::AISpawnSpec& spec, int hue) {
        Rider rd;
        rd.hue = hue;
        rd.entity = logotron::spawn_cycle(kg, "AICycle",
                                          spec.x, spec.y, spec.direction);
        configure_speed_model(kg, rd.entity, logotron::kCycleMaxSpeed,
                              logotron::kCycleRampRate);
        rd.motorcycle = make_motorcycle(spawn_motorcycle(kg), false);
        seat_rider_head(kg, rd);
        return rd;
    }

    // Seed the round's roster: kDefaultEnemyCount Programs at the
    // corners farthest from the player, no stacking.
    void seed_enemies(kg::KGModule& kg) {
        enemies_.clear();
        auto specs = pick_spawns(kg, logotron::kDefaultEnemyCount);
        for (size_t i = 0; i < specs.size(); ++i)
            enemies_.push_back(spawn_rider(kg, specs[i], static_cast<int>(i)));
    }

    // Corner specs ranked farthest-from-player; rider k takes the
    // k-th farthest so simultaneous spawns never stack.
    std::vector<logotron::director::AISpawnSpec>
    pick_spawns(kg::KGModule& kg, int n) {
        const float w = live_arena_w();
        const float h = live_arena_h();
        const float m = 5.5f;
        std::vector<logotron::director::AISpawnSpec> corners = {
            {m,     m,     logotron::Direction::NORTH},
            {w - m, m,     logotron::Direction::NORTH},
            {m,     h - m, logotron::Direction::SOUTH},
            {w - m, h - m, logotron::Direction::SOUTH},
        };
        float px = w * 0.5f, py = h * 0.5f;
        if (player_entity_ != kg::INVALID_ENTITY) {
            auto pc = logotron::read_cycle(kg, player_entity_);
            px = pc.x; py = pc.y;
        }
        std::sort(corners.begin(), corners.end(),
                  [px, py](const auto& a, const auto& b) {
                      float da = (a.x-px)*(a.x-px) + (a.y-py)*(a.y-py);
                      float db = (b.x-px)*(b.x-px) + (b.y-py)*(b.y-py);
                      return da > db;
                  });
        if (n > static_cast<int>(corners.size()))
            n = static_cast<int>(corners.size());
        corners.resize(static_cast<size_t>(n));
        return corners;
    }

    // Kill attribution: did the dead Program hit the USER's light?
    bool crash_credits_player(kg::KGModule& kg, kg::EntityID dead) {
        auto hit_s = kg.getProperty(dead, "crash_hit_entity");
        if (hit_s.empty()) return false;
        kg::EntityID hit = 0;
        try { hit = static_cast<kg::EntityID>(std::stoul(hit_s)); }
        catch (...) { return false; }
        if (hit == player_entity_) return true;   // head-on
        auto owner = kg.getProperty(hit, "owner_cycle_id");
        return owner == std::to_string(player_entity_);
    }

    // Director metric: human-readable death record ring (last 5).
    // Deaths go to the ENGINE event journal (Information layer); the
    // Director drains them at fire time via its reader cursor, so the
    // prompt's derez block is literally "since your last
    // intervention". English stays game-side, at format time.
    void emit_death_event(kg::KGModule& kg, kg::EntityID dead,
                          const std::string& label,
                          const std::string& personality,
                          const std::string& killer_kind) {
        logosphere::ontology::DeathEvent ev;
        ev.target_entity_id = std::to_string(dead);
        auto hit_s = kg.getProperty(dead, "crash_hit_entity");
        if (!hit_s.empty()) {
            auto owner = kg.getProperty(
                kg::EntityID(std::stoul(hit_s)), "owner_cycle_id");
            ev.source_entity_id = owner.empty() ? hit_s : owner;
        }
        ev.payload_keys   = {"label", "personality", "killer", "cause",
                             "x", "y"};
        ev.payload_values = {label, personality, killer_kind,
                             kg.getProperty(dead, "crash_cause"),
                             kg.getProperty(dead, "crash_x"),
                             kg.getProperty(dead, "crash_y")};
        engine_->get_event_bus().deaths().emit(std::move(ev));
    }

    // Remove a rider's world footprint (bike, run head, entity).
    void teardown_rider(kg::KGModule& kg, Rider& rd) {
        std::vector<int> idxs;
        for (auto e : {rd.entity, rd.active_run, rd.motorcycle.entity}) {
            if (e == kg::INVALID_ENTITY) continue;
            for (auto kgid : kg.getEntityKGParticles(e)) {
                auto ri = kg.getRenderIndex(kgid);
                if (ri != kg::INVALID_RENDER_INDEX)
                    idxs.push_back(static_cast<int>(ri));
            }
        }
        if (!idxs.empty())
            engine_->get_particle_system().delete_particles_immediate(idxs);
        for (auto e : {rd.active_run, rd.motorcycle.entity, rd.entity}) {
            if (e != kg::INVALID_ENTITY) kg.destroyEntity(e);
        }
    }

    // The roster lever: reconcile app state against the KG after
    // Director ops. AICycle entities with no Rider get fully wired
    // (spread spawn if the op gave no position); Riders whose entity
    // vanished are torn down. Roster capped at 6.
    void reconcile_riders(kg::KGModule& kg) {
        // Tear down riders whose entity was destroyed by an op.
        for (auto it = enemies_.begin(); it != enemies_.end();) {
            if (kg.getType(it->entity).empty()) {
                std::vector<int> idxs;
                for (auto e : {it->active_run, it->motorcycle.entity}) {
                    if (e == kg::INVALID_ENTITY) continue;
                    for (auto kgid : kg.getEntityKGParticles(e)) {
                        auto rix = kg.getRenderIndex(kgid);
                        if (rix != kg::INVALID_RENDER_INDEX)
                            idxs.push_back(static_cast<int>(rix));
                    }
                }
                if (!idxs.empty())
                    engine_->get_particle_system().delete_particles_immediate(idxs);
                if (it->active_run != kg::INVALID_ENTITY)
                    kg.destroyEntity(it->active_run);
                if (it->motorcycle.entity != kg::INVALID_ENTITY)
                    kg.destroyEntity(it->motorcycle.entity);
                if (weirden_log_)
                    weirden_log_->add_line("A Program is unmade.", 220, 130, 240);
                it = enemies_.erase(it);
            } else {
                ++it;
            }
        }
        // Allies: tear down vanished, wire Director-created
        // AllyCycles beside the player (cap 2, never respawned).
        for (auto it = allies_.begin(); it != allies_.end();) {
            if (kg.getType(it->entity).empty()) {
                teardown_rider(kg, *it);
                it = allies_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto ent : kg.findByType("AllyCycle")) {
            bool known = false;
            for (const auto& rd : allies_)
                if (rd.entity == ent) { known = true; break; }
            if (known) continue;
            if (allies_.size() >= 2) {
                kg.destroyEntity(ent);
                if (director_ledger_)
                    director_ledger_->add_line("  !ally cap (2) — refused",
                                               220, 100, 100);
                continue;
            }
            Rider rd;
            rd.hue = -1;   // ally teal
            auto pc = logotron::read_cycle(kg, player_entity_);
            kg.destroyEntity(ent);
            float ox = (pc.direction == logotron::Direction::NORTH ||
                        pc.direction == logotron::Direction::SOUTH) ? 3.0f : 0.0f;
            float oy = (ox == 0.0f) ? 3.0f : 0.0f;
            rd.entity = logotron::spawn_cycle(kg, "AllyCycle",
                                              pc.x + ox, pc.y + oy,
                                              pc.direction);
            configure_speed_model(kg, rd.entity, logotron::kCycleMaxSpeed,
                                  logotron::kCycleRampRate);
            rd.motorcycle = make_motorcycle(spawn_motorcycle(kg), false);
            seat_rider_head(kg, rd);
            allies_.push_back(std::move(rd));
            if (weirden_log_)
                weirden_log_->add_line(
                    "MERCY OF THE MASTER CONTROL: an ally rezzes beside you.",
                    120, 255, 200);
        }

        // Wire Director-created AICycles into full riders.
        for (auto ent : kg.findByType("AICycle")) {
            bool known = false;
            for (const auto& rd : enemies_)
                if (rd.entity == ent) { known = true; break; }
            if (known) continue;
            if (enemies_.size() >= 6) {
                kg.destroyEntity(ent);
                if (director_ledger_)
                    director_ledger_->add_line("  !roster cap (6) — Program refused",
                                               220, 100, 100);
                continue;
            }
            Rider rd;
            rd.hue = static_cast<int>(enemies_.size());
            rd.entity = ent;
            // An op-created cycle usually lacks ride state: place it
            // at a spread corner and set it riding.
            auto c = logotron::read_cycle(kg, ent);
            if (c.state != logotron::CycleState::RIDING || c.max_speed <= 0.1f) {
                auto spec = pick_ai_spawn(kg);
                kg.destroyEntity(ent);
                rd.entity = logotron::spawn_cycle(kg, "AICycle",
                                                  spec.x, spec.y, spec.direction);
            }
            configure_speed_model(kg, rd.entity, logotron::kCycleMaxSpeed,
                                  logotron::kCycleRampRate);
            rd.motorcycle = make_motorcycle(spawn_motorcycle(kg), false);
            seat_rider_head(kg, rd);
            enemies_.push_back(std::move(rd));
            if (weirden_log_)
                weirden_log_->add_line("Another Program rezzes onto the line.",
                                       230, 150, 255);
        }
    }

    logotron::director::AISpawnSpec pick_ai_spawn(kg::KGModule& kg) {
        const float w = live_arena_w();
        const float h = live_arena_h();
        const float m = 5.5f;  // corner inset, matches the original spawn
        const logotron::director::AISpawnSpec corners[] = {
            {m,     m,     logotron::Direction::NORTH},
            {w - m, m,     logotron::Direction::NORTH},
            {m,     h - m, logotron::Direction::SOUTH},
            {w - m, h - m, logotron::Direction::SOUTH},
        };
        float px = w * 0.5f, py = h * 0.5f;
        if (player_entity_ != kg::INVALID_ENTITY) {
            auto pc = logotron::read_cycle(kg, player_entity_);
            px = pc.x; py = pc.y;
        }
        const logotron::director::AISpawnSpec* best = &corners[0];
        float best_d2 = -1.0f;
        for (const auto& c : corners) {
            float dx = c.x - px, dy = c.y - py;
            float d2 = dx * dx + dy * dy;
            if (d2 > best_d2) { best_d2 = d2; best = &c; }
        }
        return *best;
    }

    // === Escalation (Phase B) ============================================
    // Tier == Programs derezzed this run. The Master Control's random
    // vocabulary widens with it (walls densify at kTierDenseWallsAt,
    // arena shrinks harder at kTierHardShrinkAt, personality swaps
    // unlock at kTierPersonalityAt, the Grid darkens at kTierFogAt);
    // the LLM is told the tier and what it unlocks. Resets with the
    // run on player death.
    static constexpr int kTierPersonalityAt = 2;
    static constexpr int kTierHardShrinkAt  = 3;
    static constexpr int kTierFogAt         = 4;
    int escalation_tier() const { return derez_count_; }

    // Live arena dims from the KG (Director resizes must feed back
    // into prompts and wall placement); constants as fallback.
    float live_arena_w() { return live_arena_dim("arena_w", kArenaW); }
    float live_arena_h() { return live_arena_dim("arena_h", kArenaH); }
    float live_arena_dim(const char* key, float fallback) {
        if (!engine_ || arena_entity_ == kg::INVALID_ENTITY) return fallback;
        auto raw = engine_->get_kg().getProperty(arena_entity_, key);
        if (raw.empty()) return fallback;
        float v = std::strtof(raw.c_str(), nullptr);
        return v > 0.0f ? v : fallback;
    }

    // grid_fog sync: the Director darkens the Grid by setting the
    // KG property; the app owns flipping the actual render toggle
    // (same pattern as sync_ai_personality_from_kg). Once on, it
    // stays on until the run ends — reset_round clears it.
    void sync_grid_fog_from_kg(kg::KGModule& kg) {
        if (arena_entity_ == kg::INVALID_ENTITY) return;
        bool want = kg.getProperty(arena_entity_, "grid_fog") == "1";
        if (want == grid_fog_active_) return;
        grid_fog_active_ = want;
        engine_->set_vision_cone_enabled(want);
        if (weirden_log_) {
            weirden_log_->add_line(
                want ? "THE GRID DARKENS. Your eyes are all you have."
                     : "The Grid brightens.",
                240, 120, 240);
        }
        if (director_ledger_) {
            director_ledger_->add_line(
                want ? "  fog ON" : "  fog off", 240, 120, 240);
        }
    }

    // Rewrite the score readout (cheap: two lines).
    void update_score_hud() {
        if (!score_hud_) return;
        score_hud_->clear();
        score_hud_->add_line("Derezzed: " + std::to_string(derez_count_),
                             120, 255, 160);
        score_hud_->add_line("Best run: " + std::to_string(best_derez_streak_),
                             160, 160, 180);
    }

    // Human-readable one-liner for an applied Director op. The ledger
    // speaks specifics ("Arena.arena_w = 34.2", "+ TrailSegment #59741
    // @(12,30) len 5"); the Master Control HUD keeps the voice. Rebuilt
    // per the v0.10 TODO (the KGOp rewrite retired the old per-mutation
    // ledger lines and never replaced them).
    std::string describe_op_for_ledger(const kg::KGOp& op, kg::KGModule& kg,
                                       kg::EntityID created_id) {
        if (const auto* ce = std::get_if<kg::KGOpCreateEntity>(&op)) {
            std::string line = "+ " + ce->type;
            if (created_id != kg::INVALID_ENTITY)
                line += " #" + std::to_string(created_id);
            float sx = 0, sy = 0, ex = 0, ey = 0;
            int have = 0;
            for (const auto& kv : ce->properties) {
                if (kv.first == "start_x") { sx = std::strtof(kv.second.c_str(), nullptr); ++have; }
                else if (kv.first == "start_y") { sy = std::strtof(kv.second.c_str(), nullptr); ++have; }
                else if (kv.first == "end_x")   { ex = std::strtof(kv.second.c_str(), nullptr); ++have; }
                else if (kv.first == "end_y")   { ey = std::strtof(kv.second.c_str(), nullptr); ++have; }
            }
            if (have == 4) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), " @(%.0f,%.0f) len %.0f",
                              (sx + ex) * 0.5f, (sy + ey) * 0.5f,
                              std::fabs(ex - sx) + std::fabs(ey - sy));
                line += buf;
            }
            return line;
        }
        if (const auto* sp = std::get_if<kg::KGOpSetProperty>(&op)) {
            std::string who = sp->target.is_numeric()
                ? kg.getType(sp->target.id)
                : "@" + sp->target.symbolic;
            return who + "." + sp->property + " = " + sp->value;
        }
        if (const auto* de = std::get_if<kg::KGOpDestroyEntity>(&op)) {
            std::string who = de->target.is_numeric()
                ? "#" + std::to_string(de->target.id)
                : "@" + de->target.symbolic;
            return "- " + who;
        }
        if (const auto* sr = std::get_if<kg::KGOpSetRelation>(&op)) {
            return "rel " + sr->relation;
        }
        if (const auto* pc = std::get_if<kg::KGOpPlayCinematic>(&op)) {
            return "cinematic: " + pc->name;
        }
        return kg::kg_op_kind_name(op);
    }

    void arm_crashed_trail_fade(kg::KGModule& kg, kg::EntityID owner) {
        auto ai_id_str = std::to_string(owner);
        std::vector<kg::KGParticleID> ids;
        for (auto t_ent : kg.findByType("TrailSegment")) {
            if (kg.getProperty(t_ent, "owner_cycle_id") != ai_id_str) continue;
            for (auto kgid : kg.getEntityKGParticles(t_ent)) ids.push_back(kgid);
        }
        if (!ids.empty()) {
            engine_->get_interaction_system().arm_transformation(
                trail_fade_rule_, ids);
        }
    }

    bool handle_key(int key, int /*scancode*/, int action, int /*mods*/) override {
        if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
        if (splash_active_) {
            if (key == GLFW_KEY_SPACE) end_splash();
            if (key == GLFW_KEY_ESCAPE && engine_) engine_->stop();
            return true;   // splash swallows all input either way
        }
        if (key == GLFW_KEY_ESCAPE && engine_) {
            std::cerr << "[logotron] input: ESC — leaving the Grid"
                      << std::endl;
            engine_->stop();
            return true;
        }
        if (round_over_ && key == GLFW_KEY_SPACE) {
            std::cerr << "[logotron] input: SPACE — reset round"
                      << std::endl;
            reset_round(engine_->get_kg());
            return true;
        }
        if (!engine_ || player_entity_ == kg::INVALID_ENTITY) return false;
        auto& kg = engine_->get_kg();

        // R: reset the round at any time (handy for debugging).
        if (key == GLFW_KEY_R && action == GLFW_PRESS) {
            std::cerr << "[logotron] input: R — reset round (round_over="
                      << (round_over_ ? "true" : "false") << ")" << std::endl;
            reset_round(kg);
            return true;
        }

        // F: force-fire the Director without waiting for an AI death.
        // Debug-only path for testing the LLM round trip + cinematic +
        // KGOps apply without depending on player-vs-AI combat outcome.
        if (key == GLFW_KEY_F && action == GLFW_PRESS) {
            std::cerr << "[logotron] input: F — manually firing Weirden"
                      << std::endl;
            fire_director(kg);
            return true;
        }

        if (round_over_) return false;  // ignore steering after crash

        auto c = logotron::read_cycle(kg, player_entity_);
        logotron::Direction next = c.direction;
        switch (key) {
            // Relative turns. A = anti-clockwise, D = clockwise —
            // mirrors the arrow keys. Two bindings each so the
            // player can steer with either hand while the other
            // drives the mouse-look + vision cone.
            case GLFW_KEY_LEFT:
            case GLFW_KEY_A:     next = logotron::turn_left(c.direction);  break;
            case GLFW_KEY_RIGHT:
            case GLFW_KEY_D:     next = logotron::turn_right(c.direction); break;
            case GLFW_KEY_UP:
            case GLFW_KEY_DOWN:
                return false;  // unbound, fall through to engine
            default: return false;
        }
        if (next == c.direction) return true;  // no-op

        // Freeze the run the cycle has been driving, then update
        // the direction via turn() so the speed model resets
        // (current_speed → base_speed, ramp clock → 0). See §18.
        logotron::freeze_run_at(kg, player_entity_, elapsed_);
        auto c2 = logotron::read_cycle(kg, player_entity_);
        logotron::turn(c2, next);
        logotron::write_cycle(kg, player_entity_, c2);
        ++turns_this_run_;   // Director metric: how twitchy is the User
        std::cerr << "[logotron] input: turn to " << dir_name(next) << std::endl;
        return true;
    }

    void reset_round(kg::KGModule& kg) {
        // ps.add_particle_to_entity(p, &kg, e) binds the particle to
        // the entity via kg.createKGParticle(), NOT via the
        // ParticleSystem's own entity_particle_indices_ table. So the
        // right way to find "particles owned by this entity" is
        //   kg.getEntityKGParticles(e)  → KGParticleIDs
        //   kg.getRenderIndex(kg_id)    → render index in the particle array
        auto& ps = engine_->get_particle_system();
        size_t before = ps.count();

        std::unordered_set<kg::EntityID> wipe_ids;
        auto trails = kg.findByType("TrailSegment");
        for (auto t : trails) wipe_ids.insert(t);
        if (player_entity_      != kg::INVALID_ENTITY) wipe_ids.insert(player_entity_);
        if (floor_entity_       != kg::INVALID_ENTITY) wipe_ids.insert(floor_entity_);
        if (player_active_run_  != kg::INVALID_ENTITY) wipe_ids.insert(player_active_run_);
        if (player_motorcycle_.entity != kg::INVALID_ENTITY) wipe_ids.insert(player_motorcycle_.entity);
        if (director_orb_entity_      != kg::INVALID_ENTITY) wipe_ids.insert(director_orb_entity_);
        for (const auto* squad : {&enemies_, &allies_}) {
            for (const auto& rd : *squad) {
                if (rd.entity     != kg::INVALID_ENTITY) wipe_ids.insert(rd.entity);
                if (rd.active_run != kg::INVALID_ENTITY) wipe_ids.insert(rd.active_run);
                if (rd.motorcycle.entity != kg::INVALID_ENTITY)
                    wipe_ids.insert(rd.motorcycle.entity);
            }
        }

        std::vector<int> indices_to_delete;
        for (auto e : wipe_ids) {
            auto kg_particles = kg.getEntityKGParticles(e);
            for (auto kg_id : kg_particles) {
                int idx = static_cast<int>(kg.getRenderIndex(kg_id));
                if (idx >= 0) indices_to_delete.push_back(idx);
            }
        }
        std::cerr << "[logotron] reset: " << wipe_ids.size() << " entities, "
                  << indices_to_delete.size() << " particles (ps.count=" << before
                  << ")" << std::endl;

        if (!indices_to_delete.empty()) {
            ps.delete_particles_immediate(indices_to_delete);
        }

        for (auto e : wipe_ids) kg.destroyEntity(e);

        rendered_trails_.clear();
        player_entity_      = kg::INVALID_ENTITY;
        floor_entity_       = kg::INVALID_ENTITY;
        player_active_run_  = kg::INVALID_ENTITY;
        director_orb_entity_ = kg::INVALID_ENTITY;
        player_motorcycle_ = logosphere::assembly::RigidAssembly{};
        enemies_.clear();
        allies_.clear();

        ps.update_bvh();

        floor_entity_  = spawn_floor(kg);
        player_entity_ = logotron::spawn_cycle(
            kg, "PlayerCycle",
            kArenaW * 0.5f, kArenaH * 0.5f,
            logotron::Direction::EAST);
        player_motorcycle_ = make_motorcycle(spawn_motorcycle(kg), true);
        configure_speed_model(kg, player_entity_, logotron::kCycleMaxSpeed,
                              logotron::kCycleRampRate);
        seed_enemies(kg);
        round_over_ = false;
        derez_count_ = 0;   // run score dies with the run; best streak survives
        top_speed_this_run_ = 0.0f;
        turns_this_run_ = 0;
        update_score_hud();
        if (splash_) splash_->set_visible(false);   // death card down
        // Fog dies with the run (escalation semantics: once the
        // Director darkens the Grid it stays dark until you do).
        if (arena_entity_ != kg::INVALID_ENTITY)
            kg.setProperty(arena_entity_, "grid_fog", "0");
        grid_fog_active_ = false;
        engine_->set_vision_cone_enabled(false);
        // Drop any in-flight or cached Director response so a late
        // LLM reply can't mutate the freshly-reset world. Wipe the
        // history too — fresh duel, fresh slate (the v0.9 mutation
        // persistence rule already says full reset on player death).
        director_.cancel();
        director_.clear_history();
        director_pending_ = false;
        director_pending_apply_.reset();
        director_round_ = 0;
        // Reset the auto-fire cadence so the new round gets a fresh
        // warm-up period before its first Director firing.
        director_real_clock_ = 0.0;
        next_auto_fire_at_   = kFirstAutoFireAtSec;
        director_ai_deaths_ = 0;
        if (weirden_log_) {
            weirden_log_->clear();
            weirden_log_->add_line("The Grid is reset. A new run begins.",
                                   220, 130, 240);
        }
        if (director_ledger_) {
            director_ledger_->clear();
            director_ledger_->add_line("(no mutations yet)", 160, 160, 180);
        }
        // Rider heads were seated by seed_enemies. Re-seat the player's
        // head state too — the player spawns
        // facing EAST (kPi/2). Mouse position will drive it from the
        // first frame of the new round.
        player_head_.current_yaw = 1.5707963f;
        player_head_.target_yaw  = 1.5707963f;
        player_head_.max_rate    = kPlayerHeadMaxRate;
        // Start a new AI round in the telemetry log. This bumps the
        // instrument's round counter and starts a fresh
        // round_NNN.jsonl file.
        round_started_at_ = elapsed_;
        player_crash_noted_ = false;
        if (ai_instrument_ && !enemies_.empty())
            ai_instrument_->begin_round(enemies_[0].personality.name);
        std::cerr << "[logotron] new round — ps.count=" << ps.count()
                  << " (was " << before << ")" << std::endl;
    }

    kg::EntityID arena_entity() const { return arena_entity_; }
    kg::EntityID player_entity() const { return player_entity_; }
    size_t enemy_count() const { return enemies_.size(); }
    int derez_count() const { return derez_count_; }
    size_t ally_count() const { return allies_.size(); }
    // The death block the LAST Director fire drained from the engine
    // journal (empty when nothing died since the prior fire).
    const std::vector<std::string>& last_death_log() const {
        return last_death_log_;
    }
    const std::string& last_state_delta() const { return last_state_delta_; }
    // Test hook: run the roster reconciler outside a Director apply.
    // Organic reconciles ride on real-time auto-fire cadence + random
    // derez timing — ATs call this to make lever tests deterministic.
    void reconcile_rosters_now() { reconcile_riders(engine_->get_kg()); }
    kg::EntityID ally_entity(size_t i) const {
        return i < allies_.size() ? allies_[i].entity : kg::INVALID_ENTITY;
    }
    static kg::EntityID owner_entity(const std::string& owner_str) {
        if (owner_str.empty()) return kg::INVALID_ENTITY;
        try { return static_cast<kg::EntityID>(std::stoul(owner_str)); }
        catch (...) { return kg::INVALID_ENTITY; }
    }
    int rider_hue_for(kg::EntityID owner) const {
        for (const auto& rd : enemies_)
            if (rd.entity == owner) return rd.hue;
        return 0;
    }
    std::vector<kg::EntityID> ally_entities() const {
        std::vector<kg::EntityID> out;
        for (const auto& rd : allies_) out.push_back(rd.entity);
        return out;
    }
    std::vector<kg::EntityID> enemy_entities() const {
        std::vector<kg::EntityID> out;
        for (const auto& rd : enemies_) out.push_back(rd.entity);
        return out;
    }
    kg::EntityID enemy_entity(size_t i) const {
        return i < enemies_.size() ? enemies_[i].entity : kg::INVALID_ENTITY;
    }
    const std::string& enemy_personality_name(size_t i) const {
        static const std::string kNone = "none";
        return i < enemies_.size() ? enemies_[i].personality.name : kNone;
    }
    bool llm_configured() const { return llm_configured_; }

    // ----- Weirden Director hooks -----

    // v0.9 push_ledger_lines retired with mutations.{h,cpp}. KGOp
    // apply lines land in the Weirden HUD via the loop in
    // poll_director; the dedicated ledger HUD gains a KGOp-aware
    // ledger when per-op detail outgrows single-line summaries.


    // Snapshot the current world state and fire a Director request.
    // No-op if a request is already in flight.
    //
    // is_setup=true is the pre-game pre-fire: the player hasn't moved
    // yet, no Program has died, and we want the Director to author
    // the OPENING world. The round / death counters do NOT tick on
    // setup so the next real fire is still "duel 1".
    void fire_director(kg::KGModule& kg, bool is_setup = false) {
        if (director_pending_ || director_pending_apply_.has_value()) {
            std::cerr << "[logotron] Weirden busy (pending="
                      << director_pending_
                      << " cached_apply="
                      << director_pending_apply_.has_value()
                      << "); skipping fire" << std::endl;
            return;
        }
        if (!is_setup) {
            ++director_round_;
            ++director_ai_deaths_;
        }

        // Pre-fire shortcut. Cloud LLM first-call latency (Anthropic
        // cold = 10–25 s) is too long for the player's opening
        // seconds — they'll crash before anything Director-authored
        // appears. Use the random responder instead so the opening
        // Grid is shaped INSTANTLY. Real LLM still drives every
        // subsequent round; by then prompt caching is warm and
        // round trips drop to a few seconds.
        //
        // For headless / random-fallback configurations the pre-fire
        // path is identical to the normal path — random IS the
        // configured responder.
        if (is_setup && llm_configured_) {
            std::cerr << "[logotron] pre-fire: using instant random "
                         "shaping (LLM cold)" << std::endl;
            logotron::director::RandomDirectorContext pre_ctx;
            pre_ctx.arena_w = kArenaW;
            pre_ctx.arena_h = kArenaH;
            std::string json = logotron::director::generate_random_mutation_json(
                /*seed=*/0, pre_ctx);
            director_pending_apply_ = logotron::director::parse_director_json(json);
            return;
        }

        logotron::director::GameState state;
        state.round_number       = is_setup ? 0 : director_round_;
        state.ai_deaths          = director_ai_deaths_;
        state.arena_w            = live_arena_w();   // KG truth, not the constant:
        state.arena_h            = live_arena_h();   // resizes must reach the prompt
        state.escalation_tier    = escalation_tier();
        state.player_trail_count = logotron::count_trails_owned_by(kg, player_entity_);
        // Director walls: ownerless TrailSegments tagged with director_origin.
        auto all_trails = kg.findByType("TrailSegment");
        int dwalls = 0;
        for (auto t : all_trails) {
            if (kg.getProperty(t, "director_origin") == "1") ++dwalls;
        }
        state.director_wall_count = dwalls;
        if (player_entity_ != kg::INVALID_ENTITY) {
            auto pc = logotron::read_cycle(kg, player_entity_);
            state.player_max_speed = pc.max_speed;
        }
        for (size_t ri = 0; ri < allies_.size(); ++ri) {
            const Rider& rd = allies_[ri];
            logotron::director::GameState::RiderInfo info;
            info.ref = "@ally_" + std::to_string(ri + 1);
            info.personality = rd.personality.name;
            auto rc = logotron::read_cycle(kg, rd.entity);
            info.max_speed = rc.max_speed;
            info.trail_count = logotron::count_trails_owned_by(kg, rd.entity);
            state.riders.push_back(std::move(info));
        }
        for (size_t ri = 0; ri < enemies_.size(); ++ri) {
            const Rider& rd = enemies_[ri];
            logotron::director::GameState::RiderInfo info;
            info.ref = "@program_" + std::to_string(ri + 1);
            info.personality = rd.personality.name;
            auto rc = logotron::read_cycle(kg, rd.entity);
            info.max_speed = rc.max_speed;
            info.trail_count = logotron::count_trails_owned_by(kg, rd.entity);
            state.riders.push_back(std::move(info));
        }
        // Metrics the Director reads to make INFORMED moves.
        state.player_top_speed = top_speed_this_run_;
        state.player_turns     = turns_this_run_;
        state.round_duration_s = elapsed_ - round_started_at_;
        state.derez_count      = derez_count_;
        if (death_reader_) {
            auto payload = [](const logosphere::ontology::DeathEvent& e,
                              const std::string& key) -> std::string {
                for (size_t i = 0; i < e.payload_keys.size(); ++i)
                    if (e.payload_keys[i] == key) return e.payload_values[i];
                return {};
            };
            for (const auto& ev : death_reader_->drain()) {
                std::string label = payload(ev, "label");
                std::string pers  = payload(ev, "personality");
                std::string line;
                if (label == "player") {
                    line = "The USER derezzed";
                } else if (label == "ally") {
                    line = "Ally Program (" + pers + ") derezzed";
                } else {
                    std::string k = label.size() > 8 ? label.substr(8) : "?";
                    line = "Program #" + k + " (" + pers + ") derezzed " +
                           (payload(ev, "killer") == "user" ? "by the User"
                                                            : "by the Grid");
                }
                auto cause = payload(ev, "cause");
                auto cx = payload(ev, "x"), cy = payload(ev, "y");
                if (!cause.empty()) line += " [" + cause + "]";
                if (!cx.empty() && !cy.empty())
                    line += " @(" + cx + "," + cy + ")";
                state.death_log.push_back(std::move(line));
            }
            if (state.death_log.size() > 8)
                state.death_log.erase(
                    state.death_log.begin(),
                    state.death_log.end() - 8);
        }
        last_death_log_ = state.death_log;   // test/debug surface

        // Phase B context — give the LLM ontology + live KG snapshot
        // + symbolic refs + causation hint. Each of these survives
        // empty (the prompt builder skips empty blocks), so a
        // failure to build any of them just degrades context, never
        // breaks the round.
        // Hint follows the most recently crashed Program (its crash
        // metadata is still on the entity at fire time).
        kg::EntityID hint_target = kg::INVALID_ENTITY;
        for (const auto& rd : enemies_) {
            auto rc = logotron::read_cycle(kg, rd.entity);
            if (rc.state == logotron::CycleState::CRASHED) {
                hint_target = rd.entity;
                break;
            }
        }
        if (hint_target == kg::INVALID_ENTITY && !enemies_.empty())
            hint_target = enemies_[0].entity;
        if (hint_target != kg::INVALID_ENTITY)
            state.narrative_hint = logotron::director::make_narrative_hint(
                kg, hint_target);

        auto refs = logotron::director::build_symbolic_refs(
            player_entity_, enemy_entities(), arena_entity_, ally_entities());
        state.symbols_text = refs.as_prompt_text();

        // The Director sees the `world` facet — declared in
        // logotron.yaml, selected by the engine query algebra. No
        // hand-maintained type list; UI/debug types can never leak
        // in by construction.
        auto world_types = kg.getRegistry().typesWithFacet("world");
        state.ontology_slice = kg::serialize_ontology_slice(
            kg.getRegistry(), world_types);

        // Snapshot in two parts (consumer-side composition; if this
        // recurs across games, per-type projection moves engine-side):
        // full detail for the actors + arena, budget + projection for
        // the trail field (it dominates entity count, and the
        // Director reasons on endpoints + ownership, not spawn
        // bookkeeping). Newest trails win the budget — the algebra
        // has no ordering control yet, so we trim game-side.
        kg::Query actors;
        for (const auto& t : world_types)
            if (t != "TrailSegment") actors.types.push_back(t);
        auto rows = kg::run_query(kg, actors);

        kg::Query trails;
        trails.types = {"TrailSegment"};
        trails.props = {"start_x", "start_y", "end_x", "end_y",
                        "owner_cycle_id", "director_origin"};
        auto trail_rows = kg::run_query(kg, trails);
        if (trail_rows.size() > kTrailPromptBudget)
            trail_rows.erase(trail_rows.begin(),
                             trail_rows.end() - kTrailPromptBudget);
        rows.insert(rows.end(), trail_rows.begin(), trail_rows.end());
        state.kg_snapshot = kg::render_query_json(rows);

        // What changed since the last fire: the state_changes journal
        // folded into net deltas, filtered to the entities the
        // Director reasons about (trail coordinate churn from
        // erosion, and kinematic spam, stay out — game policy).
        if (state_reader_) {
            std::unordered_set<std::string> actors_ids;
            actors_ids.insert(std::to_string(player_entity_));
            actors_ids.insert(std::to_string(arena_entity_));
            for (const auto* squad : {&enemies_, &allies_})
                for (const auto& rd : *squad)
                    actors_ids.insert(std::to_string(rd.entity));
            static const std::unordered_set<std::string> kChurnProps = {
                "x", "y", "current_speed", "time_since_turn",
                "spawn_time"};
            auto entries = state_reader_->drain_entries();
            entries.erase(
                std::remove_if(
                    entries.begin(), entries.end(),
                    [&](const auto& en) {
                        const auto& e = en.event;
                        if (!e.target_entity_id ||
                            !actors_ids.count(*e.target_entity_id))
                            return true;
                        for (size_t i = 0; i < e.payload_keys.size(); ++i)
                            if (e.payload_keys[i] == "property")
                                return kChurnProps.count(
                                           e.payload_values[i]) > 0;
                        return true;
                    }),
                entries.end());
            state.state_delta = logosphere::render_state_deltas(entries);
        }
        last_state_delta_ = state.state_delta;   // test/debug surface

        director_pending_ = director_.fire(state);
        if (!director_pending_) {
            std::cerr << "[logotron] Weirden fire failed (no responder?)"
                      << std::endl;
            if (weirden_log_) {
                weirden_log_->add_line(
                    "The Master Control is silent. Strange.",
                    220, 130, 240);
            }
            return;
        }
        // Stamp the request start so poll_director can time it out
        // if the LLM never responds. Without this a stuck cloud-LLM
        // call permanently blocks the next AI-death fire.
        director_pending_started_at_real_ = director_real_clock_;

        // The cinematic does NOT begin here. Cloud LLM round trips
        // can run 5–25 s; pausing the game on fire would freeze the
        // player's input for that whole window. Instead the game
        // keeps running while the Director thinks; poll_director
        // begins the cinematic the moment a response lands and then
        // applies the ops under the pause. While the LLM thinks the
        // player just keeps riding.
        if (weirden_log_) {
            weirden_log_->add_line(
                "Master Control eyes the Grid...",
                220, 130, 240);
        }
        if (director_ledger_) {
            char hdr[64];
            std::snprintf(hdr, sizeof(hdr), "===== Round %d =====", director_round_);
            director_ledger_->add_line(hdr, 230, 150, 255);
        }
    }

    // Drain a ready Weirden response and apply it. Mutations + AI
    // respawn are deferred until the trail fade has played out (so
    // the dead Program's light wall actually sinks before the new
    // one rezzes in). Called every frame from update_game; cheap
    // when nothing is pending.
    void poll_director(kg::KGModule& kg) {
        // Drain any completed LLM HTTP responses into their per-request
        // callbacks. The callback is the lambda we wired in the
        // responder which calls Director::on_response_(json) — so
        // until this drain runs, the worker thread can have a parsed
        // response sitting in its response_queue_ that the Director
        // never sees. Without this, ready_ stays empty forever and
        // poll_director's director_.poll() always returns false.
        if (llm_) llm_->process_completed_responses();

        // Step 0a: timeout an in-flight LLM request. Cloud LLM round
        // trips can wedge (network hiccup, server queue, cold cache)
        // and would otherwise block every subsequent fire forever.
        // After kLLMTimeoutSecs of real time, drop the request and
        // free the pending flag so the next AI-death fire can land.
        // The Director::cancel() call also drops any response that
        // arrives late so it doesn't apply to a now-stale world.
        if (director_pending_ &&
            (director_real_clock_ - director_pending_started_at_real_)
                > kLLMTimeoutSecs) {
            std::cerr << "[logotron] Director request timed out after "
                      << kLLMTimeoutSecs << "s; abandoning" << std::endl;
            director_.cancel();
            director_pending_ = false;
            if (weirden_log_) {
                weirden_log_->add_line(
                    "The Master Control's signal grew faint. Lost.",
                    200, 120, 220);
            }
        }

        // Step 0b: if a cinematic is up, run its dwell timer on real
        // time. Once the dwell elapses we end the show and let the
        // game resume. ops have already applied at begin time below.
        if (cinematic_state_.active) {
            if (director_real_clock_ >= cinematic_end_at_real_) {
                logotron::director::end(*engine_, cinematic_state_);
            }
            return;
        }

        // Step 1: drain a fresh response into the local cache.
        if (director_pending_ && !director_pending_apply_.has_value()) {
            logotron::director::DirectorResponse resp;
            if (director_.poll(resp)) {
                director_pending_apply_ = std::move(resp);
                std::cerr << "[logotron][debug] director.poll → cached "
                          << "(thoughts=" << resp.thoughts.size()
                          << "ch ops=" << resp.kg_ops.size()
                          << " warns=" << resp.warnings.size()
                          << " err=\"" << resp.parse_error << "\")"
                          << std::endl;
            }
        }

        if (!director_pending_apply_.has_value()) return;

        // Step 2: hold the cached response until the trail fade has
        // had time to play, so the dead Program's wall has sunk
        // before the new Program rezzes onto the line. Game time
        // keeps advancing here because the cinematic hasn't started
        // yet — no deadlock risk.
        for (const auto& rd : enemies_) {
            if (rd.crashed_at >= 0.0f &&
                (elapsed_ - rd.crashed_at) < kTrailFadeDuration) {
                return;
            }
        }

        // The auto-cinematic on every Director apply is OFF.
        //
        // We tried it: dolly the camera, pause game time, hide the
        // bike, rez in a Program for ~2.5 s on every mutation. In a
        // fast-paced light-cycle duel the freeze + camera swap on
        // each apply made the game feel jerky — "stopping and
        // starting weirdly" in the player's own words. The Director
        // already has visible feedback: the magenta thinking-orb
        // floating over the bike while the LLM cooks, then the new
        // walls / wormholes / speed changes appearing on the Grid.
        //
        // Cinematics are still authored on demand: the LLM can
        // emit a `play_cinematic` op (validated + dispatched through
        // the engine's MutationPlaybackRegistry / cinematic registry)
        // for a deliberate dramatic beat. That stays the right place
        // for "show this to the User now" — Director's choice, not
        // every-round overhead.
        const bool is_pre_fire_apply = (director_round_ == 0);

        // Step 3: apply.
        auto resp = std::move(*director_pending_apply_);
        director_pending_apply_.reset();
        director_pending_ = false;

        if (!resp.parse_error.empty()) {
            std::cerr << "[logotron] Weirden parse error: "
                      << resp.parse_error << " (skipping)" << std::endl;
            if (weirden_log_) {
                weirden_log_->add_line(
                    "Master Control's signal is corrupted.", 220, 130, 240);
            }
        }
        if (!resp.thoughts.empty()) {
            std::cerr << "[logotron] Weirden: " << resp.thoughts << std::endl;
            if (weirden_log_) {
                // Bright magenta for the Master Control voice.
                weirden_log_->add_line(resp.thoughts, 230, 150, 255);
            }
        }
        for (const auto& w : resp.warnings) {
            std::cerr << "[logotron]   warn: " << w << std::endl;
        }

        // Phase C — KGOp path. If the LLM responded with the new
        // ops vocabulary, validate + apply each through the engine
        // helpers. Symbolic refs (@player_cycle, @ai_cycle, @arena)
        // are resolved against the same SymbolicRefs the prompt
        // built with. v0.9 menu mutations still apply below for
        // back-compat — the LLM can mix or match.
        bool any_significant_op = false;
        if (!resp.kg_ops.empty()) {
            auto refs = logotron::director::build_symbolic_refs(
                player_entity_, enemy_entities(), arena_entity_,
                ally_entities());

            auto resolve_ref = [&](kg::EntityRef& ref) {
                if (ref.is_symbolic()) {
                    auto resolved = refs.resolve("@" + ref.symbolic);
                    if (resolved != kg::INVALID_ENTITY) {
                        ref.id = resolved;
                        ref.symbolic.clear();
                    }
                }
            };

            for (auto& op : resp.kg_ops) {
                std::visit([&](auto& concrete) {
                    using T = std::decay_t<decltype(concrete)>;
                    if constexpr (std::is_same_v<T, kg::KGOpDestroyEntity>) {
                        resolve_ref(concrete.target);
                    } else if constexpr (std::is_same_v<T, kg::KGOpSetProperty>) {
                        resolve_ref(concrete.target);
                    } else if constexpr (std::is_same_v<T, kg::KGOpSetRelation>) {
                        resolve_ref(concrete.from);
                        resolve_ref(concrete.to);
                    } else if constexpr (std::is_same_v<T, kg::KGOpPlayCinematic>) {
                        // play_cinematic.target is also a symbolic
                        // EntityRef ("@player_cycle" etc) and the
                        // validator rejects unresolved symbolics with
                        // "play_cinematic: unresolved symbolic target".
                        resolve_ref(concrete.target);
                    }
                }, op);

                auto v = kg::validate_kg_op(op, kg, kg.getRegistry());
                if (!v.ok) {
                    std::string line = "> rejected " +
                        std::string(kg::kg_op_kind_name(op)) + ": " + v.reason;
                    std::cerr << "[logotron] " << line << std::endl;
                    if (weirden_log_) weirden_log_->add_line(line, 220, 100, 100);
                    if (director_ledger_)
                        director_ledger_->add_line(
                            "  !rejected " + std::string(kg::kg_op_kind_name(op)),
                            220, 100, 100);
                    continue;
                }
                auto a = kg::apply_kg_op(op, kg);
                if (!a.ok) {
                    std::string line = "> apply-failed " +
                        std::string(kg::kg_op_kind_name(op)) + ": " + a.reason;
                    std::cerr << "[logotron] " << line << std::endl;
                    if (weirden_log_) weirden_log_->add_line(line, 220, 100, 100);
                    if (director_ledger_)
                        director_ledger_->add_line(
                            "  !failed " + std::string(kg::kg_op_kind_name(op)),
                            220, 100, 100);
                    continue;
                }
                std::string line = "> ";
                line += kg::kg_op_kind_name(op);
                if (a.created_id != kg::INVALID_ENTITY) {
                    line += " #" + std::to_string(a.created_id);
                }
                std::cerr << "[logotron] " << line << std::endl;
                if (weirden_log_) weirden_log_->add_line(line, 180, 100, 220);
                // The ledger gets the specifics (v0.10 TODO repaid).
                if (director_ledger_)
                    director_ledger_->add_line(
                        "  " + describe_op_for_ledger(op, kg, a.created_id),
                        190, 190, 230);

                // Significant ops earn the pause cinematic (policy
                // decided 2026-07-18: arena reshapes, fog, and
                // personality swaps are dramatic beats; routine wall
                // spawns stay uninterrupted).
                if (const auto* sig = std::get_if<kg::KGOpSetProperty>(&op)) {
                    if (sig->property == "arena_w" || sig->property == "arena_h" ||
                        sig->property == "grid_fog" ||
                        sig->property == "ai_personality") {
                        any_significant_op = true;
                    }
                }

                // Phase D — fire the visual rez-in play. Entity
                // type is needed for set_property dispatch (Type.prop
                // key); pull it from KG. For create_entity ops the
                // type is on the op itself (registry handles that
                // case; entity_type arg is ignored there).
                std::string entity_type;
                if (auto* sp = std::get_if<kg::KGOpSetProperty>(&op)) {
                    if (sp->target.is_numeric()) {
                        entity_type = kg.getType(sp->target.id);
                    }
                }
                engine_->get_mutation_playback_registry().begin_play(
                    engine_, op, entity_type, a.created_id);
            }
        }

        // v0.9 menu-mutation apply path retired in v0.10. The KGOp
        // path above is the single way the Director shapes the
        // world now. Legacy mutations field on DirectorResponse +
        // mutations.{h,cpp} library deleted.

        // Capture this round's outcome into the Director's history so
        // the next prompt's "Prior rounds:" section reflects what
        // actually got applied. (Recording even on parse-error gives
        // the LLM a chance to course-correct: it sees its own empty
        // round and the live game state that resulted.)
        director_.record_round(director_round_, resp);

        // The pause cinematic, revived behind a significance gate.
        // The every-apply version felt jerky (removed 2026-05); big
        // beats only: the camera dollies in, the Program stands by
        // its work for kCinematicDwellSecs, then play resumes.
        if (any_significant_op && !is_pre_fire_apply &&
            !cinematic_state_.active) {
            auto pc = logotron::read_cycle(kg, player_entity_);
            logotron::director::CinematicInputs ci;
            ci.player_bike_entity = player_entity_;
            ci.center_x = arena_to_world_x(pc.x);
            ci.center_y = arena_to_world_y(pc.y);
            logotron::director::begin(*engine_, kg, ci, cinematic_state_);
            cinematic_end_at_real_ = director_real_clock_ + kCinematicDwellSecs;
        }

        // The Director can retune AI behaviour mid-game by writing
        // the AICycle.ai_personality slot. Re-resolve our local
        // Personality cache from whatever the KG now holds, so a
        // set_property the LLM just issued takes effect immediately.
        sync_ai_personality_from_kg(kg);

        // Respawn the AI ONLY if it actually crashed. Earlier code
        // respawned unconditionally on every non-pre-fire apply, which
        // meant auto-fire (every ~8 s) wiped + re-created the AI mid-
        // ride — playtesters reported the AI "popping out of existence
        // without crashing into anything" (2026-04-28). A live-but-
        // mutated AI is the correct response when the Director just
        // re-tunes its speed or drops a wall; respawn is for actual
        // deaths. Pre-fire never respawns either since the AI has just
        // been spawned in setup() and isn't crashed.
        bool any_respawned = false;
        if (!is_pre_fire_apply) {
            for (auto& rd : enemies_) {
                if (rd.entity == kg::INVALID_ENTITY) continue;
                auto ac = logotron::read_cycle(kg, rd.entity);
                if (ac.state == logotron::CycleState::CRASHED) {
                    respawn_rider(kg, rd);
                    any_respawned = true;
                }
            }
        }
        // The Director may also have grown or culled the roster with
        // create_entity AICycle / destroy_entity ops — reconcile the
        // app state against the KG (this is the roster lever).
        reconcile_riders(kg);

        if (any_respawned) {
            if (weirden_log_) {
                weirden_log_->add_line(
                    "A new Program rezzes onto the line.", 220, 130, 240);
            }
        } else if (is_pre_fire_apply && weirden_log_) {
            weirden_log_->add_line(
                "Master Control has shaped the opening Grid.",
                220, 130, 240);
        } else if (weirden_log_) {
            // Mutations applied but the AI is still riding. Tell the
            // player the Grid changed without claiming a respawn.
            weirden_log_->add_line(
                "The Master Control bends the Grid mid-ride.",
                220, 130, 240);
        }

        // Cinematic stays up; the dwell timer at the top of
        // poll_director will end() it once kCinematicDwellSecs of
        // real time have elapsed since begin(). That gives the player
        // a beat to see the Program standing by the rezzed-in changes
        // before the camera releases and the round resumes.

        // Per-rider crash/announce/fade flags reset inside
        // respawn_rider when each Program returns to the Grid.
    }

    // Fair-play guard: pin AICycle.max_speed <= PlayerCycle.max_speed.
    // The Director can still mutate the AI's max_speed to anything
    // the schema allows ([0.1, 25]), but if it exceeds the player's
    // we silently clamp. This is a per-tick reconciliation rather
    // than a validator block because the validator can't know the
    // player's current speed without a registry callback. Playtest
    // 2026-05-07 motivated the clamp: AI rolling 12-14 m/s vs the
    // player's 6 m/s broke the skill contest.
    void clamp_ai_speed_to_player(kg::KGModule& kg) {
        if (player_entity_ == kg::INVALID_ENTITY) return;
        auto pl_str = kg.getProperty(player_entity_, "max_speed");
        if (pl_str.empty()) return;
        auto pl = kg_parse::to_float(pl_str, "max_speed", player_entity_);
        if (!pl.has_value()) return;
        for (auto& rd : enemies_) {
            if (rd.entity == kg::INVALID_ENTITY) continue;
            auto ai_str = kg.getProperty(rd.entity, "max_speed");
            if (ai_str.empty()) continue;
            auto ai = kg_parse::to_float(ai_str, "max_speed", rd.entity);
            if (ai.has_value() && ai.value() > pl.value()) {
                kg.setProperty(rd.entity, "max_speed",
                               std::to_string(pl.value()));
            }
        }
    }

    // Re-resolve the local Personality cache from the AICycle's
    // ai_personality KG slot. The Director can write that slot with
    // a set_property op (values per the AIPersonality enum:
    // AGGRESSIVE / DEFENSIVE / CHAOTIC / PURSUING — also "default").
    // No-op when the slot is missing/empty/unchanged. Logs the swap
    // when it actually changes so live-play traces show personality
    // shifts.
    void sync_ai_personality_from_kg(kg::KGModule& kg) {
        for (size_t ri = 0; ri < enemies_.size(); ++ri) {
            Rider& rd = enemies_[ri];
            if (rd.entity == kg::INVALID_ENTITY) continue;
            auto val = kg.getProperty(rd.entity, "ai_personality");
            if (val.empty() || val == rd.personality.name) continue;
            auto next = logotron::ai::personality_from_name(val);
            std::cerr << "[logotron] Program #" << (ri + 1)
                      << " personality: " << rd.personality.name
                      << " → " << next.name << std::endl;
            if (weirden_log_) {
                weirden_log_->add_line(
                    "Master Control re-tunes Program #" +
                    std::to_string(ri + 1) + ": " + next.name,
                    220, 130, 240);
            }
            rd.personality = std::move(next);
            rd.head.max_rate = rd.personality.head_max_rate;
        }
    }

    // Application-side AI respawn: pure-KG respawn_ai plus particle and
    // motorcycle assembly cleanup. Mirrors the AI-only subset of
    // reset_round. Player and accumulated director walls untouched.
    void respawn_rider(kg::KGModule& kg, Rider& rd) {
        auto& ps = engine_->get_particle_system();

        std::unordered_set<kg::EntityID> wipe;
        if (rd.entity            != kg::INVALID_ENTITY) wipe.insert(rd.entity);
        if (rd.active_run        != kg::INVALID_ENTITY) wipe.insert(rd.active_run);
        if (rd.motorcycle.entity != kg::INVALID_ENTITY) wipe.insert(rd.motorcycle.entity);
        // This rider's TrailSegments. Director walls have no owner, so
        // the query won't pick them up.
        if (rd.entity != kg::INVALID_ENTITY) {
            auto trails = kg.findByProperty(
                "owner_cycle_id", std::to_string(rd.entity));
            for (auto t : trails) wipe.insert(t);
        }

        std::vector<int> idxs;
        for (auto e : wipe) {
            for (auto kgid : kg.getEntityKGParticles(e)) {
                int idx = static_cast<int>(kg.getRenderIndex(kgid));
                if (idx >= 0) idxs.push_back(idx);
            }
        }
        if (!idxs.empty()) ps.delete_particles_immediate(idxs);

        auto result = logotron::director::respawn_ai(
            kg, rd.entity, pick_ai_spawn(kg), &rendered_trails_);

        if (rd.motorcycle.entity != kg::INVALID_ENTITY) {
            kg.destroyEntity(rd.motorcycle.entity);
        }

        int hue = rd.hue;
        rd = Rider{};
        rd.hue = hue;
        rd.entity     = result.new_ai_entity;
        rd.motorcycle = make_motorcycle(spawn_motorcycle(kg), false);
        configure_speed_model(kg, rd.entity, logotron::kCycleMaxSpeed,
                              logotron::kCycleRampRate);
        seat_rider_head(kg, rd);

        ps.update_bvh();
        std::cerr << "[logotron] Program respawned (round=" << director_round_
                  << ", trails_wiped=" << result.trails_wiped << ")"
                  << std::endl;
    }

private:
    static const char* dir_name(logotron::Direction d) {
        switch (d) {
            case logotron::Direction::NORTH: return "NORTH";
            case logotron::Direction::EAST:  return "EAST";
            case logotron::Direction::SOUTH: return "SOUTH";
            case logotron::Direction::WEST:  return "WEST";
        }
        return "?";
    }

    Engine* engine_ = nullptr;
    kg::EntityID arena_entity_ = kg::INVALID_ENTITY;
    kg::EntityID player_entity_ = kg::INVALID_ENTITY;
    kg::EntityID floor_entity_ = kg::INVALID_ENTITY;
    kg::EntityID player_active_run_ = kg::INVALID_ENTITY;
    // Opponent-crash polish: the AI's bike stays at its crash pose and
    // its trail walls fade away over kTrailFadeDuration. elapsed_ is
    // the game's monotonic time since round start; ai_crashed_at_ is
    // the elapsed value at the moment the AI flipped to CRASHED, or
    // -1 if the AI is still RIDING. Player uses a different flow.
    float elapsed_        = 0.0f;

    // Auto-fire cadence — the Director fires on AI death AND on a
    // wall-clock-ish cadence so the world keeps mutating even when
    // both bikes are still alive. First fire ~3s after game start
    // (gives the player a beat to see the unaltered world), then
    // every kAutoFireIntervalSec while a round is in progress.
    // Wall-clock units use real_time so cinematic pause doesn't
    // delay the next firing.
    static constexpr float kFirstAutoFireAtSec   = 2.0f;
    static constexpr float kAutoFireIntervalSec  = 8.0f;
    float                  next_auto_fire_at_    = kFirstAutoFireAtSec;
    double                 director_real_clock_  = 0.0;
    // Master Control cinematic state. begin() is called from
    // poll_director() the moment a Director response is ready to
    // apply — that way the pause masks the actual mutation, not the
    // LLM round trip. The cinematic then lingers for
    // kCinematicDwellSecs of real time before end() releases the
    // camera and resumes the game. See director/cinematic.h.
    logotron::director::CinematicState cinematic_state_;
    double cinematic_end_at_real_ = 0.0;
    static constexpr float kCinematicDwellSecs = 2.5f;
    static constexpr float kTrailFadeDuration = 2.0f;
    kg::EntityID trail_fade_rule_  = kg::INVALID_ENTITY;
    // No per-segment render-side lifetime — trails persist visually
    // for the full round, matching cycle.h::kTrailLifetime (1e9 s)
    // on the collision side. Earlier version used 15 s here, which
    // made trails visually disappear while staying lethal: players
    // crashed on walls they could no longer see (sealed_trail_self
    // hits at age > 17 s). Per the cycle.h comment block, the
    // invariant is "every visible wall is lethal" — keep them
    // visible as long as they're lethal. Arena breathability comes
    // from the Director's destroy_entity ops + round resets, not
    // from a silent timed fade.

    // AI state. ai_head_ is the rider's head rotation (engine yaw).
    // ai_personality_ is Phase 1's single default personality; Phase 3
    // will rewrite this between rounds via the Weirden Director.

    // Player head — same HeadState shape as the AI rider. Mouse X
    // across the window drives target_yaw as an OFFSET from the
    // bike's heading, clamped so the player can't look fully back.
    // See update_game() for the mapping.
    logotron::ai::HeadState   player_head_{};
    static constexpr float kPlayerHeadMaxRate = 10.0f;    // rad/s — fast, human reflex
    static constexpr float kMaxHeadOffsetRad  = 2.3562f;  // 135° — forbidden arc is ±45° behind
    static constexpr float kPlayerVisionFovRad = 3.14159265f;  // 180°
    // Arena diagonal is ~57 m (40 × 40); set range to comfortably
    // exceed it so the cone can see end-to-end from any position
    // when the line of sight is clear. Tron's grid is flat — far
    // visibility is the look. The LOS occlusion mask still cuts
    // sight short the moment a wall is in the way, so this isn't
    // an "x-ray vision" range; it's the *clear-sight* horizon.
    static constexpr float kPlayerVisionRange  = 60.0f;

    // Telemetry session — owned for the full process lifetime. The
    // AIInstrument pointer is a non-owning alias into the session's
    // instrument list.
    std::unique_ptr<logosphere::telemetry::Session> telemetry_session_;
    logotron::ai::AIInstrument* ai_instrument_ = nullptr;

    // HUD — owned here because UISystem::add_widget takes a raw
    // non-owning pointer (ui_system.cpp:2061). Gets state pushed
    // each frame from update_game().
    std::unique_ptr<logotron::hud::SpeedDashboard> speed_dash_;
    float round_started_at_ = 0.0f;
    // One-shot guards so the crash-observer logs each cycle's death
    // exactly once per round (rather than every frame while it
    // stays CRASHED).
    bool  player_crash_noted_ = false;
    static constexpr int   kAiDecidePeriodFrames = 10;
    static constexpr float kAiConeFovRad = 2.0944f;   // 120° — see GAME_DESIGN §16
    static constexpr float kAiConeRange  = 10.0f;
    logosphere::assembly::RigidAssembly player_motorcycle_;
    std::vector<kg::EntityID> boundary_entities_;
    float camera_target_x_ = 0.0f;
    float camera_target_y_ = 0.0f;
    bool llm_configured_ = false;
    bool round_over_ = false;
    std::unordered_set<kg::EntityID> rendered_trails_;

    // Weirden Director: fires on AI crash, mutates the world, respawns
    // the AI fresh. The LLMSystemHTTP is owned here so its worker thread
    // stays alive for the application lifetime; the Director itself
    // holds a Responder lambda that captures `llm_.get()`.
    std::unique_ptr<Logosphere::LLMSystemHTTP> llm_;
    logotron::director::Director director_;
    std::unique_ptr<TextWindow> weirden_log_;
    std::unique_ptr<TextWindow> director_ledger_;  // mutation specifics
    std::unique_ptr<TextWindow> score_hud_;        // derez run score
    std::unique_ptr<TextWindow> splash_;           // startup brain/controls card
    std::string director_brain_desc_;              // "anthropic claude-haiku-4-5"; empty = random
    bool  splash_active_ = false;                  // Grid frozen on the wordmark stage
    float splash_clock_  = 0.0f;                   // drives rez-in + pulse
    std::vector<SplashStroke> splash_strokes_;
    std::vector<kg::EntityID> splash_static_ents_;  // stage grid lines
    struct SplashLight {
        kg::EntityID ent = kg::INVALID_ENTITY;
        int mode = 0;        // 0 sweep, 1 orbit cw, 2 orbit ccw
        float phase = 0.0f;
    };
    std::vector<SplashLight> splash_lights_;
    int derez_count_ = 0;         // Programs derezzed this run
    int best_derez_streak_ = 0;   // best run this session
    float top_speed_this_run_ = 0.0f;         // Director metric
    int   turns_this_run_ = 0;                // Director metric
    // Journal cursor over the engine deaths channel; drained at each
    // Director fire (prompt = deaths since last intervention).
    std::optional<logosphere::EventReader<logosphere::ontology::DeathEvent>>
        death_reader_;
    std::optional<logosphere::EventReader<logosphere::ontology::WorldEvent>>
        state_reader_;
    std::vector<std::string> last_death_log_;   // last fire's drained block
    std::string last_state_delta_;              // last fire's delta block
    // Prompt budget for the trail field (newest first). Game policy.
    static constexpr size_t kTrailPromptBudget = 60;
    bool grid_fog_active_ = false; // mirrors @arena.grid_fog
    bool director_pending_ = false;
    int  director_round_ = 0;       // ++ each AI death; used in prompt
    int  director_ai_deaths_ = 0;   // monotonic count for telemetry
    // Cached parsed response. Held while the trail-fade plays out so the
    // mutations + respawn don't snap before the visual fade can complete.
    std::optional<logotron::director::DirectorResponse> director_pending_apply_;
    // When the current request was submitted, in real-clock seconds.
    // Drives the LLM timeout in poll_director (kLLMTimeoutSecs).
    double director_pending_started_at_real_ = 0.0;
    static constexpr float kLLMTimeoutSecs = 15.0f;

    // Master Control "thinking" orb — a pulsing cyan emissive sphere
    // that floats above the player's bike while the Director's request
    // is in flight. Visible feedback that something is COMING. The
    // entity is destroyed and the particle deleted when the request
    // resolves (or when the cinematic begins, since the bike is
    // about to vanish anyway). Re-spawned every frame in
    // update_director_orb so the pulse animation is just a fresh
    // particle each tick — same pattern as active-run heads.
    kg::EntityID director_orb_entity_ = kg::INVALID_ENTITY;
};
