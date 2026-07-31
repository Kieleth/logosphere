// Renderers over journal entries — the Knowledge layer's history
// blocks. Both take the stamped entries a reader's drain_entries()
// or a channel's collect_since() returns and produce compact,
// deterministic text for meta-agent prompts, logs, or inspectors.
// The engine renders the neutral form; game voice stays game-side.
//
// Design: docs/KNOWLEDGE_LAYER.md. Header-only; T must be
// a WorldEvent-derived ontology event (event_type, optional
// source/target ids, payload_keys/payload_values).

#pragma once

#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace logosphere {

// One line per entry, oldest first:
//   [seq] t=12.30 DEATH target=7 source=3 cause=wall x=44.5
template <typename Entry>
std::string render_journal_compact(const std::vector<Entry>& entries) {
    std::ostringstream os;
    for (const auto& en : entries) {
        const auto& e = en.event;
        os << '[' << en.seq << "] t=" << en.game_time;
        if (!e.event_type.empty()) os << ' ' << e.event_type;
        if (e.target_entity_id) os << " target=" << *e.target_entity_id;
        if (e.source_entity_id) os << " source=" << *e.source_entity_id;
        for (size_t i = 0;
             i < e.payload_keys.size() && i < e.payload_values.size(); ++i) {
            os << ' ' << e.payload_keys[i] << '=' << e.payload_values[i];
        }
        os << '\n';
    }
    return os.str();
}

// Fold state_changes entries into net deltas: one line per
// (entity, property) that actually changed across the window, first
// prev -> last value, no-op round trips dropped. Ordered by entity
// id then property name. Output:
//   7 max_speed: 8 -> 12
template <typename Entry>
std::string render_state_deltas(const std::vector<Entry>& entries) {
    auto payload = [](const auto& e, const char* key) -> const std::string* {
        for (size_t i = 0;
             i < e.payload_keys.size() && i < e.payload_values.size(); ++i) {
            if (e.payload_keys[i] == key) return &e.payload_values[i];
        }
        return nullptr;
    };

    // (entity, property) -> (first prev, last value). Ordered map for
    // determinism; entity ids are numeric strings, so compare them by
    // (length, lexicographic) to get numeric order without parsing.
    struct KeyLess {
        bool operator()(const std::pair<std::string, std::string>& a,
                        const std::pair<std::string, std::string>& b) const {
            if (a.first.size() != b.first.size())
                return a.first.size() < b.first.size();
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        }
    };
    std::map<std::pair<std::string, std::string>,
             std::pair<std::string, std::string>, KeyLess> deltas;
    for (const auto& en : entries) {
        const auto& e = en.event;
        if (e.event_type != "STATE_CHANGE" || !e.target_entity_id) continue;
        const auto* prop = payload(e, "property");
        const auto* value = payload(e, "value");
        if (!prop || !value) continue;
        const auto* prev = payload(e, "prev");

        auto key = std::make_pair(*e.target_entity_id, *prop);
        auto it = deltas.find(key);
        if (it == deltas.end()) {
            deltas.emplace(std::move(key),
                           std::make_pair(prev ? *prev : std::string{},
                                          *value));
        } else {
            it->second.second = *value;  // latest wins; first prev kept
        }
    }

    std::ostringstream os;
    for (const auto& [key, delta] : deltas) {
        if (delta.first == delta.second) continue;  // net no-op
        os << key.first << ' ' << key.second << ": "
           << (delta.first.empty() ? "(unset)" : delta.first)
           << " -> " << delta.second << '\n';
    }
    return os.str();
}

}  // namespace logosphere
