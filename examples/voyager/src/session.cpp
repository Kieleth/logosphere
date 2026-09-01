#include "session.h"

#include "graph_ops.h"
#include "procedure_catalog.h"
#include "sheet.h"

#include "logosphere/kg/kg_ops_transaction.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace voyager {
namespace {

using logosphere::rules::ProcedurePrimitiveResult;

// The book prints the sentence with its own bold run-in ("**Athlete:**
// Individual that ..."). The DATA keeps the bytes the book has; this is
// only what a panel shows.
std::string without_run_in(const std::string& sentence,
                           const std::string& name) {
    const std::string mark = "**" + name + ":** ";
    if (sentence.rfind(mark, 0) == 0) return sentence.substr(mark.size());
    return sentence;
}

// A draw against a stated chance. The granularity is mechanism, not a
// number the book fixes: one part in a thousand is finer than any
// chance the director states.
double draw_against(logosphere::dice::DiceService& dice,
                    const std::string& stream, kg::EntityID rule,
                    bool& ok) {
    const logosphere::dice::DiceExpression die{1, 1000, 0, 1};
    const auto roll = dice.roll(die, stream, "the draw", rule);
    ok = roll.id != 0;
    return static_cast<double>(roll.total) / 1000.0;
}

std::string fixed3(double value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << value;
    return out.str();
}

bool as_probability(const std::string& text, double& out) {
    try {
        size_t end = 0;
        out = std::stod(text, &end);
        return end == text.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

Session::Session(kg::KGModule& world, logosphere::dice::DiceService& dice)
    : world_(world), dice_(dice),
      primitives_(make_procedure_registry()),
      runner_(world, primitives_),
      effects_(make_effect_registry()) {
    bind_primitives();
}

void Session::bind_primitives() {
    std::string error;
    const auto bind = [&](const char* name,
                          Result (Session::*method)(const Context&)) {
        if (!primitives_.bind_primitive(
                name,
                [this, method](const Context& context) {
                    return (this->*method)(context);
                },
                error)) {
            // A registry that refuses a binding is a broken build, not
            // a condition to survive at run time.
            throw std::logic_error(error);
        }
    };
    bind("roll_characteristics", &Session::roll_characteristics);
    bind("narrate_background", &Session::narrate_background);
    bind("choose_career", &Session::choose_career);
    bind("spend_season", &Session::spend_season);
    bind("propose_arrival", &Session::propose_arrival);
    bind("face_moment", &Session::face_moment);
    bind("end_making", &Session::end_making);
}

std::string Session::players_door_key() const {
    for (const kg::EntityID id : world_.findByType("Door")) {
        if (world_.getProperty(id, "door_is_players") == "true") {
            return world_.getProperty(id, "door_key");
        }
    }
    return {};
}

bool Session::constant_real(const std::string& name, double& value,
                            std::string& error) const {
    for (const kg::EntityID id : world_.findByType("RuleConstant")) {
        if (world_.getProperty(id, "name") != name) continue;
        const std::string text = world_.getProperty(id, "constant_value");
        try {
            size_t end = 0;
            value = std::stod(text, &end);
            if (end == text.size()) return true;
        } catch (...) {
        }
        error = "the rule constant '" + name + "' holds no readable number";
        return false;
    }
    error = "the rules fix no constant named '" + name + "'";
    return false;
}

size_t Session::stage_count(kg::EntityID kind) const {
    size_t count = 0;
    for (const kg::EntityID part :
         world_.getRelated(character_, "LIVED")) {
        if (world_.getType(part) != "MomentFaced") continue;
        long long ref = 0;
        if (as_int(world_.getProperty(part, "moment_kind"), ref) &&
            static_cast<kg::EntityID>(ref) == kind) {
            ++count;
        }
    }
    return count;
}

bool Session::constant(const std::string& name, long long& value,
                       std::string& error) const {
    for (const kg::EntityID id : world_.findByType("RuleConstant")) {
        if (world_.getProperty(id, "name") != name) continue;
        if (as_int(world_.getProperty(id, "constant_value"), value)) {
            return true;
        }
        error = "the rule constant '" + name + "' holds no readable number";
        return false;
    }
    error = "the rules fix no constant named '" + name + "'";
    return false;
}

bool Session::throw_text(kg::EntityID check, std::string& text,
                         std::string& error) const {
    const std::string attribute = world_.getProperty(check, "attribute_ref");
    const std::string target = world_.getProperty(check, "target_number");
    if (attribute.empty() || target.empty()) {
        error = "a throw carries no characteristic or no target";
        return false;
    }
    for (const kg::EntityID id : world_.findByType("Characteristic")) {
        if (world_.getProperty(id, "attribute_ref") != attribute) continue;
        text = world_.getProperty(id, "characteristic_abbreviation") + " " +
               target + "+";
        return true;
    }
    error = "a throw names the attribute '" + attribute +
            "', which no characteristic in the graph holds";
    return false;
}

bool Session::begin(std::string& error) {
    error.clear();
    finished_ = false;
    in_life_ = false;
    awaiting_plan_ = false;
    context_ = kg::INVALID_ENTITY;
    landed_.clear();
    doors_.clear();
    situation_.clear();
    weight_ = kg::INVALID_ENTITY;
    character_ = kg::INVALID_ENTITY;
    procedure_ = kg::INVALID_ENTITY;
    cursor_ = {};
    choices_.clear();
    offered_.clear();
    prompt_.clear();
    question_asked_.clear();
    log_.clear();

    if (!referee_) {
        error = "no referee is installed. Two questions in this procedure "
                "are not the engine's to answer, and there is no fallback "
                "on purpose.";
        return false;
    }

    for (const kg::EntityID id : world_.findByType("Procedure")) {
        if (world_.getProperty(id, "name") != kProcedureName) continue;
        procedure_ = id;
        break;
    }
    if (procedure_ == kg::INVALID_ENTITY) {
        error = std::string("the world holds no Procedure named '") +
                kProcedureName + "'; load the rules";
        return false;
    }
    if (!runner_.validate(procedure_, error)) return false;

    character_ = world_.createEntity("Character");
    if (character_ == kg::INVALID_ENTITY) {
        error = "the world does not know what a Character is; extend the "
                "ontology with this game's pack";
        return false;
    }

    // The age of majority is a number the BOOK fixes, so it is read
    // from the graph at the point it is used and written nowhere else.
    // The playing gets its own scope in the same breath: everything
    // the director names lands in this RuntimeContext, and the book
    // takes from it only what its authors promote.
    long long majority = 0;
    if (!constant("age_of_majority", majority, error)) return false;
    kg::KGOpBatchReport report;
    const std::vector<kg::KGOp> ops = {
        set_property(character_, "age_years", std::to_string(majority)),
        create_entity("RuntimeContext", "playing",
                      {{"name", "this playing"},
                       {"context_key",
                        "session:" + std::to_string(character_)},
                       {"context_kind", "session"}}),
    };
    if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
        error = "could not begin the character: " + report.error;
        return false;
    }
    const auto playing = report.bindings.find("playing");
    if (playing == report.bindings.end()) {
        error = "the playing's own scope was not created";
        return false;
    }
    context_ = playing->second;

    return accept(runner_.start(procedure_, character_), error);
}

bool Session::accept(logosphere::rules::ProcedureResult result,
                     std::string& error) {
    if (result.status == logosphere::rules::ProcedureStatus::FAILED) {
        error = result.error;
        return false;
    }
    if (result.status == logosphere::rules::ProcedureStatus::PENDING) {
        cursor_ = result.cursor;
        prompt_ = std::move(result.prompt);
        choices_ = std::move(result.choices);
        offered_.clear();
        for (const auto& choice : choices_) offered_.push_back(choice.key);
        question_asked_ = prompt_;
        return true;
    }
    // Creation is complete. The life is a second procedure from the
    // game's OWN book, chained here because a seed cites one file and
    // the two flows cite different books. A world holding creation
    // rules but no life procedure is a broken load, not an ending.
    if (!in_life_) {
        in_life_ = true;
        kg::EntityID life = kg::INVALID_ENTITY;
        for (const kg::EntityID id : world_.findByType("Procedure")) {
            if (world_.getProperty(id, "name") != kLifeProcedureName) {
                continue;
            }
            life = id;
            break;
        }
        if (life == kg::INVALID_ENTITY) {
            error = std::string("the world holds no Procedure named '") +
                    kLifeProcedureName + "'; load the game's own book";
            return false;
        }
        if (!runner_.validate(life, error)) return false;
        return accept(runner_.start(life, character_), error);
    }
    finished_ = true;
    choices_.clear();
    return true;
}

bool Session::choose(const std::string& answer, std::string& error) {
    error.clear();
    if (finished_) {
        error = "this character is finished; nothing is being asked";
        return false;
    }
    // The player's own door takes the player's own words: any of
    // them, as long as there are some. Every other question takes one
    // of the answers on offer and nothing else.
    if (awaiting_plan_) {
        if (trimmed(answer).empty()) {
            error = "the open door needs words; say what you would do";
            return false;
        }
    } else if (std::find(offered_.begin(), offered_.end(), answer) ==
               offered_.end()) {
        error = "'" + answer + "' is not one of the answers on offer";
        return false;
    }
    return accept(runner_.resume(cursor_, answer), error);
}

// ------------------------------------------------------- primitives

Session::Result Session::roll_characteristics(const Context& context) {
    std::vector<std::pair<kg::EntityID, std::string>> characteristics;
    std::string error;
    if (!characteristics_in_order(world_, characteristics, error)) {
        return Result::failed(error);
    }

    std::vector<kg::KGOp> ops;
    for (const auto& [id, slot] : characteristics) {
        // The dice this characteristic is rolled with come off the
        // characteristic, so nothing here assumes the graph holds one
        // expression, or knows which one it is.
        long long count = 0;
        long long sides = 0;
        kg::EntityID dice = kg::INVALID_ENTITY;
        long long reference = 0;
        if (!as_int(world_.getProperty(id, "characteristic_dice"),
                    reference) ||
            reference <= 0 ||
            !world_.exists(static_cast<kg::EntityID>(reference))) {
            return Result::failed(
                "characteristic '" + world_.getProperty(id, "name") +
                "' names no dice to roll it with");
        }
        dice = static_cast<kg::EntityID>(reference);
        if (!as_int(world_.getProperty(dice, "dice_count"), count) ||
            !as_int(world_.getProperty(dice, "dice_sides"), sides)) {
            return Result::failed(
                "the dice for '" + world_.getProperty(id, "name") +
                "' carry no readable count or sides");
        }
        long long modifier = 0;
        long long multiplier = 1;
        as_int(world_.getProperty(dice, "dice_modifier"), modifier);
        as_int(world_.getProperty(dice, "dice_multiplier"), multiplier);

        logosphere::dice::DiceExpression expression{
            static_cast<int>(count), static_cast<int>(sides),
            static_cast<int>(modifier), static_cast<int>(multiplier)};
        const auto roll = dice_.roll(expression, "chargen",
                                     world_.getProperty(id, "name"), id);
        if (roll.id == 0) {
            return Result::failed(
                "the dice for '" + world_.getProperty(id, "name") +
                "' are not a valid expression");
        }
        ops.push_back(set_property(context.target, slot,
                                   std::to_string(roll.total)));
        log_.push_back(world_.getProperty(
                           id, "characteristic_abbreviation") + " " +
                       std::to_string(roll.total) + "  [" +
                       expression.to_string() + ", roll #" +
                       std::to_string(roll.id) + "]");
    }

    kg::KGOpBatchReport report;
    if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
        return Result::failed("the scores were refused: " + report.error);
    }
    return Result::advance();
}

Session::Result Session::narrate_background(const Context& context) {
    Sheet sheet;
    std::string error;
    if (!read_sheet(world_, context.target, sheet, error)) {
        return Result::failed(error);
    }

    std::ostringstream facts;
    for (const auto& line : sheet.lines) {
        if (line.value.empty()) continue;
        facts << line.label << " " << line.value << " (" << line.modifier
              << ")\n";
    }

    RefereeQuestion question;
    question.site = "background";
    question.prompt =
        "THE NUMBERS, already rolled and not yours to change:\n" +
        facts.str() +
        "\nWrite where this person comes from: a few sentences, no more. "
        "Every clause has to be something these numbers support. Do not "
        "name a characteristic, a score, a modifier or a die. Do not say "
        "what happens next.";

    std::string prose;
    if (!referee_(question, prose, error)) {
        return Result::failed("the referee did not answer: " + error);
    }
    prose = trimmed(prose);
    if (prose.empty()) {
        return Result::failed(
            "the referee returned nothing for the background. A character "
            "with no origin is not a degraded character, it is a broken "
            "run, so this stops here.");
    }

    // In the graph, held by the character it is about, so the screen can
    // read it back rather than keeping a second copy of it.
    kg::KGOpBatchReport report;
    const std::vector<kg::KGOp> ops = {
        create_entity("Narration", "background",
                      {{"name", "background"}, {"narration_text", prose}}),
        relate(context.target, "HAS_PART", "background"),
    };
    if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
        return Result::failed("the background was refused: " + report.error);
    }
    log_.push_back(prose);
    return Result::advance();
}

