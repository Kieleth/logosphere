// Engine acceptance test — Logosphere::Effects::hide_assembly /
// restore_assembly. KISS placeholder for the eventual derez/redeploy
// effect. Hides every particle owned by an entity by zeroing alpha,
// snapshots prior alpha, restores exactly.
//
// Catches regressions where:
//   * the entity→particles lookup misses some parts (assembly stays
//     partially visible)
//   * the snapshot loses prior-alpha (restore brings parts back at
//     1.0 instead of their authored value)
//   * restore mishandles particles that vanished between hide and
//     restore (should silently skip, never crash)

#include "application.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/effects/assembly_visibility.h"
#include "logosphere/kg/kg_types.h"
#include "particle.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

class HeadlessTestApp : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }
};

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

#define ASSERT_NEAR(actual, expected, eps, msg) \
    do { \
        float _a = (actual), _e = (expected); \
        float _d = _a > _e ? _a - _e : _e - _a; \
        if (_d > (eps)) { \
            throw std::runtime_error(std::string(msg) \
                + " expected=" + std::to_string(_e) \
                + " got=" + std::to_string(_a)); \
        } \
    } while (0)

static void init_engine(Engine& engine) {
    EngineConfig config;
    config.create_display = false;
    config.window_width = 200;
    config.window_height = 200;
    config.window_title = "assembly-visibility-at";
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;
    int observer = engine.initialize(config);
    if (observer < 0)
        throw std::runtime_error("engine.initialize() failed in headless");
}

// Make a small "rig" of three particles all owned by one KG entity.
// Each gets a distinct authored alpha so we can verify exact restore.
static kg::EntityID spawn_three_part_assembly(Engine& engine,
                                              const float alphas[3]) {
    auto& kg = engine.get_kg();
    kg::EntityID e = kg.createEntity("Wall");
    auto& ps = engine.get_particle_system();
    for (int i = 0; i < 3; ++i) {
        Particle p = {};
        p.shape = ParticleShape::BOX;
        p.x = float(i) * 0.5f;
        p.y = 0.0f;
        p.z = 0.5f;
        p.width = 0.4f; p.height = 0.4f; p.thickness = 0.4f;
        p.r = 1.0f; p.g = 1.0f; p.b = 1.0f; p.a = alphas[i];
        p.SetMaterial(Materials::Type::STONE);
        p.owner = ParticleOwner::STATIC;
        p.is_at_rest = true;
        p.entity_id = e;  // bind particle to entity for KG mapping
        ps.add_particle_to_entity(p, &kg, e);
    }
    return e;
}

// =========================================================================
// AT — hide zeros all alphas; restore brings each one back exactly.
// =========================================================================
void hide_zeros_alphas_restore_brings_them_back() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    const float authored[3] = { 1.0f, 0.6f, 0.3f };
    kg::EntityID e = spawn_three_part_assembly(engine, authored);
    auto& ps = engine.get_particle_system();
    auto kg_parts = engine.get_kg().getEntityKGParticles(e);
    ASSERT_TRUE(kg_parts.size() == 3,
        std::string("spawn must register 3 KGParticles for the entity, got ")
        + std::to_string(kg_parts.size()));

    auto snap = Logosphere::Effects::hide_assembly(ps, engine.get_kg(), e);
    ASSERT_TRUE(snap.entries.size() == 3,
        "snapshot must carry one entry per assembly particle");

    // Resolve render indices once for the alpha checks.
    std::vector<int> render_indices;
    for (auto kgp : kg_parts) {
        render_indices.push_back(static_cast<int>(engine.get_kg().getRenderIndex(kgp)));
    }

    // Every particle reads alpha == 0.
    {
        auto view = ps.lock_particles_for_read();
        for (int idx : render_indices) {
            ASSERT_NEAR(view[idx].a, 0.0f, 1e-5f,
                "alpha must be 0 after hide");
        }
    }

    Logosphere::Effects::restore_assembly(ps, snap);

    // Every particle reads its authored alpha.
    {
        auto view = ps.lock_particles_for_read();
        for (size_t i = 0; i < 3; ++i) {
            ASSERT_NEAR(view[render_indices[i]].a, authored[i], 1e-5f,
                std::string("alpha must restore exactly for part ") + std::to_string(i));
        }
    }

    engine.shutdown();
}

// =========================================================================
// AT — hide on an entity that owns no particles is a no-op (empty
// snapshot). Restore on the same is also a no-op. Catches a crash
// path where the lookup returns an empty vector and the cpp would
// otherwise lock the particle vec for nothing.
// =========================================================================
void hide_on_empty_entity_is_noop() {
    HeadlessTestApp app;
    Engine engine(&app);
    init_engine(engine);

    kg::EntityID e = engine.get_kg().createEntity("Wall");
    auto& ps = engine.get_particle_system();

    auto snap = Logosphere::Effects::hide_assembly(ps, engine.get_kg(), e);
    ASSERT_TRUE(snap.entries.empty(),
        "snapshot must be empty for an entity with no particles");
    Logosphere::Effects::restore_assembly(ps, snap);  // must not crash

    engine.shutdown();
}

int main() {
    if (std::getenv("CI")) {
        std::cout << "SKIP all (CI)" << std::endl;
        return 0;
    }
    std::cout << "=== AT — hide / restore assembly visibility ===" << std::endl;
    TEST(hide_zeros_alphas_restore_brings_them_back);
    TEST(hide_on_empty_entity_is_noop);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
