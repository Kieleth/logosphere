// =============================================================================
// Logogenesis AT — chat text becomes world, end to end, no network.
// =============================================================================
// Drives the REAL app headless: injected chat turns go through the
// brain (offline gardener or a scripted responder), the KG-ops
// grammar (parse -> validate -> apply), the seed materializer, and
// the deferred worldgen generators. Locks:
//   - the offline path plants a real Tree from a chat turn
//   - a scripted spec (REDWOOD + grass + rock) flows into generator
//     entities with HAS_PART structure
//   - the schema validator rejects out-of-range specs (no seed, no
//     tree)
//   - render particles appear once activation drains (the deferred
//     worldgen path this project fixed: lowercase type names were
//     rejected by KG validation and returned INVALID_ENTITY)
// =============================================================================

#include "logogenesis_app.h"
#include "core/game_time.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define AT_ASSERT_TRUE(cond, msg)                                       \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::cout << "FAIL: " << msg << std::endl;                  \
            tests_failed++;                                             \
            return;                                                     \
        }                                                               \
    } while (0)

#define AT_TEST(fn)                                                     \
    do {                                                                \
        std::cout << "  " << #fn << "... " << std::endl;                \
        int before = tests_failed;                                      \
        fn();                                                           \
        if (tests_failed == before) {                                   \
            tests_passed++;                                             \
            std::cout << "PASS" << std::endl;                           \
        }                                                               \
    } while (0)

namespace {

struct Harness {
    logogenesis::LogogenesisApplication app;
    Engine engine;

    Harness() : engine(&app) {
        unsetenv("ANTHROPIC_API_KEY");   // offline gardener by default
        EngineConfig config;
        config.create_display = false;
        config.window_width = 1600;
        config.window_height = 1200;
        config.window_title = "logogenesis-at";
        config.show_debug_overlay = false;
        config.show_kg_inspector = false;
        config.enable_chat_window = false;   // headless: test hooks instead
        if (engine.initialize(config) < 0)
            throw std::runtime_error("Engine::initialize() failed headless");
    }
    ~Harness() { engine.shutdown(); }
    void tick(double dt) { engine.update(dt); }
};

void test_offline_gardener_plants_a_tree() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    size_t particles_before = h.engine.get_particle_system().count();

    h.app.submit_text_for_test("a beautiful tree, please");
    for (int i = 0; i < 900 && h.app.creations() < 3; ++i) h.tick(dt);

    // The void is truly empty: the first act creates light AND
    // ground, then the tree (the offline gardener follows the same
    // genesis contract the prompt teaches the LLM).
    AT_ASSERT_TRUE(h.app.creations() == 3,
        "first request = sun + ground + tree (got " +
        std::to_string(h.app.creations()) + ")");
    auto floors = kg.findByType("Floor");
    AT_ASSERT_TRUE(floors.size() == 1, "ground exists");
    AT_ASSERT_TRUE(!kg.getEntityKGParticles(floors[0]).empty(),
        "ground materialized as a slab particle");
    // The liturgy is sun-first: no lamp for daylight.
    AT_ASSERT_TRUE(kg.findByType("Sky").size() == 1,
        "the sun (Sky) is the first light");
    AT_ASSERT_TRUE(h.engine.get_celestial_system().body_count() >= 1,
        "celestial sun exists");
    AT_ASSERT_TRUE(kg.findByType("LightSource").empty(),
        "no lamp created for a daylight scene");
    AT_ASSERT_TRUE(kg.findByType("SunSeed").empty(),
        "the sun seed is consumed");
    auto trees = kg.findByType("Tree");
    AT_ASSERT_TRUE(trees.size() == 1,
        "a real Tree entity exists (got " +
        std::to_string(trees.size()) + ")");
    AT_ASSERT_TRUE(kg.findByType("TreeSeed").empty(),
        "the seed is consumed by materialization");
    AT_ASSERT_TRUE(!kg.getEntityKGParticles(trees[0]).empty(),
        "the tree grew structure (KG particles on the entity)");

    // The deferred path must reach the screen: activation drains and
    // render particles appear.
    bool grew = false;
    for (int i = 0; i < 600 && !grew; ++i) {
        h.tick(dt);
        grew = h.engine.get_particle_system().count() > particles_before;
    }
    AT_ASSERT_TRUE(grew,
        "activation materializes render particles for the tree");
}

