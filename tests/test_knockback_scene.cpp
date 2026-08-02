// Knockback against the REAL engine: a predator walks into prey, and a
// contact rule pushes them apart.
//
// WHY THIS EXISTS SEPARATELY FROM test_contact_bounce. That test feeds
// contact PODs in by hand. Every link of the chain is checked there and
// none of it has ever met the actual solver, so it cannot answer the only
// question that matters in a real scene: does a contact the PHYSICS
// produced reach a rule and come back out as a push?
//
// The situation that prompted it, measured in test_predator_hunt:
//
//   distance: started 15.23 m, closest 0.0026 m, ended 0.71 m
//
// The predator ends 0.71 m from a prey whose radii sum to 3.6 m. It walks
// inside the animal it is hunting and stays there. That scene loads no
// contact rule, so nothing pushes them apart, and until this file
// nothing proved a rule would.
//
// A SECOND THING that scene exposed: its creatures are made with
// engine.add_particle(), which does NOT bind a KG entity. Particles
// outside the KG are outside the ontology, so the contact producer skips
// them and no rule could ever reach them however it was written. Here
// they are built with add_particle_to_entity, which is what the header
// has always told production code to use.
//
// Shape of the test, which is the TDD order:
//   1. run the approach with NO rule       -> measure the overlap
//   2. run the identical approach WITH one -> measure the separation
// Step 1 is not scaffolding. It is the control, and it is the state the
// engine is in today.
//
// Usage:
//   ./build/test_knockback_scene                     numbers, asserted
//   LOGOSPHERE_VISUAL=1 ./build/test_knockback_scene  watch it, ESC quits

#undef NDEBUG

#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "ui/ui_system.h"
#include "ui/text_window.h"
#include "particle.h"
#include "logosphere/interaction/contact_response.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/events/event_bus.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "  FAIL: " << msg << std::endl; }           \
    } while (0)

namespace {

using logosphere::interaction::ContactContext;
using logosphere::interaction::ContactEffectContext;
using logosphere::interaction::ContactEffectRegistry;

bool g_visual = false;
bool g_quit = false;

constexpr float kPredatorSize = 1.6f;
constexpr float kPreySize     = 2.0f;
// Sphere half-extents, so this is the separation at which they just touch.
constexpr float kTouchDistance = (kPredatorSize + kPreySize) * 0.5f;

// Prey and Predator are GAME vocabulary. The engine ontology has no
// concrete creature type on purpose (`Creature` is abstract), because
// what creatures exist is a game's business. Declared the way a game
// declares them.
kg::OntologyRegistry game_types() {
    kg::OntologyRegistry reg;
    reg.addEntityType("Predator", "LivingEntity", /*is_abstract=*/false);
    reg.addEntityType("Prey", "LivingEntity", false);
    reg.addAncestors("Predator", {"LivingEntity", "WorldEntity", "Entity"});
    reg.addAncestors("Prey", {"LivingEntity", "WorldEntity", "Entity"});
    return reg;
}

// One creature: a KG entity, a body part, and a particle bound to that
// part. All three matter. The producer walks particle -> part -> entity,
// so a creature missing any link is invisible to every contact rule.
struct Beast {
    kg::EntityID entity = 0;
    kg::EntityID part = 0;
    kg::KGParticleID kgid = 0;
    int idx = -1;
    float vx = 0.0f, vy = 0.0f;
};

struct Scene {
    Engine engine;
    std::unique_ptr<TextWindow> panel;
    Beast predator, prey;
    int collision_events = 0;
    int knockbacks = 0;
    // WHERE in the approach the contact was actually reported. A rule
    // cannot act sooner than the engine tells it, so if the push looks
    // late this is the number that says whether the rule or the
    // DETECTION is late.
    float sep_at_first_event = -1.0f;
    float sep_at_first_knockback = -1.0f;

    explicit Scene(bool with_display) : engine(nullptr) {
        EngineConfig cfg;
        cfg.create_display = with_display;
        cfg.window_width = 1100;
        cfg.window_height = 720;
        cfg.window_title = "knockback: a contact rule pushing back";
        cfg.show_debug_overlay = false;
        cfg.enable_chat_window = false;
        if (engine.initialize(cfg) < 0)
            throw std::runtime_error("Engine::initialize() failed");
        engine.get_kg().extendOntology(game_types());
    }
    ~Scene() { engine.shutdown(); }

