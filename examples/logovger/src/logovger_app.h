// Logovger: the chat window as a character-creation table.
//
// You are asked the book's own questions and you answer them. The
// career list, the throws, the skill tables and their outcomes all
// come out of the KG, loaded from a seed file the ingestion verifier
// passed; the dice come from the engine and every roll is citable.
// Nothing in this file knows what a career is worth.
//
// This is the a-b-c half of the a-b-c+L design (docs/RPG_MODULE.md):
// the baked choices are here, the free-form L option that hands the
// beat to a referee arrives in the same place later.

#ifndef LOGOVGER_APP_H
#define LOGOVGER_APP_H

#include "application.h"
#include "core/engine.h"
#include "core/game_time.h"
#include "ui/ui_system.h"

#include "chargen/chargen.h"
#include "chargen/rule_seeds.h"
#include "narrator.h"
#include "sheet_screen.h"
#include "chargen/procedure_catalog.h"
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/voyager_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"
#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"

#include <fstream>
#include <cstring>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace logovger {

// Where the game's own data lives. Set at build time so the app can
// be run from anywhere.
#ifndef LOGOVGER_GAME_DIR
#define LOGOVGER_GAME_DIR "."
#endif

class LogovgerApplication : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& kg = engine_->get_kg();
        kg.extendOntology(rulebook::ontology::registry());
        kg.extendOntology(cepheus_book1_skills::ontology::registry());
        kg.extendOntology(
            cepheus_book1_character_creation::ontology::registry());
        kg.extendOntology(voyager::ontology::registry());

        auto* ui = engine_->get_ui_system();
        if (ui) {
            ui->set_chat_theme(220, 220, 210,   // paper
                               150, 190, 255);  // cold blue accent
            screen_.build(*ui);
            screen_.on_answer = [this](const std::string& answer) {
                answer_typed(answer);
            };
            screen_.on_new_life = [this]() { start_life(); };
            screen_.on_inspect_key = [this](const std::string& key) {
                if (!session_) return;
                for (const auto& c : session_->choices()) {
                    if (c.key != key) continue;
                    // An option that knows what it is gets read
                    // properly; the rest are careers, named.
                    if (c.subject != kg::INVALID_ENTITY) {
                        screen_.inspect_entity(engine_->get_kg(), c.subject);
                    } else {
                        screen_.inspect_career(engine_->get_kg(), c.label);
                    }
                }
            };
        }

        std::string why;
        if (!load_rules(kg, why)) {
            say("The rules did not load: " + why);
            say("Nothing can be created without them. This is on "
                "purpose: missing rule data is a loud failure, never a "
                "default.");
            return;
        }

        std::string narrator_error;
        if (!narrator_.initialize(game_path("lore/voyager.md"),
                                  narrator_error)) {
            screen_.say("NARRATOR OFF: " + narrator_error,
                        SheetScreen::Tone::Bad);
        }
        screen_.say("The rulebook is in the graph: 24 careers, 48 throws, "
                    "150 rollable-table rows, all cited.");
        screen_.say("Click any value on the sheet to see where the book "
                    "says it.");
        screen_.say("");
        start_life();
    }

    // Ask for prose about what just happened, and put it on screen when
    // it arrives. The character keeps it: every narration is written
    // into the graph beside the facts it was written from.
    // Reproduce a specific life. Off unless the caller asks: lives
    // are drawn from real entropy by default.
public:
    void pin_seed(uint64_t seed) { pinned_seed_ = seed; }

