// Eden - The Knowledge Garden
// A mythological showcase of Logosphere's Knowledge Graph capabilities
// Every particle carries meaning, every relationship drives narrative

#include <iostream>
#include <ctime>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <GLFW/glfw3.h>
#include "debug_control.h"  // Centralized Eden debug control
#include "platform/platform_layer.h"  // For PlatformLayer base class
#include "core/engine.h"
#include "camera_controller.h"  // For camera follow mode
#include "core/particle_system.h"  // To access g_particle_system
#include "logosphere/kg/kg_module.h"
#include "eden_ontology_registry.h"
#include "particle.h"
#include "lighting_config.h"
#include "logosphere/worldgen/humanoid_generator.h"  // For procedural humanoid generation
#include "player_controller.h"  // For PlayerController delegation
#include "logosphere/worldgen/tree_generator.h"      // For tree generation
#include "logosphere/worldgen/physics_tree_generator.h" // For physics-based tree (gluon constraints)
#include "logosphere/worldgen/organic_generator.h"   // For organic entity generation
#include "logosphere/worldgen/snake_generator.h"     // For procedural snake generation
#include "logosphere/worldgen/butterfly_generator.h" // For procedural butterfly generation
#include "logosphere/worldgen/totem_generator.h"     // For totem test entity (physics testing)
#include "logosphere/worldgen/grass_patch_spec.h"    // For grass patch presets
#include "logosphere/worldgen/physics_rock_generator.h" // For physics-based rock generation
#include "logosphere/worldgen/fallen_tree_generator.h"   // For fallen log generation
#include "object_id.h"                    // For ObjectID encoding/decoding (click detection)

// Forward declaration from macos_platform.mm — display_framebuffer
// removed in the Renderer/Display split; presentation flows through
// the engine's IDisplay now, not via this class.
namespace Logosphere {
    class MacOSPlatform : public PlatformLayer {
    };
}

// Eden Application - implements IApplication interface to use Logosphere
class EdenApplication : public Logosphere::MacOSPlatform {
private:
    Engine* engine_;  // Pointer to the engine for key handling

    // Entity IDs for our mythological cast
    kg::EntityID eva_entity_;
    kg::EntityID adam_entity_;
    kg::EntityID tree_entity_;
    kg::EntityID apple_entity_;
    kg::EntityID serpent_entity_;
    kg::EntityID sun_entity_;
    kg::EntityID totem_entity_;  // Test entity for physics

    // Physics-based Eva (hips particle for camera/input tracking)
    int eva_hips_id_ = -1;
    PhysicsHumanoidResult eva_physics_;  // Full physics result for animation registration

    // Boulder tracking for drift diagnostics
    int boulder_core_id_ = -1;
    float boulder_initial_x_ = 0, boulder_initial_y_ = 0, boulder_initial_z_ = 0;

    // Spirit lights (floating, animated)
    struct SpiritLight {
        int particle_id = -1;
        float base_x, base_y, base_z;
        float phase;       // Animation phase offset
        float speed;       // Movement speed multiplier
        float radius;      // Wander radius
    };
    std::vector<SpiritLight> spirit_lights_;

    // Hover state for entity highlighting (debug mode only)
    kg::EntityID hovered_entity_id_ = kg::INVALID_ENTITY;

public:
    EdenApplication() : engine_(nullptr) {
        set_app_name("Eden - Logosphere Game");
        set_window_size(1600, 1200);
    }

    // --bench mode: script Eva along a box path (forward / right / back /
    // left, 150 frames per leg after a 60-frame warmup) so unattended runs
    // exercise walking, foot plants, and chunk streaming — the paths that
    // an idle session never touches. Same-day main-vs-branch A/B without a
    // human at the keyboard.
    void bench_drive(int frame) {
        if (!engine_ || eva_hips_id_ < 0) return;
        auto& loco = engine_->get_humanoid_locomotion();
        if (frame < 60) return;
        if (frame == 60) loco.set_volitional(eva_hips_id_, true);
        static const float legs[4][2] = {
            {1.5f, 0.0f}, {0.0f, 1.5f}, {-1.5f, 0.0f}, {0.0f, -1.5f}};
        const float* leg = legs[((frame - 60) / 150) % 4];
        loco.set_body_relative_velocity(eva_hips_id_, leg[0], leg[1]);
    }
    
    // Override to add Eden-specific initialization
    void initialize_game(void* engine_ptr) override {
        EDEN_LOG("Initializing Eden game content...");

        // Store engine pointer for later use
        engine_ = static_cast<Engine*>(engine_ptr);

        // Extend the engine's ontology with Eden-specific types
        engine_->get_kg().extendOntology(eden::ontology::registry());
        EDEN_LOG("[KG] Eden ontology loaded (Human, Serpent, Fruit, Knowledge, Garden...)");

        // Light from southwest (same side as camera), elevated
        // Camera is at SW looking NE, so light from SW illuminates visible faces
        {
            engine_->get_particle_system().queue_light(
                22.0f, -20.0f, 5.5f,  // Top of the streetlight pole
                500000.0f, 200.0f,
                1.0f, 0.95f, 0.9f);
        }

        // PRE-POPULATE KG: Create entities that will be loaded by scene_generator
        pre_populate_kg();

        // NOTE: Cannot enable scene_generator here - WorldGenSystem not initialized yet
        // Will enable in create_initial_scene() after worldgen_system_.initialize()

        // Note: create_initial_scene() will be called by the Engine after this
        // with the proper particle creation function
    }

    // Pre-populate KG with scene entities (cube, light)
    // These will be discovered and loaded by SceneChunkGenerator
    void pre_populate_kg() {
        EDEN_LOG("\n=== Pre-populating KG with Eden scene ===");

        auto& kg = engine_->get_kg();
        auto& config = LightingConfig::get();

        // === GARDEN LIGHT (very high so shadows clear the tree canopy) ===
        kg::EntityID light_entity = kg.createEntity("LightSource");
        kg.setProperty(light_entity, "chunk_x", "0");
        kg.setProperty(light_entity, "chunk_y", "-1");
        kg.setProperty(light_entity, "x", "25.0");
        kg.setProperty(light_entity, "y", "-25.0");
        kg.setProperty(light_entity, "z", "40.0");   // Very high — clears tree canopy shadows
        kg.setProperty(light_entity, "size", "0.3");
        kg.setProperty(light_entity, "r", "1.0");
        kg.setProperty(light_entity, "g", "0.92");
        kg.setProperty(light_entity, "b", "0.75");
        float emission_strength = config.light_sources.game_scale.stadium_light * 20.0f;
        float emission_radius = 500.0f;  // Huge radius to cover entire garden
        kg.setProperty(light_entity, "emission_strength", std::to_string(emission_strength));
        kg.setProperty(light_entity, "emission_radius", std::to_string(emission_radius));

        EDEN_LOG("Garden light at (0,-5,40) - high sun, clears tree shadows");
        EDEN_LOG("=== KG pre-populated ===\n");
    }

