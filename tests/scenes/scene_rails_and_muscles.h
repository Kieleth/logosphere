// =============================================================================
// SCENE: rails and muscles (INV-40) - G-81 the harness, G-82 a muscle under
// weight, G-83 a replanted foot is a new rail
// =============================================================================
// THE PHYSICS. Motion enters a world in exactly two ways: a force, or a
// trajectory someone prescribes. A muscle is a force; a rail is a trajectory.
// The animation layer may hold a body only as a RAIL (KINEMATIC, one writer,
// velocity = the trajectory's derivative, a jump declared as a new rail) or
// act on it only as a MUSCLE (a drive target on a DYNAMIC body). Nobody
// outside the solver writes a physics body's state.
//
// THE QUESTION. Eva's shipped rig (Phase 5 legs, Phase E upper body) is
// twenty muscles hanging from two rails: the pelvis harness and the stance
// clamp. After the solver, does any hand still move a muscle-driven bone?
// Does a DYNAMIC body wearing the muscles' flags fall when nothing holds
// it? When the stance anchor is moved a stride in one frame, does its
// writer say so, or does the ledger read a foot sliding at 42 m/s?
//
// STATION A (G-81, G-83): Eva walks north at 1 m/s for RUN_FRAMES over
// strata, the test_physics_drive_walk_legs stage. The ParticleTracer
// watches every drive child; the solver has no tracer sites, so EVERY
// record on one of them is an outside hand. The two plant anchors are
// watched for jumps of a stride in a frame.
// STATION B (G-82): 4 m east, never met: box A, DYNAMIC with the drive
// children's own flags and nothing under it; box B, KINEMATIC; and the
// arm, a DYNAMIC drive child nailed to a KINEMATIC post and commanded
// horizontal, weighing 0.5 m of wood.
//
// This scene includes the engine: the humanoid rig is registered only
// through it (HumanoidLocomotion + worldgen strata), as every
// test_physics_drive_* does. Both drivers step through Scene::step and
// neither holds a body, a force or a threshold.
// =============================================================================
#pragma once

#include "core/engine.h"
#include "core/argus.h"
#include "core/particle_tracer.h"
#include "logosphere/animation/humanoid_locomotion.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include "logosphere/worldgen/strata_floor_generator.h"
#include "logosphere/worldgen/worldgen_system.h"
#include "materials.h"
#include "math/quat.h"
#include "particle.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace scene_rails_and_muscles {

constexpr float DT            = 1.0f / 60.0f;
constexpr int   SETTLE_FRAMES = 30;
constexpr int   RUN_FRAMES    = 300;
constexpr float WALK_SPEED    = 1.0f;          // m/s, north
constexpr float FLOOR_TOP     = 0.55f;         // bedrock 0.30 + sediment 0.15 + organic 0.10
// station B, east of Eva; her walk is north, so they never meet
constexpr float B_X           = 4.0f;
constexpr float BOX           = 0.3f;
constexpr float DROP_Z        = 2.0f;
constexpr float POST          = 0.4f;
constexpr float POST_Z        = 1.6f;
constexpr float ARM_L         = 0.5f, ARM_W = 0.15f;
constexpr float ANG_STIFF     = 2000.0f, ANG_DAMP = 60.0f;   // the shipped drive profile (apply_physics_drive_*_init)
// Bars. SLOP is the engine's own 'geometric error, not motion' (INV-2).
constexpr int   FALL_FRAMES   = 60;
constexpr float FALL_MIN      = 0.25f * DROP_Z;              // a hovering body drops 0; a falling one far more
constexpr float STAY_MAX      = PhysicsV4::SLOP;
constexpr float POSE_ERR_MAX  = 0.10f;                       // rad, INV-13 budget on the cantilever
constexpr float SEP_DRIFT_MAX = 5.0f * PhysicsV4::SLOP;
constexpr float JUMP_MIN      = 0.10f;                       // m in one frame: a replant, never a slide
constexpr float JUMP_LEDGER_MAX = 0.5f;                      // m/s: a fresh plant's own velocity is ~0
constexpr float WALK_MIN_FRACTION = 0.5f;                    // the drive walk gauge's own bar
constexpr const char* JUMP_SITE = "rail.jump";               // the declaration the writer owes (G-83)
// The body is where its nails put it: a nail's two attachment points
// coincide up to geometric error, and no bone is farther from the hips
// than its standing reach (a leg bends, it does not stretch).
constexpr float JOINT_GAP_MAX = 10.0f * PhysicsV4::SLOP;    // m, per nail, whole run
// G-90: a planted foot is the body's pivot. Over its stance it slides by no
// more than geometric error, and it stands ON its support (a rail declared
// where the foot touched, not above it).
constexpr float STANCE_SLIDE_MAX = 10.0f * PhysicsV4::SLOP;   // m, per stance
constexpr float STANCE_GAP_MAX   = 2.0f * PhysicsV4::SLOP;    // m, foot bottom above its support, at any stance frame
constexpr float REACH_SLACK   = 0.15f;                       // m beyond the standing reach

struct Scene {
    logosphere::Argus argus;
    PhysicsHumanoidResult eva{};
    int hips = -1, box_a = -1, box_b = -1, post = -1, arm = -1;
    std::vector<int> muscles;                       // the drive children
    std::unordered_set<int> muscle_set;
    // station A, latched by the witness
    std::map<std::string, int> hands;               // site -> records on muscles, whole run
    int   hand_records = 0, frames_with_hands = 0;
    int   muscles_stale = 0;                        // engine drive-child ids that no joint owns (a stale bag after a swap)
    std::map<std::string, int> rail_hands;          // sites on the hips rail (narrated, waived)
    int   replants = 0, replants_declared = 0, replants_ledger_loud = 0;
    float replant_ledger_speed_max = 0.0f;
    float origin_x = 0, origin_y = 0;                // where the run starts; SPACE returns her here
    float hx0 = 0, hy0 = 0, fx = 0, fy = 1, forward = 0, prev_forward = 0;
    int   backward_frames = 0, walk_frames = 0;
    // station B, latched
    float a_z0 = 0, b_z0 = 0, a_drop_max = 0, b_drift_max = 0;
    float arm_err_max = 0, arm_sep0 = -1.0f, arm_sep_drift_max = 0;
    // the body: every rig particle, named; its nails; its standing reach
    std::vector<int> rig;
    std::unordered_set<int> rig_set;
    std::map<int, std::string> names;
    std::map<int, float> rest_reach;
    float joint_gap_max = 0.0f;  std::string joint_gap_worst;  int joint_gap_frame = -1;
    struct NailRecord { float gap_max = 0.0f; int onset = -1; };  // onset: first frame over the bar
    std::map<std::string, NailRecord> nails;
    float reach_over_max = -1e9f; std::string reach_worst;
    // internals
    struct AnchorEye { float x = 0, y = 0, z = 0; int check_frames = 0; bool loud = false; };
    std::map<int, AnchorEye> eyes;

    static Particle box(float x, float y, float z, float w, float h, float t,
                        Materials::Type m, float r, float g, float b) {
        Particle p{};
        p.x = x; p.y = y; p.z = z;
        p.shape = ParticleShape::BOX;
        p.width = w; p.height = h; p.thickness = t; p.size = w;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(m);
        return p;
    }

