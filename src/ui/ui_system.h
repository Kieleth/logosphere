#ifndef UI_SYSTEM_H
#define UI_SYSTEM_H

#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <chrono>

// Forward declarations
class EngineRenderState;
class PixelBuffer;
class SparseObjectMap;
class FontRenderer;
class PrimitiveRenderer;
class CoordinateTransformer;
class CameraSystem;
class ResolutionManager;
class IDrawSurface;
namespace Logosphere { namespace Rendering { class PixelPicker; } }
namespace kg {
    class KGModule;
}
namespace ui {
    class Widget;
    class Window;
    class Panel;
    class ListMenu;
}
class ChatWindow;
namespace ui {
    class CompassWidget;
}

// UI System - handles all 2D overlays and interface elements
// Extracted from EngineRenderState to separate world rendering from UI

class UISystem {
public:
    // Configuration for UI system
    struct Config {
        int screen_width;
        int screen_height;
        float dpi_scale;
        bool enable_debug_overlays;
        bool enable_tooltips;
        bool enable_chat_window;
        bool enable_compass;

        Config() : screen_width(1600), screen_height(1200), dpi_scale(1.0f),
                   enable_debug_overlays(false), enable_tooltips(true), enable_chat_window(false),
                   enable_compass(true) {}
    };
    
    // UI metrics for performance tracking
    struct Metrics {
        float render_time_ms = 0.0f;
        int widgets_rendered = 0;
        int text_chars_rendered = 0;
    };
    
    UISystem();
    ~UISystem();
    
    // Initialization and shutdown
    bool initialize(class Engine* engine, const Config& config = Config());
    void shutdown();

    // Attach the engine-owned IDrawSurface UISystem will pass to
    // widgets during render(). Called by Engine right after
    // construction. Until set, UISystem falls back to render_system_
    // (which still implements IDrawSurface during the port).
    void set_draw_surface(IDrawSurface* draw_surface) { draw_surface_ = draw_surface; }
    void set_pixel_picker(Logosphere::Rendering::PixelPicker* picker) { pixel_picker_ = picker; }
    
    // Main update and render
    void update(float delta_time);
    void render();  // Renders all UI elements to framebuffer
    
    // ===== RETAINED MODE API =====
    // Widget management
    void add_widget(ui::Widget* widget);
    void remove_widget(ui::Widget* widget);
    void clear_widgets();
    ui::Widget* find_widget(const std::string& id);
    
    // Factory methods for creating widgets
    ui::Window* create_window(const std::string& id, int x, int y, int width, int height, const std::string& title);
    ui::Panel* create_panel(const std::string& id, int x, int y, int width, int height);
    ui::ListMenu* create_list_menu(const std::string& id);
    
    // Event routing for retained mode
    bool handle_mouse_move(int x, int y);
    // Route a scroll to the widget under the cursor (chat scrollback
    // etc.). Returns true if a widget consumed it.
    bool handle_mouse_scroll(int x, int y, double dx, double dy);
    bool handle_mouse_down(int x, int y, int button);
    bool handle_mouse_up(int x, int y, int button);
    bool handle_key_down(int key, bool shift = false, bool ctrl = false, bool alt = false);
    bool handle_key_up(int key, bool shift = false, bool ctrl = false, bool alt = false);
    
    // Focus management
    void set_focused_widget(ui::Widget* widget);
    ui::Widget* get_focused_widget() const;

    // Input isolation (Phase 3)
    bool has_exclusive_input_focus() const;
    bool is_point_over_widget(int x, int y) const;  // Check if point hits any widget

    // ===== IMMEDIATE MODE API (existing) =====
    
    // UI Primitives - Immediate mode GUI building blocks
    // Windows
    void begin_window(const std::string& id, int x, int y, int width, int height, const std::string& title);
    void end_window();
    
    // Panels
    void begin_panel(int x, int y, int width, int height, uint8_t r = 20, uint8_t g = 20, uint8_t b = 30);
    void end_panel();
    