void test_scripted_redwood_grass_rock_specs_flow() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"A redwood, its meadow, and a stone.",
                     "ops":[
              {"op":"create_entity","type":"TreeSeed","properties":{
                 "x":"4","y":"6","species":"REDWOOD","tree_height":"28",
                 "trunk_r":"0.5","trunk_g":"0.2","trunk_b":"0.15"}},
              {"op":"create_entity","type":"GrassSeed","properties":{
                 "x":"4","y":"1","patch_width":"5","patch_depth":"3",
                 "blade_count":"40","blade_r":"0.45","blade_g":"0.25",
                 "blade_b":"0.12"}},
              {"op":"create_entity","type":"RockSeed","properties":{
                 "x":"-6","y":"-2","rock_size":"1.4"}}]})");
        });

    h.app.submit_text_for_test(
        "a big Red Wood, red-toned grass below it, and a rock");
    for (int i = 0; i < 900 && h.app.creations() < 3; ++i) h.tick(dt);

    AT_ASSERT_TRUE(h.app.creations() == 3,
        "three creations (got " + std::to_string(h.app.creations()) + ")");
    AT_ASSERT_TRUE(kg.findByType("Tree").size() == 1, "tree grew");
    auto patches = kg.findByType("GrassPatch");
    AT_ASSERT_TRUE(patches.size() == 1, "grass patch grew");
    auto blades = kg.getRelated(patches[0], "HAS_PART");
    AT_ASSERT_TRUE(!blades.empty(), "patch has blade children");
    // The requested tint must reach the STALK particles (grass
    // renders from stem color; foliage-only tinting left blades
    // default brown — the black-sprinkle RCA).
    {
        auto kgids = kg.getEntityKGParticles(blades[0]);
        AT_ASSERT_TRUE(!kgids.empty(), "blade carries particle data");
        Particle bp = kg.getKGParticleData(kgids[0]);
        AT_ASSERT_TRUE(bp.r > 0.35f && bp.g < 0.35f,
            "red-toned request reaches the stalk (got r=" +
                std::to_string(bp.r) + " g=" + std::to_string(bp.g) + ")");
    }
    // Height diversity: blade jitter must reach the grown particle
    // (segment_length once discarded it — every blade identical).
    {
        float min_t = 1e9f, max_t = 0.0f;
        int sampled = 0;
        for (auto b : blades) {
            // A blade's length = the longest axis across ALL its
            // particles (particle[0] is a small base node).
            float len = 0.0f;
            for (auto kgid2 : kg.getEntityKGParticles(b)) {
                Particle bp2 = kg.getKGParticleData(kgid2);
                len = std::max({len, bp2.width, bp2.height, bp2.thickness});
            }
            if (len <= 0.0f) continue;
            min_t = std::min(min_t, len);
            max_t = std::max(max_t, len);
            if (++sampled >= 20) break;
        }
        AT_ASSERT_TRUE(sampled >= 10, "sampled blades for height spread");
        AT_ASSERT_TRUE(max_t > min_t * 1.15f,
            "blade heights vary organically (got min=" +
                std::to_string(min_t) + " max=" + std::to_string(max_t) + ")");
    }

    // Centering regression (the SW-shift bug: every patch shipped
    // offset by half its size): blade centroid must sit on the
    // requested center (4, 1), not at (center - half_patch).
    {
        float cx = 0, cy = 0; int n = 0;
        for (auto b : blades) {
            auto xs = kg.getProperty(b, "x"), ys = kg.getProperty(b, "y");
            if (xs.empty() || ys.empty()) continue;
            cx += std::stof(xs); cy += std::stof(ys); ++n;
        }
        AT_ASSERT_TRUE(n > 10, "blades carry positions");
        cx /= n; cy /= n;
        AT_ASSERT_TRUE(std::fabs(cx - 4.0f) < 1.5f &&
                       std::fabs(cy - 1.0f) < 1.5f,
            "blade centroid sits on the requested center (got " +
                std::to_string(cx) + ", " + std::to_string(cy) + ")");
    }
    AT_ASSERT_TRUE(kg.findByType("Rock").size() == 1, "rock formed");
    AT_ASSERT_TRUE(kg.findByType("TreeSeed").empty() &&
                   kg.findByType("GrassSeed").empty() &&
                   kg.findByType("RockSeed").empty(),
        "all seeds consumed");
    AT_ASSERT_TRUE(h.app.last_thoughts().find("redwood") != std::string::npos,
        "thoughts surfaced to the chat layer");
}

void test_validator_rejects_out_of_range_spec() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"A tree taller than the sky.",
                     "ops":[
              {"op":"create_entity","type":"TreeSeed","properties":{
                 "x":"0","y":"0","tree_height":"999"}}]})");
        });

    h.app.submit_text_for_test("a tree a thousand meters tall");
    for (int i = 0; i < 20; ++i) h.tick(dt);

    AT_ASSERT_TRUE(h.app.creations() == 0,
        "out-of-range spec creates nothing (got " +
        std::to_string(h.app.creations()) + ")");
    AT_ASSERT_TRUE(kg.findByType("Tree").empty() &&
                   kg.findByType("TreeSeed").empty(),
        "no tree, no dangling seed");
}

