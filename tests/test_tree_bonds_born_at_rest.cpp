// ============================================================================
// A TREE IS BORN AT REST (issue #38)
// ============================================================================
// THE LAW. A bond's rest geometry is where the generator PUT the two bodies.
// At frame zero, before gravity, before a single solver iteration, every bond
// must read a strain of 1.0. A structure born strained is a structure that
// tears itself apart, and no solver fix can save it.
//
// WHY. test_foliage_stays_attached fails with mean canopy drift 27.5 m against
// a 0.50 limit. TEAR_DEBUG showed 51 bonds tearing at exactly the 2.0x ratio
// with BOTH BODIES STATIONARY — nothing pushing them, nothing shaking. Three
// separate causes were proposed and each turned out to be a real bug that was
// NOT the cause:
//   - unrotated offsets in the tear check's rest term (real, fixed)
//   - rotating those offsets instead (wrong, kept as 1fc74be for its numbers)
//   - mixed-frame branch offsets, local vs world (real, fixed, two sites)
// After all three the canopy still leaves and bonds still tear at rest.
//
// So the remaining possibility, never tested: the generator PLACES the bodies
// at a separation its own bond does not agree with. This test asks that
// question directly, at creation, with physics never having run.
//
// It reports the distribution rather than one number, because "how many bonds
// are born wrong and by how much" is what decides whether this is a stray edge
// case or the whole structure.
//
//   ./build-release/logosphere-tests --test test_tree_bonds_born_at_rest --no-head
// ============================================================================

#include "../src/core/engine.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/physics_tree_generator.h"
#include "logosphere/worldgen/tree_generator.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace {
// |R_a*offset_a - R_b*offset_b|: the centre separation at which this bond is
// satisfied, with each offset rotated by the body that owns it. Same Euler
// composition the solver uses to build attachment points.
// The world position of a bond's attachment point on one body: the offset
// rotated by that body's own orientation, then added to its centre. Same
// Euler composition the solver uses.
void world_attach(const Particle& p, const Vec3& o, bool rotate,
                  float& wx, float& wy, float& wz) {
    if (!rotate) { wx = p.x + o.x; wy = p.y + o.y; wz = p.z + o.z; return; }
    const float cx = cosf(p.rotation_x), sx = sinf(p.rotation_x);
    const float cy = cosf(p.rotation_y), sy = sinf(p.rotation_y);
    const float cz = cosf(p.rotation_z), sz = sinf(p.rotation_z);
    const float y1 = o.y * cx - o.z * sx;
    const float z1 = o.y * sx + o.z * cx;
    const float x2 = o.x * cy + z1 * sy;
    const float z2 = -o.x * sy + z1 * cy;
    wx = p.x + x2 * cz - y1 * sz;
    wy = p.y + x2 * sz + y1 * cz;
    wz = p.z + z2;
}

float rest_in_bond_frame(const Particle& pa, const Particle& pb,
                         const GluonConstraintBase& g) {
    auto rot = [](const Particle& p, const Vec3& o, float& wx, float& wy, float& wz) {
        const float cx = cosf(p.rotation_x), sx = sinf(p.rotation_x);
        const float cy = cosf(p.rotation_y), sy = sinf(p.rotation_y);
        const float cz = cosf(p.rotation_z), sz = sinf(p.rotation_z);
        const float y1 = o.y * cx - o.z * sx;
        const float z1 = o.y * sx + o.z * cx;
        const float x2 = o.x * cy + z1 * sy;
        const float z2 = -o.x * sy + z1 * cy;
        wx = x2 * cz - y1 * sz;  wy = x2 * sz + y1 * cz;  wz = z2;
    };
    if (!g.rotate_offsets) return g.get_segment_length();
    float ax, ay, az, bx, by, bz;
    rot(pa, g.offset_a, ax, ay, az);
    rot(pb, g.offset_b, bx, by, bz);
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}
} // namespace