    // A record is a hand only if it names a state field. Note-only records
    // ("skipped", a transfer's "anchor_z" marker) are the tracer's causal
    // log, not writes.
    static bool is_state_field(const char* f) {
        static const char* const fields[] = {"x", "y", "z", "vx", "vy", "vz",
            "rotation_x", "rotation_y", "rotation_z", "rotation_q",
            "omega", "omega_x", "omega_y", "omega_z"};
        for (const char* k : fields) if (std::strcmp(f, k) == 0) return true;
        return false;
    }
    static int inv40_step() {
        const char* e = std::getenv("INV40_STEP");
        return e ? std::atoi(e) : 0;
    }

    // A nail's attachment point in the world: the body's position plus its
    // offset rotated by the body's full orientation (INV-28). Mirrors the
    // solver's own rule at the strain-energy ledger (physics_system_v4.cpp,
    // the `attach` lambda): that helper is not exported, and INV-28 owes the
    // one shared definition; until it exists this is the fifth copy, and
    // says so.
    static void attach(const Particle& p, const Vec3& o, bool rot, float& wx, float& wy, float& wz) {
        if (!rot) { wx = p.x + o.x; wy = p.y + o.y; wz = p.z + o.z; return; }
        const float cx = std::cos(p.rotation_x), sx = std::sin(p.rotation_x);
        const float cy = std::cos(p.rotation_y), sy = std::sin(p.rotation_y);
        const float cz = std::cos(p.rotation_z), sz = std::sin(p.rotation_z);
        const float y1 = o.y * cx - o.z * sx;
        const float z1 = o.y * sx + o.z * cx;
        const float x2 = o.x * cy + z1 * sy;
        const float z2 = -o.x * sy + z1 * cy;
        // G-87: the compass turns clockwise; the solver's copies follow the
        // same lever, so the test measures what the solver enforces.
        static const bool cw = std::getenv("GLUON_OFFSETS_CW") != nullptr;
        if (cw) { wx = p.x + x2 * cz + y1 * sz; wy = p.y - x2 * sz + y1 * cz; }
        else    { wx = p.x + x2 * cz - y1 * sz; wy = p.y + x2 * sz + y1 * cz; }
        wz = p.z + z2;
    }
    std::string name_of(int id) const {
        auto it = names.find(id);
        return it != names.end() ? it->second : ("P" + std::to_string(id));
    }

    // ---- predicates: the asserts, the log and the panel read these ----
    static bool hands_off(int records)            { return records == 0; }
    static bool holds_together(float gap_max)     { return gap_max <= JOINT_GAP_MAX; }
    static bool whole(float reach_over)           { return reach_over <= 0.0f; }
    static bool muscles_live(int stale)           { return stale == 0; }
    static bool walks(float fwd, int frames)      { return frames > 0 && fwd >= WALK_MIN_FRACTION * frames * DT * WALK_SPEED; }
    static bool fell(float drop)                  { return drop > FALL_MIN; }
    static bool stayed(float drift)               { return drift <= STAY_MAX; }
    static bool holds(float err, float sep_drift) { return err <= POSE_ERR_MAX && sep_drift <= SEP_DRIFT_MAX; }
    static bool declared(int n, int d)            { return n > 0 && d == n; }
    static bool ledger_quiet(int n, int loud)     { return n > 0 && loud == 0; }
    static bool stance_holds(int stances, float slide_max) { return stances > 0 && slide_max <= STANCE_SLIDE_MAX; }
    static bool stance_stands(int stances, float gap_max)  { return stances > 0 && gap_max <= STANCE_GAP_MAX; }

