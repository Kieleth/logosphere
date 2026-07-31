#include "logosphere/interaction/particle_interaction_system.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/core/kg_parse.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace logosphere::interaction {

const InteractionProfile& ParticleInteractionSystem::default_profile() {
    // Category bit 0, full mask, no medium: rigid-contact everything
    // willing. This is what every particle was before profiles existed.
    static const InteractionProfile kDefault{};
    return kDefault;
}

const InteractionProfile& ParticleInteractionSystem::resolve(uint32_t id) const {
    if (id == 0) return default_profile();
    auto it = profiles_.find(id);
    // Unknown nonzero ids degrade to the default: a stale profile id
    // means physics-as-usual, never mutual tunneling.
    return it != profiles_.end() ? it->second : default_profile();
}

void ParticleInteractionSystem::register_profile(const InteractionProfile& profile) {
    if (profile.id == 0) {
        std::fprintf(stderr,
                     "[INTERACTION] profile id 0 is the reserved engine "
                     "default and cannot be registered — ignored\n");
        return;
    }
    profiles_[profile.id] = profile;
}

size_t ParticleInteractionSystem::load_profiles_from_kg(const kg::KGModule& kg) {
    size_t loaded = 0;
    for (kg::EntityID id : kg.findByType("ParticleInteractionProfile")) {
        InteractionProfile p;
        p.id = id;

        // Absent slots keep their profile defaults silently; present-but-
        // malformed slots warn through kg_parse (diagnostic, not crash —
        // these are optional tuning fields, not required data).
        auto read_float = [&](const char* key, float& out) {
            auto raw = kg.getProperty(id, key);
            if (raw.empty()) return;
            if (auto f = kg_parse::to_float(raw, key, id)) out = *f;
        };

        // category_bit: which bit (0-31) this profile occupies.
        {
            auto raw = kg.getProperty(id, "category_bit");
            if (!raw.empty()) {
                if (auto bit = kg_parse::to_int(raw, "category_bit", id)) {
                    if (*bit >= 0 && *bit < 32) {
                        p.category = 1u << *bit;
                    } else {
                        std::fprintf(stderr,
                                     "[INTERACTION] profile %u: category_bit %d out "
                                     "of range [0,31] — using bit 0\n", id, *bit);
                    }
                }
            }
        }

        // collides_with_mask: KG slots are int32; parse wide so games can
        // express the full uint32 domain ("4294967295"), and accept the
        // int32 bit pattern ("-1" == ALL) as a fallback.
        {
            auto raw = kg.getProperty(id, "collides_with_mask");
            if (!raw.empty()) {
                errno = 0;
                char* end = nullptr;
                unsigned long long v = std::strtoull(raw.c_str(), &end, 10);
                if (end && *end == '\0' && errno == 0 && v <= 0xFFFFFFFFull &&
                    raw[0] != '-') {
                    p.collides_with = static_cast<uint32_t>(v);
                } else if (auto s = kg_parse::to_int(raw, "collides_with_mask", id)) {
                    p.collides_with = static_cast<uint32_t>(*s);  // bit pattern
                } else {
                    std::fprintf(stderr,
                                 "[INTERACTION] profile %u: collides_with_mask "
                                 "'%s' unparseable — using ALL\n", id, raw.c_str());
                }
            }
        }

        read_float("drag_coefficient", p.drag_coefficient);
        read_float("buoyancy_factor", p.buoyancy_factor);
        read_float("field_fx", p.field_fx);
        read_float("field_fy", p.field_fy);
        read_float("field_fz", p.field_fz);

        profiles_[p.id] = p;
        ++loaded;
    }
    return loaded;
}

bool ParticleInteractionSystem::should_contact(uint32_t profile_a,
                                               uint32_t profile_b) const {
    // Fast path: two defaults (the overwhelmingly common case) always
    // contact without touching the map.
    if (profiles_.empty() || (profile_a == 0 && profile_b == 0)) return true;
    const InteractionProfile& a = resolve(profile_a);
    const InteractionProfile& b = resolve(profile_b);
    return (a.category & b.collides_with) != 0 &&
           (b.category & a.collides_with) != 0;
}

const InteractionProfile* ParticleInteractionSystem::find_profile(uint32_t id) const {
    if (id == 0) return nullptr;
    auto it = profiles_.find(id);
    return it != profiles_.end() ? &it->second : nullptr;
}

namespace {
inline uint64_t pair_key(uint32_t a, uint32_t b) {
    uint32_t lo = a < b ? a : b;
    uint32_t hi = a < b ? b : a;
    return (static_cast<uint64_t>(lo) << 32) | hi;
}
} // namespace

