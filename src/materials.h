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

// =============================================================================
// MECHANICAL PROPERTIES
// =============================================================================
// Everything above this line describes how a material LOOKS to the solver.
// Everything below describes what it IS. The difference matters: GetDamping
// returns a per-frame velocity absorption fraction that was tuned until stacks
// settled ("STONE 0.40 — increased for stacked settling"). The functions here
// return measured physical constants that do not move when the solver changes.
//
// WHY THEY EXIST. Bond stiffness is currently declared by whoever creates the
// bond — 2000 N·m/rad for every humanoid joint, 5000 N/m for every plant on
// Earth, 100 N·m/rad for anything nobody thought about. None of that is a
// property of oak or of bone. Stiffness is derivable:
//
//     axial     k = E·A / L          A = cross-section, L = rest length
//     bending   K = E·I / L          I = second moment of area
//     torsion   K = G·J / L          J = polar second moment
//     damping   c = eta · sqrt(k·m)  eta = loss factor
//
// Give the engine E, nu and eta and it can answer for any bond between any two
// bodies of any size, including the ones nobody remembered to declare. That is
// the point: not better numbers, no numbers.
//
// SHEAR MODULUS IS NOT DECLARED. G = E / (2(1+nu)) for an isotropic solid, so
// declaring it would be a fourth place for the three to disagree.
//
// ON THE VALUES. Metals, concrete, brick and rock are handbook figures and are
// good to the significant digits shown. Wood is quoted PARALLEL TO GRAIN from
// the Forest Products Laboratory Wood Handbook ranges; real wood is strongly
// anisotropic (across the grain E is roughly a twentieth of this) and the
// engine has no grain direction yet, so the stiff axis is used throughout and
// wood will read too stiff sideways until it does.
//
// Three are honest estimates, not handbook values, and are marked at each site:
// FLESH and LEAVES span orders of magnitude in the biomechanics literature
// depending on tissue and turgor, and SAND is cohesionless — its tensile
// strength is genuinely zero and its "modulus" is a confining-pressure-
// dependent soil stiffness, not a Young's modulus. Anything that leans hard on
// those three deserves a measurement, not a lookup.
// =============================================================================

// Young's modulus (Pa). Stress per unit strain in uniaxial tension.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetYoungsModulus(Type type) {
    switch (type) {
        case Type::FLESH:        return   1.0e6f;  // ESTIMATE: bulk soft tissue
        case Type::WOOD_SOFT:    return  10.0e9f;  // pine, parallel to grain
        case Type::WOOD_HARD:    return  12.0e9f;  // oak, parallel to grain
        case Type::LEAVES:       return   5.0e6f;  // ESTIMATE: turgid leaf tissue
        case Type::STONE:        return  60.0e9f;  // granite
        case Type::CONCRETE:     return  30.0e9f;  // normal-weight, 28-day
        case Type::BRICK:        return  15.0e9f;  // fired clay
        case Type::SAND:         return  30.0e6f;  // ESTIMATE: soil stiffness, not E
        case Type::DIRT:         return  30.0e6f;  // ESTIMATE: compacted soil
        case Type::IRON:         return 110.0e9f;  // cast iron
        case Type::STEEL:        return 200.0e9f;  // mild steel
        case Type::ALUMINUM:     return  69.0e9f;
        case Type::GOLD:         return  79.0e9f;
        case Type::LIGHT:        return   0.0f;    // massless, no mechanics
        case Type::HEAVY_STATIC: return  60.0e9f;  // deprecated: mirrors STONE
        default:                 return   1.0e6f;  // unknown: soft, so it yields
                                                   // visibly rather than pretending
                                                   // to be structural
    }
}
#pragma GCC diagnostic pop

// Poisson's ratio (dimensionless). Transverse contraction per axial extension.
// Bounded (-1, 0.5); 0.5 is perfectly incompressible.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetPoissonRatio(Type type) {
    switch (type) {
        case Type::FLESH:        return 0.49f;  // near-incompressible, like water
        case Type::WOOD_SOFT:    return 0.35f;
        case Type::WOOD_HARD:    return 0.35f;
        case Type::LEAVES:       return 0.30f;  // ESTIMATE
        case Type::STONE:        return 0.25f;
        case Type::CONCRETE:     return 0.20f;
        case Type::BRICK:        return 0.15f;
        case Type::SAND:         return 0.30f;
        case Type::DIRT:         return 0.35f;
        case Type::IRON:         return 0.26f;
        case Type::STEEL:        return 0.30f;
        case Type::ALUMINUM:     return 0.33f;
        case Type::GOLD:         return 0.44f;
        case Type::LIGHT:        return 0.0f;
        case Type::HEAVY_STATIC: return 0.25f;
        default:                 return 0.30f;
    }
}
#pragma GCC diagnostic pop

// Shear modulus (Pa). DERIVED, never declared: G = E / (2(1 + nu)).
constexpr float GetShearModulus(Type type) {
    return GetYoungsModulus(type) / (2.0f * (1.0f + GetPoissonRatio(type)));
}

