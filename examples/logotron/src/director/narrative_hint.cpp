#include "director/narrative_hint.h"

#include "logosphere/kg/kg_module.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>

namespace logotron::director {

namespace {

// Try to parse a float; returns nan on failure.
float parse_float(const std::string& s) {
    if (s.empty()) return std::numeric_limits<float>::quiet_NaN();
    try { return std::stof(s); } catch (...) { return std::numeric_limits<float>::quiet_NaN(); }
}

// Render a (x, y) coord with one decimal place. Hides nan as "?".
std::string fmt_xy(float x, float y) {
    if (std::isnan(x) || std::isnan(y)) return "(?, ?)";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "(%.1f, %.1f)", x, y);
    return buf;
}

// Render an age in seconds with one decimal. "" if unknown.
std::string fmt_age(float age) {
    if (std::isnan(age) || age <= 0.0f) return "";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "; %.1fs old", age);
    return buf;
}

}  // namespace

std::string make_narrative_hint(const kg::KGModule& kg,
                                kg::EntityID ai_entity) {
    if (ai_entity == kg::INVALID_ENTITY) return "";
    auto cause = kg.getProperty(ai_entity, "crash_cause");
    if (cause.empty() || cause == "none") return "";

    float x   = parse_float(kg.getProperty(ai_entity, "crash_x"));
    float y   = parse_float(kg.getProperty(ai_entity, "crash_y"));
    float age = parse_float(kg.getProperty(ai_entity, "crash_hit_age"));
    auto hit_entity_str = kg.getProperty(ai_entity, "crash_hit_entity");

    // Did the AI die on a director-spawned wall? Look up the hit
    // entity and check its director_origin tag. If yes, name it as
    // "director wall" so the LLM credits its own move from the
    // prior round.
    bool director_wall = false;
    if (!hit_entity_str.empty()) {
        try {
            kg::EntityID hit = static_cast<kg::EntityID>(std::stoi(hit_entity_str));
            if (kg.exists(hit) &&
                kg.getProperty(hit, "director_origin") == "1") {
                director_wall = true;
            }
        } catch (...) {}
    }

    std::ostringstream os;
    os << "AI ";

    if (cause == "out_of_bounds") {
        os << "ran out of arena at " << fmt_xy(x, y);
    } else if (cause == "sealed_trail_self") {
        os << "hit own sealed trail at " << fmt_xy(x, y) << fmt_age(age);
    } else if (cause == "sealed_trail_other") {
        if (director_wall) {
            os << "rammed director wall at " << fmt_xy(x, y);
        } else {
            os << "hit player's sealed trail at " << fmt_xy(x, y) << fmt_age(age);
        }
    } else if (cause == "opponent_active_run") {
        os << "rammed player head-on at " << fmt_xy(x, y);
    } else {
        os << "crashed (" << cause << ") at " << fmt_xy(x, y);
    }

    return os.str();
}

}  // namespace logotron::director