Session::Result Session::choose_career(const Context& context) {
    // Resuming: the answer is a career key the rules issued.
    if (context.input.has_value()) {
        const std::string& answer = *context.input;
        kg::EntityID career = kg::INVALID_ENTITY;
        for (const kg::EntityID id : world_.findByType("Career")) {
            if (world_.getProperty(id, "name") != answer) continue;
            career = id;
            break;
        }
        if (career == kg::INVALID_ENTITY) {
            return Result::failed("'" + answer +
                                  "' is not a career the rules hold");
        }
        std::string options;
        for (const auto& key : offered_) {
            if (!options.empty()) options += ", ";
            options += key;
        }
        kg::KGOpBatchReport report;
        // The decision and the state it produced land together or not
        // at all, and the character POINTS AT the decision: a record
        // nothing points at cannot be found from the thing it explains.
        const std::vector<kg::KGOp> ops = {
            create_entity("ArbiterDecision", "pick",
                          {{"name", "career chosen"},
                           {"event_type", "ARBITER_DECISION"},
                           {"decision_question", question_asked_},
                           {"decision_options", options},
                           {"decision_taken", answer},
                           {"arbiter", arbiter_}}),
            set_property(context.target, "chosen_career",
                         std::to_string(career)),
            set_property_ref(context.target, "career_decision", "pick"),
        };
        if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
            return Result::failed("the career was refused: " + report.error);
        }
        log_.push_back("entered the " + answer);
        return Result::complete();
    }

    // Asking: the ENGINE builds the legal set, the referee narrows it,
    // and nothing the rules did not issue can come back.
    struct Legal {
        kg::EntityID id = kg::INVALID_ENTITY;
        std::string  name;
        std::string  summary;
        std::string  qualification;
        std::string  survival;
    };
    std::vector<Legal> legal;
    std::string error;
    for (const kg::EntityID id : world_.findByType("Career")) {
        Legal entry;
        entry.id = id;
        entry.name = world_.getProperty(id, "name");
        entry.summary = without_run_in(
            world_.getProperty(id, "career_summary"), entry.name);
        long long reference = 0;
        for (const auto& [slot, into] :
             std::vector<std::pair<const char*, std::string*>>{
                 {"qualification_check", &entry.qualification},
                 {"survival_check", &entry.survival}}) {
            if (!as_int(world_.getProperty(id, slot), reference) ||
                !throw_text(static_cast<kg::EntityID>(reference), *into,
                            error)) {
                return Result::failed("career '" + entry.name + "': " +
                                      error);
            }
        }
        legal.push_back(std::move(entry));
    }
    if (legal.empty()) {
        return Result::failed("the rules hold no careers to offer");
    }
    std::sort(legal.begin(), legal.end(),
              [](const Legal& a, const Legal& b) { return a.name < b.name; });

    Sheet sheet;
    if (!read_sheet(world_, context.target, sheet, error)) {
        return Result::failed(error);
    }

    std::ostringstream prompt;
    prompt << "THIS PERSON, as the dice made them:\n";
    for (const auto& line : sheet.lines) {
        if (line.value.empty()) continue;
        prompt << "  " << line.label << " " << line.value << " ("
               << line.modifier << ")\n";
    }
    prompt << "\nWHERE THEY COME FROM:\n" << sheet.background << "\n";
    prompt << "\nEVERY DOOR THE RULES ALLOW. Each line is a career, what "
              "it is, the throw to get in and the throw to survive a "
              "term:\n";
    RefereeQuestion question;
    question.site = "careers";
    for (const auto& entry : legal) {
        prompt << "  " << entry.name << " | qualify " << entry.qualification
               << " | survive " << entry.survival << " | " << entry.summary
               << "\n";
        question.allowed.push_back(entry.name);
    }
    prompt << "\nOffer this person the few of those that make sense for "
              "THIS life, and reframe each one as the door it would be "
              "for them. You may narrow and you may reframe. You may not "
              "invent: every key must be copied exactly from the list "
              "above.\n"
              "Answer as lines, one per offer, in the form\n"
              "  <career name> | <one clause, what that door is for them>\n"
              "and nothing else.";
    question.prompt = prompt.str();

    std::string answer;
    if (!referee_(question, answer, error)) {
        return Result::failed("the referee did not answer: " + error);
    }

    std::vector<Choice> offered;
    std::istringstream lines(answer);
    std::string line;
    while (std::getline(lines, line)) {
        line = trimmed(line);
        if (line.empty()) continue;
        const auto bar = line.find('|');
        const std::string key = trimmed(bar == std::string::npos
                                            ? line
                                            : line.substr(0, bar));
        const std::string reframe =
            bar == std::string::npos ? std::string()
                                     : trimmed(line.substr(bar + 1));
        const auto found = std::find_if(
            legal.begin(), legal.end(),
            [&key](const Legal& entry) { return entry.name == key; });
        if (found == legal.end()) {
            // Refused, not dropped. An option the rules never issued is
            // not a narrower menu, it is a different game, and quietly
            // skipping it would make a referee that invents look
            // exactly like one that does not.
            return Result::failed(
                "the referee offered '" + key +
                "', which the rules did not issue");
        }
        if (std::any_of(offered.begin(), offered.end(),
                        [&key](const Choice& choice) {
                            return choice.key == key;
                        })) {
            continue;
        }
        Choice choice;
        choice.key = found->name;
        choice.label = reframe.empty() ? found->name : reframe;
        choice.detail = "qualify " + found->qualification + " | survive " +
                        found->survival;
        choice.subject = found->id;
        offered.push_back(std::move(choice));
    }
    if (offered.empty()) {
        return Result::failed(
            "the referee offered nothing. An empty menu is not a hard "
            "life, it is a broken one, and the run stops rather than "
            "quietly handing back all " +
            std::to_string(legal.size()) + " careers.");
    }
    return Result::pending("Which door do you take?", std::move(offered));
}

