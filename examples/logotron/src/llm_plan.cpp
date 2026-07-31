#include "llm_plan.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace logotron {

const char* llm_mode_name(LLMPlan::Mode m) {
    switch (m) {
        case LLMPlan::Mode::None:      return "none (built-in AI)";
        case LLMPlan::Mode::OpenAI:    return "OpenAI";
        case LLMPlan::Mode::Anthropic: return "Anthropic";
        case LLMPlan::Mode::MLX:       return "MLX-LM (local)";
    }
    return "?";
}

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* nonempty(const EnvGetter& fn, const char* key) {
    if (!fn) return nullptr;
    const char* v = fn(key);
    return (v && *v) ? v : nullptr;
}

LLMPlan resolve(const EnvGetter& fn, LLMPlan::Mode forced_mode) {
    LLMPlan plan;
    const char* override_model = nonempty(fn, "LOGOTRON_LLM_MODEL");

    auto try_anthropic = [&]() -> bool {
        const char* key = nonempty(fn, "ANTHROPIC_API_KEY");
        if (!key) return false;
        plan.mode    = LLMPlan::Mode::Anthropic;
        plan.api_key = key;
        plan.model   = override_model ? override_model : "claude-haiku-4-5";
        return true;
    };
    auto try_openai = [&]() -> bool {
        const char* key = nonempty(fn, "OPENAI_API_KEY");
        if (!key) return false;
        plan.mode    = LLMPlan::Mode::OpenAI;
        plan.api_key = key;
        plan.model   = override_model ? override_model : "gpt-4o-mini";
        return true;
    };
    auto try_mlx = [&]() -> bool {
        const char* url = nonempty(fn, "LOGOTRON_LLM_URL");
        if (!url) return false;
        plan.mode  = LLMPlan::Mode::MLX;
        plan.url   = url;
        plan.model = override_model ? override_model : "default";
        return true;
    };

    switch (forced_mode) {
        case LLMPlan::Mode::Anthropic: try_anthropic(); return plan;
        case LLMPlan::Mode::OpenAI:    try_openai();    return plan;
        case LLMPlan::Mode::MLX:      try_mlx();      return plan;
        case LLMPlan::Mode::None:      break;
    }

    if (try_anthropic()) return plan;
    if (try_openai())    return plan;
    if (try_mlx())      return plan;
    return plan;
}

}  // namespace

LLMPlan plan_llm_from_env(const EnvGetter& fn) {
    LLMPlan::Mode forced = LLMPlan::Mode::None;
    if (const char* p = nonempty(fn, "LOGOTRON_LLM_PROVIDER")) {
        std::string v = lower(p);
        if      (v == "anthropic") forced = LLMPlan::Mode::Anthropic;
        else if (v == "openai")    forced = LLMPlan::Mode::OpenAI;
        else if (v == "mlx")      forced = LLMPlan::Mode::MLX;
        else if (v == "none" || v == "off") return {};  // explicit offline
    }
    return resolve(fn, forced);
}

LLMPlan plan_llm_from_env() {
    return plan_llm_from_env(
        [](const char* name) -> const char* { return std::getenv(name); });
}

} // namespace logotron