    // Handle Eden-specific keyboard input
    bool handle_key(int key, int scancode, int action, int mods) override {
        // Debug: Log ALL key presses to trace backtick and debug mode toggle
        if (action == 1) {  // GLFW_PRESS
            std::cout << "[Eden KEYPRESS] Raw key code: " << key
                      << " (backtick=96, ` char=96, ESCAPE=" << GLFW_KEY_ESCAPE << ")" << std::endl;

            // Show if this is a known special key
            if (key == 96) std::cout << "  -> This is BACKTICK (`) key!" << std::endl;
            if (key == GLFW_KEY_ESCAPE) std::cout << "  -> This is ESCAPE key" << std::endl;

            // Check current debug mode state
            std::cout << "  -> Current debug overlay state: "
                      << (engine_ && engine_->get_show_debug_overlay() ? "ENABLED" : "DISABLED")
                      << std::endl;
        }

        // Only handle key press events
        if (action != 1) return false;  // GLFW_PRESS = 1
        
        // Use the stored engine pointer
        if (!engine_) {
            EDEN_LOG("[Eden] ERROR: No engine pointer!");
            return false;
        }
        
        switch (key) {
            case 'G':  // Toggle light visualization (show lights as white cubes)
                {
                    engine_->toggle_light_visualization();
                    EDEN_LOG("[Eden] Light visualization: "
                              << (engine_->get_show_lights_as_white() ? "ON (white cubes)" : "OFF (normal lighting)"));
                    return true;
                }
                break;

            case '0':  // Toggle sun
                engine_->toggle_sun();
                return true;
                break;

            case '1':  // Test particle at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& kg = engine_->get_kg();
                    kg::EntityID test_entity = kg.createEntityAtPosition("Rock", world_x, world_y);
                    kg.setProperty(test_entity, "z", "2.0");
                    kg.setProperty(test_entity, "size", "1.0");
                    kg.setProperty(test_entity, "r", "1.0");
                    kg.setProperty(test_entity, "g", "0.0");
                    kg.setProperty(test_entity, "b", "1.0");  // Magenta test particle

                    // Queue for activation on main thread
                    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(test_entity);
                    EDEN_LOG("[Eden] Test particle (ID=" << test_entity << ") created at (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '2':  // Serpent chain at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& kg = engine_->get_kg();

                    // Create serpent chain as hierarchical entity (parent + children)
                    kg::EntityID serpent_entity = kg.createEntityAtPosition("Serpent", world_x, world_y);

                    // Create 8 segments along a curved path
                    for (int i = 0; i < 8; i++) {
                        float t = i / 7.0f;
                        float segment_x = world_x + std::sin(t * 3.14f) * 3.0f;
                        float segment_y = world_y + t * 5.0f;
                        float segment_z = 1.0f + std::sin(t * 6.28f) * 0.5f;

                        kg::EntityID segment = kg.createEntity("SerpentSegment");
                        kg.setProperty(segment, "chunk_x", kg.getProperty(serpent_entity, "chunk_x"));
                        kg.setProperty(segment, "chunk_y", kg.getProperty(serpent_entity, "chunk_y"));
                        kg.setProperty(segment, "x", std::to_string(segment_x));
                        kg.setProperty(segment, "y", std::to_string(segment_y));
                        kg.setProperty(segment, "z", std::to_string(segment_z));
                        kg.setProperty(segment, "size", "0.5");
                        kg.setProperty(segment, "r", "0.0");
                        kg.setProperty(segment, "g", "1.0");
                        kg.setProperty(segment, "b", "0.0");  // Green segments

                        kg.createRelation(serpent_entity, "HAS_PART", segment);
                    }

                    // Queue for activation on main thread
                    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(serpent_entity);
                    EDEN_LOG("[Eden] Serpent chain (ID=" << serpent_entity << ") created at (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '3':  // Wall at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& kg = engine_->get_kg();

                    // Create wall as hierarchical entity
                    kg::EntityID wall_entity = kg.createEntityAtPosition("Wall", world_x, world_y);

                    // Create 10 blocks along X axis
                    for (int i = 0; i < 10; i++) {
                        float block_x = world_x + (i - 4.5f) * 1.5f;

                        kg::EntityID block = kg.createEntity("WallBlock");
                        kg.setProperty(block, "chunk_x", kg.getProperty(wall_entity, "chunk_x"));
                        kg.setProperty(block, "chunk_y", kg.getProperty(wall_entity, "chunk_y"));
                        kg.setProperty(block, "x", std::to_string(block_x));
                        kg.setProperty(block, "y", std::to_string(world_y));
                        kg.setProperty(block, "z", "1.5");
                        kg.setProperty(block, "size", "1.0");
                        kg.setProperty(block, "r", "0.6");
                        kg.setProperty(block, "g", "0.4");
                        kg.setProperty(block, "b", "0.2");  // Brown blocks

                        kg.createRelation(wall_entity, "HAS_PART", block);
                    }

                    // Queue for activation on main thread
                    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(wall_entity);
                    EDEN_LOG("[Eden] Wall (ID=" << wall_entity << ") created at (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '4':  // Room at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& kg = engine_->get_kg();

                    // Create room as hierarchical entity (4 walls + floor)
                    kg::EntityID room_entity = kg.createEntityAtPosition("RoomBlock", world_x, world_y);

                    const float room_size = 8.0f;
                    const float wall_spacing = 1.5f;

                    // North wall (top)
                    for (int i = 0; i < 6; i++) {
                        float block_x = world_x + (i - 2.5f) * wall_spacing;
                        kg::EntityID block = kg.createEntity("RoomBlock");
                        kg.setProperty(block, "chunk_x", kg.getProperty(room_entity, "chunk_x"));
                        kg.setProperty(block, "chunk_y", kg.getProperty(room_entity, "chunk_y"));
                        kg.setProperty(block, "x", std::to_string(block_x));
                        kg.setProperty(block, "y", std::to_string(world_y + room_size/2));
                        kg.setProperty(block, "z", "1.5");
                        kg.setProperty(block, "size", "1.0");
                        kg.setProperty(block, "r", "0.5");
                        kg.setProperty(block, "g", "0.5");
                        kg.setProperty(block, "b", "0.5");
                        kg.createRelation(room_entity, "HAS_PART", block);
                    }

                    // South wall (bottom)
                    for (int i = 0; i < 6; i++) {
                        float block_x = world_x + (i - 2.5f) * wall_spacing;
                        kg::EntityID block = kg.createEntity("RoomBlock");
                        kg.setProperty(block, "chunk_x", kg.getProperty(room_entity, "chunk_x"));
                        kg.setProperty(block, "chunk_y", kg.getProperty(room_entity, "chunk_y"));
                        kg.setProperty(block, "x", std::to_string(block_x));
                        kg.setProperty(block, "y", std::to_string(world_y - room_size/2));
                        kg.setProperty(block, "z", "1.5");
                        kg.setProperty(block, "size", "1.0");
                        kg.setProperty(block, "r", "0.5");
                        kg.setProperty(block, "g", "0.5");
                        kg.setProperty(block, "b", "0.5");
                        kg.createRelation(room_entity, "HAS_PART", block);
                    }

                    // East wall (right)
                    for (int i = 0; i < 4; i++) {
                        float block_y = world_y + (i - 1.5f) * wall_spacing;
                        kg::EntityID block = kg.createEntity("RoomBlock");
                        kg.setProperty(block, "chunk_x", kg.getProperty(room_entity, "chunk_x"));
                        kg.setProperty(block, "chunk_y", kg.getProperty(room_entity, "chunk_y"));
                        kg.setProperty(block, "x", std::to_string(world_x + room_size/2));
                        kg.setProperty(block, "y", std::to_string(block_y));
                        kg.setProperty(block, "z", "1.5");
                        kg.setProperty(block, "size", "1.0");
                        kg.setProperty(block, "r", "0.5");
                        kg.setProperty(block, "g", "0.5");
                        kg.setProperty(block, "b", "0.5");
                        kg.createRelation(room_entity, "HAS_PART", block);
                    }

                    // West wall (left)
                    for (int i = 0; i < 4; i++) {
                        float block_y = world_y + (i - 1.5f) * wall_spacing;
                        kg::EntityID block = kg.createEntity("RoomBlock");
                        kg.setProperty(block, "chunk_x", kg.getProperty(room_entity, "chunk_x"));
                        kg.setProperty(block, "chunk_y", kg.getProperty(room_entity, "chunk_y"));
                        kg.setProperty(block, "x", std::to_string(world_x - room_size/2));
                        kg.setProperty(block, "y", std::to_string(block_y));
                        kg.setProperty(block, "z", "1.5");
                        kg.setProperty(block, "size", "1.0");
                        kg.setProperty(block, "r", "0.5");
                        kg.setProperty(block, "g", "0.5");
                        kg.setProperty(block, "b", "0.5");
                        kg.createRelation(room_entity, "HAS_PART", block);
                    }

                    // Queue for activation on main thread
                    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(room_entity);
                    EDEN_LOG("[Eden] Room (ID=" << room_entity << ") created at (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '5':  // Forest cluster at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& tree_gen = engine_->get_worldgen_system().get_tree_generator();
                    TreeSpec spec = TreeSpec::oak();

                    // Create 5 trees in a cluster pattern
                    static int cluster_counter = 0;
                    for (int i = 0; i < 5; i++) {
                        float angle = (i / 5.0f) * 6.28f;
                        float radius = 3.0f + (i % 2) * 2.0f;
                        float tree_x = world_x + std::cos(angle) * radius;
                        float tree_y = world_y + std::sin(angle) * radius;

                        unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                    + static_cast<unsigned int>(tree_x * 1000 + tree_y * 1000)
                                                    + cluster_counter++;
                        spec.apply_natural_variation(0.5f, variation_seed);
                        spec.random_seed = variation_seed;

                        kg::EntityID tree_id = tree_gen.generate_tree_space_colonization(tree_x, tree_y, 0.0f, spec);
                        EDEN_LOG("[Eden]   Tree " << (i+1) << "/5 (ID=" << tree_id << ") in cluster");
                    }

                    EDEN_LOG("[Eden] Forest cluster (5 trees) created around (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '6':  // Light at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& config = LightingConfig::get();
                    float light_strength = config.light_sources.game_scale.stadium_light;
                    float light_radius = config.light_sources.radii.stadium_light;

                    // Create light entity in KG (for chunking support)
                    auto& kg = engine_->get_kg();
                    kg::EntityID light_entity = kg.createEntityAtPosition("LightSource", world_x, world_y);
                    kg.setProperty(light_entity, "z", "1.5");
                    kg.setProperty(light_entity, "size", "0.5");
                    kg.setProperty(light_entity, "r", "1.0");
                    kg.setProperty(light_entity, "g", "0.9");
                    kg.setProperty(light_entity, "b", "0.7");
                    kg.setProperty(light_entity, "emission_strength", std::to_string(light_strength));
                    kg.setProperty(light_entity, "emission_radius", std::to_string(light_radius));

                    // Queue for activation on main thread
                    engine_->get_worldgen_system().get_scene_generator().queue_entity_activation(light_entity);
                    EDEN_LOG("[Eden] Light entity (ID=" << light_entity << ") created at mouse (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '7':  // Grass patch at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& organic_gen = engine_->get_worldgen_system().get_organic_generator();
                    GrassPatchSpec spec = GrassPatchSpec::short_grass();

                    static int patch_counter = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + patch_counter++;
                    spec.blade_spec.random_seed = variation_seed;

                    kg::EntityID patch_id = organic_gen.generate_grass_patch(world_x, world_y, 0.0f, spec);
                    EDEN_LOG("[Eden] Grass patch (ID=" << patch_id << ") spawned at mouse (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '8':  // Physics tree (small) at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& physics_tree_gen = engine_->get_worldgen_system().get_physics_tree_generator();
                    TreeSpec spec = TreeSpec::oak();
                    spec.height = 3.0f;       // Small tree
                    spec.branch_depth = 3;

                    static int tree_counter = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + tree_counter++;
                    spec.random_seed = variation_seed;

                    PhysicsTreeResult result = physics_tree_gen.generate_tree(world_x, world_y, 0.0f, spec);
                    EDEN_LOG("[Eden] PHYSICS tree (small): " << result.total_segments << " segments, " << result.total_mass << "kg at (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case '9':  // Physics tree (large oak) at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& physics_tree_gen = engine_->get_worldgen_system().get_physics_tree_generator();
                    TreeSpec spec = TreeSpec::oak();
                    // Full size oak tree with physics

                    static int tree_counter_large = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + tree_counter_large++;
                    spec.random_seed = variation_seed;

                    PhysicsTreeResult result = physics_tree_gen.generate_tree(world_x, world_y, 0.0f, spec);
                    EDEN_LOG("[Eden] PHYSICS tree (large oak): " << result.total_segments << " segments, " << result.total_mass << "kg at (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case GLFW_KEY_F:  // Frame dump to /tmp/eden_frame.ppm
                {
                    auto& rs = *engine_;
                    int fw = rs.get_resolution_manager().get_render_width();
                    int fh = rs.get_resolution_manager().get_render_height();
                    const uint8_t* fd = (const uint8_t*)rs.get_render_buffer().get_native_data();
                    FILE* ff = fopen("/tmp/eden_frame.ppm", "wb");
                    if (ff) {
                        fprintf(ff, "P6\n%d %d\n255\n", fw, fh);
                        for (int i = 0; i < fw * fh; i++) {
                            fputc(fd[i*4+2], ff); fputc(fd[i*4+1], ff); fputc(fd[i*4+0], ff);
                        }
                        fclose(ff);
                        EDEN_LOG("[Eden] Frame dumped to /tmp/eden_frame.ppm (" << fw << "x" << fh << ")");
                    }
                    return true;
                }
                break;

            case GLFW_KEY_T:  // Tall grass patch at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& organic_gen = engine_->get_worldgen_system().get_organic_generator();
                    GrassPatchSpec spec = GrassPatchSpec::tall_grass();

                    static int tall_patch_counter = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + tall_patch_counter++;
                    spec.blade_spec.random_seed = variation_seed;

                    kg::EntityID patch_id = organic_gen.generate_grass_patch(world_x, world_y, 0.0f, spec);
                    EDEN_LOG("[Eden] TALL grass patch (ID=" << patch_id << ") spawned at mouse (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case GLFW_KEY_Y:  // Dense grass patch at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& organic_gen = engine_->get_worldgen_system().get_organic_generator();
                    GrassPatchSpec spec = GrassPatchSpec::dense_grass();

                    static int dense_patch_counter = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + dense_patch_counter++;
                    spec.blade_spec.random_seed = variation_seed;

                    kg::EntityID patch_id = organic_gen.generate_grass_patch(world_x, world_y, 0.0f, spec);
                    EDEN_LOG("[Eden] DENSE grass patch (ID=" << patch_id << ") spawned at mouse (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case GLFW_KEY_U:  // Sparse grass patch at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& organic_gen = engine_->get_worldgen_system().get_organic_generator();
                    GrassPatchSpec spec = GrassPatchSpec::sparse_grass();

                    static int sparse_patch_counter = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + sparse_patch_counter++;
                    spec.blade_spec.random_seed = variation_seed;

                    kg::EntityID patch_id = organic_gen.generate_grass_patch(world_x, world_y, 0.0f, spec);
                    EDEN_LOG("[Eden] SPARSE grass patch (ID=" << patch_id << ") spawned at mouse (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case GLFW_KEY_I:  // Python snake at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& snake_gen = engine_->get_worldgen_system().get_snake_generator();
                    SnakeSpec spec = SnakeSpec::python();

                    // Apply variation: randomize length, thickness, and color
                    static int snake_counter = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + snake_counter++;

                    // Use seed for deterministic random variation
                    std::srand(variation_seed);
                    float length_variation = 0.8f + (std::rand() % 100) / 100.0f * 0.4f;  // 80%-120%
                    float thickness_variation = 0.85f + (std::rand() % 100) / 100.0f * 0.3f;  // 85%-115%
                    float color_variation = 0.9f + (std::rand() % 100) / 100.0f * 0.2f;  // 90%-110%

                    spec.total_length *= length_variation;
                    spec.body_thickness *= thickness_variation;
                    spec.scale_r *= color_variation;
                    spec.scale_g *= color_variation;
                    spec.scale_b *= color_variation;

                    kg::EntityID snake_id = snake_gen.generate_snake(world_x, world_y, 0.0f, spec);

                    // Register with dynamics system for autonomous wandering
                    engine_->get_serpent_locomotion().register_serpent(snake_id);

                    EDEN_LOG("[Eden] Python snake (ID=" << snake_id << ", "
                             << spec.total_length << "m, " << spec.num_segments << " segments) "
                             << "spawned at mouse (" << world_x << ", " << world_y << ")");
                    return true;
                }
                break;

            case GLFW_KEY_B:  // Butterfly at mouse
                {
                    double mouse_x, mouse_y;
                    GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
                    glfwGetCursorPos(window, &mouse_x, &mouse_y);

                    float world_x, world_y;
                    int render_x = static_cast<int>(mouse_x);
                    int render_y = static_cast<int>(mouse_y);
                    engine_->get_coord_transformer().screen_to_world(render_x, render_y, world_x, world_y);

                    auto& butterfly_gen = engine_->get_worldgen_system().get_butterfly_generator();
                    ButterflySpec spec = ButterflySpec::monarch();  // Default template

                    // Apply variation: randomize wing size and vibrant colors
                    static int butterfly_counter = 0;
                    unsigned int variation_seed = static_cast<unsigned int>(time(nullptr))
                                                + static_cast<unsigned int>(world_x * 1000 + world_y * 1000)
                                                + butterfly_counter++;

                    std::srand(variation_seed);

                    // Bell curve helper function (Central Limit Theorem approximation)
                    auto bell_curve = [](float base, float stddev, float min_clamp = 0.0f, float max_clamp = 10.0f) -> float {
                        // Sum of random values tends toward normal distribution
                        float variation = 0.0f;
                        for (int i = 0; i < 6; i++) {
                            variation += (std::rand() % 100) / 100.0f;
                        }
                        variation = (variation / 6.0f - 0.5f) * 2.0f;  // -1 to +1, bell-curved
                        return std::max(min_clamp, std::min(max_clamp, base + variation * stddev));
                    };

                    // Apply DRAMATIC bell-curved size variation for visible differences
                    // Base: 10cm, Range: 3cm (tiny) to 30cm (giant)
                    float size_variation = bell_curve(1.0f, 1.0f, 0.3f, 3.0f);  // Mean=1.0, stddev=100% (0.3x-3.0x)
                    spec.wing_span *= size_variation;
                    spec.wing_height *= size_variation;

                    // Scale body segments proportionally to wing size for coherent scaling
                    spec.head_size *= size_variation * bell_curve(1.0f, 0.2f, 0.5f, 1.5f);
                    spec.thorax_size *= size_variation * bell_curve(1.0f, 0.2f, 0.5f, 1.5f);
                    spec.abdomen_size *= size_variation * bell_curve(1.0f, 0.2f, 0.5f, 1.5f);

                    // Natural wild random colors with bell-curved variation
                    // Generate random base color (0.0-1.0 for each channel)
                    float base_r = (std::rand() % 100) / 100.0f;
                    float base_g = (std::rand() % 100) / 100.0f;
                    float base_b = (std::rand() % 100) / 100.0f;

                    // Make colors vibrant by boosting at least one channel
                    float max_channel = std::max({base_r, base_g, base_b});
                    if (max_channel < 0.5f) {
                        // Boost the dominant channel to make it vibrant
                        if (base_r >= base_g && base_r >= base_b) base_r = 0.8f + (std::rand() % 20) / 100.0f;
                        else if (base_g >= base_b) base_g = 0.8f + (std::rand() % 20) / 100.0f;
                        else base_b = 0.8f + (std::rand() % 20) / 100.0f;
                    }

                    // Apply bell-curved natural variation to each channel independently
                    spec.wing_r = bell_curve(base_r, 0.3f, 0.0f, 1.0f);  // ±30% variation
                    spec.wing_g = bell_curve(base_g, 0.3f, 0.0f, 1.0f);
                    spec.wing_b = bell_curve(base_b, 0.3f, 0.0f, 1.0f);

                    // Generate bell-curved number of thorax segments (1-4, mean=2)
                    // Each segment gets its own wing pair with unique random colors
                    float raw_thorax = bell_curve(2.0f, 1.2f, 1.0f, 4.0f);  // Mean=2, wide variation (1-4 segments)
                    int num_thorax = static_cast<int>(raw_thorax);
                    spec.num_thorax_segments = num_thorax;

                    // Generate unique random colors for each thorax segment's wings
                    spec.segment_wing_colors.clear();
                    for (int i = 0; i < num_thorax; i++) {
                        // Generate completely random RGB for this segment
                        float seg_base_r = (std::rand() % 100) / 100.0f;
                        float seg_base_g = (std::rand() % 100) / 100.0f;
                        float seg_base_b = (std::rand() % 100) / 100.0f;

                        // Apply vibrant boost to ensure at least one channel is bright
                        float seg_max = std::max({seg_base_r, seg_base_g, seg_base_b});
                        if (seg_max < 0.5f) {
                            if (seg_base_r >= seg_base_g && seg_base_r >= seg_base_b) seg_base_r = 0.8f + (std::rand() % 20) / 100.0f;
                            else if (seg_base_g >= seg_base_b) seg_base_g = 0.8f + (std::rand() % 20) / 100.0f;
                            else seg_base_b = 0.8f + (std::rand() % 20) / 100.0f;
                        }

                        // Apply bell curve variation to this segment's colors
                        float seg_r = bell_curve(seg_base_r, 0.3f, 0.0f, 1.0f);
                        float seg_g = bell_curve(seg_base_g, 0.3f, 0.0f, 1.0f);
                        float seg_b = bell_curve(seg_base_b, 0.3f, 0.0f, 1.0f);

                        // Store in spec
                        spec.segment_wing_colors.push_back({seg_r, seg_g, seg_b});
                    }

                    float start_z = 1.0f + (std::rand() % 100) / 100.0f * 2.0f;  // Start 1-3m in the air
                    kg::EntityID butterfly_id = butterfly_gen.generate_butterfly(world_x, world_y, start_z, spec);

                    // Register for flight dynamics
                    engine_->get_butterfly_flight().register_butterfly(butterfly_id);

                    EDEN_LOG("[Eden] Butterfly (ID=" << butterfly_id << ", "
                             << spec.wing_span << "m wingspan, " << spec.wing_beat_frequency << " Hz) "
                             << "spawned at mouse (" << world_x << ", " << world_y << ", " << start_z << ")");
                    return true;
                }
                break;

            default:
                // Pass to default handler
                return false;
        }
    }

    // Update game logic (movement handled by PlayerController)
    void update_game(float dt) override {
        if (!engine_) return;
        (void)dt;

        // Boulder drift tracking
        static int debug_frame = 0;
        debug_frame++;

        // -------------------------------------------------------------------
        // EVA + CAMERA POSITION TRACE
        // Per-frame snapshot of Eva's hips and the camera. Flags any frame
        // where either jumps more than 1 m between frames (probable physics
        // glitch or camera snap). Prints every 30 frames regardless so the
        // log shows steady-state position too.
        // -------------------------------------------------------------------
        if (eva_hips_id_ >= 0) {
            auto particles_view = engine_->get_particle_system().lock_particles_for_read();
            if (static_cast<size_t>(eva_hips_id_) < particles_view.size()) {
                const auto& hips = particles_view[eva_hips_id_];

                float cam_x = 0.0f, cam_y = 0.0f, cam_z = 0.0f;
                engine_->get_camera_system().get_position(cam_x, cam_y, cam_z);

                static float last_hx = hips.x, last_hy = hips.y, last_hz = hips.z;
                static float last_cx = cam_x, last_cy = cam_y, last_cz = cam_z;
                static int last_printed_frame = -1;

                float dh = std::sqrt((hips.x - last_hx)*(hips.x - last_hx)
                                   + (hips.y - last_hy)*(hips.y - last_hy)
                                   + (hips.z - last_hz)*(hips.z - last_hz));
                float dc = std::sqrt((cam_x - last_cx)*(cam_x - last_cx)
                                   + (cam_y - last_cy)*(cam_y - last_cy)
                                   + (cam_z - last_cz)*(cam_z - last_cz));

                bool jumped = (dh > 1.0f) || (dc > 1.0f);
                bool periodic = (debug_frame % 30 == 0);

                if (jumped || periodic) {
                    std::cout << "[EVA_TRACE F" << debug_frame << "]"
                              << " hips=(" << hips.x << "," << hips.y << "," << hips.z << ")"
                              << " vel=(" << hips.vx << "," << hips.vy << "," << hips.vz << ")"
                              << " cam=(" << cam_x << "," << cam_y << "," << cam_z << ")"
                              << " dh=" << dh << " dc=" << dc
                              << (jumped ? "  <<< JUMP" : "")
                              << std::endl;
                    last_printed_frame = debug_frame;
                }

                last_hx = hips.x; last_hy = hips.y; last_hz = hips.z;
                last_cx = cam_x; last_cy = cam_y; last_cz = cam_z;
            }
        }
        if (boulder_core_id_ >= 0 && debug_frame % 60 == 0) {
            auto particles_view = engine_->get_particle_system().lock_particles_for_read();
            if (static_cast<size_t>(boulder_core_id_) < particles_view.get().size()) {
                const auto& p = particles_view.get()[boulder_core_id_];
                float dx = p.x - boulder_initial_x_;
                float dy = p.y - boulder_initial_y_;
                float dz = p.z - boulder_initial_z_;
                float drift = std::sqrt(dx*dx + dy*dy);
                std::cout << "[BOULDER_DRIFT] frame=" << debug_frame
                          << " pos=(" << p.x << "," << p.y << "," << p.z << ")"
                          << " vel=(" << p.vx << "," << p.vy << "," << p.vz << ")"
                          << " drift=" << drift << "m"
                          << " dz=" << dz << "m" << std::endl;
            }
        }

        // Animate spirit lights: gentle floating paths
        if (!spirit_lights_.empty()) {
            static float time_acc = 0.0f;
            time_acc += 0.016f;  // ~60fps
            auto particles_view = engine_->get_particle_system().lock_particles_for_write();
            for (auto& sl : spirit_lights_) {
                if (sl.particle_id < 0 || static_cast<size_t>(sl.particle_id) >= particles_view.size())
                    continue;
                auto& p = particles_view[sl.particle_id];
                float t = time_acc * sl.speed;
                float phase = sl.phase;
                // Lissajous-like path: different frequencies per axis
                p.x = sl.base_x + std::sin(t * 0.7f + phase) * sl.radius;
                p.y = sl.base_y + std::cos(t * 0.5f + phase * 1.3f) * sl.radius;
                p.z = sl.base_z + std::sin(t * 1.1f + phase * 0.7f) * sl.radius * 0.3f;
                if (p.z < 2.0f) p.z = 2.0f;  // Keep elevated to avoid shadow hotspots
            }
        }
    }

    // Handle mouse button clicks - implements "click to embody" (debug mode only)
    bool handle_mouse_button(int button, int action, int mods) override {
        (void)mods;  // Unused

        std::cout << "[Eden] handle_mouse_button called - button=" << button
                  << " action=" << action << " (GLFW_MOUSE_BUTTON_LEFT=" << GLFW_MOUSE_BUTTON_LEFT
                  << ", GLFW_PRESS=" << GLFW_PRESS << ")" << std::endl;

        // Only handle left button press
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) {
            std::cout << "[Eden] Not left button press, ignoring" << std::endl;
            return false;
        }

        if (!engine_) {
            std::cout << "[Eden] ERROR: No engine pointer!" << std::endl;
            return false;
        }

        // CRITICAL: Click-to-embody only works in debug mode
        // This implements the debug possession requirement:
        // "enter in debug with `, and in there I should be able to hover with the mouse entities,
        //  when I click on any entity, I should be able to control it/move it with the keyboard"
        if (!engine_->get_show_debug_overlay()) {
            std::cout << "[Eden] Not in debug mode - ignoring click (press ` to enter debug mode)" << std::endl;
            return false;  // Only embody entities in debug mode
        }

        std::cout << "[Eden] In debug mode - processing click for entity possession" << std::endl;

        // Get mouse position (in window points)
        double mouse_x, mouse_y;
        GLFWwindow* window = static_cast<GLFWwindow*>(engine_->get_platform()->get_native_window_handle());
        glfwGetCursorPos(window, &mouse_x, &mouse_y);

        // CRITICAL: Scale mouse coordinates by DPI for Retina displays
        // GLFW returns window points, but get_object_at_pixel() expects framebuffer pixels
        // On 2x Retina: window 1600x1051 points → framebuffer 3200x2102 pixels
        float dpi_scale_x = engine_->get_platform()->get_dpi_scale_x();
        float dpi_scale_y = engine_->get_platform()->get_dpi_scale_y();
        int fb_x = static_cast<int>(mouse_x * dpi_scale_x);
        int fb_y = static_cast<int>(mouse_y * dpi_scale_y);

        std::cout << "[EDEN_CLICK] Mouse position: window=(" << mouse_x << "," << mouse_y << ") "
                  << "DPI=" << dpi_scale_x << "x "
                  << "framebuffer=(" << fb_x << "," << fb_y << ")" << std::endl;

        // PICKING_DEBUG: Query object_map for object at click position
        std::cout << "[EDEN_CLICK] Calling get_object_at_pixel(" << fb_x << "," << fb_y << ")..." << std::endl;

        // Get encoded object ID at click position (using framebuffer coordinates)
        int object_id = engine_->get_pixel_picker().get_object_at_pixel(fb_x, fb_y);

        std::cout << "[EDEN_CLICK] get_object_at_pixel returned: " << object_id << std::endl;
        if (object_id > 0) {
            if (ObjectID::is_particle(object_id)) {
                int decoded_id = ObjectID::get_particle_id(object_id);
                std::cout << "[EDEN_CLICK]   Decoded as PARTICLE " << decoded_id << std::endl;
            } else if (ObjectID::is_ui(object_id)) {
                std::cout << "[EDEN_CLICK]   Decoded as UI element" << std::endl;
            } else {
                std::cout << "[EDEN_CLICK]   Unknown object type" << std::endl;
            }
        } else {
            std::cout << "[EDEN_CLICK]   BACKGROUND (no object detected)" << std::endl;
        }

        // Check for background click
        if (object_id <= 0 || !ObjectID::is_particle(object_id)) {
            std::cout << "[Eden] Clicked background - stopping camera follow" << std::endl;
            engine_->get_camera_controller()->stop_following();
            return true;
        }

        // CRITICAL: Decode object_id to get actual particle_id
        // GPU rendering returns encoded IDs like 309919744
        // Must decode using ObjectID::get_particle_id() to get real particle ID
        int particle_id = ObjectID::get_particle_id(object_id);
        std::cout << "[Eden] Decoded to particle ID: " << particle_id << std::endl;

        if (particle_id < 0) {
            std::cout << "[Eden] ERROR: Failed to decode object ID " << object_id << std::endl;
            return true;
        }

        // Embody the clicked particle - make it controllable with WASD
        std::cout << "[Eden] Clicked particle " << particle_id << " - embodying!" << std::endl;

        // Get the particle to find its entity ID
        // particle_id is the RENDER INDEX (position in particles array), not the stable ID!
        auto particles_view = engine_->get_particle_system().lock_particles_for_read();
        const auto& particles = particles_view.get();

        kg::EntityID clicked_entity = kg::INVALID_ENTITY;
        if (particle_id >= 0 && static_cast<size_t>(particle_id) < particles.size()) {
            // Direct array access using render index
            clicked_entity = particles[particle_id].entity_id;
        }

        if (clicked_entity != kg::INVALID_ENTITY) {
            // Make this entity the input target (WASD will now move it)
            // NOTE: This also notifies camera_controller for zoom centering
            engine_->set_input_target_entity(clicked_entity);

            // Get entity's particles to find which one to follow with camera
            auto& kg = engine_->get_kg();
            auto entity_particles = kg.getEntityKGParticlesRecursive(clicked_entity);

            if (!entity_particles.empty()) {
                // Set camera follow particle (engine looks for this property)
                // Use the first particle's render index as camera target
                kg::RenderIndex first_render_idx = kg.getRenderIndex(entity_particles[0]);
                kg.setProperty(clicked_entity, "camera_follow_particle", std::to_string(first_render_idx));

                // Enable camera follow (camera will track the input target entity)
                engine_->set_camera_follow_enabled(true);
                engine_->set_camera_deadzone(5.0f);  // 5m deadzone before camera follows
                engine_->set_camera_follow_offset(25.0f);  // Isometric offset (same as Logomancers)

                std::cout << "[Eden] Input control transferred to entity " << clicked_entity << std::endl;
                std::cout << "[Eden] Camera now following particle " << first_render_idx << " of entity " << clicked_entity << std::endl;

                // DEBUG: Show what particles belong to this entity
                std::cout << "[EMBODY DEBUG] Entity " << clicked_entity << " has "
                          << entity_particles.size() << " particles (recursive)" << std::endl;

                // Show first few particle IDs
                int shown = 0;
                for (auto kg_pid : entity_particles) {
                    auto render_idx = kg.getRenderIndex(kg_pid);
                    std::cout << "  - KG particle " << kg_pid << " -> Render index " << render_idx << std::endl;
                    if (++shown >= 5) {
                        if (entity_particles.size() > 5) {
                            std::cout << "  ... and " << (entity_particles.size() - 5) << " more" << std::endl;
                        }
                        break;
                    }
                }
            }
        } else {
            std::cout << "[Eden] WARNING: Clicked particle " << particle_id << " has NO entity!" << std::endl;
        }

        return true;  // We handled the click
    }

    // KG Inspector now uses engine's KG automatically

    void setup_knowledge_graph(int eva_id, int adam_id, int trunk_id, int leaves_id, 
                               int apple_id, int serpent_id, int sun_id, int ambient_id) {
        EDEN_LOG("\n=== Binding Knowledge to Creation ===");

        auto& kg = engine_->get_kg();

        // Create KG entities for each mythological element
        eva_entity_ = kg.createEntity("Human");
        adam_entity_ = kg.createEntity("Human");
        tree_entity_ = kg.createEntity("KnowledgeTree");
        apple_entity_ = kg.createEntity("Fruit");
        serpent_entity_ = kg.createEntity("Serpent");
        sun_entity_ = kg.createEntity("LightSource");

        // Define relationships (using Phase 1 capabilities)
        kg.createRelation(tree_entity_, "BEARS", apple_entity_);
        kg.createRelation(apple_entity_, "CONTAINS",
            kg.createEntity("Knowledge"));
        kg.createRelation(serpent_entity_, "TEMPTS", eva_entity_);
        kg.createRelation(adam_entity_, "FOLLOWS", eva_entity_);
        kg.createRelation(sun_entity_, "ILLUMINATES",
            kg.createEntity("Garden"));

        // Bind particles to entities using stable KG particle IDs
        kg.createKGParticle(eva_entity_, static_cast<unsigned>(eva_id));
        kg.createKGParticle(adam_entity_, static_cast<unsigned>(adam_id));
        kg.createKGParticle(tree_entity_, static_cast<unsigned>(trunk_id));
        kg.createKGParticle(tree_entity_, static_cast<unsigned>(leaves_id));
        kg.createKGParticle(apple_entity_, static_cast<unsigned>(apple_id));
        kg.createKGParticle(serpent_entity_, static_cast<unsigned>(serpent_id));
        kg.createKGParticle(sun_entity_, static_cast<unsigned>(sun_id));
        kg.createKGParticle(sun_entity_, static_cast<unsigned>(ambient_id));

        EDEN_LOG("Knowledge Graph initialized with "
                  << kg.getStats().entity_count << " entities and "
                  << kg.getStats().relation_count << " relationships");
    }
    
    unsigned int create_initial_scene(std::function<int(const Particle&)> add_particle) override {
        EDEN_LOG("\n=== Creating the Garden of Eden ===");

        if (!add_particle) {
            EDEN_LOG("ERROR: No particle creation function provided!");
            return -1;
        }

        // NO scene gen, NO floor gen, NO Eva
        // Just raw particles added to particle system
        float eva_x = 25.0f;
        float eva_y = -25.0f;

        std::cout << "[MAIN] Pure shadow test mode" << std::endl;

        // === STREAMING STRATA FLOOR ===
        // Three layers: bedrock (bonded STONE slab) → sediment (loose STONE) →
        // organic surface (DIRT). Top surface still walkable at z≈0.55 m. A
        // heavy impact (meteor, dropped boulder) breaks bedrock bonds and
        // scatters the upper layers via normal physics.
        {
            auto& strata = engine_->get_worldgen_system().get_strata_floor_generator();
            strata.set_tile_size(4.0f);
            strata.set_tiles_per_chunk(5);      // 5x5 = 25 tiles per layer
            strata.set_tiles_per_entity(1);
            strata.set_load_radius(60.0f);
            strata.set_unload_radius(70.0f);

            std::vector<StrataLayerSpec> layers;
            StrataLayerSpec bedrock;
            bedrock.name = "bedrock";
            bedrock.material = Materials::Type::STONE;
            bedrock.thickness = 0.30f;
            bedrock.r = 0.35f; bedrock.g = 0.33f; bedrock.b = 0.30f;
            bedrock.color_variance = 0.05f;
            bedrock.bond_within_layer = true;
            bedrock.bond_strength = 8000.0f;    // N/m² material strength
            layers.push_back(bedrock);

            StrataLayerSpec sediment;
            sediment.name = "sediment";
            sediment.material = Materials::Type::STONE;
            sediment.thickness = 0.15f;
            sediment.r = 0.45f; sediment.g = 0.4f; sediment.b = 0.3f;
            sediment.color_variance = 0.08f;
            sediment.bond_within_layer = false;
            layers.push_back(sediment);

            StrataLayerSpec organic;
            organic.name = "organic";
            organic.material = Materials::Type::DIRT;
            organic.thickness = 0.10f;
            organic.r = 0.30f; organic.g = 0.45f; organic.b = 0.22f;  // grass-green
            organic.color_variance = 0.07f;
            organic.bond_within_layer = false;
            layers.push_back(organic);

            strata.set_layers(std::move(layers));

            // --- TRENCH (C) — N of Eva. Carve out sediment + organic in a
            // rectangle, leaving bedrock visible as an exposed cross-section.
            // A wanderer approaching from the south sees the grass stop and
            // rocky bedrock takes over.
            const float tr_min_x = 36.0f, tr_max_x = 44.0f;
            const float tr_min_y = -9.0f, tr_max_y = -1.0f;
            strata.set_tile_skip_mask([=](float x, float y, size_t layer_idx) {
                if (layer_idx == 0) return false;  // bedrock stays
                return x >= tr_min_x && x <= tr_max_x &&
                       y >= tr_min_y && y <= tr_max_y;
            });
            printf("[EDEN] Trench at (%.0f..%.0f, %.0f..%.0f) — top layers skipped, bedrock exposed\n",
                   tr_min_x, tr_max_x, tr_min_y, tr_max_y);

            strata.set_enabled(true);
            strata.preload_chunks_around(eva_x, eva_y, 3);
            std::cout << "[EDEN] Streaming strata floor: 4m tiles, 3 layers "
                      << "(bedrock bonded / sediment / organic), 60m radius around ("
                      << eva_x << "," << eva_y << ")" << std::endl;
        }
        if (add_particle) {

            // 3 IDENTICAL cubes — same size, same z, same everything
            for (int i = 0; i < 3; i++) {
                Particle cube = {};
                cube.shape = ParticleShape::BOX;
                cube.x = eva_x + i * 4.0f;
                cube.y = eva_y;
                cube.z = 0.6f;
                cube.width = 0.5f;
                cube.height = 0.5f;
                cube.thickness = 1.0f;
                cube.r = 1.0f; cube.g = 0.0f; cube.b = 0.0f; cube.a = 1.0f;
                cube.SetMaterial(Materials::Type::STONE);
                cube.reflectivity = 0.0f;
                cube.owner = ParticleOwner::STATIC;
                cube.is_at_rest = true;
                add_particle(cube);
            }
        }
        // A/B TEST: Two 0.3m cubes. Blue=STATIC, Green=DYNAMICS.
        if (add_particle) {
            Particle a = {};
            a.shape = ParticleShape::BOX;
            a.x = eva_x - 5.0f; a.y = eva_y - 3.0f; a.z = 0.6f;
            a.width = 0.3f; a.height = 0.3f; a.thickness = 0.8f;
            a.r = 0.0f; a.g = 0.0f; a.b = 1.0f; a.a = 1.0f;
            a.SetMaterial(Materials::Type::STONE);
            a.owner = ParticleOwner::STATIC;
            a.is_at_rest = true;
            add_particle(a);
            printf("[A/B] Blue STATIC at (%.0f,%.0f)\n", a.x, a.y);

            Particle b = a;
            b.x = eva_x - 5.0f; b.y = eva_y + 3.0f;
            b.r = 0.0f; b.g = 1.0f; b.b = 0.0f;
            b.owner = ParticleOwner::DYNAMICS;
            b.solver_mode = ParticleSolverMode::KINEMATIC;
            b.is_at_rest = false;
            add_particle(b);
            printf("[A/B] Green DYNAMICS at (%.0f,%.0f)\n", b.x, b.y);
        }

        // =====================================================================
        // DISCOVERABLE 3D FEATURES — sprinkled around the strata, not here at
        // Eva's spawn. Walking out in different directions reveals them.
        // Strata floor top is at z = 0.30 + 0.15 + 0.10 = 0.55 m.
        // =====================================================================

        // --- CAIRN (B) — NW of Eva, vertical stone stack.
        // 5 flat-ish stones placed directly on top of each other, precomputed
        // heights, is_at_rest=true. Tests that awake-onto-at-rest stacking
        // (the bug we just fixed) holds visually in play.
        if (add_particle) {
            const float cx = 15.0f, cy = -15.0f;
            const float floor_top = 0.55f;
            const float stone_thickness = 0.20f;
            const int   stone_count = 5;
            for (int i = 0; i < stone_count; ++i) {
                Particle s = {};
                s.shape     = ParticleShape::BOX;
                s.width     = 0.8f - i * 0.08f;   // taper toward the top
                s.height    = 0.8f - i * 0.08f;
                s.thickness = stone_thickness;
                s.size      = s.width;
                s.x         = cx + (i % 2 == 0 ? 0.03f : -0.03f);  // tiny wobble
                s.y         = cy + (i % 2 == 0 ? -0.02f : 0.02f);
                s.z         = floor_top + stone_thickness * 0.5f + i * stone_thickness;
                s.r         = 0.45f; s.g = 0.42f; s.b = 0.38f; s.a = 1.0f;
                s.friction  = 0.7f;
                s.reflectivity = 0.2f;
                s.SetMaterial(Materials::Type::STONE);
                s.is_at_rest = true;
                add_particle(s);
            }
            printf("[EDEN] Cairn (%.0f, %.0f) — %d stones, top z=%.2f\n",
                   cx, cy, stone_count, floor_top + stone_count * stone_thickness);
        }

        // --- PLATEAU (D) — SE of Eva, a 3x3 raised strata patch. Extra
        // bedrock+sediment+organic stacked directly on top of the normal
        // floor, so the plateau's sides expose the layer structure.
        if (add_particle) {
            const float px = 45.0f, py = -40.0f;
            const float floor_top = 0.55f;
            const float tile = 3.0f;
            const int   grid = 3;                // 3x3 tiles
            struct Layer { float th; float r, g, b; Materials::Type mat; };
            Layer plateau[3] = {
                {0.30f, 0.35f, 0.33f, 0.30f, Materials::Type::STONE}, // bedrock
                {0.15f, 0.45f, 0.40f, 0.30f, Materials::Type::STONE}, // sediment
                {0.10f, 0.30f, 0.45f, 0.22f, Materials::Type::DIRT},  // organic
            };
            float layer_z = floor_top;
            for (int li = 0; li < 3; ++li) {
                for (int gy = 0; gy < grid; ++gy) {
                    for (int gx = 0; gx < grid; ++gx) {
                        float x = px + (gx - grid/2) * tile;
                        float y = py + (gy - grid/2) * tile;
                        Particle p = {};
                        p.shape     = ParticleShape::BOX;
                        p.width     = tile;
                        p.height    = tile;
                        p.thickness = plateau[li].th;
                        p.size      = tile;
                        p.x = x; p.y = y;
                        p.z = layer_z + plateau[li].th * 0.5f;
                        p.r = plateau[li].r; p.g = plateau[li].g; p.b = plateau[li].b;
                        p.a = 1.0f;
                        p.friction = 0.5f;
                        p.reflectivity = 0.3f;
                        p.SetMaterial(plateau[li].mat);
                        p.is_at_rest = true;
                        add_particle(p);
                    }
                }
                layer_z += plateau[li].th;
            }
            printf("[EDEN] Plateau (%.0f, %.0f) — %dx%d tiles × 3 layers, top z=%.2f\n",
                   px, py, grid, grid, layer_z);
        }

        // Add Eva
        auto& humanoid_gen = engine_->get_worldgen_system().get_humanoid_generator();
        eva_physics_ = humanoid_gen.generate_humanoid_physics(
            eva_x + 10.0f, eva_y, 0.5f, -1, HumanoidSpec::eva(), false);
        eva_hips_id_ = eva_physics_.hips_id;

        // Create KG body graph: typed body parts with health/strength/flexibility,
        // FORGE stats on entity, particles linked. Everything in KG.
        auto& kg = engine_->get_kg();
        eva_physics_.create_kg_entities(kg, "Human", 180.0f, 800.0f);
        eva_entity_ = eva_physics_.entity_id;
        engine_->set_input_target_entity(eva_entity_);
        kg.setProperty(eva_entity_, "camera_follow_particle", std::to_string(eva_hips_id_));

        // Register with dynamics: joints, foot planting, and all animation clips
        // are auto-registered by register_humanoid_direct.
        engine_->get_humanoid_locomotion().register_humanoid_direct(
            eva_hips_id_,
            eva_physics_.left_leg_ids, eva_physics_.right_leg_ids,
            eva_physics_.left_arm_ids, eva_physics_.right_arm_ids,
            eva_physics_.torso_ids, 180.0f, 800.0f);

        // Delegate input to PlayerController (mouse look, WASD, abilities).
        // Speeds default to FORGE-derived limits from dynamics registration.
        auto& pc = engine_->get_player_controller();
        pc.set_controlled_entity(eva_entity_, eva_hips_id_);
        pc.on_punch = [this]() {
            engine_->get_humanoid_locomotion().play_fk_animation(eva_hips_id_, "punch_r");
        };

        // Keep Eva's cached hips ID + KG follow properties in sync when
        // chunks unload and particle array swap-removes reshuffle indices.
        // Without this the camera chases a random strata tile and Eva's
        // controls stop working (she appears to teleport).
        engine_->get_particle_system().add_swap_callback(
            [this](size_t old_idx, size_t new_idx) {
                if (eva_hips_id_ == (int)old_idx) {
                    eva_hips_id_ = (int)new_idx;
                    auto& kg = engine_->get_kg();
                    kg.setProperty(eva_entity_, "camera_follow_particle",
                                   std::to_string(eva_hips_id_));
                    engine_->get_player_controller()
                        .set_controlled_entity(eva_entity_, eva_hips_id_);
                    std::cout << "[EDEN] Eva hips re-mapped via swap: "
                              << old_idx << " → " << new_idx << std::endl;
                }
            });

        // Sanity probe — fires at frame 600 so we can verify the probe
        // mechanism actually runs. Does NOT set halt, so play continues.
        {
            auto& probe_mgr = engine_->get_deep_probe_manager();
            probe_mgr.register_probe("Sanity frame-600",
                [](Engine&, int f) { return f == 600; },
                [](Engine&, int f) {
                    std::printf("Sanity probe confirmed alive at frame %d\n", f);
                });
        }


        // Deep probe: custom Eva-leg-separation detector. Watches hips,
        // thighs, shins, feet. When ANY gluon-connected pair in the lower
        // body drifts > 0.1 m over baseline, halt the engine and dump a
        // full state snapshot so we can diagnose in isolation.
        {
            auto& probe_mgr = engine_->get_deep_probe_manager();
            probe_mgr.set_halt_on_trigger(true);

            // Capture body part IDs at spawn. These get kept in sync with
            // swap events via the monitor's swap callback (monitor already
            // tracks these).
            struct Baselines {
                std::vector<std::tuple<int, int, std::string, float>> pairs;
            };
            auto baselines = std::make_shared<Baselines>();

            // Entire humanoid body — not just legs. Previous runs showed
            // chest↔l_shoulder breaks first. Watch everything so whichever
            // joint gives way first triggers the halt.
            auto& ps = engine_->get_particle_system();
            struct LegId { int id; std::string name; };
            static const char* kPartNames[] = {
                "l_foot","l_toe","l_shin","l_thigh",
                "r_foot","r_toe","r_shin","r_thigh",
                "hips","abdomen","chest","neck","head",
                "upper_hair","back_hair","l_ear","r_ear",
                "l_eye_o","l_eye_i","r_eye_o","r_eye_i",
                "l_shoulder","r_shoulder",
                "l_upper_arm","l_forearm","l_hand",
                "r_upper_arm","r_forearm","r_hand"
            };
            std::vector<LegId> parts;
            for (size_t k = 0; k < eva_physics_.body_ids.size(); ++k) {
                LegId p;
                p.id   = eva_physics_.body_ids[k];
                p.name = (k < sizeof(kPartNames)/sizeof(kPartNames[0]))
                            ? kPartNames[k]
                            : ("part_" + std::to_string(k));
                parts.push_back(std::move(p));
            }
            auto leg_ids = std::make_shared<std::vector<LegId>>(std::move(parts));

            // Keep leg IDs AND baseline pair ids in sync with swap-and-pop.
            // Without the baseline update, stored pair indices go stale after
            // swap — distances would read random particles and look huge.
            engine_->get_particle_system().add_swap_callback(
                [leg_ids, baselines](size_t old_idx, size_t new_idx) {
                    for (auto& l : *leg_ids) {
                        if (l.id == (int)old_idx) l.id = (int)new_idx;
                    }
                    for (auto& [a, b, name, bl] : baselines->pairs) {
                        if (a == (int)old_idx) a = (int)new_idx;
                        if (b == (int)old_idx) b = (int)new_idx;
                    }
                });

            // Snapshot baselines on first use (lazy to avoid settling noise).
            probe_mgr.register_probe("Eva body separation",
                [leg_ids, baselines](Engine& e, int /*f*/) -> bool {
                    auto& physics = e.get_physics_system();
                    auto view = e.get_particle_system().lock_particles_for_read();
                    if (baselines->pairs.empty()) {
                        // First call: record every gluon-connected pair.
                        for (size_t a = 0; a < leg_ids->size(); ++a) {
                            for (size_t b = a + 1; b < leg_ids->size(); ++b) {
                                int ia = (*leg_ids)[a].id;
                                int ib = (*leg_ids)[b].id;
                                if (ia < 0 || ib < 0) continue;
                                if ((size_t)ia >= view.size() || (size_t)ib >= view.size()) continue;
                                if (!physics.get_gluon(ia, ib)) continue;
                                const Particle& pa = view[ia];
                                const Particle& pb = view[ib];
                                float dx=pa.x-pb.x, dy=pa.y-pb.y, dz=pa.z-pb.z;
                                float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                                baselines->pairs.emplace_back(
                                    ia, ib,
                                    (*leg_ids)[a].name + "<>" + (*leg_ids)[b].name,
                                    d);
                            }
                        }
                        return false;
                    }
                    // Check drift. Threshold matches HumanoidIntegrityMonitor
                    // (0.3 m). A tighter threshold catches normal walk motion
                    // as false positive: knee flex compresses bone-midpoint
                    // distance ~0.15 m, hip hike during stance bends thigh-hips
                    // ~0.05 m. 0.3 m is the "dismemberment vs normal pose"
                    // boundary.
                    for (auto& [ia, ib, name, baseline] : baselines->pairs) {
                        if (ia < 0 || ib < 0) continue;
                        if ((size_t)ia >= view.size() || (size_t)ib >= view.size()) continue;
                        const Particle& pa = view[ia];
                        const Particle& pb = view[ib];
                        float dx=pa.x-pb.x, dy=pa.y-pb.y, dz=pa.z-pb.z;
                        float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                        if (std::abs(d - baseline) > 0.3f) return true;
                    }
                    return false;
                },
                [leg_ids, baselines](Engine& e, int f) {
                    std::printf("Eva body separating at frame %d\n", f);
                    auto& physics = e.get_physics_system();
                    auto view = e.get_particle_system().lock_particles_for_read();
                    std::printf("  --- LEG PART POSITIONS + VELOCITIES ---\n");
                    for (const auto& l : *leg_ids) {
                        if (l.id < 0 || (size_t)l.id >= view.size()) continue;
                        const Particle& p = view[l.id];
                        std::printf("  %-8s P%d  pos=(%.3f, %.3f, %.3f)  vel=(%.3f, %.3f, %.3f)  at_rest=%d  mass=%.1f\n",
                            l.name.c_str(), l.id,
                            p.x, p.y, p.z, p.vx, p.vy, p.vz,
                            (int)p.is_at_rest, p.GetMass());
                    }
                    std::printf("  --- PAIR DISTANCES vs BASELINE ---\n");
                    for (auto& [ia, ib, name, baseline] : baselines->pairs) {
                        if (ia < 0 || ib < 0) continue;
                        if ((size_t)ia >= view.size() || (size_t)ib >= view.size()) continue;
                        const Particle& pa = view[ia];
                        const Particle& pb = view[ib];
                        float dx=pa.x-pb.x, dy=pa.y-pb.y, dz=pa.z-pb.z;
                        float d = std::sqrt(dx*dx + dy*dy + dz*dz);
                        float drift = d - baseline;
                        std::printf("  %-22s  baseline=%.3f  now=%.3f  drift=%+.3f%s\n",
                            name.c_str(), baseline, d, drift,
                            std::abs(drift) > 0.3f ? "  <<<" : "");
                    }
                    std::printf("  --- GLUON ANGULAR STATE (humanoid uses angular coupling, not linear) ---\n");
                    for (size_t a = 0; a < leg_ids->size(); ++a) {
                        for (size_t b = a + 1; b < leg_ids->size(); ++b) {
                            int ia = (*leg_ids)[a].id;
                            int ib = (*leg_ids)[b].id;
                            if (ia < 0 || ib < 0) continue;
                            auto* g = physics.get_gluon(ia, ib);
                            if (!g) continue;
                            std::printf("  %s<>%s  ang_stiff=%.1f  ang_damp=%.1f  rest=%.3f  max_rot=%.3f  enabled=%d\n",
                                (*leg_ids)[a].name.c_str(),
                                (*leg_ids)[b].name.c_str(),
                                g->angular_stiffness, g->angular_damping,
                                g->target_relative_rotation,
                                g->max_relative_rotation,
                                (int)g->enable_angular_constraint);
                        }
                    }

                    // Flush the tracer: last 60 frames of every write site
                    // that touched the traced Eva particles. Gradual drift
                    // (chest<>shoulder ~0.001 m/frame) only resolves over
                    // a longer window than the instantaneous write chain.
                    std::printf("  --- PARTICLE TRACER (last 60 frames of writes) ---\n");
                    e.get_particle_tracer().dump(std::cout, /*last_n_frames=*/60);
                });
            printf("[EDEN] Deep probe 'Eva legs separation' armed (halt-on-trigger)\n");
        }

        // Particle tracer: record every write-site mutation of Eva's lower
        // body, so the probe's dump shows the full causal chain leading up
        // to the crush (FK.apply → shape.snap → IK.chain → gravity → etc.).
        {
            auto& tracer = engine_->get_particle_tracer();
            // body_ids order (humanoid_generator.cpp):
            //   0-3:   l_foot, l_toe, l_shin, l_thigh
            //   4-7:   r_foot, r_toe, r_shin, r_thigh
            //   8:     hips
            //   9-12:  abdomen, chest, neck, head
            //   13-16: upper_hair, back_hair, l_ear, r_ear
            //   17-20: l_eye_o, l_eye_i, r_eye_o, r_eye_i
            //   21-22: l_shoulder, r_shoulder
            //   23-25: l_upper_arm, l_forearm, l_hand
            //   26-28: r_upper_arm, r_forearm, r_hand
            // Trace lower body + upper body pairs that drift during walking
            // (probe at Apr 17 showed chest<>l_shoulder +0.322m over 11 s).
            struct TraceSpec { size_t idx; const char* label; };
            static const TraceSpec kSpecs[] = {
                // Lower body (stance-root mechanism).
                {0, "eva/l_foot"},  {1, "eva/l_toe"},
                {2, "eva/l_shin"},  {3, "eva/l_thigh"},
                {4, "eva/r_foot"},  {5, "eva/r_toe"},
                {6, "eva/r_shin"},  {7, "eva/r_thigh"},
                {8, "eva/hips"},
                // Upper body (drift under investigation).
                {9, "eva/abdomen"}, {10, "eva/chest"},
                {11, "eva/neck"},   {12, "eva/head"},
                {21, "eva/l_shoulder"}, {22, "eva/r_shoulder"},
                {23, "eva/l_upper_arm"}, {26, "eva/r_upper_arm"},
            };
            for (const auto& s : kSpecs) {
                if (s.idx >= eva_physics_.body_ids.size()) continue;
                tracer.trace(eva_physics_.body_ids[s.idx], s.label);
            }
            // Keep tracer labels in sync with particle-index swaps. Read
            // the label first so it survives the index shuffle.
            engine_->get_particle_system().add_swap_callback(
                [&tracer](size_t old_idx, size_t new_idx) {
                    if (tracer.is_traced((int)old_idx)) {
                        std::string label = tracer.label_of((int)old_idx);
                        tracer.untrace((int)old_idx);
                        tracer.trace((int)new_idx, std::move(label));
                    }
                });
            printf("[EDEN] ParticleTracer armed on Eva's lower body (9 particles)\n");
        }

        // Opt in to the engine's humanoid integrity monitor. Every frame it
        // checks Eva for dismemberment (pair separation), collapse (hips z),
        // and body-spread explosion. Reports the first threshold crossing
        // and each subsequent worsening so we see the progression.
        {
            auto& monitor = engine_->get_humanoid_integrity_monitor();
            monitor.set_enabled(true);
            std::vector<int> body_ids(eva_physics_.body_ids.begin(),
                                       eva_physics_.body_ids.end());
            // Names in the order HumanoidGenerator::generate_humanoid_physics
            // pushes to body_ids. Only the first 13 are fixed; the tail (hair,
            // etc.) varies by spec — labelled generically so bounds match.
            std::vector<std::string> names;
            names.reserve(body_ids.size());
            static const char* kFixed[] = {
                "l_foot", "l_toe", "l_shin", "l_thigh",
                "r_foot", "r_toe", "r_shin", "r_thigh",
                "hips", "abdomen", "chest", "neck", "head"
            };
            for (size_t k = 0; k < body_ids.size(); ++k) {
                if (k < sizeof(kFixed)/sizeof(kFixed[0])) names.emplace_back(kFixed[k]);
                else names.emplace_back("part_" + std::to_string(k));
            }
            monitor.track("Eva", eva_hips_id_, body_ids, names);
            printf("[EDEN] Humanoid integrity monitor tracking Eva (%zu parts labelled)\n",
                   names.size());
        }

        // Store cube particle IDs for position logging
        std::vector<int> cube_ids;
        // Streetlight pole — tall thin trunk with light on top
        if (add_particle) {
            Particle pole = {};
            pole.shape = ParticleShape::BOX;
            pole.x = eva_x - 3.0f;
            pole.y = eva_y + 5.0f;
            pole.z = 2.5f;  // Center of 5m pole
            pole.width = 0.1f;
            pole.height = 0.1f;
            pole.thickness = 5.0f;  // 5m tall
            pole.r = 0.3f; pole.g = 0.2f; pole.b = 0.1f; pole.a = 1.0f;
            pole.SetMaterial(Materials::Type::WOOD_HARD);
            pole.owner = ParticleOwner::STATIC;
            pole.is_at_rest = true;
            add_particle(pole);
        }
        // === POPULATE THE GARDEN OF EDEN ===
        {
            auto& physics_tree_gen = engine_->get_worldgen_system().get_physics_tree_generator();
            auto& organic_gen = engine_->get_worldgen_system().get_organic_generator();
            auto& snake_gen = engine_->get_worldgen_system().get_snake_generator();
            auto& butterfly_gen = engine_->get_worldgen_system().get_butterfly_generator();

            // --- 12 trees: mix of sizes, spread across the garden ---
            {
                float spots[][3] = {
                    // Inner ring (small trees near Eva)
                    {eva_x - 8.0f,  eva_y + 10.0f, 3.0f},
                    {eva_x + 8.0f,  eva_y + 10.0f, 3.5f},
                    {eva_x - 12.0f, eva_y - 5.0f,  4.0f},
                    {eva_x + 12.0f, eva_y - 5.0f,  3.5f},
                    // Outer ring (larger trees, forest edge)
                    {eva_x - 20.0f, eva_y + 20.0f, 5.0f},
                    {eva_x + 20.0f, eva_y + 20.0f, 6.0f},
                    {eva_x - 25.0f, eva_y,          5.5f},
                    {eva_x + 25.0f, eva_y,          4.5f},
                    {eva_x - 15.0f, eva_y - 20.0f, 5.0f},
                    {eva_x + 15.0f, eva_y - 20.0f, 4.0f},
                    // Deep forest (walk to discover)
                    {eva_x - 30.0f, eva_y + 10.0f, 7.0f},
                    {eva_x + 30.0f, eva_y - 15.0f, 6.5f},
                };
                for (int i = 0; i < 12; i++) {
                    TreeSpec spec = TreeSpec::oak();
                    spec.height = spots[i][2];
                    spec.trunk_diameter = 0.1f + spots[i][2] * 0.03f;
                    spec.branch_depth = spots[i][2] > 5.0f ? 3 : 2;
                    spec.random_seed = 500 + i * 37;
                    PhysicsTreeResult tr = physics_tree_gen.generate_tree(
                        spots[i][0], spots[i][1], 0.0f, spec);
                    std::cout << "[TREE] " << i << " at (" << spots[i][0] << ","
                              << spots[i][1] << ") h=" << spec.height
                              << " segs=" << tr.total_segments << std::endl;
                }
            }

            // --- 80 rocks scattered across the landscape ---
            for (int i = 0; i < 80; i++) {
                float angle = i * 0.157f;
                float dist = 3.0f + (i % 8) * 4.0f;
                float rx = eva_x + dist * std::cos(angle) + (i % 3 - 1) * 0.5f;
                float ry = eva_y + dist * std::sin(angle) + (i % 2) * 0.5f;
                if (add_particle) {
                    Particle rock = {};
                    rock.shape = ParticleShape::BOX;
                    rock.x = rx; rock.y = ry; rock.z = 0.15f;
                    rock.width = 0.15f + (i % 5) * 0.08f;
                    rock.height = 0.15f + (i % 4) * 0.06f;
                    rock.thickness = 0.1f + (i % 3) * 0.08f;
                    rock.r = 0.45f + (i % 3) * 0.05f;
                    rock.g = 0.4f + (i % 2) * 0.05f;
                    rock.b = 0.35f + (i % 4) * 0.03f;
                    rock.a = 1.0f;
                    rock.SetMaterial(Materials::Type::STONE);
                    rock.owner = ParticleOwner::STATIC;
                    rock.is_at_rest = true;
                    add_particle(rock);
                }
            }

            // --- Stone wall ruin (rectangular, 6m x 4m, open on one side) ---
            if (add_particle) {
                float wall_cx = eva_x + 15.0f, wall_cy = eva_y - 10.0f;
                // North wall
                for (int i = 0; i < 6; i++) {
                    Particle block = {};
                    block.shape = ParticleShape::BOX;
                    block.x = wall_cx - 3.0f + i * 1.0f;
                    block.y = wall_cy + 2.0f;
                    block.z = 0.5f + (i % 2) * 0.1f;
                    block.width = 0.4f; block.height = 0.3f; block.thickness = 1.0f + (i % 3) * 0.2f;
                    block.r = 0.55f; block.g = 0.5f; block.b = 0.45f; block.a = 1.0f;
                    block.SetMaterial(Materials::Type::STONE);
                    block.owner = ParticleOwner::STATIC;
                    block.is_at_rest = true;
                    add_particle(block);
                }
                // West wall
                for (int i = 0; i < 4; i++) {
                    Particle block = {};
                    block.shape = ParticleShape::BOX;
                    block.x = wall_cx - 3.0f;
                    block.y = wall_cy - 2.0f + i * 1.0f;
                    block.z = 0.5f + (i % 2) * 0.15f;
                    block.width = 0.3f; block.height = 0.4f; block.thickness = 0.8f + (i % 2) * 0.3f;
                    block.r = 0.55f; block.g = 0.5f; block.b = 0.45f; block.a = 1.0f;
                    block.SetMaterial(Materials::Type::STONE);
                    block.owner = ParticleOwner::STATIC;
                    block.is_at_rest = true;
                    add_particle(block);
                }
                std::cout << "[EDEN] Stone wall ruin at (" << wall_cx << "," << wall_cy << ")" << std::endl;
            }

            // --- 40 grass patches (wider spread) ---
            for (int i = 0; i < 40; i++) {
                float angle = i * 0.314f;
                float dist = 3.0f + (i % 4) * 3.0f;
                float gx = eva_x + dist * std::cos(angle);
                float gy = eva_y + dist * std::sin(angle);
                GrassPatchSpec gspec = (i % 3 == 0) ? GrassPatchSpec::tall_grass()
                                                     : GrassPatchSpec::short_grass();
                organic_gen.generate_grass_patch(gx, gy, 0.0f, gspec);
            }

            // --- 3 NPCs (village) ---
            {
                float npc_spots[][2] = {
                    {eva_x + 5.0f, eva_y + 5.0f},
                    {eva_x + 16.0f, eva_y - 8.0f},   // near the stone ruin
                    {eva_x - 10.0f, eva_y + 15.0f},   // by the trees
                };
                for (int n = 0; n < 3; n++) {
                    HumanoidSpec spec = (n == 0) ? HumanoidSpec::hunter() : HumanoidSpec::eva();
                    float reflexes = 180.0f + n * 30.0f;
                    float grit = 500.0f + n * 150.0f;
                    PhysicsHumanoidResult npc = humanoid_gen.generate_humanoid_physics(
                        npc_spots[n][0], npc_spots[n][1], 0.5f, -1, spec, false);
                    npc.create_kg_entities(kg, "Humanoid", reflexes, grit);
                    engine_->get_humanoid_locomotion().register_humanoid_direct(
                        npc.hips_id,
                        npc.left_leg_ids, npc.right_leg_ids,
                        npc.left_arm_ids, npc.right_arm_ids,
                        npc.torso_ids, reflexes, grit);
                    std::cout << "[NPC] " << n << " at (" << npc_spots[n][0] << "," << npc_spots[n][1] << ")" << std::endl;
                }
            }

            // --- 2 serpents (autonomous wandering) ---
            for (int i = 0; i < 2; i++) {
                float sx = eva_x + 8.0f * (i == 0 ? -1.0f : 1.0f);
                float sy = eva_y + 6.0f * (i == 0 ? 1.0f : -1.0f);
                SnakeSpec sspec = SnakeSpec::python();
                sspec.total_length *= 0.8f + i * 0.3f;
                // Ask where the ground is. Eden's strata surface sits
                // at 0.55 m, so spawning at zero buried both serpents
                // half a metre down - which is why one was never seen.
                float sz = 0.0f;
                if (!engine_->get_ground_locator().surface_at(sx, sy, sz,
                                                              1.0f)) {
                    std::cout << "[EDEN] no ground under serpent " << i
                              << " at (" << sx << "," << sy
                              << ") - skipping rather than burying it"
                              << std::endl;
                    continue;
                }
                kg::EntityID snake_id = snake_gen.generate_snake(sx, sy, sz, sspec);
                engine_->get_serpent_locomotion().register_serpent(snake_id);
            }

            // --- 4 butterflies (flying dynamics) ---
            for (int i = 0; i < 4; i++) {
                float bx = eva_x + 4.0f * std::cos(i * 1.57f);
                float by = eva_y + 4.0f * std::sin(i * 1.57f);
                ButterflySpec bspec = ButterflySpec::monarch();
                bspec.wing_span *= 0.8f + (i % 3) * 0.2f;
                // Height above the GROUND, not above zero. Flight then
                // holds its own band from here.
                float bz = 0.0f;
                if (!engine_->get_ground_locator().surface_at(bx, by, bz, 1.0f))
                    bz = 0.0f;
                kg::EntityID butterfly_id = butterfly_gen.generate_butterfly(
                    bx, by, bz + 1.0f + i * 0.3f, bspec);
                engine_->get_butterfly_flight().register_butterfly(butterfly_id);
            }

            // --- Forest spirit lights (floating, animated) ---
            auto& ps = engine_->get_particle_system();
            struct SpiritDef { float ox, oy, oz; float str, rad; float r, g, b; float spd, wander; };
            // Strengths tuned so lux < 1 at emission_radius (no visible circle):
            // strength = 4π * radius² * target_lux_at_boundary (~1)
            // Visible at close range (50+ lux within 2-3m), fades to nothing at radius.
            // Spirit lights elevated (z >= 3m) to avoid bright-shadow artifact.
            // Low lights create inverse-square hotspots in shadow zones.
            SpiritDef spirits[] = {
                { -8.0f, 12.0f, 3.5f, 5000.0f, 15.0f, 1.0f, 0.7f, 0.3f, 0.7f, 4.0f },  // Warm orange
                { 15.0f,-10.0f, 4.0f, 8000.0f, 20.0f, 0.5f, 0.6f, 1.0f, 0.3f, 6.0f },  // Cool blue
                {  3.0f,  3.0f, 3.0f, 2000.0f, 10.0f, 0.3f, 1.0f, 0.4f, 1.2f, 2.0f },  // Green firefly
                {-12.0f, -8.0f, 3.5f, 3000.0f, 12.0f, 1.0f, 0.4f, 0.8f, 0.9f, 3.0f },  // Purple wisp
                { 10.0f, 15.0f, 4.0f, 6000.0f, 18.0f, 1.0f, 0.9f, 0.5f, 0.5f, 5.0f },  // Golden orb
            };
            for (int i = 0; i < 5; i++) {
                auto& s = spirits[i];
                float wx = eva_x + s.ox;
                float wy = eva_y + s.oy;
                int pid = (int)ps.queue_light(wx, wy, s.oz, s.str, s.rad, s.r, s.g, s.b);
                SpiritLight sl;
                sl.particle_id = pid;
                sl.base_x = wx;
                sl.base_y = wy;
                sl.base_z = s.oz;
                sl.phase = i * 1.257f;  // Stagger phases
                sl.speed = s.spd;
                sl.radius = s.wander;
                spirit_lights_.push_back(sl);
            }

            // Subscribe to particle swaps: keep SpiritLight particle_id valid
            // when chunks unload and particles get reshuffled.
            engine_->get_particle_system().add_swap_callback(
                [this](size_t old_idx, size_t new_idx) {
                    for (auto& sl : spirit_lights_) {
                        if (sl.particle_id == (int)old_idx) {
                            sl.particle_id = (int)new_idx;
                        }
                    }
                }
            );

            int particle_count = engine_->get_particle_system().count();
            EDEN_LOG("Garden of Eden: " << particle_count << " particles, 4 lights, "
                     << "12 trees, 40 rocks, 20 grass, 3 NPCs, 2 serpents, 4 butterflies");
        }
        EDEN_LOG("Scene complete");
        
        // Return Eva entity as the input target (all three particles will move together)
        return static_cast<unsigned int>(eva_entity_);
    }
};

int main(int argc, char* argv[]) {
    // --bench N: unattended benchmark. Scripted Eva walk, per-frame time
    // capture, self-exit after N frames with a stats summary. See
    // EdenApplication::bench_drive.
    // --size W H: window-size override (render resolution follows window
    // points under the NATIVE preset) — the audit's resolution ladder.
    // --retina: after init, raise render resolution to the real
    // framebuffer pixel size (3200x2102-class on 2x displays) — the
    // 60-FPS-at-retina target datapoint.
    // --headless: no window at all (create_display=false). The deferred
    // GPU pipeline still runs and [GPU_TIMESTAMP] still fires; render
    // resolution is exactly --size (no titlebar shave). Makes bench runs
    // immune to stray keyboard/mouse input.
    int bench_frames = 0;
    int size_w = 0, size_h = 0;
    bool retina_render = false;
    bool headless_bench = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--bench") == 0 && i + 1 < argc) {
            bench_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 2 < argc) {
            size_w = std::atoi(argv[++i]);
            size_h = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--retina") == 0) {
            retina_render = true;
        } else if (std::strcmp(argv[i], "--headless") == 0) {
            headless_bench = true;
        }
    }
    if (headless_bench && bench_frames <= 0) {
        std::cout << "[EDEN] --headless requires --bench N; ignoring --headless" << std::endl;
        headless_bench = false;
    }

    EDEN_LOG("\n╔══════════════════════════════════════╗");
    EDEN_LOG("║     EDEN - The Knowledge Garden     ║");
    EDEN_LOG("║   A Logosphere Knowledge Graph Demo  ║");
    EDEN_LOG("╚══════════════════════════════════════╝");
    
    std::cout << "[EDEN] Creating Eden application..." << std::endl;

    // Create Eden application (implements IApplication)
    EdenApplication eden_app;
    if (size_w > 0 && size_h > 0) {
        eden_app.set_window_size(size_w, size_h);
    }
    
    std::cout << "[EDEN] Creating Logosphere engine..." << std::endl;
    
    // Create the Logosphere engine with our application
    Engine engine(&eden_app);
    
    std::cout << "[EDEN] Configuring engine for Interactive mode..." << std::endl;
    
    // Configure engine for interactive mode with window
    EngineConfig config;
    config.mode = EngineMode::Interactive;  // Ensure we create a window
    config.window_width = (size_w > 0) ? size_w : 1600;
    config.window_height = (size_h > 0) ? size_h : 1200;
    config.window_title = "Eden - The Knowledge Garden";
    if (headless_bench) {
        config.create_display = false;
    }
    config.show_debug_overlay = false;
    config.show_kg_inspector = false;
    config.enable_chat_window = false;  // Eden is not an LLM-enabled game
    
    std::cout << "[EDEN] Calling engine.initialize() with mode="
              << (config.mode == EngineMode::Interactive ? "Interactive" : "Debug")
              << " create_display=" << (config.create_display ? "true" : "false")
              << " window=" << config.window_width << "x" << config.window_height << std::endl;
    
    // Initialize the engine - this creates the primordial observer particle
    int primordial_observer_id = engine.initialize(config);
    if (primordial_observer_id < 0) {
        std::cout << "[EDEN] ERROR: Failed to initialize Logosphere engine!" << std::endl;
        EDEN_LOG("Failed to initialize Logosphere engine!");
        return 1;
    }
    
    std::cout << "[EDEN] Engine initialized successfully with primordial_observer_id="
              << primordial_observer_id << std::endl;

    if (retina_render) {
        // Raise the render resolution from window points (NATIVE preset)
        // to the true framebuffer pixel size. GPURasterizer::initialize
        // is idempotent on size and re-runs on the next render.
        int fb_w = 0, fb_h = 0;
        engine.get_platform()->get_framebuffer_size(fb_w, fb_h);
        if (fb_w > 0 && fb_h > 0) {
            engine.set_render_resolution(fb_w, fb_h);
            std::cout << "[EDEN] --retina: render resolution raised to "
                      << fb_w << "x" << fb_h << std::endl;
        } else {
            std::cout << "[EDEN] --retina: framebuffer size unavailable, keeping default" << std::endl;
        }
    }
    
    EDEN_LOG("Primordial observer particle created with ID: " << primordial_observer_id);
    // Eden could transfer consciousness to Eva later with: engine.setup_observer(eva_id)
    
    // Set up isometric camera position
    // Camera at SOUTHWEST (-X,-Y) and ABOVE (+Z), looking at origin
    // CRITICAL: With 5m deadzone, camera can be 0-5m from Eva before it moves
    // If camera is >2.5m from Eva in ANY direction, deadzone will immediately snap it
    // We want camera southwest but within 2.5m radius of Eva
    auto& camera_system = engine.get_camera_system();
    float eva_start_x = 30.0f;
    float eva_start_y = -25.0f;

    // Isometric camera — close enough to see Eva clearly
    float camera_offset = 8.0f;
    camera_system.set_position(eva_start_x - camera_offset, eva_start_y - camera_offset, 10.0f);
    camera_system.look_at(eva_start_x, eva_start_y, 0.5f);

    // Camera follow
    engine.set_camera_follow_enabled(true);
    engine.set_camera_deadzone(2.0f);
    engine.set_camera_follow_offset(10.0f);
    EDEN_LOG("Camera follow enabled with 5m deadzone box");

    // Pre-load 4 chunks around Eva (3x3 grid centered on Eva)
    // DISABLED: no chunks for pure shadow test
    // engine.preload_chunks_around(30.0f, -25.0f, 3);
    EDEN_LOG("Pre-loaded surrounding chunks");

    EDEN_LOG("The Logos breathes life into the garden...");
    
    EDEN_LOG("\n══════════════════════════════════════");
    EDEN_LOG("Controls:");
    EDEN_LOG("  WASD - Move Eva through the garden");
    EDEN_LOG("  Mouse - Look around at creation");
    EDEN_LOG("  0 - Create/remove sun");
    EDEN_LOG("  1 - Test particle");
    EDEN_LOG("  2 - Serpent chain");
    EDEN_LOG("  3 - Wall");
    EDEN_LOG("  4 - Room");
    EDEN_LOG("  5 - Forest cluster");
    EDEN_LOG("  6 - Light at mouse");
    EDEN_LOG("  7 - Grass patch (short) at mouse");
    EDEN_LOG("  8 - Tree (Recursive Fractal) at mouse");
    EDEN_LOG("  9 - Tree (Space Colonization) at mouse");
    EDEN_LOG("  t - Tall grass patch at mouse");
    EDEN_LOG("  y - Dense grass patch at mouse");
    EDEN_LOG("  u - Sparse grass patch at mouse");
    EDEN_LOG("  i - Python snake at mouse (with variation)");
    EDEN_LOG("  b - Butterfly at mouse (with wing beats and flight)");
    EDEN_LOG("  G - Toggle light visualization");
    EDEN_LOG("  ESC - Leave the garden");
    EDEN_LOG("══════════════════════════════════════\n");
    
    std::cout << "[EDEN] Entering main loop..." << std::endl;

    // Enter the garden - Eden controls the main loop
    // FPS-independent timing: calculate real delta_time each frame
    int frame_count = 0;
    std::vector<double> bench_frame_ms;
    if (bench_frames > 0) bench_frame_ms.reserve(bench_frames);
    auto last_frame = std::chrono::high_resolution_clock::now();

    // Headless has no window: should_continue() is false by design there,
    // so bench-headless runs are bounded by the frame count alone.
    while (headless_bench || engine.should_continue()) {
        // Calculate real elapsed time (wall clock)
        auto current_frame = std::chrono::high_resolution_clock::now();
        double delta_time = std::chrono::duration<double>(current_frame - last_frame).count();
        last_frame = current_frame;

        // Cap delta to prevent huge jumps during debugging/pauses
        if (delta_time > 0.1) delta_time = 0.1;  // Max 100ms per frame

        if (bench_frames > 0) {
            eden_app.bench_drive(frame_count);
            if (frame_count > 0) bench_frame_ms.push_back(delta_time * 1000.0);
        }

        // Update with real time (TimeSystem handles scaling internally)
        engine.update(delta_time);
        engine.render();
        engine.present();

        frame_count++;
        if (bench_frames > 0 && frame_count >= bench_frames) break;
        if (frame_count == 1) {
            std::cout << "[EDEN] First frame rendered successfully" << std::endl;
        }
        // Log cube pixel colors from the rendered framebuffer
        if (frame_count == 60 || frame_count == 120) {
            auto& rb = engine.get_render_buffer();
            int rw = engine.get_resolution_manager().get_render_width();
            int rh = engine.get_resolution_manager().get_render_height();
            printf("[PIXEL_SAMPLE] frame=%d render=%dx%d\n", frame_count, rw, rh);
            // Sample a grid across the screen
            for (int sy = rh/4; sy < rh*3/4; sy += rh/8) {
                for (int sx = rw/6; sx < rw*5/6; sx += rw/8) {
                    auto px = rb.get_pixel(sx, sy);
                    printf("  (%4d,%4d) R=%3d G=%3d B=%3d\n", sx, sy, px.r, px.g, px.b);
                }
            }
        }

        // Log ALL particle data every 120 frames
        if (frame_count % 120 == 0) {
            auto& ps = engine.get_particle_system();
            auto particles = ps.lock_particles_for_read();
            int count = ps.count();
            std::cout << "[FRAME " << frame_count << "] particles=" << count << std::endl;
            for (int i = 0; i < count && i < 20; i++) {
                const auto& p = particles[i];
                const char* type = p.is_light_source ? "LIGHT" :
                                   (p.width > 5.0f ? "FLOOR" : "OBJ");
                std::cout << "  [" << i << "] " << type
                          << " pos=(" << p.x << "," << p.y << "," << p.z
                          << ") size=(" << p.width << "x" << p.height << "x" << p.thickness
                          << ") vel=(" << p.vx << "," << p.vy << "," << p.vz
                          << ") rest=" << p.is_at_rest
                          << " owner=" << (int)p.owner
                          << std::endl;
            }
        }
    }
    
    std::cout << "[EDEN] Main loop exited after " << frame_count << " frames" << std::endl;

    if (bench_frames > 0 && bench_frame_ms.size() > 130) {
        // Steady-state window: skip the first 120 frames (chunk preload,
        // shader warmup, the startup deletion batch).
        auto stats = [](std::vector<double> v, const char* label) {
            std::sort(v.begin(), v.end());
            double sum = 0.0;
            for (double t : v) sum += t;
            double avg = sum / v.size();
            double med = v[v.size() / 2];
            double p90 = v[(size_t)(v.size() * 0.90)];
            double p99 = v[(size_t)(v.size() * 0.99)];
            printf("[BENCH_%s] frames=%zu avg=%.1fms (%.1f FPS) median=%.1fms p90=%.1fms p99=%.1fms\n",
                   label, v.size(), avg, 1000.0 / avg, med, p90, p99);
        };
        stats(bench_frame_ms, "ALL");
        stats(std::vector<double>(bench_frame_ms.begin() + 120, bench_frame_ms.end()),
              "STEADY");
    }

    EDEN_LOG("\nYou have left the garden. Knowledge remains.");
    return 0;
}