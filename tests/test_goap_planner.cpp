// GOAP planner: the engine's NPC decision layer, under test at last.
//
// src/npc-ai/ is 2,614 lines compiled into the engine library — a GOAP
// planner, A* grid pathfinding, an executor registry and a perception
// query type. It has no tests, and nothing inside this repo uses it:
// the only consumer anywhere is logomancers' shambler brain. So the
// engine ships an NPC brain that nothing here exercises and nothing
// here guards.
//
// This is the first half of the guard: the planner. It is pure std
// with no engine coupling, so it runs in the headless profile.
//
// Written to find out what the planner DOES, not to confirm what its
// documentation says it does. Where the two disagree, the measured
// behaviour is recorded and marked, not quietly asserted away.
//
// Usage:
//   ./build/test_goap_planner

#include "npc-ai/goap_system.h"

#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_xfailed = 0;

// Same convention as physics_guard_runner: a documented known-red is
// reported every run but does not gate, and says so if it starts
// passing.
static void check(bool ok, const std::string& msg, bool xfail = false) {
    if (ok) { tests_passed++; }
    else if (xfail) {
        tests_xfailed++;
        std::cout << "  XFAIL: " << msg << "  (known-red, not gating)"
                  << std::endl;
    } else {
        tests_failed++;
        std::cout << "  FAIL: " << msg << std::endl;
    }
}

