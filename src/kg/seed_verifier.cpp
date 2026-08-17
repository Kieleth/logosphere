#include "logosphere/kg/seed_verifier.h"

#include "logosphere/text/source_document.h"
#include "logosphere/text/source_locator.h"
#include "logosphere/text/source_corpus.h"

#include "logosphere/core/dice_service.h"
#include "logosphere/kg/ingestion_ledger.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/rules/procedure_runner.h"
#include "logosphere/rules/lookup_table_selector.h"
#include "logosphere/text/source_target.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
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

std::string nearest_markdown_heading(const std::string& text,
                                     size_t position) {
    std::string nearest;
    size_t line_start = 0;
    while (line_start <= position && line_start < text.size()) {
        const size_t line_end = text.find('\n', line_start);
        const size_t length =
            (line_end == std::string::npos ? text.size() : line_end) -
            line_start;
        const std::string line = text.substr(line_start, length);
        size_t hashes = 0;
        while (hashes < line.size() && hashes < 6 &&
               line[hashes] == '#') {
            ++hashes;
        }
        if (hashes > 0 && hashes < line.size() && line[hashes] == ' ') {
            nearest = trim(line.substr(hashes + 1));
            const size_t closing = nearest.find_last_not_of('#');
            if (closing != std::string::npos && closing + 1 < nearest.size() &&
                nearest[closing] == ' ') {
                nearest.erase(closing);
            }
            nearest = trim(nearest);
        }
        if (line_end == std::string::npos) break;
        line_start = line_end + 1;
    }
    return nearest;
}

bool quote_occurs_in_section(const std::string& text,
                             const std::string& quote,
                             const std::string& section,
                             std::string& actual_section) {
    size_t position = text.find(quote);
    while (position != std::string::npos) {
        const std::string heading =
            nearest_markdown_heading(text, position);
        if (actual_section.empty()) actual_section = heading;
        if (heading == section) return true;
        position = text.find(quote, position + 1);
    }
    return false;
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

    // Books write small numbers as words, and a count spelled out is
    // still a count the text states. Cepheus aging: "Reduce three
    // physical characteristics by 2" proves the 2 and, without this,
    // proves nothing about the three. Only whole words count, so
    // "someone" never yields a one, and only the small numerals a
    // rulebook actually spells out are recognised: past twelve, books
    // use digits.
    static const std::pair<const char*, const char*> kWords[] = {
        {"one", "1"},    {"two", "2"},    {"three", "3"},
        {"four", "4"},   {"five", "5"},   {"six", "6"},
        {"seven", "7"},  {"eight", "8"},  {"nine", "9"},
        {"ten", "10"},   {"eleven", "11"}, {"twelve", "12"},
        // "both other physical characteristics" states a count of two
        // as plainly as the numeral would.
        {"both", "2"},
    };
    std::string lowered;
    lowered.reserve(q.size());
    for (char c : q) {
        lowered += static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    const auto is_word_char = [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0;
    };
    for (const auto& [word, digits] : kWords) {
        const size_t length = std::strlen(word);
        size_t at = 0;
        while ((at = lowered.find(word, at)) != std::string::npos) {
            const bool starts = at == 0 || !is_word_char(lowered[at - 1]);
            const size_t after = at + length;
            const bool ends =
                after >= lowered.size() || !is_word_char(lowered[after]);
            if (starts && ends) {
                out.emplace_back(digits);
                break;          // one witness is enough to prove it
            }
            at = after;
        }
    }
    return out;
}

std::vector<logosphere::dice::DiceExpression> dice_expressions(
    const std::string& quote) {
    std::vector<logosphere::dice::DiceExpression> out;
    for (size_t d = 0; d < quote.size(); ++d) {
        if (quote[d] != 'D' && quote[d] != 'd') continue;

        size_t count_start = d;
        while (count_start > 0 &&
               std::isdigit(static_cast<unsigned char>(
                   quote[count_start - 1]))) {
            --count_start;
        }
        if (count_start > 0 && std::isalnum(static_cast<unsigned char>(
                                   quote[count_start - 1]))) {
            continue;
        }

        size_t end = d + 1;
        const size_t sides_start = end;
        while (end < quote.size() &&
               std::isdigit(static_cast<unsigned char>(quote[end]))) {
            ++end;
        }
        if (end == sides_start) continue;

        std::string expression =
            (count_start == d ? "1" : quote.substr(count_start,
                                                     d - count_start)) +
            "D" + quote.substr(sides_start, end - sides_start);

        char modifier_sign = '\0';
        size_t modifier_size = 0;
        if (end < quote.size() &&
            (quote[end] == '+' || quote[end] == '-')) {
            modifier_sign = quote[end];
            modifier_size = 1;
        } else if (quote.compare(end, 3, "\xE2\x80\x93") == 0) {
            modifier_sign = '-';
            modifier_size = 3;
        }
        if (modifier_size > 0) {
            end += modifier_size;
            const size_t digits_start = end;
            while (end < quote.size() &&
                   std::isdigit(static_cast<unsigned char>(quote[end]))) {
                ++end;
            }
            if (end == digits_start) continue;
            expression += modifier_sign;
            expression += quote.substr(digits_start, end - digits_start);
        }

        size_t multiplier = end;
        while (multiplier < quote.size() && quote[multiplier] == ' ') {
            ++multiplier;
        }
        size_t marker_size = 0;
        if (multiplier < quote.size() &&
            (quote[multiplier] == 'x' || quote[multiplier] == 'X')) {
            marker_size = 1;
        } else if (quote.compare(multiplier, 2, "\xC3\x97") == 0) {
            marker_size = 2;
        }
        if (marker_size > 0) {
            size_t value = multiplier + marker_size;
            while (value < quote.size() && quote[value] == ' ') ++value;
            std::string digits;
            while (value < quote.size()) {
                if (std::isdigit(static_cast<unsigned char>(quote[value]))) {
                    digits += quote[value++];
                } else if (quote[value] == ',' && value + 1 < quote.size() &&
                           std::isdigit(static_cast<unsigned char>(
                               quote[value + 1]))) {
                    ++value;
                } else {
                    break;
                }
            }
            if (!digits.empty()) expression += "x" + digits;
        }

        logosphere::dice::DiceExpression parsed;
        if (logosphere::dice::DiceExpression::parse(expression, parsed)) {
            out.push_back(parsed);
        }
    }
    return out;
}

std::string dice_field(const KGModule& world, EntityID id, const char* key,
                       const char* fallback) {
    const std::string value = world.getProperty(id, key);
    return value.empty() ? fallback : value;
}

// Parse the leading markdown cell of a table-row quote into a band,
// accepting the notations the vendored book itself prints - nothing
// more:
//   "| 3 |"            -> [3, 3]
//   "| 3-5 |"          -> [3, 5]   (ASCII hyphen)
//   "| 3\xE2\x80\x93""5 |" -> [3, 5]  (en dash, U+2013)
//   "| 0 through 2 |"  -> [0, 2]
//   "| 33 or higher |" -> [33, unbounded]
//   "| 0 or lower |"   -> [unbounded, 0]
//   "| \-6 |"          -> [-6, -6] (markdown-escaped negative)
// Returns false when the quote does not begin with a band-shaped
// cell.
struct Band {
    std::optional<long long> lo;
    std::optional<long long> hi;
};

std::string format_band(const Band& band) {
    return "[" + (band.lo ? std::to_string(*band.lo) : "unbounded") +
           ", " + (band.hi ? std::to_string(*band.hi) : "unbounded") +
           "]";
}

bool parse_band_cell(const std::string& q, Band& band) {
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
    long long first = 0;
    if (!parse_num(first)) return false;
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
        long long last = 0;
        if (!parse_num(last)) return false;
        band.lo = first;
        band.hi = last;
    } else if (i < q.size() && q[i] == '+') {
        // "| 1+ |" is the same statement as "1 or higher", in the
        // shorthand the book actually prints. It appears three times
        // in the vendored SRD: the aging table's 1+, retirement pay's
        // 9+, and the difficulty table's 10+.
        ++i;
        band.lo = first;
        band.hi.reset();
    } else if (q.compare(i, 9, "or higher") == 0) {
        i += 9;
        band.lo = first;
        band.hi.reset();
    } else if (q.compare(i, 8, "or lower") == 0) {
        i += 8;
        band.lo.reset();
        band.hi = first;
    } else {
        i = after_lo;
        band.lo = first;
        band.hi = first;
    }
    skip_spaces();
    return i < q.size() && q[i] == '|';
}

