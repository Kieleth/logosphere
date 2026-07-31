// CapabilityProfile: Entity-agnostic capability profile derived from KG body graph.
//
// Capabilities, not body parts. A humanoid with 2 legs and a serpent with 20
// body segments both produce a locomotion_factor. A one-armed humanoid has
// manipulation_factor = 0.5. Missing parts contribute zero, no special cases.
//
// Two construction paths:
//   compute()          - flat inputs (backward compat, tests)
//   compute_from_kg()  - queries KG body graph for component state
//
// This struct contains only engine-level concepts: capability factors, speed cap,
// response rules, cascade. It has no awareness of game-specific dynamics parameters.
// Games derive their dynamics from these factors via DynamicsParams::from_capability()
// or their own derivation.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kg { class KGModule; using EntityID = uint32_t; }

struct CapabilityProfile {
    // === Physical inputs (geometry, not game concepts) ===
    float reflexes_ms = 0;
    float grit_W = 0;
    float mass = 0;
    float leg_length = 0;
    float total_height = 0;

    // === Capability Factors (0-1, derived from body graph) ===
    // Entity-agnostic. A serpent, spider, or humanoid all produce these scores.
    // Missing parts contribute 0.
    float locomotion_factor = 0;
    float manipulation_factor = 0;
    float rotation_factor = 0;
    float perception_factor = 0;

    // === Per-side animation hints (optional, for asymmetric animation) ===
    float left_arm_factor = 0;
    float right_arm_factor = 0;
    float left_leg_factor = 0;
    float right_leg_factor = 0;

    // === Generic Capability Map (KG-driven, source of truth) ===
    std::unordered_map<std::string, float> capabilities;

    // Per-side hints for asymmetric animation (e.g., "locomotion.left" = 0.0)
    std::unordered_map<std::string, float> side_hints;

    // Speed cap from response rules (minimum of all active caps, 1.0 = no cap)
    float speed_cap = 1.0f;

    // Flat construction (tests, backward compat). All capabilities default to 1.0.
    static CapabilityProfile compute(float reflexes_ms, float grit_W,
                                     float mass, float leg_length, float total_height);

    // KG construction: queries body graph, categorizes parts into capabilities.
    static CapabilityProfile compute_from_kg(kg::KGModule& kg, kg::EntityID entity_id,
                                             float mass, float leg_length, float total_height);
};
