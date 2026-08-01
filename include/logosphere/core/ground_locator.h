#ifndef GROUND_LOCATOR_H
#define GROUND_LOCATOR_H

#include <vector>

class ParticleSystem;

namespace logosphere {

// Where can this body go?
//
// Nothing in this engine may assume a height. There is no floor at
// z = 0: the ground is particles like everything else, it is layered,
// it is streamed, it moves, and on a small world it curves away
// underfoot. Every constant that ever encoded "the ground is about
// here" has been a bug waiting for the day the ground moved -
// creatures spawned at z = 0 buried under half a metre of strata, a
// flight floor of 0.1 m that let butterflies swim through soil, a
// search box clamped to min_z = -1 that could not see ground below
// it, a tree planted at floor height growing clean through a planet.
//
// So: ask. This finds the real surface under a point by looking, and
// answers whether a body of a given size can actually stand, root or
// fly there.
//
// It reads particles through the BVH and knows nothing about game
// categories. What counts as ground is a physical question - is it
// solid, is it still, is it big enough to bear something - not a
// question about what the thing is called.
enum class SupportMode {
    // Rests on a surface. Creatures, rocks, anything with weight.
    STANDING,
    // Rests on a surface AND needs it to be substantial enough to
    // anchor into. A tree may not root on a pebble or a rooftop
    // ornament; it needs real ground.
    ROOTED,
    // Holds a height above the surface. Needs the surface only as a
    // reference and needs clear air to occupy.
    FLYING,
    // Needs no surface at all. A celestial body, a floating world.
    // Only the space itself must be free.
    FREE,
};

struct PlacementRequest {
    float x = 0.0f;
    float y = 0.0f;
    // Horizontal extent to test. A wide body needs wide support.
    float footprint = 0.5f;
    // Vertical space the body needs. Used to check headroom, so a
    // tree is not planted under a ledge it cannot grow through.
    float height = 1.0f;
    SupportMode mode = SupportMode::STANDING;
    // FLYING: how far above the surface to hold. FREE: ignored.
    float clearance = 0.0f;
    // Particles belonging to the body itself, which must not be
    // mistaken for the ground it stands on.
    const std::vector<unsigned int>* ignore = nullptr;
};

struct Placement {
    // Whether a position was found at all. Callers MUST check this;
    // the whole point is to stop guessing a height when there is
    // nothing to stand on.
    bool found = false;
    // Where the body's origin belongs.
    float z = 0.0f;
    // The surface that was found, in world Z. Meaningless for FREE.
    float surface_z = 0.0f;
    // Free vertical space above the surface.
    float headroom = 0.0f;
    // Why, when found is false. Never null.
    const char* reason = "";
};

class GroundLocator {
public:
    void initialize(ParticleSystem* particles) { particles_ = particles; }

    // The surface directly beneath (x, y), whatever height it is at.
    // False when nothing is there to stand on - a hole, the void, a
    // world that has not been poured yet.
    bool surface_at(float x, float y, float& out_surface_z,
                    float footprint = 0.5f,
                    const std::vector<unsigned int>* ignore = nullptr) const;

    // Full answer: can a body of this size, with this relationship to
    // the ground, exist at this point, and where exactly.
    Placement locate(const PlacementRequest& request) const;

    // A surface must be still and substantial to bear anything. These
    // are physical thresholds, not type checks: a falling boulder is
    // not ground while it falls, and a pebble is not ground at all.
    static constexpr float MAX_SUPPORT_SPEED = 0.5f;   // m/s
    static constexpr float MIN_SUPPORT_SIZE  = 0.25f;  // m
    // Rooting demands more than standing: real ground, not a plank.
    static constexpr float MIN_ROOTING_SIZE  = 0.5f;   // m

private:
    ParticleSystem* particles_ = nullptr;
};

}  // namespace logosphere

#endif  // GROUND_LOCATOR_H
