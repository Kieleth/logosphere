// Voyager, windowed. One screen, one character, and it stops at the
// first door.
//
// It shares every line of rules logic with voyager-headless: the same
// seeds, the same procedure, the same session, the same sheet view. The
// only thing that differs is who answers, which is the whole point of
// the referee being a seam.
//
// NO API, NO GAME. The referee is initialised before anything is
// rolled, and a missing key stops the run right there with the reason
// on screen. There is no reduced mode: a stand-in for "where does this
// person come from" would be policy nobody wrote, applied silently, and
// afterwards indistinguishable from a judgement somebody made.

#ifndef VOYAGER_APP_H
#define VOYAGER_APP_H

#include "application.h"
#include "core/engine.h"
#include "ui/ui_system.h"

#include "logosphere/core/dice_service.h"

#include "generated/rulebook_ontology_registry.h"
#include "generated/voyager_chargen_ontology_registry.h"

#include "model_referee.h"
#include "procedure_catalog.h"
#include "rule_loader.h"
#include "screen.h"
#include "session.h"

#include <chrono>
#include <memory>
#include <string>

namespace voyager {

#ifndef VOYAGER_GAME_DIR
#error "VOYAGER_GAME_DIR undefined: the game cannot find its own rules"
#endif
#ifndef VOYAGER_CORPUS_DIR
#error "VOYAGER_CORPUS_DIR undefined: declare the corpus this game reads"
#endif

inline std::string game_path(const std::string& relative) {
    return std::string(VOYAGER_GAME_DIR) + "/" + relative;
}

class VoyagerApplication : public Logosphere::IApplication {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    GLFWwindow* get_window() override { return nullptr; }
    const char* get_app_name() const override { return "Voyager"; }

    void pin_seed(uint64_t seed) { seed_ = seed; pinned_ = true; }

    // Bigger or smaller type, at any moment: plus and minus, on the
    // main row or on the keypad. The screen keeps the words rather
    // than the pixels, so one call re-lays the whole thing out.
    //
    // Consumed whether or not the size changed. The engine maps the
    // same two keys to its own UI scale, and that multiplier has to
    // stay at one or every click lands somewhere else.
    //
    // Keys reach a game only while the text field is hidden; an open
    // field takes every key, which is what typing is.
    bool handle_key(int key, int, int action, int) override {
        if (action != 1 && action != 2) return false;   // press, repeat
        int step = 0;
        switch (key) {
            case 61:                        // = and, shifted, +
            case 334: step = 1; break;      // keypad +
            case 45:                        // -
            case 333: step = -1; break;     // keypad -
            default: return false;
        }
        screen_.set_text_scale(screen_.text_scale() + step);
        return true;
    }

    void initialize_game(void* engine_ptr) override {
        engine_ = static_cast<Engine*>(engine_ptr);
        auto& world = engine_->get_kg();
        world.extendOntology(rulebook::ontology::registry());
        world.extendOntology(voyager_chargen::ontology::registry());

        if (auto* ui = engine_->get_ui_system()) {
            screen_.build(*ui);
            screen_.on_choice = [this](const std::string& key) {
                take_door(key);
            };
        }

        std::string why;
        if (!load_rules(world, VOYAGER_GAME_DIR, VOYAGER_CORPUS_DIR,
                        VOYAGER_BOOK_CORPUS_DIR,
                        make_procedure_registry(), why)) {
            screen_.say("The rules did not load: " + why +
                        "  Nothing can be made without them, and that is "
                        "on purpose: missing rule data is a loud failure, "
                        "never a default.");
            return;
        }

        // Before a single die. A referee that turns out to be missing
        // halfway through would leave a half-made character on screen
        // and read as a bug.
        if (!referee_.initialize(game_path("referee/brief.md"), why)) {
            screen_.say("No referee: " + why);
            return;
        }

        screen_.say("Rolling. The referee is reading the numbers.");
        armed_ = true;
    }

    // The two referee calls block, so the first frame paints before
    // they are made. Otherwise the window opens on nothing for as long
    // as the model takes to answer, which reads as a hang.
    void update_game(float) override {
        if (!armed_) return;
        if (!started_) {
            if (++frames_ < 2) return;
            started_ = true;
            start();
            return;
        }
        // The player's own door: their words arrive through the
        // engine's text field, and Enter hands them to the session.
        auto* ui = engine_->get_ui_system();
        if (session_ && session_->awaiting_plan() && ui &&
            ui->has_pending_submit()) {
            const std::string plan = ui->get_input_text();
            ui->clear_input_text();
            take_door(plan);
        }
    }

private:
    void start() {
        auto& world = engine_->get_kg();
        if (!pinned_) {
            seed_ = static_cast<uint64_t>(std::chrono::steady_clock::now()
                                              .time_since_epoch().count());
        }
        dice_.seed_stream("chargen", seed_);
        session_ = std::make_unique<Session>(world, dice_);
        session_->set_arbiter(referee_.who());
        session_->set_referee(
            [this](const RefereeQuestion& question, std::string& answer,
                   std::string& error) {
                return referee_.answer(question, answer, error);
            });
        std::string error;
        if (!session_->begin(error)) {
            screen_.say("The run stopped: " + error);
            session_.reset();
            return;
        }
        screen_.show(world, *session_);
    }

    void take_door(const std::string& key) {
        if (!session_ || session_->finished()) return;
        std::string error;
        if (!session_->choose(key, error)) {
            screen_.say("That was refused: " + error);
            return;
        }
        screen_.show(engine_->get_kg(), *session_);
    }

    Engine*                       engine_ = nullptr;
    Screen                        screen_;
    ModelReferee                  referee_;
    logosphere::dice::DiceService dice_;
    std::unique_ptr<Session>      session_;
    uint64_t                      seed_ = 0;
    bool                          pinned_ = false;
    bool                          armed_ = false;
    bool                          started_ = false;
    int                           frames_ = 0;
};

}  // namespace voyager

#endif  // VOYAGER_APP_H
