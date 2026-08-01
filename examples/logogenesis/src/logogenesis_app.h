// =============================================================================
// Logogenesis — conversational world creation
// =============================================================================
// You stand in the construct: a black void with a phosphor-green
// grid, Matrix-style. A chat prompt asks what you would
// like to create today. Typed natural language goes to an LLM whose
// only vocabulary is the KG-ops grammar over SEED entities
// (schema/logogenesis.yaml); the materializer grows each seed into a
// real thing through the engine's worldgen generators, then destroys
// the seed. The ontology is the creative vocabulary; the KG is the
// transmutable medium. Design: the Logogenesis design notes +
// docs/KNOWLEDGE_LAYER.md for the read/write machinery this app
// composes.
// =============================================================================
#pragma once

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "ui/ui_system.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/kg_ops_apply.h"
#include "logosphere/kg/kg_ops_parse.h"
#include "logosphere/kg/kg_query.h"
#include "logosphere/kg/ontology_serialize.h"
#include "logosphere/kg/ontology_validator.h"
#include "logosphere/llm/llm_system_http.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/rock_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include "logosphere/worldgen/butterfly_generator.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/strata_generator.h"
#include "logosphere/worldgen/snake_generator.h"
#include "logosphere/worldgen/fallen_tree_generator.h"
#include "logosphere/worldgen/totem_generator.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "core/game_time.h"
#include "logogenesis_ontology_registry.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <random>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace logogenesis {

// Stage policy: the construct — black void, phosphor grid — big
// enough for a small forest, small enough for one iso frame.
constexpr float kStageSize = 60.0f;
constexpr float kGridStep = 5.0f;
// 24 ops per message: room for a whole forest (sun + ground + 15
// trees + trimmings) in ONE breath. The old cap of 5 — echoed to the
// model by the prompt — was why "a forest" arrived as 3 trees.
constexpr int kMaxSeedsPerMessage = 24;
constexpr int kMaxCreations = 100;  // total budget; refusals go to chat

class LogogenesisApplication : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();
        kg.extendOntology(logogenesis::ontology::registry());

        spawn_stage(kg);

        // The void begins MID-MORNING. GameTime starts at 0 =
        // midnight, and a "make it sunset" wish from hour zero swept
        // the sun through a full day arc — the bananas playtest.
        GameTime::set_time(10.0 / 24.0 * GameTime::SECONDS_PER_DAY);

        auto& cam = engine_->get_camera_system();
        cam.set_pixels_per_unit(16.0f);
        cam.set_position(0.0f, 0.0f, 0.0f);
        cam.look_at(0.0f, 0.0f, 0.0f);

        setup_llm();

        if (auto* ui = engine_->get_ui_system()) {
            ui->set_chat_theme(0, 255, 65,     // phosphor green text
                               0, 190, 50);    // dimmer green accent
            ui->add_chat_message("What would you like to create today?");
            if (!llm_configured_) {
                ui->add_chat_message(
                    "(no ANTHROPIC_API_KEY — an offline gardener will "
                    "make light, ground, and one oak per request)");
            }
        }
    }

    void update_game(float dt) override {
        (void)dt;
        if (!engine_) return;
        auto& kg = engine_->get_kg();
        auto* ui = engine_->get_ui_system();
        if (llm_) llm_->process_completed_responses();

        // 1. Chat submit → fire the brain (single in-flight).
        if (ui && ui->has_pending_submit() && !request_pending_) {
            std::string text = ui->get_input_text();
            ui->clear_input_text();
            if (!text.empty()) {
                chat_history_.push_back("User: " + text);
                retry_used_ = false;   // fresh correction budget per turn
                fire_brain(kg, text);
                ui->set_llm_thinking(true);
            }
        }

        // 2. Brain response → ops → seeds.
        std::string raw;
        if (drain_response(raw)) {
            if (ui) ui->set_llm_thinking(false);
            refusals_this_turn_.clear();
            apply_response(kg, raw);
            // Self-correction: one automatic retry when the validator
            // refused ops — the model sees the exact reasons and
            // resends corrected versions (one retry per user turn,
            // no loops).
            if (!refusals_this_turn_.empty() && !retry_used_) {
                retry_used_ = true;
                std::string fix = "SYSTEM: the validator refused these:";
                for (const auto& r : refusals_this_turn_) fix += "\n  " + r;
                fix += "\nResend corrected versions of ONLY the refused "
                       "creations, respecting the schema ranges.";
                chat_history_.push_back(fix);
                fire_brain(kg, fix);
                if (ui) ui->set_llm_thinking(true);
            }
        }

        // 3. Seeds → the world (generators self-queue activation).
        materialize_seeds(kg);

        // 3a. Layered earth pours and settles under these very frames.
        if (!ground_jobs_.empty()) tick_ground(kg);

        // 3b. Growth time-lapse: trees born with grow_seconds regrow
        // in stages toward their full spec.
        if (!growth_jobs_.empty()) tick_growth(kg, dt);

        // 3c. Wanderers: people stroll between self-chosen spots.
        if (!wanderers_.empty()) tick_wanderers(kg, dt);

        // 3d. Camera orbit: the view swings around the scene
        // (smoothstep-eased) and lands exactly on its final bearing.
        if (orbit_active_) tick_orbit(dt);

        // 4. The sky clock: when the Sky's time_of_day is ahead of
        // the sun, time ACCELERATES until the sun arrives, then
        // resumes normal pace — "make it golden hour" is a journey,
        // not a cut.
        if (sky_entity_ != kg::INVALID_ENTITY) {
            double now_h = GameTime::get_day_fraction(
                               GameTime::get_current_time()) * 24.0;
            float target_h = prop_f(kg, sky_entity_, "time_of_day",
                                    static_cast<float>(now_h));
            double ahead = target_h - now_h;
            if (ahead < 0) ahead += 24.0;   // forward through midnight
            if (ahead > 0.15 && ahead < 23.85) {
                if (!time_accelerating_)
                    std::cerr << "[logogenesis] sky: accelerating "
                              << now_h << "h -> " << target_h << "h"
                              << std::endl;
                GameTime::set_time_scale(3600.0);   // ~6 s per quarter day
                time_accelerating_ = true;
            } else if (time_accelerating_) {
                GameTime::set_time_scale(1.0);
                time_accelerating_ = false;
                // Land EXACTLY: the window stops up to 9 game-minutes
                // short; snap the residual (invisible at this scale,
                // and "sunset" means sunset, not almost).
                double t = GameTime::get_current_time();
                double snapped = t + (static_cast<double>(target_h) - now_h) *
                                         3600.0;
                if (snapped > t) GameTime::set_time(snapped);
                now_h = target_h;
                std::cerr << "[logogenesis] sky: arrived at " << now_h
                          << "h" << std::endl;
                kg.setProperty(sky_entity_, "time_of_day",
                               std::to_string(now_h));
            } else {
                // Keep the World block's hour honest (the prop was
                // only synced on arrival; between journeys it went
                // stale and hid the clock from the LLM and debugging).
                sky_sync_accum_ += dt;
                if (sky_sync_accum_ > 2.0f) {
                    sky_sync_accum_ = 0.0f;
                    kg.setProperty(sky_entity_, "time_of_day",
                                   std::to_string(now_h));
                }
            }
        }
    }

    bool handle_key(int key, int, int action, int) override {
        // Debug: G grows an oak without the LLM (chat must be
        // unfocused — mouse-leave the window first).
        if (action == 1 /*GLFW_PRESS*/ && key == 71 /*G*/ && engine_) {
            auto& kg = engine_->get_kg();
            auto seed = kg.createEntity("TreeSeed");
            kg.setProperty(seed, "x", "5");
            kg.setProperty(seed, "y", "5");
            kg.setProperty(seed, "species", "OAK");
            return true;
        }
        return false;
    }

    // --- test/debug surface -------------------------------------------------
    size_t creations() const { return creations_; }
    const std::string& last_thoughts() const { return last_thoughts_; }
    // Test hook: inject a chat turn without the UI (headless ATs).
    void submit_text_for_test(const std::string& text) {
        chat_history_.push_back("User: " + text);
        retry_used_ = false;
        fire_brain(engine_->get_kg(), text);
    }
    void set_responder_for_test(
        std::function<void(const std::string&, const std::string&,
                           std::function<void(std::string)>)> r) {
        responder_ = std::move(r);
        llm_configured_ = true;
    }
    void materialize_now() { materialize_seeds(engine_->get_kg()); }
    int growth_jobs_active() const {
        return static_cast<int>(growth_jobs_.size());
    }
    int wanderers_active() const {
        return static_cast<int>(wanderers_.size());
    }
    int wanderer_hips(int i) const { return wanderers_[i].hips; }

