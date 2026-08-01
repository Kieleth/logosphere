// Headless acceptance checks for the jump mechanic (try_jump/is_grounded)
// added to HumanoidLocomotion for this example. Follows
// tests/test_humanoid_headless.cpp's pattern exactly: raw ParticleSystem
// + PhysicsSystem + ParticleDynamicsSystem + HumanoidLocomotion via their
// *_headless initializers -- no Engine, no GLFW, no Metal at all. That
// matters here specifically: this machine's windowed rendering pipeline
// renders solid black (see the "windowed rendering" project issue) and,
// it turns out, even Engine's headless (create_display=false) GPU path
// is unreliable on this machine (intermittent multi-second stalls and
// occasional hangs waiting on async GPU completion, observed while
// developing this). This harness sidesteps that class of problem
// entirely by never touching the GPU/rendering pipeline, matching how
// the engine's own physics-linux CI already validates locomotion.
//
// This does NOT reuse examples/threshold/src/scene.h's build_level/
// spawn_humanoid -- those are Engine-coupled (StrataFloorGenerator,
// HumanoidGenerator, both accessed via Engine::get_worldgen_system()).
// The windowed app is still the one exercising the real level/humanoid
// construction; this only needs to prove the new jump physics itself is
// correct, so a stub humanoid + a couple of flat static slabs (mirroring
// test_humanoid_headless.cpp's own stub-body approach) is enough.
//
// Usage:
//   ./threshold_verify

#include "core/particle_system.h"
#include "core/particle_tracer.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/dynamics/animation_primitives.h"
#include "logosphere/dynamics/particle_dynamics_system.h"
#include "logosphere/events/event_bus.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/physics/physics_system.h"
#include "particle.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int tests_passed = 0;
int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS\n"; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << "\n"; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

// Bundles the *_headless subsystem quintet test_humanoid_headless.cpp
// constructs individually, plus the stub-humanoid spawner, so each test
// below is just setup + assertions.
struct Rig {
    ParticleSystem ps;
    kg::OntologyRegistry registry;
    kg::KGModule kg{registry};
    PhysicsSystem physics;
    ParticleTracer tracer;
    ParticleDynamicsSystem dyn;
    logosphere::animation::HumanoidLocomotion humanoid;
    logosphere::interaction::ParticleInteractionSystem interaction;
    logosphere::EventBus bus;

    int hips_id = -1;

    Rig() {
        if (!physics.initialize(ps)) throw std::runtime_error("PhysicsSystem::initialize failed");
        if (!dyn.initialize_headless(ps)) throw std::runtime_error("ParticleDynamicsSystem::initialize_headless failed");
        if (!humanoid.initialize_headless(ps, physics, kg, dyn, tracer)) {
            throw std::runtime_error("HumanoidLocomotion::initialize_headless failed");
        }
        physics.set_interaction_system(&interaction);
    }

    int spawn_particle(float x, float y, float z, float w, float h, float t,
                       Materials::Type mat, bool is_at_rest, uint32_t profile_id = 0) {
        Particle p{};
        p.x = x; p.y = y; p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = w; p.height = h; p.thickness = t;
        p.SetMaterial(mat);
        p.is_at_rest = is_at_rest;
        p.interaction_profile_id = profile_id;
        int id = ps.queue_particle_addition(p);
        ps.flush_pending_particles();
        return id;
    }

    // A flat static slab -- ground to stand/land on. Not the real
    // level's StrataFloorGenerator tiles, just enough geometry for
    // is_grounded()'s BVH support query to find something.
    void add_floor_slab(float center_x, float width) {
        spawn_particle(center_x, 0.0f, 0.0f, width, 2.0f, 0.2f,
                       Materials::Type::STONE, /*is_at_rest=*/true);
        ps.update_bvh();
    }

