// A source document, parsed into the shapes rules actually live in.
//
// Rulebooks put their rules in three places: sentences of prose, cells
// of tables, and items of lists, all nested under headings. To cite a
// rule you must be able to ADDRESS one of those, and to verify a
// citation you must be able to resolve the address back to its exact
// text. A line number cannot do it (a table row holds six careers'
// numbers), and a byte offset can, but means nothing to a human and
// dies on the next re-vendor.
//
// So a source is normalized ONCE into this model, and citations
// address the model. A new source format needs a new parser into these
// structures and nothing else: markdown today, extracted PDF text or
// HTML later, without touching the locator, the resolver, or any rule
// data already captured.
//
// Engine-generic. Nothing here knows about any particular rulebook.

#ifndef LOGOSPHERE_TEXT_SOURCE_DOCUMENT_H
#define LOGOSPHERE_TEXT_SOURCE_DOCUMENT_H

#include <string>
#include <vector>

namespace logosphere::text {

// A table as printed: a header row naming the columns, then data rows
// whose first cell is the row's key ("1", "Qualifications", "Survival").
struct SourceTable {
    // What labels this table. In markdown the first header cell often
    // names the table itself ("Service Skills", "Personal Development"),
    // which is exactly what distinguishes six different rows that all
    // begin "| 1 |".
    std::string              label;
    std::vector<std::string> columns;   // header cells, label included at [0]
    std::vector<std::vector<std::string>> rows;   // data rows, key at [0]
    int                      first_line = 0;      // 1-based, for humans

    // The cell at (row key, column name), or false when either is
    // absent. Comparison is exact after trimming.
    bool cell(const std::string& row_key, const std::string& column,
              std::string& out) const;
};

// A run of prose under a heading, split into sentences so a rule
// stated in one sentence can be addressed without dragging in the
// paragraph around it.
struct SourceParagraph {
    std::string              text;
    std::vector<std::string> sentences;
    int                      first_line = 0;
};

struct SourceListItem {
    std::string text;
    int         line = 0;
};

// One heading and everything under it, until the next heading of the
// same or higher level. Sections nest, so a section knows its own
// trail from the document root: {"Careers", "Career Tables"}.
struct SourceSection {
    std::string              heading;      // heading text, markers stripped
    int                      level = 0;    // 1 = '#', 2 = '##', ...
    int                      line = 0;
    std::vector<std::string> path;         // ancestors + self

    std::vector<SourceParagraph> paragraphs;
    std::vector<SourceTable>     tables;
    std::vector<SourceListItem>  list_items;
};

class SourceDocument {
public:
    // Parse markdown. Loud by omission rather than by guessing: text
    // before the first heading lands in an implicit section with an
    // empty heading, never dropped.
    static SourceDocument parse_markdown(const std::string& text);

    const std::vector<SourceSection>& sections() const { return sections_; }

    // The section whose path ENDS with the given trail, or nullptr.
    // A trail of {"Career Tables"} matches wherever that heading sits,
    // so a citation does not have to spell out every ancestor; a
    // longer trail disambiguates when a heading repeats.
    const SourceSection* find_section(
        const std::vector<std::string>& trail) const;

    // The table with this label inside the section, or nullptr.
    static const SourceTable* find_table(const SourceSection& section,
                                         const std::string& label);

    bool empty() const { return sections_.empty(); }

private:
    std::vector<SourceSection> sections_;
};

}  // namespace logosphere::text

#endif  // LOGOSPHERE_TEXT_SOURCE_DOCUMENT_H
