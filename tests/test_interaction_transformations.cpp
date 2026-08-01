// =============================================================================
// PARTICLE INTERACTION — declarative transformations (Phase 4 contract)
// =============================================================================
// TransformationRule entities declared in the KG execute in the engine's
// interaction tick: a trigger (on_volume_enter, on_contact_filtered,
// on_timer) fires an effect (swap_profile, fade_out, delete, emit_event)
// on the affected particle. Long-running effects track particles by
// stable KGParticleID and drop out fail-safe when the particle dies.
//
// Contracts:
//   t1  SWAP_PROFILE on volume enter, trigger_profile-bound: a ball
//       entering the bound medium gets target_profile written; a ball
//       entering a DIFFERENT medium does not.
//   t2  FADE_OUT armed via arm_transformation (on_timer): alpha ramps
//       monotonically to 0 over duration_s, then the particle is
//       deleted through the deferred queue (count returns to baseline)
//       while its KG entity survives (render index goes INVALID).
//   t3  EMIT_EVENT on volume enter: exactly one TransformationEvent
//       with the rule's name per episode on transformations().
//   t4  DELETE via on_timer duration 0: fires next tick, count drops
//       after the deferred window, no leak.
//   t5  FAIL-SAFE: externally deleting a mid-fade particle drops the
//       armed entry silently — no crash, count stays consistent.
//
// Run: ./build/logosphere-physics-guards --test test_interaction_transformations
// =============================================================================

#include "core/engine.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "materials.h"
#include "particle.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "generated/earth_ontology_registry.h"

namespace {

// Engine + a large KINEMATIC medium column: x,y ∈ [-20,20], z ∈ [10,50].
// Balls are KG-backed (Rock entities) so rules can track them by
// stable KGParticleID.
struct TransformRig {
    Engine engine;
    bool ok = false;
    int medium = -1;

    explicit TransformRig(const char* title, uint32_t profile_id) {
        EngineConfig cfg;
        cfg.create_display = false;
        cfg.window_title = title;
        cfg.enable_chat_window = false;
        ok = (engine.initialize(cfg) == 0);
        if (!ok) return;

        Particle col = {};
        col.shape = ParticleShape::BOX;
        col.x = 0.0f; col.y = 0.0f; col.z = 30.0f;
        col.width = 40.0f; col.height = 40.0f; col.thickness = 40.0f;
        col.SetMaterial(Materials::Type::STONE);
        medium = engine.add_particle(col);
        auto view = engine.get_particle_system().lock_particles_for_write();
        view[medium].solver_mode = ParticleSolverMode::KINEMATIC;
        view[medium].is_at_rest = false;
        view[medium].interaction_profile_id = profile_id;
    }

    struct KGBall {
        int idx = -1;
        kg::EntityID entity = 0;
        kg::KGParticleID kgid = 0;
    };

    KGBall kg_ball(float x, float y, float z, float vz) {
        KGBall b;
        auto& kg = engine.get_kg();
        auto& ps = engine.get_particle_system();
        // Rock is earth-like vocabulary and lives in the earth pack;
        // Engine starts with the core alone. Without this the entity
        // is rejected and every ball is INVALID_ENTITY, which the old
        // code never noticed because it did not check.
        // See docs/ONTOLOGY_LAYERS.md.
        b.entity = kg.createEntity("Rock");
        if (b.entity == kg::INVALID_ENTITY) {
            kg.extendOntology(earth::ontology::registry());
            b.entity = kg.createEntity("Rock");
        }
        Particle p = {};
        p.shape = ParticleShape::SPHERE;
        p.x = x; p.y = y; p.z = z;
        p.width = 0.3f; p.height = 0.3f; p.thickness = 0.3f;
        p.size = 0.3f;
        p.SetMaterial(Materials::Type::STONE);
        b.idx = ps.add_particle_to_entity(p, &kg, b.entity);
        auto kgids = kg.getEntityKGParticles(b.entity);
        if (!kgids.empty()) b.kgid = kgids[0];
        auto view = ps.lock_particles_for_write();
        view[b.idx].solver_mode = ParticleSolverMode::DYNAMIC;
        view[b.idx].is_at_rest = false;
        view[b.idx].vz = vz;
        return b;
    }

    kg::EntityID make_rule(const char* trigger, const char* effect,
                           uint32_t target_profile, float duration_s,
                           uint32_t trigger_profile) {
        auto& kg = engine.get_kg();
        auto rule = kg.createEntity("TransformationRule");
        kg.setProperty(rule, "trigger", trigger);
        kg.setProperty(rule, "effect", effect);
        if (target_profile != 0)
            kg.setProperty(rule, "target_profile", std::to_string(target_profile));
        if (duration_s != 0.0f)
            kg.setProperty(rule, "duration_s", std::to_string(duration_s));
        if (trigger_profile != 0)
            kg.setProperty(rule, "trigger_profile", std::to_string(trigger_profile));
        return rule;
    }