// ---------------------------------------------------------- the life
//
// One season per pass of the procedure: spend it, learn what lands,
// face what landed, and the routes in the seed loop back. Nothing
// here knows a kind, a weight, a door, a standing or a turn by name;
// every one is read off the graph, and every number the director
// states is drawn against by the engine.

namespace {

constexpr const char* kEnough = "enough";

struct Named {
    kg::EntityID id = kg::INVALID_ENTITY;
    std::string  key;
    std::string  name;
    std::string  quote;
};

std::vector<Named> named_of(const kg::KGModule& world,
                            const std::string& type,
                            const std::string& key_slot) {
    std::vector<Named> out;
    for (const kg::EntityID id : world.findByType(type)) {
        out.push_back({id, world.getProperty(id, key_slot),
                       world.getProperty(id, "name"),
                       world.getProperty(id, "source_quote")});
    }
    return out;
}

std::string constant_text(const kg::KGModule& world,
                          const std::string& name) {
    for (const kg::EntityID id : world.findByType("RuleConstant")) {
        if (world.getProperty(id, "name") == name) {
            return world.getProperty(id, "constant_value");
        }
    }
    return {};
}

std::vector<std::string> split_bars(const std::string& line) {
    std::vector<std::string> out;
    std::string part;
    std::istringstream in(line);
    while (std::getline(in, part, '|')) out.push_back(trimmed(part));
    return out;
}

// The character as the director must see them: the numbers, the prose
// so far, and the lived record.
void describe(const kg::KGModule& world, kg::EntityID character,
              std::ostringstream& out, std::string& error) {
    Sheet sheet;
    if (!read_sheet(world, character, sheet, error)) return;
    out << "THIS PERSON, as the dice made them:\n";
    for (const auto& line : sheet.lines) {
        if (line.value.empty()) continue;
        out << "  " << line.label << " " << line.value << " ("
            << line.modifier << ")\n";
    }
    out << "  age " << sheet.age;
    if (!sheet.career.empty()) out << ", in the " << sheet.career;
    out << "\n\nTHEIR LIFE SO FAR:\n" << sheet.background << "\n";
    out << "\nWHAT THEY HAVE LIVED AND HOLD:";
    if (sheet.record.empty()) out << " nothing yet";
    for (const auto& row : sheet.record) {
        out << "\n  " << row.label;
        if (!row.count.empty()) out << " " << row.count;
    }
    out << "\n";
}

}  // namespace

