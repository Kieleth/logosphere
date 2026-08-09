#include "logosphere/kg/seed_verifier.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
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

// The "as" binder of a create op; empty for everything else.
std::string alias_of(const KGOp& op) {
    if (const auto* ce = std::get_if<KGOpCreateEntity>(&op)) return ce->as;
    return "";
}

std::string preview(const std::string& s, size_t n = 48) {
    return s.size() <= n ? s : s.substr(0, n) + "...";
}

// Parse the leading markdown cell of a table-row quote: "| 3 |"
// gives [3, 3], "| 3-5 |" gives [3, 5]. Returns false when the
// quote does not begin with a band-shaped cell.
bool parse_band_cell(const std::string& q, long long& lo, long long& hi) {
    size_t i = 0;
    auto skip_spaces = [&] { while (i < q.size() && q[i] == ' ') ++i; };
    auto parse_int = [&](long long& out) {
        size_t start = i;
        while (i < q.size() && q[i] >= '0' && q[i] <= '9') ++i;
        if (i == start) return false;
        out = std::strtoll(q.substr(start, i - start).c_str(), nullptr, 10);
        return true;
    };
    if (i >= q.size() || q[i] != '|') return false;
    ++i;
    skip_spaces();
    if (!parse_int(lo)) return false;
    if (i < q.size() && q[i] == '-') {
        ++i;
        if (!parse_int(hi)) return false;
    } else {
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
    const OntologyRegistry& ont;
    SeedVerifyReport& report;

    void violate(const std::string& check, int op_index,
                 const std::string& alias, const std::string& reason) {
        report.violations.push_back({check, op_index, alias, reason});
    }

    // -------------------------------------------------- 1. VERBATIM

    void check_verbatim(const std::string& source_root) {
        const std::string path = source_root + "/" + seed.source.file;
        const std::string text = slurp(path);
        if (text.empty()) {
            violate("verbatim", -1, "",
                    "cannot read source file " + path);
            return;
        }
        for (size_t i = 0; i < seed.ops.size(); ++i) {
            const auto* ce = std::get_if<KGOpCreateEntity>(&seed.ops[i]);
            if (!ce) continue;
            for (const auto& [k, v] : ce->properties) {
                if (k != "source_quote" || v.empty()) continue;
                ++report.quotes_checked;
                if (text.find(v) == std::string::npos) {
                    violate("verbatim", static_cast<int>(i), ce->as,
                            "source_quote is not a byte-exact substring "
                            "of " + seed.source.file + ": \"" +
                            preview(v) + "\"");
                }
            }
        }
    }

    // ---------------------------------------------------- 2. SCHEMA

    // Loads into `world` through the seed loader; the loaded state
    // feeds the VALUE and INVARIANT checks below.
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

    // ----------------------------------------------------- 3. VALUE

    void check_values(const KGModule& world, const SeedLoadReport& load) {
        for (size_t i = 0; i < seed.ops.size(); ++i) {
            const EntityID id = load.created_ids[i];
            if (id == INVALID_ENTITY) continue;
            const std::string quote = world.getProperty(id, "source_quote");
            if (quote.empty()) continue;
            const std::string type = world.getType(id);
            const std::string alias = alias_of(seed.ops[i]);

            // Digits: every numeric slot's absolute value must appear
            // in the entity's own quote.
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
                    quote.find(digits) == std::string::npos) {
                    violate("value", static_cast<int>(i), alias,
                            type + "." + key + " = " + value +
                            ": digits '" + digits + "' not found in the "
                            "entity's quote \"" + preview(quote) + "\"");
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
                        "leading cell (expected '| N |' or '| N-M |'): \"" +
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

        for (const auto& type : inv.unique_name_per_type) {
            ++report.invariants_checked;
            std::vector<std::string> seen;
            for (size_t i = 0; i < seed.ops.size(); ++i) {
                const auto* ce = std::get_if<KGOpCreateEntity>(&seed.ops[i]);
                if (!ce || ce->type != type) continue;
                for (const auto& [k, v] : ce->properties) {
                    if (k != "name" || v.empty()) continue;
                    if (std::find(seen.begin(), seen.end(), v) !=
                        seen.end()) {
                        violate("invariant", static_cast<int>(i), ce->as,
                                "unique_name_per_type " + type +
                                ": duplicate name '" + v + "'");
                    } else {
                        seen.push_back(v);
                    }
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
            if (b.hi < b.lo) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": malformed band ["
                        + std::to_string(b.lo) + ", " +
                        std::to_string(b.hi) + "]");
                return;
            }
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
                    "band_coverage @" + cov.alias + ": rows end at " +
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
    Checker checker{seed, registry, report};

    checker.check_verbatim(source_root);

    KGModule world(registry);
    world.setMode(KGMode::MINIMAL);
    SeedLoadReport load;
    if (!checker.check_schema(world, load)) {
        // Without a loaded world the value and invariant checks have
        // nothing sound to read; the schema violation already fails
        // the verification.
        return report;
    }

    checker.check_values(world, load);
    checker.check_invariants(world, load);
    return report;
}

}  // namespace kg
