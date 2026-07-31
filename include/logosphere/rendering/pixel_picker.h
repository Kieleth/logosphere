#ifndef LOGOSPHERE_PIXEL_PICKER_H
#define LOGOSPHERE_PIXEL_PICKER_H

// Forward decls
class PixelBuffer;
class SparseObjectMap;
class ResolutionManager;
struct EnhancedPixel;

namespace Logosphere {
namespace Rendering {

// PixelPicker: mouse hit-testing on the rendered frame. Translates
// framebuffer coordinates (from the OS / window) into render-buffer
// coordinates (where the GPU painted), then queries the object_map_
// to identify what entity is at the given pixel.
//
// Held by Engine. Engine wires it once at startup with refs to the
// rendering primitives that already live inside RenderSystem; no
// caching of dims (the framebuffer/render scale is recomputed per
// query — it's a handful of multiplies).
class PixelPicker {
public:
    PixelPicker(const PixelBuffer& render_buffer,
                const SparseObjectMap& object_map,
                const ResolutionManager& resolution);

    // Mouse position in framebuffer pixels (raw OS coords). Returns
    // -1 if the pixel is outside the rendered region (centered black
    // bars in scaled mode), otherwise the entity / UI object id at
    // that pixel. BACKGROUND collapses to -1.
    int get_object_at_pixel(int fb_x, int fb_y) const;

    // Same query but the caller has already mapped to render-buffer
    // coordinates (e.g., scanning a rendered frame for edges). No
    // transform, just bounds-check + object_map lookup.
    int get_object_at_pixel_direct(int render_x, int render_y) const;

    // Read full pixel struct (color + object id) at render-buffer
    // coordinates. Used by hover inspectors.
    EnhancedPixel get_enhanced_pixel(int render_x, int render_y) const;

private:
    const PixelBuffer&        render_buffer_;
    const SparseObjectMap&    object_map_;
    const ResolutionManager&  resolution_;
};

} // namespace Rendering
} // namespace Logosphere

#endif // LOGOSPHERE_PIXEL_PICKER_H
