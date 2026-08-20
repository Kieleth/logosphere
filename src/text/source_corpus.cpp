#include "logosphere/text/source_corpus.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/text/source_manifest.h"
#include "logosphere/text/source_target.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace logosphere::text {
namespace {

SourceCorpusMaterialization fail(std::string reason) {
    SourceCorpusMaterialization result;
    result.reason = std::move(reason);
    return result;
}

bool supported_media(rule_language::ontology::SourceMediaType media_type) {
    switch (media_type) {
        case rule_language::ontology::SourceMediaType::UTF8_TEXT:
            return true;
    }
    return false;
}

constexpr const char* kEditionMembership =
    "EDITION_INCLUDES_REPRESENTATION";

SourceCorpusKGMaterialization fail_kg(std::string reason) {
    SourceCorpusKGMaterialization result;
    result.reason = std::move(reason);
    return result;
}

void append_key_field(std::string& output, std::string_view value) {
    output += std::to_string(value.size());
    output.push_back(':');
    output.append(value.data(), value.size());
}

std::string representation_context_key(
    std::string_view source_layer,
    const MaterializedSourceRepresentation& representation) {
    std::string canonical;
    append_key_field(canonical, "SOURCE_REPRESENTATION_V1");
    append_key_field(canonical, source_layer);
    append_key_field(canonical, representation.source_file);
    append_key_field(canonical,
                     rule_language::ontology::to_string(
                         representation.source_media_type));
    append_key_field(canonical,
                     rule_language::ontology::to_string(
                         representation.source_digest_algorithm));
    append_key_field(canonical, representation.source_digest);
    append_key_field(canonical,
                     std::to_string(representation.source_byte_length));
    return "source-representation:v1:sha256:" + sha256_hex(canonical);
}

std::string revision_observation_key(std::string_view source_revision) {
    std::string canonical;
    append_key_field(canonical, "SOURCE_REVISION_OBSERVATION_V1");
    append_key_field(canonical, source_revision);
    return "source-revision-observation:v1:sha256:" +
           sha256_hex(canonical);
}

bool find_context(const kg::KGModule& world, const std::string& key,
                  const std::string& expected_type, kg::EntityID& found,
                  std::string& reason) {
    const auto matches = world.findByProperty("context_key", key);
    if (matches.empty()) {
        found = kg::INVALID_ENTITY;
        return true;
    }
    if (matches.size() != 1) {
        reason = "source context '" + key + "': expected one entity, found " +
                 std::to_string(matches.size());
        return false;
    }
    found = matches.front();
    const auto actual_type = world.getType(found);
    if (actual_type != expected_type) {
        reason = "source context '" + key + "' is " + actual_type +
                 ", not " + expected_type;
        return false;
    }
    return true;
}

bool require_entity_properties(
    const kg::KGModule& world, kg::EntityID context,
    const std::vector<std::pair<std::string, std::string>>& expected,
    std::string& reason) {
    for (const auto& [property, value] : expected) {
        if (!world.hasProperty(context, property)) {
            reason = "source entity " + std::to_string(context) +
                     " is missing required property '" + property + "'";
            return false;
        }
        const std::string actual = world.getProperty(context, property);
        if (actual != value) {
            reason = "source entity " + std::to_string(context) + "." +
                     property + " expected '" + value + "', got '" +
                     actual + "'";
            return false;
        }
    }
    return true;
}

bool parse_entity_id(const std::string& value, kg::EntityID& entity) {
    unsigned long long parsed = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsed == 0 || parsed > std::numeric_limits<kg::EntityID>::max())
        return false;
    entity = static_cast<kg::EntityID>(parsed);
    return true;
}

