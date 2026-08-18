#include "logosphere/kg/seed_loader.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_parse.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/text/source_manifest.h"
#include "logosphere/text/source_target.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <unordered_map>
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
                    (!range[0].is_null() &&
                     !range[0].is_number_integer()) ||
                    (!range[1].is_null() &&
                     !range[1].is_number_integer())) {
                    err = "invariants.band_coverage." + b.key() +
                          ": range must be [lo, hi], using an integer or "
                          "explicit null for each bound";
                    return false;
                }
                if (!range[0].is_null())
                    cov.lo = range[0].get<long long>();
                if (!range[1].is_null())
                    cov.hi = range[1].get<long long>();
                if (cov.lo && cov.hi && *cov.lo > *cov.hi) {
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

constexpr const char* kLayerAlias = "_logosphere_seed_layer_context";
constexpr const char* kDocumentAlias =
    "_logosphere_seed_document_context";

std::string layer_context_key(const SeedEnvelope& seed) {
    return "source-layer:" + seed.layer;
}

std::string document_context_key(const SeedEnvelope& seed) {
    return "source-document:" + seed.layer + ":" + seed.source.file + "@" +
           seed.source.commit;
}

bool find_context(const KGModule& kg, const std::string& key,
                  const std::string& expected_type, EntityID& out,
                  std::string& error) {
    const auto matches = kg.findByProperty("context_key", key);
    if (matches.empty()) {
        out = INVALID_ENTITY;
        return true;
    }
    if (matches.size() != 1) {
        error = "seed context '" + key + "': expected one entity, found " +
                std::to_string(matches.size());
        return false;
    }
    out = matches.front();
    const std::string actual_type = kg.getType(out);
    if (actual_type != expected_type) {
        error = "seed context '" + key + "': entity " +
                std::to_string(out) + " is " + actual_type + ", not " +
                expected_type;
        return false;
    }
    return true;
}

bool require_context_property(const KGModule& kg, EntityID id,
                              const std::string& property,
                              const std::string& expected,
                              std::string& error) {
    if (!kg.hasProperty(id, property)) {
        error = "seed context " + std::to_string(id) +
                ": missing required property '" + property + "'";
        return false;
    }
    const std::string actual = kg.getProperty(id, property);
    if (actual != expected) {
        error = "seed context " + std::to_string(id) + "." + property +
                ": expected '" + expected + "', got '" + actual + "'";
        return false;
    }
    return true;
}

bool seed_creates_addressable_content(const SeedEnvelope& seed,
                                      const KGModule& kg) {
    for (const KGOp& op : seed.ops) {
        const auto* create = std::get_if<KGOpCreateEntity>(&op);
        if (create &&
            kg.getRegistry().isSubtypeOf(create->type, "Addressable")) {
            return true;
        }
    }
    return false;
}

const std::string* create_property(const KGOpCreateEntity& create,
                                   const std::string& property) {
    for (const auto& [name, value] : create.properties)
        if (name == property) return &value;
    return nullptr;
}

bool create_has_nonempty_property(const KGOpCreateEntity& create,
                                  const std::string& property) {
    const auto* value = create_property(create, property);
    return value != nullptr && !value->empty();
}

bool seed_requires_legacy_document_context(const SeedEnvelope& seed,
                                           const KGModule& kg) {
    for (const KGOp& op : seed.ops) {
        const auto* create = std::get_if<KGOpCreateEntity>(&op);
        if (create && kg.getRegistry().isSubtypeOf(create->type, "Cited") &&
            create_has_nonempty_property(*create, "source_quote")) {
            return true;
        }
    }
    return false;
}

bool parse_nonnegative_selector_bound(const KGOpCreateEntity& create,
                                      const char* property,
                                      long long& out,
                                      std::string& error) {
    const auto* value = create_property(create, property);
    if (value == nullptr || value->empty() || value->front() == '-') {
        error = "ByteRangeSelector @" + create.as + ": " + property +
                " must be a non-negative integer";
        return false;
    }
    const auto parsed = std::from_chars(
        value->data(), value->data() + value->size(), out);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value->data() + value->size()) {
        error = "ByteRangeSelector @" + create.as + ": " + property +
                " must be a non-negative integer";
        return false;
    }
    return true;
}