void test_sun_time_and_butterflies() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // The void begins MID-MORNING (before any sun exists): a sunset
    // wish from hour 0 once swept a full day arc.
    {
        double start_h = GameTime::get_day_fraction(
                             GameTime::get_current_time()) * 24.0;
        AT_ASSERT_TRUE(start_h > 9.0 && start_h < 11.5,
            "world starts mid-morning (got " +
                std::to_string(start_h) + ")");
    }

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"Sun and wings.",
                     "ops":[
              {"op":"create_entity","type":"SunSeed","properties":{
                 "time_of_day":"14"}},
              {"op":"create_entity","type":"ButterflySeed","properties":{
                 "x":"2","y":"2","count":"3"}}]})");
        });
    h.app.submit_text_for_test("a sun, and butterflies");
    for (int i = 0; i < 900 && h.app.creations() < 4; ++i) h.tick(dt);

    AT_ASSERT_TRUE(h.engine.get_celestial_system().body_count() >= 5,
        "sun + Earth moon + three stars exist (got " +
        std::to_string(h.engine.get_celestial_system().body_count()) + ")");
    {
        bool moon_found = false;
        auto& cs = h.engine.get_celestial_system();
        for (size_t i = 0; i < cs.body_count(); ++i) {
            const auto* b = cs.get_body(static_cast<int>(i));
            if (b && b->config.name == "moon") {
                moon_found = true;
                AT_ASSERT_TRUE(b->config.color_curve[0].value.r > 0.9f,
                    "the default moon is Earth silver");
            }
        }
        AT_ASSERT_TRUE(moon_found, "Earth's moon rides in with the sun");
    }
    for (int i = 0; i < 5; ++i) h.tick(dt);   // celestial positions compute next frame

    // BIRTH IS INSTANT: a SunSeed carrying an hour starts the world
    // AT that hour — no 40-second journey on creation (journeys are
    // for later "pass time" wishes).
    {
        double birth_h = GameTime::get_day_fraction(
                             GameTime::get_current_time()) * 24.0;
        AT_ASSERT_TRUE(std::fabs(birth_h - 14.0) < 0.1,
            "the world is born at the requested hour (got " +
                std::to_string(birth_h) + ")");
    }

    // Stars: dispersed, above the horizon, and NON-rotating.
    {
        auto& cs = h.engine.get_celestial_system();
        float sx[3], sy[3], sz[3];
        int stars = 0;
        for (size_t i = 0; i < cs.body_count() && stars < 3; ++i) {
            const auto* b = cs.get_body(static_cast<int>(i));
            if (b && b->config.name == "star") {
                sx[stars] = b->x; sy[stars] = b->y; sz[stars] = b->z;
                AT_ASSERT_TRUE(b->z > 20.0f, "stars hang high");
                ++stars;
            }
        }
        AT_ASSERT_TRUE(stars == 3, "exactly three stars");
        for (int i = 0; i < 100; ++i) h.tick(dt);
        stars = 0;
        for (size_t i = 0; i < cs.body_count() && stars < 3; ++i) {
            const auto* b = cs.get_body(static_cast<int>(i));
            if (b && b->config.name == "star") {
                AT_ASSERT_TRUE(std::fabs(b->x - sx[stars]) < 0.5f &&
                               std::fabs(b->z - sz[stars]) < 0.5f,
                    "stars do not rotate");
                ++stars;
            }
        }
    }
    {
        const auto* sun = h.engine.get_celestial_system().get_body(0);
        float d = sun ? std::sqrt(sun->x * sun->x + sun->y * sun->y +
                                  sun->z * sun->z)
                      : 0.0f;
        AT_ASSERT_TRUE(sun != nullptr && d > 150.0f,
            "the sun is a FAR particle, never in frame (d=" +
                std::to_string(d) + ")");
    }
    auto skies = kg.findByType("Sky");
    AT_ASSERT_TRUE(skies.size() == 1, "the Sky entity exists");
    AT_ASSERT_TRUE(kg.findByType("Butterfly").size() == 3,
        "three butterflies (got " +
        std::to_string(kg.findByType("Butterfly").size()) + ")");

    double start_h = GameTime::get_day_fraction(
                         GameTime::get_current_time()) * 24.0;

    // Time control: journey to golden hour must be forward-only,
    // UNDER one day total, land EXACTLY, and stay.
    kg.setProperty(skies[0], "time_of_day", "18.2");
    double traveled = 0.0, prev_h = start_h;
    bool arrived = false;
    for (int i = 0; i < 1800 && !arrived; ++i) {
        h.tick(dt);
        double now_h = GameTime::get_day_fraction(
                           GameTime::get_current_time()) * 24.0;
        double step = now_h - prev_h;
        if (step < 0) step += 24.0;
        traveled += step;
        prev_h = now_h;
        arrived = std::fabs(now_h - 18.2) < 0.05;
    }
    AT_ASSERT_TRUE(arrived, "time reaches golden hour exactly");
    AT_ASSERT_TRUE(traveled < 24.0,
        "the journey never laps a full day (traveled " +
            std::to_string(traveled) + "h)");
    for (int i = 0; i < 60; ++i) h.tick(dt);
    double after = GameTime::get_day_fraction(
                       GameTime::get_current_time()) * 24.0;
    AT_ASSERT_TRUE(std::fabs(after - 18.2) < 0.1,
        "and STAYS there at normal pace (got " +
            std::to_string(after) + ")");
}

void test_destroy_unmakes_matter() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // Grow a tree, wait for its matter to activate.
    size_t base = h.engine.get_particle_system().count();
    kg.createEntity("TreeSeed");
    h.app.materialize_now();
    auto trees = kg.findByType("Tree");
    AT_ASSERT_TRUE(trees.size() == 1, "tree grew");
    bool grew = false;
    for (int i = 0; i < 600 && !grew; ++i) {
        h.tick(dt);
        grew = h.engine.get_particle_system().count() > base + 50;
    }
    AT_ASSERT_TRUE(grew, "tree matter activated");
    size_t with_tree = h.engine.get_particle_system().count();

    // destroy_entity through the ops path must unmake the MATTER.
    h.app.set_responder_for_test(
        [&](const std::string&, const std::string&,
            std::function<void(std::string)> done) {
            done(std::string("{\"thoughts\":\"Unmade.\",\"ops\":[") +
                 "{\"op\":\"destroy_entity\",\"target\":" +
                 std::to_string(trees[0]) + "}]}");
        });
    h.app.submit_text_for_test("remove the tree");
    for (int i = 0; i < 20; ++i) h.tick(dt);

    AT_ASSERT_TRUE(kg.findByType("Tree").empty(), "tree entity unmade");
    size_t after = h.engine.get_particle_system().count();
    AT_ASSERT_TRUE(after < with_tree - 50,
        "the tree's matter is gone (had " + std::to_string(with_tree) +
        ", now " + std::to_string(after) + ")");
}

