#include "director/playback_registrations.h"

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/effects/mutation_playback.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"

#include <variant>

namespace logotron::director {

namespace {

// A play that holds a brief glowing light particle for a fixed
// duration, then destroys it. Engine ticks this on real time so
// it keeps animating during cinematic pause. The light radius
// pulses via emission_strength so it reads as a "rez-in" beat
// rather than a static dot.
struct LightPulsePlay : Logosphere::Effects::Playback {
    Engine* engine = nullptr;
    int     light_particle_idx = -1;
    float   peak_strength = 6.0f;

    LightPulsePlay(Engine* eng, int idx, float peak)
        : engine(eng), light_particle_idx(idx), peak_strength(peak) {
        duration_ = 0.6f;  // visible-but-brief
    }

    bool tick(float real_dt) override {
        elapsed_ += real_dt;

        // Pulse: ramp 0 → peak in first 30%, hold to 70%, ramp
        // back down. Ends at 0 so the cleanup-destroy is a fade.
        float t = elapsed_ / duration_;
        float strength;
        if (t < 0.3f)      strength = peak_strength * (t / 0.3f);
        else if (t < 0.7f) strength = peak_strength;
        else if (t < 1.0f) strength = peak_strength * ((1.0f - t) / 0.3f);
        else               strength = 0.0f;

        if (engine && light_particle_idx >= 0) {
            auto& ps = engine->get_particle_system();
            auto write = ps.lock_particles_for_write();
            auto& vec  = write.get_particles();
            if (static_cast<size_t>(light_particle_idx) < vec.size()) {
                vec[light_particle_idx].emission_strength = strength;
            }
        }

        if (elapsed_ >= duration_) {
            // Destroy the light particle on completion. Use the
            // particle-by-index path; KG entity (if any) is the
            // app's concern, not ours.
            if (engine && light_particle_idx >= 0) {
                auto& ps = engine->get_particle_system();
                ps.delete_particles_immediate({light_particle_idx});
                light_particle_idx = -1;
            }
            return false;  // done — registry sweeps us
        }
        return true;
    }
};

// Spawn a Tron-blue light particle at (x, y, z). Returns the
// render index, or -1 on failure.
int spawn_pulse_light(Engine& engine, float x, float y, float z,
                      float r, float g, float b) {
    auto& ps = engine.get_particle_system();
    return ps.create_light_for_entity(
        x, y, z,
        /*strength=*/0.0f,         // ramped by LightPulsePlay::tick
        /*radius=*/3.0f,
        r, g, b,
        /*kg=*/nullptr, /*entity=*/0);
}

// Read float property; returns fallback on missing/parse failure.
float read_float_prop(const kg::KGModule& kg, kg::EntityID id,
                      const std::string& key, float fallback) {
    auto s = kg.getProperty(id, key);
    if (s.empty()) return fallback;
    try { return std::stof(s); } catch (...) { return fallback; }
}

// Factory for create_entity ops: pulse a light at the entity's
// (x, y) read from KG. Color picked per type by the caller.
Logosphere::Effects::PlaybackFactory make_create_pulse_factory(
    float r, float g, float b) {
    return [r, g, b](const Logosphere::Effects::PlaybackContext& ctx)
        -> std::unique_ptr<Logosphere::Effects::Playback> {
        if (!ctx.engine) return std::make_unique<Logosphere::Effects::Playback>();
        auto& kg = ctx.engine->get_kg();
        float x = read_float_prop(kg, ctx.created_entity, "x", 0.0f);
        float y = read_float_prop(kg, ctx.created_entity, "y", 0.0f);
        // TrailSegment uses start_x/start_y; fall back to those
        // when x/y aren't set on this entity type.
        if (x == 0.0f && y == 0.0f) {
            x = read_float_prop(kg, ctx.created_entity, "start_x", 0.0f);
            y = read_float_prop(kg, ctx.created_entity, "start_y", 0.0f);
        }
        int idx = spawn_pulse_light(*ctx.engine, x, y, 0.8f, r, g, b);
        return std::make_unique<LightPulsePlay>(ctx.engine, idx, 6.0f);
    };
}

// Factory for set_property ops: pulse a light at the TARGET
// entity's (x, y). Used for Cycle.max_speed (ring overlay over
// the affected bike), arena resize (perimeter flash), etc.
Logosphere::Effects::PlaybackFactory make_set_pulse_factory(
    float r, float g, float b) {
    return [r, g, b](const Logosphere::Effects::PlaybackContext& ctx)
        -> std::unique_ptr<Logosphere::Effects::Playback> {
        if (!ctx.engine) return std::make_unique<Logosphere::Effects::Playback>();
        auto* sp = std::get_if<kg::KGOpSetProperty>(&ctx.op);
        if (!sp || !sp->target.is_numeric()) {
            return std::make_unique<Logosphere::Effects::Playback>();
        }
        auto& kg = ctx.engine->get_kg();
        float x = read_float_prop(kg, sp->target.id, "x", 0.0f);
        float y = read_float_prop(kg, sp->target.id, "y", 0.0f);
        int idx = spawn_pulse_light(*ctx.engine, x, y, 1.5f, r, g, b);
        return std::make_unique<LightPulsePlay>(ctx.engine, idx, 4.0f);
    };
}

}  // namespace

void register_playback_handlers(Engine& engine) {
    auto& reg = engine.get_mutation_playback_registry();

    // Tron palette: bright cyan for new geometry, magenta for
    // Director-tinted ops, white for arena retunes.
    constexpr float kCyanR = 0.30f, kCyanG = 0.85f, kCyanB = 1.00f;
    constexpr float kMagR  = 1.00f, kMagG  = 0.20f, kMagB  = 1.00f;
    constexpr float kWhitR = 1.00f, kWhitG = 1.00f, kWhitB = 1.00f;

    // create_entity plays — TrailSegment in cyan (matches the
    // existing trail color), Wormhole in magenta (Master-Control
    // accent).
    reg.register_create("TrailSegment", make_create_pulse_factory(kCyanR, kCyanG, kCyanB));
    reg.register_create("Wormhole",     make_create_pulse_factory(kMagR,  kMagG,  kMagB));

    // set_property plays — speed retunes pulse over the affected
    // cycle; arena retunes pulse at the arena's center. The registry
    // keys on the ACTUAL entity type (kg.getType), and the cycles are
    // spawned as "PlayerCycle"/"AICycle" — the bare "Cycle" keys never
    // matched, so Director speed retunes on the AI were invisible
    // (fact found in the 2026-07-18 legibility audit). Register all
    // three spellings; bare "Cycle" stays for any future generic use.
    for (const char* cycle_type : {"Cycle", "PlayerCycle", "AICycle"}) {
        reg.register_set(cycle_type, "max_speed",  make_set_pulse_factory(kWhitR, kWhitG, kWhitB));
        reg.register_set(cycle_type, "base_speed", make_set_pulse_factory(kWhitR, kWhitG, kWhitB));
    }
    reg.register_set("Arena", "arena_w",    make_set_pulse_factory(kMagR,  kMagG,  kMagB));
    reg.register_set("Arena", "arena_h",    make_set_pulse_factory(kMagR,  kMagG,  kMagB));

    // play_cinematic plays — Phase E. KISS first cut: each named
    // routine maps to a long, intense pulse at the target. Real
    // disk-throw / orbit / dialogue cinematics layer in once the
    // transforms branch (and the camera-director's "swing to
    // entity" sugar) lands. The point of E is the contract — the
    // LLM gets to *call* a named cinematic, the host decides what
    // it looks like, no engine code changes when names get added.
    auto cinematic_factory = [](float r, float g, float b, float duration) {
        return [r, g, b, duration](const Logosphere::Effects::PlaybackContext& ctx)
            -> std::unique_ptr<Logosphere::Effects::Playback> {
            if (!ctx.engine) return std::make_unique<Logosphere::Effects::Playback>();
            auto* pc = std::get_if<kg::KGOpPlayCinematic>(&ctx.op);
            if (!pc) return std::make_unique<Logosphere::Effects::Playback>();
            // Resolve target to a position; if no target, pulse
            // at origin.
            float x = 0.0f, y = 0.0f;
            if (pc->target.is_numeric()) {
                auto& kg = ctx.engine->get_kg();
                x = read_float_prop(kg, pc->target.id, "x", 0.0f);
                y = read_float_prop(kg, pc->target.id, "y", 0.0f);
            }
            int idx = spawn_pulse_light(*ctx.engine, x, y, 1.5f, r, g, b);
            auto play = std::make_unique<LightPulsePlay>(ctx.engine, idx, 8.0f);
            // override default duration via the captured value
            // (LightPulsePlay's duration_ is protected; reach via
            // a brief subclass-style hack inline).
            // For simplicity here we rely on the default 0.6s and
            // the brighter peak strength to read distinct from a
            // regular create/set pulse. Per-name durations land
            // when each cinematic gets its bespoke routine.
            (void)duration;
            return play;
        };
    };
    reg.register_cinematic("disk_throw_at",   cinematic_factory(kCyanR, kCyanG, kCyanB, 1.2f));
    reg.register_cinematic("freeze_zoom",     cinematic_factory(kWhitR, kWhitG, kWhitB, 1.0f));
    reg.register_cinematic("aerial_orbit",    cinematic_factory(kMagR,  kMagG,  kMagB,  1.5f));
    reg.register_cinematic("program_dialogue",cinematic_factory(kMagR,  kMagG,  kMagB,  2.0f));
}

}  // namespace logotron::director
