// A predator hunting, live.
//
// The senses guard (test_predator_senses) proves each sense in
// isolation from a standing start. This one puts them together and
// lets it run: a predator that looks, chases what it sees, loses it
// behind a rock, goes to where it last saw it, and casts about when it
// gets there. Sight, smell, A* pathfinding and movement in one loop.
//
// That combination is the thing nothing in this engine guarded. GOAP
// and pathfinding each have their own test now, but "does a creature
// actually hunt" is a question about them working together over time,
// and it can only be answered by running.
//
// The interesting behaviour is the losing, not the chasing. A creature
// that always knows where its prey is does not need senses, does not
// need memory, and cannot be hidden from. Every assertion below is
// about the gap between what the world contains and what the predator
// knows.
//
//   ./build/test_predator_hunt                      numbers, asserted
//   LOGOSPHERE_VISUAL=1 ./build/test_predator_hunt  watch it, SPACE/ESC

#include "core/engine.h"
#include "core/particle_system.h"
#include "core/camera_system.h"
#include "sense_system.h"
#include "npc-ai/pathfinding_system.h"
#include "ui/ui_system.h"
#include "ui/text_window.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "particle.h"
#include "logosphere/interaction/particle_interaction_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/events/event_bus.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "  FAIL: " << msg << std::endl; }           \
    } while (0)

namespace {

bool g_visual = false;
bool g_quit = false;

constexpr float PI_F = 3.14159265358979f;
constexpr int   FAN_POOL = 220;      // reused every frame, never grown
// Debug markers are present but NOT SOLID. Without this the 900 fan
// beads are KINEMATIC bodies, and a KINEMATIC body is never skipped by
// the at-rest check (terrain must collide even while still), so they
// each generate contacts against everything they sit near. Measured
// here: physics 1652 ms per frame at 1346 particles, which is 0.6 fps
// and looks exactly like a frozen creature.
//
// This is the same defect as the Eden butterflies, 0.07 ms to 30 ms,
// and it has the same answer: an interaction profile that declines
// rigid contact.
constexpr uint32_t kMarkerProfileId = 90101u;

// The panel's highlight colour, defined ONCE. The guard looks for these
// exact bytes in the image handed to the display, so if the panel and
// the guard disagree the guard reports zero and blames the engine.
constexpr uint8_t HL_R = 255, HL_G = 232, HL_B = 140;
constexpr uint8_t TX_R = 205, TX_G = 205, TX_B = 205;

// What the predator is doing. Named for what it knows, not for a
// fantasy: these are the three states any hunter has.
enum class Hunt {
    CASTING,     // no idea where the prey is; sweeping for a scent
    STALKING,    // can see it right now
    SEARCHING,   // cannot see it, but remembers where it was
};

const char* hunt_name(Hunt h) {
    switch (h) {
        case Hunt::CASTING:   return "CASTING";
        case Hunt::STALKING:  return "STALKING";
        case Hunt::SEARCHING: return "SEARCHING";
    }
    return "?";
}

// Everything the creature currently knows, refreshed every frame, so
// the window can show WHY it is doing what it is doing rather than
// only what it is doing.
struct Readout {
    bool  prey_in_cone = false;
    float prey_distance = 0.0f;
    float prey_bearing = 0.0f;      // radians off the nose
    int   things_in_cone = 0;
    bool  smelled = false;
    float smell_distance = 0.0f;
    float smell_bearing = 0.0f;
    float smell_probability = 0.0f;
    int   path_waypoints = 0;
    float memory_age_s = 0.0f;
};

// The AI's own account of itself. Kept short and written only when
// something HAPPENS, so the log reads as a story rather than a dump.
struct EventLog {
    std::vector<std::string> lines;
    void add(float t, const std::string& what) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "%6.2fs  %s", t, what.c_str());
        lines.emplace_back(buf);
        std::cout << "    " << buf << std::endl;      // terminal too
        if (lines.size() > 11) lines.erase(lines.begin());
    }
};

struct Trace {
    int frames = 0;
    int frames_seen = 0;
    int frames_searching = 0;
    float closest = 1e9f;
    float start_distance = 0.0f;
    bool  ever_saw = false;
    bool  ever_lost_after_seeing = false;
    bool  saw_through_boulder = false;   // must stay false
    int   frames_inside_rock = 0;        // predator standing in geometry
    float deepest_intrusion = 0.0f;      // metres past the boulder face
    float furthest_seen = 0.0f;
};

struct Hunt3D;
struct Trace;
std::vector<std::string> build_panel(const Hunt3D& h, const Trace& tr,
                                     float t_seconds, int laps,
                                     bool include_log);

struct Hunt3D {
    Engine engine;
    SenseSystem senses;
    pathfinding::NavGrid grid;
    pathfinding::Pathfinder finder;

    int predator = -1;
    int prey = -1;
    std::vector<int> fan_pool;
    std::vector<int> boulders;

    float px = 0, py = 0, facing = 0.0f;
    pathfinding::Path path;
    Hunt state = Hunt::CASTING;
    float last_known_x = 0, last_known_y = 0;
    bool  has_memory = false;
    float cast_timer = 0.0f;
    Readout read;
    EventLog log;
    // The NPC's thinking, as a real UI widget rather than immediate-mode
    // text. UISystem::render() redraws registered widgets INSIDE
    // Engine::render(), which is the slot that survives: drawing after
    // render() meant the UI dirty bounds were wiped by the next frame
    // whenever present() skipped, and the display was handed an empty
    // overlay. That is why the window was blank.
    std::unique_ptr<TextWindow> panel;
    // Ray-sampled vision FLICKERS on a moving observer: the cone is 32
    // rays, so as the creature walks and turns the prey slips between
    // them for a frame or two at a time. Raw, that produced a
    // STALKING/SEARCHING oscillation twenty times a second.
    //
    // npc_ai_research.md, Key Takeaway 5: "Hysteresis prevents jitter -
    // different thresholds for enter/exit". So: believe sight the
    // instant it arrives, but require a run of misses before believing
    // it is gone. The raw signal is still counted and shown, because
    // hiding it would hide a real property of the sensor.
    int  miss_streak = 0;
    int  raw_dropouts = 0;
    // Movement heartbeat. The log used to record only DECISIONS, so a
    // creature walking steadily across the map produced no lines at
    // all and the log looked frozen while the animal plainly moved.
    // Casting about must MOVE. Without this the creature stood on one
    // spot spinning whenever it had no scent and no memory, which the
    // movement log made obvious: "0.0 m walked" for ten seconds
    // running. Wander is the first behaviour in the research for a
    // reason.
    float wander_x = 0, wander_y = 0;
    bool  has_wander = false;
    float wander_set_at = -100.0f;
    unsigned wander_seed = 12345u;
    float goal_shown_x = 0, goal_shown_y = 0;
    float last_beat = -1.0f;
    float beat_x = 0, beat_y = 0, beat_dist = 0;
    static constexpr int MISSES_BEFORE_LOST = 15;   // 0.25 s at 60 Hz
    float clock = 0.0f;
    float memory_taken_at = 0.0f;
    int   last_path_len = -1;