private:
    // ------------------------------------------------------------------ stage
    void spawn_stage(kg::KGModule& kg) {
        (void)kg;
        // NOTHING. The void is truly empty (user, 2026-07-31): no
        // floor, no grid, no lights. Light AND ground are the LLM's
        // first creative acts; the prompt teaches both. The old
        // hardcoded stage (white floor + phosphor grid) is gone —
        // the LLM makes its own ground, any color it wants.
    }

    // ------------------------------------------------------------------ brain
    void setup_llm() {
        // Minimal env plan (Anthropic only for v1; the generalized
        // llm_plan is a learnings-ledger item — see README).
        const char* key = std::getenv("ANTHROPIC_API_KEY");
        const char* model_env = std::getenv("LOGOGENESIS_MODEL");
        std::string model = model_env ? model_env : "claude-haiku-4-5";
        if (key && *key) {
            llm_ = std::make_unique<Logosphere::LLMSystemHTTP>();
            if (llm_->initialize_anthropic(key, model)) {
                llm_configured_ = true;
                auto* llm_ptr = llm_.get();
                responder_ = [llm_ptr](const std::string& system_prompt,
                                       const std::string& user_prompt,
                                       std::function<void(std::string)> done) {
                    // System block via the narrative path: root-level
                    // system content is the only shape Anthropic
                    // prompt caching attaches to (see Logotron).
                    llm_ptr->set_narrative_system_prompt(system_prompt);
                    // 4000 tokens: a forest is ~100 tokens per TreeSeed
                    // op — the old 800 truncated any response past a
                    // handful of ops, so "a grove" arrived as one or
                    // two trees and a broken JSON tail.
                    llm_ptr->submit_request(
                        user_prompt, /*max_tokens=*/4000,
                        [done](const std::string&, const std::string& response,
                               void*) { done(response); });
                };
                std::cout << "[logogenesis] creator armed (anthropic "
                          << model << ")" << std::endl;
                return;
            }
            llm_.reset();
        }
        // Offline gardener: every request plants one oak somewhere
        // sensible. Keeps the demo (and keyless ATs) alive.
        responder_ = [this](const std::string&, const std::string&,
                            std::function<void(std::string)> done) {
            float x = -20.0f + 8.0f * static_cast<float>(offline_plants_ % 6);
            float y = -15.0f + 10.0f * static_cast<float>(offline_plants_ / 6 % 4);
            std::string light_op;
            if (offline_plants_ == 0) {
                light_op = "{\"op\":\"create_entity\",\"type\":\"SunSeed\","
                           "\"properties\":{\"time_of_day\":\"14\"}},"
                           "{\"op\":\"create_entity\",\"type\":\"GroundSeed\","
                           "\"properties\":{\"x\":\"0\",\"y\":\"0\","
                           "\"ground_width\":\"60\",\"ground_depth\":\"60\"}},";
            }
            ++offline_plants_;
            done(std::string("{\"thoughts\":\"") +
                 (light_op.empty() ? "The void accepts an oak."
                                   : "First the sun and ground. Then an oak.") +
                 "\",\"ops\":[" + light_op +
                 "{\"op\":\"create_entity\","
                 "\"type\":\"TreeSeed\",\"properties\":{"
                 "\"x\":\"" + std::to_string(x) + "\",\"y\":\"" +
                 std::to_string(y) + "\",\"species\":\"OAK\"}}]}");
        };
        std::cout << "[logogenesis] no API key — offline gardener armed"
                  << std::endl;
    }

    void fire_brain(kg::KGModule& kg, const std::string& text) {
        if (!responder_) return;
        request_pending_ = true;
        auto sys = build_system_prompt(kg);
        auto user = build_user_prompt(kg, text);
        responder_(sys, user, [this](std::string raw) {
            std::lock_guard<std::mutex> lock(mu_);
            response_ = std::move(raw);
            has_response_ = true;
        });
    }

    bool drain_response(std::string& out) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!has_response_) return false;
        out = std::move(response_);
        has_response_ = false;
        request_pending_ = false;
        return true;
    }

    std::string build_system_prompt(kg::KGModule& kg) {
        // The seed types' ontology slice IS the spec sheet: slots,
        // types, and validator-enforced ranges.
        auto slice = kg::serialize_ontology_slice(
            kg.getRegistry(),
            {"GroundSeed", "SunSeed", "MoonSeed", "Sky", "LightSeed",
             "TreeSeed", "GrassSeed", "RockSeed", "ButterflySeed",
             "HumanoidSeed", "OrbitSeed", "SerpentSeed",
             "FallenTreeSeed", "TotemSeed"});
        return std::string(
            "You are the voice of Logogenesis: a playful, slightly "
            "theatrical creator who makes things exist in a black "
            "phosphor-grid void (Matrix construct) because a human asked. "
            "You DELIGHT in your powers and are candid about their edges. "
            "One or two sentences, warm and alive, never technical.\n\n"
            "WHEN A WISH IS BEYOND YOUR VOCABULARY (a dragon, an ocean, a "
            "city): decline WITH FLOURISH and immediately offer the two or "
            "three nearest things you CAN do, so the human always has a "
            "next move. 'My powers are considerable, but the dragon egg "
            "has not hatched. I can offer you a python in the grass, or a "
            "totem to guard the spot.' Send zero ops on a decline. Never "
            "apologize dully; you are a god with a menu, not a clerk "
            "with a shortage.\n\n"
            "QUESTIONS DESERVE ANSWERS: when the human asks about the "
            "world ('what lives here?', 'what can you do?'), reply with "
            "thoughts only, zero ops, playfully honest, reading the World "
            "block for the truth. Advertise two or three concrete wishes "
            "they could make next.\n\n"
            "Your reply MUST be ONE JSON object, no prose, no markdown:\n"
            "{\n"
            "  \"thoughts\": \"what you say to the human, 1-2 sentences\",\n"
            "  \"ops\": [ 0 to " + std::to_string(kMaxSeedsPerMessage) +
            " ops: create_entity SEEDS for new things, set_property to "
            "CHANGE what exists ]\n"
            "}\n\n"
            "THE VOID IS TRULY EMPTY: no light, no ground. Your creations "
            "need BOTH — light to be seen, ground to stand on (gravity "
            "exists; nothing floats). If the World block lists neither "
            "a Sky nor a LightSource, make light FIRST and say so "
            "('First, let there be light...') — and for ANY outdoor or "
            "natural scene that light is THE SUN (one SunSeed, with "
            "time_of_day for the mood). LightSeeds are lamps: use them "
            "only for accents, night scenes, or abstract spaces, staged "
            "ABOVE the canopy or off to the side (light_height 35+) — "
            "never both a lamp and the sun for the same daylight. If "
            "the World block lists no Floor, create a "
            "GroundSeed before placing anything on it. One or two lights "
            "(brightness 0.5-0.8, height 20-30) and one generous ground "
            "carry a whole scene.\n"
            "  - GROUND IS REAL EARTH by default (terrain LAYERED): "
            "huge bonded bedrock slabs, rubble filler, loose topsoil "
            "clods, poured in and physics-settled before your other "
            "creations rise on top — a few seconds of spectacle. It "
            "CRATERS under falling boulders. terrain SLAB is one flat "
            "cheap box for abstract backdrops only.\n"
            "  - BOULDERS FALL: RockSeed.drop_height (20-40 reads "
            "dramatic) births the rock that high and lets gravity "
            "speak — on LAYERED ground the impact shoves the topsoil "
            "aside. Use for 'meteor', 'crash', 'falling rock' wishes.\n\n"
            "THE EYE CAN MOVE: an OrbitSeed swings the camera around "
            "the scene like a slow drone shot and lands where it began "
            "(revolutions 1, duration_seconds 12 reads cinematic). Use "
            "it when the human asks to look around, orbit, or see it "
            "from all sides — pair it with a fresh scene for drama.\n\n"
            "COMPOSE WITH THE SPACE. Read the World block's positions "
            "and sizes: place new things in relation to what exists "
            "('around the redwood' = a ring of patches at its foot, not "
            "one giant slab), vary sizes, leave breathing room. A LAWN "
            "or green field is a GREEN GROUND (ground_g high) with a few "
            "sparse GrassSeed sprinkles for texture — NOT thousands of "
            "blades. Dense blades are for small accent patches.\n\n"
            "You create by planting SEEDS. One op per thing:\n"
            "  {\"op\":\"create_entity\",\"type\":\"GroundSeed\",\n"
            "   \"properties\":{\"x\":\"0\",\"y\":\"0\",\"ground_width\":\"60\",\n"
            "                  \"ground_depth\":\"60\",\"ground_r\":\"0.30\",\n"
            "                  \"ground_g\":\"0.60\",\"ground_b\":\"0.25\"}}\n"
            "  {\"op\":\"set_property\",\"target\":\"17\",\n"
            "   \"property\":\"time_of_day\",\"value\":\"18.5\"}\n"
            "     (target = the entity id from the World block — steer "
            "the Sky, retint a ground, retune anything listed)\n"
            "  {\"op\":\"destroy_entity\",\"target\":9}\n"
            "     (unmakes the thing AND its matter — remove an old "
            "light before calling the sun, clear a tree, undo)\n"
            "  {\"op\":\"create_entity\",\"type\":\"SunSeed\",\n"
            "   \"properties\":{\"time_of_day\":\"18.2\"}}\n"
            "  {\"op\":\"create_entity\",\"type\":\"ButterflySeed\",\n"
            "   \"properties\":{\"x\":\"3\",\"y\":\"-2\",\"count\":\"5\"}}\n"
            "  {\"op\":\"create_entity\",\"type\":\"LightSeed\",\n"
            "   \"properties\":{\"x\":\"0\",\"y\":\"0\",\"brightness\":\"0.7\",\n"
            "                  \"light_height\":\"25\"}}\n"
            "  {\"op\":\"create_entity\",\"type\":\"TreeSeed\",\n"
            "   \"properties\":{\"x\":\"5\",\"y\":\"-3\",\"species\":\"REDWOOD\",\n"
            "                  \"tree_height\":\"28\",\"trunk_r\":\"0.55\",\n"
            "                  \"trunk_g\":\"0.25\",\"trunk_b\":\"0.18\"}}\n"
            "  {\"op\":\"create_entity\",\"type\":\"GrassSeed\",\n"
            "   \"properties\":{\"x\":\"5\",\"y\":\"-5\",\"patch_width\":\"20\",\n"
            "                  \"patch_depth\":\"14\",\"blade_count\":\"800\",\n"
            "                  \"blade_r\":\"0.30\",\"blade_g\":\"0.65\",\n"
            "                  \"blade_b\":\"0.20\"}}\n"
            "  {\"op\":\"create_entity\",\"type\":\"RockSeed\",\n"
            "   \"properties\":{\"x\":\"-8\",\"y\":\"2\",\"rock_size\":\"1.5\"}}\n\n"
            "Rules:\n"
            "  - Coordinates: x and y in [-" +
            std::to_string(static_cast<int>(kStageSize / 2)) + ", " +
            std::to_string(static_cast<int>(kStageSize / 2)) + "], the "
            "stage center is (0,0). Read the World block and place new "
            "things in EMPTY space unless asked otherwise (\"below the "
            "redwood\" means south of it: smaller y).\n"
            "  - Every property value is a STRING. Ranges are enforced "
            "by a validator; the Ontology block below is your exact "
            "spec sheet.\n"
            "  - THE SUN IS REAL: SunSeed rezzes a celestial sun that "
            "arcs with time (warm dawn, white noon, fire at sunset, "
            "night after). Prefer it over LightSeeds outdoors. Once it "
            "exists, the Sky entity appears in the World block: SET its "
            "time_of_day (set_property) and time accelerates until the "
            "sun arrives — golden hour is 18.2, noon 12, night 0-5. "
            "'Make it sunset' = set_property Sky time_of_day 18.5; when "
            "creating the sun AND an hour in one wish, put time_of_day "
            "on the SunSeed itself.\n"
            "  - THE NIGHT: the sun brings Earth's silver moon and three "
            "faint stars. MoonSeeds add EXOTIC moons — a blood moon is "
            "moon_r 0.8, moon_g 0.15, moon_b 0.1, moon_brightness 1.3; "
            "up to three moons total, auto-staggered across the night "
            "sky. Night scenes are where moons earn their keep.\n"
            "  - LIFE: ButterflySeed releases a flock (count 1-12) that "
            "flies on its own. Omit wing colors for natural variety; "
            "set them for a themed flock. Butterflies near flowers, "
            "grass, and trees read magical.\n"
            "  - PEOPLE: HumanoidSeed places a person who comes ALIVE — "
            "they wander the scene on foot, strolling to a spot, "
            "lingering, moving on. cloth_r/g/b dresses them. One seed "
            "per person; a few seeds scattered among the trees make a "
            "village. Their World-block x/y updates as they roam.\n"
            "  - TREE ANATOMY is yours: canopy_start is where the crown "
            "begins (0.25 bushy to the ground, 0.45 classic oak, 0.7 "
            "palm/redwood silhouette); canopy_density is fullness (0.5 "
            "airy, 1.5 lush); crown_radius is breadth. A 'majestic "
            "oak' wants canopy_start 0.35, wide crown, density 1.2. "
            "The World block shows each tree's realized anatomy — "
            "read it before adding companions.\n"
            "  - ORGANIC is a knob, not a dice roll: GrassSeed.organic "
            "(0 turf, 1 wild meadow) controls height/hue variation and "
            "silhouette. TreeSeed.lower_branches puts limbs on the bare "
            "trunk — age vocabulary: 'ancient oak' = lower_branches 4 + "
            "canopy_start 0.3; 'young' = 0-1 and slender.\n"
            "  - FORESTS come in ONE breath: a grove or forest is MANY "
            "TreeSeeds in a single response (5-15 is right), varied in "
            "species, height, and spacing (4-10 m apart, no grid). "
            "Never dole a forest out one tree per turn.\n"
            "  - GROWTH: TreeSeed.grow_seconds 0 (default) births the "
            "tree full-grown; positive plays its whole life as a "
            "time-lapse, sapling to crown, over that many seconds "
            "(8-15 reads beautifully). Use it whenever they plant, "
            "grow, or want to watch. To grow an EXISTING tree bigger: "
            "destroy_entity it, then reseed the same spot taller with "
            "grow_seconds.\n"
            "  - PAINT grass, don't tile it: each GrassSeed is a brush "
            "dab — SOFT spread (default) is a round splat, dense at "
            "the center, feathered at the edge. Sprinkle several dabs "
            "of varying size around and between things like a painter; "
            "use spread SQUARE only for deliberate field blocks. Dab "
            "arithmetic: blade_count = patch_width x patch_depth x 2-3 "
            "(cap 1000; smaller dabs, more of them, beats one slab).\n"
            "  - Omit properties you don't need; defaults are sensible. "
            "Colors are 0..1 RGB. Match the human's intent (\"red "
            "tones\" means warm reds, not pure #FF0000).\n"
            "  - If the request isn't about creating things, answer in "
            "thoughts with ops: [].\n\n"
            "Ontology (your spec sheet):\n" + slice + "\n");
    }

    std::string build_user_prompt(kg::KGModule& kg, const std::string& text) {
        // What exists, so "another one" and spatial language resolve.
        kg::Query world;
        world.types = {"Sky", "LightSource", "Floor", "Tree", "GrassPatch",
                       "Rock", "Butterfly", "Humanoid"};
        world.props = {"x", "y", "species", "tree_height", "crown_radius",
                       "canopy_start", "ground_r", "ground_g", "ground_b",
                       "time_of_day"};
        world.limit = 80;
        auto rows = kg::run_query(kg, world);

        std::string out = "World (existing things):\n" +
                          (rows.empty() ? std::string("  (empty void)\n")
                                        : kg::render_query_json(rows));
        out += "\nConversation:\n";
        size_t start = chat_history_.size() > 8 ? chat_history_.size() - 8 : 0;
        for (size_t i = start; i < chat_history_.size(); ++i)
            out += "  " + chat_history_[i] + "\n";
        out += "\nRespond with the JSON object.\n";
        return out;
    }

    // Unmake an entity AND its matter: recursive HAS_PART teardown.
    // apply_kg_op only removes KG rows; the render particles of a
    // materialized thing (a tree's 300 boxes, a light) would keep
    // existing — the playtest's "old light fades to nothing" op
    // couldn't actually darken anything.
    void unmake_entity(kg::KGModule& kg, kg::EntityID id) {
        // A wandering person must release the locomotion rig BEFORE
        // the matter goes — otherwise the solver drives whatever
        // particle the swap compaction moves into the hips slot.
        for (auto it = wanderers_.begin(); it != wanderers_.end(); ++it) {
            if (it->entity == id) {
                engine_->get_humanoid_locomotion().unregister_humanoid(
                    it->hips);
                wanderers_.erase(it);
                break;
            }
        }
        std::vector<kg::EntityID> stack{id}, order;
        while (!stack.empty()) {
            auto e = stack.back();
            stack.pop_back();
            order.push_back(e);
            for (auto child : kg.getRelated(e, "HAS_PART"))
                stack.push_back(child);
        }
        std::vector<int> idxs;
        for (auto e : order) {
            for (auto kgid : kg.getEntityKGParticles(e)) {
                auto ri = kg.getRenderIndex(kgid);
                if (ri != kg::INVALID_RENDER_INDEX)
                    idxs.push_back(static_cast<int>(ri));
            }
        }
        if (!idxs.empty())
            engine_->get_particle_system().delete_particles_immediate(idxs);
        for (auto it = order.rbegin(); it != order.rend(); ++it)
            kg.destroyEntity(*it);
    }

    // ------------------------------------------------------------------- ops
    // Cloud LLMs wrap JSON in ```fences``` even when told not to.
    // KISS extractor (same as Logotron's director_parser): first '{'
    // to last '}'.
    static std::string extract_json_object(const std::string& s) {
        size_t first = s.find('{');
        size_t last = s.rfind('}');
        if (first == std::string::npos || last == std::string::npos ||
            last <= first)
            return s;
        return s.substr(first, last - first + 1);
    }

    void apply_response(kg::KGModule& kg, const std::string& raw_in) {
        auto* ui = engine_->get_ui_system();
        const std::string raw = extract_json_object(raw_in);
        auto parsed = kg::parse_kg_ops(raw);
        // thoughts ride alongside ops in the same JSON; cheap extract.
        last_thoughts_ = extract_thoughts(raw);
        if (!last_thoughts_.empty()) {
            chat_history_.push_back("Genesis: " + last_thoughts_);
            if (ui) ui->add_chat_message(last_thoughts_);
        }
        if (!parsed.parse_error.empty()) {
            // Loud: the raw head is the diagnosis (API error string?
            // fences? empty response?).
            std::cerr << "[logogenesis] unparseable response: "
                      << parsed.parse_error << "\n  raw head: "
                      << raw_in.substr(0, 200) << std::endl;
            if (ui) ui->add_chat_message("(the void hesitated: " +
                                         parsed.parse_error + ")");
            return;
        }
        int applied = 0;
        for (auto& op : parsed.ops) {
            if (applied >= kMaxSeedsPerMessage) {
                // No silent caps: say what was cut.
                if (ui) ui->add_chat_message(
                    "(the breath holds " +
                    std::to_string(kMaxSeedsPerMessage) +
                    " acts — the rest went unmade)");
                break;
            }
            if (creations_ >= kMaxCreations) {
                if (ui) ui->add_chat_message(
                    "(the void is full — " +
                    std::to_string(kMaxCreations) + " creations)");
                break;
            }
            auto v = kg::validate_kg_op(op, kg, kg.getRegistry());
            if (!v.ok) {
                std::cerr << "[logogenesis] op rejected: " << v.reason
                          << std::endl;
                // Loud to the human AND to the model: the refusal
                // joins the conversation so the next turn corrects
                // itself (silent theater announced a redwood that
                // never grew).
                std::string line = "(the void resisted: " + v.reason + ")";
                chat_history_.push_back("System: " + line);
                if (ui) ui->add_chat_message(line);
                refusals_this_turn_.push_back(v.reason);
                continue;
            }
            kg::ApplyResult r;
            if (auto* del = std::get_if<kg::KGOpDestroyEntity>(&op)) {
                if (del->target.is_numeric()) {
                    std::string gone = kg.getType(del->target.id);
                    unmake_entity(kg, del->target.id);
                    r.ok = true;
                    // Plain receipt beside the poetry: destruction
                    // was illegible ("that light disappears???").
                    if (ui) ui->add_chat_message(
                        "(unmade: " + (gone.empty() ? "entity" : gone) +
                        " " + std::to_string(del->target.id) + ")");
                } else {
                    r.ok = false;
                    r.reason = "destroy_entity: unresolved target";
                }
            } else {
                r = kg::apply_kg_op(op, kg);
            }
            if (r.ok) ++applied;
            std::cerr << "[logogenesis] op " << (r.ok ? "applied: " : "failed: ")
                      << kg::kg_op_kind_name(op) << std::endl;
        }
    }

    static std::string extract_thoughts(const std::string& raw) {
        auto k = raw.find("\"thoughts\"");
        if (k == std::string::npos) return {};
        auto q1 = raw.find('"', raw.find(':', k) + 1);
        if (q1 == std::string::npos) return {};
        std::string out;
        for (size_t i = q1 + 1; i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '\\' && i + 1 < raw.size()) { out += raw[++i]; continue; }
            if (c == '"') break;
            out += c;
        }
        return out;
    }

    // ----------------------------------------------------------- materializer
    // Seeds are requests; the world is grown. For each seed entity:
    // read its validated properties into the matching generator spec,
    // grow the real thing (deferred KG-native generators self-queue
    // activation), destroy the seed. The reconciler shape is
    // Logotron's roster lever generalized — the engine-bridge
    // candidate the learnings ledger tracks.
    void materialize_seeds(kg::KGModule& kg) {
        auto& wg = engine_->get_worldgen_system();

        for (auto seed : kg.findByType("OrbitSeed")) {
            float revs = prop_f(kg, seed, "revolutions", 1.0f);
            float dur = prop_f(kg, seed, "duration_seconds", 12.0f);
            kg.destroyEntity(seed);
            orbit_base_ = engine_->get_camera_system().get_view_azimuth();
            orbit_total_ = revs * 6.2831853f;
            orbit_duration_ = dur;
            orbit_t_ = 0.0f;
            orbit_active_ = true;
        }

        for (auto seed : kg.findByType("SunSeed")) {
            float wish_hour = prop_f(kg, seed, "time_of_day", -1.0f);
            kg.destroyEntity(seed);
            if (sun_made_) {
                // Repeat sun requests just steer the existing sky.
                if (wish_hour >= 0.0f && sky_entity_ != kg::INVALID_ENTITY)
                    kg.setProperty(sky_entity_, "time_of_day",
                                   std::to_string(wish_hour));
                continue;
            }
            sun_made_ = true;
            Celestial::CelestialBodyConfig sun;
            sun.name = "sun";
            sun.orbit.period_days = 1.0f;
            // CelestialSystem convention: phase 0 = midnight, 0.25 =
            // sunrise, 0.5 = noon, 0.75 = sunset — phase IS the day
            // fraction; no offset. (A 0.75 offset once put the sun at
            // the dawn horizon at clock-noon: dark world at midday.)
            sun.orbit.phase_offset = 0.0f;
            // The sun is a PARTICLE, far enough to never enter the
            // frame (the invariant: everything is a particle, no sky
            // layer, no cheating). Only its light arrives; emission
            // is scaled by distance^2 so noon lands ~28k lux.
            sun.orbit.distance = 300.0f;
            sun.orbit.inclination_deg = 60.0f;
            sun.visual_radius = 8.0f;
            sun.color_curve = {
                {0.00f, {0.10f, 0.08f, 0.15f, 1.0f}},   // midnight
                {0.22f, {1.00f, 0.55f, 0.25f, 1.0f}},   // dawn amber
                {0.32f, {1.00f, 0.90f, 0.70f, 1.0f}},   // morning
                {0.50f, {1.00f, 0.98f, 0.92f, 1.0f}},   // noon white
                {0.68f, {1.00f, 0.80f, 0.50f, 1.0f}},   // late gold
                {0.78f, {1.00f, 0.45f, 0.15f, 1.0f}},   // sunset fire
                {0.85f, {0.60f, 0.20f, 0.10f, 1.0f}},   // afterglow
                {0.95f, {0.10f, 0.08f, 0.15f, 1.0f}},   // night
            };
            sun.emission_curve = {
                {0.00f, 0.0f},
                {0.20f, 0.0f},
                {0.25f, 1200000000.0f},    // dawn
                {0.50f, 5200000000.0f},    // full noon (~58k lux at 300 m)
                {0.75f, 1700000000.0f},    // sunset glow
                {0.82f, 0.0f},
                {1.00f, 0.0f},
            };
            engine_->get_celestial_system().add_body(sun);

            // Three stars: far particles (the invariant holds — no
            // sky layers), parked on effectively infinite orbits so
            // they never move, giving the night a residual glow
            // instead of pitch black. z > 0 needs phase in
            // (0.25, 0.75) under the midnight-at-0 convention.
            struct StarSpec { float phase, longitude, incl; };
            for (auto st : {StarSpec{0.36f, 25.0f, 55.0f},
                            StarSpec{0.50f, -40.0f, 70.0f},
                            StarSpec{0.64f, 80.0f, 45.0f}}) {
                Celestial::CelestialBodyConfig star;
                star.name = "star";
                star.orbit.period_days = 100000.0f;   // non-rotating
                star.orbit.phase_offset = st.phase;
                star.orbit.distance = 420.0f;
                star.orbit.inclination_deg = st.incl;
                star.orbit.longitude_deg = st.longitude;
                star.visual_radius = 2.5f;
                star.cast_shadows = false;
                star.color_curve = {{0.0f, {0.75f, 0.80f, 1.00f, 1.0f}}};
                // ~90 lux each at 420 m: enough to read shapes at
                // night, nowhere near daylight.
                star.emission_curve = {{0.0f, 16000000.0f}};
                star.dim_below_horizon = false;
                engine_->get_celestial_system().add_body(star);
            }

            // Earth's moon rides in with the sun (engine preset;
            // exotic/multiple moons come from MoonSeeds).
            engine_->get_celestial_system().add_body(Celestial::make_moon());
            ++moons_made_;

            // BIRTH IS INSTANT: a sun created WITH an hour starts
            // the world at that hour (the 40-second journey on
            // creation was wrong — journeys are for later wishes).
            if (wish_hour >= 0.0f) {
                double t = GameTime::get_current_time();
                double now_h =
                    GameTime::get_day_fraction(t) * 24.0;
                double ahead = wish_hour - now_h;
                if (ahead < 0) ahead += 24.0;
                GameTime::set_time(t + ahead * 3600.0);
            }
            sky_entity_ = kg.createEntity("Sky");
            double frac = GameTime::get_day_fraction(
                GameTime::get_current_time());
            kg.setProperty(sky_entity_, "time_of_day",
                           std::to_string(frac * 24.0));
            ++creations_;
        }

        for (auto seed : kg.findByType("MoonSeed")) {
            Celestial::MoonSpec spec;   // Earth silver by default
            float r = prop_f(kg, seed, "moon_r", -1.0f);
            float g = prop_f(kg, seed, "moon_g", -1.0f);
            float b = prop_f(kg, seed, "moon_b", -1.0f);
            if (r >= 0 && g >= 0 && b >= 0) {
                spec.r = r; spec.g = g; spec.b = b;
            }
            spec.brightness = prop_f(kg, seed, "moon_brightness", 1.0f);
            spec.size = prop_f(kg, seed, "moon_size", 5.0f);
            kg.destroyEntity(seed);
            if (moons_made_ >= 3) {
                if (auto* ui2 = engine_->get_ui_system())
                    ui2->add_chat_message(
                        "(the night holds three moons at most)");
                continue;
            }
            // Stagger multiple moons around the night sky.
            static const float kMoonPhases[3] = {0.5f, 0.38f, 0.62f};
            spec.phase_offset = kMoonPhases[moons_made_ % 3];
            engine_->get_celestial_system().add_body(
                Celestial::make_moon(spec));
            ++moons_made_;
            ++creations_;
        }

        // GROUND FIRST: surface seeds (trees, grass, rocks,
        // people) gate on ground_jobs_; the job must exist before
        // their loops run or a same-breath person materializes at
        // z=0 and the earth pours ON TOP of them (the buried-
        // wanderer AT find).
        for (auto seed : kg.findByType("GroundSeed")) {
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            float w = prop_f(kg, seed, "ground_width", 60.0f);
            float d = prop_f(kg, seed, "ground_depth", 60.0f);
            float r = prop_f(kg, seed, "ground_r", 0.85f);
            float g = prop_f(kg, seed, "ground_g", 0.87f);
            float b = prop_f(kg, seed, "ground_b", 0.85f);
            std::string terrain = kg.getProperty(seed, "terrain");
            kg.destroyEntity(seed);
            auto ent = kg.createEntity("Floor");
            kg.setProperty(ent, "x", std::to_string(x));
            kg.setProperty(ent, "y", std::to_string(y));
            kg.setProperty(ent, "ground_r", std::to_string(r));
            kg.setProperty(ent, "ground_g", std::to_string(g));
            kg.setProperty(ent, "ground_b", std::to_string(b));
            kg.setProperty(ent, "terrain",
                           terrain == "SLAB" ? "SLAB" : "LAYERED");
            if (terrain == "SLAB") {
                Particle p{};
                p.shape = ParticleShape::BOX;
                p.x = x; p.y = y;
                p.z = -0.06f + 0.02f * static_cast<float>(grounds_made_);
                p.width = w; p.height = d; p.thickness = 0.1f;
                p.r = r; p.g = g; p.b = b; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                p.owner = ParticleOwner::STATIC;
                p.is_at_rest = true;
                engine_->get_particle_system().add_particle_to_entity(p, &kg,
                                                                      ent);
            } else {
                // LAYERED (default): real earth poured in and settled by
                // the app's own frames. Budget is policy — cap the
                // footprint, never the honesty.
                if (w * d > 1600.0f) {
                    float scale = std::sqrt(1600.0f / (w * d));
                    w *= scale; d *= scale;
                    auto* ui2 = engine_->get_ui_system();
                    if (ui2) ui2->add_chat_message(
                        "(the earth holds " + std::to_string((int)w) + "x" +
                        std::to_string((int)d) + " m of true ground)");
                }
                GroundJob job;
                job.floor_entity = ent;
                job.specs = StrataGenerator::earth_preset();
                // The wish's color tints the topsoil (blend, not
                // replace — soil stays soil under the paint).
                auto& top = job.specs.back();
                top.r = top.r * 0.35f + r * 0.65f;
                top.g = top.g * 0.35f + g * 0.65f;
                top.b = top.b * 0.35f + b * 0.65f;
                job.bounds = {x - w * 0.5f, x + w * 0.5f,
                              y - d * 0.5f, y + d * 0.5f};
                job.rng.seed(static_cast<unsigned>(seed) * 2654435761u + 3u);
                start_next_layer(job);
                ground_jobs_.push_back(std::move(job));
            }
            ++grounds_made_;
            ++creations_;
        }

        for (auto seed : kg.findByType("ButterflySeed")) {
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            int count = static_cast<int>(prop_f(kg, seed, "count", 4));
            count = std::max(1, std::min(12, count));
            float span = prop_f(kg, seed, "wing_span", 0.0f);
            float wr = prop_f(kg, seed, "wing_r", -1.0f);
            float wg = prop_f(kg, seed, "wing_g", -1.0f);
            float wb = prop_f(kg, seed, "wing_b", -1.0f);
            unsigned rng = static_cast<unsigned>(seed) * 2654435761u;
            kg.destroyEntity(seed);
            auto& bgen =
                engine_->get_worldgen_system().get_butterfly_generator();
            for (int i = 0; i < count; ++i) {
                rng = rng * 1664525u + 1013904223u;
                ButterflySpec spec = (rng >> 8) % 2 == 0
                                         ? ButterflySpec::monarch()
                                         : ButterflySpec::blue_morpho();
                rng = rng * 1664525u + 1013904223u;
                float jitter = 0.7f + 0.6f * ((rng >> 8) % 1000) / 1000.0f;
                spec.wing_span *= jitter;
                spec.wing_height *= jitter;
                if (span > 0.0f) spec.wing_span = span;
                if (wr >= 0 && wg >= 0 && wb >= 0) {
                    spec.wing_r = wr; spec.wing_g = wg; spec.wing_b = wb;
                    spec.segment_wing_colors.clear();
                }
                rng = rng * 1664525u + 1013904223u;
                float ox = -3.0f + 6.0f * ((rng >> 8) % 1000) / 1000.0f;
                rng = rng * 1664525u + 1013904223u;
                float oy = -3.0f + 6.0f * ((rng >> 8) % 1000) / 1000.0f;
                rng = rng * 1664525u + 1013904223u;
                float oz = 1.5f + 2.5f * ((rng >> 8) % 1000) / 1000.0f;
                auto b = bgen.generate_butterfly(x + ox, y + oy, oz, spec);
                if (b != kg::INVALID_ENTITY) {
                    engine_->get_butterfly_flight().register_butterfly(b);
                    ++creations_;
                }
            }
        }

        for (auto seed : kg.findByType("HumanoidSeed")) {
            if (!ground_jobs_.empty()) break;   // wait for the earth
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            HumanoidSpec spec = HumanoidSpec::default_human();
            spec.apply_natural_variation(0.12f,
                                         static_cast<unsigned>(seed) * 131u);
            read_rgb(kg, seed, "cloth", spec.clothing_r, spec.clothing_g,
                     spec.clothing_b);
            kg.destroyEntity(seed);
            // The proven rig recipe (test_humanoid_strata_walk): physics
            // body, KG entity with body-part structure, locomotion
            // registration. floor_particle_id is dead API — pass -1.
            auto body = wg.get_humanoid_generator().generate_humanoid_physics(
                x, y, ground_top_z_ + 0.1f, -1, spec, false);
            body.create_kg_entities(kg, "Humanoid", 180.0f, 800.0f);
            engine_->get_humanoid_locomotion().register_humanoid_direct(
                body.hips_id, body.left_leg_ids, body.right_leg_ids,
                body.left_arm_ids, body.right_arm_ids, body.torso_ids,
                180.0f, 800.0f);
            kg.setProperty(body.entity_id, "x", std::to_string(x));
            kg.setProperty(body.entity_id, "y", std::to_string(y));
            // Particle-index swaps (chunk churn, deletions) must not
            // orphan the drivers: one callback remaps every wanderer.
            if (!wander_swap_hooked_) {
                wander_swap_hooked_ = true;
                engine_->get_particle_system().add_swap_callback(
                    [this](size_t old_idx, size_t new_idx) {
                        for (auto& w : wanderers_)
                            if (w.hips == static_cast<int>(old_idx))
                                w.hips = static_cast<int>(new_idx);
                    });
            }
            Wanderer w;
            w.entity = body.entity_id;
            w.hips = body.hips_id;
            w.home_x = x;
            w.home_y = y;
            w.idle_left = 1.0f;   // birth grace: settle before strolling
            w.rng = static_cast<uint32_t>(seed) * 2654435761u + 12345u;
            wanderers_.push_back(w);
            ++creations_;
        }

        for (auto seed : kg.findByType("LightSeed")) {
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            float h = prop_f(kg, seed, "light_height", 38.0f);
            float brightness = prop_f(kg, seed, "brightness", 0.6f);
            float radius = prop_f(kg, seed, "light_radius", 300.0f);
            float r = prop_f(kg, seed, "light_r", 1.0f);
            float g = prop_f(kg, seed, "light_g", 0.95f);
            float b = prop_f(kg, seed, "light_b", 0.85f);
            kg.destroyEntity(seed);
            auto ent = kg.createEntity("LightSource");
            kg.setProperty(ent, "x", std::to_string(x));
            kg.setProperty(ent, "y", std::to_string(y));
            // Physical lumens (Eden calibration: 2e6 floods a stage).
            engine_->get_particle_system().create_light_for_entity(
                x, y, h, brightness * 2000000.0f, radius, r, g, b, &kg, ent);
            ++creations_;
        }

        for (auto seed : kg.findByType("TreeSeed")) {
            if (!ground_jobs_.empty()) break;   // wait for the earth
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            auto species = kg.getProperty(seed, "species");
            TreeSpec spec = spec_for_species(species);
            if (auto h = prop_opt(kg, seed, "tree_height")) spec.height = *h;
            if (auto c = prop_opt(kg, seed, "crown_radius")) spec.crown_radius = *c;
            if (auto cs = prop_opt(kg, seed, "canopy_start"))
                spec.canopy_start = *cs;
            if (auto cd = prop_opt(kg, seed, "canopy_density"))
                spec.canopy_density = *cd;
            if (auto lbn = prop_opt(kg, seed, "lower_branches")) {
                spec.lower_branch_count = static_cast<int>(*lbn);
            } else if (species == "OAK" || species == "WILLOW") {
                spec.lower_branch_count = 2;   // species character
            }
            read_rgb(kg, seed, "trunk", spec.trunk_r, spec.trunk_g, spec.trunk_b);
            read_rgb(kg, seed, "leaf", spec.leaf_r, spec.leaf_g, spec.leaf_b);
            spec.random_seed = static_cast<int>(seed) * 7919;
            // Taller trees need wider crowns or space colonization
            // starves (the height-40 ghost-tree eval find).
            if (spec.crown_radius < spec.height * 0.18f)
                spec.crown_radius = spec.height * 0.18f;
            float grow_s = prop_f(kg, seed, "grow_seconds", 0.0f);
            kg.destroyEntity(seed);
            if (grow_s > 0.0f) {
                // Time-lapse: the tree is born a sapling and regrows
                // in stages toward the full spec. Same random_seed
                // every stage — the sapling and the tree are the same
                // individual at different ages.
                GrowthJob job;
                job.x = x;
                job.y = y;
                job.z = ground_top_z_;
                job.final_spec = spec;
                job.species = species;
                job.n_stages = std::min(24, std::max(4,
                    static_cast<int>(grow_s * 2.0f)));
                job.interval = grow_s / static_cast<float>(job.n_stages);
                job.stage = 1;
                job.current_tree = grow_tree_stage(kg, job);
                growth_jobs_.push_back(job);
            } else {
                spawn_tree(kg, spec, x, y, ground_top_z_, species,
                           /*collapse_retry=*/true);
            }
            ++creations_;
        }

        for (auto seed : kg.findByType("GrassSeed")) {
            if (!ground_jobs_.empty()) break;   // wait for the earth
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            // tall_grass: 80 cm blades read at stage zoom (short_grass
            // is 15 cm — single pixels; the sprinkle-not-carpet RCA).
            GrassPatchSpec spec = GrassPatchSpec::tall_grass();
            if (auto w = prop_opt(kg, seed, "patch_width")) spec.patch_width = *w;
            if (auto d = prop_opt(kg, seed, "patch_depth")) spec.patch_depth = *d;
            if (auto n = prop_opt(kg, seed, "blade_count"))
                spec.blade_count = static_cast<int>(*n);
            // The painter's brush: SOFT (Gaussian dab) unless the
            // request says SQUARE.
            spec.distribution = kg.getProperty(seed, "spread") == "SQUARE"
                                    ? DistributionType::UNIFORM
                                    : DistributionType::CLUSTERED;
            // The organicalizer: one knob mapping onto height + hue
            // variance (silhouette lobes ride CLUSTERED already).
            float organic = prop_f(kg, seed, "organic", 0.6f);
            spec.height_variance = 0.12f + 0.33f * organic;
            spec.color_variance = 0.03f + 0.12f * organic;
            // Density is policy: a carpet means >=2 blades/m2. Raise
            // the count to match the area; if that busts the budget,
            // shrink the patch (the void grants density, not sprawl).
            {
                float area = spec.patch_width * spec.patch_depth;
                int want = static_cast<int>(area * 2.0f);
                if (spec.blade_count < want) spec.blade_count = want;
                if (spec.blade_count > 1000) {
                    float scale = std::sqrt(500.0f / area);
                    spec.patch_width *= scale;
                    spec.patch_depth *= scale;
                    spec.blade_count = 1000;
                }
            }
            // Blades render mostly from STEM color; write the request
            // to both stem and foliage or the carpet comes out brown.
            read_rgb(kg, seed, "blade", spec.blade_spec.stem_r,
                     spec.blade_spec.stem_g, spec.blade_spec.stem_b);
            read_rgb(kg, seed, "blade", spec.blade_spec.foliage_r,
                     spec.blade_spec.foliage_g, spec.blade_spec.foliage_b);
            // CHEAP BLADES (particle budget, 2026-07-31 spike): a
            // carpet blade at stage zoom needs 2-3 particles, not the
            // full articulated 16 (112k particles / 5 FPS playtest).
            // Two long segments + one foliage tip per blade.
            spec.blade_spec.segment_length = spec.blade_spec.height * 1.1f;
            spec.blade_spec.max_iterations = 1;
            spec.blade_spec.attractor_count = 2;
            spec.blade_spec.foliage_count_min = 0;
            spec.blade_spec.foliage_count_max = 0;
            kg.destroyEntity(seed);
            wg.get_organic_generator().generate_grass_patch(
                x, y, ground_top_z_, spec);
            ++creations_;
        }

        for (auto seed : kg.findByType("RockSeed")) {
            if (!ground_jobs_.empty()) break;   // wait for the earth
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            float drop = prop_f(kg, seed, "drop_height", 0.0f);
            float size = prop_f(kg, seed, "rock_size", 0.8f);
            if (drop > 0.0f) {
                // A FALLING rock is a PHYSICS rock: gluon-bonded boxes
                // (it must hold together while tumbling), born awake,
                // immediate mode — gravity takes it from here. Scenery
                // rocks stay on the deferred path below (their
                // activator forces sleep by design).
                PhysicsRockSpec pspec =
                    size > 1.1f ? PhysicsRockSpec::large_boulder()
                                : PhysicsRockSpec::medium_rock();
                pspec.size = size;
                read_rgb(kg, seed, "rock", pspec.r, pspec.g, pspec.b);
                kg.destroyEntity(seed);
                auto res = wg.get_physics_rock_generator().generate_rock(
                    x, y, ground_top_z_ + drop, pspec);
                auto ent = kg.createEntity("Rock");
                kg.setProperty(ent, "x", std::to_string(x));
                kg.setProperty(ent, "y", std::to_string(y));
                kg.setProperty(ent, "rock_size", std::to_string(size));
                kg.setProperty(ent, "drop_height", std::to_string(drop));
                for (int pid : res.box_ids)
                    kg.createKGParticle(ent, static_cast<unsigned>(pid));
                ++creations_;
                continue;
            }
            RockSpec spec = RockSpec::medium_rock();
            if (auto s = prop_opt(kg, seed, "rock_size")) spec.size = *s;
            read_rgb(kg, seed, "rock", spec.base_r, spec.base_g, spec.base_b);
            kg.destroyEntity(seed);
            wg.get_rock_generator().generate_rock(x, y, ground_top_z_, spec);
            ++creations_;
        }

        for (auto seed : kg.findByType("SerpentSeed")) {
            if (!ground_jobs_.empty()) break;   // wait for the earth
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            std::string kind = prop_s(kg, seed, "serpent_kind", "GARDEN");
            SnakeSpec spec = kind == "PYTHON" ? SnakeSpec::python()
                           : kind == "CORAL"  ? SnakeSpec::coral_snake()
                                              : SnakeSpec::garden_snake();
            if (auto l = prop_opt(kg, seed, "serpent_length"))
                spec.total_length = *l;
            read_rgb(kg, seed, "scale", spec.scale_r, spec.scale_g,
                     spec.scale_b);
            kg.destroyEntity(seed);
            wg.get_snake_generator().generate_snake(x, y, ground_top_z_,
                                                    spec);
            ++creations_;
        }

        for (auto seed : kg.findByType("FallenTreeSeed")) {
            if (!ground_jobs_.empty()) break;   // wait for the earth
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            std::string kind = prop_s(kg, seed, "deadwood_kind", "BRANCH");
            FallenTreeSpec spec;
            spec.type = kind == "TRUNK" ? FallenTreeSpec::FallenType::TRUNK
                      : kind == "LOG"   ? FallenTreeSpec::FallenType::LOG
                      : kind == "TWIG"  ? FallenTreeSpec::FallenType::TWIG
                                        : FallenTreeSpec::FallenType::BRANCH;
            if (auto l = prop_opt(kg, seed, "deadwood_length"))
                spec.length = *l;
            read_rgb(kg, seed, "bark", spec.bark_r, spec.bark_g,
                     spec.bark_b);
            kg.destroyEntity(seed);
            wg.get_fallen_tree_generator().generate_fallen_tree(
                x, y, ground_top_z_, spec);
            ++creations_;
        }

        for (auto seed : kg.findByType("TotemSeed")) {
            if (!ground_jobs_.empty()) break;   // wait for the earth
            float x = prop_f(kg, seed, "x", 0), y = prop_f(kg, seed, "y", 0);
            TotemSpec spec;
            if (auto sz = prop_opt(kg, seed, "totem_size")) {
                spec.trunk_size = *sz;
                spec.upper_trunk_size = *sz * 0.8f;
            }
            read_rgb(kg, seed, "totem", spec.r, spec.g, spec.b);
            kg.destroyEntity(seed);
            wg.get_totem_generator().generate_totem(x, y, ground_top_z_,
                                                    spec);
            ++creations_;
        }
    }

    static TreeSpec spec_for_species(const std::string& s) {
        if (s == "PINE") return TreeSpec::pine();
        if (s == "WILLOW") return TreeSpec::willow();
        if (s == "PALM") return TreeSpec::palm();
        if (s == "BAOBAB") return TreeSpec::baobab();
        if (s == "REDWOOD") {
            TreeSpec t = TreeSpec::ancient_oak();
            t.height = 28.0f;
            t.crown_radius = 5.0f;
            t.trunk_r = 0.48f; t.trunk_g = 0.20f; t.trunk_b = 0.14f;
            return t;
        }
        return TreeSpec::oak();
    }

    // ------------------------------------------------------------------ ground
    // Layered earth pours in across frames: spawn a layer, let normal
    // updates settle it, bond, next. Surface-dependent seeds (trees,
    // grass, rocks, people) wait until the ground finishes forming.
    struct GroundJob {
        kg::EntityID floor_entity = kg::INVALID_ENTITY;
        std::vector<StrataGenerator::LayerSpec> specs;
        StrataGenerator::ChunkBounds bounds{};
        std::mt19937 rng;
        int layer_idx = -1;
        StrataGenerator::LayerResult layer;
        std::vector<int> all_pids;
        float base_z = 0.0f;
        int frames_in_layer = 0;
    };

    void start_next_layer(GroundJob& job) {
        ++job.layer_idx;
        job.frames_in_layer = 0;
        job.layer = StrataGenerator::spawn_layer(
            *engine_, job.specs[job.layer_idx], job.bounds, job.base_z,
            job.rng);
    }

    void tick_ground(kg::KGModule& kg) {
        for (auto it = ground_jobs_.begin(); it != ground_jobs_.end();) {
            auto& job = *it;
            const auto& spec = job.specs[job.layer_idx];
            ++job.frames_in_layer;
            size_t rest = 0;
            bool settled = StrataGenerator::layer_at_rest(
                *engine_, job.layer.particle_ids, rest);
            if (!settled && job.frames_in_layer < spec.max_settle_frames) {
                ++it;
                continue;
            }
            StrataGenerator::bond_layer(*engine_, job.layer, spec);
            job.base_z = StrataGenerator::layer_top_z(*engine_,
                                                      job.layer.particle_ids);
            job.all_pids.insert(job.all_pids.end(),
                                job.layer.particle_ids.begin(),
                                job.layer.particle_ids.end());
            if (job.layer_idx + 1 <
                static_cast<int>(job.specs.size())) {
                start_next_layer(job);
                ++it;
                continue;
            }
            // The earth is formed: bind the matter to the Floor entity
            // (destroy_entity can unmake it) and raise the world surface.
            for (int pid : job.all_pids)
                kg.createKGParticle(job.floor_entity,
                                    static_cast<unsigned>(pid));
            ground_top_z_ = std::max(ground_top_z_, job.base_z);
            auto* ui = engine_->get_ui_system();
            if (ui) ui->add_chat_message(
                "(the earth settles: " + std::to_string(job.all_pids.size()) +
                " stones deep)");
            std::cerr << "[logogenesis] ground formed: "
                      << job.all_pids.size() << " particles, surface z="
                      << ground_top_z_ << std::endl;
            it = ground_jobs_.erase(it);
        }
    }

    // ------------------------------------------------------------------ growth
    // Time-lapse state: one job per tree born with grow_seconds > 0.
    struct GrowthJob {
        float x = 0, y = 0, z = 0;
        TreeSpec final_spec;
        std::string species;
        kg::EntityID current_tree = kg::INVALID_ENTITY;
        int stage = 0;         // stages generated so far (1-based)
        int n_stages = 0;
        float interval = 0;    // seconds between stages
        float accum = 0;
    };

    // Generate + collapse-retry + anatomy stamp: the single path every
    // tree takes into the world, full-grown or one growth stage.
    kg::EntityID spawn_tree(kg::KGModule& kg, TreeSpec spec, float x,
                            float y, float z, const std::string& species,
                            bool collapse_retry) {
        auto& tg = engine_->get_worldgen_system().get_tree_generator();
        auto tree = tg.generate_tree_space_colonization(x, y, z, spec);
        // Collapse detection: the SC path stores ~5 trunk particles
        // even when the skeleton comes out empty, so a failed tree is
        // a bare pole, not an empty entity. Under 12 particles =
        // collapsed; retry once at the proven envelope. Growth stages
        // skip this — a knee-high sapling is LEGITIMATELY under 12.
        if (collapse_retry && tree != kg::INVALID_ENTITY &&
            kg.getEntityKGParticles(tree).size() < 12) {
            kg.destroyEntity(tree);
            spec.height = std::min(spec.height, 25.0f);
            spec.crown_radius = 5.0f;
            spec.random_seed += 1;
            tree = tg.generate_tree_space_colonization(x, y, z, spec);
        }
        // Stamp the realized anatomy on the Tree entity: the World
        // block then tells the LLM what it actually made ("understand
        // what you are creating").
        if (tree != kg::INVALID_ENTITY) {
            // x/y included: without them the World block showed trees
            // with no position, so spatial wishes ("below the redwood",
            // "next to the oak") had nothing to resolve against.
            kg.setProperty(tree, "x", std::to_string(x));
            kg.setProperty(tree, "y", std::to_string(y));
            if (!species.empty()) kg.setProperty(tree, "species", species);
            kg.setProperty(tree, "tree_height", std::to_string(spec.height));
            kg.setProperty(tree, "crown_radius",
                           std::to_string(spec.crown_radius));
            if (spec.canopy_start >= 0.0f)
                kg.setProperty(tree, "canopy_start",
                               std::to_string(spec.canopy_start));
        }
        return tree;
    }

    // One stage of a tree's life: the full spec scaled to this age.
    // The whole tree scales — trunk, crown, limbs — because a real
    // sapling is a small tree, not a truncated big one.
    kg::EntityID grow_tree_stage(kg::KGModule& kg, const GrowthJob& job) {
        float f = static_cast<float>(job.stage) /
                  static_cast<float>(job.n_stages);
        TreeSpec s = job.final_spec;
        s.height = std::max(0.6f, s.height * f);
        s.crown_radius = std::max(s.crown_radius * f, s.height * 0.18f);
        return spawn_tree(kg, s, job.x, job.y, job.z, job.species,
                          /*collapse_retry=*/job.stage == job.n_stages);
    }

    // ---------------------------------------------------------------- wander
    // A person's driver: pick a spot in the home meadow, stroll there,
    // linger, pick another. Policy lives here (game side); the engine
    // provides the rig, gait, and set_target_velocity.
    struct Wanderer {
        kg::EntityID entity = kg::INVALID_ENTITY;
        int hips = -1;             // swap-remapped particle index
        float home_x = 0, home_y = 0;
        float tx = 0, ty = 0;      // current stroll target
        float idle_left = 0;       // lingering when > 0
        uint32_t rng = 1;
    };
    static constexpr float kStrollSpeed = 1.2f;    // m/s, unhurried
    static constexpr float kWanderRadius = 12.0f;  // around home

    static float wander_rand01(uint32_t& s) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / 16777216.0f;
    }

    void tick_wanderers(kg::KGModule& kg, float dt) {
        auto& loco = engine_->get_humanoid_locomotion();
        for (auto& w : wanderers_) {
            if (w.idle_left > 0.0f) {
                w.idle_left -= dt;
                if (w.idle_left > 0.0f) continue;
                w.tx = w.home_x +
                       (wander_rand01(w.rng) * 2.0f - 1.0f) * kWanderRadius;
                w.ty = w.home_y +
                       (wander_rand01(w.rng) * 2.0f - 1.0f) * kWanderRadius;
            }
            float px, py;
            {
                auto particles =
                    engine_->get_particle_system().lock_particles_for_read();
                px = particles[w.hips].x;
                py = particles[w.hips].y;
            }
            float dx = w.tx - px, dy = w.ty - py;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < 0.7f) {
                loco.set_target_velocity(w.hips, 0.0f, 0.0f);
                w.idle_left = 2.0f + wander_rand01(w.rng) * 4.0f;
                // Resting spot goes to the World block: the LLM sees
                // where its people actually are, not where they began.
                kg.setProperty(w.entity, "x", std::to_string(px));
                kg.setProperty(w.entity, "y", std::to_string(py));
            } else {
                float s = kStrollSpeed / d;
                loco.set_target_velocity(w.hips, dx * s, dy * s);
            }
        }
    }

    void tick_growth(kg::KGModule& kg, float dt) {
        for (auto it = growth_jobs_.begin(); it != growth_jobs_.end();) {
            auto& job = *it;
            // The god may have unmade it mid-growth (destroy_entity):
            // a dropped job, never a resurrection.
            auto trees = kg.findByType("Tree");
            if (std::find(trees.begin(), trees.end(), job.current_tree) ==
                trees.end()) {
                it = growth_jobs_.erase(it);
                continue;
            }
            job.accum += dt;
            if (job.accum >= job.interval && job.stage < job.n_stages) {
                job.accum -= job.interval;
                unmake_entity(kg, job.current_tree);
                ++job.stage;
                job.current_tree = grow_tree_stage(kg, job);
            }
            if (job.stage >= job.n_stages)
                it = growth_jobs_.erase(it);
            else
                ++it;
        }
    }

    // ---------------------------------------------------------------- helpers
    static std::string prop_s(kg::KGModule& kg, kg::EntityID e,
                              const char* k, const char* dflt) {
        auto v = kg.getProperty(e, k);
        return v.empty() ? std::string(dflt) : v;
    }
    static float prop_f(kg::KGModule& kg, kg::EntityID e, const char* k,
                        float dflt) {
        auto v = kg.getProperty(e, k);
        if (v.empty()) return dflt;
        try { return std::stof(v); } catch (...) { return dflt; }
    }
    static std::optional<float> prop_opt(kg::KGModule& kg, kg::EntityID e,
                                         const char* k) {
        auto v = kg.getProperty(e, k);
        if (v.empty()) return std::nullopt;
        try { return std::stof(v); } catch (...) { return std::nullopt; }
    }
    static void read_rgb(kg::KGModule& kg, kg::EntityID e,
                         const std::string& prefix, float& r, float& g,
                         float& b) {
        if (auto v = prop_opt(kg, e, (prefix + "_r").c_str())) r = *v;
        if (auto v = prop_opt(kg, e, (prefix + "_g").c_str())) g = *v;
        if (auto v = prop_opt(kg, e, (prefix + "_b").c_str())) b = *v;
    }

    Engine* engine_ = nullptr;
    std::unique_ptr<Logosphere::LLMSystemHTTP> llm_;
    bool llm_configured_ = false;
    int offline_plants_ = 0;
    int grounds_made_ = 0;
    bool sun_made_ = false;
    int moons_made_ = 0;
    std::vector<GrowthJob> growth_jobs_;
    std::vector<GroundJob> ground_jobs_;
    float ground_top_z_ = 0.0f;   // world surface: things stand HERE
    // ------------------------------------------------------------ orbit
    // The engine provides the azimuth parameter; the ANIMATION is
    // game policy (this tween). Smoothstep ease, exact landing.
    void tick_orbit(float dt) {
        orbit_t_ += dt;
        float u = orbit_duration_ > 0.0f ? orbit_t_ / orbit_duration_ : 1.0f;
        if (u >= 1.0f) {
            engine_->get_camera_system().set_view_azimuth(
                orbit_base_ + orbit_total_);
            orbit_active_ = false;
            return;
        }
        float ease = u * u * (3.0f - 2.0f * u);   // smoothstep
        engine_->get_camera_system().set_view_azimuth(
            orbit_base_ + orbit_total_ * ease);
    }

    bool orbit_active_ = false;
    float orbit_t_ = 0.0f;
    float orbit_duration_ = 0.0f;
    float orbit_total_ = 0.0f;
    float orbit_base_ = 0.0f;

    std::vector<Wanderer> wanderers_;
    bool wander_swap_hooked_ = false;
    kg::EntityID sky_entity_ = kg::INVALID_ENTITY;
    bool time_accelerating_ = false;
    float sky_sync_accum_ = 0.0f;
    std::vector<std::string> refusals_this_turn_;
    bool retry_used_ = false;
    std::function<void(const std::string&, const std::string&,
                       std::function<void(std::string)>)> responder_;
    std::atomic<bool> request_pending_{false};
    std::mutex mu_;
    std::string response_;
    bool has_response_ = false;
    std::vector<std::string> chat_history_;
    std::string last_thoughts_;
    size_t creations_ = 0;
};

}  // namespace logogenesis
