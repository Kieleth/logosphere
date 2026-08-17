#pragma once

#include "logosphere/kg/kg_types.h"
#include "generated/rule_language_ontology.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kg {
class KGModule;
}

namespace logosphere::text {

// Explicit application-owned corpus membership. It carries no digest or byte
// length: those are engine-derived facts, never caller assertions.
struct SourceRepresentationDeclaration {
    std::string source_file;
    rule_language::ontology::SourceMediaType source_media_type;
    std::string source_revision;

    SourceRepresentationDeclaration(
        std::string file,
        rule_language::ontology::SourceMediaType media_type,
        std::string revision)
        : source_file(std::move(file)),
          source_media_type(media_type),
          source_revision(std::move(revision)) {}
};

struct SourceCorpusDeclaration {
    std::string source_layer;
    std::vector<SourceRepresentationDeclaration> representations;
};

struct SourceReadResult {
    bool ok = false;
    std::string bytes;
    std::string reason;
};

// Replaceable L0 byte-access seam. A filesystem, embedded asset bundle, PDF
// store, database, or remote-backed tool can implement it without changing
// corpus identity or ingestion semantics.
class SourceAccess {
public:
    virtual ~SourceAccess() = default;
    virtual SourceReadResult read_exact(
        const SourceRepresentationDeclaration& declaration) const = 0;
};

struct MaterializedSourceRepresentation {
    std::string source_file;
    rule_language::ontology::SourceMediaType source_media_type =
        rule_language::ontology::SourceMediaType::UTF8_TEXT;
    std::string source_revision;
    rule_language::ontology::SourceDigestAlgorithm source_digest_algorithm =
        rule_language::ontology::SourceDigestAlgorithm::SHA256;
    std::string source_digest;
    std::uint64_t source_byte_length = 0;
};

struct SourceCorpusMaterialization {
    bool ok = false;
    std::vector<MaterializedSourceRepresentation> representations;
    std::string canonical_manifest;
    std::string manifest_digest;
    std::string edition_key;
    std::string reason;
};

struct SourceCorpusKGMaterialization {
    bool ok = false;
    kg::EntityID source_layer_context = kg::INVALID_ENTITY;
    std::vector<kg::EntityID> source_representations;
    std::vector<kg::EntityID> source_revision_observations;
    kg::EntityID ingestion_edition_context = kg::INVALID_ENTITY;
    std::string reason;
};

// Validate explicit membership, read every declared representation through
// SourceAccess, derive exact content identity, and build the canonical edition
// manifest. Results are sorted by source_file. Failure publishes no partial
// materialization and names the missing or invalid declaration.
SourceCorpusMaterialization materialize_source_corpus(
    const SourceCorpusDeclaration& declaration,
    const SourceAccess& source_access);

// Materialize the complete source context graph through the validated atomic
// KG-op path. Existing exact contexts are reused. Missing contexts, edition
// membership, and immutable revision observations commit together or not at
// all. A later source revision may point to an existing content representation
// without changing its identity; conflicting bytes for one revision and
// logical path fail.
SourceCorpusKGMaterialization materialize_source_corpus_into_kg(
    const SourceCorpusDeclaration& declaration,
    const SourceAccess& source_access,
    kg::KGModule& world);

}  // namespace logosphere::text
