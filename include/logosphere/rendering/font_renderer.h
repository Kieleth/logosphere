#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

#include <string>
#include <cstdint>

// Forward declarations
class PixelBuffer;

// Simple color structure for font rendering
struct FontColor {
    uint8_t r, g, b, a;
    
    FontColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}
};

// Rectangle for text measurement
struct TextRect {
    int x, y, width, height;
};

// EDUCATIONAL NOTE: Bitmap Font Rendering
//
// This class encapsulates a classic 5x7 pixel bitmap font system, similar to
// what was used in early arcade games and microcomputers. Each character is
// stored as 7 bytes, where each byte represents one row of 5 pixels.
//
// The font data is stored as static const data, making it ROM-like and
// ensuring zero runtime initialization cost. This is how fonts were stored
// in hardware character generators in systems like the Commodore 64.
//
// Key concepts:
// - Bitmap fonts: Each character is a small raster image
// - Fixed-width: All characters are the same width (monospace)
// - Direct pixel manipulation: No vector graphics or anti-aliasing
// - Memory efficient: Only 7 bytes per character

class FontRenderer {
public:
    // Constants
    static constexpr int CHAR_WIDTH = 5;
    static constexpr int CHAR_HEIGHT = 7;
    static constexpr int CHAR_SPACING = 1;
    static constexpr int LINE_SPACING = 2;
    
    // Constructor
    FontRenderer() = default;
    
    // Character rendering - basic building block
    void draw_char(uint8_t* buffer, int buffer_width, int buffer_height,
                   int x, int y, char c, 
                   const FontColor& color) const;
    
    // Character rendering with scaling
    void draw_char_scaled(uint8_t* buffer, int buffer_width, int buffer_height,
                         int x, int y, char c, 
                         const FontColor& color, float scale) const;
    
    // String rendering - builds on draw_char
    void draw_string(uint8_t* buffer, int buffer_width, int buffer_height,
                    int x, int y, const std::string& text, 
                    const FontColor& color) const;
    
    // String rendering with scaling
    void draw_string_scaled(uint8_t* buffer, int buffer_width, int buffer_height,
                           int x, int y, const std::string& text, 
                           const FontColor& color, float scale) const;
    
    // Number rendering convenience function
    void draw_number(uint8_t* buffer, int buffer_width, int buffer_height,
                    int x, int y, int number, 
                    const FontColor& color) const;
    
    // Text measurement - useful for UI layout
    TextRect measure_text(const std::string& text, float scale = 1.0f) const;
    int get_text_width(const std::string& text, float scale = 1.0f) const;
    int get_text_height(float scale = 1.0f) const;
    
    // Character metrics
    bool is_char_supported(char c) const;
    int get_char_index(char c) const;
    
private:
    // The actual font bitmap data - 90 characters, 7 rows each
    // Stored as static const for zero runtime cost
    static const uint8_t font_data_[90][CHAR_HEIGHT];

    // Helper to set a pixel in the buffer with bounds checking
    inline void set_pixel_safe(uint8_t* buffer, int buffer_width, int buffer_height,
                               int x, int y, const FontColor& color) const {
        if (x >= 0 && x < buffer_width && y >= 0 && y < buffer_height) {
            int index = (y * buffer_width + x) * 4;  // RGBA format
            buffer[index] = color.r;
            buffer[index + 1] = color.g;
            buffer[index + 2] = color.b;
            buffer[index + 3] = color.a;
        }
    }

public:
    // Public access to font data for temporary integration
    // TODO[RENDER-010]: Remove when PixelBuffer is extracted
    static const uint8_t* get_char_data(int char_index) {
        if (char_index >= 0 && char_index < 90) {
            return font_data_[char_index];
        }
        return font_data_[0]; // Return space for invalid indices
    }
};

#endif // FONT_RENDERER_H