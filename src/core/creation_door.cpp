#include "logosphere/physics/creation_door.h"

#include "logosphere/physics/narrow_phase.h"
#include "particle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace logosphere {

namespace {

const char* shape_name(ParticleShape s) {
    switch (s) {
        case ParticleShape::SPHERE:    return "SPHERE";
        case ParticleShape::ELLIPSOID: return "ELLIPSOID";
        case ParticleShape::BOX:
        default:                       return "BOX";
    }
}

// Deepest point of a manifold. The solver turns EVERY point into a row, so
// the pair's illegality is the worst of them, not their average.
float deepest_point(const ContactManifold& m) {
    float d = 0.0f;
    for (int i = 0; i < m.num_points; ++i)
        if (m.points[i].penetration > d) d = m.points[i].penetration;
    return d;
}

float area(const AABB6& b) {
    const float w = b.max_x - b.min_x;
    const float h = b.max_y - b.min_y;
    const float d = b.max_z - b.min_z;
    return 2.0f * (w * h + w * d + h * d);
}

AABB6 combine(const AABB6& a, const AABB6& b) {
    AABB6 o;
    o.min_x = std::fmin(a.min_x, b.min_x); o.max_x = std::fmax(a.max_x, b.max_x);
    o.min_y = std::fmin(a.min_y, b.min_y); o.max_y = std::fmax(a.max_y, b.max_y);
    o.min_z = std::fmin(a.min_z, b.min_z); o.max_z = std::fmax(a.max_z, b.max_z);
    return o;
}

bool touches(const AABB6& a, const AABB6& b) {
    return a.min_x <= b.max_x && a.max_x >= b.min_x &&
           a.min_y <= b.max_y && a.max_y >= b.min_y &&
           a.min_z <= b.max_z && a.max_z >= b.min_z;
}

}  // namespace

// ---------------------------------------------------------------------------
// GEOMETRY
// ---------------------------------------------------------------------------

AABB6 creation_bounds(const Particle& p) {
    switch (p.shape) {
        case ParticleShape::SPHERE: {
            const float r = p.size * 0.5f;
            return AABB6{p.x - r, p.x + r, p.y - r, p.y + r, p.z - r, p.z + r};
        }
        case ParticleShape::BOX: {
            // A rotated box's raw extents UNDER-COVER its world span (INV-12),
            // and a bound that under-covers means the exact test is never
            // consulted at all — which reads as a missing body, not a wrong
            // normal. Ask the oriented box.
            if (box_particle_is_rotated(p))
                return aabb_of_obb(obb_of_box_particle(p, p.z));
            const float hx = p.width * 0.5f, hy = p.height * 0.5f,
                        hz = p.thickness * 0.5f;
            return AABB6{p.x - hx, p.x + hx, p.y - hy, p.y + hy,
                         p.z - hz, p.z + hz};
        }
        case ParticleShape::ELLIPSOID:
        default: {
            const float hx = p.width * 0.5f, hy = p.height * 0.5f,
                        hz = p.thickness * 0.5f;
            return AABB6{p.x - hx, p.x + hx, p.y - hy, p.y + hy,
                         p.z - hz, p.z + hz};
        }
    }
}

CreationBody describe_creation_body(int index, const Particle& p) {
    CreationBody b;
    b.index = index;
    b.shape = shape_name(p.shape);
    b.x = p.x; b.y = p.y; b.z = p.z;
    b.rot[0] = p.rotation_x; b.rot[1] = p.rotation_y; b.rot[2] = p.rotation_z;
    b.mass = p.GetMass();

    switch (p.shape) {
        case ParticleShape::SPHERE:
            b.half[0] = b.half[1] = b.half[2] = p.size * 0.5f;
            b.world_min_z = p.z - p.size * 0.5f;
            break;
        case ParticleShape::BOX:
        default:
            b.half[0] = p.width * 0.5f;
            b.half[1] = p.height * 0.5f;
            b.half[2] = p.thickness * 0.5f;
            b.rotated = p.shape == ParticleShape::BOX && box_particle_is_rotated(p);
            b.world_min_z = b.rotated ? creation_bounds(p).min_z
                                      : p.z - b.half[2];
            break;
    }
    return b;
}

