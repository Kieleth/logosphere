// Headless tests for the Weirden Director's KG-side primitives.
//
// Today: respawn_ai. As the Director grows (mutations, prompt builder,
// JSON parser, end-to-end loop) tests land here.

// Tests must assert in every build type. Release passes -DNDEBUG,
// which turns assert() into a no-op and made this suite green by
// vacuity (and abort where asserts carried side effects). Undef
// BEFORE any include so <cassert> re-expands assert() for real.
#undef NDEBUG

#include "director/respawn_ai.h"
#include "director/director_parser.h"
#include "director/director.h"
#include "director/random_director.h"
#include "llm_plan.h"

#include <map>

#include "arena.h"
#include "cycle.h"
#include "logotron_ontology_registry.h"
#include "generated/logosphere_ontology_registry.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/kg/ontology_serialize.h"

#include <cassert>
#include <cstdio>
#include <vector>

namespace lt = logotron;
namespace dir = logotron::director;

namespace {

void seed_world(kg::KGModule& kg,
                kg::EntityID& player,
                kg::EntityID& ai) {
    player = lt::spawn_cycle(kg, "PlayerCycle", 20.0f, 20.0f, lt::Direction::EAST);
    ai     = lt::spawn_cycle(kg, "AICycle",      5.5f,  5.5f, lt::Direction::NORTH);
}

void test_first_spawn_no_wipe() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.extendOntology(logotron::ontology::registry());

    auto result = dir::respawn_ai(kg, kg::INVALID_ENTITY,
                                   {7.0f, 7.0f, lt::Direction::SOUTH});

    assert(result.new_ai_entity != kg::INVALID_ENTITY);
    assert(result.trails_wiped == 0);
    assert(!result.old_cycle_destroyed);

    auto cyc = lt::read_cycle(kg, result.new_ai_entity);
    assert(cyc.x == 7.0f);
    assert(cyc.y == 7.0f);
    assert(cyc.direction == lt::Direction::SOUTH);
    assert(cyc.state == lt::CycleState::RIDING);

    std::printf("[PASS] first_spawn_no_wipe\n");
}

void test_respawn_wipes_old_ai_and_its_trails_only() {
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.extendOntology(logotron::ontology::registry());

    kg::EntityID player = kg::INVALID_ENTITY;
    kg::EntityID ai = kg::INVALID_ENTITY;
    seed_world(kg, player, ai);

    // Lay 3 player trails and 2 AI trails by stepping each cycle and
    // forcing a turn so freeze_run seals a TrailSegment.
    auto lay_trail = [&](kg::EntityID e, lt::Direction next_dir) {
        lt::step_cycle_in_kg(kg, e, 1.0f);
        auto c = lt::read_cycle(kg, e);
        c.direction = next_dir;
        lt::write_cycle(kg, e, c);
        lt::freeze_run(kg, e);
    };

    lay_trail(player, lt::Direction::SOUTH);
    lay_trail(player, lt::Direction::WEST);
    lay_trail(player, lt::Direction::NORTH);
    lay_trail(ai, lt::Direction::EAST);
    lay_trail(ai, lt::Direction::SOUTH);

    int player_trails_before = lt::count_trails_owned_by(kg, player);
    int ai_trails_before = lt::count_trails_owned_by(kg, ai);
    assert(player_trails_before == 3);
    assert(ai_trails_before == 2);

    auto result = dir::respawn_ai(kg, ai,
                                   {15.0f, 15.0f, lt::Direction::WEST});

    assert(result.old_cycle_destroyed);
    assert(result.trails_wiped == 2);
    assert(result.new_ai_entity != kg::INVALID_ENTITY);
    assert(result.new_ai_entity != ai);  // fresh entity id

    int player_trails_after = lt::count_trails_owned_by(kg, player);
    assert(player_trails_after == 3);  // untouched

    int new_ai_trails = lt::count_trails_owned_by(kg, result.new_ai_entity);
    assert(new_ai_trails == 0);  // fresh

    auto fresh = lt::read_cycle(kg, result.new_ai_entity);
    assert(fresh.x == 15.0f);
    assert(fresh.y == 15.0f);
    assert(fresh.direction == lt::Direction::WEST);
    assert(fresh.state == lt::CycleState::RIDING);

    std::printf("[PASS] respawn_wipes_old_ai_and_its_trails_only "
                "(player_trails preserved=%d, ai_trails wiped=%d)\n",
                player_trails_after, result.trails_wiped);
}