    explicit Hunt3D(bool with_display) : engine(nullptr) {
        EngineConfig cfg;
        cfg.create_display = with_display;
        cfg.window_width = 1100;
        cfg.window_height = 720;
        cfg.window_title = "a predator hunting";
        cfg.show_debug_overlay = false;
        cfg.enable_chat_window = false;
        if (engine.initialize(cfg) < 0)
            throw std::runtime_error("Engine::initialize() failed");
        grid.init(-40.0f, -40.0f, 40.0f, 40.0f, 1.0f);
    }
    ~Hunt3D() { engine.shutdown(); }

    int add(ParticleShape shape, float x, float y, float z, float size,
            float r, float g, float b, OdorType odor = OdorType::NONE) {
        Particle p{};
        p.shape = shape;
        p.x = x; p.y = y; p.z = z;
        p.width = p.height = p.thickness = size;
        p.size = size;
        p.r = r; p.g = g; p.b = b; p.a = 1.0f;
        p.SetMaterial(shape == ParticleShape::BOX ? Materials::Type::STONE
                                                  : Materials::Type::FLESH);
        p.odor_type = odor;
        if (odor != OdorType::NONE) {
            p.odor_radius = 25.0f;
            p.odor_intensity = 1.0f;
        }
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        return engine.add_particle(p);
    }

    void boulder(float x, float y, float w, float d, float h) {
        Particle p{};
        p.shape = ParticleShape::BOX;
        p.x = x; p.y = y; p.z = h * 0.5f;
        p.width = w; p.height = d; p.thickness = h;
        p.size = std::max(w, d);
        p.r = 0.45f; p.g = 0.43f; p.b = 0.40f; p.a = 1.0f;
        p.SetMaterial(Materials::Type::STONE);
        p.odor_type = OdorType::NONE;
        p.solver_mode = ParticleSolverMode::KINEMATIC;
        p.is_at_rest = true;
        boulders.push_back(engine.add_particle(p));
        // The same rock, told to the navigator. Sight and navigation
        // must agree about the world or the creature walks into what
        // it can see.
        grid.set_blocked_rect(x - w * 0.5f - 0.6f, y - d * 0.5f - 0.6f,
                              x + w * 0.5f + 0.6f, y + d * 0.5f + 0.6f, true);
    }

    void ground() {
        if (!g_visual) return;
        // 8 m tiles, not 4: the ground is scenery and 441 boxes of it
        // cost more than the creature did.
        for (float x = -40.0f; x <= 40.0f; x += 8.0f)
            for (float y = -40.0f; y <= 40.0f; y += 8.0f) {
                Particle p{};
                p.shape = ParticleShape::BOX;
                p.x = x; p.y = y; p.z = -0.25f;
                p.width = p.height = 8.0f; p.thickness = 0.5f; p.size = 8.0f;
                p.r = 0.38f; p.g = 0.36f; p.b = 0.33f; p.a = 1.0f;
                p.SetMaterial(Materials::Type::STONE);
                p.solver_mode = ParticleSolverMode::KINEMATIC;
                p.is_at_rest = true;
                engine.add_particle(p);
            }
    }

    void make_panel() {
        if (!g_visual) return;
        auto* ui = engine.get_ui_system();
        if (!ui) return;
        panel = std::make_unique<TextWindow>("NPC / AI", "npc_ai_log");
        // Lower right: the hunt happens in the middle of the frame, and
        // a panel in the top-left sat on top of it.
        panel->set_position(560, 268);
        panel->set_size(528, 440);
        // MUST exceed what refresh_panel pushes. TextWindow trims from
        // the FRONT when newest_at_top is false, so a budget that is
        // too small silently evicts the live readout at the top and
        // leaves only the tail of the event log. The panel pushed ~30
        // lines against a budget of 26 and the readout vanished.
        panel->set_max_lines(34);
        panel->set_newest_at_top(false);     // read top to bottom
        panel->set_background_alpha(210);    // legible over the scene
        panel->set_draggable(true);
        ui->add_widget(panel.get());
    }

    // Rebuilt each frame: the live readout, then the decision log.
    void refresh_panel(const Trace& tr, float t_seconds, int laps) {
        if (!panel) return;
        panel->clear();
        for (const std::string& l : build_panel(*this, tr, t_seconds, laps, true))
        {
            const bool hl = !l.empty() && l[0] == '>';
            panel->add_line(l.empty() ? " " : (hl ? l.substr(1) : l),
                            hl ? HL_R : TX_R, hl ? HL_G : TX_G,
                            hl ? HL_B : TX_B);
        }
    }

    void make_markers_ghostly() {
        auto& interaction = engine.get_interaction_system();
        logosphere::interaction::InteractionProfile ghost;
        ghost.id = kMarkerProfileId;
        ghost.category = 1u << 7;
        ghost.collides_with = 0u;          // touches nothing
        interaction.register_profile(ghost);

        auto w = engine.get_particle_system().lock_particles_for_write();
        auto& all = w.get_particles();
        for (int idx : fan_pool) {
            if (idx < 0 || idx >= static_cast<int>(all.size())) continue;
            all[idx].interaction_profile_id = kMarkerProfileId;
            all[idx].is_at_rest = true;
            all[idx].vx = all[idx].vy = all[idx].vz = 0.0f;
        }
    }

    void make_fan_pool() {
        if (!g_visual) return;
        for (int i = 0; i < FAN_POOL; ++i)
            fan_pool.push_back(add(ParticleShape::SPHERE, 0, 0, -200.0f,
                                   0.42f, 0.25f, 0.85f, 0.35f));
        make_markers_ghostly();
    }

    void settle() { for (int i = 0; i < 4; ++i) engine.update(1.0 / 60.0); }

    // exclude_ids is documented as "self + body parts", and it means
    // it: every ray starts inside the predator's own 1.6 m sphere, so
    // without this the creature is blinded by its own body and sees
    // nothing anywhere. Its own debug markers go in here too.
    std::vector<int> blind_to() const {
        std::vector<int> v = fan_pool;
        if (predator >= 0) v.push_back(predator);
        return v;
    }

    SenseConfig eyes() const {
        SenseConfig c;
        c.fov_degrees = 100.0f;
        c.vision_range = 22.0f;
        c.ray_count = 32;
        return c;
    }
    SmellConfig nose() const {
        SmellConfig c;
        c.attracted_to = {OdorType::LIVING_FLESH};
        c.sensitivity = 1.0f;
        c.state_factor = 1.0f;
        return c;
    }