    // Same stub proportions test_humanoid_headless.cpp uses -- what
    // matters for these tests is real leg/hips physics, not accurate
    // body geometry.
    void spawn_stub_humanoid(float x) {
        auto spawn = [&](float dx, float z) {
            return spawn_particle(x + dx, 0.0f, z, 0.1f, 0.1f, 0.1f,
                                  Materials::Type::FLESH, false);
        };
        int hips    = spawn(0.0f, 1.00f);
        int abdomen = spawn(0.0f, 1.10f);
        int chest   = spawn(0.0f, 1.30f);
        int neck    = spawn(0.0f, 1.50f);
        int head    = spawn(0.0f, 1.65f);
        std::vector<int> torso_ids = {hips, abdomen, chest, neck, head};

        auto leg = [&](float side_x) {
            return std::vector<int>{spawn(side_x, 0.05f), spawn(side_x, 0.45f), spawn(side_x, 0.85f)};
        };
        auto arm = [&](float side_x) {
            return std::vector<int>{spawn(side_x, 1.30f), spawn(side_x, 1.05f), spawn(side_x, 0.75f)};
        };
        std::vector<int> left_leg = leg(-0.10f);
        std::vector<int> right_leg = leg(0.10f);
        std::vector<int> left_arm = arm(-0.20f);
        std::vector<int> right_arm = arm(0.20f);

        humanoid.register_humanoid_direct(
            hips, left_leg, right_leg, left_arm, right_arm,
            torso_ids, 210.0f, 520.0f, kg::INVALID_ENTITY);
        humanoid.set_volitional(hips, true);
        hips_id = hips;
    }

    float z() {
        auto view = ps.lock_particles_for_read();
        return view[hips_id].z;
    }
    float x() {
        auto view = ps.lock_particles_for_read();
        return view[hips_id].x;
    }

    void tick(int frames, double dt = 1.0 / 60.0) {
        // Mirrors Engine::update()'s real per-frame ordering (engine.cpp):
        // humanoid pre-physics -> PhysicsSystem::update (solves everything
        // that ISN'T a DYNAMICS-owned humanoid particle, e.g. a plain
        // floor-grid tile) -> dynamics/humanoid post-physics. The earlier
        // jump tests only needed the humanoid calls since maintain_entity_
        // shape self-integrates DYNAMICS particles: a plain physics-solved
        // particle (like a woken floor tile) needs physics.update() too.
        for (int i = 0; i < frames; ++i) {
            humanoid.update_pre_physics(dt);
            physics.update(dt);
            interaction.process_filtered_overlaps(physics.get_filtered_overlaps(), &bus);
            dyn.update_post_physics(dt);
            humanoid.update_post_physics(dt);
            ps.update_bvh();
        }
    }
};

// ---------------------------------------------------------------------
// A grounded humanoid can jump (real height gain, then lands again), and
// cannot jump again while still airborne.
// ---------------------------------------------------------------------
void jump_rises_and_lands() {
    Rig rig;
    rig.add_floor_slab(0.0f, 4.0f);
    rig.spawn_stub_humanoid(0.0f);

    rig.tick(60);   // settle onto the floor under gravity
    ASSERT_TRUE(rig.humanoid.is_grounded(rig.hips_id), "expected grounded after settling");
    const float ground_z = rig.z();

    ASSERT_TRUE(rig.humanoid.try_jump(rig.hips_id, 1.2f), "try_jump should succeed while grounded");
    ASSERT_TRUE(!rig.humanoid.try_jump(rig.hips_id, 1.2f),
                "try_jump should refuse a second jump before landing (no double-jump)");

    float peak_z = ground_z;
    bool left_ground = false;
    for (int i = 0; i < 90; ++i) {
        rig.tick(1);
        peak_z = std::max(peak_z, rig.z());
        if (!rig.humanoid.is_grounded(rig.hips_id)) left_ground = true;
    }

    ASSERT_TRUE(left_ground, "expected is_grounded() to go false during the jump");
    ASSERT_TRUE(peak_z > ground_z + 0.3f,
                "expected meaningful height gain (>0.3m), got " + std::to_string(peak_z - ground_z));
    ASSERT_TRUE(rig.humanoid.is_grounded(rig.hips_id), "expected grounded again after landing");
    ASSERT_TRUE(std::abs(rig.z() - ground_z) < 0.2f,
                "expected to land back near the original height, drift=" +
                std::to_string(rig.z() - ground_z));
}

