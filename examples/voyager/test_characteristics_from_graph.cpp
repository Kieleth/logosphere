// The one claim this game exists to keep, measured.
//
// THE CLAIM. The sheet on screen is assembled from the characteristics
// the GRAPH holds. Not from a list in C++ that happens to agree with
// the graph today.
//
// WHY A TEST AND NOT A REVIEW. "We read it from the KG" and "we read it
// from the KG and also wrote it down in C++" look identical in every
// screenshot, every code review and every green suite, right up to the
// day somebody adds a characteristic and the screen does not change.
// The frozen predecessor of this game has the set in the graph AND
// spelled out longhand in five places, and nothing there is broken
// today; it is simply not the property anybody thinks it is.
//
// FOUR THINGS ARE CHECKED, and the last two are what stop this being
// a test that passes for the wrong reason:
//
//   1. With the shipped rules, the sheet has exactly the lines the
//      graph has characteristics, labelled as the graph labels them.
//   2. Add a SEVENTH characteristic to the graph at run time — the
//      book's own Psionic Strength, which it prints in the same
//      numbered list and then excludes from character creation — and
//      the sheet grows a seventh line, in the right place, with the
//      right short name and the modifier the book's own table gives
//      its score. Nothing was recompiled.
//   3. Change that characteristic's short name in the graph and the
//      label on the sheet changes with it. So the label is READ, not
//      derived from a position by some private mapping.
//   4. NOT ONE of the six characteristic names, short names or
//      attribute names the graph holds appears as a word anywhere in
//      this game's shipping C++. This is the non-vacuity proof: check
//      1 would still pass against a hardcoded list, and this one would
//      not. It also grows on its own — a seventh characteristic in the
//      graph is a seventh forbidden word, with nothing to update here.
//
// And one refusal, because an indirection nobody checks is not an
// indirection: a characteristic naming an attribute Character does not
// declare is refused rather than silently printing nothing.

#undef NDEBUG

#include "procedure_catalog.h"
#include "rule_loader.h"
#include "sheet.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops_transaction.h"
#include "generated/logosphere_ontology_registry.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/voyager_chargen_ontology_registry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                       \
    do {                                                                \
        if (condition) {                                                \
            ++passed;                                                   \
        } else {                                                        \
            ++failed;                                                   \
            std::cout << "FAIL: " << message << '\n';                   \
        }                                                               \
    } while (false)

#ifndef VOYAGER_GAME_DIR
#error "VOYAGER_GAME_DIR undefined"
#endif
#ifndef VOYAGER_CORPUS_DIR
#error "VOYAGER_CORPUS_DIR undefined"
#endif

kg::OntologyRegistry game_registry() {
    auto registry = logosphere::ontology::registry();
    registry.extend(rulebook::ontology::registry());
    registry.extend(voyager_chargen::ontology::registry());
    return registry;
}

kg::KGOp set_property(kg::EntityID target, std::string property,
                      std::string value) {
    kg::KGOpSetProperty op;
    op.target.id = target;
    op.property = std::move(property);
    op.value = std::move(value);
    return op;
}

kg::KGOp create_entity(
    std::string type, std::string alias,
    std::vector<std::pair<std::string, std::string>> properties) {
    kg::KGOpCreateEntity op;
    op.type = std::move(type);
    op.as = std::move(alias);
    op.properties = std::move(properties);
    return op;
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream bytes;
    bytes << input.rdbuf();
    return bytes.str();
}

bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Does `token` appear in `text` as a whole word? Word-bounded on
// purpose: "Int" must not fire on "Integer", and "end" must not fire on
// ".end()". Case sensitive, because these are the book's spellings and
// a lower-case `strength` in a slot name is exactly the leak looked for.
bool names_word(const std::string& text, const std::string& token) {
    if (token.empty()) return false;
    for (size_t at = text.find(token); at != std::string::npos;
         at = text.find(token, at + 1)) {
        const bool before = at > 0 && is_word_char(text[at - 1]);
        const size_t after_at = at + token.size();
        const bool after =
            after_at < text.size() && is_word_char(text[after_at]);
        if (!before && !after) return true;
    }
    return false;
}