void test_exotic_moons_and_cap() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"A sun, then a blood moon, then greed.",
                     "ops":[
              {"op":"create_entity","type":"SunSeed","properties":{
                 "time_of_day":"22"}},
              {"op":"create_entity","type":"MoonSeed","properties":{
                 "moon_r":"0.8","moon_g":"0.15","moon_b":"0.1",
                 "moon_brightness":"1.5"}},
              {"op":"create_entity","type":"MoonSeed","properties":{}},
              {"op":"create_entity","type":"MoonSeed","properties":{}}]})");
        });
    h.app.submit_text_for_test("a blood moon night, and more moons");
    for (int i = 0; i < 20; ++i) h.tick(dt);

    auto& cs = h.engine.get_celestial_system();
    int moons = 0;
    bool blood = false;
    for (size_t i = 0; i < cs.body_count(); ++i) {
        const auto* b = cs.get_body(static_cast<int>(i));
        if (b && b->config.name == "moon") {
            ++moons;
            if (b->config.color_curve[0].value.r > 0.7f &&
                b->config.color_curve[0].value.g < 0.2f)
                blood = true;
        }
    }
    AT_ASSERT_TRUE(moons == 3,
        "three moons at most, Earth default included (got " +
        std::to_string(moons) + ")");
    AT_ASSERT_TRUE(blood, "the blood moon's color flowed");
}

void test_celestial_bodies_stay_off_camera() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // Full sky: sun + Earth moon + stars + two exotic moons.
    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"The whole sky.",
                     "ops":[
              {"op":"create_entity","type":"SunSeed","properties":{
                 "time_of_day":"12"}},
              {"op":"create_entity","type":"MoonSeed","properties":{
                 "moon_r":"0.8","moon_g":"0.15","moon_b":"0.1"}},
              {"op":"create_entity","type":"MoonSeed","properties":{}}]})");
        });
    h.app.submit_text_for_test("the whole sky");
    for (int i = 0; i < 20; ++i) h.tick(dt);

    // Sweep the entire day: no celestial DISC may ever project into
    // the window ("100% off camera" — their light arrives, their
    // bodies never do).
    auto& cs = h.engine.get_celestial_system();
    auto& ct = h.engine.get_coord_transformer();
    const float W = 1600.0f, H = 1200.0f;
    for (int step = 0; step < 96; ++step) {
        double hour = step * 0.25;
        GameTime::set_time(
            std::floor(GameTime::get_current_time() /
                       GameTime::SECONDS_PER_DAY + 1.0) *
                GameTime::SECONDS_PER_DAY +
            hour * 3600.0);
        for (int i = 0; i < 3; ++i) h.tick(dt);
        for (size_t bi = 0; bi < cs.body_count(); ++bi) {
            const auto* b = cs.get_body(static_cast<int>(bi));
            if (!b) continue;
            int sx = 0, sy = 0;
            ct.world_to_screen_3d(b->x, b->y, b->z, sx, sy);
            float m = b->config.visual_radius * 20.0f;  // disc margin
            bool onscreen = sx > -m && sx < W + m && sy > -m && sy < H + m;
            AT_ASSERT_TRUE(!onscreen,
                b->config.name + " projects ON screen at hour " +
                    std::to_string(hour) + " (screen " +
                    std::to_string(sx) + "," + std::to_string(sy) + ")");
        }
    }
}

// Highest KG-particle z across the tree and its HAS_PART children —
// ground truth for "how tall is it right now", independent of the
// stamped property.
float tree_top_z(kg::KGModule& kg, kg::EntityID tree) {
    float top = 0.0f;
    std::vector<kg::EntityID> stack{tree};
    while (!stack.empty()) {
        auto e = stack.back();
        stack.pop_back();
        for (auto kgid : kg.getEntityKGParticles(e))
            top = std::max(top, kg.getKGParticleData(kgid).z);
        for (auto child : kg.getRelated(e, "HAS_PART"))
            stack.push_back(child);
    }
    return top;
}