float creation_penetration(const Particle& a, const Particle& b,
                           float& out_nx, float& out_ny, float& out_nz) {
    out_nx = out_ny = out_nz = 0.0f;

    ContactManifold m{};
    const bool both_box = (a.shape == ParticleShape::BOX &&
                           b.shape == ParticleShape::BOX);
    const bool either_rotated = both_box &&
        (box_particle_is_rotated(a) || box_particle_is_rotated(b));

    bool hit;
    if (either_rotated) {
        // BOX-BOX does not go through narrow_phase_particle_pair (that path
        // keeps the axis-aligned surface merging the static tiles need), so
        // the oriented case is dispatched here exactly the way the solver
        // dispatches it (physics_system_v4.cpp, `oriented_pair`). A tilted
        // branch compared as its world-axis extents is the wrong solid in
        // both directions: it reports overlaps that are not there and misses
        // ones that are.
        hit = narrow_phase_obb(obb_of_box_particle(a, a.z),
                               obb_of_box_particle(b, b.z),
                               0, 1, /*margin=*/0.0f, m);
    } else {
        hit = narrow_phase_particle_pair(a, b, 0, 1, /*margin=*/0.0f, m);
    }
    if (!hit || m.num_points == 0) return 0.0f;

    out_nx = m.normal_x; out_ny = m.normal_y; out_nz = m.normal_z;
    return deepest_point(m);
}

std::string creation_refusal_text(const CreationRefusal& r,
                                  const char* door_name) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "[PHYSICS REFUSED] %s: the newborn %s at (%.3f, %.3f, %.3f) "
        "half(%.3f, %.3f, %.3f)%s mass=%.1f kg OVERLAPS P%d %s at "
        "(%.3f, %.3f, %.3f) half(%.3f, %.3f, %.3f)%s mass=%.1f kg "
        "by %.1f mm along normal (%.3f, %.3f, %.3f). INV-37: nothing is "
        "born inside anything. The body was NOT created (would-be index "
        "P%d); fix the placement at its source.",
        door_name,
        r.newborn.shape, r.newborn.x, r.newborn.y, r.newborn.z,
        r.newborn.half[0], r.newborn.half[1], r.newborn.half[2],
        r.newborn.rotated ? " ROTATED" : "", r.newborn.mass,
        r.blocker.index, r.blocker.shape, r.blocker.x, r.blocker.y, r.blocker.z,
        r.blocker.half[0], r.blocker.half[1], r.blocker.half[2],
        r.blocker.rotated ? " ROTATED" : "", r.blocker.mass,
        r.depth * 1000.0f, r.nx, r.ny, r.nz,
        r.would_be_index);
    return std::string(buf);
}

std::string creation_site_signature(const CreationBody& newborn,
                                    const CreationBody& blocker) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "%s %.0fx%.0fx%.0f mm inside %s %.0fx%.0fx%.0f mm",
        newborn.shape, newborn.half[0] * 2000.0f, newborn.half[1] * 2000.0f,
        newborn.half[2] * 2000.0f,
        blocker.shape, blocker.half[0] * 2000.0f, blocker.half[1] * 2000.0f,
        blocker.half[2] * 2000.0f);
    return std::string(buf);
}

bool creation_door_enabled() {
    // Read once. A lever that can change mid-run is a lever that can make
    // two halves of one measurement disagree.
    static const bool enabled = [] {
        const char* v = std::getenv("CREATION_DOOR");
        return !(v && std::strcmp(v, "0") == 0);
    }();
    return enabled;
}

// ---------------------------------------------------------------------------
// THE INDEX
// ---------------------------------------------------------------------------

void CreationIndex::clear() {
    nodes_.clear();
    root_ = -1;
    free_ = -1;
    leaf_count_ = 0;
}

int CreationIndex::allocate() {
    if (free_ >= 0) {
        const int i = free_;
        free_ = nodes_[i].next;
        nodes_[i] = Node{};
        return i;
    }
    nodes_.push_back(Node{});
    return static_cast<int>(nodes_.size()) - 1;
}

void CreationIndex::free_node(int i) {
    nodes_[i] = Node{};
    nodes_[i].next = free_;
    free_ = i;
}

void CreationIndex::insert(int body, const AABB6& bounds) {
    const int leaf = allocate();
    nodes_[leaf].box = bounds;
    nodes_[leaf].body = body;
    ++leaf_count_;
    insert_leaf(leaf);
}

