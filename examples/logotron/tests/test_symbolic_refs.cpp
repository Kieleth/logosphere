// Headless test — Logotron's SymbolicRefs map. Pure logic, no
// engine. Catches regressions where the @-prefix handling, the
// reverse map, or the prompt text formatting drifts.

#include "director/symbolic_refs.h"

#include <iostream>
#include <stdexcept>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

void resolve_with_or_without_at_prefix() {
    auto refs = logotron::director::build_symbolic_refs(
        /*player=*/7, /*programs=*/{11}, /*arena=*/3);
    ASSERT_TRUE(refs.resolve("@player_cycle") == 7,
        "@player_cycle should resolve to 7");
    ASSERT_TRUE(refs.resolve("player_cycle") == 7,
        "bare player_cycle should also resolve to 7");
    ASSERT_TRUE(refs.resolve("@program_1") == 11,
        "@program_1 should resolve to 11");
    ASSERT_TRUE(refs.resolve("@arena") == 3,
        "@arena should resolve to 3");
}

void unknown_alias_returns_invalid() {
    auto refs = logotron::director::build_symbolic_refs(7, {11}, 3);
    ASSERT_TRUE(refs.resolve("@nope") == kg::INVALID_ENTITY,
        "unknown alias must yield INVALID_ENTITY");
    ASSERT_TRUE(refs.resolve("") == kg::INVALID_ENTITY,
        "empty string must yield INVALID_ENTITY");
}

void reverse_map_returns_alias_string() {
    auto refs = logotron::director::build_symbolic_refs(7, {11}, 3);
    ASSERT_TRUE(refs.alias_for(7)  == "player_cycle", "reverse for 7");
    ASSERT_TRUE(refs.alias_for(11) == "program_1",    "reverse for 11");
    ASSERT_TRUE(refs.alias_for(3)  == "arena",        "reverse for 3");
    ASSERT_TRUE(refs.alias_for(99).empty(),
        "unknown id must yield empty alias");
}

void invalid_entity_skipped_during_build() {
    auto refs = logotron::director::build_symbolic_refs(
        kg::INVALID_ENTITY, {11}, 3);
    ASSERT_TRUE(refs.resolve("@player_cycle") == kg::INVALID_ENTITY,
        "missing player must not register an alias");
    ASSERT_TRUE(refs.resolve("@program_1") == 11,
        "ai still registers");
}

void prompt_text_contains_each_alias() {
    auto refs = logotron::director::build_symbolic_refs(7, {11}, 3);
    auto text = refs.as_prompt_text();
    ASSERT_TRUE(text.find("@player_cycle=7")  != std::string::npos,
        "prompt missing @player_cycle=7");
    ASSERT_TRUE(text.find("@program_1=11")     != std::string::npos,
        "prompt missing @program_1=11");
    ASSERT_TRUE(text.find("@arena=3")         != std::string::npos,
        "prompt missing @arena=3");
    ASSERT_TRUE(text.find("Symbols:") == 0,
        "prompt should start with 'Symbols:'");
}

void empty_refs_yield_empty_prompt() {
    auto refs = logotron::director::build_symbolic_refs(
        kg::INVALID_ENTITY, {}, kg::INVALID_ENTITY);
    ASSERT_TRUE(refs.as_prompt_text().empty(),
        "empty refs must yield empty prompt text");
}

int main() {
    std::cout << "=== test_symbolic_refs ===" << std::endl;
    TEST(resolve_with_or_without_at_prefix);
    TEST(unknown_alias_returns_invalid);
    TEST(reverse_map_returns_alias_string);
    TEST(invalid_entity_skipped_during_build);
    TEST(prompt_text_contains_each_alias);
    TEST(empty_refs_yield_empty_prompt);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
