#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <vector>
#include <deque>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <map>
#include <string>
#include "particle.h"
#include "logosphere/physics/bvh.h"
#include "logosphere/physics/creation_door.h"

// Forward declarations
namespace kg {
    class KGModule;
}

// Include kg_types for EntityID (needed for entity-bound API)
#include "logosphere/kg/kg_types.h"
class PhysicsSystem;
class ParticleDynamicsSystem;

// Ground particle configuration (defined outside class to avoid default member initializer issues)
struct GroundParticleConfig {
    float base_size = 0.5f;
    float size_variance = 0.2f;
    float z_height_variance = 0.1f;
    float r = 0.4f, g = 0.35f, b = 0.25f;  // Earth brown
    float color_variance = 0.1f;
};

// Floor tile configuration for creating grid-based floor surfaces
struct FloorTileConfig {
    float tile_width = 1.0f;           // Tile width (X dimension)
    float tile_height = 1.0f;          // Tile height (Y dimension)
    float tile_thickness = 0.1f;       // Tile thickness (Z dimension)
    float r = 0.5f, g = 0.5f, b = 0.5f;  // Gray concrete
    float color_variance = 0.05f;      // Slight color variation per tile
};

// EDUCATIONAL NOTE: Module Design in C++
// In Python, we'd create a class or module to encapsulate related functionality
// In C++, we use a combination of classes and namespaces
// 
// This header file is like a Python module's __init__.py - it declares
// what functions and types are available to users of this module
//
// The implementation (particle_system.cpp) is like the module's actual code

class ParticleSystem {
public:
    // =========================================================================
    // THREAD-SAFE PARTICLE ACCESS: Locked View Pattern
    // =========================================================================
    // Never exposes raw deque or requires manual lock/unlock.
    // Locks acquired automatically, released when view destroyed (RAII).

    // ReadView: Shared lock for read-only access (multiple readers allowed)
    class ReadView {
        std::shared_lock<std::shared_mutex> lock_;
        const std::vector<Particle>& particles_;
    public:
        ReadView(std::shared_mutex& mutex, const std::vector<Particle>& particles)
            : lock_(mutex), particles_(particles) {}

        size_t size() const { return particles_.size(); }
        bool empty() const { return particles_.empty(); }
        const Particle& operator[](size_t i) const { return particles_[i]; }
        const std::vector<Particle>& get() const { return particles_; }

        // Iterator support for range-for loops
        auto begin() const { return particles_.begin(); }
        auto end() const { return particles_.end(); }
    };

    // WriteView: Exclusive lock for write access (single writer only)
    class WriteView {
        std::unique_lock<std::shared_mutex> lock_;
        std::vector<Particle>& particles_;
    public:
        WriteView(std::shared_mutex& mutex, std::vector<Particle>& particles)
            : lock_(mutex), particles_(particles) {}

        size_t size() const { return particles_.size(); }
        bool empty() const { return particles_.empty(); }
        Particle& operator[](size_t i) { return particles_[i]; }

        // Get underlying vector (needed for BVH queries and other algorithms)
        const std::vector<Particle>& get_particles() const { return particles_; }
        std::vector<Particle>& get_particles() { return particles_; }
    };

    // Constructor and destructor
    ParticleSystem();
    ~ParticleSystem();

    // NEW SAFE API: Locked views (use these instead of lock_for_read/write + get_particles)
    ReadView lock_particles_for_read() const {
        return ReadView(particles_mutex_, particles);
    }

    WriteView lock_particles_for_write() {
        return WriteView(particles_mutex_, particles);
    }

    // Quick thread-safe query (no view needed)
    Particle get_particle_copy(size_t index) const {
        std::shared_lock<std::shared_mutex> lock(particles_mutex_);
        return (index < particles.size()) ? particles[index] : Particle{};
    }

    // Particle management
    uint32_t allocate_particle_id();  // Get unique particle_id for KG storage (thread-safe)
    void clear_particles();
    
    // Deferred particle addition for thread safety
    // TODO[ARCH-003]: Consider double-buffering if deferred addition causes gameplay issues
    // Queues a particle and returns the index it will take once flushed, or
    // -1 when THE CREATION DOOR refuses it (INV-37). The door stands here as
    // well as at add_particle so a refused body is never counted in a
    // prediction: the index this returns is computed from the live count plus
    // the queue length, so a body dropped later would silently shift every
    // prediction handed out after it (GEDANKEN-69).
    int queue_particle_addition(const Particle& particle);
    void flush_pending_particles();  // Apply all queued additions (call between frames)