    // A creature whose position is WRITTEN is not pushed out of
    // anything by the solver, so it must refuse to enter geometry
    // itself. Measured before this existed: 112 frames of 900 spent
    // standing inside the boulder, up to 0.33 m in.
    //
    // Slide rather than stop dead: a hunter that halts the instant a
    // corner clips it looks broken, and sliding along the free axis is
    // what collide-and-slide does everywhere else in this engine.
    void step_to(float nx, float ny) {
        if (grid.is_walkable(nx, ny)) { px = nx; py = ny; return; }
        if (grid.is_walkable(nx, py)) { px = nx; return; }   // slide in x
        if (grid.is_walkable(px, ny)) { py = ny; return; }   // slide in y
        // Wedged: stay put and let the planner pick another way.
    }

    void move_particle(int id, float x, float y) {
        auto w = engine.get_particle_system().lock_particles_for_write();
        auto& all = w.get_particles();
        if (id >= 0 && id < static_cast<int>(all.size())) {
            all[id].x = x; all[id].y = y;
        }
    }

    bool can_see_prey(float& out_x, float& out_y) {
        auto view = engine.get_particle_system().lock_particles_for_read();
        const BVH* bvh = engine.get_particle_system().get_shadow_bvh();
        if (!bvh) return false;
        // Unfiltered first, so the readout can say how much is in the
        // cone at all, then the prey specifically.
        SenseResult all = senses.cast_vision_cone(eyes(), px, py, 1.0f,
                                                  facing, view.get(), *bvh,
                                                  {}, blind_to());
        read.things_in_cone = static_cast<int>(all.visible_targets.size());
        if (const SenseTarget* t = all.find_by_id(prey)) {
            out_x = t->x; out_y = t->y;
            read.prey_in_cone = true;
            read.prey_distance = t->distance;
            read.prey_bearing = t->angle_offset;
            return true;
        }
        read.prey_in_cone = false;
        return false;
    }

    // Redraw the visibility fan by MOVING a fixed pool of markers, so
    // the picture animates without the particle count growing.
    int fan_tick = 0;
    void refresh_fan() {
        if (!g_visual || fan_pool.empty()) return;
        // Every 3rd frame. The cone barely moves in 50 ms, and 600 shadow
        // rays a frame cost more than the render did, dragging the whole
        // demo to a few frames a second.
        if ((fan_tick++ % 3) != 0) return;
        // Recomputed every 3rd frame. The cone barely changes in 50 ms
        // and 600 shadow rays a frame was costing more than the render.
        std::vector<std::pair<float,float>> pts;
        {
            auto view = engine.get_particle_system().lock_particles_for_read();
            const BVH* bvh = engine.get_particle_system().get_shadow_bvh();
            if (!bvh) return;
            const float half = 50.0f * PI_F / 180.0f;
            const int rays = 14;
            for (int i = 0; i < rays && pts.size() < fan_pool.size(); ++i) {
                const float t = static_cast<float>(i) / (rays - 1);
                const float a = facing - half + t * 2.0f * half;
                const float dx = std::sin(a), dy = std::cos(a);
                for (float d = 2.0f; d <= 22.0f; d += 2.2f) {
                    if (pts.size() >= fan_pool.size()) break;
                    const float qx = px + dx * d, qy = py + dy * d;
                    if (!senses.can_see_point(px, py, 1.0f, qx, qy, 1.0f,
                                              view.get(), *bvh, blind_to()))
                        break;
                    pts.emplace_back(qx, qy);
                }
            }
        }
        auto w = engine.get_particle_system().lock_particles_for_write();
        auto& all = w.get_particles();
        for (size_t i = 0; i < fan_pool.size(); ++i) {
            Particle& p = all[fan_pool[i]];
            if (i < pts.size()) {
                p.x = pts[i].first; p.y = pts[i].second; p.z = 0.3f;
                // Colour says what the predator is doing.
                if (state == Hunt::STALKING) { p.r = 1.0f; p.g = 0.45f; p.b = 0.2f; }
                else if (state == Hunt::SEARCHING) { p.r = 0.95f; p.g = 0.85f; p.b = 0.25f; }
                else { p.r = 0.25f; p.g = 0.85f; p.b = 0.35f; }
            } else {
                p.z = -200.0f;                 // parked out of sight
            }
        }
    }

