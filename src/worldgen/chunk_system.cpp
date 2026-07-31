#include "logosphere/worldgen/chunk_system.h"
#include "logosphere/worldgen/async_chunk_loader.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "entity_manager.h"
#include "logosphere/kg/kg_module.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <string>

// Chunk load latency tracking (for performance measurement)
static std::unordered_map<uint64_t, std::chrono::high_resolution_clock::time_point> g_chunk_request_times;
static inline uint64_t coord_hash(int x, int y) { return (uint64_t(x) << 32) | uint32_t(y); }

// Set to 1 for verbose chunk system logs
#define CHUNK_SYSTEM_VERBOSE 0

#if CHUNK_SYSTEM_VERBOSE
#define CHUNK_LOG std::cout
#else
#define CHUNK_LOG if(false) std::cout
#endif

ChunkSystem::ChunkSystem()
    : engine_(nullptr)
    , kg_(nullptr)
    , system_entity_(kg::INVALID_ENTITY)
{
}

ChunkSystem::~ChunkSystem() {
    // Stop async loader first (if running)
    if (async_loader_) {
        async_loader_->stop();
        async_loader_.reset();
    }
    clear_all_chunks();
}

void ChunkSystem::initialize(Engine* engine, kg::KGModule* kg, const char* system_name) {
    engine_ = engine;
    kg_ = kg;

    // Create root KG entity for this chunk system
    system_entity_ = kg_->createEntity("SystemEntity");
    kg_->setProperty(system_entity_, "name", system_name);
    kg_->setProperty(system_entity_, "chunk_size", std::to_string(chunk_size_));
    kg_->setProperty(system_entity_, "load_radius", std::to_string(load_radius_));

    CHUNK_LOG << "[ChunkSystem] Initialized '" << system_name << "' (entity " << system_entity_ << ")" << std::endl;
}