    void build(Engine& engine) {
        auto& ps       = engine.get_particle_system();
        auto& physics  = engine.get_physics_system();
        auto& humanoid = engine.get_humanoid_locomotion();
        auto& tracer   = engine.get_particle_tracer();

        // The stage: the drive walk test's strata, verbatim.
        auto& strata = engine.get_worldgen_system().get_strata_floor_generator();
        strata.set_tile_size(4.0f);
        strata.set_tiles_per_chunk(5);
        strata.set_tiles_per_entity(1);
        strata.set_load_radius(60.0f);
        strata.set_unload_radius(70.0f);
        std::vector<StrataLayerSpec> layers;
        auto add_layer = [&](const char* n, Materials::Type m, float th,
                             float r, float g, float b, bool bond, float bs) {
            StrataLayerSpec s;
            s.name = n; s.material = m; s.thickness = th;
            s.r = r; s.g = g; s.b = b;
            s.bond_within_layer = bond; s.bond_strength = bs;
            layers.push_back(s);
        };
        add_layer("bedrock",  Materials::Type::STONE, 0.30f, 0.35f, 0.33f, 0.30f, true,  8000.0f);
        add_layer("sediment", Materials::Type::STONE, 0.15f, 0.45f, 0.40f, 0.30f, false, 0.0f);
        add_layer("organic",  Materials::Type::DIRT,  0.10f, 0.30f, 0.45f, 0.22f, false, 0.0f);
        strata.set_layers(std::move(layers));
        strata.set_enabled(true);
        strata.preload_chunks_around(0.0f, 0.0f, 3);

        // Eva, born with her feet on the organic layer (INV-37).
        auto& hgen = engine.get_worldgen_system().get_humanoid_generator();
        // Diagnostic staging: RAILS_DROP=<m> births Eva that much above the
        // floor (the idle stage's condition: 0.45 m), so the harness must
        // lower her to the ground.
        const float drop = std::getenv("RAILS_DROP") ? static_cast<float>(std::atof(std::getenv("RAILS_DROP"))) : 0.0f;
        eva = hgen.generate_humanoid_physics(0.0f, 0.0f, FLOOR_TOP + drop, -1, HumanoidSpec::eva(), false);
        auto& kg = engine.get_kg();
        eva.create_kg_entities(kg, "Human", 180.0f, 800.0f);
        humanoid.register_humanoid_direct(
            eva.hips_id, eva.left_leg_ids, eva.right_leg_ids,
            eva.left_arm_ids, eva.right_arm_ids, eva.torso_ids,
            180.0f, 800.0f, eva.entity_id);
        hips = eva.hips_id;

        // STATION B: three bodies in the air, 4 m east.
        box_a = ps.queue_particle_addition(box(B_X, 0.0f, DROP_Z, BOX, BOX, BOX,
                                               Materials::Type::STONE, 0.9f, 0.6f, 0.25f));
        box_b = ps.queue_particle_addition(box(B_X + 1.0f, 0.0f, DROP_Z, BOX, BOX, BOX,
                                               Materials::Type::STONE, 0.35f, 0.38f, 0.45f));
        post  = ps.queue_particle_addition(box(B_X + 2.5f, 0.0f, POST_Z, POST, POST, POST,
                                               Materials::Type::STONE, 0.35f, 0.38f, 0.45f));
        arm   = ps.queue_particle_addition(box(B_X + 2.5f + POST * 0.5f + ARM_L * 0.5f, 0.0f, POST_Z,
                                               ARM_L, ARM_W, ARM_W, Materials::Type::WOOD_HARD, 0.55f, 0.8f, 0.4f));
        ps.flush_pending_particles();
        {
            auto v = ps.lock_particles_for_write();
            for (int id : {box_b, post}) {          // rails: a writer that prescribes 'still'
                v[id].solver_mode = ParticleSolverMode::KINEMATIC;
                v[id].owner = ParticleOwner::DYNAMICS;
                v[id].is_at_rest = true;
            }
            for (int id : {box_a, arm}) {           // the drive children's exact flags
                v[id].solver_mode = ParticleSolverMode::DYNAMIC;
                v[id].is_quat_driven = true;
                v[id].owner = ParticleOwner::DYNAMICS;
                v[id].rotation_q = logosphere::Quat::identity();
                v[id].is_at_rest = false;
            }
            a_z0 = v[box_a].z; b_z0 = v[box_b].z;
        }
        {   // the cantilever: a nail with the shipped drive profile, target identity (horizontal)
            auto nail = std::make_unique<NailGluon>();
            nail->offset_a = Vec3(+POST * 0.5f, 0.0f, 0.0f);
            nail->offset_b = Vec3(-ARM_L * 0.5f, 0.0f, 0.0f);
            nail->target_distance = 0.0f;
            nail->rotate_offsets = true;
            nail->breaking_force = 1.0e5f;
            nail->enable_angular_constraint = true;
            nail->angular_drive_enabled = true;
            nail->use_quat_target = true;
            nail->target_relative_q = logosphere::Quat::identity();
            nail->angular_stiffness = ANG_STIFF;
            nail->angular_damping = ANG_DAMP;
            nail->max_relative_rotation = PhysicsV4::ANGULAR_LIMIT_UNLIMITED;
            physics.add_gluon_between((size_t)post, (size_t)arm, std::move(nail));
        }

        // Index swaps (strata streaming): keep every id and label honest.
        ps.add_swap_callback([this, &tracer](size_t o, size_t n) {
            auto fix = [&](int& id) { if (id == (int)o) id = (int)n; };
            fix(eva.hips_id); fix(eva.head_id); fix(hips); fix(box_a); fix(box_b); fix(post); fix(arm);
            for (int& id : eva.body_ids) fix(id);
            for (int& id : eva.left_leg_ids) fix(id);
            for (int& id : eva.right_leg_ids) fix(id);
            for (int& id : eva.left_arm_ids) fix(id);
            for (int& id : eva.right_arm_ids) fix(id);
            for (int& id : eva.torso_ids) fix(id);
            for (int& id : muscles) fix(id);
            for (int& id : rig) fix(id);
            if (muscle_set.erase((int)o)) muscle_set.insert((int)n);
            if (rig_set.erase((int)o)) rig_set.insert((int)n);
            auto nm = names.find((int)o);
            if (nm != names.end()) { auto v = nm->second; names.erase(nm); names[(int)n] = v; }
            auto rr = rest_reach.find((int)o);
            if (rr != rest_reach.end()) { auto v = rr->second; rest_reach.erase(rr); rest_reach[(int)n] = v; }
            if (tracer.is_traced((int)o)) {
                auto label = tracer.label_of((int)o);
                tracer.untrace((int)o); tracer.trace((int)n, std::move(label));
            }
            auto it = eyes.find((int)o);
            if (it != eyes.end()) { auto eye = it->second; eyes.erase(it); eyes[(int)n] = eye; }
        });

        // G-89: with RAILS_ARMS the arm bodies are traced from birth, so the
        // settle's own sleep transitions (sleep.rest / sleep.wake) are on record.
        if (std::getenv("RAILS_ARMS")) {
            for (int id : eva.left_arm_ids)  tracer.trace(id, "arm/L" + std::to_string(id));
            for (int id : eva.right_arm_ids) tracer.trace(id, "arm/R" + std::to_string(id));
        }
        for (int i = 0; i < SETTLE_FRAMES; ++i) engine.update(DT);

        // The cast: every drive child is a muscle; the hips are a rail.
        const logosphere::animation::HumanoidParts* parts = humanoid.get_humanoid_parts(hips);
        if (parts) {
            for (unsigned int pid : parts->physics_drive_children) {
                muscles.push_back((int)pid); muscle_set.insert((int)pid);
                tracer.trace((int)pid, "muscle/" + std::to_string(pid));
            }
        }
        if (parts) {
            for (unsigned int pid : parts->all_particle_indices) { rig.push_back((int)pid); rig_set.insert((int)pid); }
            for (const auto& j : parts->joint_hierarchy.joints) names[(int)j.child_particle] = j.name;
            for (unsigned int c : parts->head_child_particles) names[(int)c] = "rider/" + std::to_string(c);
            names[hips] = "hips";
            auto v = ps.lock_particles_for_read();
            for (int id : rig) {
                const float dx = v[id].x - v[hips].x, dy = v[id].y - v[hips].y, dz = v[id].z - v[hips].z;
                rest_reach[id] = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
        }
        tracer.trace(hips, "rail/hips");
        tracer_ = &tracer;
        feet_enable();                                     // G-90: the feet are measured in every run; RAILS_FEET=1 prints the rows
        argus.watch(hips, "hips");
        argus.watch(eva.head_id, "head");
        argus.watch(box_a, "box_a"); argus.watch(box_b, "box_b");
        argus.watch(post, "post");   argus.watch(arm, "arm");
        {
            auto v = ps.lock_particles_for_read();
            hx0 = v[hips].x; hy0 = v[hips].y;
            origin_x = hx0; origin_y = hy0;
            fx = std::sin(v[hips].rotation_z); fy = std::cos(v[hips].rotation_z);
        }
        // Diagnostic stagings (env, never the shipped claim): RAILS_IDLE=1
        // leaves Eva standing (the neck stage's condition); RAILS_NECK=1 adds
        // the neck stage's own scalar head drive (pi/8, 200 / 12).
        if (!std::getenv("RAILS_IDLE")) {
            humanoid.set_volitional(hips, true);
            humanoid.set_body_relative_velocity(hips, WALK_SPEED, 0.0f);
        }
        if (std::getenv("RAILS_NECK")) {
            humanoid.set_joint_physics_drive(eva.entity_id, "head", static_cast<float>(M_PI) / 8.0f, 200.0f, 12.0f);
        }
        // RAILS_LOOK=1: the face stage's condition - a look-at target due east,
        // the cascade turns head, torso and hips in that order.
        if (std::getenv("RAILS_LOOK")) humanoid.set_look_at_target(hips, 10.0f, 0.0f);
    }

    void step(Engine& engine, int frame) {
        auto& ps       = engine.get_particle_system();
        auto& humanoid = engine.get_humanoid_locomotion();
        auto& tracer   = engine.get_particle_tracer();
        auto& strata   = engine.get_worldgen_system().get_strata_floor_generator();

        tracer.clear_records();
        engine.update(DT);
        ++walk_frames;
        argus.observe(ps, frame);

        float hx, hy;
        { auto v = ps.lock_particles_for_read(); hx = v[hips].x; hy = v[hips].y; }
        strata.update(hx, hy);

        // G-81: whose hands are on the muscles this frame?
        const auto recs = tracer.records();
        int frame_hands = 0;
        for (const auto& r : recs) {
            if (!is_state_field(r.field)) continue;
            if (muscle_set.count(r.particle_id)) { hands[r.site]++; ++hand_records; ++frame_hands; }
            else if (r.particle_id == hips)      { rail_hands[r.site]++; }
        }
        if (frame_hands) ++frames_with_hands;

        // hygiene: the engine's drive children must be the bodies its joints
        // name, or the riders it made parts of the head (G-84), or the
        // hands-off count above is a count over ghosts.
        if (const logosphere::animation::HumanoidParts* parts = humanoid.get_humanoid_parts(hips)) {
            int stale = 0;
            for (unsigned int pid : parts->physics_drive_children) {
                bool live = false;
                for (const auto& j : parts->joint_hierarchy.joints) if (j.child_particle == pid) { live = true; break; }
                for (unsigned int c : parts->head_child_particles) if (c == pid) { live = true; break; }
                if (!live) ++stale;
            }
            muscles_stale = stale;
        }

        // G-83: the anchors. Born lazily on the first plant; watched from then on.
        if (const logosphere::animation::HumanoidParts* parts = humanoid.get_humanoid_parts(hips)) {
            for (int id : {parts->left_plant_anchor_id, parts->right_plant_anchor_id}) {
                if (id < 0 || eyes.count(id)) continue;
                auto v = ps.lock_particles_for_read();
                eyes[id] = AnchorEye{v[id].x, v[id].y, v[id].z, 0, false};
                tracer.trace(id, id == parts->left_plant_anchor_id ? "rail/anchor_l" : "rail/anchor_r");
                argus.watch(id, id == parts->left_plant_anchor_id ? "anchor_l" : "anchor_r");
            }
        }
        for (auto& [id, eye] : eyes) {
            float x, y, z, speed;
            {
                auto v = ps.lock_particles_for_read();
                const Particle& p = v[id];
                x = p.x; y = p.y; z = p.z;
                speed = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
            }
            if (eye.check_frames > 0) {             // the ledger after a jump (the derivation runs on the next physics step)
                if (speed > replant_ledger_speed_max) replant_ledger_speed_max = speed;
                if (speed > JUMP_LEDGER_MAX && !eye.loud) { eye.loud = true; ++replants_ledger_loud; }
                --eye.check_frames;
            }
            const float d = std::sqrt((x - eye.x) * (x - eye.x) + (y - eye.y) * (y - eye.y) + (z - eye.z) * (z - eye.z));
            if (d > JUMP_MIN) {
                ++replants;
                bool said = false;
                for (const auto& r : recs) if (r.particle_id == id && std::strcmp(r.site, JUMP_SITE) == 0) { said = true; break; }
                if (said) ++replants_declared;
                eye.check_frames = 2; eye.loud = false;
                if (speed > replant_ledger_speed_max) replant_ledger_speed_max = speed;
                if (speed > JUMP_LEDGER_MAX) { eye.loud = true; ++replants_ledger_loud; }
            }
            eye.x = x; eye.y = y; eye.z = z;
        }

        // THE BODY: every nail of the rig holds its two attachment points
        // together, and no bone is beyond its standing reach from the hips.
        {
            auto& physics = engine.get_physics_system();
            auto v = ps.lock_particles_for_read();
            std::unordered_set<const GluonConstraintBase*> seen;
            for (int id : rig) {
                for (const GluonConstraintBase* g : physics.get_gluons_for_particle((size_t)id)) {
                    if (!g || !seen.insert(g).second) continue;
                    const int a = (int)g->particle_a, b = (int)g->particle_b;
                    if (!rig_set.count(a) || !rig_set.count(b)) continue;   // a pin to an anchor is not the body
                    float ax, ay, az, bx, by, bz;
                    attach(v[a], g->offset_a, g->rotate_offsets, ax, ay, az);
                    attach(v[b], g->offset_b, g->rotate_offsets, bx, by, bz);
                    const float dx = bx - ax, dy = by - ay, dz = bz - az;
                    const float gap = std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - g->target_distance);
                    const std::string nm = name_of(a) + "<>" + name_of(b);
                    NailRecord& rec = nails[nm];
                    if (gap > rec.gap_max) rec.gap_max = gap;
                    if (gap > JOINT_GAP_MAX && rec.onset < 0) rec.onset = frame;
                    if (gap > joint_gap_max) { joint_gap_max = gap; joint_gap_worst = nm; joint_gap_frame = frame; }
                }
                const float dx = v[id].x - v[hips].x, dy = v[id].y - v[hips].y, dz = v[id].z - v[hips].z;
                const float over = std::sqrt(dx * dx + dy * dy + dz * dz) - rest_reach[id] - REACH_SLACK;
                if (over > reach_over_max) { reach_over_max = over; reach_worst = name_of(id); }
            }
        }

        // G-90: the feet, every frame (the rows are printed by the drivers when RAILS_FEET is set)
        feet_observe(engine, frame);
        if (!feet_rows) { feet_last_row.clear(); feet_height_row.clear(); feet_frame_rows.clear(); }

        // the gauge: does she still walk?
        forward = (hx - hx0) * fx + (hy - hy0) * fy;
        if (forward - prev_forward < -0.005f) ++backward_frames;
        prev_forward = forward;

        // G-82: station B
        const auto* A = argus.latest(box_a); const auto* B = argus.latest(box_b);
        const auto* P = argus.latest(post);  const auto* R = argus.latest(arm);
        if (A && B && P && R) {
            const float drop = a_z0 - A->z;
            if (drop > a_drop_max) a_drop_max = drop;
            const float drift = std::fabs(B->z - b_z0);
            if (drift > b_drift_max) b_drift_max = drift;
            // the post never turns, so the arm's whole rotation is its pose error
            const float w = std::fabs(R->q.w) > 1.0f ? 1.0f : std::fabs(R->q.w);
            const float err = 2.0f * std::acos(w);
            if (err > arm_err_max) arm_err_max = err;
            const float sep = argus.separation(post, arm);
            if (arm_sep0 < 0.0f) arm_sep0 = sep;
            const float sd = std::fabs(sep - arm_sep0);
            if (sd > arm_sep_drift_max) arm_sep_drift_max = sd;
        }
    }

