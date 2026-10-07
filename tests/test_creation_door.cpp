// =============================================================================
// THE CREATION DOOR — NOTHING IS BORN INSIDE ANYTHING (INV-37): headless.
// =============================================================================
// The scene, the beats, the door paths and the one evaluator live in
// tests/scenes/scene_creation_door.h, shared with test_creation_door_visual
// (the window: the same cases on one stage, the same evaluate() on the live
// panel). This driver creates no body and holds no threshold.
//
// Run both worlds:
//     ./build/test_creation_door                 # the door
//     CREATION_DOOR=0 ./build/test_creation_door # the world before it: red by design
//     TURTLE_LENIENT=1 ./build/test_creation_door # counts the turtle reading
//     DOOR_CASE=n ./build/test_creation_door     # one case
// =============================================================================
#include "scenes/scene_creation_door.h"

int main() {
    return scene_door::run_all("THE CREATION DOOR: nothing is born inside anything (INV-37)");
}
