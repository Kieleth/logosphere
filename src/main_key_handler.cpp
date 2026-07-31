// main_key_handler.cpp - Game-specific key action handlers
//
// This file contains all the action handlers that are triggered by key presses.
// Instead of having key handling scattered throughout the codebase, everything
// is centralized here and triggered through the KeyMapper.

#include "key_mapper.h"
#include "core/engine.h"
#include "core/particle_system.h"
#include "core/game_time.h"
#include "vision_system.h"
#include "lighting_config.h"
#include "ui/ui_system.h"
#include "core/light_system.h"
#include "core/input_system.h"
#include "logosphere/physics/physics_system.h"
#include "camera_controller.h"
#include "entity_system.h"
#include "logosphere/kg/kg_types.h"
#include "platform/platform_system.h"
#include "debug_control.h"
#include "logosphere/worldgen/tree_generator.h"
#include <iostream>
#include <ctime>

// Note: All systems accessed through Engine parameter, no globals

// Register all action handlers with the KeyMapper
void register_key_action_handlers(Engine* engine) {
    DEBUG_LOG("Registering key action handlers...");
    
    // System Actions
    engine->get_key_mapper().register_action(GameAction::EXIT_GAME, [engine]() {
        if (engine && engine->get_platform()) {
            engine->get_platform()->set_should_close(true);
        }
    });
    
    engine->get_key_mapper().register_action(GameAction::TOGGLE_DEBUG_OVERLAY, [engine]() {
        if (engine) {
            engine->toggle_debug_overlay();
        }
    });
    
    // Camera/Rendering Actions
    engine->get_key_mapper().register_action(GameAction::CYCLE_PROJECTION_MODE, [engine]() {
        engine->cycle_projection_mode();
    });
    
    // Free camera toggle
    engine->get_key_mapper().register_action(GameAction::TOGGLE_FREE_CAMERA, [engine]() {
        if (engine && engine->get_camera_controller()) {
            engine->get_camera_controller()->toggle_free_camera();
        }
    });
    
    // Camera follow removed - belongs in CameraSystem
    
    // UI Scale Controls
    engine->get_key_mapper().register_action(GameAction::INCREASE_UI_SCALE, [engine]() {
        if (engine && engine->get_ui_system()) {
            engine->get_ui_system()->increase_ui_scale();
        }
    });
    
    engine->get_key_mapper().register_action(GameAction::DECREASE_UI_SCALE, [engine]() {
        if (engine && engine->get_ui_system()) {
            engine->get_ui_system()->decrease_ui_scale();
        }
    });
    
    // Resolution Controls
    engine->get_key_mapper().register_action(GameAction::DECREASE_RESOLUTION, [engine]() {
        if (engine) {
            engine->decrease_resolution();
        }
    });
    
    engine->get_key_mapper().register_action(GameAction::INCREASE_RESOLUTION, [engine]() {
        if (engine) {
            engine->increase_resolution();
        }
    });
    
    // Debug Visualization Actions
    engine->get_key_mapper().register_action(GameAction::TOGGLE_VISION_CONE, [engine]() {
        engine->get_vision_system().set_show_vision_cone_debug(!engine->get_vision_system().get_show_vision_cone_debug());
    });
    
    engine->get_key_mapper().register_action(GameAction::TOGGLE_LINE_OF_SIGHT, [engine]() {
        if (engine) {
            engine->toggle_shadowcasting_debug();
        }
    });
    
    engine->get_key_mapper().register_action(GameAction::TOGGLE_BINOCULAR_VISION, [engine]() {
        engine->get_vision_system().set_show_binocular_debug(!engine->get_vision_system().get_show_binocular_debug());
    });
    
    engine->get_key_mapper().register_action(GameAction::TOGGLE_RAY_VISUALIZATION, [engine]() {
        // SimplePixelToLight doesn't use rays - no debug visualization needed
    });
    
    engine->get_key_mapper().register_action(GameAction::TOGGLE_KG_INSPECTOR, [engine]() {
        if (engine) {
            engine->toggle_kg_inspector();
        }
    });

    engine->get_key_mapper().register_action(GameAction::TOGGLE_TIME_DISPLAY, [engine]() {
        std::cout << "[TIME] TOGGLE_TIME_DISPLAY action triggered!" << std::endl;
        if (engine) {
            engine->toggle_time_display();
            std::cout << "[TIME] Display now: " << (engine->get_show_time_display() ? "ON" : "OFF") << std::endl;
        }
    });

    engine->get_key_mapper().register_action(GameAction::TOGGLE_COMPASS, [engine]() {
        if (engine && engine->get_ui_system()) {
            engine->get_ui_system()->toggle_compass();
        }
    });

    // Time Control: [ slows down, ] speeds up
    engine->get_key_mapper().register_action(GameAction::TIME_SLOW_DOWN, []() {
        double current_scale = GameTime::get_time_scale();
        double new_scale = std::max(0.01, current_scale * 0.5);
        GameTime::set_time_scale(new_scale);
        std::cout << "[TIME] Scale: " << new_scale << "x" << std::endl;
    });

    engine->get_key_mapper().register_action(GameAction::TIME_SPEED_UP, []() {
        double current_scale = GameTime::get_time_scale();
        double new_scale = std::min(10000.0, current_scale * 2.0);
        GameTime::set_time_scale(new_scale);
        std::cout << "[TIME] Scale: " << new_scale << "x" << std::endl;
    });

    // Content creation actions removed - these are game-specific (now in Eden)
    // CREATE_TEST_PARTICLE, CREATE_SERPENT_CHAIN, CREATE_WALL, CREATE_ROOM,
    // CREATE_FOREST, CREATE_LIGHT_SOURCE, CLEAR_PARTICLES, CREATE_PARTICLE_AT_MOUSE,
    // CREATE_FLOOR_AT_MOUSE, CREATE_TREE, CREATE_SUN, RUN_LIGHT_TEST
    //
    // Engine remains agnostic - only handles system/debug actions
}

// Handle continuous movement based on active actions
void handle_continuous_movement(Engine* engine, double delta_time) {
    // REMOVED: Engine no longer handles entity movement
    //
    // DESIGN PRINCIPLE: "Engine provides mechanisms, game provides policy"
    // Movement speed is GAME POLICY (varies by entity, state, terrain)
    // Engine provides moveEntity() MECHANISM
    //
    // Movement now handled by:
    // - Eden: Reads entity speed from KG, detects Shift (run), calls moveEntity()
    // - Other games: Implement their own movement policies
    //
    // This separation allows:
    // - Per-entity speeds (Eva 1.5 m/s, Adam 2.0 m/s)
    // - State-based speeds (walk vs run vs crouch)
    // - Game-specific controls (RPG vs platformer vs RTS)
    //
    // See: eden/src/main.cpp::update_game() for movement implementation
    (void)engine;      // Unused
    (void)delta_time;  // Unused
}