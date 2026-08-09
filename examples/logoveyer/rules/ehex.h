// Pseudo-hexadecimal ("ehex") notation.
//
// [srd/cepheus introduction.md "Pseudo-Hexadecimal Notation";
//  book1/character-creation.md "Characteristic Modifiers" table,
//  PseudoHex column; "The Universal Persona Profile (UPP)"]
//
// Digits 0-9 then letters, SKIPPING I and O (the table is explicit:
// 15-17 is F-H, 18-20 is J-L, 21-23 is M-P). Range 0..33 = '0'..'Z'.
// The book's worked example is the test vector: scores
// 6,8,7,11,9,12 -> "687B9C", and with Psi 4 -> "687B9C-4".

#ifndef LOGOVEYER_EHEX_H
#define LOGOVEYER_EHEX_H

#include <string>

namespace logoveyer {

inline constexpr char kEhexDigits[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";
// index:                              0.........9..........^skip I  ^skip O

inline char to_ehex(int value) {
    if (value < 0 || value > 33) return '?';
    return kEhexDigits[value];
}

inline int from_ehex(char c) {
    for (int i = 0; i <= 33; ++i)
        if (kEhexDigits[i] == c) return i;
    return -1;   // includes 'I' and 'O', which are not ehex digits
}

// The UPP: six scores in fixed order, then "-Psi" only for psionic
// characters [book1/character-creation.md "The Explanation"].
inline std::string upp(int str, int dex, int end, int intel, int edu,
                       int soc, int psi = -1) {
    std::string s;
    for (int v : {str, dex, end, intel, edu, soc}) s += to_ehex(v);
    if (psi >= 0) { s += '-'; s += to_ehex(psi); }
    return s;
}

} // namespace logoveyer

#endif