    // One tick of the hunt. This is the whole creature.
    void tick(float dt, Trace& tr) {
        float seen_x = 0, seen_y = 0;
        const bool seen = can_see_prey(seen_x, seen_y);

        tr.frames++;
        clock += dt;
        const Hunt was = state;
        char note[160];

        if (seen) { miss_streak = 0; }
        else if (tr.ever_saw) { if (miss_streak == 0) ++raw_dropouts; ++miss_streak; }
        // Sight is BELIEVED lost only after a run of misses.
        const bool believes_seen = seen || (tr.ever_saw && has_memory &&
                                            miss_streak < MISSES_BEFORE_LOST);

        if (seen) {
            tr.frames_seen++;
            if (!tr.ever_saw) {
                std::snprintf(note, sizeof(note),
                    "SIGHT   prey acquired at %.1f m, %+.0f deg off the nose",
                    read.prey_distance, read.prey_bearing * 180.0f / PI_F);
                log.add(clock, note);
            }
            tr.ever_saw = true;
            last_known_x = seen_x; last_known_y = seen_y;
            memory_taken_at = clock;
            has_memory = true;
            state = Hunt::STALKING;
        } else if (believes_seen) {
            // Within the grace window: hold the belief, keep walking to
            // the last seen spot rather than announcing a loss.
            state = Hunt::STALKING;
        } else if (has_memory) {
            if (state == Hunt::STALKING) {
                tr.ever_lost_after_seeing = true;
                std::snprintf(note, sizeof(note),
                    "LOST    sight gone %d frames at %.1f m; remembering (%.1f, %.1f)",
                    miss_streak, std::hypot(px - prey_x(), py - prey_y()),
                    last_known_x, last_known_y);
                log.add(clock, note);
            }
            state = Hunt::SEARCHING;
        } else {
            state = Hunt::CASTING;
        }
        if (was != state && !(was == Hunt::STALKING && state == Hunt::SEARCHING)
            && !(was == Hunt::CASTING && state == Hunt::CASTING)) {
            if (!(was == Hunt::CASTING && state == Hunt::STALKING && !tr.ever_saw)) {
                std::snprintf(note, sizeof(note), "STATE   %s -> %s",
                              hunt_name(was), hunt_name(state));
                log.add(clock, note);
            }
        }
        read.memory_age_s = has_memory ? (clock - memory_taken_at) : 0.0f;
        if (state == Hunt::SEARCHING) tr.frames_searching++;

        // Where to go: what it can see, else what it remembers, else a
        // scent, else nothing.
        float goal_x = px, goal_y = py;
        bool have_goal = false;
        if (state == Hunt::STALKING || state == Hunt::SEARCHING) {
            goal_x = last_known_x; goal_y = last_known_y;
            have_goal = true;
        } else {
            auto view = engine.get_particle_system().lock_particles_for_read();
            SmellResult sm = senses.check_smell(nose(), px, py, 1.0f, 1.0f,
                                                view.get(), blind_to());
            read.smelled = sm.detected;
            read.smell_distance = sm.distance;
            read.smell_bearing = sm.direction;
            // The model's own probability, for the readout: it explains
            // why a sniff at this range succeeds or fails.
            {
                const float d = std::hypot(px - prey_x(), py - prey_y());
                const float r = 25.0f;
                read.smell_probability = (d >= r) ? 0.0f
                    : std::max(0.0f, 1.0f - (d / r) * (d / r));
            }
            if (sm.detected) {
                goal_x = px + std::sin(sm.direction) * 6.0f;
                goal_y = py + std::cos(sm.direction) * 6.0f;
                have_goal = true;
                has_wander = false;          // a scent beats wandering
                static int sniff_notes = 0;
                if (sniff_notes++ % 45 == 0) {
                    char n2[160];
                    std::snprintf(n2, sizeof(n2),
                        "SMELL   caught a scent %.1f m off, bearing %+.0f deg (p=%.2f)",
                        sm.distance, sm.direction * 180.0f / PI_F,
                        read.smell_probability);
                    log.add(clock, n2);
                }
            }
        }

        // Arrived at a memory with nothing in sight: the trail is cold.
        if (state == Hunt::SEARCHING &&
            std::hypot(last_known_x - px, last_known_y - py) < 1.5f) {
            has_memory = false;
            state = Hunt::CASTING;
            have_goal = false;
            char n3[160];
            std::snprintf(n3, sizeof(n3),
                "COLD    reached the remembered spot, nothing here; casting about");
            log.add(clock, n3);
        }

        if (have_goal) {
            path = finder.find_path(grid, {px, py}, {goal_x, goal_y});

            // A goal can land in a blocked cell: the prey walks close to
            // a rock, or the smell bearing points into one. Freezing is
            // the wrong answer, and it is what the creature did for ten
            // seconds. Look for the nearest standable square instead.
            if (!path.valid || path.empty()) {
                bool rescued = false;
                for (float ring = 1.0f; ring <= 4.0f && !rescued; ring += 1.0f) {
                    for (int k = 0; k < 8 && !rescued; ++k) {
                        const float a = k * (PI_F / 4.0f);
                        const float cx = goal_x + std::sin(a) * ring;
                        const float cy = goal_y + std::cos(a) * ring;
                        if (!grid.is_walkable(cx, cy)) continue;
                        path = finder.find_path(grid, {px, py}, {cx, cy});
                        if (path.valid && !path.empty()) {
                            rescued = true;
                            goal_shown_x = cx; goal_shown_y = cy;
                        }
                    }
                }
                if (!rescued) {
                    // Nothing reachable at all: walk straight at it and
                    // let the world stop us, rather than stand still.
                    const float dx0 = goal_x - px, dy0 = goal_y - py;
                    const float l0 = std::hypot(dx0, dy0);
                    if (l0 > 0.05f) {
                        step_to(px + (dx0 / l0) * 2.5f * dt,
                                py + (dy0 / l0) * 2.5f * dt);
                        facing = std::atan2(dx0, dy0);
                    }
                }
            }
            read.path_waypoints = static_cast<int>(path.size());
            if (last_path_len >= 0 &&
                std::abs(static_cast<int>(path.size()) - last_path_len) > 6) {
                char n4[160];
                std::snprintf(n4, sizeof(n4),
                    "PATH    replanned, %d waypoints, %.1f m to walk",
                    static_cast<int>(path.size()), path.total_length());
                log.add(clock, n4);
            }
            last_path_len = static_cast<int>(path.size());
            if (path.valid && !path.empty()) {
                // The path is recomputed from the CURRENT position every
                // frame, so waypoint 0 is always the cell already being
                // stood in. Aiming at it means walking backwards to the
                // centre of your own cell.
                //
                // The old "advance only if within 0.6 m" test made that
                // worse rather than better: at 0.608 m it declined to
                // advance and walked back toward the cell centre, at
                // 0.523 m it advanced and walked forward, so the
                // creature ping-ponged across the threshold and stood
                // still for thirteen seconds while its prey walked off.
                // Measured, before: 0.0 m walked per second.
                if (path.size() > 1) path.advance();
                pathfinding::Point wp = path.current();
                const float dx = wp.x - px, dy = wp.y - py;
                const float len = std::hypot(dx, dy);
                if (len > 0.01f) {
                    const float speed = 5.0f;
                    step_to(px + (dx / len) * speed * dt,
                            py + (dy / len) * speed * dt);
                    facing = std::atan2(dx, dy);   // 0 = +Y
                }
            }
        }

        // Nothing seen, remembered or smelled: go somewhere else and
        // look from there.
        if (!have_goal) {
            const bool arrived = has_wander &&
                std::hypot(wander_x - px, wander_y - py) < 1.5f;
            if (!has_wander || arrived || (clock - wander_set_at) > 6.0f) {
                wander_seed = wander_seed * 1664525u + 1013904223u;
                const float a = static_cast<float>(wander_seed % 6283) / 1000.0f;
                wander_seed = wander_seed * 1664525u + 1013904223u;
                const float r = 8.0f + static_cast<float>(wander_seed % 1200) / 100.0f;
                wander_x = std::max(-34.0f, std::min(34.0f, px + std::sin(a) * r));
                wander_y = std::max(-34.0f, std::min(34.0f, py + std::cos(a) * r));
                has_wander = true;
                wander_set_at = clock;
                char n5[160];
                std::snprintf(n5, sizeof(n5),
                    "WANDER  nothing to go on; trying (%.1f, %.1f)",
                    wander_x, wander_y);
                log.add(clock, n5);
            }
            goal_x = wander_x; goal_y = wander_y;
            have_goal = true;
        }
        goal_shown_x = goal_x; goal_shown_y = goal_y;

        move_particle(predator, px, py);

        // Once a second, say where it went and whether that helped.
        const float d_now = std::hypot(px - prey_x(), py - prey_y());
        if (last_beat < 0.0f) {
            last_beat = clock; beat_x = px; beat_y = py; beat_dist = d_now;
        } else if (clock - last_beat >= 1.0f) {
            const float travelled = std::hypot(px - beat_x, py - beat_y);
            const float closed = beat_dist - d_now;
            char mv[176];
            std::snprintf(mv, sizeof(mv),
                "MOVE    %s to (%.1f, %.1f), %.1f m walked, %s %.1f m  [%.1f m out, path %s %d wp, goal (%.1f, %.1f)]",
                hunt_name(state), px, py, travelled,
                closed >= 0.0f ? "closed" : "lost  ", std::fabs(closed), d_now,
                path.valid ? "ok" : "INVALID", (int)path.size(),
                goal_shown_x, goal_shown_y);
            log.add(clock, mv);
            last_beat = clock; beat_x = px; beat_y = py; beat_dist = d_now;
        }

        // Does the creature actually respect the rock, or walk through
        // it? Its position is WRITTEN, not solved, so nothing pushes it
        // out. The nav grid steers around the boulder, but the
        // straight-line fallback does not consult it at all.
        if (!grid.is_walkable(px, py)) {
            tr.frames_inside_rock++;
            // Boulder spans x 2.5..9.5, y 9..11 in this scene.
            const float dx_in = std::min(px - 2.5f, 9.5f - px);
            const float dy_in = std::min(py - 9.0f, 11.0f - py);
            const float depth = std::min(dx_in, dy_in);
            if (depth > 0.0f)
                tr.deepest_intrusion = std::max(tr.deepest_intrusion, depth);
        }

        const float d = std::hypot(px - prey_x(), py - prey_y());
        tr.closest = std::min(tr.closest, d);
        if (seen) tr.furthest_seen = std::max(tr.furthest_seen, d);
    }