    // SPACE: the run replays. Eva returns to where she started through the
    // teleport door (every rig body and both anchors shifted rigidly, then
    // reset_humanoid_position declares the jump: the rails void their
    // history, every body is forgotten by the solver), her walk is commanded
    // again, the boxes re-drop, every count restarts.
    void rearm(Engine& engine) {
        auto& ps = engine.get_particle_system();
        auto& physics = engine.get_physics_system();
        auto& humanoid = engine.get_humanoid_locomotion();
        {
            auto v = ps.lock_particles_for_write();
            const float dx = origin_x - v[hips].x, dy = origin_y - v[hips].y;
            for (int id : rig) { v[id].x += dx; v[id].y += dy; }
            for (auto& [id, eye] : eyes) {
                v[id].x += dx; v[id].y += dy;
                eye.x = v[id].x; eye.y = v[id].y; eye.z = v[id].z; eye.check_frames = 0; eye.loud = false;
            }
            auto reset = [&](int id, float x, float y, float z) {
                Particle& p = v[id]; p.x = x; p.y = y; p.z = z;
                p.vx = p.vy = p.vz = 0.0f; p.omega_x = p.omega_y = p.omega_z = 0.0f;
                p.rotation_x = p.rotation_y = p.rotation_z = 0.0f;
                p.rotation_q = logosphere::Quat::identity();
                p.is_at_rest = false; p.frames_at_rest = 0; p.low_velocity_frames = 0;
            };
            reset(box_a, B_X, 0.0f, DROP_Z);
            reset(arm, B_X + 2.5f + POST * 0.5f + ARM_L * 0.5f, 0.0f, POST_Z);
        }
        humanoid.reset_humanoid_position(hips);        // the door (INV-40 / G-83)
        if (!std::getenv("RAILS_IDLE")) humanoid.set_body_relative_velocity(hips, WALK_SPEED, 0.0f);
        physics.forget_body((size_t)box_a); physics.forget_body((size_t)arm);
        hands.clear(); rail_hands.clear(); hand_records = 0; frames_with_hands = 0;
        replants = 0; replants_declared = 0; replants_ledger_loud = 0; replant_ledger_speed_max = 0.0f;
        a_drop_max = 0; b_drift_max = 0; arm_err_max = 0; arm_sep0 = -1.0f; arm_sep_drift_max = 0;
        {
            auto v = ps.lock_particles_for_read();
            hx0 = v[hips].x; hy0 = v[hips].y;
        }
        forward = prev_forward = 0.0f; backward_frames = 0; walk_frames = 0;
        ph_l = FootPhase{}; ph_r = FootPhase{}; feet_stances = feet_swings = feet_swing_contact_frames = feet_swing_frames = 0;
        feet_stance_slide_max = feet_stance_slide_sum = feet_stance_early_sum = feet_stance_peak_max = 0.0f; feet_swing_lift_max = feet_stance_gap_max = -1e9f; feet_worst.clear();
        joint_gap_max = 0.0f; joint_gap_worst.clear(); joint_gap_frame = -1; reach_over_max = -1e9f; reach_worst.clear(); nails.clear();
        argus.reset_milestones(box_a); argus.reset_milestones(arm);
    }

