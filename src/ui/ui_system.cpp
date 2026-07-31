#include "ui_system.h"
#include "widget.h"
#include "widgets.h"
#include "chat_window.h"
#include "logosphere/rendering/font_renderer.h"
#include "logosphere/rendering/primitive_renderer.h"
#include "logosphere/rendering/coordinate_transformer.h"
#include "logosphere/rendering/pixel_buffer.h"
#include "logosphere/rendering/sparse_object_map.h"
#include "logosphere/rendering/resolution_manager.h"
#include "logosphere/rendering/pixel_picker.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_types.h"
#include "../core/particle_system.h"
#include "../particle.h"
#include "../core/engine.h"
#include "../debug_control.h"  // Centralized debug control
#include "../uv_cache.h"       // UV cache statistics
#include "../optimization_flags.h"  // Feature flags
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cassert>
#include "platform/glfw_compat.h"  // key codes (real GLFW, or shim)

// Global UI system instance removed - now accessed via Engine::get_ui_system()

UISystem::UISystem() 
    : engine_(nullptr)
    , is_initialized_(false) {
    
    // Initialize tooltip state
    tooltip_.visible = false;
    tooltip_.x = 0;
    tooltip_.y = 0;
    tooltip_.show_time = 0.0f;
    
    // Initialize inspector state
    inspector_.visible = false;
    inspector_.anchored_entity_id = -1;
    inspector_.anchored_particle_id = -1;
    inspector_.animation_time = 0.0f;
    
    // Initialize debug overlay state
    debug_.show_grid = false;
    debug_.show_vision_cones = false;
    debug_.show_light_rays = false;
    debug_.show_performance = false;
    
    // Initialize sandbox testing state
    sandbox_.menu_visible = false;
    sandbox_.menu_x = 0;
    sandbox_.menu_y = 0;
    sandbox_.menu_width = 540;
    sandbox_.menu_height = 550;
    sandbox_.current_test = "";
    sandbox_.tests_available = true;
}

UISystem::~UISystem() {
    shutdown();
}

bool UISystem::initialize(Engine* engine, const Config& config) {
    if (is_initialized_) {
        return true;  // Already initialized
    }

    if (!engine) {
        std::cerr << "UISystem: Cannot initialize without Engine" << std::endl;
        return false;
    }

    engine_     = engine;
    // UI rasters into the OVERLAY PLANE, never the scene buffer the GPU
    // completion callback owns. That plane is composited at present
    // (the UI overlay design notes).
    ui_target_  = &engine->get_ui_overlay_buffer();
    object_map_ = &engine->get_object_map();
    font_       = &engine->get_font_renderer();
    primitives_ = &engine->get_primitive_renderer();
    coord_      = &engine->get_coord_transformer();
    camera_     = &engine->get_camera_system();
    resolution_ = &engine->get_resolution_manager();
    config_ = config;
    
    std::cout << "UISystem: Initialized with screen "
              << config_.screen_width << "x" << config_.screen_height
              << " (DPI scale: " << config_.dpi_scale << ")" << std::endl;

    // Conditionally create chat window (only if enabled in config)
    if (config_.enable_chat_window) {
        // Create chat window widget (Phase 2: Widget Conversion)
        chat_window_ = new ChatWindow("main_chat");
        chat_window_->set_ui_system(this);
        root_widgets_.push_back(chat_window_);

        // Position and size (Phase 4: larger window with transparency)
        int margin_left = 50;
        int margin_top = 50;
        int max_width = 700;  // Phase 4: increased from 600
        float height_ratio = 0.15f;  // Phase 4: increased from 0.13 (~180px at 1200px height)
        int width = std::min(max_width, config_.screen_width - margin_left - 250);
        int height = static_cast<int>(config_.screen_height * height_ratio);

        chat_window_->set_position(margin_left, margin_top);
        chat_window_->set_size(width, height);
        chat_window_->set_visible(true);

        // Proper fix: Initialize focus via Widget system (calls on_focus_gained)
        set_focused_widget(chat_window_);

        std::cout << "UISystem: Created chat window at (" << margin_left << "," << margin_top
                  << ") size " << width << "x" << height << std::endl;
    } else {
        chat_window_ = nullptr;
        std::cout << "UISystem: Chat window disabled (enable_chat_window=false)" << std::endl;
    }

    // Create compass widget (hidden by default, toggle with 'C' key)
    if (config_.enable_compass) {
        compass_widget_ = new ui::CompassWidget("compass");
        compass_widget_->set_ui_system(this);
        root_widgets_.push_back(compass_widget_);

        // Position in lower-left corner
        int margin = 20;
        int compass_x = margin;
        int compass_y = 300;
        compass_widget_->set_position(compass_x, compass_y);
        // Starts hidden - toggle with 'C' key

        std::cout << "UISystem: Created compass widget at (" << compass_x << "," << compass_y
                  << ") - hidden by default, press 'C' to toggle" << std::endl;
    } else {
        compass_widget_ = nullptr;
    }

    is_initialized_ = true;
    return true;
}

void UISystem::shutdown() {
    if (!is_initialized_) {
        return;
    }
    
    engine_ = nullptr;
    is_initialized_ = false;
    
    std::cout << "UISystem: Shutdown complete" << std::endl;
}

// =========================================================================
// Private drawing helpers
// Implement the text / primitive / pixel operations UISystem used to
// get from RenderSystem::draw_*. Same semantics: write to the render
// buffer, tag object_map_ with current_object_id_ when non-zero.
// =========================================================================

void UISystem::ui_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    ui_set_pixel_with_object(x, y, r, g, b, a, current_object_id_);
}

void UISystem::ui_set_pixel_with_object(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int object_id) {
    if (!ui_target_) return;
    if (a == 255) {
        ui_target_->set_pixel_with_object(x, y, r, g, b, a, 0);
    } else if (a > 0) {
        ui_target_->blend_pixel(x, y, r, g, b, a, 0);
    } else {
        return;
    }
    if (object_id != 0 && object_map_) {
        object_map_->set_object(x, y, object_id);
    }
}

void UISystem::ui_draw_char(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b) {
    if (!font_) return;
    int char_index = font_->get_char_index(c);
    const uint8_t* char_data = FontRenderer::get_char_data(char_index);
    for (int row = 0; row < FontRenderer::CHAR_HEIGHT; row++) {
        uint8_t row_data = char_data[row];
        for (int col = 0; col < FontRenderer::CHAR_WIDTH; col++) {
            if (row_data & (0b10000 >> col)) {
                ui_set_pixel(x + col, y + row, r, g, b);
            }
        }
    }
}

void UISystem::ui_draw_char_scaled(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, float scale) {
    if (!font_) return;
    int char_index = font_->get_char_index(c);
    const uint8_t* char_data = FontRenderer::get_char_data(char_index);
    int scale_int = std::max(1, static_cast<int>(scale));
    for (int row = 0; row < FontRenderer::CHAR_HEIGHT; row++) {
        uint8_t row_data = char_data[row];
        for (int col = 0; col < FontRenderer::CHAR_WIDTH; col++) {
            if (row_data & (0b10000 >> col)) {
                for (int dy = 0; dy < scale_int; dy++) {
                    for (int dx = 0; dx < scale_int; dx++) {
                        ui_set_pixel(x + col * scale_int + dx,
                                     y + row * scale_int + dy, r, g, b);
                    }
                }
            }
        }
    }
}

void UISystem::ui_draw_string(int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b) {
    int current_x = x;
    for (char c : text) {
        ui_draw_char(current_x, y, c, r, g, b);
        current_x += FontRenderer::CHAR_WIDTH + FontRenderer::CHAR_SPACING;
    }
}

void UISystem::ui_draw_string_scaled(int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b, float scale) {
    int scale_int = std::max(1, static_cast<int>(scale));
    int scaled_char_width = FontRenderer::CHAR_WIDTH * scale_int;
    int scaled_spacing    = FontRenderer::CHAR_SPACING * scale_int;
    int current_x = x;
    for (char c : text) {
        ui_draw_char_scaled(current_x, y, c, r, g, b, scale);
        current_x += scaled_char_width + scaled_spacing;
    }
}

void UISystem::ui_draw_line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) {
    if (primitives_ && draw_surface_) primitives_->draw_line(draw_surface_, x1, y1, x2, y2, r, g, b);
}

void UISystem::ui_draw_line_3d(float x1, float y1, float z1, float x2, float y2, float z2,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!coord_) return;
    int sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
    coord_->world_to_screen_3d(x1, y1, z1, sx1, sy1);
    coord_->world_to_screen_3d(x2, y2, z2, sx2, sy2);
    // 2D line in screen space — PrimitiveRenderer for consistency.
    if (primitives_) {
        if (!draw_surface_) return;
        if (a == 255) primitives_->draw_line(draw_surface_, sx1, sy1, sx2, sy2, r, g, b);
        else          primitives_->draw_line_alpha(draw_surface_, sx1, sy1, sx2, sy2, r, g, b, a);
    }
}

void UISystem::ui_world_to_screen_3d(float world_x, float world_y, float world_z, int& screen_x, int& screen_y) const {
    if (coord_) coord_->world_to_screen_3d(world_x, world_y, world_z, screen_x, screen_y);
    else screen_x = screen_y = 0;
}

void UISystem::ui_mouse_to_framebuffer(double mouse_x, double mouse_y, int& fb_x, int& fb_y) const {
    if (coord_) coord_->mouse_to_framebuffer(mouse_x, mouse_y, fb_x, fb_y);
    else { fb_x = static_cast<int>(mouse_x); fb_y = static_cast<int>(mouse_y); }
}

void UISystem::update(float delta_time) {
    if (!is_initialized_) {
        return;
    }
    
    // Update retained mode widgets
    for (auto* widget : root_widgets_) {
        if (widget) {
            widget->update(delta_time);
        }
    }
    
    // Update tooltip fade
    if (tooltip_.visible) {
        tooltip_.show_time += delta_time;
    }

    // Update inspector animation
    if (inspector_.visible) {
        inspector_.animation_time += delta_time;
    }

    // Chat state updates now handled by ChatWindow widget
    // (widget system calls update() on root_widgets_ automatically)
}

