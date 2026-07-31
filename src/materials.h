#pragma once

// =============================================================================
// MATERIALS SYSTEM
// =============================================================================
// Standard material densities for physics calculations.
// Mass is auto-calculated from density × volume at particle creation.
//
// Usage:
//   Particle p;
//   p.SetMaterial(Materials::Type::STONE);  // Sets density to 2500 kg/m³
//   // Mass calculated automatically in add_particle()
//
// Source: Engineering handbooks, physics references
// =============================================================================

namespace Materials {

// Material types with real-world densities (kg/m³)
enum class Type {
    // Organic
    FLESH = 0,      // ~1000 kg/m³ (similar to water)
    WOOD_SOFT,      // ~500 kg/m³ (pine, balsa)
    WOOD_HARD,      // ~800 kg/m³ (oak, maple)
    LEAVES,         // ~200 kg/m³ (loose foliage)

    // Mineral
    STONE,          // ~2500 kg/m³ (granite, limestone)
    CONCRETE,       // ~2400 kg/m³
    BRICK,          // ~1800 kg/m³
    SAND,           // ~1600 kg/m³
    DIRT,           // ~1500 kg/m³

    // Metal
    IRON,           // ~7800 kg/m³
    STEEL,          // ~8000 kg/m³
    ALUMINUM,       // ~2700 kg/m³
    GOLD,           // ~19300 kg/m³

    // Special
    LIGHT,          // 0 - floats (lights, celestial)

    // HEAVY_STATIC — 10000 kg/m³. Legacy "pseudo-immovable" material for
    // floor tiles. The only truly immovable thing is the turtle boundary
    // (physics_system_v4.cpp:75-94 warning). Use STONE (or the appropriate
    // real material) + is_at_rest=true for tiles; bond with OrganicGluon if
    // cohesion is needed (see StrataFloorGenerator and PhysicsRockGenerator
    // for the pattern). Retained for one release cycle so existing tests
    // continue to compile; emits a deprecation warning on each use. To be
    // removed from the enum in a later release.
    HEAVY_STATIC [[deprecated("Use STONE + is_at_rest=true (or gluon bonding). "
                              "HEAVY_STATIC is a pseudo-immovable hack; only the "
                              "turtle is truly immovable.")]],

    COUNT
};

// Get density for material type (kg/m³)
// Suppression: we reference HEAVY_STATIC in our own switch intentionally.
// Deprecation warnings should fire at callers, not in this dispatch table.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetDensity(Type type) {
    switch (type) {
        case Type::FLESH:        return 1000.0f;
        case Type::WOOD_SOFT:    return 500.0f;
        case Type::WOOD_HARD:    return 800.0f;
        case Type::LEAVES:       return 200.0f;
        case Type::STONE:        return 2500.0f;
        case Type::CONCRETE:     return 2400.0f;
        case Type::BRICK:        return 1800.0f;
        case Type::SAND:         return 1600.0f;
        case Type::DIRT:         return 1500.0f;
        case Type::IRON:         return 7800.0f;
        case Type::STEEL:        return 8000.0f;
        case Type::ALUMINUM:     return 2700.0f;
        case Type::GOLD:         return 19300.0f;
        case Type::LIGHT:        return 0.0f;
        case Type::HEAVY_STATIC: return 10000.0f;
        default:                 return 1000.0f;  // Default to flesh
    }
}
#pragma GCC diagnostic pop

// Get material name for debugging
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr const char* GetName(Type type) {
    switch (type) {
        case Type::FLESH:        return "flesh";
        case Type::WOOD_SOFT:    return "wood_soft";
        case Type::WOOD_HARD:    return "wood_hard";
        case Type::LEAVES:       return "Leaves";
        case Type::STONE:        return "stone";
        case Type::CONCRETE:     return "concrete";
        case Type::BRICK:        return "brick";
        case Type::SAND:         return "sand";
        case Type::DIRT:         return "dirt";
        case Type::IRON:         return "iron";
        case Type::STEEL:        return "steel";
        case Type::ALUMINUM:     return "aluminum";
        case Type::GOLD:         return "gold";
        case Type::LIGHT:        return "light";
        case Type::HEAVY_STATIC: return "heavy_static";
        default:                 return "unknown";
    }
}
#pragma GCC diagnostic pop

// =============================================================================
// MATERIAL DAMPING (V4.9)
// =============================================================================
// Damping factor = fraction of relative velocity absorbed per physics frame.
// Higher damping = faster energy dissipation = quicker settling.
//
// Physics basis:
// - Organic materials (wood, flesh): Internal fibers deform and absorb energy
// - Granular materials (sand, dirt): Particle friction dissipates energy
// - Rigid materials (stone, metal): Little internal deformation, low damping
// =============================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetDamping(Type type) {
    switch (type) {
        // Organic - high damping (fibers absorb vibration)
        case Type::FLESH:        return 0.35f;  // Soft tissue, high absorption
        case Type::WOOD_SOFT:    return 0.30f;  // Pine, balsa - soft fibers
        case Type::WOOD_HARD:    return 0.25f;  // Oak - denser but still organic
        case Type::LEAVES:       return 0.40f;  // Very soft, high damping

        // Granular - very high damping (inter-particle friction)
        case Type::SAND:         return 0.50f;  // Loose particles, maximum damping
        case Type::DIRT:         return 0.45f;  // Compacted granular

        // Mineral - medium damping (rigid but heavy, need to settle stacks)
        case Type::STONE:        return 0.40f;  // V4.9: increased for stacked settling
        case Type::CONCRETE:     return 0.45f;  // Heavy, needs to settle
        case Type::BRICK:        return 0.50f;  // Stacked tiles must settle quickly

        // Metal - very low damping (elastic, rigid)
        case Type::IRON:         return 0.05f;
        case Type::STEEL:        return 0.05f;
        case Type::ALUMINUM:     return 0.08f;  // Softer metal
        case Type::GOLD:         return 0.10f;  // Soft metal

        // Special
        case Type::LIGHT:        return 0.0f;   // Massless, no damping
        case Type::HEAVY_STATIC: return 1.0f;   // High damping (still movable, settles fast)

        default:                 return 0.20f;  // Default moderate
    }
}
#pragma GCC diagnostic pop

// Compute combined damping for two materials in contact/gluon
// Uses average - both materials contribute to energy dissipation
constexpr float GetCombinedDamping(Type type_a, Type type_b) {
    return (GetDamping(type_a) + GetDamping(type_b)) * 0.5f;
}

} // namespace Materials
