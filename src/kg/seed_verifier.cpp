#include "logosphere/kg/seed_verifier.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace kg {

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// The "as" binder of a create op; empty for everything else.
std::string alias_of(const KGOp& op) {
    if (const auto* ce = std::get_if<KGOpCreateEntity>(&op)) return ce->as;
    return "";
}

std::string preview(const std::string& s, size_t n = 48) {
    return s.size() <= n ? s : s.substr(0, n) + "...";
}

// Number tokens of a quote: maximal digit runs, with thousands-
// commas inside a run absorbed ("10,000" tokenizes as "10000").
// The VALUE check requires a slot's absolute digit string to EQUAL
// one token - substring matching would accept 5 against "Dex 15+".
std::vector<std::string> number_tokens(const std::string& q) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < q.size()) {
        if (!std::isdigit(static_cast<unsigned char>(q[i]))) {
            ++i;
            continue;
        }
        std::string tok;
        while (i < q.size()) {
            if (std::isdigit(static_cast<unsigned char>(q[i]))) {
                tok += q[i];
                ++i;
            } else if (q[i] == ',' && i + 1 < q.size() &&
                       std::isdigit(static_cast<unsigned char>(q[i + 1]))) {
                ++i;  // comma inside a number: skip, keep the run going
            } else {
                break;
            }
        }
        out.push_back(std::move(tok));
    }
    return out;
}

// Parse the leading markdown cell of a table-row quote into a band,
// accepting the notations the vendored book itself prints - nothing
// more:
//   "| 3 |"            -> [3, 3]
//   "| 3-5 |"          -> [3, 5]   (ASCII hyphen)
//   "| 3\xE2\x80\x93""5 |" -> [3, 5]  (en dash, U+2013)
//   "| 0 through 2 |"  -> [0, 2]
//   "| \-6 |"          -> [-6, -6] (markdown-escaped negative)
// Returns false when the quote does not begin with a band-shaped
// cell.
bool parse_band_cell(const std::string& q, long long& lo, long long& hi) {
    size_t i = 0;
    auto skip_spaces = [&] { while (i < q.size() && q[i] == ' ') ++i; };
    auto parse_num = [&](long long& out) {
        bool neg = false;
        if (i + 1 < q.size() && q[i] == '\\' && q[i + 1] == '-') {
            i += 2;
            neg = true;
        } else if (i < q.size() && q[i] == '-') {
            ++i;
            neg = true;
        }
        size_t start = i;
        while (i < q.size() && std::isdigit(static_cast<unsigned char>(q[i])))
            ++i;
        if (i == start) return false;
        out = std::strtoll(q.substr(start, i - start).c_str(), nullptr, 10);
        if (neg) out = -out;
        return true;
    };
    if (q.empty() || q[0] != '|') return false;
    i = 1;
    skip_spaces();
    if (!parse_num(lo)) return false;
    const size_t after_lo = i;
    skip_spaces();
    bool has_range = false;
    if (i < q.size() && q[i] == '-') {
        ++i;
        has_range = true;
    } else if (q.compare(i, 3, "\xE2\x80\x93") == 0) {
        i += 3;
        has_range = true;
    } else if (q.compare(i, 8, "through ") == 0) {
        i += 8;
        has_range = true;
    }
    if (has_range) {
        skip_spaces();
        if (!parse_num(hi)) return false;
    } else {
        i = after_lo;
        hi = lo;
    }
    skip_spaces();
    return i < q.size() && q[i] == '|';
}

// A stored band property, parsed. The schema already validated these
// as integers; a parse failure here means the property is absent.
bool read_band(const KGModule& kg, EntityID id, const char* min_key,
               const char* max_key, long long& lo, long long& hi) {
    const std::string lo_s = kg.getProperty(id, min_key);
    const std::string hi_s = kg.getProperty(id, max_key);
    if (lo_s.empty() || hi_s.empty()) return false;
    lo = std::strtoll(lo_s.c_str(), nullptr, 10);
    hi = std::strtoll(hi_s.c_str(), nullptr, 10);
    return true;
}

