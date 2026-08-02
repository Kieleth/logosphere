#pragma once

// =============================================================================
// PHYSICS TRACE — layered, append-only, always compiled in
// =============================================================================
// WHY THIS EXISTS. Physics is the engine's scaling limit and its least
// observable subsystem. Four separate conclusions this campaign reached about it
// were later withdrawn, and each time the reason was the same: a number was
// available but the DECISION behind it was not. Counters say a contact was
// created. They cannot say that a pair was skipped because both bodies were
// KINEMATIC, or that an impulse was clamped to zero because it came out
// negative, or that the solver stopped because improvement fell under 5% rather
// than because it reached equilibrium. Without the why, every reading needs a
// theory laid over it, and this campaign's theories have a poor record.
//
// The engine already proves the shape works: ParticleTracer is permanent,
// costs one branch when nobody is listening, and answers "who moved this
// particle and when". This answers "what did physics decide, and why".
//
// THREE PROPERTIES, in priority order:
//
//   1. ZERO COST WHEN OFF. One relaxed atomic load, predicted not-taken, the
//      same gate telemetry uses. Off is the default and must stay free, or the
//      instrumentation gets compiled out "temporarily" and rots.
//   2. LAYERED. Depth is a dial, not a switch. Turning everything on at 14,000
//      bodies writes gigabytes and answers nothing; the useful mode is usually
//      "level 5, but only for these two particles".
//   3. APPEND-ONLY AND SELF-DESCRIBING. Every record carries its own frame,
//      substep and iteration, so a line torn out of the middle of a 2 GB log
//      still means something.
//
// LEVELS are cumulative. Each includes everything below it.
//
//   0  Off        default
//   1  Frame      one line per frame: bodies, constraints, time
//   2  Solve      per substep: rows in, iterations run, how it exited and WHY
//   3  Phase      block boundaries inside solve_contacts_v3 with their counts
//   4  Pair       per candidate pair: accepted or rejected, WITH THE REASON.
//                 This is the level that finds wasted work.
//   5  Constraint per constraint per iteration: impulse, clamp, accumulator.
//                 Enormous. Use with focus().
//   6  Full       everything, including values that are usually noise
//
// FOCUS. Levels 4 and above respect a particle filter. With no focus set they
// emit for everything, which is correct for a 3-body test and useless for Eden.
// focus(id) narrows deep levels to records touching that particle, which is how
// ParticleTracer is used and why it stays usable on real scenes.
//
// CONTROL
//   LOGOSPHERE_PHYS_TRACE=<0..6>        level at startup
//   LOGOSPHERE_PHYS_TRACE_FILE=<path>   append-only sink (default: stderr)
//   LOGOSPHERE_PHYS_TRACE_IDS=1,2,3     focus list for levels >= 4
//   -p / -pp / -ppp ... on any argv     level = number of p's (parse_argv)
//   logosphere::phystrace::set_level()  at runtime, for tests
//
// RECORD FORMAT, one line, tab-separated so it greps and cuts cleanly:
//   f<frame> s<substep> i<iter> <EVENT> a=<id> b=<id> why=<reason> v=<..>
// The reason is a short stable token (`both_kinematic`, `sep_gt_margin`,
// `neg_impulse_clamped`), not prose: these get counted and diffed, not read.
// =============================================================================

#include <atomic>
#include <cstdint>

namespace logosphere {
namespace phystrace {

enum Level : int {
    Off        = 0,
    Frame      = 1,
    Solve      = 2,
    Phase      = 3,
    Pair       = 4,
    Constraint = 5,
    Full       = 6,
};

namespace detail {
extern std::atomic<int> g_level;      // the whole hot-path gate
}

// One relaxed load. Everything else is behind this.
inline int level() { return detail::g_level.load(std::memory_order_relaxed); }
inline bool at(int l) { return level() >= l; }

void set_level(int n);
void set_sink(const char* path_or_null);      // null restores stderr
void add_focus(int particle_id);              // levels >= Pair narrow to these
void clear_focus();
bool focused(int a, int b);                   // true if no focus set

// Reads LOGOSPHERE_PHYS_TRACE / _FILE / _IDS. Safe to call more than once.
void init_from_env();
// Counts the p's in -p/-pp/-ppp style flags. Returns the level it set, or -1.
int  parse_argv(int argc, char** argv);

// Context. Cheap and unconditional so a record is never orphaned: these only
// write three ints, and the emit path is what is actually gated.
void frame_begin(uint64_t frame_index);
void set_substep(int substep);
void set_iteration(int iteration);

// The core emit. `why` is the point of the whole file: prefer a token that
// names the DECISION over one that names the state.
void emit(int lvl, const char* event, int a, int b, const char* why,
          double v0 = 0.0, double v1 = 0.0, double v2 = 0.0);

// Flush and close the sink. Called at shutdown; safe if never opened.
void shutdown();

}  // namespace phystrace
}  // namespace logosphere

// Gate first, evaluate arguments second. Anything expensive in an argument is
// not paid for when the level is off, which is the difference between
// instrumentation that survives and instrumentation that gets deleted.
#define PHYS_TRACE(lvl, event, a, b, why, ...)                                  \
    do {                                                                        \
        if (::logosphere::phystrace::at(lvl)) {                                 \
            ::logosphere::phystrace::emit((lvl), (event), (a), (b), (why),      \
                                          ##__VA_ARGS__);                       \
        }                                                                       \
    } while (0)

// Levels >= Pair additionally respect the focus filter.
#define PHYS_TRACE_F(lvl, event, a, b, why, ...)                                \
    do {                                                                        \
        if (::logosphere::phystrace::at(lvl) &&                                 \
            ::logosphere::phystrace::focused((a), (b))) {                       \
            ::logosphere::phystrace::emit((lvl), (event), (a), (b), (why),      \
                                          ##__VA_ARGS__);                       \
        }                                                                       \
    } while (0)
