#pragma once

#include "logosphere/kg/kg_types.h"
#include "generated/rule_language_ontology.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kg {
class KGModule;
}

namespace logosphere::text {

struct SourceManifestResult {
    bool        ok = false;
    std::string canonical_bytes;
    std::string digest;
    std::string edition_key;
    std::string reason;
};

// Engine-computed representation facts used by the canonical manifest.
// Callers declaring a source corpus do not provide these fields; SourceAccess
// supplies bytes and the engine derives digest and length.
struct SourceManifestRepresentation {
    std::string source_file;
    rule_language::ontology::SourceMediaType source_media_type =
        rule_language::ontology::SourceMediaType::UTF8_TEXT;
    rule_language::ontology::SourceDigestAlgorithm source_digest_algorithm =
        rule_language::ontology::SourceDigestAlgorithm::SHA256;
    std::string source_digest;
    std::uint64_t source_byte_length = 0;
};

// Mechanical compact projection of the LinkML-owned edition identity.
// The layer is length-prefixed so punctuation cannot make two layers collide.
std::string canonical_ingestion_edition_key(std::string_view source_layer,
                                            std::string_view manifest_digest);

// Canonicalize engine-computed representation facts without requiring a KG.
// This is the common identity path used by source-corpus materialization and
// by the KG projection below.
SourceManifestResult build_source_manifest(
    std::string_view source_layer,
    const std::vector<SourceManifestRepresentation>& representations);

// Build SourceManifestFormat.LENGTH_PREFIXED_V1 from exact, typed
// SourceRepresentationContext entities. Entries are sorted by source_file.
// Each field is encoded as decimal byte length, ':', then raw bytes. The field
// order is format, entry count, then for every entry: path, media type, digest
// algorithm, digest, byte length. Revision provenance lives in separate typed
// observations and never participates in this projection.
SourceManifestResult build_source_manifest(
    const kg::KGModule& world, std::string_view source_layer,
    kg::EntityID source_layer_context,
    const std::vector<kg::EntityID>& representations);

// Rebuild and verify an IngestionEditionContext from its typed
// EDITION_INCLUDES_REPRESENTATION relations. Missing data and any disagreement
// between the declared context and the canonical projection fail explicitly.
SourceManifestResult resolve_ingestion_edition(const kg::KGModule& world,
                                               kg::EntityID edition);

}  // namespace logosphere::text
