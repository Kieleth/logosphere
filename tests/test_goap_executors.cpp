// The GOAP execution layer: registry, plan executor, and the seam where
// a plan of action NAMES becomes movement.
//
// The last unguarded part of src/npc-ai/ (roadmap #37, item 2). The
// planner and the pathfinder were guarded in PR #35; this is what sits
// between them and a creature actually doing something:
//
//   Plan (names) -> GOAPPlanExecutor -> ExecutorRegistry -> ActionExecutor
//                        |                                        |
//                        +-- applies effects on success           +-- MovementIntent
//
// Written to find out what it DOES. The interesting cases are the ones a
// creature hits on a bad day, because the happy path was clearly walked
// by hand during development and the rest was not:
//   - no registry connected at all
//   - an action nobody registered
//   - an action that FAILS rather than succeeds
//   - reset() mid-action, which builds a context out of nothing
//   - a target sitting at the world origin
//
// Uses a recording stub executor rather than the shipped ones, so a
// failure here is the LOOP and not a creature behaviour. The shipped
// executors get their own file.
//
// Headless-safe: pure std, no physics, no rendering.
//
// Usage:
//   ./build/test_goap_executors

#undef NDEBUG

#include "npc-ai/executor_registry.h"
#include "npc-ai/goap_plan_executor.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_xfailed = 0;

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

// ExecutionContext / ExecutionResult / ActionExecutor / MovementIntent
// live in the GLOBAL namespace; only the registry and the loop are in
// npc_ai.
using npc_ai::ExecutorRegistry;
using npc_ai::CreatureParams;
using npc_ai::GOAPPlanExecutor;

// WorldState is a plain unordered_map<string,int>, so "is this flag set"
// has to be spelled out rather than assumed.
bool flag(const goap::WorldState& ws, const std::string& key) {
    auto it = ws.find(key);
    return it != ws.end() && it->second != 0;
}

// An executor that records what it was asked and answers however the
// test needs. Everything the loop does to an executor is observable.
struct Spy : public ActionExecutor {
    std::string label;
    // What to return from execute(): complete after N calls, with success.
    int  complete_after = -1;      // -1 = never completes
    bool succeed = true;

    // Recorded
    int executes = 0;
    int starts = 0;
    int ends = 0;
    bool last_end_success = false;
    float last_target_x = 0, last_target_y = 0;
    bool last_had_target = false;
    // on_end is where a null context bites, so record what we were given.
    bool end_saw_world_state = false;
    bool end_saw_timer = false;

    explicit Spy(std::string n) : label(std::move(n)) {}

    const char* name() const override { return label.c_str(); }

    ExecutionResult execute(const ExecutionContext& ctx) override {
        ++executes;
        last_target_x = ctx.target_x;
        last_target_y = ctx.target_y;
        last_had_target = ctx.has_target;
        MovementIntent m;
        m.is_moving = true;
        m.forward_velocity = ctx.run_speed;
        if (complete_after >= 0 && executes >= complete_after) {
            return succeed ? ExecutionResult::succeeded(m)
                           : ExecutionResult::failed(m);
        }
        return ExecutionResult::in_progress(m);
    }

    void on_start(ExecutionContext& ctx) override { ++starts; (void)ctx; }

    void on_end(const ExecutionContext& ctx, bool success) override {
        ++ends;
        last_end_success = success;
        end_saw_world_state = (ctx.world_state != nullptr);
        end_saw_timer = (ctx.action_timer != nullptr);
    }
};

// goap::Plan holds const Action*, so the actions have to be owned by
// something that outlives the plan. Getting this wrong is a dangling
// read the loop would happily follow.
struct PlanStore {
    std::vector<std::unique_ptr<goap::Action>> owned;

    goap::Plan build(const std::vector<std::string>& names) {
        goap::Plan p;
        p.valid = true;
        for (const auto& n : names) {
            auto a = std::make_unique<goap::Action>();
            a->name = n;
            a->effects[n + "_done"] = 1;
            p.actions.push_back(a.get());
            owned.push_back(std::move(a));
        }
        return p;
    }
};

CreatureParams default_params() {
    CreatureParams p;
    p.run_speed = 4.0f;
    p.arrival_distance = 1.0f;
    return p;
}

// ------------------------------------------------------------ registry

