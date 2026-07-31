#include "shadow_probe.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

void ShadowProbe::add_screen_region(const std::string& name,
                                     const ScreenBox& bounds,
                                     RegionExpectation expected) {
    screen_regions_.push_back({name, bounds, expected});
}

void ShadowProbe::add_world_region(const std::string& name,
                                    const WorldBox& bounds,
                                    RegionExpectation expected) {
    world_regions_.push_back({name, bounds, expected});
}

void ShadowProbe::clear_regions() {
    screen_regions_.clear();
    world_regions_.clear();
    results_.clear();
}

float ShadowProbe::get_brightness(uint32_t bgra) {
    // Extract RGB from BGRA
    uint8_t b = (bgra >> 0) & 0xFF;
    uint8_t g = (bgra >> 8) & 0xFF;
    uint8_t r = (bgra >> 16) & 0xFF;

    // ITU-R BT.709 luminance formula
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

std::pair<bool, std::string> ShadowProbe::compute_verdict(const RegionStats& stats) {
    if (stats.sample_count == 0) {
        return std::make_pair(false, std::string("NO_SAMPLES"));
    }

    switch (stats.expected) {
        case RegionExpectation::Shadow:
            // Shadow regions should be dark with low variance
            if (stats.mean < 50.0f && stats.stddev < 30.0f) {
                return std::make_pair(true, std::string("OK"));
            } else if (stats.mean > 150.0f) {
                return std::make_pair(false, std::string("BROKEN-TOO_BRIGHT"));
            } else if (stats.stddev > 50.0f) {
                return std::make_pair(false, std::string("BROKEN-HIGH_VARIANCE"));
            } else {
                return std::make_pair(false, std::string("SUSPECT"));
            }

        case RegionExpectation::Lit:
            // Lit regions should be bright with relatively low variance
            if (stats.mean > 100.0f && stats.stddev < stats.mean * 0.5f) {
                return std::make_pair(true, std::string("OK"));
            } else if (stats.mean < 50.0f) {
                return std::make_pair(false, std::string("BROKEN-TOO_DARK"));
            } else if (stats.stddev > stats.mean) {
                return std::make_pair(false, std::string("BROKEN-HIGH_VARIANCE"));
            } else {
                return std::make_pair(false, std::string("SUSPECT"));
            }

        case RegionExpectation::Penumbra:
            // Penumbra should have visible variance (soft transition)
            if (stats.stddev > 10.0f && stats.stddev < stats.mean) {
                return std::make_pair(true, std::string("OK"));
            } else if (stats.stddev < 5.0f) {
                return std::make_pair(false, std::string("TOO_UNIFORM"));
            } else {
                return std::make_pair(false, std::string("SUSPECT"));
            }
    }

    return std::make_pair(false, std::string("UNKNOWN"));
}

void ShadowProbe::analyze_screen(const uint32_t* pixels, int width, int height) {
    if (!pixels || screen_regions_.empty()) {
        return;
    }

    frame_number_++;
    results_.clear();

    // Initialize accumulators for each region
    struct Accumulator {
        double sum = 0.0;
        double sum_sq = 0.0;
        float min_val = 1e10f;
        float max_val = 0.0f;
        int count = 0;
    };
    std::vector<Accumulator> accums(screen_regions_.size());

    // Sample every 2nd pixel for speed (still captures 25% of pixels)
    const int step = 2;

    for (int y = 0; y < height; y += step) {
        float norm_y = static_cast<float>(y) / static_cast<float>(height);

        for (int x = 0; x < width; x += step) {
            float norm_x = static_cast<float>(x) / static_cast<float>(width);

            int idx = y * width + x;
            float brightness = get_brightness(pixels[idx]);

            // Check which region(s) this pixel belongs to
            for (size_t r = 0; r < screen_regions_.size(); r++) {
                if (screen_regions_[r].bounds.contains(norm_x, norm_y)) {
                    accums[r].sum += brightness;
                    accums[r].sum_sq += brightness * brightness;
                    accums[r].min_val = std::min(accums[r].min_val, brightness);
                    accums[r].max_val = std::max(accums[r].max_val, brightness);
                    accums[r].count++;
                }
            }
        }
    }

    // Compute final stats for each region
    for (size_t r = 0; r < screen_regions_.size(); r++) {
        RegionStats stats;
        stats.name = screen_regions_[r].name;
        stats.expected = screen_regions_[r].expected;
        stats.sample_count = accums[r].count;

        if (accums[r].count > 0) {
            stats.mean = static_cast<float>(accums[r].sum / accums[r].count);
            stats.variance = static_cast<float>((accums[r].sum_sq / accums[r].count) -
                                                 (stats.mean * stats.mean));
            stats.stddev = std::sqrt(std::max(0.0f, stats.variance));
            stats.min_val = accums[r].min_val;
            stats.max_val = accums[r].max_val;
        } else {
            stats.min_val = 0.0f;
            stats.max_val = 0.0f;
        }

        std::pair<bool, std::string> result = compute_verdict(stats);
        stats.passed = result.first;
        stats.verdict = result.second;

        results_.push_back(stats);
    }
}

void ShadowProbe::analyze_world(const uint32_t* pixels,
                                 const void* gbuffer,
                                 int width, int height) {
    if (!pixels || !gbuffer || world_regions_.empty()) {
        return;
    }

    frame_number_++;
    // Don't clear results if we already analyzed screen regions
    // results_.clear();

    const GBufferPixelCPU* gbuf = static_cast<const GBufferPixelCPU*>(gbuffer);

    // Initialize accumulators for each region
    struct Accumulator {
        double sum = 0.0;
        double sum_sq = 0.0;
        float min_val = 1e10f;
        float max_val = 0.0f;
        int count = 0;
    };
    std::vector<Accumulator> accums(world_regions_.size());

    // Sample every 4th pixel for speed
    const int step = 4;

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            int idx = y * width + x;

            // Get world position from G-buffer
            const GBufferPixelCPU& gpx = gbuf[idx];

            // Skip sky pixels (particle_id == UINT32_MAX)
            if (gpx.particle_id == UINT32_MAX) {
                continue;
            }

            float wx = gpx.world_pos[0];
            float wy = gpx.world_pos[1];
            float wz = gpx.world_pos[2];

            // Get brightness from final pixel
            float brightness = get_brightness(pixels[idx]);

            // Check which region(s) this pixel belongs to
            for (size_t r = 0; r < world_regions_.size(); r++) {
                if (world_regions_[r].bounds.contains(wx, wy, wz)) {
                    accums[r].sum += brightness;
                    accums[r].sum_sq += brightness * brightness;
                    accums[r].min_val = std::min(accums[r].min_val, brightness);
                    accums[r].max_val = std::max(accums[r].max_val, brightness);
                    accums[r].count++;
                }
            }
        }
    }

    // Compute final stats for each region
    for (size_t r = 0; r < world_regions_.size(); r++) {
        RegionStats stats;
        stats.name = world_regions_[r].name;
        stats.expected = world_regions_[r].expected;
        stats.sample_count = accums[r].count;

        if (accums[r].count > 0) {
            stats.mean = static_cast<float>(accums[r].sum / accums[r].count);
            stats.variance = static_cast<float>((accums[r].sum_sq / accums[r].count) -
                                                 (stats.mean * stats.mean));
            stats.stddev = std::sqrt(std::max(0.0f, stats.variance));
            stats.min_val = accums[r].min_val;
            stats.max_val = accums[r].max_val;
        } else {
            stats.min_val = 0.0f;
            stats.max_val = 0.0f;
        }

        std::pair<bool, std::string> result = compute_verdict(stats);
        stats.passed = result.first;
        stats.verdict = result.second;

        results_.push_back(stats);
    }
}

