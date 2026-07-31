#include "chat_window.h"
#include "ui_system.h"
#include "logosphere/rendering/i_draw_surface.h"
#include <algorithm>
#include <iostream>

ChatWindow::ChatWindow(const std::string& id)
    : Window(id)
    , max_messages_(20)
    , cursor_pos_(0)
    , submit_pending_(false)
    , cursor_blink_time_(0.0f)
    , has_focus_(false)  // Proper fix: Focus controlled by Widget system
    , llm_thinking_(false)
    , llm_thinking_time_(0.0f)
    , padding_(5)         // 15% larger text spacing
    , line_height_(14)    // 15% larger (was 12) - more spacing between lines
    , input_field_height_(20)  // 15% larger (was 17)
    , scroll_offset_(0)
    , max_scroll_offset_(0)
    , background_alpha_(204)  // Phase 4: 20% transparency (80% opaque)
{
    // Set window properties (Phase 3: enabled dragging)
    set_title("Chat");
    set_closeable(false);  // Don't show close button for now
    set_draggable(true);   // Enable dragging

    // Default size and position - 25% larger window, 20% taller for more messages
    set_size(1050, 324);
    set_position(50, 50);
}

void ChatWindow::update(float delta_time) {
    // DEBUG: Track update frequency (log every 60 frames)
    static int update_count = 0;
    static float total_time = 0.0f;
    update_count++;
    total_time += delta_time;

    if (update_count % 60 == 0) {
        std::cout << "[CHAT_UPDATE_DEBUG] Frame " << update_count
                  << " - has_focus_=" << has_focus_
                  << " llm_thinking_=" << llm_thinking_
                  << " cursor_blink_time_=" << cursor_blink_time_
                  << " avg_dt=" << (total_time / 60.0f) << "ms"
                  << std::endl;
        total_time = 0.0f;
    }

    // Update cursor blink animation (only if input has focus)
    if (has_focus_) {
        cursor_blink_time_ += delta_time;
    }

    // Update LLM thinking animation
    if (llm_thinking_) {
        llm_thinking_time_ += delta_time;
    }

    // Call parent update
    Window::update(delta_time);
}

