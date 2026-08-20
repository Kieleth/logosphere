#include "logosphere/worldgen/physics_tree_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/kg/kg_module.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <string>

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

namespace
{
    bool g_legacy_placement = false;
}
void PhysicsTreeGenerator::set_legacy_placement(bool on) { g_legacy_placement = on; }
bool PhysicsTreeGenerator::legacy_placement() { return g_legacy_placement; }

PhysicsTreeGenerator::PhysicsTreeGenerator()
    : engine_(nullptr), physics_(nullptr), particles_(nullptr), kg_(nullptr), rng_state_(0)
{
}

PhysicsTreeGenerator::~PhysicsTreeGenerator()
{
}

void PhysicsTreeGenerator::initialize(Engine* engine)
{
    engine_ = engine;
    physics_ = &engine->get_physics_system();
    particles_ = &engine->get_particle_system();
    kg_ = &engine->get_kg();
}

// ============================================================================
// MAIN GENERATION ENTRY POINT
// ============================================================================

PhysicsTreeResult PhysicsTreeGenerator::generate_tree(
    float world_x, float world_y, float world_z,
    const TreeSpec &spec)
{
    if (!engine_ || !physics_ || !particles_)
    {
        std::cerr << "[PhysicsTreeGenerator] ERROR: Not initialized!" << std::endl;
        return PhysicsTreeResult();
    }

    std::cout << "[PhysicsTreeGenerator] Generating physics tree at ("
              << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;
    std::cout << "  height=" << spec.height << "m, trunk_diameter=" << spec.trunk_diameter
              << "m, depth=" << spec.branch_depth << std::endl;

    // Seed RNG for reproducible variation
    seed_rng(spec.random_seed);

    PhysicsTreeResult result;

    // Try to find an existing kinematic floor tile at this position
    // This avoids creating duplicate floor anchors that conflict with floor tiles
    int floor_tile_id = find_floor_tile_at(world_x, world_y);
    float floor_top_z = world_z;

    if (floor_tile_id >= 0)
    {
        // Use existing floor tile as anchor
        result.floor_id = floor_tile_id;

        // Get floor tile's top surface Z
        auto particles = particles_->lock_particles_for_read();
        const Particle &tile = particles[floor_tile_id];
        floor_top_z = tile.z + tile.thickness * 0.5f; // Top of floor tile
        std::cout << "  Using existing floor tile id=" << floor_tile_id
                  << " (top at z=" << floor_top_z << ")" << std::endl;
    }
    else
    {
        // NO DEFAULTS: Tree MUST gluon to existing floor tile
        // If no floor tile exists at this position, tree creation fails
        std::cerr << "[PhysicsTreeGenerator] ERROR: No floor tile found at ("
                  << world_x << ", " << world_y << "). "
                  << "Trees must be gluoned to existing floor tiles. "
                  << "Ensure floor is generated before creating trees." << std::endl;
        return PhysicsTreeResult(); // Return empty/invalid result
    }

    // Generate tree on this floor
    return generate_tree_on_floor(world_x, world_y, floor_top_z, result.floor_id, spec);
}

PhysicsTreeResult PhysicsTreeGenerator::generate_tree_on_floor(
    float world_x, float world_y, float world_z,
    int floor_particle_id,
    const TreeSpec &spec)
{
    if (!engine_ || !physics_ || !particles_ || !kg_)
    {
        std::cerr << "[PhysicsTreeGenerator] ERROR: Not initialized!" << std::endl;
        return PhysicsTreeResult();
    }

    // Make modifiable copy and apply natural variation (bell-curved randomization)
    // This matches kinematic TreeGenerator for organic variety
    TreeSpec varied_spec = spec;
    varied_spec.apply_natural_variation(0.3f, spec.random_seed);

    seed_rng(varied_spec.random_seed);

    PhysicsTreeResult result;
    result.floor_id = floor_particle_id;

    // Create KG entity for this tree (enables removal via entity_system.removeEntity())
    result.entity_id = kg_->createEntityAtPosition("PhysicsTree", world_x, world_y);

    // ========================================================================
    // TRUNK CREATION - GRANDIOUS STYLE
    // ========================================================================
    // Trunk is the base segment - attached rigidly to floor via NailGluon
    // Make trunk substantial and impressive - the visual anchor of the tree

    // GRANDIOUS: Taller, thicker trunk (25-40% of height, not 10-24%)
    float trunk_ratio = random_variance(0.32f, 0.08f); // 0.24 to 0.40
    float trunk_length = varied_spec.height * trunk_ratio;
    // Thicker trunk for grandeur (1.2x to 1.6x base diameter)
    float trunk_thickness = varied_spec.trunk_diameter * random_variance(1.4f, 0.2f);

    // Create trunk particle
    Particle trunk = create_branch_particle(
        world_x, world_y, world_z + trunk_length * 0.5f, // Center of trunk
        trunk_length, trunk_thickness,
        0.0f, 90.0f, // Pointing straight up
        varied_spec, varied_spec.branch_depth);

    // Attach trunk to floor with OrganicGluon (tree roots)
    // Roots spread underground - large contact area = massive breaking force
    // Requires bulldozer-level force to uproot an established tree
    auto root = std::make_unique<OrganicGluon>();
    // Get floor particle's actual thickness for correct offset to top surface
    float floor_half_thickness = 0.1f; // Default fallback
    {
        auto particles_read = particles_->lock_particles_for_read();
        const Particle &floor_p = particles_read[floor_particle_id];
        floor_half_thickness = floor_p.thickness * 0.5f;
    }
    // Offset from floor center to tree position (floor is one big tile)
    float floor_x = 0.0f, floor_y = 0.0f;
    {
        auto particles_read = particles_->lock_particles_for_read();
        const Particle &floor_p = particles_read[floor_particle_id];
        floor_x = floor_p.x;
        floor_y = floor_p.y;
    }
    root->offset_a = Vec3(world_x - floor_x, world_y - floor_y, floor_half_thickness);
    root->offset_b = Vec3(0.0f, 0.0f, -trunk_length * 0.5f); // Bottom of trunk
    root->target_distance = 0.0f;
    // Root system contact area (m²) - larger trees have more extensive roots
    // Ancient oak roots spread 2-3x crown radius underground
    root->contact_area = trunk_thickness * trunk_thickness * 4.0f; // ~2-5 m² for typical trees
    // Force law derived at registration from materials (Phase C).
    // Breaking force = contact_area × avg_material_strength
    // With 2m² × 25MPa avg = 50MN (bulldozer territory)

    result.trunk_id = physics_->add_particle_with_gluon_to(
        floor_particle_id, trunk, std::move(root));
    result.branch_ids.push_back(result.trunk_id);
    result.total_segments++;

    std::cout << "  Trunk: id=" << result.trunk_id << " mass=" << trunk.GetMass()
              << "kg length=" << trunk_length << "m" << std::endl;

    // ========================================================================
    // RECURSIVE BRANCH GENERATION
    // ========================================================================
    // Branches grow from trunk top, then sub-branches grow from those

    float trunk_top_z = world_z + trunk_length;

    // Initial branching parameters - GRANDIOUS wide-spreading crown
    // Start more horizontal for DRAMATIC wide crown like old oaks
    float initial_elevation = random_variance(45.0f, 15.0f); // 30-60° (more horizontal!)
    float initial_direction = random_variance(0.0f, 180.0f); // Random horizontal

    // GRANDIOUS: 4-5 main branches for dramatic silhouette
    int num_main_branches = std::max(4, varied_spec.branches_per_split + 2);

    // GRANDIOUS: Use reduced depth for simpler, bolder structure
    int effective_depth = std::max(2, varied_spec.branch_depth - 2); // Reduce complexity

    // Generate main branches from trunk top
    for (int i = 0; i < num_main_branches; ++i)
    {
        // Distribute evenly around trunk with variance
        float base_angle = (360.0f / num_main_branches) * i;
        float angle_offset = base_angle + random_variance(0.0f, varied_spec.angle_variance);

        float branch_direction = initial_direction + angle_offset;
        // Main branches spread outward MORE horizontally for wide crown
        float branch_elevation = initial_elevation + random_variance(0.0f, 15.0f);
        // GRANDIOUS: MUCH longer main branches (40-80% of height, not trunk_length based)
        float branch_length = varied_spec.height * random_variance(0.5f, 0.15f);
        // GRANDIOUS: Thicker branches (80-100% of trunk thickness)
        float branch_thickness = trunk_thickness * random_variance(0.7f, 0.15f);

        generate_branch(
            result.trunk_id,
            world_x, world_y, trunk_top_z, // trunk is upright: tip x,y = centre x,y
            branch_direction,
            branch_elevation,
            branch_length,
            branch_thickness,
            effective_depth, // GRANDIOUS: Use reduced depth for simpler structure
            varied_spec,
            result);
    }

    std::cout << "  Total segments: " << result.total_segments
              << ", leaves: " << result.leaf_ids.size() << std::endl;

    // Bind all particles to KG entity for cleanup when tree is removed
    {
        auto particles = particles_->lock_particles_for_read();

        // Bind trunk
        if (result.trunk_id >= 0 && static_cast<size_t>(result.trunk_id) < particles.size())
        {
            kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, result.trunk_id);
            kg_->setKGParticleData(kg_id, particles[result.trunk_id]);
        }

        // Bind branches
        for (int branch_id : result.branch_ids)
        {
            if (branch_id >= 0 && static_cast<size_t>(branch_id) < particles.size())
            {
                kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, branch_id);
                kg_->setKGParticleData(kg_id, particles[branch_id]);
            }
        }

        // Bind leaves
        for (int leaf_id : result.leaf_ids)
        {
            if (leaf_id >= 0 && static_cast<size_t>(leaf_id) < particles.size())
            {
                kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, leaf_id);
                kg_->setKGParticleData(kg_id, particles[leaf_id]);
            }
        }

        std::cout << "  Bound " << (1 + result.branch_ids.size() + result.leaf_ids.size())
                  << " particles to KG entity " << result.entity_id << std::endl;
    }

    return result;
}

