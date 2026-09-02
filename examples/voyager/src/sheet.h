// The character sheet, which is a VIEW and not a store.
//
// There is no struct here holding a copy of a characteristic score.
// Every line is read out of the graph at the moment it is asked for,
// and the only reason this file exists is to put the graph into the
// shape a screen wants. A mirrored field is a drift site: it can hold a
// value the graph does not, and nothing says which of the two is the
// character. This module has already paid for three of those.
//
// It is also where the no-hardcoding claim is cashed. The lines come
// from the Characteristic entities in the graph, in the order those
// entities declare, labelled with the short name the book prints for
// each. Nothing in this file, or anywhere else in the C++, names a
// characteristic. Put a seventh in the graph and a seventh line
// appears; test_characteristics_from_graph does exactly that.

#ifndef VOYAGER_SHEET_H
#define VOYAGER_SHEET_H

#include "logosphere/kg/kg_module.h"

#include <string>
#include <vector>

namespace voyager {

// One characteristic, as it stands, with the odds it carries.
struct SheetLine {
    // The short name the book prints for this characteristic, read off
    // the graph. Deliberately not exemplified here: this file must not
    // spell a characteristic even in a comment, and a test scans it.
    std::string  label;
    std::string  value;      // the score, empty when none is recorded
    std::string  modifier;   // "+1", "-2", from the book's own table
    kg::EntityID characteristic = kg::INVALID_ENTITY;
    // What this line means, in the book's words: the characteristic
    // as the book defines it and the table band the score falls in.
    // The screen shows it on hover; nothing here invents a word of it.
    std::string  note;
};

// One row of the lived record: a kind of moment faced, and how often;
// a mark; a standing. The label is the graph's; a count is derived
// from the MomentFaced records at the time of asking. Stage is a
// QUERY, never a store. The note is the moment that left it.
struct SheetRecordLine {
    std::string label;
    std::string count;
    std::string note;
};

struct Sheet {
    kg::EntityID            character = kg::INVALID_ENTITY;
    std::vector<SheetLine>  lines;
    std::string             age;          // empty until the rule sets it
    std::string             age_note;     // the book's sentence on seasons
    std::string             career;       // empty until one is entered
    std::string             career_note;  // the book's sentence on it
    std::string             background;   // the prose, as it was lived
    std::vector<SheetRecordLine> record;  // stage, counted on demand
};

// Every Characteristic in the graph, in the order the graph declares,
// with the character's score and its modifier. False with `error` set
// when the graph cannot answer: no characteristics at all, a
// characteristic that names a slot nothing declares, or no single
// table to turn a score into a modifier. None of those is guessed at.
bool read_sheet(const kg::KGModule& world, kg::EntityID character,
                Sheet& out, std::string& error);

// The characteristics as the graph orders them, as (entity, slot name)
// pairs. The one place anything asks "which characteristics are there",
// so the roller, the sheet and the referee's brief all agree by
// construction rather than by three lists staying in step.
bool characteristics_in_order(
    const kg::KGModule& world,
    std::vector<std::pair<kg::EntityID, std::string>>& out,
    std::string& error);

// The lookup that turns a score into a modifier, found by the row type
// it holds rather than by its name. Exactly one, or an error: two would
// mean the answer depends on which was asked.
bool characteristic_modifier_table(const kg::KGModule& world,
                                   kg::EntityID& out, std::string& error);

}  // namespace voyager

#endif  // VOYAGER_SHEET_H
