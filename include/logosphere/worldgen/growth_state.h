#ifndef GROWTH_STATE_H
#define GROWTH_STATE_H

#include "logosphere/worldgen/tree_skeleton.h"
#include "logosphere/worldgen/space_colonization.h"
#include "logosphere/kg/kg_types.h"
#include "core/game_time.h"
#include <vector>
#include <string>

// OrganicType - Classification of organic entities
enum class OrganicType {
    TREE,
    BUSH,
    GRASS,
    VINE,
    ROOT,
    FLOWER,
    FERN
};

// GrowthCheckpoint - Snapshot of growth state for time travel support
//
// Purpose: Enable jumping backwards in time by storing periodic snapshots
// Memory cost: ~16KB per checkpoint, ~160KB per tree for 10 checkpoints
struct GrowthCheckpoint {
    double time;                    // Game time when checkpoint created
    int iteration;                  // Growth iteration count at checkpoint
    TreeSkeleton skeleton;          // Full skeleton state (branch segments)
    std::vector<Vec3> attractors;   // Remaining attractors for Space Colonization

    GrowthCheckpoint() : time(0.0), iteration(0) {}
};

// EntityModification - Tracks damage, pruning, or destruction events
//
// Purpose: Support entity modifications in gameplay (cutting trees, pruning branches)
// Used with timeline branching for time travel paradoxes
struct EntityModification {
    enum Type { NONE, DESTROYED, DAMAGED, PRUNED, FROZEN };

    Type type = NONE;
    double modification_time = 0.0;

    // For DAMAGED/PRUNED: which parts affected
    std::vector<int> removed_segments;  // Skeleton segment indices
    std::vector<Vec3> damage_points;    // Locations of damage

    // For PRUNED: re-simulation control
    bool allow_regrowth = false;  // Can tree regrow from remaining branches?
};

// GrowthState - Per-entity growth data (stored in KG)
//
// Design: Complete state needed to resume growth simulation from any point in time
// Storage: Serialized to KG as entity data (`kg_->setData(entity_id, "growth_state", ...)`)
//
// Lifecycle:
// 1. Entity created → Initialize growth state with seed attractors
// 2. Chunk loads → Simulate growth to current time
// 3. Chunk unloads → Save state to KG, destroy render particles
// 4. Time passes while unloaded → State persists in KG
// 5. Chunk reloads → Resume simulation from stored state
struct GrowthState {
    // === Identity ===
    kg::EntityID entity_id = kg::INVALID_ENTITY;
    OrganicType organic_type = OrganicType::TREE;
    std::string species;  // "oak", "pine", "bush", "grass", etc.

    // === Timeline Support (for multiverse/branching) ===
    timeline_id timeline = 0;  // Which timeline this state belongs to

    // === Temporal Tracking ===
    double time_created = 0.0;           // Game time when entity spawned
    double time_last_simulated = 0.0;    // Last time growth was simulated
    int current_iteration = 0;           // Current growth iteration count
    bool is_mature = false;              // Reached max_iterations?

    // === Simulation State ===
    TreeSkeleton current_skeleton;       // Current branch structure
    std::vector<Vec3> remaining_attractors;  // Attractors not yet reached

    // === Growth Parameters (species-specific) ===
    float growth_rate_iterations_per_year = 15.0f;  // How fast it grows
    int max_iterations = 300;                         // Maturity cap
    float years_to_maturity = 20.0f;                 // Real game time to full size

    // === Checkpoint History (for time travel) ===
    std::vector<GrowthCheckpoint> checkpoints;

    // === Entity Modifications (damage, pruning, destruction) ===
    bool is_destroyed = false;
    double destruction_time = 0.0;
    std::vector<EntityModification> modifications;

    // === Helper Methods ===

    // Get elapsed time since last simulation
    double elapsed_time(double current_time) const {
        return current_time - time_last_simulated;
    }

    // Calculate target iteration count for given time
    int target_iterations(double current_time) const {
        double elapsed_years = elapsed_time(current_time) / GameTime::SECONDS_PER_YEAR;
        int target = current_iteration + static_cast<int>(elapsed_years * growth_rate_iterations_per_year);
        return std::min(target, max_iterations);  // Cap at maturity
    }

    // Check if growth simulation needed
    bool needs_simulation(double current_time) const {
        if (is_destroyed || is_mature) {
            return false;  // No growth if destroyed or fully mature
        }
        return target_iterations(current_time) > current_iteration;
    }

    // Create checkpoint at current state
    void create_checkpoint() {
        GrowthCheckpoint checkpoint;
        checkpoint.time = time_last_simulated;
        checkpoint.iteration = current_iteration;
        checkpoint.skeleton = current_skeleton;
        checkpoint.attractors = remaining_attractors;
        checkpoints.push_back(checkpoint);
    }

    // Find closest checkpoint before target time
    const GrowthCheckpoint* find_checkpoint(double target_time) const {
        const GrowthCheckpoint* closest = nullptr;
        double closest_time = -1.0;

        for (const auto& cp : checkpoints) {
            if (cp.time <= target_time && cp.time > closest_time) {
                closest = &cp;
                closest_time = cp.time;
            }
        }

        return closest;
    }

    // Restore state from checkpoint
    void restore_checkpoint(const GrowthCheckpoint& checkpoint) {
        time_last_simulated = checkpoint.time;
        current_iteration = checkpoint.iteration;
        current_skeleton = checkpoint.skeleton;
        remaining_attractors = checkpoint.attractors;
        is_mature = (current_iteration >= max_iterations);
    }
};

// Helper: Convert OrganicType to string for debugging/serialization
inline std::string organic_type_to_string(OrganicType type) {
    switch (type) {
        case OrganicType::TREE: return "tree";
        case OrganicType::BUSH: return "bush";
        case OrganicType::GRASS: return "grass";
        case OrganicType::VINE: return "vine";
        case OrganicType::ROOT: return "root";
        case OrganicType::FLOWER: return "flower";
        case OrganicType::FERN: return "fern";
        default: return "unknown";
    }
}

// Helper: Parse OrganicType from string
inline OrganicType string_to_organic_type(const std::string& str) {
    if (str == "tree") return OrganicType::TREE;
    if (str == "bush") return OrganicType::BUSH;
    if (str == "grass") return OrganicType::GRASS;
    if (str == "vine") return OrganicType::VINE;
    if (str == "root") return OrganicType::ROOT;
    if (str == "flower") return OrganicType::FLOWER;
    if (str == "fern") return OrganicType::FERN;
    return OrganicType::TREE;  // Default
}

#endif // GROWTH_STATE_H
