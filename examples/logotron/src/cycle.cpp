#include "cycle.h"

#include <cmath>

namespace logotron {

GridDelta delta_for(Direction d) {
    // Camera sits on the -y side of the arena looking toward +y, so
    // world +y is the "far" / "up" direction on screen. NORTH points
    // to screen-up, meaning NORTH = +y. (Typical screen graphics has
    // +y = down, but here the camera framing is what matters.)
    switch (d) {
        case Direction::NORTH: return {0, 1};
        case Direction::EAST:  return {1, 0};
        case Direction::SOUTH: return {0, -1};
        case Direction::WEST:  return {-1, 0};
    }
    return {0, 0};  // unreachable
}

void step_cycle(Cycle& c, float dt) {
    if (c.state != CycleState::RIDING) return;
    if (dt <= 0.0f) return;

    // Recency-of-turn ramp (GAME_DESIGN.md §18). Linear climb from
    // base_speed toward max_speed at speed_ramp_rate per second.
    // Setting max_speed == base_speed disables the ramp (legacy
    // constant-speed behavior, useful for tests).
    c.time_since_turn += dt;
    float ramped = c.base_speed + c.speed_ramp_rate * c.time_since_turn;
    if (ramped > c.max_speed) ramped = c.max_speed;
    if (ramped < c.base_speed) ramped = c.base_speed;
    c.current_speed = ramped;

    auto d = delta_for(c.direction);
    c.x += static_cast<float>(d.dx) * c.current_speed * dt;
    c.y += static_cast<float>(d.dy) * c.current_speed * dt;
}

void turn(Cycle& c, Direction next_dir) {
    if (next_dir == c.direction) return;  // no-op, no reset
    c.direction = next_dir;
    c.current_speed = c.base_speed;
    c.time_since_turn = 0.0f;
}

bool is_legal_turn(Direction current, Direction next) {
    if (current == next) return true;
    switch (current) {
        case Direction::NORTH: return next != Direction::SOUTH;
        case Direction::SOUTH: return next != Direction::NORTH;
        case Direction::EAST:  return next != Direction::WEST;
        case Direction::WEST:  return next != Direction::EAST;
    }
    return true;
}

Direction turn_left(Direction d) {
    switch (d) {
        case Direction::NORTH: return Direction::WEST;
        case Direction::WEST:  return Direction::SOUTH;
        case Direction::SOUTH: return Direction::EAST;
        case Direction::EAST:  return Direction::NORTH;
    }
    return d;
}

Direction turn_right(Direction d) {
    switch (d) {
        case Direction::NORTH: return Direction::EAST;
        case Direction::EAST:  return Direction::SOUTH;
        case Direction::SOUTH: return Direction::WEST;
        case Direction::WEST:  return Direction::NORTH;
    }
    return d;
}

} // namespace logotron
