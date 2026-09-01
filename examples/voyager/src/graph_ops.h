// The handful of op builders this game writes the graph with.
//
// Every write goes through the validated batch path, so these are
// the only shapes of write there are: create an entity under an
// alias, set a property, relate the character to something the batch
// just created. Shared by the session and the effect catalog because
// two private copies of "how to write an op" is how one of them drifts.

#ifndef VOYAGER_GRAPH_OPS_H
#define VOYAGER_GRAPH_OPS_H

#include "logosphere/kg/kg_ops_transaction.h"

#include <charconv>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace voyager {

inline kg::KGOp set_property(kg::EntityID target, std::string property,
                             std::string value) {
    kg::KGOpSetProperty op;
    op.target.id = target;
    op.property = std::move(property);
    op.value = std::move(value);
    return op;
}

inline kg::KGOp create_entity(
    std::string type, std::string alias,
    std::vector<std::pair<std::string, std::string>> properties) {
    kg::KGOpCreateEntity op;
    op.type = std::move(type);
    op.as = std::move(alias);
    op.properties = std::move(properties);
    return op;
}

inline kg::KGOp relate(kg::EntityID from, std::string relation,
                       std::string to) {
    kg::KGOpSetRelation op;
    op.from.id = from;
    op.relation = std::move(relation);
    op.to.symbolic = std::move(to);
    return op;
}

// A property whose value is an entity created earlier in the same
// batch, named by its alias. The batch resolves it; nothing here has to
// know the id before it exists.
inline kg::KGOp set_property_ref(kg::EntityID target, std::string property,
                                 std::string alias) {
    kg::KGOpSetProperty op;
    op.target.id = target;
    op.property = std::move(property);
    op.value = "@" + alias;
    return op;
}

inline bool as_int(const std::string& text, long long& out) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    if (*begin == '+') ++begin;
    const auto parsed = std::from_chars(begin, end, out);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

inline std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace voyager

#endif  // VOYAGER_GRAPH_OPS_H
