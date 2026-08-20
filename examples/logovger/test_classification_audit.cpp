// The audit cannot be forgotten.
//
// The extractor reads the book mechanically, which is right: a model
// that retypes a number produces a rule that is wrong while the prose
// still reads fine. But deciding what a cell MEANS is judgement, and
// for a while that judgement was checked by nothing except the regexes
// that made it. The audit tool fixed that by having a model classify
// the same values independently.
//
// A tool nobody runs is worse than no tool, because its existence
// reads as a guarantee. So this test compares the shipped seed against
// the shipped audit and fails when they drift:
//
//   * a cell value in the seed that the audit has never seen
//   * a value the audit classified differently from the seed
//   * an audit taken against a different revision of the source
//
// Any of those means someone changed extraction and did not re-run the
// audit. It needs no network: it reads two files that are both in the
// repository, so it runs everywhere the rest of the suite does.

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    if (ok) {
        std::printf("  ok    %s\n", what.c_str());
        return;
    }
    ++failures;
    std::printf("  FAIL  %s%s%s\n", what.c_str(),
                detail.empty() ? "" : "  --  ", detail.c_str());
}

std::string slurp(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

// Minimal string-scanning rather than a JSON dependency: both files
// are generated with a stable shape, and this test only needs to find
// values and their neighbouring fields.
std::string field_after(const std::string& text, size_t from,
                        const std::string& key) {
    const auto at = text.find("\"" + key + "\"", from);
    if (at == std::string::npos) return "";
    const auto colon = text.find(':', at);
    if (colon == std::string::npos) return "";
    const auto open = text.find('"', colon);
    if (open == std::string::npos) return "";
    std::string out;
    for (size_t i = open + 1; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            out.push_back(text[i]);
            out.push_back(text[i + 1]);
            ++i;
            continue;
        }
        if (text[i] == '"') break;
        out.push_back(text[i]);
    }
    return out;
}

// Every exact-evidenced outcome in the seed, as quote -> classification,
// using the same outcome-type mapping the audit tool uses. The value cell is
// the final support of each generated Career Tables claim.
std::map<std::string, std::string> seed_classifications(
    const std::string& seed) {
    static const std::map<std::string, std::string> kinds = {
        {"AdvanceSkill", "SKILL"},
        {"ModifyAttribute", "CHARACTERISTIC"},
        {"GainFixedMoney", "MONEY"},
        {"GainPossession", "POSSESSION"},
        {"NoEffect", "NOTHING"}};
    static const std::set<std::string> audited = {
        "Personal Development", "Specialist", "Adv Education",
        "Cash Benefits", "Cost Benefits", "Material Benefits",
        "Service Skills"};

    std::map<std::string, std::string> entity_types;
    std::map<std::string, std::string> claim_statements;
    std::map<std::string, std::string> quotes;
    std::map<std::string, std::string> target_quotes;
    std::map<std::string, std::string> coverage_targets;
    std::map<std::string, std::vector<std::string>> supports;
    std::vector<std::pair<std::string, std::string>> materializations;
    size_t at = 0;
    while ((at = seed.find("\"op\"", at)) != std::string::npos) {
        const auto next = seed.find("\"op\"", at + 1);
        const auto end = next == std::string::npos ? seed.size() : next;
        const std::string op = seed.substr(at, end - at);
        const std::string operation = field_after(op, 0, "op");
        if (operation == "create_entity") {
            const std::string alias = field_after(op, 0, "as");
            const std::string type = field_after(op, 0, "type");
            entity_types[alias] = type;
            if (type == "IngestionClaim") {
                claim_statements[alias] =
                    field_after(op, 0, "claim_statement");
            } else if (type == "TextQuoteSelector") {
                quotes[alias] = field_after(op, 0, "source_quote_exact");
            } else if (type == "SourceTarget") {
                target_quotes[alias] =
                    field_after(op, 0, "target_quote_selector");
            } else if (type == "SourceCoverage") {
                coverage_targets[alias] =
                    field_after(op, 0, "coverage_target");
            }
        } else if (operation == "set_relation") {
            const std::string relation = field_after(op, 0, "relation");
            const std::string from = field_after(op, 0, "from");
            const std::string to = field_after(op, 0, "to");
            if (relation == "CLAIM_SUPPORTED_BY") {
                supports[from].push_back(to);
            } else if (relation == "CLAIM_MATERIALIZES") {
                materializations.emplace_back(from, to);
            }
        }
        at = end;
    }

    std::map<std::string, std::string> out;
    for (const auto& [claim, outcome] : materializations) {
        const auto kind = kinds.find(entity_types[outcome]);
        if (kind == kinds.end()) continue;
        const auto& statement = claim_statements[claim];
        bool audited_table = false;
        for (const auto& table : audited) {
            if (statement.rfind(table + " row ", 0) == 0) {
                audited_table = true;
                break;
            }
        }
        if (!audited_table || supports[claim].empty()) continue;
        const auto& coverage = supports[claim].back();
        const auto& target = coverage_targets[coverage];
        const auto& quote = quotes[target_quotes[target]];
        const auto prior = out.find(quote);
        if (prior != out.end() && prior->second != kind->second) {
            out[quote] = "CONFLICT";
        } else if (!quote.empty()) {
            out[quote] = kind->second;
        }
    }
    return out;
}

}  // namespace