void UISystem::render() {
    if (!is_initialized_) {
        return;
    }
    assert(engine_ && "UISystem requires RenderSystem to be set via initialize()");

    // DEBUG: Track render frequency
    static int ui_render_count = 0;
    ui_render_count++;

    if (ui_render_count % 60 == 0) {
        std::cout << "[UISYSTEM_RENDER_DEBUG] Frame " << ui_render_count
                  << " - widgets=" << root_widgets_.size()
                  << " debug_overlays=" << config_.enable_debug_overlays
                  << std::endl;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    metrics_.widgets_rendered = 0;
    metrics_.text_chars_rendered = 0;

    // Render debug overlays first (behind UI)
    if (config_.enable_debug_overlays) {
        render_debug_overlay(debug_.show_grid, debug_.show_vision_cones, debug_.show_light_rays);
    }

    // Render retained mode widgets (they take priority over immediate mode)
    // Skip widgets that have a parent - they'll be rendered as children
    for (auto* widget : root_widgets_) {
        if (widget && widget->is_visible() && !widget->get_parent()) {
            // Widgets draw through the Engine-owned IDrawSurface.
            // engine_ no longer implements IDrawSurface; a
            // DrawSurface must be wired in before render() runs.
            widget->render(draw_surface_);
            metrics_.widgets_rendered++;
        }
    }
    
    // Particle inspector removed - no longer needed
    
    // Render tooltip (on top of everything)
    if (tooltip_.visible) {
        // Draw tooltip background
        int text_width = tooltip_.text.length() * 8;  // Approximate
        int text_height = 20;
        int padding = 5;
        
        // Ensure tooltip stays on screen
        int tooltip_x = tooltip_.x + 10;
        int tooltip_y = tooltip_.y - text_height - padding * 2;
        
        if (tooltip_x + text_width + padding * 2 > config_.screen_width) {
            tooltip_x = config_.screen_width - text_width - padding * 2;
        }
        if (tooltip_y < 0) {
            tooltip_y = tooltip_.y + 20;  // Show below cursor instead
        }
        
        // Draw background
        draw_rect(tooltip_x, tooltip_y, 
                 text_width + padding * 2, text_height + padding * 2,
                 40, 40, 40, 200);
        
        // Draw text
        draw_text(tooltip_x + padding, tooltip_y + padding, tooltip_.text,
                 255, 255, 255, 255);
        
        metrics_.widgets_rendered++;
        metrics_.text_chars_rendered += tooltip_.text.length();
    }
    
    // Render performance metrics if enabled
    if (debug_.show_performance) {
        render_performance_metrics(metrics_);
        metrics_.widgets_rendered++;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    metrics_.render_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
}

// UI Primitives Implementation

void UISystem::begin_window(const std::string& id, int x, int y, int width, int height, const std::string& title) {
    assert(engine_ && "UISystem requires RenderSystem to be set via initialize()");
    
    // Store window state
    current_window_.id = id;
    current_window_.x = x;
    current_window_.y = y;
    current_window_.width = width;
    current_window_.height = height;
    current_window_.title = title;
    current_window_.is_active = true;
    
    // Draw window background (semi-transparent)
    for (int py = y; py < y + height && py < config_.screen_height; py++) {
        for (int px = x; px < x + width && px < config_.screen_width; px++) {
            if (px >= 0 && py >= 0) {
                ui_set_pixel(px, py, 20, 20, 30, 230);
            }
        }
    }
    
    // Draw window border
    for (int px = x; px < x + width; px++) {
        ui_set_pixel(px, y, 100, 150, 255, 255);  // Top
        ui_set_pixel(px, y + height - 1, 100, 150, 255, 255);  // Bottom
    }
    for (int py = y; py < y + height; py++) {
        ui_set_pixel(x, py, 100, 150, 255, 255);  // Left
        ui_set_pixel(x + width - 1, py, 100, 150, 255, 255);  // Right
    }
    
    // Draw title bar
    int title_height = 20;
    for (int py = y; py < y + title_height && py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            ui_set_pixel(px, py, 30, 30, 50, 255);
        }
    }
    
    // Draw title text
    ui_draw_string(x + 5, y + 5, title, 200, 200, 255);
    
    // Set up default layout for window content
    LayoutState layout;
    layout.type = LayoutType::None;
    layout.spacing = 5;
    layout.cursor_x = x + 5;
    layout.cursor_y = y + title_height + 5;
    layout.start_x = x + 5;
    layout.start_y = y + title_height + 5;
    layout.max_width = width - 10;
    layout.max_height = height - title_height - 10;
    layout.row_height = 0;
    layout_stack_.push_back(layout);
}

void UISystem::end_window() {
    if (!current_window_.is_active) return;
    
    current_window_.is_active = false;
    
    // Pop the window's layout
    if (!layout_stack_.empty()) {
        layout_stack_.pop_back();
    }
}

void UISystem::begin_panel(int x, int y, int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    if (!engine_) return;
    
    // Draw panel background
    for (int py = y; py < y + height && py < config_.screen_height; py++) {
        for (int px = x; px < x + width && px < config_.screen_width; px++) {
            if (px >= 0 && py >= 0) {
                ui_set_pixel(px, py, r, g, b, 200);
            }
        }
    }
    
    // Set up layout for panel content
    LayoutState layout;
    layout.type = LayoutType::None;
    layout.spacing = 5;
    layout.cursor_x = x + 5;
    layout.cursor_y = y + 5;
    layout.start_x = x + 5;
    layout.start_y = y + 5;
    layout.max_width = width - 10;
    layout.max_height = height - 10;
    layout.row_height = 0;
    layout_stack_.push_back(layout);
}

void UISystem::end_panel() {
    // Pop the panel's layout
    if (!layout_stack_.empty()) {
        layout_stack_.pop_back();
    }
}

void UISystem::label(int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b) {
    if (!engine_) return;
    
    // Use our scaled draw_text method
    draw_text(x, y, text, r, g, b);
}

void UISystem::label(const std::string& text, uint8_t r, uint8_t g, uint8_t b) {
    if (!engine_ || layout_stack_.empty()) return;
    
    auto& layout = layout_stack_.back();
    
    // Draw at current layout position using scaled text
    draw_text(layout.cursor_x, layout.cursor_y, text, r, g, b);
    
    // Update layout cursor based on layout type (account for UI scaling)
    int text_width = static_cast<int>(text.length() * 8 * ui_scale_multiplier_);  // Approximate
    int text_height = static_cast<int>(12 * ui_scale_multiplier_);
    
    if (layout.type == LayoutType::Horizontal) {
        layout.cursor_x += text_width + layout.spacing;
        layout.row_height = std::max(layout.row_height, text_height);
    } else if (layout.type == LayoutType::Vertical) {
        layout.cursor_y += text_height + layout.spacing;
    }
}

void UISystem::separator(int x, int y, int width) {
    if (!engine_) return;
    
    for (int px = x; px < x + width; px++) {
        ui_set_pixel(px, y, 50, 50, 100, 255);
    }
}

void UISystem::push_layout(LayoutType type, int spacing) {
    LayoutState layout;
    layout.type = type;
    layout.spacing = spacing;
    
    if (!layout_stack_.empty()) {
        // Inherit position from parent layout
        auto& parent = layout_stack_.back();
        layout.cursor_x = parent.cursor_x;
        layout.cursor_y = parent.cursor_y;
        layout.start_x = parent.cursor_x;
        layout.start_y = parent.cursor_y;
        layout.max_width = parent.max_width;
        layout.max_height = parent.max_height;
    } else {
        layout.cursor_x = 0;
        layout.cursor_y = 0;
        layout.start_x = 0;
        layout.start_y = 0;
        layout.max_width = config_.screen_width;
        layout.max_height = config_.screen_height;
    }
    
    layout.row_height = 0;
    layout_stack_.push_back(layout);
}

void UISystem::pop_layout() {
    if (!layout_stack_.empty()) {
        layout_stack_.pop_back();
    }
}

void UISystem::next_row() {
    if (layout_stack_.empty()) return;
    
    auto& layout = layout_stack_.back();
    layout.cursor_x = layout.start_x;
    layout.cursor_y += layout.row_height + layout.spacing;
    layout.row_height = 0;
}

void UISystem::same_line() {
    if (layout_stack_.empty()) return;
    
    auto& layout = layout_stack_.back();
    // Move cursor back to end of last element on same line
    // This is a simplified version - full implementation would track element positions
    layout.cursor_x += 50;  // Approximate spacing
}

void UISystem::spacing(int pixels) {
    if (layout_stack_.empty()) return;
    
    auto& layout = layout_stack_.back();
    if (layout.type == LayoutType::Vertical) {
        layout.cursor_y += pixels;
    } else if (layout.type == LayoutType::Horizontal) {
        layout.cursor_x += pixels;
    }
}

bool UISystem::button(int x, int y, int width, int height, const std::string& text) {
    if (!engine_) return false;
    
    // Draw button background
    for (int py = y; py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            ui_set_pixel(px, py, 60, 60, 80, 255);
        }
    }
    
    // Draw button border
    for (int px = x; px < x + width; px++) {
        ui_set_pixel(px, y, 100, 100, 120, 255);
        ui_set_pixel(px, y + height - 1, 100, 100, 120, 255);
    }
    for (int py = y; py < y + height; py++) {
        ui_set_pixel(x, py, 100, 100, 120, 255);
        ui_set_pixel(x + width - 1, py, 100, 100, 120, 255);
    }
    
    // Draw button text (centered)
    int text_width = text.length() * 8;
    int text_x = x + (width - text_width) / 2;
    int text_y = y + height / 2 - 6;
    ui_draw_string(text_x, text_y, text, 255, 255, 255);
    
    // TODO: Check for mouse click
    return false;
}

void UISystem::update_input_state(int mouse_x, int mouse_y, bool left_button, bool right_button) {
    // DEBUG: Log first few updates
    static int update_count = 0;
    if (update_count < 3) {
        std::cout << "[UI INPUT UPDATE #" << update_count << "] "
                  << "received mouse=(" << mouse_x << "," << mouse_y << ") "
                  << "buttons=(L:" << left_button << " R:" << right_button << ")" << std::endl;
        update_count++;
    }

    input_state_.mouse_x = mouse_x;
    input_state_.mouse_y = mouse_y;
    
    
    // Detect click events (transition from not pressed to pressed)
    if (left_button && !input_state_.left_button) {
        // Left click just happened
        // Actual anchoring is handled in render_kg_inspector where we have the KG module
        // This just detects the click event
    }
    
    // Update button states for next frame (IMPORTANT: must be after click detection)
    input_state_.was_left_pressed = input_state_.left_button;
    input_state_.was_right_pressed = input_state_.right_button;
    input_state_.left_button = left_button;
    input_state_.right_button = right_button;
}

// set_inspector_hovered_particle removed - no longer needed

