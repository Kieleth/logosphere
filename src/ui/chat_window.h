#ifndef CHAT_WINDOW_H
#define CHAT_WINDOW_H

#include "widgets.h"
#include <string>
#include <vector>

// ChatWindow - Draggable chat interface for LLM interaction
//
// Inherits ui::Window widget to get:
// - Draggable title bar (for future Phase 3)
// - Focus management
// - Mouse event handling
// - Standard widget lifecycle
//
// Manages:
// - Message history with word wrapping
// - Text input field with blinking cursor
// - LLM "thinking" indicator with animation
// - Auto-scroll to newest messages
class ChatWindow : public ui::Window {
public:
    ChatWindow(const std::string& id = "chat");
    ~ChatWindow() override = default;

    // Widget lifecycle
    void update(float delta_time) override;
    void render(IDrawSurface* renderer) override;

    // Mouse event handling (Phase 3)
    bool on_mouse_down(ui::MouseEvent& e) override;
    bool on_mouse_leave(ui::MouseEvent& e) override;
    bool on_mouse_scroll(int delta) override;
    void on_focus_gained() override;
    void on_focus_lost() override;

    // Message management
    void add_message(const std::string& msg);
    void clear_history();
    size_t message_count() const { return messages_.size(); }
    const std::vector<std::string>& get_messages() const { return messages_; }

    // Input management
    std::string get_input_text() const { return input_buffer_; }
    void clear_input();
    bool has_pending_submit();  // Returns true once, then clears flag
    void handle_char_input(unsigned int codepoint);
    void handle_backspace();
    void handle_enter();

    // Focus management
    // Note: Focus now controlled by Widget system via on_focus_gained/lost()
    bool has_input_focus() const { return has_focus_; }

    // LLM state
    void set_thinking(bool thinking);

    // Theme: history/input text color and accent (focused border,
    // thinking indicator). Defaults preserve the original look.
    void set_text_color(uint8_t r, uint8_t g, uint8_t b) {
        text_r_ = r; text_g_ = g; text_b_ = b; invalidate();
    }
    void set_accent_color(uint8_t r, uint8_t g, uint8_t b) {
        accent_r_ = r; accent_g_ = g; accent_b_ = b; invalidate();
    }
    bool is_thinking() const { return llm_thinking_; }

    // Visual configuration (Phase 4)
    void set_background_alpha(uint8_t alpha) { background_alpha_ = alpha; invalidate(); }
    uint8_t get_background_alpha() const { return background_alpha_; }

protected:
    // Override to allow dragging from anywhere, not just title bar
    bool is_in_title_bar(int local_x, int local_y) const override;

private:
    // Message state
    std::vector<std::string> messages_;
    size_t max_messages_;

    // Input state
    std::string input_buffer_;
    size_t cursor_pos_;
    bool submit_pending_;
    float cursor_blink_time_;
    bool has_focus_;

    // LLM state
    bool llm_thinking_;
    float llm_thinking_time_;

    // Visual config (matching current implementation)
    int padding_;
    int line_height_;
    int input_field_height_;

    // Scrolling (Phase 3)
    int scroll_offset_;        // Lines scrolled up (0 = show newest)
    int max_scroll_offset_;    // Maximum scroll (total lines - visible)

    // Transparency (Phase 4)
    uint8_t background_alpha_; // Background opacity (0=transparent, 255=opaque)

    // Theme (games set these; defaults match the original look)
    uint8_t text_r_ = 255, text_g_ = 255, text_b_ = 255;
    uint8_t accent_r_ = 0, accent_g_ = 200, accent_b_ = 255;
};

#endif // CHAT_WINDOW_H