void ChunkSystem::update(float observer_x, float observer_y) {
    if (!enabled_) return;

    // Update frequency throttling (only check every N frames)
    frame_counter_++;
    if (frame_counter_ < update_frequency_) return;
    frame_counter_ = 0;

    // 1. Determine which chunks should exist (within load radius)
    auto required_chunks = get_chunks_in_radius(observer_x, observer_y, load_radius_);

    // 2. Load new chunks (sync or async)
    for (const auto& coord : required_chunks) {
        if (!chunk_exists(coord)) {
            if (async_loading_enabled_ && async_loader_) {
                // Check if already being loaded async
                if (!async_loader_->is_pending(coord)) {
                    kg::EntityID existing = find_existing_chunk_entity(coord);
                    // Record request time for latency measurement
                    g_chunk_request_times[coord_hash(coord.x, coord.y)] = std::chrono::high_resolution_clock::now();
                    async_loader_->request_load(coord, chunk_size_, existing);
                }
            } else {
                // Sync loading (original path) - measure blocking time
                auto sync_start = std::chrono::high_resolution_clock::now();
                create_chunk(coord);
                auto sync_end = std::chrono::high_resolution_clock::now();
                double sync_ms = std::chrono::duration<double, std::milli>(sync_end - sync_start).count();
                std::cout << "[CHUNK_LATENCY] SYNC load (" << coord.x << "," << coord.y
                          << ") blocked main thread for " << sync_ms << "ms" << std::endl;
            }
        }
    }

    // 3. Unload far chunks (beyond unload radius)
    std::vector<ChunkCoord> to_unload;
    for (const auto& [coord, data] : active_chunks_) {
        if (!is_in_radius(coord, observer_x, observer_y, unload_radius_)) {
            to_unload.push_back(coord);
        }
    }

    // PERMAWORLD: Entity-centric unload - process each entity separately
    // No deduplication needed - each entity owns its particles exclusively
    if (!to_unload.empty()) {
        std::cout << "[CHUNK_UNLOAD] Unloading " << to_unload.size() << " chunks" << std::endl;

        auto& particle_system = engine_->get_particle_system();

        // Process each chunk separately (entity-centric approach)
        for (const auto& coord : to_unload) {
            auto it = active_chunks_.find(coord);
            if (it == active_chunks_.end()) continue;

            const ChunkData& data = it->second;

            // Get current frame for triple buffering safety
            int current_frame = engine_->get_renderer().current_frame_number();

            // THREAD SAFETY: Use count() instead of get_particles().size()
            const size_t particle_count = particle_system.count();

            size_t total_queued = 0;

            // Unload all content entities (trees, cubes, lights, etc.)
            std::cout << "[CHUNK_UNLOAD]   Processing " << data.entity_ids.size() << " tracked entities..." << std::endl;

            for (kg::EntityID entity_id : data.entity_ids) {
                std::string entity_type = kg_->getType(entity_id);
                auto unload_result = kg_->unloadEntity(entity_id);

                std::cout << "[CHUNK_UNLOAD]   Entity " << entity_id << " (" << entity_type
                          << "): got " << unload_result.render_indices.size() << " render indices";

                if (unload_result.render_indices.empty()) {
                    std::cout << " [WARNING: NO PARTICLES TO UNLOAD!]" << std::endl;
                } else {
                    std::cout << " [";
                    for (size_t i = 0; i < std::min(size_t(5), unload_result.render_indices.size()); i++) {
                        std::cout << unload_result.render_indices[i];
                        if (i < std::min(size_t(5), unload_result.render_indices.size()) - 1) std::cout << ",";
                    }
                    if (unload_result.render_indices.size() > 5) std::cout << "...";
                    std::cout << "]" << std::endl;
                }

                // Sort descending for proper queueing (prevents index corruption)
                std::sort(unload_result.render_indices.begin(), unload_result.render_indices.end(), std::greater<kg::RenderIndex>());

                // Queue each particle for deferred deletion (GPU-safe)
                for (kg::RenderIndex idx : unload_result.render_indices) {
                    if (idx < particle_count) {
                        particle_system.queue_particle_deletion(idx, current_frame);
                        total_queued++;
                    } else {
                        std::cerr << "[CHUNK_UNLOAD ERROR] Invalid index " << idx
                                  << " >= particle count " << particle_count << std::endl;
                    }
                }
            }

            std::cout << "[CHUNK_UNLOAD] Chunk (" << coord.x << "," << coord.y
                      << ") unloaded " << data.entity_ids.size() << " entities ("
                      << total_queued << " particles queued)" << std::endl;

            // Remove chunk from active map
            active_chunks_.erase(it);
        }
    }
}

// Phase 3: Camera Prediction Pre-loading
// Pre-loads chunks in the direction of camera movement
void ChunkSystem::update_with_prediction(float observer_x, float observer_y,
                                         float velocity_x, float velocity_y,
                                         float predict_time) {
    // First do normal update (loads chunks around current position)
    update(observer_x, observer_y);

    // Skip prediction if not moving or async not enabled
    float speed = std::sqrt(velocity_x * velocity_x + velocity_y * velocity_y);
    if (speed < 0.1f || !async_loading_enabled_ || !async_loader_) {
        return;
    }

    // Predict future position
    float predict_x = observer_x + velocity_x * predict_time;
    float predict_y = observer_y + velocity_y * predict_time;

    // Only pre-load if predicted position is in a different chunk
    ChunkCoord current_chunk = world_to_chunk(observer_x, observer_y);
    ChunkCoord predict_chunk = world_to_chunk(predict_x, predict_y);

    if (current_chunk == predict_chunk) {
        return;  // Same chunk, no prediction needed
    }

    // Pre-load chunks around predicted position (smaller radius for prediction)
    float predict_radius = load_radius_ * 0.5f;  // Half the normal radius
    auto predict_chunks = get_chunks_in_radius(predict_x, predict_y, predict_radius);

    int preloaded = 0;
    for (const auto& coord : predict_chunks) {
        if (!chunk_exists(coord) && !async_loader_->is_pending(coord)) {
            kg::EntityID existing = find_existing_chunk_entity(coord);
            if (async_loader_->request_load(coord, chunk_size_, existing)) {
                preloaded++;
            }
        }
    }

    if (preloaded > 0) {
        CHUNK_LOG << "[ChunkSystem] PREDICTION: Pre-loading " << preloaded
                  << " chunks toward (" << predict_x << "," << predict_y
                  << ") speed=" << speed << " m/s" << std::endl;
    }
}