// Now reimplement render_kg_inspector using primitives
void UISystem::render_kg_inspector(kg::KGModule* kg_module, 
                                   int hovered_particle_id,
                                   int anchored_entity_id,
                                   Engine* engine) {
    if (!is_initialized_ || !engine_) {
        return;
    }
    
    
    // Allow inspector to work without KG module (for test mode)
    // Just show particle info if KG is not available
    if (kg_module && !kg_module->isEnabled()) {
        return;
    }
    
    // Store hovered particle for click handling
    inspector_.hovered_particle_id = hovered_particle_id;
    
    // Handle click for anchoring (using input state from Engine)
    if (input_state_.left_button && !input_state_.was_left_pressed) {
        // Left click just happened
        if (hovered_particle_id >= 0) {
            // Anchor to the hovered particle
            inspector_.anchored_particle_id = hovered_particle_id;
            inspector_.anchored_surface_index = inspector_.hovered_surface_index;  // Store the clicked surface

            // Get entity if KG module is available
            if (kg_module) {
                // Get ALL entities this particle belongs to (supports multi-entity binding)
                auto entities = kg_module->getEntitiesByRenderIndex(hovered_particle_id);

                if (!entities.empty()) {
                    inspector_.anchored_entity_id = entities[0];  // Primary entity for backward compat

                    std::cout << "[UI CLICK] Particle " << hovered_particle_id
                              << " → Entities [";
                    for (size_t i = 0; i < entities.size(); ++i) {
                        std::cout << entities[i];
                        if (i < entities.size() - 1) std::cout << ", ";
                    }
                    std::cout << "] (ANCHORED!)" << std::endl;
                } else {
                    inspector_.anchored_entity_id = -1;
                }
            } else {
                inspector_.anchored_entity_id = -1;
            }
        } else {
            // Click on empty space clears anchor
            std::cout << "[UI CLICK] Clearing anchor (clicked empty space)" << std::endl;
            inspector_.anchored_entity_id = -1;
            inspector_.anchored_particle_id = -1;
            inspector_.anchored_surface_index = -1;
        }
    } else if (hovered_particle_id >= 0 && kg_module) {
        // NOT a click - just hovering
        auto hover_entities = kg_module->getEntitiesByRenderIndex(hovered_particle_id);
        kg::EntityID hover_entity = hover_entities.empty() ? kg::EntityID(-1) : hover_entities[0];
        static kg::EntityID last_hover_entity = -1;
        if (hover_entity != last_hover_entity) {
            std::cout << "[UI HOVER] Particle " << hovered_particle_id
                      << " → Entity " << hover_entity
                      << " (anchored_entity=" << inspector_.anchored_entity_id << ")" << std::endl;
            last_hover_entity = hover_entity;
        }
    }
    
    // UISystem as Canvas Master: UI owns highlighting concept, tells renderer what to draw
    if (engine) {
        // Get render dimensions once for both anchored and hovered highlighting
        int render_width = resolution_->get_render_width();
        int render_height = resolution_->get_render_height();

        // Helper lambda for pixel-perfect edge highlighting
        auto highlight_particle_edges = [&](int particle_id, uint8_t r, uint8_t g, uint8_t b) {
            // Scan object_map to find edge pixels of the particle
            // Edge pixel = particle pixel with at least one non-particle neighbor
            for (int y = 0; y < render_height; y++) {
                for (int x = 0; x < render_width; x++) {
                    int object_id = pixel_picker_->get_object_at_pixel_direct(x, y);

                    if (ObjectID::is_particle(object_id) &&
                        ObjectID::get_particle_id(object_id) == particle_id) {
                        // This pixel belongs to particle - check if it's an edge
                        bool is_edge = false;

                        // Check 4-connected neighbors
                        for (int dy = -1; dy <= 1 && !is_edge; dy++) {
                            for (int dx = -1; dx <= 1 && !is_edge; dx++) {
                                if (dx == 0 && dy == 0) continue;  // Skip center
                                if (abs(dx) + abs(dy) == 2) continue;  // Skip diagonals (4-connected)

                                int nx = x + dx;
                                int ny = y + dy;

                                if (nx < 0 || ny < 0 || nx >= render_width || ny >= render_height) {
                                    is_edge = true;  // Edge of screen
                                    continue;
                                }

                                int neighbor_id = pixel_picker_->get_object_at_pixel_direct(nx, ny);
                                int neighbor_particle = ObjectID::is_particle(neighbor_id) ?
                                                       ObjectID::get_particle_id(neighbor_id) : -1;

                                if (neighbor_particle != particle_id) {
                                    is_edge = true;  // Neighbor is different particle or background
                                }
                            }
                        }

                        // Draw edge pixel
                        if (is_edge) {
                            ui_set_pixel(x, y, r, g, b);
                        }
                    }
                }
            }
        };

        // UI decides: anchored particle should have pixel-perfect cyan edge highlight
        if (inspector_.anchored_particle_id >= 0 &&
            inspector_.anchored_particle_id < engine->get_particle_count()) {
            highlight_particle_edges(inspector_.anchored_particle_id, 0, 255, 255);  // Cyan
        }

        // UI decides: hovered particle should have pixel-perfect yellow edge highlight (if different from anchored)
        if (hovered_particle_id >= 0 &&
            hovered_particle_id < engine->get_particle_count() &&
            hovered_particle_id != inspector_.anchored_particle_id) {

            // DEBUG: Log hover attempts
            static int last_hovered = -1;
            if (hovered_particle_id != last_hovered) {
                std::cout << "[HOVER] Highlighting particle " << hovered_particle_id
                          << " (anchored=" << inspector_.anchored_particle_id << ")" << std::endl;
                last_hovered = hovered_particle_id;
            }

            highlight_particle_edges(hovered_particle_id, 255, 255, 0);  // Yellow
        }
    }
    
    // Prefer internal anchored state over passed parameter
    int entity_to_display = inspector_.anchored_entity_id;

    if (entity_to_display > 0) {
        std::cout << "[UI DISPLAY] Showing anchored entity: " << entity_to_display << std::endl;
    }

    // Create inspector window using primitives WITHOUT UI scaling for now
    // TODO: Fix UI scaling for inspector window
    int scaled_width = 300;  // Fixed size for now
    int scaled_height = 200; // Fixed size for now
    int scaled_margin = 10;   // Fixed margin
    
    int window_x = config_.screen_width - scaled_width - scaled_margin;
    int window_y = config_.screen_height - scaled_height - scaled_margin;
    
    
    begin_window("kg_inspector", 
                 window_x, window_y,
                 scaled_width, scaled_height,
                 "KG INSPECTOR");
    
    // Determine what to show
    kg::EntityID entity_id = 0;
    bool show_particle_only = false;
    
    if (kg_module) {
        // Normal mode with KG module
        if (entity_to_display > 0) {
            entity_id = entity_to_display;
            label(config_.screen_width - static_cast<int>(80 * ui_scale_multiplier_), 
                  config_.screen_height - static_cast<int>(190 * ui_scale_multiplier_), 
                  "[ANCHORED]", 200, 200, 100);
        } else if (hovered_particle_id >= 0) {
            auto hover_entities = kg_module->getEntitiesByRenderIndex(hovered_particle_id);
            entity_id = hover_entities.empty() ? kg::EntityID(-1) : hover_entities[0];
            label(config_.screen_width - static_cast<int>(80 * ui_scale_multiplier_), 
                  config_.screen_height - static_cast<int>(190 * ui_scale_multiplier_), 
                  "[HOVER]", 200, 200, 100);
        }
    } else {
        // Test mode without KG module - just show particle info
        show_particle_only = true;
        if (inspector_.anchored_particle_id >= 0) {
            label(config_.screen_width - static_cast<int>(80 * ui_scale_multiplier_), 
                  config_.screen_height - static_cast<int>(190 * ui_scale_multiplier_), 
                  "[ANCHORED]", 200, 200, 100);
        } else if (hovered_particle_id >= 0) {
            label(config_.screen_width - static_cast<int>(80 * ui_scale_multiplier_), 
                  config_.screen_height - static_cast<int>(190 * ui_scale_multiplier_), 
                  "[HOVER]", 200, 200, 100);
        }
    }
    
    // Use layout for content
    push_layout(LayoutType::Vertical, 2);

    if (kg_module && entity_id > 0 && kg_module->exists(entity_id)) {
        // Show ALL entities this particle belongs to (multi-entity support)
        if (inspector_.anchored_particle_id >= 0) {
            auto all_entities = kg_module->getEntitiesByRenderIndex(inspector_.anchored_particle_id);

            if (all_entities.size() > 1) {
                // Multiple entities - show hierarchical path
                std::string entity_path = "";
                for (size_t i = 0; i < all_entities.size(); ++i) {
                    if (kg_module->exists(all_entities[i])) {
                        entity_path += kg_module->getType(all_entities[i]);
                        if (i < all_entities.size() - 1) entity_path += " > ";
                    }
                }
                label("ENTITY: " + entity_path, 255, 255, 255);

                // Show IDs
                std::string id_str = "IDS: ";
                for (size_t i = 0; i < all_entities.size(); ++i) {
                    id_str += std::to_string(all_entities[i]);
                    if (i < all_entities.size() - 1) id_str += ", ";
                }
                label(id_str, 200, 200, 200);
            } else {
                // Single entity - show normally
                label("ENTITY: " + kg_module->getType(entity_id), 255, 255, 255);
                label("ID: " + std::to_string(entity_id), 255, 255, 255);
            }
        } else {
            // Hover mode or no anchored particle - show single entity
            label("ENTITY: " + kg_module->getType(entity_id), 255, 255, 255);
            label("ID: " + std::to_string(entity_id), 255, 255, 255);
        }

        // Particles - get KG particles recursively (handles hierarchical entities)
        auto kg_particles = kg_module->getEntityKGParticlesRecursive(entity_id);
        std::vector<kg::RenderIndex> particles;
        for (kg::KGParticleID kg_id : kg_particles) {
            kg::RenderIndex render_idx = kg_module->getRenderIndex(kg_id);
            if (render_idx != kg::INVALID_RENDER_INDEX) {
                particles.push_back(render_idx);
            }
        }
        label("PARTICLES: " + std::to_string(particles.size()), 150, 255, 150);

        if (!particles.empty()) {
            std::string ids_str = "  IDS: ";
            for (size_t i = 0; i < std::min(size_t(3), particles.size()); i++) {
                ids_str += std::to_string(particles[i]) + " ";
            }
            if (particles.size() > 3) ids_str += "...";
            label(ids_str, 200, 255, 200);
        }
        
        // Show clicked surface and particle geometry information
        if (inspector_.anchored_particle_id >= 0 && engine) {
            spacing(5);
            separator(current_x(), current_y(), 280);
            spacing(5);
            
            // Get particle information
            const auto particle = engine->get_particle_copy(inspector_.anchored_particle_id);

            // Show particle center
            label("PARTICLE CENTER:", 255, 200, 150);
            char pos_str[128];
            snprintf(pos_str, sizeof(pos_str), "  (%.2f, %.2f, %.2f)",
                     particle.x, particle.y, particle.z);
            label(pos_str, 200, 200, 200);

            // Show particle size
            snprintf(pos_str, sizeof(pos_str), "SIZE: %.2f", particle.size);
            label(pos_str, 200, 200, 200);
        }
        
        spacing(5);
        separator(current_x(), current_y(), 280);
        spacing(8);

        // Properties (from KG)
        label("PROPERTIES:", 255, 200, 100);
        // TODO: KG doesn't have getAllProperties yet - need to add this to KGModule API
        // For now, check common properties
        const std::vector<std::string> prop_keys = {
            "height", "crown_radius", "trunk_diameter", "level", "angle", "length"
        };
        int prop_count = 0;
        for (const auto& key : prop_keys) {
            auto value = kg_module->getProperty(entity_id, key);
            if (!value.empty()) {
                std::string prop_text = "  " + key + ": " + value;
                label(prop_text, 220, 220, 150);
                prop_count++;
            }
        }
        if (prop_count == 0) {
            label("  (none)", 100, 100, 100);
        }

        spacing(5);
        separator(current_x(), current_y(), 280);
        spacing(8);

        // Relationships (ALL relationships, not just predefined list)
        label("RELATIONSHIPS:", 150, 150, 255);

        // Check all common relationship types
        const std::vector<std::string> rel_types = {
            "HAS_PART", "near", "CONTAINS", "bears", "guards", "tempts", "knows",
            "follows", "ILLUMINATES", "MANAGES"
        };

        int rel_count = 0;
        for (const auto& rel_type : rel_types) {
            auto related = kg_module->getRelated(entity_id, rel_type);
            for (size_t i = 0; i < related.size() && rel_count < 10; ++i) {
                std::string rel_text = "  " + rel_type + " -> ";
                if (kg_module->exists(related[i])) {
                    rel_text += kg_module->getType(related[i]);
                    rel_text += " (#" + std::to_string(related[i]) + ")";
                }
                label(rel_text, 200, 200, 255);
                rel_count++;
            }
        }

        if (rel_count == 0) {
            label("  (none)", 100, 100, 100);
        }
    } else if (show_particle_only || hovered_particle_id >= 0) {
        // Show particle info without KG (test mode) or when no entity
        int particle_to_show = inspector_.anchored_particle_id >= 0 ?
                               inspector_.anchored_particle_id : hovered_particle_id;
        
        if (particle_to_show >= 0) {
            label("PARTICLE: " + std::to_string(particle_to_show), 150, 255, 150);
            
            // Show surface index
            int surface_to_show = inspector_.anchored_particle_id >= 0 ?
                                  inspector_.anchored_surface_index : inspector_.hovered_surface_index;
            if (surface_to_show >= 0) {
                label("SURFACE: " + std::to_string(surface_to_show), 150, 255, 150);
            }
            
            // Show particle geometry info if we have the engine
            if (engine && particle_to_show >= 0) {
                const auto particle = engine->get_particle_copy(particle_to_show);

                spacing(5);
                label("CENTER:", 255, 200, 150);
                char pos_str[128];
                snprintf(pos_str, sizeof(pos_str), "  (%.2f, %.2f, %.2f)",
                         particle.x, particle.y, particle.z);
                label(pos_str, 200, 200, 200);

                snprintf(pos_str, sizeof(pos_str), "SIZE: %.2f", particle.size);
                label(pos_str, 200, 200, 200);
            }
        } else {
            label("HOVER OVER PARTICLE", 100, 100, 100);
            label("OR CLICK TO ANCHOR", 100, 100, 100);
        }
    } else {
        label("HOVER OVER PARTICLE", 100, 100, 100);
        label("OR CLICK TO ANCHOR", 100, 100, 100);
    }
    
    pop_layout();
    end_window();
    
    // Update inspector state for future use
    // This code is OBSOLETE - inspector state is managed internally above
    // Keep visibility flag only
    if (inspector_.anchored_particle_id >= 0 || hovered_particle_id >= 0) {
        inspector_.visible = true;
    } else {
        inspector_.visible = false;
    }
}


void UISystem::render_debug_overlay(bool show_grid, bool show_vision, bool show_lights) {
    if (!is_initialized_) {
        return;
    }
    
    debug_.show_grid = show_grid;
    debug_.show_vision_cones = show_vision;
    debug_.show_light_rays = show_lights;
    
    // Grid overlay
    if (show_grid) {
        int grid_size = 50;  // pixels
        
        // Vertical lines
        for (int x = 0; x < config_.screen_width; x += grid_size) {
            draw_line(x, 0, x, config_.screen_height, 100, 100, 100, 50);
        }
        
        // Horizontal lines
        for (int y = 0; y < config_.screen_height; y += grid_size) {
            draw_line(0, y, config_.screen_width, y, 100, 100, 100, 50);
        }
    }
    
    // Vision cones and light rays would be rendered here
    // but they need more game state information
}

