#include "ai/perception.h"

#include "occluders.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

#include "arena.h"
#include "cycle.h"
#include "logosphere/core/kg_parse.h"

namespace logotron::ai {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Angle from (sx, sy) to (tx, ty) in engine convention: yaw=0 points
// +Y, +π/2 points +X. std::atan2(dx, dy) returns exactly that.
float angle_to(float sx, float sy, float tx, float ty) {
    return std::atan2(tx - sx, ty - sy);
}

// Axis-aligned segment-segment intersection, inflated perpendicularly
// by `half_thick` so "thin walls" still occlude a sight ray that
// passes near but not through them. Returns true if the ray (sx,sy)
// → (tx,ty) crosses the axis-aligned trail segment
// (ax,ay)-(bx,by) within a `half_thick` band.
//
// The trail is always axis-aligned (cycle moves cardinally), so we
// only need H vs V handling — cheap and exact.
bool ray_hits_trail(float sx, float sy, float tx, float ty,
                    float ax, float ay, float bx, float by,
                    float half_thick) {
    bool h = std::fabs(ay - by) < 0.01f;  // horizontal trail
    if (h) {
        float trail_y  = ay;
        float trail_lo = std::min(ax, bx) - half_thick;
        float trail_hi = std::max(ax, bx) + half_thick;
        // Parameterize ray as (sx,sy) + u*(tx-sx, ty-sy), u∈[0,1].
        // Find u where y crosses trail_y (if it does at all).
        float dy = ty - sy;
        if (std::fabs(dy) < 1e-6f) return false;   // parallel to trail
        float u = (trail_y - sy) / dy;
        if (u < 0.0f || u > 1.0f) return false;
        float cross_x = sx + u * (tx - sx);
        return cross_x >= trail_lo && cross_x <= trail_hi;
    } else {
        float trail_x  = ax;
        float trail_lo = std::min(ay, by) - half_thick;
        float trail_hi = std::max(ay, by) + half_thick;
        float dx = tx - sx;
        if (std::fabs(dx) < 1e-6f) return false;
        float u = (trail_x - sx) / dx;
        if (u < 0.0f || u > 1.0f) return false;
        float cross_y = sy + u * (ty - sy);
        return cross_y >= trail_lo && cross_y <= trail_hi;
    }
}

// Is the ray (sx,sy)→(tx,ty) occluded by any wall the perception
// layer should care about this frame?
//
// Source-of-truth for the occluder list is the shared
// logotron::build_active_occluders helper (occluders.h) — the
// SAME list that drives the visual cone's LOS mask. That keeps
// the AI and the player-side renderer agreeing on which trails
// block sight (CRASHED owners excluded, expired sealed trails
// excluded, caller's own active run excluded).
//
// The AI passes now_seconds=0 so the age gate is disabled —
// it doesn't track game-clock time today. Adding it is one more
// plumbing step on the caller side; trails older than
// kTrailLifetime become non-lethal anyway, so the AI being a bit
// over-cautious about expired walls is harmless.
bool ray_occluded(const kg::KGModule& kg,
                  kg::EntityID self_cycle,
                  float sx, float sy, float tx, float ty) {
    auto occluders = logotron::build_active_occluders(kg, /*now=*/0.0f, self_cycle);
    for (const auto& s : occluders) {
        if (ray_hits_trail(sx, sy, tx, ty,
                           s.ax, s.ay, s.bx, s.by, s.half_thick)) {
            return true;
        }
    }
    return false;
}

// Distance from (x, y) along cardinal direction `dir` to the nearest
// lethal hit (wall, live opponent trail, sealed trail, arena edge).
// Uses a fine-grained stride to approximate; for cardinal lookahead
// this is good enough and much simpler than ray-segment per trail.
float lethal_distance_along(const kg::KGModule& kg,
                            kg::EntityID self_cycle,
                            float x, float y,
                            logotron::Direction dir,
                            float arena_w, float arena_h,
                            float max_range) {
    // Pass self_cycle so the caller's own live run is excluded — the
    // rider isn't about to crash into their own wall starting from
    // their nose.
    auto delta = logotron::delta_for(dir);
    // Step in tiny increments; stop at the first lethal hit or at
    // max_range. 0.25 m steps = 40 samples for a 10 m look-ahead.
    constexpr float kStep = 0.25f;
    float traveled = 0.0f;
    while (traveled < max_range) {
        traveled += kStep;
        float nx = x + static_cast<float>(delta.dx) * traveled;
        float ny = y + static_cast<float>(delta.dy) * traveled;
        // logotron::check_collision treats OOB and all lethal
        // walls/trails the same way — exactly what we want.
        if (logotron::check_collision(kg, nx, ny, arena_w, arena_h,
                                      std::numeric_limits<float>::quiet_NaN(),
                                      std::numeric_limits<float>::quiet_NaN(),
                                      self_cycle)) {
            return traveled;
        }
    }
    return PerceivedWorld::INFINITY_F;
}

} // anonymous namespace

DirIndex to_dir_index(logotron::Direction d) {
    switch (d) {
        case logotron::Direction::NORTH: return DirIndex::NORTH;
        case logotron::Direction::EAST:  return DirIndex::EAST;
        case logotron::Direction::SOUTH: return DirIndex::SOUTH;
        case logotron::Direction::WEST:  return DirIndex::WEST;
    }
    return DirIndex::NORTH;
}

