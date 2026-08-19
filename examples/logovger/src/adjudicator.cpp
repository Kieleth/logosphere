#include "adjudicator.h"

#include "judgment_answer.h"

#include <sys/stat.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace logovger {
namespace {

constexpr const char* kModel = "claude-haiku-4-5-20251001";
constexpr const char* kLocalUrl = "http://127.0.0.1:8081";
constexpr const char* kLocalModel = "qwen";

std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

std::string hex16(const std::string& text) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    std::ostringstream o;
    o << std::hex << std::setw(16) << std::setfill('0') << h;
    return o.str();
}

}  // namespace

bool Adjudicator::initialize(const std::string& lore_path,
                             std::string& error) {
    lore_ = slurp(lore_path);
    if (lore_.empty()) {
        error = "no lore at " + lore_path +
                ": the adjudicator judges inside a setting, and that "
                "file is the only setting it knows";
        return false;
    }

    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (!key || !*key) {
        error = "ANTHROPIC_API_KEY is not set. The adjudicator decides "
                "what the book leaves to a referee, and there is no "
                "fallback for it: export the key and run again.";
        return false;
    }

    llm_ = std::make_unique<Logosphere::LLMSystemHTTP>();
    const bool local = env_or("LOGOVGER_LLM", "") == "mlx";
    backend_ = local ? "mlx" : "anthropic";
    model_ = env_or("LOGOVGER_LLM_MODEL", local ? kLocalModel : kModel);
    if (local) {
        const std::string url = env_or("LOGOVGER_LLM_URL", kLocalUrl);
        if (!llm_->initialize_mlx(url, model_)) {
            error = "no local model answered at " + url +
                    ". Start one (mlx_lm.server) or unset LOGOVGER_LLM.";
            llm_.reset();
            return false;
        }
    } else if (!llm_->initialize_anthropic(key, model_)) {
        error = "could not reach the Anthropic API";
        llm_.reset();
        return false;
    }

    const std::string voice =
        std::string(
            "You are the referee of a science fiction roleplaying game, "
            "making the calls the rulebook leaves to a person.\n\n"
            "You are given a RULE, the OPTIONS it allows, exactly how "
            "many to choose, and the character's history. Choose, and "
            "say why in one clause.\n\n"
            "How to choose:\n"
            "- Read the life, not the sheet. What this person has done, "
            "where they served, what it cost them. A pilot's hands and "
            "a marine's knees do not go the same way.\n"
            "- Prefer taking from what they have leaned on hardest. The "
            "body gives out first where it was used most. This is a "
            "lean, not a law: a life that argues otherwise wins.\n"
            "- Never choose at random, and never choose to be kind. You "
            "are not balancing a character, you are deciding what "
            "happened to one.\n\n"
            "Answer format, exactly:\n"
            "CHOICE: <option>, <option>\n"
            "WHY: <one clause, under fifteen words, no game terms>\n\n"
            "Name options exactly as they are given to you. Choose the "
            "exact number asked for, no more and no fewer. Never name "
            "dice, rolls, numbers, modifiers or tables.\n\n"
            "THE SETTING:\n") + lore_;
    llm_->set_narrative_system_prompt(voice);
    voice_version_ = hex16(voice + "\n@" + backend_ + "/" + model_);

    cache_dir_ = env_or("LOGOVGER_JUDGMENT_CACHE",
                        "/tmp/logovger-judgments");
    ::mkdir(cache_dir_.c_str(), 0755);
    timeout_ms_ = std::atoi(env_or("LOGOVGER_JUDGMENT_TIMEOUT_MS",
                                   "30000").c_str());
    if (timeout_ms_ < 1000) timeout_ms_ = 1000;

    // The reply budget, sized rather than defaulted. It was 200,
    // written inline at the call, and a model that reasons before
    // answering spent them all on prose and was cut off mid-sentence:
    // the CHOICE line never arrived and the life ended. The fleet's
    // shared client makes max_tokens a REQUIRED argument for exactly
    // this reason, "a library default became a production ceiling
    // nobody sized".
    //
    // 1024 is sized against the answer we ask for: two lines, a
    // choice and a clause under fifteen words. That needs tens of
    // tokens. The rest is headroom for a model that thinks out loud
    // first, which several do and which the retry no longer punishes.
    reply_budget_ = std::atoi(env_or("LOGOVGER_JUDGMENT_MAX_TOKENS",
                                     "1024").c_str());
    if (reply_budget_ < 64) reply_budget_ = 64;

    attempts_ = std::atoi(env_or("LOGOVGER_JUDGMENT_ATTEMPTS", "3").c_str());
    if (attempts_ < 1) attempts_ = 1;
    return true;
}