bool test_tree_bonds_born_at_rest() {
    printf("\n=== A TREE IS BORN AT REST (issue #38) ===\n\n");

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  engine init failed\n  FAIL\n"); return false; }

    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    PhysicsTreeGenerator gen;
    gen.initialize(&engine);
    TreeSpec spec;
    spec.random_seed = 12345;   // same fixed seed test_foliage_stays_attached uses
    PhysicsTreeResult tree = gen.generate_tree_with_roots(0.0f, 0.0f, 0.0f, spec);
    (void)tree;
    ps.flush_pending_particles();

    const size_t n_gluons = physics.get_total_gluon_count();
    if (n_gluons == 0) {
        printf("  INCONCLUSIVE: the generator produced no bonds.\n  FAIL\n");
        engine.shutdown();
        return false;
    }

    // NO engine.update() ANYWHERE ABOVE. Everything below is the world as the
    // generator left it.
    struct Bad { size_t a, b; float dist, rest, ratio; };
    std::vector<Bad> worst;
    size_t born_taut = 0, born_torn = 0, counted = 0;
    float ratio_sum = 0.0f, ratio_max = 0.0f;

    {
        auto v = ps.lock_particles_for_write();
        std::map<std::pair<size_t,size_t>, bool> seen;
        for (size_t i = 0; i < v.size(); ++i) {
            for (const GluonConstraintBase* g : physics.get_gluons_for_particle(i)) {
                if (!g) continue;
                const size_t a = std::min(g->particle_a, g->particle_b);
                const size_t b = std::max(g->particle_a, g->particle_b);
                if (a >= v.size() || b >= v.size()) continue;
                if (seen.count({a,b})) continue;
                seen[{a,b}] = true;

                // MEASURE WHAT THE BOND ENFORCES: the separation of its two
                // ATTACHMENT POINTS, not of the two centres.
                //
                // This compared centre-to-centre against a rest that is an
                // attachment-point distance, and for leaves those differ by the
                // branch's half-length — offset_a is the branch TIP while
                // offset_b is the leaf CENTRE, and target_distance is recorded
                // tip-to-leaf. That produced the entire "worst offender" list:
                // P8 to its six leaves, placed 1.68 against a rest of 1.40,
                // reported as 1.20x strain when the bond was satisfied.
                float ax, ay, az, bx, by, bz;
                world_attach(v[a], g->offset_a, g->rotate_offsets, ax, ay, az);
                world_attach(v[b], g->offset_b, g->rotate_offsets, bx, by, bz);
                const float dx = bx - ax, dy = by - ay, dz = bz - az;
                const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                // REST, MEASURED IN THE BOND'S OWN FRAME.
                //
                // This used get_segment_length() — the naive |offset_a -
                // offset_b| — on the reasoning that a test must not invent its
                // own definition and should ask the same question the tear law
                // asks. That was the wrong instinct: the tear law's formula was
                // the thing under suspicion, so deferring to it meant measuring
                // its opinion rather than the geometry.
                //
                // The placement ladder settled it. For a parent half-length 1.0
                // and child half-length 0.5, unrotated rest is a flat 1.5 at
                // every angle, while the bond's actual rest — the separation at
                // which centre_b - centre_a == R_a*off_a - R_b*off_b — is
                // 1.1180 at 0 deg rising to 1.5000 at 90 deg, where the two
                // segments finally are collinear and the naive sum is correct.
                // Predicted strains 0.7454 / 0.8189 / 0.8819 / 0.9326 matched
                // the measured ones to four decimals.
                // A distance bond holds its attachments target_distance apart.
                // Bonds that declare 0 (the trunk/branch "touch" bonds) want
                // their attachments coincident, so any separation is error.
                const float rest = std::max(g->target_distance, 0.01f);
                const float ratio = dist / rest;

                counted++;
                ratio_sum += ratio;
                ratio_max = std::fmax(ratio_max, ratio);
                if (ratio > 1.10f) born_taut++;
                if (ratio >= 2.00f) born_torn++;
                if (ratio > 1.10f && worst.size() < 6)
                    worst.push_back({a, b, dist, rest, ratio});
            }
        }
    }

    printf("  bonds inspected at frame ZERO (no physics has run)  %6zu\n", counted);
    printf("  mean strain at birth                               %8.4f  (want 1.0000)\n",
           counted ? ratio_sum / (float)counted : 0.0f);
    printf("  worst strain at birth                              %8.4f\n", ratio_max);
    printf("  bonds born TAUT   (> 1.10x)                        %6zu\n", born_taut);
    printf("  bonds born TORN   (>= 2.00x, the tear ratio)       %6zu\n", born_torn);

    if (!worst.empty()) {
        printf("\n  the worst offenders, as the generator left them:\n");
        for (const Bad& w : worst)
            printf("    P%-4zu<->P%-4zu  placed %7.4f m apart, bond rest %7.4f  -> %6.3fx\n",
                   w.a, w.b, w.dist, w.rest, w.ratio);
    }

    // A BODY IS BORN WITH ITS MATERIAL (G-69): the bond law derives its
    // stiffness and damping from material_type, so a tree part left at the
    // default (FLESH) is a wood beam priced as flesh. Every body of this tree
    // must name a material.
    size_t flesh = 0, bodies = 0;
    {
        auto v = ps.lock_particles_for_read();
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i].is_light_source || v[i].GetMass() <= 0.0f) continue;
            ++bodies;
            if (v[i].material_type == Materials::Type::FLESH) ++flesh;
        }
    }
    printf("  bodies born as FLESH (the unset default)              %zu of %zu  (want 0)\n",
           flesh, bodies);

    const bool pass = (born_torn == 0 && ratio_max <= 1.10f && flesh == 0);
    printf("\n");
    if (!pass) {
        printf("  *** THE TREE IS BORN STRAINED. ***\n"
               "  %zu bonds are already at or past their tear ratio before a single\n"
               "  solver iteration has run, and %zu are taut. The generator places two\n"
               "  bodies at one separation and gives their bond a different one. No\n"
               "  solver change can hold a structure that arrives already broken, which\n"
               "  is why three separate real fixes to the tear path moved nothing.\n",
               born_torn, born_taut);
    } else {
        printf("  BORN AT REST. Every bond agrees with where its bodies were placed.\n");
    }
    printf("\n  %s\n", pass ? "PASS" : "FAIL (a structure must be born at rest)");
    engine.shutdown();
    return pass;
}
