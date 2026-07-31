#ifndef LOGOSPHERE_I_DRAW_SURFACE_H
#define LOGOSPHERE_I_DRAW_SURFACE_H

#include <cstdint>
#include <string>

// IDrawSurface: narrow drawing interface for UI widgets, game HUDs,
// and debug-text overlays. The consumer (a widget, a HUD, a test
// overlay) asks to draw pixels, rectangles, lines, and text at
// screen coordinates and doesn't care which engine subsystem owns
// the actual framebuffer.
//
// Today RenderSystem is the only IDrawSurface implementation.
// Tomorrow a Logosphere::Rendering::UIRenderer or an offscreen
// compositor could satisfy the same contract without dragging in
// the rest of RenderSystem.
//
// Method signatures intentionally MIRROR the RenderSystem public
// API exactly so existing callers compile unchanged once they hold
// an IDrawSurface* instead of a RenderSystem*.

class IDrawSurface {
public:
    virtual ~IDrawSurface() = default;

    // Pixel ops
    virtual void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) = 0;

    // Lines
    virtual void draw_line(int x1, int y1, int x2, int y2,
                           uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void draw_line(int x1, int y1, int x2, int y2,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;

    // Rectangles
    virtual void draw_rectangle(int x, int y, int width, int height,
                                uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void draw_rectangle_outline(int x, int y, int width, int height,
                                        uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void draw_rectangle_alpha(int x, int y, int width, int height,
                                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
    // Widget aliases kept so the inline wrappers on RenderSystem keep
    // matching the interface (outline + alpha variants).
    virtual void draw_rect(int x, int y, int width, int height,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;
    virtual void fill_rect(int x, int y, int width, int height,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;

    // Text
    virtual void draw_text(int x, int y, const char* text,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) = 0;
    virtual void draw_string(int x, int y, const std::string& text,
                             uint8_t r, uint8_t g, uint8_t b) = 0;
    virtual void draw_string_scaled(int x, int y, const std::string& text,
                                    uint8_t r, uint8_t g, uint8_t b, float scale) = 0;
};

#endif // LOGOSPHERE_I_DRAW_SURFACE_H