// ============================================================================
// GENERATE TREE WITH ROOTS (no floor tile required)
// ============================================================================

PhysicsTreeResult PhysicsTreeGenerator::generate_tree_with_roots(
    float world_x, float world_y, float ground_z,
    const TreeSpec &spec)
{
    if (!engine_ || !physics_ || !particles_ || !kg_)
    {
        std::cerr << "[PhysicsTreeGenerator] ERROR: Not initialized!" << std::endl;
        return PhysicsTreeResult();
    }

    std::cout << "[PhysicsTreeGenerator] Generating tree WITH ROOTS at ("
              << world_x << ", " << world_y << ", " << ground_z << ")" << std::endl;
    std::cout << "  height=" << spec.height << "m, trunk_diameter=" << spec.trunk_diameter
              << "m, depth=" << spec.branch_depth << std::endl;

    // Seed RNG for reproducible variation
    seed_rng(spec.random_seed);

    // Make modifiable copy and apply natural variation
    TreeSpec varied_spec = spec;
    varied_spec.apply_natural_variation(0.3f, spec.random_seed);

    seed_rng(varied_spec.random_seed);

    PhysicsTreeResult result;

    // Create KG entity for this tree (enables removal via entity_system.removeEntity())
    result.entity_id = kg_->createEntityAtPosition("PhysicsTree", world_x, world_y);

    // ========================================================================
    // STEP 1: Generate root system (root plate + primary roots)
    // ========================================================================
    int root_plate_id = generate_root_system(
        world_x, world_y, ground_z,
        varied_spec.height, varied_spec.trunk_diameter,
        result);

    if (root_plate_id < 0)
    {
        std::cerr << "[PhysicsTreeGenerator] ERROR: Failed to create root system!" << std::endl;
        return PhysicsTreeResult();
    }

    // ========================================================================
    // STEP 2: Create trunk attached to root plate
    // ========================================================================
    // GRANDIOUS STYLE trunk (same as generate_tree_on_floor)

    float trunk_ratio = random_variance(0.32f, 0.08f); // 0.24 to 0.40
    float trunk_length = varied_spec.height * trunk_ratio;
    float trunk_thickness = varied_spec.trunk_diameter * random_variance(1.4f, 0.2f);

    // Trunk sits on the ground surface (root plate is now below ground)
    float plate_height = 0.25f; // Must match generate_root_system
    // The plate SITS ON the turtle now (bottom at ground_z), so its top is a
    // full plate_height above ground rather than at it. This still read
    // ground_z, which left the trunk — and through it all four first-level
    // branches — 0.25 m below the plate top they bond to, on bonds declaring
    // target_distance = 0. The KG path at root_plate_top_z below already had
    // this right; only the direct path was stale.
    float root_plate_top_z = ground_z + plate_height;

    Particle trunk = create_branch_particle(
        world_x, world_y, root_plate_top_z + trunk_length * 0.5f,
        trunk_length, trunk_thickness,
        0.0f, 90.0f, // Pointing straight up
        varied_spec, varied_spec.branch_depth);

    // Attach trunk to root plate with OrganicGluon
    auto trunk_gluon = std::make_unique<OrganicGluon>();
    trunk_gluon->offset_a = Vec3(0.0f, 0.0f, plate_height * 0.5f);  // Top of root plate
    trunk_gluon->offset_b = Vec3(0.0f, 0.0f, -trunk_length * 0.5f); // Bottom of trunk
    trunk_gluon->target_distance = 0.0f;
    // Large contact area for stability (root system)
    trunk_gluon->contact_area = trunk_thickness * trunk_thickness * 4.0f;
    // Force law derived at registration from materials (Phase C).

    result.trunk_id = physics_->add_particle_with_gluon_to(
        root_plate_id, trunk, std::move(trunk_gluon));
    result.branch_ids.push_back(result.trunk_id);
    result.total_segments++;

    std::cout << "  Trunk: id=" << result.trunk_id << " mass=" << trunk.GetMass()
              << "kg length=" << trunk_length << "m" << std::endl;

    // ========================================================================
    // STEP 3: Generate branches (same as generate_tree_on_floor)
    // ========================================================================
    float trunk_top_z = root_plate_top_z + trunk_length;

    float initial_elevation = random_variance(45.0f, 15.0f);
    float initial_direction = random_variance(0.0f, 180.0f);

    int num_main_branches = std::max(4, varied_spec.branches_per_split + 2);
    int effective_depth = std::max(2, varied_spec.branch_depth - 2);

    for (int i = 0; i < num_main_branches; ++i)
    {
        float base_angle = (360.0f / num_main_branches) * i;
        float angle_offset = base_angle + random_variance(0.0f, varied_spec.angle_variance);

        float branch_direction = initial_direction + angle_offset;
        float branch_elevation = initial_elevation + random_variance(0.0f, 15.0f);
        float branch_length = varied_spec.height * random_variance(0.5f, 0.15f);
        float branch_thickness = trunk_thickness * random_variance(0.7f, 0.15f);

        generate_branch(
            result.trunk_id,
            world_x, world_y, trunk_top_z, // trunk is upright: tip x,y = centre x,y
            branch_direction,
            branch_elevation,
            branch_length,
            branch_thickness,
            effective_depth,
            varied_spec,
            result);
    }

    std::cout << "  Total segments: " << result.total_segments
              << ", roots: " << result.root_ids.size()
              << ", leaves: " << result.leaf_ids.size() << std::endl;

    // Bind all particles to KG entity for cleanup when tree is removed
    {
        auto particles = particles_->lock_particles_for_read();

        // Bind root plate
        if (result.root_plate_id >= 0 && static_cast<size_t>(result.root_plate_id) < particles.size())
        {
            kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, result.root_plate_id);
            kg_->setKGParticleData(kg_id, particles[result.root_plate_id]);
        }

        // Bind roots
        for (int root_id : result.root_ids)
        {
            if (root_id >= 0 && static_cast<size_t>(root_id) < particles.size())
            {
                kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, root_id);
                kg_->setKGParticleData(kg_id, particles[root_id]);
            }
        }

        // Bind trunk
        if (result.trunk_id >= 0 && static_cast<size_t>(result.trunk_id) < particles.size())
        {
            kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, result.trunk_id);
            kg_->setKGParticleData(kg_id, particles[result.trunk_id]);
        }

        // Bind branches
        for (int branch_id : result.branch_ids)
        {
            if (branch_id >= 0 && static_cast<size_t>(branch_id) < particles.size())
            {
                kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, branch_id);
                kg_->setKGParticleData(kg_id, particles[branch_id]);
            }
        }

        // Bind leaves
        for (int leaf_id : result.leaf_ids)
        {
            if (leaf_id >= 0 && static_cast<size_t>(leaf_id) < particles.size())
            {
                kg::KGParticleID kg_id = kg_->createKGParticle(result.entity_id, leaf_id);
                kg_->setKGParticleData(kg_id, particles[leaf_id]);
            }
        }
    }

    return result;
}

// ============================================================================
// RECURSIVE BRANCH GENERATION
// ============================================================================

