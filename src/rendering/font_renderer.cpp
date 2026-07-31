#include "logosphere/rendering/font_renderer.h"
#include <string>

// EDUCATIONAL NOTE: Font Data Storage
//
// This font data was originally embedded in render_system.mm, making that file
// unnecessarily large and mixing concerns. By extracting it here, we achieve:
// 1. Single Responsibility - FontRenderer only handles text rendering
// 2. Reusability - Other systems can use this font renderer
// 3. Maintainability - Font changes don't require touching render system
//
// The font covers:
// - Digits 0-9
// - Uppercase letters A-Z  
// - Lowercase letters a-z
// - Common punctuation and symbols

// Static font data definition - 5x7 pixel bitmap font
const uint8_t FontRenderer::font_data_[90][CHAR_HEIGHT] = {
    // Space (ASCII 32)
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000},
    
    // Digits 0-9 (ASCII 48-57)
    // '0'
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
    // '1'
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    // '2'
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
    // '3'
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110},
    // '4'
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
    // '5'
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
    // '6'
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
    // '7'
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
    // '8'
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
    // '9'
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},
    
    // Essential punctuation
    // ':' (ASCII 58)
    {0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000},
    // '.' (ASCII 46)
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100},
    // '%' (ASCII 37)
    {0b11000, 0b11001, 0b00010, 0b00100, 0b01000, 0b10011, 0b00011},
    
    // Letters F, P, S, M (commonly used in "FPS", "MS")
    // 'F' (ASCII 70)
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000},
    // 'P' (ASCII 80)
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000},
    // 'S' (ASCII 83)
    {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110},
    // 'M' (ASCII 77)
    {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001},
    
    // Uppercase letters A-Z (excluding F,M,P,S already defined)
    // 'A' (ASCII 65)
    {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},
    // 'B'
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110},
    // 'C'
    {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110},
    // 'D'
    {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110},
    // 'E'
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111},
    // 'G'
    {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110},
    // 'H'
    {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},
    // 'I'
    {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    // 'J'
    {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100},
    // 'K'
    {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001},
    // 'L'
    {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111},
    // 'N'
    {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001},
    // 'O'
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
    // 'Q'
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101},
    // 'R'
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001},
    // 'T'
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},
    // 'U'
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
    // 'V'
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100},
    // 'W'
    {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001},
    // 'X'
    {0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b01010, 0b10001},
    // 'Y'
    {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100},
    // 'Z'
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111},
    
    // Additional punctuation
    // ',' (comma)
    {0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b00100, 0b01000},
    // '='
    {0b00000, 0b00000, 0b11111, 0b00000, 0b11111, 0b00000, 0b00000},
    // '('
    {0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010},
    // ')'
    {0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000},
    // '+'
    {0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000},
    // '-'
    {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000},
    // '['
    {0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110},
    // ']'
    {0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110},
    // '>'
    {0b10000, 0b01000, 0b00100, 0b00010, 0b00100, 0b01000, 0b10000},
    
    // Lowercase letters a-z
    // 'a'
    {0b00000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111},
    // 'b'
    {0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b11110},
    // 'c'
    {0b00000, 0b00000, 0b01110, 0b10000, 0b10000, 0b10001, 0b01110},
    // 'd'
    {0b00001, 0b00001, 0b01101, 0b10011, 0b10001, 0b10001, 0b01111},
    // 'e'
    {0b00000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110},
    // 'f'
    {0b00110, 0b01001, 0b01000, 0b11100, 0b01000, 0b01000, 0b01000},
    // 'g'
    {0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110},
    // 'h'
    {0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001},
    // 'i'
    {0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110},
    // 'j'
    {0b00010, 0b00000, 0b00110, 0b00010, 0b00010, 0b10010, 0b01100},
    // 'k'
    {0b10000, 0b10000, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010},
    // 'l'
    {0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    // 'm'
    {0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10001, 0b10001},
    // 'n'
    {0b00000, 0b00000, 0b10110, 0b11001, 0b10001, 0b10001, 0b10001},
    // 'o'
    {0b00000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110},
    // 'p'
    {0b00000, 0b00000, 0b11110, 0b10001, 0b11110, 0b10000, 0b10000},
    // 'q'
    {0b00000, 0b00000, 0b01101, 0b10011, 0b01111, 0b00001, 0b00001},
    // 'r'
    {0b00000, 0b00000, 0b10110, 0b11001, 0b10000, 0b10000, 0b10000},
    // 's'
    {0b00000, 0b00000, 0b01110, 0b10000, 0b01110, 0b00001, 0b11110},
    // 't'
    {0b01000, 0b01000, 0b11100, 0b01000, 0b01000, 0b01001, 0b00110},
    // 'u'
    {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10011, 0b01101},
    // 'v'
    {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100},
    // 'w'
    {0b00000, 0b00000, 0b10001, 0b10001, 0b10101, 0b10101, 0b01010},
    // 'x'
    {0b00000, 0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001},
    // 'y'
    {0b00000, 0b00000, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110},
    // 'z'
    {0b00000, 0b00000, 0b11111, 0b00010, 0b00100, 0b01000, 0b11111},

    // Additional punctuation and symbols
    // '_' (underscore, ASCII 95)
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111},
    // '*' (asterisk, ASCII 42)
    {0b00000, 0b10101, 0b01110, 0b11111, 0b01110, 0b10101, 0b00000},
    // '?' (question mark, ASCII 63)
    {0b01110, 0b10001, 0b00001, 0b00110, 0b00100, 0b00000, 0b00100},
    // '!' (exclamation mark, ASCII 33)
    {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100},
    // '\'' (single quote, ASCII 39)
    {0b01100, 0b00100, 0b01000, 0b00000, 0b00000, 0b00000, 0b00000},
    // '"' (double quote, ASCII 34)
    {0b01010, 0b01010, 0b10100, 0b00000, 0b00000, 0b00000, 0b00000},
    // '/' (slash, ASCII 47)
    {0b00001, 0b00010, 0b00010, 0b00100, 0b01000, 0b01000, 0b10000},
    // '\' (backslash, ASCII 92)
    {0b10000, 0b01000, 0b01000, 0b00100, 0b00010, 0b00010, 0b00001},
    // '<' (less than, ASCII 60)
    {0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100, 0b00010},
    // ';' (semicolon, ASCII 59)
    {0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b00100, 0b01000},
    // '@' (at sign, ASCII 64)
    {0b01110, 0b10001, 0b10101, 0b10111, 0b10000, 0b10001, 0b01110},
    // '#' (hash, ASCII 35)
    {0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b01010},
    // '$' (dollar, ASCII 36)
    {0b00100, 0b01111, 0b10100, 0b01110, 0b00101, 0b11110, 0b00100},
    // '&' (ampersand, ASCII 38)
    {0b01100, 0b10010, 0b10100, 0b01000, 0b10101, 0b10010, 0b01101},
    // '|' (pipe, ASCII 124)
    {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},
};

