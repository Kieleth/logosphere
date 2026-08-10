#include "logosphere/text/source_locator.h"

#include <sstream>

namespace logosphere::text {
namespace {

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out.empty() ? "(document root)" : out;
}

ResolveResult fail(const std::string& why) {
    ResolveResult r;
    r.ok = false;
    r.reason = why;
    return r;
}

// Does `text` occur here with the required context around it? Empty
// prefix or suffix means "do not care".
bool occurs_with_context(const std::string& hay, const SourceLocator& loc) {
    size_t at = 0;
    while ((at = hay.find(loc.exact, at)) != std::string::npos) {
        const bool pre_ok =
            loc.prefix.empty() ||
            (at >= loc.prefix.size() &&
             hay.compare(at - loc.prefix.size(), loc.prefix.size(),
                         loc.prefix) == 0);
        const bool suf_ok =
            loc.suffix.empty() ||
            hay.compare(at + loc.exact.size(),
                        std::min(loc.suffix.size(),
                                 hay.size() - (at + loc.exact.size())),
                        loc.suffix) == 0;
        if (pre_ok && suf_ok) return true;
        ++at;
    }
    return false;
}

}  // namespace

const char* to_string(LocatorKind k) {
    switch (k) {
        case LocatorKind::Sentence: return "sentence";
        case LocatorKind::Cell:     return "cell";
        case LocatorKind::Row:      return "row";
        case LocatorKind::Heading:  return "heading";
        case LocatorKind::ListItem: return "list_item";
    }
    return "sentence";
}

bool kind_from_string(const std::string& s, LocatorKind& out) {
    if (s == "sentence")  { out = LocatorKind::Sentence; return true; }
    if (s == "cell")      { out = LocatorKind::Cell;     return true; }
    if (s == "row")       { out = LocatorKind::Row;      return true; }
    if (s == "heading")   { out = LocatorKind::Heading;  return true; }
    if (s == "list_item") { out = LocatorKind::ListItem; return true; }
    return false;
}

