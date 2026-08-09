#include "logosphere/worldgen/scene_chunk_generator.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/kg/kg_module.h"
#include "optimization_flags.h"
#include <iostream>
#include <algorithm>
#include <string>

// Set to 1 for verbose chunk loading logs
#define SCENE_CHUNK_VERBOSE 1

#if SCENE_CHUNK_VERBOSE
#define SCENE_LOG std::cout
#else
#define SCENE_LOG if(false) std::cout
#endif

SceneChunkGenerator::SceneChunkGenerator()
    : engine_(nullptr)
    , kg_(nullptr)
{
}

SceneChunkGenerator::~SceneChunkGenerator() {
}

void SceneChunkGenerator::initialize(Engine* engine, kg::KGModule* kg) {
    engine_ = engine;
    kg_ = kg;

    // Initialize underlying chunk system
    chunk_system_.initialize(engine, kg, "scene_system");

    // Register our scene-loading callbacks
    chunk_system_.set_create_callback(
        [this](const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity) {
            return create_scene_chunk(coord, chunk_size, existing_entity);
        }
    );

    chunk_system_.set_destroy_callback(
        [this](const ChunkData& data) {
            destroy_scene_chunk(data);
        }
    );

    // Enable async chunk loading if flag is set (Two-Tier Streaming Phase 2)
    if (Optimizations::USE_ASYNC_CHUNK_LOADING) {
        chunk_system_.set_async_loading(true);
    }

    SCENE_LOG << "[SceneChunkGenerator] Initialized (loads entities from KG)" << std::endl;
}

void SceneChunkGenerator::update(float observer_x, float observer_y) {
    // Process any pending entity activations first
    process_pending_activations();

    chunk_system_.update(observer_x, observer_y);

    // Apply any chunks that finished loading async (Two-Tier Streaming Phase 2)
    if (Optimizations::USE_ASYNC_CHUNK_LOADING) {
        chunk_system_.apply_ready_chunks();
    }
}

void SceneChunkGenerator::update_with_prediction(float observer_x, float observer_y,
                                                  float velocity_x, float velocity_y,
                                                  float predict_time) {
    // Process any pending entity activations first
    process_pending_activations();

    // Use prediction-enabled update (Phase 3)
    chunk_system_.update_with_prediction(observer_x, observer_y,
                                         velocity_x, velocity_y,
                                         predict_time);

    // Apply any chunks that finished loading async
    if (Optimizations::USE_ASYNC_CHUNK_LOADING) {
        chunk_system_.apply_ready_chunks();
    }
}

void SceneChunkGenerator::queue_entity_activation(kg::EntityID entity_id) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_activations_.push_back(entity_id);
    SCENE_LOG << "[SceneChunkGenerator] Queued entity " << entity_id << " for activation" << std::endl;
}

void SceneChunkGenerator::process_pending_activations() {
    // Swap out pending list to minimize lock time
    std::vector<kg::EntityID> to_activate;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (pending_activations_.empty()) return;
        to_activate.swap(pending_activations_);
    }

    SCENE_LOG << "[SceneChunkGenerator] Processing " << to_activate.size() << " pending activations" << std::endl;

    for (kg::EntityID entity_id : to_activate) {
        activate_entity_now(entity_id);
    }
}

void SceneChunkGenerator::preload_chunks_around(float world_x, float world_y, int radius_chunks) {
    chunk_system_.preload_chunks_around(world_x, world_y, radius_chunks);
}

void SceneChunkGenerator::set_load_radius(float meters) {
    chunk_system_.set_load_radius(meters);
}

void SceneChunkGenerator::set_unload_radius(float meters) {
    chunk_system_.set_unload_radius(meters);
}

void SceneChunkGenerator::set_chunk_size(float meters) {
    chunk_system_.set_chunk_size(meters);
}

void SceneChunkGenerator::set_update_frequency(int frames) {
    chunk_system_.set_update_frequency(frames);
}

void SceneChunkGenerator::set_enabled(bool enabled) {
    chunk_system_.set_enabled(enabled);
}

bool SceneChunkGenerator::is_enabled() const {
    return chunk_system_.is_enabled();
}

int SceneChunkGenerator::get_active_chunk_count() const {
    return chunk_system_.get_active_chunk_count();
}

void SceneChunkGenerator::clear_all_chunks() {
    chunk_system_.clear_all_chunks();
}