void test_growth_time_lapse() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // A growing oak: grow_seconds turns creation into a time-lapse.
    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"An acorn's whole life in a breath.",
                     "ops":[
              {"op":"create_entity","type":"SunSeed","properties":{
                 "time_of_day":"12"}},
              {"op":"create_entity","type":"GroundSeed","properties":{
                 "x":"0","y":"0","ground_width":"60","ground_depth":"60"}},
              {"op":"create_entity","type":"TreeSeed","properties":{
                 "x":"0","y":"0","species":"OAK","tree_height":"14",
                 "grow_seconds":"2"}}]})");
        });
    h.app.submit_text_for_test("plant an oak and let it grow");
    for (int i = 0; i < 900 && kg.findByType("Tree").empty(); ++i) h.tick(dt);

    // Birth is a SAPLING, not the full tree.
    auto trees = kg.findByType("Tree");
    AT_ASSERT_TRUE(trees.size() == 1, "the sapling exists");
    float sapling_h = std::stof(kg.getProperty(trees[0], "tree_height"));
    AT_ASSERT_TRUE(sapling_h < 7.0f,
        "birth height is a sapling's, under half the target (got " +
        std::to_string(sapling_h) + ")");
    float sapling_top = tree_top_z(kg, trees[0]);
    AT_ASSERT_TRUE(sapling_top < 8.0f,
        "sapling matter stays low (top z " + std::to_string(sapling_top) + ")");

    // Mid-lapse: taller than the sapling, shorter than the target.
    for (int i = 0; i < 60; ++i) h.tick(dt);   // ~1.0 s in
    trees = kg.findByType("Tree");
    AT_ASSERT_TRUE(trees.size() == 1, "still exactly one tree mid-growth");
    float mid_h = std::stof(kg.getProperty(trees[0], "tree_height"));
    AT_ASSERT_TRUE(mid_h > sapling_h && mid_h < 14.0f,
        "mid-growth height between sapling and target (got " +
        std::to_string(mid_h) + ")");

    // Lapse complete: ONE tree, full target height, real matter up top.
    for (int i = 0; i < 90; ++i) h.tick(dt);   // past 2 s + slack
    trees = kg.findByType("Tree");
    AT_ASSERT_TRUE(trees.size() == 1,
        "exactly one tree after growth (no leaked stages, got " +
        std::to_string(trees.size()) + ")");
    float final_h = std::stof(kg.getProperty(trees[0], "tree_height"));
    AT_ASSERT_TRUE(std::fabs(final_h - 14.0f) < 0.01f,
        "grown to the full target height (got " +
        std::to_string(final_h) + ")");
    float final_top = tree_top_z(kg, trees[0]);
    AT_ASSERT_TRUE(final_top > 9.0f,
        "grown matter reaches the crown (top z " +
        std::to_string(final_top) + ")");
    AT_ASSERT_TRUE(h.app.growth_jobs_active() == 0, "growth job retired");

    // Unmaking a tree MID-growth stops the lapse — no resurrection.
    auto first_tree = trees[0];
    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"Another.","ops":[
              {"op":"create_entity","type":"TreeSeed","properties":{
                 "x":"12","y":"0","species":"PINE","tree_height":"12",
                 "grow_seconds":"2"}}]})");
        });
    h.app.submit_text_for_test("another, growing");
    for (int i = 0; i < 300 && kg.findByType("Tree").size() < 2; ++i)
        h.tick(dt);
    trees = kg.findByType("Tree");
    AT_ASSERT_TRUE(trees.size() == 2, "second sapling exists");
    for (int i = 0; i < 36; ++i) h.tick(dt);   // ~0.6 s: mid-growth
    // Growth stages regenerate under fresh entity ids, so the target
    // is resolved at FIRE time — exactly what a live LLM sees in its
    // World block.
    h.app.set_responder_for_test(
        [&kg, first_tree](const std::string&, const std::string&,
                          std::function<void(std::string)> done) {
            auto ts = kg.findByType("Tree");
            kg::EntityID target =
                ts[0] == first_tree ? ts[1] : ts[0];
            done(std::string("{\"thoughts\":\"Unmade.\",\"ops\":[") +
                 "{\"op\":\"destroy_entity\",\"target\":" +
                 std::to_string(target) + "}]}");
        });
    h.app.submit_text_for_test("unmake it");
    for (int i = 0; i < 120; ++i) h.tick(dt);  // past its whole schedule
    AT_ASSERT_TRUE(kg.findByType("Tree").size() == 1,
        "the unmade tree stays unmade — growth does not resurrect it");
    AT_ASSERT_TRUE(h.app.growth_jobs_active() == 0,
        "orphaned growth job dropped");
}

void test_forest_in_one_breath() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // Eight trees in ONE response: the whole batch must materialize
    // (the live failure was max_tokens=800 truncating the ops array —
    // "a grove" arrived as one or two trees and a broken JSON tail).
    std::string ops = R"({"thoughts":"A forest, whole.","ops":[
      {"op":"create_entity","type":"SunSeed","properties":{
         "time_of_day":"12"}},
      {"op":"create_entity","type":"GroundSeed","properties":{
         "x":"0","y":"0","ground_width":"80","ground_depth":"80"}})";
    const char* species[] = {"OAK", "PINE", "WILLOW", "PINE",
                             "OAK", "REDWOOD", "PINE", "OAK"};
    for (int i = 0; i < 8; ++i) {
        float x = -21.0f + 6.0f * static_cast<float>(i);
        ops += std::string(",\n{\"op\":\"create_entity\",")
             + "\"type\":\"TreeSeed\",\"properties\":{"
             + "\"x\":\"" + std::to_string(x) + "\",\"y\":\""
             + std::to_string((i % 2) ? 8.0f : -6.0f) + "\","
             + "\"species\":\"" + species[i] + "\",\"tree_height\":\""
             + std::to_string(8.0f + (i % 4) * 4.0f) + "\"}}";
    }
    ops += "]}";
    h.app.set_responder_for_test(
        [ops](const std::string&, const std::string&,
              std::function<void(std::string)> done) { done(ops); });
    h.app.submit_text_for_test("a forest");
    for (int i = 0; i < 900 && kg.findByType("Tree").size() < 8; ++i)
        h.tick(dt);

    auto trees = kg.findByType("Tree");
    AT_ASSERT_TRUE(trees.size() == 8,
        "the whole forest materializes in one turn (got " +
        std::to_string(trees.size()) + " of 8)");
    for (auto t : trees)
        AT_ASSERT_TRUE(!kg.getEntityKGParticles(t).empty(),
            "every tree in the batch grew real structure");
}

