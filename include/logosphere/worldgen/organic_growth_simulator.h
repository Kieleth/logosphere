#ifndef ORGANIC_GROWTH_SIMULATOR_H
#define ORGANIC_GROWTH_SIMULATOR_H

#include "logosphere/worldgen/growth_state.h"
#include "logosphere/worldgen/organic_spec.h"
#include "logosphere/worldgen/space_colonization.h"
#include "core/game_time.h"

/**
 * OrganicGrowthSimulator - Re-simulation engine for temporal organic growth
 *
 * Purpose: Simulate organic entity growth over time, supporting time travel and checkpoints
 *
 * Core Principle: Growth is iterative simulation, NOT state jumping
 * - Can't skip from iteration 0 → 100 directly
 * - Must actually run iterations 0,1,2,...,99,100
 * - Each iteration affects the next (branches change attractor visibility)
 *
 * Design Pattern: Re-simulation from checkpoints
 * 1. Detect target time (current game time)
 * 2. Check if backwards time travel (target < last_simulated)
 *    - If yes: Find closest checkpoint, restore state
 * 3. Simulate iterations from checkpoint/current to target
 * 4. Create new checkpoint if needed
 * 5. Update time_last_simulated
 *
 * Usage:
 *   OrganicGrowthSimulator sim;
 *   GrowthState state = load_from_kg(entity_id);
 *   sim.simulate_to_current_time(state, spec);
 *   save_to_kg(entity_id, state);
 *
 * Checkpoint Strategy:
 * - Every 10 iterations (early growth)
 * - Every 50 iterations (mature growth)
 * - Always at chunk unload
 * - Always at year boundaries (for seasons)
 */
class OrganicGrowthSimulator {
public:
    /**
     * Simulate growth to current game time
     * Handles time travel, checkpoints, and iterative growth
     */
    void simulate_to_current_time(GrowthState& state, const OrganicSpec& spec);

    /**
     * Simulate growth to specific target time
     * Useful for testing or custom time scenarios
     */
    void simulate_to_time(GrowthState& state, const OrganicSpec& spec, double target_time);

    /**
     * Initialize growth state for new entity
     * Creates initial skeleton and attractors
     */
    void initialize_growth(GrowthState& state, const OrganicSpec& spec,
                          float world_x, float world_y, float world_z);

    /**
     * Create checkpoint at current state
     * Call before chunk unload to preserve state
     */
    void create_checkpoint(GrowthState& state);

    /**
     * Apply entity modification (damage, pruning, destruction)
     * May trigger timeline branching if modification conflicts with future
     */
    void apply_modification(GrowthState& state, const EntityModification& mod);

private:
    /**
     * Run growth iterations from current to target
     * Core simulation loop using Space Colonization
     */
    void run_iterations(GrowthState& state, const OrganicSpec& spec, int target_iteration);

    /**
     * Detect and handle backwards time travel
     * Finds checkpoint, restores state
     */
    void handle_time_travel(GrowthState& state, double target_time);

    /**
     * Should we create checkpoint at this iteration?
     * Adaptive strategy: dense early, sparse late
     */
    bool should_create_checkpoint(int iteration) const;

    /**
     * Prune old checkpoints to save memory
     * Keep first, last, and evenly spaced middle
     */
    void prune_checkpoints(GrowthState& state);

    // Space Colonization instance for iteration
    SpaceColonization space_colonization_;
};

#endif // ORGANIC_GROWTH_SIMULATOR_H
