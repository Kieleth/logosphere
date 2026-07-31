#include "director/director_parser.h"

#include "logosphere/kg/kg_ops_parse.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace logotron::director {

// Strip surrounding ```json ... ``` (or plain ``` ... ```) markdown
// fences. Cloud LLMs frequently wrap JSON output in fences even
// when the prompt says "no markdown". KISS extractor: find first
// '{', last '}', take the slice. If the response is already plain
// JSON we still get the whole thing back unchanged.
static std::string extract_json_object(const std::string& s) {
    size_t first = s.find('{');
    size_t last  = s.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last <= first) {
        return s;  // let nlohmann reject below for a clean error
    }
    return s.substr(first, last - first + 1);
}

DirectorResponse parse_director_json(const std::string& json_str) {
    DirectorResponse out;

    const std::string cleaned = extract_json_object(json_str);

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(cleaned);
    } catch (const std::exception& e) {
        out.parse_error = std::string("JSON parse error: ") + e.what();
        return out;
    }

    if (!doc.is_object()) {
        out.parse_error = "top-level JSON must be an object";
        return out;
    }

    if (doc.contains("thoughts") && doc.at("thoughts").is_string()) {
        out.thoughts = doc.at("thoughts").get<std::string>();
    }

    // The KGOp parser is the source of truth for the ops array.
    // It accepts either {"ops":[...]} or a bare [...] payload, and
    // emits warnings for malformed entries that the caller can
    // surface to the player. Bad JSON above is the only fatal.
    auto ops_result = kg::parse_kg_ops(cleaned);
    out.kg_ops   = std::move(ops_result.ops);
    out.warnings = std::move(ops_result.warnings);

    if (out.kg_ops.empty() && out.warnings.empty()) {
        // Neither ops nor warnings — likely a v0.9 menu-shape
        // response. v0.10 retired that path; surface as a warning
        // so the player knows the LLM produced no usable actions.
        out.warnings.push_back(
            "no 'ops' array found (legacy 'mutations' shape no longer supported)");
    }

    return out;
}

}  // namespace logotron::director