void test_the_registry_dispatches_by_name() {
    ExecutorRegistry reg;
    auto spy = std::make_unique<Spy>("PURSUE");
    Spy* raw = spy.get();
    reg.register_executor("PURSUE", std::move(spy));

    std::cout << "  [measure] registry size after one register: " << reg.size()
              << std::endl;
    check(reg.size() == 1, "the executor is held");
    check(reg.has_executor("PURSUE"), "and found by its action name");
    check(reg.get_executor("PURSUE") == raw, "get_executor returns it");

    ExecutionContext ctx;
    reg.execute("PURSUE", ctx);
    check(raw->executes == 1, "execute() reaches the executor");
}

// An action nobody registered must not be silently ignored: a plan that
// names it would otherwise stall forever with no diagnosis.
void test_an_unregistered_action_fails_rather_than_hangs() {
    ExecutorRegistry reg;
    ExecutionContext ctx;
    ExecutionResult r = reg.execute("NOPE", ctx);

    std::cout << "  [measure] unregistered action -> completed=" << r.completed
              << " success=" << r.success << std::endl;
    check(r.completed, "it reports completed, so the plan can move on");
    check(!r.success, "and reports failure, so the caller knows why");
    check(!reg.has_executor("NOPE"), "and nothing was created by asking");
}

void test_registering_twice_replaces() {
    ExecutorRegistry reg;
    reg.register_executor("EAT", std::make_unique<Spy>("first"));
    auto second = std::make_unique<Spy>("second");
    Spy* raw = second.get();
    reg.register_executor("EAT", std::move(second));

    std::cout << "  [measure] size after re-registering the same name: "
              << reg.size() << std::endl;
    check(reg.size() == 1, "replaced, not duplicated");
    check(reg.get_executor("EAT") == raw, "and the new one wins");
}

void test_a_null_executor_is_refused() {
    ExecutorRegistry reg;
    reg.register_executor("EAT", nullptr);
    std::cout << "  [measure] size after registering nullptr: " << reg.size()
              << std::endl;
    check(reg.size() == 0, "a null executor is not stored");
    check(!reg.has_executor("EAT"), "so the name stays unclaimed");
    // The control: it must not have made the registry unusable either.
    reg.register_executor("EAT", std::make_unique<Spy>("real"));
    check(reg.has_executor("EAT"), "and a real one still registers after");
}

void test_start_and_end_on_an_unknown_action_are_harmless() {
    ExecutorRegistry reg;
    ExecutionContext ctx;
    reg.start_action("GHOST", ctx);
    reg.end_action("GHOST", ctx, true);
    check(true, "start/end on an unregistered action does not crash");
}

// ------------------------------------------------------- the plan loop

void test_a_plan_runs_to_completion_and_applies_effects() {
    ExecutorRegistry reg;
    auto pursue = std::make_unique<Spy>("PURSUE");
    pursue->complete_after = 3;
    Spy* p_raw = pursue.get();
    reg.register_executor("PURSUE", std::move(pursue));

    auto eat = std::make_unique<Spy>("EAT");
    eat->complete_after = 2;
    Spy* e_raw = eat.get();
    reg.register_executor("EAT", std::move(eat));

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);

    PlanStore store; goap::Plan plan = store.build({"PURSUE", "EAT"});
    goap::WorldState ws;
    auto params = default_params();

    int frames = 0;
    bool plan_done = false;
    while (frames < 100 && !plan_done) {
        auto r = exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);
        plan_done = r.plan_completed;
        ++frames;
    }

    std::cout << "  [measure] two actions (3 + 2 frames) finished in "
              << frames << " frames; PURSUE executed " << p_raw->executes
              << ", EAT " << e_raw->executes << std::endl;
    check(plan_done, "the plan completes");
    check(frames == 5, "in exactly the frames the actions asked for (" +
          std::to_string(frames) + ")");
    check(p_raw->starts == 1 && e_raw->starts == 1,
          "each action started once");
    check(p_raw->ends == 1 && e_raw->ends == 1, "and ended once");
    // Effects are the planner's contract: without them a replan would
    // re-derive the same plan forever.
    check(flag(ws, "PURSUE_done") && flag(ws, "EAT_done"),
          "both actions' effects reached the world state");
}

