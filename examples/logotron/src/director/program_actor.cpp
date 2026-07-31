#include "director/program_actor.h"

#include "core/engine.h"
#include "core/particle_system.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/worldgen/humanoid_generator.h"

#include <iostream>
#include <vector>

namespace logotron::director {

namespace {

// HumanoidSpec for the Tron Program. Glow-y circuit colors,
// human-ish proportions. KISS: no eyes, default human spec
// tweaked for darker clothing + emissive accent.
HumanoidSpec program_spec() {
    HumanoidSpec s = HumanoidSpec::default_human();
    // Pale, slightly cyan skin reads as "Program" under the
    // arena's blue lighting.
    s.skin_r = 0.85f; s.skin_g = 0.90f; s.skin_b = 0.95f;
    // Dark suit with cyan highlights — classic Tron.
    s.clothing_r = 0.05f; s.clothing_g = 0.10f; s.clothing_b = 0.20f;
    // Eyes off (KISS, fewer particles).
    s.has_eyes = false;
    return s;
}

}  // namespace

ProgramActorHandle spawn(Engine& engine, float x, float y, float z) {
    ProgramActorHandle handle;

    auto& wg = engine.get_worldgen_system();
    auto& humanoid_gen = wg.get_humanoid_generator();

    // Generate the rig. floor=-1 lets the generator skip floor
    // creation (the arena floor already exists). kg_support=false
    // keeps particles in ParticleSystem directly (matches Eden's
    // pattern; the humanoid is transient, not a chunk-loaded entity).
    PhysicsHumanoidResult rig = humanoid_gen.generate_humanoid_physics(
        x, y, z, /*floor_particle_id=*/-1, program_spec(),
        /*kg_support=*/false);

    if (rig.entity_id == kg::INVALID_ENTITY) {
        std::cerr << "[program_actor] generate_humanoid_physics failed; "
                  << "Program will not appear" << std::endl;
        return handle;
    }

    // Wire body part entities + KG body graph. Using engine's
    // "Humanoid" type for now (known to ontology). TODO: subclass
    // as a Logotron-specific "Program" type in logotron.yaml so
    // the LLM Director can distinguish a Program from any other
    // humanoid that might land in the KG. Until then we tell the
    // LLM via prompt that humanoids it sees in the arena are
    // Programs.
    auto& kg = engine.get_kg();
    rig.create_kg_entities(kg, /*entity_type=*/"Humanoid",
                           /*reflexes_ms=*/180.0f,
                           /*grit_W=*/600.0f);

    // KISS: no animation registration. The Program just stands
    // there. Big TODO: register_humanoid_direct + a "lift overhead"
    // FK pose once the cinematic deserves animated detail.

    handle.root_entity = rig.entity_id;
    return handle;
}

void despawn(Engine& engine, ProgramActorHandle handle) {
    if (!handle.valid()) return;

    auto& kg = engine.get_kg();
    auto& ps = engine.get_particle_system();

    // unloadEntity walks HAS_PART recursively and returns every
    // render index owned by the rig (root + Torso + Head + Hips +
    // limbs). Marks them unloaded so reload paths know they're gone.
    auto unload = kg.unloadEntity(handle.root_entity);
    if (!unload.render_indices.empty()) {
        std::vector<int> idxs;
        idxs.reserve(unload.render_indices.size());
        for (auto r : unload.render_indices) {
            idxs.push_back(static_cast<int>(r));
        }
        ps.delete_particles_immediate(idxs);
    }

    // Destroy the root entity. Body part entities (Torso, Head,
    // etc.) dangle in KG without particles — acceptable for KISS;
    // they're cheap. Big TODO: walk HAS_PART relations and destroy
    // each child entity, or add a kg.destroyEntityRecursive helper
    // engine-side.
    kg.destroyEntity(handle.root_entity);
}

}  // namespace logotron::director
