// Engine adapter for SerpentLocomotion — installs mouse-control hooks
// (screen→world transform + mouse position lookup) when an Engine is
// present. Lives outside logosphere_dynamics so the dynamics library
// has no link-time dependency on RenderSystem / InputSystem; that
// keeps headless test harnesses (which link logosphere_dynamics +
// logosphere_physics + logosphere_core only) clean of rendering.
//
// Called from the Engine wiring path. SerpentLocomotion::initialize
// itself stays inside the dynamics translation unit and does the
// engine-pointer + particle-system bookkeeping.

#include "logosphere/animation/serpent_locomotion.h"

#include "core/engine.h"
#include "core/input_system.h"
#include "logosphere/rendering/coordinate_transformer.h"

namespace logosphere::animation {

void serpent_install_engine_input_hooks(SerpentLocomotion& serpent, Engine* engine) {
    if (!engine) return;
    serpent.set_input_hooks(
        [engine](int sx, int sy, float z, float& wx, float& wy) {
            engine->get_coord_transformer().screen_to_world_at_z(sx, sy, z, wx, wy);
        },
        [engine](float& mx, float& my) {
            const auto& s = engine->get_input_system().get_input_state();
            mx = s.mouse_x;
            my = s.mouse_y;
        }
    );
}

}  // namespace logosphere::animation
