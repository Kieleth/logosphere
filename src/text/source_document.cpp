#include "logosphere/text/source_document.h"

#include <algorithm>
#include <cctype>

namespace logosphere::text {
namespace {

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool is_table_row(const std::string& line) {
    const auto t = trim(line);
    return t.size() > 1 && t.front() == '|';
}

// "| a | b | c |" -> {"a","b","c"}. The leading and trailing pipes are
// delimiters, not empty cells.
std::vector<std::string> split_row(const std::string& line) {
    std::string t = trim(line);
    if (!t.empty() && t.front() == '|') t.erase(t.begin());
    if (!t.empty() && t.back() == '|') t.pop_back();
    std::vector<std::string> cells;
    std::string cur;
    for (size_t i = 0; i < t.size(); ++i) {
        // A pipe escaped in the source is content, not a delimiter.
        if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cur += '|'; ++i; continue;
        }
        if (t[i] == '|') { cells.push_back(trim(cur)); cur.clear(); continue; }
        cur += t[i];
    }
    cells.push_back(trim(cur));
    return cells;
}

// "| --- | --- |" separates a markdown header from its body and is
// not data.
bool is_separator_row(const std::vector<std::string>& cells) {
    if (cells.empty()) return false;
    for (const auto& c : cells) {
        if (c.empty()) return false;
        for (char ch : c)
            if (ch != '-' && ch != ':' && ch != ' ') return false;
    }
    return true;
}

int heading_level(const std::string& line) {
    int n = 0;
    while (n < static_cast<int>(line.size()) && line[n] == '#') ++n;
    if (n == 0 || n >= static_cast<int>(line.size())) return 0;
    return line[n] == ' ' ? n : 0;
}

bool is_list_item(const std::string& line) {
    const auto t = trim(line);
    if (t.size() < 2) return false;
    if ((t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') return true;
    // "1. " and "1) "
    size_t i = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) ++i;
    return i > 0 && i + 1 < t.size() && (t[i] == '.' || t[i] == ')') &&
           t[i + 1] == ' ';
}

// Split a paragraph into sentences. Deliberately simple: a sentence
// ends at . ! or ? followed by space-then-capital or end of text. A
// rulebook writes plainly; anything cleverer would be guessing, and a
// citation that resolves to a slightly larger sentence still proves
// its value.
std::vector<std::string> split_sentences(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '.' && c != '!' && c != '?') continue;
        size_t j = i + 1;
        while (j < text.size() && (text[j] == ' ' || text[j] == '"' ||
                                   text[j] == ')' || text[j] == ']')) ++j;
        const bool at_end = j >= text.size();
        const bool next_starts =
            !at_end && (std::isupper(static_cast<unsigned char>(text[j])) ||
                        text[j] == '*' || text[j] == '[');
        if (!at_end && !next_starts) continue;
        auto s = trim(text.substr(start, i - start + 1));
        if (!s.empty()) out.push_back(s);
        start = j;
    }
    auto tail = trim(text.substr(start));
    if (!tail.empty()) out.push_back(tail);
    return out;
}

}  // namespace

bool SourceTable::cell(const std::string& row_key, const std::string& column,
                       std::string& out) const {
    size_t col = columns.size();
    for (size_t i = 0; i < columns.size(); ++i)
        if (columns[i] == column) { col = i; break; }
    if (col == columns.size()) return false;
    for (const auto& r : rows) {
        if (r.empty() || r[0] != row_key) continue;
        if (col >= r.size()) return false;
        out = r[col];
        return true;
    }
    return false;
}