bool derive_selector_keys(
    const SeedEnvelope& seed, const KGModule& kg,
    std::unordered_map<std::string, std::string>& selector_keys,
    int& failed_op, std::string& error) {
    for (std::size_t index = 0; index < seed.ops.size(); ++index) {
        const auto* create =
            std::get_if<KGOpCreateEntity>(&seed.ops[index]);
        if (!create || !kg.getRegistry().isSubtypeOf(
                           create->type, "SourceSelector")) {
            continue;
        }
        if (kg.getRegistry().isSubtypeOf(create->type,
                                         "TextQuoteSelector")) {
            continue;
        }
        if (!kg.getRegistry().isSubtypeOf(create->type,
                                          "ByteRangeSelector")) {
            failed_op = static_cast<int>(index);
            error = "ops[" + std::to_string(index) + "]: SourceSelector '" +
                    create->type + "' has no seed-loader identity rule";
            return false;
        }
        long long start = 0;
        long long end = 0;
        if (!parse_nonnegative_selector_bound(
                *create, "source_byte_start", start, error) ||
            !parse_nonnegative_selector_bound(
                *create, "source_byte_end", end, error)) {
            failed_op = static_cast<int>(index);
            error = "ops[" + std::to_string(index) + "]: " + error;
            return false;
        }
        if (start > end) {
            failed_op = static_cast<int>(index);
            error = "ops[" + std::to_string(index) +
                    "]: ByteRangeSelector @" + create->as +
                    " starts after it ends";
            return false;
        }
        selector_keys.emplace(
            create->as,
            logosphere::text::canonical_byte_range_key(start, end));
    }
    return true;
}

bool target_primary_key(
    const KGOpCreateEntity& create,
    const std::unordered_map<std::string, std::string>& selector_keys,
    std::string& key, std::string& error) {
    const auto* value = create_property(create, "target_primary_selector");
    if (value == nullptr || value->size() < 2 || value->front() != '@' ||
        (*value)[1] == '@') {
        error = "SourceTarget @" + create.as +
                " requires a same-seed ByteRangeSelector alias as its "
                "target_primary_selector";
        return false;
    }
    const auto found = selector_keys.find(value->substr(1));
    if (found == selector_keys.end()) {
        error = "SourceTarget @" + create.as +
                " target_primary_selector does not name a same-seed "
                "ByteRangeSelector";
        return false;
    }
    key = found->second;
    return true;
}

bool validate_ingestion_edition(const SeedEnvelope& seed,
                                EntityID edition,
                                const KGModule& kg,
                                EntityID& representation,
                                std::string& error) {
    if (edition == INVALID_ENTITY || !kg.exists(edition) ||
        kg.getType(edition) != "IngestionEditionContext") {
        error = "seed identity context is not an IngestionEditionContext";
        return false;
    }
    const auto resolved =
        logosphere::text::resolve_ingestion_edition(kg, edition);
    if (!resolved.ok) {
        error = "seed ingestion edition is invalid: " + resolved.reason;
        return false;
    }
    if (!require_context_property(kg, edition, "source_layer", seed.layer,
                                  error)) {
        return false;
    }

    representation = INVALID_ENTITY;
    for (const EntityID candidate : kg.getRelated(
             edition, "EDITION_INCLUDES_REPRESENTATION")) {
        if (kg.getProperty(candidate, "source_file") != seed.source.file)
            continue;
        if (representation != INVALID_ENTITY) {
            error = "seed ingestion edition contains duplicate source_file '" +
                    seed.source.file + "'";
            return false;
        }
        representation = candidate;
    }
    if (representation == INVALID_ENTITY) {
        error = "seed ingestion edition does not include source_file '" +
                seed.source.file + "'";
        return false;
    }

    size_t matching_revisions = 0;
    for (const EntityID observation :
         kg.findByType("SourceRevisionObservation")) {
        if (kg.getProperty(observation, "identity_context") ==
                std::to_string(representation) &&
            kg.getProperty(observation, "source_revision") ==
                seed.source.commit) {
            ++matching_revisions;
        }
    }
    if (matching_revisions != 1) {
        error = "seed ingestion edition source_file '" + seed.source.file +
                "' requires exactly one source revision observation for '" +
                seed.source.commit + "', found " +
                std::to_string(matching_revisions);
        return false;
    }
    return true;
}

