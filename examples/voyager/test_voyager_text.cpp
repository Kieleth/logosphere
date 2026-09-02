// The wrapper, held to the one promise it makes: the reader sees the
// whole text or the game does not show it at all.
//
// Four properties, checked over a table of real sentences rather than
// on one hand-picked case:
//   1. a line fits the column, unless it is a single word that cannot
//   2. no word is ever split across two lines
//   3. the lines joined back together hold the same words in the same
//      order, modulo runs of whitespace and the newlines the source
//      spelled
//   4. nothing is added and nothing is lost
//
// Property 3 is the one that catches a wrapper that silently drops a
// tail, which is the failure the reader cannot see and the reason
// this file exists.

#undef NDEBUG

#include "text_flow.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                       \
    do {                                                                \
        if (condition) {                                                \
            ++passed;                                                   \
        } else {                                                        \
            ++failed;                                                   \
            std::cout << "FAIL: " << message << '\n';                   \
        }                                                               \
    } while (false)

// The source with every run of spaces and newlines collapsed to one
// space, leading and trailing removed. This is what wrapping may not
// change.
std::string words_of(const std::string& text) {
    std::istringstream stream(text);
    std::string word, out;
    while (stream >> word) {
        if (!out.empty()) out += ' ';
        out += word;
    }
    return out;
}

bool has_space(const std::string& text) {
    return text.find(' ') != std::string::npos;
}

void check_one(const std::string& text, size_t columns) {
    const auto lines = voyager::wrap(text, columns);
    std::string rebuilt;
    for (const auto& line : lines) {
        CHECK(!line.empty(),
              "an empty line came out of wrapping \"" << text << "\" at "
              << columns << " columns; a blank row is a gap the reader "
              "reads as the end");
        CHECK(line.size() <= columns || !has_space(line),
              "line \"" << line << "\" is " << line.size()
              << " long in a " << columns << "-column box and it is not "
              "one unbreakable word");
        CHECK(line.front() != ' ' && line.back() != ' ',
              "line \"" << line << "\" carries an edge space, which "
              "shifts the text off its column");
        if (!rebuilt.empty()) rebuilt += ' ';
        rebuilt += line;
    }
    // Word for word, in order. Spacing INSIDE a line is the author's
    // and is left as written; what may not change is which words came
    // out and in what order. A split word shows up here as two tokens
    // where the source had one, and a dropped tail as a missing one.
    CHECK(words_of(rebuilt) == words_of(text),
          "wrapping \"" << text << "\" at " << columns
          << " columns gave back \"" << words_of(rebuilt) << "\", not \""
          << words_of(text) << "\"");
}

}  // namespace

int main() {
    const std::vector<std::string> corpus = {
        "",
        "one",
        "one two",
        "The sheet is a view of the graph and it grows as the graph "
        "grows, one row for every characteristic the rules put on it.",
        "A line\nbroken by the author\nin three places.",
        "   leading and trailing spaces   ",
        "double  spaces   between    words",
        "supercalifragilisticexpialidocious",
        "a supercalifragilisticexpialidocious b",
        "trailing newline\n",
        "\n\nleading newlines",
        "hyphen-joined words stay whole because a hyphen is not a space",
    };

    for (const auto& text : corpus) {
        for (size_t columns = 1; columns <= 64; ++columns) {
            check_one(text, columns);
        }
    }

    // No column, no lines. A zero-width box that returned one line
    // would draw a row of text nobody asked for.
    CHECK(voyager::wrap("anything at all", 0).empty(),
          "a zero-column box produced lines");
    CHECK(voyager::wrap("", 40).empty(),
          "empty text produced lines");
    CHECK(voyager::wrap("   \n  \n ", 40).empty(),
          "text of nothing but whitespace produced lines");

    // The long word, whole and on its own row.
    {
        const std::string word = "supercalifragilisticexpialidocious";
        const auto lines = voyager::wrap("a " + word + " b", 10);
        bool found = false;
        for (const auto& line : lines) {
            if (line == word) found = true;
        }
        CHECK(found,
              "a 34-letter word in a 10-column box did not survive on a "
              "row of its own; the reader would be shown a fragment");
        CHECK(lines.size() == 3,
              "a word too long for the box produced " << lines.size()
              << " rows; it should take exactly one of its own");
    }

    // The break lands on the last space that fits, not before it.
    {
        const auto lines = voyager::wrap("abcd efgh ijkl", 9);
        CHECK(lines.size() == 2 && lines[0] == "abcd efgh" &&
                  lines[1] == "ijkl",
              "a 9-column box broke \"abcd efgh ijkl\" into "
              << lines.size() << " rows starting \""
              << (lines.empty() ? std::string() : lines[0])
              << "\"; the box holds both of the first two words");
    }

    // The paint the bitmap font needs, and only that.
    {
        const std::string dashed =
            "a \xE2\x80\x93 b \xE2\x80\x9C c \xE2\x80\x9D";
        const std::string shown = voyager::renderable(dashed);
        CHECK(shown == "a - b \" c \"",
              "the font's byte substitutions gave \"" << shown << "\"");
        CHECK(voyager::renderable("plain ascii") == "plain ascii",
              "plain text was changed on the way to the screen");
    }

    std::cout << (failed ? "FAILED " : "OK ") << passed << " passed, "
              << failed << " failed\n";
    return failed ? 1 : 0;
}