int main() {
    const std::string root = std::string(LOGOSPHERE_SOURCE_DIR) +
                             "/examples/logovger/";
    const std::string seed =
        slurp(root + "seeds/cepheus_book1_career_tables.json");
    const std::string audit =
        slurp(root + "seeds/classification_audit.json");

    std::printf("\n=== the classification audit is not stale ===\n\n");
    check(!seed.empty(), "the career tables seed is readable");
    check(!audit.empty(),
          "the audit exists at all",
          "run examples/logovger/tools/audit_classifications.py");
    if (seed.empty() || audit.empty()) {
        std::printf("\n%d checks FAILED\n\n", ++failures);
        return 1;
    }

    // Same revision of the book, or the audit says nothing about what
    // shipped.
    const auto seed_commit = field_after(seed, 0, "commit");
    const auto audit_commit = field_after(audit, 0, "commit");
    check(!seed_commit.empty() && seed_commit == audit_commit,
          "the audit was taken against the source the seed cites",
          "seed " + seed_commit + " vs audit " + audit_commit);

    const auto classifications = seed_classifications(seed);
    check(classifications.size() >= 20,
          "the seed still carries exact-evidenced outcomes to audit",
          std::to_string(classifications.size()) + " found");

    // Only the classifications block. A settled disagreement records
    // the same value again under "adjudicated" with the reasoning, and
    // matching that instead reads the wrong field entirely.
    const auto block = audit.find("\"classifications\"");
    check(block != std::string::npos,
          "the audit has a classifications block");

    size_t unseen = 0, contradicted = 0;
    std::string first_unseen, first_contradicted;
    for (const auto& [quote, kind] : classifications) {
        const auto at = audit.find("\"" + quote + "\"", block);
        if (at == std::string::npos) {
            if (first_unseen.empty()) first_unseen = quote;
            ++unseen;
            continue;
        }
        const auto colon = audit.find(':', at);
        const auto open = audit.find('"', colon);
        const auto close = audit.find('"', open + 1);
        const std::string recorded =
            audit.substr(open + 1, close - open - 1);
        if (recorded != kind) {
            if (first_contradicted.empty()) {
                first_contradicted = quote + ": seed says " + kind +
                                     ", audit recorded " + recorded;
            }
            ++contradicted;
        }
    }

    check(unseen == 0,
          "every cell value the seed classifies has been audited",
          std::to_string(unseen) + " unaudited, first: " + first_unseen +
              ". Re-run tools/audit_classifications.py");
    check(contradicted == 0,
          "no value is classified one way in the seed and another in "
          "the audit",
          first_contradicted);

    std::printf("\n%s\n\n",
                failures == 0
                    ? "the audit covers what shipped"
                    : (std::to_string(failures) + " checks FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