ChunkData SceneChunkGenerator::create_scene_chunk(const ChunkCoord& coord, float chunk_size, kg::EntityID existing_entity) {
    ChunkData data;

    SCENE_LOG << "[SceneChunkGenerator] Loading chunk (" << coord.x << "," << coord.y
              << ") from KG..." << std::endl;

    // PERMAWORLD: Reuse existing entity if provided, otherwise create new
    if (existing_entity != kg::INVALID_ENTITY) {
        // Reload: reuse existing entity from KG
        data.kg_entity = existing_entity;
        SCENE_LOG << "[SceneChunkGenerator] Reloading scene chunk entity " << existing_entity
                  << " for chunk (" << coord.x << "," << coord.y << ")" << std::endl;
    } else {
        // First time: create new entity
        data.kg_entity = kg_->createEntity("SceneChunk");
        kg_->setProperty(data.kg_entity, "chunk_x", std::to_string(coord.x));
        kg_->setProperty(data.kg_entity, "chunk_y", std::to_string(coord.y));
        kg_->setProperty(data.kg_entity, "type", "scene");
        SCENE_LOG << "[SceneChunkGenerator] Created new scene chunk entity " << data.kg_entity
                  << " for chunk (" << coord.x << "," << coord.y << ")" << std::endl;
    }

    // === QUERY KG FOR ENTITIES IN THIS CHUNK ===
    // Find all entities with matching chunk coordinates
    auto entities_x = kg_->findByProperty("chunk_x", std::to_string(coord.x));
    auto entities_y = kg_->findByProperty("chunk_y", std::to_string(coord.y));

    // Intersect: find entities that have BOTH chunk_x AND chunk_y
    std::vector<kg::EntityID> chunk_entities;
    std::sort(entities_x.begin(), entities_x.end());
    std::sort(entities_y.begin(), entities_y.end());
    std::set_intersection(
        entities_x.begin(), entities_x.end(),
        entities_y.begin(), entities_y.end(),
        std::back_inserter(chunk_entities)
    );

    SCENE_LOG << "[SceneChunkGenerator] Found " << chunk_entities.size()
              << " entities in KG for chunk (" << coord.x << "," << coord.y << ")" << std::endl;

    // === ACTIVATE ENTITIES ===
    // Use EntityManager for polymorphic entity activation
    EntityManager& entity_mgr = engine_->get_entity_manager();

    for (kg::EntityID entity_id : chunk_entities) {
        std::string type = kg_->getType(entity_id);

        // Skip scene_chunk entities (metadata, not content)
        if (type == "SceneChunk") {
            continue;
        }

        // Check if entity already has active particles (skip duplicate activation)
        // Use recursive check to handle hierarchical entities (trees, etc.)
        auto existing_kg_particles = kg_->getEntityKGParticlesRecursive(entity_id, "HAS_PART");
        bool already_active = false;
        for (auto kg_id : existing_kg_particles) {
            kg::RenderIndex idx = kg_->getRenderIndex(kg_id);
            if (idx != kg::INVALID_RENDER_INDEX) {
                already_active = true;
                break;
            }
        }

        if (already_active) {
            SCENE_LOG << "[SceneChunkGenerator] Entity " << entity_id << " (" << type
                      << ") already has active particles - using existing" << std::endl;
            // Track for unload but don't re-activate
            data.entity_ids.push_back(entity_id);

            // Track hierarchical descendants (entity-agnostic - works for any entity with has_part)
            auto children = kg_->getRelated(entity_id, "HAS_PART");
            if (!children.empty()) {
                SCENE_LOG << "[SceneChunkGenerator]   Entity " << entity_id << " (" << type
                          << ") already active - tracking descendants" << std::endl;

                std::vector<kg::EntityID> all_descendants;
                std::vector<kg::EntityID> to_process = {entity_id};
                while (!to_process.empty()) {
                    kg::EntityID current = to_process.back();
                    to_process.pop_back();
                    auto child_rels = kg_->getRelated(current, "HAS_PART");
                    for (auto child_id : child_rels) {
                        all_descendants.push_back(child_id);
                        to_process.push_back(child_id);
                    }
                }
                for (auto child_id : all_descendants) {
                    data.entity_ids.push_back(child_id);
                }
                SCENE_LOG << "[SceneChunkGenerator]   Tracked " << all_descendants.size()
                          << " descendants via has_part relationships" << std::endl;
            }
            continue;
        }

        SCENE_LOG << "[SceneChunkGenerator] Activating entity " << entity_id
                  << " (type=" << type << ")" << std::endl;

        // Use EntityManager to activate entity (polymorphic dispatch)
        ActivationResult result = entity_mgr.activate_entity(entity_id);

        // Check if entity has children (hierarchical entity like grass_patch, tree, etc.)
        auto children = kg_->getRelated(entity_id, "HAS_PART");
        bool has_children = !children.empty();

        // Skip only if entity has no particles AND no children
        if (result.particles.empty() && !has_children) {
            SCENE_LOG << "[SceneChunkGenerator]   No particles and no children (skipped)" << std::endl;
            continue;
        }

        // Track this entity for chunk unload (even if it has no particles but has children)
        data.entity_ids.push_back(entity_id);

        // For hierarchical entities, track all descendants
        if (has_children) {
            SCENE_LOG << "[SceneChunkGenerator]   HIERARCHICAL (" << type << "): Tracking descendants for entity " << entity_id << std::endl;

            // Recursively collect all descendants
            std::vector<kg::EntityID> all_descendants;
            std::vector<kg::EntityID> to_process = {entity_id};

            while (!to_process.empty()) {
                kg::EntityID current = to_process.back();
                to_process.pop_back();

                auto current_children = kg_->getRelated(current, "HAS_PART");
                SCENE_LOG << "[SceneChunkGenerator]     Entity " << current << " (" << kg_->getType(current)
                          << ") has " << current_children.size() << " children" << std::endl;

                for (auto child_id : current_children) {
                    all_descendants.push_back(child_id);
                    to_process.push_back(child_id);  // Process grandchildren

                    // Check how many particles this child entity has
                    auto child_particles = kg_->getEntityKGParticles(child_id);
                    SCENE_LOG << "[SceneChunkGenerator]       Child " << child_id << " (" << kg_->getType(child_id)
                              << ") has " << child_particles.size() << " KGParticles" << std::endl;
                }
            }

            // Track all descendants for unload
            for (auto child_id : all_descendants) {
                data.entity_ids.push_back(child_id);
            }

            SCENE_LOG << "[SceneChunkGenerator]   Entity has " << all_descendants.size()
                      << " descendants - all tracked for unload" << std::endl;
            SCENE_LOG << "[SceneChunkGenerator]   Total entities tracked: " << (1 + all_descendants.size()) << std::endl;
        }

        // Prepare particles for main thread (avoid mutex contention in async mode)
        // Background thread only collects data; main thread does add_particle() + KG binding
        for (size_t i = 0; i < result.particles.size(); i++) {
            PreparedParticle pp;
            pp.particle = result.particles[i];
            pp.entity_id = entity_id;
            pp.kg_particle_id = (i < result.kg_particle_ids.size())
                                 ? result.kg_particle_ids[i] : 0;

            data.prepared_particles.push_back(pp);

            SCENE_LOG << "[SceneChunkGenerator]   Prepared particle at ("
                      << pp.particle.x << "," << pp.particle.y << "," << pp.particle.z << ")"
                      << " for entity " << entity_id
                      << (pp.kg_particle_id ? " (reload)" : " (first load)") << std::endl;
        }

        // Prepare gluons for main thread (physics constraint creation)
        // GluonRequest indices point into result.particles - convert to KGParticleIDs
        for (const auto& req : result.gluon_requests) {
            PreparedGluon pg;
            pg.kg_gluon_id = req.kg_gluon_id;
            // Look up KGParticleID from result.kg_particle_ids using request indices
            pg.kg_particle_a = (req.particle_a_index < result.kg_particle_ids.size())
                               ? result.kg_particle_ids[req.particle_a_index] : 0;
            pg.kg_particle_b = (req.particle_b_index < result.kg_particle_ids.size())
                               ? result.kg_particle_ids[req.particle_b_index] : 0;
            data.prepared_gluons.push_back(pg);

            SCENE_LOG << "[SceneChunkGenerator]   Prepared gluon " << pg.kg_gluon_id
                      << " between KG particles " << pg.kg_particle_a << " and " << pg.kg_particle_b
                      << std::endl;
        }

        SCENE_LOG << "[SceneChunkGenerator]   Prepared " << result.particles.size()
                  << " particles and " << result.gluon_requests.size()
                  << " gluons for entity " << entity_id << std::endl;
    }

    SCENE_LOG << "[SceneChunkGenerator] Chunk (" << coord.x << "," << coord.y
              << ") loaded with " << data.entity_ids.size() << " entities" << std::endl;

    // Debug: Show all tracked entity IDs
    SCENE_LOG << "[SceneChunkGenerator] Tracked entity IDs for unload: [";
    for (size_t i = 0; i < data.entity_ids.size(); i++) {
        std::cout << data.entity_ids[i] << "(" << kg_->getType(data.entity_ids[i]) << ")";
        if (i < data.entity_ids.size() - 1) std::cout << ", ";
    }
    std::cout << "]" << std::endl;

    return data;
}

