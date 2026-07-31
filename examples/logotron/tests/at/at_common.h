// Common scaffolding for Logotron Acceptance Tests (ATs).
//
// **Why this exists** — unit tests cover lower-level pure logic
// (e.g., the engine's `compute_vision_cone_occlusion` math). ATs
// cover game-level FEATURES end-to-end: "the AI bike is invisible
// when out of the player's vision cone," "the cycle ramps speed
// after a turn," "trails occlude line of sight." Each AT spawns
// the same machinery the running game uses (engine, KG, particle
// system, assemblies), exercises the feature, and asserts on
// observable state.
//
// **Headless by default**: every AT runs with no GPU window.
// Assertions read from CPU state (KG properties, particle fields,
// engine maps). When something feels right but visually different,
// re-run with `LOGOTRON_AT_VISUAL=1`: the AT spawns the SAME
// scenario in interactive mode for human eyeballs (the AT's body
// has a single hook to opt in).
//
// **One file per AT**, registered via `add_logotron_at(name)` in
// CMakeLists.txt. Keeps build + run iteration tight.

#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

namespace logotron::at {

inline bool visual_mode_requested() {
    const char* env = std::getenv("LOGOTRON_AT_VISUAL");
    return env && env[0] == '1';
}

// Number of frames an AT should drive in headless mode before
// reading state. Override with `LOGOTRON_AT_FRAMES=N`. Default = 60
// (~1 s at the game's nominal tick).
inline int headless_frames() {
    if (const char* e = std::getenv("LOGOTRON_AT_FRAMES")) {
        try { return std::stoi(e); } catch (...) {}
    }
    return 60;
}

// Seconds to run an AT in interactive mode before exiting (so the
// human can poke at it). Override with `LOGOTRON_AT_VISUAL_SECONDS`.
inline double visual_seconds() {
    if (const char* e = std::getenv("LOGOTRON_AT_VISUAL_SECONDS")) {
        try { return std::stod(e); } catch (...) {}
    }
    return 30.0;
}

} // namespace logotron::at

// Minimal assertion macros — same shape as the engine's existing
// headless tests so failure messages are familiar.
#define AT_TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); std::cout << "PASS" << std::endl; tests_passed++; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define AT_ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

#define AT_ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")

#define AT_ASSERT_NEAR(a, b, tol, msg) \
    if (std::abs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string(msg) + " [got=" + std::to_string(a) + " want=" + std::to_string(b) + "]")
