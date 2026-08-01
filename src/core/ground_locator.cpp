#include "logosphere/core/ground_locator.h"

#include "core/particle_system.h"
#include "logosphere/physics/bvh.h"
#include "particle.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace logosphere {

namespace {

// One candidate surface in the column under a point.
struct Span {
    float bottom;
    float top;
    float size;      // smaller of width/height: how much it can bear
    bool  solid;     // still enough, big enough to stand on
};

bool is_ignored(unsigned int index,
                const std::vector<unsigned int>* ignore) {
    if (!ignore) return false;
    for (unsigned int i : *ignore) if (i == index) return true;
    return false;
}

}  // namespace

bool GroundLocator::surface_at(float x, float y, float& out_surface_z,
                               float footprint,
                               const std::vector<unsigned int>* ignore) const {
    PlacementRequest r;
    r.x = x; r.y = y;
    r.footprint = footprint;
    r.height = 0.0f;          // only asking where the ground is
    r.mode = SupportMode::STANDING;
    r.ignore = ignore;
    Placement p = locate(r);
    if (!p.found) return false;
    out_surface_z = p.surface_z;
    return true;
}

Placement GroundLocator::locate(const PlacementRequest& req) const {
    Placement out;
    if (!particles_) {
        out.reason = "no particle system";
        return out;
    }
    const BVH* bvh = particles_->get_shadow_bvh();
    if (!bvh) {
        out.reason = "no spatial index";
        return out;
    }

    auto view = particles_->lock_particles_for_read();
    const std::vector<Particle>& all = view.get();

    // Search the WHOLE column. No min_z, no max_z, no assumption that
    // the ground is near any particular height: it may be a planet's
    // crust at z = 8, a cellar floor below zero, or nothing at all.
    const float half = std::max(0.01f, req.footprint * 0.5f);
    const float inf = std::numeric_limits<float>::max();
    AABB column(req.x - half, req.y - half, -inf,
                req.x + half, req.y + half, inf);

    std::vector<int> candidates;
    bvh->query_aabb(column, all, candidates);

    std::vector<Span> spans;
    spans.reserve(candidates.size());
    for (int idx : candidates) {
        if (idx < 0 || static_cast<size_t>(idx) >= all.size()) continue;
        if (is_ignored(static_cast<unsigned int>(idx), req.ignore)) continue;
        const Particle& p = all[idx];

        const float half_thick = p.thickness * 0.5f;
        Span s;
        s.bottom = p.z - half_thick;
        s.top    = p.z + half_thick;
        s.size   = std::min(p.width, p.height);

        // Whether something can be STOOD ON is physical, never a
        // question of what it is called. It must be still - a falling
        // boulder is not ground while it falls - and big enough to
        // bear a body, which is what keeps debris and body parts from
        // being mistaken for floor.
        const float speed_sq = p.vx * p.vx + p.vy * p.vy + p.vz * p.vz;
        s.solid = speed_sq <= (MAX_SUPPORT_SPEED * MAX_SUPPORT_SPEED) &&
                  s.size >= MIN_SUPPORT_SIZE;
        spans.push_back(s);
    }

    // FREE bodies need no ground, only room: a floating world, a sun.
    if (req.mode == SupportMode::FREE) {
        out.found = true;
        out.z = 0.0f;          // caller owns the height entirely
        out.surface_z = 0.0f;
        out.headroom = inf;
        return out;
    }

    if (spans.empty()) {
        out.reason = "nothing underfoot: no surface at this point";
        return out;
    }

    const float need_size = (req.mode == SupportMode::ROOTED)
                          ? MIN_ROOTING_SIZE : MIN_SUPPORT_SIZE;

    // Consider every surface in the column, highest first, and take
    // the first that can both bear the body and give it room. Highest
    // first means a body lands on the roof rather than in the cellar.
    std::vector<Span> sorted = spans;
    std::sort(sorted.begin(), sorted.end(),
              [](const Span& a, const Span& b) { return a.top > b.top; });

    for (const Span& s : sorted) {
        if (!s.solid || s.size < need_size) continue;

        // Where the body would sit, and the space it would occupy.
        const float base = (req.mode == SupportMode::FLYING)
                         ? s.top + std::max(0.0f, req.clearance)
                         : s.top;
        const float need_top = base + std::max(0.0f, req.height);

        // Is that space actually free? Anything overlapping it blocks
        // this surface - a ledge, a canopy, the underside of a floor
        // above. This is the "surrounding space" question: a point can
        // have perfectly good ground and still be unusable.
        float ceiling = inf;
        bool blocked = false;
        for (const Span& other : spans) {
            if (other.top <= s.top + 1e-4f) continue;   // at or below us
            if (other.bottom < need_top && other.top > base) {
                blocked = true;
                break;
            }
            ceiling = std::min(ceiling, other.bottom);
        }
        if (blocked) continue;

        out.found = true;
        out.surface_z = s.top;
        out.z = base;
        out.headroom = (ceiling == inf) ? inf : (ceiling - s.top);
        return out;
    }

    // Something was there, but nothing that could hold this body.
    out.reason = (req.mode == SupportMode::ROOTED)
               ? "no ground solid enough to root into"
               : "no stable surface large enough, or no room above it";
    return out;
}

}  // namespace logosphere
