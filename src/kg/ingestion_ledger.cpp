#include "logosphere/kg/ingestion_ledger.h"

#include "logosphere/kg/kg_module.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kg {
namespace {

IngestionLedgerReport fail(IngestionLedgerReport report,
                           const std::string& error) {
    report.ok = false;
    report.error = error;
    return report;
}

bool parse_nonnegative(const KGModule& world, EntityID entity,
                       const char* property, unsigned long long& out,
                       std::string& error) {
    if (!world.hasProperty(entity, property)) {
        error = "entity " + std::to_string(entity) +
                ": missing required property '" + property + "'";
        return false;
    }
    const std::string value = world.getProperty(entity, property);
    if (value.empty() || value.front() == '-') {
        error = "entity " + std::to_string(entity) + "." + property +
                ": expected a non-negative integer";
        return false;
    }
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), out);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size()) {
        error = "entity " + std::to_string(entity) + "." + property +
                ": expected a non-negative integer";
        return false;
    }
    return true;
}

bool parse_reference(const KGModule& world, EntityID entity,
                     const char* property, const char* expected_type,
                     EntityID& out, std::string& error) {
    unsigned long long parsed = 0;
    if (!parse_nonnegative(world, entity, property, parsed, error))
        return false;
    if (parsed == 0 || parsed > std::numeric_limits<EntityID>::max()) {
        error = "entity " + std::to_string(entity) + "." + property +
                ": expected an entity id";
        return false;
    }
    out = static_cast<EntityID>(parsed);
    if (!world.exists(out)) {
        error = "entity " + std::to_string(entity) + "." + property +
                ": references missing entity " + std::to_string(out);
        return false;
    }
    if (!world.getRegistry().isSubtypeOf(world.getType(out), expected_type)) {
        error = "entity " + std::to_string(entity) + "." + property +
                ": entity " + std::to_string(out) + " is " +
                world.getType(out) + ", not " + expected_type;
        return false;
    }
    return true;
}

bool require_decision_metadata(const KGModule& world, EntityID decision,
                               std::string& error) {
    if (!world.hasProperty(decision, "event_type") ||
        world.getProperty(decision, "event_type") != "ARBITER_DECISION") {
        error = "decision " + std::to_string(decision) +
                ": event_type must be ARBITER_DECISION";
        return false;
    }
    for (const char* property :
         {"decision_question", "decision_reason", "arbiter"}) {
        if (!world.hasProperty(decision, property)) {
            error = "decision " + std::to_string(decision) +
                    ": missing required property '" + property + "'";
            return false;
        }
    }
    if (world.getProperty(decision, "decision_question").empty() ||
        world.getProperty(decision, "arbiter").empty()) {
        error = "decision " + std::to_string(decision) +
                ": question and arbiter must be non-empty";
        return false;
    }
    return true;
}

using DecisionHistory =
    std::unordered_map<EntityID, std::vector<std::pair<unsigned long long,
                                                       EntityID>>>;

bool collect_histories(const KGModule& world, const char* decision_type,
                       const char* subject_type,
                       const std::unordered_set<EntityID>& subjects,
                       DecisionHistory& histories, std::size_t& count,
                       std::string& error) {
    for (const EntityID decision : world.findByType(decision_type)) {
        ++count;
        if (!require_decision_metadata(world, decision, error)) return false;
        EntityID subject = INVALID_ENTITY;
        if (!parse_reference(world, decision, "decision_subject", subject_type,
                             subject, error))
            return false;
        if (!subjects.count(subject)) {
            error = std::string(decision_type) + " " +
                    std::to_string(decision) +
                    " refers to a subject outside this ledger";
            return false;
        }
        unsigned long long sequence = 0;
        if (!parse_nonnegative(world, decision, "decision_sequence", sequence,
                               error))
            return false;
        histories[subject].emplace_back(sequence, decision);
    }
    return true;
}

bool latest_decision(DecisionHistory& histories, EntityID subject,
                     const char* label, EntityID& latest,
                     std::string& error) {
    auto found = histories.find(subject);
    if (found == histories.end() || found->second.empty()) {
        error = std::string(label) + " " + std::to_string(subject) +
                ": missing decision history";
        return false;
    }
    auto& entries = found->second;
    std::sort(entries.begin(), entries.end());
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].first != index) {
            error = std::string(label) + " " + std::to_string(subject) +
                    ": decision sequence must be contiguous from zero";
            return false;
        }
    }
    latest = entries.back().second;
    return true;
}