void ChunkSystem::preload_chunks_around(float world_x, float world_y, int radius_chunks) {
    // Pre-load chunks in a grid pattern around the position
    // radius_chunks=1 → 3x3 grid (9 chunks), radius_chunks=2 → 5x5 grid (25 chunks), etc.

    // Enable chunk system when preloading (auto-enable)
    if (!enabled_) {
        enabled_ = true;
        CHUNK_LOG << "[ChunkSystem] Auto-enabled during preload" << std::endl;
    }

    ChunkCoord center_chunk = world_to_chunk(world_x, world_y);
    CHUNK_LOG << "[ChunkSystem] Pre-loading chunks around (" << world_x << "," << world_y
              << ") - chunk (" << center_chunk.x << "," << center_chunk.y
              << ") with radius " << radius_chunks << std::endl;

    int loaded_count = 0;
    for (int dy = -radius_chunks; dy <= radius_chunks; ++dy) {
        for (int dx = -radius_chunks; dx <= radius_chunks; ++dx) {
            ChunkCoord coord(center_chunk.x + dx, center_chunk.y + dy);

            if (!chunk_exists(coord)) {
                create_chunk(coord);
                loaded_count++;
            }
        }
    }

    CHUNK_LOG << "[ChunkSystem] Pre-loaded " << loaded_count << " chunks" << std::endl;
}

ChunkCoord ChunkSystem::world_to_chunk(float world_x, float world_y) const {
    return ChunkCoord(
        static_cast<int>(std::floor(world_x / chunk_size_)),
        static_cast<int>(std::floor(world_y / chunk_size_))
    );
}

std::vector<ChunkCoord> ChunkSystem::get_chunks_in_radius(float center_x, float center_y, float radius) const {
    std::vector<ChunkCoord> chunks;

    // Convert center to chunk coords
    ChunkCoord center_chunk = world_to_chunk(center_x, center_y);

    // Calculate chunk radius (how many chunks away to check)
    int chunk_radius = static_cast<int>(std::ceil(radius / chunk_size_)) + 1;

    // Check all chunks in bounding box
    for (int dy = -chunk_radius; dy <= chunk_radius; ++dy) {
        for (int dx = -chunk_radius; dx <= chunk_radius; ++dx) {
            ChunkCoord coord(center_chunk.x + dx, center_chunk.y + dy);

            if (is_in_radius(coord, center_x, center_y, radius)) {
                chunks.push_back(coord);
            }
        }
    }

    return chunks;
}

bool ChunkSystem::is_in_radius(const ChunkCoord& chunk, float center_x, float center_y, float radius) const {
    // Check if observer is INSIDE this chunk first
    // Chunk (x,y) covers world space [x*size, (x+1)*size) × [y*size, (y+1)*size)
    float chunk_min_x = chunk.x * chunk_size_;
    float chunk_max_x = (chunk.x + 1) * chunk_size_;
    float chunk_min_y = chunk.y * chunk_size_;
    float chunk_max_y = (chunk.y + 1) * chunk_size_;

    if (center_x >= chunk_min_x && center_x < chunk_max_x &&
        center_y >= chunk_min_y && center_y < chunk_max_y) {
        // Observer is inside this chunk - always load
        return true;
    }

    // Otherwise check distance from observer to chunk center
    float chunk_cx = chunk.world_x(chunk_size_);
    float chunk_cy = chunk.world_y(chunk_size_);

    float dx = chunk_cx - center_x;
    float dy = chunk_cy - center_y;
    float distance = std::sqrt(dx * dx + dy * dy);

    return distance <= radius;
}

