// SpeedDashboard — Tron-themed speed HUD.
//
// A retained-mode UI widget that overlays a neon-cyan velocity readout
// in the bottom-right corner of the screen. Reuses the engine's
// `ui::Panel` base + `RenderSystem`'s primitive draw API; no custom
// shaders. Renders AFTER the vision-cone post-process (UISystem::render
// fires after RenderPipeline finishes per engine.cpp:1058,1176), so
// the cone never darkens the HUD.
//
// Driver code (LogotronApplication) calls `set_state(...)` each frame
// with the player Cycle's live speed numbers; the widget owns no game
// state. All input handlers are no-ops + can_focus is disabled, so
// the HUD never interferes with the mouse-look that drives the
// vision cone.
//
// Visual elements (top-to-bottom in the panel):
//   - "VELOCITY" label, small cyan
//   - Big 3-digit speed readout + "M/S" suffix
//   - Segmented horizontal bar (16 cells, neon-cyan-lit / dim)
//   - "TURN +X.Xs" recency indicator
// Corner brackets sit just outside the panel for the Tron HUD vibe.

#pragma once

#include "ui/widgets.h"

namespace logotron::hud {

class SpeedDashboard : public ui::Panel {
public:
    explicit SpeedDashboard(const std::string& id = "logotron_speed_dash");

    // Per-frame state push from the game. Cheap (no allocations).
    // current_speed: m/s, the cycle's live speed.
    // max_speed:     m/s, the cap the ramp climbs toward.
    // base_speed:    m/s, the floor the cycle resets to on turn.
    // time_since_turn: seconds, recency of the last direction change.
    void set_state(float current_speed, float max_speed,
                   float base_speed, float time_since_turn);

    void render(IDrawSurface* renderer) override;

    // Mouse / focus discipline — the HUD must never steal input from
    // the mouse-look (vision cone). All handlers return false; focus
    // is disabled in the constructor.
    bool on_mouse_move(ui::MouseEvent&)  override { return false; }
    bool on_mouse_down(ui::MouseEvent&)  override { return false; }
    bool on_mouse_up(ui::MouseEvent&)    override { return false; }
    bool on_mouse_click(ui::MouseEvent&) override { return false; }
    bool on_mouse_enter(ui::MouseEvent&) override { return false; }
    bool on_mouse_leave(ui::MouseEvent&) override { return false; }

private:
    float current_speed_   = 0.0f;
    float max_speed_       = 1.0f;   // never zero — used as denominator
    float base_speed_      = 0.0f;
    float time_since_turn_ = 0.0f;
};

} // namespace logotron::hud