// The band slots for a row entity: roll_min/roll_max on TableEntry,
// key_min/key_max on LookupEntry. Returns false if the type has
// neither pair set.
bool read_row_band(const KGModule& kg, EntityID id, long long& lo,
                   long long& hi) {
    return read_band(kg, id, "roll_min", "roll_max", lo, hi) ||
           read_band(kg, id, "key_min", "key_max", lo, hi);
}

struct Checker {
    const SeedEnvelope& seed;
    const std::string& source_root;
    const OntologyRegistry& ont;
    SeedVerifyReport& report;

    // rel path -> file content; empty string caches "unreadable".
    std::map<std::string, std::string> files;

    void violate(const std::string& check, int op_index,
                 const std::string& alias, const std::string& reason) {
        report.violations.push_back({check, op_index, alias, reason});
    }

    // Load (with cache) the source file a quote cites. nullptr with
    // `why` set on rejection or read failure. Paths with ".." are
    // refused before opening - a seed file must not read outside
    // the source root.
    const std::string* source_text(const std::string& rel,
                                   std::string& why) {
        if (rel.find("..") != std::string::npos) {
            why = "path traversal: '..' is not allowed in source path '" +
                  rel + "'";
            return nullptr;
        }
        auto it = files.find(rel);
        if (it == files.end()) {
            it = files.emplace(rel, slurp(source_root + "/" + rel)).first;
        }
        if (it->second.empty()) {
            why = "cannot read source file " + source_root + "/" + rel;
            return nullptr;
        }
        return &it->second;
    }

    // ------------------------------------------- source-commit drift

    // Report, not gate (owner CI ruling): a vendored tree that moved
    // past the envelope's pin is a re-extraction TODO, not a broken
    // seed. No SOURCE_COMMIT file, no opinion.
    void check_commit_pin() {
        const std::string pinned = trim(slurp(source_root +
                                              "/SOURCE_COMMIT"));
        if (pinned.empty()) return;
        if (pinned != seed.source.commit) {
            report.warnings.push_back(
                "source drift: envelope pins commit " + seed.source.commit +
                " but " + source_root + "/SOURCE_COMMIT is " + pinned);
        }
    }

    // ---------------------------------------------------- 1. SCHEMA

    // Loads into `world` through the seed loader; the loaded state
    // is what every other check reads.
    bool check_schema(KGModule& world, SeedLoadReport& load) {
        const bool ok = load_seed(seed, world, load);
        report.ops_loaded = load.ops_applied;
        if (!ok) {
            const std::string alias =
                load.failed_op >= 0
                    ? alias_of(seed.ops[static_cast<size_t>(load.failed_op)])
                    : "";
            violate("schema", load.failed_op, alias, load.error);
        }
        return ok;
    }

    // -------------------------------------------------- 2. VERBATIM

    // Reads the LOADED world, so quotes injected or rewritten by
    // set_property ops are certified too - the same state the VALUE
    // check treats as ground truth. The file is the entity's own
    // source_file when set (multi-file seeds), else the envelope's.
    void check_verbatim(const KGModule& world, const SeedLoadReport& load) {
        for (size_t i = 0; i < seed.ops.size(); ++i) {
            const EntityID id = load.created_ids[i];
            if (id == INVALID_ENTITY) continue;
            const std::string type = world.getType(id);
            const std::string alias = alias_of(seed.ops[i]);
            const std::string quote = world.getProperty(id, "source_quote");
            if (quote.empty()) {
                // The Cited contract (schema/packs/rulebook.yaml):
                // ingested data of a type that declares source_quote
                // must carry one. Types without the slot are exempt.
                if (ont.hasProperty(type, "source_quote")) {
                    violate("verbatim", static_cast<int>(i), alias,
                            "uncited ingested entity: " + type +
                            " declares source_quote but none is set");
                }
                continue;
            }
            ++report.quotes_checked;
            const std::string own_file =
                world.getProperty(id, "source_file");
            const std::string rel =
                own_file.empty() ? seed.source.file : own_file;
            std::string why;
            const std::string* text = source_text(rel, why);
            if (!text) {
                violate("verbatim", static_cast<int>(i), alias, why);
                continue;
            }
            if (text->find(quote) == std::string::npos) {
                violate("verbatim", static_cast<int>(i), alias,
                        "source_quote is not a byte-exact substring of " +
                        rel + ": \"" + preview(quote) + "\"");
            }
        }
    }

