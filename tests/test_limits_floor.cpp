// =============================================================================
// THE PARTICLE FLOOR: the tower on tiles (G-61) — the limits campaign, TDD headless.
// Scene, case table, bands and evaluator all live in scene_limits.h
// (one source for headless and the window). Derivation in the G-61
// record. Probes the TRIO world; red-where-informative IS the result.
// =============================================================================
#include "scenes/scene_limits.h"

int main() {
    return scene_limits::run_all("THE PARTICLE FLOOR: the tower on tiles (G-61)",
                                 scene_limits::cases_floor());
}
