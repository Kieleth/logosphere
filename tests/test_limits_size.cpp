// =============================================================================
// THE SIZE LADDER: 10 cm to 3 m (G-59) — the limits campaign, TDD headless.
// Scene, case table, bands and evaluator all live in scene_limits.h
// (one source for headless and the window). Derivation in the G-59
// record. Probes the TRIO world; red-where-informative IS the result.
// =============================================================================
#include "scenes/scene_limits.h"

int main() {
    return scene_limits::run_all("THE SIZE LADDER: 10 cm to 3 m (G-59)",
                                 scene_limits::cases_size());
}