void SceneChunkGenerator::destroy_scene_chunk(const ChunkData& data) {
    // ChunkSystem handles particle deletion and KG cleanup
    // This is for scene-specific cleanup if needed
    // (Currently nothing extra to do)
}

void SceneChunkGenerator::activate_entity_now(kg::EntityID entity_id) {
    // The queue holds ids, and an id can die before it is processed.
    // A generator that self-queues and is then thrown away by its
    // caller is normal: Logogenesis grows a tree, sees it came out
    // collapsed, destroys it and grows another in the same breath.
    // Nothing is wrong, there is simply nothing left to activate.
    //
    // This must be asked BEFORE reading properties, because a
    // destroyed entity returns empty for every one of them. Reading
    // first reported a dead entity as "has no chunk coordinates",
    // which sent the search after generators that set their
    // coordinates correctly all along.
    if (!kg_->exists(entity_id)) {
        SCENE_LOG << "[SceneChunkGenerator] Entity " << entity_id
                  << " was destroyed before activation - skipping"
                  << std::endl;
        return;
    }

    // Find which chunk this entity belongs to
    std::string chunk_x_str = kg_->getProperty(entity_id, "chunk_x");
    std::string chunk_y_str = kg_->getProperty(entity_id, "chunk_y");

    // A LIVE entity with no coordinates is a real caller error: it was
    // made with createEntity instead of createEntityAtPosition, so it
    // is a record with no place in the world and no particles will
    // ever appear for it.
    if (chunk_x_str.empty() || chunk_y_str.empty()) {
        std::cerr << "[SceneChunkGenerator] ERROR: Entity " << entity_id
                  << " (" << kg_->getType(entity_id) << ") exists but has"
                  << " no chunk coordinates - create it with"
                  << " createEntityAtPosition()" << std::endl;
        return;
    }

    int chunk_x = std::stoi(chunk_x_str);
    int chunk_y = std::stoi(chunk_y_str);
    ChunkCoord coord(chunk_x, chunk_y);

    SCENE_LOG << "[SceneChunkGenerator] Activating entity " << entity_id
              << " in chunk (" << chunk_x << "," << chunk_y << ")" << std::endl;

    // Check if chunk is loaded - if not, load it on-demand
    const ChunkData* chunk = chunk_system_.get_chunk(coord);
    if (!chunk) {
        // Force-load the chunk so the entity can be activated
        std::cout << "[SceneChunkGenerator] Chunk (" << chunk_x << "," << chunk_y
                  << ") not loaded - loading on-demand for entity " << entity_id << std::endl;
        chunk_system_.create_chunk(coord);
        chunk = chunk_system_.get_chunk(coord);
        if (!chunk) {
            std::cerr << "[SceneChunkGenerator] ERROR: Failed to load chunk for entity " << entity_id << std::endl;
            return;
        }
    }

    // Check if entity is already active (has particles with valid render indices)
    auto existing_kg_particles = kg_->getEntityKGParticlesRecursive(entity_id, "HAS_PART");
    for (auto kg_id : existing_kg_particles) {
        kg::RenderIndex idx = kg_->getRenderIndex(kg_id);
        if (idx != kg::INVALID_RENDER_INDEX) {
            SCENE_LOG << "[SceneChunkGenerator] Entity " << entity_id
                      << " already has active particles - skipping duplicate activation" << std::endl;
            return;
        }
    }

    // Chunk is loaded - activate entity now
    EntityManager& entity_mgr = engine_->get_entity_manager();
    std::string type = kg_->getType(entity_id);

    ActivationResult result = entity_mgr.activate_entity(entity_id);

    // Check if entity has children (hierarchical entity like grass_patch, tree, etc.)
    auto children = kg_->getRelated(entity_id, "HAS_PART");
    bool has_children = !children.empty();

    // Skip only if entity has no particles AND no children
    if (result.particles.empty() && !has_children) {
        SCENE_LOG << "[SceneChunkGenerator] Entity " << entity_id << " (" << type
                  << ") has no particles and no children (skipped)" << std::endl;
        return;
    }

    // Track first particle for potential floor anchoring
    int first_render_idx = -1;
    float first_particle_x = 0.0f, first_particle_y = 0.0f;

    if (!result.particles.empty()) {
        SCENE_LOG << "[SceneChunkGenerator] Activating entity " << entity_id << " (" << type
                  << ") - creating " << result.particles.size() << " particles" << std::endl;

        // Create render particles and update KG particle bindings
        std::vector<int> render_indices;
        render_indices.reserve(result.particles.size());
        for (size_t i = 0; i < result.particles.size(); i++) {
            const Particle& p = result.particles[i];
            int render_idx = engine_->get_particle_system().add_particle(p);
            render_indices.push_back(render_idx);

            // Track first particle for floor anchoring
            if (first_render_idx < 0) {
                first_render_idx = render_idx;
                first_particle_x = p.x;
                first_particle_y = p.y;
            }

            // If entity has pre-existing KG particle (reload case)
            if (i < result.kg_particle_ids.size()) {
                kg::KGParticleID kg_id = result.kg_particle_ids[i];
                kg_->updateRenderIndex(kg_id, static_cast<kg::RenderIndex>(render_idx));

                // Restore is_at_rest from stored KG data (activator creates fresh particles)
                // This ensures trees stay at_rest after chunk reload, preventing cascade wake
                if (kg_->hasKGParticleData(kg_id)) {
                    Particle stored = kg_->getKGParticleData(kg_id);
                    if (stored.is_at_rest) {
                        auto particles_write = engine_->get_particle_system().lock_particles_for_write();
                        particles_write[render_idx].is_at_rest = true;
                    }
                }

                SCENE_LOG << "[SceneChunkGenerator]   Reloaded particle " << render_idx
                          << " at (" << p.x << "," << p.y << "," << p.z << ")"
                          << " kg_particle=" << kg_id << std::endl;
            } else {
                // First load: Create KG particle binding for permaworld
                kg::KGParticleID kg_id = kg_->createKGParticle(entity_id, static_cast<kg::RenderIndex>(render_idx));

                // Store particle data for reload
                kg_->setKGParticleData(kg_id, p);

                SCENE_LOG << "[SceneChunkGenerator]   First load: created particle " << render_idx
                          << " at (" << p.x << "," << p.y << "," << p.z << ")"
                          << " kg_particle=" << kg_id << std::endl;
            }
        }

        // NOTE: Turtle architecture (2025-12-12) - no more floor anchoring
        // Trees rest on floor tiles via contact collision
        // Floor tiles rest on Turtle (world boundary at TURTLE_Z)

        // ====================================================================
        // GLUONS (issue #47). This direct-activation path created particles
        // and DROPPED result.gluon_requests on the floor; only the chunk-load
        // path a few hundred lines up ever created bonds. So an entity got
        // its gluons if and only if its activation happened to FORCE-LOAD its
        // chunk, and lost them whenever the chunk was already resident, which
        // is every click-spawned entity in a live world and every patch after
        // the first in a test. Third instance of the same defect class: the
        // generator stored bonds, an activation path silently discarded them.
        // ====================================================================
        int gluons_created_now = 0;
        for (const auto& req : result.gluon_requests) {
            if (req.particle_a_index < render_indices.size() &&
                req.particle_b_index < render_indices.size()) {
                engine_->get_entity_manager().create_gluon_from_kg(
                    req.kg_gluon_id,
                    render_indices[req.particle_a_index],
                    render_indices[req.particle_b_index]);
                gluons_created_now++;
            }
        }
        if (!result.gluon_requests.empty()) {
            SCENE_LOG << "[SceneChunkGenerator]   Created " << gluons_created_now
                      << " of " << result.gluon_requests.size()
                      << " gluons (direct activation)" << std::endl;
        }
    } else {
        SCENE_LOG << "[SceneChunkGenerator] Entity " << entity_id << " (" << type
                  << ") is container entity (no direct particles, has " << children.size() << " children)" << std::endl;
    }

    // Add entity to chunk's tracked entities (even if no particles but has children)
    chunk_system_.add_entity_to_chunk(coord, entity_id);

    SCENE_LOG << "[SceneChunkGenerator] Entity " << entity_id << " activated and tracked in chunk ("
              << chunk_x << "," << chunk_y << ")" << std::endl;
}
