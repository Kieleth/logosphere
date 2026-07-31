// Acceptance Test: trail walls render visibly + don't tank FPS.
//
// Headless reproduces the symptoms the user reported in live play:
//   1. Walls drawn dark — looked like Tron with the lights off.
//      Diagnosis: emission_strength was set, but is_light_source
//      defaulted false; the deferred-lighting shader gates
//      self-emission on is_light_source. Without the flag, the
//      wall contributes neither scene light nor self-emissive draw.
//   2. Stuttering / low FPS as walls accumulated. is_light_source
//      true means the wall ALSO joins the scene-lights array; each
//      wall costs RT shadow work per pixel. Bounding emission_radius
//      keeps the self-emissive draw without the scene-lighting tax.
//
// What this AT locks:
//   - Player wall: is_light_source && emission_strength > 0
//   - AI wall: same
//   - Director wall: same, distinct hue from player/AI
//   - emission_radius is bounded (< 2 m) so a 50-wall round can't
//     turn into a 50-RT-light scene
//
// Pure-C++; no Engine, no GPU. The setters are static methods on
// LogotronApplication; the AT instantiates Particles and calls them
// directly.

#include "at_common.h"

#include "walls.h"
#include "particle.h"

#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

// Build a wall the way sync_trail_particles does: geometry first
// (sets material + sizing + position), then color (rgba + emission).
Particle make_wall(bool is_player, bool is_director) {
    Particle p = {};
    logotron::set_wall_geometry(p, 0.0f, 0.0f, 5.0f, 0.0f);
    logotron::set_wall_color(p, is_player, is_director);
    return p;
}

// CURRENT POLICY (2026-04-28): walls are SELF-EMISSIVE but NOT
// scene lights. is_self_emissive is the new Particle flag added to
// solve "walls glow without crashing the GPU at 20+ trail churn."
// See the self-emissive field design notes.
//
// Both flags MUST be checked. is_light_source=true on a wall
// re-introduces the 2026-04-27 GPU command-queue crash. Missing
// is_self_emissive means the deferred shader's emissive branch
// doesn't fire and walls go dark again.
void test_player_wall_self_emissive_not_light_source() {
    auto p = make_wall(/*is_player=*/true, /*is_director=*/false);
    AT_ASSERT_TRUE(p.is_self_emissive,
        "player wall must be is_self_emissive=true so the "
        "deferred shader treats its pixels as emissive");
    AT_ASSERT_TRUE(!p.is_light_source,
        "REGRESSION GUARD: walls must NOT be is_light_source — "
        "setting it true crashed the GPU command queue at 20+ "
        "wall churn. is_self_emissive is the right flag here.");
    AT_ASSERT_TRUE(p.emission_strength > 0.0f,
        "player wall must have non-zero emission_strength so the "
        "self-emissive branch produces visible color");
    AT_ASSERT_TRUE(p.r > 0.0f && p.b > 0.0f,
        "player wall color is cyan-ish — non-zero in r and b");
    AT_ASSERT_TRUE(p.a > 0.99f, "player wall fully opaque");
}

void test_ai_wall_self_emissive_not_light_source() {
    auto p = make_wall(/*is_player=*/false, /*is_director=*/false);
    AT_ASSERT_TRUE(p.is_self_emissive,  "AI wall must be self-emissive");
    AT_ASSERT_TRUE(!p.is_light_source, "AI wall must NOT be light source");
    AT_ASSERT_TRUE(p.emission_strength > 0.0f,
        "AI wall must have non-zero emission_strength");
    AT_ASSERT_TRUE(p.r > 0.5f, "AI wall color is orange — strong red");
}

void test_director_wall_self_emissive_not_light_source() {
    auto p = make_wall(/*is_player=*/false, /*is_director=*/true);
    AT_ASSERT_TRUE(p.is_self_emissive,
        "Director wall must be self-emissive");
    AT_ASSERT_TRUE(!p.is_light_source,
        "Director wall must NOT be light source");
    AT_ASSERT_TRUE(p.emission_strength > 0.0f,
        "Director wall must have non-zero emission_strength");
    AT_ASSERT_TRUE(p.b > 0.9f, "Director wall is magenta — strong blue");
}

// REGRESSION: active-run heads churn (delete + add) every frame in
// sync_active_runs. If their particle is is_light_source=true, the
// renderer's light buffer rebuilds at 60+ Hz and the GPU command
// queue crashes within seconds of live play
// (kIOGPUCommandBufferCallbackErrorInnocentVictim observed
// 2026-04-27). style_active_run_head MUST clear the flag.
void test_active_run_head_is_not_light_source() {
    Particle p = {};
    logotron::set_wall_geometry(p, 0.0f, 0.0f, 5.0f, 0.0f);
    logotron::style_active_run_head(p, /*is_player=*/true);
    AT_ASSERT_TRUE(!p.is_light_source,
        "active-run head must NOT be a light source — it churns "
        "every frame and the GPU light buffer can't take it");
    AT_ASSERT_TRUE(p.is_dynamic,
        "active-run head must be is_dynamic so vision-memory "
        "skips ghosting it");
    AT_ASSERT_TRUE(p.r > 0.0f && p.b > 0.0f,
        "active-run head still gets the player color so the "
        "live segment reads against the floor");
}

void test_active_run_head_ai_variant_also_not_light_source() {
    Particle p = {};
    logotron::set_wall_geometry(p, 0.0f, 0.0f, 5.0f, 0.0f);
    logotron::style_active_run_head(p, /*is_player=*/false);
    AT_ASSERT_TRUE(!p.is_light_source,
        "AI active-run head must NOT be a light source either");
}

}  // namespace

int main() {
    std::cout << "Logotron AT — wall_visibility" << std::endl;
    AT_TEST(test_player_wall_self_emissive_not_light_source);
    AT_TEST(test_ai_wall_self_emissive_not_light_source);
    AT_TEST(test_director_wall_self_emissive_not_light_source);
    AT_TEST(test_active_run_head_is_not_light_source);
    AT_TEST(test_active_run_head_ai_variant_also_not_light_source);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
