#include "logosphere/rendering/resolution_manager.h"
#include "logosphere/rendering/pixel_buffer.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// Static preset definitions
const ResolutionManager::PresetDefinition ResolutionManager::preset_definitions_[] = {
    { ResolutionPreset::ULTRA_LOW, 1024, 768, "Lowest (1024x768)" },
    { ResolutionPreset::LOW, 1280, 960, "Low (1280x960)" },
    { ResolutionPreset::MEDIUM, 1600, 1200, "Medium (1600x1200)" },
    { ResolutionPreset::HIGH, 1920, 1440, "High (1920x1440)" },
    { ResolutionPreset::NATIVE, 0, 0, "Native (Window Size)" },  // 0,0 means use window size
    { ResolutionPreset::RETINA, 0, 0, "Retina (Full Resolution)" },  // 0,0 means use framebuffer size
    { ResolutionPreset::CUSTOM, 0, 0, "Custom" }
};

ResolutionManager::ResolutionManager() {
    // Default to maximum resolution (retina)
    current_preset_ = ResolutionPreset::RETINA;
}

void ResolutionManager::set_window_size(int width, int height) {
    window_width_ = width;
    window_height_ = height;
    
    // If using NATIVE preset, update render resolution to match
    if (current_preset_ == ResolutionPreset::NATIVE) {
        render_width_ = width;
        render_height_ = height;
        std::cout << "[ResolutionManager] NATIVE preset: render resolution updated to " 
                  << render_width_ << "x" << render_height_ << std::endl;
    }
    
    std::cout << "[ResolutionManager] Window size set to " << width << "x" << height 
              << ", render is now " << render_width_ << "x" << render_height_ << std::endl;
}

void ResolutionManager::set_framebuffer_size(int width, int height) {
    framebuffer_width_ = width;
    framebuffer_height_ = height;
    
    // If using RETINA preset, update render resolution to match
    if (current_preset_ == ResolutionPreset::RETINA) {
        render_width_ = width;
        render_height_ = height;
    }
    
    std::cout << "[ResolutionManager] Framebuffer size set to " << width << "x" << height 
              << " (DPI scale: " << get_window_to_framebuffer_scale() << ")" << std::endl;
}

void ResolutionManager::set_render_resolution(int width, int height) {
    // DIAGNOSTIC: Show inputs
    std::cout << "[ResolutionManager] set_render_resolution(" << width << ", " << height << ")" << std::endl;
    std::cout << "  Current window: " << window_width_ << "×" << window_height_ << std::endl;

    // Enforce minimum resolution (cannot be smaller than window)
    render_width_ = std::max(width, window_width_);
    render_height_ = std::max(height, window_height_);

    // DIAGNOSTIC: Show result
    if (render_width_ != width || render_height_ != height) {
        std::cerr << "  ⚠️ CLAMPED: " << width << "×" << height
                  << " → " << render_width_ << "×" << render_height_ << std::endl;
    }

    // Update preset to CUSTOM since user manually set resolution
    current_preset_ = ResolutionPreset::CUSTOM;

    std::cout << "[ResolutionManager] State after set_render_resolution:" << std::endl;
    std::cout << "  render: " << render_width_ << "×" << render_height_ << std::endl;
    std::cout << "  window: " << window_width_ << "×" << window_height_ << std::endl;
    std::cout << "  framebuffer: " << framebuffer_width_ << "×" << framebuffer_height_ << std::endl;
    std::cout << "  preset: " << (int)current_preset_ << std::endl;
}

void ResolutionManager::set_resolution_preset(ResolutionPreset preset) {
    current_preset_ = preset;
    
    // Apply the preset
    switch (preset) {
        case ResolutionPreset::ULTRA_LOW:
            render_width_ = 1024;  // Minimum useful resolution
            render_height_ = 768;
            break;
        case ResolutionPreset::LOW:
            render_width_ = 1280;  // Was 800x600, now more reasonable
            render_height_ = 960;
            break;
        case ResolutionPreset::MEDIUM:
            render_width_ = 1600;  // Was 1280x960, now 1600x1200
            render_height_ = 1200;
            break;
        case ResolutionPreset::HIGH:
            render_width_ = 1920;  // Was 1600x1200, now 1920x1440
            render_height_ = 1440;
            break;
        case ResolutionPreset::NATIVE:
            render_width_ = window_width_;
            render_height_ = window_height_;
            std::cout << "[ResolutionManager] NATIVE: using window " << window_width_ 
                      << "x" << window_height_ << std::endl;
            break;
        case ResolutionPreset::RETINA:
            render_width_ = framebuffer_width_;
            render_height_ = framebuffer_height_;
            break;
        case ResolutionPreset::CUSTOM:
            // Keep current render resolution
            break;
    }
    
    // REMOVED: "Enforce minimum resolution" was preventing lower resolution presets!
    // This was forcing all presets to be at least window size, making them all the same
    // render_width_ = std::max(render_width_, window_width_);
    // render_height_ = std::max(render_height_, window_height_);
    
    std::cout << "[ResolutionManager] Preset changed to " << get_preset_name(preset)
              << " - Render resolution: " << render_width_ << "x" << render_height_ << std::endl;
}

