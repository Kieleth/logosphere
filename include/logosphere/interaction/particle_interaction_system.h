#pragma once

// =============================================================================
// ParticleInteractionSystem — KG-declared particle interaction policy
// =============================================================================
// Games declare ParticleInteractionProfile entities in the KG; particles
// carry `interaction_profile_id` (the profile's KG EntityID, 0 = default).
// This system resolves the policy the physics broad phase consults:
//
//   contact(a, b)  <=>  (cat_a & mask_b) && (cat_b & mask_a)
//
// (Box2D-style symmetric masking.) The default profile — id 0, unknown
// ids, or any particle that never opted in — has category bit 0 and a
// full mask: it rigid-contacts everything willing, so existing worlds
// are byte-identical until a game registers profiles. A filtered pair
// is NOT decoration: Phase 3 applies the declared medium forces
// (drag/buoyancy/field) to filtered overlaps, keeping "particles are
// bodies" intact — passable media interact through forces instead of
// contact rows.
//
// Headless-core safe: no physics/render includes. The solver seam
// (Phase 2) calls should_contact from the contact broad phase; this
// header stays ignorant of the solver.
//
// Plan: the particle-interaction design notes
// =============================================================================

#include "core/particle_system.h"  // WriteView for the force pass
#include "logosphere/kg/kg_types.h"  // KGParticleID (stable effect identity)

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kg { class KGModule; }
namespace logosphere { class EventBus; }

namespace logosphere::interaction {

// One broad-phase overlap the solver skipped because the pair's
// profiles decline rigid contact. Particle indices are RAW render
// indices, valid only within the frame they were recorded (consumed
// same-frame by process_filtered_overlaps and the Phase-3 force pass,
// before any deferred deletion flush can swap them).
struct FilteredOverlap {
    uint32_t particle_a = 0;
    uint32_t particle_b = 0;
    uint32_t profile_a = 0;
    uint32_t profile_b = 0;
};

struct InteractionProfile {
    uint32_t id = 0;                       // KG EntityID of the profile
    uint32_t category = 1u << 0;           // one-hot category bit
    uint32_t collides_with = 0xFFFFFFFFu;  // mask of categories to rigid-contact
    // Medium parameters (Phase 3): applied to particles overlapping a
    // filtered pair when this profile declares a medium.
    float drag_coefficient = 0.0f;   // 0 = no drag
    float buoyancy_factor = 0.0f;    // 0 = none, 1 = neutral buoyancy
    float field_fx = 0.0f;           // directional field force, Newtons
    float field_fy = 0.0f;
    float field_fz = 0.0f;

    bool declares_medium() const {
        return drag_coefficient != 0.0f || buoyancy_factor != 0.0f ||
               field_fx != 0.0f || field_fy != 0.0f || field_fz != 0.0f;
    }
};

// A declarative EVENT rule: an occurrence fires an effect on the
// affected particle. Declared as TransformationRule KG entities and
// loaded like profiles; the game-facing on_timer trigger is armed
// explicitly via arm_transformation.
//
// Three separable questions, one field each (see the TransformationRule
// class in schema/logosphere.yaml):
//   trigger    WHICH engine event source. Closed: every value is a
//              queue this system owns, and a game cannot add one
//              without engine code.
//   condition  WHETHER a given occurrence matters. Open: evaluated
//              against the event after the trigger has selected it.
//   effect     WHAT to do. Open.
//
// These rules are PUSHED: they consume a queue of things that happened,
// fire once, and the queue is cleared. They are NOT capability response
// rules, which are state predicates PULLED over the KG and safe to
// re-evaluate (compute_from_kg does, twice per call). Reaching for the
// wrong one of the two is the confusion issue #36 exists to end.
struct TransformationRule {
    enum class Trigger {
        ON_CONTACT,           // rigid contact was exchanged
        ON_CONTACT_FILTERED,  // the pair declined rigid contact
        ON_VOLUME_ENTER,
        ON_TIMER
    };
    enum class Effect { SWAP_PROFILE, FADE_OUT, DELETE_PARTICLE, EMIT_EVENT };

    uint32_t id = 0;              // KG EntityID of the rule
    std::string name;             // KG "name" property, else id as string
    Trigger trigger = Trigger::ON_TIMER;
    Effect effect = Effect::EMIT_EVENT;
    // Predicate over the occurrence: "<name>" or "<name>:<args>". Empty
    // means unconditional. Generalizes trigger_profile, which is the
    // same idea hardcoded to a single comparison.
    std::string condition;
    uint32_t target_profile = 0;  // swap_profile: profile id to write
    float duration_s = 0.0f;      // fade_out ramp length / timer deadline
    uint32_t trigger_profile = 0; // volume/contact binding (0 = any)
};

class ParticleInteractionSystem {
public:
    // Register (or replace) a profile. id 0 is reserved for the engine
    // default and cannot be overridden.
    void register_profile(const InteractionProfile& profile);

    // Load every ParticleInteractionProfile entity from the KG.
    // Missing/malformed slots parse through kg_parse (diagnostic
    // warning + per-field default), matching the engine's KG parse
    // discipline. Returns the number of profiles loaded.
    size_t load_profiles_from_kg(const kg::KGModule& kg);

    // The broad-phase policy. Unknown ids (including 0) resolve to the
    // default profile. Two map lookups + mask math; hot-path cheap.
    bool should_contact(uint32_t profile_a, uint32_t profile_b) const;

