// The first absorbed rules, proven against the book's own numbers.
//
// Absorption discipline (docs/ABSORPTION_INVENTORY.md): a rule is DONE
// only when a test proves it against the book's published examples and
// tables. These are the Introduction/Chapter-1 rules everything else
// stands on: ehex digits, the UPP, the characteristic DM.
//
// Usage:
//   ./build/test_logovger_rules_book1

#undef NDEBUG

#include "rules/characteristics.h"
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
    check(from_ehex('I') == -1 && from_ehex('O') == -1,
          "I and O are NOT ehex digits and parse as invalid");
    // Round trip, the whole range.
    bool rt = true;
    for (int v = 0; v <= 33; ++v)
        if (from_ehex(to_ehex(v)) != v) rt = false;
    check(rt, "0..33 round-trips through ehex");

    // The DM formula against EVERY row of the published table
    // [character-creation.md "Characteristic Modifiers"]. The book
    // gives both the formula and the table; they must agree.
    struct Row { int lo, hi, dm; };
    const Row table[] = {{0,2,-2},{3,5,-1},{6,8,0},{9,11,1},{12,14,2},
                         {15,17,3},{18,20,4},{21,23,5},{24,26,6},
                         {27,29,7},{30,32,8},{33,33,9}};
    bool all = true;
    for (const Row& r : table)
        for (int v = r.lo; v <= r.hi; ++v)
            if (characteristic_dm(v) != r.dm) {
                all = false;
                std::cout << "  [measure] dm(" << v << ") = "
                          << characteristic_dm(v) << ", table says " << r.dm
                          << std::endl;
            }
    check(all, "the formula matches all 12 rows of the published table");
    check(characteristic_dm(7) == 0,
          "and the book's stated anchor: average 7 has DM+0");

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
