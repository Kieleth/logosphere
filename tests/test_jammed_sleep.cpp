// =============================================================================
// THE JAMMED CLUMP MAY NOT SLEEP — G-67 / G-68 / G-48, TDD headless.
// Owner order 2026-09-01: "I'd like to TDD all this first of any change."
// Scene, case table, bars and evaluator live in scene_jammed_sleep.h (one
// source for headless and the window). Born red where the frame-collapse
// chain is broken: the stone born in the floor, the clump that cannot
// sleep, the tile born through a sleeping stone. Green today only for
// G-48's own stack, the guard that a fix may not over-correct.
// =============================================================================
#include "scenes/scene_jammed_sleep.h"

int main() {
    return scene_jammed::run_all(
        "THE JAMMED CLUMP MAY NOT SLEEP (G-67 / G-68 / G-48)");
}
