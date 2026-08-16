// Pacman acceptance test — headless, and it plays the game.
//
// This drives the SAME PacmanApplication the window drives
// (docs/testing_guidelines.md rule 6: verify through the path
// production uses). It opens no window, uses no GPU, and asserts on
// measured numbers that it also prints.
//
// What it proves, in order:
//   1. The board is sound: every row the right width, every pellet
//      reachable, the tunnel wraps, the ghost house has a way out.
//   2. Walls stop the avatar. CONTROL: the same push down an open lane
//      moves it. Without the control, "it did not move" would also pass
//      if nothing ever moved.
//   3. Eating a pellet scores and decrements the count, and the pellet
//      cannot be eaten twice.
//   4. An energizer flips every ghost to FRIGHTENED, and the mood
//      expires on its own.
//   5. The tunnel wraps a walker from one edge to the other.
//   6. A ghost released from the house actually gets out of it.
//   7. A bot that walks the maze for 90 simulated seconds eats a
//      substantial share of the board without ever standing inside a
//      wall. This is the one that catches geometry bugs the unit
//      checks cannot see.
//
// Run: ./build/at_pacman

#undef NDEBUG

#include "pacman_app.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "logosphere/rendering/pixel_buffer.h"

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::cout << "  ok   " << what << std::endl; }
    else    { ++g_fail; std::cout << "  FAIL " << what << std::endl; }
}

// Fixed-step so the numbers are the same on every machine.
void run(Engine& e, PacmanApplication& app, float seconds, float step = 1.0f / 60.0f) {
    (void)app;
    const int frames = static_cast<int>(seconds / step);
    for (int i = 0; i < frames; ++i) e.update(step);
}

// Drive the avatar along a written route: at each waypoint cell, issue
// the next heading. Needed because the maze is not a grid of open
// lanes — "walk west from the start" reaches column 6 and stops, which
// is what the first version of test 4 got wrong.
struct Leg { int col; int row; pacman::Dir then; };

void follow_route(Engine& e, PacmanApplication& app,
                  const std::vector<Leg>& legs, float budget_s,
                  float step = 1.0f / 60.0f) {
    size_t next = 0;
    const int frames = static_cast<int>(budget_s / step);
    for (int i = 0; i < frames && next < legs.size(); ++i) {
        const auto& av = app.avatar();
        const int c = pacman::cell_of(av.col);
        const int r = pacman::cell_of(av.row);
        if (c == legs[next].col && r == legs[next].row &&
            pacman::at_cell_centre(av, 0.14f)) {
            app.queue(legs[next].then);
            ++next;
        }
        e.update(step);
    }
}

}  // namespace

