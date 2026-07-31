// Tests for the Particle::is_self_emissive field.
//
// is_self_emissive lets a particle be drawn self-emissive (color *
// emission_strength) WITHOUT joining the scene-lights array. The
// renderer's deferred-shader self-emissive predicate is the union
// of is_light_source || is_self_emissive; the lights array stays
// gated on is_light_source only (verified in
// test_render_pipeline_emissive_union; this test is just the
// field-level contract).
//
// See the self-emissive field design notes.

#include "particle.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

static int tests_passed = 0;

#define TEST(name) do {                                       \
    std::printf("  " #name "... ");                           \
    name();                                                   \
    std::printf("PASS\n");                                    \
    tests_passed++;                                           \
} while (0)

#define EXPECT_TRUE(cond, msg) \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); exit(1); }

namespace {

void default_particle_is_not_self_emissive() {
    Particle p;
    EXPECT_TRUE(!p.is_self_emissive,
        "default Particle::is_self_emissive must be false");
    EXPECT_TRUE(!p.is_light_source,
        "default Particle::is_light_source must also be false (sanity)");
}

void setting_self_emissive_does_not_set_light_source() {
    // Game code wants to flip is_self_emissive without dragging
    // the particle into the scene-lights array. Confirm the field
    // is fully independent — toggling one does NOT touch the other.
    Particle p;
    p.is_self_emissive = true;
    EXPECT_TRUE(p.is_self_emissive,
        "setter persisted");
    EXPECT_TRUE(!p.is_light_source,
        "is_light_source must NOT flip when is_self_emissive flips. "
        "If a future struct-init pattern aliases the two, the wall "
        "GPU crash from 2026-04-27 returns.");
}

void setting_light_source_does_not_set_self_emissive() {
    // Symmetric guard. Real lights stay separate from decorative
    // emissives; the engine's existing is_light_source semantics
    // (mass-validation exemption, scene-lights array entry) must
    // not silently extend to self-emissive particles.
    Particle p;
    p.is_light_source = true;
    EXPECT_TRUE(p.is_light_source,  "setter persisted");
    EXPECT_TRUE(!p.is_self_emissive,
        "is_self_emissive must NOT flip when is_light_source flips");
}

void both_flags_can_be_true_simultaneously() {
    // No mutual exclusion. A real light is also self-emissive (the
    // shader's union check catches it via either flag), and the
    // light-data uploader catches it via is_light_source. No one
    // care which flag wins.
    Particle p;
    p.is_light_source  = true;
    p.is_self_emissive = true;
    EXPECT_TRUE(p.is_light_source && p.is_self_emissive,
        "both flags can coexist");
}

}  // namespace

int main() {
    std::printf("Tests for Particle::is_self_emissive\n");
    TEST(default_particle_is_not_self_emissive);
    TEST(setting_self_emissive_does_not_set_light_source);
    TEST(setting_light_source_does_not_set_self_emissive);
    TEST(both_flags_can_be_true_simultaneously);
    std::printf("[OK] %d tests passed\n", tests_passed);
    return 0;
}
