#ifndef MAIN_KEY_HANDLER_H
#define MAIN_KEY_HANDLER_H

// Forward declaration
class Engine;

// Function to register all key action handlers with the KeyMapper
// Takes Engine pointer to access systems without globals
void register_key_action_handlers(Engine* engine);

// Function to handle continuous movement based on active actions
// Takes Engine pointer to access systems without globals
void handle_continuous_movement(Engine* engine, double delta_time);

#endif // MAIN_KEY_HANDLER_H