void ShadowProbe::log_results() const {
    if (results_.empty()) {
        std::cout << "[SHADOW_PROBE] No results (no regions defined or not analyzed yet)" << std::endl;
        return;
    }

    std::cout << "\n[SHADOW_PROBE] === Region Statistics (Frame " << frame_number_ << ") ===" << std::endl;

    int passed_count = 0;
    for (const auto& r : results_) {
        const char* expected_str = "";
        switch (r.expected) {
            case RegionExpectation::Shadow:   expected_str = "shadow"; break;
            case RegionExpectation::Lit:      expected_str = "lit"; break;
            case RegionExpectation::Penumbra: expected_str = "penumbra"; break;
        }

        std::cout << "[SHADOW_PROBE] " << r.name << " (" << expected_str << "): "
                  << "n=" << r.sample_count
                  << " mean=" << std::fixed << std::setprecision(1) << r.mean
                  << " std=" << r.stddev
                  << " [" << r.min_val << ".." << r.max_val << "]"
                  << " -> " << r.verdict << std::endl;

        if (r.passed) passed_count++;
    }

    std::cout << "[SHADOW_PROBE] ========================================" << std::endl;
    std::cout << "[SHADOW_PROBE] Summary: " << passed_count << "/" << results_.size()
              << " regions PASSED, " << (results_.size() - passed_count) << "/" << results_.size()
              << " FAILED" << std::endl;
    std::cout << std::endl;
}

bool ShadowProbe::all_passed() const {
    for (const auto& r : results_) {
        if (!r.passed) return false;
    }
    return !results_.empty();
}

std::string ShadowProbe::get_summary() const {
    int passed = 0;
    for (const auto& r : results_) {
        if (r.passed) passed++;
    }
    return std::to_string(passed) + "/" + std::to_string(results_.size()) + " regions passed";
}
