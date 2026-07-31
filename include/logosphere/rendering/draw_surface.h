#ifndef LOGOSPHERE_DRAW_SURFACE_H
#define LOGOSPHERE_DRAW_SURFACE_H

#include "logosphere/rendering/i_draw_surface.h"

// Forward decls: everything needed to satisfy the interface is held
// by reference / pointer.
class PixelBuffer;
class SparseObjectMap;
class FontRenderer;
class PrimitiveRenderer;

namespace Logosphere {
namespace Rendering {

// DrawSurface: concrete IDrawSurface that the Engine owns and hands
// to UI widgets, game HUDs, and any other consumer that needs to
// paint pixels / text / primitives onto the render target.
//
// Holds non-owning refs to the underlying drawing primitives and
// target buffer. Those are kept inside RenderSystem today; the refs
// stay valid for the life of the Engine. Once the drawing path is
// fully extracted from RenderSystem, DrawSurface can own these
// primitives directly.
class DrawSurface : public IDrawSurface {
public:
    DrawSurface(PixelBuffer& target,
                SparseObjectMap& object_map,
                FontRenderer& font,
                PrimitiveRenderer& primitives);

    ~DrawSurface() override = default;

    // IDrawSurface — pixel ops
    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) override;

    // Lines
    void draw_line(int x1, int y1, int x2, int y2,
                   uint8_t r, uint8_t g, uint8_t b) override;
    void draw_line(int x1, int y1, int x2, int y2,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;

    // Rectangles
    void draw_rectangle(int x, int y, int width, int height,
                        uint8_t r, uint8_t g, uint8_t b) override;
    void draw_rectangle_outline(int x, int y, int width, int height,
                                uint8_t r, uint8_t g, uint8_t b) override;
    void draw_rectangle_alpha(int x, int y, int width, int height,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void draw_rect(int x, int y, int width, int height,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void fill_rect(int x, int y, int width, int height,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;

    // Text
    void draw_text(int x, int y, const char* text,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) override;
    void draw_string(int x, int y, const std::string& text,
                     uint8_t r, uint8_t g, uint8_t b) override;
    void draw_string_scaled(int x, int y, const std::string& text,
                            uint8_t r, uint8_t g, uint8_t b, float scale) override;

    // Object-id tagging. set_pixel uses current_object_id_ when non-
    // zero; callers can push a specific id for a draw sequence.
    void set_current_object_id(int id) { current_object_id_ = id; }
    int  get_current_object_id() const { return current_object_id_; }

private:
    void draw_char(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b);
    void draw_char_scaled(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, float scale);

    PixelBuffer&       target_;
    SparseObjectMap&   object_map_;
    FontRenderer&      font_;
    PrimitiveRenderer& primitives_;
    int                current_object_id_ = 0;
};

} // namespace Rendering
} // namespace Logosphere

#endif // LOGOSPHERE_DRAW_SURFACE_H
