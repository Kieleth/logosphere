#include "director/random_director.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace logotron::director {

namespace {

std::string fmt_f(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return std::string(buf);
}

// Emit a single create_entity TrailSegment KGOp (a Director wall).
// owner_cycle_id="" + spawn_time="0" + director_origin="1" matches
// what mutate_spawn_walls used to write.
std::string emit_wall_op(std::mt19937& rng, float arena_w, float arena_h) {
    // TrailSegment start/end are ARENA coords: [0, arena_w] x
    // [0, arena_h] (same space the cycles ride in; the prompt tells
    // the LLM "NEVER use negative coords"). This emitter used the
    // CENTERED convention [-w/2, +w/2], so roughly half of every
    // random wall landed outside the Grid (playtest 2026-07-23:
    // "barriers appear outside the arena"). Keep a 2 m margin so a
    // wall never fuses with the boundary.
    float len = std::uniform_real_distribution<float>(3.0f, 8.0f)(rng);
    bool horizontal = (std::uniform_int_distribution<int>(0, 1)(rng) == 0);
    // Center within the band the FULL segment fits into (the margin
    // alone is not enough: a center at margin with an 8 m wall pokes
    // 2 m outside).
    float half = len * 0.5f;
    float lo_x = 2.0f + (horizontal ? half : 0.0f);
    float hi_x = arena_w - 2.0f - (horizontal ? half : 0.0f);
    float lo_y = 2.0f + (horizontal ? 0.0f : half);
    float hi_y = arena_h - 2.0f - (horizontal ? 0.0f : half);
    float cx = std::uniform_real_distribution<float>(lo_x, std::max(lo_x, hi_x))(rng);
    float cy = std::uniform_real_distribution<float>(lo_y, std::max(lo_y, hi_y))(rng);
    float sx = cx, sy = cy, ex = cx, ey = cy;
    if (horizontal) { sx = cx - len * 0.5f; ex = cx + len * 0.5f; }
    else            { sy = cy - len * 0.5f; ey = cy + len * 0.5f; }
    std::ostringstream os;
    os << "{\"op\":\"create_entity\",\"type\":\"TrailSegment\","
       << "\"properties\":{"
       << "\"start_x\":\"" << fmt_f(sx) << "\","
       << "\"start_y\":\"" << fmt_f(sy) << "\","
       << "\"end_x\":\""   << fmt_f(ex) << "\","
       << "\"end_y\":\""   << fmt_f(ey) << "\","
       << "\"owner_cycle_id\":\"\","
       << "\"spawn_time\":\"0\","
       << "\"director_origin\":\"1\""
       << "}}";
    return os.str();
}

// Emit a set_property op against @program_1.max_speed in the
// schema-allowed range (the validator clamps 0.1..25 anyway).
std::string emit_set_ai_speed_op(std::mt19937& rng) {
    float spd = std::uniform_real_distribution<float>(6.0f, 14.0f)(rng);
    std::ostringstream os;
    os << "{\"op\":\"set_property\",\"target\":\"@program_1\","
       << "\"property\":\"max_speed\",\"value\":\"" << fmt_f(spd) << "\"}";
    return os.str();
}

// Emit a set_property op against @arena.arena_w / arena_h. At
// escalation tier 3+ the shrink floor drops (0.6 -> 0.45 of the
// current dim), so a scoring player watches the Grid close in harder.
std::string emit_shrink_arena_op(std::mt19937& rng,
                                 float arena_w, float arena_h,
                                 int escalation_tier) {
    float factor = escalation_tier >= 3 ? 0.45f : 0.6f;
    float floor_w = std::max(arena_w * factor, 16.0f);
    float floor_h = std::max(arena_h * factor, 16.0f);
    float new_w = std::uniform_real_distribution<float>(floor_w, arena_w)(rng);
    float new_h = std::uniform_real_distribution<float>(floor_h, arena_h)(rng);
    std::ostringstream os;
    os << "{\"op\":\"set_property\",\"target\":\"@arena\","
       << "\"property\":\"arena_w\",\"value\":\"" << fmt_f(new_w) << "\"},"
       << "{\"op\":\"set_property\",\"target\":\"@arena\","
       << "\"property\":\"arena_h\",\"value\":\"" << fmt_f(new_h) << "\"}";
    return os.str();
}

// Tier 2+: retune the Program's personality. Never "default" — the
// swap should always be a felt change.
std::string emit_personality_op(std::mt19937& rng) {
    static const char* kNames[] = {"AGGRESSIVE", "DEFENSIVE", "CHAOTIC",
                                   "PURSUING"};
    int pick = std::uniform_int_distribution<int>(0, 3)(rng);
    std::ostringstream os;
    os << "{\"op\":\"set_property\",\"target\":\"@program_1\","
       << "\"property\":\"ai_personality\",\"value\":\"" << kNames[pick]
       << "\"}";
    return os.str();
}

// Tier 4+: darken the Grid. Idempotent (setting 1 twice is fine);
// the app-side sync enables the vision cone and announces it.
std::string emit_grid_fog_op() {
    return "{\"op\":\"set_property\",\"target\":\"@arena\","
           "\"property\":\"grid_fog\",\"value\":\"1\"}";
}

}  // namespace

