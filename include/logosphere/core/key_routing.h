// Where a key press goes.
//
// Three things can want a keystroke: a text field, whichever widget
// the pointer last clicked, and the game. The rule that decides
// between them is four lines long and it was, until now, spread
// across two branches of two functions in the input system, one of
// which was unreachable on the platform that runs.
//
// The rule it replaces swallowed EVERY key while any widget held
// focus. Since a click focuses whatever it lands on, that meant a
// game with widgets on screen stopped hearing its own keyboard the
// moment the player clicked one of them: no escape key, no shortcuts,
// nothing, until they clicked the background. A widget that answers
// to two named keys must not eat the other hundred.
//
// Typing is the exception, and the only one. A text field takes every
// key including escape, because a keystroke while typing is a
// character or an edit, never a command.
//
// Pure and header-only so the rule can be read in one place and
// measured in test_key_routing rather than reasoned about from two
// call sites.

#ifndef LOGOSPHERE_CORE_KEY_ROUTING_H
#define LOGOSPHERE_CORE_KEY_ROUTING_H

namespace logosphere {

enum class KeyRoute {
    Game,             // nothing in the UI wants it
    TextField,        // typing takes every key
    WidgetThenGame,   // the focused widget first, the game if it passes
    WidgetAndGame,    // system keys: both, always
};

// `text_field_focused` implies `widget_focused`; a caller that sets
// the first without the second still gets the text field, because a
// field that has focus is being typed into whatever else is true.
inline KeyRoute route_key(bool system_key, bool widget_focused,
                          bool text_field_focused) {
    if (text_field_focused) return KeyRoute::TextField;
    if (system_key) return KeyRoute::WidgetAndGame;
    if (widget_focused) return KeyRoute::WidgetThenGame;
    return KeyRoute::Game;
}

}  // namespace logosphere

#endif  // LOGOSPHERE_CORE_KEY_ROUTING_H