void ChatWindow::render(IDrawSurface* renderer) {
    if (!renderer) return;

    // DEBUG: Track render frequency (log every 60 frames)
    static int render_count = 0;
    render_count++;

    if (render_count % 60 == 0) {
        std::cout << "[CHAT_RENDER_DEBUG] Frame " << render_count
                  << " - visible=" << is_visible()
                  << " messages=" << messages_.size()
                  << " input_len=" << input_buffer_.size()
                  << std::endl;
    }

    // Get bounds (widget system provides absolute position)
    ui::Rectangle bounds = get_absolute_bounds();
    int x = bounds.x;
    int y = bounds.y;
    int width = bounds.width;
    int height = bounds.height;

    // Draw panel background (dark with border for visibility)
    // Phase 4: Use configurable alpha for transparency
    renderer->fill_rect(x, y, width, height, 20, 20, 30, background_alpha_);

    // Draw border with focus-based color (Phase 3: visual feedback)
    if (has_focus_) {
        // Bright cyan - focused
        renderer->draw_rect(x, y, width, height, accent_r_, accent_g_, accent_b_, 255);
    } else {
        // Dim gray - unfocused
        renderer->draw_rect(x, y, width, height, 100, 100, 120, 255);
    }

    // Draw input field at bottom
    int input_y = y + height - padding_ - input_field_height_;
    // Phase 4: Input field uses same alpha as main background
    renderer->fill_rect(x + padding_, input_y, width - padding_ * 2, input_field_height_, 40, 40, 50, background_alpha_);
    renderer->draw_rect(x + padding_, input_y, width - padding_ * 2, input_field_height_, 120, 120, 140, 255);  // Border always opaque

    // Draw input text with cursor
    std::string display_text = "> " + input_buffer_;
    if (has_focus_) {
        // Show blinking cursor (cursor_blink_time updated in update() method)
        if (static_cast<int>(cursor_blink_time_ * 2.0f) % 2 == 0) {
            display_text += "_";
        }
    }
    renderer->draw_text(x + padding_ + 2, input_y + 2, display_text.c_str(), text_r_, text_g_, text_b_);

    // Draw "thinking..." indicator INSIDE input field on the right if LLM is generating
    if (llm_thinking_) {
        // Animated dots (cycles through ".", "..", "...")
        int dots = static_cast<int>(llm_thinking_time_ * 2.0f) % 3 + 1;
        std::string dots_str(dots, '.');
        std::string thinking_text = "[" + dots_str + "]";

        // Position on right side of input field
        renderer->draw_text(x + width - padding_ - 10, input_y + 2, thinking_text.c_str(), accent_r_, accent_g_, accent_b_);
    }

    // Draw messages above input field with word wrapping
    // Font: 5x7 bitmap with 1px spacing = 6 pixels per character
    int max_line_width = width - padding_ * 2;
    int char_width = 6;  // CHAR_WIDTH (5) + CHAR_SPACING (1) from FontRenderer
    int max_chars_per_line = max_line_width / char_width;

    // Calculate total lines needed for all messages (for scroll limits)
    int total_lines = 0;
    for (const auto& msg : messages_) {
        std::string temp_msg = msg;
        while (!temp_msg.empty()) {
            size_t break_point = std::min(temp_msg.length(), static_cast<size_t>(max_chars_per_line));
            if (break_point < temp_msg.length()) {
                size_t last_space = temp_msg.rfind(' ', break_point);
                if (last_space != std::string::npos && last_space > 0) {
                    break_point = last_space;
                }
            }
            total_lines++;
            temp_msg = temp_msg.substr(break_point);
            if (!temp_msg.empty() && temp_msg[0] == ' ') {
                temp_msg = temp_msg.substr(1);
            }
        }
    }

    // Calculate visible area and scroll limits
    int visible_height = input_y - y - padding_ * 2;
    int visible_lines = visible_height / line_height_;
    max_scroll_offset_ = std::max(0, total_lines - visible_lines);

    // Clamp scroll offset to valid range
    scroll_offset_ = std::max(0, std::min(scroll_offset_, max_scroll_offset_));

    // Adjust starting Y by scroll offset (scroll up = show older = increase offset = lower start Y)
    int current_y = input_y - padding_ - line_height_ - (scroll_offset_ * line_height_);

    // Render messages from newest to oldest (bottom to top)
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        std::string message = *it;

        // First pass: collect all wrapped lines for this message
        std::vector<std::string> wrapped_lines;
        std::string remaining = message;
        while (!remaining.empty()) {
            size_t break_point = std::min(remaining.length(), static_cast<size_t>(max_chars_per_line));

            // Try to break at last space before max width
            if (break_point < remaining.length()) {
                size_t last_space = remaining.rfind(' ', break_point);
                if (last_space != std::string::npos && last_space > 0) {
                    break_point = last_space;
                }
            }

            wrapped_lines.push_back(remaining.substr(0, break_point));
            remaining = remaining.substr(break_point);

            // Trim leading space from continuation
            if (!remaining.empty() && remaining[0] == ' ') {
                remaining = remaining.substr(1);
            }
        }

        // Second pass: render wrapped lines in reverse order (last line at bottom)
        // This ensures multi-line messages read top-to-bottom correctly
        for (auto line_it = wrapped_lines.rbegin(); line_it != wrapped_lines.rend(); ++line_it) {
            if (current_y >= y + padding_ && current_y < input_y - padding_) {
                renderer->draw_text(x + padding_, current_y, line_it->c_str(), text_r_, text_g_, text_b_);
            }
            current_y -= line_height_;

            // Stop if we've scrolled past the visible area
            if (current_y < y + padding_ - line_height_) {
                break;
            }
        }

        // Early exit if we've rendered all visible lines
        if (current_y < y + padding_ - line_height_) {
            break;
        }
    }
}

void ChatWindow::add_message(const std::string& msg) {
    messages_.push_back(msg);

    // Enforce max message limit
    while (messages_.size() > max_messages_) {
        messages_.erase(messages_.begin());
    }

    invalidate();  // Mark widget for redraw
}