void test_layered_earth_and_falling_boulder() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    // LAYERED ground (the default) + a boulder born 25 m up.
    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"Earth, then a meteor.",
                     "ops":[
              {"op":"create_entity","type":"SunSeed","properties":{
                 "time_of_day":"12"}},
              {"op":"create_entity","type":"GroundSeed","properties":{
                 "x":"0","y":"0","ground_width":"26","ground_depth":"26"}},
              {"op":"create_entity","type":"RockSeed","properties":{
                 "x":"0","y":"0","rock_size":"1.5","drop_height":"25"}}]})");
        });
    h.app.submit_text_for_test("real earth, and a meteor");

    // The earth pours and settles under the app's own frames.
    auto floor_has_matter = [&]() {
        auto floors = kg.findByType("Floor");
        return !floors.empty() &&
               kg.getEntityKGParticles(floors[0]).size() > 50;
    };
    for (int i = 0; i < 1200 && !floor_has_matter(); ++i) h.tick(dt);
    AT_ASSERT_TRUE(floor_has_matter(),
        "layered earth forms with real particle depth");
    auto floors = kg.findByType("Floor");
    AT_ASSERT_TRUE(kg.getProperty(floors[0], "terrain") == "LAYERED",
        "the Floor knows its terrain");

    // Multi-LEVEL: the ground's matter spans distinct z bands (a slab
    // is 0.1 m thin; real earth is ~1 m of stacked layers). Tiles are
    // LIVE particles — read them through the render index.
    float lo = 1e9f, hi = -1e9f;
    {
        auto view = h.engine.get_particle_system().lock_particles_for_read();
        for (auto kgid : kg.getEntityKGParticles(floors[0])) {
            auto ri = kg.getRenderIndex(kgid);
            if (ri == kg::INVALID_RENDER_INDEX) continue;
            lo = std::min(lo, view[ri].z);
            hi = std::max(hi, view[ri].z);
        }
    }
    AT_ASSERT_TRUE(hi - lo > 0.5f,
        "the ground is multi-level (z span " + std::to_string(hi - lo) +
        " m)");
    AT_ASSERT_TRUE(lo > -0.35f, "nothing under the turtle");

    // The earth completed mid-frame; surface seeds materialize on the
    // NEXT materializer pass.
    for (int i = 0; i < 10 && kg.findByType("Rock").empty(); ++i) h.tick(dt);

    // The boulder: born high (drop_height stamped), then it FALLS.
    auto rocks = kg.findByType("Rock");
    AT_ASSERT_TRUE(rocks.size() == 1, "the meteor exists");
    AT_ASSERT_TRUE(std::stof(kg.getProperty(rocks[0], "drop_height")) >
                       20.0f,
        "born to fall (drop_height stamped)");
    // Wait for its matter to activate, then for gravity to finish.
    auto rock_top = [&]() {
        float top = -1e9f;
        for (auto kgid : kg.getEntityKGParticles(rocks[0])) {
            auto ri = kg.getRenderIndex(kgid);
            if (ri == kg::INVALID_RENDER_INDEX) continue;
            auto view = h.engine.get_particle_system()
                            .lock_particles_for_read();
            top = std::max(top, view[ri].z);
        }
        return top;
    };
    float born_top = -1e9f;
    for (int i = 0; i < 600 && born_top < 0.0f; ++i) {
        h.tick(dt);
        born_top = rock_top();
    }
    AT_ASSERT_TRUE(born_top > 15.0f,
        "the meteor is born high (top z " + std::to_string(born_top) + ")");
    for (int i = 0; i < 600; ++i) {
        h.tick(dt);
        if (i % 60 == 0) {
            auto kgids = kg.getEntityKGParticles(rocks[0]);
            auto ri = kg.getRenderIndex(kgids[0]);
            auto view = h.engine.get_particle_system()
                            .lock_particles_for_read();
            std::cout << "  [meteor] f" << i << " top z=" << rock_top()
                      << " vz=" << view[ri].vz
                      << " at_rest=" << (int)view[ri].is_at_rest
                      << " mode=" << (int)view[ri].solver_mode
                      << " owner=" << (int)view[ri].owner << std::endl;
        }
    }
    float landed_top = rock_top();
    AT_ASSERT_TRUE(landed_top < 5.0f && landed_top > 0.2f,
        "the meteor fell to the earth and rests in it (top z " +
        std::to_string(landed_top) + ")");
}

