#include "logosphere/worldgen/organic_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"

namespace { bool g_legacy_unbonded = false; }
void OrganicGenerator::set_legacy_unbonded(bool on) { g_legacy_unbonded = on; }
bool OrganicGenerator::legacy_unbonded() { return g_legacy_unbonded; }

#include "core/game_time.h"
#include "logosphere/worldgen/space_colonization.h"
#include "logosphere/worldgen/attractor_shapes.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <string>

OrganicGenerator::OrganicGenerator()
    : EntityGenerator()
    , rng_state_(0)
{
}

OrganicGenerator::~OrganicGenerator() {
    // Cleanup handled by KG
}

kg::EntityID OrganicGenerator::generate(float world_x, float world_y, float world_z,
                                        const OrganicSpec& spec) {
    if (!engine_ || !kg_) {
        std::cerr << "[OrganicGenerator] ERROR: Not initialized!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    std::cout << "[OrganicGenerator] Generating " << spec.species << " at ("
              << world_x << ", " << world_y << ", " << world_z << ")" << std::endl;

    // Seed RNG for reproducible variation
    seed_rng(spec.random_seed);

    // Create root KG entity. KG types are ontology names (validated;
    // the legacy lowercase strings were silently rejected and this
    // deferred path returned INVALID_ENTITY). Only TREE and GRASS
    // have base ontology types today; other organics stay rejected —
    // loudly in the KG log — until their types enter the schema.
    std::string entity_type = organic_type_to_string(spec.type);
    if (spec.type == OrganicType::TREE)  entity_type = "Tree";
    if (spec.type == OrganicType::GRASS) entity_type = "Grass";
    kg::EntityID entity = kg_->createEntityAtPosition(entity_type, world_x, world_y);
    kg_->setProperty(entity, "z", std::to_string(world_z));
    kg_->setProperty(entity, "species", spec.species);
    kg_->setProperty(entity, "method", "space_colonization");

    // Store growth state in KG for temporal growth system (MVP)
    double current_time = GameTime::get_current_time();
    kg_->setProperty(entity, "growth_enabled", "true");  // MVP: Growth enabled by default
    kg_->setProperty(entity, "growth_time_created", std::to_string(current_time));
    kg_->setProperty(entity, "growth_time_last_simulated", std::to_string(current_time));
    kg_->setProperty(entity, "growth_rate_per_year", std::to_string(spec.growth_rate_iterations_per_year));
    kg_->setProperty(entity, "growth_max_iterations", std::to_string(spec.max_iterations));

    // MVP: Simulate random initial age (0-5 game years) for variety
    // This makes entities appear at different growth stages
    float random_age_years = random_variance(2.5f, 2.5f);  // 0-5 years
    random_age_years = std::max(0.0f, random_age_years);
    int target_iteration = static_cast<int>(random_age_years * spec.growth_rate_iterations_per_year);
    target_iteration = std::min(target_iteration, spec.max_iterations);  // Cap at maturity

    kg_->setProperty(entity, "growth_iteration", std::to_string(target_iteration));
    kg_->setProperty(entity, "growth_is_mature", target_iteration >= spec.max_iterations ? "true" : "false");

    std::cout << "[OrganicGenerator] MVP Growth: age=" << random_age_years << " years, iteration="
              << target_iteration << "/" << spec.max_iterations << std::endl;

    // Generate trunk/stem with organic tapering
    Vec3 trunk_top;
    auto trunk_particles = generate_trunk(world_x, world_y, world_z, spec, trunk_top);

    // ROOT THE PLANT. The base segment is KINEMATIC: rooted in the ground is
    // immobility through solver mode, which the engine invariants explicitly
    // sanction (turtle, gluon anchors, or KINEMATIC are the three mechanisms).
    // Without this a fully bonded plant is a free chain standing on its end,
    // and it topples. With it, the chain hangs off a rooted base the way a
    // real stalk hangs off its roots.
    if (!trunk_particles.empty()) {
        trunk_particles.front().solver_mode = ParticleSolverMode::KINEMATIC;
        trunk_particles.front().is_at_rest = true;
    }

    // Store trunk particles in KG, keeping ids so the chain can be bonded.
    std::vector<kg::KGParticleID> trunk_kg;
    for (const Particle& p : trunk_particles) {
        kg::KGParticleID kg_id = kg_->createKGParticle(entity, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(kg_id, p);
        trunk_kg.push_back(kg_id);
    }

    // Calculate crown/foliage region
    float crown_height = spec.height * (1.0f - spec.trunk_ratio);

    // Trunk direction vector (for biasing growth)
    Vec3 trunk_dir(0.0f, 0.0f, 1.0f);  // Default upward
    if (trunk_particles.size() > 0) {
        // Use direction from base to trunk top
        trunk_dir = Vec3(
            trunk_top.x - world_x,
            trunk_top.y - world_y,
            trunk_top.z - world_z
        ).normalized();
    }

    // Generate attractors using AttractorShapes
    Vec3 attractor_center(
        trunk_top.x + trunk_dir.x * crown_height * 0.5f,
        trunk_top.y + trunk_dir.y * crown_height * 0.5f,
        trunk_top.z + trunk_dir.z * crown_height * 0.5f
    );
    std::vector<Vec3> attractors = AttractorShapes::generate(spec, attractor_center, spec.random_seed);

    std::cout << "[OrganicGenerator] Generated " << attractors.size() << " attractors ("
              << attractor_shape_to_string(spec.attractor_shape) << ")" << std::endl;

    // Run Space Colonization algorithm (MVP: use target_iteration for growth simulation)
    SpaceColonizationParams sc_params;
    sc_params.root_position = trunk_top;
    sc_params.root_direction = trunk_dir;
    sc_params.external_attractors = attractors;  // Pass our pre-generated attractors
    sc_params.attraction_range = spec.attraction_range;
    sc_params.kill_distance = spec.kill_distance;
    sc_params.segment_length = spec.segment_length;
    sc_params.initial_thickness = spec.base_thickness * random_variance(0.6f, 0.1f);
    sc_params.thickness_taper = spec.thickness_taper;
    sc_params.max_iterations = std::max(1, target_iteration);  // MVP: Use simulated age-based iteration
    sc_params.random_seed = spec.random_seed;

    SpaceColonization sc_algorithm;
    TreeSkeleton skeleton = sc_algorithm.generate(sc_params);

    std::cout << "[OrganicGenerator] Generated skeleton with " << skeleton.segment_count()
              << " segments" << std::endl;

    // Convert skeleton to particles (branches + foliage), with bonding edges
    std::vector<int> crown_parents;
    auto crown_particles = skeleton_to_particles(skeleton, spec, crown_parents);

    std::cout << "[OrganicGenerator] Created " << crown_particles.size()
              << " particles from skeleton" << std::endl;

    // Store crown particles in KG, keeping ids for bonding.
    std::vector<kg::KGParticleID> crown_kg;
    for (const Particle& p : crown_particles) {
        kg::KGParticleID kg_id = kg_->createKGParticle(entity, kg::INVALID_RENDER_INDEX);
        kg_->setKGParticleData(kg_id, p);
        crown_kg.push_back(kg_id);
    }

    // ========================================================================
    // BOND THE PLANT (issue #47). Before this, organic entities were stored as
    // BARE particle records: chunk activation materialised every blade as a
    // tower of free ultra-thin plates, the solver had to hold every interface
    // with contact rows forever, and on that stack the sequential-impulse
    // iteration diverges (measured 16k -> 4.5e8 in one frame; grass shrapnel
    // at ~100 m/s). The design was always "blades are joined by gluons"; this
    // is where the design becomes true. Activation needs no changes: it has
    // recreated KG gluons all along, for entities that stored any.
    // ========================================================================
    auto bond = [&](kg::KGParticleID a, kg::KGParticleID b,
                    const Particle& pa, const Particle& pb) {
        kg::KGGluonID g = kg_->createKGGluon(entity, a, b);
        kg::KGGluonData d;
        d.type = kg::KGGluonType::ORGANIC;
        // Zero offsets + the placed centre distance: hold what generation
        // built. Same shape as the physics-tree leaf fix (8c0e86e), where the
        // gluon preserving the ACTUAL placement was the difference between a
        // canopy and shrapnel.
        const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
        d.target_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        d.stiffness = 5000.0f;    // flexible stem, the physics-tree leaf value
        d.damping = 100.0f;
        d.angular_stiffness = 50.0f;   // loose: blades bend
        d.angular_damping = 5.0f;
        d.enable_angular_constraint = true;
        // Breaking scales with the child's REAL tear cross-section: the two
        // smallest extents (a blade tears across its width * thickness, not
        // its face). The old 1e-4 floor plus the 100 MPa particle default
        // made every grass joint hold 10 kN, which is why the single-blade
        // study saw a bond stretch to 434 m without tearing.
        float e0 = pb.width, e1 = pb.height, e2 = pb.thickness;
        if (e0 > e1) std::swap(e0, e1);
        if (e1 > e2) std::swap(e1, e2);
        if (e0 > e1) std::swap(e0, e1);
        d.contact_area = std::max(1e-8f, e0 * e1);
        kg_->setKGGluonData(g, d);
    };

    int gluons_stored = 0;
    if (g_legacy_unbonded) {
        std::cout << "[OrganicGenerator] LEGACY UNBONDED (A/B lever): storing no gluons"
                  << std::endl;
    } else {
    for (size_t j = 0; j + 1 < trunk_particles.size(); ++j) {
        bond(trunk_kg[j], trunk_kg[j + 1], trunk_particles[j], trunk_particles[j + 1]);
        gluons_stored++;
    }
    for (size_t cidx = 0; cidx < crown_particles.size(); ++cidx) {
        const int pp = crown_parents[cidx];
        if (pp >= 0) {
            bond(crown_kg[pp], crown_kg[cidx], crown_particles[pp], crown_particles[cidx]);
            gluons_stored++;
        } else if (!trunk_kg.empty()) {
            // Skeleton roots hang off the trunk top.
            bond(trunk_kg.back(), crown_kg[cidx],
                 trunk_particles.back(), crown_particles[cidx]);
            gluons_stored++;
        }
        // No trunk and no parent: the plant's own base, nothing to bond to.
    }
    }
    std::cout << "[OrganicGenerator] Bonded: " << gluons_stored
              << " gluons stored in KG" << std::endl;

    // Track entity
    on_entity_created(entity);

    // NOTE: Activation handled by caller (user click) or chunk system (reload)
    // DO NOT activate here - breaks hierarchical entity tracking!

    std::cout << "[OrganicGenerator] Complete - " << (trunk_particles.size() + crown_particles.size())
              << " total particles (stored in KG, not yet activated)" << std::endl;

    return entity;
}

std::vector<Particle> OrganicGenerator::generate_trunk(float world_x, float world_y, float world_z,
                                                       const OrganicSpec& spec,
                                                       Vec3& out_trunk_top) {
    std::vector<Particle> trunk_particles;

    float trunk_height = spec.height * spec.trunk_ratio;

    // For grass (very short trunk), skip trunk generation
    if (trunk_height < 0.05f) {
        out_trunk_top = Vec3(world_x, world_y, world_z);
        return trunk_particles;
    }

    // Natural trunk lean (trees) or minimal wobble (grass)
    float trunk_lean_angle = random_variance(0.0f, 15.0f * spec.size_variance);
    float trunk_lean_direction = random_variance(0.0f, 180.0f);

    // Trunk direction vector
    float lean_rad_h = trunk_lean_direction * M_PI / 180.0f;
    float lean_rad_v = (90.0f - trunk_lean_angle) * M_PI / 180.0f;
    float trunk_dir_x = std::cos(lean_rad_h) * std::cos(lean_rad_v);
    float trunk_dir_y = std::sin(lean_rad_h) * std::cos(lean_rad_v);
    float trunk_dir_z = std::sin(lean_rad_v);

    // Trunk top position
    out_trunk_top = Vec3(
        world_x + trunk_dir_x * trunk_height,
        world_y + trunk_dir_y * trunk_height,
        world_z + trunk_dir_z * trunk_height
    );

    // Split trunk into segments for organic taper
    int num_trunk_segments = std::max(1, static_cast<int>(trunk_height / 1.5f));
    float segment_length = trunk_height / num_trunk_segments;

    for (int i = 0; i < num_trunk_segments; ++i) {
        float t_mid = (static_cast<float>(i) + 0.5f) / num_trunk_segments;

        // Taper: diameter decreases from base to top
        float diameter_at_segment = spec.base_thickness * (1.0f - t_mid * spec.trunk_taper);

        // Organic variation in direction (tree sways slightly)
        float wobble_x = random_variance(0.0f, spec.trunk_wobble);
        float wobble_y = random_variance(0.0f, spec.trunk_wobble);
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

        Particle trunk_seg;
        trunk_seg.x = seg_center_x;
        trunk_seg.y = seg_center_y;
        trunk_seg.z = seg_center_z;
        trunk_seg.shape = ParticleShape::BOX;
        trunk_seg.width = diameter_at_segment;
        trunk_seg.height = diameter_at_segment;
        trunk_seg.thickness = segment_length * 1.1f;  // Slight overlap
        trunk_seg.material_strength = spec.material_strength;

        // Rotate to match segment direction
        trunk_seg.rotation_x = -std::asin(seg_dir_y);
        trunk_seg.rotation_y = std::atan2(seg_dir_x, seg_dir_z);
        trunk_seg.rotation_z = random_variance(0.0f, 0.05f);  // Slight twist

        trunk_seg.r = spec.stem_r;
        trunk_seg.g = spec.stem_g;
        trunk_seg.b = spec.stem_b;
        trunk_seg.reflectivity = 0.3f;
        trunk_seg.pattern_id = 1;  // PATTERN_WOOD

        trunk_particles.push_back(trunk_seg);
    }

    return trunk_particles;
}

std::vector<Particle> OrganicGenerator::skeleton_to_particles(const TreeSkeleton& skeleton,
                                                              const OrganicSpec& spec,
                                                              std::vector<int>& parents_out) {
    std::vector<Particle> particles;
    particles.reserve(skeleton.segment_count() * 3);  // Room for branches + foliage
    parents_out.clear();
    parents_out.reserve(skeleton.segment_count() * 3);

    // Geometric parent lookup: my parent is the segment whose END is my START.
    // Root segments (their start touches nothing) get -1, meaning "bond to the
    // trunk top". O(n^2) over ~10-100 segments at generation time only.
    auto find_parent_segment = [&](int i) -> int {
        const Vec3 s0 = skeleton.get_segment(i).start;
        for (int j = 0; j < skeleton.segment_count(); ++j) {
            if (j == i) continue;
            const Vec3 e = skeleton.get_segment(j).end;
            const float dx = e.x - s0.x, dy = e.y - s0.y, dz = e.z - s0.z;
            if (dx * dx + dy * dy + dz * dz < 1e-6f) return j;
        }
        return -1;
    };
    std::vector<int> seg_particle(skeleton.segment_count(), -1);
    std::vector<int> parent_seg_of_particle;
    parent_seg_of_particle.reserve(skeleton.segment_count() * 3);

    // Helper: Convert direction vector to Euler angles
    auto direction_to_euler = [](const Vec3& dir, float& out_rot_x, float& out_rot_y, float& out_rot_z) {
        out_rot_x = -std::asin(dir.y);
        out_rot_y = std::atan2(dir.x, dir.z);
        out_rot_z = 0.0f;
    };

    seed_rng(spec.random_seed);

    // Calculate thickness threshold for foliage placement
    float initial_thickness = skeleton.segment_count() > 0 ?
        skeleton.get_segment(0).thickness : spec.base_thickness * 0.5f;
    float foliage_thickness_threshold = initial_thickness * spec.foliage_thickness_threshold;

    // Calculate max iteration for leaf maturity
    int max_iteration = 0;
    for (int i = 0; i < skeleton.segment_count(); ++i) {
        max_iteration = std::max(max_iteration, skeleton.get_segment(i).creation_iteration);
    }

    for (int i = 0; i < skeleton.segment_count(); ++i) {
        const BranchSegment& seg = skeleton.get_segment(i);
        Vec3 dir = seg.direction();
        float length = seg.length();

        // Create branch particle (BOX centered at midpoint)
        Vec3 mid_pos(
            seg.start.x + dir.x * length * 0.5f,
            seg.start.y + dir.y * length * 0.5f,
            seg.start.z + dir.z * length * 0.5f
        );

        Particle p;
        p.x = mid_pos.x;
        p.y = mid_pos.y;
        p.z = mid_pos.z;
        p.vx = 0.0f;
        p.vy = 0.0f;
        p.vz = 0.0f;
        p.r = spec.stem_r;
        p.g = spec.stem_g;
        p.b = spec.stem_b;
        p.a = 1.0f;
        p.shape = ParticleShape::BOX;
        // Parametric stem cross-section (grass=flat, trees=round, vines=thin)
        p.width = seg.thickness * spec.stem_width_multiplier;
        p.height = seg.thickness * spec.stem_height_multiplier;
        p.thickness = length;
        p.material_strength = spec.material_strength;
        direction_to_euler(dir, p.rotation_x, p.rotation_y, p.rotation_z);
        p.reflectivity = 0.3f;
        p.pattern_id = 1;  // PATTERN_WOOD for branches
        seg_particle[i] = (int)particles.size();
        // Parent SEGMENT id for now; translated to a particle index after the
        // loop, because a geometric parent can sit later in the segment list
        // and its particle index does not exist yet at this point.
        parent_seg_of_particle.push_back(find_parent_segment(i));
        particles.push_back(p);

        // Add foliage to thin branches. Every leaf bonds to its segment.
        if (seg.thickness < foliage_thickness_threshold) {
            auto foliage = create_foliage(seg.end, seg, spec, max_iteration);
            for (size_t f = 0; f < foliage.size(); ++f)
                parent_seg_of_particle.push_back(i);
            particles.insert(particles.end(), foliage.begin(), foliage.end());
        }
    }

    // Translate segment ids to particle indices now that every segment has one.
    for (size_t k = 0; k < parent_seg_of_particle.size(); ++k) {
        const int pseg = parent_seg_of_particle[k];
        int parent_particle = (pseg >= 0) ? seg_particle[pseg] : -1;
        // A segment cannot be bonded to itself (its own foliage points at it,
        // which is correct; the segment itself must not).
        if (parent_particle == (int)k) parent_particle = -1;
        parents_out.push_back(parent_particle);
    }

    return particles;
}

std::vector<Particle> OrganicGenerator::create_foliage(const Vec3& position,
                                                       const BranchSegment& segment,
                                                       const OrganicSpec& spec,
                                                       int max_iteration) {
    std::vector<Particle> foliage;

    int leaf_count_range = spec.foliage_count_max - spec.foliage_count_min + 1;
    int num_foliage = spec.foliage_count_min + (rng_state_ % leaf_count_range);

    for (int j = 0; j < num_foliage; ++j) {
        // Distribute foliage around position in sphere
        float theta = random_variance(0.0f, 180.0f);
        float phi = random_variance(0.0f, 90.0f);
        float offset_radius = random_variance(spec.foliage_offset_radius, spec.foliage_offset_variance);

        float theta_rad = theta * M_PI / 180.0f;
        float phi_rad = phi * M_PI / 180.0f;

        Particle leaf;
        leaf.x = position.x + offset_radius * std::sin(phi_rad) * std::cos(theta_rad);
        leaf.y = position.y + offset_radius * std::sin(phi_rad) * std::sin(theta_rad);
        leaf.z = position.z + offset_radius * std::cos(phi_rad);
        leaf.vx = 0.0f;
        leaf.vy = 0.0f;
        leaf.vz = 0.0f;

        // Foliage color with variation
        leaf.r = spec.foliage_r + random_variance(0.0f, 0.1f);
        leaf.g = spec.foliage_g + random_variance(0.0f, 0.15f);
        leaf.b = spec.foliage_b + random_variance(0.0f, 0.1f);
        leaf.r = std::max(0.0f, std::min(1.0f, leaf.r));
        leaf.g = std::max(0.0f, std::min(1.0f, leaf.g));
        leaf.b = std::max(0.0f, std::min(1.0f, leaf.b));
        leaf.a = 1.0f;

        leaf.shape = ParticleShape::BOX;

        // Foliage maturity based on branch age
        // Young foliage scales from foliage_maturity_size_min to 1.0 based on branch maturity
        float branch_age = static_cast<float>(max_iteration - segment.creation_iteration);
        float maturity = (max_iteration > 0) ? (branch_age / max_iteration) : 1.0f;
        float size_range = 1.0f - spec.foliage_maturity_size_min;
        float size_factor = spec.foliage_maturity_size_min + maturity * maturity * size_range;

        leaf.width = random_variance(spec.foliage_base_width * size_factor, 0.03f);
        leaf.height = random_variance(spec.foliage_base_height * size_factor, 0.02f);
        leaf.thickness = 0.001f;  // Paper-thin grass blades (1mm)
        leaf.material_strength = spec.material_strength;
        leaf.facing_angle = random_variance(0.0f, 180.0f);
        leaf.reflectivity = 0.2f;

        // Add tilt to some foliage
        float tilt_chance = (rng_state_ % 100) / 100.0f;
        rng_state_ = (rng_state_ * 1103515245 + 12345) % 2147483648;
        if (tilt_chance < spec.foliage_tilt_probability) {
            float tilt_degrees = random_variance(
                (spec.foliage_tilt_angle_min + spec.foliage_tilt_angle_max) * 0.5f,
                (spec.foliage_tilt_angle_max - spec.foliage_tilt_angle_min) * 0.5f
            );
            leaf.rotation_x = tilt_degrees * M_PI / 180.0f;
        }

        foliage.push_back(leaf);
    }

    return foliage;
}

void OrganicGenerator::seed_rng(unsigned int seed) {
    rng_state_ = seed;
    if (rng_state_ == 0) {
        rng_state_ = 12345;  // Non-zero default
    }
}

float OrganicGenerator::random_variance(float base, float variance) {
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;
    float normalized = (float)rng_state_ / 2147483648.0f;  // [0, 1]
    float offset = (normalized * 2.0f - 1.0f) * variance;  // [-variance, +variance]
    return base + offset;
}

float OrganicGenerator::random_gaussian(float mean, float stddev) {
    // Box-Muller transform for Gaussian distribution
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;
    float u1 = (float)rng_state_ / 2147483648.0f;
    rng_state_ = (1103515245 * rng_state_ + 12345) % 2147483648;
    float u2 = (float)rng_state_ / 2147483648.0f;

    // Avoid log(0)
    u1 = std::max(u1, 0.0001f);

    float z0 = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * M_PI * u2);
    return mean + z0 * stddev;
}

std::vector<OrganicGenerator::Vec2> OrganicGenerator::generate_blade_positions(const GrassPatchSpec& spec) {
    std::vector<Vec2> positions;
    positions.reserve(spec.blade_count);

    // Positions are LOCAL to the patch, [0, w] x [0, d]: the caller
    // subtracts half the patch size to center. (The old code sampled
    // [-w/2, +w/2] here AND subtracted again — every patch shipped
    // shifted southwest by half its own size.)
    int max_attempts = spec.blade_count * 100;  // Prevent infinite loop
    int attempts = 0;

    while (positions.size() < (size_t)spec.blade_count && attempts < max_attempts) {
        float x, y;
        if (spec.distribution == DistributionType::CLUSTERED) {
            // Painter's dab: a MIXTURE of 1-3 Gaussian lobes — a
            // blobby organic silhouette instead of a clean ellipse.
            // Lobes are chosen once per patch (static across the
            // loop via lazy init below).
            if (lobes_cached_for_ != &spec) {
                lobes_cached_for_ = &spec;
                lobe_count_ = 1 + static_cast<int>(
                    random_variance(1.0f, 1.0f) + 0.5f);   // 1..3
                if (lobe_count_ < 1) lobe_count_ = 1;
                if (lobe_count_ > 3) lobe_count_ = 3;
                for (int li = 0; li < lobe_count_; ++li) {
                    lobe_x_[li] = random_variance(spec.patch_width * 0.5f,
                                                  spec.patch_width * 0.2f);
                    lobe_y_[li] = random_variance(spec.patch_depth * 0.5f,
                                                  spec.patch_depth * 0.2f);
                }
            }
            int li = static_cast<int>(
                random_variance(0.5f, 0.5f) * lobe_count_) % lobe_count_;
            if (li < 0) li = 0;
            x = random_gaussian(lobe_x_[li], spec.patch_width * 0.16f);
            y = random_gaussian(lobe_y_[li], spec.patch_depth * 0.16f);
            if (x < 0.0f || x > spec.patch_width ||
                y < 0.0f || y > spec.patch_depth) {
                attempts++;
                continue;
            }
        } else {
            // UNIFORM / POISSON: even fill of the rectangle (the
            // min-distance rejection below is the Poisson-ish part).
            x = random_variance(spec.patch_width * 0.5f,
                                spec.patch_width * 0.5f);
            y = random_variance(spec.patch_depth * 0.5f,
                                spec.patch_depth * 0.5f);
        }

        // Check min distance from existing blades
        bool valid = true;
        for (const auto& pos : positions) {
            float dx = x - pos.x;
            float dy = y - pos.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < spec.min_blade_distance) {
                valid = false;
                break;
            }
        }

        if (valid) {
            positions.push_back({x, y});
        }

        attempts++;
    }

    std::cout << "[OrganicGenerator] Generated " << positions.size()
              << " blade positions (" << distribution_type_to_string(spec.distribution) << ")" << std::endl;

    return positions;
}

kg::EntityID OrganicGenerator::generate_grass_patch(float world_x, float world_y, float world_z,
                                                    const GrassPatchSpec& spec) {
    if (!engine_ || !kg_) {
        std::cerr << "[OrganicGenerator] ERROR: Not initialized!" << std::endl;
        return kg::INVALID_ENTITY;
    }

    std::cout << "[OrganicGenerator] Generating grass patch at ("
              << world_x << ", " << world_y << ", " << world_z << ")"
              << " - " << spec.blade_count << " blades, "
              << spec.patch_width << "m × " << spec.patch_depth << "m" << std::endl;

    // Seed RNG for reproducible variation
    seed_rng(spec.blade_spec.random_seed);

    // Create parent patch entity in KG ("GrassPatch": ontology name +
    // activator key; blades hang off HAS_PART below)
    kg::EntityID patch_entity = kg_->createEntityAtPosition("GrassPatch", world_x, world_y);
    kg_->setProperty(patch_entity, "z", std::to_string(world_z));
    kg_->setProperty(patch_entity, "patch_width", std::to_string(spec.patch_width));
    kg_->setProperty(patch_entity, "patch_depth", std::to_string(spec.patch_depth));
    kg_->setProperty(patch_entity, "blade_count", std::to_string(spec.blade_count));
    kg_->setProperty(patch_entity, "distribution", distribution_type_to_string(spec.distribution));

    // Generate blade positions
    auto positions = generate_blade_positions(spec);

    int blade_index = 0;
    int total_particles = 0;

    for (const auto& pos : positions) {
        // Copy base blade spec
        OrganicSpec blade_spec = spec.blade_spec;
        // Per-blade seed: generate() reseeds the shared RNG from
        // blade_spec.random_seed, so a shared seed made every blade
        // an identical clone AND froze the patch-level jitter draws
        // (each iteration reset the stream to the same point).
        blade_spec.random_seed =
            spec.blade_spec.random_seed + blade_index * 101 + 7;

        // Apply Gaussian variation to height (±height_variance)
        float height_factor = random_gaussian(1.0f, spec.height_variance);
        height_factor = std::max(0.5f, std::min(1.5f, height_factor));  // Clamp to reasonable range
        blade_spec.height *= height_factor;
        // Short blades grow exactly segment_length (1-2 iteration
        // blades) — scale it too or the height jitter is silently
        // discarded and every blade comes out identical.
        blade_spec.segment_length *= height_factor;

        // Per-blade hue jitter (organic tone): color_variance = 0
        // keeps legacy uniform color.
        if (spec.color_variance > 0.0f) {
            auto jitter = [&](float& c) {
                c = std::max(0.0f, std::min(1.0f,
                        random_variance(c, spec.color_variance)));
            };
            jitter(blade_spec.stem_r); jitter(blade_spec.stem_g);
            jitter(blade_spec.stem_b);
            jitter(blade_spec.foliage_r); jitter(blade_spec.foliage_g);
            jitter(blade_spec.foliage_b);
        }

        // Apply Gaussian variation to width (base_thickness)
        float width_factor = random_gaussian(1.0f, spec.width_variance);
        width_factor = std::max(0.7f, std::min(1.3f, width_factor));  // Clamp
        blade_spec.base_thickness *= width_factor;

        // Apply random tilt (0 to tilt_max degrees)
        float tilt_degrees = random_variance(0.0f, spec.tilt_max * 0.5f);

        // Calculate blade world position (centered in patch, then offset)
        float blade_x = world_x - spec.patch_width * 0.5f + pos.x;
        float blade_y = world_y - spec.patch_depth * 0.5f + pos.y;

        // Generate individual blade entity using OrganicGenerator
        kg::EntityID blade_id = generate(blade_x, blade_y, world_z, blade_spec);

        if (blade_id != kg::INVALID_ENTITY) {
            // Link blade to patch via KG relation
            kg_->createRelation(patch_entity, "HAS_PART", blade_id);

            // Set blade-specific properties
            std::string blade_name = "blade_" + std::to_string(blade_index);
            kg_->setProperty(blade_id, "name", blade_name);
            kg_->setProperty(blade_id, "tilt_angle", std::to_string(tilt_degrees));
            kg_->setProperty(blade_id, "parent_patch", std::to_string(patch_entity));

            // Count particles for this blade
            auto particle_ids = kg_->getEntityKGParticles(blade_id);
            total_particles += particle_ids.size();

            blade_index++;
        }
    }

    // Track entity
    on_entity_created(patch_entity);

    std::cout << "[OrganicGenerator] Grass patch complete - " << blade_index << " blades, "
              << total_particles << " total particles ("
              << (total_particles / std::max(1, blade_index)) << " per blade avg)" << std::endl;

    // Queue for activation on main thread (thread-safe)
    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(patch_entity);

    return patch_entity;
}
