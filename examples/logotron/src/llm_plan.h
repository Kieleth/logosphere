// Resolve LLM provider configuration from environment variables.
//
// Headless-testable: the resolver takes a getenv-shaped function so
// tests can drive it with canned env values. Production passes a
// thin wrapper over std::getenv.
//
// Priority (highest to lowest):
//   1. LOGOTRON_LLM_PROVIDER explicit override (anthropic | openai | mlx)
//   2. ANTHROPIC_API_KEY  (default model: claude-haiku-4-5)
//   3. OPENAI_API_KEY     (default model: gpt-4o-mini)
//   4. LOGOTRON_LLM_URL   (default model: default; MLX-LM local server,
//                          e.g. mlx_lm.server on http://localhost:8080)
//   5. None (offline random Director)
//
// Anthropic outranks OpenAI because Logotron's design documents
// Anthropic as the canonical Director provider and haiku-4-5 has the
// latency profile we want. Flipping was a deliberate fix after the
// first live playtest landed on gpt-4o-mini against the user's
// expectation.

#pragma once

#include <functional>
#include <string>

namespace logotron {

struct LLMPlan {
    enum class Mode { None, OpenAI, Anthropic, MLX };
    Mode mode = Mode::None;
    std::string api_key;
    std::string url;
    std::string model;
};

const char* llm_mode_name(LLMPlan::Mode m);

// Env getter shape: returns nullptr when the var is unset OR empty.
using EnvGetter = std::function<const char*(const char* name)>;

// Resolve a plan from a custom env getter. Use this in tests with a
// stub getter; production calls the no-arg overload below.
LLMPlan plan_llm_from_env(const EnvGetter& getenv_fn);

// Production resolver: reads the real process environment.
LLMPlan plan_llm_from_env();

} // namespace logotron