bool has_property(const KGModule& world, EntityID entity,
                  const char* property) {
    return world.hasProperty(entity, property);
}

bool check_claim_disposition(const KGModule& world, EntityID claim,
                             EntityID decision, std::string& error) {
    if (!world.hasProperty(decision, "claim_disposition")) {
        error = "claim decision " + std::to_string(decision) +
                ": missing claim_disposition";
        return false;
    }
    const std::string disposition =
        world.getProperty(decision, "claim_disposition");
    const bool has_gap = has_property(world, decision, "claim_gap_kind");
    const bool has_related = has_property(world, decision, "related_claim");
    const auto materialized =
        world.getRelated(claim, "CLAIM_MATERIALIZES");

    if (has_gap) {
        const std::string gap = world.getProperty(decision, "claim_gap_kind");
        if (gap != "ONTOLOGY_GAP" && gap != "RULE_LANGUAGE_GAP" &&
            gap != "SOURCE_GAP") {
            error = "claim decision " + std::to_string(decision) +
                    ": unknown gap kind '" + gap + "'";
            return false;
        }
    }

    EntityID related = INVALID_ENTITY;
    if (has_related &&
        !parse_reference(world, decision, "related_claim", "IngestionClaim",
                         related, error))
        return false;
    if (related == claim) {
        error = "claim decision " + std::to_string(decision) +
                ": related claim cannot be itself";
        return false;
    }

    if (disposition == "MATERIALIZED") {
        if (materialized.empty()) {
            error = "claim " + std::to_string(claim) +
                    ": MATERIALIZED has no CLAIM_MATERIALIZES result";
            return false;
        }
        if (has_gap || has_related) {
            error = "claim " + std::to_string(claim) +
                    ": MATERIALIZED forbids gap kind and related claim";
            return false;
        }
        return true;
    }
    if (disposition == "PARTIAL") {
        if (!has_gap) {
            error = "claim " + std::to_string(claim) +
                    ": PARTIAL requires a typed gap kind";
            return false;
        }
        if (materialized.empty()) {
            error = "claim " + std::to_string(claim) +
                    ": PARTIAL has no CLAIM_MATERIALIZES result";
            return false;
        }
        if (has_related) {
            error = "claim " + std::to_string(claim) +
                    ": PARTIAL forbids a related claim";
            return false;
        }
        return true;
    }
    if (disposition == "RAISED") {
        if (!has_gap) {
            error = "claim " + std::to_string(claim) +
                    ": RAISED requires a typed gap kind";
            return false;
        }
        if (!materialized.empty() || has_related) {
            error = "claim " + std::to_string(claim) +
                    ": RAISED forbids materialized results and related claim";
            return false;
        }
        return true;
    }
    if (disposition == "DUPLICATE") {
        if (!has_related) {
            error = "claim " + std::to_string(claim) +
                    ": DUPLICATE requires a related claim";
            return false;
        }
        if (has_gap || !materialized.empty()) {
            error = "claim " + std::to_string(claim) +
                    ": DUPLICATE forbids gap kind and materialized results";
            return false;
        }
        return true;
    }
    if (disposition == "SUPERSEDED") {
        if (!has_related) {
            error = "claim " + std::to_string(claim) +
                    ": SUPERSEDED requires a replacement claim";
            return false;
        }
        if (has_gap || !materialized.empty()) {
            error = "claim " + std::to_string(claim) +
                    ": SUPERSEDED forbids gap kind and materialized results";
            return false;
        }
        return true;
    }
    if (disposition == "CONTRADICTORY") {
        if (!has_related) {
            error = "claim " + std::to_string(claim) +
                    ": CONTRADICTORY requires a related claim";
            return false;
        }
        if (has_gap || materialized.empty()) {
            error = "claim " + std::to_string(claim) +
                    ": CONTRADICTORY requires materialized content and no gap";
            return false;
        }
        return true;
    }

    error = "claim decision " + std::to_string(decision) +
            ": unknown disposition '" + disposition + "'";
    return false;
}

}  // namespace

