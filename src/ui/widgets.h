#ifndef WIDGETS_H
#define WIDGETS_H

#include "widget.h"
#include <functional>

namespace ui {

// Label - displays text
class Label : public Widget {
public:
    Label(const std::string& text = "", const std::string& id = "");
    
    void render(IDrawSurface* renderer) override;
    
    // Text management
    void set_text(const std::string& text);
    const std::string& get_text() const { return text_; }
    
    // Color
    void set_color(uint8_t r, uint8_t g, uint8_t b);
    void get_color(uint8_t& r, uint8_t& g, uint8_t& b) const;
    
private:
    std::string text_;
    uint8_t r_ = 255, g_ = 255, b_ = 255;  // Default white
};

// Button - clickable with text
class Button : public Widget {
public:
    Button(const std::string& text = "", const std::string& id = "");
    
    void render(IDrawSurface* renderer) override;
    
    // Event handlers
    bool on_mouse_enter(MouseEvent& e) override;
    bool on_mouse_leave(MouseEvent& e) override;
    bool on_mouse_down(MouseEvent& e) override;
    bool on_mouse_up(MouseEvent& e) override;
    bool on_mouse_click(MouseEvent& e) override;
    
    // Text
    void set_text(const std::string& text);
    const std::string& get_text() const { return text_; }
    
    // Click callback
    std::function<void()> on_click;
    
private:
    std::string text_;
    
    // Colors for different states
    struct ButtonColors {
        uint8_t r, g, b;
    };
    ButtonColors normal_color_ = {60, 60, 80};
    ButtonColors hover_color_ = {80, 80, 100};
    ButtonColors pressed_color_ = {100, 100, 120};
};

// Separator - visual divider line
class Separator : public Widget {
public:
    Separator(const std::string& id = "");
    
    void render(IDrawSurface* renderer) override;
    
    // Orientation
    enum Orientation { Horizontal, Vertical };
    void set_orientation(Orientation orient) { orientation_ = orient; }
    Orientation get_orientation() const { return orientation_; }
    
    // Color
    void set_color(uint8_t r, uint8_t g, uint8_t b);
    
private:
    Orientation orientation_ = Horizontal;
    uint8_t r_ = 100, g_ = 100, b_ = 100;  // Default gray
};

// Window - draggable container with title bar
class Window : public Container {
public:
    Window(const std::string& title = "", const std::string& id = "");
    
    void render(IDrawSurface* renderer) override;
    
    // Event handlers for dragging
    bool on_mouse_down(MouseEvent& e) override;
    bool on_mouse_up(MouseEvent& e) override;
    bool on_mouse_move(MouseEvent& e) override;
    
    // Title
    void set_title(const std::string& title);
    const std::string& get_title() const { return title_; }
    
    // Window properties
    void set_draggable(bool draggable) { draggable_ = draggable; }
    bool is_draggable() const { return draggable_; }
    
    void set_closeable(bool closeable) { closeable_ = closeable; }
    bool is_closeable() const { return closeable_; }
    
    // Close callback
    std::function<void()> on_close;
    
private:
    std::string title_;
    bool draggable_ = true;
    bool closeable_ = false;
    
    // Dragging state
    bool dragging_ = false;
    int drag_offset_x_ = 0;
    int drag_offset_y_ = 0;
    
    static constexpr int title_bar_height_ = 25;
    
    virtual bool is_in_title_bar(int local_x, int local_y) const;
};

// Panel - simple container with background
class Panel : public Container {
public:
    Panel(const std::string& id = "");
    
    void render(IDrawSurface* renderer) override;
    
    // Background color
    void set_background_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    
    // Border
    void set_border(bool enabled, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
    
private:
    uint8_t bg_r_ = 20, bg_g_ = 20, bg_b_ = 30, bg_a_ = 255;
    bool has_border_ = false;
    uint8_t border_r_ = 255, border_g_ = 255, border_b_ = 255;
};

// ListMenu - scrollable list of selectable items
class ListMenu : public Container {
public:
    ListMenu(const std::string& id = "");
    
    void render(IDrawSurface* renderer) override;
    void update(float delta_time) override;
    
    // Event handlers
    bool on_key_down(KeyEvent& e) override;
    bool on_mouse_down(MouseEvent& e) override;
    bool on_mouse_move(MouseEvent& e) override;
    
    // Item management
    void add_item(const std::string& text, const std::string& data = "");
    void remove_item(int index);
    void clear_items();
    int get_item_count() const { return static_cast<int>(items_.size()); }
    
    // Selection
    void select_item(int index);
    void select_next();
    void select_previous();
    int get_selected_index() const { return selected_index_; }
    std::string get_selected_data() const;
    
    // Activation (Enter key or double-click)
    void activate_selected();
    
    // Callbacks
    std::function<void(int index)> on_selection_changed;
    std::function<void(const std::string& data)> on_item_activated;
    
private:
    struct MenuItem {
        std::string text;
        std::string data;
    };
    
    std::vector<MenuItem> items_;
    int selected_index_ = -1;
    int hovered_index_ = -1;
    
    // Visual properties
    int item_height_ = 25;
    int padding_ = 5;
    
    // Colors
    uint8_t bg_r_ = 30, bg_g_ = 30, bg_b_ = 40;
    uint8_t selected_r_ = 60, selected_g_ = 60, selected_b_ = 80;
    uint8_t hover_r_ = 45, hover_g_ = 45, hover_b_ = 60;
    uint8_t text_r_ = 255, text_g_ = 255, text_b_ = 255;
    
    int get_item_at_position(int local_y) const;
};

// CompassWidget - draggable compass showing world directions
// Uses isometric projection to show N/S/E/W axes in screen space
// Hidden by default, toggle with hotkey
class CompassWidget : public Window {
public:
    CompassWidget(const std::string& id = "compass");

    void render(IDrawSurface* renderer) override;

    // View azimuth (orbit) in radians, CW-positive; fed per frame by
    // UISystem from the camera so the needle tracks the orbit.
    void set_view_azimuth(float radians) { view_azimuth_ = radians; }

protected:
    // Override to make entire widget draggable (no title bar)
    bool is_in_title_bar(int local_x, int local_y) const override;

private:
    static constexpr int compass_size_ = 80;  // Size of compass display
    float view_azimuth_ = 0.0f;
};

} // namespace ui

#endif // WIDGETS_H