bool seed_owns_type(const KGModule& kg, const std::string& type) {
    const auto& ontology = kg.getRegistry();
    return ontology.hasFacet(type, "seed-owned") ||
           ontology.isSubtypeOf(type, "SourceLayerContext") ||
           ontology.isSubtypeOf(type, "SourceDocumentContext");
}

bool validate_loader_owned_ops(const SeedEnvelope& seed, const KGModule& kg,
                               int& failed_op, std::string& error) {
    for (size_t index = 0; index < seed.ops.size(); ++index) {
        const auto* create = std::get_if<KGOpCreateEntity>(&seed.ops[index]);
        if (!create) continue;
        if (seed_owns_type(kg, create->type)) {
            failed_op = static_cast<int>(index);
            error = "ops[" + std::to_string(index) + "]: type '" +
                    create->type + "' is seed-loader-owned";
            return false;
        }
        if (create->as == kLayerAlias || create->as == kDocumentAlias) {
            failed_op = static_cast<int>(index);
            error = "ops[" + std::to_string(index) +
                    "]: create_entity alias @" + create->as +
                    " is reserved by the seed loader";
            return false;
        }
        if (kg.getRegistry().isSubtypeOf(create->type, "Addressable")) {
            if (create->as.empty()) {
                failed_op = static_cast<int>(index);
                error = "ops[" + std::to_string(index) + "]: Addressable " +
                        "type '" + create->type + "' requires a non-empty " +
                        "create_entity alias for its portable entity key";
                return false;
            }
            for (const auto& [name, value] : create->properties) {
                (void)value;
                if (name == "identity_context" || name == "entity_key") {
                    failed_op = static_cast<int>(index);
                    error = "ops[" + std::to_string(index) + "]: " + name +
                            " is seed-loader-owned";
                    return false;
                }
                if (kg.getRegistry().isSubtypeOf(create->type,
                                                 "SourceTarget") &&
                    name == "target_representation") {
                    failed_op = static_cast<int>(index);
                    error = "ops[" + std::to_string(index) +
                            "]: target_representation is seed-loader-owned";
                    return false;
                }
            }
        }
    }
    return true;
}

