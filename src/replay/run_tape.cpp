#include "logosphere/replay/run_tape.h"

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
        // Free-form. A generic engine has no business inventing prose,
        // and an empty answer is honest about that.
        out.clear();
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
        entry.site = value_of(line, "site");
        entry.answer = value_of(line, "answer");
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
    std::ostringstream line;
    line << "{\"kind\":\"ask\",\"site\":" << quote(ask.site)
         << ",\"answer\":" << quote(answer)
         << ",\"prompt\":" << quote(ask.prompt) << "}";
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
    for (const auto& line : lines_) out << line << "\n";
    flushed_ = true;
}

}  // namespace logosphere::replay