bool ChunkSystem::chunk_exists(const ChunkCoord& coord) const {
    return active_chunks_.find(coord) != active_chunks_.end();
}

const ChunkData* ChunkSystem::get_chunk(const ChunkCoord& coord) const {
    auto it = active_chunks_.find(coord);
    if (it == active_chunks_.end()) return nullptr;
    return &it->second;
}

void ChunkSystem::create_chunk(const ChunkCoord& coord) {
    if (chunk_exists(coord)) return;

    // Use callback to generate chunk content
    if (!create_callback_) {
        std::cerr << "[ChunkSystem] ERROR: No create callback set!" << std::endl;
        return;
    }

    // PERMAWORLD: Check if chunk entity already exists in KG before creating new one
    kg::EntityID existing_entity = find_existing_chunk_entity(coord);

    if (existing_entity != kg::INVALID_ENTITY) {
        // RELOAD: Entity exists in permaworld, clear old KGParticles (they reference deleted render indices)
        auto old_kg_particles = kg_->getEntityKGParticles(existing_entity);
        for (auto kg_id : old_kg_particles) {
            kg_->destroyKGParticle(kg_id);
        }
        CHUNK_LOG << "[ChunkSystem] Reloading existing chunk entity " << existing_entity
                  << " for chunk (" << coord.x << "," << coord.y << ") - cleared "
                  << old_kg_particles.size() << " old KGParticles" << std::endl;
    }

    // Call generator to create/reload content
    // Generator receives existing_entity (valid if reloading, INVALID_ENTITY if creating new)
    ChunkData data = create_callback_(coord, chunk_size_, existing_entity);

    // === SYNCHRONOUS PARTICLE CREATION ===
    // Process prepared_particles immediately (same logic as apply_ready_chunks)
    auto& particle_system = engine_->get_particle_system();
    int particles_added = 0;

    for (auto& pp : data.prepared_particles) {
        // Create render particle
        int render_idx = particle_system.add_particle(pp.particle);

        if (pp.kg_particle_id != 0) {
            // Reload case: update existing KG particle binding
            kg_->updateRenderIndex(pp.kg_particle_id, static_cast<kg::RenderIndex>(render_idx));

            // Restore is_at_rest from stored KG data
            if (kg_->hasKGParticleData(pp.kg_particle_id)) {
                Particle stored = kg_->getKGParticleData(pp.kg_particle_id);
                if (stored.is_at_rest) {
                    auto particles_write = particle_system.lock_particles_for_write();
                    particles_write[render_idx].is_at_rest = true;
                }
            }
        } else {
            // First load: create new KG particle binding
            kg::KGParticleID kg_id = kg_->createKGParticle(pp.entity_id, static_cast<kg::RenderIndex>(render_idx));
            kg_->setKGParticleData(kg_id, pp.particle);
        }

        particles_added++;
    }
    data.prepared_particles.clear();

    // === SYNCHRONOUS GLUON CREATION ===
    // Create gluons after particles (KG bindings are now updated with render indices)
    int gluons_created = 0;
    auto& entity_mgr = engine_->get_entity_manager();
    for (const auto& pg : data.prepared_gluons) {
        // Look up render indices from KG (particles were just created/bound)
        kg::RenderIndex render_a = kg_->getRenderIndex(pg.kg_particle_a);
        kg::RenderIndex render_b = kg_->getRenderIndex(pg.kg_particle_b);

        if (render_a != kg::INVALID_RENDER_INDEX && render_b != kg::INVALID_RENDER_INDEX) {
            entity_mgr.create_gluon_from_kg(pg.kg_gluon_id,
                                            static_cast<int>(render_a),
                                            static_cast<int>(render_b));
            gluons_created++;
        } else {
            std::cerr << "[ChunkSystem] ERROR: Could not resolve render indices for gluon "
                      << pg.kg_gluon_id << " (a=" << render_a << ", b=" << render_b << ")" << std::endl;
        }
    }
    data.prepared_gluons.clear();

    // Store chunk
    active_chunks_[coord] = data;

    // Link to system entity in KG (only if newly created)
    if (system_entity_ != kg::INVALID_ENTITY && data.kg_entity != kg::INVALID_ENTITY) {
        // Check if relation already exists (don't duplicate on reload)
        auto managed = kg_->getRelated(system_entity_, "MANAGES");
        bool already_linked = std::find(managed.begin(), managed.end(), data.kg_entity) != managed.end();
        if (!already_linked) {
            kg_->createRelation(system_entity_, "MANAGES", data.kg_entity);
        }
    }

    std::cout << "[CHUNK_LATENCY] SYNC load (" << coord.x << "," << coord.y
              << ") particles=" << particles_added << ", gluons=" << gluons_created << std::endl;

    CHUNK_LOG << "[ChunkSystem] Created chunk (" << coord.x << "," << coord.y
              << ") with " << data.entity_ids.size() << " entities, "
              << particles_added << " particles, " << gluons_created << " gluons" << std::endl;
}

