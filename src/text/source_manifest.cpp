#include "logosphere/text/source_manifest.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/text/source_target.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace logosphere::text {
namespace {

constexpr const char* kManifestFormat = "LENGTH_PREFIXED_V1";
constexpr const char* kDigestAlgorithm = "SHA256";
constexpr const char* kMembership = "EDITION_INCLUDES_REPRESENTATION";

SourceManifestResult fail(std::string reason) {
    SourceManifestResult result;
    result.reason = std::move(reason);
    return result;
}

void append_field(std::string& out, std::string_view value) {
    out += std::to_string(value.size());
    out.push_back(':');
    out.append(value.data(), value.size());
}

bool is_lower_sha256(const std::string& value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](const char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool require_string(const kg::KGModule& world, kg::EntityID entity,
                    const char* property, std::string& out,
                    std::string& reason) {
    if (!world.hasProperty(entity, property)) {
        reason = std::string("missing required ") + property;
        return false;
    }
    out = world.getProperty(entity, property);
    if (out.empty()) {
        reason = std::string(property) + " must not be empty";
        return false;
    }
    return true;
}

bool parse_nonnegative(const kg::KGModule& world, kg::EntityID entity,
                       const char* property, std::uint64_t& out,
                       std::string& reason) {
    std::string value;
    if (!require_string(world, entity, property, value, reason)) return false;
    if (value.front() == '-') {
        reason = std::string(property) + " must be a non-negative integer";
        return false;
    }
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        reason = std::string(property) + " must be a non-negative integer";
        return false;
    }
    return true;
}

bool parse_reference(const kg::KGModule& world, kg::EntityID entity,
                     const char* property, kg::EntityID& out,
                     std::string& reason) {
    std::uint64_t parsed = 0;
    if (!parse_nonnegative(world, entity, property, parsed, reason))
        return false;
    if (parsed > std::numeric_limits<kg::EntityID>::max() ||
        !world.exists(static_cast<kg::EntityID>(parsed))) {
        reason = std::string(property) +
                 " does not reference an existing entity";
        return false;
    }
    out = static_cast<kg::EntityID>(parsed);
    return true;
}

bool require_layer_context(const kg::KGModule& world,
                           std::string_view source_layer,
                           kg::EntityID source_layer_context,
                           std::string& reason) {
    if (source_layer.empty()) {
        reason = "source_layer must not be empty";
        return false;
    }
    if (!world.exists(source_layer_context) ||
        !world.getRegistry().isSubtypeOf(
            world.getType(source_layer_context), "SourceLayerContext")) {
        reason = "source_layer_context is not a SourceLayerContext";
        return false;
    }
    std::string declared_layer;
    if (!require_string(world, source_layer_context, "source_layer",
                        declared_layer, reason))
        return false;
    if (declared_layer != source_layer) {
        reason = "source_layer_context source_layer does not match the edition";
        return false;
    }
    return true;
}

}  // namespace

std::string canonical_ingestion_edition_key(
    std::string_view source_layer, std::string_view manifest_digest) {
    return "ingestion-edition:v1:" + std::to_string(source_layer.size()) +
           ":" + std::string(source_layer) + ":sha256:" +
           std::string(manifest_digest);
}

SourceManifestResult build_source_manifest(
    std::string_view source_layer,
    const std::vector<SourceManifestRepresentation>& representations) {
    if (source_layer.empty()) return fail("source_layer must not be empty");
    if (representations.empty()) return fail("source manifest must not be empty");

    std::vector<SourceManifestRepresentation> entries = representations;
    for (const auto& entry : entries) {
        if (entry.source_file.empty())
            return fail("source_file must not be empty in source manifest");
        switch (entry.source_media_type) {
            case rule_language::ontology::SourceMediaType::UTF8_TEXT:
                break;
            default:
                return fail("source_media_type is not supported");
        }
        if (entry.source_digest_algorithm !=
            rule_language::ontology::SourceDigestAlgorithm::SHA256)
            return fail("source_digest_algorithm is not supported");
        if (!is_lower_sha256(entry.source_digest))
            return fail(
                "source_digest must be 64 lowercase hexadecimal digits");
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& left, const auto& right) {
                  return left.source_file < right.source_file;
              });
    for (std::size_t index = 1; index < entries.size(); ++index)
        if (entries[index - 1].source_file == entries[index].source_file)
            return fail("duplicate source_file in source manifest: " +
                        entries[index].source_file);

    std::string canonical;
    append_field(canonical, kManifestFormat);
    append_field(canonical, std::to_string(entries.size()));
    for (const auto& entry : entries) {
        append_field(canonical, entry.source_file);
        append_field(canonical,
                     rule_language::ontology::to_string(
                         entry.source_media_type));
        append_field(canonical,
                     rule_language::ontology::to_string(
                         entry.source_digest_algorithm));
        append_field(canonical, entry.source_digest);
        append_field(canonical, std::to_string(entry.source_byte_length));
    }

    SourceManifestResult result;
    result.ok = true;
    result.canonical_bytes = std::move(canonical);
    result.digest = sha256_hex(result.canonical_bytes);
    result.edition_key =
        canonical_ingestion_edition_key(source_layer, result.digest);
    return result;
}

