#include "physics_trace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace logosphere {
namespace phystrace {

namespace detail {
std::atomic<int> g_level{0};
}

namespace {

// Context. Plain ints, main-thread only by the same contract as phase timers:
// physics runs on the main thread and a record is meaningless without knowing
// which substep produced it.
uint64_t g_frame = 0;
int      g_substep = 0;
int      g_iter = 0;

FILE*      g_sink = nullptr;      // null means stderr
std::mutex g_sink_mutex;          // the sink is the only shared mutable state

std::vector<int> g_focus;         // empty means "everything"

FILE* out() { return g_sink ? g_sink : stderr; }

}  // namespace

void set_level(int n) {
    if (n < Off)  n = Off;
    if (n > Full) n = Full;
    detail::g_level.store(n, std::memory_order_relaxed);
}

void set_sink(const char* path) {
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    if (g_sink) { std::fclose(g_sink); g_sink = nullptr; }
    if (!path) return;
    // APPEND, deliberately. A trace that truncates loses the run you wanted
    // when the next run starts, and these are usually captured in pairs.
    g_sink = std::fopen(path, "a");
    if (g_sink) {
        // A short trace (a level-1 run of a few frames) sat in stdio's
        // buffer and was lost at exit; the sink flushes itself on exit.
        static const bool flush_registered = []{
            std::atexit([]{ std::lock_guard<std::mutex> l(g_sink_mutex);
                            if (g_sink) std::fflush(g_sink); });
            return true; }();
        (void)flush_registered;
        std::fprintf(g_sink, "# physics trace level=%d\n", level());
        std::fflush(g_sink);
    }
}

void add_focus(int particle_id) { g_focus.push_back(particle_id); }
void clear_focus() { g_focus.clear(); }

bool focused(int a, int b) {
    if (g_focus.empty()) return true;      // no filter set: everything passes
    for (int id : g_focus) if (id == a || id == b) return true;
    return false;
}

void init_from_env() {
    if (const char* e = std::getenv("LOGOSPHERE_PHYS_TRACE")) set_level(std::atoi(e));
    if (const char* f = std::getenv("LOGOSPHERE_PHYS_TRACE_FILE")) set_sink(f);
    if (const char* ids = std::getenv("LOGOSPHERE_PHYS_TRACE_IDS")) {
        const char* p = ids;
        while (*p) {
            if (*p >= '0' && *p <= '9') { add_focus(std::atoi(p)); while (*p >= '0' && *p <= '9') ++p; }
            else ++p;
        }
    }
}

int parse_argv(int argc, char** argv) {
    // -p, -pp, -ppp ... : depth is how many p's you typed. Stacking letters for
    // verbosity is an old unix habit and it reads better than -p=4 when you are
    // iterating on a bug.
    int best = -1;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!a || a[0] != '-' || a[1] != 'p') continue;
        int n = 0;
        const char* q = a + 1;
        while (*q == 'p') { ++n; ++q; }
        if (*q != '\0') continue;          // -px is not a trace flag
        if (n > best) best = n;
    }
    if (best > 0) set_level(best);
    return best;
}

void frame_begin(uint64_t frame_index) { g_frame = frame_index; g_substep = 0; g_iter = 0; }
void set_substep(int substep) { g_substep = substep; g_iter = 0; }
void set_iteration(int iteration) { g_iter = iteration; }

void emit(int lvl, const char* event, int a, int b, const char* why,
          double v0, double v1, double v2) {
    // The caller's macro already checked the level. Re-checking here keeps
    // direct callers honest without costing the macro path anything.
    if (!at(lvl)) return;
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    FILE* f = out();
    std::fprintf(f, "f%llu\ts%d\ti%d\t%s\ta=%d\tb=%d\twhy=%s\tv=%.6g,%.6g,%.6g\n",
                 (unsigned long long)g_frame, g_substep, g_iter,
                 event ? event : "?", a, b, why ? why : "-", v0, v1, v2);
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    if (g_sink) { std::fflush(g_sink); std::fclose(g_sink); g_sink = nullptr; }
}

}  // namespace phystrace
}  // namespace logosphere
