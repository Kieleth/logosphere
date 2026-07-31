#ifndef KEY_MAPPER_H
#define KEY_MAPPER_H

#include "platform/glfw_compat.h"  // real GLFW, or no-op shim in GLFW-less profiles
#include <unordered_map>
#include <string>
#include <functional>
#include <vector>

// Forward declaration
namespace Logosphere {
    class IApplication;
}

// EDUCATIONAL NOTE: Centralized Input Mapping System
//
// This module provides a configurable key mapping system that separates
// physical keys (what the OS sends us) from game actions (what we want to do).
// 
// Benefits:
// 1. Single source of truth for all key handling
// 2. Configurable key bindings (can be loaded from config file)
// 3. No duplicate key handling across modules
// 4. Clean separation between input and game logic
// 5. Support for key combinations (Shift+1, Ctrl+S, etc.)
//
// This is similar to how professional game engines handle input:
// - Unity has Input.GetAxis("Horizontal") instead of checking specific keys
// - Unreal has Action and Axis mappings
// - We're building something similar but simpler

// Game actions that can be triggered by keys
enum class GameAction {
    // System
    EXIT_GAME,
    TOGGLE_DEBUG_OVERLAY,
    
    // Movement
    MOVE_NORTH,
    MOVE_SOUTH,
    MOVE_WEST,
    MOVE_EAST,
    MOVE_UP,
    MOVE_DOWN,
    
    // Camera/Rendering
    CYCLE_PROJECTION_MODE,
    TOGGLE_ORTHOGRAPHIC,
    CYCLE_UI_MODE,
    TOGGLE_CAMERA_FOLLOW,
    TOGGLE_FREE_CAMERA,  // Toggle free camera mode (WASD+QE movement, mouse look)
    INCREASE_UI_SCALE,
    DECREASE_UI_SCALE,
    DECREASE_RESOLUTION,
    INCREASE_RESOLUTION,
    
    // Debug Visualization
    TOGGLE_VISION_CONE,
    TOGGLE_LINE_OF_SIGHT,
    TOGGLE_BINOCULAR_VISION,
    TOGGLE_RAY_VISUALIZATION,
    TOGGLE_DEADZONE_DEBUG,
    TOGGLE_KG_INSPECTOR,
    TOGGLE_TIME_DISPLAY,         // Toggle game time HUD overlay
    TOGGLE_LIGHT_VISUALIZATION,  // Toggle showing lights as white cubes
    TOGGLE_COMPASS,              // Toggle compass widget visibility

    // Particle Creation
    CREATE_TEST_PARTICLE,
    CREATE_SERPENT_CHAIN,
    CREATE_WALL,
    CREATE_ROOM,
    CREATE_FOREST,
    CREATE_LIGHT_SOURCE,
    CREATE_SUN,
    CREATE_PARTICLE_AT_MOUSE,
    CREATE_FLOOR_AT_MOUSE,
    CREATE_TREE,
    CLEAR_PARTICLES,

    // Test Scenarios
    TOGGLE_TEST_MENU,
    SELECT_TEST_CATEGORY_1,
    SELECT_TEST_CATEGORY_2,
    SELECT_TEST_CATEGORY_3,
    SELECT_TEST_CATEGORY_4,
    LOAD_TEST_SCENARIO_0,
    LOAD_TEST_SCENARIO_1,
    LOAD_TEST_SCENARIO_2,
    LOAD_TEST_SCENARIO_3,
    LOAD_TEST_SCENARIO_4,
    LOAD_TEST_SCENARIO_5,
    LOAD_TEST_SCENARIO_6,
    LOAD_TEST_SCENARIO_7,
    LOAD_TEST_SCENARIO_8,
    LOAD_TEST_SCENARIO_9,

    // Time Control
    TIME_SLOW_DOWN,              // Halve game time speed
    TIME_SPEED_UP,               // Double game time speed

    // Special Test Actions
    RUN_LIGHT_TEST,

    // Total count
    ACTION_COUNT
};

// Key combination (key + modifiers)
struct KeyCombo {
    int key;           // GLFW key code
    int modifiers;     // GLFW modifier bits (shift, ctrl, alt, etc.)
    
    KeyCombo(int k = 0, int m = 0) : key(k), modifiers(m) {}
    
    // For use as map key
    bool operator==(const KeyCombo& other) const {
        return key == other.key && modifiers == other.modifiers;
    }
};

// Hash function for KeyCombo (needed for unordered_map)
struct KeyComboHash {
    std::size_t operator()(const KeyCombo& combo) const {
        // Combine key and modifiers into single hash
        return std::hash<int>()(combo.key) ^ (std::hash<int>()(combo.modifiers) << 1);
    }
};

// Callback function type for actions
using ActionCallback = std::function<void()>;

class KeyMapper {
public:
    KeyMapper();
    ~KeyMapper();
    
    // Initialize with default key mappings
    void initialize();
    
    // Set the application that should receive unhandled keys first
    void set_application(Logosphere::IApplication* app) { application_ = app; }
    
    // Map a key combination to an action
    void map_key(int key, int modifiers, GameAction action);
    void map_key(int key, GameAction action) { map_key(key, 0, action); }
    
    // Register a callback for an action
    void register_action(GameAction action, ActionCallback callback);
    
    // Process a key event (called from GLFW callback)
    void process_key_event(int key, int scancode, int action, int mods);
    
    // Check if an action is currently active (for continuous actions like movement)
    bool is_action_active(GameAction action) const;
    
    // Test helpers - check if actions are mapped to keys
    bool has_action_mapping(GameAction action) const;
    GameAction get_action_for_key(int key, int modifiers = 0) const;
    
    // Get human-readable name for an action
    static const char* get_action_name(GameAction action);
    
    // Get current key mapping for an action (for displaying in UI)
    std::string get_key_binding_string(GameAction action) const;
    
    // Load/save key mappings from/to config file
    bool load_mappings(const std::string& filename);
    bool save_mappings(const std::string& filename) const;
    
    // Clear all mappings
    void clear_mappings();
    
    // Reset to default mappings
    void reset_to_defaults();
    
private:
    // Maps key combinations to actions
    std::unordered_map<KeyCombo, GameAction, KeyComboHash> key_to_action_;
    
    // Reverse map: action to key combos (an action can have multiple keys)
    std::unordered_map<GameAction, std::vector<KeyCombo>> action_to_keys_;
    
    // Callbacks for each action
    std::unordered_map<GameAction, ActionCallback> action_callbacks_;
    
    // Track which actions are currently active (for continuous actions)
    bool action_states_[static_cast<int>(GameAction::ACTION_COUNT)];
    
    // Application that gets first chance at handling keys
    Logosphere::IApplication* application_ = nullptr;
    
    // Setup default key bindings
    void setup_default_mappings();
};

#endif // KEY_MAPPER_H