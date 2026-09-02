// Where a key press goes, held to the four cases there are.
//
// The one that matters: a widget holding focus does NOT swallow the
// keyboard. A click focuses whatever it lands on, so the old rule
// meant a game with a list or a panel on screen went deaf the moment
// the player clicked one, escape included. The rule now offers the
// key to the widget and hands on what the widget did not take.
//
// The exception that must stay an exception: a text field takes
// everything, escape included, because a keystroke while typing is a
// character or an edit and never a command.

#undef NDEBUG

#include "logosphere/core/key_routing.h"

#include <iostream>

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

const char* name_of(logosphere::KeyRoute route) {
    switch (route) {
        case logosphere::KeyRoute::Game: return "Game";
        case logosphere::KeyRoute::TextField: return "TextField";
        case logosphere::KeyRoute::WidgetThenGame: return "WidgetThenGame";
        case logosphere::KeyRoute::WidgetAndGame: return "WidgetAndGame";
    }
    return "?";
}

}  // namespace

int main() {
    using logosphere::KeyRoute;
    using logosphere::route_key;

    // Nothing focused: the game hears its keyboard.
    CHECK(route_key(false, false, false) == KeyRoute::Game,
          "an ordinary key with nothing focused went to "
              << name_of(route_key(false, false, false)));
    CHECK(route_key(true, false, false) == KeyRoute::WidgetAndGame,
          "a system key with nothing focused went to "
              << name_of(route_key(true, false, false)));

    // A widget focused: it gets first refusal, and what it refuses
    // reaches the game. This is the defect: it used to reach nobody.
    CHECK(route_key(false, true, false) == KeyRoute::WidgetThenGame,
          "an ordinary key with a widget focused went to "
              << name_of(route_key(false, true, false))
              << "; a widget that answers to two keys must not eat "
                 "the rest");
    CHECK(route_key(true, true, false) == KeyRoute::WidgetAndGame,
          "a system key with a widget focused went to "
              << name_of(route_key(true, true, false))
              << "; escape has to work with a list on screen");

    // Typing: everything, including the system keys.
    CHECK(route_key(false, true, true) == KeyRoute::TextField,
          "an ordinary key while typing went to "
              << name_of(route_key(false, true, true)));
    CHECK(route_key(true, true, true) == KeyRoute::TextField,
          "a system key while typing went to "
              << name_of(route_key(true, true, true))
              << "; escape in the middle of a sentence must not close "
                 "the window");
    CHECK(route_key(true, false, true) == KeyRoute::TextField,
          "a focused text field reported without the widget flag went "
          "to " << name_of(route_key(true, false, true)));

    // Whatever the flags, the game is never cut out unless something
    // is actually taking the keys.
    for (int bits = 0; bits < 8; ++bits) {
        const bool system_key = (bits & 1) != 0;
        const bool widget = (bits & 2) != 0;
        const bool field = (bits & 4) != 0;
        const auto route = route_key(system_key, widget, field);
        if (!widget && !field) {
            CHECK(route == KeyRoute::Game ||
                      route == KeyRoute::WidgetAndGame,
                  "with nothing focused the key went to "
                      << name_of(route));
        }
        if (field) {
            CHECK(route == KeyRoute::TextField,
                  "with the text field focused the key went to "
                      << name_of(route));
        }
    }

    std::cout << (failed ? "FAILED " : "OK ") << passed << " passed, "
              << failed << " failed\n";
    return failed ? 1 : 0;
}
