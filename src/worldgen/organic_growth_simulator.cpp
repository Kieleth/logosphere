#include "logosphere/worldgen/organic_growth_simulator.h"
#include "logosphere/worldgen/attractor_shapes.h"
#include <iostream>
#include <algorithm>

void OrganicGrowthSimulator::simulate_to_current_time(GrowthState& state, const OrganicSpec& spec) {
    double current_time = GameTime::get_current_time();
    simulate_to_time(state, spec, current_time);
}

void OrganicGrowthSimulator::simulate_to_time(GrowthState& state, const OrganicSpec& spec, double target_time) {
    // Check if entity destroyed before target time
    if (state.is_destroyed && state.destruction_time < target_time) {
        std::cout << "[OrganicGrowthSimulator] Entity " << state.entity_id
                  << " destroyed at t=" << state.destruction_time
                  << ", no growth past destruction" << std::endl;
        return;
    }

    // Check if backwards time travel
    if (target_time < state.time_last_simulated) {
        std::cout << "[OrganicGrowthSimulator] Backwards time travel detected: "
                  << state.time_last_simulated << " → " << target_time << std::endl;
        handle_time_travel(state, target_time);
    }

    // Calculate target iteration count
    int target_iteration = state.target_iterations(target_time);

    // Check if growth needed
    if (target_iteration <= state.current_iteration) {
        std::cout << "[OrganicGrowthSimulator] Entity " << state.entity_id
                  << " already at iteration " << state.current_iteration
                  << ", no growth needed" << std::endl;
        return;
    }

    // Check if mature
    if (state.is_mature) {
        std::cout << "[OrganicGrowthSimulator] Entity " << state.entity_id
                  << " is mature (max iterations reached)" << std::endl;
        return;
    }

    // Run growth iterations
    std::cout << "[OrganicGrowthSimulator] Simulating entity " << state.entity_id
              << " from iteration " << state.current_iteration
              << " to " << target_iteration << std::endl;

    run_iterations(state, spec, target_iteration);

    // Update temporal tracking
    state.time_last_simulated = target_time;

    // Check if reached maturity
    if (state.current_iteration >= state.max_iterations) {
        state.is_mature = true;
        std::cout << "[OrganicGrowthSimulator] Entity " << state.entity_id
                  << " reached maturity at iteration " << state.current_iteration << std::endl;
    }
}

void OrganicGrowthSimulator::initialize_growth(GrowthState& state, const OrganicSpec& spec,
                                               float world_x, float world_y, float world_z) {
    // Set identity
    state.organic_type = spec.type;
    state.species = spec.species;

    // Set temporal tracking
    state.time_created = GameTime::get_current_time();
    state.time_last_simulated = state.time_created;
    state.current_iteration = 0;
    state.is_mature = false;

    // Set growth parameters from spec
    state.growth_rate_iterations_per_year = spec.growth_rate_iterations_per_year;
    state.max_iterations = spec.max_iterations;
    state.years_to_maturity = spec.years_to_maturity;

    // Set timeline
    state.timeline = GameTime::get_current_timeline();

    // Generate initial attractors
    Vec3 center(world_x, world_y, world_z);
    state.remaining_attractors = AttractorShapes::generate(spec, center, spec.random_seed);

    // Initialize empty skeleton
    state.current_skeleton.clear();

    // Create initial checkpoint (t=0 state)
    create_checkpoint(state);

    std::cout << "[OrganicGrowthSimulator] Initialized growth for " << spec.species
              << " at (" << world_x << "," << world_y << "," << world_z << ")"
              << " with " << state.remaining_attractors.size() << " attractors" << std::endl;
}

void OrganicGrowthSimulator::create_checkpoint(GrowthState& state) {
    GrowthCheckpoint checkpoint;
    checkpoint.time = state.time_last_simulated;
    checkpoint.iteration = state.current_iteration;
    checkpoint.skeleton = state.current_skeleton;
    checkpoint.attractors = state.remaining_attractors;

    state.checkpoints.push_back(checkpoint);

    std::cout << "[OrganicGrowthSimulator] Created checkpoint at t=" << checkpoint.time
              << ", iteration=" << checkpoint.iteration
              << " (" << state.checkpoints.size() << " checkpoints total)" << std::endl;
}

