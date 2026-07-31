#include "widgets.h"
#include "ui_system.h"
#include "logosphere/rendering/i_draw_surface.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ui {

// Label implementation

Label::Label(const std::string& text, const std::string& id) 
    : Widget(id), text_(text) {
    // Labels don't need focus by default
    set_can_focus(false);
}

void Label::render(IDrawSurface* renderer) {
    if (!is_visible()) return;
    
    Rectangle bounds = get_absolute_bounds();
    
    // Get UI scale from UISystem if available
    float ui_scale = 1.0f;
    if (get_ui_system()) {
        ui_scale = get_ui_system()->get_ui_scale_multiplier();
    }
    
    // Apply UI scale to position
    int scaled_x = static_cast<int>(bounds.x * ui_scale);
    int scaled_y = static_cast<int>(bounds.y * ui_scale);
    
    // Draw text with scaled rendering
    if (ui_scale > 1.0f) {
        renderer->draw_string_scaled(scaled_x, scaled_y, text_, r_, g_, b_, ui_scale);
    } else {
        renderer->draw_text(scaled_x, scaled_y, text_.c_str(), r_, g_, b_);
    }
    
    // No need to render children for a label
}

void Label::set_text(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        invalidate();
    }
}

void Label::set_color(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r;
    g_ = g;
    b_ = b;
    invalidate();
}

void Label::get_color(uint8_t& r, uint8_t& g, uint8_t& b) const {
    r = r_;
    g = g_;
    b = b_;
}

// Button implementation

Button::Button(const std::string& text, const std::string& id)
    : Widget(id), text_(text) {
    set_can_focus(true);
    set_size(100, 30);  // Default size
}

void Button::render(IDrawSurface* renderer) {
    if (!is_visible()) return;
    
    Rectangle bounds = get_absolute_bounds();
    
    // Choose color based on state
    ButtonColors color = normal_color_;
    if (is_pressed()) {
        color = pressed_color_;
    } else if (is_hovered()) {
        color = hover_color_;
    }
    
    // Draw button background
    renderer->fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                       color.r, color.g, color.b, 255);
    
    // Draw border
    renderer->draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                       200, 200, 200, 255);
    
    // Draw text centered
    int text_width = static_cast<int>(text_.length() * 8);  // Approximate
    int text_x = bounds.x + (bounds.width - text_width) / 2;
    int text_y = bounds.y + (bounds.height - 16) / 2;  // 16 = approx font height
    renderer->draw_text(text_x, text_y, text_.c_str(), 255, 255, 255);
}

bool Button::on_mouse_enter(MouseEvent& e) {
    set_hovered(true);
    invalidate();
    return true;
}

bool Button::on_mouse_leave(MouseEvent& e) {
    set_hovered(false);
    set_pressed(false);
    invalidate();
    return true;
}

bool Button::on_mouse_down(MouseEvent& e) {
    if (e.button == 0) {  // Left button
        set_pressed(true);
        invalidate();
        return true;
    }
    return false;
}

bool Button::on_mouse_up(MouseEvent& e) {
    if (e.button == 0 && is_pressed()) {
        set_pressed(false);
        invalidate();
        
        // If still hovered, trigger click
        if (is_hovered()) {
            return on_mouse_click(e);
        }
        return true;
    }
    return false;
}

bool Button::on_mouse_click(MouseEvent& e) {
    if (on_click) {
        on_click();
    }
    return true;
}

void Button::set_text(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        invalidate();
    }
}

// Separator implementation

Separator::Separator(const std::string& id) : Widget(id) {
    set_can_focus(false);
    set_size(100, 1);  // Default horizontal separator
}

void Separator::render(IDrawSurface* renderer) {
    if (!is_visible()) return;
    
    Rectangle bounds = get_absolute_bounds();
    
    if (orientation_ == Horizontal) {
        // Draw horizontal line
        renderer->draw_line(bounds.x, bounds.y + bounds.height / 2,
                           bounds.x + bounds.width, bounds.y + bounds.height / 2,
                           r_, g_, b_, 255);
    } else {
        // Draw vertical line
        renderer->draw_line(bounds.x + bounds.width / 2, bounds.y,
                           bounds.x + bounds.width / 2, bounds.y + bounds.height,
                           r_, g_, b_, 255);
    }
}