    // Profile lookup for the force/transformation passes. nullptr for
    // the default profile or unknown ids (they declare no medium).
    const InteractionProfile* find_profile(uint32_t id) const;

    // Episode bookkeeping: consume this frame's filtered overlaps (the
    // solver records them, deduped here across substeps) and emit ONE
    // ContactFilteredEvent per pair EPISODE — the frame a pair first
    // filters, not every frame/substep it stays overlapped. Episode
    // identity is the canonical particle-index pair; a swap-and-pop
    // remap mid-episode can re-open an episode (duplicate event, no
    // force-pass impact) — acceptable throttling heuristic, documented.
    // bus may be null (no events, bookkeeping still runs).
    void process_filtered_overlaps(const std::vector<FilteredOverlap>& overlaps,
                                   logosphere::EventBus* bus);

    // Volume forces (Phase 3): for each filtered overlap whose profile
    // declares a medium, apply the medium's forces to the OTHER
    // particle as a velocity increment for the next frame — the
    // standard force model, applied at a defined frame point (after
    // physics), positions never touched:
    //   drag      dv -= (c/m) · (v − v_medium) · dt
    //   buoyancy  dv -= g · B · dt          (gross; solver applies g,
    //                                         so net lift is (B−1)·g)
    //   field     dv += F/m · dt
    // An active medium also keeps the intruder awake (a sleeping
    // particle would stop being detected by the broad phase and freeze
    // mid-medium). (gx,gy,gz) is the solver's gravity vector — no
    // world-up assumption. Pairs are deduped across substeps; forces
    // apply once per frame per pair.
    //
    // v1 scope: solver-DYNAMIC intruders. Locomotion-owned bodies
    // (humanoids) have their velocities re-written by their own system
    // each frame; humanoid-medium coupling belongs to that layer.
    void apply_volume_forces(ParticleSystem::WriteView& particles,
                             const std::vector<FilteredOverlap>& overlaps,
                             float gx, float gy, float gz, float dt) const;

    // Load every TransformationRule entity from the KG (same discipline
    // as load_profiles_from_kg: kg_parse diagnostics, malformed
    // trigger/effect strings warn and skip the rule). Returns the
    // number of rules loaded.
    size_t load_rules_from_kg(const kg::KGModule& kg);

    // Arm an on_timer rule on specific particles. The game hands over
    // stable KGParticleIDs (raw render indices go stale across
    // swap-and-pop deletions); unresolvable ids are skipped. Effects
    // fire when the rule's duration_s elapses; fade_out ramps alpha
    // continuously until then.
    void arm_transformation(uint32_t rule_id,
                            const std::vector<kg::KGParticleID>& particles);

    // Execute transformations for this frame (engine calls it right
    // after process_filtered_overlaps):
    //   1. auto-arm/fire rules triggered by this frame's episode opens
    //      (recorded by process_filtered_overlaps),
    //   2. tick armed entries: resolve each KGParticleID through the
    //      KG; INVALID render indices drop the entry (the particle
    //      died — fail-safe by construction).
    // Particles to delete are appended to out_delete; the ENGINE queues
    // them through the deferred-deletion machinery (this system stays
    // render-free and never deletes directly). Rules only track
    // KG-backed particles — a particle outside the KG is outside the
    // ontology and cannot carry a transformation.
    void tick_transformations(ParticleSystem::WriteView& particles,
                              const kg::KGModule& kg, logosphere::EventBus* bus,
                              float dt, std::vector<uint32_t>& out_delete);

    size_t profile_count() const { return profiles_.size(); }
    size_t rule_count() const { return rules_.size(); }
    void clear() {
        profiles_.clear();
        open_episodes_.clear();
        rules_.clear();
        armed_.clear();
        episode_opens_.clear();
    }

private:
    static const InteractionProfile& default_profile();
    const InteractionProfile& resolve(uint32_t id) const;

    std::unordered_map<uint32_t, InteractionProfile> profiles_;
    // Open pass-through episodes: canonical pair key -> medium profile
    // id (0 = neither side declares a medium). The medium id is kept so
    // episode close can emit the matching VolumeEvent{entered=false}.
    std::unordered_map<uint64_t, uint32_t> open_episodes_;

    // --- Transformations (Phase 4) ---
    // One episode OPEN this frame: which particle entered against
    // which profile. Recorded by process_filtered_overlaps, consumed
    // by tick_transformations the same frame (render indices valid).
    struct EpisodeOpen {
        uint32_t intruder_idx = 0;   // render index of the entering side
        uint32_t other_profile = 0;  // profile on the opposite side
        bool is_medium = false;      // opposite side declares a medium
    };
    // A long-running effect bound to a particle by stable identity.
    struct ArmedTransformation {
        kg::KGParticleID kgid = 0;
        uint32_t rule_id = 0;
        float elapsed = 0.0f;
        bool  sampled = false;           // initial visual state captured?
        float initial_a = 1.0f;
        float initial_emission = 0.0f;
        float initial_r = 1.0f, initial_g = 1.0f, initial_b = 1.0f;
    };

    std::unordered_map<uint32_t, TransformationRule> rules_;
    std::vector<ArmedTransformation> armed_;
    std::vector<EpisodeOpen> episode_opens_;
};

} // namespace logosphere::interaction