void test_respawn_after_crash() {
    // After the AI crashes, its cycle_state is CRASHED. respawn_ai
    // should still wipe it cleanly and produce a fresh RIDING cycle.
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.extendOntology(logotron::ontology::registry());

    kg::EntityID player = kg::INVALID_ENTITY;
    kg::EntityID ai = kg::INVALID_ENTITY;
    seed_world(kg, player, ai);

    auto c = lt::read_cycle(kg, ai);
    c.state = lt::CycleState::CRASHED;
    lt::write_cycle(kg, ai, c);

    auto result = dir::respawn_ai(kg, ai,
                                   {10.0f, 10.0f, lt::Direction::EAST});

    assert(result.new_ai_entity != kg::INVALID_ENTITY);
    auto fresh = lt::read_cycle(kg, result.new_ai_entity);
    assert(fresh.state == lt::CycleState::RIDING);

    std::printf("[PASS] respawn_after_crash (state=RIDING)\n");
}


void test_parser_bad_json_returns_error() {
    auto resp = dir::parse_director_json("not json at all");
    assert(!resp.parse_error.empty());
    assert(resp.kg_ops.empty());
    std::printf("[PASS] parser_bad_json_returns_error (%s)\n",
                resp.parse_error.c_str());
}

// Cloud LLMs (Anthropic, OpenAI, sometimes Qwen) wrap JSON output in
// ```json fences even when the prompt says "no markdown". The parser
// strips them by slicing first '{' to last '}'. Lock that.
void test_parser_strips_markdown_fences() {
    const char* fenced =
        "```json\n"
        "{\"thoughts\":\"x\","
        " \"ops\":[{\"op\":\"set_property\",\"target\":\"@program_1\","
        "\"property\":\"max_speed\",\"value\":\"10\"}]}\n"
        "```";
    auto resp = dir::parse_director_json(fenced);
    assert(resp.parse_error.empty());
    assert(resp.kg_ops.size() == 1);
    std::printf("[PASS] parser_strips_markdown_fences\n");
}

// Pre-fire (round_number == 0) renders a different opener than a
// real duel-N-ended fire. The Director's apply path also branches
// on this — the prompt has to land "FIRST duel" so the LLM knows
// to author the OPENING world, not react to a Program death.
void test_pre_fire_prompt_announces_first_duel() {
    dir::GameState pre;
    pre.round_number = 0;
    pre.ai_deaths    = 0;
    auto user = dir::build_director_user_prompt(pre);
    assert(user.find("FIRST duel") != std::string::npos);
    assert(user.find("Author the opening world") != std::string::npos);
    // And NOT the post-death opener:
    assert(user.find("derezzed Program") == std::string::npos);

    dir::GameState mid;
    mid.round_number = 3;
    mid.ai_deaths    = 3;
    auto user_mid = dir::build_director_user_prompt(mid);
    assert(user_mid.find("Duel 3") != std::string::npos);
    assert(user_mid.find("FIRST duel") == std::string::npos);
    std::printf("[PASS] pre_fire_prompt_announces_first_duel\n");
}

// The OntologyValidator gates every set_property the Director
// emits. If the prompt's ontology slice doesn't list a slot, the
// LLM either won't know it exists OR the apply will be rejected —
// and we'll see "rejected set_property" in the live HUD. The Arena
// schema gap (arena_w / arena_h missing on the Arena class) was a
// real bug that polluted every session's HUD; the schema fix
// declared both. This locks that the slice the Director sees ALSO
// includes them, so the LLM can author arena resizes.
void test_ontology_slice_advertises_arena_resize_slots() {
    kg::OntologyRegistry registry;
    registry.extend(logotron::ontology::registry());
    auto slice = kg::serialize_ontology_slice(
        registry, std::vector<std::string>{"Arena"});
    assert(slice.find("\"arena_w\"") != std::string::npos);
    assert(slice.find("\"arena_h\"") != std::string::npos);
    std::printf("[PASS] ontology_slice_advertises_arena_resize_slots\n");
}

