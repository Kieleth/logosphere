#include "logosphere/rules/lookup_table_selector.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace logosphere::rules {
namespace {

using kg::EntityID;

struct Row {
    EntityID id = kg::INVALID_ENTITY;
    bool low_unbounded = false;
    bool high_unbounded = false;
    int64_t low = 0;
    int64_t high = 0;
};

LookupTableResult failure(std::string error) {
    LookupTableResult result;
    result.error = std::move(error);
    return result;
}

bool parse_integer(const std::string& value, const std::string& field,
                   int64_t& out, std::string& error) {
    if (value.empty()) return true;
    try {
        size_t end = 0;
        const long long parsed = std::stoll(value, &end);
        if (end != value.size()) throw std::invalid_argument("trailing");
        out = static_cast<int64_t>(parsed);
        return true;
    } catch (...) {
        error = "invalid integer '" + field + "': '" + value + "'";
        return false;
    }
}

bool parse_flag(const std::string& value, const std::string& field,
                bool& out, std::string& error) {
    if (value.empty() || value == "false" || value == "0") {
        out = false;
        return true;
    }
    if (value == "true" || value == "1") {
        out = true;
        return true;
    }
    error = "invalid boolean '" + field + "': '" + value + "'";
    return false;
}

bool read_side(const kg::KGModule& world, EntityID row,
               const std::string& bound_name,
               const std::string& flag_name, int64_t& bound,
               bool& unbounded, std::string& error) {
    const std::string bound_value = world.getProperty(row, bound_name);
    if (!parse_integer(bound_value, bound_name, bound, error) ||
        !parse_flag(world.getProperty(row, flag_name), flag_name,
                    unbounded, error)) {
        return false;
    }
    if (!bound_value.empty() && unbounded) {
        error = "row declares both " + bound_name + " and " + flag_name;
        return false;
    }
    if (bound_value.empty() && !unbounded) {
        error = "row is missing " + bound_name +
                " without " + flag_name + "=true";
        return false;
    }
    return true;
}

bool read_rows(const kg::KGModule& world, EntityID table,
               const std::string& entry_type, std::vector<Row>& rows,
               std::string& error) {
    const auto parts = world.getRelated(table, "HAS_PART");
    if (parts.empty()) {
        error = "LookupTable has no HAS_PART rows";
        return false;
    }

    rows.reserve(parts.size());
    for (const EntityID part : parts) {
        if (!world.exists(part)) {
            error = "LookupTable contains missing HAS_PART entity " +
                    std::to_string(part);
            return false;
        }
        const std::string actual = world.getType(part);
        if (!world.getRegistry().isSubtypeOf(actual, entry_type)) {
            error = "LookupTable declares entry_type '" + entry_type +
                    "' but row " + std::to_string(part) + " has type '" +
                    actual + "'";
            return false;
        }

        Row row;
        row.id = part;
        if (!read_side(world, part, "key_min", "key_min_unbounded",
                       row.low, row.low_unbounded, error) ||
            !read_side(world, part, "key_max", "key_max_unbounded",
                       row.high, row.high_unbounded, error)) {
            error = "LookupEntry " + std::to_string(part) + ": " + error;
            return false;
        }
        if (!row.low_unbounded && !row.high_unbounded &&
            row.low > row.high) {
            error = "LookupEntry " + std::to_string(part) +
                    " has a lower bound above its upper bound";
            return false;
        }
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
        if (left.low_unbounded != right.low_unbounded)
            return left.low_unbounded;
        if (!left.low_unbounded && left.low != right.low)
            return left.low < right.low;
        if (left.high_unbounded != right.high_unbounded)
            return !left.high_unbounded;
        if (!left.high_unbounded && left.high != right.high)
            return left.high < right.high;
        return left.id < right.id;
    });

    for (size_t i = 1; i < rows.size(); ++i) {
        const Row& previous = rows[i - 1];
        const Row& current = rows[i];
        if (previous.high_unbounded || current.low_unbounded ||
            current.low <= previous.high) {
            error = "LookupTable row bands overlap";
            return false;
        }
    }
    return true;
}

bool contains(const Row& row, int64_t key) {
    return (row.low_unbounded || key >= row.low) &&
           (row.high_unbounded || key <= row.high);
}

std::string preflight(const kg::KGModule& world, EntityID table,
                      std::vector<Row>& rows) {
    if (!world.exists(table)) {
        return "LookupTable entity does not exist: " +
               std::to_string(table);
    }
    const std::string type = world.getType(table);
    if (!world.getRegistry().isSubtypeOf(type, "LookupTable")) {
        return "entity " + std::to_string(table) + " has type " + type +
               ", not LookupTable";
    }

    const std::string entry_type = world.getProperty(table, "entry_type");
    const auto& ontology = world.getRegistry();
    if (!ontology.hasEntityType(entry_type)) {
        return "LookupTable has unknown entry_type '" + entry_type + "'";
    }
    if (!ontology.isSubtypeOf(entry_type, "LookupEntry")) {
        return "LookupTable entry_type '" + entry_type +
               "' is not a LookupEntry subtype";
    }
    if (ontology.isAbstract(entry_type)) {
        return "LookupTable entry_type '" + entry_type + "' is abstract";
    }

    std::string error;
    if (!read_rows(world, table, entry_type, rows, error)) return error;
    return {};
}

}  // namespace

LookupTableResult LookupTableSelector::select(EntityID table,
                                               int64_t key) const {
    std::vector<Row> rows;
    std::string error = preflight(kg_, table, rows);
    if (!error.empty()) return failure(std::move(error));

    const auto found = std::find_if(rows.begin(), rows.end(),
                                    [key](const Row& row) {
                                        return contains(row, key);
                                    });
    if (found == rows.end()) {
        return failure("LookupTable has no row for key " +
                       std::to_string(key));
    }

    LookupTableResult result;
    result.selection = LookupTableSelection(table, found->id, key);
    return result;
}

std::string LookupTableSelector::validate(EntityID table) const {
    std::vector<Row> rows;
    return preflight(kg_, table, rows);
}

}  // namespace logosphere::rules
