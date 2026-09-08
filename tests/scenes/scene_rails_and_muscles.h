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
        wx = p.x + x2 * cz - y1 * sz;
        wy = p.y + x2 * sz + y1 * cz;
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
        eva = hgen.generate_humanoid_physics(0.0f, 0.0f, FLOOR_TOP, -1, HumanoidSpec::eva(), false);
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
        argus.watch(hips, "hips");
        argus.watch(box_a, "box_a"); argus.watch(box_b, "box_b");
        argus.watch(post, "post");   argus.watch(arm, "arm");
        {
            auto v = ps.lock_particles_for_read();
            hx0 = v[hips].x; hy0 = v[hips].y;
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
        // name, or the hands-off count above is a count over ghosts.
        if (const logosphere::animation::HumanoidParts* parts = humanoid.get_humanoid_parts(hips)) {
            int stale = 0;
            for (unsigned int pid : parts->physics_drive_children) {
                bool live = false;
                for (const auto& j : parts->joint_hierarchy.joints) if (j.child_particle == pid) { live = true; break; }
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

    // SPACE: re-drop the boxes and restart every count. Eva keeps walking
    // (her rig cannot be re-registered cheaply); the hint line says so.
    void rearm(Engine& engine) {
        auto& ps = engine.get_particle_system();
        auto& physics = engine.get_physics_system();
        {
            auto v = ps.lock_particles_for_write();
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
        physics.forget_body((size_t)box_a); physics.forget_body((size_t)arm);
        hands.clear(); rail_hands.clear(); hand_records = 0; frames_with_hands = 0;
        replants = 0; replants_declared = 0; replants_ledger_loud = 0; replant_ledger_speed_max = 0.0f;
        for (auto& [id, eye] : eyes) { eye.check_frames = 0; eye.loud = false; }
        a_drop_max = 0; b_drift_max = 0; arm_err_max = 0; arm_sep0 = -1.0f; arm_sep_drift_max = 0;
        {
            auto v = ps.lock_particles_for_read();
            hx0 = v[hips].x; hy0 = v[hips].y;
        }
        forward = prev_forward = 0.0f; backward_frames = 0; walk_frames = 0;
        joint_gap_max = 0.0f; joint_gap_worst.clear(); joint_gap_frame = -1; reach_over_max = -1e9f; reach_worst.clear(); nails.clear();
        argus.reset_milestones(box_a); argus.reset_milestones(arm);
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
