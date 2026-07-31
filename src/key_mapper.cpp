#include "key_mapper.h"
#include "application.h"
#include "debug_control.h"
#include <iostream>
#include <fstream>
#include <sstream>


KeyMapper::KeyMapper() {
    // Initialize all action states to false
    for (int i = 0; i < static_cast<int>(GameAction::ACTION_COUNT); i++) {
        action_states_[i] = false;
    }
}

KeyMapper::~KeyMapper() {
    // Cleanup if needed
}

void KeyMapper::initialize() {
    DEBUG_LOG("Initializing KeyMapper with default bindings...");
    setup_default_mappings();
}

void KeyMapper::map_key(int key, int modifiers, GameAction action) {
    KeyCombo combo(key, modifiers);
    
    // Add to forward mapping
    key_to_action_[combo] = action;
    
    // Add to reverse mapping
    action_to_keys_[action].push_back(combo);
}

void KeyMapper::register_action(GameAction action, ActionCallback callback) {
    action_callbacks_[action] = callback;
}

void KeyMapper::process_key_event(int key, int scancode, int action_type, int mods) {
    // Ignore unknown keys
    if (key == GLFW_KEY_UNKNOWN) return;

    // CRITICAL: Give application first chance to handle keys
    // This allows games to override engine behavior with custom handlers
    if (application_ && application_->handle_key(key, scancode, action_type, mods)) {
        return;
    }
    
    // Create key combo with current modifiers
    KeyCombo combo(key, mods);
    
    // Debug Shift+number combinations (disabled in test mode for clean output)
    // if (action_type == GLFW_PRESS && (mods & GLFW_MOD_SHIFT) && key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
    //     std::cout << "KeyMapper DEBUG: Shift+" << (key - GLFW_KEY_0) << " pressed, looking for action..." << std::endl;
    // }
    
    // Find the game action for this key combo
    auto it = key_to_action_.find(combo);
    if (it == key_to_action_.end()) {
        // No exact match with modifiers, try without modifiers if none were pressed
        if (mods == 0) {
            // Already no modifiers, no action mapped
            return;
        }
        // For some keys we might want to ignore modifiers, but for now we require exact match
        return;
    }
    
    GameAction game_action = it->second;
    int action_index = static_cast<int>(game_action);
    
    // Update action state
    if (action_type == GLFW_PRESS) {
        action_states_[action_index] = true;
        
        
        // Trigger callback if registered
        auto callback_it = action_callbacks_.find(game_action);
        if (callback_it != action_callbacks_.end() && callback_it->second) {
            callback_it->second();
        }
    } else if (action_type == GLFW_RELEASE) {
        action_states_[action_index] = false;
    } else if (action_type == GLFW_REPEAT) {
        // For repeat, just trigger callback without changing state
        auto callback_it = action_callbacks_.find(game_action);
        if (callback_it != action_callbacks_.end() && callback_it->second) {
            callback_it->second();
        }
    }
}

bool KeyMapper::is_action_active(GameAction action) const {
    int index = static_cast<int>(action);
    if (index >= 0 && index < static_cast<int>(GameAction::ACTION_COUNT)) {
        return action_states_[index];
    }
    return false;
}

