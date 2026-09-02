#if __has_include(<execinfo.h>)
#include <execinfo.h>  // TURTLE_TRACE caller naming; absent under MSVC
#define LOGOSPHERE_HAS_EXECINFO 1
#endif
#include "logosphere/physics/physics_solver.h"
#include <cstdio>
#include <string>
#include <set>
#include "particle_system.h"
#include "particle_geometry_v2.h"
#include "triangle_cube_geometry.h"  // For triangle-based cubes
#include "object_id.h"
#include "lighting_config.h"
#include "core/bvh_frame_counter.h"  // For BVH-rebuild tick
#include "logosphere/kg/kg_module.h"  // For swap-and-pop notifications
#include "surface_cache.h"  // For cache invalidation during particle deletion
#include "logosphere/physics/physics_system.h"  // For gluon cleanup during particle deletion
#include "logosphere/dynamics/particle_dynamics_system.h"  // For dynamics swap notification
#include "logosphere/physics/physics_flags.h"  // For forensic logging
#include "logosphere/worldgen/noise.h"  // For organic floor color variation
#include <cmath>
#include <iostream>
#include <cassert>
#include <cstdlib>  // For std::abort()
#include <algorithm>  // For std::find
#include <chrono>     // The creation door measures its own cost
#include "logosphere/physics/creation_door.h"

// EDUCATIONAL NOTE: The Pimpl Idiom (not used here, but worth knowing)
// In C++, we often separate the interface (.h) from implementation (.cpp)
// This is like Python's separation of public API from internal implementation
// 
// Benefits:
// 1. Faster compilation - changes to .cpp don't require recompiling users
// 2. Hidden implementation details - users only see the public interface
// 3. Binary compatibility - can change implementation without breaking ABI


// Constants from main.mm that we need
const float VIEWPORT_WIDTH_UNITS = 80.0f;
const float VIEWPORT_HEIGHT_UNITS = 60.0f;

ParticleSystem::ParticleSystem() {
    // Pre-allocate capacity to prevent reallocations during chunk loading
    // 2000 = max expected particles (650 current + growth headroom)
    particles.reserve(2000);
    spatial_memory.reserve(1000);
}

ParticleSystem::~ParticleSystem() {
    // Destructor - vectors clean themselves up automatically
    // This is like Python's __del__ but more predictable
    //
    // The door's census, once, on the way out. A refusal scrolls past in a
    // world that creates twelve thousand bodies; the census is what turns
    // those lines into a list of SITES to fix.
    if (door_stats_.refusals > 0) report_creation_door();
}

// ============================================================================
// THE TURTLE IS THE FLOOR OF THE WORLD — NOTHING IS PLACED BENEATH IT
// ============================================================================
// TURTLE_Z is an absolute boundary, and enforce_turtle_boundary() lifts any
// body found below it EVERY SUBSTEP as a raw position correction with no
// energy accounting. A body deliberately placed underground therefore becomes
// a permanent, unpayable energy source: the turtle lifts it, gravity returns
// it, forever. Measured with the energy ledger on one buried root plate:
// +30.6 J every substep and +15797 J on the first, which travelled up the
// bonds and threw the tree's canopy to 120 m.
//
// That is not a tuning problem and it cannot be fixed downstream, so it is
// caught HERE, at the one door every body comes through, rather than written
// down as a rule someone has to remember. A generator that wants roots
// underground has to reconsider, not negotiate.
//
// Reports once per offending call site with the arithmetic, so a scene that
// does it a thousand times says so once and stays readable.
static void assert_above_turtle(const Particle& p, const char* where) {
    if (p.GetMass() == 0.0f) return;               // lights float, no body
    const float bottom = p.z - p.thickness * 0.5f;
    // Same tolerance the boundary itself uses. Below SLOP this is float
    // residue on a body trying to sit exactly on the floor, not a placement
    // decision, and reporting it as one buries the real violations in noise.
    if (bottom >= PhysicsV4::TURTLE_Z - PhysicsV4::SLOP) return;
    static std::set<std::string> reported;
    char key[256];
    std::snprintf(key, sizeof(key), "%s|%.4f", where, bottom);
    if (!reported.insert(key).second) return;
    std::cerr << "[TURTLE VIOLATION] " << where
              << ": body placed BELOW the world floor. z=" << p.z
              << " thickness=" << p.thickness
              << " => bottom=" << bottom
              << " < TURTLE_Z=" << PhysicsV4::TURTLE_Z
              << " (by " << (PhysicsV4::TURTLE_Z - bottom) << " m)."
              << " The turtle will lift this body every substep forever and"
              << " the lift is free energy. Place it so its BOTTOM sits at or"
              << " above " << PhysicsV4::TURTLE_Z << "." << std::endl;
    // NAME THE CALLER. Grep could not find this one because the z was
    // computed rather than literal, and searching for it burned several
    // rounds. A guard that reports WHO placed the body ends that class of
    // hunt permanently: the next violation identifies its own source.
#if defined(LOGOSPHERE_HAS_EXECINFO)
    if (std::getenv("TURTLE_TRACE")) {
        void* frames[12];
        const int n = backtrace(frames, 12);
        char** syms = backtrace_symbols(frames, n);
        if (syms) {
            std::cerr << "  placed by:" << std::endl;
            for (int i = 1; i < n && i < 8; ++i)
                std::cerr << "    " << syms[i] << std::endl;
            std::free(syms);
        }
    }
#endif
    // STRICT BY DEFAULT. All 51 sites the sweep found are fixed and the whole
    // suite reports zero, so a violation from here on is a NEW one and should
    // stop the world rather than scroll past. TURTLE_LENIENT=1 downgrades it
    // to a warning for anyone mid-migration.
    if (!std::getenv("TURTLE_LENIENT")) {
        std::cerr << "[TURTLE VIOLATION] TURTLE_STRICT set — aborting so the "
                     "placement is fixed rather than absorbed." << std::endl;
        std::abort();
    }
}

// ============================================================================
// THE CREATION DOOR — NOTHING IS BORN INSIDE ANYTHING (INV-37)
// ============================================================================
// Owner decree 2026-09-01: "under no circumstances, any creation of particles
// should be allowed to overlap in space with another... we should have an
// assert/except for any creation-overlapping moment." Owner ruling 2026-09-02
// on the shape: "fresh at the choke point, with the incremental BVH cost
// measured on the headless bench before it ships, in TDD please."
//
// This is the twin of assert_above_turtle above, at the same boundary and for
// the same reason: a body placed inside another body is not a tight fit to be
// relaxed on frame one. The solver's only correct response to two solids in
// one place is a separating impulse, and when the pair cannot separate the two
// bodies never sleep — GEDANKEN-67 traced Eden's 3.4 s frames to exactly that,
// stones born inside strata tiles. Refusing at creation costs a rejected
// placement; accepting it costs the world.
//
// The DIFFERENCE from the earlier batch-audit design (feat/creation-overlap-
// door): that one ran at the end of flush_pending_particles, so it could not
// see a direct add until somebody flushed — and the chunk paths, entity
// activation, the generators and add_particle_with_gluon_to are all direct
// adds, while a headless run may never flush at all. This one stands at the
// push_back, which is the only line every birth crosses.
//
// The reasoning, the threshold (PhysicsV4::SLOP, no new constant — INV-29) and
// the geometry (the engine's own narrow phase, oriented — INV-12) are in
// include/logosphere/physics/creation_door.h.

