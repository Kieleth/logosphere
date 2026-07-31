#include "director/respawn_ai.h"

#include "arena.h"

namespace logotron::director {

AIRespawnResult respawn_ai(kg::KGModule& kg,
                            kg::EntityID old_ai_entity,
                            const AISpawnSpec& spec,
                            std::unordered_set<kg::EntityID>* rendered_trails) {
    AIRespawnResult result;

    if (old_ai_entity != kg::INVALID_ENTITY) {
        auto trails = kg.findByProperty(
            "owner_cycle_id", std::to_string(old_ai_entity));
        for (auto t : trails) {
            kg.destroyEntity(t);
            // Keep the caller's render bookkeeping honest: a destroyed
            // entity must leave the "already rendered" set, or a
            // reused EntityID would silently never get a particle.
            if (rendered_trails) rendered_trails->erase(t);
        }
        result.trails_wiped = static_cast<int>(trails.size());
        kg.destroyEntity(old_ai_entity);
        result.old_cycle_destroyed = true;
    }

    result.new_ai_entity = spawn_cycle(
        kg, "AICycle", spec.x, spec.y, spec.direction);

    return result;
}

} // namespace logotron::director