bool validate_revision_observation_conflicts(
    const kg::KGModule& world, std::string_view source_layer,
    const MaterializedSourceRepresentation& desired,
    std::string_view desired_context_key, std::string& reason) {
    for (const auto observation :
         world.findByProperty("source_revision", desired.source_revision)) {
        if (world.getType(observation) != "SourceRevisionObservation")
            continue;
        kg::EntityID representation = kg::INVALID_ENTITY;
        if (!world.hasProperty(observation, "identity_context") ||
            !parse_entity_id(world.getProperty(observation,
                                               "identity_context"),
                             representation) ||
            !world.exists(representation) ||
            world.getType(representation) !=
                "SourceRepresentationContext") {
            reason = "source revision observation " +
                     std::to_string(observation) +
                     " has invalid representation identity";
            return false;
        }
        if (!world.hasProperty(representation, "source_layer") ||
            !world.hasProperty(representation, "source_file") ||
            !world.hasProperty(representation, "context_key")) {
            reason = "source revision observation " +
                     std::to_string(observation) +
                     " points to incomplete representation context";
            return false;
        }
        if (world.getProperty(representation, "source_layer") ==
                source_layer &&
            world.getProperty(representation, "source_file") ==
                desired.source_file &&
            world.getProperty(representation, "context_key") !=
                desired_context_key) {
            reason = "source revision '" + desired.source_revision +
                     "' already records different bytes for '" +
                     desired.source_file + "'";
            return false;
        }
    }
    return true;
}

bool find_revision_observation(
    const kg::KGModule& world, kg::EntityID representation,
    const std::string& entity_key, kg::EntityID& found,
    std::string& reason) {
    found = kg::INVALID_ENTITY;
    for (const auto candidate :
         world.findByType("SourceRevisionObservation")) {
        if (world.getProperty(candidate, "identity_context") !=
                std::to_string(representation) ||
            world.getProperty(candidate, "entity_key") != entity_key)
            continue;
        if (found != kg::INVALID_ENTITY) {
            reason = "duplicate SourceRevisionObservation identity for " +
                     std::to_string(representation) + " and '" + entity_key +
                     "'";
            return false;
        }
        found = candidate;
    }
    return true;
}

std::string property_reference(kg::EntityID existing,
                               const std::string& alias) {
    return existing == kg::INVALID_ENTITY
               ? "@" + alias
               : std::to_string(existing);
}

kg::EntityRef entity_reference(kg::EntityID existing,
                               const std::string& alias) {
    return {existing, existing == kg::INVALID_ENTITY ? alias : ""};
}

kg::EntityID committed_id(
    kg::EntityID existing, const std::string& alias,
    const std::unordered_map<std::string, kg::EntityID>& bindings) {
    if (existing != kg::INVALID_ENTITY) return existing;
    const auto found = bindings.find(alias);
    return found == bindings.end() ? kg::INVALID_ENTITY : found->second;
}

}  // namespace

SourceCorpusMaterialization materialize_source_corpus(
    const SourceCorpusDeclaration& declaration,
    const SourceAccess& source_access) {
    if (declaration.source_layer.empty())
        return fail("source corpus source_layer must not be empty");
    if (declaration.representations.empty())
        return fail("source corpus declaration must not be empty");

    std::vector<SourceRepresentationDeclaration> declared =
        declaration.representations;
    std::sort(declared.begin(), declared.end(),
              [](const auto& left, const auto& right) {
                  return left.source_file < right.source_file;
              });

    for (std::size_t index = 0; index < declared.size(); ++index) {
        const auto& representation = declared[index];
        if (representation.source_file.empty())
            return fail("source corpus source_file must not be empty");
        if (representation.source_revision.empty())
            return fail("source corpus source_revision must not be empty for " +
                        representation.source_file);
        if (!supported_media(representation.source_media_type))
            return fail("unsupported source media type for " +
                        representation.source_file);
        if (index > 0 &&
            declared[index - 1].source_file == representation.source_file)
            return fail("duplicate source_file in source corpus declaration: " +
                        representation.source_file);
    }

    SourceCorpusMaterialization result;
    result.representations.reserve(declared.size());
    std::vector<SourceManifestRepresentation> manifest_representations;
    manifest_representations.reserve(declared.size());
    for (const auto& representation : declared) {
        SourceReadResult read = source_access.read_exact(representation);
        if (!read.ok) {
            std::string reason = "source access failed for " +
                                 representation.source_file;
            if (!read.reason.empty()) reason += ": " + read.reason;
            return fail(std::move(reason));
        }
        const auto digest = sha256_hex(read.bytes);
        const auto byte_length =
            static_cast<std::uint64_t>(read.bytes.size());
        result.representations.push_back({
            representation.source_file,
            representation.source_media_type,
            representation.source_revision,
            rule_language::ontology::SourceDigestAlgorithm::SHA256,
            digest,
            byte_length,
        });
        manifest_representations.push_back({
            representation.source_file,
            representation.source_media_type,
            rule_language::ontology::SourceDigestAlgorithm::SHA256,
            digest,
            byte_length,
        });
    }

    const auto manifest = build_source_manifest(
        declaration.source_layer, manifest_representations);
    if (!manifest.ok) return fail(manifest.reason);
    result.ok = true;
    result.canonical_manifest = manifest.canonical_bytes;
    result.manifest_digest = manifest.digest;
    result.edition_key = manifest.edition_key;
    return result;
}