void ParticleInteractionSystem::process_filtered_overlaps(
    const std::vector<FilteredOverlap>& overlaps,
    logosphere::EventBus* bus) {
    // Current frame's pair map (dedups the per-substep duplicates the
    // solver records), remembering which side declares a medium so the
    // matching exit event can fire on episode close.
    std::unordered_map<uint64_t, uint32_t> current;
    current.reserve(overlaps.size());
    for (const auto& o : overlaps) {
        uint64_t k = pair_key(o.particle_a, o.particle_b);
        uint32_t medium = 0;
        if (const auto* pa = find_profile(o.profile_a); pa && pa->declares_medium()) {
            medium = o.profile_a;
        } else if (const auto* pb = find_profile(o.profile_b); pb && pb->declares_medium()) {
            medium = o.profile_b;
        }
        if (!current.emplace(k, medium).second) continue;  // substep duplicate
        if (open_episodes_.count(k)) continue;             // episode already open
        // Record both directions of the open for the transformation
        // tick: each side "entered" against the other's profile. Volume
        // rules require the opposite side to declare a medium; contact
        // rules fire on any filtered entry.
        {
            const auto* pa = find_profile(o.profile_a);
            const auto* pb = find_profile(o.profile_b);
            episode_opens_.push_back({o.particle_a, o.profile_b,
                                      pb && pb->declares_medium()});
            episode_opens_.push_back({o.particle_b, o.profile_a,
                                      pa && pa->declares_medium()});
        }
        if (bus) {
            onto::ContactFilteredEvent e;
            e.profile_a = static_cast<int32_t>(o.profile_a);
            e.profile_b = static_cast<int32_t>(o.profile_b);
            bus->contact_filtered().emit(e);
            if (medium != 0) {
                onto::VolumeEvent ve;
                ve.entered = true;
                ve.medium_profile = static_cast<int32_t>(medium);
                bus->volume().emit(ve);
            }
        }
    }

    // Pairs no longer overlapping close their episodes; medium episodes
    // emit the exit event.
    if (bus) {
        for (const auto& [k, medium] : open_episodes_) {
            if (medium != 0 && !current.count(k)) {
                onto::VolumeEvent ve;
                ve.entered = false;
                ve.medium_profile = static_cast<int32_t>(medium);
                bus->volume().emit(ve);
            }
        }
    }
    open_episodes_ = std::move(current);
}

size_t ParticleInteractionSystem::load_rules_from_kg(const kg::KGModule& kg) {
    using Trigger = TransformationRule::Trigger;
    using Effect = TransformationRule::Effect;
    size_t loaded = 0;
    for (kg::EntityID id : kg.findByType("TransformationRule")) {
        TransformationRule r;
        r.id = id;

        // trigger + effect are the rule's identity: malformed values
        // warn and skip the whole rule (a rule that can't say when or
        // what is not a rule).
        auto trig = kg.getProperty(id, "trigger");
        if (trig == "on_contact_filtered") r.trigger = Trigger::ON_CONTACT_FILTERED;
        else if (trig == "on_volume_enter") r.trigger = Trigger::ON_VOLUME_ENTER;
        else if (trig == "on_timer") r.trigger = Trigger::ON_TIMER;
        else {
            std::fprintf(stderr,
                         "[INTERACTION] rule %u: unknown trigger '%s' — skipped\n",
                         id, trig.c_str());
            continue;
        }
        auto eff = kg.getProperty(id, "effect");
        if (eff == "swap_profile") r.effect = Effect::SWAP_PROFILE;
        else if (eff == "fade_out") r.effect = Effect::FADE_OUT;
        else if (eff == "delete") r.effect = Effect::DELETE_PARTICLE;
        else if (eff == "emit_event") r.effect = Effect::EMIT_EVENT;
        else {
            std::fprintf(stderr,
                         "[INTERACTION] rule %u: unknown effect '%s' — skipped\n",
                         id, eff.c_str());
            continue;
        }

        // Optional tuning slots: absent keeps defaults, malformed warns
        // through kg_parse (same discipline as profile loading).
        if (auto raw = kg.getProperty(id, "target_profile"); !raw.empty()) {
            if (auto v = kg_parse::to_int(raw, "target_profile", id))
                r.target_profile = static_cast<uint32_t>(*v);
        }
        if (auto raw = kg.getProperty(id, "duration_s"); !raw.empty()) {
            if (auto v = kg_parse::to_float(raw, "duration_s", id))
                r.duration_s = *v;
        }
        if (auto raw = kg.getProperty(id, "trigger_profile"); !raw.empty()) {
            if (auto v = kg_parse::to_int(raw, "trigger_profile", id))
                r.trigger_profile = static_cast<uint32_t>(*v);
        }

        auto name = kg.getProperty(id, "name");
        r.name = name.empty() ? std::to_string(id) : name;

        rules_[r.id] = r;
        ++loaded;
    }
    return loaded;
}