float ResolutionManager::get_render_to_window_scale() const {
    if (window_width_ == 0 || window_height_ == 0) return 1.0f;
    
    // Use the smaller scale to ensure entire render fits in window
    float scale_x = static_cast<float>(window_width_) / render_width_;
    float scale_y = static_cast<float>(window_height_) / render_height_;
    return std::min(scale_x, scale_y);
}

float ResolutionManager::get_window_to_framebuffer_scale() const {
    if (window_width_ == 0 || window_height_ == 0) return 1.0f;
    
    // This is the DPI scale
    float scale_x = static_cast<float>(framebuffer_width_) / window_width_;
    float scale_y = static_cast<float>(framebuffer_height_) / window_height_;
    return std::max(scale_x, scale_y);  // Usually uniform, but take max for safety
}

void ResolutionManager::window_to_render(int win_x, int win_y, int& render_x, int& render_y) const {
    // Scale from window coordinates to render buffer coordinates
    float scale = static_cast<float>(render_width_) / window_width_;
    render_x = static_cast<int>(win_x * scale);
    render_y = static_cast<int>(win_y * scale);
}

void ResolutionManager::render_to_window(int render_x, int render_y, int& win_x, int& win_y) const {
    // Scale from render buffer coordinates to window coordinates
    float scale = static_cast<float>(window_width_) / render_width_;
    win_x = static_cast<int>(render_x * scale);
    win_y = static_cast<int>(render_y * scale);
}

void ResolutionManager::window_to_framebuffer(int win_x, int win_y, int& fb_x, int& fb_y) const {
    // Scale from window coordinates to framebuffer coordinates (DPI scaling)
    float scale = get_window_to_framebuffer_scale();
    fb_x = static_cast<int>(win_x * scale);
    fb_y = static_cast<int>(win_y * scale);
}

void ResolutionManager::framebuffer_to_window(int fb_x, int fb_y, int& win_x, int& win_y) const {
    // Scale from framebuffer coordinates to window coordinates
    float scale = get_window_to_framebuffer_scale();
    if (scale > 0) {
        win_x = static_cast<int>(fb_x / scale);
        win_y = static_cast<int>(fb_y / scale);
    } else {
        win_x = fb_x;
        win_y = fb_y;
    }
}

void ResolutionManager::scale_render_to_window(const PixelBuffer& render_buffer, 
                                               PixelBuffer& window_buffer) const {
    // Simple nearest-neighbor scaling for now
    // TODO: Implement bilinear filtering for better quality
    
    float scale_x = static_cast<float>(render_width_) / window_width_;
    float scale_y = static_cast<float>(render_height_) / window_height_;
    
    for (int y = 0; y < window_height_; y++) {
        for (int x = 0; x < window_width_; x++) {
            // Find corresponding pixel in render buffer
            int src_x = static_cast<int>(x * scale_x);
            int src_y = static_cast<int>(y * scale_y);
            
            // Clamp to render buffer bounds
            src_x = std::min(src_x, render_width_ - 1);
            src_y = std::min(src_y, render_height_ - 1);
            
            // Copy pixel
            EnhancedPixel pixel = render_buffer.get_pixel(src_x, src_y);
            window_buffer.set_enhanced_pixel(x, y, pixel);
        }
    }
}

void ResolutionManager::scale_window_to_framebuffer(const PixelBuffer& window_buffer,
                                                    PixelBuffer& framebuffer) const {
    // Scale from window size to framebuffer size (for Retina displays)
    float scale = get_window_to_framebuffer_scale();
    
    for (int y = 0; y < framebuffer_height_; y++) {
        for (int x = 0; x < framebuffer_width_; x++) {
            // Find corresponding pixel in window buffer
            int src_x = static_cast<int>(x / scale);
            int src_y = static_cast<int>(y / scale);
            
            // Clamp to window buffer bounds
            src_x = std::min(src_x, window_width_ - 1);
            src_y = std::min(src_y, window_height_ - 1);
            
            // Copy pixel
            EnhancedPixel pixel = window_buffer.get_pixel(src_x, src_y);
            framebuffer.set_enhanced_pixel(x, y, pixel);
        }
    }
}

