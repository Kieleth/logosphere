#include "logosphere/kg/finite_float.h"

#include <cfenv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace kg {
namespace {

class NearestRoundingGuard {
public:
    NearestRoundingGuard() : previous_(std::fegetround()) {
        if (previous_ == -1) {
            error_ = "cannot read the floating-point rounding mode";
            return;
        }
        if (previous_ != FE_TONEAREST &&
            std::fesetround(FE_TONEAREST) != 0) {
            error_ = "cannot set round-to-nearest floating-point mode";
            return;
        }
        active_ = true;
    }

    ~NearestRoundingGuard() {
        if (active_ && previous_ != FE_TONEAREST) {
            std::fesetround(previous_);
        }
    }

    bool ok() const { return active_; }
    const std::string& error() const { return error_; }

private:
    int previous_ = -1;
    bool active_ = false;
    std::string error_;
};

bool is_binary64_platform() {
    return sizeof(double) == 8 &&
           std::numeric_limits<double>::is_iec559 &&
           std::numeric_limits<double>::radix == 2 &&
           std::numeric_limits<double>::digits == 53 &&
           std::numeric_limits<double>::max_exponent == 1024;
}

}  // namespace

FiniteFloatParseResult parse_finite_binary64(const std::string& text) {
    if (!is_binary64_platform()) {
        return {false, 0.0,
                "platform does not provide IEEE 754 binary64"};
    }
    NearestRoundingGuard rounding;
    if (!rounding.ok()) return {false, 0.0, rounding.error()};

    try {
        size_t end = 0;
        const double value = std::stod(text, &end);
        if (end != text.size()) {
            return {false, 0.0, "expected float, got '" + text + "'"};
        }
        if (!std::isfinite(value)) {
            return {false, 0.0,
                    "expected finite float, got '" + text + "'"};
        }
        return {true, value, ""};
    } catch (const std::out_of_range&) {
        return {false, 0.0,
                "expected finite float, got '" + text + "'"};
    } catch (...) {
        return {false, 0.0, "expected float, got '" + text + "'"};
    }
}

}  // namespace kg