// ---------------------------------------------------------------------
// try_jump() refuses while airborne (falling, not just after a jump).
// ---------------------------------------------------------------------
void jump_refuses_while_falling() {
    Rig rig;
    rig.add_floor_slab(0.0f, 4.0f);
    rig.spawn_stub_humanoid(0.0f);
    rig.tick(60);
    ASSERT_TRUE(rig.humanoid.is_grounded(rig.hips_id), "expected grounded after settling");

    // Teleport into the air (no floor beneath) and let it start falling.
    {
        auto view = rig.ps.lock_particles_for_write();
        view[rig.hips_id].z += 3.0f;
        view[rig.hips_id].vz = 0.0f;
    }
    rig.tick(10);
    ASSERT_TRUE(!rig.humanoid.is_grounded(rig.hips_id), "expected airborne after the teleport");
    ASSERT_TRUE(!rig.humanoid.try_jump(rig.hips_id, 1.2f),
                "try_jump should refuse while falling, not just right after a jump");
}

// ---------------------------------------------------------------------
// A running jump clears a horizontal gap between two floor slabs.
// ---------------------------------------------------------------------
void jump_clears_a_gap() {
    Rig rig;
    // Two slabs with a 1.5m gap between x=[-0.75, 0.75] -- no floor
    // there at all, matching the real level's spike-pit gap shape.
    rig.add_floor_slab(-2.75f, 4.0f);   // covers roughly [-4.75, -0.75]
    rig.add_floor_slab(2.75f, 4.0f);    // covers roughly [0.75, 4.75]
    rig.spawn_stub_humanoid(-2.0f);

    rig.tick(60);
    ASSERT_TRUE(rig.humanoid.is_grounded(rig.hips_id), "expected grounded before the gap");

    rig.humanoid.set_target_velocity(rig.hips_id, 3.0f, 0.0f);
    rig.tick(20);   // build up a run before the gap
    ASSERT_TRUE(rig.humanoid.try_jump(rig.hips_id, 1.2f), "expected the jump at the gap to succeed");

    rig.tick(90);   // let the arc play out while still running forward
    rig.humanoid.set_target_velocity(rig.hips_id, 0.0f, 0.0f);
    rig.tick(20);

    const bool cleared = rig.humanoid.is_grounded(rig.hips_id) && rig.x() > 0.75f;
    ASSERT_TRUE(cleared,
                "expected to clear the gap and land past x=0.75, got x=" + std::to_string(rig.x()) +
                " grounded=" + std::to_string(rig.humanoid.is_grounded(rig.hips_id)));
}

// ---------------------------------------------------------------------
// create_fk_sword_swing_right() registers and plays as a normal FK clip
// (is_fk_animation_playing() true while it runs) and actually finishes --
// proves the guard's new clip integrates with the existing FK player
// rather than just that it type-checks against FKAnimationClip.
// ---------------------------------------------------------------------
void sword_swing_clip_plays_and_completes() {
    Rig rig;
    rig.add_floor_slab(0.0f, 4.0f);
    rig.spawn_stub_humanoid(0.0f);
    rig.tick(30);   // settle

    ASSERT_TRUE(rig.humanoid.register_fk_animation(
                    rig.hips_id, "sword_swing", create_fk_sword_swing_right(0.5f)),
                "expected register_fk_animation to accept the sword swing clip");
    ASSERT_TRUE(!rig.humanoid.is_fk_animation_playing(rig.hips_id),
                "expected no FK animation playing before play_fk_animation");

    ASSERT_TRUE(rig.humanoid.play_fk_animation(rig.hips_id, "sword_swing"),
                "expected play_fk_animation to start the sword swing");
    ASSERT_TRUE(rig.humanoid.is_fk_animation_playing(rig.hips_id),
                "expected is_fk_animation_playing() true right after starting");

    // windup(<=260ms) + strike(<=90ms) + hold(70ms) + recovery(260ms) is
    // well under 1s; 90 ticks (1.5s) must be enough for it to finish.
    for (int i = 0; i < 90; ++i) rig.tick(1);

    ASSERT_TRUE(!rig.humanoid.is_fk_animation_playing(rig.hips_id),
                "expected the swing to have finished within 1.5s");
}