// Bring the index up to date with the live array. Three states, cheapest
// first: nothing changed (free), the world moved (one refit), indices moved
// (one rebuild — swap-and-pop makes every stored index mean a different body).
void ParticleSystem::sync_creation_index() {
    using clock = std::chrono::high_resolution_clock;
    if (creation_index_invalid_) {
        const auto t0 = clock::now();
        creation_index_.rebuild(particles);
        creation_indexed_count_ = particles.size();
        creation_index_invalid_ = false;
        creation_index_stale_ = false;
        door_stats_.rebuilds++;
        door_stats_.rebuild_micros +=
            std::chrono::duration<double, std::micro>(clock::now() - t0).count();
        return;
    }
    if (creation_index_stale_) {
        const auto t0 = clock::now();
        creation_index_.refit(particles);
        creation_index_stale_ = false;
        door_stats_.refits++;
        door_stats_.refit_micros +=
            std::chrono::duration<double, std::micro>(clock::now() - t0).count();
    }
    // Bodies that arrived without going through the door's insert (there are
    // none today, but a future writer must not be able to make the index lie).
    for (size_t i = creation_indexed_count_; i < particles.size(); ++i) {
        if (particles[i].is_light_source) continue;
        creation_index_.insert(static_cast<int>(i), logosphere::creation_bounds(particles[i]));
    }
    creation_indexed_count_ = particles.size();
}

bool ParticleSystem::creation_door_refuses(const Particle& p, int would_be_index,
                                           const char* door_name,
                                           bool against_pending) {
    last_refusal_ = logosphere::CreationRefusal{};
    if (!logosphere::creation_door_enabled()) return false;
    if (p.is_light_source) return false;   // a light is not a body

    using clock = std::chrono::high_resolution_clock;
    const auto t0 = clock::now();
    door_stats_.births++;

    sync_creation_index();

    const AABB6 box = logosphere::creation_bounds(p);
    std::vector<int> candidates;
    float depth = 0.0f, nx = 0.0f, ny = 0.0f, nz = 0.0f;
    int blocker = -1;
    const Particle* blocker_body = nullptr;

    // Against the world.
    creation_index_.query(box, candidates);
    door_stats_.candidates += candidates.size();
    for (int ci : candidates) {
        if (ci < 0 || static_cast<size_t>(ci) >= particles.size()) continue;
        const Particle& b = particles[ci];
        if (b.is_light_source) continue;
        door_stats_.exact_tests++;
        float tx, ty, tz;
        const float d = logosphere::creation_penetration(p, b, tx, ty, tz);
        if (d > depth) { depth = d; nx = tx; ny = ty; nz = tz;
                         blocker = ci; blocker_body = &b; }
    }

    // Against the batch already queued. Two bodies queued into the same place
    // are still two bodies in the same place, and the second one must not be
    // counted in a predicted index it will never take.
    if (against_pending && !pending_index_.empty()) {
        pending_index_.query(box, candidates);
        door_stats_.candidates += candidates.size();
        for (int ci : candidates) {
            if (ci < 0 || static_cast<size_t>(ci) >= (int)pending_particles.size()) continue;
            const Particle& b = pending_particles[ci];
            if (b.is_light_source) continue;
            door_stats_.exact_tests++;
            float tx, ty, tz;
            const float d = logosphere::creation_penetration(p, b, tx, ty, tz);
            if (d > depth) {
                depth = d; nx = tx; ny = ty; nz = tz;
                // Named by the index it will take once flushed, which is the
                // only name a queued body has.
                blocker = static_cast<int>(particles.size()) + ci;
                blocker_body = &b;
            }
        }
    }

    door_stats_.micros +=
        std::chrono::duration<double, std::micro>(clock::now() - t0).count();

    // TOUCHING IS NOT OVERLAPPING. SLOP is the engine's own "geometric error,
    // not geometry" constant; adjacent floor tiles and bonded segments share a
    // face and measure zero.
    if (depth <= PhysicsV4::SLOP || blocker_body == nullptr) return false;

    last_refusal_.refused = true;
    last_refusal_.would_be_index = would_be_index;
    last_refusal_.depth = depth;
    last_refusal_.nx = nx; last_refusal_.ny = ny; last_refusal_.nz = nz;
    last_refusal_.newborn = logosphere::describe_creation_body(-1, p);
    last_refusal_.blocker = logosphere::describe_creation_body(blocker, *blocker_body);
    last_refusal_.text = logosphere::creation_refusal_text(last_refusal_, door_name);

    door_stats_.refusals++;
    door_census_[logosphere::creation_site_signature(last_refusal_.newborn,
                                                     last_refusal_.blocker)]++;

    // Every refusal is printed: the census on a first run under the door IS
    // the red list of placements to fix, and a silent drop would be the
    // opposite of a door. A world that refuses thousands says so once past the
    // cap and keeps the log readable; CREATION_DOOR_VERBOSE=1 prints them all.
    static const bool verbose = std::getenv("CREATION_DOOR_VERBOSE") != nullptr;
    constexpr size_t LINE_CAP = 400;
    if (verbose || door_lines_printed_ < LINE_CAP) {
        std::cerr << last_refusal_.text << std::endl;
        door_lines_printed_++;
    } else if (door_lines_printed_ == LINE_CAP) {
        std::cerr << "[PHYSICS REFUSED] ... further refusals counted, not printed "
                     "(CREATION_DOOR_VERBOSE=1 prints every one; the census "
                     "arrives at shutdown)" << std::endl;
        door_lines_printed_++;
    }
    return true;
}

void ParticleSystem::report_creation_door() const {
    const logosphere::CreationDoorStats& s = door_stats_;
    std::cerr << "\n[CREATION DOOR] INV-37 census: " << s.refusals
              << " refused of " << s.births << " births judged"
              << " | " << s.candidates << " index candidates, "
              << s.exact_tests << " exact narrow-phase tests"
              << " | door " << s.micros / 1000.0 << " ms total ("
              << (s.births ? s.micros / (double)s.births : 0.0) << " us/birth)"
              << ", of which rebuild " << s.rebuild_micros / 1000.0 << " ms x"
              << s.rebuilds << " and refit " << s.refit_micros / 1000.0
              << " ms x" << s.refits << std::endl;
    for (const auto& kv : door_census_)
        std::cerr << "  " << kv.second << "  " << kv.first << std::endl;
}