// A stored band property, parsed. The schema already validated these
// as integers; a parse failure here means the property is absent.
bool read_band(const KGModule& kg, EntityID id, const char* min_key,
               const char* max_key, Band& band) {
    const std::string lo_s = kg.getProperty(id, min_key);
    const std::string hi_s = kg.getProperty(id, max_key);
    if (lo_s.empty() || hi_s.empty()) return false;
    band.lo = std::strtoll(lo_s.c_str(), nullptr, 10);
    band.hi = std::strtoll(hi_s.c_str(), nullptr, 10);
    return true;
}

bool read_lookup_band(const KGModule& kg, EntityID id, Band& band) {
    const std::string lo_s = kg.getProperty(id, "key_min");
    const std::string hi_s = kg.getProperty(id, "key_max");
    const std::string lo_flag =
        kg.getProperty(id, "key_min_unbounded");
    const std::string hi_flag =
        kg.getProperty(id, "key_max_unbounded");
    const bool lo_unbounded = lo_flag == "true" || lo_flag == "1";
    const bool hi_unbounded = hi_flag == "true" || hi_flag == "1";
    if ((lo_s.empty() != lo_unbounded) ||
        (hi_s.empty() != hi_unbounded)) {
        return false;
    }
    band.lo = lo_unbounded
                  ? std::optional<long long>{}
                  : std::optional<long long>{
                        std::strtoll(lo_s.c_str(), nullptr, 10)};
    band.hi = hi_unbounded
                  ? std::optional<long long>{}
                  : std::optional<long long>{
                        std::strtoll(hi_s.c_str(), nullptr, 10)};
    return true;
}

// The band slots for a row entity: roll_min/roll_max on TableEntry,
// key_min/key_max on LookupEntry. Returns false if the type has
// neither pair set.
bool read_row_band(const KGModule& kg, EntityID id, Band& band) {
    // A row may be open at the top, which is how the book writes its
    // last row: "| 1+ |". The flag and the number are exclusive, the
    // same contract key_max_unbounded has on a lookup row.
    const std::string top_flag = kg.getProperty(id, "roll_max_unbounded");
    if (top_flag == "true" || top_flag == "1") {
        const std::string lo = kg.getProperty(id, "roll_min");
        if (lo.empty() || !kg.getProperty(id, "roll_max").empty()) {
            return false;
        }
        band.lo = std::strtoll(lo.c_str(), nullptr, 10);
        band.hi.reset();
        return true;
    }
    return read_band(kg, id, "roll_min", "roll_max", band) ||
           read_lookup_band(kg, id, band);
}

struct Checker {
    const SeedEnvelope& seed;
    const std::string& source_root;
    const OntologyRegistry& ont;
    SeedVerifyReport& report;
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives;
    EntityID ingestion_edition_context = INVALID_ENTITY;

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
        const bool ok = ingestion_edition_context == INVALID_ENTITY
                            ? load_seed(seed, world, load)
                            : load_seed_in_edition(
                                  seed, ingestion_edition_context, world,
                                  load);
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
    // Cache: a source is parsed into the document model once.
    std::map<std::string, logosphere::text::SourceDocument> docs;

    const logosphere::text::SourceDocument* document(const std::string& rel,
                                                     std::string& why) {
        const std::string* text = source_text(rel, why);
        if (!text) return nullptr;
        auto it = docs.find(rel);
        if (it == docs.end())
            it = docs.emplace(rel,
                    logosphere::text::SourceDocument::parse_markdown(*text))
                     .first;
        return &it->second;
    }