void OrganicGrowthSimulator::apply_modification(GrowthState& state, const EntityModification& mod) {
    state.modifications.push_back(mod);

    switch (mod.type) {
        case EntityModification::DESTROYED:
            state.is_destroyed = true;
            state.destruction_time = mod.modification_time;
            std::cout << "[OrganicGrowthSimulator] Entity " << state.entity_id
                      << " destroyed at t=" << mod.modification_time << std::endl;
            break;

        case EntityModification::PRUNED:
            // Remove specified segments from skeleton
            for (int segment_idx : mod.removed_segments) {
                if (segment_idx >= 0 && segment_idx < state.current_skeleton.segment_count()) {
                    // Mark segment as removed (set thickness to 0 to indicate removal)
                    state.current_skeleton.get_segment(segment_idx).thickness = 0.0f;
                }
            }
            std::cout << "[OrganicGrowthSimulator] Entity " << state.entity_id
                      << " pruned " << mod.removed_segments.size() << " segments" << std::endl;
            break;

        case EntityModification::FROZEN:
            state.is_mature = true;  // Stops growth
            std::cout << "[OrganicGrowthSimulator] Entity " << state.entity_id
                      << " growth frozen at iteration " << state.current_iteration << std::endl;
            break;

        default:
            break;
    }
}

// Private methods

void OrganicGrowthSimulator::run_iterations(GrowthState& state, const OrganicSpec& spec, int target_iteration) {
    // Run Space Colonization iterations to grow the skeleton
    // Each iteration: branches extend toward attractors, reached attractors removed

    int iterations_to_run = target_iteration - state.current_iteration;

    if (iterations_to_run <= 0) {
        return;  // Already at target
    }

    // Check if we have attractors left
    if (state.remaining_attractors.empty()) {
        std::cout << "[OrganicGrowthSimulator] No attractors remaining, growth complete" << std::endl;
        state.is_mature = true;
        return;
    }

    // Build node representation from skeleton (for Space Colonization logic)
    struct GrowthNode {
        Vec3 position;
        Vec3 direction;
        float thickness;
        int parent_index;
        std::vector<int> attracted_to;
    };

    std::vector<GrowthNode> nodes;

    // Convert existing skeleton to nodes (node = segment endpoint)
    if (state.current_skeleton.segment_count() == 0) {
        // First iteration: create root node at world origin with upward direction
        GrowthNode root;
        root.position = Vec3(0, 0, 0);  // Start at origin
        root.direction = Vec3(0, 0, 1);  // Grow upward
        root.thickness = spec.base_thickness;
        root.parent_index = -1;
        nodes.push_back(root);
    } else {
        // Build from skeleton
        for (int i = 0; i < state.current_skeleton.segment_count(); ++i) {
            const BranchSegment& seg = state.current_skeleton.get_segment(i);
            GrowthNode node;
            node.position = seg.end;
            node.direction = (seg.end - seg.start).normalized();
            node.thickness = seg.thickness;
            node.parent_index = seg.parent_index;
            nodes.push_back(node);
        }
    }

    // Run iterations
    for (int iter = 0; iter < iterations_to_run; ++iter) {
        // Step 1: Find closest node for each attractor
        for (size_t a = 0; a < state.remaining_attractors.size(); ++a) {
            const Vec3& attr = state.remaining_attractors[a];
            int closest = -1;
            float min_dist = spec.attraction_range;

            for (size_t n = 0; n < nodes.size(); ++n) {
                float dist = (nodes[n].position - attr).length();
                if (dist < min_dist) {
                    min_dist = dist;
                    closest = n;
                }
            }

            if (closest >= 0) {
                nodes[closest].attracted_to.push_back(a);
            }
        }

        // Step 2: Grow nodes toward attractors
        std::vector<GrowthNode> new_nodes;
        for (size_t n = 0; n < nodes.size(); ++n) {
            GrowthNode& node = nodes[n];
            if (node.attracted_to.empty()) continue;

            // Average direction to attractors
            Vec3 sum(0, 0, 0);
            for (int idx : node.attracted_to) {
                Vec3 dir = (state.remaining_attractors[idx] - node.position).normalized();
                sum = sum + dir;
            }
            Vec3 growth_dir = sum.normalized();

            // Create new node
            GrowthNode new_node;
            new_node.position = node.position + growth_dir * spec.segment_length;
            new_node.direction = growth_dir;
            new_node.thickness = node.thickness * spec.thickness_taper;
            new_node.parent_index = n;
            new_nodes.push_back(new_node);

            node.attracted_to.clear();
        }

        // Add new nodes/segments
        for (const auto& new_node : new_nodes) {
            const GrowthNode& parent = nodes[new_node.parent_index];
            BranchSegment seg(parent.position, new_node.position,
                            new_node.thickness, new_node.parent_index,
                            state.current_iteration + iter);
            state.current_skeleton.add_segment(seg);
            nodes.push_back(new_node);
        }

        // Step 3: Remove reached attractors
        auto new_end = std::remove_if(
            state.remaining_attractors.begin(),
            state.remaining_attractors.end(),
            [&](const Vec3& attr) {
                for (const auto& node : nodes) {
                    if ((node.position - attr).length() < spec.kill_distance) {
                        return true;
                    }
                }
                return false;
            }
        );
        state.remaining_attractors.erase(new_end, state.remaining_attractors.end());

        state.current_iteration++;

        if (should_create_checkpoint(state.current_iteration)) {
            create_checkpoint(state);
        }

        if (state.remaining_attractors.empty()) {
            std::cout << "[OrganicGrowthSimulator] All attractors reached at iteration "
                      << state.current_iteration << std::endl;
            state.is_mature = true;
            break;
        }
    }

    std::cout << "[OrganicGrowthSimulator] Ran " << (state.current_iteration - (target_iteration - iterations_to_run))
              << " iterations, now at iteration " << state.current_iteration
              << " (" << state.remaining_attractors.size() << " attractors remaining)" << std::endl;
}

