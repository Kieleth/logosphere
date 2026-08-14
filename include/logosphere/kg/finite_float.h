#ifndef LOGOSPHERE_KG_FINITE_FLOAT_H
#define LOGOSPHERE_KG_FINITE_FLOAT_H

#include <string>

namespace kg {

struct FiniteFloatParseResult {
    bool ok = false;
    double value = 0.0;
    std::string error;
};

// Parse one complete finite IEEE 754 binary64 value under round-to-nearest,
// ties-to-even. The caller's floating-point rounding mode is restored before
// return. NaN, infinities, overflow, trailing text, and unsupported platforms
// fail loudly.
FiniteFloatParseResult parse_finite_binary64(const std::string& text);

}  // namespace kg

#endif  // LOGOSPHERE_KG_FINITE_FLOAT_H
