#ifndef LOGOSPHERE_KG_QUALIFIED_REFERENCE_H
#define LOGOSPHERE_KG_QUALIFIED_REFERENCE_H

#include "logosphere/kg/kg_types.h"

#include <string>
#include <vector>

namespace kg {

class KGModule;

enum class QualifiedReferenceKind {
    MetaClass,
    MetaProperty,
    MetaRelation,
    MetaFacet,
    MetaValueKind,
    MetaEnum,
    MetaEnumMember,
    Entity,
};

struct QualifiedReference {
    QualifiedReferenceKind kind = QualifiedReferenceKind::Entity;
    std::vector<std::string> segments;
};

struct QualifiedResolveResult {
    bool ok = false;
    EntityID entity = INVALID_ENTITY;
    std::string error;
};

std::string encode_qualified_reference_segment(const std::string& segment);

bool parse_qualified_reference(const std::string& path,
                               QualifiedReference& out,
                               std::string& error);

std::string format_qualified_reference(const QualifiedReference& reference);

QualifiedResolveResult resolve_qualified_reference(const std::string& path,
                                                    const KGModule& world);

}  // namespace kg

#endif  // LOGOSPHERE_KG_QUALIFIED_REFERENCE_H
