#include "logosphere/replay/run_tape.h"

#include "logosphere/text/source_target.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace logosphere::replay {
namespace {

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out + "\"";
}

// Minimal reader for the lines this file writes. Deliberately not a
// general JSON parser: the tape is our own format, and a parser that
// accepts more than we emit would hide a malformed tape.
// The one array the format carries: "offered":["1","2","3"]. Same
// hand-rolled shape as value_of, because a tape must stay readable by
// eye and the engine core takes no JSON dependency.
std::vector<std::string> list_of(const std::string& line,
                                 const std::string& key, bool& present) {
    std::vector<std::string> out;
    const std::string needle = "\"" + key + "\":[";
    const auto at = line.find(needle);
    present = at != std::string::npos;
    if (!present) return out;
    std::string item;
    bool inside = false;
    for (size_t i = at + needle.size(); i < line.size(); ++i) {
        const char c = line[i];
        if (!inside && c == ']') break;
        if (c == '"') {
            if (inside) { out.push_back(item); item.clear(); }
            inside = !inside;
            continue;
        }
        if (inside && c == '\\' && i + 1 < line.size()) {
            const char next = line[++i];
            item += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            continue;
        }
        if (inside) item += c;
    }
    return out;
}

std::string value_of(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\":\"";
    const auto at = line.find(needle);
    if (at == std::string::npos) return {};
    std::string out;
    for (size_t i = at + needle.size(); i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            const char next = line[++i];
            out += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            continue;
        }
        if (line[i] == '"') break;
        out += line[i];
    }
    return out;
}

bool allowed(const Ask& ask, const std::string& answer) {
    if (ask.offered.empty()) return true;   // free-form
    return std::find(ask.offered.begin(), ask.offered.end(), answer) !=
           ask.offered.end();
}

}  // namespace

// ---------------------------------------------------------------- live

bool LiveInput::answer(const Ask& ask, std::string& out, std::string& error) {
    if (!asker_) {
        error = "no live source is attached to answer '" + ask.site + "'";
        return false;
    }
    return asker_(ask, out, error);
}

uint64_t LiveInput::seed(const std::string&, uint64_t fallback) {
    return fallback;
}

// -------------------------------------------------------------- random

uint64_t RandomInput::next() {
    // xorshift64*, the same shape the engine's dice use. Small, fast
    // and identical on every platform, which is what a fuzz run needs
    // to stay reproducible.
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 2685821657736338717ull;
}

bool RandomInput::answer(const Ask& ask, std::string& out, std::string&) {
    if (ask.offered.empty()) {
        // Free-form. The engine cannot know what a plausible answer
        // looks like in someone's game, so it asks the game, and
        // answers empty when the game did not say.
        out = free_form_ ? free_form_(ask, next()) : std::string();
        return true;
    }
    out = ask.offered[next() % ask.offered.size()];
    return true;
}

uint64_t RandomInput::seed(const std::string&, uint64_t) { return next(); }

// --------------------------------------------------------------- taped

std::unique_ptr<TapedInput> TapedInput::open(const std::string& path,
                                             std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "no tape at " + path;
        return nullptr;
    }
    auto tape = std::unique_ptr<TapedInput>(new TapedInput());
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Entry entry;
        entry.kind = value_of(line, "kind");
        if (entry.kind == "rules") {
            tape->edition_ = value_of(line, "edition");
            continue;   // metadata, not a decision the run consumes
        }
        entry.site = value_of(line, "site");
        entry.answer = value_of(line, "answer");
        entry.prompt = value_of(line, "prompt");
        bool had_offered = false;
        entry.offered = list_of(line, "offered", had_offered);
        // One line carrying the field is enough to know the tape was
        // written by a build that records them. A free-form ask has an
        // empty list and would otherwise look like an old tape.
        if (had_offered) tape->records_offered_ = true;
        if (entry.kind.empty()) {
            error = "malformed tape line: " + line;
            return nullptr;
        }
        tape->entries_.push_back(std::move(entry));
    }
    return tape;
}