void Separator::set_color(uint8_t r, uint8_t g, uint8_t b) {
    r_ = r;
    g_ = g;
    b_ = b;
    invalidate();
}

// Window implementation

Window::Window(const std::string& title, const std::string& id)
    : Container(id), title_(title) {
    set_can_focus(true);
    set_size(300, 200);  // Default size
}

void Window::render(IDrawSurface* renderer) {
    if (!is_visible()) return;

    Rectangle bounds = get_absolute_bounds();

    // DEBUG: Trace Window rendering with draw details
    static int window_frame = 0;
    if (++window_frame % 60 == 0) {
        std::cout << "[Window] DRAW id=" << get_id()
                  << " bounds=(" << bounds.x << "," << bounds.y
                  << "," << bounds.width << "," << bounds.height
                  << ") title_bar_h=" << title_bar_height_
                  << " visible=" << is_visible()
                  << " children=" << get_children().size() << std::endl;
        std::cout << "[Window]   fill_rect: (" << bounds.x << "," << (bounds.y + title_bar_height_)
                  << "," << bounds.width << "," << (bounds.height - title_bar_height_)
                  << ") rgba(30,30,40,240)" << std::endl;
    }
    
    // Draw window background
    renderer->fill_rect(bounds.x, bounds.y + title_bar_height_,
                       bounds.width, bounds.height - title_bar_height_,
                       30, 30, 40, 240);  // Semi-transparent dark background
    
    // Draw title bar
    uint8_t title_r = 50, title_g = 50, title_b = 70;
    if (is_focused()) {
        title_r = 60; title_g = 60; title_b = 90;
    }
    renderer->fill_rect(bounds.x, bounds.y, bounds.width, title_bar_height_,
                       title_r, title_g, title_b, 255);
    
    // Draw window border
    renderer->draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                       100, 100, 120, 255);
    
    // Draw title text
    renderer->draw_text(bounds.x + 10, bounds.y + 5, title_.c_str(), 255, 255, 255);
    
    // Draw close button if closeable
    if (closeable_) {
        int close_x = bounds.x + bounds.width - 20;
        int close_y = bounds.y + 5;
        renderer->draw_text(close_x, close_y, "X", 255, 100, 100);
    }
    
    // Render children (they will be clipped to content area)
    render_children(renderer);
}

bool Window::on_mouse_down(MouseEvent& e) {
    Rectangle bounds = get_absolute_bounds();
    
    // Convert to local coordinates
    int local_x = e.x - bounds.x;
    int local_y = e.y - bounds.y;
    
    // Check if clicking close button
    if (closeable_ && local_x >= bounds.width - 25 && local_x < bounds.width - 5 &&
        local_y >= 0 && local_y < title_bar_height_) {
        if (on_close) {
            on_close();
        }
        set_visible(false);
        return true;
    }
    
    // Check if clicking title bar for dragging
    if (draggable_ && is_in_title_bar(local_x, local_y)) {
        dragging_ = true;
        drag_offset_x_ = local_x;
        drag_offset_y_ = local_y;
        return true;
    }
    
    return false;
}

bool Window::on_mouse_up(MouseEvent& e) {
    if (dragging_) {
        dragging_ = false;
        return true;
    }
    return false;
}

bool Window::on_mouse_move(MouseEvent& e) {
    if (dragging_) {
        // Calculate new position
        int new_x = e.x - drag_offset_x_;
        int new_y = e.y - drag_offset_y_;
        set_position(new_x, new_y);
        return true;
    }
    return false;
}

void Window::set_title(const std::string& title) {
    if (title_ != title) {
        title_ = title;
        invalidate();
    }
}

bool Window::is_in_title_bar(int local_x, int local_y) const {
    return local_x >= 0 && local_x < get_width() &&
           local_y >= 0 && local_y < title_bar_height_;
}

// Panel implementation

