#include "arena.h"
#include "logosphere/kg/kg_query.h"

#include "logosphere/core/kg_parse.h"

#include <algorithm>
#include <cmath>

namespace logotron {

namespace {

const char* dir_to_str(Direction d) {
    switch (d) {
        case Direction::NORTH: return "NORTH";
        case Direction::EAST:  return "EAST";
        case Direction::SOUTH: return "SOUTH";
        case Direction::WEST:  return "WEST";
    }
    return "EAST";
}

Direction dir_from_str(const std::string& s) {
    if (s == "NORTH") return Direction::NORTH;
    if (s == "EAST")  return Direction::EAST;
    if (s == "SOUTH") return Direction::SOUTH;
    if (s == "WEST")  return Direction::WEST;
    return Direction::EAST;
}

const char* state_to_str(CycleState s) {
    return s == CycleState::CRASHED ? "CRASHED" : "RIDING";
}

CycleState state_from_str(const std::string& s) {
    return s == "CRASHED" ? CycleState::CRASHED : CycleState::RIDING;
}

bool is_horizontal(Direction d) {
    return d == Direction::EAST || d == Direction::WEST;
}

} // namespace

kg::EntityID spawn_cycle(kg::KGModule& kg,
                         const std::string& entity_type,
                         float x, float y,
                         Direction dir) {
    kg::EntityID e = kg.createEntity(entity_type);
    if (e == kg::INVALID_ENTITY) return e;
    kg.setProperty(e, "x", std::to_string(x));
    kg.setProperty(e, "y", std::to_string(y));
    kg.setProperty(e, "direction", dir_to_str(dir));
    kg.setProperty(e, "cycle_state", state_to_str(CycleState::RIDING));
    kg.setProperty(e, "run_start_x", std::to_string(x));
    kg.setProperty(e, "run_start_y", std::to_string(y));
    // Seed the speed model with the Cycle struct's defaults so the
    // KG and the runtime agree from frame 1. The Weirden / bonus
    // packs override these via setProperty before the first step.
    Cycle defaults;  // base_speed=5, max_speed=8, ramp=1, etc.
    kg.setProperty(e, "base_speed",      std::to_string(defaults.base_speed));
    kg.setProperty(e, "max_speed",       std::to_string(defaults.max_speed));
    kg.setProperty(e, "speed_ramp_rate", std::to_string(defaults.speed_ramp_rate));
    kg.setProperty(e, "current_speed",   std::to_string(defaults.base_speed));
    kg.setProperty(e, "time_since_turn", std::to_string(0.0f));
    return e;
}

Cycle read_cycle(const kg::KGModule& kg, kg::EntityID cycle_entity) {
    Cycle c;
    auto getf = [&](const char* key, float fallback) {
        auto s = kg.getProperty(cycle_entity, key);
        if (s.empty()) return fallback;
        return kg_parse::to_float(s, key, cycle_entity).value_or(fallback);
    };
    c.x = getf("x", 0.0f);
    c.y = getf("y", 0.0f);
    c.run_start_x = getf("run_start_x", c.x);
    c.run_start_y = getf("run_start_y", c.y);
    auto d = kg.getProperty(cycle_entity, "direction");
    auto s = kg.getProperty(cycle_entity, "cycle_state");
    if (!d.empty()) c.direction = dir_from_str(d);
    if (!s.empty()) c.state = state_from_str(s);
    // Speed-model fields. Fallbacks chain: missing base_speed →
    // kCycleSpeed; missing max_speed / ramp / current → derived
    // from base_speed (preserves legacy constant-speed behavior for
    // any cycle written before this schema landed).
    c.base_speed       = getf("base_speed",      kCycleSpeed);
    c.max_speed        = getf("max_speed",       c.base_speed);
    c.speed_ramp_rate  = getf("speed_ramp_rate", 0.0f);
    c.current_speed    = getf("current_speed",   c.base_speed);
    c.time_since_turn  = getf("time_since_turn", 0.0f);
    return c;
}

void write_cycle(kg::KGModule& kg, kg::EntityID cycle_entity, const Cycle& c) {
    kg.setProperty(cycle_entity, "x", std::to_string(c.x));
    kg.setProperty(cycle_entity, "y", std::to_string(c.y));
    kg.setProperty(cycle_entity, "direction", dir_to_str(c.direction));
    kg.setProperty(cycle_entity, "cycle_state", state_to_str(c.state));
    kg.setProperty(cycle_entity, "run_start_x", std::to_string(c.run_start_x));
    kg.setProperty(cycle_entity, "run_start_y", std::to_string(c.run_start_y));
    kg.setProperty(cycle_entity, "base_speed",      std::to_string(c.base_speed));
    kg.setProperty(cycle_entity, "max_speed",       std::to_string(c.max_speed));
    kg.setProperty(cycle_entity, "speed_ramp_rate", std::to_string(c.speed_ramp_rate));
    kg.setProperty(cycle_entity, "current_speed",   std::to_string(c.current_speed));
    kg.setProperty(cycle_entity, "time_since_turn", std::to_string(c.time_since_turn));
}

void step_cycle_in_kg(kg::KGModule& kg, kg::EntityID cycle_entity, float dt) {
    Cycle c = read_cycle(kg, cycle_entity);
    if (c.state != CycleState::RIDING) return;
    step_cycle(c, dt);
    write_cycle(kg, cycle_entity, c);
}

void freeze_run(kg::KGModule& kg, kg::EntityID cycle_entity) {
    freeze_run_at(kg, cycle_entity, /*spawn_time=*/0.0f);
}

void freeze_run_at(kg::KGModule& kg, kg::EntityID cycle_entity,
                   float spawn_time) {
    Cycle c = read_cycle(kg, cycle_entity);
    float dx = c.x - c.run_start_x;
    float dy = c.y - c.run_start_y;
    float len_sq = dx * dx + dy * dy;
    const float kMinLenSq = 0.01f;  // 0.1 m minimum run length
    if (len_sq < kMinLenSq) {
        // No motion since last seal; update run_start to current in
        // case position drifted slightly but don't create an entity.
        c.run_start_x = c.x;
        c.run_start_y = c.y;
        write_cycle(kg, cycle_entity, c);
        return;
    }

    kg::EntityID t = kg.createEntity("TrailSegment");
    if (t != kg::INVALID_ENTITY) {
        kg.setProperty(t, "start_x", std::to_string(c.run_start_x));
        kg.setProperty(t, "start_y", std::to_string(c.run_start_y));
        kg.setProperty(t, "end_x",   std::to_string(c.x));
        kg.setProperty(t, "end_y",   std::to_string(c.y));
        kg.setProperty(t, "direction", dir_to_str(c.direction));
        kg.setProperty(t, "trail_state", "SOLID");
        kg.setProperty(t, "owner_cycle_id", std::to_string(cycle_entity));
        // spawn_time is the game-layer clock at seal time. The
        // trail-lifetime system reads this + the cycle's tail_lifetime
        // property to decide when each segment starts fading and gets
        // deleted. spawn_time=0 means "no lifetime tracking" — trails
        // stay forever, matching the pre-v0.8 behavior.
        if (spawn_time > 0.0f) {
            kg.setProperty(t, "spawn_time", std::to_string(spawn_time));
        }
    }

    c.run_start_x = c.x;
    c.run_start_y = c.y;
    write_cycle(kg, cycle_entity, c);
}

int count_trails_owned_by(const kg::KGModule& kg, kg::EntityID cycle_entity) {
    kg::Query q;
    q.types = {"TrailSegment"};
    q.where = {{"owner_cycle_id", std::to_string(cycle_entity)}};
    return static_cast<int>(kg::count_query(kg, q));
}

namespace {

// Is (px, py) within a perpendicular band of the line from (sx, sy)
// to (ex, ey), and between the endpoints along the run axis? The
// runs are always cardinal (axis-aligned), so we split on
// horizontal-vs-vertical.
bool point_near_run(float px, float py,
                    float sx, float sy, float ex, float ey,
                    bool horizontal, float half_thick) {
    if (horizontal) {
        if (std::fabs(py - sy) > half_thick) return false;
        float lo = std::min(sx, ex) - half_thick;
        float hi = std::max(sx, ex) + half_thick;
        return px >= lo && px <= hi;
    } else {
        if (std::fabs(px - sx) > half_thick) return false;
        float lo = std::min(sy, ey) - half_thick;
        float hi = std::max(sy, ey) + half_thick;
        return py >= lo && py <= hi;
    }
}

bool approx_eq(float a, float b, float eps = 0.05f) {
    return std::fabs(a - b) < eps;
}

} // namespace

bool check_collision(const kg::KGModule& kg, float x, float y,
                     float arena_w, float arena_h,
                     float self_run_endpoint_x,
                     float self_run_endpoint_y) {
    return check_collision(kg, x, y, arena_w, arena_h,
                           self_run_endpoint_x, self_run_endpoint_y,
                           kg::INVALID_ENTITY);
}

bool check_collision(const kg::KGModule& kg, float x, float y,
                     float arena_w, float arena_h,
                     float self_run_endpoint_x,
                     float self_run_endpoint_y,
                     kg::EntityID self_cycle) {
    return check_collision_at(kg, x, y, arena_w, arena_h,
                              self_run_endpoint_x, self_run_endpoint_y,
                              self_cycle, /*now_seconds=*/0.0f);
}

bool check_collision_at(const kg::KGModule& kg, float x, float y,
                        float arena_w, float arena_h,
                        float self_run_endpoint_x,
                        float self_run_endpoint_y,
                        kg::EntityID self_cycle,
                        float now_seconds) {
    return check_collision_detailed(kg, x, y, arena_w, arena_h,
                                    self_run_endpoint_x,
                                    self_run_endpoint_y,
                                    self_cycle, now_seconds).lethal;
}

const char* collision_cause_name(CollisionCause c) {
    switch (c) {
        case CollisionCause::NONE:                return "none";
        case CollisionCause::OUT_OF_BOUNDS:       return "out_of_bounds";
        case CollisionCause::SEALED_TRAIL_SELF:   return "sealed_trail_self";
        case CollisionCause::SEALED_TRAIL_OTHER:  return "sealed_trail_other";
        case CollisionCause::OPPONENT_ACTIVE_RUN: return "opponent_active_run";
    }
    return "?";
}

CollisionResult check_collision_detailed(const kg::KGModule& kg,
                                         float x, float y,
                                         float arena_w, float arena_h,
                                         float self_run_endpoint_x,
                                         float self_run_endpoint_y,
                                         kg::EntityID self_cycle,
                                         float now_seconds) {
    CollisionResult out;
    out.hit_x = x;
    out.hit_y = y;

    // Arena boundary.
    if (x < 0.0f || y < 0.0f || x >= arena_w || y >= arena_h) {
        out.lethal = true;
        out.cause  = CollisionCause::OUT_OF_BOUNDS;
        return out;
    }

    const float half_thick = kWallThickness * 0.5f;
    bool have_self = !std::isnan(self_run_endpoint_x);

    // Lethality follows state: the trail of a CRASHED cycle stops
    // being a wall the instant its owner dies. The game-layer fade
    // over kTrailFadeDuration is purely visual polish on top; without
    // this the player would slam into an invisible, already-dead
    // opponent's trail. Cache once per call since many trails share
    // owners.
    auto cycle_still_lethal = [&](const std::string& owner_id_str) -> bool {
        if (owner_id_str.empty()) return true;  // no owner recorded = treat as static wall
        kg::EntityID oid;
        try { oid = static_cast<kg::EntityID>(std::stoull(owner_id_str)); }
        catch (...) { return true; }  // bad parse: fail-safe to lethal
        // KG property name is "cycle_state" (matches schema slot +
        // read_cycle/write_cycle). Reading "state" by mistake here
        // ALWAYS returned empty → trails always treated as lethal,
        // including AI trails after the AI crashed. That was the
        // "crashed into nothing" bug — the visual fade hid the trail
        // but collision still hit it.
        auto state_s = kg.getProperty(oid, "cycle_state");
        // Property missing or still RIDING → lethal.
        return state_s.empty() || state_s == "RIDING";
    };

    // Age-based lifetime: if the caller supplied `now_seconds` > 0
    // and the trail carries a spawn_time, check that age is within
    // kTrailLifetime (seconds). Anything older is "faded" and no
    // longer lethal — the visual fade takes care of the rest.
    // now_seconds=0 disables this check (backwards-compatible).
    auto trail_still_lethal_by_age = [&](kg::EntityID t) -> bool {
        if (now_seconds <= 0.0f) return true;
        auto spawn_s = kg.getProperty(t, "spawn_time");
        if (spawn_s.empty()) return true;
        float spawn = 0.0f;
        try { spawn = std::stof(spawn_s); } catch (...) { return true; }
        if (spawn <= 0.0f) return true;
        return (now_seconds - spawn) < kTrailLifetime;
    };

    // Sealed trails: all TrailSegment entities.
    auto trails = kg.findByType("TrailSegment");
    for (auto t : trails) {
        auto sx_s = kg.getProperty(t, "start_x");
        auto sy_s = kg.getProperty(t, "start_y");
        auto ex_s = kg.getProperty(t, "end_x");
        auto ey_s = kg.getProperty(t, "end_y");
        auto d_s  = kg.getProperty(t, "direction");
        if (sx_s.empty() || ex_s.empty()) continue;

        // Owner dead → segment is harmless. Cheap enough to do per
        // trail; if trail counts grow we can cache dead-owner IDs in
        // a local set at the top of this function.
        auto owner_id_s = kg.getProperty(t, "owner_cycle_id");
        if (!cycle_still_lethal(owner_id_s)) continue;
        // Trail too old → harmless. Keeps the arena breathable.
        if (!trail_still_lethal_by_age(t)) continue;

        float sx = kg_parse::to_float(sx_s, "start_x", t).value_or(0.0f);
        float sy = kg_parse::to_float(sy_s, "start_y", t).value_or(0.0f);
        float ex = kg_parse::to_float(ex_s, "end_x",   t).value_or(0.0f);
        float ey = kg_parse::to_float(ey_s, "end_y",   t).value_or(0.0f);
        auto dir = dir_from_str(d_s);

        // Skip the trail whose endpoint equals the cycle's current
        // run start — it's the segment the cycle is riding out of.
        if (have_self &&
            approx_eq(ex, self_run_endpoint_x) &&
            approx_eq(ey, self_run_endpoint_y)) {
            continue;
        }

        if (point_near_run(x, y, sx, sy, ex, ey,
                           is_horizontal(dir), half_thick)) {
            out.lethal = true;
            out.hit_entity = t;
            // Distinguish self vs other by comparing owner to the
            // caller's entity id. Only meaningful if caller passed a
            // real self_cycle; when they didn't, default to OTHER
            // (conservative — the label-independent outcome is the
            // same).
            kg::EntityID owner = kg::INVALID_ENTITY;
            if (!owner_id_s.empty()) {
                try { owner = static_cast<kg::EntityID>(std::stoull(owner_id_s)); }
                catch (...) {}
            }
            out.cause = (self_cycle != kg::INVALID_ENTITY && owner == self_cycle)
                ? CollisionCause::SEALED_TRAIL_SELF
                : CollisionCause::SEALED_TRAIL_OTHER;
            // Age stamp for telemetry.
            auto spawn_s = kg.getProperty(t, "spawn_time");
            if (!spawn_s.empty() && now_seconds > 0.0f) {
                try {
                    float spawn = std::stof(spawn_s);
                    if (spawn > 0.0f) out.hit_age = now_seconds - spawn;
                } catch (...) {}
            }
            return out;
        }
    }

    // Active (unsealed) runs from OTHER cycles. Each cycle is drawing a
    // line between its run_start and its current position; that line is
    // not yet a TrailSegment. Without this check a player can drive
    // straight through the opponent's actively-drawing trail until the
    // opponent turns — the whole point of Tron is catching people on
    // their live trail, so this was the actual bug. `Cycle` entities
    // cover both kinds the game uses (PlayerCycle, AICycle) — use the
    // Cycle supertype and iterate descendants if needed.
    auto cycles = kg.findByType("Cycle");
    auto append_by_type = [&](const char* type_name) {
        auto more = kg.findByType(type_name);
        cycles.insert(cycles.end(), more.begin(), more.end());
    };
    append_by_type("PlayerCycle");
    append_by_type("AICycle");
    for (auto cyc : cycles) {
        if (cyc == self_cycle) continue;  // don't collide with own active run
        // Same property-name fix as the cycle_still_lethal lambda
        // above — "cycle_state" not "state". Without this, a CRASHED
        // opponent's frozen position still acted as a live active-
        // run line that the player could hit.
        auto state_s = kg.getProperty(cyc, "cycle_state");
        // Only RIDING cycles have a live trail worth colliding with.
        if (!state_s.empty() && state_s != "RIDING") continue;
        auto x_s  = kg.getProperty(cyc, "x");
        auto y_s  = kg.getProperty(cyc, "y");
        auto rsx  = kg.getProperty(cyc, "run_start_x");
        auto rsy  = kg.getProperty(cyc, "run_start_y");
        auto d_s  = kg.getProperty(cyc, "direction");
        if (x_s.empty() || rsx.empty()) continue;
        float cur_x = kg_parse::to_float(x_s,  "x",           cyc).value_or(0.0f);
        float cur_y = kg_parse::to_float(y_s,  "y",           cyc).value_or(0.0f);
        float s_x   = kg_parse::to_float(rsx,  "run_start_x", cyc).value_or(0.0f);
        float s_y   = kg_parse::to_float(rsy,  "run_start_y", cyc).value_or(0.0f);
        auto  dir   = dir_from_str(d_s);
        if (point_near_run(x, y, s_x, s_y, cur_x, cur_y,
                           is_horizontal(dir), half_thick)) {
            out.lethal = true;
            out.cause  = CollisionCause::OPPONENT_ACTIVE_RUN;
            out.hit_entity = cyc;
            return out;
        }
    }
    return out;  // lethal=false, cause=NONE
}

void step_cycle_in_kg_with_collision(kg::KGModule& kg,
                                     kg::EntityID cycle_entity,
                                     float arena_w, float arena_h,
                                     float dt) {
    step_cycle_in_kg_with_collision_at(kg, cycle_entity, arena_w,
                                       arena_h, dt, /*now=*/0.0f);
}

void step_cycle_in_kg_with_collision_at(kg::KGModule& kg,
                                        kg::EntityID cycle_entity,
                                        float arena_w, float arena_h,
                                        float dt,
                                        float now_seconds) {
    Cycle c = read_cycle(kg, cycle_entity);
    if (c.state != CycleState::RIDING) return;

    // Predicted speed at the END of this tick — same ramp formula as
    // step_cycle. Computing it here means the collision check uses
    // the same distance as the actual move, so we never under-
    // anticipate a crash because we used last frame's slower speed.
    float predicted_speed = c.base_speed
        + c.speed_ramp_rate * (c.time_since_turn + dt);
    if (predicted_speed > c.max_speed) predicted_speed = c.max_speed;
    if (predicted_speed < c.base_speed) predicted_speed = c.base_speed;

    // Substep so high-speed cycles can't tunnel through trails.
    // A single point-based collision check at the END of the step
    // misses any wall the cycle hopped OVER mid-step. The bound:
    // each sub-step must move less than the trail's collision-band
    // half-width (kWallThickness/2 ≈ 7.5 cm), otherwise the next
    // position can land past the band even though the path crossed
    // it. With max_speed ≈ 8 m/s and dt = 16 ms (60 FPS), one tick
    // moves 13 cm — already enough to clip a thin trail. Substep
    // count picks the conservative side.
    const float kSafeStep = kWallThickness * 0.5f;
    const float move_dist = predicted_speed * dt;
    int substeps = 1;
    if (move_dist > kSafeStep) {
        substeps = static_cast<int>(std::ceil(move_dist / kSafeStep));
    }
    const float sub_dt = dt / static_cast<float>(substeps);

    auto delta = delta_for(c.direction);
    const float dx_per_sub = static_cast<float>(delta.dx) * predicted_speed * sub_dt;
    const float dy_per_sub = static_cast<float>(delta.dy) * predicted_speed * sub_dt;

    for (int s = 0; s < substeps; ++s) {
        float next_x = c.x + dx_per_sub;
        float next_y = c.y + dy_per_sub;

        auto cr = check_collision_detailed(kg, next_x, next_y, arena_w, arena_h,
                                           c.run_start_x, c.run_start_y, cycle_entity,
                                           now_seconds);
        if (cr.lethal) {
            c.state = CycleState::CRASHED;
            write_cycle(kg, cycle_entity, c);
            // Stamp crash metadata on the cycle entity for telemetry.
            // Properties intentionally human-readable: main.cpp reads
            // them back without a codec, review script renders them
            // as "killed_by=sealed_trail_self @ (10.7, 23.6) age=12.3s".
            kg.setProperty(cycle_entity, "crash_cause",
                           collision_cause_name(cr.cause));
            kg.setProperty(cycle_entity, "crash_x", std::to_string(cr.hit_x));
            kg.setProperty(cycle_entity, "crash_y", std::to_string(cr.hit_y));
            if (cr.hit_entity != kg::INVALID_ENTITY) {
                kg.setProperty(cycle_entity, "crash_hit_entity",
                               std::to_string(cr.hit_entity));
            }
            if (cr.hit_age > 0.0f) {
                kg.setProperty(cycle_entity, "crash_hit_age",
                               std::to_string(cr.hit_age));
            }
            return;
        }

        c.x = next_x;
        c.y = next_y;
    }

    // No crash across any sub-step — commit the position + speed
    // ramp accumulator with one final write. step_cycle handles the
    // ramp + clamps internally, so we call it once with the full dt
    // (it just integrates the speed clock, position is already
    // advanced above).
    c.time_since_turn += dt;
    c.current_speed = predicted_speed;
    write_cycle(kg, cycle_entity, c);
}

} // namespace logotron
