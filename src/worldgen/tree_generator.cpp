#include "logosphere/worldgen/tree_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/physics/narrow_phase.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <string>

// Species presets - ordered by size (small → large)

TreeSpec TreeSpec::sapling() {
    TreeSpec spec;
    spec.height = 3.0f;             // Just 3m tall - a young tree
    spec.crown_radius = 1.5f;       // Small crown
    spec.trunk_diameter = 0.08f;    // Thin trunk like a thumb
    spec.branch_depth = 2;          // Minimal branching
    spec.branch_angle = 45.0f;      // Upward reaching
    spec.angle_variance = 20.0f;
    spec.length_ratio = 0.6f;
    spec.length_variance = 0.2f;
    spec.thickness_ratio = 0.5f;
    spec.side_branch_probability = 0.2f;
    spec.description = "A young sapling, barely taller than a man. Its slender trunk sways in the breeze, leaves still bright and tender.";
    return spec;
}

TreeSpec TreeSpec::young_oak() {
    TreeSpec spec;
    spec.height = 6.0f;             // 6m - adolescent tree
    spec.crown_radius = 4.0f;       // Modest crown
    spec.trunk_diameter = 0.2f;     // Arm-thick trunk
    spec.branch_depth = 3;          // Some branching
    spec.branch_angle = 55.0f;
    spec.angle_variance = 30.0f;
    spec.length_ratio = 0.65f;
    spec.length_variance = 0.25f;
    spec.thickness_ratio = 0.55f;
    spec.side_branch_probability = 0.3f;
    spec.description = "A young oak, perhaps twenty years old. Its trunk is still smooth, but the characteristic spreading shape is beginning to form.";
    return spec;
}

TreeSpec TreeSpec::oak() {
    TreeSpec spec;
    spec.height = 12.0f;            // Mature oak
    spec.crown_radius = 80.0f;      // Wide canopy
    spec.trunk_diameter = 0.5f;     // Solid trunk
    spec.branch_depth = 5;          // Full branching
    spec.branch_angle = 70.0f;      // Wide angles for spreading oak branches
    spec.angle_variance = 40.0f;    // High variance for wild variety
    spec.length_ratio = 0.7f;
    spec.length_variance = 0.3f;
    spec.thickness_ratio = 0.6f;
    spec.side_branch_probability = 0.5f;
    spec.description = "A mature oak, its gnarled branches reaching wide. The bark is deeply furrowed, and moss clings to its northern side.";
    return spec;
}

TreeSpec TreeSpec::ancient_oak() {
    TreeSpec spec;
    spec.height = 25.0f;            // Massive 25m tall
    spec.crown_radius = 150.0f;     // Huge canopy spread
    spec.trunk_diameter = 1.5f;     // Thick ancient trunk
    spec.branch_depth = 6;          // Deep branching
    spec.branch_angle = 65.0f;      // Wide spreading branches
    spec.angle_variance = 35.0f;    // Natural variation
    spec.length_ratio = 0.7f;
    spec.length_variance = 0.25f;
    spec.thickness_ratio = 0.65f;
    spec.side_branch_probability = 0.6f;
    spec.description = "A truly ancient oak, this giant has witnessed centuries. Its trunk is wide enough to hide inside, and its branches form a vast canopy that blocks the sky.";
    return spec;
}

TreeSpec TreeSpec::baobab() {
    TreeSpec spec;
    spec.height = 12.0f;
    spec.crown_radius = 8.0f;
    spec.trunk_diameter = 1.5f;     // Thick trunk
    spec.branch_depth = 6;
    spec.branch_angle = 35.0f;
    spec.length_ratio = 0.75f;
    spec.thickness_ratio = 0.7f;
    spec.description = "A massive baobab, its swollen trunk holding centuries of rainwater. The bark is smooth and grey, like elephant hide.";
    return spec;
}

TreeSpec TreeSpec::pine() {
    TreeSpec spec;
    spec.height = 15.0f;
    spec.crown_radius = 3.0f;  // Narrow conical shape
    spec.trunk_diameter = 0.8f;
    spec.branch_depth = 7;
    spec.branch_angle = 20.0f;  // Tight angle
    spec.length_ratio = 0.8f;
    spec.description = "A tall pine, arrow-straight, its dark needles whispering in the wind. The scent of resin hangs in the air.";
    return spec;
}

TreeSpec TreeSpec::palm() {
    TreeSpec spec;
    spec.height = 10.0f;
    spec.crown_radius = 4.0f;
    spec.trunk_diameter = 0.5f;
    spec.branch_depth = 2;      // Just trunk + fronds
    spec.branch_angle = 60.0f;  // Wide spreading
    spec.branches_per_split = 8; // Many fronds
    spec.description = "A graceful palm, its slender trunk swaying gently. Large fronds fan out against the sky like green fingers.";
    return spec;
}

TreeSpec TreeSpec::willow() {
    TreeSpec spec;
    spec.height = 10.0f;
    spec.crown_radius = 12.0f;  // Wide drooping canopy
    spec.trunk_diameter = 1.2f;
    spec.branch_depth = 5;
    spec.branch_angle = 40.0f;
    spec.length_ratio = 0.8f;
    spec.description = "A weeping willow, its long trailing branches cascade like green waterfalls. Something sorrowful and ancient in its posture.";
    return spec;
}

