// Body Plan Templates: Convenience setters for common body plans.
//
// Each function declares capability structure on a root entity:
// expected counts, aggregation modes, and optionally creates body
// part entities with default capability bindings.
//
// These are thin wrappers around setProperty calls. Games can call
// them directly or build custom body plans with raw KG properties.
//
// Usage:
//   body_plan::declare_biped(kg, humanoid_entity);
//   body_plan::declare_serpent(kg, snake_entity, 20);
//   body_plan::declare_winged(kg, butterfly_entity, 2);

#pragma once

#include "logosphere/kg/kg_types.h"
#include <string>

namespace kg { class KGModule; }

namespace body_plan {

// Declare biped body plan on root entity.
// Sets: locomotion (average, 2 expected), manipulation (average, 2),
//       rotation (minimum, 1), perception (minimum, 1)
void declare_biped(kg::KGModule& kg, kg::EntityID entity);

// Declare serpent body plan on root entity.
// Sets: undulation (weighted_sum, N segments), perception (minimum, 1)
void declare_serpent(kg::KGModule& kg, kg::EntityID entity, int segment_count);

// Declare winged body plan on root entity.
// Sets: flight (binary, wing_pairs*2+1 for thorax), perception (minimum, 1)
void declare_winged(kg::KGModule& kg, kg::EntityID entity, int wing_pairs);

// Declare quadruped body plan on root entity.
// Sets: locomotion (average, 4 expected), rotation (minimum, 1), perception (minimum, 1)
void declare_quadruped(kg::KGModule& kg, kg::EntityID entity);

// Helper: create a body part entity with capability binding and link via HAS_PART.
// Returns the created body part entity ID.
kg::EntityID create_capability_part(
    kg::KGModule& kg,
    kg::EntityID parent,
    const std::string& part_type,      // ontology type: "Leg", "Arm", "Segment", "Wing"
    const std::string& part_name,      // e.g. "left_leg", "segment_3"
    float max_hp,
    const std::string& capability,     // e.g. "locomotion", "flight"
    float weight,
    const std::string& side = ""       // optional: "left", "right"
);

} // namespace body_plan