SourceCorpusKGMaterialization materialize_source_corpus_into_kg(
    const SourceCorpusDeclaration& declaration,
    const SourceAccess& source_access,
    kg::KGModule& world) {
    const auto materialized =
        materialize_source_corpus(declaration, source_access);
    if (!materialized.ok) return fail_kg(materialized.reason);

    std::string reason;
    const std::string layer_key = "source-layer:" + declaration.source_layer;
    kg::EntityID layer_id = kg::INVALID_ENTITY;
    if (!find_context(world, layer_key, "SourceLayerContext", layer_id,
                      reason))
        return fail_kg(std::move(reason));
    if (layer_id != kg::INVALID_ENTITY &&
        !require_entity_properties(
            world, layer_id,
            {{"context_key", layer_key},
             {"source_layer", declaration.source_layer}},
            reason))
        return fail_kg(std::move(reason));

    std::vector<kg::KGOp> ops;
    const std::string layer_alias = "_source_corpus_layer";
    if (layer_id == kg::INVALID_ENTITY) {
        ops.emplace_back(kg::KGOpCreateEntity{
            "SourceLayerContext",
            {{"context_key", layer_key},
             {"source_layer", declaration.source_layer}},
            layer_alias});
    }
    const std::string layer_reference =
        property_reference(layer_id, layer_alias);

    const std::size_t representation_count =
        materialized.representations.size();
    std::vector<kg::EntityID> representation_ids(
        representation_count, kg::INVALID_ENTITY);
    std::vector<std::string> representation_aliases(representation_count);
    std::vector<std::string> representation_keys(representation_count);
    for (std::size_t index = 0; index < representation_count; ++index) {
        const auto& representation = materialized.representations[index];
        const std::string key = representation_context_key(
            declaration.source_layer, representation);
        representation_keys[index] = key;
        if (!find_context(world, key, "SourceRepresentationContext",
                          representation_ids[index], reason))
            return fail_kg(std::move(reason));
        const std::vector<std::pair<std::string, std::string>> properties = {
            {"context_key", key},
            {"source_layer", declaration.source_layer},
            {"source_file", representation.source_file},
            {"source_layer_context", layer_reference},
            {"source_media_type",
             rule_language::ontology::to_string(
                 representation.source_media_type)},
            {"source_digest_algorithm",
             rule_language::ontology::to_string(
                 representation.source_digest_algorithm)},
            {"source_digest", representation.source_digest},
            {"source_byte_length",
             std::to_string(representation.source_byte_length)},
        };
        if (representation_ids[index] != kg::INVALID_ENTITY) {
            auto existing_properties = properties;
            existing_properties[3].second = std::to_string(layer_id);
            if (layer_id == kg::INVALID_ENTITY ||
                !require_entity_properties(
                    world, representation_ids[index], existing_properties,
                    reason))
                return fail_kg(layer_id == kg::INVALID_ENTITY
                                   ? "existing source representation '" + key +
                                         "' has no source layer context"
                                   : std::move(reason));
            continue;
        }
        representation_aliases[index] =
            "_source_corpus_representation_" + std::to_string(index);
        ops.emplace_back(kg::KGOpCreateEntity{
            "SourceRepresentationContext", properties,
            representation_aliases[index]});
    }

    std::vector<kg::EntityID> observation_ids(
        representation_count, kg::INVALID_ENTITY);
    std::vector<std::string> observation_aliases(representation_count);
    for (std::size_t index = 0; index < representation_count; ++index) {
        const auto& representation = materialized.representations[index];
        if (!validate_revision_observation_conflicts(
                world, declaration.source_layer, representation,
                representation_keys[index], reason))
            return fail_kg(std::move(reason));

        const std::string key =
            revision_observation_key(representation.source_revision);
        if (representation_ids[index] != kg::INVALID_ENTITY &&
            !find_revision_observation(
                world, representation_ids[index], key,
                observation_ids[index], reason))
            return fail_kg(std::move(reason));
        if (observation_ids[index] != kg::INVALID_ENTITY) {
            if (!require_entity_properties(
                    world, observation_ids[index],
                    {{"identity_context",
                      std::to_string(representation_ids[index])},
                     {"entity_key", key},
                     {"source_revision", representation.source_revision}},
                    reason))
                return fail_kg(std::move(reason));
            continue;
        }

        observation_aliases[index] =
            "_source_corpus_revision_observation_" +
            std::to_string(index);
        ops.emplace_back(kg::KGOpCreateEntity{
            "SourceRevisionObservation",
            {{"identity_context",
              property_reference(representation_ids[index],
                                 representation_aliases[index])},
             {"entity_key", key},
             {"source_revision", representation.source_revision}},
            observation_aliases[index]});
    }

    kg::EntityID edition_id = kg::INVALID_ENTITY;
    if (!find_context(world, materialized.edition_key,
                      "IngestionEditionContext", edition_id, reason))
        return fail_kg(std::move(reason));
    const std::string edition_alias = "_source_corpus_edition";
    if (edition_id != kg::INVALID_ENTITY) {
        if (layer_id == kg::INVALID_ENTITY ||
            !require_entity_properties(
                world, edition_id,
                {{"source_layer", declaration.source_layer},
                 {"source_layer_context", std::to_string(layer_id)},
                 {"source_manifest_format", "LENGTH_PREFIXED_V1"},
                 {"source_manifest_digest_algorithm", "SHA256"},
                 {"source_manifest_digest", materialized.manifest_digest},
                 {"source_representation_count",
                  std::to_string(representation_count)}},
                reason))
            return fail_kg(layer_id == kg::INVALID_ENTITY
                               ? "existing ingestion edition has no source "
                                 "layer context"
                               : std::move(reason));
        const auto resolved = resolve_ingestion_edition(world, edition_id);
        if (!resolved.ok) return fail_kg(resolved.reason);
        std::vector<std::string> actual_keys;
        for (const auto representation :
             world.getRelated(edition_id, kEditionMembership))
            actual_keys.push_back(
                world.getProperty(representation, "context_key"));
        std::sort(actual_keys.begin(), actual_keys.end());
        auto expected_keys = representation_keys;
        std::sort(expected_keys.begin(), expected_keys.end());
        if (actual_keys != expected_keys)
            return fail_kg(
                "ingestion edition membership does not match declared corpus");
    } else {
        ops.emplace_back(kg::KGOpCreateEntity{
            "IngestionEditionContext",
            {{"context_key", materialized.edition_key},
             {"source_layer", declaration.source_layer},
             {"source_layer_context", layer_reference},
             {"source_manifest_format", "LENGTH_PREFIXED_V1"},
             {"source_manifest_digest_algorithm", "SHA256"},
             {"source_manifest_digest", materialized.manifest_digest},
             {"source_representation_count",
              std::to_string(representation_count)}},
            edition_alias});
    }

    if (edition_id == kg::INVALID_ENTITY) {
        for (std::size_t index = 0; index < representation_count; ++index) {
            ops.emplace_back(kg::KGOpSetRelation{
                entity_reference(edition_id, edition_alias),
                kEditionMembership,
                entity_reference(representation_ids[index],
                                 representation_aliases[index])});
        }
    }

    kg::KGOpBatchReport report;
    if (!kg::apply_kg_ops_atomically(
            ops, world, report, kg::MutationAuthority::SeedIngestion))
        return fail_kg(report.error);

    SourceCorpusKGMaterialization result;
    result.ok = true;
    result.source_layer_context =
        committed_id(layer_id, layer_alias, report.bindings);
    for (std::size_t index = 0; index < representation_count; ++index)
        result.source_representations.push_back(committed_id(
            representation_ids[index], representation_aliases[index],
            report.bindings));
    for (std::size_t index = 0; index < representation_count; ++index)
        result.source_revision_observations.push_back(committed_id(
            observation_ids[index], observation_aliases[index],
            report.bindings));
    result.ingestion_edition_context =
        committed_id(edition_id, edition_alias, report.bindings);
    return result;
}

}  // namespace logosphere::text