    // ---- THE ARM SWING (G-89), through Argus; both drivers print the rows ----
    // Per side: the wrist's forward excursion in the hips frame, the shoulder
    // drive's commanded swing against the actual bridge->upper-arm angle, the
    // absolute turning of the upper arm and of the bridge within the window,
    // the upper arm's spin, the wrist's speed, the elbow->wrist drive's error,
    // and the frames each of shoulder / elbow / wrist spent asleep (INV-18).
    // A window is one second (60 frames); "when does the swing start" is read
    // off the rows. RAILS_ARMS=1 enables it; RAILS_WAKE=<s> wakes the six arm
    // bodies at that second (the discriminator).
    struct ArmEyes { int bridge = -1, shoulder = -1, elbow = -1, wrist = -1; };
    struct ArmWin {
        float fwd_min = 1e9f, fwd_max = -1e9f, cmd_min = 1e9f, cmd_max = -1e9f, act_min = 1e9f, act_max = -1e9f;
        float spin_sum = 0, gap_max = 0, sh_turn = 0, br_turn = 0, wrist_speed_max = 0, wrist_cmd_err_max = 0;
        int n = 0, asleep_frames[3] = {0, 0, 0};
        logosphere::Quat q0_sh, q0_br; bool q0_set = false;
        void reset() { *this = ArmWin{}; }
    };
    bool arms_on = false;
    ParticleTracer* tracer_ = nullptr;                 // set in build(); the arm probe reads sleep.wake / sleep.rest records
    std::string arms_sleep_notes;                      // this window's sleep transitions on the arm bodies, for the row
    ArmEyes arm_l, arm_r;
    ArmWin win_l, win_r;
    std::string arms_last_row;                      // the latest completed second, for the panel
    int arms_first_swing_s = -1, arms_first_cmd_s = -1;