void ParticleInteractionSystem::arm_transformation(
    uint32_t rule_id, const std::vector<kg::KGParticleID>& particles) {
    if (!rules_.count(rule_id)) {
        std::fprintf(stderr,
                     "[INTERACTION] arm_transformation: unknown rule %u — ignored\n",
                     rule_id);
        return;
    }
    for (kg::KGParticleID kgid : particles) {
        if (kgid == kg::INVALID_KG_PARTICLE_ID) continue;
        // initial_a is captured lazily on the first tick (no particle
        // view here) — sentinel < 0 marks "not yet sampled".
        armed_.push_back({kgid, rule_id, 0.0f});  // visual state sampled on first tick
    }
}

void ParticleInteractionSystem::tick_transformations(
    ParticleSystem::WriteView& particles, const kg::KGModule& kg,
    logosphere::EventBus* bus, float dt, std::vector<uint32_t>& out_delete) {
    using Trigger = TransformationRule::Trigger;
    using Effect = TransformationRule::Effect;

    // One-shot effects share this application path; fade_out instead
    // arms a long-running entry bound to stable KG identity.
    auto apply_effect = [&](const TransformationRule& r, uint32_t idx) {
        if (idx >= particles.size()) return;
        switch (r.effect) {
        case Effect::SWAP_PROFILE:
            particles[idx].interaction_profile_id = r.target_profile;
            break;
        case Effect::DELETE_PARTICLE:
            out_delete.push_back(idx);
            break;
        case Effect::EMIT_EVENT:
            if (bus) {
                onto::TransformationEvent te;
                te.rule_name = r.name;
                bus->transformations().emit(te);
            }
            break;
        case Effect::FADE_OUT: {
            // Bind by stable id: the fade outlives this frame's render
            // indices. Non-KG particles can't carry a transformation —
            // they are outside the ontology (documented v1 limit).
            kg::KGParticleID kgid = kg.getKGParticleByRenderIndex(idx);
            if (kgid != kg::INVALID_KG_PARTICLE_ID) {
                armed_.push_back({kgid, r.id, 0.0f});  // sampled on first tick
            }
            break;
        }
        }
    };

    // 1. Rules triggered by this frame's episode opens (recorded by
    //    process_filtered_overlaps; render indices still valid).
    if (!rules_.empty()) {
        for (const auto& open : episode_opens_) {
            for (const auto& [rid, r] : rules_) {
                bool trigger_matches =
                    (r.trigger == Trigger::ON_VOLUME_ENTER && open.is_medium) ||
                    (r.trigger == Trigger::ON_CONTACT_FILTERED);
                if (!trigger_matches) continue;
                if (r.trigger_profile != 0 &&
                    r.trigger_profile != open.other_profile) continue;
                apply_effect(r, open.intruder_idx);
            }
        }
    }
    episode_opens_.clear();

    // 2. Armed entries: resolve stable ids; a dead particle resolves
    //    INVALID and drops out (fail-safe — deletion already erased
    //    its KG mapping).
    for (size_t i = 0; i < armed_.size();) {
        ArmedTransformation& a = armed_[i];
        auto erase_current = [&]() {
            armed_[i] = armed_.back();
            armed_.pop_back();
        };
        auto rule_it = rules_.find(a.rule_id);
        kg::RenderIndex idx = kg.getRenderIndex(a.kgid);
        if (rule_it == rules_.end() || idx == kg::INVALID_RENDER_INDEX ||
            idx >= particles.size()) {
            erase_current();
            continue;
        }
        const TransformationRule& r = rule_it->second;
        // Sample the starting visual state lazily (no particle view at
        // arm time). "Fade" must dim whatever the renderer actually
        // reads. A self-emissive particle is drawn at its COLOR
        // (r,g,b) at full intensity via a binary emissive flag — the
        // render path reads neither alpha nor emission_strength for it
        // (see render_pipeline is_light_source_map). So for emissive
        // bodies the only dimmer is the color itself; for normally-lit
        // bodies it is alpha. Ramp both so fade_out works for either.
        if (!a.sampled) {
            const Particle& p0 = particles[idx];
            a.initial_a = p0.a;
            a.initial_emission = p0.emission_strength;
            a.initial_r = p0.r; a.initial_g = p0.g; a.initial_b = p0.b;
            a.sampled = true;
        }
        a.elapsed += dt;

        if (r.effect == Effect::FADE_OUT) {
            // How a particle can actually fade depends on which render path
            // draws it, and alpha is the ROUTING KEY: the moment particle_a
            // drops below 1.0, prepare_gpu_data reroutes the surface out of
            // the deferred (opaque) pipeline into the forward transparent
            // pass, whose emissive branch draws full-bright regardless of
            // color. So a self-emissive particle must fade by COLOR with its
            // alpha pinned at 1.0 (a glow dims, it never turns translucent),
            // while a normally-lit particle fades by alpha as usual.
            // Measured in test_trail_fade_render: rgb-only ramp renders
            // 255 -> 38; any alpha ramp on an emissive particle freezes the
            // screen at 255 until deletion (the "pop").
            const bool emissive = particles[idx].is_self_emissive;
            if (r.duration_s > 0.0f && a.elapsed < r.duration_s) {
                const float k = 1.0f - a.elapsed / r.duration_s;
                if (emissive) {
                    particles[idx].r = a.initial_r * k;
                    particles[idx].g = a.initial_g * k;
                    particles[idx].b = a.initial_b * k;
                } else {
                    particles[idx].a = a.initial_a * k;
                }
                ++i;
            } else {
                if (emissive) {
                    particles[idx].r = particles[idx].g = particles[idx].b = 0.0f;
                } else {
                    particles[idx].a = 0.0f;
                }
                out_delete.push_back(idx);
                erase_current();
            }
        } else if (a.elapsed >= r.duration_s) {
            // Timed one-shot: fires once when the deadline passes
            // (duration 0 = next tick).
            apply_effect(r, idx);
            erase_current();
        } else {
            ++i;
        }
    }
}

