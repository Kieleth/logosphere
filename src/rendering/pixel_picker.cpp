#include "logosphere/rendering/pixel_picker.h"

#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/sparse_object_map.h"
#include "logosphere/rendering/resolution_manager.h"
#include "object_id.h"

#include <algorithm>

namespace Logosphere {
namespace Rendering {

PixelPicker::PixelPicker(const PixelBuffer& render_buffer,
                         const SparseObjectMap& object_map,
                         const ResolutionManager& resolution)
    : render_buffer_(render_buffer)
    , object_map_(object_map)
    , resolution_(resolution) {}

int PixelPicker::get_object_at_pixel(int fb_x, int fb_y) const {
    const int render_w = resolution_.get_render_width();
    const int render_h = resolution_.get_render_height();
    const int fb_w     = resolution_.get_framebuffer_width();
    const int fb_h     = resolution_.get_framebuffer_height();

    int render_x = fb_x;
    int render_y = fb_y;

    if (render_w != fb_w || render_h != fb_h) {
        // Scaled mode: framebuffer holds the rendered region centered
        // with black bars on the short axis. Map fb → render with the
        // uniform aspect-preserving scale, and reject points in the
        // black bars early.
        const float uniform_scale = std::min(
            static_cast<float>(fb_w) / render_w,
            static_cast<float>(fb_h) / render_h);
        const int scaled_width  = static_cast<int>(render_w * uniform_scale);
        const int scaled_height = static_cast<int>(render_h * uniform_scale);
        const int offset_x = (fb_w - scaled_width)  / 2;
        const int offset_y = (fb_h - scaled_height) / 2;

        if (fb_x < offset_x || fb_x >= offset_x + scaled_width ||
            fb_y < offset_y || fb_y >= offset_y + scaled_height) {
            return -1;  // outside render area (black bars)
        }
        render_x = static_cast<int>((fb_x - offset_x) / uniform_scale);
        render_y = static_cast<int>((fb_y - offset_y) / uniform_scale);
    }

    if (render_x < 0 || render_x >= render_w || render_y < 0 || render_y >= render_h) {
        return -1;
    }

    const int object_id = object_map_.get_object(render_x, render_y);
    return (object_id == ObjectID::BACKGROUND) ? -1 : object_id;
}

int PixelPicker::get_object_at_pixel_direct(int render_x, int render_y) const {
    const int render_w = resolution_.get_render_width();
    const int render_h = resolution_.get_render_height();
    if (render_x < 0 || render_x >= render_w || render_y < 0 || render_y >= render_h) {
        return -1;
    }
    const int object_id = object_map_.get_object(render_x, render_y);
    return (object_id == ObjectID::BACKGROUND) ? -1 : object_id;
}

EnhancedPixel PixelPicker::get_enhanced_pixel(int render_x, int render_y) const {
    return render_buffer_.get_pixel(render_x, render_y);
}

} // namespace Rendering
} // namespace Logosphere