// Ultimate tensile strength (Pa). Stress at which the material pulls apart.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetTensileStrength(Type type) {
    switch (type) {
        case Type::FLESH:        return   1.0e6f;  // ESTIMATE
        case Type::WOOD_SOFT:    return  80.0e6f;  // parallel to grain
        case Type::WOOD_HARD:    return 100.0e6f;  // parallel to grain
        case Type::LEAVES:       return   2.0e6f;  // ESTIMATE
        case Type::STONE:        return  10.0e6f;  // rock is weak in tension
        case Type::CONCRETE:     return   3.0e6f;  // ~1/10 of compressive
        case Type::BRICK:        return   2.0e6f;
        case Type::SAND:         return   0.0f;    // cohesionless: genuinely zero
        case Type::DIRT:         return  20.0e3f;  // ESTIMATE: slight cohesion
        case Type::IRON:         return 200.0e6f;  // cast iron, weak in tension
        case Type::STEEL:        return 400.0e6f;  // mild steel
        case Type::ALUMINUM:     return 310.0e6f;  // 6061-T6
        case Type::GOLD:         return 120.0e6f;  // annealed
        case Type::LIGHT:        return   0.0f;
        case Type::HEAVY_STATIC: return  10.0e6f;
        default:                 return   1.0e6f;
    }
}
#pragma GCC diagnostic pop

// Ultimate compressive strength (Pa). Brittle materials are far stronger here
// than in tension — that asymmetry is why stone builds arches and not beams.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetCompressiveStrength(Type type) {
    switch (type) {
        case Type::FLESH:        return   1.0e6f;  // ESTIMATE
        case Type::WOOD_SOFT:    return  40.0e6f;  // parallel to grain
        case Type::WOOD_HARD:    return  50.0e6f;  // parallel to grain
        case Type::LEAVES:       return   2.0e6f;  // ESTIMATE
        case Type::STONE:        return 200.0e6f;  // granite: 20x its tension
        case Type::CONCRETE:     return  30.0e6f;
        case Type::BRICK:        return  20.0e6f;
        case Type::SAND:         return 100.0e3f;  // ESTIMATE: confinement-dependent
        case Type::DIRT:         return 200.0e3f;  // ESTIMATE
        case Type::IRON:         return 700.0e6f;  // cast iron: strong in compression
        case Type::STEEL:        return 400.0e6f;  // ductile: symmetric
        case Type::ALUMINUM:     return 310.0e6f;  // ductile: symmetric
        case Type::GOLD:         return 120.0e6f;  // ductile: symmetric
        case Type::LIGHT:        return   0.0f;
        case Type::HEAVY_STATIC: return 200.0e6f;
        default:                 return   1.0e6f;
    }
}
#pragma GCC diagnostic pop

// Ultimate shear strength (Pa). Sideways failure — how a bond tears rather
// than snaps.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetShearStrength(Type type) {
    switch (type) {
        case Type::FLESH:        return   0.5e6f;  // ESTIMATE
        case Type::WOOD_SOFT:    return   7.0e6f;  // across grain: wood's weak axis
        case Type::WOOD_HARD:    return  13.0e6f;
        case Type::LEAVES:       return   1.0e6f;  // ESTIMATE
        case Type::STONE:        return  20.0e6f;
        case Type::CONCRETE:     return   6.0e6f;
        case Type::BRICK:        return   3.0e6f;
        case Type::SAND:         return   0.0f;    // cohesionless
        case Type::DIRT:         return  15.0e3f;  // ESTIMATE
        case Type::IRON:         return 170.0e6f;
        case Type::STEEL:        return 250.0e6f;  // ~0.6x tensile, ductile metals
        case Type::ALUMINUM:     return 207.0e6f;
        case Type::GOLD:         return  70.0e6f;
        case Type::LIGHT:        return   0.0f;
        case Type::HEAVY_STATIC: return  20.0e6f;
        default:                 return   0.5e6f;
    }
}
#pragma GCC diagnostic pop

// Loss factor eta (dimensionless). Fraction of strain energy dissipated per
// radian of oscillation — the material's own internal friction, distinct from
// GetDamping's solver-tuned settling rate. Steel rings for a long time
// (eta ~ 5e-4); flesh does not ring at all (eta ~ 0.3).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
constexpr float GetLossFactor(Type type) {
    switch (type) {
        case Type::FLESH:        return 0.30f;    // ESTIMATE: heavily damped tissue
        case Type::WOOD_SOFT:    return 0.010f;
        case Type::WOOD_HARD:    return 0.008f;
        case Type::LEAVES:       return 0.20f;    // ESTIMATE
        case Type::STONE:        return 0.020f;
        case Type::CONCRETE:     return 0.020f;
        case Type::BRICK:        return 0.020f;
        case Type::SAND:         return 0.50f;    // ESTIMATE: inter-grain friction
        case Type::DIRT:         return 0.40f;    // ESTIMATE
        case Type::IRON:         return 0.0020f;  // cast iron: graphite flakes damp
        case Type::STEEL:        return 0.0005f;  // rings
        case Type::ALUMINUM:     return 0.0005f;
        case Type::GOLD:         return 0.0030f;
        case Type::LIGHT:        return 0.0f;
        case Type::HEAVY_STATIC: return 0.020f;
        default:                 return 0.050f;
    }
}
#pragma GCC diagnostic pop

} // namespace Materials
