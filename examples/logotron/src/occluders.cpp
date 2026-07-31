#include "occluders.h"

#include "cycle.h"
#include "logosphere/core/kg_parse.h"

#include <cmath>
#include <string>

namespace logotron {

namespace {

bool owner_still_blocks_sight(const kg::KGModule& kg,
                              const std::string& owner_id) {
    if (owner_id.empty()) return true;          // anonymous: keep
    kg::EntityID oid;
    try { oid = static_cast<kg::EntityID>(std::stoull(owner_id)); }
    catch (...) { return true; }
    auto state = kg.getProperty(oid, "cycle_state");
    return state.empty() || state == "RIDING";
}

bool trail_still_blocks_by_age(const kg::KGModule& kg,
                               kg::EntityID trail,
                               float now_seconds) {
    if (now_seconds <= 0.0f) return true;
    auto spawn_s = kg.getProperty(trail, "spawn_time");
    if (spawn_s.empty()) return true;
    float spawn = 0.0f;
    try { spawn = std::stof(spawn_s); } catch (...) { return true; }
    if (spawn <= 0.0f) return true;
    return (now_seconds - spawn) < kTrailLifetime;
}

} // namespace

std::vector<logosphere::rendering::OccluderSegment>
build_active_occluders(const kg::KGModule& kg,
                       float now_seconds,
                       kg::EntityID self_cycle)
{
    using logosphere::rendering::OccluderSegment;
    const float half_thick = logotron::kWallThickness * 0.5f;

    std::vector<OccluderSegment> out;
    out.reserve(64);

    // Sealed TrailSegments (own + opponents). Filtered through
    // both gates so the visual cone, the AI's perception layer,
    // and the gameplay collision all agree on which walls exist
    // this frame.
    for (auto t : kg.findByType("TrailSegment")) {
        auto ax_s = kg.getProperty(t, "start_x");
        auto bx_s = kg.getProperty(t, "end_x");
        if (ax_s.empty() || bx_s.empty()) continue;
        if (!owner_still_blocks_sight(kg, kg.getProperty(t, "owner_cycle_id"))) continue;
        if (!trail_still_blocks_by_age(kg, t, now_seconds)) continue;
        OccluderSegment s{};
        s.ax = kg_parse::to_float(ax_s,                        "start_x", t).value_or(0.0f);
        s.ay = kg_parse::to_float(kg.getProperty(t, "start_y"),"start_y", t).value_or(0.0f);
        s.bx = kg_parse::to_float(bx_s,                        "end_x",   t).value_or(0.0f);
        s.by = kg_parse::to_float(kg.getProperty(t, "end_y"),  "end_y",   t).value_or(0.0f);
        s.half_thick = half_thick;
        out.push_back(s);
    }

    // Other cycles' active (un-frozen) runs. Skip self_cycle (own
    // bike never blocks own sight), CRASHED cycles (their stale
    // run line is gameplay-dead), and zero-length runs.
    auto add_active = [&](kg::EntityID cyc) {
        if (cyc == self_cycle) return;
        auto state = kg.getProperty(cyc, "cycle_state");
        if (!state.empty() && state != "RIDING") return;
        auto rsx = kg.getProperty(cyc, "run_start_x");
        auto rsy = kg.getProperty(cyc, "run_start_y");
        auto cx_s = kg.getProperty(cyc, "x");
        auto cy_s = kg.getProperty(cyc, "y");
        if (rsx.empty() || cx_s.empty()) return;
        OccluderSegment s{};
        s.ax = kg_parse::to_float(rsx,  "run_start_x", cyc).value_or(0.0f);
        s.ay = kg_parse::to_float(rsy,  "run_start_y", cyc).value_or(0.0f);
        s.bx = kg_parse::to_float(cx_s, "x",           cyc).value_or(0.0f);
        s.by = kg_parse::to_float(cy_s, "y",           cyc).value_or(0.0f);
        s.half_thick = half_thick;
        float dx = s.bx - s.ax, dy = s.by - s.ay;
        if (dx * dx + dy * dy < 1e-6f) return;  // degenerate
        out.push_back(s);
    };
    for (auto c : kg.findByType("Cycle"))       add_active(c);
    for (auto c : kg.findByType("PlayerCycle")) add_active(c);
    for (auto c : kg.findByType("AICycle"))     add_active(c);

    return out;
}

} // namespace logotron
