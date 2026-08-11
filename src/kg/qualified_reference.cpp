#include "logosphere/kg/qualified_reference.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kg {
namespace {

bool is_literal_byte(unsigned char byte) {
    return (byte >= 'A' && byte <= 'Z') ||
           (byte >= 'a' && byte <= 'z') ||
           (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
           byte == '~' || byte == '-';
}

bool is_upper_hex(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'A' && value <= 'F');
}

unsigned char hex_value(char value) {
    return static_cast<unsigned char>(
        value <= '9' ? value - '0' : value - 'A' + 10);
}

bool validate_utf8_and_controls(const std::string& value,
                                std::string& error) {
    size_t at = 0;
    while (at < value.size()) {
        const auto first = static_cast<unsigned char>(value[at]);
        uint32_t codepoint = 0;
        size_t length = 0;
        if (first <= 0x7F) {
            codepoint = first;
            length = 1;
        } else if (first >= 0xC2 && first <= 0xDF) {
            codepoint = first & 0x1F;
            length = 2;
        } else if (first >= 0xE0 && first <= 0xEF) {
            codepoint = first & 0x0F;
            length = 3;
        } else if (first >= 0xF0 && first <= 0xF4) {
            codepoint = first & 0x07;
            length = 4;
        } else {
            error = "invalid UTF-8 leading byte";
            return false;
        }
        if (at + length > value.size()) {
            error = "truncated UTF-8 sequence";
            return false;
        }
        for (size_t offset = 1; offset < length; ++offset) {
            const auto next =
                static_cast<unsigned char>(value[at + offset]);
            if ((next & 0xC0) != 0x80) {
                error = "invalid UTF-8 continuation byte";
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((length == 3 && codepoint < 0x800) ||
            (length == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0x10FFFF) {
            error = "non-canonical UTF-8 sequence";
            return false;
        }
        if (codepoint <= 0x1F ||
            (codepoint >= 0x7F && codepoint <= 0x9F)) {
            error = "control characters are not allowed";
            return false;
        }
        at += length;
    }
    return true;
}

bool decode_segment(const std::string& encoded, std::string& decoded,
                    std::string& error) {
    if (encoded.empty()) {
        error = "empty path segment";
        return false;
    }
    decoded.clear();
    for (size_t at = 0; at < encoded.size();) {
        const auto byte = static_cast<unsigned char>(encoded[at]);
        if (is_literal_byte(byte)) {
            decoded.push_back(static_cast<char>(byte));
            ++at;
            continue;
        }
        if (byte != '%' || at + 2 >= encoded.size() ||
            !is_upper_hex(encoded[at + 1]) ||
            !is_upper_hex(encoded[at + 2])) {
            error = "segment is not canonically percent encoded";
            return false;
        }
        const auto decoded_byte = static_cast<unsigned char>(
            (hex_value(encoded[at + 1]) << 4) |
            hex_value(encoded[at + 2]));
        if (is_literal_byte(decoded_byte)) {
            error = "literal byte must not be percent encoded";
            return false;
        }
        decoded.push_back(static_cast<char>(decoded_byte));
        at += 3;
    }
    if (!validate_utf8_and_controls(decoded, error)) return false;
    if (decoded == "." || decoded == "..") {
        error = "dot path segments are not allowed";
        return false;
    }
    return true;
}

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (at <= path.size()) {
        const size_t end = path.find('/', at);
        if (end == std::string::npos) {
            parts.push_back(path.substr(at));
            break;
        }
        parts.push_back(path.substr(at, end - at));
        at = end + 1;
    }
    return parts;
}

const char* meta_kind_name(QualifiedReferenceKind kind) {
    switch (kind) {
        case QualifiedReferenceKind::MetaClass: return "class";
        case QualifiedReferenceKind::MetaProperty: return "property";
        case QualifiedReferenceKind::MetaRelation: return "relation";
        case QualifiedReferenceKind::MetaFacet: return "facet";
        case QualifiedReferenceKind::MetaValueKind: return "value-kind";
        case QualifiedReferenceKind::MetaEnum: return "enum";
        case QualifiedReferenceKind::MetaEnumMember: return "enum-member";
        case QualifiedReferenceKind::Entity: break;
    }
    throw std::invalid_argument("Entity reference has no meta kind name");
}

size_t expected_segment_count(QualifiedReferenceKind kind) {
    switch (kind) {
        case QualifiedReferenceKind::MetaProperty:
        case QualifiedReferenceKind::MetaEnumMember:
            return 2;
        case QualifiedReferenceKind::Entity:
            return 3;
        default:
            return 1;
    }
}

bool parse_kind(const std::vector<std::string>& parts,
                QualifiedReferenceKind& kind, size_t& variable_at,
                std::string& error) {
    if (parts.empty()) {
        error = "empty qualified reference";
        return false;
    }
    if (parts[0] == "@@entity") {
        kind = QualifiedReferenceKind::Entity;
        variable_at = 1;
        return true;
    }
    if (parts.size() < 2 || parts[0] != "@@meta") {
        error = "expected @@meta/... or @@entity/... qualified reference";
        return false;
    }
    static const std::array<std::pair<const char*, QualifiedReferenceKind>, 7>
        kinds{{
            {"class", QualifiedReferenceKind::MetaClass},
            {"property", QualifiedReferenceKind::MetaProperty},
            {"relation", QualifiedReferenceKind::MetaRelation},
            {"facet", QualifiedReferenceKind::MetaFacet},
            {"value-kind", QualifiedReferenceKind::MetaValueKind},
            {"enum", QualifiedReferenceKind::MetaEnum},
            {"enum-member", QualifiedReferenceKind::MetaEnumMember},
        }};
    for (const auto& [name, candidate] : kinds) {
        if (parts[1] == name) {
            kind = candidate;
            variable_at = 2;
            return true;
        }
    }
    error = "unknown @@meta reference kind '" + parts[1] + "'";
    return false;
}

QualifiedResolveResult fail(std::string error) {
    return {false, INVALID_ENTITY, std::move(error)};
}

}  // namespace

std::string encode_qualified_reference_segment(const std::string& segment) {
    std::string error;
    if (segment.empty()) {
        throw std::invalid_argument("Cannot encode an empty path segment");
    }
    if (!validate_utf8_and_controls(segment, error)) {
        throw std::invalid_argument("Cannot encode path segment: " + error);
    }
    if (segment == "." || segment == "..") {
        throw std::invalid_argument("Cannot encode a dot path segment");
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char byte : segment) {
        if (is_literal_byte(byte)) {
            out.push_back(static_cast<char>(byte));
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0F]);
        }
    }
    return out;
}