int ParticleSystem::add_particle(const Particle& particle) {
    assert_above_turtle(particle, "add_particle");
    // THREAD SAFETY: Acquire exclusive lock for particle addition
    std::unique_lock<std::shared_mutex> lock(particles_mutex_);

    // AUTO-CREATE ENTITY: If entity_id is 0, auto-create via KG
    // This allows tests and simple code to call add_particle() directly
    Particle p = particle;
    if (p.entity_id == 0 && kg_module_ != nullptr) {
        p.entity_id = kg_module_->createEntity("AutoParticle");
    }

    // ORIENTATION SEED: rotation_q is born coherent with the Euler triple
    // instead of born identity-and-stale. Callers spell orientation in
    // Euler (worldgen, tests, examples); the quaternion of a body that is
    // not quat-driven was previously "a stale by-product", which meant a
    // body spawned tilted carried an IDENTITY quaternion — harmless while
    // nothing read it, fatal the moment the quaternion becomes the truth
    // (LOGOSPHERE_QUAT_TRUTH would erase the tilt on frame one). Nothing
    // in the default path reads q for these bodies, so this is
    // observable-behavior-neutral today, and it establishes the contract
    // the unification needs: one body, one orientation, from birth.
    // Quat-driven callers set rotation_q deliberately and are left alone.
    if (!p.is_quat_driven) {
        p.rotation_q = logosphere::Quat::from_euler(
            p.rotation_x, p.rotation_y, p.rotation_z);
    }

    // AUTO-CALCULATE MASS from density × volume
    // Light sources are exempt - they use density=0 to float (not subject to physics)
    // Mass is now PRIVATE - only CalculateMass() can set it
    if (!p.is_light_source) {
        p.CalculateMass();  // Sets mass = width × height × thickness × material_density
    }

    // MASS VALIDATION: All physical particles MUST have mass > 0 after auto-calc
    // This catches cases where density was 0 (invalid configuration, unless light source)
    if (!p.is_light_source && p.GetMass() <= 0.0f) {
        std::cerr << "[PARTICLE_ERROR] Particle has no mass after auto-calc! "
                  << "pos=(" << p.x << "," << p.y << "," << p.z << ") "
                  << "size=" << p.width << "x" << p.height << "x" << p.thickness
                  << " density=" << p.material_density
                  << std::endl;
        assert(false && "Particle must have mass > 0 or density > 0 (unless is_light_source=true)");
    }

    // THE CREATION DOOR (INV-37). Last gate before the body exists: it is
    // judged with the geometry it will actually have (the quaternion seed and
    // the mass are already computed above), and a refusal leaves the world
    // exactly as it was. No lenient switch, no class list.
    if (creation_door_refuses(p, static_cast<int>(particles.size()),
                              "add_particle", /*against_pending=*/false)) {
        return -1;
    }

    // Assign stable particle ID for BVH slot mapping (before adding to vector)
    if (p.particle_id == 0) {  // Only assign if not already set
        p.particle_id = next_particle_id_++;
    }

    particles.push_back(p);
    int particle_index = particles.size() - 1;  // Index of newly added particle

    // Maintain entity→particle index mapping
    if (p.entity_id > 0) {
        if (p.entity_id >= entity_particle_indices_.size()) {
            entity_particle_indices_.resize(p.entity_id + 1);
        }
        entity_particle_indices_[p.entity_id].push_back(particle_index);
    }

    // FORENSICS: Log particle state immediately after addition
    if constexpr (PhysicsSolver::ENABLE_FORENSIC_LOGGING) {
        std::cout << "[PARTICLE_ADD] Index=" << particle_index
                  << " z=" << p.z << "m mass=" << p.GetMass() << "kg"
                  << std::endl;
    }

    // Mark BVH as dirty when adding particles so shadows work immediately
    mark_bvh_dirty();

    // The door's index owns this body from here. Inserted AFTER
    // mark_bvh_dirty, which sets the stale flag: our own insert is exact, so
    // a birth must not cost the next birth a refit. With the door killed
    // (CREATION_DOOR=0) the index is never built and never queried, so the
    // A/B measures the door and nothing else.
    if (logosphere::creation_door_enabled()) {
        if (!p.is_light_source)
            creation_index_.insert(particle_index, logosphere::creation_bounds(p));
        creation_indexed_count_ = particles.size();
        creation_index_stale_ = false;
    }

    // Return the particle index (0, 1, 2, ...) which is what the rendering system uses
    return particle_index;
}

void ParticleSystem::clear_particles() {
    // THREAD SAFETY: Acquire exclusive lock for clearing particles
    std::unique_lock<std::shared_mutex> lock(particles_mutex_);

    particles.clear();
    entity_particle_indices_.clear();  // Clear entity→particle mapping

    // The door's index is a map from live-array indices to bodies; an empty
    // array makes every entry a lie (INV-37).
    creation_index_.clear();
    creation_indexed_count_ = 0;
    creation_index_invalid_ = true;
    next_particle_id_ = 1;  // Reset ID counter (0 is reserved)

    // Mark BVH dirty so acceleration structures rebuild with empty scene
    mark_bvh_dirty();
}

uint32_t ParticleSystem::allocate_particle_id() {
    // Thread-safe allocation of a unique particle ID
    // Used when storing particles in KG before they're added to ParticleSystem
    std::unique_lock<std::shared_mutex> lock(particles_mutex_);
    return next_particle_id_++;
}

// Internal unlocked version - caller must hold particles_mutex_
void ParticleSystem::remove_particle_unlocked(size_t index) {
    if (index >= particles.size()) {
        std::cerr << "[PARTICLE_SYSTEM ERROR] Tried to remove invalid index " << index
                  << " (size=" << particles.size() << ")" << std::endl;
        return;
    }

    // Deferred-deletion queue coherence: entries hold raw indices, so this
    // removal must (a) drop entries aimed at the dying slot — duplicates
    // and already-deleted targets — and (b) retarget entries tracking the
    // tail particle that is about to move into `index`. Without this pass
    // a queued index goes stale at the first unrelated swap and the flush
    // deletes the wrong particle (tests/test_deferred_deletion_integrity.cpp).
    {
        const size_t tail_index = particles.size() - 1;
        for (auto it = pending_deletions_.begin(); it != pending_deletions_.end(); ) {
            if (it->particle_index == index) {
                it = pending_deletions_.erase(it);
            } else {
                if (it->particle_index == tail_index) it->particle_index = index;
                ++it;
            }
        }
    }

    // CRITICAL: Remove gluons referencing this particle BEFORE swap
    // This prevents dangling gluon references after particle deletion
    if (physics_system_) {
        physics_system_->remove_gluons_for_particle(index);
    }

    // CRITICAL: Erase KG mappings for particle being deleted BEFORE swap
    // This prevents stale KGParticle mappings after swap-and-pop
    kg_module_->eraseRenderIndexMappings(index);

    // Remove deleted particle from entity→particle mapping
    kg::EntityID deleted_entity_id = particles[index].entity_id;
    if (deleted_entity_id > 0 && deleted_entity_id < entity_particle_indices_.size()) {
        auto& indices = entity_particle_indices_[deleted_entity_id];
        auto it = std::find(indices.begin(), indices.end(), static_cast<int>(index));
        if (it != indices.end()) {
            // Swap-and-pop for O(1) removal from entity's list too
            *it = indices.back();
            indices.pop_back();
        }
    }

    // Efficient removal by swapping with last element (O(1) instead of O(n))
    if (index != particles.size() - 1) {
        size_t last_index = particles.size() - 1;

        // DEBUG: Log particle swaps with light source info
        bool swapped_is_light = particles[last_index].is_light_source;
        if (swapped_is_light) {
            std::cout << "[PARTICLE_SWAP WARNING] Light source particle at index " << last_index
                      << " being swapped to index " << index << " (emission="
                      << particles[last_index].emission_strength << ")" << std::endl;
        }

        particles[index] = particles.back();

        // CRITICAL: Notify KG that particle at last_index moved to index
        kg_module_->notifyParticleSwap(last_index, index);

        // CRITICAL: Update gluon indices - particle at last_index now lives at index
        if (physics_system_) {
            physics_system_->notify_particle_swap(last_index, index);
        }

        // CRITICAL: Update dynamics system cached IDs (humanoid/serpent/butterfly)
        // so their per-frame updates write to the correct particle positions.
        if (dynamics_system_) {
            dynamics_system_->notify_particle_swap(last_index, index);
        }

        // Notify external swap subscribers (e.g., Eden's SpiritLight IDs)
        for (auto& cb : swap_callbacks_) {
            cb(last_index, index);
        }

        // CRITICAL: Update entity→particle mapping for swapped particle
        // The particle that was at last_index is now at index
        kg::EntityID swapped_entity_id = particles[index].entity_id;  // Already swapped
        if (swapped_entity_id > 0 && swapped_entity_id < entity_particle_indices_.size()) {
            auto& indices = entity_particle_indices_[swapped_entity_id];
            auto it = std::find(indices.begin(), indices.end(), static_cast<int>(last_index));
            if (it != indices.end()) {
                *it = static_cast<int>(index);  // Update to new location
            }
        }

        // CRITICAL: Invalidate surface cache - swapped particle has different geometry
        SurfaceCache::get().invalidate(index);
    }

    // Invalidate the particle being removed
    SurfaceCache::get().invalidate(particles.size() - 1);

    particles.pop_back();

    // CRITICAL: Mark BVH dirty - swap-and-pop changes particle indices
    mark_bvh_dirty();

    // Same reason, for the creation door's index: after a swap-and-pop every
    // stored index means a DIFFERENT body, so the tree cannot be refitted, it
    // has to be rebuilt. Deferred to the next birth — a chunk unload that
    // removes seven hundred bodies pays for one rebuild, not seven hundred.
    creation_index_invalid_ = true;
}