// Box2D's branch-and-bound descent, reduced to the greedy form: at each node
// go down the child whose bound would grow least. O(depth) per insert, which
// is the whole point of this class.
void CreationIndex::insert_leaf(int leaf) {
    if (root_ < 0) { root_ = leaf; return; }

    const AABB6 lb = nodes_[leaf].box;
    int node = root_;
    while (!nodes_[node].leaf()) {
        const int c1 = nodes_[node].child1;
        const int c2 = nodes_[node].child2;
        const float cost1 = area(combine(lb, nodes_[c1].box)) - area(nodes_[c1].box);
        const float cost2 = area(combine(lb, nodes_[c2].box)) - area(nodes_[c2].box);
        node = (cost1 < cost2) ? c1 : c2;
    }

    const int sibling = node;
    const int old_parent = nodes_[sibling].parent;
    const int new_parent = allocate();
    nodes_[new_parent].parent = old_parent;
    nodes_[new_parent].box = combine(lb, nodes_[sibling].box);
    nodes_[new_parent].child1 = sibling;
    nodes_[new_parent].child2 = leaf;
    nodes_[sibling].parent = new_parent;
    nodes_[leaf].parent = new_parent;

    if (old_parent < 0) {
        root_ = new_parent;
    } else if (nodes_[old_parent].child1 == sibling) {
        nodes_[old_parent].child1 = new_parent;
    } else {
        nodes_[old_parent].child2 = new_parent;
    }

    refit_up(nodes_[new_parent].parent);
}

void CreationIndex::refit_up(int i) {
    while (i >= 0) {
        nodes_[i].box = combine(nodes_[nodes_[i].child1].box,
                                nodes_[nodes_[i].child2].box);
        i = nodes_[i].parent;
    }
}

void CreationIndex::rebuild(const std::vector<Particle>& particles) {
    clear();
    nodes_.reserve(particles.size() * 2 + 2);
    for (std::size_t i = 0; i < particles.size(); ++i) {
        if (particles[i].is_light_source) continue;   // a light is not a body
        insert(static_cast<int>(i), creation_bounds(particles[i]));
    }
}

// The world moved: every leaf re-reads its body, then every internal node is
// rebuilt bottom-up. The TREE SHAPE is kept — that is what makes this cheaper
// than a rebuild and why the leaves' identity must not have changed (a
// swap-and-pop calls rebuild instead).
void CreationIndex::refit(const std::vector<Particle>& particles) {
    if (root_ < 0) return;
    std::vector<int> stack;
    std::vector<int> order;
    stack.reserve(64);
    order.reserve(leaf_count_ * 2);
    stack.push_back(root_);
    while (!stack.empty()) {
        const int i = stack.back(); stack.pop_back();
        order.push_back(i);
        if (!nodes_[i].leaf()) {
            stack.push_back(nodes_[i].child1);
            stack.push_back(nodes_[i].child2);
        }
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        Node& n = nodes_[*it];
        if (n.leaf()) {
            if (n.body >= 0 && static_cast<std::size_t>(n.body) < particles.size())
                n.box = creation_bounds(particles[n.body]);
        } else {
            n.box = combine(nodes_[n.child1].box, nodes_[n.child2].box);
        }
    }
}

void CreationIndex::query(const AABB6& box, std::vector<int>& out) const {
    out.clear();
    if (root_ < 0) return;
    int stack[128];
    int top = 0;
    stack[top++] = root_;
    while (top > 0) {
        const int i = stack[--top];
        const Node& n = nodes_[i];
        if (!touches(n.box, box)) continue;
        if (n.leaf()) {
            out.push_back(n.body);
        } else if (top + 2 <= 128) {
            stack[top++] = n.child1;
            stack[top++] = n.child2;
        } else {
            // A tree deeper than the stack is a bug in the descent, not a
            // reason to answer wrongly: say so instead of dropping bodies.
            std::fprintf(stderr, "[CREATION DOOR] index deeper than 128; "
                                 "query truncated — this is a defect\n");
        }
    }
}

int CreationIndex::depth() const {
    if (root_ < 0) return 0;
    std::vector<std::pair<int,int>> stack{{root_, 1}};
    int best = 0;
    while (!stack.empty()) {
        auto [i, d] = stack.back(); stack.pop_back();
        best = std::max(best, d);
        if (!nodes_[i].leaf()) {
            stack.push_back({nodes_[i].child1, d + 1});
            stack.push_back({nodes_[i].child2, d + 1});
        }
    }
    return best;
}

}  // namespace logosphere