    // ----------------------------------------------------- 3. VALUE

    void check_values(const KGModule& world, const SeedLoadReport& load) {
        for (size_t i = 0; i < seed.ops.size(); ++i) {
            const EntityID id = load.created_ids[i];
            if (id == INVALID_ENTITY) continue;
            const std::string quote = world.getProperty(id, "source_quote");
            if (quote.empty()) continue;
            const std::string type = world.getType(id);
            const std::string alias = alias_of(seed.ops[i]);
            const std::vector<std::string> tokens = number_tokens(quote);

            // Digits: every numeric slot's absolute value must equal
            // one of the quote's number tokens.
            for (const auto& [key, value] :
                 world.getPropertiesWithPrefix(id, "")) {
                const PropertyDef* def = ont.findProperty(type, key);
                if (!def || (def->value_type != "integer" &&
                             def->value_type != "float")) {
                    continue;
                }
                ++report.values_checked;
                std::string digits = value;
                if (!digits.empty() &&
                    (digits[0] == '-' || digits[0] == '+')) {
                    digits.erase(0, 1);
                }
                if (digits.empty() ||
                    std::find(tokens.begin(), tokens.end(), digits) ==
                        tokens.end()) {
                    violate("value", static_cast<int>(i), alias,
                            type + "." + key + " = " + value +
                            ": digits '" + digits + "' equal no number "
                            "token of the entity's quote \"" +
                            preview(quote) + "\"");
                }
            }

            // Band derivation for table rows: the quoted leading cell
            // is the band the book printed; the stored band must
            // equal it.
            const bool is_row = ont.isSubtypeOf(type, "TableEntry") ||
                                ont.isSubtypeOf(type, "LookupEntry");
            if (!is_row) continue;
            long long cell_lo = 0, cell_hi = 0;
            if (!parse_band_cell(quote, cell_lo, cell_hi)) {
                violate("value", static_cast<int>(i), alias,
                        type + ": cannot derive a band from the quoted "
                        "leading cell (the book's notations: '| N |', "
                        "'| N-M |', en dash, '| N through M |'): \"" +
                        preview(quote) + "\"");
                continue;
            }
            ++report.bands_derived;
            long long row_lo = 0, row_hi = 0;
            if (!read_row_band(world, id, row_lo, row_hi)) {
                violate("value", static_cast<int>(i), alias,
                        type + ": quote declares band [" +
                        std::to_string(cell_lo) + ", " +
                        std::to_string(cell_hi) + "] but the row has no "
                        "band slots set");
                continue;
            }
            if (row_lo != cell_lo || row_hi != cell_hi) {
                violate("value", static_cast<int>(i), alias,
                        type + ": band [" + std::to_string(row_lo) + ", " +
                        std::to_string(row_hi) + "] does not equal the "
                        "quoted cell's [" + std::to_string(cell_lo) + ", " +
                        std::to_string(cell_hi) + "]");
            }
        }
    }

    // ------------------------------------------------- 4. INVARIANT

