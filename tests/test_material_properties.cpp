// ============================================================================
// MATERIAL PROPERTIES — the physical constants must be physically possible
// ============================================================================
// Materials now carries measured mechanical properties alongside the density
// and the solver-tuned damping factor: Young's modulus, Poisson's ratio,
// tensile / compressive / shear strength, and loss factor. Bond stiffness is
// derived from these rather than declared at every creation site.
//
// A lookup table of physical constants is easy to typo and impossible to
// eyeball. This test asserts the relationships that hold for ANY real solid,
// so a wrong digit shows up as a physical impossibility rather than as a
// slightly odd-looking rig three weeks later.
//
// It deliberately does NOT assert the values themselves — that would just
// restate the table. It asserts what the table cannot violate and stay
// physical.
//
// Headless-safe: materials.h is header-only constexpr with no engine
// dependency, so this runs in the Linux CI profile too.
//
//   ./build-release/test_material_properties
// ============================================================================

#include "../src/materials.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct Row { Materials::Type t; const char* name; };

// Every real material in the enum. LIGHT is excluded from the "must have
// mechanics" checks below: it is massless by construction, not a solid.
std::vector<Row> solids() {
    using T = Materials::Type;
    return {
        {T::FLESH, "flesh"}, {T::WOOD_SOFT, "wood_soft"}, {T::WOOD_HARD, "wood_hard"},
        {T::LEAVES, "leaves"}, {T::STONE, "stone"}, {T::CONCRETE, "concrete"},
        {T::BRICK, "brick"}, {T::SAND, "sand"}, {T::DIRT, "dirt"},
        {T::IRON, "iron"}, {T::STEEL, "steel"}, {T::ALUMINUM, "aluminum"},
        {T::GOLD, "gold"},
    };
}

int failures = 0;

void check(bool ok, const char* what, const char* who, double got) {
    if (!ok) {
        printf("      *** %-10s %-38s got %g\n", who, what, got);
        failures++;
    }
}

} // namespace