void UISystem::render_hexagonal_grid() {
    // Hexagonal grid with "flat-top" orientation where:
    // - Flat-to-flat distance (width) = 1.0m (distance between parallel sides)
    // - Vertex-to-vertex distance = width × 2/√3 ≈ 1.155m
    // - Side length = width / √3 ≈ 0.577m
    
    const float HEX_WIDTH = 1.0f;  // Flat-to-flat distance in world units
    const float HEX_HEIGHT = HEX_WIDTH * 0.866f;  // sqrt(3)/2
    const float HEX_SIDE = HEX_WIDTH / 1.732f;  // 1/sqrt(3)
    
    // Get camera position to determine visible range
    float cam_x, cam_y, cam_z;
    (*camera_).get_position(cam_x, cam_y, cam_z);
    
    // Calculate visible range (draw hexagons within ~20m of camera)
    const float GRID_RANGE = 20.0f;
    int grid_min_x = static_cast<int>((cam_x - GRID_RANGE) / HEX_WIDTH) - 1;
    int grid_max_x = static_cast<int>((cam_x + GRID_RANGE) / HEX_WIDTH) + 1;
    int grid_min_y = static_cast<int>((cam_y - GRID_RANGE) / HEX_HEIGHT) - 1;
    int grid_max_y = static_cast<int>((cam_y + GRID_RANGE) / HEX_HEIGHT) + 1;
    
    // Draw hexagonal grid
    for (int row = grid_min_y; row <= grid_max_y; row++) {
        for (int col = grid_min_x; col <= grid_max_x; col++) {
            // Calculate hexagon center in world space
            // For proper tessellation with flat-to-flat = 1.0m
            float center_x = col * HEX_WIDTH;  // Each column is 1.0m apart
            float center_y = row * HEX_HEIGHT;  // Each row is 0.866m apart
            
            // Offset every other row by half width for hexagonal packing
            // Use proper modulo that handles negative numbers correctly
            // In C++, -1 % 2 = -1, but we want it to behave like mathematical modulo
            int row_mod = ((row % 2) + 2) % 2;  // Ensures positive result
            if (row_mod == 1) {
                center_x += HEX_WIDTH * 0.5f;
            }
            float center_z = 0.0f;  // Grid at ground level
            
            // Calculate distance from camera for fading
            float dist_from_cam = std::sqrt((center_x - cam_x) * (center_x - cam_x) + 
                                           (center_y - cam_y) * (center_y - cam_y));
            if (dist_from_cam > GRID_RANGE) continue;
            
            // Fade alpha based on distance
            float alpha = 1.0f - (dist_from_cam / GRID_RANGE);
            alpha = alpha * 0.15f;  // Make grid very subtle - just a guide
            if (alpha < 0.02f) continue;
            
            // Calculate the 6 vertices of the hexagon
            // For flat-top hexagon with flat-to-flat = 1.0m, vertex radius = 1.0/√3
            const float VERTEX_RADIUS = HEX_WIDTH / 1.732f;  // 1/sqrt(3) ≈ 0.577
            float vertices_x[6], vertices_y[6];
            for (int i = 0; i < 6; i++) {
                float angle = i * 60.0f * 3.14159f / 180.0f + (30.0f * 3.14159f / 180.0f);  // Start at 30°
                vertices_x[i] = center_x + VERTEX_RADIUS * std::cos(angle);
                vertices_y[i] = center_y + VERTEX_RADIUS * std::sin(angle);
            }
            
            // Draw the hexagon edges using 3D lines for proper perspective
            // Light grey color for visibility against black background
            unsigned char base_intensity = 160;  // Light grey base
            unsigned char r = base_intensity;
            unsigned char g = base_intensity;
            unsigned char b = base_intensity;
            unsigned char final_alpha = static_cast<unsigned char>(alpha * 255);
            
            // Draw directly in 3D space - let the projection handle perspective
            for (int i = 0; i < 6; i++) {
                int next = (i + 1) % 6;
                ui_draw_line_3d(vertices_x[i], vertices_y[i], center_z,
                                            vertices_x[next], vertices_y[next], center_z,
                                            r, g, b, final_alpha);
            }
        }
    }
}

void UISystem::render_world_compass() {
    // Draw a compass IN THE WORLD using actual 3D->2D projection
    // This shows if the axes are correctly oriented

    // Origin point (center of compass)
    float origin_x = -20.0f;  // Place it to the left of the character
    float origin_y = 0.0f;
    float origin_z = 0.0f;

    // Compass arm length
    float length = 5.0f;

    // Convert world positions to screen using current projection
    int center_sx, center_sy;
    ui_world_to_screen_3d(origin_x, origin_y, origin_z, center_sx, center_sy);

    // North (+Y) - Should point "up" on screen in correct projection
    int north_sx, north_sy;
    ui_world_to_screen_3d(origin_x, origin_y + length, origin_z, north_sx, north_sy);

    // South (-Y) - Should point "down" on screen
    int south_sx, south_sy;
    ui_world_to_screen_3d(origin_x, origin_y - length, origin_z, south_sx, south_sy);

    // East (+X) - Should point "right" on screen
    int east_sx, east_sy;
    ui_world_to_screen_3d(origin_x + length, origin_y, origin_z, east_sx, east_sy);

    // West (-X) - Should point "left" on screen
    int west_sx, west_sy;
    ui_world_to_screen_3d(origin_x - length, origin_y, origin_z, west_sx, west_sy);

    // Up (+Z) - Should point "up" vertically
    int up_sx, up_sy;
    ui_world_to_screen_3d(origin_x, origin_y, origin_z + length, up_sx, up_sy);

    // Draw the compass lines
    // North-South axis (Y)
    ui_draw_line(south_sx, south_sy, north_sx, north_sy, 255, 255, 255);
    // East-West axis (X)
    ui_draw_line(west_sx, west_sy, east_sx, east_sy, 255, 255, 255);
    // Up axis (Z) - vertical line
    ui_draw_line(center_sx, center_sy, up_sx, up_sy, 255, 255, 255);

    // Draw labels at the ends
    ui_draw_string(north_sx - 10, north_sy - 10, "N+Y", 0, 255, 0);
    ui_draw_string(south_sx - 10, south_sy + 5, "S-Y", 255, 0, 0);
    ui_draw_string(east_sx + 5, east_sy - 3, "E+X", 255, 255, 0);
    ui_draw_string(west_sx - 25, west_sy - 3, "W-X", 0, 255, 255);
    ui_draw_string(up_sx - 10, up_sy - 10, "+Z", 255, 0, 255);

    // Draw center origin marker
    // Note: draw_circle removed if not available in RenderSystem
}

void UISystem::render_performance_metrics(const Metrics& metrics) {
    // Performance overlay in top-left corner
    int x = 10;
    int y = 10;
    int line_height = 15;
    
    // Background
    draw_rect(x - 5, y - 5, 200, line_height * 4 + 10, 0, 0, 0, 180);
    
    // Metrics text
    std::stringstream ss;
    
    ss.str("");
    ss << "UI Render: " << std::fixed << std::setprecision(2) << metrics.render_time_ms << " ms";
    draw_text(x, y, ss.str(), 0, 255, 0, 255);
    y += line_height;
    
    ss.str("");
    ss << "Widgets: " << metrics.widgets_rendered;
    draw_text(x, y, ss.str(), 255, 255, 255, 255);
    y += line_height;
    
    ss.str("");
    ss << "Text chars: " << metrics.text_chars_rendered;
    draw_text(x, y, ss.str(), 255, 255, 255, 255);
}

