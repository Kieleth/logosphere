#ifndef LOGOSPHERE_CORE_PROJECTION_MODE_H
#define LOGOSPHERE_CORE_PROJECTION_MODE_H

// ProjectionMode — selector for the engine's active projection.
// Lives in its own header so consumers (CoordinateTransformer,
// Engine, tests) can pick up the full definition without dragging
// in the engine state holder.
enum class ProjectionMode {
    Isometric,           // Classic isometric, no perspective
    IsometricWithDepth,  // Isometric with depth-based scaling
    Perspective,         // True 3D perspective
    Cabinet,             // Oblique / cabinet projection
    BirdsEye             // Top-down orthographic
};

#endif  // LOGOSPHERE_CORE_PROJECTION_MODE_H