int PhysicsTreeGenerator::generate_branch(
    int parent_id,
    float parent_top_x,
    float parent_top_y,
    float parent_top_z,
    float direction_angle,
    float elevation_angle,
    float length,
    float thickness,
    int depth,
    const TreeSpec &spec,
    PhysicsTreeResult &result)
{
    // Base case: too deep or too thin - stop branching
    if (depth <= 0 || thickness < 0.02f)
    {
        return -1;
    }

    // Calculate direction vector
    float rad_h = direction_angle * M_PI / 180.0f;
    float rad_v = elevation_angle * M_PI / 180.0f;
    float dir_x = std::cos(rad_h) * std::cos(rad_v);
    float dir_y = std::sin(rad_h) * std::cos(rad_v);
    float dir_z = std::sin(rad_v);

    // THE PARENT'S ATTACHMENT POINT IS A POINT, AND IT ARRIVES AS ONE.
    //
    // This used the parent's CENTRE x and y with the parent's TOP z, because
    // the caller only ever tracked the tip's z ("branch_top_z = centre + dir *
    // half") and there was no branch_top_x or _y anywhere in the file. For an
    // upright segment the tip sits directly above the centre and nothing is
    // lost; for a TILTED one the tip is displaced horizontally by
    // sin(tilt) * half_length and that displacement was simply dropped.
    //
    // Measured, test_branch_placement_ladder, strain at birth (1.0 = at rest):
    //   rung 1, upright parent, child at 0..90 deg  -> 1.0000 at every angle
    //   rung 2, parent tilted 0/15/30/45/60 deg     -> 1.0000, 0.8855,
    //                                                  0.7559, 0.6190, 0.4861
    // Rung 1 green with rung 2 red isolates it to the PARENT's tilt, and the
    // error tracks sin(tilt) * parent_half_length exactly. In the full tree
    // that is 75 of 153 bonds born taut against a 2.0x tear ratio: the trunk
    // is upright and fine, every branch-off-branch is not.
    //
    // Fixed by carrying the tip as a POINT through the recursion, derived once
    // as centre + dir * half on all three axes. An earlier attempt recomputed
    // the attachment from the particle's Euler rotation instead, which made
    // things worse (mean strain 1.0821 -> 1.1376): the caller's z was already
    // correct, so that introduced a second, drifting derivation of a number
    // that only needed to be passed along.
    //
    // The general law, not specific to trees: a child placed relative to a
    // parent must be placed relative to the parent's ATTACHMENT POINT, in
    // world space, on all three axes.
    float branch_center_x = parent_top_x + dir_x * length * 0.5f;
    float branch_center_y = parent_top_y + dir_y * length * 0.5f;
    float branch_center_z = parent_top_z + dir_z * length * 0.5f;

    // Create branch particle
    Particle branch = create_branch_particle(
        branch_center_x, branch_center_y, branch_center_z,
        length, thickness,
        direction_angle, elevation_angle,
        spec, depth);

    // Create OrganicGluon to attach branch to parent
    auto gluon = std::make_unique<OrganicGluon>();

    // Offset on parent: top center (we're attaching to end of parent)
    // For trunk/branches pointing up, this is +Z
    // For angled branches, we need to calculate the actual attachment point
    float parent_half_length;
    {
        auto particles = particles_->lock_particles_for_read();
        parent_half_length = particles[parent_id].thickness * 0.5f;
    }

    // Parent attachment: top of parent segment
    gluon->offset_a = Vec3(0.0f, 0.0f, parent_half_length);

    // Child attachment: bottom of this branch, IN THE BRANCH'S OWN FRAME.
    //
    // This used to be built from dir_*, the branch's WORLD growth direction,
    // under a comment claiming local coordinates. offset_a above is genuinely
    // local (+Z is the parent's own axis), so one gluon carried two different
    // frames and every consumer that differences them read a quantity existing
    // in neither.
    //
    // Measured, test_foliage_stays_attached first tear, both bodies stationary:
    //   off_a=(0,0,1.94975)  = thick_a/2 exactly, correct and local
    //   off_b=(0.306,-1.084,-0.154), |off_b| = thick_b/2 exactly — right
    //     LENGTH, wrong FRAME; its x and y matched the world vector exactly,
    //     which is what gave it away
    // get_segment_length() then read 2.387 where the true rest separation was
    // 1.968, and rotating the offsets instead read 0.931 (commit 1fc74be, a
    // failed experiment kept for its numbers). Neither can be right while the
    // two offsets live in different frames.
    //
    // A segment's local +Z runs along its grown direction — the convention
    // direction_to_euler establishes and the solver's rotate_full relies on —
    // so the end nearest the parent is simply -length/2 along local Z. The
    // rotation carries the direction; the offset must not carry it too.
    gluon->offset_b = Vec3(0.0f, 0.0f, -length * 0.5f);

    gluon->target_distance = 0.0f;
    gluon->contact_area = calculate_contact_area(thickness);

    // Add branch with gluon attachment
    // KEEP THE POSITION WE COMPUTED. branch_center_* was derived from the
    // parent's actual direction; the gluon's offset_a runs along world +Z, so
    // letting the gluon place this puts an angled parent's child nowhere near
    // the tip it was drawn from. Issue #38.
    // PLACE_DEBUG=1: does the position we computed SURVIVE the call?
    // Three placement fixes in a row made the frame-zero audit worse rather
    // than better, and the worst strain never moved at all (1.8116 through all
    // three). Both are explained if this call re-places the particle and the
    // number we compute is discarded. Measure it instead of assuming either way.
    const float want_x = branch.x, want_y = branch.y, want_z = branch.z;
    int branch_id = physics_->add_particle_with_gluon_to(parent_id, branch, std::move(gluon),
                                                         /*use_config_position=*/!g_legacy_placement);
    {
        static const bool place_dbg = std::getenv("PLACE_DEBUG") != nullptr;
        if (place_dbg && branch_id >= 0)
        {
            auto pv = particles_->lock_particles_for_read();
            const Particle &got = pv[branch_id];
            const float dx = got.x - want_x, dy = got.y - want_y, dz = got.z - want_z;
            const float moved = std::sqrt(dx * dx + dy * dy + dz * dz);
            const Particle &par = pv[parent_id];
            std::cout << "[PLACE] depth=" << depth
                      << " P" << parent_id << "->P" << branch_id
                      << " wanted=(" << want_x << "," << want_y << "," << want_z << ")"
                      << " got=(" << got.x << "," << got.y << "," << got.z << ")"
                      << " MOVED=" << moved
                      << " | parent rot=(" << par.rotation_x << "," << par.rotation_y
                      << "," << par.rotation_z << ") thick=" << par.thickness
                      << " | child rot=(" << got.rotation_x << "," << got.rotation_y
                      << "," << got.rotation_z << ") len=" << length
                      << std::endl;
        }
    }
    result.branch_ids.push_back(branch_id);
    result.total_segments++;

    // Calculate branch end position (for child branches)
    float branch_top_x = branch_center_x + dir_x * length * 0.5f;
    float branch_top_y = branch_center_y + dir_y * length * 0.5f;
    float branch_top_z = branch_center_z + dir_z * length * 0.5f;

    // ========================================================================
    // SIDE BRANCHES - DISABLED for simpler grandious silhouette
    // ========================================================================
    // Side branches removed to prevent crown collision/complexity
    // Trees now rely solely on end-branching for cleaner structure

    // ========================================================================
    // LEAVES AT BRANCH TIPS - GRANDIOUS WIDE SPREAD
    // ========================================================================
    // Add leaves at terminal branches with WIDER sphere distribution like kinematic

    bool is_terminal = (depth == 1);                    // This branch won't have children
    float leaf_threshold = spec.trunk_diameter * 0.25f; // Increased threshold

    if (is_terminal || thickness < leaf_threshold)
    {
        // GRANDIOUS: More leaves per cluster (6-10 instead of 4-8)
        int num_leaves = 6 + (rng_state_ % 5); // 6-10 leaves per tip
        std::cout << "[PhysicsTreeGenerator] Adding " << num_leaves << " leaves at depth=" << depth << std::endl;
        rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;

        // GRANDIOUS: Larger leaves (0.25-0.45m instead of 0.1-0.3m)
        const float base_leaf_width = 0.35f;
        const float leaf_width_variance = 0.10f;

        // Branch tip position
        float tip_x = branch_center_x + dir_x * length * 0.5f;
        float tip_y = branch_center_y + dir_y * length * 0.5f;
        float tip_z = branch_top_z;

        // GRANDIOUS: Use SPHERE distribution like kinematic (wider 1.0-2.0m spread)
        for (int i = 0; i < num_leaves; ++i)
        {
            // Spherical distribution with random angles (like kinematic)
            float theta = random_variance(0.0f, 180.0f); // Random horizontal angle
            float phi = random_variance(0.0f, 90.0f);    // Random vertical angle (0-180°)
            // GRANDIOUS: Wider spread radius (1.0-2.0m like kinematic instead of 0.12-0.18m)
            float offset_radius = random_variance(1.5f, 0.5f); // 1.0-2.0m radius

            float theta_rad = theta * M_PI / 180.0f;
            float phi_rad = phi * M_PI / 180.0f;

            float leaf_x = tip_x + offset_radius * std::sin(phi_rad) * std::cos(theta_rad);
            float leaf_y = tip_y + offset_radius * std::sin(phi_rad) * std::sin(theta_rad);
            float leaf_z = tip_z + offset_radius * std::cos(phi_rad);

            Particle leaf;
            leaf.x = leaf_x;
            leaf.y = leaf_y;
            leaf.z = leaf_z;
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
            float this_leaf_width = random_variance(base_leaf_width, leaf_width_variance); // 0.25-0.45m
            leaf.width = this_leaf_width;
            leaf.height = this_leaf_width * 0.7f;
            leaf.thickness = 0.02f;    // 2cm thin (matches kinematic)
            leaf.facing_angle = theta; // Random orientation (theta is now in degrees)
            leaf.reflectivity = 0.20f;

            // DYNAMIC LEAVES: Follow parent branch via gluon constraint
            leaf.material_density = 100.0f; // Light leaf material (100 kg/m³)
            // Mass will auto-calculate from dimensions and material_density when particle is added
            leaf.material_strength = 1000.0f; // Weak - leaves tear easily

            // COLLISION CHECK: Retry placement until non-overlapping position found
            // This prevents leaf-to-leaf overlap that causes physics oscillation
            // Use large gap (0.8m) because gluons can pull leaves toward each other
            // and the contact solver needs margin to avoid the gluon+contact conflict
            // GAP IS 2 cm, NOT 0.8 m. The requirement is that leaves do not
            // OVERLAP; 0.8 m between 0.35 m leaves is 2x their own width of
            // empty space, which no canopy can satisfy, so 30 of 115 leaves
            // were being dropped outright. That was invisible while
            // add_particle_with_gluon_to overwrote the position anyway (issue
            // #38): the search failed, the leaf was skipped, and the ones that
            // "succeeded" got moved on top of each other regardless.
            //
            // With the position now honoured, this number decides canopy
            // density directly. Attempts raised to 60 because a tight gap needs
            // more tries in a crowded crown, and a dropped leaf is lost detail.
            if (!particles_->try_place_with_retry(leaf, 60, 1.5f, 0.02f))
            {
                // All attempts failed - skip this leaf rather than create overlap
                std::cout << "[PhysicsTreeGenerator] WARNING: Skipping leaf - no valid position found"
                          << std::endl;
                continue;
            }

            // Create gluon to attach leaf to parent branch
            // target_distance = actual distance from branch tip to leaf's final position
            float actual_dx = leaf.x - tip_x;
            float actual_dy = leaf.y - tip_y;
            float actual_dz = leaf.z - tip_z;
            float actual_distance = std::sqrt(actual_dx * actual_dx + actual_dy * actual_dy + actual_dz * actual_dz);

            auto leaf_gluon = std::make_unique<OrganicGluon>();
            leaf_gluon->particle_a = branch_id;
            leaf_gluon->particle_b = 0;                       // Will be set by add_particle_with_gluon_to
            leaf_gluon->offset_a = Vec3(0, 0, length * 0.5f); // Branch tip
            leaf_gluon->offset_b = Vec3(0, 0, 0);             // Leaf center
            leaf_gluon->target_distance = actual_distance;    // Maintain placed distance (may have been jittered)
            // Force law derived at registration from materials (Phase C).
            leaf_gluon->contact_area = 0.0001f;   // Tiny stem (1 cm²)
            leaf_gluon->angular_stiffness = 0.0f; // Leaves swing freely
            leaf_gluon->enable_angular_constraint = false;

            // KEEP THE POSITION try_place_with_retry FOUND. Without this the
            // search above is pure waste: every leaf on the branch would be
            // placed at the branch tip, on top of each other. Issue #38.
            int leaf_id = physics_->add_particle_with_gluon_to(
                branch_id, leaf, std::move(leaf_gluon),
                /*use_config_position=*/!g_legacy_placement);
            result.leaf_ids.push_back(leaf_id);
        }
    }

    // ========================================================================
    // END BRANCHES - SIMPLIFIED for grandious silhouette
    // ========================================================================
    // Only 1-2 branches per split to prevent crown collision

    // Drop more aggressively per depth - outer branches spread WIDE
    float elevation_drop = random_variance(25.0f, 10.0f); // 15-35° drop per level
    float child_elevation = elevation_angle - elevation_drop;
    // Clamp - allow more horizontal spread
    child_elevation = std::max(10.0f, std::min(60.0f, child_elevation));

    // SIMPLIFIED: Only 1-2 branches (not full spec.branches_per_split)
    int num_children = std::min(2, spec.branches_per_split);

    for (int i = 0; i < num_children; ++i)
    {
        // WIDER spread angle - branches go opposite directions
        float angle_offset;
        if (num_children == 2)
        {
            // Two branches spread 120-180° apart (wide V shape)
            float spread = random_variance(150.0f, 30.0f); // 120-180° spread
            angle_offset = (i == 0) ? -spread * 0.5f : spread * 0.5f;
        }
        else
        {
            angle_offset = random_variance(0.0f, 45.0f); // Single branch with slight offset
        }

        float child_direction = direction_angle + angle_offset;
        // Shorter child branches to reduce overlap
        float child_length = length * spec.length_ratio * random_variance(0.8f, 0.2f);
        // Thinner branches
        float child_thickness = thickness * spec.thickness_ratio * random_variance(0.8f, 0.15f);
        // More horizontal variance
        float varied_elevation = child_elevation + random_variance(0.0f, 15.0f);

        generate_branch(
            branch_id,
            branch_top_x, branch_top_y, branch_top_z,
            child_direction,
            varied_elevation,
            child_length,
            child_thickness,
            depth - 1,
            spec,
            result);
    }

    return branch_id;
}