void UISystem::render_engine_debug_overlay(const EngineDebugInfo& info) {
    // EDUCATIONAL NOTE: Proper UI Architecture
    //
    // This method demonstrates proper separation of concerns:
    // - Engine provides data (metrics, state)  
    // - UISystem handles presentation (layout, scaling, colors)
    // - All line spacing and scaling logic is centralized here
    
    if (!engine_) return;
    
    // Calculate scaled line height for consistent spacing
    int line_height = static_cast<int>(10 * ui_scale_multiplier_);
    int y_pos = 10;
    
    // Resolution Manager section - CRITICAL DEBUG INFO
    char res_str[256];
    snprintf(res_str, sizeof(res_str), "RESOLUTION: %s", info.resolution_preset.c_str());
    draw_text(10, y_pos, res_str, 255, 200, 100);  // Orange header
    y_pos += line_height;
    
    // Render buffer info
    snprintf(res_str, sizeof(res_str), "  Render: %dx%d (scale: %.2fx)",
             info.render_width, info.render_height, info.render_scale);
    draw_text(10, y_pos, res_str, 200, 255, 200);  // Light green
    y_pos += line_height;
    
    // Window buffer info
    snprintf(res_str, sizeof(res_str), "  Window: %dx%d (logical)",
             info.window_width, info.window_height);
    draw_text(10, y_pos, res_str, 200, 200, 255);  // Light blue
    y_pos += line_height;
    
    // Framebuffer info 
    snprintf(res_str, sizeof(res_str), "  Framebuffer: %dx%d (DPI: %.1fx)",
             info.framebuffer_width, info.framebuffer_height, info.dpi_scale);
    draw_text(10, y_pos, res_str, 255, 200, 255);  // Light magenta
    y_pos += line_height;
    
    // Aspect ratio analysis
    float render_aspect = static_cast<float>(info.render_width) / info.render_height;
    float fb_aspect = static_cast<float>(info.framebuffer_width) / info.framebuffer_height;
    snprintf(res_str, sizeof(res_str), "  Aspect: render=%.3f fb=%.3f %s",
             render_aspect, fb_aspect, 
             std::abs(render_aspect - fb_aspect) > 0.01f ? "MISMATCH!" : "matched");
    draw_text(10, y_pos, res_str, 
              std::abs(render_aspect - fb_aspect) > 0.01f ? 255 : 150,
              std::abs(render_aspect - fb_aspect) > 0.01f ? 100 : 150,
              std::abs(render_aspect - fb_aspect) > 0.01f ? 100 : 150);
    y_pos += line_height;
    
    // Performance metrics section - Main FPS
    char fps_str[128];
    snprintf(fps_str, sizeof(fps_str), "FPS: %d (min:%d max:%d)", 
             static_cast<int>(info.current_fps),
             static_cast<int>(info.min_fps),
             static_cast<int>(info.max_fps));
    draw_text(10, y_pos, fps_str, 255, 255, 255);
    y_pos += line_height;
    
    // Frame timing breakdown
    char frame_str[128];
    snprintf(frame_str, sizeof(frame_str), "FRAME: %.1fms (I:%.1f P:%.1f L:%.1f R:%.1f)", 
             info.total_frame_time,
             info.input_time,
             info.physics_time,
             info.lighting_time,
             info.render_time);
    draw_text(10, y_pos, frame_str, 255, 255, 255);
    y_pos += line_height;
    
    // Render pipeline breakdown will be shown below with proper hierarchy
    
    // Render pipeline breakdown - hierarchical view
    char render_str[128];
    snprintf(render_str, sizeof(render_str), "RENDER PIPELINE: %.1fms total",
             info.render_particles_time);
    draw_text(10, y_pos, render_str, 255, 200, 100);
    y_pos += line_height;
    
    // Render pipeline breakdown - focus on what matters
    if (info.render_rasterization_time > 0.01) {
        snprintf(render_str, sizeof(render_str), "  Rasterization: %.1fms",
                 info.render_rasterization_time);
        draw_text(10, y_pos, render_str, 255, 255, 150);
        y_pos += line_height;
        
        // Show breakdown of rasterization time
        if (info.pixel_lighting_time > 0.01) {
            double pct = (info.pixel_lighting_time / info.render_rasterization_time) * 100;
            snprintf(render_str, sizeof(render_str), "    Lighting: %.1fms (%.0f%%)",
                     info.pixel_lighting_time, pct);
            draw_text(10, y_pos, render_str, 255, 150, 150);
            y_pos += line_height;
            
            // Show detailed lighting breakdown under Rasterization > Lighting
            double total_breakdown = info.light_surface_fetch_time + info.light_uv_to_world_time + 
                                    info.light_shadow_ray_time + info.light_tone_mapping_time;
            
            if (total_breakdown > 0.001) {
                // Surface fetching time
                if (info.light_surface_fetch_time > 0.0001) {
                    double pct = (info.light_surface_fetch_time / total_breakdown) * 100;
                    snprintf(render_str, sizeof(render_str), "      Surface Fetch: %.3fms (%.0f%%)",
                             info.light_surface_fetch_time, pct);
                    draw_text(10, y_pos, render_str, 200, 200, 150);
                    y_pos += line_height;
                }
                
                // UV to world conversion time - THE BIG BOTTLENECK AT 96%!
                if (info.light_uv_to_world_time > 0.0001) {
                    double pct = (info.light_uv_to_world_time / total_breakdown) * 100;
                    snprintf(render_str, sizeof(render_str), "      UV→World: %.3fms (%.0f%%)",
                             info.light_uv_to_world_time, pct);
                    draw_text(10, y_pos, render_str, 255, 100, 100);  // Red to highlight bottleneck
                    y_pos += line_height;
                    
                    // Show UV cache statistics if enabled
                    if (Optimizations::USE_UV_CACHE) {
                        UVCache& cache = UVCache::get();
                        float hit_rate = cache.get_hit_rate() * 100.0f;
                        snprintf(render_str, sizeof(render_str), "        Cache: %.0f%% hits (%d/%d)",
                                 hit_rate, cache.get_hits(), cache.get_lookups());
                        draw_text(10, y_pos, render_str, 100, 255, 100);  // Green for cache stats
                        y_pos += line_height;
                    }
                }
                
                // Shadow ray testing
                if (info.light_shadow_ray_time > 0.0001) {
                    double pct = (info.light_shadow_ray_time / total_breakdown) * 100;
                    snprintf(render_str, sizeof(render_str), "      Shadow Rays: %.3fms (%.0f%%)",
                             info.light_shadow_ray_time, pct);
                    draw_text(10, y_pos, render_str, 200, 150, 150);
                    y_pos += line_height;
                }
                
                // Tone mapping time
                if (info.light_tone_mapping_time > 0.0001) {
                    double pct = (info.light_tone_mapping_time / total_breakdown) * 100;
                    snprintf(render_str, sizeof(render_str), "      Tone Mapping: %.3fms (%.0f%%)",
                             info.light_tone_mapping_time, pct);
                    draw_text(10, y_pos, render_str, 150, 150, 200);
                    y_pos += line_height;
                }
            }
        }
        
        if (info.pixel_depth_time > 0.01) {
            double pct = (info.pixel_depth_time / info.render_rasterization_time) * 100;
            snprintf(render_str, sizeof(render_str), "    Depth: %.1fms (%.0f%%)",
                     info.pixel_depth_time, pct);
            draw_text(10, y_pos, render_str, 150, 255, 150);
            y_pos += line_height;
        }
        
        // Removed Edge/UV display - misleading since pixel_lighting_time and pixel_depth_time are 0
        // The actual UV time is shown in console output via RENDER PROFILING
    }
    
    // Pixel-level breakdown
    snprintf(render_str, sizeof(render_str), "PIXELS: %d tested, %d rejected, %d drawn",
             info.pixels_tested, info.pixels_rejected, info.pixels_drawn);
    draw_text(10, y_pos, render_str, 255, 255, 100);
    y_pos += line_height;
    
    // Simplified pixel processing metrics
    if (info.render_rasterization_time > 0.01 || info.pixels_tested > 0) {
        snprintf(render_str, sizeof(render_str), "PIXEL PROCESSING:");
        draw_text(10, y_pos, render_str, 255, 200, 100);
        y_pos += line_height;
        
        // Show pixel counts
        snprintf(render_str, sizeof(render_str), "  Pixels/frame: %d", info.pixels_drawn);
        draw_text(10, y_pos, render_str, 180, 180, 180);
        y_pos += line_height;
        
        // Show lighting performance
        if (info.light_call_count > 0) {
            double us_per_call = (info.lighting_time * 1000.0) / info.light_call_count;
            snprintf(render_str, sizeof(render_str), "  Lighting: %.1fms (%.2fμs/pixel)",
                     info.lighting_time, us_per_call);
            draw_text(10, y_pos, render_str, 180, 180, 180);
            y_pos += line_height;
        }
        
        // Show shadow ray count if we have it
        if (info.light_ray_count > 0) {
            snprintf(render_str, sizeof(render_str), "  Shadow rays: %d/frame", 
                     info.light_ray_count);
            draw_text(10, y_pos, render_str, 150, 150, 150);
            y_pos += line_height;
            
            // BVH metrics - show if BVH is enabled or if we have any rays
            if (info.bvh_enabled || info.bvh_rays_traced > 0 || info.linear_rays_traced > 0) {
                // BVH status line
                if (info.bvh_built) {
                    snprintf(render_str, sizeof(render_str), "      BVH: ACTIVE (built %.2fms)",
                             info.bvh_build_time);
                    draw_text(10, y_pos, render_str, 100, 255, 100);  // Green for active
                } else {
                    snprintf(render_str, sizeof(render_str), "      BVH: %s",
                             info.bvh_enabled ? "ENABLED (not built)" : "DISABLED");
                    draw_text(10, y_pos, render_str, 255, 100, 100);  // Red for inactive
                }
                y_pos += line_height;
                
                // Ray distribution
                int total_rays = info.bvh_rays_traced + info.linear_rays_traced;
                if (total_rays > 0) {
                    float bvh_percent = (info.bvh_rays_traced * 100.0f) / total_rays;
                    snprintf(render_str, sizeof(render_str), "      Rays: %d BVH (%.0f%%), %d linear",
                             info.bvh_rays_traced, bvh_percent, info.linear_rays_traced);
                    draw_text(10, y_pos, render_str, 150, 150, 150);
                    y_pos += line_height;
                    
                    // BVH efficiency - nodes visited per ray
                    if (info.bvh_rays_traced > 0) {
                        float nodes_per_ray = (float)info.bvh_nodes_visited / info.bvh_rays_traced;
                        snprintf(render_str, sizeof(render_str), "      Nodes/ray: %.1f (%d total)",
                                 nodes_per_ray, info.bvh_nodes_visited);
                        draw_text(10, y_pos, render_str, 150, 150, 150);
                        y_pos += line_height;
                    }
                }
            }
            
        }
    }
    
    // Game state section  
    char particles_str[128];
    snprintf(particles_str, sizeof(particles_str), "PARTICLES: %d",
             info.particle_count);
    draw_text(10, y_pos, particles_str, 255, 255, 255);
    y_pos += line_height;
    
    // BVH Status Section - Always show if we have any shadow rays
    if (info.light_ray_count > 0 || info.bvh_enabled) {
        char bvh_str[256];
        
        // Main BVH status
        if (info.bvh_built) {
            snprintf(bvh_str, sizeof(bvh_str), "BVH: ACTIVE (built in %.2fms)", 
                     info.bvh_build_time);
            draw_text(10, y_pos, bvh_str, 100, 255, 100);  // Green
        } else if (info.bvh_enabled) {
            snprintf(bvh_str, sizeof(bvh_str), "BVH: ENABLED (not built)");
            draw_text(10, y_pos, bvh_str, 255, 255, 100);  // Yellow
        } else {
            snprintf(bvh_str, sizeof(bvh_str), "BVH: DISABLED");
            draw_text(10, y_pos, bvh_str, 255, 100, 100);  // Red
        }
        y_pos += line_height;
        
        // Ray statistics
        int total_rays = info.bvh_rays_traced + info.linear_rays_traced;
        if (total_rays > 0) {
            float bvh_percent = (total_rays > 0) ? (info.bvh_rays_traced * 100.0f / total_rays) : 0;
            snprintf(bvh_str, sizeof(bvh_str), "SHADOW RAYS: %d total (%d BVH %.0f%%, %d linear)",
                     total_rays, info.bvh_rays_traced, bvh_percent, info.linear_rays_traced);
            draw_text(10, y_pos, bvh_str, 200, 200, 200);
            y_pos += line_height;
            
            // Efficiency metric
            if (info.bvh_rays_traced > 0) {
                float nodes_per_ray = (float)info.bvh_nodes_visited / info.bvh_rays_traced;
                snprintf(bvh_str, sizeof(bvh_str), "BVH EFFICIENCY: %.1f nodes/ray", nodes_per_ray);
                draw_text(10, y_pos, bvh_str, 150, 150, 150);
                y_pos += line_height;
            }
        }
    }
    
    // UV Coherence Cache Status
    if (info.uv_coherent_hits > 0 || info.uv_coherent_misses > 0) {
        char uv_str[256];
        
        // Calculate hit rate
        uint64_t total_uv = info.uv_coherent_hits + info.uv_coherent_misses;
        float hit_rate = total_uv > 0 ? (100.0f * info.uv_coherent_hits / total_uv) : 0.0f;
        
        // Main UV coherence status
        snprintf(uv_str, sizeof(uv_str), "UV COHERENCE: %.1f%% hit rate", hit_rate);
        
        // Color based on hit rate (green = good, red = bad)
        if (hit_rate > 80.0f) {
            draw_text(10, y_pos, uv_str, 100, 255, 100);  // Green for good
        } else if (hit_rate > 50.0f) {
            draw_text(10, y_pos, uv_str, 255, 255, 100);  // Yellow for okay
        } else {
            draw_text(10, y_pos, uv_str, 255, 100, 100);  // Red for poor
        }
        y_pos += line_height;
        
        // Detailed stats
        snprintf(uv_str, sizeof(uv_str), "  Hits: %llu, Misses: %llu",
                 (unsigned long long)info.uv_coherent_hits,
                 (unsigned long long)info.uv_coherent_misses);
        draw_text(10, y_pos, uv_str, 200, 200, 200);
        y_pos += line_height;
    }
    
    // Pixel to UV Conversion Status
    if (info.pixel_uv_calls_per_frame > 0) {
        char pixel_uv_str[256];
        
        // Main status line
        snprintf(pixel_uv_str, sizeof(pixel_uv_str), "PIXEL→UV: %u calls/frame", 
                 info.pixel_uv_calls_per_frame);
        draw_text(10, y_pos, pixel_uv_str, 255, 200, 100);
        y_pos += line_height;
        
        // Path breakdown
        snprintf(pixel_uv_str, sizeof(pixel_uv_str), "  Fast: %.1f%%, Newton: %.1f%%, Rejected: %.1f%%",
                 info.pixel_uv_fast_rate,
                 info.pixel_uv_newton_rate,
                 100.0f - info.pixel_uv_fast_rate - info.pixel_uv_newton_rate);
        draw_text(10, y_pos, pixel_uv_str, 200, 200, 200);
        y_pos += line_height;
    }
    
    draw_text(10, y_pos, "MEMORY: " + std::to_string(info.memory_size), 255, 255, 255);
    y_pos += line_height;
    
    draw_text(10, y_pos, "UI SCALE: " + std::to_string(ui_scale_multiplier_).substr(0,4) + "x (+/-)", 255, 255, 0);
    y_pos += line_height;
    
    // Character position (if available)
    if (info.has_character) {
        draw_text(10, y_pos, "POS: X:" + std::to_string(static_cast<int>(info.char_x)) + 
                  " Y:" + std::to_string(static_cast<int>(info.char_y)) +
                  " Z:" + std::to_string(static_cast<int>(info.char_z)), 0, 255, 0);
        y_pos += line_height;
    }
    
    // Add spacing before controls
    y_pos += line_height;
    
    // Controls section
    draw_text(10, y_pos, "=== CONTROLS ===", 255, 255, 0);
    y_pos += line_height;
    draw_text(10, y_pos, "WASD: Move character", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "Mouse: Look direction", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "Click: Paint particles", 200, 200, 200);
    y_pos += line_height;
    
    // Add spacing
    y_pos += line_height;
    
    // Spawn section
    draw_text(10, y_pos, "=== SPAWN ===", 255, 255, 0);
    y_pos += line_height;
    draw_text(10, y_pos, "1: Single particle", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "2: Serpent chain", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "3: Wall", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "4: Room", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "5: Forest", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "6: Light source", 200, 200, 200);
    y_pos += line_height;
    
    // Add spacing
    y_pos += line_height;
    
    // Debug section
    draw_text(10, y_pos, "=== DEBUG ===", 255, 255, 0);
    y_pos += line_height;
    draw_text(10, y_pos, "`: Toggle this overlay", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "V: Toggle vision cone", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "L: Toggle line-of-sight", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "B: Toggle binocular vision", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "C: Clear particles", 200, 200, 200);
    y_pos += line_height;
    draw_text(10, y_pos, "T: Light test scenario", 200, 200, 200);
    y_pos += line_height;
    
    // Add spacing
    y_pos += line_height;
    
    // Projection info
    draw_text(10, y_pos, "PROJECTION: " + info.projection_mode, 255, 255, 0);
    y_pos += line_height;
    draw_text(10, y_pos, "Press P to cycle", 200, 200, 200);
}

