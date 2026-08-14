// Portable environment set/unset for tests. MSVC has no setenv or
// unsetenv; _putenv_s with an empty value removes the variable. Every
// headless test compiles on Windows CI, so tests reach for this
// instead of the POSIX calls.
#pragma once
#include <cstdlib>

namespace test_env {

inline void set(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

inline void unset(const char* key) {
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

}  // namespace test_env