    // Basic elements
    void label(int x, int y, const std::string& text, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
    void label(const std::string& text, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);  // Uses layout
    bool button(int x, int y, int width, int height, const std::string& text);
    void separator(int x, int y, int width);
    
    // Layout management
    enum class LayoutType { None, Vertical, Horizontal, Grid };
    void push_layout(LayoutType type, int spacing = 5);
    void pop_layout();
    void next_row();
    void same_line();
    void spacing(int pixels);
    int current_x() const { return layout_stack_.empty() ? 0 : layout_stack_.back().cursor_x; }
    int current_y() const { return layout_stack_.empty() ? 0 : layout_stack_.back().cursor_y; }
    
    // High-level components (use primitives internally)
    void render_kg_inspector(kg::KGModule* kg_module, 
                            int hovered_particle_id = -1,
                            int anchored_entity_id = -1,
                            class Engine* engine = nullptr);
    
    // Debug UI elements - moved from Engine for proper separation
    void render_hexagonal_grid();  // Draw hexagonal grid with 1m spacing in world space
    void render_world_compass();   // Draw 3D compass in world space
    void render_debug_overlay(bool show_grid, bool show_vision, bool show_lights);
    void render_performance_metrics(const Metrics& metrics);
    
    // Engine debug overlay - proper separation of concerns
    struct EngineDebugInfo {
        // Performance metrics
        double current_fps;
        double min_fps;
        double max_fps;
        double total_frame_time;
        
        // System timings (in milliseconds)
        double input_time;
        double physics_time;
        double vision_time;
        double lighting_time;
        double render_time;
        double render_clear_time;
        double render_particles_time;
        double ui_time;
        
        // Detailed render pipeline timings
        double render_culling_time;
        double render_projection_time;
        double render_sorting_time;
        double render_rasterization_time;
        
        // Per-pixel breakdown
        double pixel_edge_test_time;
        double pixel_uv_calc_time;
        double pixel_depth_time;
        double pixel_lighting_time;
        double pixel_write_time;
        
        // Lighting subsystem breakdown
        double light_uv_to_world_time;
        double light_intensity_calc_time;
        double light_shadow_ray_time;
        double light_surface_fetch_time;
        double light_tone_mapping_time;
        double light_color_calc_time;
        int light_ray_count;
        int light_call_count;
        
        // BVH metrics
        bool bvh_enabled;
        bool bvh_built;
        int bvh_rays_traced;
        int linear_rays_traced;
        int bvh_nodes_visited;
        double bvh_build_time;
        
        // UV Coherence metrics
        uint64_t uv_coherent_hits;
        uint64_t uv_coherent_misses;
        float uv_coherence_rate;
        
        // Pixel to UV conversion metrics
        uint32_t pixel_uv_calls_per_frame;
        uint32_t pixel_uv_fast_path;
        uint32_t pixel_uv_newton_path;
        float pixel_uv_fast_rate;
        float pixel_uv_newton_rate;
        
        // Resolution and buffer management
        int render_width;
        int render_height;
        int window_width;
        int window_height;
        int framebuffer_width;
        int framebuffer_height;
        float dpi_scale;
        float render_scale;
        std::string resolution_preset;
        
        // Game state
        int particle_count;
        int surfaces_rendered;
        int surfaces_culled;
        int surfaces_projected;
        int pixels_tested;
        int pixels_rejected;
        int pixels_drawn;
        int rays_cast;
        int memory_size;
        bool has_character;
        float char_x, char_y, char_z;
        std::string projection_mode;
    };
    void render_engine_debug_overlay(const EngineDebugInfo& info);
    
    // Sandbox mode for visual testing - comprehensive test menu overlay
    void render_sandbox_test_menu(bool show_menu = true);
    
    // Test selection menu system
    struct MenuItem {
        std::string name;
        std::string description;
        std::string test_id;  // Test function name to call
    };
    
    class TestMenuSystem {
    public:
        TestMenuSystem();
        
        // Menu state
        bool is_menu_active() const { return menu_active_; }
        void show_menu() { menu_active_ = true; }
        void hide_menu() { menu_active_ = false; }
        
        // Menu navigation
        void move_selection_up();
        void move_selection_down();
        int get_selected_index() const { return selected_index_; }
        std::string select_current_item();  // Returns test_id of selected item
        
        // Menu items
        void add_item(const MenuItem& item) { items_.push_back(item); }
        const std::vector<MenuItem>& get_items() const { return items_; }
        void clear_items() { items_.clear(); selected_index_ = 0; }
        
    private:
        bool menu_active_ = true;
        int selected_index_ = 0;
        std::vector<MenuItem> items_;
    };
    
    // Visual test menu interface
    void render_test_selection_menu(TestMenuSystem& menu_system);
    bool render_menu_return_button();  // Returns true if clicked

    // Chat window - simple text display for LLM/game messages (Phase 2: Widget Conversion)
    // Chat rendering now handled by ChatWindow widget (no explicit render calls needed)
    void add_chat_message(const std::string& message);
    void clear_chat();
    void set_chat_visible(bool visible);  // Show/hide chat window

    // LLM thinking indicator - shows visual feedback during generation
    void set_llm_thinking(bool thinking);
    // Chat theme passthrough (text color; accent = border/thinking)
    void set_chat_theme(uint8_t text_r, uint8_t text_g, uint8_t text_b,
                        uint8_t accent_r, uint8_t accent_g, uint8_t accent_b);
    bool is_llm_thinking() const;

    // Text input - for chat/LLM queries
    void handle_char_input(unsigned int codepoint);  // Handle character typing
    void handle_text_input_key(int key);  // Handle special keys (backspace, enter)
    std::string get_input_text() const;
    void clear_input_text();
    bool has_pending_submit();
    // Note: Focus now controlled via Widget focus system (set_focused_widget)

    // Background fill - for games that want solid color backgrounds
    void fill_background(uint8_t r, uint8_t g, uint8_t b);

    // Tooltips
    void show_tooltip(int x, int y, const std::string& text);
    void hide_tooltip();
    
    // Inspector state access
    int get_anchored_particle_id() const { return inspector_.anchored_particle_id; }
    
    // Text rendering
    void draw_text(int x, int y, const std::string& text, 
                   uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255);
    void draw_text_box(int x, int y, int width, int height,
                       const std::string& text, bool centered = false);
    
    // Input handling - Engine should call these with input state
    void update_input_state(int mouse_x, int mouse_y, bool left_button, bool right_button);
    bool handle_mouse_click(int x, int y, int button);
    bool handle_key_press(int key);
    
    // Enhanced pixel mouse interaction - the breakthrough implementation
    int get_particle_at_mouse() const;
    int get_surface_at_mouse() const;  // Get the surface index at mouse position

    // Entity hover highlighting - runs picking pass and highlights hovered entities
    void update_hover_detection(class Engine* engine);  // Call in update phase
    void render_entity_highlighting(class Engine* engine);  // Call in render phase
    
    // Coordinate conversion
    void screen_to_ui(int screen_x, int screen_y, int& ui_x, int& ui_y) const;
    void ui_to_screen(int ui_x, int ui_y, int& screen_x, int& screen_y) const;
    
    // Configuration
    void set_dpi_scale(float scale) { config_.dpi_scale = scale; }
    void set_screen_size(int width, int height);
    void toggle_debug_overlays();
    void toggle_compass();  // Toggle compass visibility
    
    // UI scale control
    void increase_ui_scale();
    void decrease_ui_scale();
    void set_ui_scale(float scale) { ui_scale_multiplier_ = std::max(0.5f, std::min(10.0f, scale)); }
    float get_ui_scale_multiplier() const { return ui_scale_multiplier_; }

    // Content scale: render-buffer pixels per logical window point
    // (render_width / window_width). Fed by Engine::set_render_resolution.
    // UI layout coords stay logical; draw_text/draw_rect rasterize at
    // content_scale_ * ui_scale_multiplier_, so HUD text keeps its
    // on-screen size at retina-native render resolutions.
    void set_content_scale(float scale) { content_scale_ = std::max(0.1f, scale); }
    float get_content_scale() const { return content_scale_; }
    
    // Metrics
    const Metrics& get_metrics() const { return metrics_; }
    
private:
    // Helper functions for KG inspector
    // Particle inspector functions removed - no longer needed
    
    // Helper functions for drawing
    void draw_rect(int x, int y, int width, int height, 
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void draw_rect_outline(int x, int y, int width, int height,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void draw_line(int x1, int y1, int x2, int y2,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    
    // Internal state
    Config config_;
    Metrics metrics_;
    class Engine* engine_ = nullptr;  // Engine handle for accessor-based init.
    // Direct refs to rendering deps, extracted from EngineRenderState at
    // init. UISystem owns its own drawing path: set_pixel hits
    // ui_target_ + object_map_ directly; text goes through font_;
    // primitives through primitives_. Avoids the RS::draw_* wrappers.
    PixelBuffer* ui_target_ = nullptr;
    SparseObjectMap* object_map_ = nullptr;
    FontRenderer* font_ = nullptr;
    PrimitiveRenderer* primitives_ = nullptr;
    CoordinateTransformer* coord_ = nullptr;
    CameraSystem* camera_ = nullptr;
    ResolutionManager* resolution_ = nullptr;
    IDrawSurface* draw_surface_ = nullptr;  // engine-owned; set after init
    Logosphere::Rendering::PixelPicker* pixel_picker_ = nullptr;  // engine-owned
    int current_object_id_ = 0;  // Object ID tagged on UI pixels.

    // Private helpers mirroring what RS::draw_* did. Same semantics
    // (render_buffer_ target, object-id tagging), implemented via the
    // extracted deps instead of hopping through EngineRenderState.
    void ui_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void ui_set_pixel_with_object(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int object_id);
    void ui_draw_char(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b);
    void ui_draw_char_scaled(int x, int y, char c, uint8_t r, uint8_t g, uint8_t b, float scale);
    void ui_draw_string(int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b);
    void ui_draw_string_scaled(int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b, float scale);
    void ui_draw_number(int x, int y, int number, uint8_t r, uint8_t g, uint8_t b) {
        ui_draw_string(x, y, std::to_string(number), r, g, b);
    }
    void ui_draw_line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b);
    void ui_draw_line_3d(float x1, float y1, float z1, float x2, float y2, float z2,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void ui_world_to_screen_3d(float world_x, float world_y, float world_z, int& screen_x, int& screen_y) const;
    void ui_mouse_to_framebuffer(double mouse_x, double mouse_y, int& fb_x, int& fb_y) const;

    bool is_initialized_;
    float ui_scale_multiplier_ = 1.0f;  // UI scale multiplier (MUST be 1.0 or widget hit testing breaks)
    float content_scale_ = 1.0f;        // render px per logical window point (see set_content_scale)
    
    // Tooltip state
    struct {
        bool visible;
        int x, y;
        std::string text;
        float show_time;  // Time tooltip has been shown
    } tooltip_;
    
    // Inspector state
    struct {
        bool visible;
        int anchored_entity_id;
        int anchored_particle_id;  // Track which particle is anchored
        int anchored_surface_index;  // Track which surface of the particle is anchored
        int hovered_particle_id;  // Track what's being hovered
        int hovered_surface_index;  // Track which surface is being hovered
        float animation_time;
    } inspector_;
    
    // Input state (updated by Engine)
    struct {
        int mouse_x, mouse_y;
        bool left_button;
        bool right_button;
        bool was_left_pressed;  // For click detection
        bool was_right_pressed;
    } input_state_;
    
    // Debug overlay state
    struct {
        bool show_grid;
        bool show_vision_cones;
        bool show_light_rays;
        bool show_performance;
    } debug_;
    
    // Sandbox testing mode state
    struct {
        bool menu_visible;
        int menu_x, menu_y;
        int menu_width, menu_height;
        std::string current_test;  // Name of currently running test
        bool tests_available;      // Whether test mode is accessible
    } sandbox_;

    // Chat state moved to ChatWindow widget (Phase 2: Widget Conversion)
    // Old members: chat_messages_, chat_max_messages_, llm_thinking_, llm_thinking_time_, text_input_
    // Now: API methods delegate to chat_window_

    // Window state (for immediate mode)
    struct WindowState {
        std::string id;
        int x, y, width, height;
        std::string title;
        bool is_active;
    };
    WindowState current_window_;
    
    // Layout state
    struct LayoutState {
        LayoutType type;
        int spacing;
        int cursor_x, cursor_y;  // Current position for next element
        int start_x, start_y;    // Where this layout started
        int max_width, max_height;  // Bounds
        int row_height;  // Height of current row (for horizontal layout)
    };
    std::vector<LayoutState> layout_stack_;
    
    // ===== RETAINED MODE STATE =====
    // Root widgets (top-level widgets managed by UISystem)
    std::vector<ui::Widget*> root_widgets_;

    // All widgets we've created (for cleanup)
    std::vector<std::unique_ptr<ui::Widget>> owned_widgets_;

    // Chat window widget (Phase 2: Widget Conversion)
    ChatWindow* chat_window_;

    // Compass widget (draggable, always visible)
    ui::CompassWidget* compass_widget_;

    // Current widget states
    ui::Widget* hovered_widget_ = nullptr;
    ui::Widget* focused_widget_ = nullptr;
    ui::Widget* pressed_widget_ = nullptr;
    
    // Helper methods for retained mode
    ui::Widget* hit_test_widgets(int x, int y);
    void update_hover_state(int x, int y);

    // Hover detection state
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    TimePoint last_hover_check_ = Clock::now();
    const double hover_check_interval_ms_ = 100.0;  // 10Hz hover detection
    int hovered_entity_id_ = -1;  // Currently hovered entity (kg::EntityID)
    bool should_run_picking_pass_ = false;  // Flag set by update, consumed by render
};

// Global UI system instance removed - now accessed via Engine::get_ui_system()

#endif // UI_SYSTEM_H