    // Build the locator an entity claims. Its shape decides how much
    // the citation can prove: a cell citation proves one value, a
    // sentence citation proves a stated rule, and a bare quote proves
    // only that the words are somewhere under a heading.
    logosphere::text::SourceLocator locator_of(const KGModule& world,
                                               EntityID id,
                                               const std::string& rel) {
        using namespace logosphere::text;
        SourceLocator loc;
        loc.file   = rel;
        loc.exact  = world.getProperty(id, "source_quote");
        loc.table  = world.getProperty(id, "source_table");
        loc.row    = world.getProperty(id, "source_row");
        loc.column = world.getProperty(id, "source_column");
        const auto section = world.getProperty(id, "source_section");
        if (!section.empty()) loc.path = {section};
        const auto kind = world.getProperty(id, "source_kind");
        if (kind.empty())
            loc.kind = loc.table.empty() ? LocatorKind::Sentence
                                         : LocatorKind::Cell;
        else if (!kind_from_string(kind, loc.kind))
            loc.kind = LocatorKind::Sentence;
        return loc;
    }

    // What the source says at each cited entity's address, kept so the
    // VALUE check can test digits against the addressed text rather
    // than against a whole line.
    std::map<EntityID, std::string> resolved;

    bool entity_reference(const KGModule& world, EntityID owner,
                          const char* property, EntityID& out,
                          std::string& why) const {
        if (!world.hasProperty(owner, property)) {
            why = "missing required " + std::string(property);
            return false;
        }
        const std::string value = world.getProperty(owner, property);
        unsigned long long parsed = 0;
        const auto result = std::from_chars(
            value.data(), value.data() + value.size(), parsed);
        if (value.empty() || result.ec != std::errc{} ||
            result.ptr != value.data() + value.size() || parsed == 0 ||
            parsed > std::numeric_limits<EntityID>::max() ||
            !world.exists(static_cast<EntityID>(parsed))) {
            why = std::string(property) +
                  " does not reference an existing entity";
            return false;
        }
        out = static_cast<EntityID>(parsed);
        return true;
    }

    bool has_legacy_locator_field(const KGModule& world, EntityID entity,
                                  std::string& field) const {
        for (const char* candidate :
             {"source_file", "source_section", "source_kind",
              "source_table", "source_row", "source_column"}) {
            if (world.hasProperty(entity, candidate)) {
                field = candidate;
                return true;
            }
        }
        return false;
    }

    bool resolve_source_target(const KGModule& world, EntityID target,
                               std::string& text, std::string& why) {
        EntityID representation = INVALID_ENTITY;
        if (!entity_reference(world, target, "target_representation",
                              representation, why)) {
            return false;
        }
        if (!world.hasProperty(representation, "source_file")) {
            why = "target representation has no source_file";
            return false;
        }
        const std::string file =
            world.getProperty(representation, "source_file");
        if (file.empty()) {
            why = "target representation source_file is empty";
            return false;
        }
        const std::string* bytes = source_text(file, why);
        if (!bytes) return false;
        const auto result =
            logosphere::text::resolve_text_target(world, target, *bytes);
        if (!result.ok) {
            why = result.reason;
            return false;
        }
        text = result.text;
        return true;
    }

    bool exact_evidence_for_rule(const KGModule& world, EntityID rule,
                                 std::string& evidence,
                                 std::string& why) {
        const auto claims =
            world.getRelatedReverse(rule, "CLAIM_MATERIALIZES");
        if (claims.empty()) {
            why = "rule has no legacy citation and no "
                  "CLAIM_MATERIALIZES ingestion claim";
            return false;
        }

        std::unordered_set<EntityID> seen_targets;
        for (const EntityID claim : claims) {
            if (!ont.isSubtypeOf(world.getType(claim), "IngestionClaim")) {
                why = "CLAIM_MATERIALIZES source is not an IngestionClaim";
                return false;
            }
            const auto coverages =
                world.getRelated(claim, "CLAIM_SUPPORTED_BY");
            if (coverages.empty()) {
                why = "materializing claim has no CLAIM_SUPPORTED_BY "
                      "coverage";
                return false;
            }
            for (const EntityID coverage : coverages) {
                EntityID target = INVALID_ENTITY;
                if (!entity_reference(world, coverage, "coverage_target",
                                      target, why)) {
                    return false;
                }
                if (!seen_targets.insert(target).second) continue;
                std::string selected;
                if (!resolve_source_target(world, target, selected, why)) {
                    return false;
                }
                if (!evidence.empty()) evidence.push_back('\n');
                evidence += selected;
            }
        }
        if (evidence.empty()) {
            why = "materializing claims resolve to no source evidence bytes";
            return false;
        }
        return true;
    }