bool test_material_properties() {
    printf("\n=== MATERIAL PROPERTIES: are these physically possible? ===\n");
    using namespace Materials;

    printf("\n  %-10s %10s %6s %10s %10s %10s %10s %8s\n",
           "material", "E (GPa)", "nu", "G (GPa)", "tens(MPa)", "comp(MPa)",
           "shear(MPa)", "eta");
    printf("  %s\n", "--------------------------------------------------"
                     "----------------------------------");

    for (const Row& r : solids()) {
        const float E  = GetYoungsModulus(r.t);
        const float nu = GetPoissonRatio(r.t);
        const float G  = GetShearModulus(r.t);
        const float ft = GetTensileStrength(r.t);
        const float fc = GetCompressiveStrength(r.t);
        const float fs = GetShearStrength(r.t);
        const float eta = GetLossFactor(r.t);

        printf("  %-10s %10.2f %6.2f %10.2f %10.3f %10.3f %10.3f %8.4f\n",
               r.name, E / 1e9, nu, G / 1e9, ft / 1e6, fc / 1e6, fs / 1e6, eta);

        // A solid has stiffness. Zero modulus with non-zero density is a body
        // that has mass but cannot carry load — there is no such thing.
        check(E > 0.0f, "Young's modulus must be positive", r.name, E);
        check(GetDensity(r.t) > 0.0f, "a solid has density", r.name,
              GetDensity(r.t));

        // Poisson's ratio is bounded by thermodynamic stability for an
        // isotropic solid: outside (-1, 0.5) the elastic tensor is not
        // positive-definite and the material gains energy when deformed.
        check(nu > -1.0f && nu < 0.5f, "Poisson ratio inside (-1, 0.5)",
              r.name, nu);

        // Follows from the two above, but assert it directly: a negative or
        // zero shear modulus means the material offers no resistance to
        // twisting, which nothing solid does.
        check(G > 0.0f, "shear modulus must be positive", r.name, G);

        // Strengths are magnitudes.
        check(ft >= 0.0f, "tensile strength non-negative", r.name, ft);
        check(fc >= 0.0f, "compressive strength non-negative", r.name, fc);
        check(fs >= 0.0f, "shear strength non-negative", r.name, fs);

        // Tension-vs-compression asymmetry is NOT universal, and asserting
        // that it is caught a bug in this test rather than in the table: wood
        // parallel to grain is roughly twice as strong in tension as in
        // compression, because it fails in compression by cell-wall
        // microbuckling long before the fibres can be pulled apart. The
        // correct statement is per failure mode, so it is made per class
        // below rather than here.

        // Loss factor is a dissipated fraction per radian. Above 1 the
        // material would dissipate more than the strain energy it stores.
        check(eta >= 0.0f && eta <= 1.0f, "loss factor in [0, 1]", r.name, eta);

        // Consistency with the derivation identity, checked rather than
        // assumed, because GetShearModulus is the one property that is
        // computed instead of declared.
        const float expect_G = E / (2.0f * (1.0f + nu));
        check(std::fabs(G - expect_G) <= 1e-3f * expect_G,
              "G == E / (2(1+nu))", r.name, G - expect_G);
    }

    // LIGHT is the deliberate exception: massless, no mechanics at all.
    printf("\n  light (massless): E %g, density %g\n",
           (double)GetYoungsModulus(Type::LIGHT), (double)GetDensity(Type::LIGHT));
    check(GetYoungsModulus(Type::LIGHT) == 0.0f,
          "LIGHT carries no stiffness", "light", GetYoungsModulus(Type::LIGHT));
    check(GetDensity(Type::LIGHT) == 0.0f,
          "LIGHT carries no mass", "light", GetDensity(Type::LIGHT));

    // Ordering the table must respect, independent of the exact figures: a
    // steel beam is stiffer than an aluminium one, which is stiffer than oak,
    // which is stiffer than meat. If a digit slips by a factor of a thousand
    // this is what catches it.
    check(GetYoungsModulus(Type::STEEL) > GetYoungsModulus(Type::ALUMINUM),
          "steel stiffer than aluminium", "order", 0);
    check(GetYoungsModulus(Type::ALUMINUM) > GetYoungsModulus(Type::WOOD_HARD),
          "aluminium stiffer than oak", "order", 0);
    check(GetYoungsModulus(Type::WOOD_HARD) > GetYoungsModulus(Type::WOOD_SOFT),
          "oak stiffer than pine", "order", 0);
    check(GetYoungsModulus(Type::WOOD_SOFT) > GetYoungsModulus(Type::FLESH),
          "pine stiffer than flesh", "order", 0);
    check(GetLossFactor(Type::FLESH) > GetLossFactor(Type::STEEL),
          "flesh damps harder than steel", "order", 0);

    // Tension-vs-compression asymmetry, stated per failure mode.
    //
    // Brittle minerals crack from pre-existing flaws under tension and have to
    // crush the whole section under compression, so compression wins by a wide
    // margin. This is what makes stone build arches and not beams.
    for (Type t : {Type::STONE, Type::CONCRETE, Type::BRICK, Type::IRON}) {
        check(GetCompressiveStrength(t) > GetTensileStrength(t) * 2.0f,
              "brittle: compressive far exceeds tensile", GetName(t),
              GetCompressiveStrength(t) / GetTensileStrength(t));
    }

    // Ductile metals yield by dislocation glide, which does not care about the
    // sign of the stress, so the two are equal.
    for (Type t : {Type::STEEL, Type::ALUMINUM, Type::GOLD}) {
        check(GetCompressiveStrength(t) == GetTensileStrength(t),
              "ductile: compressive == tensile", GetName(t),
              GetCompressiveStrength(t) - GetTensileStrength(t));
    }

    // Wood runs the other way, parallel to grain. Keeping this as an assertion
    // means a future edit that "corrects" wood to look like stone fails loudly.
    for (Type t : {Type::WOOD_SOFT, Type::WOOD_HARD}) {
        check(GetTensileStrength(t) > GetCompressiveStrength(t),
              "wood: tensile exceeds compressive (grain)", GetName(t),
              GetTensileStrength(t) / GetCompressiveStrength(t));
    }

    // Sand is cohesionless. This is not a gap in the table, it is the fact:
    // dry sand carries no tension at all, which is why a sandcastle needs
    // water and a sand pile has an angle of repose.
    check(GetTensileStrength(Type::SAND) == 0.0f,
          "sand carries no tension", "sand", GetTensileStrength(Type::SAND));

    printf("\n  %d physical-consistency violation(s)\n", failures);
    const bool pass = (failures == 0);
    printf("\n  %s\n", pass ? "PASS" : "FAIL (the table describes an impossible solid)");
    return pass;
}

int main() {
    return test_material_properties() ? 0 : 1;
}