void UISystem::show_tooltip(int x, int y, const std::string& text) {
    tooltip_.visible = true;
    tooltip_.x = x;
    tooltip_.y = y;
    tooltip_.text = text;
    tooltip_.show_time = 0.0f;
}

void UISystem::hide_tooltip() {
    tooltip_.visible = false;
}

void UISystem::draw_text(int x, int y, const std::string& text,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!engine_) {
        return;
    }

    // Rasterize logical UI coords at content_scale * multiplier
    // (same as draw_rect does)
    const float eff = content_scale_ * ui_scale_multiplier_;
    int scaled_x = static_cast<int>(x * eff);
    int scaled_y = static_cast<int>(y * eff);

    if (eff > 1.0f) {
        ui_draw_string_scaled(scaled_x, scaled_y, text, r, g, b, eff);
    } else {
        ui_draw_string(scaled_x, scaled_y, text, r, g, b);
    }

    metrics_.text_chars_rendered += text.length();
}

void UISystem::draw_text_box(int x, int y, int width, int height,
                             const std::string& text, bool centered) {
    // Draw background
    draw_rect(x, y, width, height, 40, 40, 40, 200);
    draw_rect_outline(x, y, width, height, 255, 255, 255, 255);
    
    // Draw text
    if (centered) {
        int text_width = text.length() * 8;  // Approximate
        int text_x = x + (width - text_width) / 2;
        int text_y = y + height / 2 - 8;
        draw_text(text_x, text_y, text, 255, 255, 255, 255);
    } else {
        draw_text(x + 5, y + 5, text, 255, 255, 255, 255);
    }
}

// OLD IMMEDIATE MODE VERSION - REMOVED
// (Replaced by new retained mode handle_mouse_move below)

bool UISystem::handle_mouse_click(int x, int y, int button) {
    // Check if click is on UI elements
    // Return true if event was consumed
    
    // Check inspector panel
    if (inspector_.visible) {
        int panel_x = config_.screen_width - 310;
        int panel_y = 10;
        int panel_width = 300;
        int panel_height = 400;
        
        if (x >= panel_x && x <= panel_x + panel_width &&
            y >= panel_y && y <= panel_y + panel_height) {
            // Handle click on inspector
            // Could add close button, tabs, etc.
            return true;  // Click was on inspector, consume event
        }
    }
    
    return false;  // Event not consumed
}

bool UISystem::handle_key_press(int key) {
    // Handle UI-specific keys
    
    // ESC closes inspector
    if (key == 256 && inspector_.visible) {  // 256 = ESC
        inspector_.visible = false;
        inspector_.anchored_entity_id = -1;
        return true;  // Consumed
    }
    
    return false;  // Not consumed
}

void UISystem::screen_to_ui(int screen_x, int screen_y, int& ui_x, int& ui_y) const {
    // Account for DPI scaling and UI scale multiplier
    float effective_scale = config_.dpi_scale * ui_scale_multiplier_;
    ui_x = static_cast<int>(screen_x / effective_scale);
    ui_y = static_cast<int>(screen_y / effective_scale);
}

void UISystem::ui_to_screen(int ui_x, int ui_y, int& screen_x, int& screen_y) const {
    // Account for DPI scaling and UI scale multiplier
    float effective_scale = config_.dpi_scale * ui_scale_multiplier_;
    screen_x = static_cast<int>(ui_x * effective_scale);
    screen_y = static_cast<int>(ui_y * effective_scale);
}

void UISystem::set_screen_size(int width, int height) {
    config_.screen_width = width;
    config_.screen_height = height;
}

void UISystem::toggle_debug_overlays() {
    // DEBUG: Track toggle calls
    static int toggle_count = 0;
    toggle_count++;

    config_.enable_debug_overlays = !config_.enable_debug_overlays;

    std::cout << "[DEBUG_OVERLAY_TOGGLE] Call #" << toggle_count
              << ": " << (config_.enable_debug_overlays ? "ENABLED" : "DISABLED")
              << std::endl;
}

void UISystem::toggle_compass() {
    if (compass_widget_) {
        bool new_state = !compass_widget_->is_visible();
        compass_widget_->set_visible(new_state);
        std::cout << "[COMPASS] " << (new_state ? "SHOWN" : "HIDDEN") << std::endl;
    }
}

void UISystem::increase_ui_scale() {
    ui_scale_multiplier_ += 0.5f;
    if (ui_scale_multiplier_ > 10.0f) {
        ui_scale_multiplier_ = 10.0f;  // Cap at 10x
    }
    std::cout << "UI Scale: " << ui_scale_multiplier_ << "x" << std::endl;
}

void UISystem::decrease_ui_scale() {
    ui_scale_multiplier_ -= 0.5f;
    if (ui_scale_multiplier_ < 0.5f) {
        ui_scale_multiplier_ = 0.5f;  // Minimum 0.5x
    }
    std::cout << "UI Scale: " << ui_scale_multiplier_ << "x" << std::endl;
}

// Particle inspector functions removed - no longer needed

void UISystem::draw_rect(int x, int y, int width, int height,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!engine_) {
        return;
    }

    // Get actual framebuffer dimensions (not config, which may be stale)
    int fb_width = resolution_->get_framebuffer_width();
    int fb_height = resolution_->get_framebuffer_height();

    // Rasterize logical UI coords at content_scale * multiplier
    const float eff = content_scale_ * ui_scale_multiplier_;
    int scaled_x = static_cast<int>(x * eff);
    int scaled_y = static_cast<int>(y * eff);
    int scaled_width = static_cast<int>(width * eff);
    int scaled_height = static_cast<int>(height * eff);

    // NO ALPHA BLENDING - always write fully opaque pixels
    // This prevents any interaction with world rendering behind UI
    for (int py = scaled_y; py < scaled_y + scaled_height && py < fb_height; py++) {
        for (int px = scaled_x; px < scaled_x + scaled_width && px < fb_width; px++) {
            if (px >= 0 && py >= 0) {
                // Always write with alpha=255 to bypass blending completely
                ui_set_pixel(px, py, r, g, b, 255);
            }
        }
    }
}

void UISystem::draw_rect_outline(int x, int y, int width, int height,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // Rasterize logical UI coords at content_scale * multiplier
    const float eff = content_scale_ * ui_scale_multiplier_;
    int scaled_x = static_cast<int>(x * eff);
    int scaled_y = static_cast<int>(y * eff);
    int scaled_width = static_cast<int>(width * eff);
    int scaled_height = static_cast<int>(height * eff);

    // Top and bottom
    for (int px = scaled_x; px < scaled_x + scaled_width; px++) {
        if (px >= 0 && px < config_.screen_width) {
            if (scaled_y >= 0 && scaled_y < config_.screen_height) {
                ui_set_pixel(px, scaled_y, r, g, b, a);
            }
            if (scaled_y + scaled_height - 1 >= 0 && scaled_y + scaled_height - 1 < config_.screen_height) {
                ui_set_pixel(px, scaled_y + scaled_height - 1, r, g, b, a);
            }
        }
    }
    
    // Left and right
    for (int py = scaled_y; py < scaled_y + scaled_height; py++) {
        if (py >= 0 && py < config_.screen_height) {
            if (scaled_x >= 0 && scaled_x < config_.screen_width) {
                ui_set_pixel(scaled_x, py, r, g, b, a);
            }
            if (scaled_x + scaled_width - 1 >= 0 && scaled_x + scaled_width - 1 < config_.screen_width) {
                ui_set_pixel(scaled_x + scaled_width - 1, py, r, g, b, a);
            }
        }
    }
}

void UISystem::draw_line(int x1, int y1, int x2, int y2,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!engine_) {
        return;
    }
    
    // Apply UI scale multiplier
    int scaled_x1 = static_cast<int>(x1 * ui_scale_multiplier_);
    int scaled_y1 = static_cast<int>(y1 * ui_scale_multiplier_);
    int scaled_x2 = static_cast<int>(x2 * ui_scale_multiplier_);
    int scaled_y2 = static_cast<int>(y2 * ui_scale_multiplier_);
    
    // Bresenham's line algorithm
    int dx = abs(scaled_x2 - scaled_x1);
    int dy = abs(scaled_y2 - scaled_y1);
    int sx = scaled_x1 < scaled_x2 ? 1 : -1;
    int sy = scaled_y1 < scaled_y2 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        if (scaled_x1 >= 0 && scaled_x1 < config_.screen_width && 
            scaled_y1 >= 0 && scaled_y1 < config_.screen_height) {
            ui_set_pixel(scaled_x1, scaled_y1, r, g, b, a);
        }
        
        if (scaled_x1 == scaled_x2 && scaled_y1 == scaled_y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            scaled_x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            scaled_y1 += sy;
        }
    }
}

// EDUCATIONAL NOTE: Sandbox Visual Testing Mode
//
// This is our "sandbox" for visual integration testing. During development, we often need to:
// 1. Verify complex features visually (particles, lighting, KG interactions)
// 2. Run specific test scenarios interactively
// 3. Debug rendering issues that automated tests can't catch
// 4. Validate that automated tests match visual expectations
//
// By embedding this in UISystem, we create a permanent capability for visual testing
// that can be invoked during development, CI/CD visual validation, or user demos.
//
// This follows the principle that testing capabilities should be built into the engine
// from the start, not bolted on later.
void UISystem::render_sandbox_test_menu(bool show_menu) {
    if (!show_menu || !sandbox_.tests_available) {
        sandbox_.menu_visible = false;
        return;
    }
    
    sandbox_.menu_visible = true;
    
    // Position menu in top-right corner with margin
    sandbox_.menu_x = config_.screen_width - sandbox_.menu_width - 20;
    sandbox_.menu_y = 20;
    
    // Create semi-transparent dark background using enhanced pixels
    // Unlike the old Engine approach, we properly use the enhanced pixel system
    if (engine_) {
        // Set object ID for UI elements (sandbox menu gets its own UI ID)
        int sandbox_ui_id = ObjectID::ui_to_object_id(100);  // UI element #100 for sandbox
        current_object_id_ = sandbox_ui_id;
        
        // Draw darkened background using enhanced pixel system
        for (int y = sandbox_.menu_y; y < sandbox_.menu_y + sandbox_.menu_height; y++) {
            for (int x = sandbox_.menu_x; x < sandbox_.menu_x + sandbox_.menu_width; x++) {
                // Get current pixel
                EnhancedPixel current = ui_target_->get_pixel(x, y);
                
                // Darken existing pixel (50% opacity effect) while preserving object tracking
                ui_set_pixel_with_object(
                    x, y, 
                    current.r / 2,      // Darken red
                    current.g / 2,      // Darken green  
                    current.b / 2,      // Darken blue
                    255,                // Full alpha
                    sandbox_ui_id       // Track as sandbox UI element
                );
            }
        }
        
        // Draw menu border using enhanced pixels
        for (int x = sandbox_.menu_x; x < sandbox_.menu_x + sandbox_.menu_width; x++) {
            ui_set_pixel_with_object(x, sandbox_.menu_y, 255, 255, 255, 255, sandbox_ui_id);
            ui_set_pixel_with_object(x, sandbox_.menu_y + sandbox_.menu_height - 1, 255, 255, 255, 255, sandbox_ui_id);
        }
        for (int y = sandbox_.menu_y; y < sandbox_.menu_y + sandbox_.menu_height; y++) {
            ui_set_pixel_with_object(sandbox_.menu_x, y, 255, 255, 255, 255, sandbox_ui_id);
            ui_set_pixel_with_object(sandbox_.menu_x + sandbox_.menu_width - 1, y, 255, 255, 255, 255, sandbox_ui_id);
        }
    }
    
    // Use UI system's text rendering for consistent styling
    int text_x = sandbox_.menu_x + 15;
    int text_y = sandbox_.menu_y + 25;
    int line_height = 25;
    
    // Title
    draw_text(text_x, text_y, "SANDBOX VISUAL TESTING", 255, 255, 0);
    text_y += line_height + 10;
    
    // Separator line
    if (engine_) {
        int separator_ui_id = ObjectID::ui_to_object_id(101);
        current_object_id_ = separator_ui_id;
        for (int x = text_x; x < sandbox_.menu_x + sandbox_.menu_width - 15; x++) {
            ui_set_pixel_with_object(x, text_y, 128, 128, 128, 255, separator_ui_id);
        }
    }
    text_y += 15;
    
    // Test categories
    draw_text(text_x, text_y, "VISUAL INTEGRATION TESTS:", 200, 200, 200);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "F1 - Particle Rendering Tests", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "F2 - 3D Projection Tests", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "F3 - Mouse Interaction Tests", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "F4 - KG Module Tests", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "F5 - Lighting System Tests", 255, 255, 255);
    text_y += line_height;
    
    text_y += 10;
    draw_text(text_x, text_y, "PROJECTION MODES:", 200, 200, 200);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "TAB - Cycle Projection Mode", 255, 255, 255);
    text_y += line_height;
    
    text_y += 10;
    draw_text(text_x, text_y, "DEBUG TOOLS:", 200, 200, 200);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "G - Toggle Grid", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "V - Toggle Vision Cones", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "L - Toggle Light Rays", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "K - Toggle KG Inspector", 255, 255, 255);
    text_y += line_height;
    
    text_y += 10;
    draw_text(text_x, text_y, "CONTROLS:", 200, 200, 200);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "ESC - Hide/Show This Menu", 255, 255, 255);
    text_y += line_height;
    
    draw_text(text_x + 10, text_y, "Q - Quit", 255, 255, 255);
    text_y += line_height;
    
    // Show current test if one is running
    if (!sandbox_.current_test.empty()) {
        text_y += 10;
        draw_text(text_x, text_y, "CURRENT TEST:", 255, 255, 0);
        text_y += line_height;
        draw_text(text_x + 10, text_y, sandbox_.current_test, 0, 255, 0);
    }
    
    // Reset object ID to background after UI rendering
    if (engine_) {
        current_object_id_ = ObjectID::BACKGROUND;
    }
}