void ChunkSystem::destroy_chunk(const ChunkCoord& coord) {
    // DEPRECATED: This method violates permaworld architecture
    // Use update() with chunk distance to unload instead
    std::cerr << "[CHUNK_SYSTEM ERROR] destroy_chunk() called! This method is DEPRECATED and does nothing!" << std::endl;
    std::cerr << "[CHUNK_SYSTEM ERROR] Coord: (" << coord.x << "," << coord.y << ")" << std::endl;
    std::cerr << "[CHUNK_SYSTEM ERROR] Use update() with proper unload_radius instead!" << std::endl;
    // DO NOT DELETE - violates permaworld and bypasses GPU triple-buffering safety
    return;
}

void ChunkSystem::clear_all_chunks() {
    if (active_chunks_.empty()) {
        CHUNK_LOG << "[ChunkSystem] No chunks to clear" << std::endl;
        return;
    }

    CHUNK_LOG << "[ChunkSystem] Clearing " << active_chunks_.size() << " chunks..." << std::endl;

    auto& particle_system = engine_->get_particle_system();
    int current_frame = engine_->get_renderer().current_frame_number();
    const size_t particle_count = particle_system.count();

    size_t total_entities = 0;
    size_t total_particles = 0;

    // Unload all content entities from all chunks
    for (const auto& [coord, data] : active_chunks_) {
        for (kg::EntityID entity_id : data.entity_ids) {
            auto unload_result = kg_->unloadEntity(entity_id);

            // Sort descending for proper queueing
            std::sort(unload_result.render_indices.begin(), unload_result.render_indices.end(), std::greater<kg::RenderIndex>());

            // Queue particles for deferred deletion (GPU-safe)
            for (kg::RenderIndex idx : unload_result.render_indices) {
                if (idx < particle_count) {
                    particle_system.queue_particle_deletion(idx, current_frame);
                }
            }

            total_entities++;
            total_particles += unload_result.render_indices.size();
        }
    }

    // Clear chunk map
    active_chunks_.clear();

    CHUNK_LOG << "[ChunkSystem] All chunks cleared (" << total_entities << " entities, "
              << total_particles << " particles queued for deletion)" << std::endl;
}

kg::EntityID ChunkSystem::find_existing_chunk_entity(const ChunkCoord& coord) const {
    // PERMAWORLD: Query KG for existing chunk entity before creating new one
    // Prevents entity duplication when chunks reload

    // Get all entities managed by this chunk system
    auto managed_entities = kg_->getRelated(system_entity_, "MANAGES");

    // Find one with matching chunk coordinates
    for (auto entity_id : managed_entities) {
        std::string chunk_x_str = kg_->getProperty(entity_id, "chunk_x");
        std::string chunk_y_str = kg_->getProperty(entity_id, "chunk_y");

        if (!chunk_x_str.empty() && !chunk_y_str.empty()) {
            int chunk_x = std::stoi(chunk_x_str);
            int chunk_y = std::stoi(chunk_y_str);

            if (chunk_x == coord.x && chunk_y == coord.y) {
                // Found existing chunk entity for this coordinate
                return entity_id;
            }
        }
    }

    return kg::INVALID_ENTITY;  // No existing entity, need to create new
}

