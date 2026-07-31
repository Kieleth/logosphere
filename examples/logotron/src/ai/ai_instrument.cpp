#include "ai/ai_instrument.h"

#include <cstdio>
#include <sstream>
#include <string>

namespace logotron::ai {

namespace {

const char* dir_name(logotron::Direction d) {
    switch (d) {
        case logotron::Direction::NORTH: return "N";
        case logotron::Direction::EAST:  return "E";
        case logotron::Direction::SOUTH: return "S";
        case logotron::Direction::WEST:  return "W";
    }
    return "?";
}

// Minimal JSON escape for strings we write into records.
std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Infinity sentinel as a large but JSON-valid number. JSONL doesn't
// allow `Infinity`; choose a value that readers can filter on.
float finite_or(float v, float cap = 1e9f) {
    return (v >= cap) ? cap : v;
}

} // namespace

void AIInstrument::begin_round(const std::string& personality_name) {
    ++round_num_;
    decision_count_ = 0;
    personality_name_ = personality_name;
    have_first_ = false;
    crashes_this_round_.clear();
}

void AIInstrument::note_crash(const CrashRecord& cr) {
    crashes_this_round_.push_back(cr);
}

std::string AIInstrument::round_filename() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "round_%03d.jsonl", round_num_);
    return buf;
}

void AIInstrument::record_decision(float elapsed_s,
                                   const PerceivedWorld& pw,
                                   logotron::Direction chose,
                                   float head_target_yaw) {
    if (!have_first_) {
        first_tick_x_     = pw.self.x;
        first_tick_y_     = pw.self.y;
        spawn_direction_  = dir_name(pw.self.direction);
        round_start_s_    = elapsed_s;
        have_first_       = true;
    }
    ++decision_count_;

    std::ostringstream j;
    j << "{"
      << "\"t\":"          << elapsed_s                               << ","
      << "\"x\":"          << pw.self.x                               << ","
      << "\"y\":"          << pw.self.y                               << ","
      << "\"dir\":\""      << dir_name(pw.self.direction)             << "\","
      << "\"head_cur\":"   << pw.head.current_yaw                     << ","
      << "\"head_tgt\":"   << head_target_yaw                         << ","
      << "\"lethal_n\":"   << finite_or(pw.lethal_distance[0])        << ","
      << "\"lethal_e\":"   << finite_or(pw.lethal_distance[1])        << ","
      << "\"lethal_s\":"   << finite_or(pw.lethal_distance[2])        << ","
      << "\"lethal_w\":"   << finite_or(pw.lethal_distance[3])        << ","
      << "\"opp_vis\":"    << (pw.opponent_rider.has_value() ? 1 : 0);
    if (pw.opponent_rider.has_value()) {
        const auto& op = *pw.opponent_rider;
        j << ",\"opp_x\":" << op.x
          << ",\"opp_y\":" << op.y
          << ",\"opp_d\":" << op.distance;
    }
    j << ",\"chose\":\"" << dir_name(chose) << "\""
      << "}";
    append_jsonl(round_filename(), j.str());
}

void AIInstrument::end_round(const std::string& outcome, float duration_s) {
    // Meta file is one JSON doc (not JSONL) — easier to read at a
    // glance and the caller typically writes exactly one per round.
    std::ostringstream j;
    j << "{"
      << "\"round\":"      << round_num_                              << ","
      << "\"personality\":\"" << esc(personality_name_)               << "\","
      << "\"outcome\":\""   << esc(outcome)                            << "\","
      << "\"duration_s\":" << duration_s                              << ","
      << "\"decisions\":"  << decision_count_                         << ","
      << "\"spawn_x\":"    << first_tick_x_                           << ","
      << "\"spawn_y\":"    << first_tick_y_                           << ","
      << "\"spawn_dir\":\""<< esc(spawn_direction_)                    << "\","
      << "\"crashes\":[";
    for (size_t i = 0; i < crashes_this_round_.size(); ++i) {
        const auto& cr = crashes_this_round_[i];
        if (i > 0) j << ",";
        j << "{"
          << "\"label\":\""     << esc(cr.label) << "\","
          << "\"cause\":\""     << esc(cr.cause) << "\","
          << "\"x\":"           << cr.x          << ","
          << "\"y\":"           << cr.y          << ","
          << "\"at_t\":"        << cr.at_t       << ","
          << "\"hit_age\":"     << cr.hit_age    << ","
          << "\"hit_entity\":"  << cr.hit_entity
          << "}";
    }
    j << "]}";
    // One file per round for the meta too.
    char buf[40];
    std::snprintf(buf, sizeof(buf), "round_%03d_meta.json", round_num_);
    append_jsonl(buf, j.str());
}

} // namespace logotron::ai