    void arms_enable() {
        arms_on = true;
        std::map<std::string, int> by_name;
        for (const auto& [id, nm] : names) by_name[nm] = id;
        auto pick = [&](ArmEyes& a, const char* side) {
            auto get = [&](const std::string& k) { auto it = by_name.find(k); return it == by_name.end() ? -1 : it->second; };
            a.bridge = get(std::string(side) + "_shoulder_bridge"); a.shoulder = get(std::string(side) + "_shoulder");
            a.elbow = get(std::string(side) + "_elbow"); a.wrist = get(std::string(side) + "_wrist");
            for (int id : {a.bridge, a.shoulder, a.elbow, a.wrist}) if (id >= 0) argus.watch(id, std::string(side) + "/arm" + std::to_string(id));
        };
        pick(arm_l, "left"); pick(arm_r, "right");
        for (int id : {arm_l.shoulder, arm_l.elbow, arm_l.wrist, arm_r.shoulder, arm_r.elbow, arm_r.wrist})
            if (id >= 0 && !tracer_->is_traced(id)) tracer_->trace(id, "arm/" + name_of(id));
        std::printf("  [arms] cast: L bridge P%d shoulder P%d elbow P%d wrist P%d | R bridge P%d shoulder P%d elbow P%d wrist P%d\n",
                    arm_l.bridge, arm_l.shoulder, arm_l.elbow, arm_l.wrist, arm_r.bridge, arm_r.shoulder, arm_r.elbow, arm_r.wrist);
        if (tracer_) {                                     // the settle's sleep transitions on the arm bodies, before the first step clears the ring
            std::string settle;
            for (const auto& r : tracer_->records()) {
                if (std::strcmp(r.field, "asleep") != 0) continue;
                bool ours = false;
                for (int id : {arm_l.shoulder, arm_l.elbow, arm_l.wrist, arm_r.shoulder, arm_r.elbow, arm_r.wrist}) if (id == r.particle_id) ours = true;
                if (ours && settle.size() < 600) settle += " f" + std::to_string(r.frame) + " " + name_of(r.particle_id) + " " + r.site + (r.note ? std::string(" [") + r.note + "]" : "");
            }
            std::printf("  [arms] the settle's sleep transitions:%s\n", settle.empty() ? " none" : settle.c_str());
        }
    }
    static float qangle(const logosphere::Quat& q) { float c = std::fabs(q.w); if (c > 1.0f) c = 1.0f; return 2.0f * std::acos(c); }
    void arm_observe(Engine& engine, ArmEyes& a, ArmWin& w) {
        if (a.wrist < 0 || a.shoulder < 0 || a.bridge < 0) return;
        const auto* H = argus.latest(hips); const auto* W = argus.latest(a.wrist);
        const auto* S = argus.latest(a.shoulder); const auto* B = argus.latest(a.bridge);
        if (!H || !W || !S || !B) return;
        const float fwd = (W->x - H->x) * fx + (W->y - H->y) * fy;                  // the wrist, forward of the hips
        w.fwd_min = std::min(w.fwd_min, fwd); w.fwd_max = std::max(w.fwd_max, fwd);
        const float act = qangle((B->q.conjugate() * S->q).normalized());          // actual bridge->upper arm relative angle
        w.act_min = std::min(w.act_min, act); w.act_max = std::max(w.act_max, act);
        auto& physics = engine.get_physics_system();
        auto v = engine.get_particle_system().lock_particles_for_read();
        float cmd = -1.0f, gap = 0.0f;
        for (const GluonConstraintBase* g : physics.get_gluons_for_particle((size_t)a.shoulder)) {
            if (!g) continue;
            const int other = (int)(g->particle_a == (size_t)a.shoulder ? g->particle_b : g->particle_a);
            if (other != a.bridge) continue;
            if (const auto* nail = dynamic_cast<const NailGluon*>(g)) if (nail->use_quat_target) cmd = qangle(nail->target_relative_q);
            float ax, ay, az, bx, by, bz;
            attach(v[g->particle_a], g->offset_a, g->rotate_offsets, ax, ay, az);
            attach(v[g->particle_b], g->offset_b, g->rotate_offsets, bx, by, bz);
            gap = std::fabs(std::sqrt((bx-ax)*(bx-ax) + (by-ay)*(by-ay) + (bz-az)*(bz-az)) - g->target_distance);
        }
        if (cmd >= 0.0f) { w.cmd_min = std::min(w.cmd_min, cmd); w.cmd_max = std::max(w.cmd_max, cmd); }
        w.gap_max = std::max(w.gap_max, gap);
        int k = 0;
        for (int id : {a.shoulder, a.elbow, a.wrist}) { if (id >= 0 && v[id].is_at_rest) ++w.asleep_frames[k]; ++k; }
        w.spin_sum += argus.spin(a.shoulder); ++w.n;
        if (!w.q0_set) { w.q0_sh = S->q; w.q0_br = B->q; w.q0_set = true; }
        w.sh_turn = std::max(w.sh_turn, qangle((w.q0_sh.conjugate() * S->q).normalized()));
        w.br_turn = std::max(w.br_turn, qangle((w.q0_br.conjugate() * B->q).normalized()));
        w.wrist_speed_max = std::max(w.wrist_speed_max, std::sqrt(W->vx * W->vx + W->vy * W->vy + W->vz * W->vz));
        if (a.elbow >= 0) if (const auto* E = argus.latest(a.elbow)) {
            for (const GluonConstraintBase* g : physics.get_gluons_for_particle((size_t)a.wrist)) {
                if (!g) continue;
                const int other = (int)(g->particle_a == (size_t)a.wrist ? g->particle_b : g->particle_a);
                if (other != a.elbow) continue;
                if (const auto* nail = dynamic_cast<const NailGluon*>(g)) if (nail->use_quat_target) {
                    const bool fwdp = g->particle_a == (size_t)a.elbow;
                    const logosphere::Quat rel = fwdp ? (E->q.conjugate() * W->q).normalized() : (W->q.conjugate() * E->q).normalized();
                    w.wrist_cmd_err_max = std::max(w.wrist_cmd_err_max, qangle((nail->target_relative_q.conjugate() * rel).normalized()));
                }
            }
        }
    }
    // Call after step() every frame when arms_on. Returns true when a second
    // completed and arms_last_row holds its row.
    bool arms_observe(Engine& engine, int f) {
        if (!arms_on) return false;
        static const int wake_s = std::getenv("RAILS_WAKE") ? std::atoi(std::getenv("RAILS_WAKE")) : -1;
        if (wake_s >= 0 && f == wake_s * 60) {
            auto& physics = engine.get_physics_system();
            for (int id : {arm_l.shoulder, arm_l.elbow, arm_l.wrist, arm_r.shoulder, arm_r.elbow, arm_r.wrist}) if (id >= 0) physics.wake_particle((size_t)id);
            std::printf("  [arms] WOKE the six arm bodies at second %d (G-89)\n", wake_s);
        }
        arm_observe(engine, arm_l, win_l); arm_observe(engine, arm_r, win_r);
        if (tracer_) for (const auto& r : tracer_->records()) {
            if (std::strcmp(r.field, "asleep") != 0) continue;
            bool ours = false;
            for (int id : {arm_l.shoulder, arm_l.elbow, arm_l.wrist, arm_r.shoulder, arm_r.elbow, arm_r.wrist}) if (id == r.particle_id) ours = true;
            if (!ours) continue;
            if (arms_sleep_notes.size() < 400) arms_sleep_notes += " f" + std::to_string(f) + " " + name_of(r.particle_id) + " " + r.site + (r.note ? std::string(" [") + r.note + "]" : "");
        }
        if (f % 60 != 59) return false;
        const int sec = f / 60;
        auto amp = [](float lo, float hi) { return hi < lo ? 0.0f : hi - lo; };
        const float lamp = amp(win_l.fwd_min, win_l.fwd_max), ramp = amp(win_r.fwd_min, win_r.fwd_max);
        if (arms_first_swing_s < 0 && std::max(lamp, ramp) > 0.10f) arms_first_swing_s = sec;
        const float lcmd = amp(win_l.cmd_min, win_l.cmd_max), rcmd = amp(win_r.cmd_min, win_r.cmd_max);
        if (arms_first_cmd_s < 0 && std::max(lcmd, rcmd) > 0.10f) arms_first_cmd_s = sec;
        char row[640];
        std::snprintf(row, sizeof(row),
            "[arms s%2d] L wrist fwd %+.3f..%+.3f amp %.3f | cmd amp %.3f actual amp %.3f spin %.2f rad/s gap %.3f asleep s/e/w %d/%d/%d turn sh %.2f br %.2f wrist v %.2f err %.2f"
            " || R wrist fwd %+.3f..%+.3f amp %.3f | cmd amp %.3f actual amp %.3f spin %.2f rad/s gap %.3f asleep s/e/w %d/%d/%d turn sh %.2f br %.2f wrist v %.2f err %.2f",
            sec, win_l.fwd_min, win_l.fwd_max, lamp, lcmd, amp(win_l.act_min, win_l.act_max), win_l.n ? win_l.spin_sum / win_l.n : 0.0f, win_l.gap_max,
            win_l.asleep_frames[0], win_l.asleep_frames[1], win_l.asleep_frames[2], win_l.sh_turn, win_l.br_turn, win_l.wrist_speed_max, win_l.wrist_cmd_err_max,
            win_r.fwd_min, win_r.fwd_max, ramp, rcmd, amp(win_r.act_min, win_r.act_max), win_r.n ? win_r.spin_sum / win_r.n : 0.0f, win_r.gap_max,
            win_r.asleep_frames[0], win_r.asleep_frames[1], win_r.asleep_frames[2], win_r.sh_turn, win_r.br_turn, win_r.wrist_speed_max, win_r.wrist_cmd_err_max);
        arms_last_row = row;
        if (!arms_sleep_notes.empty()) { arms_last_row += " || sleep:" + arms_sleep_notes; arms_sleep_notes.clear(); }
        win_l.reset(); win_r.reset();
        return true;
    }

