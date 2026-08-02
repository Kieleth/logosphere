#include "logosphere/interaction/particle_interaction_system.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/core/kg_parse.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace logosphere::interaction {

namespace {
// The ontology spells enum values upper (as every other enum in
// schema/logosphere.yaml does); rules in the wild are authored lower.
// One normalization rule, applied at the single parse point, so both
// spellings resolve and neither becomes a second vocabulary.
std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}
}  // namespace

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

// Who a contacting particle actually is, in ontology terms.
//
// A particle belongs to a body part; a body part belongs to a creature.
// Both matter to a consequence: armour and injury are per-part, while
// "the predator touched the prey" is per-entity. An unowned part (no
// incoming HAS_PART) is its own entity, which is the common case for
// scenery.
namespace {
struct ContactParty {
    kg::EntityID part = kg::INVALID_ENTITY;
    kg::EntityID entity = kg::INVALID_ENTITY;
    bool resolved() const { return part != kg::INVALID_ENTITY; }
};

ContactParty resolve_party(const kg::KGModule& kg, uint32_t render_idx) {
    ContactParty p;
    kg::KGParticleID kgid = kg.getKGParticleByRenderIndex(render_idx);
    if (kgid == kg::INVALID_KG_PARTICLE_ID) return p;   // not in the ontology
    p.part = kg.getEntityByKGParticle(kgid);
    if (p.part == kg::INVALID_ENTITY) return p;

    // One hop up. A part claimed by several owners is legal but
    // ambiguous for attribution, so take the first and stay
    // deterministic rather than guessing.
    auto owners = kg.getRelatedReverse(p.part, "HAS_PART");
    p.entity = owners.empty() ? p.part : owners.front();
    return p;
}
}  // namespace

bool ParticleInteractionSystem::wants_contacts(
    const logosphere::EventBus* bus) const {
    if (has_contact_rules_) return true;
    return bus && bus->collisions().subscriber_count() > 0;
}

size_t ParticleInteractionSystem::process_contacts(
    const std::vector<RigidContact>& contacts, const kg::KGModule& kg,
    logosphere::EventBus* bus) {
    // The gate. Contacts are cheap to ignore and expensive to resolve,
    // and a tiled floor produces them by the thousand, so nothing below
    // runs when nobody is listening. open_contacts_ is cleared so a
    // later listener starts from a clean slate rather than inheriting
    // episodes opened while unobserved.
    if (!wants_contacts(bus)) {
        open_contacts_.clear();
        return 0;
    }

    std::unordered_set<uint64_t> current;
    current.reserve(contacts.size());
    size_t emitted = 0;

    for (const auto& c : contacts) {
        const uint64_t k = pair_key(c.particle_a, c.particle_b);
        if (!current.insert(k).second) continue;   // substep duplicate
        if (open_contacts_.count(k)) continue;     // episode already open

        // A pair with no KG identity on either side cannot be named in
        // an event. Skipping here also keeps the common floor-tile case
        // off the resolution path entirely.
        ContactParty a = resolve_party(kg, c.particle_a);
        ContactParty b = resolve_party(kg, c.particle_b);
        if (!a.resolved() && !b.resolved()) continue;

        if (bus) {
            onto::CollisionEvent e;
            e.event_type = "COLLISION";
            if (a.resolved()) {
                e.source_entity_id = std::to_string(a.entity);
                e.source_part_id   = std::to_string(a.part);
            }
            if (b.resolved()) {
                e.target_entity_id = std::to_string(b.entity);
                e.target_part_id   = std::to_string(b.part);
            }
            e.normal_x = c.normal_x;
            e.normal_y = c.normal_y;
            e.normal_z = c.normal_z;
            e.contact_x = c.contact_x;
            e.contact_y = c.contact_y;
            e.contact_z = c.contact_z;
            e.penetration = c.penetration;
            e.approach_speed = c.approach_speed;
            bus->collisions().emit(e);
            ++emitted;
        }
    }

    open_contacts_ = std::move(current);
    return emitted;
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
        //
        // The vocabulary lives in the ontology (enum TransformationTrigger)
        // and is parsed through the GENERATED from_string, so the schema
        // and this loader cannot drift: adding a value to the YAML is the
        // only way to add one here. Case is normalized first because the
        // schema spells enums upper (matching every other enum in the
        // file) while rules in the wild are authored lower.
        //
        // This parse is the ONLY enforcement point. KGCore::setProperty
        // validates nothing (ontology_registry.h:46 claims otherwise and
        // is wrong for property writes), so an unknown trigger is caught
        // here or never.
        auto trig = kg.getProperty(id, "trigger");
        onto::TransformationTrigger parsed_trigger{};
        if (!onto::from_string(to_upper(trig).c_str(), parsed_trigger)) {
            std::fprintf(stderr,
                         "[INTERACTION] rule %u: unknown trigger '%s' — skipped\n",
                         id, trig.c_str());
            continue;
        }
        switch (parsed_trigger) {
        case onto::TransformationTrigger::ON_CONTACT:
            r.trigger = Trigger::ON_CONTACT; break;
        case onto::TransformationTrigger::ON_CONTACT_FILTERED:
            r.trigger = Trigger::ON_CONTACT_FILTERED; break;
        case onto::TransformationTrigger::ON_VOLUME_ENTER:
            r.trigger = Trigger::ON_VOLUME_ENTER; break;
        case onto::TransformationTrigger::ON_TIMER:
            r.trigger = Trigger::ON_TIMER; break;
        }
        // Effects are case-normalized the same way, but the vocabulary
        // is a FLOOR rather than a closed set: the schema types this slot
        // string precisely because a game may register its own effect
        // name. Today the engine only knows its built-ins, so an
        // unrecognized name still warns and skips; the registry that
        // makes an unknown name legal lands with the first game effect.
        auto eff = kg.getProperty(id, "effect");
        const std::string eff_norm = to_upper(eff);
        if (eff_norm == "SWAP_PROFILE") r.effect = Effect::SWAP_PROFILE;
        else if (eff_norm == "FADE_OUT") r.effect = Effect::FADE_OUT;
        else if (eff_norm == "DELETE") r.effect = Effect::DELETE_PARTICLE;
        else if (eff_norm == "EMIT_EVENT") r.effect = Effect::EMIT_EVENT;
        else {
            std::fprintf(stderr,
                         "[INTERACTION] rule %u: unknown effect '%s' — skipped\n",
                         id, eff.c_str());
            continue;
        }

        // The predicate. Unlike trigger and effect this is NOT validated
        // here: it is open by design, its evaluator is chosen when the
        // event fires, and an unknown condition name is reported there
        // where the context to describe it exists. Empty = unconditional.
        r.condition = kg.getProperty(id, "condition");

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

    // Refresh the hot-path gate once, here, rather than scanning rules
    // every frame in process_contacts.
    has_contact_rules_ = false;
    for (const auto& [id, r] : rules_) {
        if (r.trigger == Trigger::ON_CONTACT) { has_contact_rules_ = true; break; }
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
