#ifndef EDEN_DEBUG_CONTROL_H
#define EDEN_DEBUG_CONTROL_H

// =========================================================================
// Eden Debug Control - Separate from engine debug
// 
// To enable Eden debug output: #define ENABLE_EDEN_DEBUG before including
// To disable (for production): Leave undefined (default)
// =========================================================================

// Uncomment to enable Eden debug output (development only)
// #define ENABLE_EDEN_DEBUG

#ifdef ENABLE_EDEN_DEBUG
    #define EDEN_LOG(msg) std::cout << msg << std::endl
    #define EDEN_PRINT(...) printf(__VA_ARGS__)
#else
    // No-op macros - compiler will optimize these away completely
    #define EDEN_LOG(msg) ((void)0)
    #define EDEN_PRINT(...) ((void)0)
#endif

#endif // EDEN_DEBUG_CONTROL_H