const char* KeyMapper::get_action_name(GameAction action) {
    switch (action) {
        // System
        case GameAction::EXIT_GAME: return "Exit Game";
        case GameAction::TOGGLE_DEBUG_OVERLAY: return "Toggle Debug Overlay";
        
        // Movement
        case GameAction::MOVE_NORTH: return "Move North";
        case GameAction::MOVE_SOUTH: return "Move South";
        case GameAction::MOVE_WEST: return "Move West";
        case GameAction::MOVE_EAST: return "Move East";
        case GameAction::MOVE_UP: return "Move Up (Z)";
        case GameAction::MOVE_DOWN: return "Move Down (Z)";
        
        // Camera/Rendering
        case GameAction::CYCLE_PROJECTION_MODE: return "Cycle Projection Mode";
        case GameAction::TOGGLE_ORTHOGRAPHIC: return "Toggle Orthographic";
        case GameAction::CYCLE_UI_MODE: return "Cycle UI Mode";
        case GameAction::TOGGLE_CAMERA_FOLLOW: return "Toggle Camera Follow";
        case GameAction::TOGGLE_FREE_CAMERA: return "Toggle Free Camera";
        
        // Debug Visualization
        case GameAction::TOGGLE_VISION_CONE: return "Toggle Vision Cone";
        case GameAction::TOGGLE_LINE_OF_SIGHT: return "Toggle Line of Sight";
        case GameAction::TOGGLE_BINOCULAR_VISION: return "Toggle Binocular Vision";
        case GameAction::TOGGLE_RAY_VISUALIZATION: return "Toggle Ray Visualization";
        case GameAction::TOGGLE_DEADZONE_DEBUG: return "Toggle Deadzone Debug";
        case GameAction::TOGGLE_KG_INSPECTOR: return "Toggle KG Inspector";
        case GameAction::TOGGLE_TIME_DISPLAY: return "Toggle Time Display";
        case GameAction::TOGGLE_LIGHT_VISUALIZATION: return "Toggle Light Visualization";
        case GameAction::TOGGLE_COMPASS: return "Toggle Compass";

        // Particle Creation
        case GameAction::CREATE_TEST_PARTICLE: return "Create Test Particle";
        case GameAction::CREATE_SERPENT_CHAIN: return "Create Serpent Chain";
        case GameAction::CREATE_WALL: return "Create Wall";
        case GameAction::CREATE_ROOM: return "Create Room";
        case GameAction::CREATE_FOREST: return "Create Forest";
        case GameAction::CREATE_LIGHT_SOURCE: return "Create Light Source";
        case GameAction::CREATE_SUN: return "Create Sun (Dawn)";
        case GameAction::CREATE_PARTICLE_AT_MOUSE: return "Create Particle at Mouse";
        case GameAction::CREATE_FLOOR_AT_MOUSE: return "Toggle Floor System (WorldGen)";
        case GameAction::CREATE_TREE: return "Create Procedural Tree";
        case GameAction::CLEAR_PARTICLES: return "Clear Particles";
        
        // Test Scenarios
        case GameAction::TOGGLE_TEST_MENU: return "Toggle Test Menu";
        case GameAction::SELECT_TEST_CATEGORY_1: return "Select Test Category 1";
        case GameAction::SELECT_TEST_CATEGORY_2: return "Select Test Category 2";
        case GameAction::SELECT_TEST_CATEGORY_3: return "Select Test Category 3";
        case GameAction::SELECT_TEST_CATEGORY_4: return "Select Test Category 4";
        case GameAction::LOAD_TEST_SCENARIO_0: return "Load Test Scenario 0";
        case GameAction::LOAD_TEST_SCENARIO_1: return "Load Test Scenario 1";
        case GameAction::LOAD_TEST_SCENARIO_2: return "Load Test Scenario 2";
        case GameAction::LOAD_TEST_SCENARIO_3: return "Load Test Scenario 3";
        case GameAction::LOAD_TEST_SCENARIO_4: return "Load Test Scenario 4";
        case GameAction::LOAD_TEST_SCENARIO_5: return "Load Test Scenario 5";
        case GameAction::LOAD_TEST_SCENARIO_6: return "Load Test Scenario 6";
        case GameAction::LOAD_TEST_SCENARIO_7: return "Load Test Scenario 7";
        case GameAction::LOAD_TEST_SCENARIO_8: return "Load Test Scenario 8";
        case GameAction::LOAD_TEST_SCENARIO_9: return "Load Test Scenario 9";

        // Time Control
        case GameAction::TIME_SLOW_DOWN: return "Slow Down Time";
        case GameAction::TIME_SPEED_UP: return "Speed Up Time";

        // Special
        case GameAction::RUN_LIGHT_TEST: return "Run Light Test";

        default: return "Unknown Action";
    }
}