void ParticleSystem::remove_particle(size_t index) {
    // THREAD SAFETY: Acquire exclusive lock for particle deletion
    std::unique_lock<std::shared_mutex> lock(particles_mutex_);
    remove_particle_unlocked(index);
}

// ParticleSystem::update (the main.mm-era universal x/y Euler loop) was
// deleted 2026-07-09. It integrated EVERY particle's x/y from vx/vy each
// frame regardless of solver_mode/owner, double-advancing anything with
// sustained horizontal velocity: humanoid bodies (on top of the
// locomotion shape pipeline) and solver-DYNAMIC bodies (on top of the
// solver) moved ~2x their velocity field, so the FORGE speed cap never
// bound actual ground speed. Position authority is per-owner now:
// solver-DYNAMIC → physics integrate_positions (which also marks the
// BVH dirty); DYNAMICS-owned → their dynamics system; ANIMATION-owned →
// FK. Contract: tests/test_position_authority.cpp.

void ParticleSystem::update_bvh() {
    if (bvh_dirty_) {
        size_t current_count = particles.size();

        if (last_bvh_particle_count_ == current_count && shadow_bvh_.is_ready()) {
            // Particle count unchanged → fast refit (update AABBs only)
            shadow_bvh_.refit(particles);
        } else {
            // First build or particle count changed → full rebuild
            shadow_bvh_.build(particles);
            last_bvh_particle_count_ = current_count;
        }

        bvh_dirty_ = false;

        // Signal to worker threads that BVH has been rebuilt
        // They need to refresh their thread-local copies
        logosphere::bvh_frame::increment();
    }
}

int ParticleSystem::queue_particle_addition(const Particle& particle) {
    assert_above_turtle(particle, "queue_particle_addition");

    // THE CREATION DOOR, at the queue as well as at the push_back (INV-37).
    // Not belt-and-braces: this function hands back a PREDICTED index
    // (live count + queue length), and generators bond by it. A body dropped
    // later at the flush would silently shift every prediction handed out
    // after it, so a body that cannot be born must never be counted here
    // either (GEDANKEN-69). Judged against the live world AND against the
    // batch already queued.
    {
        std::shared_lock<std::shared_mutex> lock(particles_mutex_);
        const int would_be = static_cast<int>(particles.size() + pending_particles.size());
        if (creation_door_refuses(particle, would_be, "queue_particle_addition",
                                  /*against_pending=*/true)) {
            return -1;
        }
        if (logosphere::creation_door_enabled() && !particle.is_light_source)
            pending_index_.insert(static_cast<int>(pending_particles.size()),
                                  logosphere::creation_bounds(particle));
    }

    // Queue the particle for deferred addition
    pending_particles.push_back(particle);

    // CRITICAL: Mark BVH as dirty immediately when particles are queued
    // This ensures shadows work as soon as particles are flushed
    mark_bvh_dirty();

    // Return what the future ID will be (current size + pending count)
    return static_cast<int>(particles.size() + pending_particles.size() - 1);
}

void ParticleSystem::flush_pending_particles() {
    // Apply all queued particle additions
    // This should be called between frames when no rendering is happening
    while (!pending_particles.empty()) {
        add_particle(pending_particles.front());
        pending_particles.pop_front();
    }
    // The batch is over: its bodies are live (or were refused), and the
    // pending index has nothing left to stand for.
    pending_index_.clear();
}

void ParticleSystem::queue_particle_deletion(size_t index, int current_frame_number) {
    const int FRAMES_IN_FLIGHT = 3;  // Triple buffering
    int safe_frame = current_frame_number + FRAMES_IN_FLIGHT;

    pending_deletions_.push_back({index, safe_frame});
}

void ParticleSystem::flush_safe_deletions(int current_frame_number) {
    if (pending_deletions_.empty() ||
        pending_deletions_.front().safe_deletion_frame > current_frame_number) {
        return;  // Nothing ready this frame
    }

    // Pop one, delete one. Each removal's coherence pass (see
    // remove_particle_unlocked) retargets or drops the remaining
    // entries, so every entry executes against a live index.
    // Pre-draining into a local batch would freeze indices captured
    // before the batch's own swaps and re-create the wrong-particle
    // deletion bug this loop replaced.
    size_t deleted = 0;
    size_t count_after = 0;
    {
        // THREAD SAFETY: exclusive lock once for the whole batch
        std::unique_lock<std::shared_mutex> lock(particles_mutex_);
        while (!pending_deletions_.empty() &&
               pending_deletions_.front().safe_deletion_frame <= current_frame_number) {
            size_t idx = pending_deletions_.front().particle_index;
            pending_deletions_.pop_front();
            remove_particle_unlocked(idx);
            deleted++;
        }
        count_after = particles.size();
    }

    std::cout << "[PARTICLE_DELETE_FLUSH] Frame " << current_frame_number
              << " - deleting " << deleted << " particles" << std::endl;
    std::cout << "[PARTICLE_DELETE_FLUSH] Done - particle count now: " << count_after << std::endl;
}

bool ParticleSystem::has_ready_deletions(int current_frame_number) const {
    if (pending_deletions_.empty()) {
        return false;
    }

    // Check if the oldest deletion is ready (front of deque has lowest frame number)
    return pending_deletions_.front().safe_deletion_frame <= current_frame_number;
}

void ParticleSystem::update_spatial_memory(double current_time) {
    // This is a placeholder - the full implementation requires access to
    // vision system which we'll extract later
    // For now, just apply decay to existing memories
    
    for (auto& memory : spatial_memory) {
        apply_memory_decay(memory, current_time);
    }
    
    // Remove forgotten memories
    spatial_memory.erase(
        std::remove_if(spatial_memory.begin(), spatial_memory.end(),
                      [](const MemoryParticle& m) { return m.confidence <= 0.0f; }),
        spatial_memory.end()
    );
}

void ParticleSystem::add_to_spatial_memory(const Particle& particle, double current_time) {
    // Check if we already remember this particle
    for (auto& memory : spatial_memory) {
        float dx = memory.original_x - particle.x;
        float dy = memory.original_y - particle.y;
        float dist = sqrtf(dx * dx + dy * dy);
        
        if (dist < 0.5f) {
            // Update existing memory
            memory.x = particle.x;
            memory.y = particle.y;
            memory.r = particle.r;
            memory.g = particle.g;
            memory.b = particle.b;
            memory.size = particle.size;
            memory.last_seen_time = current_time;
            memory.observation_count++;
            memory.confidence = std::min(1.0f, memory.confidence + 0.1f);
            return;
        }
    }
    
    // Create new memory
    MemoryParticle new_memory;
    new_memory.x = particle.x;
    new_memory.y = particle.y;
    new_memory.original_x = particle.x;
    new_memory.original_y = particle.y;
    new_memory.r = particle.r;
    new_memory.g = particle.g;
    new_memory.b = particle.b;
    new_memory.a = particle.a;
    new_memory.size = particle.size;
    new_memory.confidence = 1.0f;
    new_memory.last_seen_time = current_time;
    new_memory.distance_when_seen = 0.0f;  // Will be set by vision system
    new_memory.observation_count = 1;
    
    spatial_memory.push_back(new_memory);
}

MemoryLayer ParticleSystem::get_memory_layer(const MemoryParticle& memory, double current_time) {
    double time_since_seen = current_time - memory.last_seen_time;
    
    if (time_since_seen < 10.0) {
        return MemoryLayer::RECENT_MEMORY;
    } else if (time_since_seen < 60.0) {
        return MemoryLayer::SHORT_MEMORY;
    } else {
        return MemoryLayer::LONG_MEMORY;
    }
}

