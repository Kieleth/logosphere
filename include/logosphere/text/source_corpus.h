#pragma once

#include "generated/rule_language_ontology.h"

#include <cstdint>
#include <string>
#include <vector>

namespace logosphere::text {

// Explicit application-owned corpus membership. It carries no digest or byte
// length: those are engine-derived facts, never caller assertions.
struct SourceRepresentationDeclaration {
    std::string source_file;
    rule_language::ontology::SourceMediaType source_media_type =
        rule_language::ontology::SourceMediaType::UTF8_TEXT;
    std::string source_revision;
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

// Validate explicit membership, read every declared representation through
// SourceAccess, derive exact content identity, and build the canonical edition
// manifest. Results are sorted by source_file. Failure publishes no partial
// materialization and names the missing or invalid declaration.
SourceCorpusMaterialization materialize_source_corpus(
    const SourceCorpusDeclaration& declaration,
    const SourceAccess& source_access);

}  // namespace logosphere::text
