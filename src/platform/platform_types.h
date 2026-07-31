#ifndef PLATFORM_TYPES_H
#define PLATFORM_TYPES_H

#include <functional>

// Platform-independent types and structures for the platform abstraction layer

namespace Platform {

// Display information
struct DisplayInfo {
    int width;           // Display width in pixels
    int height;          // Display height in pixels
    float dpi_scale_x;   // DPI scale factor for X axis
    float dpi_scale_y;   // DPI scale factor for Y axis
    float refresh_rate;  // Refresh rate in Hz
};

// Window creation parameters
struct WindowConfig {
    int width = 800;
    int height = 600;
    const char* title = "Logosphere";
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
};

// Input event types
enum class InputEventType {
    KeyPress,
    KeyRelease,
    MouseMove,
    MousePress,
    MouseRelease,
    MouseScroll,
    WindowResize,
    WindowClose
};

// Key codes (platform-independent)
enum class KeyCode {
    Unknown = -1,
    
    // Letters
    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G', H = 'H',
    I = 'I', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N', O = 'O', P = 'P',
    Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U', V = 'V', W = 'W', X = 'X',
    Y = 'Y', Z = 'Z',
    
    // Numbers
    Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
    Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',
    
    // Function keys
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    
    // Special keys
    Space = 32,
    Enter = 257,
    Tab = 258,
    Escape = 256,
    Backspace = 259,
    Delete = 261,
    GraveAccent = 96,  // Backtick key (`)
    Comma = 44,        // , key
    Period = 46,       // . key
    Semicolon = 59,    // ; key
    LeftBracket = 91,  // [ key (GLFW_KEY_LEFT_BRACKET)
    RightBracket = 93, // ] key (GLFW_KEY_RIGHT_BRACKET)
    Backslash = 92,    // \ key (GLFW_KEY_BACKSLASH)
    Minus = 45,        // - key
    Equal = 61,        // = key

    // Arrow keys
    Up = 265,
    Down = 264,
    Left = 263,
    Right = 262,
    
    // Modifiers
    LeftShift = 340,
    RightShift = 344,
    LeftControl = 341,
    RightControl = 345,
    LeftAlt = 342,
    RightAlt = 346,
    LeftSuper = 343,   // Windows/Command key
    RightSuper = 347
};

// Mouse buttons
enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4
};

// Modifier keys state
struct ModifierKeys {
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;  // Windows/Command key
};

// Input event data
struct InputEvent {
    InputEventType type;
    
    // Key event data
    KeyCode key;
    int scancode;
    
    // Mouse event data
    double mouse_x;
    double mouse_y;
    double scroll_x;
    double scroll_y;
    MouseButton button;
    
    // Modifiers
    ModifierKeys modifiers;
    
    // Window event data
    int window_width;
    int window_height;
};

// Input callbacks interface
class IInputCallbacks {
public:
    virtual ~IInputCallbacks() = default;

    virtual void on_key_event(KeyCode key, int scancode, bool pressed, const ModifierKeys& mods) = 0;
    virtual void on_char_input(unsigned int codepoint) = 0;
    virtual void on_mouse_move(double x, double y) = 0;
    virtual void on_mouse_button(MouseButton button, bool pressed, const ModifierKeys& mods) = 0;
    virtual void on_mouse_scroll(double x_offset, double y_offset) = 0;
    virtual void on_window_resize(int width, int height) = 0;
    virtual void on_window_close() = 0;
};

} // namespace Platform

#endif // PLATFORM_TYPES_H