    void check_verbatim(const KGModule& world, const SeedLoadReport& load) {
        const auto targets = world.findByType("SourceTarget");
        if (!targets.empty()) {
            for (const EntityID target : targets) {
                std::string selected;
                std::string why;
                if (!resolve_source_target(world, target, selected, why)) {
                    violate("verbatim", -1, "",
                            "source target " + std::to_string(target) +
                                ": " + why);
                }
            }
            const auto ledger =
                reconcile_ingestion_ledger(world, targets);
            if (!ledger.ok) {
                violate("verbatim", -1, "",
                        "ingestion ledger does not reconcile: " +
                            ledger.error);
            }
        }

        for (size_t i = 0; i < seed.ops.size(); ++i) {
            const EntityID id = load.created_ids[i];
            if (id == INVALID_ENTITY) continue;
            const std::string type = world.getType(id);
            const std::string alias = alias_of(seed.ops[i]);
            const std::string quote = world.getProperty(id, "source_quote");
            if (quote.empty()) {
                if (!ont.hasProperty(type, "source_quote")) continue;
                std::string locator_field;
                if (has_legacy_locator_field(world, id, locator_field)) {
                    violate("verbatim", static_cast<int>(i), alias,
                            "exact-evidence rule retains legacy locator "
                            "field '" + locator_field + "'");
                    continue;
                }
                std::string evidence;
                std::string why;
                if (!exact_evidence_for_rule(world, id, evidence, why)) {
                    violate("verbatim", static_cast<int>(i), alias, why);
                    continue;
                }
                if (ingestion_edition_context == INVALID_ENTITY ||
                    world.getProperty(id, "origin_context") !=
                        std::to_string(ingestion_edition_context)) {
                    violate("verbatim", static_cast<int>(i), alias,
                            "exact-evidence rule origin_context must be its "
                            "ingestion edition");
                    continue;
                }
                resolved[id] = std::move(evidence);
                continue;
            }
            if (!world.getRelatedReverse(id, "CLAIM_MATERIALIZES").empty()) {
                violate("verbatim", static_cast<int>(i), alias,
                        "rule combines exact ledger evidence with the legacy "
                        "source_quote path");
                continue;
            }
            ++report.quotes_checked;
            const std::string own_file = world.getProperty(id, "source_file");
            const std::string rel =
                own_file.empty() ? seed.source.file : own_file;
            std::string why;
            const auto* doc = document(rel, why);
            if (!doc) {
                violate("verbatim", static_cast<int>(i), alias, why);
                continue;
            }
            if (world.getProperty(id, "source_section").empty()) {
                violate("verbatim", static_cast<int>(i), alias,
                        "cited entity has no source_section");
                continue;
            }
            const auto loc = locator_of(world, id, rel);
            const auto r = logosphere::text::resolve_and_match(*doc, loc);
            if (!r.ok) {
                violate("verbatim", static_cast<int>(i), alias,
                        r.reason + " [" + rel + "]");
                continue;
            }
            resolved[id] = r.text;
        }
    }

    // ----------------------------------------------------- 3. VALUE

