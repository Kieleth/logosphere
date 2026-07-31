#include "logosphere/worldgen/entity_generator.h"
#include "logosphere/events/event_bus.h"
#include <string>

void EntityGenerator::on_entity_created(kg::EntityID entity) {
    tracked_entities_.push_back(entity);

    if (event_bus_ && kg_) {
        logosphere::ontology::SpawnEvent evt;
        evt.target_entity_id = std::to_string(entity);
        evt.event_type = "SPAWN";
        event_bus_->spawns().emit(std::move(evt));
    }
}