SourceDocument SourceDocument::parse_markdown(const std::string& text) {
    SourceDocument doc;
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
        lines.push_back(cur);
    }

    // Everything before the first heading still belongs somewhere.
    SourceSection current;
    current.level = 0;
    current.line = 1;
    std::vector<std::string> trail;   // headings by level

    auto flush_paragraph = [](SourceSection& s, std::string& buf, int line) {
        if (trim(buf).empty()) { buf.clear(); return; }
        SourceParagraph p;
        p.text = trim(buf);
        p.sentences = split_sentences(p.text);
        p.first_line = line;
        s.paragraphs.push_back(std::move(p));
        buf.clear();
    };

    std::string para;
    int para_line = 1;

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const int lineno = static_cast<int>(i) + 1;
        const int level = heading_level(line);

        if (level > 0) {
            flush_paragraph(current, para, para_line);
            doc.sections_.push_back(current);
            current = SourceSection{};
            current.heading = trim(line.substr(level + 1));
            current.level = level;
            current.line = lineno;
            trail.resize(static_cast<size_t>(level) - 1);
            trail.push_back(current.heading);
            current.path = trail;
            continue;
        }

        if (is_table_row(line)) {
            flush_paragraph(current, para, para_line);
            // Consume the whole run of table rows as one table.
            std::vector<std::vector<std::string>> rows;
            const int first = lineno;
            size_t j = i;
            for (; j < lines.size() && is_table_row(lines[j]); ++j)
                rows.push_back(split_row(lines[j]));
            i = j - 1;

            // A run of pipe rows can hold SEVERAL logical tables. The
            // Cepheus career block writes one separator and then
            // restates the column headers to start each sub-table:
            //
            //   | Career           | Athlete | Aerospace Defense | ...
            //   | Qualifications   | End 8+  | End 5+            | ...
            //   | Ranks and Skills | Athlete | Aerospace         | ...   <- new table
            //   | 0                | ...
            //
            // So a row that RESTATES the headers opens a new table.
            // Restating is judged by prefix, because sources abbreviate
            // ("Aerospace" for "Aerospace Defense"). A source that does
            // not do this simply yields one table, as plain markdown
            // does.
            std::vector<std::string> block_header;
            for (const auto& row : rows) {
                if (is_separator_row(row)) continue;
                if (block_header.empty()) { block_header = row; continue; }
            }
            auto restates_header = [&block_header](
                    const std::vector<std::string>& row) {
                if (block_header.size() < 2 || row.size() != block_header.size())
                    return false;
                for (size_t i = 1; i < row.size(); ++i) {
                    const auto& a = row[i];
                    const auto& b = block_header[i];
                    if (a.empty() || b.empty()) return false;
                    const auto n = std::min(a.size(), b.size());
                    if (a.compare(0, n, b, 0, n) != 0) return false;
                }
                return true;
            };

            SourceTable t;
            t.first_line = first;
            bool have_header = false;
            for (size_t r = 0; r < rows.size(); ++r) {
                if (is_separator_row(rows[r])) continue;
                const bool is_header =
                    !have_header || (have_header && restates_header(rows[r]));
                if (is_header) {
                    if (have_header) {          // close the previous one
                        current.tables.push_back(std::move(t));
                        t = SourceTable{};
                        t.first_line = first + static_cast<int>(r);
                    }
                    t.columns = rows[r];
                    t.label = t.columns.empty() ? "" : t.columns[0];
                    have_header = true;
                    continue;
                }
                t.rows.push_back(rows[r]);
            }
            if (have_header) current.tables.push_back(std::move(t));
            continue;
        }

        if (is_list_item(line)) {
            flush_paragraph(current, para, para_line);
            current.list_items.push_back({trim(line), lineno});
            continue;
        }

        if (trim(line).empty()) { flush_paragraph(current, para, para_line); continue; }
        if (para.empty()) para_line = lineno;
        para += (para.empty() ? "" : " ") + trim(line);
    }
    flush_paragraph(current, para, para_line);
    doc.sections_.push_back(current);
    return doc;
}

const SourceSection* SourceDocument::find_section(
    const std::vector<std::string>& trail) const {
    if (trail.empty()) return nullptr;
    for (const auto& s : sections_) {
        if (s.path.size() < trail.size()) continue;
        bool match = true;
        for (size_t i = 0; i < trail.size(); ++i) {
            const auto& want = trail[trail.size() - 1 - i];
            const auto& have = s.path[s.path.size() - 1 - i];
            if (want != have) { match = false; break; }
        }
        if (match) return &s;
    }
    return nullptr;
}

const SourceTable* SourceDocument::find_table(const SourceSection& section,
                                              const std::string& label) {
    for (const auto& t : section.tables)
        if (t.label == label) return &t;
    return nullptr;
}

}  // namespace logosphere::text