void ParticleInteractionSystem::apply_volume_forces(
    ParticleSystem::WriteView& particles,
    const std::vector<FilteredOverlap>& overlaps,
    float gx, float gy, float gz, float dt) const {
    if (overlaps.empty() || profiles_.empty()) return;

    auto apply_medium = [&](const InteractionProfile* medium_prof,
                            uint32_t medium_idx, uint32_t intruder_idx) {
        if (!medium_prof || !medium_prof->declares_medium()) return;
        if (medium_idx >= particles.size() || intruder_idx >= particles.size()) return;
        Particle& in = particles[intruder_idx];
        const Particle& med = particles[medium_idx];
        const float m = in.GetMass();
        if (m <= 0.0f) return;

        // Drag toward the medium's own velocity (a river carries you).
        if (medium_prof->drag_coefficient != 0.0f) {
            const float f = (medium_prof->drag_coefficient / m) * dt;
            in.vx -= (in.vx - med.vx) * f;
            in.vy -= (in.vy - med.vy) * f;
            in.vz -= (in.vz - med.vz) * f;
        }
        // Buoyancy: gross lift B·|g| against the gravity VECTOR (the
        // solver applies gravity itself, so the net is (B-1)·g).
        if (medium_prof->buoyancy_factor != 0.0f) {
            const float b = medium_prof->buoyancy_factor * dt;
            in.vx -= gx * b;
            in.vy -= gy * b;
            in.vz -= gz * b;
        }
        // Directional field force (Newtons).
        if (medium_prof->field_fx != 0.0f || medium_prof->field_fy != 0.0f ||
            medium_prof->field_fz != 0.0f) {
            in.vx += (medium_prof->field_fx / m) * dt;
            in.vy += (medium_prof->field_fy / m) * dt;
            in.vz += (medium_prof->field_fz / m) * dt;
        }
        // An active medium keeps the intruder awake: a sleeping
        // particle drops out of broad phase and would freeze
        // mid-medium, never receiving forces (or its exit event).
        // Both fields, or the rest system re-sleeps next frame off the
        // accumulated counter (and zeroes the velocity on the way).
        in.is_at_rest = false;
        in.frames_at_rest = 0;
    };

    // Dedup pairs across substeps: forces integrate once per frame.
    std::unordered_set<uint64_t> seen;
    seen.reserve(overlaps.size());
    for (const auto& o : overlaps) {
        if (!seen.insert(pair_key(o.particle_a, o.particle_b)).second) continue;
        apply_medium(find_profile(o.profile_a), o.particle_a, o.particle_b);
        apply_medium(find_profile(o.profile_b), o.particle_b, o.particle_a);
    }
}

} // namespace logosphere::interaction