// ============================================================================
// PARTICLE CREATION
// ============================================================================

Particle PhysicsTreeGenerator::create_branch_particle(
    float x, float y, float z,
    float length, float thickness,
    float angle_h, float angle_v,
    const TreeSpec &spec,
    int depth)
{
    Particle p;
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.vz = 0.0f;

    // Visual properties (same as TreeGenerator)
    p.r = spec.trunk_r;
    p.g = spec.trunk_g;
    p.b = spec.trunk_b;
    p.a = 1.0f;

    // Shape: BOX with length along thickness dimension
    p.shape = ParticleShape::BOX;
    p.width = thickness;
    p.height = thickness;
    p.thickness = length;

    // Rotation to align with branch direction
    float rad_h = angle_h * M_PI / 180.0f;
    float rad_v = angle_v * M_PI / 180.0f;
    float dir_x = std::cos(rad_h) * std::cos(rad_v);
    float dir_y = std::sin(rad_h) * std::cos(rad_v);
    float dir_z = std::sin(rad_v);

    // A TWIST MUST BE ABOUT THE BRANCH'S OWN AXIS.
    //
    // rotation_z used to be `random_variance(0.0f, 0.1f)` — a "slight twist"
    // for visual variety. But the engine composes Euler angles X then Y then
    // Z, so that value is applied LAST, ABOUT WORLD VERTICAL. For an upright
    // branch that happens to be a roll about its own length and is harmless.
    // For a branch pointing sideways it is not a roll at all: it swings the
    // whole branch like a clock hand.
    //
    // That matters because the branch's bond is anchored to its ENDS, and the
    // anchors move with the rotation. Placement puts the branch where `dir`
    // says; the twist then rotates it somewhere slightly else; the bond's rest
    // geometry follows the rotation and no longer matches the placement. The
    // branch is born stretched.
    //
    // Measured (test_tree_bonds_born_at_rest, frame zero, no physics has run):
    // 75 of 153 bonds born taut, worst 1.81x against a 2.0x tear ratio. The
    // twist is INVISIBLE — a rolled cylinder looks identical — and the old
    // solver absorbed the strain, so nothing ever looked wrong until split
    // impulse stopped the solver inventing energy to hide it. Issue #57.
    //
    // Three placement fixes failed before this was found, because they were
    // correcting one side of an equation whose other side is randomised.
    //
    // The fix is not to delete the twist: a real branch does have roll. It is
    // to apply the roll about the branch's OWN axis, before the direction
    // rotation, so it stays a roll at every angle and the ends do not move.
    // Composed X then Y then Z, a rotation about local Z applied FIRST is a
    // spin about the segment's length, which is exactly what "twist" means.
    const float twist = random_variance(0.0f, 0.1f);
    p.rotation_x = -std::asin(dir_y);
    p.rotation_y = std::atan2(dir_x, dir_z);
    p.rotation_z = 0.0f;
    // The branch's own geometry is a cylinder about its local +Z, so a roll
    // about that axis changes nothing the bond can see. Carried on the
    // particle's own spin field rather than the orientation that defines
    // where its ends are.
    p.facing_angle = twist;

    p.reflectivity = 0.3f;
    p.pattern_id = 1; // PATTERN_WOOD for bark texture

    // ========================================================================
    // PHYSICS PROPERTIES (the key difference from TreeGenerator!)
    // ========================================================================

    // Material properties for wood
    p.material_density = spec.trunk_diameter > 0.5f ? 750.0f : 600.0f; // Oak vs lighter wood
    p.material_strength = 50e6f;                                       // 50 MPa (typical wood strength)

    // Mass will auto-calculate from dimensions and material_density when particle is added to system

    // Trees start at-rest (static scenery). They're gluoned to floor tiles which are
    // also at_rest. Contact wake (>1 m/s collision) can wake them if hit hard enough.
    // This prevents trees from falling during settling and cascading wake events.
    p.is_at_rest = true;

    return p;
}