    void check_values(const KGModule& world, const SeedLoadReport& load) {
        for (size_t i = 0; i < seed.ops.size(); ++i) {
            const EntityID id = load.created_ids[i];
            if (id == INVALID_ENTITY) continue;
            const auto evidence = resolved.find(id);
            if (evidence == resolved.end()) continue;
            const std::string& quote = evidence->second;
            const std::string type = world.getType(id);
            const std::string alias = alias_of(seed.ops[i]);
            const std::vector<std::string> tokens = number_tokens(quote);

            // Digits: every numeric rule value's absolute value must
            // equal one of the quote's number tokens. Sequence and choice
            // indices are structural ordering introduced by the data model,
            // not numbers printed by the rulebook.
            for (const auto& [key, value] :
                 world.getPropertiesWithPrefix(id, "")) {
                const PropertyDef* def = ont.findProperty(type, key);
                if (!def ||
                    (def->value_kind != PropertyValueKind::Integer &&
                     def->value_kind != PropertyValueKind::Float)) {
                    continue;
                }
                if (key == "step_index" || key == "option_index") continue;
                // A row's band is proven by the row KEY it addresses,
                // checked below, not by the cell's text. Asking the
                // cell "Electronics" to contain a 1 is asking the
                // wrong question.
                if (!world.getProperty(id, "source_row").empty() &&
                    (key == "roll_min" || key == "roll_max" ||
                     key == "key_min"  || key == "key_max")) {
                    continue;
                }
                ++report.values_checked;
                std::string digits = value;
                if (!digits.empty() &&
                    (digits[0] == '-' || digits[0] == '+')) {
                    digits.erase(0, 1);
                }
                // A citation that quotes a MULTI-COLUMN table row
                // cannot prove a number: the row carries every
                // column's, so a wrong value borrows a neighbour's.
                // This is the defect found 2026-08-10, where Scout
                // qualifying on 5+ passed because Pirate's cell said
                // Dex 5+. Such a citation must address its cell.
                if (world.getProperty(id, "source_column").empty() &&
                    quote.size() > 2 && quote.front() == '|') {
                    size_t cells = 0;
                    for (char ch : quote) if (ch == '|') ++cells;
                    if (cells > 3) {   // more than one data column
                        violate("value", static_cast<int>(i), alias,
                                type + "." + key + " = " + value +
                                ": cited to a table ROW with " +
                                std::to_string(cells - 2) + " data columns, "
                                "which cannot prove a value - any column's "
                                "number would match. Address the cell "
                                "(source_table / source_row / "
                                "source_column).");
                        continue;
                    }
                }
                // A book can state a number without writing it: "An
                // additional benefit is gained if the character held
                // rank O4" is a count of one carried by the article.
                // implied_by names the words that carry it, and they
                // become the proof in place of a token - still pinned
                // to the source, so a phrase that is not in the quote
                // fails exactly as a wrong digit does.
                const std::string implied =
                    world.getProperty(id, "implied_by");
                if (!implied.empty()) {
                    if (quote.find(implied) == std::string::npos) {
                        violate("value", static_cast<int>(i), alias,
                                type + "." + key + " = " + value +
                                ": implied_by \"" + preview(implied) +
                                "\" does not appear in the entity's quote \"" +
                                preview(quote) + "\"");
                    }
                    continue;
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

            if (type == "DiceExpression" ||
                ont.isSubtypeOf(type, "DiceExpression")) {
                const std::string count =
                    dice_field(world, id, "dice_count", "0");
                const std::string sides =
                    dice_field(world, id, "dice_sides", "0");
                const std::string modifier =
                    dice_field(world, id, "dice_modifier", "0");
                const std::string multiplier =
                    dice_field(world, id, "dice_multiplier", "1");
                std::string stored_text = count + "D" + sides;
                if (modifier != "0") {
                    if (modifier[0] != '+' && modifier[0] != '-') {
                        stored_text += "+";
                    }
                    stored_text += modifier;
                }
                if (multiplier != "1") {
                    stored_text += "x" + multiplier;
                }
                logosphere::dice::DiceExpression stored;
                if (!logosphere::dice::DiceExpression::parse(stored_text,
                                                              stored)) {
                    violate("value", static_cast<int>(i), alias,
                            type + " stores invalid engine dice expression '" +
                            stored_text + "'");
                } else {
                    const auto quoted = dice_expressions(quote);
                    const bool matched = std::any_of(
                        quoted.begin(), quoted.end(),
                        [&](const auto& candidate) {
                            return candidate.count == stored.count &&
                                   candidate.sides == stored.sides &&
                                   candidate.modifier == stored.modifier &&
                                   candidate.multiplier == stored.multiplier;
                        });
                    if (!matched) {
                        violate(
                            "value", static_cast<int>(i), alias,
                            type + " stores " + stored.to_string() +
                                ", which matches no quoted dice expression "
                                "in \"" + preview(quote) + "\"");
                    }
                }
            }

            // Band derivation for table rows. The band the book printed
            // is the row's KEY. A locator says which row it addressed,
            // so use that; a citation that quotes the whole line still
            // works by parsing its leading cell.
            const bool is_row = ont.isSubtypeOf(type, "TableEntry") ||
                                ont.isSubtypeOf(type, "LookupEntry");
            if (!is_row) continue;
            const std::string addressed_row =
                world.getProperty(id, "source_row");
            // A row the book states in PROSE has no printed band cell
            // to parse. Cepheus gives extra benefits "if the character
            // held rank O4, and two for rank O5" - three rows of a
            // real table, written as a sentence. Its key is still
            // proved, by the same rule every other number obeys: it
            // must appear in the quote.
            if (addressed_row.empty() &&
                world.getProperty(id, "source_kind") == "sentence") {
                for (const char* slot : {"key_min", "key_max"}) {
                    const std::string bound = world.getProperty(id, slot);
                    if (bound.empty()) continue;
                    if (std::find(tokens.begin(), tokens.end(), bound) ==
                        tokens.end()) {
                        violate("value", static_cast<int>(i), alias,
                                type + "." + slot + " = " + bound +
                                ": a row stated in prose must have its key "
                                "in the words that state it, and this is "
                                "not in \"" + preview(quote) + "\"");
                    }
                }
                continue;
            }
            const std::string band_text =
                addressed_row.empty() ? quote : ("| " + addressed_row + " |");
            Band cell_band;
            if (!parse_band_cell(band_text, cell_band)) {
                violate("value", static_cast<int>(i), alias,
                        type + ": cannot derive a band from the quoted "
                        "leading cell (the book's notations: '| N |', "
                        "'| N-M |', en dash, '| N through M |', "
                        "'| N or higher |'): \"" +
                        preview(band_text) + "\"");
                continue;
            }
            ++report.bands_derived;
            Band row_band;
            if (!read_row_band(world, id, row_band)) {
                violate("value", static_cast<int>(i), alias,
                        type + ": quote declares band " +
                        format_band(cell_band) + " but the row has no valid "
                        "band slots set");
                continue;
            }
            if (row_band.lo != cell_band.lo ||
                row_band.hi != cell_band.hi) {
                violate("value", static_cast<int>(i), alias,
                        type + ": band " + format_band(row_band) +
                        " does not equal the quoted cell's " +
                        format_band(cell_band));
            }
        }
    }

    // -------------------------------------------------- 4. SEMANTIC

    std::pair<int, std::string> origin_of(
        EntityID id, const SeedLoadReport& load) const {
        for (size_t i = 0; i < load.created_ids.size(); ++i) {
            if (load.created_ids[i] == id) {
                return {static_cast<int>(i), alias_of(seed.ops[i])};
            }
        }
        return {-1, ""};
    }

    void semantic_violation(EntityID id, const SeedLoadReport& load,
                            const std::string& reason) {
        const auto [op_index, alias] = origin_of(id, load);
        violate("semantic", op_index, alias, reason);
    }

    void check_lookup_table(const KGModule& world,
                            const SeedLoadReport& load, EntityID table) {
        ++report.semantics_checked;
        const std::string selector_error =
            logosphere::rules::LookupTableSelector(world).validate(table);
        if (!selector_error.empty()) {
            semantic_violation(table, load, selector_error);
            return;
        }
        const std::string declared = world.getProperty(table, "entry_type");
        const auto rows = world.getRelated(table, "HAS_PART");
        for (const EntityID row : rows) {
            ++report.semantics_checked;
            const std::string actual = world.getType(row);
            if (!ont.isSubtypeOf(actual, declared)) {
                semantic_violation(
                    row, load,
                    "LookupTable declares entry_type '" + declared +
                    "' but contains row type '" + actual + "'");
            }
        }
    }

    void check_rollable_table(const KGModule& world,
                              const SeedLoadReport& load, EntityID table) {
        ++report.semantics_checked;
        const auto rows = world.getRelated(table, "HAS_PART");
        if (rows.empty()) {
            semantic_violation(table, load,
                               "RollableTable has no HAS_PART rows");
            return;
        }
        for (const EntityID row : rows) {
            ++report.semantics_checked;
            const std::string actual = world.getType(row);
            if (!ont.isSubtypeOf(actual, "TableEntry")) {
                semantic_violation(
                    row, load,
                    "RollableTable contains non-TableEntry row type '" +
                        actual + "'");
            }
        }
    }

    void check_task_check(const KGModule& world,
                          const SeedLoadReport& load, EntityID check) {
        ++report.semantics_checked;
        const std::string table_value =
            world.getProperty(check, "modifier_table");
        const std::string attribute =
            world.getProperty(check, "attribute_ref");
        // A throw the book prints as a bare "6+" has no characteristic
        // and therefore nothing to look up. The attribute and its
        // modifier table travel together: neither, or both.
        if (table_value.empty() && attribute.empty()) return;
        if (table_value.empty() || attribute.empty()) {
            semantic_violation(
                check, load,
                "TaskCheck has " +
                    std::string(attribute.empty() ? "a modifier_table but "
                                                    "no attribute_ref"
                                                  : "an attribute_ref but "
                                                    "no modifier_table") +
                    ": a throw is modified by a characteristic through a "
                    "lookup, or by neither");
            return;
        }
        EntityID table = INVALID_ENTITY;
        try {
            size_t end = 0;
            const unsigned long parsed = std::stoul(table_value, &end);
            if (end != table_value.size() ||
                parsed > std::numeric_limits<EntityID>::max()) {
                throw std::invalid_argument("range");
            }
            table = static_cast<EntityID>(parsed);
        } catch (...) {
            semantic_violation(
                check, load,
                "TaskCheck has invalid modifier_table entity reference");
            return;
        }

        const std::string entry_type =
            world.getProperty(table, "entry_type");
        const std::string modifier_property =
            world.getProperty(check, "modifier_property");
        const PropertyDef* definition =
            ont.findProperty(entry_type, modifier_property);
        if (!definition) {
            semantic_violation(
                check, load,
                "TaskCheck has unknown modifier_property '" +
                    modifier_property + "' for lookup entry type '" +
                    entry_type + "'");
            return;
        }
        if (definition->value_kind != PropertyValueKind::Integer) {
            semantic_violation(
                check, load,
                "TaskCheck modifier_property '" + modifier_property +
                    "' for lookup entry type '" + entry_type +
                    "' is not integer");
        }
    }

    void check_ordered_parts(const KGModule& world,
                             const SeedLoadReport& load, EntityID owner,
                             const std::string& owner_type,
                             const std::string& part_type,
                             const std::string& index_property) {
        ++report.semantics_checked;
        const auto parts = world.getRelated(owner, "HAS_PART");
        if (parts.empty()) {
            semantic_violation(owner, load, owner_type + " has no " +
                                part_type + " parts");
            return;
        }

        std::vector<long long> indices;
        for (const EntityID part : parts) {
            ++report.semantics_checked;
            const std::string actual = world.getType(part);
            if (!ont.isSubtypeOf(actual, part_type)) {
                semantic_violation(
                    part, load,
                    owner_type + " contains non-" + part_type +
                        " part type '" + actual + "'");
                continue;
            }
            if (part_type == "OutcomeOption" &&
                world.getProperty(part, "option_label").empty()) {
                semantic_violation(part, load,
                                   "OutcomeOption has empty option_label");
            }
            indices.push_back(std::strtoll(
                world.getProperty(part, index_property).c_str(), nullptr, 10));
        }
        if (indices.empty()) return;

        std::sort(indices.begin(), indices.end());
        if (indices.front() != 0) {
            semantic_violation(
                owner, load,
                owner_type + " " + index_property +
                    " values must start at 0, got " +
                    std::to_string(indices.front()));
            return;
        }
        for (size_t i = 1; i < indices.size(); ++i) {
            if (indices[i] == indices[i - 1]) {
                semantic_violation(
                    owner, load,
                    owner_type + " has duplicate " + index_property + " " +
                        std::to_string(indices[i]));
                return;
            }
            if (indices[i] != indices[i - 1] + 1) {
                semantic_violation(
                    owner, load,
                    owner_type + " " + index_property +
                        " values are not contiguous: " +
                        std::to_string(indices[i - 1]) + " then " +
                        std::to_string(indices[i]));
                return;
            }
        }
    }

    void check_outcome_sequence(const KGModule& world,
                                const SeedLoadReport& load,
                                EntityID sequence) {
        check_ordered_parts(world, load, sequence, "OutcomeSequence",
                            "OutcomeStep", "step_index");
    }

    void check_outcome_choice(const KGModule& world,
                              const SeedLoadReport& load, EntityID choice) {
        const std::string authority =
            world.getProperty(choice, "choice_authority");
        if (authority != "player" && authority != "referee" &&
            authority != "procedure") {
            semantic_violation(choice, load,
                               "OutcomeChoice has unknown choice_authority '" +
                                   authority + "'");
        }
        check_ordered_parts(world, load, choice, "OutcomeChoice",
                            "OutcomeOption", "option_index");
    }

    void check_procedure(const KGModule& world,
                         const SeedLoadReport& load, EntityID procedure) {
        check_ordered_parts(world, load, procedure, "Procedure",
                            "ProcedureStep", "step_index");
        const auto parts = world.getRelated(procedure, "HAS_PART");
        std::unordered_set<EntityID> steps;
        for (const EntityID part : parts) {
            if (ont.isSubtypeOf(world.getType(part), "ProcedureStep")) {
                steps.insert(part);
            }
        }
        if (steps.empty()) return;
        if (!procedure_primitives) {
            semantic_violation(
                procedure, load,
                "Procedure cannot resolve primitive_ref values without a "
                "procedure primitive registry");
            return;
        }

        for (const EntityID step : steps) {
            ++report.semantics_checked;
            const std::string primitive =
                world.getProperty(step, "primitive_ref");
            const auto* contract =
                procedure_primitives->contract(primitive);
            if (!contract) {
                semantic_violation(step, load,
                                   "ProcedureStep names unknown primitive '" +
                                       primitive + "'");
                continue;
            }

            std::unordered_set<std::string> route_labels;
            for (const EntityID route : world.getRelated(step, "HAS_PART")) {
                ++report.semantics_checked;
                const std::string type = world.getType(route);
                if (!ont.isSubtypeOf(type, "StepRoute")) {
                    semantic_violation(
                        route, load,
                        "ProcedureStep contains non-StepRoute part type '" +
                            type + "'");
                    continue;
                }
                const std::string label =
                    world.getProperty(route, "route_label");
                if (!contract->route_labels.count(label)) {
                    semantic_violation(
                        route, load,
                        "StepRoute has undeclared route_label '" + label +
                            "' for primitive '" + primitive + "'");
                }
                if (!route_labels.insert(label).second) {
                    semantic_violation(
                        route, load,
                        "ProcedureStep has duplicate route_label '" + label +
                            "'");
                }

                const std::string next_value =
                    world.getProperty(route, "next_step");
                EntityID next = INVALID_ENTITY;
                try {
                    size_t end = 0;
                    const unsigned long parsed =
                        std::stoul(next_value, &end);
                    if (end != next_value.size() ||
                        parsed > std::numeric_limits<EntityID>::max()) {
                        throw std::invalid_argument("range");
                    }
                    next = static_cast<EntityID>(parsed);
                } catch (...) {
                    semantic_violation(route, load,
                                       "StepRoute has invalid next_step");
                    continue;
                }
                if (!steps.count(next)) {
                    semantic_violation(
                        route, load,
                        "StepRoute next_step points outside Procedure");
                }
            }
        }
    }

    void check_semantics(const KGModule& world,
                         const SeedLoadReport& load) {
        for (const EntityID id : load.created_ids) {
            if (id == INVALID_ENTITY) continue;
            const std::string type = world.getType(id);
            if (ont.isSubtypeOf(type, "LookupTable")) {
                check_lookup_table(world, load, id);
            } else if (ont.isSubtypeOf(type, "RollableTable")) {
                check_rollable_table(world, load, id);
            } else if (ont.isSubtypeOf(type, "TaskCheck")) {
                check_task_check(world, load, id);
            } else if (ont.isSubtypeOf(type, "OutcomeSequence")) {
                check_outcome_sequence(world, load, id);
            } else if (ont.isSubtypeOf(type, "OutcomeChoice")) {
                check_outcome_choice(world, load, id);
            } else if (ont.isSubtypeOf(type, "Procedure")) {
                check_procedure(world, load, id);
            }
        }
    }

    // ------------------------------------------------- 5. INVARIANT

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
            // Scoped to the WORLD, not to this file. Scanning only our
            // own ops let two seeds each create a "Gun Combat" and
            // both pass, which is how four duplicate Skills reached
            // the graph. A name is unique among everything loaded or
            // it is not unique at all.
            std::map<std::string, int> counts;
            for (EntityID id : world.findByType(type)) {
                const std::string name = world.getProperty(id, "name");
                if (!name.empty()) ++counts[name];
            }
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
                if (counts[name] > 1) {
                    violate("invariant", static_cast<int>(i), alias,
                            "unique_name_per_type " + type +
                            ": '" + name + "' exists " +
                            std::to_string(counts[name]) +
                            " times in the loaded world (another seed "
                            "may already create it; reference its canonical "
                            "@@entity/<context-key>/<exact-type>/<entity-key> "
                            "path instead)");
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
        std::vector<Band> bands;
        for (EntityID row : world.getRelated(it->second, "HAS_PART")) {
            Band band;
            if (!read_row_band(world, row, band)) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": row entity " +
                        std::to_string(row) +
                        " has no valid band slots set");
                return;
            }
            if (band.lo && band.hi && *band.hi < *band.lo) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias +
                        ": malformed band " + format_band(band));
                return;
            }
            if ((cov.lo && (!band.lo || *band.lo < *cov.lo)) ||
                (cov.hi && (!band.hi || *band.hi > *cov.hi))) {
                const Band declared{cov.lo, cov.hi};
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": row band " +
                        format_band(band) + " outside declared range " +
                        format_band(declared));
                return;
            }
            bands.push_back(std::move(band));
        }
        if (bands.empty()) {
            const Band declared{cov.lo, cov.hi};
            violate("invariant", -1, cov.alias,
                    "band_coverage @" + cov.alias +
                    ": no HAS_PART rows to cover " +
                    format_band(declared));
            return;
        }
        std::sort(bands.begin(), bands.end(),
                  [](const Band& a, const Band& b) {
                      if (a.lo.has_value() != b.lo.has_value())
                          return !a.lo.has_value();
                      if (a.lo != b.lo) return a.lo < b.lo;
                      if (a.hi.has_value() != b.hi.has_value())
                          return a.hi.has_value();
                      return a.hi < b.hi;
                  });
        if (bands.front().lo != cov.lo) {
            const std::string missing = cov.lo
                ? std::to_string(*cov.lo) : "the unbounded lower range";
            violate("invariant", -1, cov.alias,
                    "band_coverage @" + cov.alias + ": gap - value " +
                    missing + " is claimed by no row");
            return;
        }
        for (size_t i = 1; i < bands.size(); ++i) {
            const Band& previous = bands[i - 1];
            const Band& current = bands[i];
            if (!previous.hi || !current.lo ||
                *previous.hi == std::numeric_limits<long long>::max() ||
                *current.lo <= *previous.hi) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": overlap at " +
                        (current.lo ? std::to_string(*current.lo)
                                    : "unbounded lower range"));
                return;
            }
            const long long expected = *previous.hi + 1;
            if (*current.lo != expected) {
                violate("invariant", -1, cov.alias,
                        "band_coverage @" + cov.alias + ": gap - value " +
                        std::to_string(expected) +
                        " is claimed by no row");
                return;
            }
        }
        if (bands.back().hi != cov.hi) {
            violate("invariant", -1, cov.alias,
                    "band_coverage @" + cov.alias + ": gap - rows end at " +
                    (bands.back().hi ? std::to_string(*bands.back().hi)
                                     : "the unbounded upper range") +
                    ", declared range ends at " +
                    (cov.hi ? std::to_string(*cov.hi)
                            : "the unbounded upper range"));
        }
    }
};

}  // namespace