// A failed action must NOT write its effects, or the world state claims
// something happened that did not.
void test_a_failed_action_applies_no_effects() {
    ExecutorRegistry reg;
    auto spy = std::make_unique<Spy>("PURSUE");
    spy->complete_after = 2;
    spy->succeed = false;
    Spy* raw = spy.get();
    reg.register_executor("PURSUE", std::move(spy));

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);

    PlanStore store; goap::Plan plan = store.build({"PURSUE"});
    goap::WorldState ws;
    auto params = default_params();

    npc_ai::PlanExecutionResult r;
    for (int f = 0; f < 10 && !r.plan_completed; ++f)
        r = exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);

    std::cout << "  [measure] failed action: action_failed=" << r.action_failed
              << " effects_written=" << flag(ws, "PURSUE_done")
              << " needs_replan=" << r.needs_replan << std::endl;
    check(r.action_failed, "the failure is reported");
    check(!flag(ws, "PURSUE_done"),
          "and its effects are NOT applied to the world state");
    check(raw->last_end_success == false,
          "the executor's on_end was told it failed");
}

// A plan is a sequence with a reason. If PURSUE fails, EAT is being run
// at a place the creature never reached. Measure what actually happens.
void test_what_a_failure_does_to_the_rest_of_the_plan() {
    ExecutorRegistry reg;
    auto pursue = std::make_unique<Spy>("PURSUE");
    pursue->complete_after = 1;
    pursue->succeed = false;
    reg.register_executor("PURSUE", std::move(pursue));
    auto eat = std::make_unique<Spy>("EAT");
    eat->complete_after = 1;
    Spy* e_raw = eat.get();
    reg.register_executor("EAT", std::move(eat));

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);
    PlanStore store; goap::Plan plan = store.build({"PURSUE", "EAT"});
    goap::WorldState ws;
    auto params = default_params();

    for (int f = 0; f < 10 && !plan.empty(); ++f)
        exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);

    std::cout << "  [measure] PURSUE failed, then EAT executed "
              << e_raw->executes << " time(s)" << std::endl;
    // Documenting the real behaviour: a failed action is popped and the
    // plan carries on. Whether that is right is a design question
    // (#37), but it must not be a surprise.
    check(e_raw->executes > 0,
          "the plan CONTINUES past a failed action rather than aborting");
}

void test_no_registry_is_reported_honestly() {
    GOAPPlanExecutor exec;              // never given a registry
    exec.set_verbose(false);
    PlanStore store; goap::Plan plan = store.build({"PURSUE"});
    goap::WorldState ws;
    auto params = default_params();

    auto r = exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);
    std::cout << "  [measure] no registry -> plan_completed=" << r.plan_completed
              << " action_failed=" << r.action_failed
              << " needs_replan=" << r.needs_replan
              << ", plan still holds " << plan.size() << " action(s)"
              << std::endl;

    check(plan.size() == 1, "the plan is untouched, so nothing was faked");
    // A caller that trusts plan_completed will believe the creature did
    // its job. Nothing distinguishes this from success.
    check(r.action_failed || r.needs_replan,
          "a missing registry is reported as a failure, not as a completed plan "
          "(#45)",
          /*xfail=*/true);
}

void test_an_empty_plan_completes_without_executing() {
    ExecutorRegistry reg;
    auto spy = std::make_unique<Spy>("PURSUE");
    Spy* raw = spy.get();
    reg.register_executor("PURSUE", std::move(spy));

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);
    goap::Plan plan;                    // empty, and not marked valid
    goap::WorldState ws;
    auto params = default_params();

    auto r = exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);
    std::cout << "  [measure] empty plan -> plan_completed=" << r.plan_completed
              << ", executor calls " << raw->executes << std::endl;
    check(r.plan_completed, "an empty plan is complete");
    check(raw->executes == 0, "and nothing was executed to discover that");
}

// ------------------------------------------------------- goal checking