void TreeSpec::apply_natural_variation(float variance_amount, unsigned int seed) {
    // Simple LCG for deterministic randomness
    unsigned int rng_state = seed;
    auto rng_next = [&rng_state]() -> float {
        rng_state = (1103515245 * rng_state + 12345) % 2147483648;
        return (float)rng_state / 2147483648.0f;  // [0, 1]
    };

    auto vary = [&rng_next, variance_amount](float base, float max_variance) -> float {
        float random = rng_next();  // [0, 1]
        // Convert to [-1, 1] for symmetric variation
        float normalized = (random * 2.0f - 1.0f);
        // Apply bell-curve-like distribution (x^3 keeps values near 0 more often)
        float curved = normalized * normalized * normalized;
        return base + base * max_variance * variance_amount * curved;
    };

    // Vary each parameter independently for natural combinations
    // Some trees might be tall with thin trunks, others short with thick trunks
    height = vary(height, 0.40f);                      // ±40% height variation (more dramatic)
    crown_radius = vary(crown_radius, 0.50f);          // ±50% canopy size (wild variation)
    trunk_diameter = vary(trunk_diameter, 0.40f);      // ±40% trunk thickness

    // Branch structure variation
    float depth_vary = rng_next() - 0.5f;  // [-0.5, 0.5]
    branch_depth = std::max(3, std::min(8,
        branch_depth + static_cast<int>(depth_vary * 3.0f * variance_amount)));  // More depth variation

    branch_angle = vary(branch_angle, 0.35f);          // ±35% angle variation (wider variety)
    angle_variance = vary(angle_variance, 0.40f);      // ±40% variance variation
    length_ratio = vary(length_ratio, 0.25f);          // ±25% length ratio (more variety)
    thickness_ratio = vary(thickness_ratio, 0.25f);    // ±25% thickness ratio
    side_branch_probability = vary(side_branch_probability, 0.40f); // ±40% branching variation

    // Keep values in reasonable ranges (expanded for more variety)
    height = std::max(5.0f, std::min(30.0f, height));
    crown_radius = std::max(10.0f, std::min(120.0f, crown_radius));
    trunk_diameter = std::max(0.2f, std::min(2.0f, trunk_diameter));
    branch_angle = std::max(40.0f, std::min(90.0f, branch_angle));  // Keep wide angles
    angle_variance = std::max(20.0f, std::min(60.0f, angle_variance));
    side_branch_probability = std::max(0.2f, std::min(0.8f, side_branch_probability));
    length_ratio = std::max(0.5f, std::min(0.9f, length_ratio));
    thickness_ratio = std::max(0.5f, std::min(0.9f, thickness_ratio));
}

TreeGenerator::TreeGenerator()
    : EntityGenerator()
    , rng_state_(0)
{
}

TreeGenerator::~TreeGenerator() {
    // Cleanup handled by KG
}