float PhysicsTreeGenerator::calculate_mass(float length, float thickness, float density)
{
    // Approximate branch as cylinder
    float radius = thickness * 0.5f;
    float volume = M_PI * radius * radius * length;
    return volume * density;
}

float PhysicsTreeGenerator::calculate_contact_area(float thickness)
{
    // Cross-sectional area of branch
    float radius = thickness * 0.5f;
    return M_PI * radius * radius;
}

// ============================================================================
// ROOT SYSTEM GENERATION
// ============================================================================

int PhysicsTreeGenerator::generate_root_system(
    float world_x, float world_y, float ground_z,
    float tree_height, float trunk_thickness,
    PhysicsTreeResult &result)
{
    // ========================================================================
    // ROOT PLATE - Central anchor for the root system
    // ========================================================================
    // A flat BOX partially underground, providing bulk stability
    // Dimensions proportional to trunk thickness

    // Plate diameter should be MUCH larger than trunk for visible root spread
    // Use tree height as reference: plate radius = 30% of height for stability
    float plate_diameter = std::max(trunk_thickness * 4.0f, tree_height * 0.3f);
    float plate_height = 0.25f; // 25cm tall - thicker for visibility
    // Root plate fully below ground — top face well under floor surface
    // NOTHING GOES BELOW THE TURTLE. TURTLE_Z = 0 is an absolute world floor,
    // and enforce_turtle_boundary() lifts anything beneath it EVERY SUBSTEP,
    // forever, as a raw position correction with no energy accounting.
    //
    // This plate was placed underground because roots are underground — first
    // at ground_z - plate_height (bottom at -0.375), then at
    // ground_z - plate_height*0.5 (bottom at -0.25). Both put the plate and
    // its four radial roots permanently in violation, so the turtle lifted
    // them every substep and gravity pulled them back, for free.
    //
    // Measured with the energy ledger (ENERGY_LEDGER=1), per substep:
    //   d_solve     -19 to -194     the constraint solver dissipates
    //   d_angular    0              neutral
    //   d_positions -1 to -18       integration dissipates
    //   d_turtle    +30.6 EVERY SUBSTEP, +15797 on the first
    // Three phases lose energy, one creates it, and it was the boundary
    // fighting geometry that could never satisfy it. That energy reached the
    // canopy through the bonds, which is how leaves ended up at 120 m.
    //
    // The plate now SITS ON the turtle: bottom exactly at ground_z, so the
    // boundary has nothing to correct. Roots being visually underground is
    // not worth an invariant that says the world has a floor.
    float plate_center_z = ground_z + plate_height * 0.5f;

    Particle root_plate = {};
    root_plate.x = world_x;
    root_plate.y = world_y;
    root_plate.z = plate_center_z; // Center of plate above ground
    // A TREE IS ROOTED. The plate was DYNAMIC and unsupported: it sat with its
    // bottom exactly on the turtle, and the boundary only corrects a body whose
    // bottom goes BELOW it, so nothing held it up. It fell, and it dragged the
    // whole tree down with it — measured, P0 vel 0 -> -0.0817 -> -0.0996,
    // accelerating, while trunk, branch and leaf all descended with it. That is
    // why every leaf "drifted" ~7 m and none of them relative to each other:
    // the canopy was not scattering, the tree was sinking.
    //
    // IT WAS STANDING ON A BUG. Before today the plate was placed BELOW the
    // turtle, so the boundary lifted it every substep — and that illegal lift
    // was the only thing supporting the tree. Removing the buried placement
    // (correctly) removed the accidental support with it.
    //
    // KINEMATIC is one of the three sanctioned immobility mechanisms and is
    // exactly what OrganicGenerator already does for a grass blade's base.
    // Roots hold a tree up; now they do.
    root_plate.solver_mode = ParticleSolverMode::KINEMATIC;
    root_plate.is_at_rest = true;

    root_plate.vx = 0.0f;
    root_plate.vy = 0.0f;
    root_plate.vz = 0.0f;

    // Dark bark color (root plate is mostly underground)
    root_plate.r = 0.3f;
    root_plate.g = 0.2f;
    root_plate.b = 0.1f;
    root_plate.a = 1.0f;

    // Flat cylindrical shape (approximated as box)
    root_plate.shape = ParticleShape::BOX;
    root_plate.width = plate_diameter;
    root_plate.height = plate_diameter;
    root_plate.thickness = plate_height;

    // Wood material properties
    root_plate.material_density = 800.0f; // Dense root wood
    root_plate.material_strength = 50e6f;
    root_plate.reflectivity = 0.2f;
    root_plate.friction = 0.7f; // Roots grip the ground

    // Add root plate as free particle (rests on turtle via collision)
    // Particle will participate in physics automatically through collision detection
    int plate_id = particles_->add_particle(root_plate);
    result.root_plate_id = plate_id;
    result.total_segments++;

    std::cout << "  Root plate: id=" << plate_id
              << " diameter=" << plate_diameter << "m"
              << " at z=" << root_plate.z << std::endl;

    // ========================================================================
    // PRIMARY ROOTS - Radial spread from root plate
    // ========================================================================
    //
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // !!  HISTORICAL WARNING, AND IT IS NO LONGER TRUE                    !!
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //
    // WHAT THIS SAID, AND WHY IT WAS RIGHT IN 2024: physics computed
    // collision bounds from width/height/thickness as WORLD-AXIS-ALIGNED
    // extents and ignored rotation entirely, so a rotated body collided
    // as though it were not rotated.
    //
    // WHAT IS TRUE NOW: a rotated BOX collides as an ORIENTED box.
    // physics_system_v4.cpp:1025 takes the oriented bounds for any BOX
    // where box_particle_is_rotated() holds, and its own comment gives
    // the reason: "a ROTATED box's raw extents under-cover its world
    // span (a tilted blade's length leaves Z and enters Y), so its
    // bounds come from the oriented box instead - exact for a box,
    // never loose." Unrotated boxes keep the raw arithmetic to the bit.
    // ELLIPSOID is the remaining approximation, colliding as its
    // enclosing AABB; that one is tracked as invariant debt against
    // INV-12 rather than as a rule.
    //
    // The segments below ARE rotated boxes, so they get oriented
    // bounds. The sizing convention here is kept anyway: it is correct
    // regardless, and it is what the 2024 session paid for.
    //
    // WHAT WENT WRONG (2024-12 debugging session, hours wasted):
    //   - We set root.thickness = root_length (1.25m, the root's LENGTH)
    //   - We set rotation_x/y to orient the root horizontally
    //   - Physics computed: bottom = 0.125 - 1.25/2 = -0.5m (BELOW TURTLE!)
    //   - Turtle collision pushed roots UP to z = 0.625m
    //   - Roots appeared to "float above" the slab instead of at its sides
    //
    // THE FIX:
    // - Set width/height/thickness to match WORLD axes, not local particle axes
    // - For horizontal East/West roots: width=length, height=cross, thickness=cross
    // - For horizontal North/South roots: width=cross, height=length, thickness=cross
    // - Do not rely on rotation_x/y to orient collision bounds;
    //   collision extents are determined by width/height/thickness.
    //
    // FUTURE PREVENTION:
    //   - Always think "what is the WORLD Z extent?" when setting thickness
    //   - If particle is horizontal, thickness should be SMALL (cross-section)
    //   - Test with simple axis-aligned particles FIRST before adding complexity
    //   - See test_physics_tree_roots.cpp Case 0 for baseline test pattern
    //
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //
    // CANONICAL TABLE: which shapes get oriented collision treatment and
    // which stay axis-aligned lives in ONE place,
    // include/logosphere/physics/narrow_phase.h, pinned by
    // tests/test_collision_bounds_rotation.cpp so it cannot rot in
    // silence. When this history note and that table disagree, the
    // table is the truth.

    int num_roots = 3 + (rng_state_ % 2);    // 3-4 roots
    float root_length = tree_height * 0.25f; // Each root = 25% of height
    // A ROOT CANNOT BE THICKER THAN THE PLATE IT GROWS OUT OF.
    // Roots attach at the plate's side faces, which are at the plate's CENTRE
    // height, so a root whose Z cross-section exceeds plate_height hangs below
    // the plate's underside — and with the plate sitting on the turtle, that
    // puts the root under the world floor. Caught by the turtle guard in
    // ParticleSystem::add_particle: z=0.125 thickness=0.402 => bottom=-0.076.
    // Clamping is geometry, not tuning: the plate is the root's socket.
    float root_thickness = std::min(trunk_thickness * 0.4f, plate_height);

    // BOX has 4 side surfaces: +X, -X, +Y, -Y
    // Place one root on each side surface
    num_roots = 4;
    const float cardinal_angles[4] = {0.0f, 90.0f, 180.0f, 270.0f}; // +X, +Y, -X, -Y

    for (int i = 0; i < num_roots; ++i)
    {
        // Use cardinal direction + small variance
        float radial_angle = cardinal_angles[i] + random_variance(0.0f, 5.0f);

        // Radial direction in XY plane (horizontal outward from plate)
        float rad_h = radial_angle * M_PI / 180.0f;
        float radial_x = std::cos(rad_h);
        float radial_y = std::sin(rad_h);

        // Root direction: purely horizontal (axis-aligned boxes don't support rotation)
        float dir_x = radial_x;
        float dir_y = radial_y;
        float dir_z = 0.0f; // Horizontal - physics can't handle rotated boxes

        // Root particle config
        Particle root = {};
        root.vx = 0.0f;
        root.vy = 0.0f;
        root.vz = 0.0f;

        // BRIGHT BLUE for roots - unmistakably visible for debugging
        root.r = 0.0f;
        root.g = 0.0f;
        root.b = 1.0f;
        root.a = 1.0f;

        // BOX collision dimensions must match WORLD axes (rotation is not applied
        // to collision bounds).
        // For nearly horizontal roots at cardinal directions:
        //   X-axis roots (East/West): width=length, height=cross, thickness=cross
        //   Y-axis roots (North/South): width=cross, height=length, thickness=cross
        root.shape = ParticleShape::BOX;
        if (std::abs(radial_x) > std::abs(radial_y))
        {
            // X-axis dominant (East/West)
            root.width = root_length;        // X = root length
            root.height = root_thickness;    // Y = cross-section
            root.thickness = root_thickness; // Z = cross-section (small!)
        }
        else
        {
            // Y-axis dominant (North/South)
            root.width = root_thickness;     // X = cross-section
            root.height = root_length;       // Y = root length
            root.thickness = root_thickness; // Z = cross-section (small!)
        }

        // NO rotation needed - dimensions are axis-aligned
        root.rotation_x = 0.0f;
        root.rotation_y = 0.0f;
        root.rotation_z = 0.0f;

        // Material
        root.material_density = 800.0f;
        root.material_strength = 50e6f;
        root.reflectivity = 0.2f;
        root.friction = 0.7f;

        // ================================================================
        // CALCULATE ROOT POSITION: Plate side face center + outward offset
        // ================================================================
        // Plate is at (world_x, world_y) with no rotation
        // Side face centers are at: plate_center ± (half_diameter, 0, 0) or ± (0, half_diameter, 0)
        float plate_half_w = plate_diameter * 0.5f;

        // Determine which side face based on radial direction (dominant axis)
        float face_offset_x = 0.0f, face_offset_y = 0.0f;
        if (std::abs(radial_x) > std::abs(radial_y))
        {
            // X face (±X)
            face_offset_x = (radial_x > 0) ? plate_half_w : -plate_half_w;
        }
        else
        {
            // Y face (±Y)
            face_offset_y = (radial_y > 0) ? plate_half_w : -plate_half_w;
        }

        // Face center in world coordinates
        float face_center_x = world_x + face_offset_x;
        float face_center_y = world_y + face_offset_y;
        // The plate's side faces are at the plate's OWN centre height, and
        // offset_a below carries z = 0 to say exactly that. This read
        // ground_z + plate_height*0.5 — a guess at where the plate centre is
        // rather than where it actually is — and was wrong by 0.375 m, which
        // is precisely the gap the four root bonds were born with.
        float face_center_z = plate_center_z;

        // Root center = face center + half root length outward
        float root_center_x = face_center_x + dir_x * root_length * 0.5f;
        float root_center_y = face_center_y + dir_y * root_length * 0.5f;
        float root_center_z = face_center_z + dir_z * root_length * 0.5f;

        root.x = root_center_x;
        root.y = root_center_y;
        root.z = root_center_z;

        // ================================================================
        // CREATE GLUON: Attach root to plate's side face
        // ================================================================
        // NOTE: add_particle_with_gluon_to() uses offsets in WORLD coordinates
        // to compute initial particle position: pos_b = pos_a + offset_a - offset_b
        auto root_gluon = std::make_unique<OrganicGluon>();
        // Offset A: Attachment point on plate (side face center in plate's local coords)
        // Plate has no rotation, so local = world
        root_gluon->offset_a = Vec3(face_offset_x, face_offset_y, 0.0f); // Side face at plate center Z
        // Offset B: Attachment point on root (inner end in WORLD coords)
        // Inner end is at root_center - half_length in the direction of the root
        // In world coords: inner_end = root_center - dir * root_length/2
        // So offset from root center to inner end = -dir * root_length/2
        float inner_offset_x = -dir_x * root_length * 0.5f;
        float inner_offset_y = -dir_y * root_length * 0.5f;
        float inner_offset_z = -dir_z * root_length * 0.5f;
        root_gluon->offset_b = Vec3(inner_offset_x, inner_offset_y, inner_offset_z);
        root_gluon->target_distance = 0.0f;                         // Touch attachment
        root_gluon->contact_area = root_thickness * root_thickness; // Root cross-section
        // Force law derived at registration from materials (Phase C).

        // KEEP THE COMPUTED POSITION. root_center_x/y/z are worked out above
        // from the plate face and the root's direction, assigned to root.x/y/z,
        // and were then thrown away by the offset-derived placement. Same bug
        // as the leaves and branches (issue #38), same fix.
        int root_id = physics_->add_particle_with_gluon_to(
            plate_id, root, std::move(root_gluon),
            /*use_config_position=*/!g_legacy_placement);

        if (root_id >= 0)
        {
            result.root_ids.push_back(root_id);
            result.total_segments++;

            // MILLIMETER PRECISION DEBUG: Log actual position with full dimensions
            auto particles = particles_->lock_particles_for_read();
            const Particle &p = particles[root_id];
            const Particle &plate = particles[plate_id];

            std::cout << "  ┌─[ROOT " << i << "] id=" << root_id << " (GLUONED to plate)" << std::endl;
            std::cout << "  │ GLUON OFFSETS:" << std::endl;
            std::cout << "  │   offset_a=(" << face_offset_x << ", " << face_offset_y << ", 0)" << std::endl;
            std::cout << "  │   offset_b=(" << inner_offset_x << ", " << inner_offset_y << ", " << inner_offset_z << ")" << std::endl;
            std::cout << "  │   dir=(" << dir_x << ", " << dir_y << ", " << dir_z << ")" << std::endl;
            std::cout << "  │ Face center=(" << face_center_x << ", " << face_center_y << ", " << face_center_z << ")" << std::endl;
            std::cout << "  │ PLATE: center=(" << plate.x << ", " << plate.y << ", " << plate.z << ")" << std::endl;
            std::cout << "  │        Z range: [" << (plate.z - plate.thickness * 0.5f) << " to " << (plate.z + plate.thickness * 0.5f) << "]" << std::endl;
            std::cout << "  │ ROOT:  center=(" << p.x << ", " << p.y << ", " << p.z << ")" << std::endl;
            std::cout << "  │        expected=(" << root_center_x << ", " << root_center_y << ", " << root_center_z << ")" << std::endl;

            // Verify root Z is near plate center (within plate Z range)
            float plate_z_min = plate.z - plate.thickness * 0.5f;
            float plate_z_max = plate.z + plate.thickness * 0.5f;
            bool z_ok = (p.z >= plate_z_min - 0.01f) && (p.z <= plate_z_max + 0.01f);
            std::cout << "  └─ ROOT Z=" << p.z * 1000 << "mm vs PLATE Z=[" << plate_z_min * 1000 << "," << plate_z_max * 1000 << "]mm: "
                      << (z_ok ? "OK" : "*** OUT OF RANGE ***") << std::endl;
        }
        else
        {
            std::cerr << "  [ROOT " << i << "] FAILED to create" << std::endl;
        }
    }

    std::cout << "  Plate edge at radius=" << (plate_diameter * 0.4f) << "m from center"
              << ", trunk at (0,0) with radius ~" << (trunk_thickness * 0.5f) << "m" << std::endl;
    std::cout << "  Roots: " << num_roots << " primary roots, length="
              << root_length << "m each" << std::endl;

    return plate_id;
}

