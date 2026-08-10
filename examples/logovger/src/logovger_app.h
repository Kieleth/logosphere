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
#include "generated/rulebook_ontology_registry.h"
#include "generated/cepheus_book1_skills_ontology_registry.h"
#include "generated/cepheus_book1_character_creation_ontology_registry.h"
#include "logosphere/kg/seed_loader.h"
#include "logosphere/kg/seed_verifier.h"

#include <fstream>
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

        auto* ui = engine_->get_ui_system();
        if (ui) {
            ui->set_chat_theme(220, 220, 210,   // paper
                               150, 190, 255);  // cold blue accent
        }

        std::string why;
        if (!load_rules(kg, why)) {
            say("The rules did not load: " + why);
            say("Nothing can be created without them. This is on "
                "purpose: missing rule data is a loud failure, never a "
                "default.");
            return;
        }

        say("CHARACTER CREATION -- Cepheus Engine, absorbed.");
        say("Every number below is read from the rulebook in the "
            "knowledge graph, and every roll is the engine's, recorded "
            "and citable.");
        say("");
        start_life();
    }

    void update_game(float dt) override {
        (void)dt;
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

        std::string error;
        if (!session_->choose(text, error)) {
            say(error + ". " + options_line());
            return;
        }
        report_progress();
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
        const std::string json =
            slurp(game_path("seeds/cepheus_careers.json"));
        if (json.empty()) { why = "the careers seed is unreadable"; return false; }

        auto parsed = kg::parse_seed_envelope(json);
        if (!parsed.ok()) { why = parsed.error; return false; }

        const auto v = kg::verify_seed(parsed.seed,
                                       game_path("srd/cepheus"),
                                       kg.getRegistry());
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
        rules_loaded_ = true;
        return true;
    }

    // --- the table ----------------------------------------------------

    void start_life() {
        if (!rules_loaded_) return;
        auto& kg = engine_->get_kg();
        // A fresh stream per life, so each is its own reproducible
        // sequence rather than a continuation of the last.
        seed_ += 1;
        session_ = std::make_unique<ChargenSession>(kg, dice_);
        std::string error;
        if (!session_->begin(seed_, error)) {
            say("Could not begin: " + error);
            session_.reset();
            return;
        }
        say("--- a new life, seed " + std::to_string(seed_) + " ---");
        report_progress();
    }

    // Everything that happened since the last question, then the next
    // question. This is the whole loop.
    void report_progress() {
        for (const auto& e : session_->drain()) {
            std::string line = "  " + e.what;
            if (!e.detail.empty()) line += ": " + e.detail;
            if (e.roll_id) line += "   [roll #" + std::to_string(e.roll_id) + "]";
            say(line);
        }
        const auto& s = session_->sheet();
        if (session_->finished()) {
            say("");
            say(session_->prompt());
            say(sheet_line(s));
            say("Type 'again' for another life.");
            return;
        }
        say("");
        say(session_->prompt());
        say(options_line());
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

    Engine*                            engine_ = nullptr;
    logosphere::dice::DiceService      dice_;
    std::unique_ptr<ChargenSession>    session_;
    bool                               rules_loaded_ = false;
    uint64_t                           seed_ = 0;
};

}  // namespace logovger

#endif  // LOGOVGER_APP_H
