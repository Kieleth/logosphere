#ifndef SHADOW_PROBE_H
#define SHADOW_PROBE_H

#include <string>
#include <vector>
#include <cstdint>

// Screen-space bounding box for probe regions (normalized 0.0-1.0)
struct ScreenBox {
    float x_min, x_max;  // 0.0 = left edge, 1.0 = right edge
    float y_min, y_max;  // 0.0 = top edge, 1.0 = bottom edge

    bool contains(float x, float y) const {
        return x >= x_min && x <= x_max &&
               y >= y_min && y <= y_max;
    }
};

// World-space bounding box for probe regions (future: requires G-buffer)
struct WorldBox {
    float x_min, x_max;
    float y_min, y_max;
    float z_min, z_max;

    bool contains(float x, float y, float z) const {
        return x >= x_min && x <= x_max &&
               y >= y_min && y <= y_max &&
               z >= z_min && z <= z_max;
    }
};

// What type of lighting we expect in a region
enum class RegionExpectation {
    Shadow,    // Low brightness, low variance
    Lit,       // High brightness, low variance
    Penumbra   // Medium brightness, higher variance (soft edge)
};

// Statistics for a single probe region
struct RegionStats {
    std::string name;
    RegionExpectation expected;
    int sample_count = 0;
    float mean = 0.0f;
    float variance = 0.0f;
    float stddev = 0.0f;
    float min_val = 1e10f;
    float max_val = 0.0f;
    bool passed = false;
    std::string verdict;
};

// CPU struct matching Metal's GBufferPixel (32 bytes)
// Must match gbuffer_types.metal exactly for correct buffer indexing
struct GBufferPixelCPU {
    float world_pos[3];    // 12 bytes
    float normal[3];       // 12 bytes
    uint8_t base_color[4]; // 4 bytes (BGRA)
    uint32_t particle_id;  // 4 bytes
};  // Total: 32 bytes

/**
 * ShadowProbe - Reusable tool to diagnose shadow/lighting quality from final render
 *
 * Two modes:
 * 1. Screen-space regions (MVP): Uses normalized screen coordinates (0.0-1.0)
 * 2. World-space regions (future): Uses world coordinates, requires G-buffer
 *
 * Usage (screen-space):
 *   ShadowProbe probe;
 *   probe.add_screen_region("shadow_area", {0.4, 0.6, 0.5, 0.8}, RegionExpectation::Shadow);
 *   probe.analyze_screen(pixels, width, height);
 *   probe.log_results();
 *
 * Usage (world-space, future):
 *   probe.add_world_region("shadow_area", {-5,5, 2,8, -1,1}, RegionExpectation::Shadow);
 *   probe.analyze_world(pixels, gbuffer, width, height);
 */
class ShadowProbe {
public:
    // === Screen-space regions (MVP) ===
    // Coordinates are normalized 0.0-1.0 (x: left-right, y: top-bottom)
    void add_screen_region(const std::string& name,
                           const ScreenBox& bounds,
                           RegionExpectation expected);

    // Analyze using screen-space regions only (no G-buffer needed)
    void analyze_screen(const uint32_t* pixels, int width, int height);

    // === World-space regions (future) ===
    void add_world_region(const std::string& name,
                          const WorldBox& bounds,
                          RegionExpectation expected);

    // Analyze using world-space regions (requires G-buffer)
    void analyze_world(const uint32_t* pixels,
                       const void* gbuffer,
                       int width, int height);

    // Clear all regions
    void clear_regions();

    // Log results to stdout with verdicts
    void log_results() const;

    // Get stats programmatically (for automated tests)
    const std::vector<RegionStats>& get_results() const { return results_; }

    // Check if all regions passed
    bool all_passed() const;

    // Get summary string
    std::string get_summary() const;

private:
    struct ScreenProbeRegion {
        std::string name;
        ScreenBox bounds;
        RegionExpectation expected;
    };

    struct WorldProbeRegion {
        std::string name;
        WorldBox bounds;
        RegionExpectation expected;
    };

    std::vector<ScreenProbeRegion> screen_regions_;
    std::vector<WorldProbeRegion> world_regions_;
    std::vector<RegionStats> results_;
    int frame_number_ = 0;

    // Extract brightness from BGRA pixel (ITU-R BT.709 luminance)
    static float get_brightness(uint32_t bgra);

    // Determine verdict based on stats and expected type
    static std::pair<bool, std::string> compute_verdict(const RegionStats& stats);
};

#endif // SHADOW_PROBE_H
