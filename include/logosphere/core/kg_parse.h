// kg_parse: defensive string → number parsing for KG-driven code paths.
//
// KG property values are strings. Games write them via setProperty; the
// engine consumes them during capability aggregation, rule evaluation,
// damage lookup, etc. A game typo (e.g. rule.0.trigger = "health_below:fify")
// used to throw std::invalid_argument from std::stof and kill the engine.
//
// This helper replaces std::stof/stod/stoi at the game-supplied string
// boundary. On parse failure it returns nullopt and logs a one-line
// warning to stderr with full context (entity id, property key, value).
// Callers apply their own fallback via .value_or(default).
//
// Only use this at the game/engine boundary. Internal engine code that
// has guaranteed well-formed inputs should crash loud if that guarantee
// is violated — a std::invalid_argument from our own code means we have
// a bug, not a misconfigured game.

#pragma once

#include "logosphere/kg/kg_types.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace kg_parse {

namespace detail {

inline void warn(const char* type_name,
                 const std::string& value,
                 const char* property_key,
                 kg::EntityID entity_id) {
    std::cerr << "[kg_parse] entity=" << entity_id
              << " property=\"" << (property_key ? property_key : "")
              << "\" malformed value=\"" << value
              << "\" — expected " << type_name << std::endl;
}

// std::from_chars for floating-point requires macOS 26.0+. Use
// strtof/strtod with errno + end-pointer checking instead. Works on
// every libc.

inline bool parse_float(const std::string& s, float& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    if (errno != 0) return false;
    if (end != s.c_str() + s.size()) return false;  // rejects "3.5abc"
    return true;
}

inline bool parse_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    if (errno != 0) return false;
    if (end != s.c_str() + s.size()) return false;
    return true;
}

inline bool parse_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0) return false;
    if (end != s.c_str() + s.size()) return false;
    if (v < INT_MIN || v > INT_MAX) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace detail

inline std::optional<float> to_float(
    const std::string& value,
    const char* property_key,
    kg::EntityID entity_id)
{
    float out;
    if (detail::parse_float(value, out)) return out;
    detail::warn("float", value, property_key, entity_id);
    return std::nullopt;
}

inline std::optional<double> to_double(
    const std::string& value,
    const char* property_key,
    kg::EntityID entity_id)
{
    double out;
    if (detail::parse_double(value, out)) return out;
    detail::warn("double", value, property_key, entity_id);
    return std::nullopt;
}

inline std::optional<int> to_int(
    const std::string& value,
    const char* property_key,
    kg::EntityID entity_id)
{
    int out;
    if (detail::parse_int(value, out)) return out;
    detail::warn("int", value, property_key, entity_id);
    return std::nullopt;
}

} // namespace kg_parse