void ParticleSystem::apply_memory_decay(MemoryParticle& memory, double current_time) {
    double time_since_seen = current_time - memory.last_seen_time;
    float decay_rate = 0.01f;
    
    memory.confidence -= decay_rate * time_since_seen;
    if (memory.confidence < 0.0f) {
        memory.confidence = 0.0f;
    }
    
    // Add position drift for uncertainty
    float drift_amount = (1.0f - memory.confidence) * 0.5f;
    memory.x = memory.original_x + (rand() / (float)RAND_MAX - 0.5f) * drift_amount;
    memory.y = memory.original_y + (rand() / (float)RAND_MAX - 0.5f) * drift_amount;
}

// Simple, clean particle creation methods
size_t ParticleSystem::create_static_particle(float x, float y, float z) {
    Particle p;  // Now safely initialized with defaults!
    p.x = x;
    p.y = y;
    p.z = z;
    // Velocities already 0 by default
    // Color already white by default
    return add_particle(p);  // FIXED: Use add_particle() to mark BVH dirty
}

size_t ParticleSystem::create_light(float x, float y, float z, float strength, 
                                    float r, float g, float b) {
    Particle p;  // Safe defaults
    p.x = x;
    p.y = y;
    p.z = z;
    p.r = r;
    p.g = g;
    p.b = b;
    p.size = 0.5f;  // Make light emitter smaller
    p.is_light_source = true;
    p.emission_strength = strength;
    // Use appropriate radius based on light strength
    auto& config = LightingConfig::get();
    if (strength >= config.light_sources.game_scale.stadium_light * 0.9f) {
        p.emission_radius = config.light_sources.radii.stadium_light;  // 200m for stadium lights
    } else if (strength >= config.light_sources.game_scale.street_light * 0.9f) {
        p.emission_radius = config.light_sources.radii.street_light;  // 50m for street lights
    } else {
        p.emission_radius = config.light_sources.radii.torch;  // 15m for smaller lights
    }
    p.reflectivity = 0.0f;  // Lights don't reflect
    return add_particle(p);  // FIXED: Use add_particle() to mark BVH dirty
}

// =============================================================================
// ENTITY-BOUND PARTICLE CREATION
// =============================================================================
// These methods create particles AND bind them to KG entities in one call.
// This ensures all particles are properly tracked for Entity BVH optimization.

int ParticleSystem::add_particle_to_entity(const Particle& p, kg::KGModule* kg, kg::EntityID entity) {
    // Bind the REAL entity id up front so add_particle's auto-create
    // branch does not fire. Before this, every KG-backed particle ALSO
    // minted an orphan "AutoParticle" entity that nothing ever
    // destroyed — per-frame particle churn (Logotron's run heads /
    // director orb) leaked ~180 entities/s and hit the KG's 500k
    // entity cap in seconds when running unthrottled, after which all
    // entity creation (including Director walls) failed.
    Particle bound = p;
    if (kg != nullptr && entity != kg::INVALID_ENTITY) {
        bound.entity_id = entity;
    }
    int render_idx = add_particle(bound);
    if (render_idx >= 0 && kg != nullptr && entity != kg::INVALID_ENTITY) {
        kg->createKGParticle(entity, static_cast<kg::RenderIndex>(render_idx));
    }
    return render_idx;
}

int ParticleSystem::create_light_for_entity(float x, float y, float z,
                                            float strength, float radius,
                                            float r, float g, float b,
                                            kg::KGModule* kg, kg::EntityID entity) {
    Particle p = {};
    p.x = x;
    p.y = y;
    p.z = z;
    p.r = r;
    p.g = g;
    p.b = b;
    p.size = 0.5f;
    p.is_light_source = true;
    p.emission_strength = strength;
    p.emission_radius = radius;
    p.reflectivity = 0.0f;
    p.SetMaterial(Materials::Type::LIGHT);  // Lights float

    return add_particle_to_entity(p, kg, entity);
}

size_t ParticleSystem::create_character_particle(float x, float y, float z) {
    Particle p;  // Safe defaults
    p.x = x;
    p.y = y;
    p.z = z;
    p.r = 0.0f;  // Blue-green character
    p.g = 0.8f;
    p.b = 0.6f;
    p.size = 1.5f;
    // Character particle (no type needed)
    p.entity_id = 1;
    return add_particle(p);  // FIXED: Use add_particle() to mark BVH dirty
}

std::vector<int> ParticleSystem::create_ground_particles(float center_x, float center_y,
                                                          float radius, int count,
                                                          const GroundParticleConfig& config) {
    std::vector<int> particle_ids;
    particle_ids.reserve(count);

    for (int i = 0; i < count; ++i) {
        // Jittered uniform distribution in circle
        // Use polar coordinates with uniform area distribution
        float angle = (i / (float)count) * 2.0f * M_PI;
        float r = radius * sqrtf(rand() / (float)RAND_MAX);  // sqrt for uniform area
        float x = center_x + r * cosf(angle);
        float y = center_y + r * sinf(angle);

        // Add variance to size, z-height, and color
        float size = config.base_size +
                     (rand() / (float)RAND_MAX - 0.5f) * config.size_variance;
        float z = (rand() / (float)RAND_MAX) * config.z_height_variance;

        float red = config.r + (rand() / (float)RAND_MAX - 0.5f) * config.color_variance;
        float green = config.g + (rand() / (float)RAND_MAX - 0.5f) * config.color_variance;
        float blue = config.b + (rand() / (float)RAND_MAX - 0.5f) * config.color_variance;

        // Clamp colors to valid range
        red = std::max(0.0f, std::min(1.0f, red));
        green = std::max(0.0f, std::min(1.0f, green));
        blue = std::max(0.0f, std::min(1.0f, blue));

        // Create particle
        Particle p;
        p.x = x;
        p.y = y;
        p.z = z;
        p.size = size;
        p.r = red;
        p.g = green;
        p.b = blue;

        particle_ids.push_back(add_particle(p));
    }

    return particle_ids;
}

std::vector<int> ParticleSystem::create_floor_grid(float center_x, float center_y,
                                                     int tiles_x, int tiles_y,
                                                     const FloorTileConfig& config) {
    std::vector<int> particle_ids;
    particle_ids.reserve(tiles_x * tiles_y);

    // Calculate starting position (bottom-left corner of grid)
    float start_x = center_x - (tiles_x * config.tile_width) / 2.0f + config.tile_width / 2.0f;
    float start_y = center_y - (tiles_y * config.tile_height) / 2.0f + config.tile_height / 2.0f;

    for (int ty = 0; ty < tiles_y; ++ty) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            // Calculate tile position
            float tile_x = start_x + tx * config.tile_width;
            float tile_y = start_y + ty * config.tile_height;

            // Organic color variation using multi-octave Perlin noise
            // Coarse noise (large patches ~8m) + fine noise (small detail ~2m)
            float coarse = noise::perlin_noise_2d(tile_x * 0.12f, tile_y * 0.12f) * 0.7f;
            float fine = noise::perlin_noise_2d(tile_x * 0.4f, tile_y * 0.4f) * 0.3f;
            float tile_noise = coarse + fine;  // Range: [-1, 1]

            // Apply noise with different multipliers per channel for natural earthy tones
            float red = config.r + tile_noise * config.color_variance;
            float green = config.g + tile_noise * config.color_variance * 0.8f;
            float blue = config.b + tile_noise * config.color_variance * 0.5f;

            // Clamp colors to valid range
            red = std::max(0.0f, std::min(1.0f, red));
            green = std::max(0.0f, std::min(1.0f, green));
            blue = std::max(0.0f, std::min(1.0f, blue));

            // Create flat tile particle
            Particle p;
            p.shape = ParticleShape::BOX;
            p.x = tile_x;
            p.y = tile_y;
            p.z = config.tile_thickness / 2.0f;  // Center tile at half-thickness above ground
            p.width = config.tile_width;
            p.height = config.tile_height;
            p.thickness = config.tile_thickness;
            p.r = red;
            p.g = green;
            p.b = blue;
            p.reflectivity = 0.3f;  // Tiles reflect some light

            // Floor tiles use STONE, not HEAVY_STATIC. Same strata convention as
            // StrataFloorGenerator: real material density (2500 kg/m³), tiles
            // spawn is_at_rest=true on the turtle, solver wakes them if
            // disturbed. See the awake-onto-at-rest notes for the
            // rationale behind retiring HEAVY_STATIC.
            p.SetMaterial(Materials::Type::STONE);
            p.is_at_rest = true;

            particle_ids.push_back(add_particle(p));
        }
    }

    return particle_ids;
}

