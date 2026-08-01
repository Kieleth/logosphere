// physics_guard_runner — dedicated entry point for the locomotion guard
// suite on the render-free (physics-profile) Engine.
//
// Why not the existing harness: test_main/test_harness link UI+GLFW via
// their interactive path, and unified_test_runner.cpp externs the whole
// ~150-function test suite, so neither can link against the physics
// profile. The guards themselves are self-contained free functions that
// construct their own headless Engine; all they need is this table.
//
// XFAIL: documented known-reds (CHANGELOG "Known issues", full RCAs in
// the cap-double-advance RCA notes) run and report but
// do not fail the job. They flip to gating when the kinematic-root
// reconciliation lands.

#include <cstdio>
#include <cstring>

// The self-contained guard functions (each spins up its own Engine
// with create_display=false, or bare subsystems).
extern bool test_stance_foot_invariance();
extern bool test_walk_forward_progress();
extern bool test_strafe_progress();
extern bool test_rotation_cascade_yaw();
extern bool test_turn_in_place_foot_step();
extern bool test_rotation_during_walk();
extern bool test_strafe_sidestep_pattern();
extern bool test_leg_shoot_out_during_rotation();
extern bool test_idle_pose_stability();
extern bool test_humanoid_strata_walk();
extern bool test_spider_eva_shin_crush();
extern bool test_joint_hierarchy_swap_integrity();
extern bool test_reverse_leg_chain();
extern bool test_phase_e_diagnostic();
extern bool test_position_authority();
extern bool test_deferred_deletion_integrity();
extern bool test_pin_anchor_persistence();
extern bool test_interaction_filtering();
extern bool test_interaction_volume_forces();
extern bool test_interaction_transformations();

namespace {

struct GuardTest {
    const char* name;
    bool (*fn)();
    bool xfail;  // documented known-red: report, don't gate
};

const GuardTest kGuards[] = {
    {"test_stance_foot_invariance",        test_stance_foot_invariance,        false},
    {"test_walk_forward_progress",         test_walk_forward_progress,         false},
    {"test_strafe_progress",               test_strafe_progress,               false},
    {"test_rotation_cascade_yaw",          test_rotation_cascade_yaw,          true},
    {"test_turn_in_place_foot_step",       test_turn_in_place_foot_step,       false},
    {"test_rotation_during_walk",          test_rotation_during_walk,          false},
    {"test_strafe_sidestep_pattern",       test_strafe_sidestep_pattern,       false},
    {"test_leg_shoot_out_during_rotation", test_leg_shoot_out_during_rotation, false},
    {"test_idle_pose_stability",           test_idle_pose_stability,           false},
    {"test_humanoid_strata_walk",          test_humanoid_strata_walk,          false},
    {"test_spider_eva_shin_crush",         test_spider_eva_shin_crush,         false},
    {"test_joint_hierarchy_swap_integrity",test_joint_hierarchy_swap_integrity,false},
    {"test_reverse_leg_chain",             test_reverse_leg_chain,             false},
    {"test_phase_e_diagnostic",            test_phase_e_diagnostic,            false},
    {"test_position_authority",            test_position_authority,            false},
    {"test_deferred_deletion_integrity",   test_deferred_deletion_integrity,   false},
    {"test_pin_anchor_persistence",        test_pin_anchor_persistence,        false},
    {"test_interaction_filtering",         test_interaction_filtering,         false},
    {"test_interaction_volume_forces",     test_interaction_volume_forces,     false},
    {"test_interaction_transformations",   test_interaction_transformations,   false},
};

}  // namespace

int main(int argc, char* argv[]) {
    const char* only = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            only = argv[++i];
        }
    }

    int passed = 0, failed = 0, xfailed = 0, xpassed = 0, ran = 0;
    for (const auto& t : kGuards) {
        if (only && std::strcmp(only, t.name) != 0) continue;
        ++ran;
        std::printf("\n========== %s%s ==========\n",
                    t.name, t.xfail ? "  [XFAIL: documented known-red]" : "");
        std::fflush(stdout);
        const bool ok = t.fn();
        if (ok && !t.xfail)       { ++passed;  std::printf("[GUARD PASS]  %s\n", t.name); }
        else if (!ok && t.xfail)  { ++xfailed; std::printf("[GUARD XFAIL] %s (known-red, not gating)\n", t.name); }
        else if (ok && t.xfail)   { ++xpassed; std::printf("[GUARD XPASS] %s (known-red now PASSES — promote it to gating!)\n", t.name); }
        else                      { ++failed;  std::printf("[GUARD FAIL]  %s\n", t.name); }
        std::fflush(stdout);
    }

    if (only && ran == 0) {
        std::printf("unknown test '%s'\n", only);
        return 2;
    }

    std::printf("\n==================================================\n");
    std::printf("PHYSICS GUARD SUITE: %d passed, %d failed, %d xfail, %d xpass (of %d run)\n",
                passed, failed, xfailed, xpassed, ran);
    std::printf("==================================================\n");
    return failed > 0 ? 1 : 0;
}
