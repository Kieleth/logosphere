// ============================================================================
// GRASS BENDS, IT DOES NOT MOW (issue #47)
// ============================================================================
// THE LAW. Walking through grass bends it. It does not sever it. A blade may
// lean, may be pushed flat, may stay flat — but the pass of a leg must not
// leave a trail of detached blades behind it.
//
// THE DEFECT, measured on the walk gate with its drifter named:
//   worst drifter P277: d = (0.00, -2.52, -0.50) m
//     bonds still attached  0
//     grass bonds in world: 614 -> 605  (NINE TORE in one pass)
// The displacement is purely along the walk axis, and the blade has no bonds
// left, so it was not dragged while attached: it was severed and then carried.
//
// WHY. A blade's root is pinned and its top is pushed by a rotation-blind AABB
// that cannot slide. The bond is a DISTANCE constraint, so the only way it can
// absorb that push is to STRETCH, and it reaches OrganicGluon's 2.0x rest
// ratio and snaps. OrganicSpec already carries the mechanism that would let it
// bend instead — `gluon_quat_drive`, documented as "blades hold their grown
// pose and bend by ROTATING" — and it is false on every species that exists.
// Commit 29ead13 said it in its title a long time ago: the blade must bend by
// rotating, not by shearing.
//
// THE REDUCTION. A kinematic sweeper driven through the patch instead of a
// humanoid: a leg brushing a blade is a moving body pushing on it, so the rig
// around the leg ought not to matter.
//
//   ./build/logosphere-tests --test test_grass_bends_not_tears --no-head
//   SINGLE_LAW=1 ...       the exact-manifold-LCP world (BORN-RED, below)
//   SWEEP_SPEED=8 ...      a swinging foot's pace, not a walking body's
//   TEAR_DEBUG=1 ...       names every bond that snaps and the state that did it
//   GRASS_PARK_SWEEPER=1 . THE CONTROL: same scene, same frames, shin parked
//
// ============================================================================
// BORN-RED TDD FLAG (owner ruling, 2026-09-01)
// ============================================================================
// THE LEVER-OFF WORLD: green, and green on its first run, at 1.2, 2.4, 4.0 and
// 8.0 m/s, with 223 bonds before and 223 after every time.
//
// THE SINGLE-LAW WORLD (SINGLE_LAW=1): the same pass severs bonds and leaves
// blades adrift. The owner ruled this a born-red flag: it stays red, the
// physics is NOT to be fixed here, and no threshold or scene parameter moves.
//
// WHAT THE CONVERSION FOUND, AND IT CHANGES BOTH SENTENCES ABOVE (2026-09-01).
// Instrumenting the INTERACTION instead of inferring it: the shin makes ZERO
// contacts with any blade, in either world, across all 579 frames — while the
// same shin makes 642 contacts with other bodies, so the event stream is not
// silent, it is telling the truth. Measured geometrically as well: the closest
// the two boxes ever come is a 30.3 mm gap in x, the nearest blade sitting at
// x = 0.164 with a 0.147 m width against the shin's 0.12 at x = 0.
//
// Two consequences, both booked, neither fixed here:
//   1  The lever-off green is VACUOUS. Nothing tore because nothing was
//      touched. The file's own claim to be "a guard: the invariant is real and
//      currently holds" is not supported by what it runs.
//   2  The single-law tears happen with NOTHING TOUCHING THE PATCH, and the
//      first one lands at f471 — 72 frames INTO the settle, after the shin has
//      already left. THE CONTROL SETTLES IT: GRASS_PARK_SWEEPER=1 leaves the
//      shin standing at its start line, 4 m short, for exactly the same frames,
//      and every number is identical to the digit — 223 -> 216 bonds, 7 torn,
//      14 adrift, peak blade speed 3.0088 m/s, peak |omega| 9.0084 rad/s, first
//      tear at f471, worst observed strain 2.1746x. The shin contributes
//      nothing. A patch standing on a floor with nobody near it tearing itself
//      apart is INV-34's territory (rest is reached) and INV-14's, not a mowing
//      defect. TEAR_DEBUG names the bodies: they sit at x ~ 1.02, y ~ -1.30,
//      a metre off the sweep line, and every tear is LICENSED — strain 2.01x to
//      2.38x against a 2.00x limit, at 0.02 to 0.6 m/s. The tear law is doing
//      its job; whatever stretched those bonds is upstream of it.
// The premise check is printed as [BOOK], not [FAIL]: reddening the default
// world is an owner ruling, not a conversion's call. The scene, the sweep and
// both original verdict conditions are untouched; everything added is
// instrumentation.
//
// ============================================================================
// FULL-STATE NARRATION — assert or waive, per degree of freedom
// ============================================================================
// THE CAST: 91 floor tiles (KINEMATIC, at rest), one tall-grass patch (the
// generator's own bodies and bonds), one shin-sized KINEMATIC sweeper.
//
// PHASE A — BUILD. Floor, then the patch, then the sweeper. Nothing has moved.
//   bond strain      1.0x before a single solver iteration. ASSERTED (INV-4).
//   bond count       the generator's own count, printed; the baseline every
//                    later count is read against.
//   adrift bodies    the union-find baseline (a component of the bond graph
//                    with no KINEMATIC body in it). ASSERTED against itself
//                    after the pass (INV-14).
//
// PHASE B — THE PASS. The sweeper's position is written by this test, 8 m at
// SWEEP_SPEED through the middle of the patch. It is an external writer, so
// its own DOFs are inputs, not outcomes.
//   sweeper position  written here; not an outcome. WAIVED as physics, but
//                    ASSERTED as measurement hygiene: it really did travel the
//                    8 m, or every "nothing tore" below is vacuous.
//   the contact      "a shin walking through grass" is a CONTACT between those
//                    two bodies, and a test that never checks it happened has
//                    not checked its own premise. MEASURED two ways (the event
//                    stream and the boxes' own overlap) and BOOKED [BOOK], not
//                    asserted: it fails in BOTH worlds, and turning the default
//                    world red is an owner ruling.
//   floor tiles      KINEMATIC and at rest: they take no momentum and do not
//                    move. ASSERTED (INV-7).
//   blade position   where a blade bends to is the bonds' business. WAIVED as
//                    an absolute; the two absolutes that ARE laws are asserted:
//                    nothing below the turtle plane (INV-1) and nothing adrift
//                    from a root (INV-14).
//   blade velocity   a contact may stop an approach; it may never amplify one.
//                    ASSERTED (INV-17), per pair: worst separation speed at a
//                    sweeper contact against worst approach speed at that same
//                    pair, plus the support cushion. Peak blade speed printed
//                    beside the sweeper's own speed.
//   blade omega      THE MECHANISM QUESTION. "Bend by rotating, not by
//                    shearing" is what the header claims the fix is, so peak
//                    |omega| across the patch is the number that says which one
//                    happened. WAIVED from assertion — no registry law names a
//                    rotation a bent blade must reach — and PRINTED, because it
//                    is the discriminator. Listed in the report as a
//                    missing-law candidate.
//   bond strain      must stay under each bond's own tear ratio. ASSERTED
//                    (INV-14). Known blind spot, stated not papered over: the
//                    witness samples once per engine.update() and the tear
//                    fires inside a substep, so the observed peak is a LOWER
//                    bound; the frame the count fell is printed beside it and
//                    TEAR_DEBUG=1 prints the crossing itself.
//   bond count       must not fall. ASSERTED (INV-14).
//   speed ceiling    the explosion detector is INV-11's own instrument.
//                    ASSERTED silent.
//   finiteness       every blade DOF finite at all times. ASSERTED (INV-33).
//
// PHASE C — SETTLE, 180 frames, nobody touches anything. Bending is allowed,
//   leaving is not. Every Phase B assertion is read at the end of Phase C.
// ============================================================================

