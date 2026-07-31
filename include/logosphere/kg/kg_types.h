#ifndef KG_TYPES_H
#define KG_TYPES_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kg {

// Knowledge Graph operation modes - from zero overhead to full capabilities
enum class KGMode {
    DISABLED,    // Zero overhead - KG doesn't exist
    MINIMAL,     // Just entity tracking (id, type)
    SPATIAL,     // + spatial queries (what's nearby?)
    TEMPORAL,    // + history/events (what happened?)
    INFERENCE,   // + rules/reasoning (why did it happen?)
    FULL        // Everything including LLM bridge
};

// Entity ID type - simple incrementing integers for cache efficiency
using EntityID = uint32_t;
using RelationID = uint32_t;
using EventID = uint32_t;

// Two-tier particle ID system for permaworld support
using KGParticleID = uint64_t;   // Stable, permanent IDs (never reused, survives save/load)
using RenderIndex = uint32_t;     // Temporary render indices (0-N, swap-and-pop, rebuilt each chunk load)
using KGGluonID = uint64_t;       // Stable gluon IDs for physics constraint storage

// Invalid ID constants
constexpr EntityID INVALID_ENTITY = 0;
constexpr RelationID INVALID_RELATION = 0;
constexpr EventID INVALID_EVENT = 0;
constexpr KGParticleID INVALID_KG_PARTICLE_ID = 0;
constexpr RenderIndex INVALID_RENDER_INDEX = 0xFFFFFFFF;
constexpr KGGluonID INVALID_KG_GLUON_ID = 0;

// Gluon types for physics serialization
enum class KGGluonType {
    NAIL,       // Rigid constraint with fixed breaking threshold
    ORGANIC,    // Breaking force from contact area × material strength
    ELASTIC     // Spring behavior (knees, tendons)
};

// Gluon data for KG storage - all parameters needed to recreate a gluon
struct KGGluonData {
    KGGluonType type = KGGluonType::NAIL;

    // Attachment points (local coordinates)
    float offset_a_x = 0.0f, offset_a_y = 0.0f, offset_a_z = 0.0f;
    float offset_b_x = 0.0f, offset_b_y = 0.0f, offset_b_z = 0.0f;

    // Position constraint
    float target_distance = 0.0f;
    float stiffness = 100000.0f;
    float damping = 0.0f;
    bool rotate_offsets = true;

    // Angular constraint
    float angular_stiffness = 100.0f;
    float angular_damping = 10.0f;
    float target_relative_rotation = 0.0f;
    bool enable_angular_constraint = true;
    float max_relative_rotation = 3.14159f;

    // Type-specific parameters
    float breaking_force = 50000.0f;  // For Nail, Elastic
    float contact_area = 0.01f;       // For Organic (m²)
    float max_strain = 0.0f;          // For Elastic
};

// Basic value type for properties (start simple, extend later)
using PropertyValue = std::string;
using PropertyMap = std::unordered_map<std::string, PropertyValue>;

// Core entity structure - minimal to start
struct Entity {
    EntityID id = INVALID_ENTITY;
    std::string type;  // "fire", "chair", "character", etc.
    PropertyMap properties;

    // Particle binding removed - now managed via KGParticleID mapping
    // Use KGCore::createKGParticle() and getRenderIndex() instead
};

// Relationship between entities
struct Relation {
    RelationID id = INVALID_RELATION;
    EntityID from;
    EntityID to;
    std::string type;  // "BURNS", "SUPPORTS", "CONTAINS", etc.
    float strength = 1.0f;  // How strong is this relationship
};

// Event for temporal tracking (added in TEMPORAL mode)
struct Event {
    EventID id = INVALID_EVENT;
    float timestamp;
    EntityID actor;
    std::string action;
    EntityID target;
    EventID caused_by = INVALID_EVENT;  // Causal chain
};

// Configuration for KG module
struct KGConfig {
    KGMode mode = KGMode::DISABLED;

    // Memory limits (increased for forest scenes with dynamic chunk loading)
    size_t max_entities = 500000;
    size_t max_relations = 1000000;
    size_t max_events = 10000;

    // Performance tuning
    float spatial_grid_size = 10.0f;
    float inference_frequency = 0.1f;  // 10 Hz

    // Feature flags
    bool enable_debug_viz = false;
    bool enable_causal_chains = true;
};

// Result of unloading an entity (permaworld-compliant)
struct UnloadResult {
    std::vector<RenderIndex> render_indices;  // Render indices to delete from ParticleSystem
    std::vector<KGParticleID> kg_particle_ids;  // KG particles that were unloaded (for permaworld tracking)
};

} // namespace kg

#endif // KG_TYPES_H