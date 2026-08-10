#ifndef PARTICLE_CORE_H
#define PARTICLE_CORE_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <memory>

#include "particle_types.h"
#include "materials.h"
#include "particle_geometry_v2.h"  // Surface (returned by GetSurfaces)
#include "math/quat.h"             // logosphere::Quat for 3-axis angular state

// Core physics particle. The shape branch in GetVolume / GetMomentOfInertia
// keeps BOX on the legacy formulas while SPHERE and ELLIPSOID use the
// analytical closed forms.
struct Particle {
    // Position and velocity (world units).
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;

    // Force accumulator (Newtons, reset each frame).
    float fx = 0.0f, fy = 0.0f, fz = 0.0f;

    // Visual.
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float size = 1.0f;

    // Shape + per-axis extents.
    //
    // !!! PHYSICS COLLISION WARNING !!!
    // Physics treats (width, height, thickness) as WORLD-AXIS extents, ignoring
    // rotation_x/y/z. If your particle is horizontal (e.g., a root), set
    // thickness to the CROSS-SECTION, not the length.
    ParticleShape shape = ParticleShape::BOX;
    float width     = 1.0f;   // World X extent (≥0.15 for visibility)
    float height    = 1.0f;   // World Y extent (≥0.15)
    float thickness = 0.15f;  // World Z extent (≥0.15)

    // Facing direction (radians, 0 = +Y / north).
    float facing_angle = 0.0f;

    // Euler rotation (radians, applied X → Y → Z).
    float rotation_x = 0.0f;
    float rotation_y = 0.0f;
    float rotation_z = 0.0f;

    // FK rotation (accumulated joint angles, separate from visual rotation).
    float fk_rotation_x = 0.0f;
    float fk_rotation_y = 0.0f;
    float fk_rotation_z = 0.0f;

    // Angular physics.
    //
    // Scalar Z-axis fields are the today-path; the solver integrates
    // omega_z on rotation_z, FK cascade reads/writes rotation_z, and
    // the gluon PD (Phase 1) acts on target_relative_rotation (scalar
    // around Z). They stay live until the rotational-DOF upgrade
    // retires them.
    //
    // The quaternion + 3-vector trio below is the upgrade target. The
    // upgrade lands in stages: Stage 1 adds the fields (here, no-op),
    // Stage 2 wires integration, Stage 3 adds 3-axis gluon drive,
    // Stage 4 migrates callers. See plan "rotational-DOF upgrade".
    float omega_z = 0.0f;
    float torque_z = 0.0f;
    float moment_of_inertia = 0.0f;  // 0 = compute on demand

    // 3-axis angular state. Default identity / zero so anything that
    // never writes them still passes through the new code paths as a
    // no-op.
    logosphere::Quat rotation_q = logosphere::Quat::identity();
    float omega_x = 0.0f, omega_y = 0.0f;   // Stage 1: unintegrated
    float torque_x = 0.0f, torque_y = 0.0f; // Stage 1: unaccumulated
    float inertia_x = 0.0f, inertia_y = 0.0f;  // 0 = compute on demand

    // When true, rotation_q is the truth for this particle's
    // orientation; the solver integrates it from omega and derives
    // Euler rotation_x/y/z from it after integration. Gluon PD
    // drives with quaternion targets set this on their child
    // particle (see ParticleDynamicsSystem::set_joint_physics_drive_q).
    //
    // When false (default), Euler remains the truth; the 3-axis
    // gluon constraint build syncs rotation_q from Euler on-the-fly
    // when it needs a quaternion view of this particle.
    bool is_quat_driven = false;

    // Light emission.
    bool  is_light_source = false;
    // Self-emissive draw without contributing to the scene-lights
    // array. Setting this true makes the deferred shader's
    // self-emissive branch fire (color * emission_strength) on the
    // particle's own pixels, but the renderer does NOT add the
    // particle to the per-pixel lights iteration / shadow ray work.
    // Use for decorative emissives — Tron trail walls, neon labels,
    // power-up auras — where you want glow without the per-light
    // shadow tax. Real lights (lamps, stadium strips) should keep
    // is_light_source = true so they actually illuminate their
    // surroundings. See the self-emissive field design notes.
    bool  is_self_emissive = false;
    float emission_strength = 0.0f;
    float emission_radius   = 30.0f;

