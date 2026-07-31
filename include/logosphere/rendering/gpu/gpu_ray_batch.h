#pragma once

namespace Logosphere {

// Ray batch: Multiple rays processed by one GPU thread
// Phase II-A: Establishes multiple-rays-per-thread pattern
struct GPURayBatch {
    static constexpr int RAYS_PER_THREAD = 8;  // SIMD-friendly (256 bits / 32 bytes)

    struct Ray {
        float origin_x, origin_y, origin_z;     // 12 bytes
        float dir_x, dir_y, dir_z;              // 12 bytes
        // Total: 24 bytes per ray (GPU will pad to 32 for alignment)
    };

    Ray rays[RAYS_PER_THREAD];  // 8 rays
    int count;                   // Actual rays in batch (may be < 8 for last batch)
    int padding[3];              // Align to 16-byte boundary
    // Total: 8*32 + 16 = 272 bytes per batch
};

} // namespace Logosphere