    // ---- THE SLIDING FOOT (G-90), through Argus, the contact events and the plant state ----
    // A stride has two halves per foot. STANCE: the frames the writer holds
    // this foot as the planted one (has_planted_foot && planted_foot_is_right
    // == this side). SWING: the rest. Per stance: the slide (horizontal
    // displacement summed frame to frame), its first BLEND_FRAMES share (the
    // pin's blend-in), the peak slide speed, the foot-anchor separation at the
    // start and end, the pin's blend at the start and end, and the contact
    // frames. Per swing: frames, frames in contact with the floor (a foot that
    // never lifts drags), the lift (highest bottom over the support), and the
    // slide while touching. Touching = a collision event between the foot or
    // its toe and a body outside the rig with a normal within 60 degrees of
    // up. RAILS_FEET=1 enables it; both drivers print the rows.
    static constexpr int BLEND_FRAMES = 8;                  // ~ the 0.12 s blend-in at 8 per second
    struct FootPhase {
        bool active = false, stance = false; int start = -1, frames = 0, contact_frames = 0;
        float px = 0, py = 0, slide = 0, slide_early = 0, slide_touching = 0, peak_speed = 0, lift_max = -1e9f, gap_max = -1e9f;
        float sep_start = -1, sep_end = -1, blend_start = -1, blend_end = -1;
    };
    bool feet_on = false;
    int foot_l = -1, foot_r = -1, toe_l = -1, toe_r = -1, thigh_l = -1, thigh_r = -1;
    FootPhase ph_l, ph_r;
    std::string feet_last_row;
    int feet_stances = 0, feet_swings = 0, feet_swing_contact_frames = 0, feet_swing_frames = 0;
    float feet_stance_slide_max = 0, feet_stance_slide_sum = 0, feet_stance_early_sum = 0, feet_stance_peak_max = 0, feet_swing_lift_max = -1e9f, feet_stance_gap_max = -1e9f;
    bool feet_rows = false;                              // RAILS_FEET=1: print every phase's row; the measurement itself is always on
    int feet_rows_level = 0;                             // RAILS_FEET=2: a row per foot per frame (the landing offset, the height, the anchor, the tilt, the contacts)
    std::string feet_frame_rows;
    std::string feet_worst;
    // the height row: per 30 frames, each foot's lowest and highest bottom, the support top under it, its contacts
    struct FootHeight { float bottom_min = 1e9f, bottom_max = -1e9f, support_top = -1e9f; int contacts = 0, near = 0; void reset() { *this = FootHeight{}; } };
    FootHeight fh_l, fh_r;
    std::string feet_height_row;