    // Vision-memory exclusion. When true, this particle's pixels in
    // the gbuffer are NOT blended with the vision-memory grid in the
    // Pass-4 cone post-process — so a moving cycle (or any other
    // transient particle) doesn't appear in the player's "what I
    // just saw" trail. Default false: most engine particles are
    // static-ish (walls, floor, sealed trails) and SHOULD persist
    // in memory. Game flips true for movers (vehicles, NPCs, live
    // trail heads). See vision_cone_postprocess.metal + the
    // `is_dynamic_map` upload in render_pipeline.cpp.
    bool  is_dynamic = false;

    // Light interaction material.
    float reflectivity = 0.1f;
    float roughness    = 0.8f;

    // Physical material.
    float material_density = 1000.0f;
    Materials::Type material_type = Materials::Type::FLESH;

private:
    // Mass is cached from volume × density on CalculateMass(). DO NOT read
    // directly — always go through GetMass() so a zero cache still computes.
    float mass_ = 0.0f;

public:
    // Tensile strength in Pa. OrganicGluon turns it into a breaking force as
    // contact_area x strength, which is a stress criterion — so this field is
    // the material's tensile strength and nothing else.
    //
    // The initialiser is oak's 100 MPa. It is a fallback for a particle that
    // never declared a material at all, NOT a default anyone should rely on:
    // SetMaterial overwrites it with the real figure. A stone wall that never
    // called SetMaterial is ten times too hard to break, and it will look
    // fine right up until someone leans on it.
    float material_strength = Materials::GetTensileStrength(Materials::Type::WOOD_HARD);

    bool is_sleeping = false;

    // Declaring a material sets everything the material decides: how heavy it
    // is, and how hard it is to pull apart. Callers that want a different
    // strength must say so AFTER this call, and the four physics generators
    // (humanoid, organic, tree, rock) do exactly that.
    void SetMaterial(Materials::Type type) {
        material_type = type;
        material_density = Materials::GetDensity(type);
        material_strength = Materials::GetTensileStrength(type);
    }

    // Friction (Coulomb, combined = min of the two contact friction values).
    // Set to 0 for volitional entities (Eva, NPCs) where friction fights the
    // animation velocity and causes gluon-chain desync.
    float friction = 0.5f;

    // Equilibrium state (at-rest particles skip contact generation).
    bool     is_at_rest = false;
    uint8_t  frames_at_rest = 0;
    uint16_t low_velocity_frames = 0;

    ParticleOwner owner = ParticleOwner::PHYSICS;
    // Physics-layer solver authority. Independent of `owner` (which is a
    // game-layer bookkeeping concept). DYNAMIC = integrate normally;
    // KINEMATIC = external writer owns position, physics treats as
    // inv_mass=0 and skips integration. See ParticleSolverMode docs.
    ParticleSolverMode solver_mode = ParticleSolverMode::DYNAMIC;

    // Procedural pattern ID for GPU shader material differentiation.
    // 0 = flat color, 1 = wood, 2 = stone (see apply_lighting_deferred.metal).
    uint8_t pattern_id = 0;

    // Odor (creature smell detection).
    OdorType odor_type = OdorType::NONE;
    float    odor_radius    = 0.0f;
    float    odor_intensity = 0.0f;

    uint32_t particle_id = 0;

    // ⚠️ MONSTER WARNING: DO NOT set entity_id on particles added to the
    // ParticleSystem. Non-zero entity_id corrupts the GPU shadow pipeline.
    // Field exists for UI object picking only. See TODO[ARCH-010].
    uint32_t entity_id = 0;

    // Interaction profile: KG EntityID of a ParticleInteractionProfile
    // (0 = INVALID_ENTITY = engine default: rigid-contact everything,
    // no medium). Consulted by ParticleInteractionSystem at contact
    // broad phase; physics reads only this id, never game categories.
    // See the particle-interaction design notes.
    uint32_t interaction_profile_id = 0;

    // Geometry.
    std::vector<Surface> GetSurfaces() const;
    // Appending variant for hot loops: fills a caller-owned buffer so the
    // per-particle-per-frame heap allocation disappears. Clears `out` first.
    void GetSurfacesInto(std::vector<Surface>& out) const;

    // Fast shadow-ray triangle path: raw floats, 9 per triangle
    // (v0.xyz, v1.xyz, v2.xyz). ~5–10× faster than GetSurfaces().
    void GetShadowTriangles(std::vector<float>& out_vertices) const;

    // Analytical volume by shape.
    float GetVolume() const {
        switch (shape) {
            case ParticleShape::SPHERE: {
                float r = size * 0.5f;
                return (4.0f / 3.0f) * 3.14159265358979323846f * r * r * r;
            }
            case ParticleShape::ELLIPSOID: {
                float a = width * 0.5f, b = height * 0.5f, c = thickness * 0.5f;
                return (4.0f / 3.0f) * 3.14159265358979323846f * a * b * c;
            }
            case ParticleShape::BOX:
            default:
                return width * height * thickness;
        }
    }

