// ============================================================================
// HUMANOID TUNING COVERAGE — which declared joint values actually survive?
// ============================================================================
// WHY. humanoid_generator.cpp declares twenty per-joint angular profiles, each
// with a comment explaining its value: neck 25 ("Loose"), knee 250 ("rigid"),
// wrist 150, elbow 40, shoulder 50. Then humanoid_locomotion.cpp, in
// apply_physics_drive_legs_init and apply_physics_drive_upper_body_init, walks
// a list of joint names and overwrites angular_stiffness / angular_damping with
// a single pair of literals — 2000 / 60 — before frame one.
//
// If that overwrite reaches every joint, then every one of those twenty
// annotated values is dead code: it is written, then overwritten, and never
// read by the solver. Deleting it would be bit-identical, and keeping it is a
// lie about how the rig behaves.
//
// This test does not assume that. It MEASURES it: it snapshots every gluon on
// the humanoid immediately after the generator runs, snapshots again after
// registration, and prints the difference. Whatever it prints is the truth
// about coverage, whichever way it comes out.
//
// It is a CHARACTERIZATION test in the same spirit as
// test_physics_characterization: it does not claim the current numbers are
// right, only that they are what the engine does. It fails if a gluon is left
// in an UNDECLARED state — carrying the class default rather than either an
// intentional generator value or an intentional override — because that is the
// one outcome nobody chose.
//
//   ./build-release/logosphere-tests --test test_humanoid_tuning_coverage --no-head
// ============================================================================

#include "../src/core/engine.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "logosphere/physics/physics_system.h"
#include "../src/materials.h"
#include "../src/particle.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace {

struct AngularProfile {
    float stiffness = -1.0f;
    float damping   = -1.0f;
    bool  operator==(const AngularProfile& o) const {
        return stiffness == o.stiffness && damping == o.damping;
    }
};

using PairKey = std::pair<size_t, size_t>;

// Every gluon touching any humanoid particle, keyed by its unordered pair so a
// bond is sampled once regardless of which end we reach it from.
std::map<PairKey, AngularProfile> snapshot(const PhysicsSystem& physics,
                                           const std::vector<int>& body_ids) {
    std::map<PairKey, AngularProfile> out;
    for (int pid : body_ids) {
        if (pid < 0) continue;
        for (const GluonConstraintBase* g :
                 physics.get_gluons_for_particle(static_cast<size_t>(pid))) {
            if (!g) continue;
            PairKey k{std::min(g->particle_a, g->particle_b),
                      std::max(g->particle_a, g->particle_b)};
            out[k] = AngularProfile{g->angular_stiffness, g->angular_damping};
        }
    }
    return out;
}

// The GluonConstraintBase class defaults (physics_system.h). A gluon still
// carrying these was never given an opinion by anybody. 2026-08-13: the old
// silent defaults (100/10) were destroyed by owner order — the class now
// carries the UNDECLARED sentinel (0/0), so "nobody's opinion" is 0/0.
constexpr float CLASS_DEFAULT_STIFFNESS = 0.0f;
constexpr float CLASS_DEFAULT_DAMPING   =  0.0f;

} // namespace