logotron::Direction from_dir_index(DirIndex d) {
    switch (d) {
        case DirIndex::NORTH: return logotron::Direction::NORTH;
        case DirIndex::EAST:  return logotron::Direction::EAST;
        case DirIndex::SOUTH: return logotron::Direction::SOUTH;
        case DirIndex::WEST:  return logotron::Direction::WEST;
        case DirIndex::COUNT: return logotron::Direction::NORTH;
    }
    return logotron::Direction::NORTH;
}

PerceivedWorld build_perceived_world(
    const kg::KGModule& kg,
    kg::EntityID self_cycle,
    const HeadState& head,
    float cone_fov_rad,
    float cone_range,
    float arena_w,
    float arena_h,
    const std::vector<const char*>& rival_types)
{
    PerceivedWorld pw;
    pw.head = head;
    pw.self = logotron::read_cycle(kg, self_cycle);

    const float half_fov = cone_fov_rad * 0.5f;
    const float sx = pw.self.x;
    const float sy = pw.self.y;

    // Proprioceptive lookahead in each cardinal direction.
    static constexpr logotron::Direction dirs[4] = {
        logotron::Direction::NORTH, logotron::Direction::EAST,
        logotron::Direction::SOUTH, logotron::Direction::WEST
    };
    for (int i = 0; i < 4; ++i) {
        pw.lethal_distance[i] = lethal_distance_along(
            kg, self_cycle, sx, sy, dirs[i], arena_w, arena_h, 20.0f);
    }

    // Arena edges. With origin at (0,0) and size (arena_w, arena_h):
    // NORTH distance = arena_h - y, EAST = arena_w - x, SOUTH = y,
    // WEST = x. Clamp nonnegative.
    pw.arena_edge_distance[0] = std::max(0.0f, arena_h - sy);  // N
    pw.arena_edge_distance[1] = std::max(0.0f, arena_w - sx);  // E
    pw.arena_edge_distance[2] = std::max(0.0f, sy);            // S
    pw.arena_edge_distance[3] = std::max(0.0f, sx);            // W

    // Opponent rider — find the OTHER RIDING cycle and test cone + LOS.
    auto seen_opponent = [&]() -> std::optional<SeenPoint> {
        auto try_cyc = [&](kg::EntityID c) -> std::optional<SeenPoint> {
            if (c == self_cycle) return std::nullopt;
            auto state_s = kg.getProperty(c, "state");
            if (state_s == "CRASHED") return std::nullopt;
            auto c_x_s = kg.getProperty(c, "x");
            auto c_y_s = kg.getProperty(c, "y");
            if (c_x_s.empty() || c_y_s.empty()) return std::nullopt;
            float ox = kg_parse::to_float(c_x_s, "x", c).value_or(0.0f);
            float oy = kg_parse::to_float(c_y_s, "y", c).value_or(0.0f);
            float dist = std::sqrt((ox-sx)*(ox-sx) + (oy-sy)*(oy-sy));
            if (dist > cone_range) return std::nullopt;
            float ang = angle_to(sx, sy, ox, oy);
            float off = shortest_arc_delta(head.current_yaw, ang);
            if (std::fabs(off) > half_fov) return std::nullopt;
            if (ray_occluded(kg, self_cycle, sx, sy, ox, oy)) return std::nullopt;
            SeenPoint p;
            p.x = ox; p.y = oy; p.distance = dist; p.angle_offset = off;
            return p;
        };
        // NEAREST visible rider wins (with a roster of Programs the
        // first-found pick targeted arbitrarily — arcade Tron rule:
        // everyone hunts whoever is closest).
        std::optional<SeenPoint> best;
        for (const char* type : rival_types) {
            for (auto c : kg.findByType(type)) {
                auto s = try_cyc(c);
                if (s && (!best || s->distance < best->distance)) best = s;
            }
        }
        return best;
    };
    pw.opponent_rider = seen_opponent();

    // Visible trail endpoints. Useful for tactics that want to
    // pilot around a specific wall. v1 populates; tactics optional.
    for (auto t : kg.findByType("TrailSegment")) {
        auto ex_s = kg.getProperty(t, "end_x");
        auto ey_s = kg.getProperty(t, "end_y");
        if (ex_s.empty()) continue;
        float ex = kg_parse::to_float(ex_s, "end_x", t).value_or(0.0f);
        float ey = kg_parse::to_float(ey_s, "end_y", t).value_or(0.0f);
        float dist = std::sqrt((ex-sx)*(ex-sx) + (ey-sy)*(ey-sy));
        if (dist > cone_range) continue;
        float ang = angle_to(sx, sy, ex, ey);
        float off = shortest_arc_delta(head.current_yaw, ang);
        if (std::fabs(off) > half_fov) continue;
        if (ray_occluded(kg, self_cycle, sx, sy, ex, ey)) continue;
        SeenPoint p;
        p.x = ex; p.y = ey; p.distance = dist; p.angle_offset = off;
        pw.visible_trail_endpoints.push_back(p);
    }

    return pw;
}

} // namespace logotron::ai