size_t ParticleSystem::queue_light(float x, float y, float z, float strength, float radius,
                                   float r, float g, float b) {
    // Create a light particle and queue it for thread-safe addition
    Particle p;
    p.x = x;
    p.y = y;
    p.z = z;
    p.r = r;
    p.g = g;
    p.b = b;
    p.size = 0.5f;  // Light sources are visually small
    p.is_light_source = true;
    p.emission_strength = strength;
    p.emission_radius = radius;  // Caller specifies the radius
    p.reflectivity = 0.0f;  // Lights don't reflect
    p.SetMaterial(Materials::Type::LIGHT);  // density=0, floats (mass auto-calc to 0)

    return queue_particle_addition(p);
}

// DEPRECATED - kept for compatibility but should not be used
void ParticleSystem::create_test_particle() {
    Particle p;  // Now has safe defaults instead of random memory!
    p.x = (rand() / (float)RAND_MAX - 0.5f) * 40.0f;
    p.y = (rand() / (float)RAND_MAX - 0.5f) * 30.0f;
    // Don't set random velocities anymore - defaults to 0!
    p.r = rand() / (float)RAND_MAX;
    p.g = rand() / (float)RAND_MAX;
    p.b = rand() / (float)RAND_MAX;
    p.size = 1.0f + (rand() / (float)RAND_MAX) * 2.0f;
    // Basic particle
    
    add_particle(p);  // FIXED: Use add_particle() to mark BVH dirty
}

void ParticleSystem::create_serpent_chain(int num_particles) {
    float start_x = -20.0f;
    float start_y = 0.0f;
    float segment_length = 1.5f;
    
    for (int i = 0; i < num_particles; i++) {
        Particle p;
        p.x = start_x + i * segment_length;
        p.y = start_y + sin(i * 0.5f) * 3.0f;
        p.z = 0.0f;  // Initialize Z
        p.vx = 2.0f;
        p.vy = 0.0f;
        p.vz = 0.0f;  // Initialize VZ
        p.r = 0.0f;
        p.g = 0.8f;
        p.b = 0.0f;
        p.a = 1.0f;
        p.size = 1.2f;
        // Sphere particle
        
        add_particle(p);  // FIXED: Use add_particle() to mark BVH dirty
    }
}

void ParticleSystem::create_wall(float start_x, float start_y, float end_x, float end_y, float spacing) {
    // Create vertical walls extending up from ground
    // Walls now have height in the Z dimension
    float dx = end_x - start_x;
    float dy = end_y - start_y;
    float length = sqrtf(dx * dx + dy * dy);
    int num_particles_horizontal = static_cast<int>(length / spacing) + 1;
    int num_particles_vertical = 5;  // Make walls 5 particles tall
    float wall_height = 4.0f;  // Total wall height in world units
    
    for (int i = 0; i < num_particles_horizontal; i++) {
        float t = i / static_cast<float>(num_particles_horizontal - 1);
        float base_x = start_x + dx * t;
        float base_y = start_y + dy * t;
        
        // Create vertical stack of particles for wall height
        for (int j = 0; j < num_particles_vertical; j++) {
            float z = (j / static_cast<float>(num_particles_vertical - 1)) * wall_height;
            
            Particle wall_particle;
            wall_particle.x = base_x;
            wall_particle.y = base_y;
            wall_particle.z = z;  // Wall extends upward from ground
            wall_particle.vx = 0.0f;
            wall_particle.vy = 0.0f;
            wall_particle.vz = 0.0f;
            
            // Darken color slightly with height for depth perception
            float brightness = 0.6f - (z / wall_height) * 0.2f;
            wall_particle.r = brightness;
            wall_particle.g = brightness;
            wall_particle.b = brightness * 1.2f;  // Slightly bluish
            wall_particle.a = 1.0f;
            wall_particle.size = spacing * 0.8f;  // Slightly smaller for better visibility
            // Wall particle
            
            add_particle(wall_particle);  // FIXED: Use add_particle() to mark BVH dirty
        }
    }
}

void ParticleSystem::create_room(float center_x, float center_y, float width, float height) {
    float half_width = width / 2.0f;
    float half_height = height / 2.0f;
    
    create_wall(center_x - half_width, center_y - half_height,
                center_x + half_width, center_y - half_height);
    create_wall(center_x + half_width, center_y - half_height,
                center_x + half_width, center_y + half_height);
    create_wall(center_x + half_width, center_y + half_height,
                center_x - half_width, center_y + half_height);
    create_wall(center_x - half_width, center_y + half_height,
                center_x - half_width, center_y - half_height);
}

// NOTE: Particle::GetSurfaces() and Particle::GetShadowTriangles() now live
// in src/particle_shape_methods.cpp so the pure-geometry path compiles into
// the headless build profile without dragging in ParticleSystem.

void ParticleSystem::create_forest_cluster(float center_x, float center_y, int tree_count) {
    for (int i = 0; i < tree_count; i++) {
        float angle = (i / static_cast<float>(tree_count)) * 2.0f * M_PI;
        float radius = 5.0f + (rand() / (float)RAND_MAX) * 3.0f;
        
        Particle tree;
        tree.x = center_x + cos(angle) * radius;
        tree.y = center_y + sin(angle) * radius;
        tree.z = 0.0f;  // Initialize Z - trees are on ground
        tree.vx = 0.0f;
        tree.vy = 0.0f;
        tree.vz = 0.0f;  // Initialize VZ
        tree.r = 0.0f;
        tree.g = 0.4f + (rand() / (float)RAND_MAX) * 0.2f;
        tree.b = 0.0f;
        tree.a = 1.0f;
        tree.size = 2.0f + (rand() / (float)RAND_MAX) * 1.0f;
        // Tree particle
        // TODO[ARCH-038]: Trees will be fractally constructed from multiple cube particles
        
        add_particle(tree);  // FIXED: Use add_particle() to mark BVH dirty
    }
}

// Surface query method implementation

// Simple inline vector operations for better performance and clarity
namespace {
    inline float dot3(float ax, float ay, float az, float bx, float by, float bz) {
        return ax * bx + ay * by + az * bz;
    }
    
    inline float length3(float x, float y, float z) {
        return sqrtf(x * x + y * y + z * z);
    }
    
    inline void normalize3(float& x, float& y, float& z) {
        float len = length3(x, y, z);
        if (len > 0.001f) {
            float inv_len = 1.0f / len;
            x *= inv_len;
            y *= inv_len;
            z *= inv_len;
        }
    }
}

void ParticleSystem::get_surface_normal(size_t particle_index, size_t surface_index,
                                       float& nx, float& ny, float& nz) const {
    nx = 0; ny = 0; nz = 0;  // Default
    
    if (particle_index >= particles.size()) return;
    
    const Particle& p = particles[particle_index];
    auto surfaces = p.GetSurfaces();
    
    if (surface_index >= surfaces.size()) return;
    
    // Get the normal from the surface
    nx = surfaces[surface_index].nx;
    ny = surfaces[surface_index].ny;
    nz = surfaces[surface_index].nz;
}