void test_director_double_fire_rejected() {
    dir::Director d;
    // Responder that delays calling done — simulates pending LLM.
    std::function<void(std::string)> stash;
    d.set_responder([&stash](const std::string&, const std::string&,
                              std::function<void(std::string)> done) {
        stash = std::move(done);
    });
    dir::GameState s;
    assert(d.fire(s));
    assert(d.is_requesting());
    assert(!d.fire(s));  // second fire while pending should fail
    // Resolve the pending one.
    stash(R"({"ops":[{"op":"set_property","target":"@program_1","property":"max_speed","value":"10"}]})");
    assert(!d.is_requesting());
    dir::DirectorResponse out;
    assert(d.poll(out));
    std::printf("[PASS] director_double_fire_rejected\n");
}

void test_director_prompt_contains_state() {
    dir::GameState s;
    s.round_number = 7;
    s.ai_deaths = 3;
    s.arena_w = 32.0f;
    s.player_trail_count = 12;
    dir::GameState::RiderInfo ri;
    ri.ref = "@program_1";
    ri.personality = "kamikaze";
    ri.max_speed = 9.0f;
    ri.trail_count = 4;
    s.riders.push_back(ri);
    s.player_top_speed = 14.5f;
    s.player_turns = 23;
    s.death_log.push_back("Program #1 (kamikaze) derezzed by the User");
    auto user = dir::build_director_user_prompt(s);
    assert(user.find("Duel 7") != std::string::npos);
    assert(user.find("Program #3") != std::string::npos);
    assert(user.find("32") != std::string::npos);
    assert(user.find("kamikaze") != std::string::npos);
    // The Metrics + roster + death-log blocks the LLM decides from.
    assert(user.find("@program_1") != std::string::npos);
    assert(user.find("Metrics:") != std::string::npos);
    assert(user.find("user_top_speed_this_run=14.5") != std::string::npos);
    assert(user.find("user_turns_this_run=23") != std::string::npos);
    assert(user.find("Derezzes since your last intervention:") != std::string::npos);
    s.state_delta = "7 max_speed: 8 -> 12\n";
    auto user2 = dir::build_director_user_prompt(s);
    assert(user2.find("State changes since your last intervention") !=
           std::string::npos);
    assert(user2.find("7 max_speed: 8 -> 12") != std::string::npos);
    // Empty delta renders no block.
    assert(user.find("State changes since") == std::string::npos);
    auto sys = dir::build_director_system_prompt();
    assert(sys.find("MASTER CONTROL PROGRAM") != std::string::npos);
    // Wisdom pass: the prompt must teach the LLM to READ the field
    // report, not just list ops.
    assert(sys.find("THE FIELD REPORT IS YOUR EYES") != std::string::npos);
    assert(sys.find("NEVER repeat your previous intervention") !=
           std::string::npos);
    assert(sys.find("PURSUING") != std::string::npos);
    assert(sys.find("Tron-vernacular") != std::string::npos);
    assert(sys.find("create_entity") != std::string::npos);
    assert(sys.find("set_property") != std::string::npos);
    std::printf("[PASS] director_prompt_contains_state\n");
}