bool test_humanoid_tuning_coverage() {
    printf("\n=== HUMANOID TUNING COVERAGE: what survives to frame one? ===\n");

    Engine engine;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    if (engine.initialize(cfg) != 0) {
        printf("  engine init failed\n  FAIL\n");
        return false;
    }

    auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
    strata.set_tile_size(4.0f);
    strata.set_tiles_per_chunk(5);
    strata.set_tiles_per_entity(1);
    strata.set_load_radius(30.0f);
    strata.set_unload_radius(40.0f);
    {
        std::vector<StrataLayerSpec> layers;
        StrataLayerSpec s;
        s.name = "bedrock"; s.material = Materials::Type::STONE;
        s.thickness = 0.30f; s.r = 0.35f; s.g = 0.33f; s.b = 0.30f;
        s.bond_within_layer = true; s.bond_strength = 8000.0f;
        layers.push_back(s);
        strata.set_layers(std::move(layers));
    }
    strata.set_enabled(true);
    strata.preload_chunks_around(0.0f, 0.0f, 3);

    auto& physics = engine.get_physics_system();
    auto& hgen = engine.get_worldgen_system().get_humanoid_generator();

    // ---- 1. the generator's opinion ---------------------------------------
    auto eva = hgen.generate_humanoid_physics(
        0.0f, 0.0f, 1.0f, -1, HumanoidSpec::eva(), false);
    const auto declared = snapshot(physics, eva.body_ids);

    // ---- 2. registration: the two physics-drive init passes run here ------
    engine.get_humanoid_locomotion().register_humanoid_direct(
        eva.hips_id,
        eva.left_leg_ids, eva.right_leg_ids,
        eva.left_arm_ids, eva.right_arm_ids,
        eva.torso_ids, 180.0f, 800.0f,
        eva.entity_id);
    const auto live = snapshot(physics, eva.body_ids);

    // ---- 2b. and does anything move it later? -----------------------------
    // The per-frame publisher writes gluon TARGETS. If it also rewrote
    // stiffness, a snapshot at registration would be a lie about steady state.
    // Run the rig and re-sample, so "dead" means dead for good.
    for (int f = 0; f < 120; ++f) engine.update(1.0f / 60.0f);
    const auto after_sim = snapshot(physics, eva.body_ids);
    size_t moved_during_sim = 0;
    for (const auto& [key, prof] : after_sim) {
        auto it = live.find(key);
        if (it == live.end() || !(it->second == prof)) moved_during_sim++;
    }

    // ---- 3. the difference ------------------------------------------------
    printf("\n  %-14s %10s %8s   %10s %8s   %s\n",
           "gluon", "declared", "damp", "live", "damp", "verdict");
    printf("  %s\n", "---------------------------------------------------"
                    "-------------------");

    size_t overwritten = 0, survived = 0, class_default = 0, appeared = 0;
    for (const auto& [key, live_prof] : live) {
        auto it = declared.find(key);
        const bool is_new = (it == declared.end());
        if (is_new) appeared++;
        const AngularProfile dec = is_new ? AngularProfile{} : it->second;

        const char* verdict;
        if (is_new) {
            verdict = "(created at registration)";
        } else if (dec == live_prof) {
            const bool untouched_default =
                live_prof.stiffness == CLASS_DEFAULT_STIFFNESS &&
                live_prof.damping   == CLASS_DEFAULT_DAMPING;
            if (untouched_default) { class_default++; verdict = "*** NOBODY'S OPINION ***"; }
            else                   { survived++;      verdict = "generator value SURVIVES"; }
        } else {
            overwritten++;
            verdict = "generator value DEAD (overwritten)";
        }

        printf("  P%-5zu-P%-6zu %10.1f %8.1f   %10.1f %8.1f   %s\n",
               key.first, key.second,
               dec.stiffness, dec.damping,
               live_prof.stiffness, live_prof.damping,
               verdict);
    }

    printf("\n  bonds on the humanoid           %zu\n", live.size());
    printf("  generator value overwritten     %zu\n", overwritten);
    printf("  generator value survives        %zu\n", survived);
    printf("  carrying the class default      %zu\n", class_default);
    printf("  created during registration     %zu\n", appeared);
    printf("  changed by 120 frames of sim    %zu%s\n", moved_during_sim,
           moved_during_sim == 0
               ? "   (so registration IS the steady state)"
               : "   (something rewrites stiffness per frame)");

    // The failing condition is a bond nobody gave an opinion about: it is
    // running on GluonConstraintBase's 100/10, which is a header default, not
    // a decision. Overwriting is not a failure here — it is the fact this test
    // exists to record.
    const bool pass = (class_default == 0);
    printf("\n  %s\n", pass
        ? "PASS"
        : "FAIL (a bond is running on the class default — nobody chose it)");

    engine.shutdown();
    return pass;
}
