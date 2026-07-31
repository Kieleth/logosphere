// Weirden Director — async orchestrator between AI deaths.
//
// Lifecycle:
//   1. AI cycle crashes.
//   2. Application calls director.fire(state). Returns immediately.
//      The responder is invoked off the main thread (LLM worker, or
//      synchronously for tests / random fallback).
//   3. Application polls director.poll(out) each frame. While the
//      responder hasn't completed, poll() returns false. Once it does,
//      poll() returns true and `out` is filled with the parsed
//      DirectorResponse.
//   4. Application applies each mutation via apply_mutation_call,
//      respawns the AI, resumes the round.
//
// Concurrency: state writes from the responder thread go through a
// mutex. The application reads via poll() on the main thread.

#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "director/director_parser.h"

namespace logotron::director {

// Snapshot of the world that the Director needs to choose mutations.
// Built at fire() time on the main thread; immutable thereafter.
struct GameState {
    int   round_number = 0;
    int   ai_deaths    = 0;
    float arena_w      = 40.0f;
    float arena_h      = 40.0f;
    int   player_trail_count = 0;
    int   director_wall_count = 0;
    float player_max_speed   = 5.0f;

    // One entry per enemy Program on the Grid (the roster). ref is
    // the prompt alias (@program_k).
    struct RiderInfo {
        std::string ref;
        std::string personality;
        float max_speed = 0.0f;
        int   trail_count = 0;
    };
    std::vector<RiderInfo> riders;

    // Player metrics — what the Director reads to make informed
    // moves: how fast the User dares to go, how twitchy they steer,
    // how long this run has lasted, how many Programs they took down.
    float player_top_speed = 0.0f;
    int   player_turns     = 0;
    float round_duration_s = 0.0f;
    int   derez_count      = 0;
    // Last few death sentences ("Program #2 (CHAOTIC) derezzed by the
    // User [sealed_trail_other] @(12,30)").
    std::vector<std::string> death_log;
    // Net property deltas since the last fire (engine journal,
    // render_state_deltas), filtered game-side to actors + arena.
    std::string state_delta;
    // Escalation tier == Programs derezzed this run. Gates the
    // Director's vocabulary; the prompt tells the LLM what each
    // tier unlocks.
    int escalation_tier = 0;
    // Free-form note the application can use to nudge the LLM ("the
    // player rammed the AI head-on"). Optional. Populated from the
    // crash_* metadata via make_narrative_hint().
    std::string narrative_hint;

    // === Phase B context fields (KG-snapshot prompt) ===
    // The application builds these at fire() time from the
    // engine's ontology + KG via kg::serialize_ontology_slice /
    // kg::render_query_json, plus the game's symbolic ref
    // map. Empty strings turn off the corresponding prompt block;
    // tests + offline mode happily run without any of them.
    std::string ontology_slice;   // compact JSON of relevant types
    std::string kg_snapshot;      // multi-line JSON of live entities
    std::string symbols_text;     // "Symbols: @player_cycle=7 ..."
};

// One past-round capsule for the prompt's "Prior rounds:" section.
// Built by Director::record_round() from a DirectorResponse and
// kept in a small ring buffer so the LLM can remember what it
// tried and react to which moves actually changed the duel.
struct RoundRecord {
    int         round_number = 0;
    std::string thoughts;       // verbatim from the LLM
    std::string ops_summary;    // one-liner: "spawn_walls(3),set_speeds"
};

// System prompt: the static role description, vocabulary, examples,
// and rules. If `ontology_slice` is non-empty it gets embedded inside
// the system block — this is the path that lets Anthropic prompt
// caching kick in (system block above the 1024-token threshold and
// stable across rounds means the cache hits on every duel after the
// first). The user-prompt path drops the ontology block when this
// is used; otherwise the user-prompt embeds it as before.
std::string build_director_system_prompt(const std::string& ontology_slice = "");
std::string build_director_user_prompt(
    const GameState& state,
    const std::vector<RoundRecord>& history = {});

class Director {
public:
    // The responder takes the system prompt and the user prompt
    // separately, and must invoke `done(json_response)` exactly once
    // when the LLM (or a stub) returns. Splitting system/user lets
    // backends that support prompt caching (Anthropic, OpenAI) flag
    // the system block as cacheable — for a fast Director, that
    // matters: the system prompt holds the (large, static) ontology
    // and never changes round-to-round.
    //
    // Synchronous responders may invoke `done` before the outer
    // fire() call returns; that's allowed.
    using Responder =
        std::function<void(const std::string& system_prompt,
                           const std::string& user_prompt,
                           std::function<void(std::string)> done)>;

    void set_responder(Responder r);

    // Submit a Director request. No-op if a request is already in
    // flight. Returns true on submit, false if busy.
    bool fire(const GameState& state);

    // True between fire() and the responder completing.
    bool is_requesting() const;

    // Drain a ready response. Returns true and fills `out` if one is
    // ready (and clears internal state). False otherwise.
    bool poll(DirectorResponse& out);

    // Drop any in-flight or cached response and reset to IDLE. Used
    // when the surrounding round resets so a stale LLM response
    // doesn't apply to a fresh world. The underlying LLM request, if
    // any, may still complete on its worker thread; its result will
    // be ignored by Director (the on_response_ caches it, but the
    // next fire() clears ready_ before its own response lands).
    void cancel();

    // Append a finished round to the rolling history. Builds an
    // ops_summary from the response so future prompts include "in
    // round N you tried X". Drops the oldest entry past the cap.
    // Application calls this after applying mutations.
    void record_round(int round_number, const DirectorResponse& resp);

    // Snapshot of the current history (oldest first). Cheap copy.
    std::vector<RoundRecord> history() const;

    // Wipe the history. Used at full-round-reset (player death).
    void clear_history();

private:
    void on_response_(std::string json);

    Responder responder_;
    mutable std::mutex mu_;
    std::optional<DirectorResponse> ready_;
    std::atomic<bool> requesting_{false};

    // Cap chosen so the prompt's "Prior rounds:" section fits in
    // ~1 KB even with chatty thoughts. Bigger context windows could
    // afford more; this stays conservative.
    static constexpr size_t kHistoryCap = 8;
    std::deque<RoundRecord> history_;
};

} // namespace logotron::director
