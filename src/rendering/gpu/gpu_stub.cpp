// GPU stub — the GLFW/Metal-less bodies for GPURasterizer and
// MetalComputeBridge. Compiled INSTEAD of gpu_rasterizer.mm and
// metal_compute_bridge.mm in the physics profile (never alongside
// them). Both headers already parse as pure C++ (pImpl: ObjC types are
// void* behind #ifdef __OBJC__), so this TU is plain C++17.
//
// Contract (see docs in the Stage-2 plan):
// - GPURasterizer::initialize() MUST return true: render_pipeline.cpp
//   treats false as fatal (std::exit(1)).
// - The two async rasterize entry points MUST invoke their completion
//   callback immediately (zeroed framebuffer/depth, null G-buffer) so
//   Engine::render()'s frame-ready spin completes instantly instead of
//   hitting its 5-second timeout each frame.
// - MetalComputeBridge::initialize() returns false (honest "no GPU");
//   its shadow rays report fully lit (1.0f) if anything ever calls them.
// - Do NOT redefine the header-inline methods (is_initialized,
//   supports_raytracing, get_vision_cone_enabled).

#include "logosphere/rendering/gpu/gpu_rasterizer.h"
#include "logosphere/rendering/gpu/metal_compute_bridge.h"

#include <vector>

