#include "director/symbolic_refs.h"

#include <sstream>

namespace logotron::director {

namespace {
// Strip leading '@' if present so resolve("@player_cycle") and
// resolve("player_cycle") both work. The canonical form in the
// prompt is "@name"; the parser is permissive.
std::string canonical(const std::string& name) {
    if (!name.empty() && name[0] == '@') return name.substr(1);
    return name;
}

void register_alias(SymbolicRefs& r, const std::string& alias,
                    kg::EntityID id) {
    if (id == kg::INVALID_ENTITY) return;
    r.name_to_id[alias] = id;
    r.id_to_name[id]    = alias;
}
}  // namespace

kg::EntityID SymbolicRefs::resolve(const std::string& name) const {
    auto it = name_to_id.find(canonical(name));
    return it == name_to_id.end() ? kg::INVALID_ENTITY : it->second;
}

std::string SymbolicRefs::alias_for(kg::EntityID id) const {
    auto it = id_to_name.find(id);
    return it == id_to_name.end() ? std::string{} : it->second;
}

std::string SymbolicRefs::as_prompt_text() const {
    if (id_to_name.empty()) return "";
    std::ostringstream os;
    os << "Symbols:";
    // Iterate name_to_id so the order matches insertion (we rely
    // on three known aliases; for that small set this is fine).
    // For larger sets a sorted output would be friendlier.
    for (const auto& [name, id] : name_to_id) {
        os << " @" << name << "=" << id;
    }
    return os.str();
}

SymbolicRefs build_symbolic_refs(kg::EntityID player_cycle,
                                 const std::vector<kg::EntityID>& programs,
                                 kg::EntityID arena,
                                 const std::vector<kg::EntityID>& allies) {
    SymbolicRefs r;
    register_alias(r, "player_cycle", player_cycle);
    for (size_t i = 0; i < programs.size(); ++i) {
        register_alias(r, "program_" + std::to_string(i + 1), programs[i]);
    }
    for (size_t i = 0; i < allies.size(); ++i) {
        register_alias(r, "ally_" + std::to_string(i + 1), allies[i]);
    }
    register_alias(r, "arena", arena);
    return r;
}

}  // namespace logotron::director