void test_humanoid_wanders() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"A person to walk this garden.",
                     "ops":[
              {"op":"create_entity","type":"SunSeed","properties":{
                 "time_of_day":"12"}},
              {"op":"create_entity","type":"GroundSeed","properties":{
                 "x":"0","y":"0","ground_width":"60","ground_depth":"60"}},
              {"op":"create_entity","type":"HumanoidSeed","properties":{
                 "x":"3","y":"2","cloth_r":"0.8","cloth_g":"0.2",
                 "cloth_b":"0.2"}}]})");
        });
    h.app.submit_text_for_test("someone to wander among the trees");
    for (int i = 0; i < 900 && kg.findByType("Humanoid").empty(); ++i)
        h.tick(dt);

    auto people = kg.findByType("Humanoid");
    AT_ASSERT_TRUE(people.size() == 1,
        "a person exists (got " + std::to_string(people.size()) +
        ", seeds left " +
        std::to_string(kg.findByType("HumanoidSeed").size()) + ")");
    AT_ASSERT_TRUE(kg.getRelated(people[0], "HAS_PART").size() >= 5,
        "the person has body-part structure");
    AT_ASSERT_TRUE(h.app.wanderers_active() == 1, "a wander driver runs");
    AT_ASSERT_TRUE(kg.findByType("HumanoidSeed").empty(),
        "the seed is consumed");

    // They MOVE: sample the hips each second; the summed path over
    // ~10 s of strolling (1.2 m/s minus lingering) must be real
    // travel, and they stay upright the whole way.
    auto hips_pos = [&](float& x, float& y, float& z) {
        int hips = h.app.wanderer_hips(0);
        auto particles =
            h.engine.get_particle_system().lock_particles_for_read();
        x = particles[hips].x;
        y = particles[hips].y;
        z = particles[hips].z;
    };
    float px, py, pz;
    hips_pos(px, py, pz);
    const float hips_home_z = pz;   // whatever surface they stand on
    float traveled = 0.0f;
    float min_z = pz, max_z = pz;
    for (int s = 0; s < 10; ++s) {
        for (int i = 0; i < 60; ++i) h.tick(dt);
        float nx, ny, nz;
        hips_pos(nx, ny, nz);
        traveled += std::sqrt((nx - px) * (nx - px) +
                              (ny - py) * (ny - py));
        if (nz < min_z)
            std::cout << "  [measure] hips sank to z=" << nz << " at ("
                      << nx << "," << ny << ") after " << (s + 1)
                      << " s" << std::endl;
        min_z = std::min(min_z, nz);
        max_z = std::max(max_z, nz);
        px = nx; py = ny; pz = nz;
    }
    AT_ASSERT_TRUE(traveled > 2.0f,
        "the person strolls (path " + std::to_string(traveled) + " m)");
    AT_ASSERT_TRUE(min_z > hips_home_z - 0.5f && max_z < hips_home_z + 0.6f,
        "upright the whole walk (hips z spanned " +
        std::to_string(min_z) + ".." + std::to_string(max_z) +
        " around home " + std::to_string(hips_home_z) + ")");

    // Unmaking them releases the rig and the driver — no crash, no
    // ghost walker.
    auto person = people[0];
    h.app.set_responder_for_test(
        [person](const std::string&, const std::string&,
                 std::function<void(std::string)> done) {
            done(std::string("{\"thoughts\":\"Gone.\",\"ops\":[") +
                 "{\"op\":\"destroy_entity\",\"target\":" +
                 std::to_string(person) + "}]}");
        });
    h.app.submit_text_for_test("unmake them");
    for (int i = 0; i < 120; ++i) h.tick(dt);
    AT_ASSERT_TRUE(kg.findByType("Humanoid").empty(), "the person is gone");
    AT_ASSERT_TRUE(h.app.wanderers_active() == 0, "the driver is dropped");
}

}  // namespace

// The eye can move: an OrbitSeed swings the camera azimuth through a
// full revolution and lands exactly where it began (issue #9's engine
// mechanism driven by game policy). Mid-orbit the azimuth must be off
// the classic bearing; at the end it must equal 2*pi exactly enough
// that the view is home again.
void test_orbit_seed_swings_the_camera() {
    Harness h;
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"Let us walk around it.",
                     "ops":[
              {"op":"create_entity","type":"OrbitSeed","properties":{
                 "revolutions":"1","duration_seconds":"3"}}]})");
        });

    float before = h.engine.get_camera_system().get_view_azimuth();
    AT_ASSERT_TRUE(before == 0.0f, "camera starts on the classic bearing");

    h.app.submit_text_for_test("orbit around the scene");

    // 1.5 s in (half of the 3 s orbit): mid-swing, azimuth well off 0.
    for (int i = 0; i < 90; ++i) h.tick(dt);
    float mid = h.engine.get_camera_system().get_view_azimuth();
    AT_ASSERT_TRUE(mid > 0.5f && mid < 6.0f,
        "mid-orbit the view is swinging (azimuth " +
        std::to_string(mid) + ")");

    // Past the end: exactly one revolution, landed home.
    for (int i = 0; i < 150; ++i) h.tick(dt);
    float after = h.engine.get_camera_system().get_view_azimuth();
    AT_ASSERT_TRUE(std::fabs(after - 6.2831853f) < 1e-3f,
        "the orbit lands exactly one revolution later (azimuth " +
        std::to_string(after) + ")");

    // The seed is consumed: requests are wishes, not objects.
    AT_ASSERT_TRUE(h.engine.get_kg().findByType("OrbitSeed").empty(),
        "the OrbitSeed is destroyed after materializing");
}

