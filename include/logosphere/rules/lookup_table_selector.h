#ifndef LOGOSPHERE_RULES_LOOKUP_TABLE_SELECTOR_H
#define LOGOSPHERE_RULES_LOOKUP_TABLE_SELECTOR_H

// Validated, data-only LookupTable selection. The selected row remains a
// game-declared type. Consumers interpret its result columns.

#include "logosphere/kg/kg_types.h"

#include <cstdint>
#include <optional>
#include <string>

namespace kg { class KGModule; }

namespace logosphere::rules {

class LookupTableSelection {
public:
    LookupTableSelection(const LookupTableSelection&) = default;
    LookupTableSelection(LookupTableSelection&&) = default;
    LookupTableSelection& operator=(const LookupTableSelection&) = default;
    LookupTableSelection& operator=(LookupTableSelection&&) = default;

    kg::EntityID table() const { return table_; }
    kg::EntityID row() const { return row_; }
    int64_t key() const { return key_; }

private:
    friend class LookupTableSelector;
    LookupTableSelection(kg::EntityID selected_table,
                         kg::EntityID selected_row, int64_t selected_key)
        : table_(selected_table), row_(selected_row), key_(selected_key) {}

    kg::EntityID table_ = kg::INVALID_ENTITY;
    kg::EntityID row_ = kg::INVALID_ENTITY;
    int64_t key_ = 0;
};

struct LookupTableResult {
    std::optional<LookupTableSelection> selection;
    std::string error;

    bool ok() const { return selection.has_value() && error.empty(); }
};

class LookupTableSelector {
public:
    explicit LookupTableSelector(const kg::KGModule& kg) : kg_(kg) {}

    // Empty means the complete table contract is valid.
    std::string validate(kg::EntityID table) const;
    LookupTableResult select(kg::EntityID table, int64_t key) const;

private:
    const kg::KGModule& kg_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_LOOKUP_TABLE_SELECTOR_H
