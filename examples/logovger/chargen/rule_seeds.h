// The rules Logovger loads, in dependency order. ONE list.
//
// There were two: the application had its own and test_chargen had a
// copy. Adding the skill vocabulary to the application left the test
// loading careers whose skill references pointed at nothing, and the
// failures read like citation bugs rather than a stale list. A gate
// that checks a different set of rules than the game runs is not a
// gate, which is the same lesson the CI test list taught.
//
// ORDER MATTERS and it is dependency order, not preference. The
// vocabulary seed OWNS every Skill; everything after it references
// those by canonical source-document, exact type, and seed alias.
// Load a referencing seed first and it fails loudly, which is the
// intended behaviour, not a reason to reorder by hand. The production
// manifest is loaded out of order by
// test_rule_seed_manifest_rejects_wrong_order, which gates this claim.

#ifndef LOGOVGER_RULE_SEEDS_H
#define LOGOVGER_RULE_SEEDS_H

#include <cstddef>

namespace logovger {

inline constexpr const char* kRuleSeeds[] = {
    "seeds/cepheus_book1_skill_vocabulary.json",
    "seeds/cepheus_book1_tables.json",
    // Aging, mishaps and injuries: the tables no career owns. After
    // the dice and currency it references, before anything else.
    "seeds/cepheus_book1_shared_tables.json",
    "seeds/cepheus_careers.json",
    // Last of the rule data: it references the Skills the vocabulary
    // owns, the Currency and dice the earlier seeds create, and the
    // characteristic-modifier lookup from the careers seed.
    "seeds/cepheus_book1_career_tables.json",
    "seeds/cepheus_basic_chargen_procedure.json",
};

inline constexpr std::size_t kRuleSeedCount =
    sizeof(kRuleSeeds) / sizeof(kRuleSeeds[0]);

}  // namespace logovger

#endif  // LOGOVGER_RULE_SEEDS_H
