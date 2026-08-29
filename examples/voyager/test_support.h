// Shared scanners for Voyager's gate tests. A header, not a library:
// each test remains a standalone executable with its own CHECK and its
// own verdict; what they share is only how shipping code is found and
// read, because three private copies of a scanner is how one of them
// quietly diverges from the property the other two still hold.
//
// NOT under src/ on purpose: this file is test tooling, ships no
// behaviour, and must stay outside the very scans it implements.

#ifndef VOYAGER_TEST_SUPPORT_H
#define VOYAGER_TEST_SUPPORT_H

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace voyager_test {

inline std::string slurp(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream bytes;
    bytes << input.rdbuf();
    return bytes.str();
}

inline bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Does `token` appear in `text` as a whole word? Word-bounded on
// purpose: "Int" must not fire on "Integer", and "end" must not fire
// on ".end()". Case sensitive, because these are the book's spellings
// and a lower-case `strength` in a slot name is exactly the leak
// looked for.
inline bool names_word(const std::string& text, const std::string& token) {
    if (token.empty()) return false;
    for (size_t at = text.find(token); at != std::string::npos;
         at = text.find(token, at + 1)) {
        const bool before = at > 0 && is_word_char(text[at - 1]);
        const size_t after_at = at + token.size();
        const bool after =
            after_at < text.size() && is_word_char(text[after_at]);
        if (!before && !after) return true;
    }
    return false;
}

// Every shipping translation unit and header of this game. Generated
// registries are excluded and only they: a generated registry declares
// the ontology, which is where graph-owned names BELONG, and it is
// written by a tool from the schema rather than by a person from
// memory. `generated_skipped` counts the exclusions so a caller can
// prove the exclusion excluded something; a scope that silently
// widens reports the schema's own output as leaks.
inline std::vector<std::filesystem::path> shipping_sources(
    const std::filesystem::path& root, std::size_t& generated_skipped) {
    std::vector<std::filesystem::path> out;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root / "src")) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        // generic_string, not string: native separators are '\' on
        // Windows and this match must not depend on the platform.
        if (path.generic_string().find("/generated/") != std::string::npos) {
            ++generated_skipped;
            continue;
        }
        const auto extension = path.extension().string();
        if (extension != ".cpp" && extension != ".h") continue;
        out.push_back(path);
    }
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        if (path.extension() != ".cpp") continue;
        // Tests spell what they are testing; they ship no behaviour.
        if (path.filename().string().rfind("test_", 0) == 0) continue;
        out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The same set, concatenated, for scans that only need presence.
inline std::string shipping_text(const std::filesystem::path& root,
                                 std::size_t& generated_skipped) {
    std::string all;
    for (const auto& path : shipping_sources(root, generated_skipped)) {
        all += slurp(path);
    }
    return all;
}

}  // namespace voyager_test

#endif  // VOYAGER_TEST_SUPPORT_H
