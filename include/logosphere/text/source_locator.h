// A locator: the bounding box of a piece of a source text.
//
// OCR gives a box on a page and the characters inside it, and you can
// always go back and look. This is the same idea for a rulebook: an
// address that names a piece of a document, plus the text that piece
// is supposed to contain. Resolving the address returns the text the
// source actually has there, and the two are compared.
//
// The point is that a citation must prove a VALUE, and values live in
// cells and sentences, not in lines. Citing the line
//
//   | Qualifications | Dex 5+ | Dex 5+ | Edu 6+ | Int 6+ | End 5+ | Edu 6+ |
//
// proves nothing about Scout, because the line also contains Pirate's
// 5 and Technician's 6. Citing the CELL (table "Qualifications", row
// "Qualifications", column "Scout") proves exactly one thing, and a
// claim of 5+ fails against it.
//
// Shaped after the W3C Web Annotation selectors: a structural address
// plus a quote with a little surrounding context, deliberately NOT
// byte offsets, which no human can navigate and which every re-vendor
// invalidates.
//
// Engine-generic: any game ingesting any rulebook uses this.

#ifndef LOGOSPHERE_TEXT_SOURCE_LOCATOR_H
#define LOGOSPHERE_TEXT_SOURCE_LOCATOR_H

#include "logosphere/text/source_document.h"

#include <string>
#include <vector>

namespace logosphere::text {

enum class LocatorKind {
    Sentence,   // one sentence of prose, the usual home of a stated rule
    Cell,       // one cell of a table: where tabulated rules live
    Row,        // a whole table row, when the rule really is the row
    Table,      // the table itself, addressed by its label - what a
                // table entity cites when it says "I am this table"
    Heading,    // the heading itself
    ListItem,
};

struct SourceLocator {
    std::string              file;    // path relative to the source root
    std::vector<std::string> path;    // heading trail, innermost last
    LocatorKind              kind = LocatorKind::Sentence;

    // Cell and Row only.
    std::string table;    // the table's label, which is what tells six
                          // rows that all begin "| 1 |" apart
    std::string row;      // the row's key (its first cell)
    std::string column;   // the column's header

    // What the addressed piece is supposed to say. For a Cell this is
    // the cell alone, which is the whole point.
    std::string exact;

    // Optional disambiguation for prose, when `exact` occurs more than
    // once under the same heading. Empty means "any occurrence".
    std::string prefix;
    std::string suffix;

    bool valid() const { return !file.empty() && !exact.empty(); }
};

struct ResolveResult {
    bool        ok = false;
    std::string text;      // what the source ACTUALLY has at that address
    std::string reason;    // why not, when !ok - always actionable
    int         line = 0;  // 1-based, so a human can go and look
};

// Resolve a locator against a parsed document: return the text at that
// address, without consulting `exact`. The caller compares. Failure is
// loud and specific ("no table 'Service Skills' under Career Tables"),
// never a silent miss.
ResolveResult resolve(const SourceDocument& doc, const SourceLocator& loc);

// Resolve AND check that the address holds what the locator claims.
// This is what verification calls: it fails both when the address does
// not exist and when it exists but says something else.
ResolveResult resolve_and_match(const SourceDocument& doc,
                                const SourceLocator& loc);

const char* to_string(LocatorKind kind);
bool kind_from_string(const std::string& s, LocatorKind& out);

}  // namespace logosphere::text

#endif  // LOGOSPHERE_TEXT_SOURCE_LOCATOR_H