void OrganicGrowthSimulator::handle_time_travel(GrowthState& state, double target_time) {
    // Find closest checkpoint before target time
    const GrowthCheckpoint* checkpoint = state.find_checkpoint(target_time);

    if (checkpoint) {
        std::cout << "[OrganicGrowthSimulator] Restoring from checkpoint at t=" << checkpoint->time
                  << ", iteration=" << checkpoint->iteration << std::endl;
        state.restore_checkpoint(*checkpoint);
    } else {
        // No checkpoint found, reset to initial state
        std::cout << "[OrganicGrowthSimulator] No checkpoint found, resetting to initial state" << std::endl;
        state.current_iteration = 0;
        state.time_last_simulated = state.time_created;
        state.current_skeleton.clear();
        // Note: remaining_attractors should be restored from initial state (not implemented yet)
    }
}

bool OrganicGrowthSimulator::should_create_checkpoint(int iteration) const {
    // Adaptive checkpoint strategy:
    // - Every 10 iterations early (0-100)
    // - Every 50 iterations mid (100-200)
    // - Every 100 iterations late (200+)

    if (iteration <= 100) {
        return (iteration % 10 == 0);
    } else if (iteration <= 200) {
        return (iteration % 50 == 0);
    } else {
        return (iteration % 100 == 0);
    }
}

void OrganicGrowthSimulator::prune_checkpoints(GrowthState& state) {
    const size_t MAX_CHECKPOINTS = 10;

    if (state.checkpoints.size() <= MAX_CHECKPOINTS) {
        return;  // No pruning needed
    }

    // Strategy: Keep first, last, and evenly spaced middle
    std::vector<GrowthCheckpoint> pruned;

    // Keep first
    pruned.push_back(state.checkpoints.front());

    // Keep evenly spaced middle
    int stride = state.checkpoints.size() / (MAX_CHECKPOINTS - 2);
    for (size_t i = stride; i < state.checkpoints.size() - 1; i += stride) {
        if (pruned.size() >= MAX_CHECKPOINTS - 1) break;
        pruned.push_back(state.checkpoints[i]);
    }

    // Keep last
    pruned.push_back(state.checkpoints.back());

    state.checkpoints = pruned;

    std::cout << "[OrganicGrowthSimulator] Pruned checkpoints, kept " << state.checkpoints.size() << std::endl;
}
