// =============================================================================
// A BODY IS BORN WITH ITS MATERIAL (INV-38 / G-74)
// =============================================================================
// Owner ruling 2026-09-03: "material is needed ... we need to add material to
// all generators, that's the fix, and we need proper tests for this, even the
// tree generator, with our argus-assertion style."
//
// THE LAW. A material is the body's physical identity: density, Young's
// modulus, loss factor, tensile strength. The organic bond law derives its
// stiffness and damping from the two bodies' material_type, contact damping
// reads it too. A body left at the default is priced as FLESH whatever it is
// - a stone, a stem, a wing. So every generator names a material on every
// body it births, and a deliberate FLESH is SET, not inherited.
//
// THE INSTRUMENTS. Particle::material_set (raised only by SetMaterial) read
// over exactly the bodies each stage added; the door's own counter
// (ParticleSystem::births_without_material) as the second witness; on the
// bonded stages the first registered bond's stiffness recomputed from its
// fields and the NAMED moduli (A / (la/Ea + lb/Eb), the law's own formula);
// and Argus on one body per stage for 60 frames so a stage that launches
// says so (INV-11).
//
// ONE STAGE PER GENERATOR, far apart on a bare turtle:
//   strata floor (sets its layers' materials: the control), physics tree,
//   organic grass, physics rock, humanoid, snake, butterfly.
//
//   ./build/test_born_with_material
// =============================================================================
#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/core/argus.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"
#include "logosphere/worldgen/physics_rock_generator.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/snake_generator.h"
#include "logosphere/worldgen/butterfly_generator.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "logosphere/kg/ontology_registry.h"
#include "generated/earth_ontology_registry.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr float DT = 1.0f / 60.0f;
constexpr int   WATCH_FRAMES = 60;
constexpr float SPEED_BOUND = 10.0f;    // INV-11's bar in the jammed-sleep scene
constexpr float K_TOLERANCE = 1e-3f;    // relative, the law's own formula reproduced

struct Verdict { std::string text; bool ok; };

struct Stage {
    const char* name;
    float x;
    float min_modulus;          // 0: any named material; >0: "not flesh" is part of the law here
    std::function<void()> birth;
    size_t first = 0, last = 0;
    size_t bodies = 0, named = 0, unnamed = 0;
    size_t unnamed_at_door = 0;
    std::map<int, size_t> kinds;
    int watched = -1;
    // The stage's bond census, read AT BIRTH (before any frame): bonds whose
    // BOTH bodies the stage birthed, and the first of them with a contact
    // area, priced by the organic law from the named materials.
    size_t own_bonds = 0, own_with_area = 0;
    bool   law_checked = false, law_ok = false;
    std::string law_text;
};

const char* mat_name(Materials::Type t) { return Materials::GetName(t); }

}  // namespace