    void feet_enable() {
        feet_on = true;
        if (const char* e = std::getenv("RAILS_FEET")) feet_rows_level = std::max(1, std::atoi(e));
        feet_rows = feet_rows_level >= 1;
        std::map<std::string, int> by_name;
        for (const auto& [id, nm] : names) by_name[nm] = id;
        auto get = [&](const char* k) { auto it = by_name.find(k); return it == by_name.end() ? -1 : it->second; };
        foot_l = get("left_ankle"); foot_r = get("right_ankle"); toe_l = get("left_toe"); toe_r = get("right_toe");
        thigh_l = get("left_hip"); thigh_r = get("right_hip");           // the hip joint's child is the thigh
        if (foot_l < 0 && !eva.left_leg_ids.empty())  foot_l = eva.left_leg_ids[0];
        if (foot_r < 0 && !eva.right_leg_ids.empty()) foot_r = eva.right_leg_ids[0];
        for (int id : {foot_l, foot_r}) if (id >= 0) argus.watch(id, id == foot_l ? "foot_l" : "foot_r");
        if (feet_rows) std::printf("  [feet] cast: L foot P%d toe P%d | R foot P%d toe P%d | touching = a contact between foot or toe and a body outside the rig, normal within 60 deg of up\n",
                    foot_l, toe_l, foot_r, toe_r);
    }
    // One foot, one frame. Returns true when a phase ended (feet_last_row holds its row).
    bool foot_observe(Engine& engine, int f, int foot, int toe, FootPhase& ph, const char* side) {
        if (foot < 0) return false;
        auto& physics  = engine.get_physics_system();
        auto& humanoid = engine.get_humanoid_locomotion();
        int contacts = 0, near = 0; float pen_max = 0.0f;
        for (const auto& e : physics.get_collision_events()) {
            const bool a_foot = (int)e.particle_a == foot || (int)e.particle_a == toe;
            const bool b_foot = (int)e.particle_b == foot || (int)e.particle_b == toe;
            if (a_foot == b_foot) continue;
            const int other = (int)(a_foot ? e.particle_b : e.particle_a);
            if (rig_set.count(other)) continue;
            if (std::fabs(e.normal_z) < 0.5f) continue;
            if (e.penetration > 0.0f) { ++contacts; pen_max = std::max(pen_max, e.penetration); } else ++near;   // touching is overlap; a proximity event is not a touch
        }
        const bool right = (foot == foot_r);
        float x, y, bottom, sep = -1.0f, blend = 0.0f, support_top = -1e9f; bool stance = false;
        {
            auto v = engine.get_particle_system().lock_particles_for_read();
            const Particle& p = v[foot];
            x = p.x; y = p.y; bottom = p.z - 0.5f * p.thickness;
            for (size_t i = 0; i < v.size(); ++i) {                   // the support: the highest top under the foot's footprint
                if (rig_set.count((int)i) || v[i].is_light_source) continue;
                const Particle& q = v[i];
                if (std::fabs(q.x - x) > 0.5f * q.width + 0.15f || std::fabs(q.y - y) > 0.5f * q.height + 0.15f) continue;
                const float top = q.z + 0.5f * q.thickness;
                if (top > bottom + 0.05f) continue;
                support_top = std::max(support_top, top);
            }
            float sep_z = 0.0f;
            if (const auto* parts = humanoid.get_humanoid_parts(hips)) {
                stance = parts->has_planted_foot && parts->planted_foot_is_right == right;
                const int anchor = right ? parts->right_plant_anchor_id : parts->left_plant_anchor_id;
                if (anchor >= 0 && (size_t)anchor < v.size()) { const float dx = x - v[anchor].x, dy = y - v[anchor].y; sep = std::sqrt(dx * dx + dy * dy); sep_z = p.z - v[anchor].z; }
                blend = stance ? parts->plant_blend : 0.0f;
            }
            float hip_cmd = -1.0f, hip_act = -1.0f, hip_err = -1.0f;    // the hip drive: commanded vs actual relative angle (deg)
            if (const int thigh = right ? thigh_r : thigh_l; thigh >= 0)
                for (const GluonConstraintBase* g : physics.get_gluons_for_particle((size_t)thigh)) {
                    if (!g) continue;
                    const int other = (int)(g->particle_a == (size_t)thigh ? g->particle_b : g->particle_a);
                    if (other != hips) continue;
                    const auto* nail = dynamic_cast<const NailGluon*>(g);
                    if (!nail || !nail->use_quat_target) continue;
                    const bool fwdp = g->particle_a == (size_t)hips;
                    const logosphere::Quat rel = fwdp ? (v[hips].rotation_q.conjugate() * v[thigh].rotation_q).normalized()
                                                      : (v[thigh].rotation_q.conjugate() * v[hips].rotation_q).normalized();
                    hip_cmd = qangle(nail->target_relative_q) * 57.2958f;
                    hip_act = qangle(rel) * 57.2958f;
                    hip_err = qangle((nail->target_relative_q.conjugate() * rel).normalized()) * 57.2958f;
                }
            if (feet_rows_level >= 2) {
                // the row per frame: where the foot is along the walk relative to the hips (the clip's reach at
                // landing, the leg's stretch at toe-off), its height over the support, its offset from its
                // anchor in the plane and in z, its tilt, and its contacts with the floor
                const Particle& h = v[hips];
                float dx = h.vx, dy = h.vy; const float sp = std::sqrt(dx * dx + dy * dy);
                if (sp > 0.1f) { dx /= sp; dy /= sp; } else { dx = std::sin(h.rotation_z); dy = std::cos(h.rotation_z); }
                const float ahead = (x - h.x) * dx + (y - h.y) * dy;
                char row[300];
                std::snprintf(row, sizeof(row), "  [foot f%3d] %s %s: ahead of hips %+.3f m | bottom - support %+.4f | to anchor dxy %.4f dz %+.4f | tilt rx %+.1f ry %+.1f deg | contacts %d pen max %.4f | hip cmd %.1f act %.1f err %.1f\n",
                              f, side, stance ? "STANCE" : "swing ", ahead, support_top > -1e8f ? bottom - support_top : 0.0f, stance ? sep : -1.0f, stance ? sep_z : 0.0f,
                              p.rotation_x * 57.2958f, p.rotation_y * 57.2958f, contacts, pen_max, hip_cmd, hip_act, hip_err);
                feet_frame_rows += row;
            }
        }
        FootHeight& fh = right ? fh_r : fh_l;
        fh.bottom_min = std::min(fh.bottom_min, bottom); fh.bottom_max = std::max(fh.bottom_max, bottom);
        fh.support_top = std::max(fh.support_top, support_top); fh.contacts += contacts; fh.near += near;
        const bool touching = contacts > 0;
        bool ended = false;
        auto close = [&]() {
            ended = true;
            char row[360];
            if (ph.stance) {
                ++feet_stances; feet_stance_slide_sum += ph.slide; feet_stance_early_sum += ph.slide_early;
                feet_stance_peak_max = std::max(feet_stance_peak_max, ph.peak_speed);
                if (ph.slide > feet_stance_slide_max) { feet_stance_slide_max = ph.slide; char b[48]; std::snprintf(b, sizeof(b), "%s stance f%d", side, ph.start); feet_worst = b; }
                feet_stance_gap_max = std::max(feet_stance_gap_max, ph.gap_max);
                std::snprintf(row, sizeof(row), "[feet] %s STANCE f%d-f%d (%d fr, touching %d): slide %.3f m, first %d fr %.3f, peak %.2f m/s, above support up to %.3f | anchor sep %.3f -> %.3f | pin blend %.2f -> %.2f",
                              side, ph.start, ph.start + ph.frames - 1, ph.frames, ph.contact_frames, ph.slide, BLEND_FRAMES, ph.slide_early, ph.peak_speed, ph.gap_max, ph.sep_start, ph.sep_end, ph.blend_start, ph.blend_end);
            } else {
                ++feet_swings; feet_swing_frames += ph.frames; feet_swing_contact_frames += ph.contact_frames;
                feet_swing_lift_max = std::max(feet_swing_lift_max, ph.lift_max);
                std::snprintf(row, sizeof(row), "[feet] %s swing  f%d-f%d (%d fr, touching %d): lift max %.3f m over the support, slide while touching %.3f m, peak %.2f m/s",
                              side, ph.start, ph.start + ph.frames - 1, ph.frames, ph.contact_frames, ph.lift_max, ph.slide_touching, ph.peak_speed);
            }
            if (!feet_last_row.empty()) feet_last_row += "\n  ";
            feet_last_row += row;
        };
        if (ph.active && ph.stance != stance) { close(); ph.active = false; }
        if (!ph.active) { ph = FootPhase{}; ph.active = true; ph.stance = stance; ph.start = f; ph.px = x; ph.py = y; ph.sep_start = sep; ph.blend_start = blend; }
        else {
            const float d = std::sqrt((x - ph.px) * (x - ph.px) + (y - ph.py) * (y - ph.py));
            ph.slide += d;
            if (ph.frames < BLEND_FRAMES) ph.slide_early += d;
            if (touching) ph.slide_touching += d;
            ph.peak_speed = std::max(ph.peak_speed, d / DT);
        }
        ph.frames++; if (touching) ph.contact_frames++;
        if (support_top > -1e8f) { ph.lift_max = std::max(ph.lift_max, bottom - support_top); ph.gap_max = std::max(ph.gap_max, bottom - support_top); }
        ph.px = x; ph.py = y; ph.sep_end = sep; ph.blend_end = blend;
        return ended;
    }
    // Call after step() every frame when feet_on. Returns true when a phase ended.
    bool feet_observe(Engine& engine, int f) {
        if (!feet_on) return false;
        feet_last_row.clear(); feet_frame_rows.clear();
        const bool l = foot_observe(engine, f, foot_l, toe_l, ph_l, "L");
        const bool r = foot_observe(engine, f, foot_r, toe_r, ph_r, "R");
        if (f % 30 == 29) {
            char row[300];
            std::snprintf(row, sizeof(row), "[feet f%3d] L bottom %.3f..%.3f support top %.3f overlaps %d near %d | R bottom %.3f..%.3f support top %.3f overlaps %d near %d",
                          f, fh_l.bottom_min, fh_l.bottom_max, fh_l.support_top, fh_l.contacts, fh_l.near, fh_r.bottom_min, fh_r.bottom_max, fh_r.support_top, fh_r.contacts, fh_r.near);
            feet_height_row = row; fh_l.reset(); fh_r.reset();
        } else feet_height_row.clear();
        return l || r;
    }
    std::string feet_summary() const {
        char b[320];
        std::snprintf(b, sizeof(b), "[feet] %d stances: slide max %.3f m (%s, bar %.3f), mean %.3f m, of which the first %d frames mean %.3f m, peak %.2f m/s, above support up to %.3f m (bar %.3f) | %d swings: touching the floor %d of %d swing frames, lift max %.3f m",
                      feet_stances, feet_stance_slide_max, feet_worst.c_str(), STANCE_SLIDE_MAX, feet_stances ? feet_stance_slide_sum / feet_stances : 0.0f, BLEND_FRAMES,
                      feet_stances ? feet_stance_early_sum / feet_stances : 0.0f, feet_stance_peak_max, feet_stance_gap_max, STANCE_GAP_MAX, feet_swings, feet_swing_contact_frames, feet_swing_frames, feet_swing_lift_max);
        return b;
    }

    // The worst nails, for the log: name, max gap, onset frame.
    std::string nails_summary(int max_n = 5) const {
        std::vector<std::pair<float, std::string>> v;
        for (const auto& [nm, r] : nails) v.push_back({r.gap_max, nm + " " + std::to_string(r.gap_max).substr(0, 6) + (r.onset >= 0 ? " (opens f" + std::to_string(r.onset) + ")" : "")});
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string out; int k = 0;
        for (const auto& [g, txt] : v) { if (k++ == max_n) break; if (!out.empty()) out += "; "; out += txt; }
        return out.empty() ? "none" : out;
    }
    // The top hands, for the log and the panel.
    std::string hands_summary(int max_sites = 3) const {
        std::vector<std::pair<int, std::string>> v;
        for (const auto& [site, n] : hands) v.push_back({n, site});
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string out;
        int k = 0;
        for (const auto& [n, site] : v) {
            if (k++ == max_sites) break;
            if (!out.empty()) out += ", ";
            out += site + " " + std::to_string(n);
        }
        return out.empty() ? "none" : out;
    }
};

}  // namespace scene_rails_and_muscles
