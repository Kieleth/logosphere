#pragma once

#include "logosphere/kg/kg_types.h"

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

// Mechanical compact projection of the LinkML-owned edition identity.
// The layer is length-prefixed so punctuation cannot make two layers collide.
std::string canonical_ingestion_edition_key(std::string_view source_layer,
                                            std::string_view manifest_digest);

// Build SourceManifestFormat.LENGTH_PREFIXED_V1 from exact, typed
// SourceRepresentationContext entities. Entries are sorted by source_file.
// Each field is encoded as decimal byte length, ':', then raw bytes. The field
// order is format, entry count, then for every entry: path, media type, digest
// algorithm, digest, byte length. source_commit is provenance, not identity.
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