IngestionLedgerReport reconcile_ingestion_ledger(
    const KGModule& world,
    const std::vector<EntityID>& enumerated_targets) {
    IngestionLedgerReport report;

    std::unordered_set<EntityID> expected;
    for (const EntityID target : enumerated_targets) {
        if (!world.exists(target) ||
            !world.getRegistry().isSubtypeOf(world.getType(target),
                                             "SourceTarget")) {
            return fail(report, "enumerated source target " +
                                    std::to_string(target) +
                                    " is missing or has the wrong type");
        }
        if (!expected.insert(target).second) {
            return fail(report, "enumerated source target " +
                                    std::to_string(target) +
                                    " appears more than once");
        }
    }

    const auto coverages = world.findByType("SourceCoverage");
    const auto claims = world.findByType("IngestionClaim");
    report.coverage_count = coverages.size();
    report.claim_count = claims.size();
    const std::unordered_set<EntityID> coverage_set(coverages.begin(),
                                                    coverages.end());
    const std::unordered_set<EntityID> claim_set(claims.begin(), claims.end());

    std::unordered_map<EntityID, EntityID> coverage_by_target;
    for (const EntityID coverage : coverages) {
        EntityID target = INVALID_ENTITY;
        if (!parse_reference(world, coverage, "coverage_target", "SourceTarget",
                             target, report.error))
            return fail(report, report.error);
        if (!expected.count(target)) {
            return fail(report, "coverage " + std::to_string(coverage) +
                                    " targets a leaf outside the enumeration");
        }
        if (!coverage_by_target.emplace(target, coverage).second) {
            return fail(report, "duplicate coverage for source target " +
                                    std::to_string(target));
        }
    }
    for (const EntityID target : expected) {
        if (!coverage_by_target.count(target)) {
            return fail(report, "missing coverage for enumerated source target " +
                                    std::to_string(target));
        }
    }

    for (const EntityID claim : claims) {
        if (!world.hasProperty(claim, "claim_statement") ||
            world.getProperty(claim, "claim_statement").empty()) {
            return fail(report, "claim " + std::to_string(claim) +
                                    ": missing non-empty claim_statement");
        }
        const auto evidence = world.getRelated(claim, "CLAIM_SUPPORTED_BY");
        if (evidence.empty()) {
            return fail(report, "claim " + std::to_string(claim) +
                                    " has no CLAIM_SUPPORTED_BY coverage");
        }
        for (const EntityID coverage : evidence) {
            if (!coverage_set.count(coverage)) {
                return fail(report, "claim " + std::to_string(claim) +
                                        " cites coverage outside this ledger");
            }
        }
    }

    DecisionHistory coverage_histories;
    DecisionHistory claim_histories;
    if (!collect_histories(world, "CoverageDecision", "SourceCoverage",
                           coverage_set, coverage_histories,
                           report.decision_count, report.error))
        return fail(report, report.error);
    if (!collect_histories(world, "ClaimDecision", "IngestionClaim",
                           claim_set, claim_histories, report.decision_count,
                           report.error))
        return fail(report, report.error);

    for (const EntityID coverage : coverages) {
        EntityID decision = INVALID_ENTITY;
        if (!latest_decision(coverage_histories, coverage, "coverage",
                             decision, report.error))
            return fail(report, report.error);
        if (!world.hasProperty(decision, "coverage_judgement")) {
            return fail(report, "coverage decision " +
                                    std::to_string(decision) +
                                    ": missing coverage_judgement");
        }
        const std::string judgement =
            world.getProperty(decision, "coverage_judgement");
        const std::size_t count =
            world.getRelatedReverse(coverage, "CLAIM_SUPPORTED_BY").size();
        if (count == 0 && judgement != "NO_RULE_CONTENT") {
            return fail(report, "coverage " + std::to_string(coverage) +
                                    " has zero claims without a current "
                                    "NO_RULE_CONTENT decision");
        }
        if (count > 0 && judgement != "CLAIMS_PRESENT") {
            return fail(report, "coverage " + std::to_string(coverage) +
                                    " has claims but its current decision is not "
                                    "CLAIMS_PRESENT");
        }
    }

    for (const EntityID claim : claims) {
        EntityID decision = INVALID_ENTITY;
        if (!latest_decision(claim_histories, claim, "claim", decision,
                             report.error))
            return fail(report, report.error);
        if (!check_claim_disposition(world, claim, decision, report.error))
            return fail(report, report.error);
    }

    report.ok = true;
    return report;
}

}  // namespace kg