SourceManifestResult build_source_manifest(
    const kg::KGModule& world, std::string_view source_layer,
    kg::EntityID source_layer_context,
    const std::vector<kg::EntityID>& representations) {
    std::string reason;
    if (!require_layer_context(world, source_layer, source_layer_context,
                               reason))
        return fail(reason);
    if (representations.empty()) return fail("source manifest must not be empty");

    const auto& ontology = world.getRegistry();
    std::vector<SourceManifestRepresentation> entries;
    entries.reserve(representations.size());
    for (const auto representation : representations) {
        if (!world.exists(representation) ||
            !ontology.isSubtypeOf(world.getType(representation),
                                  "SourceRepresentationContext"))
            return fail("manifest member is not a SourceRepresentationContext");

        std::string representation_layer;
        std::string source_file;
        std::string source_revision;
        std::string media_type;
        std::string digest_algorithm;
        std::string digest;
        if (!require_string(world, representation, "source_layer",
                            representation_layer, reason) ||
            !require_string(world, representation, "source_file", source_file,
                            reason) ||
            !require_string(world, representation, "source_revision",
                            source_revision, reason) ||
            !require_string(world, representation, "source_media_type",
                            media_type, reason) ||
            !require_string(world, representation, "source_digest_algorithm",
                            digest_algorithm, reason) ||
            !require_string(world, representation, "source_digest", digest,
                            reason))
            return fail(reason);

        if (representation_layer != source_layer)
            return fail("representation source_layer does not match the edition");
        kg::EntityID representation_layer_context = kg::INVALID_ENTITY;
        if (!parse_reference(world, representation, "source_layer_context",
                             representation_layer_context, reason))
            return fail(reason);
        if (representation_layer_context != source_layer_context)
            return fail(
                "representation source_layer_context does not match the edition");
        if (!ontology.isEnumMember("SourceMediaType", media_type))
            return fail("source_media_type is not a SourceMediaType member");
        if (digest_algorithm != kDigestAlgorithm)
            return fail("source_digest_algorithm is not supported");
        if (!is_lower_sha256(digest))
            return fail(
                "source_digest must be 64 lowercase hexadecimal digits");

        std::uint64_t byte_length = 0;
        if (!parse_nonnegative(world, representation, "source_byte_length",
                               byte_length, reason))
            return fail(reason);
        rule_language::ontology::SourceMediaType parsed_media;
        if (!rule_language::ontology::from_string(media_type.c_str(),
                                                  parsed_media))
            return fail("source_media_type has no generated enum member");
        rule_language::ontology::SourceDigestAlgorithm parsed_algorithm;
        if (!rule_language::ontology::from_string(digest_algorithm.c_str(),
                                                  parsed_algorithm))
            return fail(
                "source_digest_algorithm has no generated enum member");
        entries.push_back({std::move(source_file), parsed_media,
                           parsed_algorithm, std::move(digest), byte_length});
    }
    return build_source_manifest(source_layer, entries);
}

SourceManifestResult resolve_ingestion_edition(const kg::KGModule& world,
                                               kg::EntityID edition) {
    if (!world.exists(edition)) return fail("ingestion edition does not exist");
    if (!world.getRegistry().isSubtypeOf(world.getType(edition),
                                         "IngestionEditionContext"))
        return fail("entity is not an IngestionEditionContext");

    std::string reason;
    std::string source_layer;
    std::string manifest_format;
    std::string digest_algorithm;
    std::string declared_digest;
    std::string declared_key;
    if (!require_string(world, edition, "source_layer", source_layer, reason) ||
        !require_string(world, edition, "source_manifest_format",
                        manifest_format, reason) ||
        !require_string(world, edition, "source_manifest_digest_algorithm",
                        digest_algorithm, reason) ||
        !require_string(world, edition, "source_manifest_digest",
                        declared_digest, reason) ||
        !require_string(world, edition, "context_key", declared_key, reason))
        return fail(reason);
    if (manifest_format != kManifestFormat)
        return fail("source_manifest_format is not supported");
    if (digest_algorithm != kDigestAlgorithm)
        return fail("source_manifest_digest_algorithm is not supported");
    if (!is_lower_sha256(declared_digest))
        return fail(
            "source_manifest_digest must be 64 lowercase hexadecimal digits");

    kg::EntityID source_layer_context = kg::INVALID_ENTITY;
    if (!parse_reference(world, edition, "source_layer_context",
                         source_layer_context, reason))
        return fail(reason);

    std::uint64_t declared_count = 0;
    if (!parse_nonnegative(world, edition, "source_representation_count",
                           declared_count, reason))
        return fail(reason);
    if (declared_count == 0)
        return fail("source_representation_count must be at least one");

    const auto representations = world.getRelated(edition, kMembership);
    if (declared_count != representations.size())
        return fail("source_representation_count does not match membership");

    auto result = build_source_manifest(world, source_layer,
                                        source_layer_context, representations);
    if (!result.ok) return result;
    if (declared_digest != result.digest)
        return fail("source_manifest_digest does not match canonical manifest");
    if (declared_key != result.edition_key)
        return fail("context_key does not match ingestion edition identity");
    return result;
}

}  // namespace logosphere::text