// ---------------------------------------------------------------------
// A sensor-only spike profile (collides_with = 0, declares a medium)
// fires a VolumeEvent when a default-profile body overlaps it, and a
// matching exit event once it moves away -- proves the mechanism
// scene.cpp's spawn_spikes and threshold_app.cpp's update_hazards
// depend on, rather than just that the two calls exist.
// ---------------------------------------------------------------------
void spike_volume_trigger_fires_on_entry() {
    using logosphere::interaction::InteractionProfile;
    Rig rig;

    constexpr uint32_t kSpikeProfileId = 1;
    InteractionProfile spikes;
    spikes.id = kSpikeProfileId;
    spikes.category = 1u << 1;
    spikes.collides_with = 0;      // sensor only, matches scene.cpp's spawn_spikes
    spikes.drag_coefficient = 0.01f;
    rig.interaction.register_profile(spikes);

    // Spans x=[4,5] at pit-floor height, mirroring scene.cpp's placement.
    rig.spawn_particle(4.5f, 0.0f, 0.05f, 1.0f, 1.0f, 0.1f,
                       Materials::Type::IRON, /*is_at_rest=*/true, kSpikeProfileId);

    auto reader = rig.bus.volume().create_reader();

    // Humanoid spawned well clear of the spikes -- no entry yet.
    rig.spawn_stub_humanoid(0.0f);
    rig.tick(2);
    ASSERT_TRUE(reader.drain().empty(), "expected no volume event before overlapping the spikes");

    // Move the whole body onto the spikes (feet sit at z=0.05, same
    // height as the spike sensor) and confirm entry fires.
    // spawn_stub_humanoid doesn't expose per-part ids, so reposition by
    // offsetting every particle it created (ids are contiguous from 1
    // since this Rig's spike -- id 0 -- was the only prior spawn).
    {
        auto view = rig.ps.lock_particles_for_write();
        const float dx = 4.5f;
        for (size_t i = 1; i < view.size(); ++i) view[i].x += dx;  // skip the spike (id 0)
    }
    rig.ps.update_bvh();
    rig.tick(2);

    auto entered = reader.drain();
    bool saw_entry = false;
    for (const auto& ev : entered) {
        if (ev.entered.value_or(false) && ev.medium_profile.value_or(0) == static_cast<int32_t>(kSpikeProfileId)) {
            saw_entry = true;
        }
    }
    ASSERT_TRUE(saw_entry, "expected a VolumeEvent(entered=true) for the spike profile after overlapping");

    // Move back off the spikes and confirm the episode closes (exit event).
    {
        auto view = rig.ps.lock_particles_for_write();
        for (size_t i = 1; i < view.size(); ++i) view[i].x -= 4.5f;
    }
    rig.ps.update_bvh();
    rig.tick(2);

    auto exited = reader.drain();
    bool saw_exit = false;
    for (const auto& ev : exited) {
        if (!ev.entered.value_or(true) && ev.medium_profile.value_or(0) == static_cast<int32_t>(kSpikeProfileId)) {
            saw_exit = true;
        }
    }
    ASSERT_TRUE(saw_exit, "expected a VolumeEvent(entered=false) once the body left the spikes");
}