    // =========================================================================
    // THE CREATION DOOR (INV-37) — nothing is born inside anything
    // =========================================================================
    // Owner decree 2026-09-01. The refusal itself lives in add_particle, at
    // the push_back every birth crosses; the contract and the reasoning are in
    // include/logosphere/physics/creation_door.h.
    //
    // A refused creation returns the invalid id -1 and leaves the world
    // untouched. The reason is readable here, by the caller that asked.
    const logosphere::CreationRefusal& last_creation_refusal() const {
        return last_refusal_;
    }
    const logosphere::CreationDoorStats& creation_door_stats() const {
        return door_stats_;
    }
    // The census: every refusal grouped by the recipe that produced it, with
    // the door's own accumulated cost. Printed once at destruction when the
    // door refused anything, and callable on demand.
    void report_creation_door() const;

    // A BODY IS BORN WITH ITS MATERIAL (INV-38, owner ruling 2026-09-03).
    // Every birth that crosses add_particle without SetMaterial is counted,
    // printed once (capped) and censused by recipe at shutdown - the door's
    // playbook, without the refusal (that enforcement is the owner's call).
    size_t births_without_material() const { return births_unnamed_; }
    void report_unnamed_births() const;
    // A PROMISE IS KEPT OR IT IS LOUD (night 2026-09-04, journal 17).
    // queue_particle_addition hands back a predicted index (live + pending);
    // a DIRECT add_particle before the flush takes that index for itself and
    // every promise handed out after it points at a stranger (Eden's spirit
    // lights orbiting a trunk and a branch at 45 m/s). Counted here, reported
    // at shutdown like INV-38's census; the guard is the next step.
    size_t promise_breaks() const { return promise_breaks_; }
    void report_promise_breaks() const;

    // THE DOOR'S MEASURE, offered to the sleep law (G-72): the deepest overlap
    // `probe` has with any live body other than `exclude` (pass the probe's
    // own index when it is a shifted copy of a live body). 0.0 when it meets
    // nothing. Same index, same narrow phase as the refusal, so "supported"
    // and "overlapping" are one geometry.
    float overlap_depth(const Particle& probe, int exclude);

    // Deferred particle deletion for GPU triple buffering safety
    // Queue deletions with frame number, flush when GPU has finished with those frames
    void queue_particle_deletion(size_t index, int current_frame_number);
    void flush_safe_deletions(int current_frame_number);
    bool has_pending_deletions() const { return !pending_deletions_.empty(); }
    bool has_ready_deletions(int current_frame_number) const;

    void remove_particle(size_t index);

    // =========================================================================
    // TEST HELPERS: Safe, deadlock-proof particle access
    // =========================================================================
    // These methods eliminate common test pitfalls:
    // 1. Automatic lock management (no manual scoping needed)
    // 2. Deadlock-proof (lock released before return)
    // 3. Hide GPU triple-buffering details (no magic numbers)

    // Snapshot structure for safe multi-value extraction
    struct ParticleSnapshot {
        float x, y, z;
        float vx, vy, vz;
        float mass;
    };

    // Safe immediate deletion for tests (hides GPU buffering details)
    void delete_particle_immediate(int particle_id);
    void delete_particles_immediate(const std::vector<int>& particle_ids);

    // Safe single-value extraction (deadlock-proof)
    float get_particle_z(int particle_id) const;
    float get_particle_vz(int particle_id) const;

    // Safe multi-value extraction (deadlock-proof)
    ParticleSnapshot get_particle_snapshot(int particle_id) const;

    // NOTE: the old `update(double)` universal x/y Euler loop was deleted
    // 2026-07-09 — position authority is per-owner (solver / dynamics /
    // FK). See tests/test_position_authority.cpp.