Panel::Panel(const std::string& id) : Container(id) {
    set_can_focus(false);
}

void Panel::render(IDrawSurface* renderer) {
    if (!is_visible()) return;
    
    Rectangle bounds = get_absolute_bounds();
    
    // Draw background
    renderer->fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                       bg_r_, bg_g_, bg_b_, bg_a_);
    
    // Draw border if enabled
    if (has_border_) {
        renderer->draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                           border_r_, border_g_, border_b_, 255);
    }
    
    // Render children
    render_children(renderer);
}

void Panel::set_background_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    bg_r_ = r;
    bg_g_ = g;
    bg_b_ = b;
    bg_a_ = a;
    invalidate();
}

void Panel::set_border(bool enabled, uint8_t r, uint8_t g, uint8_t b) {
    has_border_ = enabled;
    border_r_ = r;
    border_g_ = g;
    border_b_ = b;
    invalidate();
}

// ListMenu implementation

ListMenu::ListMenu(const std::string& id) : Container(id) {
    set_can_focus(true);
    set_size(200, 300);  // Default size
}

void ListMenu::render(IDrawSurface* renderer) {
    if (!is_visible()) return;

    Rectangle bounds = get_absolute_bounds();

    // Get UI scale from UISystem if available
    float ui_scale = 1.0f;
    if (get_ui_system()) {
        ui_scale = get_ui_system()->get_ui_scale_multiplier();
    }

    // Apply UI scale to all dimensions
    int scaled_x = static_cast<int>(bounds.x * ui_scale);
    int scaled_y = static_cast<int>(bounds.y * ui_scale);
    int scaled_width = static_cast<int>(bounds.width * ui_scale);
    int scaled_height = static_cast<int>(bounds.height * ui_scale);
    int scaled_padding = static_cast<int>(padding_ * ui_scale);
    int scaled_item_height = static_cast<int>(item_height_ * ui_scale);

    // DEBUG: Trace ACTUAL drawing coordinates - EVERY FRAME for first 10 frames
    static int frame_counter = 0;
    frame_counter++;
    if (frame_counter <= 10 || frame_counter % 60 == 0) {
        std::cout << "[ListMenu] RENDER frame=" << frame_counter
                  << " id=" << get_id()
                  << " items_.size()=" << items_.size()
                  << " visible=" << is_visible()
                  << " scaled=(" << scaled_x << "," << scaled_y
                  << "," << scaled_width << "," << scaled_height << ")"
                  << std::endl;
        for (size_t i = 0; i < items_.size() && i < 5; ++i) {
            std::cout << "[ListMenu]   item[" << i << "]: \"" << items_[i].text << "\"" << std::endl;
        }
    }

    // Draw background
    renderer->fill_rect(scaled_x, scaled_y, scaled_width, scaled_height,
                       bg_r_, bg_g_, bg_b_, 255);
    
    // Draw border
    renderer->draw_rect(scaled_x, scaled_y, scaled_width, scaled_height,
                       100, 100, 120, 255);
    
    // Draw items
    int y = scaled_y + scaled_padding;
    for (size_t i = 0; i < items_.size(); ++i) {
        // Draw selection/hover background
        if (static_cast<int>(i) == selected_index_) {
            renderer->fill_rect(scaled_x + 2, y, scaled_width - 4, scaled_item_height,
                               selected_r_, selected_g_, selected_b_, 255);
        } else if (static_cast<int>(i) == hovered_index_) {
            renderer->fill_rect(scaled_x + 2, y, scaled_width - 4, scaled_item_height,
                               hover_r_, hover_g_, hover_b_, 255);
        }

        // DEBUG: Log EVERY text draw for first few frames
        int text_x = scaled_x + scaled_padding + 5;
        int text_y = y + 5;
        if (frame_counter <= 3) {
            std::cout << "[ListMenu] DRAW_TEXT[" << i << "] at (" << text_x << "," << text_y
                      << ") color=(" << (int)text_r_ << "," << (int)text_g_ << "," << (int)text_b_
                      << ") ui_scale=" << ui_scale
                      << " text=\"" << items_[i].text.substr(0, 30) << "\"" << std::endl;
        }

        // Draw item text with scaled rendering
        if (ui_scale > 1.0f) {
            renderer->draw_string_scaled(text_x, text_y, items_[i].text,
                                        text_r_, text_g_, text_b_, ui_scale);
        } else {
            renderer->draw_text(text_x, text_y, items_[i].text.c_str(),
                               text_r_, text_g_, text_b_);
        }

        y += scaled_item_height;
    }
    
    // Don't render children for list menu
}