static SeedVerifyReport verify_seed_impl(
    const SeedEnvelope& seed,
    const std::string& source_root,
    const OntologyRegistry& registry,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives,
    const std::vector<const SeedEnvelope*>& prerequisites,
    const logosphere::text::SourceCorpusDeclaration* corpus,
    const logosphere::text::SourceAccess* source_access) {
    SeedVerifyReport report;
    Checker checker{seed, source_root, registry, report,
                    procedure_primitives, INVALID_ENTITY, {}};

    checker.check_commit_pin();

    KGModule world(registry);
    world.setMode(KGMode::MINIMAL);
    if (corpus != nullptr) {
        if (source_access == nullptr) {
            report.violations.push_back(
                {"schema", -1, "",
                 "edition-scoped verification has no source access"});
            return report;
        }
        const auto materialized =
            logosphere::text::materialize_source_corpus_into_kg(
                *corpus, *source_access, world);
        if (!materialized.ok) {
            report.violations.push_back(
                {"schema", -1, "",
                 "source corpus materialization failed: " +
                     materialized.reason});
            return report;
        }
        checker.ingestion_edition_context =
            materialized.ingestion_edition_context;
    }

    // What this seed depends on, loaded first and in order, so its
    // Canonical @@entity references resolve against the same world the game
    // will give them. A prerequisite that will not load is reported
    // against this seed, because from here it is the reason this one
    // cannot be checked at all.
    for (const SeedEnvelope* prerequisite : prerequisites) {
        if (!prerequisite) continue;
        SeedLoadReport prior;
        const bool loaded = checker.ingestion_edition_context == INVALID_ENTITY
                                ? load_seed(*prerequisite, world, prior)
                                : load_seed_in_edition(
                                      *prerequisite,
                                      checker.ingestion_edition_context,
                                      world, prior);
        if (!loaded) {
            report.violations.push_back(
                {"schema", prior.failed_op, "",
                 "prerequisite seed '" + prerequisite->source.file +
                     "' does not load, so this one cannot be verified: " +
                     prior.error});
            return report;
        }
    }

    SeedLoadReport load;
    if (!checker.check_schema(world, load)) {
        // Every other check certifies the loaded state; without one
        // the schema violation already fails the verification.
        return report;
    }

    checker.check_verbatim(world, load);
    checker.check_values(world, load);
    checker.check_semantics(world, load);
    checker.check_invariants(world, load);
    return report;
}

