// Source locators: the bounding box of a piece of a rulebook.
//
// The defect this exists to close, found 2026-08-10 by injecting a lie
// into a real seed: a rule's value was cited to the LINE it was printed
// on, and a career-table line carries six careers' numbers. Claiming
// Scout qualifies on 5+ when the book says 6+ passed verification,
// because "5" is in that line as Pirate's Dex 5+. A line citation
// cannot prove a cell.
//
// So: address the cell. The tests below use the vendored Cepheus SRD
// rather than toy text, because the defect was in real data and the
// fixture that hides it is worthless.
//
// Usage:
//   ./build/test_source_locator

#undef NDEBUG

#include "logosphere/text/source_document.h"
#include "logosphere/text/source_locator.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (cond) { tests_passed++; }                                   \
        else { tests_failed++;                                          \
               std::cout << "FAIL: " << msg << std::endl; }             \
    } while (0)

namespace {

using namespace logosphere::text;

const char* kChapter = "book1/character-creation.md";

std::string slurp(const std::string& rel) {
    const std::string path = std::string(LOGOSPHERE_SOURCE_DIR) +
                             "/examples/logovger/srd/cepheus/" + rel;
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

SourceDocument chapter() {
    return SourceDocument::parse_markdown(slurp(kChapter));
}

SourceLocator cell(const char* table, const char* row, const char* col,
                   const char* exact) {
    SourceLocator l;
    l.file = kChapter;
    l.path = {"Career Tables"};
    l.kind = LocatorKind::Cell;
    l.table = table;
    l.row = row;
    l.column = col;
    l.exact = exact;
    return l;
}

// ------------------------------------------------- the document model

void test_the_chapter_parses_into_shapes() {
    const auto doc = chapter();
    CHECK(!doc.empty(), "the vendored chapter parses");

    const auto* s = doc.find_section({"Career Tables"});
    CHECK(s != nullptr, "the Career Tables section is found by heading");
    if (!s) return;
    CHECK(s->tables.size() >= 8,
          "and it holds many tables, not one blob (" +
              std::to_string(s->tables.size()) + ")");

    // The label is what tells six rows that all begin "| 1 |" apart.
    bool has_service = false, has_personal = false;
    for (const auto& t : s->tables) {
        if (t.label == "Service Skills") has_service = true;
        if (t.label == "Personal Development") has_personal = true;
    }
    CHECK(has_service && has_personal,
          "the skill sub-tables are separate, each labelled");

    const auto* skills = SourceDocument::find_table(*s, "Service Skills");
    CHECK(skills && skills->rows.size() == 6,
          "Service Skills has six rows, one per 1D6 result");
}

// ------------------------------------------------------ the addressing

void test_a_cell_resolves_to_one_value() {
    const auto doc = chapter();

    auto scout = cell("Career", "Qualifications", "Scout", "Int 6+");
    auto r = resolve(doc, scout);
    CHECK(r.ok && r.text == "Int 6+",
          "Scout's qualification cell resolves to Int 6+, got '" + r.text +
              "' (" + r.reason + ")");
    CHECK(r.line > 0, "and reports a line a human can go and look at");

    auto pirate = cell("Career", "Qualifications", "Pirate", "Dex 5+");
    CHECK(resolve(doc, pirate).text == "Dex 5+",
          "the neighbouring column resolves to ITS value, not Scout's");
}

// THE regression. This exact claim passed the old line-based check.
void test_the_lie_that_got_through_is_refused() {
    const auto doc = chapter();

    auto truth = cell("Career", "Qualifications", "Scout", "Int 6+");
    CHECK(resolve_and_match(doc, truth).ok,
          "the truth verifies: Scout qualifies on Int 6+");

    auto lie = cell("Career", "Qualifications", "Scout", "Int 5+");
    const auto r = resolve_and_match(doc, lie);
    CHECK(!r.ok, "and the lie is REFUSED: Scout does not qualify on 5+");
    CHECK(r.reason.find("Int 6+") != std::string::npos,
          "with the source's own answer in the reason: " + r.reason);
    std::cout << "  [measure] refusal: " << r.reason << std::endl;

    // The line-based check passed this because "5" appears in the row
    // as another career's number. Prove the row really does contain it,
    // so this test documents WHY the cell is necessary.
    SourceLocator row = lie;
    row.kind = LocatorKind::Row;
    row.exact = "Dex 5+";
    // The column stays, purely to say WHICH career block's row this is:
    // all four print a table "Career" with a row "Qualifications".
    const auto row_r = resolve_and_match(doc, row);
    CHECK(row_r.ok,
          "the row genuinely contains a 5+ - from Pirate, which is exactly "
          "how a wrong Scout target borrowed it");
}

void test_a_table_row_is_told_from_its_neighbours() {
    const auto doc = chapter();

    // Six tables in this block have a row keyed "1". The label decides.
    auto service = cell("Service Skills", "1", "Scout", "Comms");
    CHECK(resolve_and_match(doc, service).ok,
          "Service Skills row 1, Scout column, is Comms");

    auto personal = cell("Personal Development", "1", "Scout", "+1 Str");
    CHECK(resolve_and_match(doc, personal).ok,
          "while Personal Development row 1 is a characteristic bump");

    auto wrong_table = cell("Personal Development", "1", "Scout", "Comms");
    CHECK(!resolve_and_match(doc, wrong_table).ok,
          "and citing the wrong sub-table is refused, which the old "
          "line citation could not tell apart");
}

// ------------------------------------------------------------- prose

void test_prose_is_addressed_by_sentence() {
    const auto doc = chapter();
    SourceLocator l;
    l.file = kChapter;
    l.path = {"Survival"};
    l.kind = LocatorKind::Sentence;
    l.exact = "If you fail this roll, your character is dead";
    const auto r = resolve_and_match(doc, l);
    CHECK(r.ok, "a stated rule resolves to its sentence: " + r.reason);
    CHECK(r.text.find("survival roll") != std::string::npos ||
              r.text.find("dead") != std::string::npos,
          "and the sentence is what the book says there");

    SourceLocator wrong = l;
    wrong.path = {"Aging"};
    CHECK(!resolve_and_match(doc, wrong).ok,
          "the same sentence cited to the wrong section is refused");
}

// ------------------------------------------------------ loud failures

void test_a_bad_address_says_what_is_wrong() {
    const auto doc = chapter();

    auto no_table = cell("Nonexistent Table", "1", "Scout", "Comms");
    auto r1 = resolve(doc, no_table);
    CHECK(!r1.ok && r1.reason.find("Nonexistent Table") != std::string::npos,
          "a missing table names itself, and lists what IS there");
    std::cout << "  [measure] " << r1.reason.substr(0, 110) << std::endl;

    auto no_col = cell("Service Skills", "1", "Xenobiologist", "Comms");
    auto r2 = resolve(doc, no_col);
    CHECK(!r2.ok && r2.reason.find("column") != std::string::npos,
          "a missing column names the columns that exist");

    auto no_row = cell("Service Skills", "9", "Scout", "Comms");
    CHECK(!resolve(doc, no_row).ok, "a missing row fails");

    SourceLocator no_label = cell("", "1", "Scout", "Comms");
    auto r4 = resolve(doc, no_label);
    CHECK(!r4.ok && r4.reason.find("label") != std::string::npos,
          "and a cell locator with no table label is refused outright, "
          "because that is the ambiguity that started all this");
}

}  // namespace

int main() {
    std::cout << "Source locators (the bounding box of a rule)" << std::endl;
    test_the_chapter_parses_into_shapes();
    test_a_cell_resolves_to_one_value();
    test_the_lie_that_got_through_is_refused();
    test_a_table_row_is_told_from_its_neighbours();
    test_prose_is_addressed_by_sentence();
    test_a_bad_address_says_what_is_wrong();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