#include "generated/earth_ontology_registry.h"
#include "generated/physics_constants.h"
#include "../src/core/argus.h"
#include "../src/core/engine.h"
#include "../src/core/explosion_detector.h"
#include "../src/core/particle_system.h"
#include "../src/materials.h"
#include "../src/particle.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/organic_generator.h"
#include "logosphere/worldgen/grass_patch_spec.h"
#include "logosphere/worldgen/scene_chunk_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "logosphere/kg/ontology_registry.h"
#include "scenes/probe_bond_strain.h"

#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

namespace {

struct DisjointSet {
    std::vector<size_t> parent;
    explicit DisjointSet(size_t n) : parent(n) {
        for (size_t i = 0; i < n; ++i) parent[i] = i;
    }
    size_t find(size_t a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    }
    void join(size_t a, size_t b) { a = find(a); b = find(b); if (a != b) parent[b] = a; }
};

// How many blade bodies sit in a bond-graph component containing no KINEMATIC
// body — i.e. how much grass is no longer attached to the ground.
size_t count_adrift(Engine& engine, const std::vector<size_t>& ids) {
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();
    std::map<size_t, size_t> slot;
    for (size_t k = 0; k < ids.size(); ++k) slot[ids[k]] = k;

    DisjointSet ds(ids.size());
    std::vector<bool> rooted(ids.size(), false);
    {
        auto v = ps.lock_particles_for_write();
        for (size_t k = 0; k < ids.size(); ++k) {
            if (ids[k] < v.size())
                rooted[k] = v[ids[k]].solver_mode == ParticleSolverMode::KINEMATIC;
            for (const GluonConstraintBase* g : physics.get_gluons_for_particle(ids[k])) {
                if (!g) continue;
                auto ia = slot.find(g->particle_a), ib = slot.find(g->particle_b);
                if (ia == slot.end() || ib == slot.end()) continue;
                ds.join(ia->second, ib->second);
            }
        }
    }
    std::map<size_t, bool> comp_rooted;
    std::map<size_t, size_t> comp_size;
    for (size_t k = 0; k < ids.size(); ++k) {
        const size_t r = ds.find(k);
        comp_size[r]++;
        comp_rooted[r] = comp_rooted.count(r) ? (comp_rooted[r] || rooted[k]) : rooted[k];
    }
    size_t adrift = 0;
    for (const auto& [root, is_rooted] : comp_rooted)
        if (!is_rooted) adrift += comp_size[root];
    return adrift;
}

int failures = 0;
int booked = 0;
void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void check(bool ok, const char* fmt, ...) {
    char msg[640];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("  %s %s\n", ok ? "[PASS]" : "[FAIL]", msg);
    if (!ok) failures++;
}
// A claim that is TRUE, MEASURED and FAILING, and that this conversion is not
// authorised to make the verdict turn on. It prints as loudly as a [FAIL],
// counts in its own line, and is carried to the owner in TEST_AUDIT. The one
// thing it must never be is quiet.
void book(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void book(bool ok, const char* fmt, ...) {
    char msg[640];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("  %s %s\n", ok ? "[PASS]" : "[BOOK]", msg);
    if (!ok) booked++;
}

bool finite3(float a, float b, float c) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
}

}  // namespace