ResolveResult resolve(const SourceDocument& doc, const SourceLocator& loc) {
    if (doc.empty()) return fail("the source document is empty");

    const SourceSection* section = nullptr;
    if (!loc.path.empty()) {
        section = doc.find_section(loc.path);
        if (!section)
            return fail("no section '" + join(loc.path, " > ") +
                        "' in " + loc.file);
    }

    switch (loc.kind) {
        case LocatorKind::Heading: {
            if (!section) return fail("a heading locator needs a path");
            ResolveResult r;
            r.ok = true;
            r.text = section->heading;
            r.line = section->line;
            return r;
        }

        case LocatorKind::Cell:
        case LocatorKind::Row: {
            if (!section)
                return fail("a table locator needs the heading path of the "
                            "section the table is printed in");
            if (loc.table.empty())
                return fail("a table locator needs the table's label (the "
                            "first cell of its header row) - without it, "
                            "six different tables can each have a row '" +
                            loc.row + "'");
            // A section can print SEVERAL tables under one label (the
            // career blocks all label their first table "Career"). The
            // address is the whole triple, so pick the table that has
            // this row and column; more than one is ambiguous and says
            // so rather than guessing.
            const SourceTable* table = nullptr;
            {
                std::vector<const SourceTable*> labelled, matching;
                for (const auto& t : section->tables)
                    if (t.label == loc.table) labelled.push_back(&t);
                for (const auto* t : labelled) {
                    if (loc.kind == LocatorKind::Row) {
                        // A column may be given purely to disambiguate:
                        // four career blocks all print a table labelled
                        // "Career" with a row "Qualifications", and only
                        // the column says which block.
                        if (!loc.column.empty()) {
                            bool has_col = false;
                            for (const auto& c : t->columns)
                                if (c == loc.column) { has_col = true; break; }
                            if (!has_col) continue;
                        }
                        for (const auto& r : t->rows)
                            if (!r.empty() && r[0] == loc.row) {
                                matching.push_back(t); break;
                            }
                    } else {
                        std::string ignored;
                        if (t->cell(loc.row, loc.column, ignored))
                            matching.push_back(t);
                    }
                }
                if (matching.size() > 1)
                    return fail("'" + loc.table + "' row '" + loc.row +
                                "' column '" + loc.column + "' is ambiguous: " +
                                std::to_string(matching.size()) +
                                " tables under '" + join(loc.path, " > ") +
                                "' match it");
                if (matching.size() == 1) table = matching.front();
                else if (labelled.size() == 1) table = labelled.front();
            }
            if (!table) {
                // Distinguish "that table is not here" from "that table
                // is here but has no such cell" - they are different
                // mistakes and deserve different answers.
                std::vector<const SourceTable*> labelled;
                for (const auto& t : section->tables)
                    if (t.label == loc.table) labelled.push_back(&t);
                if (!labelled.empty()) {
                    std::string cols;
                    for (const auto* t : labelled)
                        for (const auto& c : t->columns)
                            if (cols.find(c) == std::string::npos)
                                cols += (cols.empty() ? "" : ", ") + c;
                    return fail("no table labelled '" + loc.table +
                                "' has a cell at row '" + loc.row +
                                "', column '" + loc.column +
                                "' (columns present: " + cols + ")");
                }
                std::string had;
                for (const auto& t : section->tables)
                    had += (had.empty() ? "" : ", ") + t.label;
                return fail("no table '" + loc.table + "' under '" +
                            join(loc.path, " > ") + "' (that section has: " +
                            (had.empty() ? "no tables" : had) + ")");
            }
            if (loc.kind == LocatorKind::Row) {
                for (const auto& r : table->rows) {
                    if (r.empty() || r[0] != loc.row) continue;
                    ResolveResult out;
                    out.ok = true;
                    out.line = table->first_line;
                    for (size_t i = 0; i < r.size(); ++i)
                        out.text += (i ? " | " : "") + r[i];
                    return out;
                }
                return fail("table '" + loc.table + "' has no row '" +
                            loc.row + "'");
            }
            if (loc.column.empty())
                return fail("a cell locator needs a column: the value lives "
                            "in one cell, and the row holds every column's");
            std::string cell;
            if (!table->cell(loc.row, loc.column, cell)) {
                std::string cols;
                for (const auto& c : table->columns)
                    cols += (cols.empty() ? "" : ", ") + c;
                return fail("table '" + loc.table + "' has no cell at row '" +
                            loc.row + "', column '" + loc.column +
                            "' (columns: " + cols + ")");
            }
            ResolveResult r;
            r.ok = true;
            r.text = cell;
            r.line = table->first_line;
            return r;
        }

        case LocatorKind::ListItem: {
            if (!section) return fail("a list locator needs a path");
            for (const auto& item : section->list_items) {
                if (item.text.find(loc.exact) == std::string::npos) continue;
                ResolveResult r;
                r.ok = true;
                r.text = item.text;
                r.line = item.line;
                return r;
            }
            return fail("no list item containing '" + loc.exact + "' under '" +
                        join(loc.path, " > ") + "'");
        }

        case LocatorKind::Sentence: {
            // Sentence first (a rule is usually one), paragraph as the
            // fallback for a rule that spans two.
            auto scan = [&](const SourceSection& s) -> const ResolveResult* {
                static ResolveResult hit;
                for (const auto& p : s.paragraphs) {
                    for (const auto& sent : p.sentences) {
                        if (!occurs_with_context(sent, loc)) continue;
                        hit = ResolveResult{};
                        hit.ok = true;
                        hit.text = sent;
                        hit.line = p.first_line;
                        return &hit;
                    }
                }
                for (const auto& p : s.paragraphs) {
                    if (!occurs_with_context(p.text, loc)) continue;
                    hit = ResolveResult{};
                    hit.ok = true;
                    hit.text = p.text;
                    hit.line = p.first_line;
                    return &hit;
                }
                return nullptr;
            };
            if (section) {
                if (const auto* r = scan(*section)) return *r;
                return fail("'" + loc.exact.substr(0, 60) +
                            "' is not in section '" + join(loc.path, " > ") +
                            "' of " + loc.file);
            }
            for (const auto& s : doc.sections())
                if (const auto* r = scan(s)) return *r;
            return fail("'" + loc.exact.substr(0, 60) + "' is nowhere in " +
                        loc.file);
        }
    }
    return fail("unknown locator kind");
}

ResolveResult resolve_and_match(const SourceDocument& doc,
                                const SourceLocator& loc) {
    auto r = resolve(doc, loc);
    if (!r.ok) return r;

    // A cell must be EXACTLY what was claimed. This is the check that
    // makes a citation prove a value: "Int 6+" is not "Dex 5+", and a
    // claim of 5 cannot borrow another column's number.
    if (loc.kind == LocatorKind::Cell || loc.kind == LocatorKind::Heading) {
        if (r.text != loc.exact) {
            r.ok = false;
            r.reason = "the source says '" + r.text + "' there, not '" +
                       loc.exact + "'";
        }
        return r;
    }
    // Sentences, rows and list items are addressed BY their text, so
    // resolution already proved containment; hold them to it anyway so
    // a future resolver change cannot quietly weaken the contract.
    if (r.text.find(loc.exact) == std::string::npos) {
        r.ok = false;
        r.reason = "the source says '" + r.text.substr(0, 80) +
                   "' there, which does not contain the quote";
    }
    return r;
}

}  // namespace logosphere::text