    float prey_x() const { return prey_pos_x; }
    float prey_y() const { return prey_pos_y; }
    float prey_pos_x = 0, prey_pos_y = 0;

    void move_prey(float x, float y) {
        prey_pos_x = x; prey_pos_y = y;
        move_particle(prey, x, y);
    }
};

// ---------------------------------------------------------------- viewing

// The NPC's thinking, as text. Built in ONE place so the guard below
// and the window are provably showing the same thing.
//
// include_log exists for the guard: it renders the panel with and
// without the event log and compares the lit-pixel counts, which is
// how "the decisions are on screen" is proved rather than asserted.
std::vector<std::string> build_panel(const Hunt3D& h, const Trace& tr,
                                     float t_seconds, int laps,
                                     bool include_log) {
    const float dist = std::hypot(h.px - h.prey_x(), h.py - h.prey_y());
    char b[9][160];
    std::snprintf(b[0], 160, "A PREDATOR HUNTING     t = %6.1f s     lap %d",
                  t_seconds, laps + 1);
    std::snprintf(b[1], 160, "state  %-9s   %s", hunt_name(h.state),
                  h.state == Hunt::STALKING  ? "(it can see the prey)"
                : h.state == Hunt::SEARCHING ? "(walking to a memory)"
                                             : "(no idea where it is)");
    std::snprintf(b[2], 160,
                  "SIGHT  cone holds %d thing(s).  prey in cone: %-3s  (raw dropouts %d)",
                  h.read.things_in_cone, h.read.prey_in_cone ? "YES" : "no",
                  h.raw_dropouts);
    if (h.read.prey_in_cone)
        std::snprintf(b[3], 160, "       prey at %.1f m, %+.0f deg off the nose",
                      h.read.prey_distance, h.read.prey_bearing * 180.0f / PI_F);
    else
        std::snprintf(b[3], 160,
                      "       (true distance %.1f m - the predator does not know)",
                      dist);
    std::snprintf(b[4], 160, "SMELL  %s   p=%.2f at %.1f m",
                  h.read.smelled ? "caught a scent" : "nothing       ",
                  h.read.smell_probability, dist);
    std::snprintf(b[5], 160, "MEMORY %s   last seen (%.1f, %.1f), %.1f s ago",
                  h.has_memory ? "yes" : "no ", h.last_known_x, h.last_known_y,
                  h.read.memory_age_s);
    std::snprintf(b[6], 160, "PATH   %d waypoints ahead", h.read.path_waypoints);
    std::snprintf(b[7], 160, "TALLY  seen on %d of %d frames, closest %.1f m",
                  tr.frames_seen, tr.frames, tr.closest);

    std::vector<std::string> lines = {
        std::string(">") + b[0], "",
        b[1], "",
        b[2], b[3], b[4], b[5], b[6], "",
        b[7], "",
        ">GREEN cone = casting   ORANGE = sees it   YELLOW = searching"};
    if (include_log) {
        lines.push_back("");
        lines.push_back(">EVENT LOG   (what the AI decided, and when)");
        for (const std::string& l : h.log.lines) lines.push_back(l);
    }
    lines.push_back("");
    lines.push_back("ESC to stop");
    return lines;
}

void draw_caption(Engine& e, const std::vector<std::string>& lines) {
    auto* ui = e.get_ui_system();
    if (!ui) return;
    int y = 18;
    for (const std::string& l : lines) {
        const bool key = !l.empty() && l[0] == '>';
        ui->draw_text(20, y, key ? l.substr(1) : l,
                      key ? 255 : 225, key ? 235 : 225, key ? 140 : 225);
        y += 15;
    }
}

void bring_to_front(Engine& e) {
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return;
    glfwShowWindow(win);
    glfwRestoreWindow(win);
    for (int i = 0; i < 8; ++i) { glfwPollEvents(); glfwFocusWindow(win); }
    std::cout << "    window focused: "
              << (glfwGetWindowAttrib(win, GLFW_FOCUSED) ? "yes"
                                                         : "NO - click it")
              << std::endl;
}

bool pump(Engine& e) {
    e.get_platform()->poll_events();
    auto* win = static_cast<GLFWwindow*>(
        e.get_platform()->get_native_window_handle());
    if (!win) return true;
    if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) { g_quit = true; return false; }
    if (e.get_platform()->should_close()) { g_quit = true; return false; }
    return true;
}


// ======================================================================
// THE GUARD: the NPC's thinking must actually be ON THE SCREEN.
// ======================================================================
//
// Not "a panel exists in the source", not "it looked right once": the
// UI rasterises into an overlay buffer that can be read with
// create_display=false, so this runs headless in CI and fails if the
// decisions stop reaching the window.
//
// Counting lit pixels alone would be too weak, because the header
// alone would light some. So the panel is rendered TWICE, once with
// the event log and once without, and the difference is the log. If
// the AI's decisions are not being drawn, that difference collapses
// and this fails.

int lit_pixels(Engine& e) {
    const PixelBuffer& ui = e.get_ui_overlay_buffer();
    int lit = 0;
    for (int y = 0; y < ui.height(); ++y)
        for (int x = 0; x < ui.width(); ++x)
            if (ui.get_pixel(x, y).a != 0) ++lit;
    return lit;
}