namespace {

// The canonical example from the header: a hungry NPC that knows where
// food is should work out PURSUE then EAT.
goap::Action pursue() {
    goap::Action a;
    a.name = "PURSUE";
    a.preconditions = {{"food_known", 1}, {"at_food", 0}};
    a.effects = {{"at_food", 1}};
    a.cost = 1.0f;
    return a;
}

goap::Action eat() {
    goap::Action a;
    a.name = "EAT";
    a.preconditions = {{"at_food", 1}, {"hungry", 1}};
    a.effects = {{"hungry", 0}};
    a.cost = 1.0f;
    return a;
}

std::string plan_str(const goap::Plan& p) {
    if (p.actions.empty()) return "(empty)";
    std::string s;
    for (size_t i = 0; i < p.actions.size(); ++i) {
        if (i) s += " -> ";
        s += p.actions[i]->name;
    }
    return s;
}

// ---------------------------------------------------------------- state

void test_world_state_matching() {
    using namespace goap;
    WorldState s = {{"hungry", 1}, {"food_known", 1}, {"at_food", 0}};

    check(satisfies(s, {}), "empty requirements are always satisfied");
    check(satisfies(s, {{"hungry", 1}}), "a matching key satisfies");
    check(satisfies(s, {{"hungry", 1}, {"at_food", 0}}),
          "several matching keys satisfy");
    check(!satisfies(s, {{"hungry", 0}}), "a wrong value does not satisfy");
    check(!satisfies(s, {{"tired", 1}}),
          "a key the state has never heard of does not satisfy");

    WorldState t = s;
    apply_effects(t, {{"at_food", 1}, {"tired", 1}});
    check(t["at_food"] == 1, "effects overwrite an existing key");
    check(t["tired"] == 1, "effects introduce a new key");
    check(t["hungry"] == 1, "effects leave untouched keys alone");
}

// ---------------------------------------------------------------- planning

void test_it_finds_the_canonical_plan() {
    goap::Planner p;
    p.add_action(pursue());
    p.add_action(eat());

    goap::WorldState now = {{"hungry", 1}, {"food_known", 1}, {"at_food", 0}};
    goap::Goal satiated{"SATIATED", {{"hungry", 0}}, 1.0f};

    goap::Plan plan = p.plan(now, satiated);
    std::cout << "  [measure] plan: " << plan_str(plan)
              << "  valid=" << plan.valid
              << "  cost=" << plan.total_cost << std::endl;

    check(plan.valid, "a plan was found");
    check(plan.size() == 2, "it takes two actions (got " +
                            std::to_string(plan.size()) + ")");
    if (plan.size() == 2) {
        check(plan.actions[0]->name == "PURSUE", "PURSUE comes first");
        check(plan.actions[1]->name == "EAT", "EAT comes second");
    }
    check(plan.total_cost == 2.0f,
          "cost is the sum of the actions (got " +
          std::to_string(plan.total_cost) + ")");
}

// Ordering must come from the preconditions, not from the order the
// actions happened to be registered in.
void test_ordering_is_forced_by_preconditions() {
    goap::Planner p;
    p.add_action(eat());        // registered FIRST, must still run second
    p.add_action(pursue());

    goap::WorldState now = {{"hungry", 1}, {"food_known", 1}, {"at_food", 0}};
    goap::Plan plan = p.plan(now, {"SATIATED", {{"hungry", 0}}, 1.0f});

    std::cout << "  [measure] registered EAT first, plan: "
              << plan_str(plan) << std::endl;
    check(plan.valid && plan.size() == 2 &&
          plan.actions[0]->name == "PURSUE",
          "registration order does not decide execution order");
}

void test_no_plan_when_the_goal_is_unreachable() {
    goap::Planner p;
    p.add_action(eat());        // needs at_food, and nothing can provide it

    goap::WorldState now = {{"hungry", 1}, {"food_known", 0}, {"at_food", 0}};
    goap::Plan plan = p.plan(now, {"SATIATED", {{"hungry", 0}}, 1.0f});

    std::cout << "  [measure] unreachable goal: valid=" << plan.valid
              << " size=" << plan.size() << std::endl;
    check(!plan.valid, "an unreachable goal yields an invalid plan");
    check(plan.empty(), "and no actions");
}

void test_a_satisfied_goal_needs_no_actions() {
    goap::Planner p;
    p.add_action(pursue());
    p.add_action(eat());

    goap::WorldState fed = {{"hungry", 0}};
    goap::Plan plan = p.plan(fed, {"SATIATED", {{"hungry", 0}}, 1.0f});

    std::cout << "  [measure] already satisfied: valid=" << plan.valid
              << " size=" << plan.size() << " cost=" << plan.total_cost
              << std::endl;
    check(plan.valid, "an already-satisfied goal is a valid plan");
    check(plan.empty(), "with nothing to do");
    check(plan.total_cost == 0.0f, "and no cost");
}

// The planner is an A* search, so it must prefer the cheaper route
// rather than the first one it stumbles on.
void test_it_prefers_the_cheaper_route() {
    goap::Planner p;

    goap::Action trudge;
    trudge.name = "TRUDGE";
    trudge.preconditions = {{"at_food", 0}};
    trudge.effects = {{"at_food", 1}};
    trudge.cost = 10.0f;

    goap::Action sprint;
    sprint.name = "SPRINT";
    sprint.preconditions = {{"at_food", 0}};
    sprint.effects = {{"at_food", 1}};
    sprint.cost = 1.0f;

    p.add_action(trudge);       // expensive one registered first
    p.add_action(sprint);
    p.add_action(eat());

    goap::Plan plan = p.plan({{"hungry", 1}, {"at_food", 0}},
                             {"SATIATED", {{"hungry", 0}}, 1.0f});
    std::cout << "  [measure] two routes, plan: " << plan_str(plan)
              << " cost=" << plan.total_cost << std::endl;
    check(plan.valid && plan.size() == 2 &&
          plan.actions[0]->name == "SPRINT",
          "the cheaper action wins (got " + plan_str(plan) + ")");
}

// A dynamic cost function must override the static cost, or a game
// cannot make a choice depend on the situation.
void test_dynamic_cost_overrides_static_cost() {
    goap::Planner p;

    goap::Action trudge;
    trudge.name = "TRUDGE";
    trudge.preconditions = {{"at_food", 0}};
    trudge.effects = {{"at_food", 1}};
    trudge.cost = 1.0f;                       // cheap by the static number
    trudge.cost_fn = [](const goap::WorldState&) { return 50.0f; };

    goap::Action sprint;
    sprint.name = "SPRINT";
    sprint.preconditions = {{"at_food", 0}};
    sprint.effects = {{"at_food", 1}};
    sprint.cost = 5.0f;                       // dearer by the static number

    p.add_action(trudge);
    p.add_action(sprint);
    p.add_action(eat());

    goap::Plan plan = p.plan({{"hungry", 1}, {"at_food", 0}},
                             {"SATIATED", {{"hungry", 0}}, 1.0f});
    std::cout << "  [measure] cost_fn 50 vs static 5, plan: "
              << plan_str(plan) << " cost=" << plan.total_cost << std::endl;
    check(plan.valid && plan.size() == 2 &&
          plan.actions[0]->name == "SPRINT",
          "cost_fn is consulted instead of the static cost (got " +
          plan_str(plan) + ")");
}

// A cycle must not hang the planner. Two actions that undo each other
// are an obvious way for a game to write an infinite loop by accident.
void test_a_cycle_terminates() {
    goap::Planner p;

    goap::Action open_door;
    open_door.name = "OPEN";
    open_door.preconditions = {{"door", 0}};
    open_door.effects = {{"door", 1}};

    goap::Action shut_door;
    shut_door.name = "SHUT";
    shut_door.preconditions = {{"door", 1}};
    shut_door.effects = {{"door", 0}};

    p.add_action(open_door);
    p.add_action(shut_door);

    // Unreachable: nothing here can ever make us fed.
    goap::Plan plan = p.plan({{"door", 0}, {"hungry", 1}},
                             {"SATIATED", {{"hungry", 0}}, 1.0f});
    std::cout << "  [measure] cycling actions, unreachable goal: valid="
              << plan.valid << std::endl;
    check(!plan.valid, "it gives up rather than looping forever");
}

// ---------------------------------------------------------------- depth

// max_depth is the caller's bound on how long a plan may be. The
// question is whether a plan of exactly max_depth actions is allowed.
void test_max_depth_semantics() {
    goap::Planner p;
    p.add_action(pursue());
    p.add_action(eat());
    goap::WorldState now = {{"hungry", 1}, {"food_known", 1}, {"at_food", 0}};
    goap::Goal satiated{"SATIATED", {{"hungry", 0}}, 1.0f};

    const goap::Plan at_2 = p.plan(now, satiated, 2);
    const goap::Plan at_3 = p.plan(now, satiated, 3);
    const goap::Plan at_1 = p.plan(now, satiated, 1);

    std::cout << "  [measure] the shortest plan is 2 actions. "
              << "max_depth=1 -> " << (at_1.valid ? plan_str(at_1) : "none")
              << ", max_depth=2 -> " << (at_2.valid ? plan_str(at_2) : "none")
              << ", max_depth=3 -> " << (at_3.valid ? plan_str(at_3) : "none")
              << std::endl;

    check(!at_1.valid, "a 2-action plan is refused at max_depth=1");
    check(at_3.valid, "and allowed at max_depth=3");

    // Issue #34, FIXED: the goal test now runs before the depth gate,
    // so a finished plan of exactly max_depth actions is an answer and
    // the depth limit means what it says. The gate still stops
    // expansion (the max_depth=1 refusal above is the control).
    check(at_2.valid,
          "a 2-action plan is allowed at max_depth=2: the bound is "
          "inclusive, as documented");
}

// ---------------------------------------------------------------- lifetime

// Plan holds RAW POINTERS into the planner's action vector
// (successor.actions_taken.push_back(&action) over a
// std::vector<Action>). Adding an action can reallocate that vector and
// leave every previously returned Plan pointing at freed memory.
//
// This does not dereference the stale plan: that is undefined
// behaviour, and a test that reads freed memory is not a test. It
// pins the ownership fact instead, so the hazard is written down.
void test_plans_borrow_from_the_planner() {
    goap::Planner p;
    p.add_action(pursue());
    p.add_action(eat());

    goap::Plan plan = p.plan({{"hungry", 1}, {"food_known", 1}, {"at_food", 0}},
                             {"SATIATED", {{"hungry", 0}}, 1.0f});
    check(plan.valid && plan.size() == 2, "a plan was produced");

    const goap::Action* first = plan.actions.empty() ? nullptr
                                                     : plan.actions[0];
    check(first != nullptr, "the plan holds a pointer, not a copy");
    std::cout << "  [measure] Plan::actions is std::vector<const Action*>, "
                 "borrowed from Planner::actions_" << std::endl;
    std::cout << "            so a Plan MUST NOT outlive the Planner, and "
                 "add_action()/clear_actions()" << std::endl;
    std::cout << "            after planning invalidates any plan already "
                 "handed out." << std::endl;
}

// ---------------------------------------------------------------- plan use

void test_plan_is_consumed_front_to_back() {
    goap::Planner p;
    p.add_action(pursue());
    p.add_action(eat());
    goap::Plan plan = p.plan({{"hungry", 1}, {"food_known", 1}, {"at_food", 0}},
                             {"SATIATED", {{"hungry", 0}}, 1.0f});

    check(plan.next_action() && plan.next_action()->name == "PURSUE",
          "next_action is the front of the plan");
    plan.pop_action();
    check(plan.size() == 1, "pop_action removes it");
    check(plan.next_action() && plan.next_action()->name == "EAT",
          "and the next one moves up");
    plan.pop_action();
    check(plan.empty(), "the plan empties");
    check(plan.next_action() == nullptr,
          "and next_action on an empty plan is null rather than a crash");
    plan.pop_action();   // must be harmless
    check(plan.empty(), "popping an empty plan is harmless");
}

}  // namespace

int main() {
    std::cout << "GOAP planner (src/npc-ai/goap_system)" << std::endl;
    test_world_state_matching();
    test_it_finds_the_canonical_plan();
    test_ordering_is_forced_by_preconditions();
    test_no_plan_when_the_goal_is_unreachable();
    test_a_satisfied_goal_needs_no_actions();
    test_it_prefers_the_cheaper_route();
    test_dynamic_cost_overrides_static_cost();
    test_a_cycle_terminates();
    test_max_depth_semantics();
    test_plans_borrow_from_the_planner();
    test_plan_is_consumed_front_to_back();

    std::cout << tests_passed << " passed, " << tests_failed << " failed";
    if (tests_xfailed)
        std::cout << ", " << tests_xfailed << " known-red (not gating)";
    std::cout << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
