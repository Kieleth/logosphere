#ifndef WIDGET_H
#define WIDGET_H

#include <string>
#include <vector>
#include <functional>
#include <memory>

// Forward declarations
class EngineRenderState;  // retained for transitional helpers on subclasses
class UISystem;
class IDrawSurface;

namespace ui {

// Event types for the widget system
struct MouseEvent {
    int x, y;           // Mouse position in screen coordinates
    int button;         // 0=left, 1=right, 2=middle
    bool pressed;       // true=down, false=up
    int local_x, local_y;  // Position relative to widget
};

struct KeyEvent {
    int key;            // Key code
    bool pressed;       // true=down, false=up
    bool shift;         // Shift modifier
    bool ctrl;          // Control modifier
    bool alt;           // Alt modifier
};

// Rectangle for bounds
struct Rectangle {
    int x, y, width, height;
    
    bool contains(int px, int py) const {
        return px >= x && px < x + width && 
               py >= y && py < y + height;
    }
    
    Rectangle intersect(const Rectangle& other) const {
        int x1 = std::max(x, other.x);
        int y1 = std::max(y, other.y);
        int x2 = std::min(x + width, other.x + other.width);
        int y2 = std::min(y + height, other.y + other.height);
        
        if (x2 > x1 && y2 > y1) {
            return {x1, y1, x2 - x1, y2 - y1};
        }
        return {0, 0, 0, 0};  // No intersection
    }
};

// Base class for all UI widgets
class Widget {
public:
    Widget(const std::string& id = "");
    virtual ~Widget();
    
    // Core lifecycle methods
    virtual void update(float delta_time);
    virtual void render(IDrawSurface* surface);
    virtual void on_destroy();
    
    // UI System reference for accessing scale and utilities
    void set_ui_system(class UISystem* ui_system) { ui_system_ = ui_system; }
    class UISystem* get_ui_system() const { return ui_system_; }
    
    // Event handlers (return true if event was handled)
    virtual bool on_mouse_enter(MouseEvent& e) { return false; }
    virtual bool on_mouse_leave(MouseEvent& e) { return false; }
    virtual bool on_mouse_move(MouseEvent& e) { return false; }
    virtual bool on_mouse_down(MouseEvent& e) { return false; }
    virtual bool on_mouse_up(MouseEvent& e) { return false; }
    virtual bool on_mouse_click(MouseEvent& e) { return false; }  // Called after mouse up if still over widget
    virtual bool on_mouse_scroll(int delta) { return false; }  // Scroll delta (positive = up, negative = down)

    virtual bool on_key_down(KeyEvent& e) { return false; }
    virtual bool on_key_up(KeyEvent& e) { return false; }

    virtual void on_focus_gained() {}
    virtual void on_focus_lost() {}
    
    // Properties
    const std::string& get_id() const { return id_; }
    void set_id(const std::string& id) { id_ = id; }
    
    // Transform
    void set_position(int x, int y);
    void set_size(int width, int height);
    void set_bounds(int x, int y, int width, int height);
    Rectangle get_bounds() const { return {x_, y_, width_, height_}; }
    Rectangle get_absolute_bounds() const;
    
    int get_x() const { return x_; }
    int get_y() const { return y_; }
    int get_width() const { return width_; }
    int get_height() const { return height_; }
    
    // Visibility and interaction
    void set_visible(bool visible) { visible_ = visible; invalidate(); }
    bool is_visible() const { return visible_; }
    
    void set_enabled(bool enabled) { enabled_ = enabled; invalidate(); }
    bool is_enabled() const { return enabled_; }
    
    // State
    bool is_hovered() const { return hovered_; }
    bool is_focused() const { return focused_; }
    bool is_pressed() const { return pressed_; }
    
    // Parent-child relationships
    Widget* get_parent() { return parent_; }
    const Widget* get_parent() const { return parent_; }
    
    void add_child(Widget* child);
    void remove_child(Widget* child);
    void remove_all_children();
    Widget* find_child(const std::string& id);
    const std::vector<Widget*>& get_children() const { return children_; }
    
    // Z-order
    void set_z_order(int z) { z_order_ = z; }
    int get_z_order() const { return z_order_; }
    
    // Hit testing
    virtual bool contains_point(int x, int y) const;
    virtual Widget* hit_test(int x, int y);  // Returns deepest widget at point
    
    // Invalidation (marks widget for redraw)
    void invalidate();
    bool is_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }
    
    // Focus management
    bool can_focus() const { return can_focus_; }
    void set_can_focus(bool can) { can_focus_ = can; }
    
    // State management (public for UISystem access)
    void set_hovered(bool hovered) { hovered_ = hovered; }
    void set_focused(bool focused) { focused_ = focused; }
    void set_pressed(bool pressed) { pressed_ = pressed; }
    
protected:
    
    // For derived classes to override
    virtual void on_bounds_changed() {}
    virtual void on_parent_changed() {}
    
    // Helper for rendering
    void render_children(IDrawSurface* renderer);
    
private:
    std::string id_;
    Widget* parent_ = nullptr;
    std::vector<Widget*> children_;
    class UISystem* ui_system_ = nullptr;  // Reference to UI system for utilities
    
    // Transform
    int x_ = 0, y_ = 0;
    int width_ = 100, height_ = 100;
    
    // State
    bool visible_ = true;
    bool enabled_ = true;
    bool hovered_ = false;
    bool focused_ = false;
    bool pressed_ = false;
    bool dirty_ = true;
    bool can_focus_ = false;
    
    int z_order_ = 0;
    
    friend class UISystem;  // Allow UISystem to manage internal state
};

// Container widget - base for Window, Panel, etc.
class Container : public Widget {
public:
    Container(const std::string& id = "") : Widget(id) {}
    
    // Clipping
    void set_clip_children(bool clip) { clip_children_ = clip; }
    bool get_clip_children() const { return clip_children_; }
    
    // Override render to handle clipping
    void render(IDrawSurface* surface) override;
    
protected:
    bool clip_children_ = false;
};

} // namespace ui

#endif // WIDGET_H