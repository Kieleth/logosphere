#include "logosphere/rendering/draw_surface.h"

#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/sparse_object_map.h"
#include "logosphere/rendering/font_renderer.h"
#include "logosphere/rendering/primitive_renderer.h"

#include <algorithm>
#include <string>

namespace Logosphere {
namespace Rendering {

DrawSurface::DrawSurface(PixelBuffer& target,
                         SparseObjectMap& object_map,
                         FontRenderer& font,
                         PrimitiveRenderer& primitives)
    : target_(target)
    , object_map_(object_map)
    , font_(font)
    , primitives_(primitives) {}

// =========================================================================
// Pixel ops
// Bodies mirror EngineRenderState::set_pixel / set_pixel_with_object. Write
// to the render buffer, tag object_map_ when current_object_id_ != 0.
// =========================================================================

void DrawSurface::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 255) {
        target_.set_pixel_with_object(x, y, r, g, b, a, 0);
    } else if (a > 0) {
        target_.blend_pixel(x, y, r, g, b, a, 0);
    } else {
        return;
    }
    if (current_object_id_ != 0) {
        object_map_.set_object(x, y, current_object_id_);
    }
}

// =========================================================================
// Lines / Rectangles — delegate to PrimitiveRenderer, which takes
// IDrawSurface* directly. (The old note here described an rs_compat_
// EngineRenderState shim; that migration already happened and no such
// member exists.)
// =========================================================================

void DrawSurface::draw_line(int x1, int y1, int x2, int y2,
                            uint8_t r, uint8_t g, uint8_t b) {
    primitives_.draw_line(this, x1, y1, x2, y2, r, g, b);
}

void DrawSurface::draw_line(int x1, int y1, int x2, int y2,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    primitives_.draw_line_alpha(this, x1, y1, x2, y2, r, g, b, a);
}

void DrawSurface::draw_rectangle(int x, int y, int width, int height,
                                 uint8_t r, uint8_t g, uint8_t b) {
    primitives_.draw_rectangle(this, x, y, width, height, r, g, b);
}

void DrawSurface::draw_rectangle_outline(int x, int y, int width, int height,
                                         uint8_t r, uint8_t g, uint8_t b) {
    primitives_.draw_rectangle_outline(this, x, y, width, height, r, g, b);
}

void DrawSurface::draw_rectangle_alpha(int x, int y, int width, int height,
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    primitives_.draw_rectangle_alpha(this, x, y, width, height, r, g, b, a);
}

void DrawSurface::draw_rect(int x, int y, int width, int height,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t /*a*/) {
    // Widget outline variant — alpha ignored, same as EngineRenderState's alias.
    draw_rectangle_outline(x, y, width, height, r, g, b);
}

void DrawSurface::fill_rect(int x, int y, int width, int height,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    draw_rectangle_alpha(x, y, width, height, r, g, b, a);
}

// =========================================================================
// Text — body mirrors EngineRenderState::draw_char{,_scaled}/draw_string{,_scaled}.
// Uses FontRenderer's glyph bitmap; each set pixel goes through set_pixel
// so object-id tagging is consistent with other draws.
// =========================================================================

void DrawSurface::draw_char(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b) {
    int char_index = font_.get_char_index(c);
    const uint8_t* char_data = FontRenderer::get_char_data(char_index);
    for (int row = 0; row < FontRenderer::CHAR_HEIGHT; row++) {
        uint8_t row_data = char_data[row];
        for (int col = 0; col < FontRenderer::CHAR_WIDTH; col++) {
            if (row_data & (0b10000 >> col)) {
                set_pixel(x + col, y + row, r, g, b);
            }
        }
    }
}

void DrawSurface::draw_char_scaled(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, float scale) {
    int char_index = font_.get_char_index(c);
    const uint8_t* char_data = FontRenderer::get_char_data(char_index);
    int scale_int = std::max(1, static_cast<int>(scale));
    for (int row = 0; row < FontRenderer::CHAR_HEIGHT; row++) {
        uint8_t row_data = char_data[row];
        for (int col = 0; col < FontRenderer::CHAR_WIDTH; col++) {
            if (row_data & (0b10000 >> col)) {
                for (int dy = 0; dy < scale_int; dy++) {
                    for (int dx = 0; dx < scale_int; dx++) {
                        set_pixel(x + col * scale_int + dx,
                                  y + row * scale_int + dy, r, g, b);
                    }
                }
            }
        }
    }
}

void DrawSurface::draw_string(int x, int y, const std::string& text,
                              uint8_t r, uint8_t g, uint8_t b) {
    int current_x = x;
    for (char c : text) {
        draw_char(current_x, y, c, r, g, b);
        current_x += FontRenderer::CHAR_WIDTH + FontRenderer::CHAR_SPACING;
    }
}

void DrawSurface::draw_string_scaled(int x, int y, const std::string& text,
                                     uint8_t r, uint8_t g, uint8_t b, float scale) {
    int scale_int = std::max(1, static_cast<int>(scale));
    int scaled_char_width = FontRenderer::CHAR_WIDTH * scale_int;
    int scaled_spacing    = FontRenderer::CHAR_SPACING * scale_int;
    int current_x = x;
    for (char c : text) {
        draw_char_scaled(current_x, y, c, r, g, b, scale);
        current_x += scaled_char_width + scaled_spacing;
    }
}

void DrawSurface::draw_text(int x, int y, const char* text,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t /*a*/) {
    // Alpha currently ignored, same as EngineRenderState's wrapper. Can be
    // wired through once blend_pixel-based text is added.
    if (text) {
        draw_string(x, y, std::string(text), r, g, b);
    }
}

} // namespace Rendering
} // namespace Logosphere
