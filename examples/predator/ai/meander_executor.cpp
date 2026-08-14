#include "meander_executor.h"

#include <cmath>

namespace predator {

float MeanderExecutor::frand() {
    // xorshift32: cheap, seedable, good enough for wandering. Not
    // std::rand, so this creature's meander does not perturb (or get
    // perturbed by) anything else's use of the global stream.
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    return static_cast<float>(rng_state_ & 0xFFFFFF) / 16777216.0f;
}

void MeanderExecutor::pick_waypoint(const ExecutionContext& ctx) {
    // QUARTERING, not a random walk. A random walk revisits the same
    // ground endlessly (measured: 157 simulated seconds to stumble
    // within a 17 m odor radius on a 40 m arena). A searching animal
    // works compass sectors in turn, each leg jittered but committed
    // to a direction it has not just come from. Same statelessness of
    // purpose — it still knows NOTHING about the goal — but the ground
    // gets covered.
    for (int attempt = 0; attempt < 8; ++attempt) {
        sector_ = (sector_ + ((attempt == 0) ? 1 : 3)) % 8;
        const float a = sector_ * 0.7853982f + (frand() - 0.5f) * 0.6f;
        const float r = meander_radius * (0.6f + 0.4f * frand());
        const float cx = ctx.pos_x + std::sin(a) * r;
        const float cy = ctx.pos_y + std::cos(a) * r;
        if (ctx.nav_grid && !ctx.nav_grid->is_walkable(cx, cy)) continue;
        wx_ = cx; wy_ = cy;
        has_waypoint_ = true;
        if (ctx.pathfinder && ctx.nav_grid && ctx.current_path) {
            *ctx.current_path = ctx.pathfinder->find_path(
                *ctx.nav_grid, pathfinding::Point(ctx.pos_x, ctx.pos_y),
                pathfinding::Point(wx_, wy_));
        }
        return;
    }
    has_waypoint_ = false;   // wedged in geometry; stand still this leg
}

void MeanderExecutor::on_start(ExecutionContext& ctx) {
    pick_waypoint(ctx);
}

ExecutionResult MeanderExecutor::execute(const ExecutionContext& ctx) {
    MovementIntent intent;
    if (!has_waypoint_) pick_waypoint(ctx);
    if (!has_waypoint_) return ExecutionResult::in_progress(intent);

    const float dx = wx_ - ctx.pos_x, dy = wy_ - ctx.pos_y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.8f) {
        pick_waypoint(ctx);      // arrived: next leg
        return ExecutionResult::in_progress(intent);
    }

    // Walk the path if there is one, else straight at the waypoint.
    float gx = wx_, gy = wy_;
    if (ctx.current_path && !ctx.current_path->empty()) {
        const auto wp = ctx.current_path->current();
        const float wdx = wp.x - ctx.pos_x, wdy = wp.y - ctx.pos_y;
        if (std::sqrt(wdx * wdx + wdy * wdy) < 0.7f && ctx.current_path->size() > 1)
            ctx.current_path->advance();
        const auto wp2 = ctx.current_path->current();
        gx = wp2.x; gy = wp2.y;
    }
    intent.is_moving = true;
    intent.forward_velocity = ctx.walk_speed;   // a meander AMBLES
    intent.body_facing = std::atan2(gx - ctx.pos_x, gy - ctx.pos_y);
    intent.wants_body_turn = true;
    // Never completes: only a better idea (the brain replanning on a
    // scent or a sighting) ends a meander.
    return ExecutionResult::in_progress(intent);
}

} // namespace predator