    // Called by ParticleSystem::add_particle() — do NOT call manually.
    void CalculateMass() { mass_ = GetVolume() * material_density; }

    float GetMass() const {
        if (mass_ == 0.0f) return GetVolume() * material_density;
        return mass_;
    }

    // Runtime mass updates wake the particle: `is_at_rest` is a solver
    // optimization meaning "force balance holds," and a mass change
    // invalidates that balance.
    void SetMass(float m) {
        mass_ = m;
        is_at_rest = false;
        frames_at_rest = 0;
    }

    bool IsFloating() const { return material_density == 0.0f; }

    // Z-axis moment of inertia by shape.
    // BOX uses the legacy solid-cylinder approximation; SPHERE uses 0.4 m r²;
    // ELLIPSOID uses 0.2 m (a² + b²) for rotation about Z.
    float GetMomentOfInertia() const {
        if (moment_of_inertia != 0.0f) return moment_of_inertia;
        float m = GetMass();
        switch (shape) {
            case ParticleShape::SPHERE: {
                float r = size * 0.5f;
                return 0.4f * m * r * r;
            }
            case ParticleShape::ELLIPSOID: {
                float a = width  * 0.5f;
                float b = height * 0.5f;
                return 0.2f * m * (a * a + b * b);
            }
            case ParticleShape::BOX:
            default: {
                float r = (width + height) * 0.25f;
                return 0.5f * m * r * r;
            }
        }
    }

    // Inertia about an arbitrary WORLD axis, from the body's real extents.
    //
    // GetMomentOfInertia() above returns ONE scalar, built from width and
    // height only, and the solver has been using it for rotation about all
    // three axes alike. For anything that is not roughly cubic that is wrong
    // by the aspect ratio squared: a 40 x 3 x 300 mm grass blade gets
    // 5.8e-5*m where its true transverse inertia is 7.5e-3*m, so the solver
    // believes the blade is 130x easier to tip over than it is. Nothing
    // catches it because the error is internally consistent: the same wrong
    // number is used to size the impulse and to apply it.
    //
    // A box's inertia is diagonal in its own frame, so the honest answer is
    // to rotate the world axis into body space and project. No tensor
    // storage, no new state: the extents already say everything.
    float GetInertiaAboutAxis(float ax, float ay, float az) const {
        const float m = GetMass();
        if (m <= 0.0f) return 0.0f;
        const float w = width, h = height, t = thickness;
        float Ixx, Iyy, Izz;
        switch (shape) {
            case ParticleShape::SPHERE: {
                const float r = size * 0.5f;
                Ixx = Iyy = Izz = 0.4f * m * r * r;
                break;
            }
            case ParticleShape::ELLIPSOID: {
                const float a = w * 0.5f, b = h * 0.5f, cc = t * 0.5f;
                Ixx = 0.2f * m * (b * b + cc * cc);
                Iyy = 0.2f * m * (a * a + cc * cc);
                Izz = 0.2f * m * (a * a + b * b);
                break;
            }
            case ParticleShape::BOX:
            default: {
                Ixx = m * (h * h + t * t) / 12.0f;
                Iyy = m * (w * w + t * t) / 12.0f;
                Izz = m * (w * w + h * h) / 12.0f;
                break;
            }
        }
        // World axis -> body frame: inverse of the engine's X then Y then Z.
        const float cx = std::cos(rotation_x), sx = std::sin(rotation_x);
        const float cy = std::cos(rotation_y), sy = std::sin(rotation_y);
        const float cz = std::cos(rotation_z), sz = std::sin(rotation_z);
        const float x1 =  ax * cz + ay * sz, y1 = -ax * sz + ay * cz;
        const float x2 =  x1 * cy - az * sy, z2 =  x1 * sy + az * cy;
        const float y3 =  y1 * cx + z2 * sx, z3 = -y1 * sx + z2 * cx;
        const float bx = x2, by = y3, bz = z3;
        const float len2 = bx * bx + by * by + bz * bz;
        if (len2 < 1e-12f) return Izz;
        return (Ixx * bx * bx + Iyy * by * by + Izz * bz * bz) / len2;
    }

    float GetHalfHeight() const { return thickness * 0.5f; }
    float GetHalfWidth()  const { return width     * 0.5f; }
    float GetHalfDepth()  const { return height    * 0.5f; }
};

#endif  // PARTICLE_CORE_H