void test_goal_satisfaction_decides_replan() {
    ExecutorRegistry reg;
    auto spy = std::make_unique<Spy>("PURSUE");
    spy->complete_after = 1;
    reg.register_executor("PURSUE", std::move(spy));

    goap::Goal goal;
    goal.name = "fed";
    goal.desired_state["fed"] = 1;   // the plan never sets this

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);
    exec.set_goal(&goal);

    PlanStore store; goap::Plan plan = store.build({"PURSUE"});
    goap::WorldState ws;
    auto params = default_params();

    npc_ai::PlanExecutionResult r;
    for (int f = 0; f < 10 && !r.plan_completed; ++f)
        r = exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);

    std::cout << "  [measure] plan done but goal unmet: goal_achieved="
              << r.goal_achieved << " needs_replan=" << r.needs_replan
              << std::endl;
    check(!r.goal_achieved, "the goal is correctly seen as unmet");
    check(r.needs_replan,
          "and a replan is requested rather than reporting success");

    // The control: same shape, but the plan DOES satisfy the goal.
    ExecutorRegistry reg2;
    auto spy2 = std::make_unique<Spy>("PURSUE");
    spy2->complete_after = 1;
    reg2.register_executor("PURSUE", std::move(spy2));
    goap::Goal goal2;
    goal2.desired_state["PURSUE_done"] = 1;
    GOAPPlanExecutor exec2;
    exec2.set_registry(&reg2);
    exec2.set_verbose(false);
    exec2.set_goal(&goal2);
    PlanStore store2; goap::Plan plan2 = store2.build({"PURSUE"});
    goap::WorldState ws2;
    npc_ai::PlanExecutionResult r2;
    for (int f = 0; f < 10 && !r2.plan_completed; ++f)
        r2 = exec2.update(plan2, ws2, 0, 0, 0, 1.0f / 60.0f, params);

    std::cout << "  [measure] control, goal met: goal_achieved="
              << r2.goal_achieved << " needs_replan=" << r2.needs_replan
              << std::endl;
    check(r2.goal_achieved, "a satisfied goal is reported achieved");
    check(!r2.needs_replan, "and does not ask for a replan");
}

// ----------------------------------------------------- targets, and #53

// build_context picks the target from the action NAME. The engine
// therefore knows the words PURSUE, EAT, INVESTIGATE_SMELL and
// ESCAPE_BLOCK, which is the boundary question in roadmap #37 item 3.
// Whatever the answer, the routing must at least be correct.
void test_the_target_follows_the_action_name() {
    ExecutorRegistry reg;
    auto pursue = std::make_unique<Spy>("PURSUE");
    pursue->complete_after = 1;
    Spy* p_raw = pursue.get();
    reg.register_executor("PURSUE", std::move(pursue));
    auto smell = std::make_unique<Spy>("INVESTIGATE_SMELL");
    smell->complete_after = 1;
    Spy* s_raw = smell.get();
    reg.register_executor("INVESTIGATE_SMELL", std::move(smell));

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);

    auto params = default_params();
    params.food_x = 10.0f;  params.food_y = 11.0f;
    params.smell_target_x = -5.0f; params.smell_target_y = -6.0f;

    PlanStore store; goap::Plan plan = store.build({"PURSUE", "INVESTIGATE_SMELL"});
    goap::WorldState ws;
    for (int f = 0; f < 6 && !plan.empty(); ++f)
        exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);

    std::cout << "  [measure] PURSUE got target (" << p_raw->last_target_x
              << ", " << p_raw->last_target_y << "), INVESTIGATE_SMELL got ("
              << s_raw->last_target_x << ", " << s_raw->last_target_y << ")"
              << std::endl;
    check(p_raw->last_target_x == 10.0f && p_raw->last_target_y == 11.0f,
          "PURSUE is sent to the food");
    check(s_raw->last_target_x == -5.0f && s_raw->last_target_y == -6.0f,
          "INVESTIGATE_SMELL is sent to the smell");
}