void ResolutionManager::composite_buffers(const PixelBuffer& render_buffer,
                                         const PixelBuffer& ui_buffer,
                                         PixelBuffer& output_buffer) const {
    // First, scale render buffer to output size
    scale_render_to_window(render_buffer, output_buffer);
    
    // Then overlay UI on top (UI is already at window resolution)
    for (int y = 0; y < window_height_; y++) {
        for (int x = 0; x < window_width_; x++) {
            EnhancedPixel ui_pixel = ui_buffer.get_pixel(x, y);
            
            // Only draw UI pixel if it has alpha (not transparent)
            if (ui_pixel.a > 0) {
                if (ui_pixel.a == 255) {
                    // Opaque - direct copy
                    output_buffer.set_enhanced_pixel(x, y, ui_pixel);
                } else {
                    // Alpha blend
                    output_buffer.set_pixel_with_object(x, y, ui_pixel.r, ui_pixel.g, ui_pixel.b, 
                                                       ui_pixel.a, ui_pixel.object_id);
                }
            }
        }
    }
}

bool ResolutionManager::can_increase_resolution() const {
    // Can increase if not already at framebuffer size
    return render_width_ < framebuffer_width_ || render_height_ < framebuffer_height_;
}

bool ResolutionManager::can_decrease_resolution() const {
    // Can decrease if larger than window size
    return render_width_ > window_width_ || render_height_ > window_height_;
}

void ResolutionManager::cycle_resolution_up() {
    // Simple linear cycling through ALL presets
    switch (current_preset_) {
        case ResolutionPreset::ULTRA_LOW:
            set_resolution_preset(ResolutionPreset::LOW);
            break;
        case ResolutionPreset::LOW:
            set_resolution_preset(ResolutionPreset::MEDIUM);
            break;
        case ResolutionPreset::MEDIUM:
            set_resolution_preset(ResolutionPreset::HIGH);
            break;
        case ResolutionPreset::HIGH:
            set_resolution_preset(ResolutionPreset::NATIVE);
            break;
        case ResolutionPreset::NATIVE:
            if (framebuffer_width_ > window_width_) {
                set_resolution_preset(ResolutionPreset::RETINA);
            }
            break;
        case ResolutionPreset::RETINA:
            // Already at highest
            break;
        case ResolutionPreset::CUSTOM:
            // From custom, go to next standard preset
            // Start from ULTRA_LOW to get back into the cycle
            set_resolution_preset(ResolutionPreset::LOW);
            break;
    }
}

void ResolutionManager::cycle_resolution_down() {
    // Simple linear cycling through ALL presets
    switch (current_preset_) {
        case ResolutionPreset::RETINA:
            set_resolution_preset(ResolutionPreset::NATIVE);
            break;
        case ResolutionPreset::NATIVE:
            set_resolution_preset(ResolutionPreset::HIGH);
            break;
        case ResolutionPreset::HIGH:
            set_resolution_preset(ResolutionPreset::MEDIUM);
            break;
        case ResolutionPreset::MEDIUM:
            set_resolution_preset(ResolutionPreset::LOW);
            break;
        case ResolutionPreset::LOW:
            set_resolution_preset(ResolutionPreset::ULTRA_LOW);
            break;
        case ResolutionPreset::ULTRA_LOW:
            // Already at lowest
            break;
        case ResolutionPreset::CUSTOM:
            // From custom, go to a standard preset
            // Start from HIGH to get back into the cycle
            set_resolution_preset(ResolutionPreset::HIGH);
            break;
    }
}

void ResolutionManager::get_available_presets(std::vector<ResolutionPreset>& presets) const {
    presets.clear();
    
    // Always available
    presets.push_back(ResolutionPreset::ULTRA_LOW);
    presets.push_back(ResolutionPreset::LOW);
    presets.push_back(ResolutionPreset::MEDIUM);
    presets.push_back(ResolutionPreset::HIGH);
    presets.push_back(ResolutionPreset::NATIVE);
    
    // Only add RETINA if we have a high-DPI display
    if (framebuffer_width_ > window_width_ || framebuffer_height_ > window_height_) {
        presets.push_back(ResolutionPreset::RETINA);
    }
    
    presets.push_back(ResolutionPreset::CUSTOM);
}

const char* ResolutionManager::get_preset_name(ResolutionPreset preset) const {
    for (const auto& def : preset_definitions_) {
        if (def.preset == preset) {
            return def.name;
        }
    }
    return "Unknown";
}

void ResolutionManager::update_current_preset() {
    // Check if current resolution matches any preset
    for (const auto& def : preset_definitions_) {
        if (def.preset == ResolutionPreset::NATIVE) {
            if (render_width_ == window_width_ && render_height_ == window_height_) {
                current_preset_ = ResolutionPreset::NATIVE;
                return;
            }
        } else if (def.preset == ResolutionPreset::RETINA) {
            if (render_width_ == framebuffer_width_ && render_height_ == framebuffer_height_) {
                current_preset_ = ResolutionPreset::RETINA;
                return;
            }
        } else if (def.width > 0 && def.height > 0) {
            if (render_width_ == def.width && render_height_ == def.height) {
                current_preset_ = def.preset;
                return;
            }
        }
    }
    
    // Doesn't match any preset
    current_preset_ = ResolutionPreset::CUSTOM;
}