void test_director_prompt_contains_phase_b_blocks() {
    dir::GameState s;
    s.round_number = 9;
    s.ai_deaths = 4;
    s.narrative_hint    = "AI rammed director wall at (3.5, 12.0)";
    s.symbols_text      = "Symbols: @player_cycle=7 @program_1=11 @arena=3";
    s.ontology_slice    = R"({"Cycle":{"parent":"","abstract":false,"properties":{}}})";
    s.kg_snapshot       = R"({"id":7,"type":"Cycle","props":{"x":"3.5"}}
)";
    auto user = dir::build_director_user_prompt(s);

    // Per-round Phase B blocks land in the user prompt under their
    // labels. Ontology moved to the system prompt (caching) so it's
    // checked separately below.
    assert(user.find("Note: AI rammed director wall") != std::string::npos);
    assert(user.find("Symbols: @player_cycle=7") != std::string::npos);
    assert(user.find("World:\n{\"id\":7") != std::string::npos);
    assert(user.find("Ontology:") == std::string::npos);  // moved to system

    // Ontology embeds in the system prompt when supplied, so the
    // Anthropic prompt cache can keep it warm across rounds.
    auto sys_with_onto = dir::build_director_system_prompt(s.ontology_slice);
    assert(sys_with_onto.find("Ontology") != std::string::npos);
    assert(sys_with_onto.find("\"Cycle\"") != std::string::npos);

    // Empty fields must NOT emit empty labels.
    dir::GameState empty;
    empty.round_number = 1;
    auto bare = dir::build_director_user_prompt(empty);
    assert(bare.find("Note:")     == std::string::npos);
    assert(bare.find("Symbols:")  == std::string::npos);
    assert(bare.find("Ontology:") == std::string::npos);
    assert(bare.find("World:")    == std::string::npos);
    // The static system prompt mentions the word "Ontology" once
    // (in the ops-vocabulary intro). The OPT-IN ontology block uses
    // a more specific marker: "Ontology (the entity types".
    auto sys_bare = dir::build_director_system_prompt();
    assert(sys_bare.find("Ontology (the entity types") == std::string::npos);
    assert(sys_with_onto.find("Ontology (the entity types") != std::string::npos);
    std::printf("[PASS] director_prompt_contains_phase_b_blocks\n");
}

void test_random_director_thoughts_are_in_character() {
    // Sample a handful of seeds; the thought string must always be
    // non-empty and never the literal "random fallback" placeholder.
    for (uint32_t seed : {1u, 7u, 42u, 99u, 1024u}) {
        auto json = dir::generate_random_mutation_json(seed, dir::RandomDirectorContext{});
        auto resp = dir::parse_director_json(json);
        assert(resp.parse_error.empty());
        assert(!resp.thoughts.empty());
        assert(resp.thoughts != "random fallback");
        // Master Control vernacular: at least one of these tokens.
        bool in_character =
            resp.thoughts.find("Grid")    != std::string::npos ||
            resp.thoughts.find("User")    != std::string::npos ||
            resp.thoughts.find("Program") != std::string::npos ||
            resp.thoughts.find("derez")   != std::string::npos ||
            resp.thoughts.find("Master")  != std::string::npos ||
            resp.thoughts.find("I/O")     != std::string::npos;
        assert(in_character);
    }
    std::printf("[PASS] random_director_thoughts_are_in_character\n");
}

void test_random_director_tier_gates_vocabulary() {
    // Tier 0: personality swaps and grid_fog must NEVER appear —
    // the Master Control earns its meaner vocabulary. High tier:
    // across many seeds both must appear at least once, and every
    // emitted JSON must still parse clean.
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        dir::RandomDirectorContext ctx;  // tier 0 defaults
        auto json = dir::generate_random_mutation_json(seed, ctx);
        assert(json.find("ai_personality") == std::string::npos);
        assert(json.find("grid_fog") == std::string::npos);
        auto resp = dir::parse_director_json(json);
        assert(resp.parse_error.empty());
    }
    bool saw_personality = false, saw_fog = false;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        dir::RandomDirectorContext ctx;
        ctx.escalation_tier = 4;
        auto json = dir::generate_random_mutation_json(seed, ctx);
        if (json.find("ai_personality") != std::string::npos) saw_personality = true;
        if (json.find("grid_fog") != std::string::npos) saw_fog = true;
        auto resp = dir::parse_director_json(json);
        assert(resp.parse_error.empty());
    }
    assert(saw_personality);
    assert(saw_fog);

    // Tier 2: personality unlocked, fog still locked.
    bool fog_at_tier2 = false;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        dir::RandomDirectorContext ctx;
        ctx.escalation_tier = 2;
        auto json = dir::generate_random_mutation_json(seed, ctx);
        if (json.find("grid_fog") != std::string::npos) fog_at_tier2 = true;
    }
    assert(!fog_at_tier2);
    std::printf("[PASS] random_director_tier_gates_vocabulary\n");
}