namespace Logosphere {

// =========================================================================
// GPURasterizer
// =========================================================================

GPURasterizer::GPURasterizer() {
    // Only the scalars read outside the (absent) .mm need defined values.
    initialized_ = false;
    supports_raytracing_ = false;
    vision_cone_enabled_ = false;
    width_ = 0;
    height_ = 0;
}

GPURasterizer::~GPURasterizer() {}

// --- Shadow acceleration backend (see gpu_rasterizer.h for the contract) ---
// A build without Metal has no driver-owned acceleration structure, so the
// only backend it can report is SoftwareBVH. That is the honest answer AND the
// one that keeps the CPU trees being built, which a non-Metal target needs.
//
// Read docs/PORTING_SHADOWS.md before relying on this: the SoftwareBVH path
// currently renders NO LIGHTING. Reporting it here is correct, but it does not
// make it work. Fixing that path is the first task of a Linux or Windows port.
ShadowAccelBackend GPURasterizer::shadow_accel_backend() const {
    return ShadowAccelBackend::SoftwareBVH;
}

const char* to_string(ShadowAccelBackend b) {
    return b == ShadowAccelBackend::HardwareRT ? "HardwareRT" : "SoftwareBVH";
}

void set_forced_shadow_accel_backend(const char* /*name*/) {
    // No-op: there is no hardware backend to choose between on this profile.
}

// --- Serialized diagnostic mode (see gpu_rasterizer.h for the contract) ---
// Always off, and not merely unimplemented: this profile encodes no command
// buffers at all, so there is nothing to serialise and no per-stage cost to
// isolate. A port that grows a real GPU backend should implement both of these
// alongside it. The mode is how per-stage attribution stops being guesswork:
// pipelined stage timestamps overlap, so they are neither additive nor
// subtractive, and "what would removing this pass buy" has no answer without it.
bool gpu_serialized_diagnostic() { return false; }
void set_gpu_serialized_diagnostic(bool /*on*/) {}

bool GPURasterizer::initialize(int width, int height) {
    width_ = width;
    height_ = height;
    initialized_ = true;
    return true;  // false is treated as fatal by render_pipeline (std::exit)
}

bool GPURasterizer::build_acceleration_structure(const void*, uint32_t) { return false; }

void GPURasterizer::rasterize_minimal(uint32_t*) {}
void GPURasterizer::rasterize_triangle(uint32_t*, const float[6],
                                       uint8_t, uint8_t, uint8_t, uint8_t) {}
void GPURasterizer::rasterize_triangle_barycentric(uint32_t*, const float[6]) {}
void GPURasterizer::rasterize_triangle_with_depth(uint32_t*, uint32_t*, const float[9],
                                                  uint8_t, uint8_t, uint8_t, uint8_t) {}
void GPURasterizer::rasterize_triangles_batch(uint32_t*, uint32_t*,
                                              const TriangleGPU*, uint32_t) {}
void GPURasterizer::rasterize_triangles_lit(uint32_t*, uint32_t*,
                                            const TriangleLit*, uint32_t,
                                            const void*, uint32_t,
                                            const void*, uint32_t,
                                            const void*, uint32_t) {}

namespace {
// Zeroed scratch buffers handed to the completion callback. The
// pipeline memcpys width*height from each, so they must be real
// allocations of that size. Main-thread only (matches the render path).
void fire_completion(GPURasterizer::CompletionCallback callback,
                     void* user_data, int width, int height) {
    if (!callback) return;
    static std::vector<uint32_t> framebuffer;
    static std::vector<uint32_t> depth;
    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
    framebuffer.assign(n, 0u);
    depth.assign(n, 0u);
    callback(framebuffer.data(), depth.data(), /*gbuffer=*/nullptr,
             width, height, user_data);
}
}  // namespace

void GPURasterizer::rasterize_triangles_lit_async(
    const TriangleLit*, uint32_t,
    const void*, uint32_t,
    const void*, uint32_t,
    const void*, uint32_t,
    const uint32_t*, const uint32_t*, const uint32_t*,
    int, int,
    CompletionCallback callback, void* user_data) {
    fire_completion(callback, user_data, width_, height_);
}

void GPURasterizer::rasterize_triangles_deferred_async(
    const TriangleLit*, uint32_t,
    const void*, uint32_t,
    const void*, uint32_t,
    const void*, uint32_t,
    const void*, uint32_t,
    const void*, uint32_t,
    const uint32_t*, const uint32_t*, const uint32_t*,
    int, int,
    const uint8_t*, uint32_t,
    const void*, uint32_t,
    CompletionCallback callback, void* user_data) {
    fire_completion(callback, user_data, width_, height_);
}

void GPURasterizer::set_transparent_triangles(const TriangleLit*, uint32_t) {}
void GPURasterizer::wait_for_completion() {}

bool GPURasterizer::read_latest_framebuffer(uint32_t*, int&, int&) { return false; }

void GPURasterizer::reset_temporal_state() {}

int GPURasterizer::acquire_all_slots() { return 0; }
void GPURasterizer::release_all_slots(int) {}

void GPURasterizer::set_vision_cone_enabled(bool enabled) { vision_cone_enabled_ = enabled; }
void GPURasterizer::set_vision_cone(float, float, float, float, float) {}
void GPURasterizer::set_vision_cone_style(float, float) {}
void GPURasterizer::set_vision_cone_focus(float, float, float) {}
void GPURasterizer::set_vision_cone_occlusion(const float*, int) {}
void GPURasterizer::clear_vision_cone_occlusion() {}

void GPURasterizer::set_vision_memory_enabled(bool) {}
void GPURasterizer::set_vision_memory_extent(float, float, float, float, int) {}
void GPURasterizer::set_vision_memory_decay(float, float) {}
void GPURasterizer::update_vision_memory(float) {}
void GPURasterizer::set_dynamic_particle_map(const uint8_t*, size_t) {}

void GPURasterizer::set_shadow_culling_camera(float, float) {}
void GPURasterizer::set_shadow_projection_params(float) {}

bool GPURasterizer::read_shadow_debug(int, int, uint32_t&, float&, uint32_t&) const { return false; }
bool GPURasterizer::read_gi_debug(int, int, float&, float&, float&, float&) const { return false; }
bool GPURasterizer::read_ssdo_debug(int, int, float&, float&, float&, float&) const { return false; }
void GPURasterizer::set_triangle_bboxes(const int32_t*, uint32_t) {}
bool GPURasterizer::read_gbuffer_debug(int, int, uint8_t&, uint8_t&, uint8_t&, uint32_t&) const { return false; }
bool GPURasterizer::read_gpu_framebuffer(int, int, uint8_t&, uint8_t&, uint8_t&) const { return false; }

void GPURasterizer::convert_surface_to_gpu_triangles(
    const struct Surface&, class CameraSystem&,
    uint8_t, uint8_t, uint8_t, uint8_t,
    std::vector<TriangleGPU>&) {}

void GPURasterizer::convert_surface_to_lit_triangles(
    const struct Surface&, class CameraSystem&,
    uint8_t, uint8_t, uint8_t, uint8_t,
    std::vector<TriangleLit>&,
    const float* /*smooth_center*/) {}

// =========================================================================
// MetalComputeBridge
// =========================================================================

MetalComputeBridge::MetalComputeBridge() {
    initialized_ = false;
}

MetalComputeBridge::~MetalComputeBridge() {}

bool MetalComputeBridge::initialize() { return false; }  // honest: no GPU here

float MetalComputeBridge::trace_shadow_ray(const ShadowRay&, const ShadowTriangle&) {
    return 1.0f;  // fully lit
}

float MetalComputeBridge::trace_shadow_ray_multi(const ShadowRay&,
                                                 const ShadowTriangle*, int) {
    return 1.0f;
}

namespace {
void fill_lit(float* results, int ray_count) {
    if (!results) return;
    for (int i = 0; i < ray_count; ++i) results[i] = 1.0f;
}
}  // namespace

void MetalComputeBridge::trace_shadow_rays_parallel(const ShadowRay*, int ray_count,
                                                    const ShadowTriangle*, int,
                                                    float* results) {
    fill_lit(results, ray_count);
}

void MetalComputeBridge::trace_shadow_rays_batched(const ShadowRay*, int ray_count,
                                                   const ShadowTriangle*, int,
                                                   float* results) {
    fill_lit(results, ray_count);
}

void MetalComputeBridge::upload_bvh(const void*, int) {}

void MetalComputeBridge::trace_shadow_rays_batched_bvh(const ShadowRay*, int ray_count,
                                                       const ShadowTriangle*, int,
                                                       float* results) {
    fill_lit(results, ray_count);
}

void MetalComputeBridge::compute_lighting(const PixelData*, int,
                                          const LightData*, int,
                                          const float*, float*) {}

void MetalComputeBridge::trace_shadow_rays_batched_bvh_async(
    const ShadowRay*, int ray_count,
    const ShadowTriangle*, int,
    CompletionCallback callback, void* user_data) {
    if (!callback) return;
    static std::vector<float> results;
    results.assign(static_cast<size_t>(ray_count > 0 ? ray_count : 0), 1.0f);
    callback(results.data(), ray_count, user_data);
}

void MetalComputeBridge::wait_for_completion() {}

}  // namespace Logosphere