// Every shipping translation unit and header of this game. Generated
// registries are excluded and only they: a generated registry declares
// the ontology, which is where these names BELONG, and it is written by
// a tool from the schema rather than by a person from memory.
std::vector<std::filesystem::path> shipping_sources(
    size_t& generated_skipped) {
    std::vector<std::filesystem::path> out;
    const std::filesystem::path root(VOYAGER_GAME_DIR);
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root / "src")) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        // generic_string, not string: native separators are '\' on
        // Windows and this match must not depend on the platform.
        if (path.generic_string().find("/generated/") != std::string::npos) {
            ++generated_skipped;
            continue;
        }
        const auto extension = path.extension().string();
        if (extension != ".cpp" && extension != ".h") continue;
        out.push_back(path);
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        if (path.extension() != ".cpp") continue;
        // Tests spell what they are testing; they ship no behaviour.
        if (path.filename().string().rfind("test_", 0) == 0) continue;
        out.push_back(path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

int main() {
    kg::KGModule world(game_registry());
    world.setMode(kg::KGMode::MINIMAL);

    const auto primitives = voyager::make_procedure_registry();
    std::string error;
    if (!voyager::load_rules(world, VOYAGER_GAME_DIR, VOYAGER_CORPUS_DIR,
                             primitives, error)) {
        std::cout << "FAIL: the rules did not load: " << error << '\n';
        return 1;
    }

    // ---------------------------------------------------------------
    // 1. The sheet is as long as the graph says, labelled as it says.
    // ---------------------------------------------------------------
    const auto seeded = world.findByType("Characteristic");
    CHECK(!seeded.empty(), "the rules seeded no characteristics at all");

    const kg::EntityID character = world.createEntity("Character");
    CHECK(character != kg::INVALID_ENTITY, "no Character could be created");

    // Give every characteristic a score, through the slot the GRAPH
    // says holds it. The test does not know the slot names either.
    std::vector<std::pair<kg::EntityID, std::string>> characteristics;
    if (!voyager::characteristics_in_order(world, characteristics, error)) {
        std::cout << "FAIL: " << error << '\n';
        return 1;
    }
    {
        std::vector<kg::KGOp> ops;
        int score = 4;
        for (const auto& [id, slot] : characteristics) {
            (void)id;
            ops.push_back(set_property(character, slot,
                                       std::to_string(score++)));
        }
        kg::KGOpBatchReport report;
        CHECK(kg::apply_kg_ops_atomically(ops, world, report),
              "scores refused: " << report.error);
    }

    voyager::Sheet sheet;
    CHECK(voyager::read_sheet(world, character, sheet, error),
          "the sheet could not be read: " << error);
    CHECK(sheet.lines.size() == seeded.size(),
          "the sheet has " << sheet.lines.size() << " lines and the graph "
          << seeded.size() << " characteristics");
    for (size_t i = 0; i < sheet.lines.size() && i < characteristics.size();
         ++i) {
        const std::string expected = world.getProperty(
            characteristics[i].first, "characteristic_abbreviation");
        CHECK(sheet.lines[i].label == expected,
              "line " << i << " is labelled '" << sheet.lines[i].label
                      << "', the graph says '" << expected << "'");
    }
    std::cout << "the shipped rules put " << sheet.lines.size()
              << " characteristics on the sheet:";
    for (const auto& line : sheet.lines) std::cout << " " << line.label;
    std::cout << '\n';
    const size_t before = sheet.lines.size();

    // ---------------------------------------------------------------
    // 2. A seventh, added to the graph, reaches the screen.
    //
    // The book's own: it prints Psionic Strength as position 7 of the
    // same profile and then keeps it out of character creation, so this
    // is a real characteristic arriving late rather than a fiction
    // invented to make a test pass.
    // ---------------------------------------------------------------
    kg::EntityID dice = kg::INVALID_ENTITY;
    for (const kg::EntityID id : world.findByType("DiceExpression")) {
        dice = id;
        break;
    }
    CHECK(dice != kg::INVALID_ENTITY, "the rules seeded no dice to reuse");

    // Cited content carries identity, and a rule that did not come out
    // of a book still has to say where it DID come from. So the seventh
    // arrives in a RuntimeContext, which is the engine's own answer to
    // "a rule created while the game was running", rather than being
    // smuggled in without one.
    kg::EntityID seventh = kg::INVALID_ENTITY;
    {
        kg::KGOpBatchReport report;
        const std::vector<kg::KGOp> ops = {
            create_entity("RuntimeContext", "here",
                          {{"name", "the table this was decided at"},
                           {"context_key", "voyager:test:seventh"},
                           {"context_kind", "session"}}),
            create_entity("Characteristic", "psi",
                          {{"name", "Psionic Strength"},
                           {"identity_context", "@here"},
                           {"origin_context", "@here"},
                           {"entity_key", "psionic-strength"},
                           {"attribute_ref", "psionic_strength"},
                           {"characteristic_abbreviation", "Psi"},
                           {"characteristic_index", "7"},
                           {"characteristic_dice", std::to_string(dice)}}),
            set_property(character, "psionic_strength", "11"),
        };
        CHECK(kg::apply_kg_ops_atomically(ops, world, report),
              "the seventh characteristic was refused: " << report.error);
        const auto bound = report.bindings.find("psi");
        if (bound != report.bindings.end()) seventh = bound->second;
    }
    CHECK(seventh != kg::INVALID_ENTITY, "the seventh was never created");

    CHECK(voyager::read_sheet(world, character, sheet, error),
          "the sheet could not be read after the seventh: " << error);
    CHECK(sheet.lines.size() == before + 1,
          "a characteristic was added to the graph and the sheet still has "
          << sheet.lines.size() << " lines. THIS IS THE DEFECT THIS TEST "
          "EXISTS FOR: the screen is being built from a list in C++, not "
          "from the graph.");
    if (sheet.lines.size() == before + 1) {
        const auto& line = sheet.lines.back();
        CHECK(line.label == "Psi",
              "the seventh line is labelled '" << line.label
                                               << "', not 'Psi'");
        CHECK(line.value == "11",
              "the seventh line reads '" << line.value << "', not '11'");
        // 11 falls in the book's "9 through 11" band, which pays +1.
        // Read from the table, so this also proves the odds are not
        // recomputed in C++ from arithmetic the book happens to print.
        CHECK(line.modifier == "+1",
              "the seventh line's modifier is '" << line.modifier
                                                 << "', not '+1'");
    }

    // ---------------------------------------------------------------
    // 3. The label is read, not derived from a position.
    // ---------------------------------------------------------------
    {
        kg::KGOpBatchReport report;
        const std::vector<kg::KGOp> ops = {
            set_property(seventh, "characteristic_abbreviation", "Zed")};
        CHECK(kg::apply_kg_ops_atomically(ops, world, report),
              "renaming the seventh was refused: " << report.error);
    }
    CHECK(voyager::read_sheet(world, character, sheet, error),
          "the sheet could not be read after renaming: " << error);
    CHECK(!sheet.lines.empty() && sheet.lines.back().label == "Zed",
          "the graph renamed the seventh and the sheet still says '"
          << (sheet.lines.empty() ? std::string() : sheet.lines.back().label)
          << "', so the label is coming from somewhere else");

    // ---------------------------------------------------------------
    // The refusal: an indirection nobody checks is not an indirection.
    // ---------------------------------------------------------------
    {
        kg::KGOpBatchReport report;
        const std::vector<kg::KGOp> ops = {
            create_entity("RuntimeContext", "elsewhere",
                          {{"name", "a table with a house rule"},
                           {"context_key", "voyager:test:luck"},
                           {"context_kind", "session"}}),
            create_entity("Characteristic", "nonsense",
                          {{"name", "Luck"},
                           {"identity_context", "@elsewhere"},
                           {"origin_context", "@elsewhere"},
                           {"entity_key", "luck"},
                           {"attribute_ref", "luck"},
                           {"characteristic_abbreviation", "Lck"},
                           {"characteristic_index", "8"},
                           {"characteristic_dice", std::to_string(dice)}})};
        CHECK(kg::apply_kg_ops_atomically(ops, world, report),
              "the graph refused a well-formed characteristic: "
              << report.error);
    }
    std::vector<std::pair<kg::EntityID, std::string>> refused;
    std::string refusal;
    CHECK(!voyager::characteristics_in_order(world, refused, refusal),
          "a characteristic naming an attribute Character does not declare "
          "was accepted; the reference is decorative");
    CHECK(refusal.find("luck") != std::string::npos,
          "the refusal does not say which attribute was wrong: " << refusal);

    // ---------------------------------------------------------------
    // 4. NON-VACUITY. None of the words the graph holds is spelled in
    //    this game's shipping C++.
    // ---------------------------------------------------------------
    std::set<std::string> forbidden;
    for (const kg::EntityID id : seeded) {
        for (const char* slot : {"name", "characteristic_abbreviation",
                                 "attribute_ref"}) {
            const std::string value = world.getProperty(id, slot);
            if (!value.empty()) forbidden.insert(value);
        }
    }
    CHECK(forbidden.size() >= seeded.size(),
          "the graph holds " << seeded.size() << " characteristics and only "
          << forbidden.size() << " words to look for, so this check is "
          "weaker than it reads");

    size_t generated_skipped = 0;
    const auto sources = shipping_sources(generated_skipped);
    CHECK(!sources.empty(),
          "no shipping source was scanned, so this check proved nothing");
    CHECK(generated_skipped >= 1,
          "the generated-file exclusion excluded nothing: either "
          "src/generated moved or the path match broke, and a broken "
          "match reports the schema's own generated output as leaks");
    size_t scanned = 0;
    for (const auto& path : sources) {
        const std::string text = slurp(path);
        CHECK(!text.empty(), "could not read " << path);
        if (text.empty()) continue;
        ++scanned;
        for (const std::string& word : forbidden) {
            CHECK(!names_word(text, word),
                  path.filename().string() << " spells '" << word
                  << "'. Every characteristic is the graph's; a name in "
                     "C++ is the leak this game was written to not have.");
        }
    }
    std::cout << "scanned " << scanned << " shipping source(s) for "
              << forbidden.size() << " word(s) the graph owns:";
    for (const std::string& word : forbidden) std::cout << " '" << word << "'";
    std::cout << '\n';

    std::cout << (failed ? "FAILED " : "OK ") << passed << " passed, "
              << failed << " failed\n";
    return failed ? 1 : 0;
}