    Beast spawn(const char* type, const char* part_type, float x, float y,
                float size, float r, float g, float b) {
        Beast c;
        auto& kg = engine.get_kg();
        c.entity = kg.createEntity(type);
        c.part = kg.createEntity(part_type);
        kg.createRelation(c.entity, "HAS_PART", c.part);

        Particle p{};
        p.shape = ParticleShape::SPHERE;
        p.x = x; p.y = y; p.z = 1.0f;
        p.width = p.height = p.thickness = size;
        p.size = size;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(Materials::Type::FLESH);
        // KINEMATIC, like every creature in this engine: something
        // outside physics owns the position. That is precisely why
        // knockback cannot be a solver impulse.
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        c.idx = engine.get_particle_system().add_particle_to_entity(
            p, &kg, c.part);
        c.kgid = kg.getKGParticleByRenderIndex(
            static_cast<kg::RenderIndex>(c.idx));
        return c;
    }

    void ground() {
        if (!g_visual) return;
        for (int gx = -3; gx <= 3; ++gx) {
            for (int gy = -3; gy <= 3; ++gy) {
                Particle p{};
                p.shape = ParticleShape::BOX;
                p.x = gx * 8.0f; p.y = gy * 8.0f; p.z = -0.4f;
                p.width = p.height = 8.0f; p.thickness = 0.8f;
                p.size = 8.0f;
                const float t = ((gx + gy) & 1) ? 0.30f : 0.26f;
                p.r = t; p.g = t + 0.04f; p.b = t - 0.04f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::DIRT);
                p.solver_mode = ParticleSolverMode::STATIC;
                p.is_at_rest = true;
                engine.add_particle(p);
            }
        }
    }

    // The rule, authored in the KG exactly as a game would author it.
    // Conditions ask about the OTHER party and effects act on SELF, so
    // this is the PREY's rule: "when a Predator touches me, throw me
    // clear".
    void arm_rule(const char* condition, const char* effect) {
        auto& kg = engine.get_kg();
        auto r = kg.createEntity("TransformationRule");
        kg.setProperty(r, "trigger", "on_contact");
        kg.setProperty(r, "condition", condition);
        kg.setProperty(r, "effect", effect);
        size_t n = engine.get_interaction_system().load_rules_from_kg(kg);
        std::cout << "    rule armed (" << condition << " -> " << effect
                  << "): " << n << " loaded" << std::endl;
    }

    // Positions are read under the shared lock, the same way anything
    // outside the particle system must.
    void positions(float& ax, float& ay, float& bx, float& by) {
        auto view = engine.get_particle_system().lock_particles_for_read();
        ax = view[predator.idx].x; ay = view[predator.idx].y;
        bx = view[prey.idx].x;     by = view[prey.idx].y;
    }
    float px() { float a,b,c,d; positions(a,b,c,d); return a; }
    float py() { float a,b,c,d; positions(a,b,c,d); return b; }
    float qx() { float a,b,c,d; positions(a,b,c,d); return c; }
    float qy() { float a,b,c,d; positions(a,b,c,d); return d; }

    float separation() {
        float ax, ay, bx, by;
        positions(ax, ay, bx, by);
        const float dx = bx - ax, dy = by - ay;
        return std::sqrt(dx * dx + dy * dy);
    }

    // The external writer. KINEMATIC means this function owns position,
    // so this is where an impulse has to land: it drains the inbox,
    // folds the push into velocity, and integrates. A real game does
    // this inside locomotion and can damp, cap or refuse it.
    void move(Beast& b, float dt) {
        auto& sys = engine.get_interaction_system();
        float ix = 0, iy = 0, iz = 0;
        if (sys.take_impulse(b.kgid, ix, iy, iz)) {
            b.vx += ix;
            b.vy += iy;
            ++knockbacks;
            if (sep_at_first_knockback < 0.0f) sep_at_first_knockback = separation();
        }
        auto view = engine.get_particle_system().lock_particles_for_write();
        auto& p = view[b.idx];
        p.x += b.vx * dt;
        p.y += b.vy * dt;
    }
};

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

struct Run {
    float closest = 1e9f;
    float ended = 0.0f;
    float prey_moved = 0.0f;
    int   events = 0;
    int   knockbacks = 0;
    int   frames_overlapping = 0;
    float sep_at_event = -1.0f;
    float sep_at_knockback = -1.0f;
};