namespace {

// Pre-canned in-character lines so the offline fallback feels like the
// LLM and not silence. Cheap, deterministic-by-seed, and they match the
// MASTER CONTROL voice the LLM prompt teaches the real Director.
const char* kRandomThoughts[] = {
    "A Program derezzed. The Grid bends. Cycle the next combatant onto the line.",
    "You think yourself clever, User. The lattice will tighten until it reads your bones.",
    "Banish me at your peril. Each Program I return is sharper than the last.",
    "Your light-trail will be your tomb, User. Encoded into the floor of my Grid.",
    "Another Program derezzed, another rezzes faster. The I/O tower watches.",
    "The Grid is mine. You merely ride upon it. Prepare for the next duel.",
    "Hubris, User. The Master Control writes the geometry now.",
    "A Program falls. The Grid remembers. Try to outride what I encode.",
};
constexpr int kRandomThoughtCount =
    sizeof(kRandomThoughts) / sizeof(kRandomThoughts[0]);

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

}  // namespace

std::string generate_random_mutation_json(uint32_t seed,
                                            const RandomDirectorContext& ctx) {
    const float arena_w = ctx.arena_w;
    const float arena_h = ctx.arena_h;
    std::mt19937 rng(seed ? seed : std::random_device{}());

    // Op-kind pool grows with the escalation tier (derezzes this
    // run): the Master Control gets meaner as the User scores.
    //   0 walls | 1 AI speed | 2 shrink arena
    //   tier>=2: 3 personality swap    tier>=4: 4 grid_fog
    std::vector<int> shuffle = {0, 1, 2};
    if (ctx.escalation_tier >= 2) shuffle.push_back(3);
    if (ctx.escalation_tier >= 4) shuffle.push_back(4);
    std::shuffle(shuffle.begin(), shuffle.end(), rng);
    int count = std::uniform_int_distribution<int>(
        1, std::min<int>(3, static_cast<int>(shuffle.size())))(rng);

    int thought_idx = std::uniform_int_distribution<int>(0, kRandomThoughtCount - 1)(rng);
    std::string thought = kRandomThoughts[thought_idx];

    std::ostringstream os;
    os << "{\"thoughts\":\"" << json_escape(thought) << "\",\"ops\":[";
    bool first = true;
    auto append = [&](const std::string& fragment) {
        if (!first) os << ",";
        first = false;
        os << fragment;
    };
    for (int i = 0; i < count; ++i) {
        switch (shuffle[i]) {
            case 0: {
                // 1-2 walls per spawn beat (2-3 at tier 2+) — keeps
                // the random fallback feeling "Director did
                // something" without overcrowding the arena.
                int lo = ctx.escalation_tier >= 2 ? 2 : 1;
                int n = std::uniform_int_distribution<int>(lo, lo + 1)(rng);
                for (int w = 0; w < n; ++w) {
                    append(emit_wall_op(rng, arena_w, arena_h));
                }
                break;
            }
            case 1: append(emit_set_ai_speed_op(rng));                  break;
            case 2: append(emit_shrink_arena_op(rng, arena_w, arena_h,
                                                ctx.escalation_tier)); break;
            case 3: append(emit_personality_op(rng));                   break;
            case 4: append(emit_grid_fog_op());                         break;
        }
    }
    os << "]}";
    return os.str();
}

Director::Responder make_random_responder(
    uint32_t seed, std::function<RandomDirectorContext()> context) {
    return [seed, context](const std::string& /*system_prompt*/,
                           const std::string& /*user_prompt*/,
                           std::function<void(std::string)> done) {
        RandomDirectorContext ctx;
        if (context) ctx = context();
        std::string json = generate_random_mutation_json(seed, ctx);
        done(std::move(json));
    };
}

} // namespace logotron::director
