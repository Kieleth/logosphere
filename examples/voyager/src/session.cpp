#include "session.h"

#include "procedure_catalog.h"
#include "sheet.h"

#include "logosphere/kg/kg_ops_transaction.h"
#include "logosphere/kg/ontology_registry.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace voyager {
namespace {

using logosphere::rules::ProcedurePrimitiveResult;

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

kg::KGOp relate(kg::EntityID from, std::string relation, std::string to) {
    kg::KGOpSetRelation op;
    op.from.id = from;
    op.relation = std::move(relation);
    op.to.symbolic = std::move(to);
    return op;
}

// A property whose value is an entity created earlier in the same
// batch, named by its alias. The batch resolves it; nothing here has to
// know the id before it exists.
kg::KGOp set_property_ref(kg::EntityID target, std::string property,
                          std::string alias) {
    kg::KGOpSetProperty op;
    op.target.id = target;
    op.property = std::move(property);
    op.value = "@" + alias;
    return op;
}

bool as_int(const std::string& text, long long& out) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    if (*begin == '+') ++begin;
    const auto parsed = std::from_chars(begin, end, out);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

// The book prints the sentence with its own bold run-in ("**Athlete:**
// Individual that ..."). The DATA keeps the bytes the book has; this is
// only what a panel shows.
std::string without_run_in(const std::string& sentence,
                           const std::string& name) {
    const std::string mark = "**" + name + ":** ";
    if (sentence.rfind(mark, 0) == 0) return sentence.substr(mark.size());
    return sentence;
}

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

Session::Session(kg::KGModule& world, logosphere::dice::DiceService& dice)
    : world_(world), dice_(dice),
      primitives_(make_procedure_registry()),
      runner_(world, primitives_) {
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
    bind("face_moment", &Session::face_moment);
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
    long long majority = 0;
    if (!constant("age_of_majority", majority, error)) return false;
    kg::KGOpBatchReport report;
    const std::vector<kg::KGOp> ops = {
        set_property(character_, "age_years", std::to_string(majority))};
    if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
        error = "could not set the character's age: " + report.error;
        return false;
    }

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
    if (std::find(offered_.begin(), offered_.end(), answer) ==
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

Session::Result Session::spend_season(const Context& context) {
    struct Mode {
        kg::EntityID id = kg::INVALID_ENTITY;
        std::string  name;
        std::string  quote;
    };
    std::vector<Mode> modes;
    for (const kg::EntityID id : world_.findByType("SeasonMode")) {
        modes.push_back({id, world_.getProperty(id, "name"),
                         world_.getProperty(id, "source_quote")});
    }
    if (modes.empty()) {
        return Result::failed(
            "the world holds no way to spend a season; load the game's "
            "own book");
    }

    // Resuming: the answer is a way the book fixed.
    if (context.input.has_value()) {
        const std::string& answer = *context.input;
        const Mode* taken = nullptr;
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
        kg::KGOpBatchReport report;
        const std::vector<kg::KGOp> ops = {
            create_entity("SeasonLived", "season",
                          {{"name",
                            "season at " + std::to_string(after)},
                           {"event_type", "SEASON_LIVED"},
                           {"season_mode", std::to_string(taken->id)},
                           {"lived_year", std::to_string(after)}}),
            relate(context.target, "LIVED", "season"),
            set_property(context.target, "age_years",
                         std::to_string(after)),
        };
        if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
            return Result::failed("the season was refused: " +
                                  report.error);
        }
        log_.push_back("a season spent in " + taken->name);
        return Result::advance();
    }

    // Asking: the ways come from the graph, the sentence that fixes
    // them is the prompt, and this code knows neither the count nor
    // the names.
    std::vector<Choice> ways;
    for (const auto& mode : modes) {
        Choice way;
        way.key = mode.name;
        way.label = mode.name;
        way.subject = mode.id;
        ways.push_back(std::move(way));
    }
    return Result::pending(modes.front().quote + "\n\nHow is this season "
                           "spent?", std::move(ways));
}

Session::Result Session::face_moment(const Context& context) {
    struct Kind {
        kg::EntityID id = kg::INVALID_ENTITY;
        std::string  key;
        std::string  name;
        std::string  quote;
    };
    std::vector<Kind> kinds;
    for (const kg::EntityID id : world_.findByType("MomentKind")) {
        kinds.push_back({id, world_.getProperty(id, "moment_kind_key"),
                         world_.getProperty(id, "name"),
                         world_.getProperty(id, "source_quote")});
    }
    if (kinds.empty()) {
        return Result::failed(
            "the world holds no kind a moment could be; load the game's "
            "own book");
    }

    std::string error;
    double floor = 0.0;
    double ceiling = 0.0;
    std::string floor_text;
    std::string ceiling_text;
    if (!constant_real("moment_chance_floor", floor, error) ||
        !constant_real("moment_chance_ceiling", ceiling, error)) {
        return Result::failed(error);
    }
    for (const kg::EntityID id : world_.findByType("RuleConstant")) {
        const std::string name = world_.getProperty(id, "name");
        if (name == "moment_chance_floor") {
            floor_text = world_.getProperty(id, "constant_value");
        } else if (name == "moment_chance_ceiling") {
            ceiling_text = world_.getProperty(id, "constant_value");
        }
    }

    Sheet sheet;
    if (!read_sheet(world_, context.target, sheet, error)) {
        return Result::failed(error);
    }
    std::string last_season;
    for (const kg::EntityID part :
         world_.getRelated(context.target, "LIVED")) {
        if (world_.getType(part) != "SeasonLived") continue;
        long long mode = 0;
        if (as_int(world_.getProperty(part, "season_mode"), mode)) {
            last_season = world_.getProperty(
                static_cast<kg::EntityID>(mode), "name");
        }
    }

    std::ostringstream prompt;
    prompt << "THIS PERSON, as the dice made them:\n";
    for (const auto& line : sheet.lines) {
        if (line.value.empty()) continue;
        prompt << "  " << line.label << " " << line.value << " ("
               << line.modifier << ")\n";
    }
    prompt << "\nWHERE THEY COME FROM:\n" << sheet.background << "\n";
    prompt << "\nTHE CAREER THEY ENTERED: " << sheet.career << "\n";
    prompt << "THE SEASON JUST SPENT: " << last_season << "\n";
    prompt << "\nTHE KINDS A MOMENT CAN BE, in the book's words:\n";
    RefereeQuestion question;
    question.site = "moment";
    question.low = floor_text;
    question.high = ceiling_text;
    for (const auto& kind : kinds) {
        prompt << "  " << kind.key << " | " << kind.quote << "\n";
        question.allowed.push_back(kind.key);
    }
    prompt << "\nWHAT THEY HAVE LIVED:";
    bool lived_any = false;
    for (const auto& kind : kinds) {
        const size_t lived = stage_count(kind.id);
        if (lived == 0) continue;
        prompt << " " << kind.key << " x" << lived;
        lived_any = true;
    }
    if (!lived_any) prompt << " nothing yet";
    prompt << "\n\nTheir season breaks. Pick the kind of trouble this "
              "life and this career invite, copying its key exactly "
              "from the list. Set the chance of it going against them, "
              "a probability between " << floor_text << " and "
           << ceiling_text << " inclusive. Then describe the situation "
              "arriving: present tense, two or three sentences, no "
              "numbers, no chances. They have not seen it coming.\n"
              "Answer as one line and then the situation:\n"
              "  <kind key> | <chance>\n"
              "  <the situation, on the following lines>";
    question.prompt = prompt.str();

    std::string answer;
    if (!referee_(question, answer, error)) {
        return Result::failed("the referee did not answer: " + error);
    }
    std::istringstream reply(trimmed(answer));
    std::string header;
    while (std::getline(reply, header) && trimmed(header).empty()) {
    }
    const size_t bar = header.find('|');
    if (bar == std::string::npos) {
        return Result::failed(
            "the referee's moment names no chance; a moment without "
            "odds is not a moment, and the run stops here");
    }
    const std::string key = trimmed(header.substr(0, bar));
    const std::string chance_text = trimmed(header.substr(bar + 1));
    const Kind* kind = nullptr;
    for (const auto& candidate : kinds) {
        if (candidate.key == key) kind = &candidate;
    }
    if (!kind) {
        return Result::failed("the referee offered '" + key +
                              "', which is not a kind the book defines");
    }
    double chance = 0.0;
    try {
        size_t end = 0;
        chance = std::stod(chance_text, &end);
        if (end != chance_text.size()) throw std::invalid_argument("");
    } catch (...) {
        return Result::failed("the referee's chance '" + chance_text +
                              "' is not a probability");
    }
    if (chance < floor || chance > ceiling) {
        return Result::failed(
            "the referee set the chance at " + chance_text +
            ", outside the book's bounds of " + floor_text + " to " +
            ceiling_text + "; certainty is not on offer, in either "
            "direction");
    }
    std::string situation;
    std::string line;
    while (std::getline(reply, line)) {
        if (!situation.empty()) situation += "\n";
        situation += line;
    }
    situation = trimmed(situation);
    if (situation.empty()) {
        return Result::failed(
            "the referee's moment has no situation; a chance with no "
            "scene is a die with no game");
    }

    // The engine draws; the referee never does. The granularity is
    // mechanism, not a number the book fixes: any sufficiently fine
    // uniform draw implements a probability, and one part in a
    // thousand is finer than any chance the referee states.
    const logosphere::dice::DiceExpression draw_die{1, 1000, 0, 1};
    const auto roll = dice_.roll(draw_die, "moment", "the draw",
                                 kind->id);
    if (roll.id == 0) {
        return Result::failed("the draw could not be made");
    }
    const double draw = static_cast<double>(roll.total) / 1000.0;
    const bool went_against = draw <= chance;
    std::ostringstream drawn;
    drawn.setf(std::ios::fixed);
    drawn.precision(3);
    drawn << draw;

    RefereeQuestion aftermath;
    aftermath.site = "moment.aftermath";
    aftermath.prompt =
        "THE SITUATION:\n" + situation + "\n\nThe chance of it going "
        "against them was " + chance_text + ". The engine drew " +
        drawn.str() + ": it " +
        (went_against ? "went against them" : "spared them") +
        ". Tell what it did, past tense, two sentences at most. No "
        "numbers, no dice, no chances in the telling.";
    std::string outcome;
    if (!referee_(aftermath, outcome, error)) {
        return Result::failed("the referee did not answer: " + error);
    }
    outcome = trimmed(outcome);
    if (outcome.empty()) {
        return Result::failed(
            "the referee told nothing of the outcome; a moment that "
            "leaves no telling leaves no record");
    }

    // The record the book requires: its kind, the chance as it was
    // stated, what was drawn, and what it did. Stage will be counted
    // from these and stored nowhere.
    kg::KGOpBatchReport report;
    const std::vector<kg::KGOp> ops = {
        create_entity("MomentFaced", "moment",
                      {{"name", "moment of " + kind->name},
                       {"event_type", "MOMENT_FACED"},
                       {"moment_kind", std::to_string(kind->id)},
                       {"moment_chance", chance_text},
                       {"moment_draw", drawn.str()},
                       {"moment_went_against",
                        went_against ? "true" : "false"},
                       {"moment_situation", situation},
                       {"moment_outcome", outcome}}),
        relate(context.target, "LIVED", "moment"),
    };
    if (!kg::apply_kg_ops_atomically(ops, world_, report)) {
        return Result::failed("the moment was refused: " + report.error);
    }
    log_.push_back("the season breaks: " + kind->name + " (chance " +
                   chance_text + ", drew " + drawn.str() +
                   (went_against ? ", against them)" : ", spared them)"));
    log_.push_back(situation);
    log_.push_back(outcome);
    return Result::complete();
}

}  // namespace voyager
