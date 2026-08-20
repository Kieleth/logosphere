#include "logosphere/text/source_target.h"

#include "logosphere/kg/kg_module.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace logosphere::text {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256Round = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

std::uint32_t rotate_right(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

SourceTargetResult fail(const std::string& reason) {
    SourceTargetResult result;
    result.reason = reason;
    return result;
}

bool parse_nonnegative(const kg::KGModule& world, kg::EntityID entity,
                       const char* property, std::uint64_t& out,
                       std::string& reason) {
    if (!world.hasProperty(entity, property)) {
        reason = std::string("missing required ") + property;
        return false;
    }
    const auto value = world.getProperty(entity, property);
    if (value.empty() || value.front() == '-') {
        reason = std::string(property) + " must be a non-negative integer";
        return false;
    }
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, out);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        reason = std::string(property) + " must be a non-negative integer";
        return false;
    }
    return true;
}

bool parse_reference(const kg::KGModule& world, kg::EntityID entity,
                     const char* property, kg::EntityID& out,
                     std::string& reason) {
    std::uint64_t parsed = 0;
    if (!parse_nonnegative(world, entity, property, parsed, reason))
        return false;
    if (parsed > UINT32_MAX || !world.exists(static_cast<kg::EntityID>(parsed))) {
        reason = std::string(property) + " does not reference an existing entity";
        return false;
    }
    out = static_cast<kg::EntityID>(parsed);
    return true;
}

