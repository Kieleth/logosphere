#pragma once

#include "logosphere/kg/kg_types.h"

#include <string>
#include <string_view>

namespace kg {
class KGModule;
}

namespace logosphere::text {

struct SourceTargetResult {
    bool        ok = false;
    std::string text;
    std::string reason;
};

// Portable digest used by SourceRepresentationContext. The algorithm
// vocabulary comes from the LinkML schema; this function implements its
// currently supported SHA256 member.
std::string sha256_hex(std::string_view bytes);

// Mechanical projection of a ByteRangeSelector for Addressable.entity_key.
// The source representation remains the identity context, so the complete
// portable identity is representation plus this selector key.
std::string canonical_byte_range_key(long long start, long long end);

// Resolve one LinkML-typed SourceTarget against the bytes held by the source
// tool. Every required property is checked explicitly. No missing property
// receives a default, and a supporting quote must converge with the primary
// byte selector.
SourceTargetResult resolve_text_target(const kg::KGModule& world,
                                       kg::EntityID target,
                                       std::string_view source_bytes);

}  // namespace logosphere::text
