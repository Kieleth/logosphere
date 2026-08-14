#ifndef LOGOSPHERE_RULES_PLANNED_WORLD_H
#define LOGOSPHERE_RULES_PLANNED_WORLD_H

// A read view over the world as the current plan will leave it.
//
// A typed outcome resolves into an ordered list of KG ops that are
// applied atomically at the very end. Between the first op being
// planned and the batch committing, the graph still reads as it did
// before the rule started, and the event bus is detached so nothing is
// announced either (see kg_ops_transaction.cpp). Both are guarantees,
// not gaps: a reader that saw a change which was later rolled back
// would be holding a fact the world never contained.
//
// The cost of that guarantee is that anything reasoning mid-rule about
// what the SAME rule has already decided cannot ask the graph and
// cannot subscribe for it. It reads here instead.
//
// This is the shape EF Core calls a LocalView: a second read surface
// beside the unchanged primary one, projecting pending changes over
// the committed base, chosen explicitly by the code that wants it.
// Deliberately NOT an overlay installed underneath KGModule. Physics,
// rendering and every other consumer must keep reading committed
// state and must never learn that a rules batch is in flight. PhysX
// shipped the other design, promising reads reflected changes during
// simulation, and removed it in PhysX 5.
//
// INVARIANT: a plan is ADDITIVE. apply_kg_ops_atomically refuses
// destroy_entity and play_cinematic, so no op in a plan removes an
// entity, a relation or a property. That is the single fact that makes
// this view cheap: there are no tombstones, and absence is never
// ambiguous. If destroy ever becomes plannable, every method below
// needs a deletion case. test_planned_world locks the refusal so that
// day cannot arrive quietly.
//
// ORDERING: a read sees every op planned BEFORE it and never its own.
// Property lookup scans the plan in reverse and takes the newest
// write, which is the "see all prior commands, not your own" rule
// PostgreSQL implements with a command counter to avoid the Halloween
// problem. Here it falls out of the scan direction; it is written down
// so it stops being an accident.
//
// LIFETIME: holds two references and owns nothing. It is valid only
// while the plan that produced it is alive. Do not store it. Anything
// that must outlive the plan (a choice handed to a player or to a
// model, and answered later) takes copied values instead, which is why
// AttributeSelectionRequest carries vectors rather than a view.

#include "logosphere/kg/kg_ops.h"

#include <string>
#include <vector>

namespace kg { class KGModule; }

namespace logosphere::rules {

struct OutcomePlan;

class PlannedWorld {
public:
    PlannedWorld(const kg::KGModule& world, const OutcomePlan& plan)
        : world_(world), plan_(plan) {}

    PlannedWorld(const PlannedWorld&) = default;
    PlannedWorld& operator=(const PlannedWorld&) = delete;

    // --- what the world will look like ---

    // The newest planned value, else the committed one, else empty.
    // A symbolic ref that no op has written falls back to the
    // properties of the pending create that named it.
    std::string property(const kg::EntityRef& target,
                         const std::string& key) const;
    bool has_property(const kg::EntityRef& target,
                      const std::string& key) const;

    // The committed type, or the type of a pending create for a
    // symbolic ref. Empty when neither knows it.
    std::string type_of(const kg::EntityRef& target) const;
    bool exists(const kg::EntityRef& target) const;

    // Committed targets first, then pending ones in plan order. The
    // order is part of the contract because callers filter on it.
    std::vector<kg::EntityRef> related(const kg::EntityRef& from,
                                       const std::string& relation) const;

    // --- what this plan has already decided ---

    // True when any op in the plan writes this property on this
    // target. Deliberately NOT derivable from property(): a write of
    // the value the target already held is invisible to a value read
    // and visible here. Collapsing the two is a real bug, and
    // test_planned_world has the case that catches it.
    bool was_written(const kg::EntityRef& target,
                     const std::string& key) const;

    const kg::KGModule& committed() const { return world_; }

private:
    const kg::KGModule& world_;
    const OutcomePlan& plan_;
};

}  // namespace logosphere::rules

#endif  // LOGOSPHERE_RULES_PLANNED_WORLD_H