int main() {
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { std::printf("engine init failed\n"); return 1; }
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    auto& wg = engine.get_worldgen_system();
    // A bare engine rejects Grass and GrassPatch: extend with the earth
    // setting pack, as test_walk_through_grass does.
    engine.get_kg().extendOntology(earth::ontology::registry());

    std::printf("\n=== A BODY IS BORN WITH ITS MATERIAL (INV-38 / G-74) ===\n");

    std::vector<Stage> stages;
    stages.push_back({"the strata floor", 0.0f, 0.0f, [&] {
        auto& strata = wg.get_strata_floor_generator();
        strata.set_tile_size(4.0f);
        strata.set_tiles_per_chunk(5);
        strata.set_tiles_per_entity(1);
        strata.set_load_radius(20.0f);
        strata.set_unload_radius(30.0f);
        std::vector<StrataLayerSpec> layers;
        auto add_layer = [&](const char* name, Materials::Type mat, float th, bool bond) {
            StrataLayerSpec sl;
            sl.name = name; sl.material = mat; sl.thickness = th;
            sl.r = 0.4f; sl.g = 0.4f; sl.b = 0.4f;
            sl.bond_within_layer = bond; sl.bond_strength = bond ? 8000.0f : 0.0f;
            layers.push_back(sl);
        };
        add_layer("bedrock",  Materials::Type::STONE, 0.30f, true);
        add_layer("sediment", Materials::Type::STONE, 0.15f, false);
        add_layer("organic",  Materials::Type::DIRT,  0.10f, false);
        strata.set_layers(std::move(layers));
        strata.set_enabled(true);
        strata.preload_chunks_around(0.0f, 0.0f, 1);
    }});
    stages.push_back({"the physics tree", 60.0f, Materials::GetYoungsModulus(Materials::Type::LEAVES), [&] {
        PhysicsTreeGenerator gen;
        gen.initialize(&engine);
        TreeSpec spec;
        spec.random_seed = 12345;
        gen.generate_tree_with_roots(60.0f, 0.0f, 0.0f, spec);
    }});
    stages.push_back({"the organic grass", 90.0f, Materials::GetYoungsModulus(Materials::Type::LEAVES), [&] {
        auto& ogen  = wg.get_organic_generator();
        auto& scene = wg.get_scene_generator();
        kg::EntityID patch = ogen.generate_grass_patch(90.0f, 0.0f, 0.0f, GrassPatchSpec::short_grass());
        scene.activate_entity_now(patch);
    }});
    stages.push_back({"the physics rock", 120.0f, 0.0f, [&] {
        PhysicsRockGenerator rgen;
        rgen.initialize(&engine);
        PhysicsRockSpec rs;
        rgen.generate_rock(120.0f, 0.0f, 1.0f, rs);
    }});
    stages.push_back({"the humanoid", 150.0f, 0.0f, [&] {
        auto& hgen = wg.get_humanoid_generator();
        hgen.generate_humanoid_physics(150.0f, 0.0f, 1.0f, -1, HumanoidSpec::eva(), false);
    }});
    stages.push_back({"the snake", 180.0f, 0.0f, [&] {
        SnakeGenerator sgen;
        sgen.initialize(&engine, &engine.get_kg());
        sgen.generate_snake(180.0f, 0.0f, 0.5f, SnakeSpec::garden_snake());
    }});
    stages.push_back({"the butterfly", 210.0f, 0.0f, [&] {
        ButterflyGenerator bgen;
        bgen.initialize(&engine, &engine.get_kg());
        bgen.generate_butterfly(210.0f, 0.0f, 2.0f, ButterflySpec::monarch());
    }});

    logosphere::Argus argus;
    for (Stage& st : stages) {
        const size_t door0 = ps.births_without_material();
        st.first = [&] { auto v = ps.lock_particles_for_read(); return v.size(); }();
        st.birth();
        ps.flush_pending_particles();
        st.last = [&] { auto v = ps.lock_particles_for_read(); return v.size(); }();
        st.unnamed_at_door = ps.births_without_material() - door0;
        auto v = ps.lock_particles_for_read();
        for (size_t i = st.first; i < st.last && i < v.size(); ++i) {
            const Particle& p = v[i];
            if (p.is_light_source || p.GetMass() <= 0.0f) continue;
            ++st.bodies;
            if (p.material_set) ++st.named; else ++st.unnamed;
            st.kinds[(int)p.material_type]++;
            if (st.watched < 0) { st.watched = (int)i; }
        }
        if (st.watched >= 0) argus.watch(st.watched, st.name);
        std::printf("  [measure] %-18s bodies %4zu  named %4zu  defaulted %4zu  door %zu  kinds:",
                    st.name, st.bodies, st.named, st.unnamed, st.unnamed_at_door);
        for (const auto& kv : st.kinds)
            std::printf(" %s x%zu", mat_name((Materials::Type)kv.first), kv.second);
        std::printf("\n");

        // The bond census at birth. A stage's bond is one whose BOTH bodies the
        // stage birthed: the strata streams chunks and their bonds into later
        // stages' flushes, and removals reorder the registry, so neither an
        // index range nor a later scan is a stage.
        {
            const GluonConstraintBase* g = nullptr; const OrganicGluon* og = nullptr;
            for (size_t i = 0, n = physics.get_total_gluon_count(); i < n; ++i) {
                const GluonConstraintBase* c = physics.gluon_at(i);
                if (!c || c->particle_a < st.first || c->particle_a >= st.last ||
                    c->particle_b < st.first || c->particle_b >= st.last) continue;
                ++st.own_bonds;
                const auto* o = dynamic_cast<const OrganicGluon*>(c);
                if (o && o->contact_area > 0.0f) { ++st.own_with_area; if (!og) { g = c; og = o; } }
            }
            std::printf("  [measure] %-18s bonds between its own bodies %zu, with a contact area %zu%s\n",
                        st.name, st.own_bonds, st.own_with_area,
                        st.own_bonds > 0 && st.own_with_area == 0
                            ? "  (no area: the material law cannot price these)" : "");
            if (og) {
                auto v = ps.lock_particles_for_read();
                const Particle& pa = v[g->particle_a]; const Particle& pb = v[g->particle_b];
                const float Ea = Materials::GetYoungsModulus(pa.material_type);
                const float Eb = Materials::GetYoungsModulus(pb.material_type);
                float la = std::sqrt(g->offset_a.x*g->offset_a.x + g->offset_a.y*g->offset_a.y + g->offset_a.z*g->offset_a.z);
                float lb = std::sqrt(g->offset_b.x*g->offset_b.x + g->offset_b.y*g->offset_b.y + g->offset_b.z*g->offset_b.z);
                const float total = la + lb;
                if (total < 0.01f) { const float pad = (0.01f - total) * 0.5f; la += pad; lb += pad; }
                const float k_law = og->contact_area / (la / Ea + lb / Eb);
                const float err = std::fabs(g->stiffness - k_law) / std::fmax(k_law, 1e-9f);
                const bool not_flesh = st.min_modulus <= 0.0f ||
                                       (Ea >= st.min_modulus && Eb >= st.min_modulus);
                char t[260];
                std::snprintf(t, sizeof t, "INV-29/INV-38: %s's first bond is priced from its NAMED "
                              "materials at birth (%s E %.1e, %s E %.1e; k %.3g = law %.3g, err %.2f%%%s)",
                              st.name, mat_name(pa.material_type), Ea, mat_name(pb.material_type), Eb,
                              g->stiffness, k_law, err * 100.0f,
                              st.min_modulus > 0.0f ? (not_flesh ? ", not flesh" : ", FLESH-PRICED") : "");
                st.law_checked = true; st.law_ok = err < K_TOLERANCE && not_flesh; st.law_text = t;
            } else if (st.min_modulus > 0.0f) {
                // A wood-and-leaf stage with no priceable bond is a finding, not a pass.
                char t[200];
                std::snprintf(t, sizeof t, "INV-29/INV-38: %s has a bond the material law can price "
                              "(%zu bonds between its own bodies, %zu with a contact area)",
                              st.name, st.own_bonds, st.own_with_area);
                st.law_checked = true; st.law_ok = false; st.law_text = t;
            }
        }
    }

    // Sixty frames with Argus watching one body per stage (INV-11).
    for (int f = 0; f < WATCH_FRAMES; ++f) {
        engine.update(DT);
        argus.observe(ps, f);
    }

    std::vector<Verdict> vs;
    char t[260];
    for (const Stage& st : stages) {
        std::snprintf(t, sizeof t, "INV-38: %s names a material on every body it births "
                      "(named %zu of %zu, defaulted %zu)", st.name, st.named, st.bodies, st.unnamed);
        vs.push_back({t, st.bodies > 0 && st.unnamed == 0});
        std::snprintf(t, sizeof t, "INV-38: no birth of %s crossed the door unnamed (%zu)",
                      st.name, st.unnamed_at_door);
        vs.push_back({t, st.unnamed_at_door == 0});

        if (st.law_checked) vs.push_back({st.law_text, st.law_ok});
        if (st.watched >= 0) {
            const float pk = argus.peak_speed(st.watched);
            std::snprintf(t, sizeof t, "INV-11: %s's first body stays bounded over %d frames "
                          "(peak %.2f m/s < %.0f)", st.name, WATCH_FRAMES, pk, SPEED_BOUND);
            vs.push_back({t, pk < SPEED_BOUND});
        }
    }

    int red = 0;
    std::printf("\n");
    for (const Verdict& v : vs) {
        std::printf("  [%s] %s\n", v.ok ? "PASS" : "FAIL", v.text.c_str());
        if (!v.ok) ++red;
    }
    std::printf("\n  %s (%d red of %zu)\n",
                red == 0 ? "EVERY GENERATOR NAMES ITS BODIES" : "BORN RED: bodies born without a material",
                red, vs.size());
    engine.shutdown();
    return red == 0 ? 0 : 1;
}