// ESCAPE_BLOCK picks its target with
//   food_x != 0 ? food_x : smell_x     (and the same, separately, for y)
// Zero is a real coordinate. A creature whose food sits at the world
// origin is told to escape toward the smell instead, and because the
// axes are chosen INDEPENDENTLY it can be handed a point that is
// neither location.
void test_a_target_on_the_origin_is_mistaken_for_absent() {
    ExecutorRegistry reg;
    auto esc = std::make_unique<Spy>("ESCAPE_BLOCK");
    esc->complete_after = 1;
    Spy* raw = esc.get();
    reg.register_executor("ESCAPE_BLOCK", std::move(esc));

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);

    auto params = default_params();
    params.food_x = 0.0f;   params.food_y = 0.0f;      // food AT the origin
    params.smell_target_x = 99.0f; params.smell_target_y = 77.0f;

    PlanStore store; goap::Plan plan = store.build({"ESCAPE_BLOCK"});
    goap::WorldState ws;
    for (int f = 0; f < 4 && !plan.empty(); ++f)
        exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);

    std::cout << "  [measure] food at the origin, smell at (99, 77) -> "
              << "ESCAPE_BLOCK got (" << raw->last_target_x << ", "
              << raw->last_target_y << ")" << std::endl;
    check(raw->last_target_x == 0.0f && raw->last_target_y == 0.0f,
          "a target at the origin is used, not treated as unset (#44)",
          /*xfail=*/true);

    // The mixed-axis case, which is the worse half: food on the y axis
    // only. x comes from food, y comes from smell, and the result is a
    // place nothing is.
    ExecutorRegistry reg2;
    auto esc2 = std::make_unique<Spy>("ESCAPE_BLOCK");
    esc2->complete_after = 1;
    Spy* raw2 = esc2.get();
    reg2.register_executor("ESCAPE_BLOCK", std::move(esc2));
    GOAPPlanExecutor exec2;
    exec2.set_registry(&reg2);
    exec2.set_verbose(false);
    auto p2 = default_params();
    p2.food_x = 4.0f;  p2.food_y = 0.0f;               // on the x axis
    p2.smell_target_x = 99.0f; p2.smell_target_y = 77.0f;
    PlanStore store2; goap::Plan plan2 = store2.build({"ESCAPE_BLOCK"});
    goap::WorldState ws2;
    for (int f = 0; f < 4 && !plan2.empty(); ++f)
        exec2.update(plan2, ws2, 0, 0, 0, 1.0f / 60.0f, p2);

    std::cout << "  [measure] food at (4, 0), smell at (99, 77) -> "
              << "ESCAPE_BLOCK got (" << raw2->last_target_x << ", "
              << raw2->last_target_y << ")" << std::endl;
    check(raw2->last_target_x == 4.0f && raw2->last_target_y == 0.0f,
          "the two axes come from the SAME location, #44 (" +
          std::to_string(raw2->last_target_x) + ", " +
          std::to_string(raw2->last_target_y) + ")",
          /*xfail=*/true);
}

// ------------------------------------------------------------- reset()

// reset() ends the in-flight action with a default-constructed context:
// every pointer null except the timer. An executor whose on_end touches
// the world state or the food state would dereference null.
void test_reset_mid_action_hands_the_executor_an_empty_context() {
    ExecutorRegistry reg;
    auto spy = std::make_unique<Spy>("PURSUE");
    Spy* raw = spy.get();                     // never completes
    reg.register_executor("PURSUE", std::move(spy));

    GOAPPlanExecutor exec;
    exec.set_registry(&reg);
    exec.set_verbose(false);
    PlanStore store; goap::Plan plan = store.build({"PURSUE"});
    goap::WorldState ws;
    auto params = default_params();

    exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);
    check(raw->starts == 1, "the action is in flight");

    exec.reset();
    std::cout << "  [measure] after reset mid-action: ends=" << raw->ends
              << " on_end saw world_state=" << raw->end_saw_world_state
              << " timer=" << raw->end_saw_timer << std::endl;
    check(raw->ends == 1, "reset ends the in-flight action");
    check(raw->last_end_success == false, "reporting it as unsuccessful");
    // Documented, not asserted as correct: an executor that reads the
    // world state in on_end gets nullptr here.
    check(!raw->end_saw_world_state,
          "and hands it a context with NO world state, which any on_end "
          "that reads one must survive");

    // After reset the loop must be reusable, not wedged.
    exec.update(plan, ws, 0, 0, 0, 1.0f / 60.0f, params);
    check(raw->starts == 2, "and the next update starts the action again");
}

void test_reset_without_a_registry_is_harmless() {
    GOAPPlanExecutor exec;
    exec.set_verbose(false);
    exec.reset();
    check(true, "reset on a fresh executor does not crash");
}

}  // namespace

int main() {
    std::cout << "GOAP execution layer (registry + plan executor)" << std::endl;
    test_the_registry_dispatches_by_name();
    test_an_unregistered_action_fails_rather_than_hangs();
    test_registering_twice_replaces();
    test_a_null_executor_is_refused();
    test_start_and_end_on_an_unknown_action_are_harmless();

    test_a_plan_runs_to_completion_and_applies_effects();
    test_a_failed_action_applies_no_effects();
    test_what_a_failure_does_to_the_rest_of_the_plan();
    test_no_registry_is_reported_honestly();
    test_an_empty_plan_completes_without_executing();

    test_goal_satisfaction_decides_replan();
    test_the_target_follows_the_action_name();
    test_a_target_on_the_origin_is_mistaken_for_absent();

    test_reset_mid_action_hands_the_executor_an_empty_context();
    test_reset_without_a_registry_is_harmless();

    std::cout << tests_passed << " passed, " << tests_failed << " failed";
    if (tests_xfailed)
        std::cout << ", " << tests_xfailed << " known-red (not gating)";
    std::cout << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