// The check that actually answers "will he see it".
//
// Reading the UI overlay buffer proves the text RASTERISED. It does
// NOT prove the window shows it: present() composites the overlay onto
// the presented image separately, and my earlier screenshots merged
// the two buffers myself in a script, which is why they looked right
// while the real window was blank.
//
// Engine::get_overlay_staging() is exactly what was handed to the
// display. If the caption colour is not in there, nothing was shown.
void test_the_panel_reaches_the_presented_image(Hunt3D& h, const Trace& tr) {
    if (!g_visual) {
        std::cout << "  (window compositing check needs a display; run with "
                     "LOGOSPHERE_VISUAL=1)" << std::endl;
        return;
    }
    // present() legitimately SKIPS a frame when no new GPU frame is
    // ready and the UI refresh interval has not elapsed, so a single
    // render/present proves nothing. Give it a few frames, which is
    // what the real loop does anyway. Reporting FAIL off one skipped
    // present is how this check called a working panel broken.
    for (int i = 0; i < 12; ++i) {
        h.refresh_panel(tr, 1.0f, 0);   // the widget, as the loop uses it
        h.engine.render();              // UISystem draws widgets in here
        h.engine.present();
        if (!h.engine.get_overlay_staging().empty()) break;
    }

    const std::vector<uint32_t>& staged = h.engine.get_overlay_staging();
    const uint32_t header = (0xFFu << 24) |
                            (static_cast<uint32_t>(HL_R) << 16) |
                            (static_cast<uint32_t>(HL_G) << 8) |
                             static_cast<uint32_t>(HL_B);
    int header_pixels = 0;
    for (uint32_t px : staged) if (px == header) ++header_pixels;

    std::cout << "  [measure] overlay staged for the display: "
              << staged.size() << " pixels, of which " << header_pixels
              << " are caption text" << std::endl;
    CHECK(!staged.empty(),
          "present() built an overlay for the display at all");
    CHECK(header_pixels > 50,
          "and the caption text is IN the image handed to the window (" +
          std::to_string(header_pixels) + " pixels of it)");
}

// Touch WITHOUT being pushed, but WITH consequence.
//
// The engine already separates these, and I had not used it. A pair
// whose profiles decline rigid contact is not simply ignored: the
// solver records a FilteredOverlap and process_filtered_overlaps emits
// one ContactFilteredEvent per pair EPISODE on the event bus. There is
// even a declarative TransformationRule whose trigger is
// ON_CONTACT_FILTERED.
//
// So the distinction the engine offers is not "collides or not". It is:
//
//   rigid contact   impulse + CollisionEvent        (both kinematic:
//                                                    event, no impulse)
//   filtered        no impulse + ContactFilteredEvent
//
// That is exactly what "body parts must not shove each other, but a
// predator touching prey must MEAN something" needs: the parts filter
// and nobody listens, the creatures filter and the game listens.
void test_a_filtered_touch_still_reports_itself() {
    std::cout << "\n  Filtered contact" << std::endl;
    Hunt3D h(false);

    // Two bodies that decline to shove each other, as animation-driven
    // creatures must.
    auto& interaction = h.engine.get_interaction_system();
    logosphere::interaction::InteractionProfile creature;
    creature.id = 90201u;
    creature.category = 1u << 9;
    creature.collides_with = 0u;          // no rigid contact between them
    interaction.register_profile(creature);

    const int a = h.add(ParticleShape::SPHERE, 0.0f, 0.0f, 1.0f, 2.0f,
                        0.9f, 0.7f, 0.2f);
    const int b = h.add(ParticleShape::SPHERE, 1.0f, 0.0f, 1.0f, 2.0f,
                        0.85f, 0.25f, 0.25f);
    {
        auto w = h.engine.get_particle_system().lock_particles_for_write();
        auto& all = w.get_particles();
        all[a].interaction_profile_id = creature.id;
        all[b].interaction_profile_id = creature.id;
    }

    int touches = 0;
    h.engine.get_event_bus().contact_filtered().subscribe(
        [&touches](const logosphere::ontology::ContactFilteredEvent&) {
            ++touches;
        });

    for (int i = 0; i < 20; ++i) h.engine.update(1.0 / 60.0);

    float ax = 0, bx = 0;
    {
        auto view = h.engine.get_particle_system().lock_particles_for_read();
        ax = view[a].x; bx = view[b].x;
    }
    std::cout << "  [measure] two overlapping bodies that DECLINE rigid "
                 "contact: " << touches << " ContactFilteredEvent(s), "
              << "separation still " << std::fabs(bx - ax) << " m"
              << std::endl;

    CHECK(touches > 0,
          "declining rigid contact still REPORTS the touch, so a game "
          "can make it mean something (" + std::to_string(touches) +
          " events)");
    CHECK(std::fabs(std::fabs(bx - ax) - 1.0f) < 0.05f,
          "and neither body was shoved, so animation keeps control");
}

void test_the_ai_thinking_is_on_the_screen() {
    std::cout << "\n  The AI panel" << std::endl;

    Hunt3D h(/*with_display=*/false);
    h.boulder(6.0f, 10.0f, 7.0f, 2.0f, 5.0f);
    h.predator = h.add(ParticleShape::SPHERE, -14.0f, -6.0f, 1.0f, 1.6f,
                       0.95f, 0.75f, 0.2f);
    h.prey = h.add(ParticleShape::SPHERE, 0.0f, 0.0f, 1.0f, 2.0f,
                   0.85f, 0.25f, 0.25f, OdorType::LIVING_FLESH);
    h.settle();
    h.px = -14.0f; h.py = -6.0f; h.facing = std::atan2(14.0f, 6.0f);
    h.move_prey(0.0f, 0.0f);

    // Run far enough that the creature has actually decided things.
    Trace tr;
    tr.start_distance = 15.2f;
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < 400; ++f) {
        const float t = static_cast<float>(f) / 900.0f;
        float ax, ay;
        if (t < 0.35f)      { ax = t / 0.35f * 6.0f;  ay = t / 0.35f * 6.0f; }
        else if (t < 0.62f) { float u = (t - 0.35f) / 0.27f; ax = 6.0f + u * 3.0f; ay = 6.0f + u * 7.0f; }
        else                { float u = (t - 0.62f) / 0.38f; ax = 9.0f + u * 10.0f; ay = 13.0f - u * 2.0f; }
        h.move_prey(ax, ay);
        h.tick(dt, tr);
        h.engine.update(dt);
    }

    CHECK(h.log.lines.size() >= 2,
          "the AI wrote decisions to its log (" +
          std::to_string(h.log.lines.size()) + " entries)");

    // Render the panel WITHOUT the log.
    h.engine.render();
    draw_caption(h.engine, build_panel(h, tr, 6.6f, 0, /*include_log=*/false));
    const int without_log = lit_pixels(h.engine);

    // And WITH it.
    h.engine.render();
    draw_caption(h.engine, build_panel(h, tr, 6.6f, 0, /*include_log=*/true));
    const int with_log = lit_pixels(h.engine);

    std::cout << "  [measure] panel without the event log: " << without_log
              << " lit pixels; with it: " << with_log
              << "  (log contributes " << (with_log - without_log) << ")"
              << std::endl;

    CHECK(without_log > 1200,
          "the live sense readout reaches the screen (" +
          std::to_string(without_log) + " pixels)");
    CHECK(with_log > without_log + 400,
          "and the AI's decisions reach it too: the event log is worth " +
          std::to_string(with_log - without_log) +
          " pixels, so it is genuinely being drawn rather than merely "
          "assembled");

    // The readout must be LIVE, not a fixed banner: change what the
    // creature knows and the pixels must change with it.
    const Hunt saved = h.state;
    h.state = Hunt::STALKING;
    h.read.prey_in_cone = true;
    h.read.prey_distance = 7.25f;
    h.engine.render();
    draw_caption(h.engine, build_panel(h, tr, 6.6f, 0, true));
    const int stalking = lit_pixels(h.engine);
    h.state = Hunt::CASTING;
    h.read.prey_in_cone = false;
    h.engine.render();
    draw_caption(h.engine, build_panel(h, tr, 6.6f, 0, true));
    const int casting = lit_pixels(h.engine);
    h.state = saved;

    std::cout << "  [measure] same panel while STALKING: " << stalking
              << " pixels, while CASTING: " << casting << std::endl;
    CHECK(stalking != casting,
          "the panel changes with what the creature knows, so it is a "
          "readout rather than a fixed caption");

}