SeedVerifyReport verify_seed(const SeedEnvelope& seed,
                             const std::string& source_root,
                             const OntologyRegistry& registry,
                             const logosphere::rules::
                                 ProcedurePrimitiveRegistry*
                                     procedure_primitives,
                             const std::vector<const SeedEnvelope*>&
                                 prerequisites) {
    return verify_seed_impl(seed, source_root, registry,
                            procedure_primitives, prerequisites, nullptr,
                            nullptr);
}

SeedVerifyReport verify_seed_in_edition(
    const SeedEnvelope& seed,
    const std::string& source_root,
    const logosphere::text::SourceCorpusDeclaration& corpus,
    const logosphere::text::SourceAccess& source_access,
    const OntologyRegistry& registry,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives,
    const std::vector<const SeedEnvelope*>& prerequisites) {
    return verify_seed_impl(seed, source_root, registry,
                            procedure_primitives, prerequisites, &corpus,
                            &source_access);
}

static bool verify_and_load_seed_sequence_impl(
    const std::vector<SeedEnvelope>& seeds,
    const std::string& source_root,
    const logosphere::text::SourceCorpusDeclaration* corpus,
    const logosphere::text::SourceAccess* source_access,
    KGModule& world,
    SeedSequenceLoadReport& report,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives) {
    report = SeedSequenceLoadReport{};
    if (seeds.empty()) {
        report.ok = false;
        report.error = "seed sequence is empty";
        return false;
    }

    EntityID edition = INVALID_ENTITY;
    if (corpus != nullptr) {
        if (source_access == nullptr) {
            report.ok = false;
            report.error = "edition-scoped seed sequence has no source access";
            return false;
        }
        const auto materialized =
            logosphere::text::materialize_source_corpus_into_kg(
                *corpus, *source_access, world);
        if (!materialized.ok) {
            report.ok = false;
            report.error = "source corpus materialization failed: " +
                           materialized.reason;
            return false;
        }
        edition = materialized.ingestion_edition_context;
    }

    std::vector<const SeedEnvelope*> prerequisites;
    prerequisites.reserve(seeds.size());
    for (size_t index = 0; index < seeds.size(); ++index) {
        const SeedEnvelope& seed = seeds[index];
        SeedVerifyReport verified = verify_seed_impl(
            seed, source_root, world.getRegistry(), procedure_primitives,
            prerequisites, corpus, source_access);
        report.verifications.push_back(std::move(verified));
        const SeedVerifyReport& current = report.verifications.back();
        if (!current.ok()) {
            report.ok = false;
            report.failed_seed = static_cast<int>(index);
            std::ostringstream error;
            error << "verification failed";
            for (const auto& violation : current.violations) {
                error << ": [" << violation.check << "] ";
                if (!violation.alias.empty()) {
                    error << "@" << violation.alias << " ";
                }
                error << violation.reason;
            }
            report.error = error.str();
            return false;
        }
        ++report.seeds_verified;

        SeedLoadReport loaded;
        const bool loaded_ok = edition == INVALID_ENTITY
                                   ? load_seed(seed, world, loaded)
                                   : load_seed_in_edition(
                                         seed, edition, world, loaded);
        if (!loaded_ok) {
            report.ok = false;
            report.failed_seed = static_cast<int>(index);
            report.error = "load failed: " + loaded.error;
            return false;
        }
        ++report.seeds_loaded;
        prerequisites.push_back(&seed);
    }
    return true;
}

bool verify_and_load_seed_sequence(
    const std::vector<SeedEnvelope>& seeds,
    const std::string& source_root,
    KGModule& world,
    SeedSequenceLoadReport& report,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives) {
    return verify_and_load_seed_sequence_impl(
        seeds, source_root, nullptr, nullptr, world, report,
        procedure_primitives);
}

bool verify_and_load_seed_sequence_in_edition(
    const std::vector<SeedEnvelope>& seeds,
    const std::string& source_root,
    const logosphere::text::SourceCorpusDeclaration& corpus,
    const logosphere::text::SourceAccess& source_access,
    KGModule& world,
    SeedSequenceLoadReport& report,
    const logosphere::rules::ProcedurePrimitiveRegistry*
        procedure_primitives) {
    return verify_and_load_seed_sequence_impl(
        seeds, source_root, &corpus, &source_access, world, report,
        procedure_primitives);
}

}  // namespace kg
