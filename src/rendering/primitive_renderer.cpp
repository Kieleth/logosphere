#include "logosphere/rendering/primitive_renderer.h"
#include "logosphere/rendering/i_draw_surface.h"
#include <cmath>

// All draw calls go through IDrawSurface. Object-id tagging happens
// inside the surface implementation (the surface tracks a current
// object_id internally), so this code only needs set_pixel overloads.

void PrimitiveRenderer::draw_line(IDrawSurface* surface,
                                  int x1, int y1, int x2, int y2,
                                  uint8_t r, uint8_t g, uint8_t b) const {
    bresenham_line(surface, x1, y1, x2, y2, r, g, b, false, 255);
}

void PrimitiveRenderer::draw_line_alpha(IDrawSurface* surface,
                                        int x1, int y1, int x2, int y2,
                                        uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
    bresenham_line(surface, x1, y1, x2, y2, r, g, b, true, a);
}

void PrimitiveRenderer::draw_rectangle(IDrawSurface* surface,
                                       int x, int y, int width, int height,
                                       uint8_t r, uint8_t g, uint8_t b) const {
    // Filled rectangle — horizontal scanlines, cache-friendly.
    for (int py = y; py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            surface->set_pixel(px, py, r, g, b);
        }
    }
}

void PrimitiveRenderer::draw_rectangle_outline(IDrawSurface* surface,
                                               int x, int y, int width, int height,
                                               uint8_t r, uint8_t g, uint8_t b) const {
    // Top and bottom horizontal edges
    for (int px = x; px < x + width; px++) {
        surface->set_pixel(px, y, r, g, b);
        surface->set_pixel(px, y + height - 1, r, g, b);
    }
    // Left and right vertical edges
    for (int py = y; py < y + height; py++) {
        surface->set_pixel(x, py, r, g, b);
        surface->set_pixel(x + width - 1, py, r, g, b);
    }
}

void PrimitiveRenderer::draw_rectangle_alpha(IDrawSurface* surface,
                                             int x, int y, int width, int height,
                                             uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
    for (int py = y; py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            surface->set_pixel(px, py, r, g, b, a);
        }
    }
}

void PrimitiveRenderer::draw_quad_filled(IDrawSurface* surface,
                                         int x1, int y1, int x2, int y2,
                                         int x3, int y3, int x4, int y4,
                                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
    // Scanline fill for convex quadrilateral.
    int min_x = std::min({x1, x2, x3, x4});
    int max_x = std::max({x1, x2, x3, x4});
    int min_y = std::min({y1, y2, y3, y4});
    int max_y = std::max({y1, y2, y3, y4});

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            if (point_in_convex_quad(x, y, x1, y1, x2, y2, x3, y3, x4, y4)) {
                surface->set_pixel(x, y, r, g, b, a);
            }
        }
    }
}

void PrimitiveRenderer::bresenham_line(IDrawSurface* surface,
                                       int x1, int y1, int x2, int y2,
                                       uint8_t r, uint8_t g, uint8_t b,
                                       bool use_alpha, uint8_t a) const {
    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (use_alpha) {
            surface->set_pixel(x1, y1, r, g, b, a);
        } else {
            surface->set_pixel(x1, y1, r, g, b);
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

bool PrimitiveRenderer::point_in_convex_quad(int px, int py,
                                             int x1, int y1, int x2, int y2,
                                             int x3, int y3, int x4, int y4) const {
    int vx[4] = {x1, x2, x3, x4};
    int vy[4] = {y1, y2, y3, y4};

    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        int edge_x = vx[j] - vx[i];
        int edge_y = vy[j] - vy[i];
        int test_x = px - vx[i];
        int test_y = py - vy[i];
        int cross = edge_x * test_y - edge_y * test_x;
        if (cross < 0) {
            return false;
        }
    }
    return true;
}
