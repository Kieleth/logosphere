// Walking a KG relation backwards.
//
// getRelated(creature, "HAS_PART") lists the parts. Nothing could take a
// part back to its creature, at any price: relations are stored with an
// outgoing index and an incoming index, and only the outgoing one had a
// reader. The incoming index has been maintained since relations existed
// (kg_core.cpp: createRelation appends, destroyRelation and destroyEntity
// prune) and was queried by nothing.
//
// Issue #36 needs it. A contact arrives as two particle indices; getting
// from there to "the predator bit the prey" runs
//   render index -> KGParticleID -> body part entity -> the creature
// and that last hop is this.
//
// Because the index was live but unread, the risk here is not "does the
// query work" but "was the index ever actually correct". So the negative
// cases carry the weight: destroyed relations, destroyed entities, and
// the direction confusion that makes a reverse query silently return the
// forward answer.
//
// Headless-safe: KG only.
//
// Usage:
//   ./build/test_kg_reverse_relations

#undef NDEBUG

#include "generated/logosphere_ontology_registry.h"
#include "logosphere/kg/kg_module.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool ok, const std::string& msg) {
    if (ok) { tests_passed++; }
    else { tests_failed++; std::cout << "  FAIL: " << msg << std::endl; }
}

namespace {

bool contains(const std::vector<kg::EntityID>& v, kg::EntityID id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

// KGModule owns a unique_ptr, so it is neither copyable nor movable:
// it has to be built in place.
struct Fixture {
    kg::KGModule kg{logosphere::ontology::registry()};
    Fixture() { kg.setMode(kg::KGMode::MINIMAL); }
};

// An entity with two parts, which is the shape the contact resolver
// actually walks.
struct Rig {
    kg::EntityID creature = 0;
    kg::EntityID head = 0;
    kg::EntityID torso = 0;
};

Rig build(kg::KGModule& kg) {
    Rig r;
    r.creature = kg.createEntity("Humanoid");
    r.head     = kg.createEntity("Head");
    r.torso    = kg.createEntity("Torso");
    kg.createRelation(r.creature, "HAS_PART", r.head);
    kg.createRelation(r.creature, "HAS_PART", r.torso);
    return r;
}

// ------------------------------------------------------------ the basics

void test_a_part_finds_its_owner() {
    Fixture fx; auto& kg = fx.kg;
    Rig r = build(kg);

    auto owners = kg.getRelatedReverse(r.head, "HAS_PART");
    std::cout << "  [measure] reverse HAS_PART from the head: " << owners.size()
              << " owner(s)" << std::endl;
    check(owners.size() == 1, "a part has exactly one owner (" +
          std::to_string(owners.size()) + ")");
    check(!owners.empty() && owners[0] == r.creature,
          "and it is the creature that declared the part");

    // The forward query still answers the forward question. If adding the
    // reverse index reader broke this, everything downstream is wrong.
    auto parts = kg.getRelated(r.creature, "HAS_PART");
    check(parts.size() == 2, "the forward query still lists both parts (" +
          std::to_string(parts.size()) + ")");
}

// The failure this is most likely to hide: a reverse query that is really
// a forward query. Both return non-empty on a connected graph, so "it
// returned something" proves nothing. Asking each question in the
// direction it CANNOT be answered is the control.
void test_the_directions_are_not_confused() {
    Fixture fx; auto& kg = fx.kg;
    Rig r = build(kg);

    auto forward_from_part  = kg.getRelated(r.head, "HAS_PART");
    auto reverse_from_owner = kg.getRelatedReverse(r.creature, "HAS_PART");

    std::cout << "  [measure] forward from a part: " << forward_from_part.size()
              << ", reverse from the owner: " << reverse_from_owner.size()
              << "  (both must be 0)" << std::endl;
    check(forward_from_part.empty(),
          "a part has no outgoing HAS_PART, so forward finds nothing");
    check(reverse_from_owner.empty(),
          "nothing points AT the creature, so reverse finds nothing");
}

void test_relation_type_is_respected() {
    Fixture fx; auto& kg = fx.kg;
    Rig r = build(kg);
    kg.createRelation(r.creature, "PERCEIVES", r.torso);

    auto by_part   = kg.getRelatedReverse(r.torso, "HAS_PART");
    auto by_percv  = kg.getRelatedReverse(r.torso, "PERCEIVES");
    auto by_absent = kg.getRelatedReverse(r.torso, "CONTAINS");

    std::cout << "  [measure] torso reverse by type: HAS_PART=" << by_part.size()
              << " PERCEIVES=" << by_percv.size()
              << " CONTAINS=" << by_absent.size() << std::endl;
    check(by_part.size() == 1, "the HAS_PART edge is found");
    check(by_percv.size() == 1, "so is the PERCEIVES edge to the same entity");
    check(by_absent.empty(),
          "a relation type with no edge returns nothing, not everything");
}

// A creature can be assembled from parts that are themselves assemblies.
// The reverse query must return the immediate owner, not the root.
void test_it_returns_the_immediate_owner_only() {
    Fixture fx; auto& kg = fx.kg;
    auto creature = kg.createEntity("Humanoid");
    auto arm      = kg.createEntity("Arm");
    auto hand     = kg.createEntity("Hand");
    kg.createRelation(creature, "HAS_PART", arm);
    kg.createRelation(arm, "HAS_PART", hand);

    auto hand_owner = kg.getRelatedReverse(hand, "HAS_PART");
    std::cout << "  [measure] hand's reverse HAS_PART: " << hand_owner.size()
              << " owner(s)" << std::endl;
    check(hand_owner.size() == 1, "one immediate owner");
    check(!hand_owner.empty() && hand_owner[0] == arm,
          "the arm, not the creature: the query is one hop, not a walk");
    check(!contains(hand_owner, creature),
          "the root is NOT included (a caller wanting it must walk up)");
}

// Shared parts are unusual but legal, and a resolver that assumes one
// owner would pick arbitrarily. Measure what actually happens.
void test_multiple_owners_are_all_returned() {
    Fixture fx; auto& kg = fx.kg;
    auto a = kg.createEntity("Humanoid");
    auto b = kg.createEntity("Humanoid");
    auto shared = kg.createEntity("Torso");
    kg.createRelation(a, "HAS_PART", shared);
    kg.createRelation(b, "HAS_PART", shared);

    auto owners = kg.getRelatedReverse(shared, "HAS_PART");
    std::cout << "  [measure] a part claimed by two entities: " << owners.size()
              << " owner(s)" << std::endl;
    check(owners.size() == 2, "both owners come back, none is dropped (" +
          std::to_string(owners.size()) + ")");
    check(contains(owners, a) && contains(owners, b), "and they are the right two");
}

// -------------------------------------------------- the index is honest

// The whole reason to be suspicious: this index was written for years and
// never read. If pruning was wrong, a reverse query would resurrect dead
// edges, and a contact resolver would attribute a touch to a creature
// that no longer exists.
void test_a_destroyed_relation_leaves_no_ghost() {
    Fixture fx; auto& kg = fx.kg;
    Rig r = build(kg);

    check(kg.getRelatedReverse(r.head, "HAS_PART").size() == 1,
          "precondition: the edge is there");

    kg.destroyRelation(r.creature, "HAS_PART", r.head);

    auto after = kg.getRelatedReverse(r.head, "HAS_PART");
    std::cout << "  [measure] after destroying the edge: " << after.size()
              << " owner(s) (must be 0)" << std::endl;
    check(after.empty(), "the reverse edge is gone too, not just the forward one");

    // The sibling must survive. A prune that clears the whole bucket
    // would pass the check above and be badly wrong.
    auto torso_owner = kg.getRelatedReverse(r.torso, "HAS_PART");
    check(torso_owner.size() == 1,
          "the sibling's edge is untouched (" +
          std::to_string(torso_owner.size()) + ")");
}

void test_a_destroyed_owner_leaves_no_ghost() {
    Fixture fx; auto& kg = fx.kg;
    Rig r = build(kg);

    kg.destroyEntity(r.creature);

    auto after = kg.getRelatedReverse(r.head, "HAS_PART");
    std::cout << "  [measure] after destroying the owner: " << after.size()
              << " owner(s) (must be 0)" << std::endl;
    check(after.empty(),
          "destroying an entity prunes the edges that pointed out of it");
}

// ------------------------------------------------------------ boundaries

void test_it_answers_rather_than_crashing() {
    Fixture fx; auto& kg = fx.kg;
    Rig r = build(kg);

    auto unknown  = kg.getRelatedReverse(999999u, "HAS_PART");
    auto invalid  = kg.getRelatedReverse(kg::INVALID_ENTITY, "HAS_PART");
    auto no_type  = kg.getRelatedReverse(r.head, "");
    auto orphan   = kg.getRelatedReverse(kg.createEntity("Head"), "HAS_PART");

    std::cout << "  [measure] unknown id=" << unknown.size()
              << " INVALID_ENTITY=" << invalid.size()
              << " empty type=" << no_type.size()
              << " unattached part=" << orphan.size() << std::endl;
    check(unknown.empty(), "an id that was never created returns empty");
    check(invalid.empty(), "INVALID_ENTITY returns empty");
    check(no_type.empty(), "an empty relation type matches nothing");
    check(orphan.empty(), "a part nobody owns returns empty, not garbage");
}

}  // namespace

int main() {
    std::cout << "KG reverse relations (getRelatedReverse)" << std::endl;
    test_a_part_finds_its_owner();
    test_the_directions_are_not_confused();
    test_relation_type_is_respected();
    test_it_returns_the_immediate_owner_only();
    test_multiple_owners_are_all_returned();
    test_a_destroyed_relation_leaves_no_ghost();
    test_a_destroyed_owner_leaves_no_ghost();
    test_it_answers_rather_than_crashing();

    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
