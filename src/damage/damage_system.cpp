#include "logosphere/damage/damage_system.h"
#include "logosphere/core/kg_parse.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include <iostream>
#include <algorithm>

// apply_to_particle (particle-id → entity resolution via spatial query)
// lives in src/damage/damage_particle_bridge.cpp to keep this TU free
// of ParticleSystem / entity_spatial includes.

// Map engine DamageType to ontology DamageType for event emission
static logosphere::ontology::DamageType to_ontology_type(DamageType type) {
    switch (type) {
        case DamageType::Blunt:  return logosphere::ontology::DamageType::BLUNT;
        case DamageType::Slash:  return logosphere::ontology::DamageType::SLASH;
        case DamageType::Pierce: return logosphere::ontology::DamageType::PIERCE;
        case DamageType::Bite:   return logosphere::ontology::DamageType::BITE;
        case DamageType::Fire:   return logosphere::ontology::DamageType::FIRE;
        case DamageType::Cold:   return logosphere::ontology::DamageType::COLD;
        case DamageType::Poison: return logosphere::ontology::DamageType::POISON;
        case DamageType::Pure:   return logosphere::ontology::DamageType::PURE;
    }
    return logosphere::ontology::DamageType::PURE;
}

DamageSystem::DamageSystem(spatial::EntitySpatialQuery* spatial,
                           logosphere::EventBus* event_bus)
    : spatial_(spatial), event_bus_(event_bus) {}

void DamageSystem::register_entity(kg::EntityID entity, float max_hp) {
    EntityHealth h;
    h.hp = max_hp;
    h.max_hp = max_hp;
    h.dead = false;
    entities_[entity] = h;
}

void DamageSystem::unregister_entity(kg::EntityID entity) {
    entities_.erase(entity);
}

bool DamageSystem::has_entity(kg::EntityID entity) const {
    return entities_.find(entity) != entities_.end();
}

float DamageSystem::get_hp(kg::EntityID entity) const {
    auto it = entities_.find(entity);
    return it != entities_.end() ? it->second.hp : 0.0f;
}

float DamageSystem::get_max_hp(kg::EntityID entity) const {
    auto it = entities_.find(entity);
    return it != entities_.end() ? it->second.max_hp : 0.0f;
}

bool DamageSystem::is_dead(kg::EntityID entity) const {
    auto it = entities_.find(entity);
    return it != entities_.end() ? it->second.dead : true;
}

float DamageSystem::apply(kg::EntityID entity, float damage, DamageType type) {
    auto it = entities_.find(entity);
    if (it == entities_.end()) return 0.0f;

    auto& h = it->second;
    if (h.dead) return 0.0f;

    // Apply resistance
    float final_damage = apply_resistance(h, damage, type);

    h.hp -= final_damage;
    if (h.hp < 0.0f) h.hp = 0.0f;

    // Notify damage
    if (on_damage) {
        on_damage(entity, final_damage, h.hp, type);
    }

    // Emit damage event to bus
    if (event_bus_) {
        logosphere::ontology::DamageEvent evt;
        evt.target_entity_id = std::to_string(entity);
        evt.damage_type = to_ontology_type(type);
        evt.damage_amount = final_damage;
        event_bus_->damage().emit(std::move(evt));
    }

    // Death check
    if (h.hp <= 0.0f && !h.dead) {
        h.dead = true;
        if (on_death) {
            on_death(entity, type);
        }
        // Emit death event to bus
        if (event_bus_) {
            logosphere::ontology::DeathEvent evt;
            evt.target_entity_id = std::to_string(entity);
            event_bus_->deaths().emit(std::move(evt));
        }
    }

    return h.hp;
}

float DamageSystem::heal(kg::EntityID entity, float amount) {
    auto it = entities_.find(entity);
    if (it == entities_.end()) return 0.0f;

    auto& h = it->second;
    if (h.dead) return 0.0f;

    h.hp += amount;
    if (h.hp > h.max_hp) h.hp = h.max_hp;

    return h.hp;
}

void DamageSystem::set_resistance(kg::EntityID entity, DamageType type, float multiplier) {
    auto it = entities_.find(entity);
    if (it == entities_.end()) return;

    auto& h = it->second;
    switch (type) {
        case DamageType::Blunt:  h.blunt_resistance = multiplier; break;
        case DamageType::Slash:  h.slash_resistance = multiplier; break;
        case DamageType::Pierce: h.pierce_resistance = multiplier; break;
        case DamageType::Bite:   h.pierce_resistance = multiplier; break;  // Bite uses pierce
        case DamageType::Fire:   h.fire_resistance = multiplier; break;
        default: break;
    }
}

float DamageSystem::get_resistance(kg::EntityID entity, DamageType type) const {
    auto it = entities_.find(entity);
    if (it == entities_.end()) return 1.0f;

    const auto& h = it->second;
    switch (type) {
        case DamageType::Blunt:  return h.blunt_resistance;
        case DamageType::Slash:  return h.slash_resistance;
        case DamageType::Pierce: return h.pierce_resistance;
        case DamageType::Bite:   return h.pierce_resistance;
        case DamageType::Fire:   return h.fire_resistance;
        default: return 1.0f;
    }
}

float DamageSystem::apply_resistance(const EntityHealth& health, float damage, DamageType type) const {
    float resistance = 1.0f;
    switch (type) {
        case DamageType::Blunt:  resistance = health.blunt_resistance; break;
        case DamageType::Slash:  resistance = health.slash_resistance; break;
        case DamageType::Pierce: resistance = health.pierce_resistance; break;
        case DamageType::Bite:   resistance = health.pierce_resistance; break;
        case DamageType::Fire:   resistance = health.fire_resistance; break;
        case DamageType::Pure:   resistance = 1.0f; break;  // Pure ignores resistance
        default: break;
    }
    return damage * resistance;
}

float DamageSystem::apply_to_body_part(kg::EntityID entity, const std::string& body_part_name,
                                        float damage, DamageType type) {
    if (!kg_) {
        std::cerr << "[DamageSystem] No KG set, can't apply body part damage" << std::endl;
        return -1.0f;
    }

    // Apply entity-level damage (existing behavior)
    apply(entity, damage, type);

    // Find the body part entity via HAS_PART relations
    auto parts = kg_->getRelated(entity, "HAS_PART");
    for (auto part_id : parts) {
        std::string name = kg_->getProperty(part_id, "body_part_name");
        if (name == body_part_name) {
            // Read current health
            auto h_str = kg_->getProperty(part_id, "health");
            auto mh_str = kg_->getProperty(part_id, "max_health");
            if (h_str.empty() || mh_str.empty()) return -1.0f;

            auto h = kg_parse::to_float(h_str, "health", part_id);
            auto mh = kg_parse::to_float(mh_str, "max_health", part_id);
            if (!h || !mh) return -1.0f;  // malformed health values, treat as absent
            float health = *h;
            float max_health = *mh;

            // Apply resistance and reduce
            auto it = entities_.find(entity);
            float final_damage = it != entities_.end()
                ? apply_resistance(it->second, damage, type)
                : damage;

            health = std::max(0.0f, health - final_damage);

            // Write back to KG (triggers STATE_CHANGE event)
            kg_->setProperty(part_id, "health", std::to_string(health));

            return health;
        }
    }

    std::cerr << "[DamageSystem] Body part '" << body_part_name
              << "' not found on entity " << entity << std::endl;
    return -1.0f;
}
