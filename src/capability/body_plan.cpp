#include "logosphere/capability/body_plan.h"
#include "logosphere/kg/kg_module.h"

namespace body_plan {

void declare_biped(kg::KGModule& kg, kg::EntityID entity) {
    kg.setProperty(entity, "cap.locomotion.expected_count", "2");
    kg.setProperty(entity, "cap.locomotion.default_mode", "average");
    kg.setProperty(entity, "cap.manipulation.expected_count", "2");
    kg.setProperty(entity, "cap.manipulation.default_mode", "average");
    kg.setProperty(entity, "cap.rotation.expected_count", "1");
    kg.setProperty(entity, "cap.rotation.default_mode", "minimum");
    kg.setProperty(entity, "cap.perception.expected_count", "1");
    kg.setProperty(entity, "cap.perception.default_mode", "minimum");
}

void declare_serpent(kg::KGModule& kg, kg::EntityID entity, int segment_count) {
    kg.setProperty(entity, "cap.undulation.expected_count", std::to_string(segment_count));
    kg.setProperty(entity, "cap.undulation.default_mode", "weighted_sum");
    kg.setProperty(entity, "cap.undulation.normalize", "1.0");
    kg.setProperty(entity, "cap.perception.expected_count", "1");
    kg.setProperty(entity, "cap.perception.default_mode", "minimum");
}

void declare_winged(kg::KGModule& kg, kg::EntityID entity, int wing_pairs) {
    int flight_parts = wing_pairs * 2 + 1;  // wings + thorax
    kg.setProperty(entity, "cap.flight.expected_count", std::to_string(flight_parts));
    kg.setProperty(entity, "cap.flight.default_mode", "binary");
    kg.setProperty(entity, "cap.perception.expected_count", "1");
    kg.setProperty(entity, "cap.perception.default_mode", "minimum");
}

void declare_quadruped(kg::KGModule& kg, kg::EntityID entity) {
    kg.setProperty(entity, "cap.locomotion.expected_count", "4");
    kg.setProperty(entity, "cap.locomotion.default_mode", "average");
    kg.setProperty(entity, "cap.rotation.expected_count", "1");
    kg.setProperty(entity, "cap.rotation.default_mode", "minimum");
    kg.setProperty(entity, "cap.perception.expected_count", "1");
    kg.setProperty(entity, "cap.perception.default_mode", "minimum");
}

kg::EntityID create_capability_part(
    kg::KGModule& kg, kg::EntityID parent,
    const std::string& part_type, const std::string& part_name,
    float max_hp, const std::string& capability, float weight,
    const std::string& side)
{
    kg::EntityID part = kg.createEntity(part_type);
    kg.setProperty(part, "body_part_name", part_name);
    kg.setProperty(part, "health", std::to_string(max_hp));
    kg.setProperty(part, "max_health", std::to_string(max_hp));
    kg.setProperty(part, "cap_list", capability);
    kg.setProperty(part, "cap." + capability + ".weight", std::to_string(weight));
    if (!side.empty())
        kg.setProperty(part, "cap." + capability + ".side", side);
    kg.createRelation(parent, "HAS_PART", part);
    return part;
}

} // namespace body_plan