bool test_grass_bends_not_tears() {
    printf("\n=== GRASS BENDS, IT DOES NOT MOW (issue #47) ===\n\n");
    printf("  A shin-sized KINEMATIC box driven through a generated tall-grass patch.\n");
    printf("  Observed through Argus, the collision-event stream and the bond-strain\n");
    printf("  probe (the tear law's own measure); every assert names its law.\n\n");

    failures = 0;
    booked = 0;
    EngineConfig cfg;
    cfg.create_display = false;
    cfg.enable_chat_window = false;
    cfg.show_debug_overlay = false;
    Engine engine;
    if (engine.initialize(cfg) != 0) { printf("  engine init failed\n  FAIL\n"); return false; }
    {
        kg::OntologyRegistry reg;
        // The union-merged registry validates parents (malleus H2 made real):
        // a runtime extension must declare the chain it claims.
        // The CANONICAL grass vocabulary: the generated earth-pack
        // registry. Hand-rolled fixture registries drifted from the
        // generator's real writes 16 keys deep (2026-08-14).
        (void)reg;
        engine.get_kg().extendOntology(earth::ontology::registry());
    }
    auto& ps = engine.get_particle_system();
    auto& physics = engine.get_physics_system();

    // A floor to stand the grass on.
    std::vector<size_t> tiles;
    for (int cx = -3; cx <= 3; ++cx)
        for (int cy = -6; cy <= 6; ++cy) {
            Particle t = {};
            t.shape = ParticleShape::BOX;
            t.x = (float)cx; t.y = (float)cy; t.z = 0.05f;
            t.width = t.height = 1.0f; t.thickness = 0.1f; t.size = 1.0f;
            t.SetMaterial(Materials::Type::STONE);
            const int id = engine.add_particle(t);
            ps.flush_pending_particles();
            auto v = ps.lock_particles_for_write();
            v[id].solver_mode = ParticleSolverMode::KINEMATIC;
            v[id].owner = ParticleOwner::DYNAMICS;
            v[id].is_at_rest = true;
            tiles.push_back((size_t)id);
        }

    const size_t before_bodies = [&]{ auto v = ps.lock_particles_for_write(); return v.size(); }();

    auto& ogen  = engine.get_worldgen_system().get_organic_generator();
    auto& scene = engine.get_worldgen_system().get_scene_generator();
    kg::EntityID patch = ogen.generate_grass_patch(0.0f, 0.0f, 0.10f,
                                                   GrassPatchSpec::tall_grass());
    scene.activate_entity_now(patch);
    ps.flush_pending_particles();

    std::vector<size_t> grass;
    {
        auto v = ps.lock_particles_for_write();
        for (size_t i = before_bodies; i < v.size(); ++i) grass.push_back(i);
    }
    const size_t bonds_before  = physics.get_total_gluon_count();
    const size_t adrift_before = count_adrift(engine, grass);

    // ---- PHASE A: born ---------------------------------------------------
    // INV-4 is stated about frame zero, before a single solver iteration.
    const probe::BondCensus born = probe::census(ps, physics, grass);

    // THE SWEEPER: a shin-sized kinematic box driven through the patch at
    // walking pace. Kinematic because a leg's motion is commanded, not
    // negotiated — the same reason a humanoid's legs are quat-driven.
    Particle shin = {};
    shin.shape = ParticleShape::BOX;
    shin.x = 0.0f; shin.y = -4.0f; shin.z = 0.45f;
    shin.width = 0.12f; shin.height = 0.12f; shin.thickness = 0.80f; shin.size = 0.12f;
    shin.r = 0.8f; shin.g = 0.6f; shin.b = 0.5f; shin.a = 1.0f;
    shin.SetMaterial(Materials::Type::FLESH);
    const int sweeper = engine.add_particle(shin);
    ps.flush_pending_particles();
    {
        auto v = ps.lock_particles_for_write();
        v[sweeper].solver_mode = ParticleSolverMode::KINEMATIC;
        v[sweeper].owner = ParticleOwner::DYNAMICS;
        v[sweeper].is_at_rest = false;
    }
    const float sweeper_y0 = -4.0f;

    // THE WITNESS. Argus watches the sweeper and, so the log has a named
    // subject rather than a max with no owner, the blade nearest its path.
    logosphere::Argus argus;
    argus.watch(sweeper, "sweeper");
    size_t hero = grass.empty() ? (size_t)sweeper : grass.front();
    {
        auto v = ps.lock_particles_for_read();
        float best = 1e9f;
        for (size_t id : grass) {
            if (id >= v.size()) continue;
            const float d = std::fabs(v[id].x);      // the sweep runs along x = 0
            if (d < best) { best = d; hero = id; }
        }
    }
    argus.watch((int)hero, "hero blade");
    logosphere::expdet::reset();
    const bool expdet_on = logosphere::expdet::enabled();

    // ---- PHASE B: the pass ----------------------------------------------
    // 8 m at 1.2 m/s, straight through the middle of the patch.
    // SWEEP_SPEED overrides the pace. A walking BODY moves at 1.2 m/s but a
    // swinging FOOT moves several times that, so "walking speed" is the wrong
    // number for the thing that actually hits a blade.
    const char* sp_env = std::getenv("SWEEP_SPEED");
    const float SPEED = sp_env ? (float)std::atof(sp_env) : 1.2f;
    constexpr float DT = 1.0f / 60.0f;
    const int FRAMES = (int)(8.0f / (SPEED * DT));

    probe::StrainWatch strain;
    int    tear_frame = -1;
    float  strain_before_tear = 0.0f;
    int    strain_before_tear_frame = -1;
    size_t prev_bonds = (size_t)-1;
    float  prev_peak_strain = 0.0f;

    float  peak_blade_speed = 0.0f;   size_t peak_blade_id = 0;
    float  peak_blade_spin = 0.0f;    size_t peak_spin_id = 0;
    float  lowest_bottom = 1e9f;      size_t lowest_id = 0;
    bool   all_finite = true;
    float  tile_drift = 0.0f, tile_speed = 0.0f;   // INV-7

    // INV-17, per pair: a contact may stop an approach, never amplify one.
    std::map<std::pair<size_t, size_t>, float> worst_approach, worst_separate;
    size_t sweeper_contacts = 0;
    float  sweeper_worst_approach = 0.0f;   size_t sweeper_first_blade = 0;
    size_t sweeper_any_contacts = 0;        // with ANY body, blade or not

    // THE PREMISE, measured geometrically as well as through the event stream.
    // "0 contacts" has two very different causes and they must not look the
    // same: the shin never got near a blade (a SCENE fact), or it passed
    // through one and the engine emitted nothing (an ENGINE fact). The
    // axis-aligned overlap of the two boxes separates them.
    float  worst_overlap = -1e9f;   size_t overlap_blade = 0;  int overlap_frame = -1;
    float  ov_x = 0, ov_y = 0, ov_z = 0;          // per axis, at that moment
    float  ov_bx = 0, ov_by = 0, ov_bz = 0;       // and the blade's box
    float  ov_bw = 0, ov_bh = 0, ov_bt = 0;

    std::set<size_t> grass_set(grass.begin(), grass.end());
    int frame = 0;

    auto observe = [&]() {
        argus.observe(ps, frame);
        const probe::BondCensus c = probe::census(ps, physics, grass);
        strain.take(c, frame);
        if (prev_bonds != (size_t)-1 && c.bonds < prev_bonds && tear_frame < 0) {
            tear_frame = frame;
            strain_before_tear = prev_peak_strain;
            strain_before_tear_frame = frame - 1;
        }
        prev_bonds = c.bonds;
        prev_peak_strain = c.peak_strain;

        {   // every blade DOF, every frame
            auto v = ps.lock_particles_for_read();
            for (size_t id : grass) {
                if (id >= v.size()) continue;
                const Particle& p = v[id];
                if (!finite3(p.x, p.y, p.z) || !finite3(p.vx, p.vy, p.vz) ||
                    !finite3(p.omega_x, p.omega_y, p.omega_z) ||
                    !finite3(p.rotation_x, p.rotation_y, p.rotation_z))
                    all_finite = false;                                // INV-33
                const float sp = std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz);
                if (sp > peak_blade_speed) { peak_blade_speed = sp; peak_blade_id = id; }
                const float om = std::sqrt(p.omega_x*p.omega_x + p.omega_y*p.omega_y
                                         + p.omega_z*p.omega_z);
                if (om > peak_blade_spin) { peak_blade_spin = om; peak_spin_id = id; }
                const float bot = probe::body_bottom(p);               // INV-1
                if (bot < lowest_bottom) { lowest_bottom = bot; lowest_id = id; }
            }
            // Axis-aligned overlap, shin against every blade: positive means
            // the two boxes share space this frame.
            if ((size_t)sweeper < v.size()) {
                const Particle& s = v[sweeper];
                for (size_t id : grass) {
                    if (id >= v.size()) continue;
                    const Particle& p = v[id];
                    const float ox = (s.width  + p.width)  * 0.5f - std::fabs(s.x - p.x);
                    const float oy = (s.height + p.height) * 0.5f - std::fabs(s.y - p.y);
                    const float oz = (s.thickness + p.thickness) * 0.5f - std::fabs(s.z - p.z);
                    const float ov = std::fmin(ox, std::fmin(oy, oz));
                    if (ov > worst_overlap) {
                        worst_overlap = ov; overlap_blade = id; overlap_frame = frame;
                        ov_x = ox; ov_y = oy; ov_z = oz;
                        ov_bx = p.x; ov_by = p.y; ov_bz = p.z;
                        ov_bw = p.width; ov_bh = p.height; ov_bt = p.thickness;
                    }
                }
            }
            for (size_t id : tiles) {                                  // INV-7
                if (id >= v.size()) continue;
                const Particle& p = v[id];
                const float want_x = std::round(p.x), want_y = std::round(p.y);
                tile_drift = std::fmax(tile_drift,
                    std::sqrt((p.x-want_x)*(p.x-want_x) + (p.y-want_y)*(p.y-want_y)
                            + (p.z-0.05f)*(p.z-0.05f)));
                tile_speed = std::fmax(tile_speed,
                    std::sqrt(p.vx*p.vx + p.vy*p.vy + p.vz*p.vz));
            }
        }

        // THE INTERACTION, instrumented rather than inferred (owner directive
        // 2026-08-15): relative_velocity is NEGATIVE while approaching and
        // positive while separating, at the emission site's own normal.
        for (const CollisionEvent& e : physics.get_collision_events()) {
            const bool a_grass = grass_set.count(e.particle_a) != 0;
            const bool b_grass = grass_set.count(e.particle_b) != 0;
            const bool touches_sweeper =
                (e.particle_a == (size_t)sweeper) || (e.particle_b == (size_t)sweeper);
            if (touches_sweeper) sweeper_any_contacts++;
            if (touches_sweeper && (a_grass || b_grass)) {
                if (sweeper_contacts == 0)
                    sweeper_first_blade = a_grass ? e.particle_a : e.particle_b;
                sweeper_contacts++;
                if (-e.relative_velocity > sweeper_worst_approach)
                    sweeper_worst_approach = -e.relative_velocity;
            }
            if (!a_grass && !b_grass) continue;
            const std::pair<size_t, size_t> key{std::min(e.particle_a, e.particle_b),
                                                std::max(e.particle_a, e.particle_b)};
            if (e.relative_velocity < 0.0f)
                worst_approach[key] = std::fmax(worst_approach[key], -e.relative_velocity);
            else
                worst_separate[key] = std::fmax(worst_separate[key], e.relative_velocity);
        }
        frame++;
    };

    // THE CONTROL (default OFF, GRASS_PARK_SWEEPER=1). A check is not proven
    // non-vacuous until the case that must come out the other way has been run.
    // Here the control is the shin PARKED at its start line, 4 m short of the
    // patch, for exactly the same number of frames: same bodies, same ids, same
    // stepping, no pass. If the patch tears anyway, the tears were never the
    // shin's, and the test's name is describing something that did not happen.
    static const bool parked = std::getenv("GRASS_PARK_SWEEPER") != nullptr;
    for (int f = 0; f < FRAMES; ++f) {
        if (!parked) { auto v = ps.lock_particles_for_write();
          v[sweeper].y += SPEED * DT; v[sweeper].vy = SPEED; }
        engine.update(DT);
        observe();
    }
    const float sweeper_travel = [&]{
        auto v = ps.lock_particles_for_read(); return v[sweeper].y - sweeper_y0;
    }();

    // ---- PHASE C: settle -------------------------------------------------
    // Let it settle after the pass: bending is allowed, leaving is not.
    for (int f = 0; f < 180; ++f) { engine.update(DT); observe(); }

    const size_t bonds_after  = physics.get_total_gluon_count();
    const size_t adrift_after = count_adrift(engine, grass);
    const size_t tore = (bonds_before > bonds_after) ? (bonds_before - bonds_after) : 0;

    // INV-17's worst pair: how much faster a contact sent a pair away than it
    // ever saw them close.
    float worst_amplify = 0.0f;
    size_t amp_a = 0, amp_b = 0; float amp_sep = 0.0f, amp_app = 0.0f;
    for (const auto& [key, sep] : worst_separate) {
        const auto it = worst_approach.find(key);
        const float app = it == worst_approach.end() ? 0.0f : it->second;
        if (sep - app > worst_amplify) {
            worst_amplify = sep - app;
            amp_a = key.first; amp_b = key.second; amp_sep = sep; amp_app = app;
        }
    }

    const logosphere::expdet::Stats st = logosphere::expdet::stats();
    const uint64_t warnings =
        st.speed_warnings + st.energy_warnings + st.escape_warnings;

    // ---- the measures ----------------------------------------------------
    printf("  [measure] A blade bodies in the patch      %6zu\n", grass.size());
    printf("  [measure] A bonds at birth                 %6zu, worst strain %.4fx "
           "(limit %.2fx) on P%zu<->P%zu\n",
           born.bonds, born.peak_strain, born.peak_limit, born.peak_a, born.peak_b);
    printf("  [measure] A bodies adrift at birth         %6zu\n", adrift_before);
    printf("  [measure] B sweep                          %.1f m/s, %d frames, "
           "travelled %.4f m (wanted 8.0)\n", SPEED, FRAMES, sweeper_travel);
    printf("  [measure] B sweeper-to-blade contacts      %6zu, worst approach "
           "%.4f m/s, first blade P%zu\n",
           sweeper_contacts, sweeper_worst_approach, sweeper_first_blade);
    printf("  [measure] B sweeper contacts, any body     %6zu\n", sweeper_any_contacts);
    printf("  [measure] B closest shin-to-blade box      %+.4f m (positive = they "
           "shared space) worst on P%zu at f%d\n"
           "                                             per axis x %+.4f  y %+.4f  "
           "z %+.4f\n"
           "                                             shin box  %.3f x %.3f x %.3f "
           "at x %.3f z %.3f\n"
           "                                             blade box %.3f x %.3f x %.3f "
           "at (%.3f, %.3f, %.3f)\n",
           worst_overlap, overlap_blade, overlap_frame, ov_x, ov_y, ov_z,
           shin.width, shin.height, shin.thickness, shin.x, shin.z,
           ov_bw, ov_bh, ov_bt, ov_bx, ov_by, ov_bz);
    printf("  [measure] C bonds, before -> after         %6zu -> %zu   (torn %zu, want 0)\n",
           bonds_before, bonds_after, tore);
    printf("  [measure] C bodies adrift from any root    %6zu   (want %zu)\n",
           adrift_after, adrift_before);
    printf("  [measure] C worst bond strain observed     %.4fx of %.2fx at f%d "
           "on P%zu<->P%zu (dist %.4f m, rest %.4f m)\n",
           strain.peak_strain, strain.peak_limit, strain.peak_frame,
           strain.peak_a, strain.peak_b, strain.peak_dist, strain.peak_rest);
    if (tear_frame >= 0)
        printf("  [measure] C first tear                     bond count fell at f%d; "
               "last strain the witness could see %.4fx at f%d\n"
               "                                             (LOWER BOUND: the tear "
               "fires inside a substep. TEAR_DEBUG=1 prints the crossing.)\n",
               tear_frame, strain_before_tear, strain_before_tear_frame);
    printf("  [measure] C peak blade speed               %.4f m/s on P%zu "
           "(the sweeper travels at %.4f)\n",
           peak_blade_speed, peak_blade_id, SPEED);
    printf("  [measure] C peak blade |omega|             %.4f rad/s on P%zu\n",
           peak_blade_spin, peak_spin_id);
    printf("  [measure] C worst contact amplification    %+.4f m/s on P%zu<->P%zu "
           "(separated %.4f, ever approached %.4f; cushion %.1f)\n",
           worst_amplify, amp_a, amp_b, amp_sep, amp_app,
           PhysicsV4::CONTACT_CAPTURE_CUSHION);
    printf("  [measure] C lowest blade down-reach        %.6f m on P%zu "
           "(turtle %.3f, slop %.4f)\n",
           lowest_bottom, lowest_id, PhysicsV4::TURTLE_Z, PhysicsV4::SLOP);
    printf("  [measure] C floor tiles                    drift %.2e m, peak |v| %.2e m/s\n",
           tile_drift, tile_speed);
    printf("  [measure] C detector                       %llu warning(s), worst body "
           "P%d at %.3f m/s (ceiling %.1f, detector %s)\n",
           (unsigned long long)warnings, st.worst_id, st.worst_speed,
           PhysicsV4::EXPDET_SPEED_CEILING, expdet_on ? "on" : "OFF");
    printf("  [waive]   blade |omega|: no registry law names a rotation a bent blade "
           "must reach.\n"
           "            It is the DISCRIMINATOR between bending and shearing and is "
           "printed above;\n"
           "            listed as a missing-law candidate rather than asserted "
           "against a number nobody ruled.\n");
    printf("\n");

    check(born.peak_strain <= 1.0f + 1e-3f,
          "INV-4: the patch is born at strain 1.0 (worst %.4fx before any solver "
          "iteration)", born.peak_strain);
    if (parked)
        printf("  [CONTROL] GRASS_PARK_SWEEPER=1: the shin never moved. Every number "
               "above is the patch on its own.\n");
    check(parked || sweeper_travel > 7.9f,
          "hygiene: the sweeper really crossed the patch (%.4f m of 8.0) — without "
          "this every count below is vacuous", sweeper_travel);
    book(sweeper_contacts > 0,
         "hygiene: THE SHIN NEVER TOUCHES THE GRASS. %zu sweeper-to-blade contacts "
         "in %d frames, while the same shin made %zu contacts with other bodies; "
         "closest the two boxes ever came was %+.4f m (a %.1f mm GAP in x). The "
         "test's own premise does not happen, so 'nothing tore' under the default "
         "lever is vacuous and the single-law tears happen with NOTHING touching "
         "the patch. BOOKED, not failed: turning the default world red is an "
         "owner ruling, not a conversion's call",
         sweeper_contacts, frame, sweeper_any_contacts, worst_overlap,
         -worst_overlap * 1000.0f);
    check(tile_drift <= 1e-6f && tile_speed <= 1e-6f,
          "INV-7: the KINEMATIC floor takes no momentum (drift %.2e m, peak |v| "
          "%.2e m/s)", tile_drift, tile_speed);
    check(all_finite, "INV-33: every blade DOF stays finite for the whole run");
    check(lowest_bottom >= PhysicsV4::TURTLE_Z - PhysicsV4::SLOP,
          "INV-1: no blade reaches below the turtle plane beyond slop (lowest "
          "%.6f m on P%zu, floor %.6f m)",
          lowest_bottom, lowest_id, PhysicsV4::TURTLE_Z - PhysicsV4::SLOP);
    check(warnings == 0,
          "INV-11: the explosion detector stays silent (%llu warning(s), worst body "
          "%.3f m/s vs ceiling %.1f)",
          (unsigned long long)warnings, st.worst_speed, PhysicsV4::EXPDET_SPEED_CEILING);
    check(worst_amplify <= PhysicsV4::CONTACT_CAPTURE_CUSHION,
          "INV-17: a contact stops an approach, it never amplifies one (worst "
          "%+.4f m/s over what the pair ever closed at, P%zu<->P%zu, cushion %.1f)",
          worst_amplify, amp_a, amp_b, PhysicsV4::CONTACT_CAPTURE_CUSHION);
    check(strain.peak_strain < strain.peak_limit,
          "INV-14: no OBSERVED frame stands a bond at or past its tear ratio "
          "(worst %.4fx of %.2fx at f%d on P%zu<->P%zu)",
          strain.peak_strain, strain.peak_limit, strain.peak_frame,
          strain.peak_a, strain.peak_b);

    const bool nothing_tore = (tore == 0);
    const bool still_held   = (adrift_after <= adrift_before);
    check(nothing_tore,
          "INV-14: one pass of a shin severs nothing (%zu bond(s) torn, %zu -> %zu)",
          tore, bonds_before, bonds_after);
    check(still_held,
          "INV-14: every blade still reaches the ground through its bonds "
          "(%zu adrift, was %zu)", adrift_after, adrift_before);

    printf("\n");
    if (!nothing_tore) {
        printf("  *** THE GRASS WAS MOWED. *** One pass of a shin severed %zu bonds.\n"
               "  A blade can only absorb a sideways push by STRETCHING, because its\n"
               "  bond is a distance constraint and its root is pinned, so it reaches\n"
               "  the 2.0x tear ratio and snaps. OrganicSpec::gluon_quat_drive exists\n"
               "  for exactly this — 'blades hold their grown pose and bend by\n"
               "  ROTATING' — and is false on every species in the tree.\n"
               "  Peak blade |omega| this run: %.4f rad/s. If that is ~0 while blades\n"
               "  are being severed, they were SHEARED, not bent.\n", tore, peak_blade_spin);
    } else if (!still_held) {
        printf("  *** BLADES CAME LOOSE. *** No bond broke, but %zu bodies ended in a\n"
               "  component with no root, so something else detached them.\n", adrift_after);
    } else {
        printf("  IT BENT. One pass at %.1f m/s, nothing severed, every body still\n"
               "  reaches the ground through its bonds. Peak blade |omega| %.4f rad/s.\n",
               SPEED, peak_blade_spin);
    }

    printf("\n  %d assertion(s) failed, %d claim(s) BOOKED for owner ruling\n",
           failures, booked);
    const bool pass = (failures == 0);
    printf("\n  %s\n", pass ? "PASS" : "FAIL (walking through grass is mowing it)");
    engine.shutdown();
    return pass;
}
