#include "sheet.h"

#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/lookup_table_selector.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <utility>
#include <vector>

namespace voyager {
namespace {

// The type of a row of the score-to-modifier lookup. A TYPE name, not
// a label: it is the ontology's own word for the thing, checked by the
// registry, and it is how every other reader in the engine finds what
// it needs. A table's printed NAME would be a magic string; its row
// type is not.
constexpr const char* kModifierRowType = "CharacteristicModifierEntry";
constexpr const char* kModifierColumn = "characteristic_modifier";

bool as_int(const std::string& text, long long& out) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    if (*begin == '+') ++begin;
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

std::string signed_text(long long value) {
    return (value >= 0 ? "+" : "") + std::to_string(value);
}

}  // namespace

bool characteristics_in_order(
    const kg::KGModule& world,
    std::vector<std::pair<kg::EntityID, std::string>>& out,
    std::string& error) {
    out.clear();
    std::vector<std::pair<long long, kg::EntityID>> ordered;
    for (const kg::EntityID id : world.findByType("Characteristic")) {
        long long index = 0;
        if (!as_int(world.getProperty(id, "characteristic_index"), index)) {
            error = "characteristic '" + world.getProperty(id, "name") +
                    "' has no readable position in the profile";
            return false;
        }
        ordered.emplace_back(index, id);
    }
    if (ordered.empty()) {
        error = "the graph holds no characteristics; load the rules";
        return false;
    }
    std::sort(ordered.begin(), ordered.end());
    for (const auto& [index, id] : ordered) {
        const std::string slot = world.getProperty(id, "attribute_ref");
        if (slot.empty()) {
            error = "characteristic '" + world.getProperty(id, "name") +
                    "' names no attribute to hold its score";
            return false;
        }
        if (!world.getRegistry().hasProperty("Character", slot)) {
            error = "characteristic '" + world.getProperty(id, "name") +
                    "' names attribute '" + slot +
                    "', which Character does not declare";
            return false;
        }
        out.emplace_back(id, slot);
    }
    return true;
}

bool characteristic_modifier_table(const kg::KGModule& world,
                                   kg::EntityID& out, std::string& error) {
    out = kg::INVALID_ENTITY;
    size_t found = 0;
    for (const kg::EntityID id : world.findByType("LookupTable")) {
        if (world.getProperty(id, "entry_type") != kModifierRowType) continue;
        out = id;
        ++found;
    }
    if (found == 1) return true;
    out = kg::INVALID_ENTITY;
    error = found == 0
                ? "no lookup turns a characteristic score into a modifier"
                : "several lookups turn a score into a modifier, so which "
                  "one answers depends on which was asked";
    return false;
}

bool read_sheet(const kg::KGModule& world, kg::EntityID character,
                Sheet& out, std::string& error) {
    out = Sheet{};
    out.character = character;

    std::vector<std::pair<kg::EntityID, std::string>> characteristics;
    if (!characteristics_in_order(world, characteristics, error)) return false;

    kg::EntityID table = kg::INVALID_ENTITY;
    if (!characteristic_modifier_table(world, table, error)) return false;
    const logosphere::rules::LookupTableSelector modifiers(world);

    for (const auto& [id, slot] : characteristics) {
        SheetLine line;
        line.characteristic = id;
        line.label = world.getProperty(id, "characteristic_abbreviation");
        if (line.label.empty()) {
            error = "characteristic '" + world.getProperty(id, "name") +
                    "' has no short name to print";
            return false;
        }
        if (character != kg::INVALID_ENTITY && world.exists(character)) {
            line.value = world.getProperty(character, slot);
        }
        long long score = 0;
        if (as_int(line.value, score)) {
            const auto row = modifiers.select(table, score);
            if (!row.ok()) {
                error = "no modifier for " + line.label + " " + line.value +
                        ": " + (row.error.empty()
                                    ? "the table has no row for that score"
                                    : row.error);
                return false;
            }
            long long modifier = 0;
            if (!as_int(world.getProperty(row.selection->row(),
                                          kModifierColumn), modifier)) {
                error = "the modifier row for " + line.label + " " +
                        line.value + " carries no readable modifier";
                return false;
            }
            line.modifier = signed_text(modifier);
        }
        out.lines.push_back(std::move(line));
    }

    if (character == kg::INVALID_ENTITY || !world.exists(character)) return true;

    out.age = world.getProperty(character, "age_years");
    const std::string career = world.getProperty(character, "chosen_career");
    long long career_id = 0;
    if (as_int(career, career_id) && world.exists(
            static_cast<kg::EntityID>(career_id))) {
        out.career = world.getProperty(
            static_cast<kg::EntityID>(career_id), "name");
    }
    // The prose, in the order it was lived: narrations first, then
    // each moment's situation and what it did, read straight off the
    // records rather than from a display copy that could drift.
    const auto append_prose = [&out](const std::string& text) {
        if (text.empty()) return;
        if (!out.background.empty()) out.background += "\n\n";
        out.background += text;
    };
    for (const kg::EntityID part : world.getRelated(character, "HAS_PART")) {
        if (world.getType(part) != "Narration") continue;
        append_prose(world.getProperty(part, "narration_text"));
    }
    for (const kg::EntityID lived : world.getRelated(character, "LIVED")) {
        if (world.getType(lived) != "MomentFaced") continue;
        append_prose(world.getProperty(lived, "moment_situation"));
        append_prose(world.getProperty(lived, "moment_outcome"));
    }

    // Stage, counted and never stored: one row per kind of moment this
    // character has faced, labelled as the graph labels the kind. A
    // kind never faced has no row, exactly as it has no stage.
    for (const kg::EntityID kind : world.findByType("MomentKind")) {
        size_t faced = 0;
        for (const kg::EntityID part :
             world.getRelated(character, "LIVED")) {
            if (world.getType(part) != "MomentFaced") continue;
            long long ref = 0;
            if (as_int(world.getProperty(part, "moment_kind"), ref) &&
                static_cast<kg::EntityID>(ref) == kind) {
                ++faced;
            }
        }
        if (faced == 0) continue;
        out.record.push_back({world.getProperty(kind, "name"),
                              "x" + std::to_string(faced)});
    }

    // What the life holds: the latest standing per counterpart, and
    // every mark. Labels and values are the graph's words throughout.
    std::vector<std::pair<kg::EntityID, std::string>> standings;
    for (const kg::EntityID lived : world.getRelated(character, "LIVED")) {
        const std::string type = world.getType(lived);
        if (type == "StandingHeld") {
            long long with = 0;
            if (!as_int(world.getProperty(lived, "standing_with"), with)) {
                continue;
            }
            const auto id = static_cast<kg::EntityID>(with);
            const std::string key = world.getProperty(lived, "standing_key");
            bool replaced = false;
            for (auto& held : standings) {
                if (held.first == id) { held.second = key; replaced = true; }
            }
            if (!replaced) standings.emplace_back(id, key);
        } else if (type == "MarkLeft") {
            out.record.push_back({world.getProperty(lived, "mark_text"), ""});
        }
    }
    for (const auto& [id, key] : standings) {
        out.record.push_back({world.getProperty(id, "name"), key});
    }
    return true;
}

}  // namespace voyager
