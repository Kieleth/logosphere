// Characteristic modifiers.
//
// [srd/cepheus book1/character-creation.md "Characteristic Modifiers"]:
// "calculated by dividing the ability score by three, dropping all
// fractions, and then subtracting two". The table beside the formula
// runs 0-2 -> -2 up to 33+ -> +9; the test checks the formula against
// every row of that table, because the book publishes both and they
// must agree.

#ifndef LOGOVEYER_CHARACTERISTICS_H
#define LOGOVEYER_CHARACTERISTICS_H

namespace logovger {

inline int characteristic_dm(int score) {
    if (score < 0) score = 0;
    return score / 3 - 2;   // integer division IS "dropping fractions"
}

} // namespace logovger

#endif
