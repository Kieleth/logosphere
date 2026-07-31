#ifndef LOGOSPHERE_KG_KG_OPS_H
#define LOGOSPHERE_KG_KG_OPS_H

// KGOp vocabulary — the typed surface an LLM director (or any
// agent that authors the KG remotely) speaks. Generic engine
// surface; games only care about the parser + a host-side apply
// loop, which is also game-agnostic.
//
// Wire shape from the LLM (per Phase C of the v1 plan):
//
//   {
//     "thoughts": "...",
//     "ops": [
//       {"op":"create_entity","type":"TrailSegment",
//        "properties":{"start_x":"-5","start_y":"0",...}},
//       {"op":"destroy_entity","target":"@some_alias"},
//       {"op":"set_property","target":"@ai_cycle",
//        "property":"max_speed","value":"12"},
//       {"op":"set_relation","from":"@a","relation":"PARENT_OF",
//        "to":"@b"}
//     ]
//   }
//
// Targets accept either a numeric entity id ("7") or a symbolic
// alias ("@player_cycle"). Symbolic resolution happens host-side
// via a SymbolicRefs-like helper; the engine vocabulary stays
// game-agnostic.

#include "logosphere/kg/kg_types.h"

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kg {

// Reference to an entity. Either a resolved numeric id, or a
// symbolic name the host resolves at apply time. Empty `symbolic`
// means use `id`. Symbolic strings are stored WITHOUT the leading
// '@' (the parser strips it).
struct EntityRef {
    EntityID    id = INVALID_ENTITY;
    std::string symbolic;

    bool is_symbolic() const { return !symbolic.empty(); }
    bool is_numeric()  const { return symbolic.empty() && id != INVALID_ENTITY; }
};

// --- Op variants --------------------------------------------------

struct KGOpCreateEntity {
    std::string type;
    std::vector<std::pair<std::string, std::string>> properties;
};

struct KGOpDestroyEntity {
    EntityRef target;
};

struct KGOpSetProperty {
    EntityRef   target;
    std::string property;
    std::string value;  // KG stores all properties as strings
};

struct KGOpSetRelation {
    EntityRef   from;
    std::string relation;
    EntityRef   to;
};

// Request a named, host-registered cinematic. The KG itself
// doesn't change — the host's cinematic registry decides what
// the name maps to (a humanoid spawning, a disk thrown at a
// target, a camera flourish, etc.). Engine-generic; the
// vocabulary stays game-agnostic by leaving the catalog of
// names entirely game-side. Validator accepts any name; the
// cinematic registry is the gate at apply time.
struct KGOpPlayCinematic {
    std::string                                          name;
    EntityRef                                            target;  // optional; INVALID_ENTITY if none
    std::vector<std::pair<std::string, std::string>>     params;
};

using KGOp = std::variant<KGOpCreateEntity,
                          KGOpDestroyEntity,
                          KGOpSetProperty,
                          KGOpSetRelation,
                          KGOpPlayCinematic>;

// --- Helpers (defined in kg_ops_parse.cpp so includers don't
// have to pull in <variant> impl details).
const char* kg_op_kind_name(const KGOp& op);

}  // namespace kg

#endif  // LOGOSPHERE_KG_KG_OPS_H
