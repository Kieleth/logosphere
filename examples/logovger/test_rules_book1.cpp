// The first absorbed rules, proven against the book's own numbers.
//
// Absorption discipline (docs/ABSORPTION_INVENTORY.md): a rule is DONE
// only when a test proves it against the book's published examples and
// tables. These are the Introduction/Chapter-1 encoding rules everything
// else stands on: ehex digits and the UPP. Characteristic modifiers are
// verified and selected from cepheus_book1_tables.json.
//
// Usage:
//   ./build/test_logovger_rules_book1

#undef NDEBUG

#include "rules/ehex.h"

#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

int main() {
    using namespace logovger;
    std::cout << "Book 1 rules vs the book's own numbers" << std::endl;

    // The book's worked UPP example [character-creation.md "The
    // Explanation"]: 6,8,7,11,9,12 -> 687B9C; Psi 4 appends -4.
    const std::string base = upp(6, 8, 7, 11, 9, 12);
    const std::string psionic = upp(6, 8, 7, 11, 9, 12, 4);
    std::cout << "  [measure] book example: " << base << " / " << psionic
              << std::endl;
    check(base == "687B9C", "the book's UPP example reproduces (" + base + ")");
    check(psionic == "687B9C-4", "and the psionic form (" + psionic + ")");

    // The ehex column of the DM table names its own skip points:
    // 15-17 = F-H, 18-20 = J-L (no I), 21-23 = M-P (no O).
    check(to_ehex(15) == 'F' && to_ehex(17) == 'H', "15-17 is F-H");
    check(to_ehex(18) == 'J', "18 is J: the alphabet skips I");
    check(to_ehex(22) == 'N' && to_ehex(23) == 'P',
          "22-23 is N-P: the alphabet skips O");
    check(to_ehex(33) == 'Z', "33 is Z, the table's last row");
    check(to_ehex(34) == 'Z' && to_ehex(1000) == 'Z',
          "the book's 33-or-higher row remains Z");
    check(from_ehex('I') == -1 && from_ehex('O') == -1,
          "I and O are NOT ehex digits and parse as invalid");
    // Round trip, the whole range.
    bool rt = true;
    for (int v = 0; v <= 33; ++v)
        if (from_ehex(to_ehex(v)) != v) rt = false;
    check(rt, "0..33 round-trips through ehex");

    check(upp(34, 8, 7, 11, 9, 12) == "Z87B9C",
          "UPP uses Z for a characteristic above 33");

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
