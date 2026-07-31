// AIInstrument — Logotron's concrete telemetry instrument.
//
// Records per-round gameplay so you can read back what the AI saw,
// what it chose, and why at every decision tick. Files land under
// the session's logotron/ subdirectory (the engine session layer
// creates it automatically from Instrument::name()).
//
// Layout:
//     <session>/logotron/
//       round_001.jsonl      one line per AI decision tick
//       round_001_meta.json  spawn pose, outcome, duration, totals
//       round_002.jsonl
//       ...
//
// Call `begin_round(personality_name)` when the round starts,
// `record_decision(...)` on every AI tick, `end_round(outcome,
// duration_s)` when the round ends. Rolls the round counter
// automatically.

#pragma once

#include <string>
#include <vector>

#include "logosphere/telemetry/session.h"
#include "ai/perceived_world.h"
#include "cycle.h"

namespace logotron::ai {

class AIInstrument : public logosphere::telemetry::Instrument {
public:
    const char* name() const override { return "logotron"; }

    // Start recording a new round. `personality_name` ends up in the
    // meta file alongside the spawn pose the instrument captures
    // from the first decision tick.
    void begin_round(const std::string& personality_name);

    // One record per AI decision tick. Kept tight — this fires
    // ~6/sec * 30s round = ~180 records. Full PerceivedWorld is
    // NOT serialized verbatim; we pick the load-bearing fields.
    void record_decision(float elapsed_s,
                         const PerceivedWorld& pw,
                         logotron::Direction chose,
                         float head_target_yaw);

    // One crash detail per cycle that died this round. Call from
    // main.cpp whenever it observes a cycle transitioning to
    // CRASHED. label = "player" or "ai" (or whatever the game
    // wants). cause matches the KG "crash_cause" property.
    struct CrashRecord {
        std::string label;
        std::string cause;          // e.g., "sealed_trail_self"
        float       x = 0.0f;
        float       y = 0.0f;
        float       at_t = 0.0f;    // elapsed when detected
        float       hit_age = 0.0f; // 0 if not a timed trail
        long long   hit_entity = 0; // 0 if N/A
    };
    void note_crash(const CrashRecord& cr);

    // Close the round file + write the meta. `outcome` is the
    // user-facing label: "PLAYER_WON" / "AI_WON" / "DRAW" / etc.
    // Any crashes noted since the last begin_round() are embedded in
    // the meta file.
    void end_round(const std::string& outcome, float duration_s);

    // Helpers exposed for tests + the review script.
    int  current_round() const { return round_num_; }
    std::string round_filename() const;

private:
    int                         round_num_ = 0;       // incremented per begin_round
    int                         decision_count_ = 0;
    std::vector<CrashRecord>    crashes_this_round_;
    std::string personality_name_;
    float       round_start_s_ = 0.0f;
    float       first_tick_x_ = 0.0f;
    float       first_tick_y_ = 0.0f;
    bool        have_first_ = false;
    std::string spawn_direction_;
};

} // namespace logotron::ai
