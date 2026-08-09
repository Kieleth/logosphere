// AT: a hungry predator plans, pursues, eats, and is fed.
//
// This is the acceptance test for the engine/game AI boundary (#37 item
// 3), and it exists so the diet code moved out of the engine is
// consumed by something in-repo — "compiled but run by nothing" is how
// the whole NPC layer went untested for months, and it does not get to
// happen twice.
//
// What it demonstrates, which is the boundary working end to end:
//   - ENGINE mechanism: GOAP planner, A* pathfinding, ExecutorRegistry,
//     GOAPPlanExecutor, the generic PURSUE executor.
//   - GAME policy (this directory): the EAT executor, FoodState
//     bite-by-bite consumption, and a PredatorContext riding the
//     engine's opaque game_data slot. The engine never learns a mouth
//     exists.
//   - The declaration-routed targets that replaced the hardcoded action
//     name ladder: EAT and PURSUE declare target_key = "food", the
//     brain publishes params.targets["food"], and the plan executor
//     routes by data. The food sits AT THE WORLD ORIGIN deliberately:
//     under the old `food_x != 0` sentinel this exact scene picked the
//     wrong target (#44), so this AT is also that regression's guard.
//
// Headless: pure engine-core APIs, runs in every profile.
//
// Usage:
//   ./build/at_predator_hunger

#undef NDEBUG

#include "ai/eat_executor.h"
#include "ai/food_state.h"
#include "ai/predator_context.h"
#include "npc-ai/executors/pursue_executor.h"
#include "npc-ai/goap_plan_executor.h"
#include "npc-ai/goap_system.h"
#include "npc-ai/pathfinding_system.h"

#include <cmath>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