void ListMenu::update(float delta_time) {
    Container::update(delta_time);
}

bool ListMenu::on_key_down(KeyEvent& e) {
    switch (e.key) {
        case 265:  // Up arrow (GLFW_KEY_UP)
            select_previous();
            return true;
            
        case 264:  // Down arrow (GLFW_KEY_DOWN)
            select_next();
            return true;
            
        case 257:  // Enter (GLFW_KEY_ENTER)
            activate_selected();
            return true;
    }
    return false;
}

bool ListMenu::on_mouse_down(MouseEvent& e) {
    if (e.button != 0) return false;  // Only handle left click
    
    Rectangle bounds = get_absolute_bounds();
    int local_y = e.y - bounds.y;
    
    int index = get_item_at_position(local_y);
    if (index >= 0) {
        select_item(index);
        
        // Double-click detection would go here
        // For now, single click activates
        activate_selected();
        return true;
    }
    
    return false;
}

bool ListMenu::on_mouse_move(MouseEvent& e) {
    Rectangle bounds = get_absolute_bounds();
    int local_y = e.y - bounds.y;
    
    int index = get_item_at_position(local_y);
    if (index != hovered_index_) {
        hovered_index_ = index;
        invalidate();
    }
    
    return false;
}

void ListMenu::add_item(const std::string& text, const std::string& data) {
    std::cout << "[ListMenu] ADD_ITEM id=" << get_id() << " text=\"" << text << "\" items_before=" << items_.size() << std::endl;
    items_.push_back({text, data.empty() ? text : data});
    std::cout << "[ListMenu] ADD_ITEM items_after=" << items_.size() << std::endl;
    invalidate();
}

void ListMenu::remove_item(int index) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        items_.erase(items_.begin() + index);
        
        // Adjust selection
        if (selected_index_ >= static_cast<int>(items_.size())) {
            selected_index_ = static_cast<int>(items_.size()) - 1;
        }
        
        invalidate();
    }
}

void ListMenu::clear_items() {
    items_.clear();
    selected_index_ = -1;
    hovered_index_ = -1;
    invalidate();
}

void ListMenu::select_item(int index) {
    if (index >= -1 && index < static_cast<int>(items_.size())) {
        int old_index = selected_index_;
        selected_index_ = index;
        
        if (old_index != selected_index_) {
            invalidate();
            if (on_selection_changed) {
                on_selection_changed(selected_index_);
            }
        }
    }
}

void ListMenu::select_next() {
    if (items_.empty()) return;
    
    int new_index = selected_index_ + 1;
    if (new_index >= static_cast<int>(items_.size())) {
        new_index = 0;  // Wrap around
    }
    select_item(new_index);
}

void ListMenu::select_previous() {
    if (items_.empty()) return;
    
    int new_index = selected_index_ - 1;
    if (new_index < 0) {
        new_index = static_cast<int>(items_.size()) - 1;  // Wrap around
    }
    select_item(new_index);
}

std::string ListMenu::get_selected_data() const {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(items_.size())) {
        return items_[selected_index_].data;
    }
    return "";
}

void ListMenu::activate_selected() {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(items_.size())) {
        if (on_item_activated) {
            on_item_activated(items_[selected_index_].data);
        }
    }
}

int ListMenu::get_item_at_position(int local_y) const {
    if (local_y < padding_) return -1;
    
    int index = (local_y - padding_) / item_height_;
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        return index;
    }
    
    return -1;
}

// CompassWidget implementation