std::string KeyMapper::get_key_binding_string(GameAction action) const {
    auto it = action_to_keys_.find(action);
    if (it == action_to_keys_.end() || it->second.empty()) {
        return "Unbound";
    }
    
    // Get the first key binding
    const KeyCombo& combo = it->second[0];
    std::string result;
    
    // Add modifiers
    if (combo.modifiers & GLFW_MOD_SHIFT) result += "Shift+";
    if (combo.modifiers & GLFW_MOD_CONTROL) result += "Ctrl+";
    if (combo.modifiers & GLFW_MOD_ALT) result += "Alt+";
    if (combo.modifiers & GLFW_MOD_SUPER) result += "Super+";
    
    // Add key name (simplified - in production you'd have a full key name table)
    switch (combo.key) {
        case GLFW_KEY_ESCAPE: result += "ESC"; break;
        case GLFW_KEY_SPACE: result += "Space"; break;
        case GLFW_KEY_GRAVE_ACCENT: result += "`"; break;
        case GLFW_KEY_0: result += "0"; break;
        case GLFW_KEY_1: result += "1"; break;
        case GLFW_KEY_2: result += "2"; break;
        case GLFW_KEY_3: result += "3"; break;
        case GLFW_KEY_4: result += "4"; break;
        case GLFW_KEY_5: result += "5"; break;
        case GLFW_KEY_6: result += "6"; break;
        case GLFW_KEY_7: result += "7"; break;
        case GLFW_KEY_8: result += "8"; break;
        case GLFW_KEY_9: result += "9"; break;
        case GLFW_KEY_A: result += "A"; break;
        case GLFW_KEY_B: result += "B"; break;
        case GLFW_KEY_C: result += "C"; break;
        case GLFW_KEY_D: result += "D"; break;
        case GLFW_KEY_E: result += "E"; break;
        case GLFW_KEY_F: result += "F"; break;
        case GLFW_KEY_G: result += "G"; break;
        case GLFW_KEY_H: result += "H"; break;
        case GLFW_KEY_I: result += "I"; break;
        case GLFW_KEY_J: result += "J"; break;
        case GLFW_KEY_K: result += "K"; break;
        case GLFW_KEY_L: result += "L"; break;
        case GLFW_KEY_M: result += "M"; break;
        case GLFW_KEY_N: result += "N"; break;
        case GLFW_KEY_O: result += "O"; break;
        case GLFW_KEY_P: result += "P"; break;
        case GLFW_KEY_Q: result += "Q"; break;
        case GLFW_KEY_R: result += "R"; break;
        case GLFW_KEY_S: result += "S"; break;
        case GLFW_KEY_T: result += "T"; break;
        case GLFW_KEY_U: result += "U"; break;
        case GLFW_KEY_V: result += "V"; break;
        case GLFW_KEY_W: result += "W"; break;
        case GLFW_KEY_X: result += "X"; break;
        case GLFW_KEY_Y: result += "Y"; break;
        case GLFW_KEY_Z: result += "Z"; break;
        case GLFW_KEY_F1: result += "F1"; break;
        case GLFW_KEY_F2: result += "F2"; break;
        case GLFW_KEY_F3: result += "F3"; break;
        case GLFW_KEY_F4: result += "F4"; break;
        default: result += "Key" + std::to_string(combo.key); break;
    }
    
    return result;
}

void KeyMapper::clear_mappings() {
    key_to_action_.clear();
    action_to_keys_.clear();
    action_callbacks_.clear();
    
    // Reset all states
    for (int i = 0; i < static_cast<int>(GameAction::ACTION_COUNT); i++) {
        action_states_[i] = false;
    }
}

void KeyMapper::reset_to_defaults() {
    clear_mappings();
    setup_default_mappings();
}

