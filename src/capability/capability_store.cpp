#include "logosphere/capability/capability_store.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"

#include <cstdio>
#include <cstdlib>

namespace capability {

void CapabilityStore::initialize(kg::KGModule* kg, logosphere::EventBus* bus) {
    kg_ = kg;
    bus_ = bus;
    if (!bus_) return;
    bus_->state_changes().subscribe(
        [this](const logosphere::ontology::WorldEvent& e) {
            // Guard layer 2: anything emitted while the store itself is
            // writing capability.* is the store's own echo.
            if (writing_) return;
            if (!e.target_entity_id.has_value()) return;
            // Payload is {property, value, prev}; the property name is
            // what the key-prefix guard needs.
            std::string prop;
            for (size_t i = 0; i < e.payload_keys.size() &&
                               i < e.payload_values.size(); ++i) {
                if (e.payload_keys[i] == "property") {
                    prop = e.payload_values[i];
                    break;
                }
            }
            const char* s = e.target_entity_id->c_str();
            char* end = nullptr;
            const unsigned long id = std::strtoul(s, &end, 10);
            if (end == s || *end != '\0') return;   // not a numeric id
            on_state_change(static_cast<kg::EntityID>(id), prop);
        });
}

void CapabilityStore::track(kg::EntityID entity, const Physical& phys) {
    if (!kg_ || entity == kg::INVALID_ENTITY) return;
    Entry& e = tracked_[entity];
    e.phys = phys;
    e.recomputes = 0;
    recompute(entity, e);
}

void CapabilityStore::untrack(kg::EntityID entity) { tracked_.erase(entity); }

bool CapabilityStore::is_tracked(kg::EntityID entity) const {
    return tracked_.count(entity) > 0;
}

const CapabilityProfile* CapabilityStore::get(kg::EntityID entity) const {
    auto it = tracked_.find(entity);
    return it != tracked_.end() ? &it->second.profile : nullptr;
}

int CapabilityStore::recompute_count(kg::EntityID entity) const {
    auto it = tracked_.find(entity);
    return it != tracked_.end() ? it->second.recomputes : 0;
}

kg::EntityID CapabilityStore::tracked_root_of(kg::EntityID id) const {
    // Walk up reverse HAS_PART: a part's write must recompute its
    // CREATURE. Depth-limited: body graphs here are shallow, and a
    // cycle in HAS_PART would otherwise hang the bus.
    kg::EntityID cur = id;
    for (int hop = 0; hop < 4; ++hop) {
        if (tracked_.count(cur)) return cur;
        auto owners = kg_->getRelatedReverse(cur, "HAS_PART");
        if (owners.empty()) return kg::INVALID_ENTITY;
        cur = owners.front();
    }
    return tracked_.count(cur) ? cur : kg::INVALID_ENTITY;
}

void CapabilityStore::on_state_change(kg::EntityID changed,
                                      const std::string& property) {
    if (!kg_ || tracked_.empty()) return;
    // Guard layer 1: our own vocabulary. A capability.* write is
    // derived state, never an input to it.
    if (property.rfind("capability.", 0) == 0) return;

    const kg::EntityID root = tracked_root_of(changed);
    if (root == kg::INVALID_ENTITY) return;
    auto it = tracked_.find(root);
    if (it == tracked_.end()) return;
    recompute(root, it->second);
}

void CapabilityStore::recompute(kg::EntityID entity, Entry& e) {
    // Rules fire here, eagerly: an emit_event effect belongs to the
    // moment the state changed.
    e.profile = CapabilityProfile::compute_from_kg(
        *kg_, entity, e.phys.mass, e.phys.leg_length, e.phys.total_height);
    ++e.recomputes;
    write_back(entity, e.profile);
}

void CapabilityStore::write_back(kg::EntityID entity,
                                 const CapabilityProfile& p) {
    if (!kg_) return;
    writing_ = true;
    kg_->setProperty(entity, "capability.speed_cap",
                     std::to_string(p.speed_cap));
    kg_->setProperty(entity, "capability.locomotion",
                     std::to_string(p.locomotion_factor));
    kg_->setProperty(entity, "capability.manipulation",
                     std::to_string(p.manipulation_factor));
    kg_->setProperty(entity, "capability.rotation",
                     std::to_string(p.rotation_factor));
    kg_->setProperty(entity, "capability.perception",
                     std::to_string(p.perception_factor));
    // The generic map is the declared source of truth; every entry
    // lands under the same prefix the guards ignore.
    for (const auto& [name, value] : p.capabilities) {
        kg_->setProperty(entity, "capability." + name, std::to_string(value));
    }
    writing_ = false;
}

} // namespace capability