int ParticleSystem::find_surface_by_normal(size_t particle_index, 
                                          float nx, float ny, float nz) const {
    if (particle_index >= particles.size()) return -1;
    
    const Particle& p = particles[particle_index];
    auto surfaces = p.GetSurfaces();
    
    // Normalize the input direction
    normalize3(nx, ny, nz);
    if (length3(nx, ny, nz) < 0.001f) return -1;
    
    // Find surface with maximum dot product (most aligned)
    int best_surface = -1;
    float best_dot = -1.0f;
    
    for (size_t i = 0; i < surfaces.size(); i++) {
        float dot = dot3(surfaces[i].nx, surfaces[i].ny, surfaces[i].nz, nx, ny, nz);
        
        // We want alignment > 0.9 (within ~25 degrees)
        if (dot > 0.9f && dot > best_dot) {
            best_dot = dot;
            best_surface = i;
        }
    }

    return best_surface;
}

// ============================================================================
// SURFACE-BASED PARTICLE PLACEMENT
// ============================================================================

int ParticleSystem::create_particle_at_surface(
    int target_particle_id,
    float surface_normal_x, float surface_normal_y, float surface_normal_z,
    float offset_x, float offset_y, float offset_z,
    const Particle& new_particle_config)
{
    std::shared_lock<std::shared_mutex> lock(particles_mutex_);

    // Validate target particle
    if (target_particle_id < 0 || static_cast<size_t>(target_particle_id) >= particles.size()) {
        std::cerr << "[ParticleSystem] create_particle_at_surface: invalid target_particle_id "
                  << target_particle_id << std::endl;
        return -1;
    }

    const Particle& target = particles[target_particle_id];

    // Find the surface that matches the given normal
    int surface_idx = find_surface_by_normal(target_particle_id,
                                             surface_normal_x, surface_normal_y, surface_normal_z);
    if (surface_idx < 0) {
        std::cerr << "[ParticleSystem] create_particle_at_surface: no surface matches normal ("
                  << surface_normal_x << "," << surface_normal_y << "," << surface_normal_z << ")"
                  << std::endl;
        return -1;
    }

    // Get the surfaces and extract the matching one's world-space center
    auto surfaces = target.GetSurfaces();
    if (static_cast<size_t>(surface_idx) >= surfaces.size()) {
        std::cerr << "[ParticleSystem] create_particle_at_surface: surface_idx out of range" << std::endl;
        return -1;
    }

    const Surface& surface = surfaces[surface_idx];

    // GetSurfaces returns TRIANGLE centers, not FACE centers
    // Calculate proper face center from particle geometry using the surface normal
    float half_w = target.width * 0.5f;
    float half_h = target.height * 0.5f;
    float half_t = target.thickness * 0.5f;

    // Determine which face based on dominant normal component
    float surface_center_x, surface_center_y, surface_center_z;
    float abs_nx = std::abs(surface.nx);
    float abs_ny = std::abs(surface.ny);
    float abs_nz = std::abs(surface.nz);

    if (abs_nx > abs_ny && abs_nx > abs_nz) {
        // X face: center at (particle.x ± half_w, particle.y, particle.z)
        surface_center_x = target.x + (surface.nx > 0 ? half_w : -half_w);
        surface_center_y = target.y;
        surface_center_z = target.z;
    } else if (abs_ny > abs_nz) {
        // Y face: center at (particle.x, particle.y ± half_h, particle.z)
        surface_center_x = target.x;
        surface_center_y = target.y + (surface.ny > 0 ? half_h : -half_h);
        surface_center_z = target.z;
    } else {
        // Z face: center at (particle.x, particle.y, particle.z ± half_t)
        surface_center_x = target.x;
        surface_center_y = target.y;
        surface_center_z = target.z + (surface.nz > 0 ? half_t : -half_t);
    }

    // Apply offset (in world coordinates)
    float final_x = surface_center_x + offset_x;
    float final_y = surface_center_y + offset_y;
    float final_z = surface_center_z + offset_z;

    // MILLIMETER PRECISION DEBUG: Log ALL surfaces and calculations
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  create_particle_at_surface DEBUG                            ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << "  TARGET particle_id=" << target_particle_id << std::endl;
    std::cout << "  │ pos=(" << target.x << ", " << target.y << ", " << target.z << ")" << std::endl;
    std::cout << "  │ size=(w=" << target.width << ", h=" << target.height << ", t=" << target.thickness << ")" << std::endl;
    std::cout << "  │ half_extents=(w=" << half_w << ", h=" << half_h << ", t=" << half_t << ")" << std::endl;
    std::cout << "  │ rotation=(" << target.rotation_x << ", " << target.rotation_y << ", " << target.rotation_z << ")" << std::endl;
    std::cout << "  │" << std::endl;
    std::cout << "  │ REQUESTED normal=(" << surface_normal_x << ", " << surface_normal_y << ", " << surface_normal_z << ")" << std::endl;
    std::cout << "  │" << std::endl;
    std::cout << "  │ All " << surfaces.size() << " triangle surfaces:" << std::endl;
    for (size_t s = 0; s < surfaces.size(); s++) {
        float dot = surfaces[s].nx * surface_normal_x + surfaces[s].ny * surface_normal_y + surfaces[s].nz * surface_normal_z;
        std::cout << "  │   [" << s << "] tri_center=(" << surfaces[s].x << ", " << surfaces[s].y << ", " << surfaces[s].z << ")"
                  << " normal=(" << surfaces[s].nx << ", " << surfaces[s].ny << ", " << surfaces[s].nz << ")"
                  << " dot=" << dot << (s == (size_t)surface_idx ? " <<<SELECTED" : "")
                  << std::endl;
    }
    std::cout << "  │" << std::endl;
    std::cout << "  │ FACE CENTER CALCULATION:" << std::endl;
    std::cout << "  │   abs_n=(" << abs_nx << ", " << abs_ny << ", " << abs_nz << ")" << std::endl;
    if (abs_nx > abs_ny && abs_nx > abs_nz) {
        std::cout << "  │   Dominant: X face (normal.x=" << surface.nx << ")" << std::endl;
        std::cout << "  │   face_center_x = target.x + " << (surface.nx > 0 ? "+" : "-") << "half_w = " << target.x << " + " << (surface.nx > 0 ? half_w : -half_w) << " = " << surface_center_x << std::endl;
        std::cout << "  │   face_center_y = target.y = " << surface_center_y << std::endl;
        std::cout << "  │   face_center_z = target.z = " << surface_center_z << std::endl;
    } else if (abs_ny > abs_nz) {
        std::cout << "  │   Dominant: Y face (normal.y=" << surface.ny << ")" << std::endl;
        std::cout << "  │   face_center_x = target.x = " << surface_center_x << std::endl;
        std::cout << "  │   face_center_y = target.y + " << (surface.ny > 0 ? "+" : "-") << "half_h = " << target.y << " + " << (surface.ny > 0 ? half_h : -half_h) << " = " << surface_center_y << std::endl;
        std::cout << "  │   face_center_z = target.z = " << surface_center_z << std::endl;
    } else {
        std::cout << "  │   Dominant: Z face (normal.z=" << surface.nz << ")" << std::endl;
        std::cout << "  │   face_center_x = target.x = " << surface_center_x << std::endl;
        std::cout << "  │   face_center_y = target.y = " << surface_center_y << std::endl;
        std::cout << "  │   face_center_z = target.z + " << (surface.nz > 0 ? "+" : "-") << "half_t = " << target.z << " + " << (surface.nz > 0 ? half_t : -half_t) << " = " << surface_center_z << std::endl;
    }
    std::cout << "  │" << std::endl;
    std::cout << "  │ FINAL CALCULATION:" << std::endl;
    std::cout << "  │   face_center=(" << surface_center_x << ", " << surface_center_y << ", " << surface_center_z << ")" << std::endl;
    std::cout << "  │   offset=(" << offset_x << ", " << offset_y << ", " << offset_z << ")" << std::endl;
    std::cout << "  │   final_pos = face_center + offset" << std::endl;
    std::cout << "  └─► RESULT: (" << final_x << ", " << final_y << ", " << final_z << ")" << std::endl;
    std::cout << std::endl;

    // Release read lock before adding particle (add_particle takes write lock)
    lock.unlock();

    // Create new particle with calculated position
    Particle new_particle = new_particle_config;
    new_particle.x = final_x;
    new_particle.y = final_y;
    new_particle.z = final_z;

    return add_particle(new_particle);
}

