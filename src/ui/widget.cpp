#include "widget.h"
#include "logosphere/rendering/i_draw_surface.h"
#include <algorithm>

namespace ui {

// Widget implementation

Widget::Widget(const std::string& id) : id_(id) {
    if (id_.empty()) {
        // Generate a unique ID if none provided
        static int widget_counter = 0;
        id_ = "widget_" + std::to_string(++widget_counter);
    }
}

Widget::~Widget() {
    // Clean up children
    remove_all_children();
    
    // Remove from parent if we have one
    if (parent_) {
        parent_->remove_child(this);
    }
}

void Widget::update(float delta_time) {
    // Update children
    for (auto* child : children_) {
        if (child && child->is_visible()) {
            child->update(delta_time);
        }
    }
}

void Widget::render(IDrawSurface* renderer) {
    if (!visible_) return;
    
    // Render children (derived classes should render themselves first)
    render_children(renderer);
}

void Widget::render_children(IDrawSurface* renderer) {
    // Sort children by z-order for rendering
    std::vector<Widget*> sorted_children = children_;
    std::sort(sorted_children.begin(), sorted_children.end(),
              [](Widget* a, Widget* b) { return a->get_z_order() < b->get_z_order(); });
    
    // Render each child
    for (auto* child : sorted_children) {
        if (child && child->is_visible()) {
            child->render(renderer);
        }
    }
}

void Widget::on_destroy() {
    // Called when widget is being destroyed
    // Derived classes can override for cleanup
}

void Widget::set_position(int x, int y) {
    if (x_ != x || y_ != y) {
        x_ = x;
        y_ = y;
        on_bounds_changed();
        invalidate();
    }
}

void Widget::set_size(int width, int height) {
    if (width_ != width || height_ != height) {
        width_ = width;
        height_ = height;
        on_bounds_changed();
        invalidate();
    }
}

void Widget::set_bounds(int x, int y, int width, int height) {
    if (x_ != x || y_ != y || width_ != width || height_ != height) {
        x_ = x;
        y_ = y;
        width_ = width;
        height_ = height;
        on_bounds_changed();
        invalidate();
    }
}

Rectangle Widget::get_absolute_bounds() const {
    int abs_x = x_;
    int abs_y = y_;
    
    // Walk up the parent chain to get absolute position
    const Widget* current = parent_;
    while (current) {
        abs_x += current->get_x();
        abs_y += current->get_y();
        current = current->get_parent();
    }
    
    return {abs_x, abs_y, width_, height_};
}

void Widget::add_child(Widget* child) {
    if (!child || child->parent_ == this) return;
    
    // Remove from previous parent
    if (child->parent_) {
        child->parent_->remove_child(child);
    }
    
    // Add to our children
    children_.push_back(child);
    child->parent_ = this;
    child->on_parent_changed();
    child->invalidate();
    invalidate();
}

void Widget::remove_child(Widget* child) {
    if (!child || child->parent_ != this) return;
    
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        children_.erase(it);
        child->parent_ = nullptr;
        child->on_parent_changed();
        invalidate();
    }
}

void Widget::remove_all_children() {
    for (auto* child : children_) {
        if (child) {
            child->parent_ = nullptr;
            child->on_parent_changed();
        }
    }
    children_.clear();
    invalidate();
}

Widget* Widget::find_child(const std::string& id) {
    for (auto* child : children_) {
        if (child && child->get_id() == id) {
            return child;
        }
        // Recursive search
        Widget* found = child->find_child(id);
        if (found) return found;
    }
    return nullptr;
}

bool Widget::contains_point(int x, int y) const {
    Rectangle bounds = get_absolute_bounds();
    return bounds.contains(x, y);
}

Widget* Widget::hit_test(int x, int y) {
    if (!visible_ || !enabled_) return nullptr;
    
    // Check if point is within our bounds
    if (!contains_point(x, y)) return nullptr;
    
    // Check children in reverse z-order (top to bottom)
    std::vector<Widget*> sorted_children = children_;
    std::sort(sorted_children.begin(), sorted_children.end(),
              [](Widget* a, Widget* b) { return a->get_z_order() > b->get_z_order(); });
    
    for (auto* child : sorted_children) {
        Widget* hit = child->hit_test(x, y);
        if (hit) return hit;
    }
    
    // Point is in us but not in any child
    return this;
}

void Widget::invalidate() {
    dirty_ = true;
    
    // Propagate invalidation up the parent chain
    if (parent_) {
        parent_->invalidate();
    }
}

// Container implementation

void Container::render(IDrawSurface* renderer) {
    if (!is_visible()) return;
    
    // TODO: Implement clipping if clip_children_ is true
    // For now, just render normally
    
    // Render self (derived classes should override)
    // Then render children
    render_children(renderer);
}

} // namespace ui