private:
    void narrate_beat(const std::string& kind, int term,
                      std::vector<std::string> facts,
                      std::vector<uint64_t> rolls) {
        if (!narrator_.ready() || !session_) return;
        Beat beat;
        beat.kind = kind;
        beat.term = term;
        beat.facts = std::move(facts);
        beat.roll_ids = std::move(rolls);
        if (!character_for_narrator(session_->sheet(), beat.sheet)) return;
        // Hold the next decision until this beat has been told.
        screen_.set_waiting(true);
        waiting_since_ = 0.0f;
        const auto character = session_->sheet().id;
        const auto seed = seed_;
        narrator_.narrate(beat, seed,
            [this, beat, character, seed](const std::string& reply) {
                // A reply that outlived its life belongs to nobody.
                // Starting a new character while one is in flight used
                // to print the old character's story into the new
                // one's page, which is where the two contradictory
                // bios came from.
                if (seed != seed_) return;
                // An empty reply means the request failed. Say so
                // plainly and let play continue; a missing story is
                // not a reason to hold up a decision.
                if (reply.empty()) {
                    screen_.say("(the narrator had nothing to say about "
                                "that one)", SheetScreen::Tone::Plain);
                    screen_.set_waiting(false);
                    if (session_) screen_.show(engine_->get_kg(), *session_);
                    return;
                }
                // One reply, two places: the story on the left, and
                // one clipped clause appended to the file. The file is
                // written as the life happens, a line per interaction.
                std::string prose, file_line;
                Narrator::split_file_line(reply, prose, file_line);
                screen_.say("");
                screen_.say(prose, SheetScreen::Tone::Good);
                screen_.add_file_line(file_line);
                narrator_.record(engine_->get_kg(), character, beat, prose);
                screen_.set_waiting(false);
                if (session_) screen_.show(engine_->get_kg(), *session_);
            });
    }

    // The file's head: a name, a birthplace, a body, and one dry line
    // an assessor would write. Asked for once, at the start, and it
    // does NOT hold up play: the file fills in beside the rolls.
    void open_file() {
        if (!narrator_.ready() || !session_) return;
        Beat beat;
        beat.kind = "dossier";
        if (!character_for_narrator(session_->sheet(), beat.sheet)) return;
        const auto character = session_->sheet().id;
        const auto seed = seed_;
        narrator_.narrate(beat, seed,
            [this, beat, character, seed](const std::string& prose) {
                // Same rule as every other beat: a file opened for a
                // character who has been replaced is not this
                // character's file.
                if (seed != seed_ || prose.empty()) return;
                apply_file_head(prose);
                narrator_.record(engine_->get_kg(), character, beat, prose);
            });
    }

    // Four labelled lines in, four fields out. A line that does not
    // arrive stays empty rather than being invented here.
    void apply_file_head(const std::string& prose) {
        std::string name, born, build, note;
        std::istringstream in(prose);
        std::string line;
        auto take = [&line](const char* label, std::string& into) {
            const size_t n = std::strlen(label);
            if (line.size() < n || line.compare(0, n, label) != 0)
                return false;
            into = line.substr(n);
            while (!into.empty() && into.front() == ' ') into.erase(0, 1);
            while (!into.empty() &&
                   (into.back() == '\r' || into.back() == ' '))
                into.pop_back();
            return true;
        };
        while (std::getline(in, line)) {
            if (take("NAME:", name)) continue;
            if (take("BORN:", born)) continue;
            if (take("BUILD:", build)) continue;
            take("NOTE:", note);
        }
        character_name_ = name;
        screen_.set_dossier(name.empty() ? "(unnamed)" : name,
                            born, build, note);
    }

    // The file closes with someone's opinion of the life in it.
    void close_file() {
        if (!narrator_.ready() || !session_) return;
        Beat beat;
        beat.kind = "assessment";
        beat.term = session_->sheet().terms_served;
        for (const auto& e : session_->sheet().life)
            beat.facts.push_back("T" + std::to_string(e.term) + " " + e.what +
                                 (e.detail.empty() ? "" : ": " + e.detail));
        if (!character_for_narrator(session_->sheet(), beat.sheet)) return;
        const auto character = session_->sheet().id;
        screen_.set_assessment("... the file is being closed ...");
        const auto seed = seed_;
        narrator_.narrate(beat, seed,
            [this, beat, character, seed](const std::string& reply) {
                if (seed != seed_) return;   // a closed file, not this one
                if (reply.empty()) {
                    screen_.set_assessment("(no assessment was written)");
                    return;
                }
                std::string prose, file_line;
                Narrator::split_file_line(reply, prose, file_line);
                screen_.set_assessment(prose);
                screen_.add_file_line(file_line);
                narrator_.record(engine_->get_kg(), character, beat, prose);
            });
    }

    // One place answers arrive, whether clicked in the list or typed.
    void answer_typed(const std::string& text) {
        if (!session_) return;
        if (equals_ignoring_case(text, "again") ||
            equals_ignoring_case(text, "new")) { start_life(); return; }
        if (session_->finished()) {
            screen_.say("This life is finished. Type 'again' for another.");
            return;
        }
        std::string error;
        if (!session_->choose(text, error)) { screen_.say(error); return; }
        report_progress();
    }

    void update_game(float dt) override {
        screen_.tick(dt);          // fade the roll flashes
        narrator_.poll();          // prose arrives on the main thread
        // A narrator that never answers must not lock the game out.
        if (screen_.waiting()) {
            waiting_since_ += dt;
            if (waiting_since_ > 15.0f) {
                screen_.say("(the narrator did not answer in time; "
                            "carrying on)", SheetScreen::Tone::Bad);
                screen_.set_waiting(false);
                if (session_) screen_.show(engine_->get_kg(), *session_);
            }
        }
        if (!engine_) return;
        auto* ui = engine_->get_ui_system();
        if (!ui || !ui->has_pending_submit()) return;

        std::string text = ui->get_input_text();
        ui->clear_input_text();
        trim(text);
        if (text.empty()) return;
        say("> " + text);

        if (equals_ignoring_case(text, "again") ||
            equals_ignoring_case(text, "new")) {
            start_life();
            return;
        }
        if (!session_) return;

        if (session_->finished()) {
            say("This life is finished. Type 'again' for another.");
            return;
        }

        answer_typed(text);
    }

