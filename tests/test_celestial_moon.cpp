// Moon factory (engine preset) — pure config tests.
// Earth defaults, exotic colors, brightness scaling, anti-phase.
//
// Usage: ./build/test_celestial_moon

#include "celestial/celestial_system.h"

#include <cmath>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << std::endl; tests_failed++; } \
    else { tests_passed++; } \
} while (0)

using Celestial::MoonSpec;
using Celestial::make_moon;

int main() {
    std::cout << "=== Celestial moon factory ===" << std::endl;

    // Earth by default: silver, anti-phase, night-keyed emission.
    auto earth = make_moon();
    ASSERT(earth.name == "moon", "named moon");
    ASSERT(earth.orbit.phase_offset == 0.5f, "anti-phase with the sun");
    ASSERT(earth.color_curve.size() == 1 &&
           earth.color_curve[0].value.r > 0.9f &&
           earth.color_curve[0].value.b > 0.8f &&
           std::fabs(earth.color_curve[0].value.r -
                     earth.color_curve[0].value.g) < 0.02f,
           "Earth silver by default");
    float earth_peak = 0.0f;
    for (const auto& k : earth.emission_curve)
        earth_peak = std::max(earth_peak, k.value);
    ASSERT(std::fabs(earth_peak / (earth.orbit.distance *
                                   earth.orbit.distance) - 400.0f) < 1.0f,
           "full moon lands ~400 lux at ground");
    // Dark through the day: emission at phase 0 and 1 is zero.
    ASSERT(earth.emission_curve.front().value == 0.0f &&
           earth.emission_curve.back().value == 0.0f,
           "moon dark at its nadir (world noon)");

    // Blood moon: color flows, brightness scales the peak.
    MoonSpec blood;
    blood.r = 0.8f; blood.g = 0.15f; blood.b = 0.1f;
    blood.brightness = 1.5f;
    auto red = make_moon(blood);
    ASSERT(red.color_curve[0].value.r > 0.7f &&
           red.color_curve[0].value.g < 0.2f,
           "blood moon color flows");
    float red_peak = 0.0f;
    for (const auto& k : red.emission_curve)
        red_peak = std::max(red_peak, k.value);
    ASSERT(std::fabs(red_peak / earth_peak - 1.5f) < 0.01f,
           "brightness scales emission");

    // Staggered second moon keeps its offset.
    MoonSpec second;
    second.phase_offset = 0.38f;
    ASSERT(make_moon(second).orbit.phase_offset == 0.38f,
           "phase offset respected for multiple moons");

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