bool is_lower_hex_digest(const std::string& value) {
    if (value.size() != 64) return false;
    for (const char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

}  // namespace

std::string sha256_hex(std::string_view bytes) {
    std::array<std::uint32_t, 8> state = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    std::string padded(bytes);
    const std::uint64_t bit_length =
        static_cast<std::uint64_t>(padded.size()) * 8;
    padded.push_back(static_cast<char>(0x80));
    while (padded.size() % 64 != 56) padded.push_back('\0');
    for (int shift = 56; shift >= 0; shift -= 8)
        padded.push_back(static_cast<char>((bit_length >> shift) & 0xff));

    for (std::size_t offset = 0; offset < padded.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto at = offset + i * 4;
            words[i] =
                (static_cast<std::uint32_t>(
                     static_cast<unsigned char>(padded[at])) << 24) |
                (static_cast<std::uint32_t>(
                     static_cast<unsigned char>(padded[at + 1])) << 16) |
                (static_cast<std::uint32_t>(
                     static_cast<unsigned char>(padded[at + 2])) << 8) |
                static_cast<std::uint32_t>(
                    static_cast<unsigned char>(padded[at + 3]));
        }
        for (std::size_t i = 16; i < words.size(); ++i) {
            const auto s0 = rotate_right(words[i - 15], 7) ^
                            rotate_right(words[i - 15], 18) ^
                            (words[i - 15] >> 3);
            const auto s1 = rotate_right(words[i - 2], 17) ^
                            rotate_right(words[i - 2], 19) ^
                            (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                              rotate_right(e, 25);
            const auto choose = (e & f) ^ ((~e) & g);
            const auto temp1 = h + sum1 + choose + kSha256Round[i] + words[i];
            const auto sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                              rotate_right(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto word : state) out << std::setw(8) << word;
    return out.str();
}

namespace {

// The digest of a source file, remembered so the same file is not hashed
// again for the next target that cites it.
//
// A verifier resolves one SourceTarget per citation, and every resolution
// re-hashes the WHOLE file the target cites to prove the declared digest
// still describes those bytes. Logovger's rule seeds hold 2064 targets over
// two source files, and test_chargen builds the rule world 49 times: that
// was 785,489 SHA-256 passes over 44.5 GB, 114 of the test's 263 seconds,
// to compute two distinct digests.
//
// Reuse is safe because it is earned by comparison, not assumed. An entry
// is used only when the incoming bytes are byte-for-byte the cached ones,
// which is a memcmp instead of a SHA-256 pass. Nothing is taken on trust:
// no pointer identity, no length-and-mtime, no digest supplied by a caller.
// sha256_hex is a pure function, so a hit returns exactly what a fresh pass
// would have returned.
//
// Small and thread-local on purpose. A corpus is a handful of files, so
// four MRU entries cover it (measured: 1,487 passes instead of 785,489),
// and per-thread state keeps a const resolver free of shared mutable state.
std::string memoized_digest(std::string_view bytes) {
    struct Remembered {
        std::string bytes;
        std::string digest;
    };
    static thread_local std::vector<Remembered> remembered;
    constexpr std::size_t kCapacity = 4;

    for (std::size_t i = 0; i < remembered.size(); ++i) {
        if (remembered[i].bytes.size() != bytes.size()) continue;
        // Empty bytes compare equal on the size alone. memcmp is skipped
        // rather than called with a length of zero: a string_view over
        // nothing may carry a null data pointer, and passing it is
        // undefined even when nothing would be read.
        if (!bytes.empty() &&
            std::memcmp(remembered[i].bytes.data(), bytes.data(),
                        bytes.size()) != 0)
            continue;
        if (i != 0) std::swap(remembered[0], remembered[i]);
        return remembered[0].digest;
    }

    std::string digest = sha256_hex(bytes);
    if (remembered.size() >= kCapacity) remembered.pop_back();
    remembered.insert(remembered.begin(),
                      Remembered{std::string(bytes), digest});
    return digest;
}

}  // namespace

std::string canonical_byte_range_key(long long start, long long end) {
    return "byte-range:" + std::to_string(start) + ":" +
           std::to_string(end);
}

SourceTargetResult resolve_text_target(const kg::KGModule& world,
                                       kg::EntityID target,
                                       std::string_view source_bytes) {
    if (!world.exists(target)) return fail("source target does not exist");
    const auto& ontology = world.getRegistry();
    if (!ontology.isSubtypeOf(world.getType(target), "SourceTarget"))
        return fail("entity is not a SourceTarget");

    std::string reason;
    kg::EntityID representation = kg::INVALID_ENTITY;
    if (!parse_reference(world, target, "target_representation",
                         representation, reason))
        return fail(reason);
    if (!ontology.isSubtypeOf(world.getType(representation),
                              "SourceRepresentationContext"))
        return fail("target_representation is not a SourceRepresentationContext");
    if (world.getProperty(target, "identity_context") !=
        std::to_string(representation))
        return fail("SourceTarget identity_context must be its representation");

    if (!world.hasProperty(representation, "source_media_type"))
        return fail("missing required source_media_type");
    if (world.getProperty(representation, "source_media_type") != "UTF8_TEXT")
        return fail("source_media_type is not supported by the text resolver");
    if (!world.hasProperty(representation, "source_digest_algorithm"))
        return fail("missing required source_digest_algorithm");
    if (world.getProperty(representation, "source_digest_algorithm") !=
        "SHA256")
        return fail("source_digest_algorithm is not supported");

    std::uint64_t declared_length = 0;
    if (!parse_nonnegative(world, representation, "source_byte_length",
                           declared_length, reason))
        return fail(reason);
    if (declared_length != source_bytes.size())
        return fail("source byte length does not match the representation");

    if (!world.hasProperty(representation, "source_digest"))
        return fail("missing required source_digest");
    const auto digest = world.getProperty(representation, "source_digest");
    if (!is_lower_hex_digest(digest))
        return fail("source digest must be 64 lowercase hexadecimal digits");
    if (digest != memoized_digest(source_bytes))
        return fail("source digest does not match the representation bytes");

    kg::EntityID selector = kg::INVALID_ENTITY;
    if (!parse_reference(world, target, "target_primary_selector", selector,
                         reason))
        return fail(reason);
    if (!ontology.isSubtypeOf(world.getType(selector), "ByteRangeSelector"))
        return fail("UTF8_TEXT requires a ByteRangeSelector");
    if (world.getProperty(selector, "identity_context") !=
        std::to_string(representation))
        return fail("primary selector identity_context must be its representation");

    std::uint64_t start = 0;
    std::uint64_t end = 0;
    if (!parse_nonnegative(world, selector, "source_byte_start", start, reason) ||
        !parse_nonnegative(world, selector, "source_byte_end", end, reason))
        return fail(reason);
    if (start > end)
        return fail("source byte start must not exceed source byte end");
    if (end > source_bytes.size())
        return fail("source byte end exceeds the representation length");

    const auto canonical = canonical_byte_range_key(
        static_cast<long long>(start), static_cast<long long>(end));
    if (world.getProperty(selector, "entity_key") != canonical)
        return fail("primary selector entity_key is not canonical");
    if (world.getProperty(target, "entity_key") != canonical)
        return fail("SourceTarget entity_key is not its primary selector");

    const std::string selected(source_bytes.substr(
        static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)));

    if (world.hasProperty(target, "target_quote_selector")) {
        kg::EntityID quote = kg::INVALID_ENTITY;
        if (!parse_reference(world, target, "target_quote_selector", quote,
                             reason))
            return fail(reason);
        if (!ontology.isSubtypeOf(world.getType(quote), "TextQuoteSelector"))
            return fail("target_quote_selector is not a TextQuoteSelector");
        if (world.getProperty(quote, "identity_context") !=
            std::to_string(representation))
            return fail("quote selector identity_context must be its representation");
        if (!world.hasProperty(quote, "source_quote_exact"))
            return fail("missing required source_quote_exact");
        if (world.getProperty(quote, "source_quote_exact") != selected)
            return fail("quote selector does not match the primary byte range");

        const auto prefix = world.getProperty(quote, "source_quote_prefix");
        if (!prefix.empty() &&
            (start < prefix.size() ||
             source_bytes.substr(static_cast<std::size_t>(start) - prefix.size(),
                                 prefix.size()) != prefix))
            return fail("quote prefix does not match the primary byte range");
        const auto suffix = world.getProperty(quote, "source_quote_suffix");
        if (!suffix.empty() &&
            (end + suffix.size() > source_bytes.size() ||
             source_bytes.substr(static_cast<std::size_t>(end), suffix.size()) !=
                 suffix))
            return fail("quote suffix does not match the primary byte range");
    }

    SourceTargetResult result;
    result.ok = true;
    result.text = selected;
    return result;
}

}  // namespace logosphere::text
