#include "model_referee.h"

#include "logosphere/llm/llm_system_http.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace voyager {
namespace {

// Haiku by default. Both calls are short, written from facts that are
// already decided, and the whole point is that they come back while the
// dice are still on screen.
constexpr const char* kModel = "claude-haiku-4-5-20251001";
constexpr const char* kLocalUrl = "http://127.0.0.1:8081";
constexpr const char* kLocalModel = "qwen";

std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

int env_int(const char* name, int fallback, int floor_value) {
    const int value = std::atoi(env_or(name, std::to_string(fallback)).c_str());
    return value < floor_value ? floor_value : value;
}

std::string slurp(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

ModelReferee::ModelReferee() = default;
ModelReferee::~ModelReferee() = default;

std::string ModelReferee::who() const {
    if (backend_.empty() && model_.empty()) return {};
    return backend_ + "/" + model_;
}

bool ModelReferee::initialize(const std::string& brief_path,
                              std::string& error) {
    brief_ = slurp(brief_path);
    if (brief_.empty()) {
        error = "no referee brief at " + brief_path +
                ": that file is the only thing this referee knows about "
                "its job and about the universe, so there is nothing to "
                "referee with";
        return false;
    }

    const char* key = std::getenv("ANTHROPIC_API_KEY");
    const bool local = env_or("VOYAGER_LLM", "") == "mlx";
    if (!local && (!key || !*key)) {
        error = "ANTHROPIC_API_KEY is not set. Two questions in this "
                "procedure are not the engine's to answer and there is "
                "no fallback for them: export the key and run again, or "
                "point VOYAGER_LLM=mlx at a local model.";
        return false;
    }

    llm_ = std::make_unique<Logosphere::LLMSystemHTTP>();
    backend_ = local ? "mlx" : "anthropic";
    model_ = env_or("VOYAGER_LLM_MODEL", local ? kLocalModel : kModel);
    if (local) {
        const std::string url = env_or("VOYAGER_LLM_URL", kLocalUrl);
        if (!llm_->initialize_mlx(url, model_)) {
            error = "no local model answered at " + url +
                    ". Start one (mlx_lm.server) or unset VOYAGER_LLM.";
            llm_.reset();
            return false;
        }
    } else if (!llm_->initialize_anthropic(key, model_)) {
        error = "could not reach the Anthropic API";
        llm_.reset();
        return false;
    }

    llm_->set_narrative_system_prompt(brief_);
    // Sized, never defaulted at the call site. A library default that
    // becomes a production ceiling is how a reply arrives cut off
    // mid-sentence and the run dies on a formatting error.
    reply_budget_ = env_int("VOYAGER_REFEREE_MAX_TOKENS", 1024, 128);
    timeout_ms_ = env_int("VOYAGER_REFEREE_TIMEOUT_MS", 45000, 1000);
    return true;
}

bool ModelReferee::answer(const RefereeQuestion& question, std::string& out,
                          std::string& error) {
    out.clear();
    error.clear();
    if (!llm_) {
        error = "the referee was never initialized";
        return false;
    }

    bool done = false;
    std::string reply;
    llm_->submit_request(
        question.prompt, reply_budget_,
        [&done, &reply](const std::string&, const std::string& response,
                        void*) {
            reply = response;
            done = true;
        });
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms_);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        llm_->process_completed_responses();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!done) {
        error = "the referee did not answer '" + question.site + "' within " +
                std::to_string(timeout_ms_) + "ms";
        return false;
    }
    // The transport reports failures as text, and text is what this
    // deals in, so the two are told apart before either is used.
    if (reply.empty() || reply.rfind("[ERROR", 0) == 0) {
        error = "the referee failed on '" + question.site + "': " +
                (reply.empty() ? std::string("empty reply") : reply);
        return false;
    }
    out = reply;
    return true;
}

}  // namespace voyager