bool TapedInput::answer(const Ask& ask, std::string& out,
                        std::string& error) {
    if (at_ >= entries_.size()) {
        error = "the tape ran out at '" + ask.site + "': it holds " +
                std::to_string(entries_.size()) +
                " answers and the run wanted more";
        return false;
    }
    const Entry& entry = entries_[at_];
    if (entry.kind != "ask" || entry.site != ask.site) {
        error = "the tape does not fit this run: entry " +
                std::to_string(at_) + " is " + entry.kind + " '" +
                entry.site + "', the run asked '" + ask.site + "'";
        return false;
    }
    if (!allowed(ask, entry.answer)) {
        // The rules moved under the tape. Silently asking live here
        // would turn a broken replay into a passing one.
        error = "the tape answers '" + entry.answer + "' at '" + ask.site +
                "', which this run does not offer";
        return false;
    }
    out = entry.answer;
    ++at_;
    return true;
}

uint64_t TapedInput::seed(const std::string& stream, uint64_t fallback) {
    for (size_t i = at_; i < entries_.size(); ++i) {
        if (entries_[i].kind == "seed" && entries_[i].site == stream) {
            if (i == at_) ++at_;
            try {
                return std::stoull(entries_[i].answer);
            } catch (...) {
                return fallback;
            }
        }
    }
    return fallback;
}

std::vector<TapedInput::Fork> TapedInput::forks() const {
    std::vector<Fork> out;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        if (entry.kind != "ask") continue;
        Fork fork;
        fork.index = i;
        fork.site = entry.site;
        fork.prompt = entry.prompt;
        fork.taken = entry.answer;
        for (const auto& option : entry.offered) {
            if (option != entry.answer) fork.untaken.push_back(option);
        }
        if (!fork.untaken.empty()) out.push_back(std::move(fork));
    }
    return out;
}

// ------------------------------------------------------------- fork

std::unique_ptr<ForkedInput> ForkedInput::create(
    std::unique_ptr<TapedInput> trunk, size_t at, const std::string& instead,
    InputSource& then, std::string& error) {
    if (!trunk) {
        error = "a fork needs a trunk";
        return nullptr;
    }
    if (at >= trunk->entries_.size()) {
        error = "cannot fork at " + std::to_string(at) + ": the tape holds " +
                std::to_string(trunk->entries_.size()) + " entries";
        return nullptr;
    }
    const auto& entry = trunk->entries_[at];
    if (entry.kind != "ask") {
        error = "cannot fork at " + std::to_string(at) + ": that entry is a " +
                entry.kind + ", not a decision";
        return nullptr;
    }
    if (instead == entry.answer) {
        error = "forking at " + std::to_string(at) + " onto '" + instead +
                "' is the answer the run already gave; a fork must differ";
        return nullptr;
    }
    // A fork onto an answer the rules never offered is not a
    // counterfactual, it is a fiction. Old tapes recorded no
    // alternatives, so they cannot be forked safely and say so rather
    // than allowing anything.
    if (entry.offered.empty()) {
        error = "cannot fork at " + std::to_string(at) +
                ": this tape records no alternatives, so there is no way to "
                "tell a legal branch from an invented one. Re-record it.";
        return nullptr;
    }
    if (std::find(entry.offered.begin(), entry.offered.end(), instead) ==
        entry.offered.end()) {
        error = "'" + instead + "' was not offered at entry " +
                std::to_string(at) + "; that decision offered " +
                std::to_string(entry.offered.size()) + " answers";
        return nullptr;
    }
    return std::unique_ptr<ForkedInput>(
        new ForkedInput(std::move(trunk), at, instead, then));
}