std::string Adjudicator::build_prompt(const Judgment& judgment) const {
    std::ostringstream o;
    o << "RULE: " << judgment.rule << "\n\n";
    o << "CHOOSE EXACTLY " << judgment.count << " OF:\n";
    for (const auto& option : judgment.options) o << "  - " << option << "\n";
    if (!judgment.standing.empty()) {
        o << "\nWHERE THEY STAND NOW: " << judgment.standing << "\n"
          << "Anything marked already taken was struck by THIS same "
             "rule a moment ago. Do not take it again unless the rule "
             "means to.\n";
    }
    if (!judgment.hint.empty()) o << "\nLEAN: " << judgment.hint << "\n";
    o << "\nTHE LIFE SO FAR:\n" << judgment.history << "\n\n";
    o << judgment.question << "\n";
    return o.str();
}

std::string Adjudicator::cache_key(const Judgment& judgment,
                                   uint64_t seed) const {
    std::ostringstream o;
    o << seed << '|' << voice_version_ << '|' << build_prompt(judgment);
    return hex16(o.str());
}

bool Adjudicator::decide(const Judgment& judgment, uint64_t seed,
                         std::vector<std::string>& chosen,
                         std::string& reason, std::string& error) {
    chosen.clear();
    reason.clear();
    error.clear();
    if (!llm_) {
        error = "the adjudicator was never initialized";
        return false;
    }
    if (judgment.count < 1 ||
        static_cast<size_t>(judgment.count) > judgment.options.size()) {
        error = "a judgment must choose between 1 and " +
                std::to_string(judgment.options.size()) + " options";
        return false;
    }

    const std::string key = cache_key(judgment, seed);
    const std::string cache_file = cache_dir_ + "/" + key + ".txt";
    std::string reply = slurp(cache_file);

    // A cached reply is a reply that already PARSED. Nothing else is
    // written, so a bad answer cannot be served from disk forever.
    if (!reply.empty()) {
        return parse_judgment_answer(reply, judgment.options,
                                     judgment.count, chosen, reason, error);
    }

    // The referee is not optional, so this waits. A rule cannot move
    // until the call is made.
    //
    // It also RE-asks. A model that answers in prose instead of the
    // declared format is not a failed run, it is a model that needs
    // telling again, and one strike used to end a life outright. The
    // retry restates the format and says what was wrong with the last
    // reply, so the second attempt has something the first did not.
    std::string last_error;
    for (int attempt = 0; attempt < attempts_; ++attempt) {
        std::string prompt = build_prompt(judgment);
        if (attempt > 0) {
            prompt +=
                "\n\nYour last answer could not be read: " + last_error +
                "\nAnswer again. The FIRST line must be exactly "
                "'CHOICE: <option>' naming the options as given above, "
                "and the second 'WHY: <one short clause>'. Put nothing "
                "before the CHOICE line.\n";
        }

        bool done = false;
        std::string got;
        llm_->submit_request(
            prompt, reply_budget_,
            [&done, &got](const std::string&, const std::string& response,
                          void*) {
                got = response;
                done = true;
            });
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeout_ms_);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            llm_->process_completed_responses();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!done) {
            // A timeout is not a wrong answer, and re-asking a model
            // that has not finished thinking only stacks calls.
            error = "the adjudicator did not answer within " +
                    std::to_string(timeout_ms_) + "ms";
            return false;
        }
        if (got.empty() || got.rfind("[ERROR", 0) == 0) {
            last_error = got.empty() ? std::string("empty reply") : got;
            // A truncated reply IS worth re-asking: the model was mid
            // sentence, not wrong. Anything else from the transport is
            // not the model's fault and re-asking will not fix it.
            if (got.find("truncated") == std::string::npos) {
                error = "the adjudicator failed: " + last_error;
                return false;
            }
            continue;
        }

        if (parse_judgment_answer(got, judgment.options, judgment.count,
                                  chosen, reason, last_error)) {
            std::ofstream f(cache_file, std::ios::binary);
            f << got;
            return true;
        }
    }

    error = "the adjudicator did not answer in the required format after " +
            std::to_string(attempts_) + " attempts: " + last_error;
    return false;
}

}  // namespace logovger
