// The search: walls, meander, smell, walls, getting lost, and dinner.
//
// The full NPC loop, on screen: SENSES write facts, the PLANNER turns
// facts into a plan, EXECUTORS turn the plan into movement, and when
// reality disagrees with the plan — a scent fades, a wall was in the
// way, the carcass is not where the nose said — the precondition gate
// breaks the plan honestly and the brain replans. "Getting lost" is not
// scripted anywhere below; it is what replanning looks like when the
// world takes information away.
//
// The story a run tells (watch the event log):
//   MEANDER    no scent, no sight: amble between random waypoints
//   SMELL      the nose crosses the odor radius: replan, follow it
//   (walls)    the path detours; at the radius edge the scent flickers
//   LOST       enough misses in a row: the fact drops, plan is stale,
//              back to MEANDER — visibly lost, near the goal
//   SIGHT      the cone finally clears a wall edge: replan, PURSUE
//   EAT        ten bites, carcass gone, FED
//
// Facts are owned by SENSORS, not by action effects. Actions still
// declare optimistic effects (GOAP plans over them), but every frame
// the sensors overwrite smells_food / sees_food / at_food with what is
// actually true, and the gate does the rest. An executor can therefore
// "succeed" at walking to a stale scent point and the world state still
// refuses to say the food was found — that honesty is load-bearing.
//
// Headless asserts the structure (meandered first, smelled before it
// saw, saw before it ate, fed at the end) and runs the control: a
// carcass with NO odor leaves the predator meandering forever — same
// walls, same eyes, nothing to find. Losses are probabilistic (the
// smell model's own dropout), so they are MEASURED, not asserted.
//
// Usage:
//   ./build/at_predator_search                     numbers, asserted
//   LOGOSPHERE_VISUAL=1 ./build/at_predator_search
//     SPACE releases the predator, SPACE again reruns, ESC quits.
//     The window WAITS for you.

#undef NDEBUG

#include "core/engine.h"
#include "logosphere/capability/capability_store.h"
#include "logosphere/damage/damage_system.h"
#include "logosphere/interaction/contact_response.h"
#include "logosphere/kg/ontology_registry.h"
#include "src/generated/predator_ontology_registry.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "sense_system.h"
#include "ui/ui_system.h"
#include "ui/text_window.h"
#include "particle.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/physics/bvh.h"

#include "ai/eat_executor.h"
#include "ai/food_state.h"
#include "ai/meander_executor.h"
#include "ai/predator_context.h"
#include "npc-ai/executors/pursue_executor.h"
#include "npc-ai/executors/investigate_smell_executor.h"
#include "npc-ai/goap_plan_executor.h"
#include "npc-ai/goap_system.h"
#include "npc-ai/pathfinding_system.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

namespace {

bool g_visual = false;
bool g_quit = false;

void bring_to_front(Engine& e) {
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return;
    glfwShowWindow(win);
    glfwRestoreWindow(win);
    for (int i = 0; i < 8; ++i) { glfwPollEvents(); glfwFocusWindow(win); }
    std::cout << "    window focused: "
              << (glfwGetWindowAttrib(win, GLFW_FOCUSED) ? "yes"
                                                         : "NO - click it")
              << std::endl;
}

bool pump(Engine& e) {
    e.get_platform()->poll_events();
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return true;
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; return false; }
    if (e.get_platform()->should_close()) { g_quit = true; return false; }
    return true;
}

bool space_pressed(Engine& e) {
    static bool was_down = false;
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return false;
    const bool down = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
    const bool edge = down && !was_down;
    was_down = down;
    return edge;
}

struct OverlayPixels { size_t lit = 0; size_t text = 0; };
OverlayPixels overlay_pixels(Engine& e) {
    const PixelBuffer& ui = e.get_ui_overlay_buffer();
    OverlayPixels out;
    for (int y = 0; y < ui.height(); ++y)
        for (int x = 0; x < ui.width(); ++x) {
            const auto px = ui.get_pixel(x, y);
            if (px.a > 0) ++out.lit;
            if (px.a > 220) ++out.text;
        }
    return out;
}