bool Session::ask_in_shape(
    RefereeQuestion question,
    const std::function<bool(const std::string& reply, std::string& why)>&
        accept,
    std::string& error) {
    std::string why;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        std::string reply;
        if (!referee_(question, reply, error)) {
            error = "the director did not answer: " + error;
            return false;
        }
        if (accept(reply, why)) return true;
        log_.push_back("the director's answer was refused: " + why);
        question.prompt += "\n\nYOUR LAST ANSWER WAS REFUSED: " + why +
                           "\nAnswer again, whole, exactly in the shape "
                           "asked: every part of the shape, every time, "
                           "with only the refused part changed, and "
                           "nothing else.";
    }
    error = "the director could not answer in shape after three attempts; "
            "the last refusal: " + why;
    return false;
}

Session::Result Session::spend_season(const Context& context) {
    const auto modes = named_of(world_, "SeasonMode", "season_mode_key");
    if (modes.empty()) {
        return Result::failed(
            "the world holds no way to spend a season; load the game's "
            "own book");
    }

    if (context.input.has_value()) {
        const std::string& answer = *context.input;
        if (answer == kEnough) {
            log_.push_back("the making ends, by the player's word");
            return Result::advance("enough");
        }
        const Named* taken = nullptr;
        for (const auto& mode : modes) {
            if (mode.name == answer) taken = &mode;
        }
        if (!taken) {
            return Result::failed("'" + answer +
                                  "' is not a way the book fixes for "
                                  "spending a season");
        }
        long long years = 0;
        long long age = 0;
        std::string error;
        if (!constant("season_standard_years", years, error)) {
            return Result::failed(error);
        }
        if (!as_int(world_.getProperty(context.target, "age_years"), age)) {
            return Result::failed(
                "the character has no readable age to add a season to");
        }
        const long long after = age + years;

        // The season as it was lived. Prose only: it may go sideways
        // within the year, but nothing lasting happens here, because
        // what lasts happens in moments.
        const std::string plan = season_hints_[taken->name];
        std::ostringstream prompt;
        describe(world_, context.target, prompt, error);
        if (!error.empty()) return Result::failed(error);
        prompt << "\nTHE WAY THIS SEASON IS SPENT: " << taken->name << " | "
               << plan << "\n" << taken->quote
               << "\n\nTell the season as it was lived, past tense, two or "
                  "three sentences. It may go sideways inside the year, "
                  "but nothing lasting happens in a season: no wound, no "
                  "enemy, no turn. What lasts happens in moments. No "
                  "numbers, no dice, no chances.";
        RefereeQuestion telling;
        telling.site = "season";
        telling.prompt = prompt.str();
        std::string told;
        if (!referee_(telling, told, error)) {
            return Result::failed("the director did not tell the season: " +
                                  error);
        }
        told = trimmed(told);
        if (told.empty()) {
            return Result::failed("the director told nothing of the season");
        }

        kg::KGOpBatchReport report;
        const std::vector<kg::KGOp> ops = {
            create_entity("SeasonLived", "season",
                          {{"name", "season at " + std::to_string(after)},
                           {"event_type", "SEASON_LIVED"},
                           {"season_mode", std::to_string(taken->id)},
                           {"lived_year", std::to_string(after)},
                           {"season_plan", plan},
                           {"season_telling", told}}),
            relate(context.target, "LIVED", "season"),
            set_property(context.target, "age_years",
                         std::to_string(after)),
        };
        if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
            return Result::failed("the season was refused: " +
                                  report.error);
        }
        season_hints_.clear();
        log_.push_back("a season spent in " + taken->name + ": " + plan);
        log_.push_back(told);
        return Result::advance();
    }

    // Asking: the ways come from the graph, and for each the director
    // says what THIS person would do with the coming year that way,
    // fresh each season, so the choice is a plan and not a word.
    std::ostringstream prompt;
    std::string error;
    describe(world_, context.target, prompt, error);
    if (!error.empty()) return Result::failed(error);
    prompt << "\nSEASONS ALREADY LIVED, so this one is unlike them:";
    bool any = false;
    for (const kg::EntityID lived :
         world_.getRelated(context.target, "LIVED")) {
        if (world_.getType(lived) != "SeasonLived") continue;
        prompt << "\n  " << world_.getProperty(lived, "season_plan");
        any = true;
    }
    if (!any) prompt << " none; this is the first";
    prompt << "\n\nTHE WAYS A SEASON IS SPENT, in the book's words:\n";
    RefereeQuestion question;
    question.site = "seasons";
    for (const auto& mode : modes) {
        prompt << "  " << mode.name << " | " << mode.quote << "\n";
        question.allowed.push_back(mode.name);
    }
    question.vocab["ways"] = question.allowed;
    prompt << "\nFor EACH way, one clause: what this person would do with "
              "the coming year spent that way, specific to their trade, "
              "their place and their record, and unlike any season "
              "already lived. Plans, not outcomes.\nAnswer one line per "
              "way, every way, and nothing else:\n  <way> | <clause>";
    question.prompt = prompt.str();
    const auto accept = [&](const std::string& reply, std::string& why) {
        season_hints_.clear();
        std::istringstream lines(reply);
        std::string line;
        while (std::getline(lines, line)) {
            const auto parts = split_bars(line);
            if (parts.size() < 2 || parts[0].empty()) continue;
            season_hints_[parts[0]] = parts[1];
        }
        for (const auto& mode : modes) {
            const auto it = season_hints_.find(mode.name);
            if (it == season_hints_.end() || it->second.empty()) {
                why = "no plan was given for '" + mode.name +
                      "'; every way gets one";
                return false;
            }
        }
        return true;
    };
    if (!ask_in_shape(question, accept, error)) return Result::failed(error);

    std::vector<Choice> ways;
    for (const auto& mode : modes) {
        Choice way;
        way.key = mode.name;
        way.label = mode.name;
        way.detail = season_hints_[mode.name];
        way.subject = mode.id;
        ways.push_back(std::move(way));
    }
    Choice done;
    done.key = kEnough;
    done.label = "That is enough. The making ends here.";
    ways.push_back(std::move(done));
    return Result::pending("How is this year spent?", std::move(ways));
}