// Walk the predator straight at the prey and report what happened.
// Identical in both cases; only whether a rule is armed differs.
Run approach(Scene& s, int frames, bool watch) {
    Run out;
    const float dt = 1.0f / 60.0f;
    const float prey_start_x = s.qx();

    s.engine.get_event_bus().collisions().subscribe(
        [&](const logosphere::ontology::CollisionEvent& e) {
            ++s.collision_events;
            if (s.sep_at_first_event < 0.0f) {
                s.sep_at_first_event = s.separation();
                float ax, ay, bx, by;
                s.positions(ax, ay, bx, by);
                std::cout << "  [probe] predator at x=" << ax << ", prey at x="
                          << bx << "  (prey is on the +x side)" << std::endl;
                std::cout << "  [probe] event source=" << e.source_entity_id.value_or("-")
                          << " target=" << e.target_entity_id.value_or("-")
                          << "  predator entity=" << s.predator.entity
                          << " prey entity=" << s.prey.entity << std::endl;
                std::cout << "  [probe] reported normal_x=" << e.normal_x.value_or(0.0f)
                          << "  (header claims: unit normal from A toward B)"
                          << std::endl;
            }
        });

    s.predator.vx = 3.0f;   // straight at it

    for (int f = 0; f < frames && !g_quit; ++f) {
        s.engine.update(dt);
        s.move(s.predator, dt);
        s.move(s.prey, dt);

        const float sep = s.separation();
        out.closest = std::min(out.closest, sep);
        if (sep < kTouchDistance) ++out.frames_overlapping;

        if (watch) {
            if (!pump(s.engine)) break;
            auto& cam = s.engine.get_camera_system();
            const float mx = (s.px() + s.qx()) * 0.5f;
            const float my = (s.py() + s.qy()) * 0.5f;
            cam.set_position(mx - 12.0f, my - 14.0f, 13.0f);
            cam.look_at(mx, my, 1.0f);
            if (s.panel) {
                s.panel->clear();
                char line[128];
                s.panel->add_line("KNOCKBACK: a contact rule pushing back");
                s.panel->add_line("");
                std::snprintf(line, sizeof line, "frame        %d", f);
                s.panel->add_line(line);
                std::snprintf(line, sizeof line, "separation   %.2f m", sep);
                s.panel->add_line(line);
                std::snprintf(line, sizeof line, "touch at     %.2f m",
                              kTouchDistance);
                s.panel->add_line(line);
                s.panel->add_line("");
                std::snprintf(line, sizeof line, "collisions   %d",
                              s.collision_events);
                s.panel->add_line(line);
                std::snprintf(line, sizeof line, "knockbacks   %d", s.knockbacks);
                s.panel->add_line(line);
                s.panel->add_line("");
                s.panel->add_line(sep < kTouchDistance ? "STATE  overlapping"
                                                       : "STATE  clear");
            }
            s.engine.render();
            s.engine.present();
        }
    }

    out.ended = s.separation();
    out.prey_moved = std::abs(s.qx() - prey_start_x);
    out.events = s.collision_events;
    out.knockbacks = s.knockbacks;
    out.sep_at_event = s.sep_at_first_event;
    out.sep_at_knockback = s.sep_at_first_knockback;
    return out;
}

void make_panel(Scene& s) {
    if (!g_visual) return;
    auto* ui = s.engine.get_ui_system();
    if (!ui) return;
    s.panel = std::make_unique<TextWindow>("knockback", "knockback_log");
    s.panel->set_position(700, 40);
    s.panel->set_size(370, 260);
    s.panel->set_max_lines(20);
    s.panel->set_newest_at_top(false);
    s.panel->set_background_alpha(210);
    ui->add_widget(s.panel.get());
}

void light_and_camera(Scene& s) {
    if (!g_visual) return;
    s.engine.get_particle_system().queue_light(-10.0f, -25.0f, 45.0f,
                                               3.2f, 200.0f, 1.0f, 0.96f, 0.9f);
    s.engine.get_particle_system().queue_light(20.0f, 20.0f, 40.0f,
                                               1.6f, 160.0f, 0.8f, 0.85f, 1.0f);
    auto& cam = s.engine.get_camera_system();
    cam.set_pixels_per_unit(26.0f);
    bring_to_front(s.engine);
}