bool parse_qualified_reference(const std::string& path,
                               QualifiedReference& out,
                               std::string& error) {
    out = QualifiedReference{};
    error.clear();
    const auto parts = split_path(path);
    size_t variable_at = 0;
    if (!parse_kind(parts, out.kind, variable_at, error)) return false;
    const size_t expected = expected_segment_count(out.kind);
    if (parts.size() != variable_at + expected) {
        error = "qualified reference has " +
                std::to_string(parts.size() - variable_at) +
                " variable segments, expected " +
                std::to_string(expected);
        return false;
    }
    out.segments.reserve(expected);
    for (size_t index = variable_at; index < parts.size(); ++index) {
        std::string decoded;
        if (!decode_segment(parts[index], decoded, error)) {
            error = "path segment " + std::to_string(index - variable_at) +
                    ": " + error;
            out = QualifiedReference{};
            return false;
        }
        out.segments.push_back(std::move(decoded));
    }
    return true;
}

std::string format_qualified_reference(const QualifiedReference& reference) {
    const size_t expected = expected_segment_count(reference.kind);
    if (reference.segments.size() != expected) {
        throw std::invalid_argument("Wrong segment count for qualified reference");
    }
    std::string out;
    if (reference.kind == QualifiedReferenceKind::Entity) {
        out = "@@entity";
    } else {
        out = std::string("@@meta/") + meta_kind_name(reference.kind);
    }
    for (const auto& segment : reference.segments) {
        out += "/" + encode_qualified_reference_segment(segment);
    }
    return out;
}

QualifiedResolveResult resolve_qualified_reference(const std::string& path,
                                                    const KGModule& world) {
    QualifiedReference reference;
    std::string error;
    if (!parse_qualified_reference(path, reference, error)) {
        return fail("qualified reference '" + path + "': " + error);
    }
    if (reference.kind != QualifiedReferenceKind::Entity) {
        if (!world.hasCurrentOntologyMetaGraph()) {
            return fail("qualified reference '" + path +
                        "': ontology meta-graph is not current");
        }
        const EntityID entity = world.findOntologyMetaEntity(path);
        if (entity == INVALID_ENTITY || !world.exists(entity)) {
            return fail("qualified reference '" + path +
                        "': no canonical ontology meta-entity exists");
        }
        return {true, entity, ""};
    }

    const auto& ontology = world.getRegistry();
    const std::string& context_key = reference.segments[0];
    const std::string& exact_type = reference.segments[1];
    const std::string& entity_key = reference.segments[2];
    if (!ontology.hasEntityType(exact_type) ||
        !ontology.isSubtypeOf(exact_type, "Addressable") ||
        ontology.isAbstract(exact_type)) {
        return fail("qualified reference '" + path + "': type '" +
                    exact_type + "' is not a concrete Addressable type");
    }

    std::vector<EntityID> contexts;
    for (EntityID id : world.findByProperty("context_key", context_key)) {
        if (ontology.isSubtypeOf(world.getType(id), "KnowledgeContext")) {
            contexts.push_back(id);
        }
    }
    if (contexts.size() != 1) {
        return fail("qualified reference '" + path + "': context key '" +
                    context_key + "' has " +
                    std::to_string(contexts.size()) +
                    " matching KnowledgeContext entities");
    }

    std::vector<EntityID> matches;
    const std::string context_id = std::to_string(contexts.front());
    for (EntityID id : world.findByType(exact_type)) {
        if (world.getProperty(id, "identity_context") == context_id &&
            world.getProperty(id, "entity_key") == entity_key) {
            matches.push_back(id);
        }
    }
    if (matches.empty()) {
        return fail("qualified reference '" + path +
                    "': no matching Addressable entity is loaded");
    }
    if (matches.size() != 1) {
        return fail("qualified reference '" + path + "': found " +
                    std::to_string(matches.size()) +
                    " matching Addressable entities, expected exactly one");
    }
    return {true, matches.front(), ""};
}

}  // namespace kg