// ============================================================================
// FLOOR TILE SEARCH
// ============================================================================

int PhysicsTreeGenerator::find_floor_tile_at(float world_x, float world_y)
{
    // Search for a floor tile that contains (world_x, world_y)
    // Returns particle ID if found, -1 otherwise
    // NOTE: Floor tiles identified by position (z<1m) and shape (flat box)

    auto particles = particles_->lock_particles_for_read();
    const size_t count = particles.size();

    for (size_t i = 0; i < count; ++i)
    {
        const Particle &p = particles[i];

        // Must be near ground level (Z center < 1m)
        if (p.z > 1.0f)
            continue;

        // Must be a flat box (thickness < width/height, typical for floor tiles)
        if (p.thickness > p.width || p.thickness > p.height)
            continue;

        // Check if (world_x, world_y) is within the tile's XY bounds
        float half_w = p.width * 0.5f;
        float half_h = p.height * 0.5f;

        if (world_x >= p.x - half_w && world_x <= p.x + half_w &&
            world_y >= p.y - half_h && world_y <= p.y + half_h)
        {
            return static_cast<int>(i);
        }
    }

    return -1; // No floor tile found
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

void PhysicsTreeGenerator::seed_rng(unsigned int seed)
{
    rng_state_ = seed;
}

float PhysicsTreeGenerator::random_variance(float base, float variance)
{
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;
    float normalized = (float)rng_state_ / 2147483648.0f;
    float offset = (normalized * 2.0f - 1.0f) * variance;
    return base + offset;
}

// ============================================================================
// PERMAWORLD: STORE TREE ENTITY IN KG (no particles created)
// ============================================================================

kg::EntityID PhysicsTreeGenerator::store_tree_entity(
    float world_x, float world_y, float ground_z,
    const TreeSpec &spec,
    float chunk_size)
{
    if (!kg_ || !kg_->isEnabled())
    {
        std::cerr << "[PhysicsTreeGenerator] ERROR: KG not enabled!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    // Collect all particle and gluon specs
    std::vector<Particle> particles;
    std::vector<GluonSpec> gluons;
    collect_tree_specs(world_x, world_y, ground_z, spec, particles, gluons);

    if (particles.empty())
    {
        std::cerr << "[PhysicsTreeGenerator] ERROR: No particles generated!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    // Create KG entity with chunk coordinates
    // Type is "PhysicsTree" (not "tree") - uses physics_tree_activator with gluon support
    kg::EntityID entity_id = kg_->createEntityAtPosition("PhysicsTree", world_x, world_y, chunk_size);
    if (entity_id == kg::INVALID_ENTITY)
    {
        std::cerr << "[PhysicsTreeGenerator] ERROR: Failed to create KG entity!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    // Store TreeSpec properties for regeneration (optional - for debugging)
    kg_->setProperty(entity_id, "height", std::to_string(spec.height));
    kg_->setProperty(entity_id, "trunk_diameter", std::to_string(spec.trunk_diameter));
    kg_->setProperty(entity_id, "branch_depth", std::to_string(spec.branch_depth));

    // Store particles in KG
    std::vector<kg::KGParticleID> kg_particle_ids;
    for (const auto &p : particles)
    {
        kg::KGParticleID kg_pid = kg_->createKGParticle(entity_id, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(kg_pid, p);
        kg_particle_ids.push_back(kg_pid);
    }

    // Store gluons in KG
    for (const auto &gs : gluons)
    {
        kg::KGGluonID gluon_id = kg_->createKGGluon(
            entity_id,
            kg_particle_ids[gs.parent_index],
            kg_particle_ids[gs.child_index]);
        kg_->setKGGluonData(gluon_id, gs.data);
    }

    std::cout << "[PhysicsTreeGenerator] Stored tree entity " << entity_id
              << " with " << particles.size() << " particles and "
              << gluons.size() << " gluons in KG" << std::endl;

    return entity_id;
}

// ============================================================================
// COLLECT TREE SPECS (build particles/gluons without creating them)
// ============================================================================

void PhysicsTreeGenerator::collect_tree_specs(
    float world_x, float world_y, float ground_z,
    const TreeSpec &spec,
    std::vector<Particle> &out_particles,
    std::vector<GluonSpec> &out_gluons)
{
    // Seed RNG for reproducible results
    seed_rng(spec.random_seed);

    // Apply natural variation (same as generate_tree_with_roots)
    TreeSpec varied_spec = spec;
    varied_spec.apply_natural_variation(0.3f, spec.random_seed);
    seed_rng(varied_spec.random_seed);

    // ========================================================================
    // ROOT PLATE - First particle (index 0)
    // ========================================================================
    float plate_diameter = std::max(varied_spec.trunk_diameter * 4.0f, varied_spec.height * 0.3f);
    float plate_height = 0.25f;
    float plate_center_z = ground_z + plate_height * 0.5f;

    Particle root_plate = {};
    root_plate.x = world_x;
    root_plate.y = world_y;
    root_plate.z = plate_center_z;
    // Same rooting on the KG/permaworld path — a streamed tree must stand up
    // too. See the note at the direct path above.
    root_plate.solver_mode = ParticleSolverMode::KINEMATIC;
    root_plate.is_at_rest = true;
    root_plate.shape = ParticleShape::BOX;
    root_plate.width = plate_diameter;
    root_plate.height = plate_diameter;
    root_plate.thickness = plate_height;
    root_plate.r = 0.4f;
    root_plate.g = 0.25f;
    root_plate.b = 0.1f; // Brown
    root_plate.a = 1.0f;
    root_plate.material_density = 800.0f;
    root_plate.material_strength = 50e6f;
    root_plate.friction = 0.7f;

    out_particles.push_back(root_plate);
    size_t plate_index = 0;

    // ========================================================================
    // PRIMARY ROOTS - Connected to root plate
    // ========================================================================
    float root_length = varied_spec.height * 0.25f;
    // SECOND COPY of the root-thickness bug, in the KG/permaworld path. The
    // fix at generate_root_system did not reach here, so every STREAMED tree
    // still buried its roots. A root cannot be thicker than the plate socket
    // it grows out of.
    float root_thickness = std::min(varied_spec.trunk_diameter * 0.4f, plate_height);
    const float cardinal_angles[4] = {0.0f, 90.0f, 180.0f, 270.0f};

    for (int i = 0; i < 4; ++i)
    {
        float radial_angle = cardinal_angles[i] + random_variance(0.0f, 5.0f);
        float rad_h = radial_angle * M_PI / 180.0f;
        float radial_x = std::cos(rad_h);
        float radial_y = std::sin(rad_h);

        Particle root = {};
        root.r = 0.35f;
        root.g = 0.2f;
        root.b = 0.08f; // Dark brown
        root.a = 1.0f;
        root.shape = ParticleShape::BOX;

        // Axis-aligned collision dimensions (rotation is not applied to bounds)
        if (std::abs(radial_x) > std::abs(radial_y))
        {
            root.width = root_length;
            root.height = root_thickness;
            root.thickness = root_thickness;
        }
        else
        {
            root.width = root_thickness;
            root.height = root_length;
            root.thickness = root_thickness;
        }

        // Position: attached to plate side face
        float plate_half_w = plate_diameter * 0.5f;
        float face_offset_x = (std::abs(radial_x) > std::abs(radial_y)) ? ((radial_x > 0) ? plate_half_w : -plate_half_w) : 0.0f;
        float face_offset_y = (std::abs(radial_y) >= std::abs(radial_x)) ? ((radial_y > 0) ? plate_half_w : -plate_half_w) : 0.0f;

        root.x = world_x + face_offset_x + radial_x * root_length * 0.5f;
        root.y = world_y + face_offset_y + radial_y * root_length * 0.5f;
        root.z = plate_center_z;

        root.material_density = 800.0f;
        root.material_strength = 50e6f;
        root.friction = 0.7f;

        size_t root_index = out_particles.size();
        out_particles.push_back(root);

        // Gluon: root to plate
        GluonSpec gs;
        gs.parent_index = plate_index;
        gs.child_index = root_index;
        gs.data.type = kg::KGGluonType::ORGANIC;
        gs.data.offset_a_x = face_offset_x;
        gs.data.offset_a_y = face_offset_y;
        gs.data.offset_a_z = 0.0f;
        gs.data.offset_b_x = -radial_x * root_length * 0.5f;
        gs.data.offset_b_y = -radial_y * root_length * 0.5f;
        gs.data.offset_b_z = 0.0f;
        gs.data.target_distance = 0.0f;
        gs.data.contact_area = root_thickness * root_thickness;
        gs.data.stiffness = 50000.0f;
        gs.data.damping = 500.0f;

        out_gluons.push_back(gs);
    }

    // ========================================================================
    // TRUNK - Connected to root plate
    // ========================================================================
    float trunk_ratio = random_variance(0.32f, 0.08f);
    float trunk_length = varied_spec.height * trunk_ratio;
    float trunk_thickness = varied_spec.trunk_diameter * random_variance(1.4f, 0.2f);
    float root_plate_top_z = ground_z + plate_height;

    Particle trunk = create_branch_particle(
        world_x, world_y, root_plate_top_z + trunk_length * 0.5f,
        trunk_length, trunk_thickness,
        0.0f, 90.0f,
        varied_spec, varied_spec.branch_depth);

    size_t trunk_index = out_particles.size();
    out_particles.push_back(trunk);

    // Gluon: trunk to plate
    GluonSpec trunk_gluon;
    trunk_gluon.parent_index = plate_index;
    trunk_gluon.child_index = trunk_index;
    trunk_gluon.data.type = kg::KGGluonType::ORGANIC;
    trunk_gluon.data.offset_a_x = 0.0f;
    trunk_gluon.data.offset_a_y = 0.0f;
    trunk_gluon.data.offset_a_z = plate_height * 0.5f;
    trunk_gluon.data.offset_b_x = 0.0f;
    trunk_gluon.data.offset_b_y = 0.0f;
    trunk_gluon.data.offset_b_z = -trunk_length * 0.5f;
    trunk_gluon.data.target_distance = 0.0f;
    trunk_gluon.data.contact_area = trunk_thickness * trunk_thickness * 4.0f;
    trunk_gluon.data.stiffness = 100000.0f;
    trunk_gluon.data.damping = 1000.0f;

    out_gluons.push_back(trunk_gluon);

    // ========================================================================
    // BRANCHES - Recursive collection
    // ========================================================================
    float trunk_top_z = root_plate_top_z + trunk_length;
    float initial_elevation = random_variance(45.0f, 15.0f);
    float initial_direction = random_variance(0.0f, 180.0f);

    int num_main_branches = std::max(4, varied_spec.branches_per_split + 2);
    int effective_depth = std::max(2, varied_spec.branch_depth - 2);

    for (int i = 0; i < num_main_branches; ++i)
    {
        float base_angle = (360.0f / num_main_branches) * i;
        float angle_offset = base_angle + random_variance(0.0f, varied_spec.angle_variance);
        float branch_direction = initial_direction + angle_offset;
        float branch_elevation = initial_elevation + random_variance(0.0f, 15.0f);
        float branch_length = varied_spec.height * random_variance(0.5f, 0.15f);
        float branch_thickness = trunk_thickness * random_variance(0.7f, 0.15f);

        collect_branch(
            trunk_index,
            trunk_top_z,
            branch_direction,
            branch_elevation,
            branch_length,
            branch_thickness,
            effective_depth,
            varied_spec,
            out_particles,
            out_gluons);
    }
}

// ============================================================================
// RECURSIVE BRANCH COLLECTION (store mode)
// ============================================================================

void PhysicsTreeGenerator::collect_branch(
    size_t parent_index,
    float parent_top_z,
    float direction_angle,
    float elevation_angle,
    float length,
    float thickness,
    int depth,
    const TreeSpec &spec,
    std::vector<Particle> &particles,
    std::vector<GluonSpec> &gluons)
{
    if (depth <= 0 || thickness < 0.02f)
    {
        return;
    }

    // Direction vector
    float rad_h = direction_angle * M_PI / 180.0f;
    float rad_v = elevation_angle * M_PI / 180.0f;
    float dir_x = std::cos(rad_h) * std::cos(rad_v);
    float dir_y = std::sin(rad_h) * std::cos(rad_v);
    float dir_z = std::sin(rad_v);

    // Parent position
    const Particle &parent = particles[parent_index];
    float parent_x = parent.x;
    float parent_y = parent.y;

    // Branch center
    float branch_center_x = parent_x + dir_x * length * 0.5f;
    float branch_center_y = parent_y + dir_y * length * 0.5f;
    float branch_center_z = parent_top_z + dir_z * length * 0.5f;

    // Create branch particle
    Particle branch = create_branch_particle(
        branch_center_x, branch_center_y, branch_center_z,
        length, thickness,
        direction_angle, elevation_angle,
        spec, depth);

    size_t branch_index = particles.size();
    particles.push_back(branch);

    // Gluon: branch to parent
    float parent_half_length = parent.thickness * 0.5f;

    GluonSpec gs;
    gs.parent_index = parent_index;
    gs.child_index = branch_index;
    gs.data.type = kg::KGGluonType::ORGANIC;
    gs.data.offset_a_x = 0.0f;
    gs.data.offset_a_y = 0.0f;
    gs.data.offset_a_z = parent_half_length;
    // Same frame bug as the direct path in generate_branch, and the same fix:
    // a segment's local +Z runs along its grown direction, so the end nearest
    // the parent is -length/2 along local Z. Building it from the WORLD
    // direction dir_* put this offset in a different frame from offset_a,
    // which is local. Two sites, one defect; fixing only one leaves trees
    // stored through the KG path still broken.
    gs.data.offset_b_x = 0.0f;
    gs.data.offset_b_y = 0.0f;
    gs.data.offset_b_z = -length * 0.5f;
    gs.data.target_distance = 0.0f;
    gs.data.contact_area = calculate_contact_area(thickness);
    gs.data.stiffness = 30000.0f;
    gs.data.damping = 500.0f;

    gluons.push_back(gs);

    // Branch end position
    float branch_top_x = branch_center_x + dir_x * length * 0.5f;
    float branch_top_y = branch_center_y + dir_y * length * 0.5f;
    float branch_top_z = branch_center_z + dir_z * length * 0.5f;

    // ========================================================================
    // LEAVES AT BRANCH TIPS (simplified for store mode)
    // ========================================================================
    bool is_terminal = (depth == 1);
    float leaf_threshold = spec.trunk_diameter * 0.25f;

    if (is_terminal || thickness < leaf_threshold)
    {
        int num_leaves = 6 + (rng_state_ % 5);
        rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;

        float base_leaf_width = 0.35f;
        float leaf_width_variance = 0.10f;

        float tip_x = branch_center_x + dir_x * length * 0.5f;
        float tip_y = branch_center_y + dir_y * length * 0.5f;
        float tip_z = branch_top_z;

        for (int i = 0; i < num_leaves; ++i)
        {
            float theta = random_variance(0.0f, 180.0f);
            float phi = random_variance(0.0f, 90.0f);
            float offset_radius = random_variance(1.5f, 0.5f);

            float theta_rad = theta * M_PI / 180.0f;
            float phi_rad = phi * M_PI / 180.0f;

            float leaf_x = tip_x + offset_radius * std::sin(phi_rad) * std::cos(theta_rad);
            float leaf_y = tip_y + offset_radius * std::sin(phi_rad) * std::sin(theta_rad);
            float leaf_z = tip_z + offset_radius * std::cos(phi_rad);

            Particle leaf = {};
            leaf.x = leaf_x;
            leaf.y = leaf_y;
            leaf.z = leaf_z;
            leaf.r = spec.leaf_r + random_variance(0.0f, 0.1f);
            leaf.g = spec.leaf_g + random_variance(0.0f, 0.15f);
            leaf.b = spec.leaf_b + random_variance(0.0f, 0.1f);
            leaf.r = std::max(0.0f, std::min(1.0f, leaf.r));
            leaf.g = std::max(0.0f, std::min(1.0f, leaf.g));
            leaf.b = std::max(0.0f, std::min(1.0f, leaf.b));
            leaf.a = 1.0f;
            leaf.shape = ParticleShape::BOX;
            float this_leaf_width = random_variance(base_leaf_width, leaf_width_variance);
            leaf.width = this_leaf_width;
            leaf.height = this_leaf_width * 0.7f;
            leaf.thickness = 0.02f;
            leaf.facing_angle = theta;
            leaf.reflectivity = 0.20f;
            leaf.material_density = 100.0f;
            leaf.material_strength = 1000.0f;

            size_t leaf_index = particles.size();
            particles.push_back(leaf);

            // Gluon: leaf to branch
            float actual_distance = std::sqrt(
                (leaf_x - tip_x) * (leaf_x - tip_x) +
                (leaf_y - tip_y) * (leaf_y - tip_y) +
                (leaf_z - tip_z) * (leaf_z - tip_z));

            GluonSpec leaf_gs;
            leaf_gs.parent_index = branch_index;
            leaf_gs.child_index = leaf_index;
            leaf_gs.data.type = kg::KGGluonType::ORGANIC;
            leaf_gs.data.offset_a_x = 0.0f;
            leaf_gs.data.offset_a_y = 0.0f;
            leaf_gs.data.offset_a_z = length * 0.5f;
            leaf_gs.data.offset_b_x = 0.0f;
            leaf_gs.data.offset_b_y = 0.0f;
            leaf_gs.data.offset_b_z = 0.0f;
            leaf_gs.data.target_distance = actual_distance;
            leaf_gs.data.stiffness = 5000.0f;
            leaf_gs.data.damping = 100.0f;
            leaf_gs.data.contact_area = 0.0001f;

            gluons.push_back(leaf_gs);
        }
    }

    // ========================================================================
    // CHILD BRANCHES
    // ========================================================================
    float elevation_drop = random_variance(25.0f, 10.0f);
    float child_elevation = elevation_angle - elevation_drop;
    child_elevation = std::max(10.0f, std::min(60.0f, child_elevation));

    int num_children = std::min(2, spec.branches_per_split);

    for (int i = 0; i < num_children; ++i)
    {
        float angle_offset;
        if (num_children == 2)
        {
            float spread = random_variance(150.0f, 30.0f);
            angle_offset = (i == 0) ? -spread * 0.5f : spread * 0.5f;
        }
        else
        {
            angle_offset = random_variance(0.0f, 45.0f);
        }

        float child_direction = direction_angle + angle_offset;
        float child_length = length * spec.length_ratio * random_variance(0.8f, 0.2f);
        float child_thickness = thickness * spec.thickness_ratio * random_variance(0.8f, 0.15f);
        float varied_elevation = child_elevation + random_variance(0.0f, 15.0f);

        collect_branch(
            branch_index,
            branch_top_z,
            child_direction,
            varied_elevation,
            child_length,
            child_thickness,
            depth - 1,
            spec,
            particles,
            gluons);
    }
}
