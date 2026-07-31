// =============================================================================
// LLMSystemHTTP JSON extraction — the double-encoding regression.
// =============================================================================
// TDD fixture from the Logogenesis playtest (2026-07-31): the
// Anthropic response body's "text" value was returned as its RAW
// JSON-encoded bytes — literal \n and \" two-char sequences — so
// every downstream parse of the model's JSON failed at column 2
// ("invalid literal; last read: '{\'"). json_extract_string must
// return the DECODED string. Also locks the escape-state scan
// (a value ending in an escaped backslash must not eat the real
// closing quote) and basic \uXXXX handling.
//
// Usage:
//   ./build/test_llm_json_extract
// =============================================================================

#include "logosphere/llm/llm_system_http.h"

#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while (0)

using Logosphere::LLMSystemHTTP;

void test_captured_anthropic_fixture_is_decoded() {
    // Verbatim shape of the failing playtest response (trimmed): the
    // model's reply is a fenced JSON object; inside the HTTP body it
    // is one JSON string, so every quote/newline is escaped.
    const std::string body =
        "{\"id\":\"msg_x\",\"content\":[{\"type\":\"text\","
        "\"text\":\"```json\\n{\\n  \\\"thoughts\\\": \\\"A tree blooms"
        " into being.\\\",\\n  \\\"ops\\\": []\\n}\\n```\"}],"
        "\"model\":\"claude-haiku-4-5\"}";

    auto text = LLMSystemHTTP::json_extract_string(body, "text");

    ASSERT(text.find("\\n") == std::string::npos,
           "no literal backslash-n survives (got: " + text + ")");
    ASSERT(text.find("\\\"") == std::string::npos,
           "no literal backslash-quote survives");
    ASSERT(text.find("\"thoughts\": \"A tree blooms into being.\"") !=
               std::string::npos,
           "decoded text contains the model's real JSON");
    ASSERT(text.find("```json\n{") == 0,
           "real newlines restored (fence then newline then brace)");
}

void test_escaped_backslash_before_closing_quote() {
    // Value ends with an escaped backslash; the naive scan saw
    // [prev char == '\\'] and skipped the REAL closing quote.
    const std::string body = "{\"text\":\"C:\\\\path\\\\\",\"next\":\"v\"}";
    auto text = LLMSystemHTTP::json_extract_string(body, "text");
    ASSERT(text == "C:\\path\\",
           "escaped backslashes decode and terminate correctly (got: " +
               text + ")");
}

void test_unicode_escape_basic() {
    const std::string body = "{\"text\":\"\\u0041 tree \\u00e9\"}";
    auto text = LLMSystemHTTP::json_extract_string(body, "text");
    ASSERT(text.find("A tree") == 0,
           "\\u0041 decodes to 'A' (got: " + text + ")");
    ASSERT(text.find("\xc3\xa9") != std::string::npos,
           "BMP escape decodes to UTF-8");
}

void test_whitespace_after_colon() {
    // Pretty-printed / spaced bodies: "text": "value"
    const std::string body = "{\"content\":[{\"type\":\"text\", \"text\": \"hello\"}]}";
    auto text = LLMSystemHTTP::json_extract_string(body, "text");
    ASSERT(text == "hello",
           "spaced colon extracts (got: '" + text + "')");
}

void test_plain_value_passthrough() {
    const std::string body = "{\"text\":\"hello world\"}";
    ASSERT(LLMSystemHTTP::json_extract_string(body, "text") == "hello world",
           "plain values pass through");
    ASSERT(LLMSystemHTTP::json_extract_string(body, "missing").empty(),
           "missing key returns empty");
}

void test_escape_roundtrip() {
    const std::string original = "line1\nline2 \"quoted\" back\\slash\ttab";
    auto encoded = LLMSystemHTTP::json_escape(original);
    auto decoded = LLMSystemHTTP::json_unescape(encoded);
    ASSERT(decoded == original, "escape -> unescape roundtrips");
}

int main() {
    std::cout << "=== LLM JSON extraction (double-encoding regression) ==="
              << std::endl;
    test_captured_anthropic_fixture_is_decoded();
    test_escaped_backslash_before_closing_quote();
    test_unicode_escape_basic();
    test_whitespace_after_colon();
    test_plain_value_passthrough();
    test_escape_roundtrip();
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