int FontRenderer::get_char_index(char c) const {
    // Map character to font array index
    if (c == ' ') return 0;        // Space
    if (c >= '0' && c <= '9') return 1 + (c - '0');  // Digits 0-9
    if (c == ':') return 11;       // Colon
    if (c == '.') return 12;       // Period
    if (c == '%') return 13;       // Percent
    
    // Letters
    if (c == 'F') return 14;
    if (c == 'P') return 15;
    if (c == 'S') return 16;
    if (c == 'M') return 17;
    
    // More letters A-Z (excluding F,M,P,S)
    if (c >= 'A' && c <= 'E') return 18 + (c - 'A');      // A-E
    if (c == 'G') return 23;
    if (c >= 'H' && c <= 'L') return 24 + (c - 'H');      // H-L (excluding M)
    if (c == 'N' || c == 'O') return 29 + (c - 'N');     // N-O (excluding P)
    if (c == 'Q' || c == 'R') return 31 + (c - 'Q');     // Q-R (excluding S)
    if (c >= 'T' && c <= 'Z') return 33 + (c - 'T');      // T-Z
    
    // Punctuation
    if (c == ',') return 40;
    if (c == '=') return 41;
    if (c == '(') return 42;
    if (c == ')') return 43;
    if (c == '+') return 44;
    if (c == '-') return 45;
    if (c == '[') return 46;
    if (c == ']') return 47;
    if (c == '>') return 48;
    
    // Lowercase letters
    if (c >= 'a' && c <= 'z') return 49 + (c - 'a');

    // Additional punctuation and symbols
    if (c == '_') return 75;       // Underscore
    if (c == '*') return 76;       // Asterisk
    if (c == '?') return 77;       // Question mark
    if (c == '!') return 78;       // Exclamation mark
    if (c == '\'') return 79;      // Single quote
    if (c == '"') return 80;       // Double quote
    if (c == '/') return 81;       // Slash
    if (c == '\\') return 82;      // Backslash
    if (c == '<') return 83;       // Less than
    if (c == ';') return 84;       // Semicolon
    if (c == '@') return 85;       // At sign
    if (c == '#') return 86;       // Hash
    if (c == '$') return 87;       // Dollar
    if (c == '&') return 88;       // Ampersand
    if (c == '|') return 89;       // Pipe

    // Unknown character - return space
    return 0;
}