void test_random_director_walls_stay_in_arena() {
    // Every wall coordinate must be an ARENA coord inside
    // [0, arena_w] x [0, arena_h] (the responder used centered
    // coords once; half the walls spawned outside the Grid).
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        dir::RandomDirectorContext ctx;
        ctx.arena_w = 34.0f; ctx.arena_h = 44.0f;
        ctx.escalation_tier = (seed % 5);
        auto json = dir::generate_random_mutation_json(seed, ctx);
        auto resp = dir::parse_director_json(json);
        assert(resp.parse_error.empty());
        for (const auto& op : resp.kg_ops) {
            const auto* ce = std::get_if<kg::KGOpCreateEntity>(&op);
            if (!ce || ce->type != "TrailSegment") continue;
            for (const auto& kv : ce->properties) {
                if (kv.first != "start_x" && kv.first != "start_y" &&
                    kv.first != "end_x" && kv.first != "end_y") continue;
                float v = std::stof(kv.second);
                float hi = (kv.first == "start_x" || kv.first == "end_x")
                               ? ctx.arena_w : ctx.arena_h;
                assert(v >= 0.0f && v <= hi);
            }
        }
    }
    std::printf("[PASS] random_director_walls_stay_in_arena\n");
}

void test_llm_plan_anthropic_outranks_openai() {
    std::map<std::string, std::string> env = {
        {"OPENAI_API_KEY",    "sk-openai-fake"},
        {"ANTHROPIC_API_KEY", "sk-ant-fake"},
    };
    auto getter = [&env](const char* k) -> const char* {
        auto it = env.find(k);
        return (it == env.end() || it->second.empty()) ? nullptr : it->second.c_str();
    };
    auto plan = logotron::plan_llm_from_env(getter);
    assert(plan.mode == logotron::LLMPlan::Mode::Anthropic);
    assert(plan.api_key == "sk-ant-fake");
    assert(plan.model == "claude-haiku-4-5");
    std::printf("[PASS] llm_plan_anthropic_outranks_openai\n");
}

void test_llm_plan_openai_alone() {
    std::map<std::string, std::string> env = {{"OPENAI_API_KEY", "sk-openai-fake"}};
    auto getter = [&env](const char* k) -> const char* {
        auto it = env.find(k);
        return (it == env.end() || it->second.empty()) ? nullptr : it->second.c_str();
    };
    auto plan = logotron::plan_llm_from_env(getter);
    assert(plan.mode == logotron::LLMPlan::Mode::OpenAI);
    assert(plan.model == "gpt-4o-mini");
    std::printf("[PASS] llm_plan_openai_alone\n");
}

void test_llm_plan_explicit_override() {
    std::map<std::string, std::string> env = {
        {"OPENAI_API_KEY",         "sk-openai-fake"},
        {"ANTHROPIC_API_KEY",      "sk-ant-fake"},
        {"LOGOTRON_LLM_PROVIDER",  "openai"},
    };
    auto getter = [&env](const char* k) -> const char* {
        auto it = env.find(k);
        return (it == env.end() || it->second.empty()) ? nullptr : it->second.c_str();
    };
    auto plan = logotron::plan_llm_from_env(getter);
    assert(plan.mode == logotron::LLMPlan::Mode::OpenAI);

    env["LOGOTRON_LLM_PROVIDER"] = "anthropic";
    plan = logotron::plan_llm_from_env(getter);
    assert(plan.mode == logotron::LLMPlan::Mode::Anthropic);

    env["LOGOTRON_LLM_PROVIDER"] = "none";
    plan = logotron::plan_llm_from_env(getter);
    assert(plan.mode == logotron::LLMPlan::Mode::None);
    std::printf("[PASS] llm_plan_explicit_override\n");
}

void test_llm_plan_model_override() {
    std::map<std::string, std::string> env = {
        {"ANTHROPIC_API_KEY",   "sk-ant-fake"},
        {"LOGOTRON_LLM_MODEL",  "claude-opus-4-7"},
    };
    auto getter = [&env](const char* k) -> const char* {
        auto it = env.find(k);
        return (it == env.end() || it->second.empty()) ? nullptr : it->second.c_str();
    };
    auto plan = logotron::plan_llm_from_env(getter);
    assert(plan.mode == logotron::LLMPlan::Mode::Anthropic);
    assert(plan.model == "claude-opus-4-7");
    std::printf("[PASS] llm_plan_model_override\n");
}