Session::Result Session::propose_arrival(const Context& context) {
    const auto kinds = named_of(world_, "MomentKind", "moment_kind_key");
    if (kinds.empty()) {
        return Result::failed("the world holds no kind a moment could be");
    }
    std::string error;
    double floor = 0.0;
    double ceiling = 0.0;
    if (!constant_real("arrival_chance_floor", floor, error) ||
        !constant_real("arrival_chance_ceiling", ceiling, error)) {
        return Result::failed(error);
    }
    const std::string floor_text = constant_text(world_, "arrival_chance_floor");
    const std::string ceiling_text =
        constant_text(world_, "arrival_chance_ceiling");

    std::ostringstream prompt;
    describe(world_, context.target, prompt, error);
    if (!error.empty()) return Result::failed(error);
    prompt << "\nTHE KINDS OF TROUBLE, in the book's words:\n";
    RefereeQuestion question;
    question.site = "arrival";
    question.low = floor_text;
    question.high = ceiling_text;
    for (const auto& kind : kinds) {
        prompt << "  " << kind.key << " | " << kind.quote << "\n";
        question.allowed.push_back(kind.key);
    }
    question.vocab["kinds"] = question.allowed;
    prompt << "\nA season is passing. For EACH kind, state the chance that "
              "kind finds this life this season, given the trade, the "
              "place and how the season is spent. Events belong to the "
              "world, not to this person: state what this place and trade "
              "send, never what would test or suit them. A probability "
              "between " << floor_text << " and " << ceiling_text
           << " inclusive.\nAnswer one line per kind, every kind, and "
              "nothing else:\n  <kind key> | <chance>";
    question.prompt = prompt.str();

    std::map<std::string, std::string> stated;
    std::map<std::string, double> rated;
    const auto accept = [&](const std::string& reply, std::string& why) {
        stated.clear();
        rated.clear();
        std::istringstream lines(reply);
        std::string line;
        while (std::getline(lines, line)) {
            const auto parts = split_bars(line);
            if (parts.size() != 2 || parts[0].empty()) continue;
            stated[parts[0]] = parts[1];
        }
        for (const auto& kind : kinds) {
            const auto it = stated.find(kind.key);
            if (it == stated.end()) {
                why = "no chance was rated for '" + kind.key +
                      "'; every kind is rated, every season";
                return false;
            }
            double chance = 0.0;
            if (!as_probability(it->second, chance)) {
                why = "the chance '" + it->second + "' for '" + kind.key +
                      "' is not a probability";
                return false;
            }
            if (chance < floor || chance > ceiling) {
                why = "'" + kind.key + "' was rated at " + it->second +
                      ", outside the book's bounds of " + floor_text +
                      " to " + ceiling_text;
                return false;
            }
            rated[kind.key] = chance;
        }
        return true;
    };
    if (!ask_in_shape(question, accept, error)) return Result::failed(error);

    landed_.clear();
    std::vector<kg::KGOp> ops;
    int alias = 0;
    for (const auto& kind : kinds) {
        const auto it = stated.find(kind.key);
        const double chance = rated[kind.key];
        bool ok = false;
        const double draw = draw_against(dice_, "arrival", kind.id, ok);
        if (!ok) return Result::failed("the arrival draw could not be made");
        const bool landed = draw <= chance;
        const std::string name = "arrival " + std::to_string(++alias);
        ops.push_back(create_entity(
            "ArrivalProposed", name,
            {{"name", kind.name + " rated " + it->second},
             {"event_type", "ARRIVAL_PROPOSED"},
             {"moment_kind", std::to_string(kind.id)},
             {"moment_chance", it->second},
             {"moment_draw", fixed3(draw)},
             {"arrival_landed", landed ? "true" : "false"}}));
        ops.push_back(relate(context.target, "LIVED", name));
        if (landed) landed_.push_back(kind.id);
        log_.push_back(kind.name + " rated " + it->second + ", drew " +
                       fixed3(draw) + (landed ? ": it lands" : ""));
    }
    kg::KGOpBatchReport report;
    if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
        return Result::failed("the arrival record was refused: " +
                              report.error);
    }
    if (landed_.empty()) {
        log_.push_back("the season passes unbroken");
        return Result::advance("unbroken");
    }
    return Result::advance();
}

bool Session::parse_effect(const std::string& line, Effect& out,
                           std::string& error) const {
    const auto parts = split_bars(line);
    if (parts.empty() || parts[0].empty()) {
        error = "an empty effect";
        return false;
    }
    out.key = parts[0];
    out.args.assign(parts.begin() + 1, parts.end());
    out.text = trimmed(line);
    const auto handler = effects_.find(out.key);
    if (handler == effects_.end()) {
        error = "'" + out.text + "': '" + out.key +
                "' is not a thing a moment may do";
        return false;
    }
    return handler->second.validate(world_, character_, out, error);
}

