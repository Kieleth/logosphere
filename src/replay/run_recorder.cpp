#include "logosphere/replay/run_recorder.h"

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_query.h"

#include <sstream>

namespace logosphere::replay {
namespace {

namespace onto = logosphere::ontology;

// JSON string escaping, by hand. The repo deliberately carries no JSON
// library, and a trace nobody can parse is worse than none.
std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char esc[8];
                    std::snprintf(esc, sizeof(esc), "\\u%04x", c);
                    out += esc;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

std::string field(const char* key, const std::optional<std::string>& value) {
    if (!value) return {};
    return ",\"" + std::string(key) + "\":" + quote(*value);
}

std::string field(const char* key, const std::optional<int32_t>& value) {
    if (!value) return {};
    return ",\"" + std::string(key) + "\":" + std::to_string(*value);
}

std::string field(const char* key, const std::optional<float>& value) {
    if (!value) return {};
    return ",\"" + std::string(key) + "\":" + std::to_string(*value);
}

// Every event here derives from WorldEvent, so who it was about and
// what it carried is written once rather than per channel.
std::string world_fields(const onto::WorldEvent& e) {
    std::string out = field("entity", e.source_entity_id) +
                      field("target", e.target_entity_id);
    for (size_t i = 0; i < e.payload_keys.size() &&
                       i < e.payload_values.size(); ++i) {
        out += "," + quote(e.payload_keys[i]) + ":" +
               quote(e.payload_values[i]);
    }
    return out;
}

}  // namespace

RunRecorder::RunRecorder(logosphere::EventBus& bus, RecordSpec spec)
    : bus_(bus), spec_(spec) {
    // Channel tags are local to this file and only exist so detach()
    // can unsubscribe from the right channel.
    enum Tag { kDice, kState, kRelation, kDamage, kDeath, kSpawn, kTransform };

    if (spec_.dice) {
        subscriptions_.emplace_back(kDice, bus_.dice_rolls().subscribe(
            [this](const onto::DiceRollEvent& e) {
                write("dice", field("roll_id", e.roll_id) +
                                  field("expression", e.dice_expression) +
                                  field("values", e.roll_values) +
                                  field("total", e.roll_total) +
                                  field("stream", e.roll_stream) +
                                  field("purpose", e.roll_purpose));
            }));
    }
    if (spec_.state_changes) {
        subscriptions_.emplace_back(kState, bus_.state_changes().subscribe(
            [this](const onto::WorldEvent& e) {
                write("state", world_fields(e));
            }));
    }
    if (spec_.relations) {
        subscriptions_.emplace_back(kRelation, bus_.relations().subscribe(
            [this](const onto::RelationEvent& e) {
                write("relation", world_fields(e) + ",\"relation\":" +
                                      quote(e.relation_type));
            }));
    }
    if (spec_.damage) {
        subscriptions_.emplace_back(kDamage, bus_.damage().subscribe(
            [this](const onto::DamageEvent& e) {
                write("damage", world_fields(e) +
                                    field("amount", e.damage_amount));
            }));
    }
    if (spec_.deaths) {
        subscriptions_.emplace_back(kDeath, bus_.deaths().subscribe(
            [this](const onto::DeathEvent& e) {
                write("death", world_fields(e));
            }));
    }
    if (spec_.spawns) {
        subscriptions_.emplace_back(kSpawn, bus_.spawns().subscribe(
            [this](const onto::SpawnEvent& e) {
                write("spawn", world_fields(e));
            }));
    }
    if (spec_.transformations) {
        subscriptions_.emplace_back(kTransform,
            bus_.transformations().subscribe(
                [this](const onto::TransformationEvent& e) {
                    write("transformation", world_fields(e) +
                                                field("rule", e.rule_name));
                }));
    }
    attached_ = true;
}

RunRecorder::~RunRecorder() { detach(); }

void RunRecorder::detach() {
    if (!attached_) return;
    enum Tag { kDice, kState, kRelation, kDamage, kDeath, kSpawn, kTransform };
    for (const auto& [tag, id] : subscriptions_) {
        switch (tag) {
            case kDice:      bus_.dice_rolls().unsubscribe(id); break;
            case kState:     bus_.state_changes().unsubscribe(id); break;
            case kRelation:  bus_.relations().unsubscribe(id); break;
            case kDamage:    bus_.damage().unsubscribe(id); break;
            case kDeath:     bus_.deaths().unsubscribe(id); break;
            case kSpawn:     bus_.spawns().unsubscribe(id); break;
            case kTransform: bus_.transformations().unsubscribe(id); break;
            default: break;
        }
    }
    subscriptions_.clear();
    attached_ = false;
}

void RunRecorder::write(const std::string& channel,
                        const std::string& payload) {
    if (spec_.max_records != 0 && records_ >= spec_.max_records) {
        ++dropped_;
        return;
    }
    // The sequence is this recorder's own, dense and global. Per
    // channel numbering cannot order two channels against each other,
    // and the order events actually happened in is the point.
    std::ostringstream line;
    line << "{\"seq\":" << records_ << ",\"channel\":" << quote(channel)
         << payload << "}";
    append_jsonl("run.jsonl", line.str());
    ++records_;
}

void RunRecorder::snapshot_kg(const kg::KGModule& world,
                              const kg::Query& query) {
    // Deterministic by construction: type order as given, ids
    // ascending, properties sorted. Two runs of the same world produce
    // byte-identical output, which is what makes a trace diffable.
    const auto rows = kg::run_query(world, query);
    append_jsonl("kg0.jsonl", kg::render_query_json(rows));
}

}  // namespace logosphere::replay