    uint32_t profile_of(int idx) {
        auto v = engine.get_particle_system().lock_particles_for_read();
        return v[idx].interaction_profile_id;
    }
    float alpha_of(int idx) {
        auto v = engine.get_particle_system().lock_particles_for_read();
        return v[idx].a;
    }
};

// Water-like medium: passable (mask 0), gentle drag so it declares a
// medium (volume episodes only open for medium-declaring profiles).
logosphere::interaction::InteractionProfile medium_profile(uint32_t id,
                                                           unsigned bit) {
    logosphere::interaction::InteractionProfile p;
    p.id = id;
    p.category = 1u << bit;
    p.collides_with = 0u;
    p.drag_coefficient = 0.5f;
    return p;
}

} // namespace

bool test_interaction_transformations() {
    printf("\n=== Interaction Transformations ===\n");
    const double dt = 1.0 / 60.0;
    bool all_ok = true;

    // ------------------------------------------------------------------
    // t1 — swap_profile on volume enter, bound to trigger_profile.
    // ------------------------------------------------------------------
    {
        uint32_t got_bound = 0, got_other = 0;
        for (int variant = 0; variant <= 1; ++variant) {
            // variant 0: medium IS the bound profile (600) -> swap fires.
            // variant 1: medium is a different profile (601) -> no swap.
            uint32_t medium_id = variant == 0 ? 600u : 601u;
            TransformRig rig("transformations t1", medium_id);
            if (!rig.ok) { printf("  ERROR: engine init (t1)\n"); return false; }
            auto& isys = rig.engine.get_interaction_system();
            isys.register_profile(medium_profile(medium_id, 3));
            isys.register_profile(medium_profile(777u, 4));  // swap target

            rig.make_rule("on_volume_enter", "swap_profile",
                          /*target=*/777u, /*duration=*/0.0f,
                          /*trigger_profile=*/600u);
            isys.load_rules_from_kg(rig.engine.get_kg());

            auto ball = rig.kg_ball(0.0f, 0.0f, 55.0f, 0.0f);  // falls in
            for (int f = 0; f < 120; ++f) rig.engine.update(dt);
            if (variant == 0) got_bound = rig.profile_of(ball.idx);
            else              got_other = rig.profile_of(ball.idx);
        }
        bool swapped = (got_bound == 777u);
        bool control_untouched = (got_other == 0u);
        printf("  %s: t1 swap_profile: bound medium -> %u, other medium -> %u\n",
               (swapped && control_untouched) ? "PASS" : "FAIL",
               got_bound, got_other);
        all_ok = all_ok && swapped && control_untouched;
    }

    // ------------------------------------------------------------------
    // t2 — fade_out armed on_timer: monotone alpha ramp, then deferred
    //      deletion; KG entity survives with an INVALID render index.
    // ------------------------------------------------------------------
    {
        TransformRig rig("transformations t2", 610u);
        if (!rig.ok) { printf("  ERROR: engine init (t2)\n"); return false; }
        auto& isys = rig.engine.get_interaction_system();
        auto rule = rig.make_rule("on_timer", "fade_out",
                                  0u, /*duration=*/0.5f, 0u);
        isys.load_rules_from_kg(rig.engine.get_kg());

        auto ball = rig.kg_ball(0.0f, 0.0f, 5.0f, 0.0f);
        const size_t baseline = rig.engine.get_particle_system().count();
        isys.arm_transformation(rule, {ball.kgid});

        rig.engine.update(dt);                       // arm ticks in
        float a_early = 0.0f, a_mid = 0.0f;
        for (int f = 0; f < 8; ++f) rig.engine.update(dt);
        a_early = rig.alpha_of(ball.idx);            // ~0.15 s in
        for (int f = 0; f < 12; ++f) rig.engine.update(dt);
        a_mid = rig.alpha_of(ball.idx);              // ~0.35 s in
        // Finish the fade + deferred window (0.5 s = 30 frames total,
        // +3 frames triple-buffer, headless flush at top of update).
        for (int f = 0; f < 16; ++f) rig.engine.update(dt);

        const size_t after = rig.engine.get_particle_system().count();
        bool ramped = a_early < 0.95f && a_mid < a_early && a_mid > 0.0f;
        bool deleted = (after == baseline - 1);
        bool entity_survives =
            !rig.engine.get_kg().getEntityKGParticles(ball.entity).empty();
        bool index_invalid =
            rig.engine.get_kg().getRenderIndex(ball.kgid) == kg::INVALID_RENDER_INDEX;
        printf("  %s: t2 fade_out: a %.2f -> %.2f, count %zu -> %zu, "
               "entity kept=%d, render idx invalid=%d\n",
               (ramped && deleted && entity_survives && index_invalid) ? "PASS" : "FAIL",
               a_early, a_mid, baseline, after,
               (int)entity_survives, (int)index_invalid);
        all_ok = all_ok && ramped && deleted && entity_survives && index_invalid;
    }

    // ------------------------------------------------------------------
    // t3 — emit_event on volume enter: one TransformationEvent per
    //      episode, carrying the rule's name (entity id as string).
    // ------------------------------------------------------------------
    {
        TransformRig rig("transformations t3", 620u);
        if (!rig.ok) { printf("  ERROR: engine init (t3)\n"); return false; }
        auto& isys = rig.engine.get_interaction_system();
        isys.register_profile(medium_profile(620u, 3));
        auto rule = rig.make_rule("on_volume_enter", "emit_event",
                                  0u, 0.0f, /*trigger_profile=*/0u);
        isys.load_rules_from_kg(rig.engine.get_kg());

        auto reader = rig.engine.get_event_bus().transformations().create_reader();
        rig.kg_ball(0.0f, 0.0f, 55.0f, 0.0f);   // falls in, out the bottom

        int events = 0;
        std::string seen_name;
        for (int f = 0; f < 300; ++f) {
            rig.engine.update(dt);
            for (const auto& e : reader.drain()) {
                ++events;
                seen_name = e.rule_name.value_or("");
            }
        }
        bool once = (events == 1);
        bool named = (seen_name == std::to_string(rule));
        printf("  %s: t3 emit_event: %d event(s), rule_name '%s' (expect '%u')\n",
               (once && named) ? "PASS" : "FAIL",
               events, seen_name.c_str(), rule);
        all_ok = all_ok && once && named;
    }

    // ------------------------------------------------------------------
    // t4 — delete armed with duration 0: fires next tick, deferred
    //      deletion lands, no leak.
    // ------------------------------------------------------------------
    {
        TransformRig rig("transformations t4", 630u);
        if (!rig.ok) { printf("  ERROR: engine init (t4)\n"); return false; }
        auto& isys = rig.engine.get_interaction_system();
        auto rule = rig.make_rule("on_timer", "delete", 0u, 0.0f, 0u);
        isys.load_rules_from_kg(rig.engine.get_kg());

        auto ball = rig.kg_ball(0.0f, 0.0f, 5.0f, 0.0f);
        const size_t baseline = rig.engine.get_particle_system().count();
        isys.arm_transformation(rule, {ball.kgid});
        for (int f = 0; f < 6; ++f) rig.engine.update(dt);  // fire + window
        const size_t after = rig.engine.get_particle_system().count();
        for (int f = 0; f < 10; ++f) rig.engine.update(dt); // stability
        const size_t later = rig.engine.get_particle_system().count();

        bool deleted = (after == baseline - 1) && (later == after);
        printf("  %s: t4 delete: count %zu -> %zu (stable %zu)\n",
               deleted ? "PASS" : "FAIL", baseline, after, later);
        all_ok = all_ok && deleted;
    }

    // ------------------------------------------------------------------
    // t5 — fail-safe: external deletion mid-fade drops the armed entry.
    // ------------------------------------------------------------------
    {
        TransformRig rig("transformations t5", 640u);
        if (!rig.ok) { printf("  ERROR: engine init (t5)\n"); return false; }
        auto& isys = rig.engine.get_interaction_system();
        auto rule = rig.make_rule("on_timer", "fade_out", 0u, 0.5f, 0u);
        isys.load_rules_from_kg(rig.engine.get_kg());

        auto ball = rig.kg_ball(0.0f, 0.0f, 5.0f, 0.0f);
        const size_t baseline = rig.engine.get_particle_system().count();
        isys.arm_transformation(rule, {ball.kgid});
        for (int f = 0; f < 10; ++f) rig.engine.update(dt);  // mid-fade
        rig.engine.get_particle_system().delete_particle_immediate(ball.idx);
        for (int f = 0; f < 40; ++f) rig.engine.update(dt);  // past fade end

        const size_t after = rig.engine.get_particle_system().count();
        bool consistent = (after == baseline - 1);
        printf("  %s: t5 fail-safe: survived external delete, count %zu -> %zu\n",
               consistent ? "PASS" : "FAIL", baseline, after);
        all_ok = all_ok && consistent;
    }

    printf("  [%s]\n", all_ok ? "PASS" : "FAIL - transformations broken");
    return all_ok;
}