bool Session::validate_effects(const std::vector<Effect>& effects,
                               std::string& error) const {
    // The rung caps the list. Its cap, whether it may turn the life,
    // and whether it is unbounded are all ON the rung, read here.
    const bool unbounded =
        world_.getProperty(weight_, "weight_unbounded") == "true";
    const bool may_turn =
        world_.getProperty(weight_, "weight_may_turn") == "true";
    long long limit = 0;
    if (!unbounded &&
        !as_int(world_.getProperty(weight_, "weight_effect_limit"), limit)) {
        error = "the rung '" + world_.getProperty(weight_, "name") +
                "' states no cap and is not unbounded";
        return false;
    }
    if (!unbounded && static_cast<long long>(effects.size()) > limit) {
        error = "a moment at " + world_.getProperty(weight_, "name") +
                " may do " + std::to_string(limit) + " thing(s), and " +
                std::to_string(effects.size()) + " were written";
        return false;
    }
    std::map<std::string, bool> turns;
    std::map<std::string, bool> moves;
    for (const kg::EntityID id : world_.findByType("EffectKind")) {
        const std::string key = world_.getProperty(id, "effect_key");
        turns[key] = world_.getProperty(id, "effect_turns_life") == "true";
        moves[key] =
            world_.getProperty(id, "effect_moves_characteristic") == "true";
    }
    std::map<std::string, int> pull;
    for (const auto& effect : effects) {
        if (turns[effect.key] && !may_turn) {
            error = "a moment at " + world_.getProperty(weight_, "name") +
                    " may not turn the life, and '" + effect.text +
                    "' would";
            return false;
        }
        if (moves[effect.key] && effect.args.size() == 2) {
            long long delta = 0;
            as_int(effect.args[1], delta);
            const int sign = delta < 0 ? -1 : 1;
            auto& seen = pull[effect.args[0]];
            if (seen != 0 && seen != sign) {
                error = "two effects pull '" + effect.args[0] +
                        "' in opposite directions in one moment; the book "
                        "refuses that rather than netting it";
                return false;
            }
            seen = sign;
        }
    }
    return true;
}

bool Session::resolve_door(const Context& context, const OfferedDoor& door,
                           std::string& route, std::string& error) {
    bool ok = false;
    const double draw = draw_against(dice_, "moment", landed_.front(), ok);
    if (!ok) {
        error = "the draw could not be made";
        return false;
    }
    const bool against = draw <= door.chance;
    const auto& lands = against ? door.risks : door.reaches;

    std::ostringstream telling;
    telling << "THE SITUATION:\n" << situation_ << "\n\nTHE DOOR TAKEN: "
            << door.label << "\nThe chance of it going against them was "
            << door.chance_text << ". The engine drew " << fixed3(draw)
            << ": it " << (against ? "went against them" : "spared them")
            << ".\nWHAT LANDS, and nothing else does:";
    if (lands.empty()) telling << " nothing lasting; only the record.";
    for (const auto& effect : lands) telling << "\n  " << effect.text;
    telling << "\n\nTell what it did, past tense, two sentences at most. "
               "No numbers, no dice, no chances in the telling. What "
               "lands is exactly the list above.";
    RefereeQuestion aftermath;
    aftermath.site = "moment.aftermath";
    aftermath.prompt = telling.str();
    std::string outcome;
    if (!referee_(aftermath, outcome, error)) {
        error = "the referee did not answer: " + error;
        return false;
    }
    outcome = trimmed(outcome);
    if (outcome.empty()) {
        error = "the referee told nothing of the outcome; a moment that "
                "leaves no telling leaves no record";
        return false;
    }

    std::vector<kg::KGOp> ops;
    int alias = 0;
    const std::string weight_key = world_.getProperty(weight_, "weight_key");
    for (const kg::EntityID kind : landed_) {
        const std::string name = "moment " + std::to_string(++alias);
        ops.push_back(create_entity(
            "MomentFaced", name,
            {{"name", "moment of " + world_.getProperty(kind, "name")},
             {"event_type", "MOMENT_FACED"},
             {"moment_kind", std::to_string(kind)},
             {"moment_chance", door.chance_text},
             {"moment_draw", fixed3(draw)},
             {"moment_went_against", against ? "true" : "false"},
             {"moment_situation", situation_},
             {"moment_outcome", outcome},
             {"moment_weight", weight_key},
             {"moment_door", door.key}}));
        ops.push_back(relate(context.target, "LIVED", name));
    }
    const size_t turns_before = [&] {
        size_t n = 0;
        for (const kg::EntityID e : world_.getRelated(context.target, "LIVED"))
            if (world_.getType(e) == "TurnTaken") ++n;
        return n;
    }();
    EffectSite site{world_, context.target, context_, ops, alias, "moment 1"};
    for (const auto& effect : lands) {
        if (!effects_.at(effect.key).apply(site, effect, error)) return false;
    }
    kg::KGOpBatchReport report;
    if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
        error = "the moment was refused: " + report.error;
        return false;
    }

    // A turn that ends the life ends the making. Which turn does is a
    // fact on the Turn entity, read off the book, never spelled here.
    bool ended = false;
    size_t turns_seen = 0;
    for (const kg::EntityID e : world_.getRelated(context.target, "LIVED")) {
        if (world_.getType(e) != "TurnTaken") continue;
        if (++turns_seen <= turns_before) continue;
        const std::string key = world_.getProperty(e, "turn_key");
        for (const kg::EntityID turn : world_.findByType("Turn")) {
            if (world_.getProperty(turn, "turn_key") == key &&
                world_.getProperty(turn, "turn_ends_life") == "true") {
                ended = true;
            }
        }
    }
    log_.push_back("the door: " + door.label + " (chance " +
                   door.chance_text + ", drew " + fixed3(draw) +
                   (against ? ", against them)" : ", spared them)"));
    log_.push_back(outcome);
    landed_.clear();
    doors_.clear();
    situation_.clear();
    weight_ = kg::INVALID_ENTITY;
    route = ended ? "ended" : "continue";
    return true;
}

