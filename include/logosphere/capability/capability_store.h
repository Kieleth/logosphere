// CapabilityStore: what each tracked entity can DO, kept current and
// written back into the KG.
//
// The gap this closes (#36 -> #37): capability response rules were
// humanoid-only in practice, because the only recompute path lived in
// HumanoidLocomotion's STATE_CHANGE subscription and the only consumer
// was its own walk cycle. A wounded non-humanoid creature had rules,
// health, and a damage system — and nothing that ever evaluated the
// rules or held the answer. The store is that missing piece: the bridge
// between DamageSystem and the NPC layer.
//
//   damage -> part health write -> STATE_CHANGE
//     -> store recomputes the entity's CapabilityProfile (rules fire
//        HERE, eagerly: emit_event effects are rule semantics and
//        belong to the moment the state changed, not to whoever
//        happens to query later)
//     -> profile cached for cheap get()
//     -> and WRITTEN BACK into the KG as capability.* properties, so
//        the inspector, a director, an LLM read what an entity can do
//        from the same medium as everything else it is.
//
// OPT-IN. Only tracked entities are evaluated. Humanoids keep their
// existing locomotion-owned recompute; tracking one here too would
// double-fire its rules' emit_event effects. (Migrating locomotion to
// consume this store is future work in the animation area.)
//
// LOOP GUARD, two layers. The write-back itself emits STATE_CHANGE, so
// a recompute inside a recompute is one property write away: (1) the
// store ignores events for property keys under "capability."; (2) a
// reentrancy flag drops anything emitted while the store itself is
// writing. recompute_count() exists so a test can PROVE the guard
// holds rather than trust it.
//
// Headless-core safe: KG + events + capability only.

#pragma once

#include "logosphere/capability/capability_profile.h"
#include "logosphere/kg/kg_types.h"

#include <string>
#include <unordered_map>

namespace kg { class KGModule; }
namespace logosphere { class EventBus; }

namespace capability {

class CapabilityStore {
public:
    // Physical inputs compute_from_kg needs and cannot derive from the
    // body graph. Defaults are a plausible mid-size creature; a game
    // that cares passes real numbers.
    struct Physical {
        float mass;
        float leg_length;
        float total_height;
        // Plausible mid-size creature; a game that cares passes real
        // numbers. (Explicit ctor, not member initializers: a defaulted
        // `const Physical& = {}` argument needs the complete default
        // inside the class definition, which clang rejects with NSDMIs.)
        Physical(float m = 60.0f, float leg = 0.8f, float height = 1.6f)
            : mass(m), leg_length(leg), total_height(height) {}
    };

    // Wire to the KG and the bus. Subscribes to state_changes; call
    // once, before track().
    void initialize(kg::KGModule* kg, logosphere::EventBus* bus);

    // Start tracking an entity: computes immediately (rules fire),
    // caches, writes capability.* back to the KG.
    void track(kg::EntityID entity, const Physical& phys = {});
    void untrack(kg::EntityID entity);
    bool is_tracked(kg::EntityID entity) const;
    size_t tracked_count() const { return tracked_.size(); }

    // The current profile, or nullptr for an untracked entity. Cheap:
    // a map lookup, never a recompute (eager model).
    const CapabilityProfile* get(kg::EntityID entity) const;

    // How many times this entity's profile has been recomputed since
    // track(). The loop-guard proof: after N damage writes this is
    // N + 1 (the initial compute), not unbounded.
    int recompute_count(kg::EntityID entity) const;

private:
    struct Entry {
        Physical phys;
        CapabilityProfile profile;
        int recomputes = 0;
    };

    void on_state_change(kg::EntityID changed, const std::string& property);
    // The tracked root an entity belongs to, walking reverse HAS_PART
    // up to a small depth (a part of a part still reports its
    // creature). INVALID_ENTITY when it belongs to nobody tracked.
    kg::EntityID tracked_root_of(kg::EntityID id) const;
    void recompute(kg::EntityID entity, Entry& e);
    void write_back(kg::EntityID entity, const CapabilityProfile& p);

    kg::KGModule* kg_ = nullptr;
    logosphere::EventBus* bus_ = nullptr;
    std::unordered_map<kg::EntityID, Entry> tracked_;
    bool writing_ = false;   // reentrancy guard for write_back
};

} // namespace capability