// Timestamped event log, newest kept, oldest dropped.
struct EventLog {
    std::deque<std::string> lines;
    int smells = 0, losses = 0, sightings = 0, replans = 0;
    void add(float t, const std::string& s) {
        char buf[120];
        std::snprintf(buf, sizeof buf, "%6.1fs  %s", t, s.c_str());
        lines.push_back(buf);
        while (lines.size() > 9) lines.pop_front();
    }
};

}  // namespace

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::srand(20260808u);   // the smell model's dropout uses the global
                             // stream; seeded so headless is reproducible
    std::cout << "The search: walls, meander, smell, getting lost (NPC loop)"
              << std::endl;

    EngineConfig cfg;
    cfg.create_display = g_visual;
    cfg.window_width = 1100;
    cfg.window_height = 720;
    cfg.window_title = "a predator searches: meander, scent, lost, found";
    cfg.show_debug_overlay = false;
    cfg.enable_chat_window = false;
    Engine engine(nullptr);
    if (engine.initialize(cfg) < 0) {
        std::cout << "FAIL: Engine::initialize" << std::endl;
        return 1;
    }
    auto& ps = engine.get_particle_system();

    // ---- the arena: 40x40, three walls the story needs ----
    pathfinding::NavGrid grid;
    grid.init(-20.0f, -20.0f, 20.0f, 20.0f, 1.0f);

    auto wall = [&](float x, float y, float w, float d) {
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = 1.1f;
        p.width = w; p.height = d; p.thickness = 2.2f;
        p.size = std::max(w, d);
        p.r = 0.42f; p.g = 0.40f; p.b = 0.38f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        engine.add_particle(p);
        grid.set_blocked_rect(x - w * 0.5f - 0.6f, y - d * 0.5f - 0.6f,
                              x + w * 0.5f + 0.6f, y + d * 0.5f + 0.6f, true);
    };
    // Between the meandering ground (SW) and the carcass (NE): a long
    // wall with a gap, a spur past the gap, and one near the carcass so
    // the final sighting needs a corner cleared.
    wall(-2.0f, 2.0f, 14.0f, 1.2f);    // long east-west wall
    wall(7.0f, 7.0f, 1.2f, 9.0f);      // north-south spur
    wall(12.0f, 13.5f, 7.0f, 1.2f);    // screen in front of the carcass

    if (g_visual) {
        for (int gx = -2; gx <= 2; ++gx)
            for (int gy = -2; gy <= 2; ++gy) {
                Particle p{};
                p.shape = ParticleShape::BOX;
                p.x = gx * 8.0f; p.y = gy * 8.0f; p.z = -0.4f;
                p.width = p.height = 8.0f; p.thickness = 0.8f;
                p.size = 8.0f;
                const float t = ((gx + gy) & 1) ? 0.30f : 0.26f;
                p.r = t - 0.03f; p.g = t + 0.05f; p.b = t; p.a = 1.0f;
                p.SetMaterial(Materials::Type::DIRT);
                p.solver_mode = ParticleSolverMode::KINEMATIC;
                p.is_at_rest = true;
                engine.add_particle(p);
            }
    }

    // Game vocabulary from the game's own SCHEMA: Predator and Thorns
    // are declared in examples/predator/schema/predator.yaml and arrive
    // through the generated registry — the documented pattern, and the
    // one the entity-type scanner can see (a hand-rolled runtime extend
    // failed the headless-windows lane precisely because no ontology
    // layer declared the names).
    engine.get_kg().extendOntology(predator::ontology::registry());
    auto& kg = engine.get_kg();

    // Predator, SW, far outside the odor radius. KG-BACKED — an entity
    // with a leg part whose particle is bound — because the contact
    // producer resolves particles through the KG, and a creature
    // outside the ontology is invisible to every contact rule.
    const kg::EntityID pred_entity = kg.createEntity("Predator");
    const kg::EntityID pred_leg = kg.createEntity("Leg");
    kg.createRelation(pred_entity, "HAS_PART", pred_leg);
    kg.setProperty(pred_leg, "body_part_name", "leg");
    kg.setProperty(pred_leg, "health", "100");
    kg.setProperty(pred_leg, "max_health", "100");
    kg.setProperty(pred_leg, "cap_list", "locomotion");
    // The capability rule: a mangled leg halves the pace. THIS is the
    // store's first consumer earning its keep.
    kg.setProperty(pred_leg, "rule.0.trigger", "health_below:60");
    kg.setProperty(pred_leg, "rule.0.effect", "speed_cap:0.5");

    Particle pred{};
    pred.shape = ParticleShape::SPHERE;
    pred.x = -14.0f; pred.y = -12.0f; pred.z = 0.8f;
    pred.width = pred.height = pred.thickness = 1.6f;
    pred.size = 1.6f;
    pred.r = 0.95f; pred.g = 0.75f; pred.b = 0.20f; pred.a = 1.0f;
    pred.SetMaterial(Materials::Type::FLESH);
    pred.solver_mode = ParticleSolverMode::KINEMATIC;
    pred.is_at_rest = true;
    const int pred_idx = ps.add_particle_to_entity(pred, &kg, pred_leg);

    // The thorn patch: a band across the southern approach to the
    // carcass. WALKABLE — thorns do not stop a predator, they bill it —
    // so they are absent from the nav grid and present as bodies.
    // Placed on the measured route: the seeded hunt crosses the thorn
    // latitude at x ~4.4 (west corridor, probed), so the band spans it
    // with margin for run-to-run drift.
    // Plants DO NOT touch each other (0.3 m gaps): the first layout had
    // them edge-to-edge, and a thorn-thorn contact satisfies
    // with_type:Thorns from BOTH sides — six of eight "wounds" were
    // thorns stinging thorns, billed to a leg neither of them has.
    for (int i = 0; i < 4; ++i) {
        const float tx = 2.0f + i * 2.1f;
        const float ty = 10.6f;
        const kg::EntityID te = kg.createEntity("Thorns");
        Particle th{};
        th.shape = ParticleShape::BOX;
        th.x = tx; th.y = ty; th.z = 0.35f;
        th.width = 1.8f; th.height = 1.6f; th.thickness = 0.7f;
        th.size = 1.8f;
        th.r = 0.20f; th.g = 0.45f; th.b = 0.12f; th.a = 1.0f;
        th.SetMaterial(Materials::Type::FLESH);
        th.solver_mode = ParticleSolverMode::KINEMATIC;
        th.is_at_rest = true;
        ps.add_particle_to_entity(th, &kg, te);
    }

    // Carcass, NE, behind the walls, smellable within 14 m.
    const float kCarcassFull = 1.0f;
    const float kFoodX = 12.0f, kFoodY = 16.5f;
    Particle carc{};
    carc.shape = ParticleShape::BOX;
    carc.x = kFoodX; carc.y = kFoodY; carc.z = 0.5f;
    carc.width = carc.height = carc.thickness = kCarcassFull;
    carc.size = kCarcassFull;
    carc.r = 0.90f; carc.g = 0.12f; carc.b = 0.10f; carc.a = 1.0f;
    carc.SetMaterial(Materials::Type::FLESH);
    carc.material_density = 1000.0f;
    carc.odor_type = OdorType::LIVING_FLESH;
    carc.odor_radius = 17.0f;
    carc.odor_intensity = 1.0f;
    carc.solver_mode = ParticleSolverMode::KINEMATIC;
    carc.is_at_rest = true;
    const int carc_idx = engine.add_particle(carc);

    // ---- GAME policy: facts, actions, goal ----
    // Facts: hungry (owned by EAT), smells_food / sees_food / at_food
    // (owned by the SENSORS, overwritten every frame).
    goap::Action meander;
    meander.name = "MEANDER";
    meander.preconditions = {{"hungry", 1}, {"smells_food", 0}, {"sees_food", 0}};
    meander.effects = {{"smells_food", 1}};      // optimistic: "walking
                                                 // around finds scents"
    meander.cost = 5.0f;                         // last resort
    goap::Action investigate;
    investigate.name = "INVESTIGATE_SMELL";
    investigate.preconditions = {{"smells_food", 1}, {"sees_food", 0}};
    investigate.effects = {{"sees_food", 1}};
    investigate.target_key = "scent";
    goap::Action pursue;
    pursue.name = "PURSUE";
    pursue.preconditions = {{"sees_food", 1}};
    pursue.effects = {{"at_food", 1}};
    pursue.target_key = "food";
    goap::Action eat;
    eat.name = "EAT";
    eat.preconditions = {{"at_food", 1}};
    eat.effects = {{"hungry", 0}};
    eat.target_key = "food";
    goap::Goal fed;
    fed.name = "fed";
    fed.desired_state["fed_now"] = 1;
    fed.desired_state = {{"hungry", 0}};

    goap::Planner planner;
    planner.add_action(meander);
    planner.add_action(investigate);
    planner.add_action(pursue);
    planner.add_action(eat);

    npc_ai::ExecutorRegistry registry;
    {
        auto m = std::make_unique<predator::MeanderExecutor>(20260808u);
        m->meander_radius = 12.0f;   // longer legs: the arena is 40 m
                                     // and the nose is in one corner
        registry.register_executor("MEANDER", std::move(m));
    }
    registry.register_executor("INVESTIGATE_SMELL",
        std::make_unique<npc_ai::InvestigateSmellExecutor>());
    registry.register_executor("PURSUE", std::make_unique<npc_ai::PursueExecutor>());
    registry.register_executor("EAT", std::make_unique<npc_ai::EatExecutor>());

    // ---- the wound chain: contact -> damage -> health -> store ----
    DamageSystem damage(nullptr, &engine.get_event_bus());
    damage.set_kg(&kg);
    int wounds = 0;
    logosphere::interaction::ContactEffectRegistry::instance().register_effect(
        "wound_leg",
        [&](logosphere::interaction::ContactEffectContext& ctx,
            const std::string& args) {
            float amount = 15.0f;
            try { amount = std::stof(args); } catch (...) {}
            // Only a landed wound counts: a contact whose self has no
            // leg (thorn-thorn, scenery) returns negative and is not a
            // wound, whatever the rule thought.
            if (damage.apply_to_body_part(ctx.contact.self_entity, "leg",
                                          amount, DamageType::Pierce) >= 0.0f)
                ++wounds;
        });
    {
        // The predator's OWN rule: how I react to touching thorns.
        // Conditions ask about the other party, effects act on self.
        const kg::EntityID rule = kg.createEntity("TransformationRule");
        kg.setProperty(rule, "trigger", "on_contact");
        kg.setProperty(rule, "condition", "with_type:Thorns");
        kg.setProperty(rule, "effect", "wound_leg:45");
        const size_t n = engine.get_interaction_system().load_rules_from_kg(kg);
        std::cout << "    contact rules loaded: " << n << std::endl;
    }

    // The store: the predator opts in; from here the leg rule is live
    // for a creature no humanoid system has ever heard of.
    engine.get_capability_store().track(
        pred_entity, capability::CapabilityStore::Physical(90.0f, 0.9f, 1.6f));

    pathfinding::Pathfinder finder;
    pathfinding::Path path;
    npc_ai::GOAPPlanExecutor exec;
    exec.set_registry(&registry);
    exec.set_pathfinder(&finder);
    exec.set_nav_grid(&grid);
    exec.set_current_path(&path);
    exec.set_goal(&fed);
    exec.set_verbose(false);

    SenseSystem senses;
    SenseConfig eyes;
    eyes.fov_degrees = 100.0f;
    // MYOPIC ON PURPOSE. With 22 m eyes this predator SAW the carcass
    // across the arena before ever smelling it (measured: sight at
    // frame 1684, smell at 1769) and the whole scent story collapsed.
    // A nose-hunter's ecology is a long nose (17 m odor radius) and
    // short eyes: smell-first is now geometry, not a script.
    eyes.vision_range = 9.0f;
    eyes.ray_count = 48;     // 0.33 m ray gaps at 9 m: no sampling flicker
    SmellConfig nose;
    nose.attracted_to = {OdorType::LIVING_FLESH};
    nose.sensitivity = 1.0f;
    nose.state_factor = 1.0f;

    npc_ai::FoodState food = npc_ai::FoodState::from_particle(carc, 0.5f);
    const float initial_mass = food.remaining_g;
    predator::PredatorContext pctx;
    pctx.mouth_volume_cm3 = 110000.0f;
    pctx.food_state = &food;

    npc_ai::CreatureParams params;
    params.walk_speed = 2.2f;
    params.run_speed = 4.5f;
    params.arrival_distance = 1.4f;
    params.game_data = &pctx;

    // ---- the brain's per-run state ----
    float px = -14.0f, py = -12.0f, facing = 0.0f;
    goap::WorldState world;
    goap::Plan plan;
    EventLog log;
    float clock = 0.0f;
    int frames = 0;
    int frames_meandering = 0;
    int first_smell_frame = -1, first_sight_frame = -1;
    int smell_miss_streak = 0, sight_miss_streak = 0;
    int stuck_frames = 0;      // proprioception: pushing but not moving
    float stuck_ref_x = 0, stuck_ref_y = 0;
    int cast_n = 0;   // how many times a scent-following plan went stale:
                      // each retry swings the search wider around the
                      // blocked line (casting, what a real nose does at
                      // a wall)
    bool smells = false, sees = false;
    float scent_x = 0, scent_y = 0, seen_x = 0, seen_y = 0;
    int bites_seen = 0;
    float last_mass = initial_mass;
    float max_step_whole = 0.0f, max_step_wounded = 0.0f;
    bool goal_achieved = false;
    static constexpr int MISSES_BEFORE_LOST = 20;   // smell flickers at
                                                    // range; believe it
                                                    // gone only after a
                                                    // third of a second

    auto replan = [&](const char* why) {
        plan = planner.plan(world, fed);
        exec.reset();
        ++log.replans;
        log.add(clock, std::string("REPLAN  ") + why);
    };

    // The nearest walkable point to (x,y), searched in growing rings.
    // A scent projection inside a wall is not a place a creature can
    // stand; the brain owes its executors reachable targets.
    auto nearest_walkable = [&](float x, float y, float& ox, float& oy) {
        if (grid.is_walkable(x, y)) { ox = x; oy = y; return; }
        for (float r = 0.7f; r <= 4.2f; r += 0.7f) {
            for (int k = 0; k < 12; ++k) {
                const float a = k * 0.5235988f;
                const float cx = x + std::sin(a) * r, cy = y + std::cos(a) * r;
                if (grid.is_walkable(cx, cy)) { ox = cx; oy = cy; return; }
            }
        }
        ox = x; oy = y;   // nothing near: hand it over and let the
                          // pathfinder report no path
    };

    auto reset_scenario = [&]() {
        px = -14.0f; py = -12.0f; facing = 0.0f;
        food = npc_ai::FoodState::from_particle(carc, 0.5f);
        last_mass = food.remaining_g;
        bites_seen = 0;
        frames = 0; clock = 0.0f;
        frames_meandering = 0;
        first_smell_frame = first_sight_frame = -1;
        smell_miss_streak = sight_miss_streak = 0;
        stuck_frames = 0;
        cast_n = 0;
        smells = sees = false;
        goal_achieved = false;
        log = EventLog{};
        kg.setProperty(pred_leg, "health", "100");   // runs start whole
        wounds = 0;
        max_step_whole = max_step_wounded = 0.0f;
        world.clear();
        world["hungry"] = 1;
        world["smells_food"] = 0;
        world["sees_food"] = 0;
        world["at_food"] = 0;
        plan = planner.plan(world, fed);
        exec.reset();
        auto view = ps.lock_particles_for_write();
        view[pred_idx].x = px; view[pred_idx].y = py;
        view[carc_idx].width = view[carc_idx].height =
            view[carc_idx].thickness = kCarcassFull;
        view[carc_idx].size = kCarcassFull;
        view[carc_idx].z = kCarcassFull * 0.5f;
        view[carc_idx].a = 1.0f;
        view[carc_idx].odor_radius = 17.0f;
    };

    const float dt = 1.0f / 60.0f;

    // ---- one frame: sense, replan if the world changed, act ----
    auto sim_step = [&]() -> bool {
        // The engine tick lives HERE, not in the caller: the headless
        // loop skipped it, the shadow BVH the vision cone casts into
        // was never built, and the predator was blind in exactly one
        // of the two modes — measured as cone_hits=0 against a wall
        // four metres dead ahead. Guideline 9, violated and caught.
        engine.update(dt);
        // SENSE. Smell with hysteresis: the model's dropout at range is
        // real, and believing every miss makes a creature that forgets
        // its nose twenty times a second.
        {
            auto view = ps.lock_particles_for_read();
            SmellResult sm = senses.check_smell(nose, px, py, 1.0f, 1.0f,
                                                view.get(), {pred_idx});
            if (sm.detected) {
                smell_miss_streak = 0;
                // Follow the nose — swung wider each time a straight
                // line at the scent produced a stale plan. cast_n = 0
                // is the direct line; each retry alternates side and
                // widens: +0.7, -1.4, +2.1 rad... enough to round a
                // 7 m screen wall by the third cast.
                const float swing =
                    (cast_n == 0) ? 0.0f
                                  : ((cast_n % 2 == 1) ? 1.0f : -1.0f) *
                                        0.7f * ((cast_n + 1) / 2);
                const float dir = sm.direction + swing;
                const float reach = std::min(sm.distance + 2.0f, 9.0f);
                nearest_walkable(px + std::sin(dir) * reach,
                                 py + std::cos(dir) * reach,
                                 scent_x, scent_y);
                if (!smells) {
                    smells = true;
                    ++log.smells;
                    if (first_smell_frame < 0) first_smell_frame = frames;
                    char b[96];
                    std::snprintf(b, sizeof b,
                                  "SMELL   scent, %.1f m off", sm.distance);
                    log.add(clock, b);
                }
            } else if (smells && ++smell_miss_streak > MISSES_BEFORE_LOST) {
                smells = false;
                cast_n = 0;
                ++log.losses;
                log.add(clock, "LOST    the scent is gone");
            }

            // Sight: the cone, walls occluding, with its own hysteresis.
            const BVH* bvh = ps.get_shadow_bvh();
            bool saw = false;
            if (bvh) {
                SenseResult sr = senses.cast_vision_cone(
                    eyes, px, py, 1.0f, facing, view.get(), *bvh, {},
                    {pred_idx});
                if (const SenseTarget* t = sr.find_by_id(carc_idx)) {
                    saw = true;
                    seen_x = t->x; seen_y = t->y;
                }
                // [probe] close to the carcass and still blind: what IS
                // the cone seeing, and where are the eyes pointing?
                const float cd = std::hypot(px - kFoodX, py - kFoodY);
                static int probe_n = 0;
                if (!saw && cd < 8.0f && probe_n++ % 60 == 0) {
                    const float bearing = std::atan2(kFoodX - px, kFoodY - py);
                    std::cout << "  [probe] d=" << cd << " facing=" << facing
                              << " bearing_to_food=" << bearing
                              << " cone_hits=" << sr.visible_targets.size();
                    for (const auto& t : sr.visible_targets)
                        std::cout << " id" << t.particle_id;
                    std::cout << std::endl;
                }
            }
            if (saw) {
                sight_miss_streak = 0;
                if (!sees) {
                    sees = true;
                    cast_n = 0;
                    ++log.sightings;
                    if (first_sight_frame < 0) first_sight_frame = frames;
                    log.add(clock, "SIGHT   there it is");
                }
            } else if (sees && ++sight_miss_streak > MISSES_BEFORE_LOST) {
                sees = false;
                log.add(clock, "LOST    sight of it");
            }
        }

        // FACTS: sensors own perception; effects own only `hungry`.
        const int old_smells = world["smells_food"];
        const int old_sees = world["sees_food"];
        world["smells_food"] = smells ? 1 : 0;
        world["sees_food"] = sees ? 1 : 0;
        world["at_food"] =
            (std::hypot(px - kFoodX, py - kFoodY) < params.arrival_distance + 0.2f)
                ? 1 : 0;

        // The brain replans on UPGRADES (new information opens a better
        // plan). DOWNGRADES need nothing here: the precondition gate
        // refuses stale actions on its own and reports needs_replan.
        if (world["smells_food"] > old_smells) replan("scent acquired");
        if (world["sees_food"] > old_sees) replan("sighted it");

        // ACT.
        params.targets.clear();
        if (smells) params.targets["scent"] = {scent_x, scent_y};
        if (sees || world["at_food"])
            params.targets["food"] = {seen_x, seen_y};
        // The pace is whatever the leg can carry: the store's cap,
        // read fresh each frame. This line is the store's whole point.
        {

            const CapabilityProfile* cap =
                engine.get_capability_store().get(pred_entity);
            const float k = cap ? cap->speed_cap : 1.0f;
            params.walk_speed = 2.2f * k;
            params.run_speed = 4.5f * k;
        }
        auto r = exec.update(plan, world, px, py, facing, dt, params);
        if (r.needs_replan) {
            // A stale plan while smelling-but-blind means the straight
            // line at the smell did not produce a sighting: CAST wider.
            if (smells && !sees) {
                ++cast_n;
                char b[80];
                std::snprintf(b, sizeof b, "CAST    swing %d, try again", cast_n);
                log.add(clock, b);
            }
            replan("plan went stale");
        }
        if (exec.get_state().current_action_name == "MEANDER")
            ++frames_meandering;

        if (r.movement.is_moving) {
            const float step = r.movement.forward_velocity * dt;
            // The limp, measured: fastest commanded step before any
            // wound vs after the cap dropped.
            {
                const auto* cap = engine.get_capability_store().get(pred_entity);
                if (cap && cap->speed_cap < 1.0f) {
                    if (step > max_step_wounded) max_step_wounded = step;
                } else if (wounds == 0) {
                    if (step > max_step_whole) max_step_whole = step;
                }
            }
            const float mx = std::sin(r.movement.body_facing) * step;
            const float my = std::cos(r.movement.body_facing) * step;
            // Collide-and-slide against the walls: a KINEMATIC creature
            // owns its own refusal to enter geometry.
            const float nx = px + mx, ny = py + my;
            if (grid.is_walkable(nx, ny)) { px = nx; py = ny; }
            else if (grid.is_walkable(nx, py)) { px = nx; }
            else if (grid.is_walkable(px, ny)) { py = ny; }
            if (std::abs(mx) + std::abs(my) > 1e-5f)
                facing = r.movement.body_facing;
        } else if (r.movement.wants_head_look) {
            facing = std::atan2(r.movement.look_x - px,
                                r.movement.look_y - py);
        }
        // PROPRIOCEPTION. An in-flight action is exempt from the
        // precondition gate, so an executor shoving into a wall would
        // otherwise push forever and nothing above would ever fire —
        // measured: 200 simulated seconds pinned against the screen
        // wall, 5 replans, zero sightings. A creature can FEEL that it
        // is walking and going nowhere; that feeling forces a recast.
        if (r.movement.is_moving) {
            if (stuck_frames == 0) { stuck_ref_x = px; stuck_ref_y = py; }
            if (++stuck_frames >= 45) {
                if (std::hypot(px - stuck_ref_x, py - stuck_ref_y) < 0.3f) {
                    log.add(clock, "STUCK   pushing, not moving");
                    if (smells && !sees) {
                        ++cast_n;
                        char b[80];
                        std::snprintf(b, sizeof b, "CAST    swing %d", cast_n);
                        log.add(clock, b);
                    }
                    replan("wedged against something");
                }
                stuck_frames = 0;
            }
        } else {
            stuck_frames = 0;
        }

        goal_achieved = r.goal_achieved;
        if (food.remaining_g < last_mass) { ++bites_seen; last_mass = food.remaining_g; }

        {
            auto view = ps.lock_particles_for_write();
            view[pred_idx].x = px; view[pred_idx].y = py;
            const float frac = std::max(0.0f, food.remaining_g) / initial_mass;
            const float sz = kCarcassFull * std::cbrt(std::max(frac, 0.001f));
            view[carc_idx].width = view[carc_idx].height =
                view[carc_idx].thickness = sz;
            view[carc_idx].size = sz;
            view[carc_idx].z = sz * 0.5f;
            if (frac <= 0.001f) view[carc_idx].a = 0.0f;
        }

        // [probe] the route through the thorn latitude, to place the
        // band where the hunt actually walks.
        static float last_probe_y = -99.0f;
        if ((py > 9.5f && py < 12.5f) && std::abs(py - last_probe_y) > 0.8f) {
            last_probe_y = py;
            std::cout << "  [probe] crossing y=" << py << " at x=" << px
                      << std::endl;
        }
        clock += dt;
        ++frames;
        return !goal_achieved && frames < 14400;   // 4 minutes, generous
    };

    // ---- panel ----
    std::unique_ptr<TextWindow> panel;
    if (auto* ui = engine.get_ui_system()) {
        panel = std::make_unique<TextWindow>("predator", "predator_search_log");
        panel->set_position(660, 330);
        panel->set_size(410, 350);
        panel->set_max_lines(26);
        panel->set_newest_at_top(false);
        panel->set_background_alpha(210);
        ui->add_widget(panel.get());
    }

    enum class Phase { WAITING, HUNTING, DONE };
    Phase phase = g_visual ? Phase::WAITING : Phase::HUNTING;

    auto refresh_panel = [&]() {
        if (!panel) return;
        panel->clear();
        char line[110];
        panel->add_line("PREDATOR / the search");
        panel->add_line("");
        if (phase == Phase::WAITING) {
            panel->add_line("somewhere NE, behind walls,");
            panel->add_line("there is a tonne of carrion.");
            panel->add_line("the predator knows nothing.");
            panel->add_line("");
            panel->add_line("> SPACE  release it");
            panel->add_line("> ESC    quit");
            return;
        }
        std::snprintf(line, sizeof line, "action  %s",
                      exec.get_state().current_action_name.empty()
                          ? "-" : exec.get_state().current_action_name.c_str());
        panel->add_line(line);
        std::snprintf(line, sizeof line, "facts   smell:%d sight:%d at:%d hungry:%d",
                      world["smells_food"], world["sees_food"],
                      world["at_food"], world["hungry"]);
        panel->add_line(line);
        std::snprintf(line, sizeof line,
                      "smelled %d   lost %d   sighted %d   replans %d",
                      log.smells, log.losses, log.sightings, log.replans);
        panel->add_line(line);
        std::snprintf(line, sizeof line, "carcass %.0f g   bites %d",
                      std::max(0.0f, food.remaining_g), bites_seen);
        panel->add_line(line);
        {
            const auto* cap = engine.get_capability_store().get(pred_entity);
            std::snprintf(line, sizeof line,
                          "leg %s hp   pace x%.1f   wounds %d",
                          kg.getProperty(pred_leg, "health").c_str(),
                          cap ? cap->speed_cap : 1.0f, wounds);
            panel->add_line(line);
        }
        panel->add_line("");
        for (const auto& l : log.lines) panel->add_line(l);
        if (phase == Phase::DONE) {
            panel->add_line("");
            panel->add_line(world["hungry"] ? "> gave up. SPACE: again"
                                            : "> FED. SPACE: run it again");
            panel->add_line("> ESC    quit");
        }
    };

    if (g_visual) {
        ps.queue_light(-10.0f, -25.0f, 45.0f, 26000000.0f, 700.0f,
                       1.0f, 0.96f, 0.9f);
        ps.queue_light(20.0f, 20.0f, 40.0f, 16000000.0f, 700.0f,
                       0.85f, 0.9f, 1.0f);
        auto& cam = engine.get_camera_system();
        cam.set_pixels_per_unit(17.0f);
        cam.set_position(0.0f - 14.0f, 1.0f - 16.0f, 15.0f);
        cam.look_at(0.0f, 1.0f, 0.8f);
        for (int i = 0; i < 6; ++i) engine.update(1.0 / 60.0);
        bring_to_front(engine);

        engine.render();
        engine.present();
        int fw = 0, fh = 0;
        std::vector<uint32_t> fpx(
            static_cast<size_t>(engine.get_render_buffer().width()) *
            engine.get_render_buffer().height(), 0u);
        if (engine.read_latest_framebuffer(fpx.data(), fw, fh)) {
            size_t bright = 0;
            for (uint32_t v : fpx) {
                const int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
                if ((r > g ? (r > b ? r : b) : (g > b ? g : b)) > 24) ++bright;
            }
            const double pct = 100.0 * double(bright) / double(fpx.size());
            std::cout << "  [measure] frame brightness: " << pct
                      << "% above black" << std::endl;
            check(pct > 20.0, "the scene is lit (" + std::to_string(pct) + "%)");
        }
    }

    reset_scenario();

    if (g_visual) {
        while (!g_quit && pump(engine)) {
            switch (phase) {
            case Phase::WAITING:
                engine.update(dt);   // sim_step ticks the engine while
                                     // hunting; idle phases tick it here
                if (space_pressed(engine)) {
                    phase = Phase::HUNTING;
                    log.add(clock, "RELEASED");
                }
                break;
            case Phase::HUNTING:
                if (!sim_step()) phase = Phase::DONE;
                break;
            case Phase::DONE:
                engine.update(dt);
                if (space_pressed(engine)) {
                    reset_scenario();
                    phase = Phase::HUNTING;
                }
                break;
            }
            // Fixed camera: the walls are the story, keep the whole
            // arena in frame and let the predator do the moving.
            auto& cam = engine.get_camera_system();
            cam.set_position(0.0f - 14.0f, 1.0f - 16.0f, 15.0f);
            cam.look_at(0.0f, 1.0f, 0.8f);
            refresh_panel();
            engine.render();
            engine.present();
        }
    } else {
        while (sim_step()) {}
        refresh_panel();
    }

    // Headless widget rasterisation: direct UISystem::render() until
    // Engine::render() reaches the widget pass without a display (#46).
    if (!g_visual && engine.get_ui_system()) {
        engine.get_ui_system()->render();
    } else {
        engine.render();
    }
    const OverlayPixels panel_px = overlay_pixels(engine);

    std::cout << "  [measure] " << frames << " frames ("
              << frames / 60.0f << " s simulated)" << std::endl;
    std::cout << "  [measure] meandered " << frames_meandering
              << " frames; first smell at " << first_smell_frame
              << ", first sight at " << first_sight_frame << std::endl;
    std::cout << "  [measure] smell acquired " << log.smells
              << ", lost " << log.losses << "; sighted " << log.sightings
              << "; replans " << log.replans << std::endl;
    std::cout << "  [measure] carcass " << initial_mass << " g -> "
              << food.remaining_g << " g in " << bites_seen << " bites"
              << std::endl;
    {
        const auto* cap = engine.get_capability_store().get(pred_entity);
        std::cout << "  [measure] wounds " << wounds << ", leg "
                  << kg.getProperty(pred_leg, "health") << " hp, pace x"
                  << (cap ? cap->speed_cap : -1.0f)
                  << ", kg capability.speed_cap='"
                  << kg.getProperty(pred_entity, "capability.speed_cap")
                  << "'" << std::endl;
    }
    std::cout << "  [measure] panel overlay: " << panel_px.lit << " lit, "
              << panel_px.text << " text" << std::endl;

    if (!g_visual) {
        check(goal_achieved, "the predator ends FED (" +
              std::to_string(frames) + " frames)");
        check(frames_meandering > 120,
              "it MEANDERED first: no scent reaches the start (" +
              std::to_string(frames_meandering) + " frames ambling)");
        check(first_smell_frame > 0 && log.smells >= 1,
              "the nose found the scent (first at frame " +
              std::to_string(first_smell_frame) + ")");
        check(first_sight_frame > first_smell_frame,
              "it SMELLED before it SAW: sight came at frame " +
              std::to_string(first_sight_frame) + ", behind walls");
        check(log.replans >= 2,
              "the plan changed as information arrived (" +
              std::to_string(log.replans) + " replans)");
        check(food.remaining_g < initial_mass * 0.05f,
              "and the carcass is gone");

        // ---- the wound chain, end to end (#37 capability store) ----
        {
            const auto* cap = engine.get_capability_store().get(pred_entity);
            const std::string leg_hp = kg.getProperty(pred_leg, "health");
            const std::string kg_cap =
                kg.getProperty(pred_entity, "capability.speed_cap");
            std::cout << "  [measure] limp: fastest step whole "
                      << max_step_whole << " m, wounded " << max_step_wounded
                      << " m" << std::endl;
            check(wounds >= 1,
                  "the thorns billed the crossing through ON_CONTACT (" +
                  std::to_string(wounds) + " wounds; one deep sting is "
                  "the deterministic minimum on this route)");
            check(!leg_hp.empty() && std::stof(leg_hp) < 60.0f,
                  "DamageSystem drove the leg below the rule threshold (" +
                  leg_hp + " hp)");
            check(cap && cap->speed_cap == 0.5f,
                  "the store recomputed off the bus: pace x" +
                  std::to_string(cap ? cap->speed_cap : -1.0f));
            check(kg_cap.rfind("0.5", 0) == 0,
                  "and the KG itself says so: capability.speed_cap='" +
                  kg_cap + "'");
            // The limp, honestly framed. Before the sting this route
            // only ever WALKS (meander, 2.2 m/s); the sprint happens
            // after, so wounded-vs-whole steps compare different gaits
            // and the first version of this assert failed on a 2%
            // artifact. The real claim: a wounded PURSUIT never reaches
            // even 60% of the healthy sprint it would otherwise use
            // (4.5 m/s -> 0.075 m/frame uncapped).
            const float healthy_sprint_step = 4.5f * (1.0f / 60.0f);
            check(max_step_wounded > 0.0f &&
                      max_step_wounded < healthy_sprint_step * 0.6f,
                  "the limp is REAL: it pursued, and slowly (" +
                  std::to_string(max_step_wounded) + " m/frame vs healthy "
                  "sprint " + std::to_string(healthy_sprint_step) + ")");
        }

        // THE CONTROL: same walls, same eyes, a carcass with NO odor.
        // Every fact upgrade above came through the nose first, so
        // without it the predator must still be meandering at the end
        // of the same budget — lost forever, honestly.
        std::srand(20260808u);
        reset_scenario();
        {
            auto view = ps.lock_particles_for_write();
            view[carc_idx].odor_radius = 0.0f;
        }
        int budget = 3600;
        while (budget-- > 0 && sim_step() ) {}
        std::cout << "  [measure] control (odorless carcass): smells="
                  << log.smells << " sightings=" << log.sightings
                  << " fed=" << (world["hungry"] ? 0 : 1)
                  << " after " << frames << " frames" << std::endl;
        check(log.smells == 0, "no nose, no scent events, ever");
        // Whether the myopic eyes eventually luck into the 9 m sight
        // pocket is chance, not structure: measured and printed, not
        // asserted.
    }
    check(panel_px.text > 300,
          "the search narrative is ON SCREEN as text (" +
          std::to_string(panel_px.text) + " glyph pixels)");

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