kg::EntityID TreeGenerator::generate_tree(float world_x, float world_y, float world_z,
                                          const TreeSpec& spec) {
    if (!engine_ || !kg_) {
        std::cerr << "[TreeGenerator] ERROR: Not initialized!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    std::cout << "[TreeGenerator] Generating tree at (" << world_x << ", " << world_y << ", " << world_z
              << ") depth=" << spec.branch_depth << std::endl;

    // Seed RNG for reproducible variation
    seed_rng(spec.random_seed);

    // Create root KG entity for tree ("Tree": ontology name, also the
    // EntityManager activator key — lowercase was rejected by KG
    // validation and this whole deferred path returned INVALID_ENTITY)
    kg::EntityID tree_entity = kg_->createEntityAtPosition("Tree", world_x, world_y);
    kg_->setProperty(tree_entity, "z", std::to_string(world_z));
    kg_->setProperty(tree_entity, "floor_z", std::to_string(world_z));  // Base position for gluon anchor
    kg_->setProperty(tree_entity, "height", std::to_string(spec.height));
    kg_->setProperty(tree_entity, "crown_radius", std::to_string(spec.crown_radius));
    kg_->setProperty(tree_entity, "trunk_diameter", std::to_string(spec.trunk_diameter));
    kg_->setProperty(tree_entity, "description", spec.description);

    // Calculate trunk parameters
    // Trunk is the initial branch at depth = branch_depth (works backward)
    float trunk_length = spec.height * 0.15f;  // Short trunk (~15% of total height, branches start low)
    float initial_elevation = 75.0f;  // Start growing mostly upward (degrees from horizontal)

    // Random initial direction: trees can lean/grow in any direction
    float initial_direction = random_variance(0.0f, 180.0f);  // Random angle 0-360°

    // Generate trunk and all branches recursively
    kg::EntityID trunk_entity = generate_branch(
        world_x, world_y, world_z,
        initial_direction,  // Random direction for natural variety
        initial_elevation,
        trunk_length,
        spec.trunk_diameter,
        spec.branch_depth,
        spec,
        tree_entity  // Parent is tree root
    );

    // Link trunk to tree
    kg_->createRelation(tree_entity, "HAS_PART", trunk_entity);

    // Track tree using base class
    on_entity_created(tree_entity);

    std::cout << "[TreeGenerator] Tree created with entity ID " << tree_entity << std::endl;

    // Queue for activation on main thread (thread-safe)
    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(tree_entity);

    return tree_entity;
}

kg::EntityID TreeGenerator::generate_branch(
    float start_x, float start_y, float start_z,
    float direction_angle,
    float elevation_angle,
    float length,
    float thickness,
    int depth,
    const TreeSpec& spec,
    kg::EntityID parent_entity)
{
    // Base case: create leaves instead of more branches
    if (depth <= 0) {
        return create_leaf_cluster(start_x, start_y, start_z, spec);
    }

    // Create KG entity for this branch
    kg::EntityID branch_entity = kg_->createEntity("Branch");
    kg_->setProperty(branch_entity, "level", std::to_string(spec.branch_depth - depth));
    kg_->setProperty(branch_entity, "angle", std::to_string(direction_angle));
    kg_->setProperty(branch_entity, "length", std::to_string(length));

    // Mark trunk base with floor_z for gluon anchoring (only first branch)
    if (depth == spec.branch_depth) {
        kg_->setProperty(branch_entity, "floor_z", std::to_string(start_z));
    }

    // Create particle data for this branch segment (no render particles - chunk system will activate)
    auto particle_data = create_branch_particles(
        start_x, start_y, start_z,
        length, thickness,
        direction_angle, elevation_angle,
        spec.trunk_r, spec.trunk_g, spec.trunk_b,
        depth  // Pass tree depth for particle allocation strategy
    );

    // Store particle data in KG (no render particles yet - chunk system will activate)
    for (const Particle& p : particle_data) {
        kg::KGParticleID kg_id = kg_->createKGParticle(branch_entity, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(kg_id, p);
    }

    // Calculate direction vectors for positioning
    float rad_h = direction_angle * M_PI / 180.0f;
    float rad_v = elevation_angle * M_PI / 180.0f;
    float dir_x = std::cos(rad_h) * std::cos(rad_v);
    float dir_y = std::sin(rad_h) * std::cos(rad_v);
    float dir_z = std::sin(rad_v);

    // ORGANIC BRANCHING: Spawn side branches at intermediate points along this branch
    // Thicker branches have higher probability of spawning branches
    if (depth > 1) {  // Only spawn side branches if we have depth left for recursion
        int num_segments = particle_data.size();
        float thickness_multiplier = thickness / spec.trunk_diameter;  // Normalized thickness (1.0 = trunk)

        for (int seg = 1; seg < num_segments - 1; ++seg) {  // Skip first and last
            // Position along branch
            float t = (float)seg / num_segments;
            float seg_x = start_x + dir_x * length * t;
            float seg_y = start_y + dir_y * length * t;
            float seg_z = start_z + dir_z * length * t;

            // Probability scaled by thickness: thicker branches = more side branches
            float spawn_probability = spec.side_branch_probability * thickness_multiplier;
            float random_val = (float)(rng_state_ % 1000) / 1000.0f;
            rng_state_ = (rng_state_ * 1103515245 + 12345) % 2147483648;  // Advance RNG

            if (random_val < spawn_probability) {
                // Spawn a side branch here with high variation
                float side_angle = direction_angle + random_variance(0.0f, 180.0f);  // Random direction (360° full circle)

                // Vary elevation dramatically: some up, some down, some horizontal
                float elevation_variance = random_variance(0.0f, 40.0f);  // ±40° from base
                float side_elevation = elevation_angle - 20.0f + elevation_variance;

                // Vary side branch size dramatically: 40% to 100% of normal size
                float size_multiplier = random_variance(0.7f, 0.3f);  // 0.4 to 1.0
                float side_length = length * spec.length_ratio * size_multiplier;

                // Vary thickness independently
                float thickness_multiplier = random_variance(0.8f, 0.2f);  // 0.6 to 1.0
                float side_thickness = thickness * spec.thickness_ratio * thickness_multiplier;

                kg::EntityID side_entity = generate_branch(
                    seg_x, seg_y, seg_z,
                    side_angle,
                    side_elevation,
                    side_length,
                    side_thickness,
                    depth - 1,  // Use remaining depth
                    spec,
                    branch_entity
                );

                kg_->createRelation(branch_entity, "HAS_PART", side_entity);
            }
        }
    }

    // Calculate end position of this branch segment
    float end_x = start_x + length * dir_x;
    float end_y = start_y + length * dir_y;
    float end_z = start_z + length * dir_z;

    // Calculate child branch parameters
    float child_length = length * spec.length_ratio;
    float child_thickness = thickness * spec.thickness_ratio;
    int child_depth = depth - 1;

    // Oak branches: reduce elevation to spread horizontally more
    // Lower elevation = more horizontal spreading (oak-like)
    float child_elevation = elevation_angle - 10.0f;  // Branches spread outward/downward

    // Generate child branches at END of this branch (traditional fractal)
    for (int i = 0; i < spec.branches_per_split; ++i) {
        // Calculate angle for this child
        // For binary tree (2 branches): -angle, +angle
        // For ternary tree (3 branches): -angle, 0, +angle
        float angle_offset;
        if (spec.branches_per_split == 2) {
            angle_offset = (i == 0) ? -spec.branch_angle : spec.branch_angle;
        } else {
            // Distribute evenly around circle
            angle_offset = (360.0f / spec.branches_per_split) * i;
        }

        // Add random variance
        angle_offset += random_variance(0.0f, spec.angle_variance);
        float child_direction = direction_angle + angle_offset;

        // Generate child branch recursively
        kg::EntityID child_entity = generate_branch(
            end_x, end_y, end_z,
            child_direction,
            child_elevation,
            child_length * random_variance(1.0f, spec.length_variance),
            child_thickness,
            child_depth,
            spec,
            branch_entity  // Parent is this branch
        );

        // Link child to this branch
        kg_->createRelation(branch_entity, "HAS_PART", child_entity);
    }

    return branch_entity;
}

std::vector<Particle> TreeGenerator::create_branch_particles(
    float x, float y, float z,
    float length, float thickness,
    float angle_h, float angle_v,
    float r, float g, float b,
    int depth)
{
    // Helper: Convert branch direction angles to particle rotation values
    // A BOX particle's long axis (thickness) points UP (0,0,1) by default
    // We need to rotate it to align with the branch direction
    auto calc_rotation = [](float angle_h_deg, float angle_v_deg, float roll_deg,
                           float& out_rot_x, float& out_rot_y, float& out_rot_z) {
        // Convert angles to radians
        float rad_h = angle_h_deg * M_PI / 180.0f;
        float rad_v = angle_v_deg * M_PI / 180.0f;

        // Calculate direction vector from spherical coordinates
        // angle_h: horizontal angle (0°=East/+X, 90°=North/+Y)
        // angle_v: elevation angle (0°=horizontal, 90°=up)
        float dir_x = std::cos(rad_h) * std::cos(rad_v);
        float dir_y = std::sin(rad_h) * std::cos(rad_v);
        float dir_z = std::sin(rad_v);

        // Convert direction vector to Euler angles (X→Y→Z rotation order)
        // We want to rotate UP (0,0,1) to direction (dir_x, dir_y, dir_z)
        //
        // After applying rot_x and rot_y to (0,0,1):
        // result = (sin(rot_y)*cos(rot_x), -sin(rot_x), cos(rot_y)*cos(rot_x))
        //
        // We want: result = (dir_x, dir_y, dir_z)
        // Therefore:
        //   -sin(rot_x) = dir_y  →  rot_x = -asin(dir_y)
        //   tan(rot_y) = dir_x / dir_z  →  rot_y = atan2(dir_x, dir_z)

        out_rot_x = -std::asin(dir_y);
        out_rot_y = std::atan2(dir_x, dir_z);
        out_rot_z = roll_deg * M_PI / 180.0f;
    };

    // Depth-based particle allocation strategy
    std::vector<Particle> particles;

    if (depth >= 4) {
        // TRUNK / MAIN BRANCHES: Single long BOX with rotation
        // This gives massive particle savings for thick structural branches

        // A BOX IS CENTRED, SO PLACE IT AT THE BRANCH'S MIDPOINT.
        // This sat the box on the branch's START point while giving it the
        // whole branch length as its Z extent, so half the length hung below
        // the start. For the trunk (generate_branch at world_z with
        // trunk_length = height*0.15) that put ancient_oak 1.875 m and oak
        // 0.90 m underground — the largest violation in the sweep.
        const float br_rad_h = angle_h * (float)M_PI / 180.0f;
        const float br_rad_v = angle_v * (float)M_PI / 180.0f;
        const float br_mid_x = x + std::cos(br_rad_h) * std::cos(br_rad_v) * length * 0.5f;
        const float br_mid_y = y + std::sin(br_rad_h) * std::cos(br_rad_v) * length * 0.5f;
        const float br_mid_z = z + std::sin(br_rad_v) * length * 0.5f;

        Particle branch;
        branch.x = br_mid_x;
        branch.y = br_mid_y;
        branch.z = br_mid_z;
        branch.vx = 0.0f;
        branch.vy = 0.0f;
        branch.vz = 0.0f;
        branch.r = r;
        branch.g = g;
        branch.b = b;
        branch.a = 1.0f;

        // BOX shape with length along thickness axis
        branch.shape = ParticleShape::BOX;
        branch.width = thickness;      // Cross-section width
        branch.height = thickness;     // Cross-section height
        branch.thickness = length;     // Length of branch

        // Apply rotation to align with branch direction
        float roll = random_variance(0.0f, 15.0f);  // ±15° organic twist
        calc_rotation(angle_h, angle_v, roll,
                     branch.rotation_x, branch.rotation_y, branch.rotation_z);

        branch.reflectivity = 0.3f;  // Bark reflects some light
        branch.pattern_id = 1;       // PATTERN_WOOD for bark texture

        // DEBUG: Log trunk rotation for first few trunks
        static int trunk_count = 0;
        if (trunk_count++ < 3) {
            std::cout << "[TRUNK_DEBUG depth=" << depth << "] angle_h=" << angle_h
                      << "° angle_v=" << angle_v << "° roll=" << roll << "°" << std::endl;
            std::cout << "  → rot_x=" << (branch.rotation_x * 180.0f / M_PI)
                      << "° rot_y=" << (branch.rotation_y * 180.0f / M_PI)
                      << "° rot_z=" << (branch.rotation_z * 180.0f / M_PI) << "°" << std::endl;
            std::cout << "  position=(" << branch.x << "," << branch.y << "," << branch.z << ")"
                      << " dims: w=" << branch.width << " h=" << branch.height << " t=" << branch.thickness << std::endl;
        }

        particles.push_back(branch);

    } else {
        // FINE BRANCHES: Keep existing CUBE strategy for now
        // TODO: Will optimize these in next step

        const float PARTICLE_SPACING_FACTOR = 1.3f;  // 30% extra spacing for leaner connections
        int num_particles = std::max(1, static_cast<int>(std::ceil(length / (thickness * PARTICLE_SPACING_FACTOR))));

        float rad_h = angle_h * M_PI / 180.0f;
        float rad_v = angle_v * M_PI / 180.0f;

        // Direction vector for this branch
        float dir_x = std::cos(rad_h) * std::cos(rad_v);
        float dir_y = std::sin(rad_h) * std::cos(rad_v);
        float dir_z = std::sin(rad_v);

        // Step size between particle centers
        float step = length / num_particles;

        for (int i = 0; i < num_particles; ++i) {
            float t = i * step;

            Particle branch;
            branch.x = x + dir_x * t;
            branch.y = y + dir_y * t;
            branch.z = z + dir_z * t;
            branch.vx = 0.0f;
            branch.vy = 0.0f;
            branch.vz = 0.0f;
            branch.r = r;
            branch.g = g;
            branch.b = b;
            branch.a = 1.0f;

            // Fine branches use BOX with equal dimensions
            branch.shape = ParticleShape::BOX;
            branch.width = thickness;
            branch.height = thickness;
            branch.thickness = thickness;
            branch.size = thickness;  // For BVH bounding
            branch.reflectivity = 0.3f;
            branch.pattern_id = 1;    // PATTERN_WOOD for bark texture

            particles.push_back(branch);
        }
    }

    return particles;
}

kg::EntityID TreeGenerator::create_leaf_cluster(
    float x, float y, float z,
    const TreeSpec& spec)
{
    // Create KG entity for leaf cluster
    kg::EntityID leaf_entity = kg_->createEntity("Leaves");

    // Create leaf particle data (no render particles - chunk system will activate)
    int num_leaves = 4 + (rng_state_ % 3);  // 4-6 leaves (reduced from 8-12)

    for (int i = 0; i < num_leaves; ++i) {
        // Distribute leaves in 3D sphere using spherical coordinates
        // This prevents clustering and gives natural canopy appearance
        float theta = random_variance(0.0f, 180.0f);  // Horizontal angle (0-360°)
        float phi = random_variance(0.0f, 90.0f);     // Vertical angle (0-180°)
        float offset_radius = random_variance(1.5f, 0.5f);  // 1.0-2.0m radius (wider spread)

        float theta_rad = theta * M_PI / 180.0f;
        float phi_rad = phi * M_PI / 180.0f;

        Particle leaf;
        leaf.x = x + offset_radius * std::sin(phi_rad) * std::cos(theta_rad);
        leaf.y = y + offset_radius * std::sin(phi_rad) * std::sin(theta_rad);
        leaf.z = z + offset_radius * std::cos(phi_rad);
        leaf.vx = 0.0f;
        leaf.vy = 0.0f;
        leaf.vz = 0.0f;

        // Color variation: shades of green for natural look
        leaf.r = spec.leaf_r + random_variance(0.0f, 0.1f);  // Slight red variation
        leaf.g = spec.leaf_g + random_variance(0.0f, 0.15f); // More green variation (lighter/darker greens)
        leaf.b = spec.leaf_b + random_variance(0.0f, 0.1f);  // Slight blue variation
        leaf.r = std::max(0.0f, std::min(1.0f, leaf.r));    // Clamp to valid range
        leaf.g = std::max(0.0f, std::min(1.0f, leaf.g));
        leaf.b = std::max(0.0f, std::min(1.0f, leaf.b));
        leaf.a = 1.0f;

        // Use BOX particles for leaves with random orientations
        leaf.shape = ParticleShape::BOX;
        leaf.width = random_variance(0.2f, 0.1f);    // 0.1-0.3m wide (half of previous)
        leaf.height = random_variance(0.135f, 0.075f); // 0.06-0.21m tall (half of previous)
        leaf.thickness = 0.02f;  // Very thin (2cm)

        // Random orientation (facing angle) for natural look
        leaf.facing_angle = random_variance(0.0f, 180.0f);

        leaf.reflectivity = 0.2f;

        // Store particle data in KG (no render particle yet)
        kg::KGParticleID kg_id = kg_->createKGParticle(leaf_entity, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(kg_id, leaf);
    }

    return leaf_entity;
}

// Simple LCG (Linear Congruential Generator) for deterministic randomness
void TreeGenerator::seed_rng(unsigned int seed) {
    rng_state_ = seed;
}

float TreeGenerator::random_variance(float base, float variance) {
    // Simple LCG: X_{n+1} = (a * X_n + c) mod m
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;

    // Convert to [-variance, +variance] range
    float normalized = (float)rng_state_ / 2147483648.0f;  // [0, 1]
    float offset = (normalized * 2.0f - 1.0f) * variance;  // [-variance, +variance]

    return base + offset;
}

// Generate tree using Space Colonization Algorithm
kg::EntityID TreeGenerator::generate_tree_space_colonization(
    float world_x, float world_y, float world_z,
    const TreeSpec& spec)
{
    if (!engine_ || !kg_) {
        std::cerr << "[TreeGenerator] ERROR: Not initialized!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    std::cout << "[TreeGenerator] Generating tree (Space Colonization) at ("
              << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;

    // Create root KG entity for tree ("Tree": ontology name, also the
    // EntityManager activator key)
    kg::EntityID tree_entity = kg_->createEntityAtPosition("Tree", world_x, world_y);
    kg_->setProperty(tree_entity, "z", std::to_string(world_z));
    kg_->setProperty(tree_entity, "method", "space_colonization");
    kg_->setProperty(tree_entity, "description", spec.description);

    // Organic growth - no artificial segments, just trunk + Space Colonization
    seed_rng(spec.random_seed);

    // Trunk fraction: spec-controlled (canopy_start), legacy random
    // 30-60% when unset.
    float canopy_start = spec.canopy_start >= 0.0f
                             ? spec.canopy_start
                             : random_variance(0.45f, 0.15f);
    float trunk_height = spec.height * canopy_start;
    float crown_height = spec.height - trunk_height;

    // Natural trunk lean, scaled DOWN with height: 15° reads organic
    // on a sapling and broken on a 25 m redwood.
    float lean_max = 15.0f * std::min(1.0f, 8.0f / std::max(spec.height, 1.0f));
    float trunk_lean_angle = random_variance(0.0f, lean_max);
    float trunk_lean_direction = random_variance(0.0f, 180.0f);  // Random direction

    // Trunk direction vector
    float lean_rad_h = trunk_lean_direction * M_PI / 180.0f;
    float lean_rad_v = (90.0f - trunk_lean_angle) * M_PI / 180.0f;
    float trunk_dir_x = std::cos(lean_rad_h) * std::cos(lean_rad_v);
    float trunk_dir_y = std::sin(lean_rad_h) * std::cos(lean_rad_v);
    float trunk_dir_z = std::sin(lean_rad_v);

    // Trunk top position
    float trunk_top_x = world_x + trunk_dir_x * trunk_height;
    float trunk_top_y = world_y + trunk_dir_y * trunk_height;
    float trunk_top_z = world_z + trunk_dir_z * trunk_height;

    // Space Colonization: starts from trunk top, grows organically toward attractors
    SpaceColonizationParams sc_params;
    sc_params.root_position = Vec3(trunk_top_x, trunk_top_y, trunk_top_z);
    sc_params.root_direction = Vec3(trunk_dir_x, trunk_dir_y, trunk_dir_z);

    // Attractor sphere: positioned to encourage initial upward growth, then spreading
    // Center slightly above and forward of trunk top
    Vec3 crown_center(
        trunk_top_x + trunk_dir_x * crown_height * 0.5f,
        trunk_top_y + trunk_dir_y * crown_height * 0.5f,
        trunk_top_z + trunk_dir_z * crown_height * 0.5f
    );
    sc_params.attractor_center = crown_center;
    // HONOR THE SPEC. The old code hardcoded a ~5 m attractor sphere
    // (crown_radius was ignored entirely) with fixed count and
    // iterations — every large tree came out a bare pole with a
    // 5 m tuft on top (the Logogenesis redwood stick).
    // Physical clamp: presets carry legacy crown values that never
    // mattered while this path ignored the field (oak says 80 m!).
    // A crown does not reach wider than ~60% of the tree's height.
    float crown_reach = std::max({spec.crown_radius,
                                  crown_height * 0.45f, 2.0f});
    crown_reach = std::min(crown_reach, spec.height * 0.6f);
    sc_params.attractor_radius = crown_reach;
    float density = std::max(0.2f, spec.canopy_density);
    float reach_scale = (crown_reach / 5.0f) * (crown_reach / 5.0f);
    sc_params.attractor_count = static_cast<int>(
        std::min(500.0f, std::max(80.0f, 120.0f * reach_scale * density)));
    // Space colonization has ONE length unit: the crown it is filling.
    // Every distance below is a fraction of crown_reach, because a 1 m
    // tree and a 20 m tree are then the same problem at different
    // scales. The ratios are the ones this path already used at a 5 m
    // crown; what is gone are the floors in metres that used to sit on
    // top of them.
    //
    // Those floors are what made small trees bare poles. Growth ends
    // when the attractors run out, and step 3 deletes every attractor
    // within kill_distance of ANY node. A floor of 1.5 m against a
    // crown that is only 0.6 m across (crown_reach is capped at 60% of
    // height) meant the root node sat inside the cloud and deleted all
    // 80 attractors on the first iteration: one segment, every time,
    // for every tree up to 3 m. The retry could not help, because it
    // raises crown_radius and the height cap throws that away.
    // The fractions reproduce what the old constants gave a real
    // full-size tree, which is the size they were tuned for. A 20 m
    // tree carries a crown about 7 m across, and at crown_reach = 7
    // these land on the previous 3.5 m kill distance, 0.5 m segments
    // and 5.6 m attraction range. Big trees keep their detail; small
    // ones now get the same treatment instead of floors wider than
    // they are.
    sc_params.attraction_range = crown_reach * 0.80f;
    sc_params.kill_distance    = crown_reach * 0.50f;
    sc_params.segment_length   = crown_reach * 0.07f;
    sc_params.initial_thickness = spec.trunk_diameter * random_variance(0.6f, 0.1f);
    sc_params.thickness_taper = spec.thickness_ratio;
    sc_params.max_iterations = static_cast<int>(
        std::min(240.0f, std::max(80.0f, spec.height * 6.0f)));
    sc_params.random_seed = spec.random_seed;

    // Create trunk: multiple segments with tapering diameter (organic)
    // Split trunk into segments for natural taper and slight variation
    int num_trunk_segments = std::max(3, static_cast<int>(trunk_height / 1.5f));  // ~1.5m per segment
    float segment_length = trunk_height / num_trunk_segments;

    // A trunk that LEANS dips below its base point when seated by axis
    // arithmetic alone: segment 0's centre sits 0.55·L up the tilted AXIS,
    // but its box reaches further down VERTICALLY than the axis rise —
    // by 0.55·L·(1−dir_z) under the door guard's z−thickness/2 measure,
    // and by the cross-section's tilt contribution under the solver's
    // oriented-extent measure. Both put the base millimetres below the
    // turtle (INV-1 aborts at the kg door). Measured on segment 0 below,
    // then the WHOLE tree is lifted by it: a rigid translation, so every
    // seam, offset and later bond is untouched (INV-4 born-at-rest).
    float base_lift = 0.0f;

    for (int i = 0; i < num_trunk_segments; ++i) {
        // Position along trunk (0 = base, 1 = top)
        float t_start = static_cast<float>(i) / num_trunk_segments;
        float t_mid = (static_cast<float>(i) + 0.5f) / num_trunk_segments;

        // Taper: diameter decreases from base to top
        // Base = 100%, top = 60% (smooth taper)
        float diameter_at_segment = spec.trunk_diameter * (1.0f - t_mid * 0.4f);

        // Slight organic variation in direction (tree sways slightly)
        float wobble_x = random_variance(0.0f, 0.02f);  // ±2cm horizontal wobble
        float wobble_y = random_variance(0.0f, 0.02f);
        float seg_dir_x = trunk_dir_x + wobble_x;
        float seg_dir_y = trunk_dir_y + wobble_y;
        float seg_dir_z = trunk_dir_z;
        // Normalize
        float len = std::sqrt(seg_dir_x * seg_dir_x + seg_dir_y * seg_dir_y + seg_dir_z * seg_dir_z);
        seg_dir_x /= len;
        seg_dir_y /= len;
        seg_dir_z /= len;

        // Segment center position
        float seg_center_x = world_x + seg_dir_x * trunk_height * t_mid;
        float seg_center_y = world_y + seg_dir_y * trunk_height * t_mid;
        float seg_center_z = world_z + seg_dir_z * trunk_height * t_mid;

        // The 1.1 overlap extends a BOX about its centre, so half of the extra
        // 10% hung below the segment — and below the floor for the bottom one.
        // Shift the centre up its own axis by half the extra so the underside
        // stays put and the overlap lands at the seam above. Same fix
        // organic_generator got; this copy was missed.
        const float tseg_shift = segment_length * 0.05f;

        Particle trunk_seg;
        trunk_seg.x = seg_center_x + seg_dir_x * tseg_shift;
        trunk_seg.y = seg_center_y + seg_dir_y * tseg_shift;
        trunk_seg.z = seg_center_z + seg_dir_z * tseg_shift;
        trunk_seg.shape = ParticleShape::BOX;
        trunk_seg.width = diameter_at_segment;
        trunk_seg.height = diameter_at_segment;
        trunk_seg.thickness = segment_length * 1.1f;  // Slight overlap for seamless connection
        // Rotate to match segment direction
        trunk_seg.rotation_x = -std::asin(seg_dir_y);
        trunk_seg.rotation_y = std::atan2(seg_dir_x, seg_dir_z);
        trunk_seg.rotation_z = random_variance(0.0f, 0.05f);  // Slight twist
        trunk_seg.r = spec.trunk_r;
        trunk_seg.g = spec.trunk_g;
        trunk_seg.b = spec.trunk_b;
        trunk_seg.reflectivity = 0.3f;
        trunk_seg.pattern_id = 1;  // PATTERN_WOOD for bark texture

        if (i == 0) {
            // Both measures of "below the base", worst one wins:
            // the solver's oriented extent (canonical helper, same math
            // enforce_turtle_boundary uses) and the door guard's
            // rotation-blind z − thickness/2.
            const AABB6 bb = aabb_of_obb(
                obb_of_box_particle(trunk_seg, trunk_seg.z));
            const float oriented_dip = world_z - bb.min_z;
            const float door_dip =
                world_z - (trunk_seg.z - trunk_seg.thickness * 0.5f);
            base_lift = std::max({0.0f, oriented_dip, door_dip});
        }
        trunk_seg.z += base_lift;

        kg::KGParticleID seg_kg_id = kg_->createKGParticle(tree_entity, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(seg_kg_id, trunk_seg);
    }

    // Lower trunk branches: short tapered limbs with leaf tufts,
    // sprouting between 45% and 95% of the trunk. Species/age
    // character (an ancient oak reads gnarled; a redwood stays
    // clean with lower_branch_count = 0).
    for (int lb = 0; lb < spec.lower_branch_count; ++lb) {
        float t = random_variance(0.7f, 0.25f);            // 0.45..0.95
        float base_x = world_x + trunk_dir_x * trunk_height * t;
        float base_y = world_y + trunk_dir_y * trunk_height * t;
        float base_z = world_z + base_lift + trunk_dir_z * trunk_height * t;
        float yaw = random_variance(0.0f, 180.0f) * static_cast<float>(M_PI) / 180.0f;
        float pitch = random_variance(38.0f, 14.0f) * static_cast<float>(M_PI) / 180.0f;
        float dir_x = std::cos(yaw) * std::cos(pitch);
        float dir_y = std::sin(yaw) * std::cos(pitch);
        float dir_z = std::sin(pitch);
        float branch_len = std::max(1.0f, crown_reach *
                                    random_variance(0.45f, 0.15f));
        int segs = 3;
        float seg_len = branch_len / segs;
        float thick = spec.trunk_diameter * (1.0f - t * 0.4f) * 0.45f;
        for (int si = 0; si < segs; ++si) {
            float mid = (si + 0.5f) * seg_len;
            Particle bseg;
            bseg.x = base_x + dir_x * mid;
            bseg.y = base_y + dir_y * mid;
            bseg.z = base_z + dir_z * mid;
            bseg.shape = ParticleShape::BOX;
            float taper = 1.0f - 0.25f * si;
            bseg.width = thick * taper;
            bseg.height = thick * taper;
            bseg.thickness = seg_len * 1.15f;
            bseg.rotation_x = -std::asin(dir_y);
            bseg.rotation_y = std::atan2(dir_x, dir_z);
            bseg.r = spec.trunk_r; bseg.g = spec.trunk_g; bseg.b = spec.trunk_b;
            bseg.reflectivity = 0.3f;
            bseg.pattern_id = 1;
            auto kgid = kg_->createKGParticle(tree_entity, kg::INVALID_RENDER_INDEX);
            kg_->setKGParticleData(kgid, bseg);
        }
        // Leaf tuft at the limb tip.
        int tuft = 2 + (lb % 2);
        for (int li = 0; li < tuft; ++li) {
            Particle leaf;
            leaf.x = base_x + dir_x * branch_len + random_variance(0.0f, 0.5f);
            leaf.y = base_y + dir_y * branch_len + random_variance(0.0f, 0.5f);
            leaf.z = base_z + dir_z * branch_len + random_variance(0.2f, 0.4f);
            leaf.shape = ParticleShape::BOX;
            leaf.width = spec.leaf_base_width;
            leaf.height = spec.leaf_base_height;
            leaf.thickness = 0.03f;
            leaf.r = spec.leaf_r; leaf.g = spec.leaf_g; leaf.b = spec.leaf_b;
            leaf.rotation_z = random_variance(0.0f, 1.5f);
            auto kgid = kg_->createKGParticle(tree_entity, kg::INVALID_RENDER_INDEX);
            kg_->setKGParticleData(kgid, leaf);
        }
    }

    // Run Space Colonization algorithm for crown. The crown rides the
    // trunk, so it carries the same base lift.
    sc_params.root_position.z += base_lift;
    sc_params.attractor_center.z += base_lift;
    SpaceColonization sc_algorithm;
    TreeSkeleton skeleton = sc_algorithm.generate(sc_params);

    std::cout << "[TreeGenerator] Generated skeleton with " << skeleton.segment_count()
              << " segments" << std::endl;

    // Convert skeleton to particles (branches + leaves)
    auto particle_data = skeleton_to_particles(skeleton, spec);

    std::cout << "[TreeGenerator] Created " << particle_data.size()
              << " particles from skeleton" << std::endl;
    if (particle_data.empty()) {
        std::cerr << "[TreeGenerator] ERROR: space colonization produced an "
                     "EMPTY tree (height/crown outside the algorithm's "
                     "envelope?) — callers should treat this entity as failed"
                  << std::endl;
    }

    // Store particles in KG
    for (const Particle& p : particle_data) {
        kg::KGParticleID kg_id = kg_->createKGParticle(tree_entity, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(kg_id, p);
    }

    // Track tree
    on_entity_created(tree_entity);

    // Queue for activation on main thread (thread-safe)
    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(tree_entity);

    return tree_entity;
}

// Convert skeleton to particles (branches + leaves at tips)
std::vector<Particle> TreeGenerator::skeleton_to_particles(
    const TreeSkeleton& skeleton,
    const TreeSpec& spec)
{
    std::vector<Particle> particles;
    particles.reserve(skeleton.segment_count() * 2);  // Room for branches + leaves

    // Helper: Convert direction vector to Euler angles
    auto direction_to_euler = [](const Vec3& dir, float& out_rot_x, float& out_rot_y, float& out_rot_z) {
        out_rot_x = -std::asin(dir.y);
        out_rot_y = std::atan2(dir.x, dir.z);
        out_rot_z = 0.0f; // No roll
    };

    // Strategy: Add leaves to thin branches (outer crown), not just tips
    // This creates fuller foliage throughout the crown
    seed_rng(spec.random_seed);

    // Calculate thickness threshold for leaf placement
    // Branches thinner than this get leaves (outer crown branches)
    float initial_thickness = skeleton.segment_count() > 0 ?
        skeleton.get_segment(0).thickness : spec.trunk_diameter * 0.5f;
    float leaf_thickness_threshold = initial_thickness * spec.leaf_thickness_threshold;

    // Calculate max creation_iteration (total iterations) for leaf maturity
    int max_iteration = 0;
    for (int i = 0; i < skeleton.segment_count(); ++i) {
        max_iteration = std::max(max_iteration, skeleton.get_segment(i).creation_iteration);
    }

    for (int i = 0; i < skeleton.segment_count(); ++i) {
        const BranchSegment& seg = skeleton.get_segment(i);
        Vec3 dir = seg.direction();
        float length = seg.length();

        // Create branch particle (BOX centered at midpoint)
        Vec3 mid_pos(seg.start.x + dir.x * length * 0.5f,
                     seg.start.y + dir.y * length * 0.5f,
                     seg.start.z + dir.z * length * 0.5f);

        Particle p;
        p.x = mid_pos.x;  // CENTER of branch segment
        p.y = mid_pos.y;
        p.z = mid_pos.z;
        p.vx = 0.0f;
        p.vy = 0.0f;
        p.vz = 0.0f;
        p.r = spec.trunk_r;
        p.g = spec.trunk_g;
        p.b = spec.trunk_b;
        p.a = 1.0f;
        p.shape = ParticleShape::BOX;
        p.width = seg.thickness;
        p.height = seg.thickness;
        p.thickness = length;
        direction_to_euler(dir, p.rotation_x, p.rotation_y, p.rotation_z);
        p.reflectivity = 0.3f;
        p.pattern_id = 1;  // PATTERN_WOOD for bark texture
        particles.push_back(p);

        // Add leaves to thin branches (outer crown)
        // Only at endpoint for reduced particle count
        if (seg.thickness < leaf_thickness_threshold) {
            // Only use endpoint (skip midpoint for optimization)
            Vec3 leaf_center = seg.end;

            int leaf_count_range = spec.leaf_count_max - spec.leaf_count_min + 1;
            int num_leaves = spec.leaf_count_min + (rng_state_ % leaf_count_range);

            for (int j = 0; j < num_leaves; ++j) {
                // Distribute leaves around position in small sphere
                float theta = random_variance(0.0f, 180.0f);
                float phi = random_variance(0.0f, 90.0f);
                float offset_radius = random_variance(spec.leaf_offset_radius, spec.leaf_offset_variance);

                float theta_rad = theta * M_PI / 180.0f;
                float phi_rad = phi * M_PI / 180.0f;

                Particle leaf;
                leaf.x = leaf_center.x + offset_radius * std::sin(phi_rad) * std::cos(theta_rad);
                leaf.y = leaf_center.y + offset_radius * std::sin(phi_rad) * std::sin(theta_rad);
                leaf.z = leaf_center.z + offset_radius * std::cos(phi_rad);
                leaf.vx = 0.0f;
                leaf.vy = 0.0f;
                leaf.vz = 0.0f;

                // Green color with variation
                leaf.r = spec.leaf_r + random_variance(0.0f, 0.1f);
                leaf.g = spec.leaf_g + random_variance(0.0f, 0.15f);
                leaf.b = spec.leaf_b + random_variance(0.0f, 0.1f);
                leaf.r = std::max(0.0f, std::min(1.0f, leaf.r));
                leaf.g = std::max(0.0f, std::min(1.0f, leaf.g));
                leaf.b = std::max(0.0f, std::min(1.0f, leaf.b));
                leaf.a = 1.0f;

                leaf.shape = ParticleShape::BOX;

                // Leaf maturity based on branch age
                // Older branches (created early) = mature leaves (large)
                // Younger branches (created late) = young leaves (small)
                float branch_age = static_cast<float>(max_iteration - seg.creation_iteration);
                float maturity = (max_iteration > 0) ? (branch_age / max_iteration) : 1.0f;  // 0.0-1.0

                // Bias towards mature leaves using maturity^2
                // This gives more weight to larger sizes (mature leaves more common)
                float size_factor = 0.5f + maturity * maturity * 0.5f;  // 0.5-1.0 (biased high)

                leaf.width = random_variance(spec.leaf_base_width * size_factor, 0.03f);   // Scale by maturity
                leaf.height = random_variance(spec.leaf_base_height * size_factor, 0.02f);  // Scale by maturity
                leaf.thickness = 0.02f;
                leaf.facing_angle = random_variance(0.0f, 180.0f);
                leaf.reflectivity = 0.2f;

                // Add tilt to some leaves for natural droop/movement
                float tilt_chance = (rng_state_ % 100) / 100.0f;
                rng_state_ = (rng_state_ * 1103515245 + 12345) % 2147483648;
                if (tilt_chance < spec.leaf_tilt_probability) {
                    // Tilt downward (convert degrees to radians)
                    float tilt_degrees = random_variance(
                        (spec.leaf_tilt_angle_min + spec.leaf_tilt_angle_max) * 0.5f,
                        (spec.leaf_tilt_angle_max - spec.leaf_tilt_angle_min) * 0.5f
                    );
                    leaf.rotation_x = tilt_degrees * M_PI / 180.0f;  // Downward tilt
                }

                particles.push_back(leaf);
            }
        }
    }

    return particles;
}
