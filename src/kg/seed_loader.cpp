#include "logosphere/kg/seed_loader.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_parse.h"
#include "logosphere/kg/kg_ops_transaction.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace kg {

namespace {

using nlohmann::json;

// --- Envelope parsing ----------------------------------------------

// Every helper writes a full "where: why" message into err and
// returns false; parse_seed_envelope surfaces the first one as the
// fatal error. No field is defaulted - a seed file that omits its
// source pin or its layer is malformed, not minimal.

bool require_string(const json& obj, const char* key, const char* where,
                    std::string& out, std::string& err) {
    if (!obj.contains(key) || !obj.at(key).is_string() ||
        obj.at(key).get<std::string>().empty()) {
        err = std::string(where) + ": missing or empty string field '" +
              key + "'";
        return false;
    }
    out = obj.at(key).get<std::string>();
    return true;
}

// "@alias" -> "alias", refusing anything that is not an @-prefixed
// non-empty name (same strictness as the create op's "as" binder).
bool strip_alias(const std::string& s, const char* where,
                 std::string& out, std::string& err) {
    if (s.size() < 2 || s[0] != '@') {
        err = std::string(where) + ": expected '@alias', got '" + s + "'";
        return false;
    }
    out = s.substr(1);
    return true;
}

bool parse_invariants(const json& inv, SeedInvariants& out,
                      std::string& err) {
    if (!inv.is_object()) {
        err = "invariants: must be an object";
        return false;
    }
    for (auto it = inv.begin(); it != inv.end(); ++it) {
        const std::string& kind = it.key();
        const json& v = it.value();
        if (kind == "count_of_type") {
            if (!v.is_object()) {
                err = "invariants.count_of_type: must be an object of "
                      "type -> count";
                return false;
            }
            for (auto c = v.begin(); c != v.end(); ++c) {
                if (!c.value().is_number_integer() ||
                    c.value().get<long long>() < 0) {
                    err = "invariants.count_of_type." + c.key() +
                          ": count must be a non-negative integer";
                    return false;
                }
                out.count_of_type.emplace_back(c.key(),
                                               c.value().get<long long>());
            }
        } else if (kind == "unique_name_per_type") {
            if (!v.is_array()) {
                err = "invariants.unique_name_per_type: must be an array "
                      "of type names";
                return false;
            }
            for (const auto& t : v) {
                if (!t.is_string() || t.get<std::string>().empty()) {
                    err = "invariants.unique_name_per_type: entries must "
                          "be non-empty type names";
                    return false;
                }
                out.unique_name_per_type.push_back(t.get<std::string>());
            }
        } else if (kind == "band_coverage") {
            if (!v.is_object()) {
                err = "invariants.band_coverage: must be an object of "
                      "@alias -> [lo, hi]";
                return false;
            }
            for (auto b = v.begin(); b != v.end(); ++b) {
                SeedInvariants::BandCoverage cov;
                if (!strip_alias(b.key(), "invariants.band_coverage key",
                                 cov.alias, err)) {
                    return false;
                }
                const json& range = b.value();
                if (!range.is_array() || range.size() != 2 ||
                    !range[0].is_number_integer() ||
                    !range[1].is_number_integer()) {
                    err = "invariants.band_coverage." + b.key() +
                          ": range must be [lo, hi] integers";
                    return false;
                }
                cov.lo = range[0].get<long long>();
                cov.hi = range[1].get<long long>();
                if (cov.lo > cov.hi) {
                    err = "invariants.band_coverage." + b.key() +
                          ": lo > hi";
                    return false;
                }
                out.band_coverage.push_back(std::move(cov));
            }
        } else {
            err = "invariants: unknown assertion kind '" + kind +
                  "' (count_of_type, unique_name_per_type, "
                  "band_coverage)";
            return false;
        }
    }
    return true;
}

}  // namespace

SeedParseResult parse_seed_envelope(const std::string& json_text) {
    SeedParseResult result;

    json root;
    try {
        root = json::parse(json_text);
    } catch (const std::exception& e) {
        result.error = std::string("invalid JSON: ") + e.what();
        return result;
    }
    if (!root.is_object()) {
        result.error = "seed file: root must be an object";
        return result;
    }

    // Unknown top-level keys are refused: a typo'd "invariant" key
    // silently asserting nothing is exactly the failure mode a seed
    // format exists to prevent.
    for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string& k = it.key();
        if (k != "source" && k != "layer" && k != "invariants" &&
            k != "ops") {
            result.error = "seed file: unknown top-level field '" + k + "'";
            return result;
        }
    }

    if (!root.contains("source") || !root.at("source").is_object()) {
        result.error = "seed file: missing 'source' object";
        return result;
    }
    if (!require_string(root.at("source"), "file", "source",
                        result.seed.source.file, result.error) ||
        !require_string(root.at("source"), "commit", "source",
                        result.seed.source.commit, result.error) ||
        !require_string(root, "layer", "seed file", result.seed.layer,
                        result.error)) {
        return result;
    }

    if (root.contains("invariants") &&
        !parse_invariants(root.at("invariants"), result.seed.invariants,
                          result.error)) {
        return result;
    }

    if (!root.contains("ops") || !root.at("ops").is_array()) {
        result.error = "seed file: missing 'ops' array";
        return result;
    }
    // Reuse the wire-grammar parser, but promote its per-op warnings
    // to fatal: a dropped op would skew every invariant below.
    KGOpParseResult ops = parse_kg_ops(json_text);
    if (!ops.parse_error.empty()) {
        result.error = ops.parse_error;
        return result;
    }
    if (!ops.warnings.empty()) {
        result.error = "seed file: " + ops.warnings.front();
        return result;
    }
    result.seed.ops = std::move(ops.ops);
    return result;
}

bool load_seed(const SeedEnvelope& seed, KGModule& kg,
               SeedLoadReport& report) {
    KGOpBatchReport batch;
    const bool ok = apply_kg_ops_atomically(seed.ops, kg, batch);
    report = SeedLoadReport{};
    report.ok = batch.ok;
    report.failed_op = batch.failed_op;
    report.error = std::move(batch.error);
    report.ops_applied = batch.ops_applied;
    report.bindings = std::move(batch.bindings);
    report.created_ids = std::move(batch.created_ids);
    return ok;
}

}  // namespace kg