// A KINEMATIC body still collides. It is only exempt from being MOVED.
//
// This was worth pinning because I claimed the opposite. The solver is
// explicit about it: "Exception: KINEMATIC particles always query BVH"
// (physics_system_v4.cpp:612), and the at-rest skip refuses to apply
// to them (:619). Collision EVENTS are emitted carrying which side was
// kinematic (:929-937), which is how combat reacts to a kinematic
// weapon striking a kinematic body.
//
// What kinematic does not get is positional resolution: inv_mass is 0
// on both sides (:1083-1084), so a kinematic pair generates a contact
// and moves neither body. That is exactly why a creature whose
// position is written walks through rock unless its own controller
// declines to go there.
void test_kinematic_bodies_still_collide() {
    std::cout << "\n  Kinematic collision" << std::endl;
    Hunt3D h(false);

    // Two kinematic bodies, overlapping on purpose.
    const int rock = h.add(ParticleShape::BOX, 0.0f, 0.0f, 1.0f, 3.0f,
                           0.5f, 0.5f, 0.5f);
    const int walker = h.add(ParticleShape::SPHERE, 1.0f, 0.0f, 1.0f, 2.0f,
                             0.9f, 0.3f, 0.2f);
    h.settle();

    const auto& events = h.engine.get_physics_system().get_collision_events();
    bool found = false;
    float penetration = 0.0f;
    for (const auto& e : events) {
        const bool pair = (static_cast<int>(e.particle_a) == rock &&
                           static_cast<int>(e.particle_b) == walker) ||
                          (static_cast<int>(e.particle_a) == walker &&
                           static_cast<int>(e.particle_b) == rock);
        if (pair) { found = true; penetration = e.penetration; }
    }
    std::cout << "  [measure] two overlapping KINEMATIC bodies produced "
              << events.size() << " collision event(s); the pair was "
              << (found ? "reported" : "NOT reported")
              << ", penetration " << penetration << " m" << std::endl;
    CHECK(found,
          "two overlapping KINEMATIC bodies DO generate a collision event, "
          "which is what combat reacts to");

    // And neither is moved by it: that is the part that is exempt.
    float wx = 0, wy = 0;
    {
        auto view = h.engine.get_particle_system().lock_particles_for_read();
        wx = view[walker].x; wy = view[walker].y;
    }
    for (int i = 0; i < 30; ++i) h.engine.update(1.0 / 60.0);
    float wx2 = 0, wy2 = 0;
    {
        auto view = h.engine.get_particle_system().lock_particles_for_read();
        wx2 = view[walker].x; wy2 = view[walker].y;
    }
    std::cout << "  [measure] after 30 frames overlapping, it moved "
              << std::hypot(wx2 - wx, wy2 - wy) << " m" << std::endl;
    CHECK(std::hypot(wx2 - wx, wy2 - wy) < 0.01f,
          "but the solver does not push a kinematic body out, which is "
          "why its own controller must decline to enter geometry");
}

}  // namespace

// ---------------------------------------------------------------- the hunt

