// Universal Damage System
//
// Engine-level HP tracking and damage routing.
// Any game can use this for entities that take damage.
//
// Features:
// - Entity HP tracking
// - Standard damage types (physical, elemental, special)
// - Resistance modifiers per type
// - Death callbacks
// - Particle → entity resolution
//
// Usage:
//   DamageSystem damage(&spatial_query);
//   damage.register_entity(entity_id, 100.0f);
//   damage.on_death = [&](EntityID e, DamageType t) { /* game handles death */ };
//   damage.apply(entity_id, 25.0f, DamageType::Bite);

#ifndef LOGOSPHERE_DAMAGE_SYSTEM_H
#define LOGOSPHERE_DAMAGE_SYSTEM_H

#include <string>
#include <functional>
#include <unordered_map>
#include "logosphere/kg/kg_types.h"

// Forward declarations
namespace spatial { class EntitySpatialQuery; }
namespace logosphere { class EventBus; }
namespace kg { class KGModule; }

// ============================================================================
// Standard Damage Types
// ============================================================================
enum class DamageType {
    // Physical
    Blunt,      // Fists, clubs, falling - vs toughness
    Slash,      // Blades, claws - vs cut resistance
    Pierce,     // Arrows, fangs, stabs
    Bite,       // Creature bites - pierce + blunt

    // Elemental
    Fire,
    Cold,
    Poison,

    // Special
    Pure        // Ignores resistances
};

inline std::string damage_type_to_string(DamageType type) {
    switch (type) {
        case DamageType::Blunt:  return "blunt";
        case DamageType::Slash:  return "slash";
        case DamageType::Pierce: return "pierce";
        case DamageType::Bite:   return "bite";
        case DamageType::Fire:   return "fire";
        case DamageType::Cold:   return "cold";
        case DamageType::Poison: return "poison";
        case DamageType::Pure:   return "pure";
    }
    return "unknown";
}

// ============================================================================
// Entity Health State
// ============================================================================
struct EntityHealth {
    float hp = 100.0f;
    float max_hp = 100.0f;
    bool dead = false;

    // Resistance multipliers (1.0 = normal, 0.5 = 50% damage, 2.0 = 200% damage)
    float blunt_resistance = 1.0f;
    float slash_resistance = 1.0f;
    float pierce_resistance = 1.0f;
    float fire_resistance = 1.0f;
};

// ============================================================================
// Damage System
// ============================================================================
class DamageSystem {
public:
    explicit DamageSystem(spatial::EntitySpatialQuery* spatial = nullptr,
                          logosphere::EventBus* event_bus = nullptr);

    void set_spatial_query(spatial::EntitySpatialQuery* spatial) { spatial_ = spatial; }
    void set_event_bus(logosphere::EventBus* bus) { event_bus_ = bus; }
    void set_kg(kg::KGModule* kg) { kg_ = kg; }

    // === Registration ===
    void register_entity(kg::EntityID entity, float max_hp);
    void unregister_entity(kg::EntityID entity);
    bool has_entity(kg::EntityID entity) const;

    // === Queries ===
    float get_hp(kg::EntityID entity) const;
    float get_max_hp(kg::EntityID entity) const;
    bool is_dead(kg::EntityID entity) const;

    // === Damage ===
    // Returns remaining HP (0 if dead/not registered)
    float apply(kg::EntityID entity, float damage, DamageType type = DamageType::Pure);

    // Resolve particle → entity, then apply damage
    // Returns -1 if particle has no owner
    float apply_to_particle(int particle_id, float damage, DamageType type = DamageType::Pure);

    // Apply damage to a specific body part (updates KG health property)
    // Finds the body part entity via HAS_PART + body_part_name match.
    // Also applies entity-level HP damage. Returns remaining body part health, -1 if not found.
    float apply_to_body_part(kg::EntityID entity, const std::string& body_part_name,
                             float damage, DamageType type = DamageType::Pure);

    // === Healing ===
    float heal(kg::EntityID entity, float amount);

    // === Resistances ===
    void set_resistance(kg::EntityID entity, DamageType type, float multiplier);
    float get_resistance(kg::EntityID entity, DamageType type) const;

    // === Callbacks ===
    std::function<void(kg::EntityID, DamageType)> on_death;
    std::function<void(kg::EntityID, float damage, float remaining_hp, DamageType)> on_damage;

private:
    spatial::EntitySpatialQuery* spatial_ = nullptr;
    logosphere::EventBus* event_bus_ = nullptr;
    kg::KGModule* kg_ = nullptr;
    std::unordered_map<kg::EntityID, EntityHealth> entities_;

    float apply_resistance(const EntityHealth& health, float damage, DamageType type) const;
};

#endif  // LOGOSPHERE_DAMAGE_SYSTEM_H