void ChatWindow::clear_history() {
    messages_.clear();
    invalidate();
}

void ChatWindow::clear_input() {
    input_buffer_.clear();
    cursor_pos_ = 0;
    invalidate();
}

bool ChatWindow::has_pending_submit() {
    bool pending = submit_pending_;
    submit_pending_ = false;  // Clear flag after reading
    return pending;
}

void ChatWindow::handle_char_input(unsigned int codepoint) {
    if (!has_focus_) return;

    // Only handle printable ASCII for now (can extend to UTF-8 later)
    if (codepoint >= 32 && codepoint < 127) {
        input_buffer_ += static_cast<char>(codepoint);
        invalidate();
    }
}

void ChatWindow::handle_backspace() {
    if (!has_focus_) return;

    if (!input_buffer_.empty()) {
        input_buffer_.pop_back();
        invalidate();
    }
}

void ChatWindow::handle_enter() {
    if (!has_focus_) return;

    if (!input_buffer_.empty()) {
        submit_pending_ = true;
        invalidate();
    }
}

void ChatWindow::set_thinking(bool thinking) {
    // DEBUG: Track state changes
    static int thinking_change_count = 0;
    static bool last_thinking = false;

    if (llm_thinking_ != thinking) {
        thinking_change_count++;
        std::cout << "[CHAT_THINKING_DEBUG] State change #" << thinking_change_count
                  << ": " << (thinking ? "TRUE" : "FALSE")
                  << " (invalidating widget)"
                  << std::endl;

        llm_thinking_ = thinking;
        if (!thinking) {
            llm_thinking_time_ = 0.0f;  // Reset animation
        }
        invalidate();
    } else {
        // DEBUG: Track redundant calls
        static int redundant_count = 0;
        redundant_count++;
        if (redundant_count % 60 == 0) {
            std::cout << "[CHAT_THINKING_DEBUG] Redundant call #" << redundant_count
                      << " with same value: " << (thinking ? "TRUE" : "FALSE")
                      << std::endl;
        }
    }

    last_thinking = thinking;
}

// Phase 3: Mouse Event Handlers

bool ChatWindow::on_mouse_down(ui::MouseEvent& e) {
    // Request focus from UISystem when clicked
    auto* ui = get_ui_system();
    if (ui) {
        ui->set_focused_widget(this);
    }

    // Call parent to handle dragging (Window class manages drag state)
    // Note: Window only allows dragging from title bar, but we want anywhere
    // So we override the title bar check behavior
    return Window::on_mouse_down(e);
}

bool ChatWindow::on_mouse_leave(ui::MouseEvent& e) {
    // Clear focus when mouse leaves the chat window
    // This prevents typing when mouse is outside the chat
    auto* ui = get_ui_system();
    if (ui && has_focus_) {
        ui->set_focused_widget(nullptr);
        std::cout << "[CHAT_FOCUS] Mouse left - clearing focus" << std::endl;
    }
    return true;  // Event handled
}

bool ChatWindow::on_mouse_scroll(int delta) {
    // Natural scrolling: wheel up (positive) = scroll up = show older messages
    scroll_offset_ += delta;

    // Clamp to valid range [0, max_scroll_offset_]
    scroll_offset_ = std::max(0, std::min(scroll_offset_, max_scroll_offset_));

    invalidate();  // Request redraw
    return true;   // Event handled
}

void ChatWindow::on_focus_gained() {
    has_focus_ = true;
    invalidate();  // Redraw with focused border color
    std::cout << "[CHAT_FOCUS] Focus gained" << std::endl;
}

void ChatWindow::on_focus_lost() {
    has_focus_ = false;
    invalidate();  // Redraw with unfocused border color
    std::cout << "[CHAT_FOCUS] Focus lost" << std::endl;
}

// Allow dragging from anywhere in the window, not just title bar
bool ChatWindow::is_in_title_bar(int local_x, int local_y) const {
    // Always return true - any click in the window can drag it
    return local_x >= 0 && local_x < get_width() &&
           local_y >= 0 && local_y < get_height();
}