// EDUCATIONAL NOTE: Enhanced Pixel Mouse Detection
//
// This is the breakthrough implementation using our enhanced pixel system!
// Instead of complex 3D ray-cube intersection math, we simply:
// 1. Get mouse position from UI input state
// 2. Sample the enhanced pixel at that position
// 3. Extract the object_id and convert to particle_id
//
// This is pixel-perfect, handles all projection modes automatically,
// and is incredibly fast (single pixel lookup vs geometric calculations).
// 
// By placing this in UISystem, we properly separate concerns:
// - UISystem handles all mouse interaction logic
// - Engine just calls UISystem methods
// - Clean separation between rendering and interaction
int UISystem::get_particle_at_mouse() const {
    if (!engine_) {
        return -1;  // No render system available
    }

    // Convert mouse coordinates to framebuffer coordinates
    // UISystem stores the current mouse position in input_state_
    int fb_x, fb_y;
    ui_mouse_to_framebuffer(input_state_.mouse_x, input_state_.mouse_y, fb_x, fb_y);

    // Query the enhanced pixel system for the object at this position
    int object_id = pixel_picker_->get_object_at_pixel(fb_x, fb_y);

    // DEBUG: Log when we find a particle
    static int last_found_particle = -1;
    if (ObjectID::is_particle(object_id)) {
        int particle_id = ObjectID::get_particle_id(object_id);
        if (particle_id != last_found_particle) {
            std::cout << "[UI HOVER] Found particle " << particle_id
                      << " at mouse=(" << input_state_.mouse_x << "," << input_state_.mouse_y << ")"
                      << " fb=(" << fb_x << "," << fb_y << ") object_id=" << object_id << std::endl;
            last_found_particle = particle_id;
        }
    } else if (last_found_particle != -1) {
        std::cout << "[UI HOVER] No particle (was " << last_found_particle << ")" << std::endl;
        last_found_particle = -1;
    }
    
    
    // Check if this is a particle object
    if (ObjectID::is_particle(object_id)) {
        int particle_id = ObjectID::get_particle_id(object_id);
        int surface_index = ObjectID::get_surface_index(object_id);
        
        // Store the hovered surface for the inspector
        const_cast<UISystem*>(this)->inspector_.hovered_surface_index = surface_index;
        
        // Found particle
        
        return particle_id;
    }
    
    
    // Check if it's background
    
    return -1;  // No particle found
}

int UISystem::get_surface_at_mouse() const {
    // Return the last hovered surface index
    return inspector_.hovered_surface_index;
}

// ==========================================
// Test Menu System Implementation
// ==========================================

UISystem::TestMenuSystem::TestMenuSystem() : menu_active_(true), selected_index_(0) {
    // Initialize with empty items
}

void UISystem::TestMenuSystem::move_selection_up() {
    if (selected_index_ > 0) {
        selected_index_--;
    } else if (!items_.empty()) {
        // Wrap around to bottom
        selected_index_ = static_cast<int>(items_.size()) - 1;
    }
}

void UISystem::TestMenuSystem::move_selection_down() {
    if (!items_.empty()) {
        if (selected_index_ < static_cast<int>(items_.size()) - 1) {
            selected_index_++;
        } else {
            // Wrap around to top
            selected_index_ = 0;
        }
    }
}

std::string UISystem::TestMenuSystem::select_current_item() {
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(items_.size())) {
        hide_menu();  // Hide menu when item is selected
        return items_[selected_index_].test_id;
    }
    return "";
}

void UISystem::render_test_selection_menu(TestMenuSystem& menu_system) {
    if (!menu_system.is_menu_active()) {
        return;
    }
    
    // Calculate menu dimensions
    int menu_width = 600;
    int menu_height = 400;
    int menu_x = (config_.screen_width - menu_width) / 2;
    int menu_y = (config_.screen_height - menu_height) / 2;
    
    // Draw menu background with semi-transparent overlay
    // First draw full-screen darkening overlay
    for (int y = 0; y < config_.screen_height; y++) {
        for (int x = 0; x < config_.screen_width; x++) {
            draw_rect(x, y, 1, 1, 0, 0, 0, 128);  // Semi-transparent black
        }
    }
    
    // Draw menu panel
    draw_rect(menu_x, menu_y, menu_width, menu_height, 30, 30, 40, 240);
    draw_rect_outline(menu_x, menu_y, menu_width, menu_height, 100, 100, 120, 255);
    
    // Draw title
    int title_y = menu_y + 20;
    draw_text(menu_x + menu_width/2 - 100, title_y, "SELECT VISUAL TEST", 255, 255, 255);
    
    // Draw separator line
    draw_line(menu_x + 20, title_y + 30, menu_x + menu_width - 20, title_y + 30, 100, 100, 120, 255);
    
    // Draw menu items
    const auto& items = menu_system.get_items();
    int item_y = title_y + 50;
    int item_height = 50;
    int selected = menu_system.get_selected_index();
    
    for (int i = 0; i < static_cast<int>(items.size()); i++) {
        int item_x = menu_x + 40;
        
        // Highlight selected item
        if (i == selected) {
            draw_rect(menu_x + 20, item_y - 5, menu_width - 40, item_height - 10, 50, 50, 70, 200);
            draw_rect_outline(menu_x + 20, item_y - 5, menu_width - 40, item_height - 10, 100, 150, 255, 255);
        }
        
        // Draw item name
        uint8_t text_r = (i == selected) ? 255 : 200;
        uint8_t text_g = (i == selected) ? 255 : 200;
        uint8_t text_b = (i == selected) ? 255 : 200;
        
        draw_text(item_x, item_y, items[i].name, text_r, text_g, text_b);
        
        // Draw item description (smaller, below name)
        draw_text(item_x + 20, item_y + 20, items[i].description, 150, 150, 170);
        
        item_y += item_height;
    }
    
    // Draw instructions at bottom
    int instructions_y = menu_y + menu_height - 40;
    draw_text(menu_x + 20, instructions_y, "↑↓ Navigate    ENTER Select    ESC Cancel", 150, 150, 170);
}

bool UISystem::render_menu_return_button() {
    // Draw a small "Menu" button in the top-right corner
    int button_width = 80;
    int button_height = 30;
    int button_x = config_.screen_width - button_width - 20;
    int button_y = 20;
    
    // Check if mouse is over button
    bool hovered = (input_state_.mouse_x >= button_x && 
                   input_state_.mouse_x < button_x + button_width &&
                   input_state_.mouse_y >= button_y && 
                   input_state_.mouse_y < button_y + button_height);
    
    // Draw button background
    uint8_t bg_r = hovered ? 70 : 50;
    uint8_t bg_g = hovered ? 70 : 50;
    uint8_t bg_b = hovered ? 90 : 70;
    draw_rect(button_x, button_y, button_width, button_height, bg_r, bg_g, bg_b, 200);
    draw_rect_outline(button_x, button_y, button_width, button_height, 150, 150, 170, 255);
    
    // Draw button text
    uint8_t text_r = hovered ? 255 : 200;
    uint8_t text_g = hovered ? 255 : 200;
    uint8_t text_b = hovered ? 255 : 200;
    draw_text(button_x + 20, button_y + 8, "MENU", text_r, text_g, text_b);
    
    // Check for click
    if (hovered && input_state_.left_button && !input_state_.was_left_pressed) {
        return true;  // Button was clicked
    }
    
    return false;
}

// ========== RETAINED MODE IMPLEMENTATION ==========

// Widget management
void UISystem::add_widget(ui::Widget* widget) {
    if (!widget) return;
    
    // Add to root widgets if it has no parent
    if (!widget->get_parent()) {
        root_widgets_.push_back(widget);
    }
}

void UISystem::remove_widget(ui::Widget* widget) {
    if (!widget) return;
    
    auto it = std::find(root_widgets_.begin(), root_widgets_.end(), widget);
    if (it != root_widgets_.end()) {
        root_widgets_.erase(it);
    }
}

void UISystem::clear_widgets() {
    root_widgets_.clear();
    owned_widgets_.clear();
    hovered_widget_ = nullptr;
    focused_widget_ = nullptr;
    pressed_widget_ = nullptr;
}

ui::Widget* UISystem::find_widget(const std::string& id) {
    for (auto* widget : root_widgets_) {
        if (widget->get_id() == id) {
            return widget;
        }
        // Recursive search in children
        ui::Widget* found = widget->find_child(id);
        if (found) return found;
    }
    return nullptr;
}

// Factory methods
ui::Window* UISystem::create_window(const std::string& id, int x, int y, int width, int height, const std::string& title) {
    auto window = std::make_unique<ui::Window>(title, id);
    window->set_bounds(x, y, width, height);
    
    ui::Window* ptr = window.get();
    owned_widgets_.push_back(std::move(window));
    add_widget(ptr);
    
    return ptr;
}

ui::Panel* UISystem::create_panel(const std::string& id, int x, int y, int width, int height) {
    auto panel = std::make_unique<ui::Panel>(id);
    panel->set_bounds(x, y, width, height);
    
    ui::Panel* ptr = panel.get();
    owned_widgets_.push_back(std::move(panel));
    add_widget(ptr);
    
    return ptr;
}

ui::ListMenu* UISystem::create_list_menu(const std::string& id) {
    auto menu = std::make_unique<ui::ListMenu>(id);
    
    ui::ListMenu* ptr = menu.get();
    ptr->set_ui_system(this);  // Give widget access to UI system
    owned_widgets_.push_back(std::move(menu));
    add_widget(ptr);
    
    return ptr;
}

