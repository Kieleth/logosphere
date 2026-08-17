#include "logosphere/text/source_corpus.h"

#include "logosphere/text/source_manifest.h"
#include "logosphere/text/source_target.h"

#include <algorithm>
#include <string>
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

}  // namespace logosphere::text