    // Query functions (THREAD-SAFE)
    size_t count() const {
        std::shared_lock<std::shared_mutex> lock(particles_mutex_);
        return particles.size();
    }

    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(particles_mutex_);
        return particles.empty();
    }

    // Entity→particle index mapping (for collect_surfaces fast iteration)
    // Returns empty vector if entity_id out of range
    const std::vector<int>& get_entity_particle_indices(kg::EntityID entity_id) const {
        static const std::vector<int> empty;
        if (entity_id >= entity_particle_indices_.size()) return empty;
        return entity_particle_indices_[entity_id];
    }

    // Get the full mapping (for iteration over visible entities)
    const std::vector<std::vector<int>>& get_all_entity_particle_indices() const {
        return entity_particle_indices_;
    }

    // =========================================================================
    // COLLISION CHECK FOR GENERATORS (Thread-safe)
    // =========================================================================
    // Check if placing a particle at given position would overlap existing particles.
    // Used by generators to ensure no overlapping particles are created.
    // gap: minimum separation between particle surfaces (default 2cm for physics stability)
    // THE ONE OVERLAP PREDICATE (INV-12, INV-37). How deep this body would
    // be inside the deepest thing it touches, through the engine's own narrow
    // phase - the same verdict the creation door will reach, so a generator
    // that asks this and acts on the answer is never refused for a placement
    // it was told was legal. Returns 0 when clear or merely touching.
    // `ignore_index` is the body this one is about to be BONDED to (-1 for
    // none); `out_blocker` receives the live index it hit.
    float deepest_overlap(const Particle& probe, int ignore_index = -1,
                          int* out_blocker = nullptr);

    bool can_place_at(float x, float y, float z, float w, float h, float t, float gap = 0.02f);

    // Same question, but ignoring one body: the thing this one is about to be
    // BONDED to. A child branch is meant to touch its parent end to end, so
    // counting the parent as a collision would reject every legal structure.
    // `ignore_id` is that parent; pass -1 for none.
    //
    // Exists because branches were never collision-checked at all (issue #38).
    // Leaves had try_place_with_retry; branches were placed structurally and
    // whatever they grew into was simply left overlapping for the solver to
    // find, which it does, by throwing them apart.
    // `facing_angle` widens the proposed box to its rotated bound, so a turned
    // particle is not measured as if it were axis-aligned. Existing bodies are
    // widened by their own facing_angle automatically.
    bool can_place_at_ignoring(float x, float y, float z, float w, float h, float t,
                               float gap, int ignore_id, float facing_angle = 0.0f);

    // Try to place a particle with retry. Jitters position if overlap detected.
    // Returns true if placement succeeded, false if all attempts failed.
    // On success, particle position is updated to valid location.
    // gap: minimum separation between surfaces (default 2cm, use larger for thin particles like leaves)
    bool try_place_with_retry(Particle& particle, int max_attempts = 10, float jitter_range = 0.5f, float gap = 0.02f);

    // Spatial memory system
    void update_spatial_memory(double current_time);
    void add_to_spatial_memory(const Particle& particle, double current_time);
    const std::vector<MemoryParticle>& get_spatial_memory() const { return spatial_memory; }
    void clear_spatial_memory() { spatial_memory.clear(); }
    
    // Simple, clean particle creation methods
    // These return the index of the created particle for further customization
    size_t create_static_particle(float x, float y, float z = 0.0f);
    size_t create_light(float x, float y, float z, float strength,
                       float r = 1.0f, float g = 1.0f, float b = 1.0f);
    size_t create_character_particle(float x, float y, float z = 0.0f);

    // Create organic ground particles in a circular area
    std::vector<int> create_ground_particles(float center_x, float center_y,
                                              float radius, int count,
                                              const GroundParticleConfig& config = GroundParticleConfig());

    // Create floor grid of flat tile particles
    std::vector<int> create_floor_grid(float center_x, float center_y,
                                        int tiles_x, int tiles_y,
                                        const FloorTileConfig& config = FloorTileConfig());

    // Thread-safe queued versions (use these during gameplay)
    size_t queue_light(float x, float y, float z, float strength, float radius,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f);
    
    // Legacy utility functions (to be refactored)
    void create_test_particle();  // DEPRECATED: Creates random particle - don't use!
    void create_serpent_chain(int num_particles);
    void create_wall(float start_x, float start_y, float end_x, float end_y, float spacing = 1.0f);
    void create_room(float center_x, float center_y, float width, float height);
    void create_forest_cluster(float center_x, float center_y, int tree_count = 5);

    // =========================================================================
    // ENTITY-BOUND PARTICLE CREATION (Preferred API)
    // =========================================================================
    // All particles MUST be bound to a KG entity for proper Entity BVH grouping.
    // Unbound particles (entity 0) degrade shadow ray performance.
    //
    // Pattern: Create entity first, then create particles bound to it.
    //   kg::EntityID entity = kg.createEntity("my_type");
    //   ps.add_particle_to_entity(p, &kg, entity);

    // Create single particle bound to entity
    // Returns render index (particle ID in particle system)
    int add_particle_to_entity(const Particle& p, kg::KGModule* kg, kg::EntityID entity);

    // Create light source bound to entity
    int create_light_for_entity(float x, float y, float z,
                                float strength, float radius,
                                float r, float g, float b,
                                kg::KGModule* kg, kg::EntityID entity);

    // System integration - allows particle system to notify other systems when particles are added
    void set_light_system(class LightSystem* light_system) { light_system_ = light_system; }
    void set_kg_module(class kg::KGModule* kg_module) { kg_module_ = kg_module; }
    void set_physics_system(PhysicsSystem* physics_system) { physics_system_ = physics_system; }
    void set_dynamics_system(ParticleDynamicsSystem* dynamics_system) { dynamics_system_ = dynamics_system; }

    // Particle swap callback: invoked whenever a particle is swapped during removal.
    // old_idx is the index the swapped particle was at, new_idx is where it moved.
    // External systems (like Eden's SpiritLight) register to keep their cached
    // particle IDs in sync with the particle array.
    using SwapCallback = std::function<void(size_t old_idx, size_t new_idx)>;
    void add_swap_callback(SwapCallback cb) { swap_callbacks_.push_back(std::move(cb)); }
    
    // BVH acceleration structure for shadow rays
    // Call this before lighting calculations when particles have moved
    void update_bvh();
    
    // Get the BVH for shadow ray acceleration (may be null if not built)
    const BVH* get_shadow_bvh() const { return &shadow_bvh_; }
    
    // Mark BVH as needing rebuild (call when particles move)
    //
    // This is also the creation door's signal that the world has MOVED: the
    // door's index stores each body's bounds where it was when it was
    // inserted, and a stale bound makes the door miss an overlap. The refit
    // is deferred to the next birth, so a frame that creates nothing pays
    // nothing.
    void mark_bvh_dirty() { bvh_dirty_ = true; creation_index_stale_ = true; }
    
    // Surface query methods
    
    // Get world normal for a surface (after rotation to world coordinates)
    void get_surface_normal(size_t particle_index, size_t surface_index,
                           float& nx, float& ny, float& nz) const;
    
    // Find the surface whose normal is most aligned with the given direction
    // Returns surface index, or -1 if no good match (e.g., edge/corner case)
    int find_surface_by_normal(size_t particle_index,
                               float nx, float ny, float nz) const;

    // =========================================================================
    // SURFACE-BASED PARTICLE PLACEMENT
    // =========================================================================
    // Place a new particle at a specific surface of an existing particle.
    // surface_normal: direction to match (e.g., {0,0,1} for top surface)
    // offset: offset from surface center (in world coordinates)
    // Returns particle ID, or -1 on failure
    int create_particle_at_surface(
        int target_particle_id,
        float surface_normal_x, float surface_normal_y, float surface_normal_z,
        float offset_x, float offset_y, float offset_z,
        const Particle& new_particle_config
    );

