#ifndef DEBUG_CONTROL_H
#define DEBUG_CONTROL_H

// =========================================================================
// PERFORMANCE OPTIMIZATION: Debug Output Control
// 
// Debug output (cout, printf, etc) can significantly impact performance.
// This header provides centralized control over all debug output.
//
// To enable debug output: #define ENABLE_DEBUG_OUTPUT before including
// To disable (for production): Leave undefined (default)
// =========================================================================

// Uncomment to enable debug output (development only)
// #define ENABLE_DEBUG_OUTPUT

#ifdef ENABLE_DEBUG_OUTPUT
    #define DEBUG_LOG(msg) std::cout << msg << std::endl
    #define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    // No-op macros - compiler will optimize these away completely
    #define DEBUG_LOG(msg) ((void)0)
    #define DEBUG_PRINT(...) ((void)0)
#endif

// Performance-critical debug that should ALWAYS be disabled in production
// #define FORCE_DEBUG_MODE  // Uncomment to force debug output
#ifndef FORCE_DEBUG_MODE
    #define PERF_DEBUG_LOG(msg) ((void)0)
    #define PERF_DEBUG_PRINT(...) ((void)0)
#else
    #define PERF_DEBUG_LOG(msg) std::cout << msg << std::endl
    #define PERF_DEBUG_PRINT(...) printf(__VA_ARGS__)
#endif

#endif // DEBUG_CONTROL_H