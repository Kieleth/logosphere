#ifndef PRIMITIVE_RENDERER_H
#define PRIMITIVE_RENDERER_H

#include <cstdint>
#include <algorithm>

// Forward declaration
class IDrawSurface;

/**
 * PrimitiveRenderer - Basic 2D shape drawing primitives
 *
 * Stateless Bresenham + scanline primitives. Every method takes an
 * IDrawSurface* as the draw target, so the primitive renderer has
 * no dependency on RenderSystem / framebuffer details.
 *
 * Historical note: these algorithms date back to Bresenham 1962.
 * Still used today for integer-friendly pixel accuracy.
 */
class PrimitiveRenderer {
public:
    // Line drawing primitives
    void draw_line(IDrawSurface* surface,
                   int x1, int y1, int x2, int y2,
                   uint8_t r, uint8_t g, uint8_t b) const;

    void draw_line_alpha(IDrawSurface* surface,
                         int x1, int y1, int x2, int y2,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) const;

    // Rectangle primitives
    void draw_rectangle(IDrawSurface* surface,
                        int x, int y, int width, int height,
                        uint8_t r, uint8_t g, uint8_t b) const;

    void draw_rectangle_outline(IDrawSurface* surface,
                                int x, int y, int width, int height,
                                uint8_t r, uint8_t g, uint8_t b) const;

    void draw_rectangle_alpha(IDrawSurface* surface,
                              int x, int y, int width, int height,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a) const;

    // Quad primitive (for arbitrary quadrilaterals)
    void draw_quad_filled(IDrawSurface* surface,
                          int x1, int y1, int x2, int y2,
                          int x3, int y3, int x4, int y4,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) const;

    // 3D line removed in the IDrawSurface migration: callers that need
    // it project world → screen themselves (via CoordinateTransformer)
    // and then call draw_line_alpha. ui_system does this inline.

private:
    // Bresenham's line algorithm — the classic integer-only line drawing
    void bresenham_line(IDrawSurface* surface,
                        int x1, int y1, int x2, int y2,
                        uint8_t r, uint8_t g, uint8_t b,
                        bool use_alpha, uint8_t a = 255) const;

    // Point-in-convex-quad test using cross products
    bool point_in_convex_quad(int px, int py,
                              int x1, int y1, int x2, int y2,
                              int x3, int y3, int x4, int y4) const;
};

#endif // PRIMITIVE_RENDERER_H