Session::Result Session::face_moment(const Context& context) {
    if (landed_.empty()) {
        return Result::failed("a moment was to be faced and nothing landed");
    }
    const std::string players = players_door_key();
    if (players.empty()) {
        return Result::failed("the book names no door as the player's");
    }
    std::string error;
    double floor = 0.0;
    double ceiling = 0.0;
    if (!constant_real("moment_chance_floor", floor, error) ||
        !constant_real("moment_chance_ceiling", ceiling, error)) {
        return Result::failed(error);
    }
    const std::string floor_text = constant_text(world_, "moment_chance_floor");
    const std::string ceiling_text =
        constant_text(world_, "moment_chance_ceiling");

    // The effect grammar, from the catalog and the graph: each kind
    // the book allows, its shape, and the words it may draw on.
    const auto grammar = [&](std::ostringstream& out) {
        out << "\nWHAT A MOMENT MAY DO, one effect per line, fields "
               "separated by ' | ', exactly:\n";
        for (const auto& kind : named_of(world_, "EffectKind", "effect_key")) {
            const auto handler = effects_.find(kind.key);
            if (handler == effects_.end()) continue;
            out << "  " << kind.key << " " << handler->second.usage << "\n"
                << "      " << kind.quote << "\n"
                << "      for example:  " << handler->second.example(world_)
                << "\n";
        }
        out << "  standings: ";
        for (const auto& s : named_of(world_, "StandingKind", "standing_key"))
            out << s.key << " ";
        out << "\n  turns: ";
        for (const auto& t : named_of(world_, "Turn", "turn_key"))
            out << t.key << " ";
        out << "\n  When a door risks nothing lasting, or reaches for "
               "nothing lasting, write NO risk or reach line for it at "
               "all; never a placeholder. Most moments leave only their "
               "record.\n";
    };
    const auto parse_lists = [&](const std::vector<std::string>& lines,
                                 size_t from, std::vector<Effect>& risks,
                                 std::vector<Effect>& reaches,
                                 std::string& why) {
        for (size_t i = from; i < lines.size(); ++i) {
            const auto parts = split_bars(lines[i]);
            if (parts.empty() || parts[0].empty()) continue;
            std::vector<Effect>* list = nullptr;
            if (parts[0] == "risk") list = &risks;
            else if (parts[0] == "reach") list = &reaches;
            else break;
            if (parts.size() < 2 || parts[1].empty()) continue;
            Effect effect;
            const std::string rest =
                trimmed(lines[i].substr(lines[i].find('|') + 1));
            if (!parse_effect(rest, effect, why)) return false;
            list->push_back(std::move(effect));
        }
        return true;
    };

    // Resuming: a door, or the player's words for their own door.
    if (context.input.has_value()) {
        const std::string& answer = *context.input;
        std::string route;
        if (awaiting_plan_) {
            awaiting_plan_ = false;
            std::ostringstream prompt;
            describe(world_, context.target, prompt, error);
            if (!error.empty()) return Result::failed(error);
            prompt << "\nTHE SITUATION:\n" << situation_
                   << "\n\nTHE PLAYER'S OWN PLAN:\n" << answer << "\n";
            grammar(prompt);
            const bool unbounded =
                world_.getProperty(weight_, "weight_unbounded") == "true";
            prompt << "\nPrice it. The chance of it going against them, "
                      "between " << floor_text << " and " << ceiling_text
                   << "; a plan that uses the record and the event's own "
                      "details is priced better than one that ignores "
                      "them, and never worse. The world prices; it never "
                      "gifts. The moment's weight is "
                   << world_.getProperty(weight_, "weight_key")
                   << ": at most "
                   << (unbounded ? std::string("any number of")
                                 : world_.getProperty(weight_,
                                                      "weight_effect_limit"))
                   << " effect line(s) in the risk list and as many in "
                      "the reach list.\n"
                      "Answer in exactly this shape, first line first, no "
                      "prose before or after it:\n"
                      "  chance | <probability written as a decimal>\n"
                      "  risk | <effect>\n  reach | <effect>\n"
                      "For example, a plan priced at four in ten with "
                      "one risk and nothing reached:\n"
                      "  chance | 0.4\n"
                      "  risk | leave_mark | a grudge on the ridge\n"
                      "(as many risk and reach lines as the rung allows, "
                      "or none)";
            RefereeQuestion price;
            price.site = "price";
            price.prompt = prompt.str();
            price.low = floor_text;
            price.high = ceiling_text;
            OfferedDoor door;
            const auto accept = [&](const std::string& reply,
                                    std::string& why) {
                door = OfferedDoor{};
                door.key = players;
                door.label = answer;
                std::vector<std::string> lines;
                std::istringstream in(reply);
                std::string line;
                while (std::getline(in, line)) lines.push_back(line);
                size_t at = 0;
                for (; at < lines.size(); ++at) {
                    const auto parts = split_bars(lines[at]);
                    if (parts.size() == 2 && parts[0] == "chance") {
                        door.chance_text = parts[1];
                        ++at;
                        break;
                    }
                }
                if (door.chance_text.empty() ||
                    !as_probability(door.chance_text, door.chance)) {
                    why = "the plan was priced with no probability";
                    return false;
                }
                if (door.chance < floor || door.chance > ceiling) {
                    why = "the plan was priced at " + door.chance_text +
                          ", outside the book's bounds of " + floor_text +
                          " to " + ceiling_text;
                    return false;
                }
                return parse_lists(lines, at, door.risks, door.reaches,
                                   why) &&
                       validate_effects(door.risks, why) &&
                       validate_effects(door.reaches, why);
            };
            if (!ask_in_shape(price, accept, error)) {
                return Result::failed("the price was refused: " + error);
            }
            if (!resolve_door(context, door, route, error)) {
                return Result::failed(error);
            }
            return Result::advance(route);
        }
        if (answer == players) {
            awaiting_plan_ = true;
            Choice tell;
            tell.key = players;
            for (const auto& door : named_of(world_, "Door", "door_key")) {
                if (door.key == players) tell.label = door.quote;
            }
            return Result::pending(situation_ + "\n\nSay what you would do.",
                                   {tell});
        }
        for (const auto& door : doors_) {
            if (door.key != answer) continue;
            if (!resolve_door(context, door, route, error)) {
                return Result::failed(error);
            }
            return Result::advance(route);
        }
        return Result::failed("'" + answer + "' is not a door on the table");
    }

    // Asking: the director dresses what landed.
    std::ostringstream prompt;
    describe(world_, context.target, prompt, error);
    if (!error.empty()) return Result::failed(error);
    RefereeQuestion question;
    question.site = "doors";
    question.low = floor_text;
    question.high = ceiling_text;
    prompt << "\nWHAT LANDED THIS SEASON, in the book's words:\n";
    bool priced = true;
    for (const kg::EntityID id : landed_) {
        const std::string key = world_.getProperty(id, "moment_kind_key");
        prompt << "  " << key << " | " << world_.getProperty(id, "source_quote")
               << "\n";
        question.vocab["kinds"].push_back(key);
        if (stage_count(id) == 0) priced = false;
        for (const kg::EntityID ex : world_.findByType("PlaybookExample")) {
            if (world_.getProperty(ex, "moment_kind_key") != key) continue;
            prompt << "      example, at " << world_.getProperty(ex, "weight_key")
                   << ": " << world_.getProperty(ex, "source_quote") << "\n";
        }
        for (const kg::EntityID ex : world_.findByType("PlaybookDoorExample")) {
            if (world_.getProperty(ex, "moment_kind_key") != key) continue;
            prompt << "      example door: "
                   << world_.getProperty(ex, "source_quote") << "\n";
        }
    }
    prompt << "\nTHE WEIGHTS, and what each permits:\n";
    for (const auto& w : named_of(world_, "Weight", "weight_key")) {
        prompt << "  " << w.key << " | " << w.quote << "\n";
        question.vocab["weights"].push_back(w.key);
    }
    prompt << "\nTHE DOORS YOU WRITE, in this order:\n";
    for (const auto& d : named_of(world_, "Door", "door_key")) {
        if (d.key == players) continue;
        prompt << "  " << d.key << " | " << d.quote << "\n";
        question.vocab["doors"].push_back(d.key);
    }
    grammar(prompt);
    prompt << "\nEvents belong to the world, not to this person: dress what "
              "landed as this place and trade would send it, never as a "
              "test built for them. State the weight. Describe the "
              "situation arriving, present tense, two or three sentences, "
              "no numbers. Then each door you write: its chance of going "
              "against them, between " << floor_text << " and " << ceiling_text
           << ", what it risks and what it reaches for, as effects the "
              "rung permits.\nAnswer exactly in this shape:\n"
              "  weight | <one of:";
    for (const auto& w : question.vocab["weights"]) prompt << " " << w;
    prompt << ">\n  situation\n  <the situation>\n"
              "  door | <one of:";
    for (const auto& d : question.vocab["doors"]) prompt << " " << d;
    prompt << "> | <chance> | <one clause, the door as it is for them>\n"
              "  risk | <effect>\n  reach | <effect>\n"
              "(the door, risk and reach lines repeat for every door "
              "listed above; every door listed above appears exactly "
              "once, in every answer)";
    question.prompt = prompt.str();

    const auto accept = [&](const std::string& reply, std::string& why) {
        std::vector<std::string> lines;
        {
            std::istringstream in(reply);
            std::string line;
            while (std::getline(in, line)) lines.push_back(line);
        }
        weight_ = kg::INVALID_ENTITY;
        situation_.clear();
        doors_.clear();
        size_t at = 0;
        for (; at < lines.size(); ++at) {
            const auto parts = split_bars(lines[at]);
            if (parts.size() == 2 && parts[0] == "weight") {
                for (const auto& w : named_of(world_, "Weight", "weight_key")) {
                    if (w.key == parts[1]) weight_ = w.id;
                }
                if (weight_ == kg::INVALID_ENTITY) {
                    why = "the weight '" + parts[1] +
                          "' is not a rung the book has";
                    return false;
                }
                ++at;
                break;
            }
        }
        if (weight_ == kg::INVALID_ENTITY) {
            why = "no weight was stated";
            return false;
        }
        for (; at < lines.size(); ++at) {
            const std::string line = trimmed(lines[at]);
            if (line == "situation") { ++at; break; }
        }
        for (; at < lines.size(); ++at) {
            const auto parts = split_bars(lines[at]);
            if (!parts.empty() && parts[0] == "door") break;
            if (!trimmed(lines[at]).empty()) {
                if (!situation_.empty()) situation_ += "\n";
                situation_ += trimmed(lines[at]);
            }
        }
        if (situation_.empty()) {
            why = "the moment has no situation";
            return false;
        }
        while (at < lines.size()) {
            const auto parts = split_bars(lines[at]);
            if (parts.size() < 4 || parts[0] != "door") { ++at; continue; }
            OfferedDoor door;
            door.key = parts[1];
            door.chance_text = parts[2];
            door.label = parts[3];
            const auto& written = question.vocab["doors"];
            if (std::find(written.begin(), written.end(), door.key) ==
                written.end()) {
                why = "the door '" + door.key +
                      "' is not one the book names as the director's";
                return false;
            }
            if (!as_probability(door.chance_text, door.chance)) {
                why = "the door '" + door.key + "' carries no probability";
                return false;
            }
            if (door.chance < floor || door.chance > ceiling) {
                why = "the door '" + door.key + "' is priced at " +
                      door.chance_text + ", outside the book's bounds of " +
                      floor_text + " to " + ceiling_text;
                return false;
            }
            ++at;
            std::string inner;
            if (!parse_lists(lines, at, door.risks, door.reaches, inner) ||
                !validate_effects(door.risks, inner) ||
                !validate_effects(door.reaches, inner)) {
                why = "the door '" + door.key + "' was refused: " + inner;
                return false;
            }
            doors_.push_back(std::move(door));
            while (at < lines.size()) {
                const auto next = split_bars(lines[at]);
                if (!next.empty() && next[0] == "door") break;
                ++at;
            }
        }
        for (const auto& expected : question.vocab["doors"]) {
            size_t seen = 0;
            for (const auto& door : doors_) if (door.key == expected) ++seen;
            if (seen != 1) {
                why = "the door '" + expected + "' was written " +
                      std::to_string(seen) + " times; every door is "
                      "written once";
                return false;
            }
        }
        return true;
    };
    if (!ask_in_shape(question, accept, error)) return Result::failed(error);

    std::vector<Choice> offered;
    for (const auto& door : doors_) {
        Choice choice;
        choice.key = door.key;
        choice.label = door.label;
        if (priced) {
            choice.detail = "chance " + door.chance_text;
            if (!door.risks.empty()) {
                choice.detail += " | risks:";
                for (const auto& e : door.risks) choice.detail += " " + e.text;
            }
            if (!door.reaches.empty()) {
                choice.detail += " | reaches:";
                for (const auto& e : door.reaches)
                    choice.detail += " " + e.text;
            }
        } else {
            choice.detail = "unpriced. This is a kind of trouble never "
                            "faced before, so the doors show without their "
                            "prices: what each risks and reaches for is "
                            "there, but not the odds.";
        }
        offered.push_back(std::move(choice));
    }
    Choice own;
    own.key = players;
    for (const auto& door : named_of(world_, "Door", "door_key")) {
        if (door.key == players) own.label = door.quote;
    }
    offered.push_back(std::move(own));
    log_.push_back("the season breaks at " +
                   world_.getProperty(weight_, "name"));
    log_.push_back(situation_);
    return Result::pending(situation_ + "\n\nWhich door?", std::move(offered));
}

Session::Result Session::end_making(const Context&) {
    return Result::complete();
}

}  // namespace voyager
