#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include "particle.h"

// EDUCATIONAL NOTE: Game Logic Module
//
// This module contains game-specific logic that doesn't belong in the engine.
// Things like character initialization, spawn functions, and game rules.
// This is part of the refactoring to separate engine from game code.
//
// In the future, this will move to the game/ directory when we complete
// the separation of Logosphere engine from game-specific code.

class GameLogic {
public:
    // Character management
    static void initialize_character(Character& character);
    static void update_character_particle(Character& character);
    
    // Collision detection
    static bool check_character_collision(float new_x, float new_y);
    
    // Scene creation / spawn functions
    static void create_light_test_scenario();
    static void spawn_particle(float x, float y, float z, 
                              float r, float g, float b, float a,
                              float size, int type);
    static void spawn_particle_chain(float start_x, float start_y, float end_x, float end_y,
                                    int count, float r, float g, float b);
    static void spawn_wall(float x, float y, float width, float height,
                          float r, float g, float b);
    static void spawn_room(float center_x, float center_y, float width, float height,
                          float wall_thickness, float r, float g, float b);
    static void spawn_forest(float center_x, float center_y, float radius, int tree_count);
    
private:
    // Helper functions
    static float calculate_distance(float x1, float y1, float x2, float y2);
};

#endif // GAME_LOGIC_H