// ============================================================================
// TEST HELPERS: Safe, deadlock-proof particle access
// ============================================================================

void ParticleSystem::delete_particle_immediate(int particle_id) {
    // Queue for deletion with frame 0 (immediate)
    queue_particle_deletion(particle_id, 0);

    // Flush with high frame number to bypass 3-frame safety window
    // This is safe for tests (no GPU rendering) but exposes implementation detail
    flush_safe_deletions(9999);
}

void ParticleSystem::delete_particles_immediate(const std::vector<int>& particle_ids) {
    // Queue all deletions first
    for (int id : particle_ids) {
        queue_particle_deletion(id, 0);
    }

    // Single flush at end (more efficient than per-particle)
    flush_safe_deletions(9999);
}

float ParticleSystem::get_particle_z(int particle_id) const {
    // Lock acquired, extracted, and released automatically
    auto particles_view = lock_particles_for_read();

    // Bounds check
    if (particle_id < 0 || particle_id >= static_cast<int>(particles_view.size())) {
        return 0.0f;
    }

    return particles_view[particle_id].z;
}  // Lock released here via RAII

float ParticleSystem::get_particle_vz(int particle_id) const {
    auto particles_view = lock_particles_for_read();

    if (particle_id < 0 || particle_id >= static_cast<int>(particles_view.size())) {
        return 0.0f;
    }

    return particles_view[particle_id].vz;
}

ParticleSystem::ParticleSnapshot ParticleSystem::get_particle_snapshot(int particle_id) const {
    auto particles_view = lock_particles_for_read();

    // Bounds check
    if (particle_id < 0 || particle_id >= static_cast<int>(particles_view.size())) {
        return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    }

    const Particle& p = particles_view[particle_id];

    // Extract all values while lock is held
    return {
        p.x, p.y, p.z,
        p.vx, p.vy, p.vz,
        p.GetMass()
    };
}  // Lock released here via RAII

// ============================================================================
// COLLISION CHECK FOR GENERATORS
// ============================================================================

bool ParticleSystem::can_place_at(float x, float y, float z,
                                   float w, float h, float t,
                                   float gap) const {
    return can_place_at_ignoring(x, y, z, w, h, t, gap, -1, 0.0f);
}

bool ParticleSystem::can_place_at_ignoring(float x, float y, float z,
                                   float w, float h, float t,
                                   float gap, int ignore_id, float facing_angle) const {
    // Lock for thread-safe read access
    auto particles_view = lock_particles_for_read();

    // Proposed box half-extents, WIDENED FOR ROTATION.
    //
    // This used to take width/height as-is, which silently under-measures any
    // particle that is turned. A leaf is a thin plate 0.35 x 0.245 m with a
    // random facing_angle: rotate it a quarter turn and its true reach along Y
    // is 0.35, not the 0.245 this was comparing. Overlaps were therefore missed
    // at creation and found later by the solver, which resolves them the only
    // way it can, by pushing (issue #38, leaf pairs 73-80 and 74-81 at 6 cm).
    //
    // The standard conservative bound for a box turned by theta about Z:
    //   ex = hx|cos| + hy|sin|,  ey = hx|sin| + hy|cos|
    // It never under-estimates, so a pass here is a real pass. It can
    // over-estimate for a rotated box, which costs a retry, never a miss.
    const float ca = std::fabs(std::cos(facing_angle));
    const float sa = std::fabs(std::sin(facing_angle));
    float half_w = (w * 0.5f) * ca + (h * 0.5f) * sa;
    float half_h = (w * 0.5f) * sa + (h * 0.5f) * ca;
    float half_t = t * 0.5f;

    // Check against all existing particles (skip lights - they don't collide)
    for (size_t i = 0; i < particles_view.size(); ++i) {
        const Particle& p = particles_view[i];
        if (p.is_light_source) continue;
        if (ignore_id >= 0 && i == (size_t)ignore_id) continue;   // the body we bond to

        // Get existing particle's half-extents
        float p_half_w, p_half_h, p_half_t;
        if (p.shape == ParticleShape::BOX) {
            // Same rotation widening for the body already in the world.
            const float pca = std::fabs(std::cos(p.facing_angle));
            const float psa = std::fabs(std::sin(p.facing_angle));
            p_half_w = (p.width * 0.5f) * pca + (p.height * 0.5f) * psa;
            p_half_h = (p.width * 0.5f) * psa + (p.height * 0.5f) * pca;
            p_half_t = p.thickness * 0.5f;
        } else {
            // Cube/sphere: use size for all dimensions
            p_half_w = p_half_h = p_half_t = p.size * 0.5f;
        }

        // AABB overlap check with gap
        // Two boxes overlap if their extents overlap on ALL three axes
        // With gap: extend each box by gap/2 on each side
        float gap_half = gap * 0.5f;

        // Separation on each axis (positive = separated, negative = overlapping)
        float sep_x = std::abs(x - p.x) - (half_w + p_half_w + gap);
        float sep_y = std::abs(y - p.y) - (half_h + p_half_h + gap);
        float sep_z = std::abs(z - p.z) - (half_t + p_half_t + gap);

        // Overlap exists if separated on NONE of the axes (all separations negative)
        if (sep_x < 0.0f && sep_y < 0.0f && sep_z < 0.0f) {
            return false;  // Collision detected
        }
    }

    return true;  // No collision
}

bool ParticleSystem::try_place_with_retry(Particle& particle, int max_attempts, float jitter_range, float gap) {
    // Get particle dimensions
    float w, h, t;
    if (particle.shape == ParticleShape::BOX) {
        w = particle.width;
        h = particle.height;
        t = particle.thickness;
    } else {
        w = h = t = particle.size;
    }

    // Save original position
    float orig_x = particle.x;
    float orig_y = particle.y;
    float orig_z = particle.z;

    // Try original position first
    if (can_place_at_ignoring(particle.x, particle.y, particle.z, w, h, t, gap, -1,
                              particle.facing_angle)) {
        return true;
    }

    // Try jittered positions
    // Use a simple LCG for jitter, seeded by position + particle count for uniqueness
    // The particle count ensures each new particle gets different jitter even at same position
    unsigned int seed = static_cast<unsigned int>(
        std::abs(orig_x * 1000.0f) + std::abs(orig_y * 100.0f) + std::abs(orig_z * 10.0f)
        + particles.size() * 7919  // Prime multiplier for better distribution
    );

    for (int attempt = 1; attempt < max_attempts; ++attempt) {
        // LCG random
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        float rx = ((seed % 1000) / 500.0f - 1.0f) * jitter_range;

        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        float ry = ((seed % 1000) / 500.0f - 1.0f) * jitter_range;

        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        float rz = ((seed % 1000) / 500.0f - 1.0f) * jitter_range * 0.5f;  // Less Z jitter

        particle.x = orig_x + rx;
        particle.y = orig_y + ry;
        particle.z = orig_z + rz;

        // Keep Z positive (above ground)
        if (particle.z < t * 0.5f) {
            particle.z = t * 0.5f;
        }

        if (can_place_at_ignoring(particle.x, particle.y, particle.z, w, h, t, gap, -1,
                                  particle.facing_angle)) {
            return true;
        }
    }

    // All attempts failed - restore original position
    particle.x = orig_x;
    particle.y = orig_y;
    particle.z = orig_z;

    return false;
}