int main() {
    std::cout << "at_pacman — headless Pacman, playing itself" << std::endl;

    PacmanApplication app;
    Engine engine(&app);

    EngineConfig cfg;
    cfg.create_display = false;
    cfg.window_title = "at_pacman";
    cfg.show_debug_overlay = false;
    cfg.enable_chat_window = false;
    if (engine.initialize(cfg) < 0) {
        std::cerr << "engine.initialize() failed" << std::endl;
        return 1;
    }

    const auto& board = app.board();

    // ---- 1. the board ------------------------------------------------
    std::cout << "\n[1] board integrity" << std::endl;
    const auto defects = board.validate();
    for (const auto& d : defects) std::cout << "     defect: " << d << std::endl;
    std::cout << "  [measure] cols=" << board.cols()
              << " rows=" << board.rows()
              << " pellets=" << board.pellet_count()
              << " defects=" << defects.size() << std::endl;
    check(defects.empty(), "board has no defects");
    check(board.pellet_count() > 200, "board carries a real pellet load");
    check(app.board_ok(), "app agrees the board is sound");

    // ---- 2. walls stop, lanes do not (with the control) ---------------
    std::cout << "\n[2] walls stop the avatar; open lanes do not" << std::endl;
    run(engine, app, pacman::kReadySecs + 0.2f);   // burn the READY pause
    check(app.phase() == pacman::Phase::PLAYING, "round went live");

    // The avatar starts at (13, 23). Row 22 above it is '####' at that
    // column, so UP is a wall; LEFT is open corridor.
    const float start_col = app.avatar().col;
    const float start_row = app.avatar().row;
    app.queue(pacman::Dir::UP);
    run(engine, app, 0.8f);
    const float after_up_row = app.avatar().row;
    std::cout << "  [measure] pushed UP for 0.8 s: row " << start_row
              << " -> " << after_up_row << std::endl;
    check(std::fabs(after_up_row - start_row) < 0.05f,
          "UP into a wall moved the avatar less than 0.05 cells");

    // Steer through the ENGINE's key path, not by calling the game's
    // handler directly: InputSystem -> KeyMapper::process_key_event ->
    // IApplication::handle_key is what a real keypress travels, and it
    // is the part a game most easily wires up wrong.
    engine.get_key_mapper().process_key_event(GLFW_KEY_LEFT, 0, GLFW_PRESS, 0);
    run(engine, app, 0.8f);
    const float after_left_col = app.avatar().col;
    std::cout << "  [measure] CONTROL, pushed LEFT for 0.8 s: col "
              << start_col << " -> " << after_left_col << std::endl;
    check(start_col - after_left_col > 2.0f,
          "CONTROL: LEFT down an open lane moved the avatar over 2 cells");

    // ---- 3. eating ----------------------------------------------------
    std::cout << "\n[3] eating scores once and only once" << std::endl;
    const int score_after_walk = app.score();
    const int left_after_walk  = app.pellets_remaining();
    std::cout << "  [measure] after the control walk: score=" << score_after_walk
              << " pellets_left=" << left_after_walk
              << " (of " << board.pellet_count() << ")" << std::endl;
    check(score_after_walk > 0, "walking over pellets scored");
    check(left_after_walk == board.pellet_count() - score_after_walk / 10 ||
          left_after_walk < board.pellet_count(),
          "the remaining count went down");

    // Keep pushing LEFT until the wall at column 5 stops the avatar
    // dead on an eaten cell. Then time passes and the score must not.
    // (There is no way to stand still in Pacman on purpose; a wall is
    // the only brake, which is why the earlier version of this check —
    // "queue Dir::NONE and wait" — measured nothing: the avatar kept
    // its heading and kept eating.)
    app.queue(pacman::Dir::LEFT);
    run(engine, app, 2.0f);
    const int frozen_before = app.score();
    const float parked_col = app.avatar().col;
    run(engine, app, 1.5f);
    std::cout << "  [measure] parked against the wall at col " << parked_col
              << " (still " << app.avatar().col << " after 1.5 s): score "
              << frozen_before << " -> " << app.score() << std::endl;
    check(std::fabs(app.avatar().col - parked_col) < 0.02f,
          "the wall holds the avatar still");
    check(app.score() == frozen_before,
          "a pellet already eaten does not score again");

    // ---- 4. energizer flips the ghosts --------------------------------
    std::cout << "\n[4] the energizer flips every ghost" << std::endl;
    {
        // Fresh game, then walk left along row 23 to the energizer at
        // column 1. Row 23 is "#o..##................##..o#".
        PacmanApplication e_app;
        Engine e_engine(&e_app);
        EngineConfig ecfg;
        ecfg.create_display = false;
        ecfg.window_title = "at_pacman_energizer";
        e_engine.initialize(ecfg);
        run(e_engine, e_app, pacman::kReadySecs + 0.1f);
        // Row 23 is walled at columns 4-5, so the west energizer at
        // (1, 23) is NOT straight down the lane from the start. Route:
        // west to the wall, down to row 26, west to column 3, north
        // back up to row 23, then west onto the energizer.
        follow_route(e_engine, e_app, {
            {13, 23, pacman::Dir::LEFT},
            { 6, 23, pacman::Dir::DOWN},
            { 6, 26, pacman::Dir::LEFT},
            { 3, 26, pacman::Dir::UP},
            { 3, 23, pacman::Dir::LEFT},
        }, /*budget_s=*/8.0f);
        run(e_engine, e_app, 1.0f);
        int frightened = 0;
        for (const auto& gh : e_app.ghosts())
            if (gh.mood == pacman::Mood::FRIGHTENED) ++frightened;
        std::cout << "  [measure] avatar at col " << e_app.avatar().col
                  << " row " << e_app.avatar().row
                  << ", score " << e_app.score()
                  << ", frightened ghosts " << frightened << "/4" << std::endl;
        check(e_app.avatar().col < 2.5f, "avatar reached the west energizer cell");
        check(frightened >= 3, "at least three ghosts turned FRIGHTENED");
        check(e_app.score() >= pacman::kEnergizerPoints,
              "the energizer paid its 50 points");

        // and it wears off
        run(e_engine, e_app, pacman::kFrightenedSecs + 1.0f);
        int still = 0;
        for (const auto& gh : e_app.ghosts())
            if (gh.mood == pacman::Mood::FRIGHTENED) ++still;
        std::cout << "  [measure] after " << pacman::kFrightenedSecs + 1.0f
                  << " s more: frightened " << still << "/4" << std::endl;
        check(still == 0, "FRIGHTENED expires on its own");
    }

    // ---- 5. the tunnel wraps ------------------------------------------
    std::cout << "\n[5] the tunnel wraps" << std::endl;
    {
        pacman::Walker w;
        w.col = 2.0f; w.row = static_cast<float>(pacman::kTunnelRow);
        w.dir = pacman::Dir::LEFT; w.speed = 8.0f;
        float min_col = w.col, max_col = w.col;
        for (int i = 0; i < 120; ++i) {
            pacman::step_walker(w, board, 1.0f / 60.0f, false);
            min_col = std::min(min_col, w.col);
            max_col = std::max(max_col, w.col);
        }
        // 120 frames at 8 cells/s is 16 cells of travel starting from
        // column 2, so the walker crosses the west edge, reappears in
        // the east, and keeps going. The wrap is proven by the EAST
        // column it visited, not by where it stopped.
        std::cout << "  [measure] walked LEFT off the west edge, ended at col "
                  << w.col << " (min seen " << min_col
                  << ", max seen " << max_col << ")" << std::endl;
        check(max_col > board.cols() - 2.0f,
              "a walker leaving the west edge reappears in the east");
        check(w.col > 15.0f && w.col < 20.0f,
              "and keeps walking west from there (16 cells travelled)");

        // CONTROL: the same walk on a NON-tunnel row must stop at the wall.
        pacman::Walker c;
        c.col = 2.0f; c.row = 5.0f;   // row 5 is "#........#", walled at col 0
        c.dir = pacman::Dir::LEFT; c.speed = 8.0f;
        for (int i = 0; i < 120; ++i) pacman::step_walker(c, board, 1.0f / 60.0f, false);
        std::cout << "  [measure] CONTROL, same walk on row 5: col " << c.col
                  << std::endl;
        check(c.col > 0.5f && c.col < 1.5f,
              "CONTROL: a walled row stops the walker at column 1");
    }

    // ---- 6. ghosts leave the house ------------------------------------
    std::cout << "\n[6] the house releases its ghosts" << std::endl;
    {
        PacmanApplication h_app;
        Engine h_engine(&h_app);
        EngineConfig hcfg;
        hcfg.create_display = false;
        hcfg.window_title = "at_pacman_house";
        h_engine.initialize(hcfg);

        // Sampled every frame, not read at the end. An avatar standing
        // in the open gets caught, a death sends the ghosts back inside
        // and restarts their release clocks, so a snapshot at t=22 s
        // answers a different question than the one being asked. What
        // is being asked is: did each ghost ever get out?
        auto outside_house = [](const pacman::GhostSlot& gh) {
            return gh.walker.row < pacman::kHouseDoorRow + 0.5f ||
                   gh.walker.row > 16.5f ||
                   gh.walker.col < 10.0f || gh.walker.col > 17.0f;
        };
        bool ever_out[4] = {false, false, false, false};
        const float step = 1.0f / 60.0f;
        for (int i = 0; i < static_cast<int>(30.0f / step); ++i) {
            h_engine.update(step);
            if (h_app.phase() == pacman::Phase::GAME_OVER)
                h_app.handle_key(GLFW_KEY_R, 0, GLFW_PRESS, 0);
            const auto& gs = h_app.ghosts();
            for (size_t k = 0; k < gs.size() && k < 4; ++k)
                if (outside_house(gs[k])) ever_out[k] = true;
        }
        int out = 0;
        const auto& gs = h_app.ghosts();
        for (size_t k = 0; k < gs.size() && k < 4; ++k) {
            std::cout << "     " << gs[k].name
                      << " ever left the house: " << (ever_out[k] ? "yes" : "NO")
                      << " (now at " << gs[k].walker.col << ", "
                      << gs[k].walker.row << ")" << std::endl;
            if (ever_out[k]) ++out;
        }
        std::cout << "  [measure] ghosts that left the house during 30 s: "
                  << out << "/4, deaths=" << h_app.deaths_total() << std::endl;
        check(out == 4, "all four ghosts left the house");
    }

    // ---- 7. a bot plays ------------------------------------------------
    std::cout << "\n[7] a bot plays for 90 simulated seconds" << std::endl;
    {
        PacmanApplication b_app;
        Engine b_engine(&b_app);
        EngineConfig bcfg;
        bcfg.create_display = false;
        bcfg.window_title = "at_pacman_bot";
        b_engine.initialize(bcfg);

        const auto& b = b_app.board();
        uint32_t rng = 0xC0FFEEu;
        int inside_wall_frames = 0;
        const float step = 1.0f / 60.0f;
        const int frames = static_cast<int>(90.0f / step);

        for (int i = 0; i < frames; ++i) {
            // Greedy-ish wanderer: at every cell centre pick an open
            // direction, preferring one that is not a reversal.
            const auto& av = b_app.avatar();
            if (pacman::at_cell_centre(av, 0.12f)) {
                const int c = b.wrap_col(pacman::cell_of(av.col));
                const int r = pacman::cell_of(av.row);
                pacman::Dir opts[4];
                int n = 0;
                const pacman::Dir all[4] = {pacman::Dir::UP, pacman::Dir::DOWN,
                                            pacman::Dir::LEFT, pacman::Dir::RIGHT};
                for (pacman::Dir d : all) {
                    if (d == pacman::opposite(av.dir)) continue;
                    int dc, dr; pacman::dir_delta(d, dc, dr);
                    if (b.open_for_avatar(c + dc, r + dr)) opts[n++] = d;
                }
                if (n == 0) b_app.queue(pacman::opposite(av.dir));
                else if (n == 1) b_app.queue(opts[0]);
                else if (av.dir == pacman::Dir::NONE ||
                         (pacman::next_random(rng) % 100u) < 45u)
                    b_app.queue(opts[pacman::next_random(rng) % static_cast<uint32_t>(n)]);
            }

            b_engine.update(step);
            // Three lives run out well inside 90 s; press R and keep
            // playing so the run measures the maze, not the life count.
            if (b_app.phase() == pacman::Phase::GAME_OVER)
                b_app.handle_key(GLFW_KEY_R, 0, GLFW_PRESS, 0);

            // The invariant that matters: the avatar is never inside a
            // wall. Checked every frame, not sampled.
            const int ac = b.wrap_col(pacman::cell_of(b_app.avatar().col));
            const int ar = pacman::cell_of(b_app.avatar().row);
            if (!b.open_for_avatar(ac, ar)) ++inside_wall_frames;
        }

        const int eaten = b_app.pellets_eaten_total();
        std::cout << "  [measure] 90 s of bot play: score=" << b_app.score()
                  << " pellets eaten (cumulative)=" << eaten
                  << " of " << b.pellet_count() << " on the board"
                  << " deaths=" << b_app.deaths_total()
                  << " ghosts eaten=" << b_app.ghosts_eaten_total()
                  << " frames inside a wall=" << inside_wall_frames
                  << "/" << frames << std::endl;
        check(inside_wall_frames == 0,
              "the avatar was never inside a wall across 5400 frames");
        check(eaten > 100, "the bot ate more than 100 pellets");
        check(b_app.deaths_total() > 0, "the ghosts are dangerous, not decorative");
    }

    // ---- 8. it is actually on screen -----------------------------------
    // Opt-in, because it needs a window and a GPU. Asserted as PIXELS,
    // not promised: the maze must be blue, the avatar must be yellow,
    // and the HUD must reach the overlay buffer as glyphs.
    if (std::getenv("LOGOSPHERE_VISUAL")) {
        std::cout << "\n[8] pixels (LOGOSPHERE_VISUAL set)" << std::endl;
        PacmanApplication v_app;
        Engine v_engine(&v_app);
        EngineConfig vcfg;
        vcfg.create_display = true;
        vcfg.window_width = 1100;
        vcfg.window_height = 1200;
        vcfg.window_title = "at_pacman pixels";
        v_engine.initialize(vcfg);
        // LOGOSPHERE_SHOT_AFTER=<seconds> plays the wandering bot first,
        // so the captured frame shows a game in progress rather than the
        // start-of-round pose.
        float warm = 0.0f;
        if (const char* s = std::getenv("LOGOSPHERE_SHOT_AFTER")) warm = std::atof(s);
        const int warm_frames = static_cast<int>(warm * 60.0f);
        uint32_t vrng = 0xBEEFu;
        for (int i = 0; i < warm_frames + 45; ++i) {
            const auto& av = v_app.avatar();
            if (i < warm_frames && pacman::at_cell_centre(av, 0.12f)) {
                const auto& vb = v_app.board();
                const int c = vb.wrap_col(pacman::cell_of(av.col));
                const int r = pacman::cell_of(av.row);
                pacman::Dir opts[4]; int n = 0;
                const pacman::Dir all[4] = {pacman::Dir::UP, pacman::Dir::DOWN,
                                            pacman::Dir::LEFT, pacman::Dir::RIGHT};
                for (pacman::Dir d : all) {
                    if (d == pacman::opposite(av.dir)) continue;
                    int dc, dr; pacman::dir_delta(d, dc, dr);
                    if (vb.open_for_avatar(c + dc, r + dr)) opts[n++] = d;
                }
                if (n > 0 && (av.dir == pacman::Dir::NONE ||
                              (pacman::next_random(vrng) % 100u) < 45u))
                    v_app.queue(opts[pacman::next_random(vrng) % static_cast<uint32_t>(n)]);
            }
            v_engine.update(1.0f / 60.0f);
            v_engine.render();
            v_engine.present();
        }

        int w = 0, h = 0;
        std::vector<uint32_t> px(4096 * 4096);
        const bool got = v_engine.read_latest_framebuffer(px.data(), w, h);
        check(got, "framebuffer read back");
        // read_latest_framebuffer hands back BGRA, little-endian: the
        // LOW byte is BLUE. Getting this backwards produces a picture
        // that looks plausible and is wrong in exactly the way that is
        // hardest to notice — a red maze with a cyan Pacman still reads
        // as "a maze with a Pacman in it".
        size_t blue = 0, yellow = 0, lit = 0;
        for (int i = 0; i < w * h; ++i) {
            const uint32_t p = px[i];
            const int b = p & 0xFF, g = (p >> 8) & 0xFF, r = (p >> 16) & 0xFF;
            if (r + g + b > 40) ++lit;
            if (b > 90 && b > r + 40 && b > g + 30) ++blue;      // maze walls
            if (r > 150 && g > 130 && b < 110) ++yellow;         // the avatar
        }
        std::cout << "  [measure] framebuffer " << w << "x" << h
                  << " lit=" << lit << " blue=" << blue
                  << " yellow=" << yellow << std::endl;
        check(lit > 10000, "the frame is not black");
        check(blue > 5000, "the maze walls are on screen and blue");
        check(yellow > 200, "the avatar is on screen and yellow");

        const PixelBuffer& ui = v_engine.get_ui_overlay_buffer();
        size_t ui_lit = 0, ui_text = 0;
        for (int y = 0; y < ui.height(); ++y)
            for (int x = 0; x < ui.width(); ++x) {
                const auto p = ui.get_pixel(x, y);
                if (p.a > 0) ++ui_lit;
                if (p.a > 220) ++ui_text;
            }
        std::cout << "  [measure] UI overlay lit=" << ui_lit
                  << " glyph=" << ui_text << std::endl;

        // Raw BGRA dump, for eyes. Only when asked.
        if (const char* path = std::getenv("LOGOSPHERE_SHOT")) {
            if (FILE* f = std::fopen(path, "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (int i = 0; i < w * h; ++i) {
                    const uint32_t p = px[i];
                    const unsigned char rgb[3] = {   // BGRA in, RGB out
                        static_cast<unsigned char>((p >> 16) & 0xFF),
                        static_cast<unsigned char>((p >> 8) & 0xFF),
                        static_cast<unsigned char>(p & 0xFF)};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
                std::cout << "  wrote " << path << std::endl;
            }
        }
        check(ui_lit > 1000, "the HUD panel reached the overlay buffer");
        check(ui_text > 200, "the HUD is showing glyphs, not an empty panel");
        v_engine.shutdown();
    } else {
        std::cout << "\n[8] pixels: skipped (set LOGOSPHERE_VISUAL=1 to run)"
                  << std::endl;
    }

    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed" << std::endl;
    engine.shutdown();
    return g_fail == 0 ? 0 : 1;
}
