// =============================================================================
// THE SLIP JOINT: one icy interface (G-62) — the limits campaign, TDD headless.
// Scene, case table, bands and evaluator all live in scene_limits.h
// (one source for headless and the window). Derivation in the G-62
// record. Probes the TRIO world; red-where-informative IS the result.
// =============================================================================
#include "scenes/scene_limits.h"

int main() {
    return scene_limits::run_all("THE SLIP JOINT: one icy interface (G-62)",
                                 scene_limits::cases_slipjoint());
}
