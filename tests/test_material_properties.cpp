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
// LAWS (assert-protocol migration, 2026-08-21):
//   INV-9  derived-not-declared. This table is the INPUT side of INV-9: axial
//          k = A/(La/Ea + Lb/Eb), damping from the loss factor, bending
//          K = E*I/L. A wrong digit here propagates into every derived force
//          law in the engine, and INV-9's promise that there is never a second
//          declaration for the derivation to disagree with is only worth
//          having if the one declaration is physically possible.
//   INV-29 constants-are-inputs. Every value checked here is a declared,
//          grouped, unit-annotated engine input rather than a literal at a
//          use site; these checks are what stops the table becoming a place
//          to hide a tuning.
//   The G == E/(2(1+nu)) check is the one COMPUTED property and is an
//   identity of linear elasticity, not a convention.
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
        check(E > 0.0f, "INV-9: Young's modulus must be positive", r.name, E);
        check(GetDensity(r.t) > 0.0f, "INV-9: a solid has density", r.name,
              GetDensity(r.t));

        // Poisson's ratio is bounded by thermodynamic stability for an
        // isotropic solid: outside (-1, 0.5) the elastic tensor is not
        // positive-definite and the material gains energy when deformed.
        check(nu > -1.0f && nu < 0.5f, "INV-9: Poisson ratio inside (-1, 0.5), the thermodynamic bound for an isotropic solid",
              r.name, nu);

        // Follows from the two above, but assert it directly: a negative or
        // zero shear modulus means the material offers no resistance to
        // twisting, which nothing solid does.
        check(G > 0.0f, "INV-9: shear modulus must be positive", r.name, G);

        // Strengths are magnitudes.
        check(ft >= 0.0f, "INV-9: tensile strength non-negative", r.name, ft);
        check(fc >= 0.0f, "INV-9: compressive strength non-negative", r.name, fc);
        check(fs >= 0.0f, "INV-9: shear strength non-negative", r.name, fs);

        // Tension-vs-compression asymmetry is NOT universal, and asserting
        // that it is caught a bug in this test rather than in the table: wood
        // parallel to grain is roughly twice as strong in tension as in
        // compression, because it fails in compression by cell-wall
        // microbuckling long before the fibres can be pulled apart. The
        // correct statement is per failure mode, so it is made per class
        // below rather than here.

        // Loss factor is a dissipated fraction per radian. Above 1 the
        // material would dissipate more than the strain energy it stores.
        check(eta >= 0.0f && eta <= 1.0f, "INV-19: loss factor in [0, 1] — damping is a material dissipating, and a loss factor outside the unit interval is not a dissipation", r.name, eta);

        // Consistency with the derivation identity, checked rather than
        // assumed, because GetShearModulus is the one property that is
        // computed instead of declared.
        const float expect_G = E / (2.0f * (1.0f + nu));
        check(std::fabs(G - expect_G) <= 1e-3f * expect_G,
              "INV-9: G == E / (2(1+nu)), the identity of linear elasticity", r.name, G - expect_G);
    }

    // LIGHT is the deliberate exception: massless, no mechanics at all.
    printf("\n  light (massless): E %g, density %g\n",
           (double)GetYoungsModulus(Type::LIGHT), (double)GetDensity(Type::LIGHT));
    check(GetYoungsModulus(Type::LIGHT) == 0.0f,
          "INV-9: LIGHT carries no stiffness", "light", GetYoungsModulus(Type::LIGHT));
    check(GetDensity(Type::LIGHT) == 0.0f,
          "INV-7: LIGHT carries no mass, so the momentum door answers no for it", "light", GetDensity(Type::LIGHT));

    // Ordering the table must respect, independent of the exact figures: a
    // steel beam is stiffer than an aluminium one, which is stiffer than oak,
    // which is stiffer than meat. If a digit slips by a factor of a thousand
    // this is what catches it.
    check(GetYoungsModulus(Type::STEEL) > GetYoungsModulus(Type::ALUMINUM),
          "INV-9: steel stiffer than aluminium", "order", 0);
    check(GetYoungsModulus(Type::ALUMINUM) > GetYoungsModulus(Type::WOOD_HARD),
          "INV-9: aluminium stiffer than oak", "order", 0);
    check(GetYoungsModulus(Type::WOOD_HARD) > GetYoungsModulus(Type::WOOD_SOFT),
          "INV-9: oak stiffer than pine", "order", 0);
    check(GetYoungsModulus(Type::WOOD_SOFT) > GetYoungsModulus(Type::FLESH),
          "INV-9: pine stiffer than flesh", "order", 0);
    check(GetLossFactor(Type::FLESH) > GetLossFactor(Type::STEEL),
          "INV-19: flesh damps harder than steel", "order", 0);

    // Tension-vs-compression asymmetry, stated per failure mode.
    //
    // Brittle minerals crack from pre-existing flaws under tension and have to
    // crush the whole section under compression, so compression wins by a wide
    // margin. This is what makes stone build arches and not beams.
    for (Type t : {Type::STONE, Type::CONCRETE, Type::BRICK, Type::IRON}) {
        check(GetCompressiveStrength(t) > GetTensileStrength(t) * 2.0f,
              "INV-9: brittle, compressive far exceeds tensile (why stone builds arches, not beams)", GetName(t),
              GetCompressiveStrength(t) / GetTensileStrength(t));
    }

    // Ductile metals yield by dislocation glide, which does not care about the
    // sign of the stress, so the two are equal.
    for (Type t : {Type::STEEL, Type::ALUMINUM, Type::GOLD}) {
        check(GetCompressiveStrength(t) == GetTensileStrength(t),
              "INV-9: ductile, compressive == tensile (dislocation glide does not read the sign of the stress)", GetName(t),
              GetCompressiveStrength(t) - GetTensileStrength(t));
    }

    // Wood runs the other way, parallel to grain. Keeping this as an assertion
    // means a future edit that "corrects" wood to look like stone fails loudly.
    for (Type t : {Type::WOOD_SOFT, Type::WOOD_HARD}) {
        check(GetTensileStrength(t) > GetCompressiveStrength(t),
              "INV-9: wood, tensile exceeds compressive (grain)", GetName(t),
              GetTensileStrength(t) / GetCompressiveStrength(t));
    }

    // Sand is cohesionless. This is not a gap in the table, it is the fact:
    // dry sand carries no tension at all, which is why a sandcastle needs
    // water and a sand pile has an angle of repose.
    check(GetTensileStrength(Type::SAND) == 0.0f,
          "INV-9: sand carries no tension (cohesionless: why a sandcastle needs water and a pile has an angle of repose)", "sand", GetTensileStrength(Type::SAND));

    printf("\n  %d physical-consistency violation(s)\n", failures);
    const bool pass = (failures == 0);
    printf("\n  %s\n", pass ? "PASS" : "FAIL INV-9 (the table describes an impossible solid, and every derived force law is built on it)");
    return pass;
}

int main() {
    return test_material_properties() ? 0 : 1;
}