int main() {
    std::cout << "AT: a hungry predator plans, pursues, eats (the AI boundary)"
              << std::endl;

    // ---- GAME policy: what actions exist, what they want ----
    goap::Action pursue;
    pursue.name = "PURSUE";
    pursue.preconditions["hungry"] = 1;
    pursue.effects["at_food"] = 1;
    pursue.target_key = "food";          // declaration, not a name ladder

    goap::Action eat;
    eat.name = "EAT";
    eat.preconditions["at_food"] = 1;
    eat.effects["hungry"] = 0;
    eat.target_key = "food";

    goap::Goal fed;
    fed.name = "fed";
    fed.desired_state["hungry"] = 0;

    // ---- ENGINE mechanism: plan it ----
    goap::Planner planner;
    planner.add_action(pursue);
    planner.add_action(eat);
    goap::WorldState world;
    world["hungry"] = 1;
    goap::Plan plan = planner.plan(world, fed);
    std::cout << "  [measure] planner produced " << plan.size()
              << " action(s), valid=" << plan.valid << std::endl;
    check(plan.valid && plan.size() == 2,
          "the planner derives PURSUE then EAT from hunger (" +
          std::to_string(plan.size()) + " actions)");

    // ---- Registry: one engine behaviour, one game behaviour ----
    npc_ai::ExecutorRegistry registry;
    registry.register_executor("PURSUE", std::make_unique<npc_ai::PursueExecutor>());
    registry.register_executor("EAT", std::make_unique<npc_ai::EatExecutor>());

    pathfinding::NavGrid grid;
    grid.init(-20.0f, -20.0f, 20.0f, 20.0f, 1.0f);
    pathfinding::Pathfinder finder;
    pathfinding::Path path;

    npc_ai::GOAPPlanExecutor exec;
    exec.set_registry(&registry);
    exec.set_pathfinder(&finder);
    exec.set_nav_grid(&grid);
    exec.set_current_path(&path);
    exec.set_goal(&fed);
    exec.set_verbose(false);

    // ---- The scene: predator away from food, food AT THE ORIGIN ----
    // The origin is the #44 regression: under the zero sentinel this
    // target read as "absent" and the creature was routed elsewhere.
    float px = 8.0f, py = 6.0f, rotation = 0.0f;
    const float food_x = 0.0f, food_y = 0.0f;

    // The carcass: 30cm cube of flesh-density food, eaten bite by bite
    // through the game's own FoodState — the physics-based path, not
    // the timer fallback, so the moved consumption code actually runs.
    Particle carcass{};
    carcass.particle_id = 7;
    carcass.width = 0.3f; carcass.height = 0.3f; carcass.thickness = 0.3f;
    carcass.material_density = 1000.0f;
    npc_ai::FoodState food = npc_ai::FoodState::from_particle(carcass, 0.5f);
    const float initial_mass = food.remaining_g;

    predator::PredatorContext pctx;
    pctx.mouth_volume_cm3 = 400.0f;      // a big mouth: few bites needed
    pctx.food_state = &food;

    npc_ai::CreatureParams params;
    params.run_speed = 4.0f;
    params.arrival_distance = 1.0f;
    params.targets["food"] = {food_x, food_y};
    params.game_data = &pctx;

    // ---- Run it ----
    const float dt = 1.0f / 60.0f;
    int frames = 0;
    bool goal_achieved = false;
    float dist_at_eat_start = -1.0f;
    while (frames < 3600 && !goal_achieved) {
        auto r = exec.update(plan, world, px, py, rotation, dt, params);
        // The brain applies movement — the position owner, as always.
        if (r.movement.is_moving) {
            const float dx = food_x - px, dy = food_y - py;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-4f) {
                px += (dx / len) * r.movement.forward_velocity * dt;
                py += (dy / len) * r.movement.forward_velocity * dt;
            }
        }
        if (r.action_completed && dist_at_eat_start < 0.0f) {
            dist_at_eat_start = std::hypot(px - food_x, py - food_y);
        }
        goal_achieved = r.goal_achieved;
        ++frames;
    }

    const float final_dist = std::hypot(px - food_x, py - food_y);
    std::cout << "  [measure] " << frames << " frames; arrived at "
              << dist_at_eat_start << " m from food; ended " << final_dist
              << " m away" << std::endl;
    std::cout << "  [measure] carcass " << initial_mass << " g -> "
              << food.remaining_g << " g remaining" << std::endl;
    std::cout << "  [measure] hungry=" << world["hungry"]
              << " at_food=" << world["at_food"]
              << " goal_achieved=" << goal_achieved << std::endl;

    check(goal_achieved, "the goal is achieved (" +
          std::to_string(frames) + " frames)");
    check(dist_at_eat_start >= 0.0f && dist_at_eat_start <= 1.5f,
          "PURSUE actually arrived before EAT began (" +
          std::to_string(dist_at_eat_start) + " m, arrival at 1.0)");
    check(food.remaining_g < initial_mass,
          "the carcass lost mass through the game's bite model (" +
          std::to_string(initial_mass - food.remaining_g) + " g eaten)");
    check(world["hungry"] == 0, "and the world state says fed");

    // ---- The control: same scene, food never published ----
    // An action that declares a target nobody publishes must read
    // has_target=false and the pursuit must fail to arrive, not walk to
    // a fabricated coordinate. This is absence-as-absence, the #44 fix.
    {
        goap::Plan plan2 = planner.plan({{"hungry", 1}}, fed);
        goap::WorldState world2;
        world2["hungry"] = 1;
        npc_ai::GOAPPlanExecutor exec2;
        pathfinding::Path path2;
        exec2.set_registry(&registry);
        exec2.set_pathfinder(&finder);
        exec2.set_nav_grid(&grid);
        exec2.set_current_path(&path2);
        exec2.set_goal(&fed);
        exec2.set_verbose(false);

        npc_ai::CreatureParams starved;   // no targets published at all
        starved.run_speed = 4.0f;

        float qx = 8.0f, qy = 6.0f;
        bool achieved2 = false;
        bool replan_reported = false;
        for (int f = 0; f < 600 && !achieved2; ++f) {
            auto r = exec2.update(plan2, world2, qx, qy, 0.0f, dt, starved);
            if (r.movement.is_moving) qx += r.movement.forward_velocity * dt;
            achieved2 = r.goal_achieved;
            replan_reported = replan_reported || r.needs_replan;
        }
        std::cout << "  [measure] control (no target published): moved from "
                  << "(8, 6) to (" << qx << ", " << qy
                  << "), goal_achieved=" << achieved2
                  << ", needs_replan reported=" << replan_reported << std::endl;
        // The first run of this control FAILED and found a real defect:
        // PURSUE failed honestly, but the loop ran EAT anyway, in timer
        // mode, at a place never reached — actions' preconditions were
        // never checked at execution time. The precondition gate in
        // GOAPPlanExecutor::update is the fix this control forced.
        check(!achieved2,
              "with no published target the predator does NOT get fed");
        check(replan_reported,
              "the stale plan is reported as needing a replan");
        check(std::abs(qy - 6.0f) < 1e-4f,
              "and it was never routed toward a fabricated coordinate");
        check(world2["hungry"] == 1, "the world still says hungry, honestly");
    }

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