// Event routing
bool UISystem::handle_mouse_move(int x, int y) {
    // Update input state
    input_state_.mouse_x = x;
    input_state_.mouse_y = y;
    
    // Update hover state
    update_hover_state(x, y);
    
    // Route to pressed widget if dragging
    if (pressed_widget_) {
        ui::MouseEvent event;
        event.x = x;
        event.y = y;
        event.button = 0;
        event.pressed = true;
        
        ui::Rectangle bounds = pressed_widget_->get_absolute_bounds();
        event.local_x = x - bounds.x;
        event.local_y = y - bounds.y;
        
        return pressed_widget_->on_mouse_move(event);
    }
    
    // Route to hovered widget
    if (hovered_widget_) {
        ui::MouseEvent event;
        event.x = x;
        event.y = y;
        event.button = -1;
        event.pressed = false;
        
        ui::Rectangle bounds = hovered_widget_->get_absolute_bounds();
        event.local_x = x - bounds.x;
        event.local_y = y - bounds.y;
        
        return hovered_widget_->on_mouse_move(event);
    }
    
    return false;
}

bool UISystem::handle_mouse_down(int x, int y, int button) {
    // DEBUG: Track mouse clicks
    std::cout << "[UISYSTEM_MOUSE] Mouse down at (" << x << ", " << y << ") button=" << button << std::endl;

    // Find widget at position
    ui::Widget* target = hit_test_widgets(x, y);

    std::cout << "[UISYSTEM_MOUSE] Hit test result: " << (target ? target->get_id() : "nullptr") << std::endl;

    if (target) {
        // Set focus
        set_focused_widget(target);

        // Set pressed state
        if (button == 0) {  // Left button
            pressed_widget_ = target;
            target->set_pressed(true);
        }

        // Send event
        ui::MouseEvent event;
        event.x = x;
        event.y = y;
        event.button = button;
        event.pressed = true;

        ui::Rectangle bounds = target->get_absolute_bounds();
        event.local_x = x - bounds.x;
        event.local_y = y - bounds.y;

        return target->on_mouse_down(event);
    } else {
        // Click outside widgets - clear focus (Phase 3)
        std::cout << "[UISYSTEM_MOUSE] Click outside widgets - clearing focus" << std::endl;
        set_focused_widget(nullptr);
    }

    return false;
}

bool UISystem::handle_mouse_up(int x, int y, int button) {
    bool handled = false;
    
    if (pressed_widget_ && button == 0) {
        // Send mouse up event
        ui::MouseEvent event;
        event.x = x;
        event.y = y;
        event.button = button;
        event.pressed = false;
        
        ui::Rectangle bounds = pressed_widget_->get_absolute_bounds();
        event.local_x = x - bounds.x;
        event.local_y = y - bounds.y;
        
        handled = pressed_widget_->on_mouse_up(event);
        
        // Clear pressed state
        pressed_widget_->set_pressed(false);
        pressed_widget_ = nullptr;
    }
    
    return handled;
}

bool UISystem::handle_key_down(int key, bool shift, bool ctrl, bool alt) {
    if (focused_widget_) {
        ui::KeyEvent event;
        event.key = key;
        event.pressed = true;
        event.shift = shift;
        event.ctrl = ctrl;
        event.alt = alt;
        
        return focused_widget_->on_key_down(event);
    }
    return false;
}

bool UISystem::handle_key_up(int key, bool shift, bool ctrl, bool alt) {
    if (focused_widget_) {
        ui::KeyEvent event;
        event.key = key;
        event.pressed = false;
        event.shift = shift;
        event.ctrl = ctrl;
        event.alt = alt;
        
        return focused_widget_->on_key_up(event);
    }
    return false;
}

// Focus management
void UISystem::set_focused_widget(ui::Widget* widget) {
    std::cout << "[UISYSTEM_FOCUS] set_focused_widget called: old="
              << (focused_widget_ ? focused_widget_->get_id() : "nullptr")
              << " new=" << (widget ? widget->get_id() : "nullptr") << std::endl;

    if (focused_widget_ == widget) {
        std::cout << "[UISYSTEM_FOCUS] Already focused - skipping" << std::endl;
        return;
    }

    if (focused_widget_) {
        std::cout << "[UISYSTEM_FOCUS] Calling on_focus_lost on " << focused_widget_->get_id() << std::endl;
        focused_widget_->set_focused(false);
        focused_widget_->on_focus_lost();
    }

    focused_widget_ = widget;

    if (focused_widget_) {
        std::cout << "[UISYSTEM_FOCUS] Calling on_focus_gained on " << focused_widget_->get_id() << std::endl;
        focused_widget_->set_focused(true);
        focused_widget_->on_focus_gained();
    }
}

ui::Widget* UISystem::get_focused_widget() const {
    return focused_widget_;
}

bool UISystem::has_exclusive_input_focus() const {
    // UI has exclusive input focus when a widget is focused and visible
    // This blocks game input (WASD, etc.) when chat is active
    return focused_widget_ != nullptr && focused_widget_->is_visible();
}

bool UISystem::is_point_over_widget(int x, int y) const {
    // Check if a point hits any visible widget (for click blocking)
    for (auto it = root_widgets_.rbegin(); it != root_widgets_.rend(); ++it) {
        ui::Widget* hit = (*it)->hit_test(x, y);
        if (hit) return true;
    }
    return false;
}

// Helper methods
ui::Widget* UISystem::hit_test_widgets(int x, int y) {
    // Test in reverse order (top to bottom)
    for (auto it = root_widgets_.rbegin(); it != root_widgets_.rend(); ++it) {
        ui::Widget* hit = (*it)->hit_test(x, y);
        if (hit) return hit;
    }
    return nullptr;
}

void UISystem::update_hover_state(int x, int y) {
    ui::Widget* new_hover = hit_test_widgets(x, y);
    
    if (new_hover != hovered_widget_) {
        // Send mouse leave to old widget
        if (hovered_widget_) {
            hovered_widget_->set_hovered(false);

            ui::MouseEvent event;
            event.x = x;
            event.y = y;
            hovered_widget_->on_mouse_leave(event);
        }

        hovered_widget_ = new_hover;

        // Send mouse enter to new widget
        if (hovered_widget_) {
            hovered_widget_->set_hovered(true);

            ui::MouseEvent event;
            event.x = x;
            event.y = y;
            hovered_widget_->on_mouse_enter(event);
        }
    }
}

// ===== ENTITY HOVER HIGHLIGHTING =====
// Proper separation of concerns: UISystem handles hover detection and highlighting

void UISystem::update_hover_detection(Engine* engine) {
    if (!engine || !is_initialized_) {
        return;
    }

    // Only run hover detection when debug mode is active
    bool debug_mode = engine->get_show_debug_overlay();
    if (!debug_mode) {
        hovered_entity_id_ = -1;
        should_run_picking_pass_ = false;
        return;
    }

    // Throttle hover detection to 10Hz (every 100ms)
    auto now = Clock::now();
    auto elapsed_ms = std::chrono::duration<double, std::milli>(now - last_hover_check_).count();

    if (elapsed_ms < hover_check_interval_ms_) {
        return;  // Too soon, skip this frame
    }

    last_hover_check_ = now;
    should_run_picking_pass_ = true;  // Signal render phase to run picking pass
}

void UISystem::render_entity_highlighting(Engine* engine) {
    if (!engine || !is_initialized_) {
        return;
    }

    // Only render highlighting when debug mode is active
    bool debug_mode = engine->get_show_debug_overlay();
    if (!debug_mode) {
        hovered_entity_id_ = -1;
        should_run_picking_pass_ = false;
        return;
    }

    // Only run hover detection when throttle check passed (10Hz, not 60Hz)
    if (should_run_picking_pass_) {
        should_run_picking_pass_ = false;  // Consume flag immediately

        // Get mouse position from input state
        const auto& input_state = input_state_;
        int mouse_x = input_state.mouse_x;
        int mouse_y = input_state.mouse_y;

        // Convert to framebuffer coordinates
        int fb_x, fb_y;
        ui_mouse_to_framebuffer(mouse_x, mouse_y, fb_x, fb_y);

        // Get object ID directly from the object_map (populated during normal rendering)
        int object_id = pixel_picker_->get_object_at_pixel(fb_x, fb_y);

        // Check if we're hovering over a particle
        if (ObjectID::is_particle(object_id)) {
            unsigned int particle_id = ObjectID::get_particle_id(object_id);

            // Get the entity for this particle
            kg::EntityID entity_id = engine->get_kg().getEntityByKGParticle(particle_id);

            if (entity_id != kg::INVALID_ENTITY) {
                hovered_entity_id_ = static_cast<int>(entity_id);
            } else {
                hovered_entity_id_ = -1;
            }
        } else {
            hovered_entity_id_ = -1;
        }
    }

    // Now render highlighting if we have a hovered entity
    if (hovered_entity_id_ < 0) {
        return;
    }

    kg::EntityID entity_id = static_cast<kg::EntityID>(hovered_entity_id_);

    // Get all particles in this entity
    const auto& particle_ids = engine->get_kg().getEntityKGParticles(entity_id);
    if (particle_ids.empty()) {
        return;
    }

    // Calculate bounding box that encompasses all particles
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();

    for (unsigned int particle_id : particle_ids) {
        // Get particle copy (thread-safe)
        Particle particle = engine->get_particle_copy(particle_id);

        // Project to screen space using camera system
        int screen_x, screen_y;
        engine->get_camera_system().world_to_screen(
            particle.x, particle.y, particle.z,
            screen_x, screen_y
        );

        // Expand bounding box
        min_x = std::min(min_x, screen_x);
        min_y = std::min(min_y, screen_y);
        max_x = std::max(max_x, screen_x);
        max_y = std::max(max_y, screen_y);
    }

    // Check if we found any visible particles
    if (min_x > max_x || min_y > max_y) {
        return;
    }

    // Add padding for better visibility
    const int padding = 5;

    // Draw entity bounding box in bright cyan
    draw_rect_outline(
        min_x - padding, min_y - padding,
        (max_x - min_x) + padding * 2,
        (max_y - min_y) + padding * 2,
        0, 255, 255, 255  // Bright cyan
    );
}

// Chat window implementation - Phase 2: Widget Conversion
// All methods now delegate to ChatWindow widget

void UISystem::add_chat_message(const std::string& message) {
    if (chat_window_) {
        chat_window_->add_message(message);
    }
}

void UISystem::clear_chat() {
    if (chat_window_) {
        chat_window_->clear_history();
    }
}

void UISystem::set_chat_visible(bool visible) {
    if (chat_window_) {
        chat_window_->set_visible(visible);
    }
}

void UISystem::handle_char_input(unsigned int codepoint) {
    if (chat_window_) {
        chat_window_->handle_char_input(codepoint);
    }
}

void UISystem::handle_text_input_key(int key) {
    if (!chat_window_) return;

    // Handle backspace (GLFW_KEY_BACKSPACE = 259)
    if (key == 259) {
        chat_window_->handle_backspace();
    }
    // Handle Enter (GLFW_KEY_ENTER = 257)
    else if (key == 257) {
        chat_window_->handle_enter();
    }
}

// LLM thinking indicator
void UISystem::set_llm_thinking(bool thinking) {
    if (chat_window_) {
        chat_window_->set_thinking(thinking);
    }
}

void UISystem::set_chat_theme(uint8_t text_r, uint8_t text_g, uint8_t text_b,
                              uint8_t accent_r, uint8_t accent_g,
                              uint8_t accent_b) {
    if (!chat_window_) return;
    chat_window_->set_text_color(text_r, text_g, text_b);
    chat_window_->set_accent_color(accent_r, accent_g, accent_b);
}

bool UISystem::is_llm_thinking() const {
    return chat_window_ ? chat_window_->is_thinking() : false;
}

// Text input methods
std::string UISystem::get_input_text() const {
    return chat_window_ ? chat_window_->get_input_text() : "";
}

void UISystem::clear_input_text() {
    if (chat_window_) {
        chat_window_->clear_input();
    }
}

bool UISystem::has_pending_submit() {
    return chat_window_ ? chat_window_->has_pending_submit() : false;
}

// Removed: set_input_focus(bool) - now use Widget focus system via set_focused_widget()

// Old immediate-mode render_chat_window() functions removed - Phase 2: Widget Conversion
// Chat rendering now handled by ChatWindow widget in widget system

void UISystem::fill_background(uint8_t r, uint8_t g, uint8_t b) {
    if (!is_initialized_ || !engine_) return;

    // Get actual framebuffer dimensions from RenderSystem
    int fb_width = resolution_->get_framebuffer_width();
    int fb_height = resolution_->get_framebuffer_height();

    // Fill entire framebuffer with solid color
    draw_rect(0, 0, fb_width, fb_height, r, g, b, 255);
}
