// =============================================================================
// THE ANVIL LADDER: mass ratios (G-57) — the limits campaign, TDD headless.
// Scene, case table, bands and evaluator all live in scene_limits.h
// (one source for headless and the window). Derivation in the G-57
// record. Probes the TRIO world; red-where-informative IS the result.
// =============================================================================
#include "scenes/scene_limits.h"

int main() {
    return scene_limits::run_all("THE ANVIL LADDER: mass ratios (G-57)",
                                 scene_limits::cases_anvil());
}
