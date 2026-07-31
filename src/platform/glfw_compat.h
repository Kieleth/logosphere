#pragma once

// GLFW compatibility shim.
//
// Full profile: forwards to the real <GLFW/glfw3.h>.
//
// GLFW-less profiles (physics/core): the input layer (input_system,
// camera_controller, player_controller, key_mapper, ui_system) and
// engine.cpp compile UNMODIFIED against the no-op inlines below. No
// window ever exists in those profiles (create_display is false and
// window creation is display-gated), so every call here is a neutral
// no-op by design: keys read as released, cursors sit at origin,
// callbacks are never invoked. This mirrors the runtime behavior of a
// headless macOS run, where the same code paths execute against a
// null window handle.
//
// Constants mirror GLFW 3.4 numeric values. Only symbols actually used
// by this codebase are defined; a new GLFW usage in a shimmed TU shows
// up as a compile error in the physics-profile CI build, never as a
// silent wrong value.

#if __has_include(<GLFW/glfw3.h>)
#include <GLFW/glfw3.h>
#else

typedef struct GLFWwindow GLFWwindow;

// Key/button actions
#define GLFW_RELEASE 0
#define GLFW_PRESS   1
#define GLFW_REPEAT  2

// Printable keys (ASCII-aligned, as in glfw3.h)
#define GLFW_KEY_UNKNOWN       -1
#define GLFW_KEY_SPACE         32
#define GLFW_KEY_APOSTROPHE    39
#define GLFW_KEY_COMMA         44
#define GLFW_KEY_MINUS         45
#define GLFW_KEY_PERIOD        46
#define GLFW_KEY_0             48
#define GLFW_KEY_1             49
#define GLFW_KEY_2             50
#define GLFW_KEY_3             51
#define GLFW_KEY_4             52
#define GLFW_KEY_5             53
#define GLFW_KEY_6             54
#define GLFW_KEY_7             55
#define GLFW_KEY_8             56
#define GLFW_KEY_9             57
#define GLFW_KEY_SEMICOLON     59
#define GLFW_KEY_EQUAL         61
#define GLFW_KEY_A             65
#define GLFW_KEY_B             66
#define GLFW_KEY_C             67
#define GLFW_KEY_D             68
#define GLFW_KEY_E             69
#define GLFW_KEY_F             70
#define GLFW_KEY_G             71
#define GLFW_KEY_H             72
#define GLFW_KEY_I             73
#define GLFW_KEY_J             74
#define GLFW_KEY_K             75
#define GLFW_KEY_L             76
#define GLFW_KEY_M             77
#define GLFW_KEY_N             78
#define GLFW_KEY_O             79
#define GLFW_KEY_P             80
#define GLFW_KEY_Q             81
#define GLFW_KEY_R             82
#define GLFW_KEY_S             83
#define GLFW_KEY_T             84
#define GLFW_KEY_U             85
#define GLFW_KEY_V             86
#define GLFW_KEY_W             87
#define GLFW_KEY_X             88
#define GLFW_KEY_Y             89
#define GLFW_KEY_Z             90
#define GLFW_KEY_LEFT_BRACKET  91
#define GLFW_KEY_BACKSLASH     92
#define GLFW_KEY_RIGHT_BRACKET 93
#define GLFW_KEY_GRAVE_ACCENT  96

// Function keys
#define GLFW_KEY_ESCAPE      256
#define GLFW_KEY_ENTER       257
#define GLFW_KEY_BACKSPACE   259
#define GLFW_KEY_F1          290
#define GLFW_KEY_F2          291
#define GLFW_KEY_F3          292
#define GLFW_KEY_F4          293
#define GLFW_KEY_F12         301
#define GLFW_KEY_LEFT_SHIFT  340
#define GLFW_KEY_RIGHT_SHIFT 344
#define GLFW_KEY_MENU        348
#define GLFW_KEY_LAST        GLFW_KEY_MENU

// Modifier bits
#define GLFW_MOD_SHIFT   0x0001
#define GLFW_MOD_CONTROL 0x0002
#define GLFW_MOD_ALT     0x0004
#define GLFW_MOD_SUPER   0x0008

// Mouse
#define GLFW_MOUSE_BUTTON_LEFT 0

// Cursor mode
#define GLFW_CURSOR        0x00033001
#define GLFW_CURSOR_NORMAL 0x00034001

// Callback types (match glfw3.h signatures)
typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow*, double, double);

// No-op function surface (only what this codebase calls)
inline int glfwGetKey(GLFWwindow*, int) { return GLFW_RELEASE; }
inline void glfwGetCursorPos(GLFWwindow*, double* x, double* y) {
    if (x) *x = 0.0;
    if (y) *y = 0.0;
}
inline void glfwSetInputMode(GLFWwindow*, int, int) {}
inline void glfwGetFramebufferSize(GLFWwindow*, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
}
inline void glfwGetWindowSize(GLFWwindow*, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
}
inline GLFWkeyfun glfwSetKeyCallback(GLFWwindow*, GLFWkeyfun) { return nullptr; }
inline GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow*, GLFWmousebuttonfun) { return nullptr; }
inline GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow*, GLFWcursorposfun) { return nullptr; }

#endif  // __has_include(<GLFW/glfw3.h>)
