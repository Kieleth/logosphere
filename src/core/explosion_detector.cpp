#include "explosion_detector.h"

// INV-29 constants registry: the calibrated defaults live in
// schema/physics.yaml (ExplosionDetectorDefaults group).
#include "generated/physics_constants.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace logosphere {
namespace expdet {

namespace {

// Defaults from the INV-29 registry; the header and the schema say how
// each number was calibrated against the engine's own measured
// behaviour. Mutable because tests and env levers may retune at runtime.
float  g_speed_ceiling   = PhysicsV4::EXPDET_SPEED_CEILING;
double g_ke_ratio        = PhysicsV4::EXPDET_KE_RATIO;
double g_ke_abs_rise_j   = PhysicsV4::EXPDET_KE_ABS_RISE;
float  g_escape_xy       = PhysicsV4::EXPDET_ESCAPE_XY;
float  g_escape_below_z  = PhysicsV4::EXPDET_ESCAPE_BELOW_Z;
constexpr uint64_t WARN_EVERY_FRAMES = PhysicsV4::EXPDET_WARN_EVERY_FRAMES;

bool g_enabled = [] {
    if (const char* e = std::getenv("LOGOSPHERE_EXPLOSION_DETECTOR"))
        return *e && *e != '0';
    return true;   // ON by default: a tripwire that must be armed is not one
}();
bool g_speed_from_env = [] {
    if (const char* e = std::getenv("LOGOSPHERE_EXPLOSION_SPEED")) {
        const float v = (float)std::atof(e);
        if (v > 0.0f) { g_speed_ceiling = v; return true; }
        // An unparseable/nonpositive ceiling used to be SILENTLY ignored
        // (inventory D2): the operator flew with a different instrument
        // than they believed armed. Operator-input validation: refuse.
        std::fprintf(stderr,
            "[EXPDET REFUSED] LOGOSPHERE_EXPLOSION_SPEED='%s' does not parse "
            "to a positive m/s ceiling. Fix the value or unset it; the "
            "default is %g m/s.\n", e, (double)PhysicsV4::EXPDET_SPEED_CEILING);
        std::abort();
    }
    return false;
}();

// Frame accumulators. Main-thread only, same contract as phystrace: physics
// runs on the main thread and a sample is meaningless without its frame.
uint64_t g_frame = 0;
double   g_ke = 0.0;
double   g_ke_prev = -1.0;           // <0 = no previous frame yet
float    g_max_sq = 0.0f;            // fastest body this frame, squared
int      g_max_id = -1;
float    g_max_x = 0, g_max_y = 0, g_max_z = 0, g_max_mass = 0;
uint64_t g_over_this_frame = 0;      // bodies over ceiling this frame
uint64_t g_escaped_this_frame = 0;
int      g_escape_id = -1;
float    g_esc_x = 0, g_esc_y = 0, g_esc_z = 0;

// Rate limiting + cumulative stats.
uint64_t g_last_warn_speed = 0, g_last_warn_ke = 0, g_last_warn_escape = 0;
uint64_t g_suppressed_speed = 0, g_suppressed_escape = 0;
Stats    g_stats;

}  // namespace

bool enabled() { return g_enabled; }
void set_enabled(bool on) { g_enabled = on; }
void set_speed_ceiling(float ms) { if (ms > 0.0f) g_speed_ceiling = ms; }

void begin_frame() {
    g_frame++;
    g_ke = 0.0;
    g_max_sq = 0.0f; g_max_id = -1;
    g_over_this_frame = 0;
    g_escaped_this_frame = 0; g_escape_id = -1;
}

void sample(int id, float vx, float vy, float vz,
            float mass, float x, float y, float z) {
    const float sq = vx * vx + vy * vy + vz * vz;
    g_ke += 0.5 * (double)mass * (double)sq;
    if (sq > g_max_sq) {
        g_max_sq = sq; g_max_id = id;
        g_max_x = x; g_max_y = y; g_max_z = z; g_max_mass = mass;
    }
    if (sq > g_speed_ceiling * g_speed_ceiling) g_over_this_frame++;
    if (z < g_escape_below_z || x > g_escape_xy || x < -g_escape_xy ||
        y > g_escape_xy || y < -g_escape_xy) {
        g_escaped_this_frame++;
        if (g_escape_id < 0) { g_escape_id = id; g_esc_x = x; g_esc_y = y; g_esc_z = z; }
    }
}

void end_frame() {
    const float max_speed = std::sqrt(g_max_sq);
    if (max_speed > g_stats.worst_speed) {
        g_stats.worst_speed = max_speed;
        g_stats.worst_id = g_max_id;
    }
    g_stats.ke_total = g_ke;
    g_stats.speed_events  += g_over_this_frame;
    g_stats.escape_events += g_escaped_this_frame;

    // SPEED — one warning per window, carrying everything the window hid.
    // "|| == 0" means A FIRST VIOLATION ALWAYS WARNS IMMEDIATELY. Without it,
    // frame arithmetic silently suppressed the first 59 frames after start or
    // reset, which is precisely when a spawn-time explosion happens. Found by
    // this module's own test: the detonation fired zero warnings in 5 frames.
    if (g_over_this_frame > 0) {
        if (g_last_warn_speed == 0 || g_frame - g_last_warn_speed >= WARN_EVERY_FRAMES) {
            char supp[64] = "";
            if (g_suppressed_speed)
                std::snprintf(supp, sizeof(supp), ", %llu suppressed since last warning",
                              (unsigned long long)g_suppressed_speed);
            std::fprintf(stderr,
                "[EXPLOSION WARNING] speed: particle %d at %.1f m/s (ceiling %.1f) "
                "pos=(%.1f, %.1f, %.1f) mass=%.2f f=%llu | %llu bodies over ceiling this frame%s\n",
                g_max_id, max_speed, g_speed_ceiling,
                g_max_x, g_max_y, g_max_z, g_max_mass,
                (unsigned long long)g_frame,
                (unsigned long long)g_over_this_frame, supp);
            g_last_warn_speed = g_frame;
            g_suppressed_speed = 0;
            g_stats.speed_warnings++;
        } else {
            g_suppressed_speed += g_over_this_frame;
        }
    }

    // ENERGY — the COLLECTIVE signal, and that word is load-bearing. Three
    // conditions: ratio (a big world's slow drift must not fire it), absolute
    // rise (a quiet world's first pebble must not), and the rise must exceed
    // TWICE the fastest single body's own KE. That last one is what separates
    // an explosion from a throw: gameplay legitimately injects velocity into
    // ONE body (a thrown boulder is 39 kJ from nothing, measured in the
    // battery's projectile scenario, which fired a false alarm without this),
    // and a rise explained by one body is the speed ceiling's job. An
    // explosion is many bodies at once; its rise dwarfs any single one.
    const double ke_fastest = 0.5 * (double)g_max_mass * (double)g_max_sq;
    if (g_ke_prev >= 0.0 &&
        g_ke > g_ke_prev * g_ke_ratio &&
        g_ke - g_ke_prev > g_ke_abs_rise_j &&
        g_ke - g_ke_prev > 2.0 * ke_fastest) {
        if (g_last_warn_ke == 0 || g_frame - g_last_warn_ke >= WARN_EVERY_FRAMES) {
            // Ratio from a zero base is infinite; print the honest inf
            // instead of the old invented 999.0 (inventory D1) — an
            // invented number has no place in a safety instrument's output.
            std::fprintf(stderr,
                "[EXPLOSION WARNING] energy: total KE jumped %.1f kJ -> %.1f kJ "
                "(%.0fx) in one frame; fastest body is particle %d at %.1f m/s\n",
                g_ke_prev / 1000.0, g_ke / 1000.0,
                g_ke_prev > 0.0 ? g_ke / g_ke_prev
                                : std::numeric_limits<double>::infinity(),
                g_max_id, max_speed);
            g_last_warn_ke = g_frame;
            g_stats.energy_warnings++;
        }
    }
    g_ke_prev = g_ke;

    // ESCAPE — a body somewhere the world cannot legitimately put one.
    if (g_escaped_this_frame > 0) {
        if (g_last_warn_escape == 0 || g_frame - g_last_warn_escape >= WARN_EVERY_FRAMES) {
            char supp[64] = "";
            if (g_suppressed_escape)
                std::snprintf(supp, sizeof(supp), ", %llu suppressed since last warning",
                              (unsigned long long)g_suppressed_escape);
            std::fprintf(stderr,
                "[EXPLOSION WARNING] escape: particle %d left the world at "
                "(%.1f, %.1f, %.1f) | %llu bodies out of bounds this frame%s\n",
                g_escape_id, g_esc_x, g_esc_y, g_esc_z,
                (unsigned long long)g_escaped_this_frame, supp);
            g_last_warn_escape = g_frame;
            g_suppressed_escape = 0;
            g_stats.escape_warnings++;
        } else {
            g_suppressed_escape += g_escaped_this_frame;
        }
    }
}

Stats stats() { return g_stats; }

void reset() {
    g_stats = Stats{};
    g_frame = 0;
    g_ke_prev = -1.0;
    g_last_warn_speed = g_last_warn_ke = g_last_warn_escape = 0;
    g_suppressed_speed = g_suppressed_escape = 0;
}

}  // namespace expdet
}  // namespace logosphere
