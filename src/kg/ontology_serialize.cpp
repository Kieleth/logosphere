#include "logosphere/kg/ontology_serialize.h"


#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <sstream>
#include <string>

namespace kg {

namespace detail {

// JSON-escape a string. We expect ontology names to be safe ASCII
// identifiers in practice (entity types like "Cycle", property
// names like "max_speed"), but escape defensively so a hostile
// schema can't break the JSON we hand to the LLM. Shared with
// kg_query.cpp's renderer (declared in ontology_serialize.h).
void emit_json_string(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    os << buf;
                } else {
                    os << c;
                }
        }
    }
    os << '"';
}

}  // namespace detail

std::string serialize_ontology_slice(
    const OntologyRegistry& registry,
    const std::vector<std::string>& type_names) {
    std::ostringstream os;
    os << '{';

    bool first_type = true;
    for (const auto& type : type_names) {
        if (!registry.hasEntityType(type)) continue;  // skip unknown
        if (!first_type) os << ',';
        first_type = false;

        // Look up parent + abstract flag via the public entityTypes() map.
        const auto& all_types = registry.entityTypes();
        auto it = all_types.find(type);
        std::string parent = (it == all_types.end()) ? "" : it->second.parent;
        bool abstract      = (it == all_types.end()) ? false : it->second.is_abstract;

        detail::emit_json_string(os, type);
        os << ":{";
        os << "\"parent\":";
        detail::emit_json_string(os, parent);
        os << ",\"abstract\":" << (abstract ? "true" : "false");
        os << ",\"properties\":{";

        bool first_prop = true;
        for (const auto& prop : registry.propertiesOf(type)) {
            if (!first_prop) os << ',';
            first_prop = false;
            detail::emit_json_string(os, prop.name);
            os << ":{\"type\":";
            detail::emit_json_string(
                os, property_value_kind_name(prop.value_kind));
            if (prop.required) os << ",\"required\":true";
            // The validator enforces these; a spec sheet that omits
            // them invites out-of-range ops (the redwood-45 lesson).
            if (prop.has_min) os << ",\"min\":" << prop.min_value;
            if (prop.has_max) os << ",\"max\":" << prop.max_value;
            os << '}';
        }
        os << "}}";
    }

    os << '}';
    return os.str();
}

}  // namespace kg