// ---------------------------------------------------------------------
// A create_floor_grid() tile stays put while is_at_rest, and actually
// falls once woken -- this combination (a tile from create_floor_grid,
// later woken via PhysicsSystem::wake_particle) has no other caller
// anywhere in the engine, so this is the first real proof it behaves as
// the collapsing-floor design assumes, not just that both calls exist.
// ---------------------------------------------------------------------
void woken_floor_tile_falls() {
    Rig rig;
    // create_floor_grid() always spawns a tile flush with the world's
    // turtle boundary (z = thickness/2, i.e. its bottom sits exactly at
    // TURTLE_Z) -- there is no arbitrary-Z overload. A tile spawned that
    // way has zero clearance to fall: it's already resting on the one
    // truly immovable plane in the engine. The real level's loose tile
    // sits elevated (matching the walkway surface height, see scene.cpp's
    // spawn_loose_tile) with NOTHING solid underneath it -- the strata
    // skip_mask carves out bedrock at that column too, same as the gap.
    // is_at_rest=true is what holds it in place pre-wake, not literal
    // support, so mirror that here: lift the tile and leave the space
    // below it empty (no slab -- an earlier version of this test put a
    // static slab directly under the tile, which just meant the "woken"
    // tile immediately settled into a resting contact with it, same bug
    // as the turtle-flush case one level up).
    // High enough that even resting flush on the turtle afterward
    // (thickness/2 = 0.1) still clears the >0.3m fall assertion below
    // with margin.
    constexpr float kLiftedZ = 1.0f;
    rig.add_floor_slab(0.0f, 4.0f);   // unrelated ground slab, away from the tile's column

    FloorTileConfig cfg;
    cfg.tile_width = 1.0f;
    cfg.tile_height = 1.0f;
    cfg.tile_thickness = 0.2f;
    std::vector<int> tile_ids = rig.ps.create_floor_grid(3.0f, 0.0f, 1, 1, cfg);
    ASSERT_TRUE(tile_ids.size() == 1, "expected create_floor_grid(1,1) to return one tile");
    const int tile_id = tile_ids[0];
    {
        // Lift the tile above create_floor_grid's default turtle-flush
        // placement, giving it room to fall once woken.
        auto view = rig.ps.lock_particles_for_write();
        view[tile_id].z = kLiftedZ;
    }
    rig.ps.update_bvh();

    float z_before;
    {
        auto view = rig.ps.lock_particles_for_read();
        z_before = view[tile_id].z;
    }

    // Settle: while is_at_rest, the tile must not drift on its own.
    rig.tick(30);
    {
        auto view = rig.ps.lock_particles_for_read();
        ASSERT_TRUE(std::abs(view[tile_id].z - z_before) < 0.01f,
                    "expected the resting tile not to move before being woken");
    }

    rig.physics.wake_particle(static_cast<size_t>(tile_id));
    // The solver damps toward a fairly low terminal velocity (~0.37 m/s
    // observed, well under raw g*t), so this needs real wall-clock time,
    // not just a handful of frames, to accumulate a visible fall.
    rig.tick(120);

    float z_after;
    {
        auto view = rig.ps.lock_particles_for_read();
        z_after = view[tile_id].z;
    }
    ASSERT_TRUE(z_after < z_before - 0.3f,
                "expected the woken tile to fall (>0.3m) under gravity, dz=" +
                std::to_string(z_after - z_before));
}

} // namespace

int main() {
    std::cout << "=== threshold headless verification ===\n";

    TEST(jump_rises_and_lands);
    TEST(jump_refuses_while_falling);
    TEST(jump_clears_a_gap);
    TEST(woken_floor_tile_falls);
    TEST(spike_volume_trigger_fires_on_entry);
    TEST(sword_swing_clip_plays_and_completes);

    std::cout << "\n" << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
