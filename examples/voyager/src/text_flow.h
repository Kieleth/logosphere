// Text on its way to the screen: the bytes the bitmap font can draw,
// and the lines a column of a given width holds.
//
// No widget, no draw surface, no engine here on purpose. It is
// arithmetic over strings, which means the wrapping can be measured
// instead of looked at, and it is measured in test_voyager_text.
//
// A word is never cut. A word longer than the column keeps its own
// line whole and overhangs the column rather than losing its tail: a
// cut word is a word the reader has to guess at, and this game shows
// its text or does not show it.

#ifndef VOYAGER_TEXT_FLOW_H
#define VOYAGER_TEXT_FLOW_H

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace voyager {

// The engine's bitmap font draws bytes it does not know as blanks, and
// the book is full of en dashes and typographic quotes. Show them as
// their ASCII cousins. The DATA is untouched; this is only paint.
inline std::string renderable(const std::string& text) {
    static const struct { const char* from; const char* to; } kMap[] = {
        {"\xE2\x80\x93", "-"},
        {"\xE2\x80\x94", "--"},
        {"\xC3\x97", "x"},
        {"\xE2\x80\x99", "'"},
        {"\xE2\x80\x98", "'"},
        {"\xE2\x80\x9C", "\""},
        {"\xE2\x80\x9D", "\""},
    };
    std::string out = text;
    for (const auto& entry : kMap) {
        size_t at = 0;
        while ((at = out.find(entry.from, at)) != std::string::npos) {
            out.replace(at, std::strlen(entry.from), entry.to);
        }
    }
    return out;
}

// `text` broken into lines of at most `columns` characters. Runs of
// spaces and newlines are the breaks; both collapse, so the lines
// joined by one space each give the source back.
inline std::vector<std::string> wrap(const std::string& text,
                                     size_t columns) {
    std::vector<std::string> out;
    if (columns == 0) return out;
    size_t at = 0;
    while (true) {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\n')) {
            ++at;
        }
        if (at >= text.size()) break;
        const size_t hard = text.find('\n', at);
        const size_t stop = hard == std::string::npos ? text.size() : hard;
        size_t take = std::min(columns, stop - at);
        if (at + take < stop) {
            const size_t brk = text.rfind(' ', at + take);
            if (brk != std::string::npos && brk > at) {
                take = brk - at;
            } else {
                // One word, longer than the column. It goes out whole
                // and overhangs; the alternative is a word cut in half.
                const size_t space = text.find(' ', at);
                take = (space == std::string::npos || space > stop)
                           ? stop - at
                           : space - at;
            }
        }
        while (take > 0 && text[at + take - 1] == ' ') --take;
        out.push_back(text.substr(at, take));
        at += take;
    }
    return out;
}

}  // namespace voyager

#endif  // VOYAGER_TEXT_FLOW_H
