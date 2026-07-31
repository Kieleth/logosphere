#include "logosphere/worldgen/space_colonization.h"
#include <iostream>
#include <algorithm>
#include <limits>

SpaceColonization::SpaceColonization()
    : rng_state_(0)
{
}

TreeSkeleton SpaceColonization::generate(const SpaceColonizationParams& params) {
    seed_rng(params.random_seed);

    std::cout << "[SpaceColonization] Starting generation..." << std::endl;
    std::cout << "  Attraction range: " << params.attraction_range << std::endl;
    std::cout << "  Kill distance: " << params.kill_distance << std::endl;

    // Use external attractors if provided, otherwise generate our own
    std::vector<Vec3> attractors;
    if (!params.external_attractors.empty()) {
        attractors = params.external_attractors;
        std::cout << "  Using " << attractors.size() << " external attractors" << std::endl;
    } else {
        attractors = generate_attractors(params);
        std::cout << "  Generated " << attractors.size() << " internal attractors (sphere)" << std::endl;
    }

    // Initialize with root/trunk node
    std::vector<GrowthNode> nodes;
    GrowthNode root;
    root.position = params.root_position;
    root.direction = params.root_direction.normalized();
    root.thickness = params.initial_thickness;
    root.parent_index = -1;
    nodes.push_back(root);

    TreeSkeleton skeleton;

    // Iterative growth
    int iteration = 0;
    while (!attractors.empty() && iteration < params.max_iterations) {
        // Step 1: For each attractor, find closest node
        for (size_t a = 0; a < attractors.size(); ++a) {
            int closest_node = find_closest_node(nodes, attractors[a]);

            if (closest_node >= 0) {
                float dist = distance(nodes[closest_node].position, attractors[a]);

                if (dist < params.attraction_range) {
                    // This node is attracted to this attractor
                    nodes[closest_node].attractor_indices.push_back(a);
                }
            }
        }

        // Step 2: Grow nodes toward their attractors
        std::vector<GrowthNode> new_nodes;
        for (size_t n = 0; n < nodes.size(); ++n) {
            GrowthNode& node = nodes[n];

            if (node.attractor_indices.empty()) {
                continue; // No growth for this node
            }

            // Calculate average direction to all attractors pulling this node
            std::vector<Vec3> attractor_positions;
            for (int idx : node.attractor_indices) {
                attractor_positions.push_back(attractors[idx]);
            }

            Vec3 growth_dir = average_direction(attractor_positions, node.position);

            // Create new node
            GrowthNode new_node;
            new_node.position = node.position + growth_dir * params.segment_length;
            new_node.direction = growth_dir;
            new_node.thickness = node.thickness * params.thickness_taper;
            new_node.parent_index = n;

            new_nodes.push_back(new_node);

            // Clear attractor list for next iteration
            node.attractor_indices.clear();
        }

        // Add new nodes to tree
        int base_idx = nodes.size();
        for (const auto& new_node : new_nodes) {
            nodes.push_back(new_node);

            // Add segment to skeleton with creation iteration (for leaf maturity)
            const GrowthNode& parent = nodes[new_node.parent_index];
            BranchSegment seg(parent.position, new_node.position,
                            new_node.thickness, new_node.parent_index, iteration);
            skeleton.add_segment(seg);
        }

        // Step 3: Remove attractors that have been reached (kill distance)
        attractors.erase(
            std::remove_if(attractors.begin(), attractors.end(),
                [&](const Vec3& attr) {
                    for (const auto& node : nodes) {
                        if (distance(node.position, attr) < params.kill_distance) {
                            return true; // Remove this attractor
                        }
                    }
                    return false;
                }),
            attractors.end()
        );

        iteration++;

        if (iteration % 50 == 0) {
            std::cout << "  Iteration " << iteration << ": "
                      << nodes.size() << " nodes, "
                      << attractors.size() << " attractors remaining" << std::endl;
        }
    }

    std::cout << "[SpaceColonization] Complete after " << iteration << " iterations" << std::endl;
    std::cout << "  Final segment count: " << skeleton.segment_count() << std::endl;

    return skeleton;
}

std::vector<Vec3> SpaceColonization::generate_attractors(const SpaceColonizationParams& params) {
    std::vector<Vec3> attractors;
    attractors.reserve(params.attractor_count);

    for (int i = 0; i < params.attractor_count; ++i) {
        Vec3 point = random_in_sphere(params.attractor_center, params.attractor_radius);
        attractors.push_back(point);
    }

    return attractors;
}

Vec3 SpaceColonization::random_in_sphere(Vec3 center, float radius) {
    // Uniform random point in sphere using rejection sampling
    while (true) {
        float x = (random_01() * 2.0f - 1.0f) * radius;
        float y = (random_01() * 2.0f - 1.0f) * radius;
        float z = (random_01() * 2.0f - 1.0f) * radius;

        float dist_sq = x * x + y * y + z * z;
        if (dist_sq <= radius * radius) {
            return Vec3(center.x + x, center.y + y, center.z + z);
        }
    }
}

int SpaceColonization::find_closest_node(const std::vector<GrowthNode>& nodes, const Vec3& point) {
    int closest = -1;
    float min_dist = std::numeric_limits<float>::max();

    for (size_t i = 0; i < nodes.size(); ++i) {
        float dist = distance(nodes[i].position, point);
        if (dist < min_dist) {
            min_dist = dist;
            closest = i;
        }
    }

    return closest;
}

float SpaceColonization::distance(const Vec3& a, const Vec3& b) {
    return (b - a).length();
}

Vec3 SpaceColonization::average_direction(const std::vector<Vec3>& attractors, const Vec3& from) {
    Vec3 sum(0, 0, 0);

    for (const Vec3& attr : attractors) {
        Vec3 dir = (attr - from).normalized();
        sum = sum + dir;
    }

    return sum.normalized();
}

void SpaceColonization::seed_rng(unsigned int seed) {
    rng_state_ = seed;
    if (rng_state_ == 0) {
        rng_state_ = 12345; // Non-zero default
    }
}

float SpaceColonization::random_01() {
    // Simple LCG
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;
    return (float)rng_state_ / 2147483648.0f;
}