void translate_batch_failure(const KGOpBatchReport& batch,
                             size_t prefix_count, SeedLoadReport& report) {
    report.ok = false;
    report.error = batch.error;
    if (batch.failed_op < 0 ||
        static_cast<size_t>(batch.failed_op) < prefix_count) {
        report.failed_op = -1;
        report.error = "seed context: " + report.error;
        return;
    }
    report.failed_op = batch.failed_op - static_cast<int>(prefix_count);
    const std::string augmented = "ops[" +
        std::to_string(batch.failed_op) + "]";
    const std::string original = "ops[" +
        std::to_string(report.failed_op) + "]";
    if (const size_t at = report.error.find(augmented);
        at != std::string::npos) {
        report.error.replace(at, augmented.size(), original);
    }
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
            k != "generated_by" && k != "ops") {
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

// A seed declaring unique_name_per_type is promising that a name of
// that type identifies one thing. Seeds are loaded into a SHARED
// world, so that promise can only be checked here, against everything
// already loaded. Checking inside one file let two seeds each create
// a "Gun Combat" and both pass.
//
// This runs BEFORE anything is applied, so a collision costs nothing
// to refuse and there is no half-loaded world to unwind.
bool refuse_names_already_loaded(const SeedEnvelope& seed, const KGModule& kg,
                                 int& failed_op, std::string& error) {
    if (seed.invariants.unique_name_per_type.empty()) return true;
    for (size_t i = 0; i < seed.ops.size(); ++i) {
        const auto* create = std::get_if<KGOpCreateEntity>(&seed.ops[i]);
        if (!create) continue;
        const auto& types = seed.invariants.unique_name_per_type;
        if (std::find(types.begin(), types.end(), create->type) ==
            types.end()) {
            continue;
        }
        std::string name;
        for (const auto& [k, v] : create->properties)
            if (k == "name") name = v;
        if (name.empty()) continue;
        for (EntityID id : kg.findByType(create->type)) {
            if (kg.getProperty(id, "name") != name) continue;
            failed_op = static_cast<int>(i);
            error = "unique_name_per_type " + create->type + ": '" + name +
                    "' is already loaded from another seed. Reference it "
                    "with its canonical @@entity/<context-key>/<exact-type>/"
                    "<entity-key> path instead of creating it again.";
            return false;
        }
    }
    return true;
}

static bool load_seed_with_identity(const SeedEnvelope& seed,
                                    EntityID ingestion_edition_context,
                                    KGModule& kg,
                                    SeedLoadReport& report) {
    report = SeedLoadReport{};
    EntityID source_representation = INVALID_ENTITY;
    if (ingestion_edition_context != INVALID_ENTITY &&
        !validate_ingestion_edition(seed, ingestion_edition_context, kg,
                                    source_representation, report.error)) {
        report.ok = false;
        return false;
    }
    int invalid_op = -1;
    if (!refuse_names_already_loaded(seed, kg, invalid_op, report.error)) {
        report.ok = false;
        report.failed_op = invalid_op;
        return false;
    }
    if (!validate_loader_owned_ops(seed, kg, invalid_op, report.error)) {
        report.ok = false;
        report.failed_op = invalid_op;
        return false;
    }
    std::unordered_map<std::string, std::string> selector_keys;
    if (!derive_selector_keys(seed, kg, selector_keys, invalid_op,
                              report.error)) {
        report.ok = false;
        report.failed_op = invalid_op;
        return false;
    }
    if (!selector_keys.empty() && source_representation == INVALID_ENTITY) {
        report.ok = false;
        report.error =
            "source selectors require load_seed_in_edition with an exact "
            "source representation";
        return false;
    }
    if (!seed_creates_addressable_content(seed, kg)) {
        KGOpBatchReport batch;
        const bool ok = apply_kg_ops_atomically(
            seed.ops, kg, batch, MutationAuthority::SeedIngestion);
        report.ok = batch.ok;
        report.failed_op = batch.failed_op;
        report.error = std::move(batch.error);
        report.ops_applied = batch.ops_applied;
        report.bindings = std::move(batch.bindings);
        report.created_ids = std::move(batch.created_ids);
        return ok;
    }

    EntityID layer_id = INVALID_ENTITY;
    EntityID document_id = INVALID_ENTITY;
    const std::string layer_key = layer_context_key(seed);
    const std::string document_key = document_context_key(seed);
    const bool needs_document =
        ingestion_edition_context == INVALID_ENTITY ||
        seed_requires_legacy_document_context(seed, kg);
    if (!find_context(kg, layer_key, "SourceLayerContext", layer_id,
                      report.error) ||
        (needs_document &&
         !find_context(kg, document_key, "SourceDocumentContext", document_id,
                       report.error))) {
        report.ok = false;
        return false;
    }
    if (layer_id != INVALID_ENTITY &&
        !require_context_property(kg, layer_id, "source_layer", seed.layer,
                                  report.error)) {
        report.ok = false;
        return false;
    }
    if (document_id != INVALID_ENTITY) {
        if (layer_id == INVALID_ENTITY) {
            report.ok = false;
            report.error = "seed context '" + document_key +
                           "': source layer context is missing";
            return false;
        }
        if (!require_context_property(kg, document_id, "source_layer",
                                      seed.layer, report.error) ||
            !require_context_property(kg, document_id, "source_file",
                                      seed.source.file, report.error) ||
            !require_context_property(kg, document_id, "source_commit",
                                      seed.source.commit, report.error) ||
            !require_context_property(
                kg, document_id, "source_layer_context",
                std::to_string(layer_id), report.error)) {
            report.ok = false;
            return false;
        }
    }

    std::vector<KGOp> augmented;
    augmented.reserve(seed.ops.size() + 2);
    if (layer_id == INVALID_ENTITY) {
        augmented.emplace_back(KGOpCreateEntity{
            "SourceLayerContext",
            {{"context_key", layer_key}, {"source_layer", seed.layer}},
            kLayerAlias});
    }
    const std::string layer_ref = layer_id == INVALID_ENTITY
        ? std::string("@") + kLayerAlias
        : std::to_string(layer_id);
    if (needs_document && document_id == INVALID_ENTITY) {
        augmented.emplace_back(KGOpCreateEntity{
            "SourceDocumentContext",
            {{"context_key", document_key},
             {"source_layer", seed.layer},
             {"source_file", seed.source.file},
             {"source_commit", seed.source.commit},
             {"source_layer_context", layer_ref}},
            kDocumentAlias});
    }
    const size_t prefix_count = augmented.size();
    const std::string document_ref = needs_document
        ? (document_id == INVALID_ENTITY
               ? std::string("@") + kDocumentAlias
               : std::to_string(document_id))
        : std::string{};
    const std::string identity_ref =
        ingestion_edition_context == INVALID_ENTITY
            ? document_ref
            : std::to_string(ingestion_edition_context);

    for (size_t index = 0; index < seed.ops.size(); ++index) {
        KGOp copied = seed.ops[index];
        if (auto* create = std::get_if<KGOpCreateEntity>(&copied);
            create &&
            kg.getRegistry().isSubtypeOf(create->type, "Addressable")) {
            const bool is_selector = kg.getRegistry().isSubtypeOf(
                create->type, "SourceSelector");
            const bool is_target = kg.getRegistry().isSubtypeOf(
                create->type, "SourceTarget");
            const std::string scoped_identity = is_selector || is_target
                ? std::to_string(source_representation)
                : identity_ref;
            std::string entity_key = create->as;
            if (kg.getRegistry().isSubtypeOf(create->type,
                                             "ByteRangeSelector")) {
                entity_key = selector_keys.at(create->as);
            } else if (is_target) {
                if (!target_primary_key(*create, selector_keys, entity_key,
                                        report.error)) {
                    report.ok = false;
                    report.failed_op = static_cast<int>(index);
                    report.error = "ops[" + std::to_string(index) +
                                   "]: " + report.error;
                    return false;
                }
                create->properties.emplace_back(
                    "target_representation",
                    std::to_string(source_representation));
            }
            create->properties.emplace_back("identity_context",
                                            scoped_identity);
            create->properties.emplace_back("entity_key", entity_key);
        }
        if (auto* create = std::get_if<KGOpCreateEntity>(&copied);
            create && kg.getRegistry().isSubtypeOf(create->type, "Cited")) {
            const auto existing = std::find_if(
                create->properties.begin(), create->properties.end(),
                [](const auto& property) {
                    return property.first == "origin_context";
                });
            if (existing != create->properties.end()) {
                report.ok = false;
                report.failed_op = static_cast<int>(index);
                report.error = "ops[" + std::to_string(index) +
                               "]: origin_context is seed-loader-owned";
                return false;
            }
            const bool legacy =
                create_has_nonempty_property(*create, "source_quote");
            create->properties.emplace_back(
                "origin_context", legacy ? document_ref : identity_ref);
        }
        augmented.push_back(std::move(copied));
    }

    KGOpBatchReport batch;
    if (!apply_kg_ops_atomically(augmented, kg, batch,
                                 MutationAuthority::SeedIngestion)) {
        translate_batch_failure(batch, prefix_count, report);
        return false;
    }

    report.ok = true;
    report.ops_applied = seed.ops.size();
    report.bindings = std::move(batch.bindings);
    if (layer_id == INVALID_ENTITY) {
        layer_id = report.bindings.at(kLayerAlias);
    }
    if (needs_document && document_id == INVALID_ENTITY) {
        document_id = report.bindings.at(kDocumentAlias);
    }
    report.bindings.erase(kLayerAlias);
    report.bindings.erase(kDocumentAlias);
    report.identity_context =
        ingestion_edition_context == INVALID_ENTITY
            ? document_id
            : ingestion_edition_context;
    report.source_layer_context = layer_id;
    report.source_document_context = document_id;
    report.created_ids.assign(batch.created_ids.begin() + prefix_count,
                              batch.created_ids.end());
    return true;
}

bool load_seed(const SeedEnvelope& seed, KGModule& kg,
               SeedLoadReport& report) {
    return load_seed_with_identity(seed, INVALID_ENTITY, kg, report);
}

bool load_seed_in_edition(const SeedEnvelope& seed,
                          EntityID ingestion_edition_context,
                          KGModule& kg,
                          SeedLoadReport& report) {
    return load_seed_with_identity(seed, ingestion_edition_context, kg,
                                   report);
}

}  // namespace kg