bool ForkedInput::answer(const Ask& ask, std::string& out,
                         std::string& error) {
    if (past_) return then_.answer(ask, out, error);

    // Count ASKS, not entries: the trunk's index space includes seed
    // lines, and the run only asks questions.
    size_t ask_index = 0;
    for (size_t i = 0; i < trunk_->entries_.size(); ++i) {
        if (trunk_->entries_[i].kind != "ask") continue;
        if (i == at_) break;
        ++ask_index;
    }

    if (seen_ == ask_index) {
        // The fork itself. Validated against what THIS run offers, not
        // only against what the tape recorded, so a branch cannot be
        // taken onto an answer the current rules stopped allowing.
        if (!ask.offered.empty() &&
            std::find(ask.offered.begin(), ask.offered.end(), instead_) ==
                ask.offered.end()) {
            error = "the fork answers '" + instead_ + "' at '" + ask.site +
                    "', which this run does not offer";
            return false;
        }
        out = instead_;
        ++seen_;
        past_ = true;
        return true;
    }

    if (!trunk_->answer(ask, out, error)) return false;
    ++seen_;
    return true;
}

uint64_t ForkedInput::seed(const std::string& stream, uint64_t fallback) {
    // The seed comes off the trunk even after divergence. A branch that
    // rolled its own dice would differ from its trunk for two reasons
    // at once, and the point of a counterfactual is to change exactly
    // one thing.
    return trunk_->seed(stream, fallback);
}

// ----------------------------------------------------------------- tape

RunTape::RunTape(InputSource& source, std::string path)
    : source_(source), path_(std::move(path)) {}

RunTape::~RunTape() { flush(); }

bool RunTape::ask(const Ask& ask, std::string& answer, std::string& error) {
    if (!source_.answer(ask, answer, error)) return false;
    if (!allowed(ask, answer)) {
        error = "'" + answer + "' is not one of the answers '" + ask.site +
                "' offered";
        return false;
    }
    // The name of this decision, before the line that records it. An
    // empty parent is the root, so the first decision's name is the
    // hash of "\n" + its answer, and every later name carries all of
    // its ancestors through its parent.
    node_ = logosphere::text::sha256_hex(node_ + "\n" + answer);

    std::ostringstream line;
    line << "{\"kind\":\"ask\",\"site\":" << quote(ask.site)
         << ",\"answer\":" << quote(answer)
         << ",\"prompt\":" << quote(ask.prompt) << ",\"offered\":[";
    // What ELSE was legal here. Recorded so a tape carries its own
    // forks: without this a replay can only repeat a life, never ask
    // what the other branch would have done.
    for (size_t i = 0; i < ask.offered.size(); ++i) {
        if (i) line << ',';
        line << quote(ask.offered[i]);
    }
    line << "],\"node\":" << quote(node_) << "}";
    lines_.push_back(line.str());
    return true;
}

uint64_t RunTape::seed(const std::string& stream, uint64_t fallback) {
    const uint64_t value = source_.seed(stream, fallback);
    std::ostringstream line;
    line << "{\"kind\":\"seed\",\"site\":" << quote(stream)
         << ",\"answer\":" << quote(std::to_string(value)) << "}";
    lines_.push_back(line.str());
    return value;
}

void RunTape::flush() {
    if (flushed_ || path_.empty()) return;
    std::ofstream out(path_, std::ios::trunc);
    // The edition goes FIRST, so a reader learns which rulebook this
    // was played against before it reads a single answer.
    if (!edition_.empty()) {
        out << "{\"kind\":\"rules\",\"edition\":" << quote(edition_)
            << "}\n";
    }
    for (const auto& line : lines_) out << line << "\n";
    flushed_ = true;
}

bool TapedInput::fits_edition(const std::string& current,
                              std::string& why) const {
    if (edition_.empty()) {
        why = "this tape does not say which rulebook it was played "
              "against, so whether it still fits cannot be judged";
        return true;
    }
    if (current.empty()) {
        why = "this run does not name its rulebook, so the tape's "
              "edition cannot be checked against it";
        return true;
    }
    if (edition_ == current) return true;
    why = "the tape was played against " + edition_ + " and this run is " +
          current + "; the same answers against different rules do not "
          "make the same life";
    return false;
}

}  // namespace logosphere::replay