int main() {
    g_visual = std::getenv("LOGOSPHERE_VISUAL") != nullptr;
    std::srand(20260802u);
    std::cout << "A predator hunting (senses + pathfinding, over time)"
              << std::endl;

    Hunt3D h(g_visual);
    h.ground();

    // A pillar of rock the prey can break line of sight behind. Placed
    // between the two of them, off to one side, so the prey has to be
    // driven behind it rather than starting hidden.
    h.boulder(6.0f, 10.0f, 7.0f, 2.0f, 5.0f);

    h.predator = h.add(ParticleShape::SPHERE, -14.0f, -6.0f, 1.0f, 1.6f,
                       0.95f, 0.75f, 0.2f);
    h.prey = h.add(ParticleShape::SPHERE, 0.0f, 0.0f, 1.0f, 2.0f,
                   0.85f, 0.25f, 0.25f, OdorType::LIVING_FLESH);
    h.make_fan_pool();
    h.make_panel();
    h.settle();

    h.px = -14.0f; h.py = -6.0f; h.facing = std::atan2(14.0f, 6.0f);
    h.move_prey(0.0f, 0.0f);

    if (g_visual) {
        h.engine.get_particle_system().queue_light(-10.0f, -25.0f, 45.0f,
                                                   26000000.0f, 700.0f,
                                                   1.0f, 0.95f, 0.9f);
        h.engine.get_particle_system().queue_light(20.0f, 20.0f, 40.0f,
                                                   16000000.0f, 700.0f,
                                                   0.85f, 0.9f, 1.0f);
        auto& cam = h.engine.get_camera_system();
        cam.set_pixels_per_unit(17.0f);
        cam.set_position(h.px - 14.0f, h.py - 16.0f, 15.0f);
        cam.look_at(h.px, h.py, 1.0f);
        for (int i = 0; i < 6; ++i) h.engine.update(1.0 / 60.0);
        bring_to_front(h.engine);
    }

    // Before a single frame of the hunt: does the panel actually reach
    // the image handed to the window? If not, nothing below is worth
    // watching, and it says so instead of running for ten minutes.
    {
        Trace probe;
        test_the_panel_reaches_the_presented_image(h, probe);
    }

    Trace tr;
    {
        tr.start_distance = std::hypot(h.px - 0.0f, h.py - 0.0f);
    }

    // The prey walks a path that takes it BEHIND the pillar and out the
    // far side. The predator is not told any of this.
    const int TOTAL = 900;                    // 15 s at 60 Hz
    const float dt = 1.0f / 60.0f;
    int lost_at_frame = -1;

    int laps = 0;
    for (int f = 0; !g_quit; ++f) {
        if (f >= TOTAL) {
            if (!g_visual) break;         // headless: one lap, then assert
            if (f % TOTAL == 0) {
                ++laps;
                h.log.add(h.clock, "LAP     the prey starts its round again");
            }
        }
        // Prey route: east, then north behind the pillar, then east again.
        const int fm = f % TOTAL;
        float t = static_cast<float>(fm) / TOTAL;
        float ax, ay;
        if (t < 0.35f)      { ax = 0.0f + t / 0.35f * 6.0f;  ay = 0.0f + t / 0.35f * 6.0f; }
        else if (t < 0.62f) { float u = (t - 0.35f) / 0.27f; ax = 6.0f + u * 3.0f; ay = 6.0f + u * 7.0f; }
        else                { float u = (t - 0.62f) / 0.38f; ax = 9.0f + u * 10.0f; ay = 13.0f - u * 2.0f; }
        h.move_prey(ax, ay);

        const Hunt before = h.state;
        h.tick(dt, tr);
        if (before == Hunt::STALKING && h.state == Hunt::SEARCHING &&
            lost_at_frame < 0)
            lost_at_frame = f;
        // Only the first lap is measured; later laps are for watching.
        const bool measuring = (f < TOTAL);

        // Occlusion honesty, checked every frame it claims sight.
        //
        // NOT "is the prey's centre visible": the prey is 2 m across,
        // so the cone can legitimately catch an edge of it while a ray
        // to the centre is blocked. Seeing part of something is seeing
        // it. The real invariant is that it never reports prey that is
        // FULLY hidden, so this samples across the body and only
        // objects if every one of those points is blocked.
        if (h.state == Hunt::STALKING) {
            auto view = h.engine.get_particle_system().lock_particles_for_read();
            const BVH* bvh = h.engine.get_particle_system().get_shadow_bvh();
            if (bvh) {
                // The PREY must be excluded as well as the predator.
                // can_see_point is a shadow ray, so the prey's own
                // surface sits between the viewer and the prey's
                // centre and blocks it every single time. Asking "can
                // I see the middle of that animal" is always no.
                // Excluding it asks the question that was meant: is
                // the line clear of everything ELSE.
                std::vector<int> ignore = h.blind_to();
                ignore.push_back(h.prey);
                const float ox[5] = {0.0f, 0.9f, -0.9f, 0.0f,  0.0f};
                const float oy[5] = {0.0f, 0.0f,  0.0f, 0.9f, -0.9f};
                bool any_visible = false;
                for (int k = 0; k < 5 && !any_visible; ++k)
                    if (h.senses.can_see_point(h.px, h.py, 1.0f,
                                               h.prey_x() + ox[k],
                                               h.prey_y() + oy[k], 1.0f,
                                               view.get(), *bvh, ignore))
                        any_visible = true;
                if (!any_visible) tr.saw_through_boulder = true;
            }
        }

        if (g_visual) {
            h.refresh_fan();
            h.engine.update(dt);
            h.engine.render();

            h.refresh_panel(tr, f * dt, laps);
            // Keep both animals in frame: aim between them.
            {
                auto& cam = h.engine.get_camera_system();
                const float mx = (h.px + h.prey_x()) * 0.5f;
                const float my = (h.py + h.prey_y()) * 0.5f;
                cam.set_position(mx - 14.0f, my - 16.0f, 15.0f);
                cam.look_at(mx, my, 1.0f);
            }
            if (const char* sd = std::getenv("LOGOSPHERE_SHOT")) {
                if (f == 90 && !g_quit) {
                    h.engine.get_renderer().wait_for_completion();
                    int ww = 0, hh = 0;
                    std::vector<uint32_t> px(
                        static_cast<size_t>(h.engine.get_render_buffer().width()) *
                        h.engine.get_render_buffer().height());
                    if (h.engine.read_latest_framebuffer(px.data(), ww, hh)) {
                        const PixelBuffer& ui = h.engine.get_ui_overlay_buffer();
                        for (int yy = 0; yy < hh && yy < ui.height(); ++yy)
                            for (int xx = 0; xx < ww && xx < ui.width(); ++xx) {
                                const EnhancedPixel q = ui.get_pixel(xx, yy);
                                if (q.a == 0) continue;
                                px[yy * ww + xx] = (0xFFu << 24) |
                                    (static_cast<uint32_t>(q.r) << 16) |
                                    (static_cast<uint32_t>(q.g) << 8) |
                                     static_cast<uint32_t>(q.b);
                            }
                        std::string path2 = std::string(sd) + "/hunt.ppm";
                        if (FILE* fp = std::fopen(path2.c_str(), "wb")) {
                            std::fprintf(fp, "P6\n%d %d\n255\n", ww, hh);
                            for (int k = 0; k < ww * hh; ++k) {
                                const uint32_t q = px[k];
                                unsigned char rgb[3] = {
                                    static_cast<unsigned char>((q >> 16) & 0xFF),
                                    static_cast<unsigned char>((q >> 8) & 0xFF),
                                    static_cast<unsigned char>(q & 0xFF)};
                                std::fwrite(rgb, 1, 3, fp);
                            }
                            std::fclose(fp);
                            std::cout << "      shot -> " << path2 << std::endl;
                        }
                    }
                }
            }
            h.engine.present();
            if (!pump(h.engine)) break;
        } else {
            h.engine.update(dt);
        }
    }

    const float ended = std::hypot(h.px - h.prey_x(), h.py - h.prey_y());
    std::cout << "\n  [measure] " << tr.frames << " frames. saw prey on "
              << tr.frames_seen << ", searched on " << tr.frames_searching
              << std::endl;
    std::cout << "  [measure] distance: started " << tr.start_distance
              << " m, closest " << tr.closest << " m, ended " << ended
              << " m" << std::endl;
    std::cout << "  [measure] first lost sight at frame " << lost_at_frame
              << std::endl;

    // It has to actually hunt.
    CHECK(tr.ever_saw, "the predator sees the prey at some point");
    CHECK(tr.closest < tr.start_distance - 4.0f,
          "and closes on it (started " + std::to_string(tr.start_distance) +
          " m, closest " + std::to_string(tr.closest) + " m)");

    // And it has to be capable of NOT knowing, which is the whole point.
    CHECK(tr.frames_seen < tr.frames,
          "there are frames where it cannot see the prey (" +
          std::to_string(tr.frames_seen) + " of " +
          std::to_string(tr.frames) + ")");
    CHECK(tr.ever_lost_after_seeing,
          "it loses sight after having had it, rather than knowing "
          "for ever once it has looked");
    CHECK(tr.frames_searching > 0,
          "and it goes looking, using a memory rather than a live feed (" +
          std::to_string(tr.frames_searching) + " frames)");

    // The hard invariant: it never claims to see through rock.
    CHECK(!tr.saw_through_boulder,
          "it never sees the prey through the boulder");

    std::cout << "  [measure] frames standing in blocked ground: "
              << tr.frames_inside_rock << " of " << tr.frames
              << ", deepest intrusion " << tr.deepest_intrusion << " m"
              << std::endl;
    CHECK(tr.frames_inside_rock == 0,
          "and it never walks INTO the rock (" +
          std::to_string(tr.frames_inside_rock) + " frames inside, "
          "deepest " + std::to_string(tr.deepest_intrusion) + " m)");

    test_kinematic_bodies_still_collide();
    test_a_filtered_touch_still_reports_itself();
    test_the_ai_thinking_is_on_the_screen();

    std::cout << "\n" << tests_passed << " passed, " << tests_failed
              << " failed" << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
