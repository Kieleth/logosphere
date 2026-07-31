#include "game_time.h"
#include <cmath>
#include <iostream>

// Static member initialization
double GameTime::current_time_ = 0.0;
double GameTime::time_scale_ = 1.0;
timeline_id GameTime::current_timeline_ = 0;
timeline_id GameTime::next_timeline_id_ = 1;  // 0 is reserved for root timeline
std::vector<GameTime::TimelineInfo> GameTime::timelines_;

// === Time Queries ===

double GameTime::get_current_time() {
    return current_time_;
}

timeline_id GameTime::get_current_timeline() {
    return current_timeline_;
}

double GameTime::get_time_scale() {
    return time_scale_;
}

// === Time Manipulation ===

void GameTime::advance(double delta_seconds) {
    if (time_scale_ <= 0.0) {
        return;  // Time is paused
    }

    current_time_ += delta_seconds * time_scale_;
}

void GameTime::set_time(double target_time) {
    // Check for backwards time travel (potential timeline branch)
    if (target_time < current_time_) {
        std::cout << "[GameTime] Backwards time travel detected: "
                  << current_time_ << " → " << target_time
                  << " (Timeline branching not yet implemented)" << std::endl;
        // TODO[PHASE-2]: Implement timeline branching here
        // For now, just allow backwards travel
    }

    current_time_ = target_time;
}

void GameTime::jump_to(double target_time) {
    set_time(target_time);
}

void GameTime::set_time_scale(double scale) {
    time_scale_ = scale;
}

void GameTime::pause() {
    time_scale_ = 0.0;
}

void GameTime::resume() {
    time_scale_ = 1.0;
}

// === Timeline Management ===

timeline_id GameTime::create_timeline_branch(timeline_id parent_timeline,
                                              double branch_time,
                                              const std::string& reason) {
    timeline_id new_id = next_timeline_id_++;

    TimelineInfo info;
    info.id = new_id;
    info.parent_id = parent_timeline;
    info.branch_time = branch_time;
    info.branch_reason = reason;

    timelines_.push_back(info);

    std::cout << "[GameTime] Created timeline branch: " << new_id
              << " (parent: " << parent_timeline
              << ", branch_time: " << branch_time
              << ", reason: " << reason << ")" << std::endl;

    return new_id;
}

void GameTime::set_timeline(timeline_id timeline) {
    current_timeline_ = timeline;
    std::cout << "[GameTime] Switched to timeline: " << timeline << std::endl;
}

timeline_id GameTime::get_parent_timeline(timeline_id timeline) {
    for (const auto& info : timelines_) {
        if (info.id == timeline) {
            return info.parent_id;
        }
    }
    return 0;  // Root timeline has no parent
}

double GameTime::get_branch_time(timeline_id timeline) {
    for (const auto& info : timelines_) {
        if (info.id == timeline) {
            return info.branch_time;
        }
    }
    return 0.0;  // Root timeline branched at t=0
}

std::vector<timeline_id> GameTime::get_all_timelines() {
    std::vector<timeline_id> ids;
    ids.push_back(0);  // Root timeline
    for (const auto& info : timelines_) {
        ids.push_back(info.id);
    }
    return ids;
}

// === Calendar Functions ===

int GameTime::get_day(double time) {
    return static_cast<int>(std::floor(time / SECONDS_PER_DAY));
}

int GameTime::get_year(double time) {
    return static_cast<int>(std::floor(time / SECONDS_PER_YEAR));
}

int GameTime::get_season(double time) {
    double year_fraction = std::fmod(time, SECONDS_PER_YEAR) / SECONDS_PER_YEAR;
    int season = static_cast<int>(year_fraction * 4.0);
    return season % 4;  // 0=spring, 1=summer, 2=autumn, 3=winter
}

double GameTime::get_day_fraction(double time) {
    return std::fmod(time, SECONDS_PER_DAY) / SECONDS_PER_DAY;
}

// === Initialization ===

void GameTime::initialize(double initial_time) {
    current_time_ = initial_time;
    time_scale_ = 1.0;
    current_timeline_ = 0;  // Root timeline
    next_timeline_id_ = 1;
    timelines_.clear();

    std::cout << "[GameTime] Initialized at t=" << initial_time << std::endl;
}

void GameTime::reset() {
    initialize(0.0);
}