    void check_invariants(const KGModule& world,
                          const SeedLoadReport& load) {
        const SeedInvariants& inv = seed.invariants;

        for (const auto& [type, expected] : inv.count_of_type) {
            ++report.invariants_checked;
            long long actual = 0;
            for (const auto& op : seed.ops) {
                const auto* ce = std::get_if<KGOpCreateEntity>(&op);
                if (ce && ce->type == type) ++actual;
            }
            if (actual != expected) {
                violate("invariant", -1, "",
                        "count_of_type " + type + ": expected " +
                        std::to_string(expected) + ", got " +
                        std::to_string(actual));
            }
        }

        // Names come from the LOADED world (a set_property rename
        // counts), and on a listed type every instance must carry
        // one - an unnamed instance cannot be deduplicated.
        for (const auto& type : inv.unique_name_per_type) {
            ++report.invariants_checked;
            std::vector<std::string> seen;
            for (size_t i = 0; i < seed.ops.size(); ++i) {
                const EntityID id = load.created_ids[i];
                if (id == INVALID_ENTITY || world.getType(id) != type)
                    continue;
                const std::string name = world.getProperty(id, "name");
                const std::string alias = alias_of(seed.ops[i]);
                if (name.empty()) {
                    violate("invariant", static_cast<int>(i), alias,
                            "unique_name_per_type " + type +
                            ": instance has no name");
                    continue;
                }
                if (std::find(seen.begin(), seen.end(), name) !=
                    seen.end()) {
                    violate("invariant", static_cast<int>(i), alias,
                            "unique_name_per_type " + type +
                            ": duplicate name '" + name + "'");
                } else {
                    seen.push_back(name);
                }
            }
        }

        for (const auto& cov : inv.band_coverage) {
            ++report.invariants_checked;
            check_band_coverage(world, load, cov);
        }
    }

    void check_band_coverage(const KGModule& world,
                             const SeedLoadReport& load,
                             const SeedInvariants::BandCoverage& cov) {
        auto it = load.bindings.find(cov.alias);
        if (it == load.bindings.end()) {
            violate("invariant", -1, cov.alias,
                    "band_coverage @" + cov.alias +
                    ": alias is not bound by any create op");
            return;
        }
        struct Band { long long lo, hi; };
        std::vector<Band> bands;
        for (EntityID row : world.getRelated(it->second, "HAS_PART")) {
            long long lo = 0, hi = 0;
            if (!read_row_band(world, row, lo, hi)) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": row entity " +
                        std::to_string(row) + " has no band slots set");
                return;
            }
            if (hi < lo) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": malformed band ["
                        + std::to_string(lo) + ", " + std::to_string(hi) +
                        "]");
                return;
            }
            if (lo < cov.lo || hi > cov.hi) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": row band [" +
                        std::to_string(lo) + ", " + std::to_string(hi) +
                        "] outside declared range [" +
                        std::to_string(cov.lo) + ", " +
                        std::to_string(cov.hi) + "]");
                return;
            }
            bands.push_back({lo, hi});
        }
        if (bands.empty()) {
            violate("invariant", -1, cov.alias,
                    "band_coverage @" + cov.alias +
                    ": no HAS_PART rows to cover [" +
                    std::to_string(cov.lo) + ", " + std::to_string(cov.hi) +
                    "]");
            return;
        }
        std::sort(bands.begin(), bands.end(),
                  [](const Band& a, const Band& b) { return a.lo < b.lo; });
        long long cursor = cov.lo;  // next value that must be claimed
        for (const Band& b : bands) {
            if (b.lo > cursor) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": gap - value " +
                        std::to_string(cursor) + " is claimed by no row");
                return;
            }
            if (b.lo < cursor) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": overlap at " +
                        std::to_string(b.lo));
                return;
            }
            cursor = b.hi + 1;
        }
        if (cursor != cov.hi + 1) {
            violate("invariant", -1, cov.alias,
                    "band_coverage @" + cov.alias + ": gap - rows end at " +
                    std::to_string(cursor - 1) + ", declared range ends at "
                    + std::to_string(cov.hi));
        }
    }
};

}  // namespace

SeedVerifyReport verify_seed(const SeedEnvelope& seed,
                             const std::string& source_root,
                             const OntologyRegistry& registry) {
    SeedVerifyReport report;
    Checker checker{seed, source_root, registry, report, {}};

    checker.check_commit_pin();

    KGModule world(registry);
    world.setMode(KGMode::MINIMAL);
    SeedLoadReport load;
    if (!checker.check_schema(world, load)) {
        // Every other check certifies the loaded state; without one
        // the schema violation already fails the verification.
        return report;
    }

    checker.check_verbatim(world, load);
    checker.check_values(world, load);
    checker.check_invariants(world, load);
    return report;
}

}  // namespace kg