void ChunkSystem::add_entity_to_chunk(const ChunkCoord& coord, kg::EntityID entity_id) {
    auto it = active_chunks_.find(coord);
    if (it == active_chunks_.end()) {
        std::cerr << "[ChunkSystem] ERROR: Cannot add entity to non-loaded chunk ("
                  << coord.x << "," << coord.y << ")" << std::endl;
        return;
    }

    ChunkData& data = it->second;

    // Add entity to tracking list
    data.entity_ids.push_back(entity_id);

    CHUNK_LOG << "[ChunkSystem] Added entity " << entity_id << " (" << kg_->getType(entity_id)
              << ") to chunk (" << coord.x << "," << coord.y << ") - now tracking "
              << data.entity_ids.size() << " entities" << std::endl;

    // For hierarchical entities (any type with has_part relations), track all descendants
    auto children = kg_->getRelated(entity_id, "HAS_PART");
    if (!children.empty()) {
        std::string type = kg_->getType(entity_id);
        CHUNK_LOG << "[ChunkSystem]   HIERARCHICAL (" << type << "): Tracking all descendants..." << std::endl;

        // Recursively collect all descendants
        std::vector<kg::EntityID> all_descendants;
        std::vector<kg::EntityID> to_process = {entity_id};

        while (!to_process.empty()) {
            kg::EntityID current = to_process.back();
            to_process.pop_back();

            auto current_children = kg_->getRelated(current, "HAS_PART");
            for (auto child_id : current_children) {
                all_descendants.push_back(child_id);
                to_process.push_back(child_id);  // Process grandchildren
            }
        }

        // Track all descendants for unload
        for (auto child_id : all_descendants) {
            data.entity_ids.push_back(child_id);
        }

        CHUNK_LOG << "[ChunkSystem]   Entity has " << all_descendants.size()
                  << " descendants - total tracked: " << data.entity_ids.size() << std::endl;
    }
}

void ChunkSystem::set_async_loading(bool enabled) {
    if (enabled == async_loading_enabled_) return;

    async_loading_enabled_ = enabled;

    if (enabled) {
        // Start async loader
        if (!async_loader_) {
            async_loader_ = std::make_unique<AsyncChunkLoader>();
        }
        if (create_callback_) {
            async_loader_->start(engine_, kg_, create_callback_);
            std::cout << "[ChunkSystem] Async loading ENABLED" << std::endl;
        } else {
            std::cerr << "[ChunkSystem] WARNING: Async loading enabled but no create_callback set!" << std::endl;
        }
    } else {
        // Stop async loader
        if (async_loader_) {
            async_loader_->stop();
        }
        std::cout << "[ChunkSystem] Async loading DISABLED" << std::endl;
    }
}