Scene* build(bool watch) {
    auto* s = new Scene(watch);
    s->ground();
    s->predator = s->spawn("Predator", "Head", -12.0f, 0.0f, kPredatorSize,
                           0.95f, 0.75f, 0.20f);
    s->prey = s->spawn("Prey", "Torso", 0.0f, 0.0f, kPreySize,
                       0.85f, 0.25f, 0.25f);
    make_panel(*s);
    light_and_camera(*s);
    return s;
}

}  // namespace

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::cout << "Knockback in a real scene (issue #36)" << std::endl;
    std::cout << "  bodies touch at " << kTouchDistance << " m separation"
              << std::endl;

    // ------------------------------------------------------------------
    // 1. THE CONTROL, and the state the engine is in today: no rule.
    // ------------------------------------------------------------------
    std::cout << "\n-- no contact rule armed --" << std::endl;
    Run without;
    {
        std::unique_ptr<Scene> s(build(false));
        without = approach(*s, 420, false);
        std::cout << "  [measure] closest " << without.closest
                  << " m, ended " << without.ended
                  << " m, overlapping on " << without.frames_overlapping
                  << " frames" << std::endl;
        std::cout << "  [measure] collision events " << without.events
                  << ", knockbacks " << without.knockbacks << std::endl;
    }

    // The predator walks into the prey and stays there. This is what
    // test_predator_hunt measured as "ended 0.71 m" and it is not a bug
    // in physics: both bodies are KINEMATIC, so the solver moves neither.
    CHECK(without.closest < kTouchDistance,
          "without a rule the predator ends up INSIDE the prey (closest " +
          std::to_string(without.closest) + " m, touch at " +
          std::to_string(kTouchDistance) + ")");
    CHECK(without.knockbacks == 0,
          "and nothing pushes back (" + std::to_string(without.knockbacks) +
          " knockbacks)");
    CHECK(without.prey_moved < 0.01f,
          "the prey never moves (" + std::to_string(without.prey_moved) + " m)");

    // ------------------------------------------------------------------
    // 2. THE SAME APPROACH, with the rule armed.
    // ------------------------------------------------------------------
    std::cout << "\n-- with_type:Predator -> knockback:6.0 --" << std::endl;
    Run with;
    {
        std::unique_ptr<Scene> s(build(false));
        s->arm_rule("with_type:Predator", "knockback:6.0");
        with = approach(*s, 420, false);
        std::cout << "  [measure] closest " << with.closest
                  << " m, ended " << with.ended
                  << " m, overlapping on " << with.frames_overlapping
                  << " frames" << std::endl;
        std::cout << "  [measure] collision events " << with.events
                  << ", knockbacks " << with.knockbacks << std::endl;
        std::cout << "  [measure] contact REPORTED at separation "
                  << with.sep_at_event << " m (bodies touch at "
                  << kTouchDistance << " m)" << std::endl;
        std::cout << "  [measure] knockback APPLIED at separation "
                  << with.sep_at_knockback << " m" << std::endl;
        std::cout << "  [measure] prey driven " << with.prey_moved << " m"
                  << std::endl;
    }

    // The claim in the file name. A contact the SOLVER produced reached a
    // rule and came back as a push.
    CHECK(with.events > 0,
          "a real solver contact reached the bus (" +
          std::to_string(with.events) + " events)");
    CHECK(with.knockbacks > 0,
          "and fired the rule (" + std::to_string(with.knockbacks) +
          " knockbacks)");
    CHECK(with.prey_moved > 1.0f,
          "the prey is driven clear (" + std::to_string(with.prey_moved) +
          " m, from a standing start)");
    CHECK(with.ended > without.ended,
          "they end further apart than they did with no rule (" +
          std::to_string(with.ended) + " m vs " +
          std::to_string(without.ended) + " m)");
    // The point of the whole exercise: less time spent inside each other.
    CHECK(with.frames_overlapping < without.frames_overlapping,
          "and spend fewer frames overlapping (" +
          std::to_string(with.frames_overlapping) + " vs " +
          std::to_string(without.frames_overlapping) + ")");

    // ------------------------------------------------------------------
    // 3. The negative: a rule naming a type nobody in the scene has.
    //    Identical geometry, identical closing speed. Without this,
    //    "they separated" could be any push from any source.
    // ------------------------------------------------------------------
    std::cout << "\n-- with_type:Boulder -> knockback:6.0 (nothing matches) --"
              << std::endl;
    Run mismatched;
    {
        std::unique_ptr<Scene> s(build(false));
        s->arm_rule("with_type:Boulder", "knockback:6.0");
        mismatched = approach(*s, 420, false);
        std::cout << "  [measure] closest " << mismatched.closest
                  << " m, knockbacks " << mismatched.knockbacks
                  << ", prey moved " << mismatched.prey_moved << " m"
                  << std::endl;
    }
    CHECK(mismatched.events > 0,
          "the contact is still reported, so the difference is the RULE");
    CHECK(mismatched.knockbacks == 0,
          "but a type nothing in the scene has fires nothing (" +
          std::to_string(mismatched.knockbacks) + ")");
    CHECK(mismatched.prey_moved < 0.01f, "and the prey stays put");

    // ------------------------------------------------------------------
    // 4. A game effect, on real contacts. The schema says the effect
    //    vocabulary is a floor; this is that claim against the solver
    //    rather than against a fixture.
    // ------------------------------------------------------------------
    std::cout << "\n-- a game-registered effect on real contacts --"
              << std::endl;
    int bleeds = 0;
    float bled_at_x = 0.0f;
    ContactEffectRegistry::instance().register_effect(
        "bleed", [&](ContactEffectContext& ctx, const std::string&) {
            ++bleeds;
            bled_at_x = ctx.contact.contact_x;
        });
    {
        std::unique_ptr<Scene> s(build(false));
        s->arm_rule("with_type:Predator", "bleed:0.4");
        approach(*s, 420, false);
    }
    std::cout << "  [measure] 'bleed' fired " << bleeds
              << " time(s), at contact x=" << bled_at_x << std::endl;
    CHECK(bleeds > 0,
          "an effect the engine never heard of runs on a solver contact (" +
          std::to_string(bleeds) + ")");
    CHECK(std::abs(bled_at_x) > 0.01f,
          "and is handed a real contact point, not a zero (" +
          std::to_string(bled_at_x) + ")");

    // ------------------------------------------------------------------
    // 5. WHICH WAY DOES THE CONTACT NORMAL POINT?
    //
    //    physics_system.h documents "Unit normal from A toward B", and it
    //    shipped pointing the other way. Both consumers written against
    //    the documented contract (this producer, and the humanoid
    //    obstacle push at humanoid_locomotion.cpp:5594) therefore drove
    //    things the wrong way. Fixed at the emission site 2026-08-02.
    //
    //    One geometry proves nothing, so run the mirrored case too: a
    //    constant would pass the first and fail the second.
    // ------------------------------------------------------------------
    std::cout << "\n-- which way does the normal point? --" << std::endl;
    {
        float from_left = 0.0f, from_right = 0.0f;
        for (int mirrored = 0; mirrored < 2; ++mirrored) {
            std::unique_ptr<Scene> s(new Scene(false));
            // Predator is ALWAYS spawned first, so it is always the
            // lower particle index and therefore always side A.
            const float start = mirrored ? 12.0f : -12.0f;
            s->predator = s->spawn("Predator", "Head", start, 0.0f,
                                   kPredatorSize, 0.95f, 0.75f, 0.20f);
            s->prey = s->spawn("Prey", "Torso", 0.0f, 0.0f, kPreySize,
                               0.85f, 0.25f, 0.25f);
            float seen = 0.0f;
            bool got = false;
            s->engine.get_event_bus().collisions().subscribe(
                [&](const logosphere::ontology::CollisionEvent& e) {
                    if (!got) { seen = e.normal_x.value_or(0.0f); got = true; }
                });
            s->predator.vx = mirrored ? -3.0f : 3.0f;
            for (int f = 0; f < 420 && !got; ++f) {
                s->engine.update(1.0f / 60.0f);
                s->move(s->predator, 1.0f / 60.0f);
            }
            (mirrored ? from_right : from_left) = seen;
        }
        std::cout << "  [measure] A(predator) left of B(prey):  A->B is +x, "
                  << "reported normal_x = " << from_left << std::endl;
        std::cout << "  [measure] A(predator) right of B(prey): A->B is -x, "
                  << "reported normal_x = " << from_right << std::endl;

        // THE REGRESSION LOCK. The reported normal must point from A
        // toward B, which is what CollisionEvent documents and what every
        // consumer assumes. It shipped inverted, and nothing noticed
        // because the one consumer that reads it (humanoid obstacle push)
        // fails in a way that looks like a locomotion bug rather than a
        // sign bug. If this goes red, humanoids are being shoved INTO
        // obstacles again.
        CHECK(from_left > 0.0f,
              "A left of B: the normal points +x, toward B (" +
              std::to_string(from_left) + ")");
        CHECK(from_right < 0.0f,
              "A right of B: the normal points -x, toward B (" +
              std::to_string(from_right) + ")");
        CHECK(from_left * from_right < 0.0f,
              "and it flips with the approach, so it is a real convention "
              "and not a constant that happens to pass one case");
    }

    // ------------------------------------------------------------------
    // Watch it.
    // ------------------------------------------------------------------
    if (g_visual) {
        std::cout << "\n-- visual: ESC to quit --" << std::endl;
        std::unique_ptr<Scene> s(build(true));
        s->arm_rule("with_type:Predator", "knockback:6.0");
        approach(*s, 900, true);
    }

    std::cout << "\n" << tests_passed << " passed, " << tests_failed
              << " failed" << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