void test_llm_plan_empty_env_is_offline() {
    std::map<std::string, std::string> env;
    auto getter = [&env](const char* k) -> const char* {
        auto it = env.find(k);
        return (it == env.end() || it->second.empty()) ? nullptr : it->second.c_str();
    };
    auto plan = logotron::plan_llm_from_env(getter);
    assert(plan.mode == logotron::LLMPlan::Mode::None);
    std::printf("[PASS] llm_plan_empty_env_is_offline\n");
}

void test_respawned_ai_crashes_into_fresh_player_trail() {
    // Regression for "the third enemy was crossing over my trails no
    // problem" report. The contract: after the AI respawns, the
    // player's still-fresh sealed trail must remain lethal to the new
    // AI cycle. Aged trails (>kTrailLifetime) becoming non-lethal is
    // by design (GAME_DESIGN.md §7), so we use now_seconds=1.0 to
    // stay well inside the lethal window.
    kg::KGModule kg(logosphere::ontology::registry());
    kg.setMode(kg::KGMode::MINIMAL);
    kg.extendOntology(logotron::ontology::registry());

    kg::EntityID player = lt::spawn_cycle(
        kg, "PlayerCycle", 0.0f, 0.0f, lt::Direction::EAST);

    // Player lays a fresh horizontal trail along y=2 from x=-5..+5.
    {
        auto c = lt::read_cycle(kg, player);
        c.run_start_x = -5.0f; c.run_start_y = 2.0f;
        c.x = 5.0f; c.y = 2.0f;
        c.direction = lt::Direction::EAST;
        lt::write_cycle(kg, player, c);
        lt::freeze_run_at(kg, player, /*spawn_time=*/0.5f);
    }

    // Spawn an old AI, kill it, then have the Director respawn it
    // somewhere south of the wall, facing north so its next step
    // crosses y=2.
    kg::EntityID ai_old = lt::spawn_cycle(
        kg, "AICycle", 0.0f, 10.0f, lt::Direction::SOUTH);
    {
        auto c = lt::read_cycle(kg, ai_old);
        c.state = lt::CycleState::CRASHED;
        lt::write_cycle(kg, ai_old, c);
    }
    auto result = dir::respawn_ai(kg, ai_old,
                                   {0.0f, 0.0f, lt::Direction::NORTH});
    assert(result.new_ai_entity != kg::INVALID_ENTITY);
    kg::EntityID ai_new = result.new_ai_entity;

    // Step the new AI through the player's trail using the
    // age-aware collision wrapper. now_seconds=1.0 << kTrailLifetime,
    // so the trail must still be lethal.
    float dt = 1.0f;  // big step, plenty to cross from y=0 to y>2
    lt::step_cycle_in_kg_with_collision_at(
        kg, ai_new, /*arena_w=*/40.0f, /*arena_h=*/40.0f,
        dt, /*now_seconds=*/1.0f);

    auto post = lt::read_cycle(kg, ai_new);
    assert(post.state == lt::CycleState::CRASHED);
    std::printf("[PASS] respawned_ai_crashes_into_fresh_player_trail "
                "(new_ai state=CRASHED at (%.2f, %.2f))\n",
                post.x, post.y);
}

void test_director_cancel_clears_ready_and_requesting() {
    dir::Director d;
    std::function<void(std::string)> stash;
    d.set_responder([&stash](const std::string&, const std::string&,
                              std::function<void(std::string)> done) {
        stash = std::move(done);
    });
    dir::GameState s;
    assert(d.fire(s));
    assert(d.is_requesting());

    // Cancel mid-flight.
    d.cancel();
    assert(!d.is_requesting());
    dir::DirectorResponse out;
    assert(!d.poll(out));

    // A fresh fire() should now succeed.
    assert(d.fire(s));
    // Resolve the second pending request.
    stash(R"({"ops":[{"op":"set_property","target":"@program_1","property":"max_speed","value":"10"}]})");
    assert(d.poll(out));
    assert(out.kg_ops.size() == 1);
    std::printf("[PASS] director_cancel_clears_ready_and_requesting\n");
}