CompassWidget::CompassWidget(const std::string& id)
    : Window("", id) {
    set_size(compass_size_ + 20, compass_size_ + 35);  // +20 for padding, +35 for title bar
    set_draggable(true);
    set_closeable(false);
    set_visible(false);  // Hidden by default, toggle with hotkey
}

bool CompassWidget::is_in_title_bar(int local_x, int local_y) const {
    // Entire widget is draggable (no title bar)
    (void)local_x;
    (void)local_y;
    return true;
}

void CompassWidget::render(IDrawSurface* renderer) {
    if (!is_visible()) return;

    Rectangle bounds = get_absolute_bounds();

    static int frame = 0;
    if (frame++ % 60 == 0) {
        std::cout << "[CompassWidget] render() called - bounds: x=" << bounds.x
                  << " y=" << bounds.y << " w=" << bounds.width << " h=" << bounds.height << std::endl;
    }

    // Draw bright background so compass is visible
    renderer->fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                       40, 40, 60, 255);  // Solid dark blue-gray

    // Draw bright border
    renderer->draw_rect(bounds.x, bounds.y, bounds.width, bounds.height,
                       200, 200, 220, 255);  // Bright border

    // Center of compass
    int cx = bounds.x + bounds.width / 2;
    int cy = bounds.y + bounds.height / 2;
    int radius = compass_size_ / 2 - 5;

    // Isometric projection for compass directions (from docs/ARCHITECTURE.md):
    // World: X=East/West, Y=North/South, Z=Up/Down
    // iso_x = (world_x - world_y) * cos(30°) = (x - y) * 0.866
    // iso_y = (world_x + world_y) * 0.5 (screen Y is inverted, so we negate)
    //
    // East  (+1, 0): iso_x=+0.866, iso_y=+0.5 → screen: right-down
    // West  (-1, 0): iso_x=-0.866, iso_y=-0.5 → screen: left-up
    // North (0, +1): iso_x=-0.866, iso_y=+0.5 → screen: left-down
    // South (0, -1): iso_x=+0.866, iso_y=-0.5 → screen: right-up

    constexpr float COS30 = 0.866f;
    constexpr float SIN30 = 0.5f;

    struct CompassDir {
        const char* label;
        float world_x;
        float world_y;
        uint8_t r, g, b;
    };

    // World-space unit directions; the view azimuth rotates them into
    // the view frame before the fixed 45-degree iso mapping, so the
    // needle tracks the camera orbit.
    CompassDir dirs[] = {
        {"E", +1.0f,  0.0f, 255, 200, 100},  // East: warm yellow
        {"W", -1.0f,  0.0f, 200, 200, 255},  // West: cool blue
        {"N",  0.0f, +1.0f, 100, 255, 100},  // North: green
        {"S",  0.0f, -1.0f, 255, 100, 100},  // South: red
    };
    const float az_c = std::cos(view_azimuth_);
    const float az_s = std::sin(view_azimuth_);

    // Draw compass lines and labels
    for (const auto& dir : dirs) {
        // World -> view frame (CW azimuth rotation), then the fixed
        // 45-degree iso mapping used by the projection.
        float vx = dir.world_x * az_c + dir.world_y * az_s;
        float vy = -dir.world_x * az_s + dir.world_y * az_c;
        float iso_x = (vx - vy) * COS30;
        float iso_y = (vx + vy) * SIN30;

        // Screen coordinates (Y inverted for screen space)
        int end_x = cx + static_cast<int>(iso_x * radius);
        int end_y = cy - static_cast<int>(iso_y * radius);  // Minus for screen Y inversion

        // Draw direction line
        renderer->draw_line(cx, cy, end_x, end_y, dir.r, dir.g, dir.b, 255);

        // Draw label slightly past line end
        int label_x = cx + static_cast<int>(iso_x * (radius + 8)) - 3;
        int label_y = cy - static_cast<int>(iso_y * (radius + 8)) - 5;
        renderer->draw_text(label_x, label_y, dir.label, dir.r, dir.g, dir.b);
    }

    // Draw center dot
    renderer->fill_rect(cx - 2, cy - 2, 4, 4, 255, 255, 255, 255);
}

} // namespace ui