private:
    // --- the rules ----------------------------------------------------

    static std::string slurp(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return "";
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static std::string game_path(const std::string& rel) {
        return std::string(LOGOVGER_GAME_DIR) + "/" + rel;
    }

    // Verify, then load: the same two calls a game makes at start, and
    // the same the ingestion pipeline makes offline. A seed that
    // cannot prove its citations never reaches the table.
    bool load_rules(kg::KGModule& kg, std::string& why) {
        const auto procedures = make_chargen_procedure_registry();
        // A seed may reference what an earlier one owns, so
        // verification sees the same growing world the game does.
        std::vector<kg::SeedEnvelope> kept;
        kept.reserve(kRuleSeedCount);
        std::vector<const kg::SeedEnvelope*> loaded_before;
        for (const char* seed : kRuleSeeds) {
            const std::string json = slurp(game_path(seed));
            if (json.empty()) {
                why = std::string(seed) + " is unreadable";
                return false;
            }

            auto parsed = kg::parse_seed_envelope(json);
            if (!parsed.ok()) { why = parsed.error; return false; }

            const auto v = kg::verify_seed(
                parsed.seed, game_path("srd/cepheus"), kg.getRegistry(),
                &procedures, loaded_before);
            if (!v.ok()) {
                std::ostringstream o;
                for (const auto& viol : v.violations)
                    o << "[" << viol.check << "] " << viol.reason << " ";
                why = o.str();
                return false;
            }
            kg::SeedLoadReport report;
            if (!kg::load_seed(parsed.seed, kg, report)) {
                why = report.error;
                return false;
            }
            kept.push_back(std::move(parsed.seed));
            loaded_before.push_back(&kept.back());
        }
        chapter_ = logosphere::text::SourceDocument::parse_markdown(
            slurp(game_path("srd/cepheus/book1/character-creation.md")));
        skills_doc_ = logosphere::text::SourceDocument::parse_markdown(
            slurp(game_path("srd/cepheus/book1/skills.md")));
        screen_.set_sources(&chapter_, &skills_doc_);
        rules_loaded_ = true;
        return true;
    }

    // --- the table ----------------------------------------------------

    void start_life() {
        if (!rules_loaded_) return;
        auto& kg = engine_->get_kg();
        // Real randomness by default: a life should be a life, not
        // the same life every launch. A fixed seed is available with
        // --seed for reproducing a specific one, and the seed of the
        // life you are playing is printed so a good one can be asked
        // for again.
        seed_ = pinned_seed_ ? *pinned_seed_ + lives_ : random_seed();
        ++lives_;
        session_ = std::make_unique<ChargenSession>(kg, dice_);
        file_closed_ = false;
        std::string error;
        if (!session_->begin(seed_, error)) {
            say("Could not begin: " + error);
            session_.reset();
            return;
        }
        screen_.say("--- a new life, seed " + std::to_string(seed_) +
                    " ---");
        // Also on the console, where it can be copied into --seed.
        std::cout << "[logovger] life seed " << seed_ << std::endl;
        // The opening rolls are the BIO's material, not a term of
        // their own. Show them, collect nothing, and let the bio be
        // the one thing said about a character who has done nothing.
        screen_.clear_dossier();
        character_name_.clear();
        open_file();               // fills in beside the opening rolls
        suppress_beat_ = true;
        report_progress();
        suppress_beat_ = false;
        beat_facts_.clear();
        beat_rolls_.clear();
        beat_bad_ = false;
        narrate_beat("bio", 0, {}, {});
    }

    // Everything that happened since the last question, then the next
    // question. This is the whole loop.
    void report_progress() {
        for (const auto& e : session_->drain()) {
            std::string line = "  " + e.what;
            if (!e.detail.empty()) line += ": " + e.detail;
            if (e.roll_id) line += "   [roll #" +
                                   std::to_string(e.roll_id) + "]";
            // Colour by what actually happened, read from the event's
            // own words: the session says "survived" or "did not
            // survive", and the screen shows it landing.
            auto tone = SheetScreen::Tone::Plain;
            if (e.what.find("did not") != std::string::npos ||
                e.what.find("failed") != std::string::npos)
                tone = SheetScreen::Tone::Bad;
            else if (e.what.find("survived") != std::string::npos ||
                     e.what.find("qualified") != std::string::npos ||
                     e.what.find("gained") != std::string::npos)
                tone = SheetScreen::Tone::Good;
            else if (e.roll_id)
                tone = SheetScreen::Tone::Roll;
            screen_.say(line, tone);
            beat_facts_.push_back(e.what +
                                  (e.detail.empty() ? "" : ": " + e.detail));
            if (e.roll_id) beat_rolls_.push_back(e.roll_id);
            if (tone == SheetScreen::Tone::Bad) beat_bad_ = true;
        }
        // The prose is asked for AFTER the mechanics are on screen, so
        // nothing waits on the network. It slots in when it lands.
        if (!beat_facts_.empty() && !suppress_beat_) {
            const auto kind = session_->finished() ? "ending"
                            : (beat_bad_ ? "refusal" : "term");
            narrate_beat(kind, session_->sheet().terms_served,
                         beat_facts_, beat_rolls_);
            beat_facts_.clear();
            beat_rolls_.clear();
            beat_bad_ = false;
        }
        screen_.show(engine_->get_kg(), *session_);
        if (session_->finished()) {
            if (!file_closed_) { file_closed_ = true; close_file(); }
            screen_.say("");
            screen_.say(session_->prompt());
            screen_.say("Type 'again' for another life.");
        }
    }

    std::string options_line() const {
        if (!session_) return "";
        std::ostringstream o;
        for (const auto& c : session_->choices()) {
            o << "  [" << c.key << "] " << c.label;
            if (!c.detail.empty()) o << " - " << c.detail;
            o << "\n";
        }
        std::string s = o.str();
        if (!s.empty() && s.back() == '\n') s.pop_back();
        return s;
    }

    bool character_for_narrator(const logovger::CharacterSheet& sheet,
                                std::string& facts) {
        std::string error;
        if (format_character_facts(sheet, engine_->get_kg(), facts, error)) {
            return true;
        }
        screen_.say("NARRATOR SKIPPED: " + error, SheetScreen::Tone::Bad);
        return false;
    }

    static std::string sheet_line(const CharacterSheet& s) {
        std::ostringstream o;
        o << "  " << (s.career.empty() ? std::string("no career") : s.career)
          << " " << s.upp << ", age " << s.age_years << ", "
          << s.terms_served << " term(s)";
        if (!s.skills.empty()) {
            o << " -- ";
            for (size_t i = 0; i < s.skills.size(); ++i)
                o << (i ? ", " : "") << s.skills[i];
        }
        return o.str();
    }

    void say(const std::string& line) {
        if (auto* ui = engine_->get_ui_system()) ui->add_chat_message(line);
    }

    static void trim(std::string& s) {
        const auto a = s.find_first_not_of(" \t\r\n");
        const auto b = s.find_last_not_of(" \t\r\n");
        s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    }

    static bool equals_ignoring_case(const std::string& a, const char* b) {
        std::string x = a, y = b;
        for (auto& c : x) c = static_cast<char>(std::tolower(c));
        for (auto& c : y) c = static_cast<char>(std::tolower(c));
        return x == y;
    }

    SheetScreen                        screen_;
    Narrator                           narrator_;
    std::vector<std::string>           beat_facts_;
    std::vector<uint64_t>              beat_rolls_;
    bool                               beat_bad_ = false;
    float                              waiting_since_ = 0.0f;
    bool                               suppress_beat_ = false;
    bool                               file_closed_ = false;
    std::string                        character_name_;
    logosphere::text::SourceDocument   chapter_;
    logosphere::text::SourceDocument   skills_doc_;
    Engine*                            engine_ = nullptr;
    logosphere::dice::DiceService      dice_;
    std::unique_ptr<ChargenSession>    session_;
    bool                               rules_loaded_ = false;
    uint64_t                           seed_ = 0;
    // Off by default. Set with --seed to replay a particular life.
    std::optional<uint64_t>            pinned_seed_;
    uint64_t                           lives_ = 0;

    // Entropy from the machine, not from a counter. Two draws because
    // random_device yields 32 bits at a time on the platforms we run.
    static uint64_t random_seed() {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32) ^ rd();
    }
};

}  // namespace logovger

#endif  // LOGOVGER_APP_H