void test_director_history_records_round_and_appears_in_prompt() {
    dir::Director d;
    // Capture two consecutive prompts so we can verify round 2's
    // prompt sees round 1's record. Synchronous responder echoes a
    // canned JSON.
    std::vector<std::string> prompts_seen;
    d.set_responder([&prompts_seen](const std::string& /*sys*/,
                                    const std::string& user_prompt,
                                    std::function<void(std::string)> done) {
        prompts_seen.push_back(user_prompt);
        done(R"({"thoughts":"derez them","ops":[{"op":"set_property","target":"@program_1","property":"max_speed","value":"10"}]})");
    });

    dir::GameState s;
    s.round_number = 1;
    s.ai_deaths = 1;
    bool ok = d.fire(s);
    assert(ok);
    dir::DirectorResponse out1;
    assert(d.poll(out1));
    d.record_round(s.round_number, out1);

    // Round 2: the prompt should contain "Prior rounds:" + round 1's
    // thoughts string.
    s.round_number = 2;
    s.ai_deaths = 2;
    ok = d.fire(s);
    assert(ok);
    dir::DirectorResponse out2;
    assert(d.poll(out2));

    assert(prompts_seen.size() == 2);
    const auto& second_prompt = prompts_seen[1];
    assert(second_prompt.find("Prior rounds:") != std::string::npos);
    assert(second_prompt.find("derez them") != std::string::npos);
    assert(second_prompt.find("set_property") != std::string::npos);
    std::printf("[PASS] director_history_records_round_and_appears_in_prompt\n");
}

void test_director_history_capped_and_clearable() {
    dir::Director d;
    d.set_responder([](const std::string&, const std::string&,
                       std::function<void(std::string)> done) {
        done(R"({"thoughts":"x","ops":[]})");
    });

    // Fire 12 rounds; the cap is 8 so the oldest 4 should drop.
    for (int i = 1; i <= 12; ++i) {
        dir::GameState s; s.round_number = i;
        d.fire(s);
        dir::DirectorResponse r; d.poll(r);
        d.record_round(i, r);
    }
    auto h = d.history();
    assert(h.size() == 8);
    assert(h.front().round_number == 5);  // oldest kept
    assert(h.back().round_number  == 12); // newest

    d.clear_history();
    assert(d.history().empty());
    std::printf("[PASS] director_history_capped_and_clearable\n");
}

}  // namespace

int main() {
    test_first_spawn_no_wipe();
    test_respawn_wipes_old_ai_and_its_trails_only();
    test_respawn_after_crash();
    // v0.10: 4 mutate_* unit tests + parser_dispatch_round_trip +
    // e2e_ai_dies_director_mutates_ai_respawns deleted along with
    // mutations.{h,cpp}. KGOp coverage lives in test_kg_ops_apply
    // (engine-side AT) which exercises the same KG behaviors via
    // create_entity / set_property / destroy_entity ops.
    test_parser_bad_json_returns_error();
    test_parser_strips_markdown_fences();
    test_pre_fire_prompt_announces_first_duel();
    test_ontology_slice_advertises_arena_resize_slots();
    test_director_double_fire_rejected();
    test_director_history_records_round_and_appears_in_prompt();
    test_director_history_capped_and_clearable();
    test_director_cancel_clears_ready_and_requesting();
    test_director_prompt_contains_state();
    test_director_prompt_contains_phase_b_blocks();
    test_random_director_thoughts_are_in_character();
    test_random_director_tier_gates_vocabulary();
    test_random_director_walls_stay_in_arena();
    test_llm_plan_anthropic_outranks_openai();
    test_llm_plan_openai_alone();
    test_llm_plan_explicit_override();
    test_llm_plan_model_override();
    test_llm_plan_empty_env_is_offline();
    test_respawned_ai_crashes_into_fresh_player_trail();
    std::printf("[OK] director tests\n");
    return 0;
}
