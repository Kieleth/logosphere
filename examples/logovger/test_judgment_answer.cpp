// The referee's answer is read strictly, or the rule is not the rule.
//
// A model is on the other end of this. It replies in prose, it can be
// wrong, and it has every opportunity to be loose: name four things
// when the book said two, name something that was never on offer, say
// the same thing twice, or answer in a shape nobody asked for. None of
// that may reach the character sheet.
//
// Every case here is a way an answer can fail to fit, and every one
// must come back refused with nothing chosen.

#include "src/judgment_answer.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int tests_passed = 0;
int tests_failed = 0;

#define CHECK(condition, message)                                        \
    do {                                                                 \
        if (condition) {                                                 \
            ++tests_passed;                                              \
        } else {                                                         \
            ++tests_failed;                                              \
            std::cout << "FAIL: " << (message) << "\n";                  \
        }                                                                \
    } while (false)

const std::vector<std::string> kPhysical = {"strength", "dexterity",
                                            "endurance"};

void a_well_formed_answer_is_read() {
    std::vector<std::string> chosen;
    std::string reason;
    std::string error;
    const bool ok = logovger::parse_judgment_answer(
        "CHOICE: strength, endurance\n"
        "WHY: the loading decks took his back years ago\n",
        kPhysical, 2, chosen, reason, error);
    CHECK(ok, "a well-formed answer must be accepted: " + error);
    CHECK(chosen.size() == 2 && chosen[0] == "strength" &&
              chosen[1] == "endurance",
          "both named options come back, in the rule's own spelling");
    CHECK(reason == "the loading decks took his back years ago",
          "the clause is carried for the life log, got: " + reason);
}

void spelling_and_spacing_are_forgiven_but_nothing_else_is() {
    std::vector<std::string> chosen;
    std::string reason;
    std::string error;
    const bool ok = logovger::parse_judgment_answer(
        "  choice:   STRENGTH ,dexterity  \n", kPhysical, 2, chosen,
        reason, error);
    CHECK(ok, "case and spacing must not matter: " + error);
    CHECK(chosen.size() == 2 && chosen[0] == "strength" &&
              chosen[1] == "dexterity",
          "the answer is normalised to how the rule spells it, so a "
          "later check compares like with like");
    CHECK(reason.empty(),
          "a missing WHY is not an error; the choice is what the rule "
          "needs");
}

// The whole point of the seam.
void an_answer_that_does_not_fit_the_rule_is_refused() {
    struct Case {
        const char* name;
        const char* reply;
        int count;
        const char* expect;
    };
    const Case cases[] = {
        {"too many", "CHOICE: strength, dexterity, endurance\n", 2,
         "not 2"},
        {"too few", "CHOICE: strength\n", 2, "not 2"},
        {"an option never offered", "CHOICE: strength, intelligence\n", 2,
         "does not allow"},
        {"the same one twice", "CHOICE: strength, strength\n", 2, "twice"},
        {"no CHOICE line at all", "I think his knees would go first.\n", 2,
         "no CHOICE line"},
        {"an empty CHOICE line", "CHOICE:\nWHY: hard to say\n", 2,
         "no CHOICE line"},
        {"prose instead of options", "CHOICE: his hands and his wind\n", 2,
         "does not allow"},
    };
    for (const auto& item : cases) {
        std::vector<std::string> chosen;
        std::string reason;
        std::string error;
        const bool ok = logovger::parse_judgment_answer(
            item.reply, kPhysical, item.count, chosen, reason, error);
        CHECK(!ok, std::string("an answer with ") + item.name +
                       " must be refused");
        CHECK(error.find(item.expect) != std::string::npos,
              std::string("refusing ") + item.name +
                  " must say why, got: " + error);
        CHECK(chosen.empty(),
              std::string("a refused answer leaves nothing chosen (") +
                  item.name + ")");
    }
}

// A rule that asks for the whole group is still a rule: the answer has
// to name all of it, not be excused from naming any.
void asking_for_everything_still_has_to_name_everything() {
    std::vector<std::string> chosen;
    std::string reason;
    std::string error;
    CHECK(logovger::parse_judgment_answer(
              "CHOICE: endurance, strength, dexterity\n", kPhysical, 3,
              chosen, reason, error),
          "all three named is a valid answer to a count of three: " + error);
    CHECK(chosen.size() == 3, "all three come back");

    CHECK(!logovger::parse_judgment_answer("CHOICE: strength, dexterity\n",
                                           kPhysical, 3, chosen, reason,
                                           error),
          "two named against a count of three is refused");
    CHECK(chosen.empty(), "and leaves nothing chosen");
}

// The first CHOICE line wins rather than the last, so a model that
// reconsiders out loud cannot smuggle a second answer past the check.
void a_second_choice_line_does_not_override_the_first() {
    std::vector<std::string> chosen;
    std::string reason;
    std::string error;
    const bool ok = logovger::parse_judgment_answer(
        "CHOICE: strength, endurance\n"
        "WHY: the years in the hold\n"
        "CHOICE: dexterity, endurance\n",
        kPhysical, 2, chosen, reason, error);
    CHECK(ok, "the answer is still readable: " + error);
    CHECK(chosen.size() == 2 && chosen[0] == "strength" &&
              chosen[1] == "endurance",
          "the first answer stands, got " +
              (chosen.empty() ? std::string("nothing") : chosen[0]));
}

}  // namespace

int main() {
    std::cout << "=== judgment answers are held to the rule ===\n";
    a_well_formed_answer_is_read();
    spelling_and_spacing_are_forgiven_but_nothing_else_is();
    an_answer_that_does_not_fit_the_rule_is_refused();
    asking_for_everything_still_has_to_name_everything();
    a_second_choice_line_does_not_override_the_first();
    std::cout << "\n[measure] " << tests_passed << " passed, "
              << tests_failed << " failed\n";
    return tests_failed == 0 ? 0 : 1;
}
