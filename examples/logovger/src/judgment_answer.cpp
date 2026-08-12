#include "judgment_answer.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace logovger {
namespace {

std::string trim(const std::string& text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last &&
           std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    while (last > first &&
           std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

std::string lowered(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) out += static_cast<char>(std::tolower(c));
    return out;
}

}  // namespace

bool parse_judgment_answer(const std::string& reply,
                           const std::vector<std::string>& options,
                           int count, std::vector<std::string>& chosen,
                           std::string& reason, std::string& error) {
    chosen.clear();
    reason.clear();
    error.clear();

    std::string choice_line;
    bool saw_choice = false;
    std::istringstream lines(reply);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string flat = trim(line);
        const std::string key = lowered(flat);
        if (!saw_choice && key.rfind("choice:", 0) == 0) {
            choice_line = trim(flat.substr(7));
            saw_choice = true;
        } else if (reason.empty() && key.rfind("why:", 0) == 0) {
            reason = trim(flat.substr(4));
        }
    }
    if (!saw_choice || choice_line.empty()) {
        error = "no CHOICE line in the answer: " + reply;
        return false;
    }

    size_t at = 0;
    while (at <= choice_line.size()) {
        const size_t cut = choice_line.find(',', at);
        const size_t end =
            cut == std::string::npos ? choice_line.size() : cut;
        const std::string piece = trim(choice_line.substr(at, end - at));
        if (!piece.empty()) {
            const auto found = std::find_if(
                options.begin(), options.end(),
                [&piece](const std::string& option) {
                    return lowered(option) == lowered(piece);
                });
            if (found == options.end()) {
                error = "chose '" + piece + "', which the rule does not allow";
                chosen.clear();
                return false;
            }
            if (std::find(chosen.begin(), chosen.end(), *found) !=
                chosen.end()) {
                error = "chose '" + *found + "' twice";
                chosen.clear();
                return false;
            }
            chosen.push_back(*found);
        }
        if (cut == std::string::npos) break;
        at = cut + 1;
    }
    if (chosen.size() != static_cast<size_t>(count)) {
        error = "chose " + std::to_string(chosen.size()) + " options, not " +
                std::to_string(count);
        chosen.clear();
        return false;
    }
    return true;
}

}  // namespace logovger