protected:
    // =========================================================================
    // INTERNAL PARTICLE CREATION - Use entity-bound public API instead
    // =========================================================================
    // Direct particle creation bypasses Entity BVH grouping.
    // External code should use: add_particle_to_entity(), create_light_for_entity()
    int add_particle(const Particle& particle);

    // Friend classes that need direct particle creation for internal reasons
    friend class Engine;              // Default light, debug particles
    friend class PhysicsSystem;       // Gluon particle creation
    friend class HumanoidGenerator;   // Body part creation (transitional)
    friend class TreeGenerator;       // Tree segment creation
    friend class SceneManager;        // Test scene setup
    friend class TestContext;         // Test helpers
    friend class TestEngine;          // Test framework
    friend class PhysicsRockGenerator;
    friend class PhysicsTreeGenerator;
    friend class SceneChunkGenerator;
    friend class ChunkSystem;          // Async chunk loading (main thread particle creation)

private:
    // Internal unlocked version - caller must hold particles_mutex_
    void remove_particle_unlocked(size_t index);

    // RESOLVED[ARCH-002]: Migrated from deque to vector for cache coherency
    // Vector uses contiguous memory (no internal pointer arrays like deque)
    // Thread-safe with shared_mutex: Multiple concurrent readers OR single writer
    // Pre-reserved capacity prevents reallocations during chunk loading
    std::vector<Particle> particles;
    std::vector<MemoryParticle> spatial_memory;

    // Entity → particle indices mapping for fast iteration of visible entities
    // Updated on add/remove. Enables collect_surfaces to skip 90% of iteration.
    // entity_particle_indices_[entity_id] = {particle_index_0, particle_index_1, ...}
    std::vector<std::vector<int>> entity_particle_indices_;

    // Thread safety for chunk loading/unloading concurrent with rendering
    // Using shared_mutex: Multiple readers (render threads) OR single writer (chunk system)
    mutable std::shared_mutex particles_mutex_;

    // Deferred particle queue for thread-safe addition during rendering.
    // Deque, not queue, so the creation door can test a newcomer against the
    // batch already queued (INV-37: two bodies queued into the same place are
    // still two bodies in the same place).
    std::deque<Particle> pending_particles;

    // ---- THE CREATION DOOR (INV-37) ----------------------------------------
    // creation_index_ mirrors the LIVE array; pending_index_ mirrors the
    // pending deque and is cleared at every flush. Both are incremental: a
    // birth inserts one leaf, the world moving costs one refit, and only a
    // swap-and-pop (which changes what an index MEANS) costs a rebuild.
    logosphere::CreationIndex     creation_index_;
    logosphere::CreationIndex     pending_index_;
    size_t                        creation_indexed_count_ = 0;
    bool                          creation_index_stale_ = false;    // world moved
    bool                          creation_index_invalid_ = true;   // indices moved
    logosphere::CreationRefusal   last_refusal_;
    logosphere::CreationDoorStats door_stats_;
    std::map<std::string, size_t> door_census_;
    size_t                        door_lines_printed_ = 0;
    size_t                        births_unnamed_ = 0;          // INV-38
    bool   flushing_pending_ = false;
    size_t promise_breaks_ = 0;
    int    first_break_live_ = -1;
    int    first_break_pending_ = 0;
    std::map<std::string, size_t> unnamed_census_;
    size_t                        unnamed_lines_printed_ = 0;

    // Judge one birth against what already exists. Returns true when the body
    // must be REFUSED; fills last_refusal_ and prints the [PHYSICS REFUSED]
    // line. The caller must already hold whatever lock protects `particles`.
    bool creation_door_refuses(const Particle& p, int would_be_index,
                               const char* door_name, bool against_pending);
    void sync_creation_index();   // refit / rebuild as the flags demand
    // The world half of the judgement: deepest overlap of p against every
    // live body but `exclude`, with the blocker and the normal.
    float deepest_against_world(const Particle& p, int exclude, int& blocker,
                                float& nx, float& ny, float& nz);

    // Deferred deletion queue for GPU triple buffering safety
    // Mirrors pending_particles pattern - deletions queued with future frame number
    //
    // Entries hold RAW indices, so remove_particle_unlocked keeps them
    // coherent across every swap-and-pop: entries aimed at the dying slot
    // are dropped, entries tracking the moved tail are retargeted. Deque
    // (not queue) so that pass can iterate and erase in place.
    //
    // THREADING: main-thread only, like every current producer (chunk
    // unload, pin flush) and consumer (engine update/render flush). No
    // lock guards it; adding an off-thread producer requires one.
    struct DeferredDeletion {
        size_t particle_index;
        int safe_deletion_frame;  // current_frame + 3 (triple buffering)
    };
    std::deque<DeferredDeletion> pending_deletions_;

    // System references for cross-system notifications
    class LightSystem* light_system_ = nullptr;
    class kg::KGModule* kg_module_ = nullptr;
    PhysicsSystem* physics_system_ = nullptr;
    ParticleDynamicsSystem* dynamics_system_ = nullptr;
    std::vector<SwapCallback> swap_callbacks_;
    
    // BVH acceleration structure for shadow rays
    BVH shadow_bvh_;

    // Particle ID counter for stable BVH slot mapping
    uint32_t next_particle_id_ = 1;  // 0 is reserved for uninitialized
    bool bvh_dirty_ = true;  // Needs rebuild when particles move
    size_t last_bvh_particle_count_ = 0;  // Track for refit vs rebuild decision
    
    // Helper functions
    MemoryLayer get_memory_layer(const MemoryParticle& memory, double current_time);
    void apply_memory_decay(MemoryParticle& memory, double current_time);
};

#endif // PARTICLE_SYSTEM_H