void KeyMapper::setup_default_mappings() {
    // EDUCATIONAL NOTE: Default Key Bindings
    //
    // Here we define the default key mappings for our game.
    // This is where physical keys get mapped to game actions.
    // The beauty is that this is the ONLY place we need to define keys!
    
    // System Keys
    map_key(GLFW_KEY_ESCAPE, GameAction::EXIT_GAME);
    map_key(GLFW_KEY_GRAVE_ACCENT, GameAction::TOGGLE_DEBUG_OVERLAY);
    
    // Movement Keys (WASD + QE)
    map_key(GLFW_KEY_W, GameAction::MOVE_NORTH);
    map_key(GLFW_KEY_S, GameAction::MOVE_SOUTH);
    map_key(GLFW_KEY_A, GameAction::MOVE_WEST);
    map_key(GLFW_KEY_D, GameAction::MOVE_EAST);
    map_key(GLFW_KEY_Q, GameAction::MOVE_DOWN);
    map_key(GLFW_KEY_E, GameAction::MOVE_UP);
    
    // Camera/Rendering Keys
    map_key(GLFW_KEY_P, GameAction::CYCLE_PROJECTION_MODE);
    map_key(GLFW_KEY_O, GameAction::TOGGLE_CAMERA_FOLLOW);
    map_key(GLFW_KEY_F, GameAction::TOGGLE_FREE_CAMERA);  // F for Free camera
    map_key(GLFW_KEY_U, GameAction::TOGGLE_DEADZONE_DEBUG);
    map_key(GLFW_KEY_EQUAL, GameAction::INCREASE_UI_SCALE);    // = key (+ when shifted)
    map_key(GLFW_KEY_EQUAL, GLFW_MOD_SHIFT, GameAction::INCREASE_UI_SCALE);  // Explicit Shift++ 
    map_key(GLFW_KEY_MINUS, GameAction::DECREASE_UI_SCALE);    // - key
    
    // Resolution controls
    map_key(GLFW_KEY_COMMA, GameAction::DECREASE_RESOLUTION);   // , key
    map_key(GLFW_KEY_PERIOD, GameAction::INCREASE_RESOLUTION);  // . key
    
    // Debug Visualization Keys
    map_key(GLFW_KEY_V, GameAction::TOGGLE_VISION_CONE);
    map_key(GLFW_KEY_L, GameAction::TOGGLE_LINE_OF_SIGHT);
    map_key(GLFW_KEY_B, GameAction::TOGGLE_BINOCULAR_VISION);
    map_key(GLFW_KEY_R, GameAction::TOGGLE_RAY_VISUALIZATION);
    map_key(GLFW_KEY_K, GameAction::TOGGLE_KG_INSPECTOR);
    map_key(GLFW_KEY_BACKSLASH, GameAction::TOGGLE_TIME_DISPLAY);  // \ for time display
    map_key(GLFW_KEY_G, GameAction::TOGGLE_LIGHT_VISUALIZATION);  // G for "glow" or light visualization
    map_key(GLFW_KEY_LEFT_BRACKET, GameAction::TIME_SLOW_DOWN);   // [ for slow down time
    map_key(GLFW_KEY_RIGHT_BRACKET, GameAction::TIME_SPEED_UP);   // ] for speed up time

    // Particle Creation Keys (without Shift)
    map_key(GLFW_KEY_0, GameAction::CREATE_SUN);
    map_key(GLFW_KEY_1, GameAction::CREATE_TEST_PARTICLE);
    map_key(GLFW_KEY_2, GameAction::CREATE_SERPENT_CHAIN);
    map_key(GLFW_KEY_3, GameAction::CREATE_WALL);
    map_key(GLFW_KEY_4, GameAction::CREATE_ROOM);
    map_key(GLFW_KEY_5, GameAction::CREATE_FOREST);
    map_key(GLFW_KEY_6, GameAction::CREATE_LIGHT_SOURCE);
    map_key(GLFW_KEY_7, GameAction::CREATE_FLOOR_AT_MOUSE);
    map_key(GLFW_KEY_8, GameAction::CREATE_TREE);
    map_key(GLFW_KEY_SPACE, GameAction::CREATE_PARTICLE_AT_MOUSE);
    map_key(GLFW_KEY_C, GameAction::TOGGLE_COMPASS);
    
    // Test Menu Keys
    map_key(GLFW_KEY_H, GameAction::TOGGLE_TEST_MENU);
    map_key(GLFW_KEY_F1, GameAction::SELECT_TEST_CATEGORY_1);
    map_key(GLFW_KEY_F2, GameAction::SELECT_TEST_CATEGORY_2);
    map_key(GLFW_KEY_F3, GameAction::SELECT_TEST_CATEGORY_3);
    map_key(GLFW_KEY_F4, GameAction::SELECT_TEST_CATEGORY_4);
    
    // Test Scenario Keys (with Shift modifier)
    map_key(GLFW_KEY_9, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_0);  // Shift+9 for scenario 0
    map_key(GLFW_KEY_1, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_1);
    map_key(GLFW_KEY_2, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_2);
    map_key(GLFW_KEY_3, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_3);
    map_key(GLFW_KEY_4, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_4);
    map_key(GLFW_KEY_5, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_5);
    map_key(GLFW_KEY_6, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_6);
    map_key(GLFW_KEY_7, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_7);
    map_key(GLFW_KEY_8, GLFW_MOD_SHIFT, GameAction::LOAD_TEST_SCENARIO_8);
    
    // Special Test Keys
    map_key(GLFW_KEY_T, GameAction::RUN_LIGHT_TEST);
    
    DEBUG_LOG("Default key mappings loaded: " << key_to_action_.size() << " bindings");
}

bool KeyMapper::load_mappings(const std::string& filename) {
    // TODO: Implement config file loading
    // For now, just use defaults
    return false;
}

bool KeyMapper::save_mappings(const std::string& filename) const {
    // TODO: Implement config file saving
    // For now, not implemented
    return false;
}

// Test helper methods
bool KeyMapper::has_action_mapping(GameAction action) const {
    // Check if any key combination maps to this action
    for (const auto& mapping : key_to_action_) {
        if (mapping.second == action) {
            return true;
        }
    }
    return false;
}

GameAction KeyMapper::get_action_for_key(int key, int modifiers) const {
    KeyCombo combo(key, modifiers);
    auto it = key_to_action_.find(combo);
    if (it != key_to_action_.end()) {
        return it->second;
    }
    return GameAction::ACTION_COUNT; // Invalid action
}