// The menagerie: serpent + deadwood + totem seeds materialize through
// their generators; entities exist in the KG with particles bound.
void test_serpent_deadwood_totem() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"A python, a fallen log, a totem to watch them.",
                     "ops":[
              {"op":"create_entity","type":"SerpentSeed","properties":{
                 "x":"2","y":"3","serpent_kind":"PYTHON",
                 "serpent_length":"4"}},
              {"op":"create_entity","type":"FallenTreeSeed","properties":{
                 "x":"-3","y":"1","deadwood_kind":"LOG",
                 "deadwood_length":"2.5"}},
              {"op":"create_entity","type":"TotemSeed","properties":{
                 "x":"0","y":"-4","totem_size":"1.2",
                 "totem_r":"0.5","totem_g":"0.3","totem_b":"0.2"}}]})");
        });

    h.app.submit_text_for_test("a python, a fallen log, and a totem");
    for (int i = 0; i < 900 && h.app.creations() < 3; ++i) h.tick(dt);

    AT_ASSERT_TRUE(h.app.creations() == 3,
        "three menagerie creations (got " +
        std::to_string(h.app.creations()) + ")");
    AT_ASSERT_TRUE(kg.findByType("SerpentSeed").empty() &&
                   kg.findByType("FallenTreeSeed").empty() &&
                   kg.findByType("TotemSeed").empty(),
        "all seeds consumed");
    AT_ASSERT_TRUE(!kg.findByType("Snake").empty() ||
                   !kg.findByType("Serpent").empty(),
        "a serpent entity exists in the KG");
}

// Playful declines are thoughts-only: zero ops must create nothing
// and the reply must reach the chat verbatim.
void test_thoughts_only_reply_creates_nothing() {
    Harness h;
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"My powers are considerable, but the dragon egg has not hatched. I can offer you a python in the grass, or a totem to guard the spot.","ops":[]})");
        });

    h.app.submit_text_for_test("a dragon please");
    for (int i = 0; i < 240; ++i) h.tick(dt);

    AT_ASSERT_TRUE(h.app.creations() == 0,
        "a decline creates nothing (creations " +
        std::to_string(h.app.creations()) + ")");
    AT_ASSERT_TRUE(h.app.last_thoughts().find("dragon egg") !=
                   std::string::npos,
        "the flourish reaches the chat (got: " + h.app.last_thoughts() +
        ")");
}

// The grandest wish: one PlanetSeed births the whole tableau -
// bonded sphere, rose at the pole, prince at the apex - floating
// clear of the turtle, crust constrained to the core.
void test_prince_planet() {
    Harness h;
    auto& kg = h.engine.get_kg();
    const double dt = 1.0 / 60.0;
    h.tick(dt);

    h.app.set_responder_for_test(
        [](const std::string&, const std::string&,
           std::function<void(std::string)> done) {
            done(R"({"thoughts":"The lonely asteroid, whole: earth, flower, dreamer.",
                     "ops":[
              {"op":"create_entity","type":"PlanetSeed","properties":{
                 "x":"0","y":"0","planet_radius":"3",
                 "with_rose":"true","with_prince":"true"}}]})");
        });

    h.app.submit_text_for_test("the Little Prince's asteroid, please");
    for (int i = 0; i < 900 && h.app.creations() < 3; ++i) h.tick(dt);

    AT_ASSERT_TRUE(h.app.creations() == 3,
        "planet + rose + prince (creations " +
        std::to_string(h.app.creations()) + ")");
    auto planets = kg.findByType("Planet");
    AT_ASSERT_TRUE(planets.size() == 1, "one Planet entity in the KG");
    AT_ASSERT_TRUE(kg.findByType("PlanetSeed").empty(),
        "the seed is consumed");

    // The crust is a bonded body: constraints exist on the planet.
    int constraints = 0;
    for (auto c : kg.findByType("Constraint")) {
        if (kg.getProperty(c, "entity_id") ==
            std::to_string(planets[0])) constraints++;
    }
    AT_ASSERT_TRUE(constraints >= 24,
        "crust stones are bonded to the core (constraints " +
        std::to_string(constraints) + ")");

    // Let it exist for a while: the planet must float, not fall.
    for (int i = 0; i < 300; ++i) h.tick(dt);
    float min_z = 1e9f;
    {
        auto view = h.engine.get_particle_system().lock_particles_for_read();
        for (size_t i = 0; i < view.size(); ++i)
            min_z = std::min(min_z, view[i].z - view[i].thickness * 0.5f);
    }
    AT_ASSERT_TRUE(min_z > -0.01f,
        "nothing driven below the turtle (lowest " +
        std::to_string(min_z) + ")");
}

int main() {
    std::cout << "Logogenesis AT — creation" << std::endl;
    AT_TEST(test_offline_gardener_plants_a_tree);
    AT_TEST(test_scripted_redwood_grass_rock_specs_flow);
    AT_TEST(test_validator_rejects_out_of_range_spec);
    AT_TEST(test_sun_time_and_butterflies);
    AT_TEST(test_destroy_unmakes_matter);
    AT_TEST(test_exotic_moons_and_cap);
    AT_TEST(test_celestial_bodies_stay_off_camera);
    AT_TEST(test_growth_time_lapse);
    AT_TEST(test_forest_in_one_breath);
    AT_TEST(test_layered_earth_and_falling_boulder);
    AT_TEST(test_humanoid_wanders);
    AT_TEST(test_orbit_seed_swings_the_camera);
    AT_TEST(test_serpent_deadwood_totem);
    AT_TEST(test_thoughts_only_reply_creates_nothing);
    AT_TEST(test_prince_planet);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
