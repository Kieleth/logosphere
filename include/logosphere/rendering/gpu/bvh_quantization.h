#ifndef BVH_QUANTIZATION_H
#define BVH_QUANTIZATION_H

// =========================================================================
// BVH QUANTIZATION - Shared Helper Functions
// =========================================================================
// Compress BVH nodes from 48 bytes → 28 bytes to fit in GPU L2 cache
// Used by both forward and deferred rendering paths
//
// REFERENCE: BVH_QUANTIZATION_IMPLEMENTATION.md

#include <cstdint>
#include <limits>
#include <algorithm>

// Forward declarations to avoid circular includes
struct GPUBVHNode {
    float bbox_min[3];
    float _padding0;
    float bbox_max[3];
    float _padding1;
    int left_child;
    int right_child;
    int triangle_idx;
    int _padding2;
};

struct QuantizedBVHNode {
    uint16_t bbox_min_quantized[3];
    uint16_t bbox_max_quantized[3];
    int left_child;
    int right_child;
    int triangle_idx;
    uint16_t _pad0;
};

// World bounding box for quantization
struct WorldBounds {
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
};

// =========================================================================
// Calculate world bounds from BVH nodes
// =========================================================================
inline WorldBounds calculate_world_bounds_from_bvh(const GPUBVHNode* bvh_nodes, uint32_t bvh_count) {
    WorldBounds bounds = {
        .min_x = std::numeric_limits<float>::max(),
        .min_y = std::numeric_limits<float>::max(),
        .min_z = std::numeric_limits<float>::max(),
        .max_x = std::numeric_limits<float>::lowest(),
        .max_y = std::numeric_limits<float>::lowest(),
        .max_z = std::numeric_limits<float>::lowest()
    };

    // Iterate through all BVH nodes to find min/max coordinates
    for (uint32_t i = 0; i < bvh_count; ++i) {
        const GPUBVHNode& node = bvh_nodes[i];

        bounds.min_x = std::min(bounds.min_x, node.bbox_min[0]);
        bounds.min_y = std::min(bounds.min_y, node.bbox_min[1]);
        bounds.min_z = std::min(bounds.min_z, node.bbox_min[2]);

        bounds.max_x = std::max(bounds.max_x, node.bbox_max[0]);
        bounds.max_y = std::max(bounds.max_y, node.bbox_max[1]);
        bounds.max_z = std::max(bounds.max_z, node.bbox_max[2]);
    }

    return bounds;
}

// =========================================================================
// Quantize a single float coordinate to 16-bit unsigned integer
// =========================================================================
inline uint16_t quantize_coord(float value, float world_min, float world_max) {
    // Normalize to [0.0, 1.0]
    float range = world_max - world_min;
    if (range < 0.0001f) {  // Avoid division by zero
        return 0;
    }
    float normalized = (value - world_min) / range;
    normalized = std::max(0.0f, std::min(1.0f, normalized));  // Clamp

    // Quantize to 16-bit: [0, 65535]
    return static_cast<uint16_t>(normalized * 65535.0f);
}

// =========================================================================
// Quantize a single BVH node
// =========================================================================
inline QuantizedBVHNode quantize_bvh_node(const GPUBVHNode& node, const WorldBounds& bounds) {
    QuantizedBVHNode qnode = {};

    // Quantize bounding box minimum
    qnode.bbox_min_quantized[0] = quantize_coord(node.bbox_min[0], bounds.min_x, bounds.max_x);
    qnode.bbox_min_quantized[1] = quantize_coord(node.bbox_min[1], bounds.min_y, bounds.max_y);
    qnode.bbox_min_quantized[2] = quantize_coord(node.bbox_min[2], bounds.min_z, bounds.max_z);

    // Quantize bounding box maximum
    qnode.bbox_max_quantized[0] = quantize_coord(node.bbox_max[0], bounds.min_x, bounds.max_x);
    qnode.bbox_max_quantized[1] = quantize_coord(node.bbox_max[1], bounds.min_y, bounds.max_y);
    qnode.bbox_max_quantized[2] = quantize_coord(node.bbox_max[2], bounds.min_z, bounds.max_z);

    // Copy tree structure (not quantized)
    qnode.left_child = node.left_child;
    qnode.right_child = node.right_child;
    qnode.triangle_idx = node.triangle_idx;
    qnode._pad0 = 0;

    return qnode;
}

#endif // BVH_QUANTIZATION_H