bool FontRenderer::is_char_supported(char c) const {
    // Quick check if character is in our font
    return (c == ' ') ||
           (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c == ':') || (c == '.') || (c == '%') ||
           (c == ',') || (c == '=') || (c == '(') || (c == ')') ||
           (c == '+') || (c == '-') || (c == '[') || (c == ']') || (c == '>') ||
           (c == '_') || (c == '*') || (c == '?') || (c == '!') ||
           (c == '\'') || (c == '"') || (c == '/') || (c == '\\') ||
           (c == '<') || (c == ';') || (c == '@') || (c == '#') ||
           (c == '$') || (c == '&') || (c == '|');
}

void FontRenderer::draw_char(uint8_t* buffer, int buffer_width, int buffer_height,
                             int x, int y, char c, const FontColor& color) const {
    // Get the font data for this character
    int char_index = get_char_index(c);
    const uint8_t* char_data = font_data_[char_index];
    
    // Draw each row of the character
    for (int row = 0; row < CHAR_HEIGHT; row++) {
        uint8_t row_data = char_data[row];
        
        // Check each bit in the row (5 bits, starting from MSB)
        for (int col = 0; col < CHAR_WIDTH; col++) {
            // Check if this pixel should be drawn (bit is set)
            if (row_data & (0b10000 >> col)) {  // Start from MSB and shift right
                set_pixel_safe(buffer, buffer_width, buffer_height,
                              x + col, y + row, color);
            }
        }
    }
}

void FontRenderer::draw_char_scaled(uint8_t* buffer, int buffer_width, int buffer_height,
                                    int x, int y, char c, const FontColor& color, float scale) const {
    // Get the font data for this character
    int char_index = get_char_index(c);
    const uint8_t* char_data = font_data_[char_index];
    
    int scale_int = static_cast<int>(scale);
    if (scale_int < 1) scale_int = 1;  // Minimum scale of 1
    
    // Draw each row of the character, scaled
    for (int row = 0; row < CHAR_HEIGHT; row++) {
        uint8_t row_data = char_data[row];
        
        // Check each bit in the row (5 bits, starting from MSB)
        for (int col = 0; col < CHAR_WIDTH; col++) {
            // Check if this pixel should be drawn (bit is set)
            if (row_data & (0b10000 >> col)) {  // Start from MSB and shift right
                // Draw a scale_int x scale_int block for this pixel
                for (int dy = 0; dy < scale_int; dy++) {
                    for (int dx = 0; dx < scale_int; dx++) {
                        set_pixel_safe(buffer, buffer_width, buffer_height,
                                      x + col * scale_int + dx, 
                                      y + row * scale_int + dy, 
                                      color);
                    }
                }
            }
        }
    }
}

void FontRenderer::draw_string(uint8_t* buffer, int buffer_width, int buffer_height,
                               int x, int y, const std::string& text, const FontColor& color) const {
    int current_x = x;
    
    for (char c : text) {
        draw_char(buffer, buffer_width, buffer_height, current_x, y, c, color);
        current_x += CHAR_WIDTH + CHAR_SPACING;
    }
}

void FontRenderer::draw_string_scaled(uint8_t* buffer, int buffer_width, int buffer_height,
                                      int x, int y, const std::string& text, 
                                      const FontColor& color, float scale) const {
    int scale_int = static_cast<int>(scale);
    if (scale_int < 1) scale_int = 1;
    
    int scaled_char_width = CHAR_WIDTH * scale_int;
    int scaled_spacing = CHAR_SPACING * scale_int;
    
    int current_x = x;
    for (char c : text) {
        draw_char_scaled(buffer, buffer_width, buffer_height, current_x, y, c, color, scale);
        current_x += scaled_char_width + scaled_spacing;
    }
}

void FontRenderer::draw_number(uint8_t* buffer, int buffer_width, int buffer_height,
                               int x, int y, int number, const FontColor& color) const {
    std::string number_str = std::to_string(number);
    draw_string(buffer, buffer_width, buffer_height, x, y, number_str, color);
}

TextRect FontRenderer::measure_text(const std::string& text, float scale) const {
    int scale_int = static_cast<int>(scale);
    if (scale_int < 1) scale_int = 1;
    
    int width = get_text_width(text, scale);
    int height = get_text_height(scale);
    
    return TextRect{0, 0, width, height};
}

int FontRenderer::get_text_width(const std::string& text, float scale) const {
    if (text.empty()) return 0;
    
    int scale_int = static_cast<int>(scale);
    if (scale_int < 1) scale_int = 1;
    
    int scaled_char_width = CHAR_WIDTH * scale_int;
    int scaled_spacing = CHAR_SPACING * scale_int;
    
    // Width is: (number of chars * char width) + (number of gaps * spacing)
    // Number of gaps = number of chars - 1
    return text.length() * scaled_char_width + (text.length() - 1) * scaled_spacing;
}

int FontRenderer::get_text_height(float scale) const {
    int scale_int = static_cast<int>(scale);
    if (scale_int < 1) scale_int = 1;
    
    return CHAR_HEIGHT * scale_int;
}