void ChunkSystem::apply_ready_chunks() {
    if (!async_loader_) return;

    auto apply_start = std::chrono::high_resolution_clock::now();
    int chunks_applied = 0;

    // Process all ready chunks (non-blocking)
    while (async_loader_->has_ready()) {
        auto result = async_loader_->pop_ready();

        if (!result.success) {
            std::cerr << "[ChunkSystem] Async load failed for chunk ("
                      << result.coord.x << "," << result.coord.y << ")" << std::endl;
            continue;
        }

        // Calculate latency from request to ready (background thread work)
        auto hash = coord_hash(result.coord.x, result.coord.y);
        auto it = g_chunk_request_times.find(hash);
        double bg_latency_ms = 0.0;
        if (it != g_chunk_request_times.end()) {
            auto now = std::chrono::high_resolution_clock::now();
            bg_latency_ms = std::chrono::duration<double, std::milli>(now - it->second).count();
            g_chunk_request_times.erase(it);
        }

        // === MAIN THREAD PARTICLE CREATION ===
        // Add particles and bind KG (avoids mutex contention with background thread)
        auto& particle_system = engine_->get_particle_system();
        int particles_added = 0;

        for (auto& pp : result.data.prepared_particles) {
            // Create render particle (main thread - no contention)
            int render_idx = particle_system.add_particle(pp.particle);

            if (pp.kg_particle_id != 0) {
                // Reload case: update existing KG particle binding
                kg_->updateRenderIndex(pp.kg_particle_id, static_cast<kg::RenderIndex>(render_idx));

                // Restore is_at_rest from stored KG data
                if (kg_->hasKGParticleData(pp.kg_particle_id)) {
                    Particle stored = kg_->getKGParticleData(pp.kg_particle_id);
                    if (stored.is_at_rest) {
                        auto particles_write = particle_system.lock_particles_for_write();
                        particles_write[render_idx].is_at_rest = true;
                    }
                }
            } else {
                // First load: create new KG particle binding
                kg::KGParticleID kg_id = kg_->createKGParticle(pp.entity_id, static_cast<kg::RenderIndex>(render_idx));
                kg_->setKGParticleData(kg_id, pp.particle);
            }

            particles_added++;
        }

        // Clear prepared_particles after processing (they're now in ParticleSystem)
        result.data.prepared_particles.clear();

        // === MAIN THREAD GLUON CREATION ===
        // Create gluons after particles (KG bindings are now updated with render indices)
        int gluons_created = 0;
        auto& entity_mgr = engine_->get_entity_manager();
        for (const auto& pg : result.data.prepared_gluons) {
            // Look up render indices from KG (particles were just created/bound)
            kg::RenderIndex render_a = kg_->getRenderIndex(pg.kg_particle_a);
            kg::RenderIndex render_b = kg_->getRenderIndex(pg.kg_particle_b);

            if (render_a != kg::INVALID_RENDER_INDEX && render_b != kg::INVALID_RENDER_INDEX) {
                entity_mgr.create_gluon_from_kg(pg.kg_gluon_id,
                                                static_cast<int>(render_a),
                                                static_cast<int>(render_b));
                gluons_created++;
            } else {
                std::cerr << "[ChunkSystem] ERROR: Could not resolve render indices for gluon "
                          << pg.kg_gluon_id << " (a=" << render_a << ", b=" << render_b << ")" << std::endl;
            }
        }
        result.data.prepared_gluons.clear();

        // Store chunk in active map
        active_chunks_[result.coord] = result.data;
        chunks_applied++;

        // Link to system entity in KG
        if (system_entity_ != kg::INVALID_ENTITY && result.data.kg_entity != kg::INVALID_ENTITY) {
            auto managed = kg_->getRelated(system_entity_, "MANAGES");
            bool already_linked = std::find(managed.begin(), managed.end(), result.data.kg_entity) != managed.end();
            if (!already_linked) {
                kg_->createRelation(system_entity_, "MANAGES", result.data.kg_entity);
            }
        }

        std::cout << "[CHUNK_LATENCY] ASYNC load (" << result.coord.x << "," << result.coord.y
                  << ") bg_time=" << bg_latency_ms << "ms, particles=" << particles_added
                  << ", gluons=" << gluons_created << std::endl;

        CHUNK_LOG << "[ChunkSystem] Applied async chunk (" << result.coord.x
                  << "," << result.coord.y << ") with "
                  << result.data.entity_ids.size() << " entities, "
                  << particles_added << " particles, "
                  << gluons_created << " gluons" << std::endl;
    }

    // Flush any queued particle additions (from async loader)
    if (engine_) {
        engine_->get_particle_system().flush_pending_particles();
    }

    // Log main thread time for applying chunks
    if (chunks_applied > 0) {
        auto apply_end = std::chrono::high_resolution_clock::now();
        double apply_ms = std::chrono::duration<double, std::milli>(apply_end - apply_start).count();
        std::cout << "[CHUNK_LATENCY] Main thread apply time: " << apply_ms
                  << "ms for " << chunks_applied << " chunks" << std::endl;
    }
}
