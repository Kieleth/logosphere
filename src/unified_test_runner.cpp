#include "unified_test_runner.h"
#include "test_suite_coordinator.h"
#include "test_context.h"
#include "core/engine.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// Global test debug configuration definition
TestDebugConfig g_test_debug_config;

// Declare test functions directly
extern bool test_camera_offset();
extern bool test_hexagon_grid();
extern bool test_input_target_basic();
extern bool test_no_input_target();
extern bool test_particle_creation_interface();
extern bool test_wasd_compass_alignment();
extern bool test_round_trip_conversion();
extern bool test_isometric_inverse_math();
extern bool test_projection_mode_differences();
extern bool test_vision_fov();
extern bool test_vision_uses_input_target();
// REMOVED: Obsolete tests from early development
// These were basic unit tests that are now covered by comprehensive integration tests
extern bool test_wall_shadow_scenario();
extern bool test_basic_scenarios();
extern bool test_cube_around_light();
extern bool test_brightness_rendering();
extern bool test_brightness_normalization();
extern bool test_dynamic_lighting();
extern bool test_eden_headless_rendering();
extern bool test_light_distance_falloff();
extern bool test_culling_observer();
extern bool test_basic_shadow_casting();
extern bool test_large_shadow_casting();
extern bool test_small_shadow_casting();
extern bool test_shadow_rendering();
extern bool test_shadow_pixel_rendering();
extern bool test_coordinate_system(TestContext& ctx);
extern bool test_pixel_rasterization(TestContext& ctx);  // Tests Isometric projection
extern bool test_pixel_rasterization_isometric_depth(TestContext& ctx);  // Tests IsometricWithDepth
extern bool test_pixel_rasterization_perspective(TestContext& ctx);  // Tests Perspective
extern bool test_pixel_rasterization_cabinet(TestContext& ctx);  // Tests Cabinet
extern bool test_surface_light_grid_integration();  // NEW: Catches grid creation and pixel mapping bugs
extern bool test_basic_cube(TestContext& ctx);  // KISS: Simple cube face verification
extern bool test_triangle_shadow_artifacts(TestContext& ctx);  // Test triangle shadow distortions
extern bool test_shadow_ray_blocking(TestContext& ctx);  // Test shadow ray blocking
extern bool test_small_particle_shadow(TestContext& ctx);  // Test Eva-sized particle shadows
extern bool test_bvh_shadow_accuracy(TestContext& ctx);  // Test BVH shadow accuracy
extern bool test_wall_rendering_issue(TestContext& ctx);  // Test wall rendering issue
extern bool test_pixel_shadow_visual(TestContext& ctx);  // Test pixel shadow visual
extern bool test_basic_cube_projections(TestContext& ctx);  // Test 3D->2D projection pipeline
extern bool test_bvh_basic(TestContext& ctx);  // Test BVH basic functionality
extern bool test_bvh_performance(TestContext& ctx);  // Test BVH performance vs linear
extern bool test_entity_bvh_basic(TestContext& ctx);  // Test EntityBVH basic functionality
extern bool test_entity_bvh_directional_culling(TestContext& ctx);  // Test directional culling
extern bool test_entity_bvh_performance(TestContext& ctx);  // Test EntityBVH performance
extern bool test_uv_coordinates(TestContext& ctx);  // Test UV coordinate variation for pixel lighting
extern bool test_shadow_movement_direction(TestContext& ctx);  // Tests shadow movement with particle movement
extern bool test_kg_stable_particle_ids(TestContext& ctx);  // Tests KG stable particle ID system
extern bool test_surface_ray_tracing(TestContext& ctx);  // Tests surface-based ray tracing vs sphere approximation
extern bool test_eden_shadows(TestContext& ctx);  // Tests Eden-like shadow scenario
extern bool test_shadow_diagonal(TestContext& ctx);  // Debug diagonal shadow pattern
extern bool test_occlusion_culling(TestContext& ctx);  // Tests occlusion culling optimization
extern bool visual_test_top(TestContext& ctx);  // Visual test with light above cube
extern bool test_simd_edge_equations();  // Test SIMD edge equation evaluation
extern bool test_parallel_tiles(TestContext& ctx);  // Test parallel tile rendering (2025-01-22)
extern bool test_performance_single_light_20(TestContext& ctx);  // Performance benchmark: 20 particles, 1 light
extern bool test_multi_light_performance(TestContext& ctx);  // Performance impact of multiple lights
extern bool test_multi_light_progressive();  // Progressive multi-light scaling vehicle (STANDALONE; PARTICLES/LIGHTS/INTERACTIVE env)
extern bool test_sphere_lod_quality();       // Icosphere LOD quality vs cost (STANDALONE; INTERACTIVE/LOD_LEVELS env)
extern bool test_gpu_shadow_ray(TestContext& ctx);  // GPU compute shadow ray validation (Phase I MVP)
// extern bool test_gpu_multi_triangle(TestContext& ctx);  // GPU multi-triangle shadow ray (Phase I-B) - FILE MISSING
extern bool test_gpu_parallel_rays(TestContext& ctx);  // GPU parallel rays (Phase I-C)
extern bool test_gpu_ray_batching(TestContext& ctx);  // GPU ray batching (Phase II-A)
extern bool test_gpu_rasterize_minimal(TestContext& ctx);  // GPU rasterization minimal (Phase III STEP 1)
extern bool test_gpu_rasterize_triangle(TestContext& ctx);  // GPU triangle rasterization (Phase III STEP 2)
extern bool test_gpu_barycentric(TestContext& ctx);             // GPU barycentric interpolation (Phase III STEP 3)
extern bool test_gpu_lighting(TestContext& ctx);                // GPU lighting integration (Phase III STEP 7)
// REMOVED: test_uv_mapping_direction - old tangent space system
// REMOVED: test_tangent_space_generation - old tangent space system

// Physics tests
// REMOVED: test_load_bearing_constraints - dead feature (is_load_bearing_ field removed)
// REMOVED: test_eva_humanoid - old test
extern bool test_stiffness_stability();  // XPBD stability investigation - incremental complexity
// REMOVED: test_eva_floor_interaction - old test, Eva floor interaction now covered by test_physics_experiment_01 Case 5
// STANDALONE EXPERIMENT TESTS (create own Engine, run via --test <name> only):
extern bool test_physics_experiment_01();  // PHYSICS v2: Newtonian gravity basics (STANDALONE)
extern bool test_turtle_single_particle();  // TURTLE EXPERIMENT: Single particle on Turtle (STANDALONE)
extern bool test_oscillation_diagnostic();  // OSCILLATION DIAGNOSTIC: Gluoned tile on Turtle (STANDALONE)
extern bool test_physics_minimal();  // MINIMAL PHYSICS: Simplest tile-on-turtle cases (STANDALONE)
extern bool test_physics_minimal_v2();  // MINIMAL PHYSICS V2: Progressive complexity (STANDALONE)
extern bool test_physics_tree_roots();  // PHYSICS: Tree with roots on turtle (STANDALONE)
extern bool test_experiment_totem_builder();  // PHYSICS v2: Totem collision stacking (STANDALONE)
extern bool test_totem_gluon_nails();  // PHYSICS v2: Totem with gluon nail constraints (STANDALONE)
extern bool test_gluon_tree();  // PHYSICS v2: Tree structure with gluon branches (STANDALONE)
extern bool test_gluon_tree_v34();  // PHYSICS v3.4: Load propagation through contacts (STANDALONE)
extern bool test_physics_tree();  // PHYSICS: PhysicsTreeGenerator test (STANDALONE)
extern bool test_physics_tree_drift();  // PHYSICS: 10-second tree drift test (STANDALONE)
extern bool test_ancient_oak();  // PHYSICS: Ancient Oak tree test (STANDALONE)
extern bool test_sleep_diagnostics();  // PHYSICS: Sleep diagnostics test (STANDALONE)
extern bool test_tree_wiggly();  // PHYSICS: Tree wiggly test - same oak as logomancers (STANDALONE)
extern bool test_tree_shadow_wiggly();  // PHYSICS: Tree shadow stability test (STANDALONE)
extern bool test_falling_cube();  // PHYSICS: Falling cube collision test (STANDALONE)
extern bool test_physics_rock();  // PHYSICS: PhysicsRockGenerator test (STANDALONE)
extern bool test_contact_manifold();  // PHYSICS: SAT + clipping unit tests (STANDALONE)
extern bool test_body_coherence(TestContext&);  // PHYSICS: Body stays intact during walking
extern bool test_humanoid_movement_instrumented(TestContext&);  // PHYSICS: Full limb instrumentation for idle + walk
extern bool test_layered_floor_v1();  // PHYSICS: Organic layered floor generation (STANDALONE)
extern bool test_layered_floor_v2();  // PHYSICS: Frontier-based floor generation (STANDALONE)
extern bool test_layered_floor_v3();  // PHYSICS: Sequential strata with settling (STANDALONE)
extern bool test_strata_generator();  // WORLDGEN: Strata generator unit test (STANDALONE)
extern bool test_awake_onto_at_rest();  // PHYSICS: minimal 2-particle stack trace (STANDALONE)
extern bool test_humanoid_strata_walk();  // PHYSICS: humanoid walks streaming strata (STANDALONE)
extern bool test_spider_eva_shin_crush();  // PHYSICS: wedge — l_shin<>l_thigh crush during SW walk (STANDALONE)
extern bool test_stance_foot_invariance();  // PHYSICS: kinematic-root TDD wedge — stance foot stays on ground (STANDALONE)
extern bool test_walk_forward_progress();  // PHYSICS: kinematic-root post-shift wedge — Eva walks forward as intended (STANDALONE)
extern bool test_strafe_progress();  // PHYSICS: strafe moves body in intent direction (STANDALONE)
extern bool test_rotation_cascade_yaw();  // PHYSICS: head→chest→hips yaw cascade (STANDALONE)
extern bool test_turn_in_place_foot_step();  // PHYSICS: feet step under rotating hips (STANDALONE)
extern bool test_rotation_during_walk();  // PHYSICS: walk+yaw → curved path (STANDALONE)
extern bool test_strafe_sidestep_pattern();  // PHYSICS: strafe sidestep — no midline cross (STANDALONE)
extern bool test_leg_shoot_out_during_rotation();  // PHYSICS: no leg teleport during yaw rotation (STANDALONE)
extern bool test_idle_pose_stability();  // PHYSICS: standing Eva — feet stay under body (STANDALONE)
extern bool test_gluon_angular_drive_converges();  // PHYSICS-DRIVE: gluon PD drive convergence (Phase 1, STANDALONE)
extern bool test_gluon_3axis_drive_converges();     // ROTATIONAL-DOF: gluon 3-axis PD drive (Stage 2, STANDALONE)
extern bool test_particle_quat_euler_sync();        // ROTATIONAL-DOF: quat-driven Euler derivation (Stage 3, STANDALONE)
extern bool test_physics_drive_shoulder_multiaxis(); // ROTATIONAL-DOF: humanoid shoulder quat drive (Stage 3, STANDALONE)
extern bool test_physics_drive_two_joints();        // PHYSICS-DRIVE: concurrent neck + shoulder drive (Phase 3a, STANDALONE)
extern bool test_physics_drive_arm_chain();         // PHYSICS-DRIVE: shoulder+elbow+wrist chain (Phase 3b, STANDALONE)
extern bool test_gluon_chain_converges();           // PHYSICS-DRIVE: isolated 3-particle chain probe (STANDALONE)
extern bool test_physics_drive_full_idle();         // PHYSICS-DRIVE: whole-body idle on Eva (Phase 3c, STANDALONE)
extern bool test_physics_drive_walk_upper_body();   // PHYSICS-DRIVE: walk with upper body physics-driven (Phase 4a, STANDALONE)
extern bool test_gluon_distance_holds_offset();      // GLUON-REHAB: gluon closes position gap from offset (TDD, STANDALONE)
extern bool test_pin_gluon_holds_particle();         // PHYSICS-DRIVE: pin gluon holds DYNAMIC at KINEMATIC anchor (Phase 4b-2 mechanism, STANDALONE)
extern bool test_physics_drive_walk_legs();          // PHYSICS-DRIVE: walk with physics-driven legs on Eva (Phase 4b C3 capstone, STANDALONE)
extern bool test_physics_drive_walk_legs_fast();     // PHYSICS-DRIVE: 2 m/s walk on Eva — substepping proof (V4.11, STANDALONE)
extern bool test_phase_e_diagnostic();                // PHASE-E: headless RCA harness for "shooting stuff" Eden bug (STANDALONE)
extern bool test_collision_event_swap_integrity();    // PHYSICS: collision events stay valid across particle delete/swap (STANDALONE)
extern bool test_gluon_removal_unindexes();           // PHYSICS: marked gluon removal also clears the pair index (STANDALONE)
extern bool test_position_authority();                // PHYSICS: one integrator per particle — speed cap binds ground speed (STANDALONE)
extern bool test_deferred_deletion_integrity();       // PHYSICS: queued deletions survive swap-and-pop (STANDALONE)
extern bool test_pin_anchor_persistence();             // PHYSICS: foot-plant anchors moved, never churned (STANDALONE)
extern bool test_shadow_edge_quantization();           // RENDERING: hard-shadow staircase catcher (STANDALONE)
extern bool test_ui_text_dpi();                        // UI: HUD glyph scale follows render resolution (STANDALONE)
extern bool test_gpu_wait_no_fixed_sleep();            // RENDERING: GPU drain has no fixed sleep (STANDALONE)
extern bool test_ui_overlay_plane();                   // UI: overlay plane separation + composite (STANDALONE)
extern bool test_ui_overlay_survives_frames();         // UI: flicker regression lock (STANDALONE)
extern bool test_interaction_filtering();              // INTERACTION: profile filter seam (STANDALONE)
extern bool test_interaction_volume_forces();          // INTERACTION: medium forces (STANDALONE)
extern bool test_interaction_transformations();        // INTERACTION: declarative rules (STANDALONE)
extern bool test_physics_drive_neck_yaw();  // PHYSICS-DRIVE: humanoid neck yaw physics-driven (Phase 2, STANDALONE)
extern bool test_joint_hierarchy_swap_integrity();  // PHYSICS: joint hierarchy ids stay in sync across chunk swaps (STANDALONE)
extern bool test_reverse_leg_chain();  // ANIMATION: reverse_leg_chain_to_hip roundtrip (kinematic-root Stage 1b, STANDALONE)
extern bool test_soft_shadows(TestContext& ctx);  // RENDERING: Soft shadow visual test
extern bool test_ssgi_visual();  // RENDERING: SSGI + soft shadows visual test suite (STANDALONE)
extern bool test_shadow_frame_stability();  // RENDERING: Shadow frame stability (GI convergence)
extern bool test_shadow_penumbra_exists();  // RENDERING: Shadow penumbra gradient exists
extern bool test_shadow_no_color_artifacts();  // RENDERING: No colored artifacts in grayscale scene
// REMOVED: test_physics_profile - file does not exist
// REMOVED: test_eva_physics - old test
extern bool test_eva_movement(TestContext& ctx);  // PHYSICS: Eva movement stress test (STANDALONE)
extern bool test_capability_profile(TestContext& ctx);  // Engine: CapabilityProfile + DynamicsParams
extern bool test_spirit_light_artifacts(TestContext& ctx);  // RENDERING: Spirit light shadow/box artifacts
extern bool test_shadow_invariant(TestContext& ctx);  // RENDERING: Shadow can only reduce light, never increase
extern bool test_rock_shadow_artifact(TestContext& ctx);  // RENDERING: Rock shadow vertical streak artifact
extern bool test_gi_radial_artifact(TestContext& ctx);  // RENDERING: GI noise radial artifact
extern bool test_gi_speckle(TestContext& ctx);  // RENDERING: GI speckle on floor
extern bool test_ssao_basic(TestContext& ctx);  // RENDERING: SSAO contact shadows
extern bool test_gi_bounce(TestContext& ctx);   // RENDERING: GI colored bounce from red obelisk
extern bool test_ddgi_bounce(TestContext& ctx); // RENDERING: DDGI probe-based colored bounce
extern bool test_bvh_stress(TestContext& ctx);  // PERF: BVH rebuild stall during chunk streaming
extern bool test_chunk_floor(TestContext& ctx); // WORLDGEN: Floor chunk streaming position
extern bool test_tile_sticking(TestContext& ctx); // PHYSICS: Tile boundary sticking (telemetry)
extern bool test_memory_leak(TestContext& ctx); // PERF: Memory leak during chunk streaming
extern bool test_humanoid_ground(TestContext& ctx);  // PHYSICS: Humanoid ground support test
extern bool test_humanoid_ground_multitile(TestContext& ctx);  // PHYSICS: Humanoid multi-tile ground test
extern bool test_humanoid_rotation(TestContext& ctx);  // DYNAMICS: Humanoid rotation/look-at test
extern bool test_humanoid_lie_down(TestContext& ctx);  // DYNAMICS: Humanoid lie down/stand up test
// REMOVED: test_physics_profile - file does not exist
extern bool test_physics_experiment_03_eva_constraints();  // PHYSICS v2: Eva body parts + constraints (incremental)
extern bool test_humanoid_impact();  // PHYSICS: Humanoid impact response (projectile knockback)

extern void run_engine_neutrality_tests();

// Physics and module tests
extern int run_lighting_physics_tests();
extern int run_kg_basic_tests();
extern int run_isometric_surface_tests();
extern int run_performance_benchmark();
extern int run_camera_culling_tests();

// UI tests
extern bool test_ui_scaling_functionality();

// Create a static test registry
static std::unordered_map<std::string, std::function<bool(TestContext&)>> create_test_registry() {
    std::unordered_map<std::string, std::function<bool(TestContext&)>> registry;
    
    // CONVERTED tests (use TestContext)
    registry["test_coordinate_system"] = test_coordinate_system;
    registry["test_pixel_rasterization"] = test_pixel_rasterization;  // Isometric projection
    registry["test_pixel_rasterization_isometric_depth"] = test_pixel_rasterization_isometric_depth;
    registry["test_pixel_rasterization_perspective"] = test_pixel_rasterization_perspective;
    registry["test_pixel_rasterization_cabinet"] = test_pixel_rasterization_cabinet;
    registry["test_basic_cube"] = test_basic_cube;
    registry["test_triangle_shadow_artifacts"] = test_triangle_shadow_artifacts;
    registry["test_shadow_ray_blocking"] = test_shadow_ray_blocking;
    registry["test_small_particle_shadow"] = test_small_particle_shadow;
    registry["test_bvh_shadow_accuracy"] = test_bvh_shadow_accuracy;
    registry["test_wall_rendering_issue"] = test_wall_rendering_issue;
    registry["test_pixel_shadow_visual"] = test_pixel_shadow_visual;
    registry["test_basic_cube_projections"] = test_basic_cube_projections;
    registry["test_bvh_basic"] = test_bvh_basic;
    registry["test_bvh_performance"] = test_bvh_performance;
    registry["test_entity_bvh_basic"] = test_entity_bvh_basic;
    registry["test_entity_bvh_directional_culling"] = test_entity_bvh_directional_culling;
    registry["test_entity_bvh_performance"] = test_entity_bvh_performance;
    registry["test_uv_coordinates"] = test_uv_coordinates;
    registry["test_shadow_movement_direction"] = test_shadow_movement_direction;
    registry["test_kg_stable_particle_ids"] = test_kg_stable_particle_ids;
    registry["test_surface_ray_tracing"] = test_surface_ray_tracing;
    registry["test_eden_shadows"] = test_eden_shadows;
    registry["test_shadow_diagonal"] = test_shadow_diagonal;
    registry["test_occlusion_culling"] = test_occlusion_culling;
    registry["test_parallel_tiles"] = test_parallel_tiles;
    registry["visual_test_top"] = visual_test_top;  // Visual test with light above
    // REMOVED: test_uv_mapping_direction - old tangent space system
    // REMOVED: test_tangent_space_generation - old tangent space system
    
    // SIMD test (doesn't use TestContext)
    registry["test_simd_edge_equations"] = [](TestContext&) { return test_simd_edge_equations(); };
    
    // Performance tests
    registry["test_performance_single_light_20"] = test_performance_single_light_20;
    registry["test_multi_light_performance"] = test_multi_light_performance;
    registry["test_multi_light_progressive"] = [](TestContext&) { return test_multi_light_progressive(); };  // STANDALONE: own engine
    registry["test_sphere_lod_quality"] = [](TestContext&) { return test_sphere_lod_quality(); };  // STANDALONE: own engine

    // GPU compute tests (Phase I MVP, I-B, I-C, II-A)
    registry["test_gpu_shadow_ray"] = test_gpu_shadow_ray;
    // registry["test_gpu_multi_triangle"] = test_gpu_multi_triangle;  // FILE MISSING
    registry["test_gpu_parallel_rays"] = test_gpu_parallel_rays;
    registry["test_gpu_ray_batching"] = test_gpu_ray_batching;  // Phase II-A: Batched rays
    registry["test_gpu_rasterize_minimal"] = test_gpu_rasterize_minimal;  // Phase III STEP 1: Minimal rasterization
    registry["test_gpu_rasterize_triangle"] = test_gpu_rasterize_triangle;  // Phase III STEP 2: Triangle rasterization
    registry["test_gpu_barycentric"] = test_gpu_barycentric;          // Phase III STEP 3: Barycentric interpolation
    registry["test_gpu_lighting"] = test_gpu_lighting;                // Phase III STEP 7: Lighting integration

    // Physics tests
    // REMOVED: registry["test_load_bearing"] - dead feature (is_load_bearing_ field removed)
    // REMOVED: test_eva_humanoid - old test
    registry["test_stiffness_stability"] = [](TestContext&) { return test_stiffness_stability(); };  // XPBD stability investigation
    // REMOVED: test_eva_floor_interaction - old test, Eva floor interaction now covered by test_physics_experiment_01 Case 5
    // STANDALONE EXPERIMENT TESTS (create own Engine - registered but don't use test harness Engine):
    registry["test_physics_experiment_01"] = [](TestContext&) { return test_physics_experiment_01(); };  // PHYSICS v2: Newtonian gravity basics (STANDALONE)
    registry["test_turtle_single_particle"] = [](TestContext&) { return test_turtle_single_particle(); };  // TURTLE EXPERIMENT: Single particle on Turtle (STANDALONE)
    registry["test_oscillation_diagnostic"] = [](TestContext&) { return test_oscillation_diagnostic(); };  // OSCILLATION DIAGNOSTIC: Gluoned tile on Turtle (STANDALONE)
    registry["test_physics_minimal"] = [](TestContext&) { return test_physics_minimal(); };  // MINIMAL PHYSICS: Simplest tile-on-turtle cases (STANDALONE)
    registry["test_physics_minimal_v2"] = [](TestContext&) { return test_physics_minimal_v2(); };  // MINIMAL PHYSICS V2: Progressive complexity (STANDALONE)
    registry["test_physics_tree_roots"] = [](TestContext&) { return test_physics_tree_roots(); };  // PHYSICS: Tree with roots on turtle (STANDALONE)
    registry["test_experiment_totem_builder"] = [](TestContext&) { return test_experiment_totem_builder(); };  // PHYSICS v2: Totem collision stacking (STANDALONE)
    registry["test_totem_gluon_nails"] = [](TestContext&) { return test_totem_gluon_nails(); };  // PHYSICS v2: Totem with gluon nail constraints (STANDALONE)
    registry["test_gluon_tree"] = [](TestContext&) { return test_gluon_tree(); };  // PHYSICS v2: Tree structure with gluon branches (STANDALONE)
    registry["test_gluon_tree_v34"] = [](TestContext&) { return test_gluon_tree_v34(); };  // PHYSICS v3.4: Load propagation through contacts (STANDALONE)
    registry["test_physics_tree"] = [](TestContext&) { return test_physics_tree(); };  // PHYSICS: PhysicsTreeGenerator test (STANDALONE)
    registry["test_physics_tree_drift"] = [](TestContext&) { return test_physics_tree_drift(); };  // PHYSICS: 10-second tree drift test (STANDALONE)
    registry["test_ancient_oak"] = [](TestContext&) { return test_ancient_oak(); };  // PHYSICS: Ancient Oak tree test (STANDALONE)
    registry["test_sleep_diagnostics"] = [](TestContext&) { return test_sleep_diagnostics(); };  // PHYSICS: Sleep diagnostics test (STANDALONE)
    registry["test_tree_wiggly"] = [](TestContext&) { return test_tree_wiggly(); };  // PHYSICS: Tree wiggly test - same oak as logomancers (STANDALONE)
    registry["test_tree_shadow_wiggly"] = [](TestContext&) { return test_tree_shadow_wiggly(); };  // PHYSICS: Tree shadow stability test (STANDALONE)
    registry["test_falling_cube"] = [](TestContext&) { return test_falling_cube(); };  // PHYSICS: Falling cube collision test (STANDALONE)
    registry["test_physics_rock"] = [](TestContext&) { return test_physics_rock(); };  // PHYSICS: PhysicsRockGenerator test (STANDALONE)
    registry["test_contact_manifold"] = [](TestContext&) { return test_contact_manifold(); };  // PHYSICS: SAT + clipping (STANDALONE)
    registry["test_body_coherence"] = [](TestContext& ctx) { return test_body_coherence(ctx); };  // PHYSICS: Body coherence
    registry["test_humanoid_movement_instrumented"] = [](TestContext& ctx) { return test_humanoid_movement_instrumented(ctx); };  // PHYSICS: Full movement instrumentation
    registry["test_layered_floor_v1"] = [](TestContext&) { return test_layered_floor_v1(); };  // PHYSICS: Organic layered floor (STANDALONE)
    registry["test_layered_floor_v2"] = [](TestContext&) { return test_layered_floor_v2(); };  // PHYSICS: Frontier-based floor (STANDALONE)
    registry["test_layered_floor_v3"] = [](TestContext&) { return test_layered_floor_v3(); };  // PHYSICS: Sequential strata (STANDALONE)
    registry["test_strata_generator"] = [](TestContext&) { return test_strata_generator(); };  // WORLDGEN: Strata generator (STANDALONE)
    registry["test_awake_onto_at_rest"] = [](TestContext&) { return test_awake_onto_at_rest(); };  // PHYSICS: minimal 2-particle stack trace (STANDALONE)
    registry["test_humanoid_strata_walk"] = [](TestContext&) { return test_humanoid_strata_walk(); };  // PHYSICS: humanoid walks streaming strata (STANDALONE)
    registry["test_spider_eva_shin_crush"] = [](TestContext&) { return test_spider_eva_shin_crush(); };  // PHYSICS: wedge — l_shin<>l_thigh crush during SW walk (STANDALONE)
    registry["test_stance_foot_invariance"] = [](TestContext&) { return test_stance_foot_invariance(); };  // PHYSICS: kinematic-root TDD wedge (STANDALONE)
    registry["test_walk_forward_progress"] = [](TestContext&) { return test_walk_forward_progress(); };  // PHYSICS: walk-forward wedge (STANDALONE)
    registry["test_strafe_progress"] = [](TestContext&) { return test_strafe_progress(); };  // PHYSICS: strafe direction (STANDALONE)
    registry["test_rotation_cascade_yaw"] = [](TestContext&) { return test_rotation_cascade_yaw(); };  // PHYSICS: yaw cascade (STANDALONE)
    registry["test_turn_in_place_foot_step"] = [](TestContext&) { return test_turn_in_place_foot_step(); };  // PHYSICS: step under rotating hips (STANDALONE)
    registry["test_rotation_during_walk"] = [](TestContext&) { return test_rotation_during_walk(); };  // PHYSICS: walk+yaw curved path (STANDALONE)
    registry["test_strafe_sidestep_pattern"] = [](TestContext&) { return test_strafe_sidestep_pattern(); };  // PHYSICS: strafe no midline cross (STANDALONE)
    registry["test_leg_shoot_out_during_rotation"] = [](TestContext&) { return test_leg_shoot_out_during_rotation(); };  // PHYSICS: no leg teleport during rotation (STANDALONE)
    registry["test_idle_pose_stability"] = [](TestContext&) { return test_idle_pose_stability(); };  // PHYSICS: standing Eva feet stay under body (STANDALONE)
    registry["test_gluon_angular_drive_converges"] = [](TestContext&) { return test_gluon_angular_drive_converges(); };  // PHYSICS-DRIVE: gluon PD convergence (STANDALONE)
    registry["test_gluon_3axis_drive_converges"] = [](TestContext&) { return test_gluon_3axis_drive_converges(); };     // ROTATIONAL-DOF: gluon 3-axis PD drive (STANDALONE)
    registry["test_particle_quat_euler_sync"] = [](TestContext&) { return test_particle_quat_euler_sync(); };           // ROTATIONAL-DOF: quat-driven Euler sync (STANDALONE)
    registry["test_physics_drive_shoulder_multiaxis"] = [](TestContext&) { return test_physics_drive_shoulder_multiaxis(); }; // ROTATIONAL-DOF: shoulder quat drive (STANDALONE)
    registry["test_physics_drive_two_joints"] = [](TestContext&) { return test_physics_drive_two_joints(); };    // PHYSICS-DRIVE: two-joint concurrent (STANDALONE)
    registry["test_physics_drive_arm_chain"] = [](TestContext&) { return test_physics_drive_arm_chain(); };      // PHYSICS-DRIVE: arm chain (STANDALONE)
    registry["test_gluon_chain_converges"] = [](TestContext&) { return test_gluon_chain_converges(); };         // PHYSICS-DRIVE: isolated chain (STANDALONE)
    registry["test_physics_drive_full_idle"] = [](TestContext&) { return test_physics_drive_full_idle(); };    // PHYSICS-DRIVE: full body idle (STANDALONE)
    registry["test_physics_drive_walk_upper_body"] = [](TestContext&) { return test_physics_drive_walk_upper_body(); }; // PHYSICS-DRIVE: walk w/ upper body driven (STANDALONE)
    registry["test_gluon_distance_holds_offset"] = [](TestContext&) { return test_gluon_distance_holds_offset(); }; // GLUON-REHAB: distance constraint TDD (STANDALONE)
    registry["test_pin_gluon_holds_particle"] = [](TestContext&) { return test_pin_gluon_holds_particle(); };  // PHYSICS-DRIVE: pin gluon mechanism (STANDALONE)
    registry["test_physics_drive_walk_legs"] = [](TestContext&) { return test_physics_drive_walk_legs(); };  // PHYSICS-DRIVE: walk with physics-driven legs (STANDALONE)
    registry["test_physics_drive_walk_legs_fast"] = [](TestContext&) { return test_physics_drive_walk_legs_fast(); };  // PHYSICS-DRIVE: 2 m/s walk substepping proof (STANDALONE)
    registry["test_phase_e_diagnostic"] = [](TestContext&) { return test_phase_e_diagnostic(); };  // PHASE-E: shooting-stuff RCA (STANDALONE)
    registry["test_collision_event_swap_integrity"] = [](TestContext&) { return test_collision_event_swap_integrity(); };  // PHYSICS: event swap integrity (STANDALONE)
    registry["test_gluon_removal_unindexes"] = [](TestContext&) { return test_gluon_removal_unindexes(); };  // PHYSICS: gluon removal unindexes (STANDALONE)
    registry["test_position_authority"] = [](TestContext&) { return test_position_authority(); };  // PHYSICS: position authority (STANDALONE)
    registry["test_deferred_deletion_integrity"] = [](TestContext&) { return test_deferred_deletion_integrity(); };  // PHYSICS: deletion queue swap integrity (STANDALONE)
    registry["test_pin_anchor_persistence"] = [](TestContext&) { return test_pin_anchor_persistence(); };  // PHYSICS: pin anchor persistence (STANDALONE)
    registry["test_shadow_edge_quantization"] = [](TestContext&) { return test_shadow_edge_quantization(); };  // RENDERING: shadow staircase catcher (STANDALONE)
    registry["test_ui_text_dpi"] = [](TestContext&) { return test_ui_text_dpi(); };  // UI: HUD glyph DPI scale (STANDALONE)
    registry["test_gpu_wait_no_fixed_sleep"] = [](TestContext&) { return test_gpu_wait_no_fixed_sleep(); };  // RENDERING: GPU drain sleep guard (STANDALONE)
    registry["test_ui_overlay_plane"] = [](TestContext&) { return test_ui_overlay_plane(); };  // UI: overlay plane (STANDALONE)
    registry["test_ui_overlay_survives_frames"] = [](TestContext&) { return test_ui_overlay_survives_frames(); };  // UI: flicker lock (STANDALONE)
    registry["test_interaction_filtering"] = [](TestContext&) { return test_interaction_filtering(); };  // INTERACTION: filter seam (STANDALONE)
    registry["test_interaction_volume_forces"] = [](TestContext&) { return test_interaction_volume_forces(); };  // INTERACTION: medium forces (STANDALONE)
    registry["test_interaction_transformations"] = [](TestContext&) { return test_interaction_transformations(); };  // INTERACTION: declarative rules (STANDALONE)
    registry["test_physics_drive_neck_yaw"] = [](TestContext&) { return test_physics_drive_neck_yaw(); };  // PHYSICS-DRIVE: humanoid neck yaw (STANDALONE)
    registry["test_joint_hierarchy_swap_integrity"] = [](TestContext&) { return test_joint_hierarchy_swap_integrity(); };  // PHYSICS: joint hierarchy swap (STANDALONE)
    registry["test_reverse_leg_chain"] = [](TestContext&) { return test_reverse_leg_chain(); };  // ANIMATION: reverse_leg_chain roundtrip (STANDALONE)
    registry["test_soft_shadows"] = [](TestContext& ctx) { return test_soft_shadows(ctx); };  // RENDERING: Soft shadow visual test
    registry["test_ssgi_visual"] = [](TestContext&) { return test_ssgi_visual(); };  // RENDERING: SSGI + soft shadows visual test (STANDALONE)
    registry["test_shadow_frame_stability"] = [](TestContext&) { return test_shadow_frame_stability(); };  // RENDERING: Shadow frame stability
    registry["test_shadow_penumbra_exists"] = [](TestContext&) { return test_shadow_penumbra_exists(); };  // RENDERING: Shadow penumbra gradient
    registry["test_shadow_no_color_artifacts"] = [](TestContext&) { return test_shadow_no_color_artifacts(); };  // RENDERING: No colored artifacts
    // REMOVED: test_physics_profile - file does not exist
    // REMOVED: test_eva_physics - old test
    registry["test_eva_movement"] = test_eva_movement;  // PHYSICS: Eva movement stress test (STANDALONE)
    registry["test_capability_profile"] = test_capability_profile;  // Engine: CapabilityProfile + DynamicsParams
    registry["test_spirit_light_artifacts"] = test_spirit_light_artifacts;  // RENDERING: Spirit light artifacts
    registry["test_shadow_invariant"] = test_shadow_invariant;  // RENDERING: Shadow invariant
    registry["test_rock_shadow_artifact"] = test_rock_shadow_artifact;  // RENDERING: Rock shadow artifact
    registry["test_gi_radial_artifact"] = test_gi_radial_artifact;  // RENDERING: GI radial artifact
    registry["test_gi_speckle"] = test_gi_speckle;  // RENDERING: GI speckle on floor
    registry["test_ssao_basic"] = test_ssao_basic;  // RENDERING: SSAO contact shadows
    registry["test_gi_bounce"] = test_gi_bounce;   // RENDERING: GI colored bounce
    registry["test_ddgi_bounce"] = test_ddgi_bounce; // RENDERING: DDGI probe bounce
    registry["test_bvh_stress"] = test_bvh_stress;  // PERF: BVH rebuild stall
    registry["test_chunk_floor"] = test_chunk_floor; // WORLDGEN: Floor chunk streaming
    registry["test_tile_sticking"] = test_tile_sticking; // PHYSICS: Tile sticking
    registry["test_memory_leak"] = test_memory_leak; // PERF: Memory leak detection
    registry["test_humanoid_ground"] = test_humanoid_ground;  // PHYSICS: Humanoid ground support test
    registry["test_humanoid_ground_multitile"] = test_humanoid_ground_multitile;  // PHYSICS: Humanoid multi-tile ground test
    registry["test_humanoid_rotation"] = test_humanoid_rotation;  // DYNAMICS: Humanoid rotation/look-at test
    registry["test_humanoid_lie_down"] = test_humanoid_lie_down;  // DYNAMICS: Humanoid lie down/stand up test
    registry["test_physics_experiment_03_eva_constraints"] = [](TestContext&) { return test_physics_experiment_03_eva_constraints(); };  // PHYSICS v2: Eva body parts + constraints (incremental)
    registry["test_humanoid_impact"] = [](TestContext&) { return test_humanoid_impact(); };  // PHYSICS: Humanoid impact response (projectile knockback)

    // TODO: Convert remaining tests to use TestContext
    // registry["test_basic_scenarios"] = test_basic_scenarios;
    // registry["test_cube_around_light"] = test_cube_around_light;
    // registry["test_brightness_rendering"] = test_brightness_rendering;
    // registry["test_brightness_normalization"] = test_brightness_normalization;
    // registry["test_dynamic_lighting"] = test_dynamic_lighting;
    // registry["test_eden_headless_rendering"] = test_eden_headless_rendering;
    // registry["test_light_distance_falloff"] = test_light_distance_falloff;
    // registry["test_culling_observer"] = test_culling_observer;
    // registry["test_shadow_casting"] = test_basic_shadow_casting;  // Alias
    // registry["test_basic_shadow_casting"] = test_basic_shadow_casting;
    // registry["test_large_shadow_casting"] = test_large_shadow_casting;
    // registry["test_small_shadow_casting"] = test_small_shadow_casting;
    // registry["test_shadow_rendering"] = test_shadow_rendering;
    // registry["test_shadow_pixel_rendering"] = test_shadow_pixel_rendering;
    // registry["test_surface_light_grid_integration"] = test_surface_light_grid_integration;
    
    return registry;
}

bool UnifiedTestRunner::run_specific_test(const std::string& test_name, Engine& engine) {
    std::cout << "\n[SPECIFIC TEST] Running: " << test_name << std::endl;

    // Special group: "physics" runs all physics tests
    if (test_name == "physics") {
        return run_physics_tests(engine);
    }

    // Get the test registry
    static const auto test_registry = create_test_registry();

    // Look up and run the test
    auto it = test_registry.find(test_name);
    if (it != test_registry.end()) {
        // Create test context and pass to test
        auto ctx = engine.create_test_context();
        return it->second(ctx);  // Call the test function with context
    }

    // Test not found - show available tests
    std::cout << "ERROR: Unknown test '" << test_name << "'" << std::endl;
    std::cout << "Available tests:" << std::endl;
    std::cout << "  - physics  (run all physics tests)" << std::endl;
    for (const auto& [name, func] : test_registry) {
        std::cout << "  - " << name << std::endl;
    }
    return false;
}

// ==========================================
// TEST PROTOCOL - CRITICAL FOR ALL TESTS!
// ==========================================
// Every test MUST follow these rules:
// 1. Return bool - true for pass, false for fail
// 2. Be added to proper test module (not ad-hoc in run_all_tests)
// 3. Have its return value checked and tracked
// 4. Be registered in the test registry if callable by name
// 5. Print clear PASS/FAIL status at the end
// 
// NEVER just call a test and ignore its return value!
// NEVER add tests directly to run_all_tests()!
// 
// MODULE-LEVEL "All tests passed" MESSAGES:
// - Individual test modules may print "X/Y tests passed" for their module
// - They must NEVER print "All tests passed" unless qualified with module name
// - ONLY the final TestSuiteCoordinator report can make overall pass/fail claims
// - Example OK: "KG Module: 12/12 tests passed"
// - Example BAD: "All tests passed! ✓" (misleading if other modules failed)
// 
// FAILURE TRACKING:
// - Every test that prints "FAIL:" MUST also return false
// - The test runner MUST track this return value in passed/total counts
// - The coordinator MUST report failures in the final summary
// - Exit code MUST be non-zero if any test failed
//
// NO WARNINGS IN TESTS - ONLY ASSERTIONS:
// - Tests MUST NOT print "WARNING" and continue execution
// - Every unexpected condition MUST cause immediate test failure
// - Use clear assertions: if condition is not met, return false
// - This ensures we catch issues immediately, not hide them
// - Example BAD:  if (!lit) std::cout << "WARNING: Not lit" << std::endl;
// - Example GOOD: if (!lit) { std::cout << "FAIL: Not lit" << std::endl; return false; }
// - Performance warnings in benchmarks are the only exception (informational)
// ==========================================

void UnifiedTestRunner::run_all_tests() {
    // Old version without engine - just shows empty results
    std::cout << "\n[UNIFIED_TEST_RUNNER] Starting run_all_tests()" << std::endl;
    std::cout << "WARNING: No engine provided - cannot run TestContext-based tests" << std::endl;
    coordinator_.generate_final_report();
}

void UnifiedTestRunner::run_all_tests(Engine& engine) {
    // New version with engine - runs all TestContext-based tests
    std::cout << "\n[UNIFIED_TEST_RUNNER] Starting run_all_tests() with Engine" << std::endl;
    std::cout.flush();
    
    TestContext ctx(engine);
    
    // Core engine tests
    {
        auto start = std::chrono::high_resolution_clock::now();
        int passed = 0, total = 14;
        // Retired 2026-07-15: test_tile_rendering, test_triangle_rendering
        // (CPU tile rasterization is compiled out behind USE_GPU_RASTERIZATION),
        // test_gpu_depth, test_gpu_batch_triangles, test_gpu_surface_upload
        // (STEP 4-6 test-only host entrypoints with no production caller;
        // the shipping path is rasterize_triangles_deferred_async and is
        // covered by test_gpu_lighting + the headless-render AT).

        ctx.clear_state();
        if (test_basic_cube(ctx)) passed++;
        ctx.clear_state();
        if (test_basic_cube_projections(ctx)) passed++;
        ctx.clear_state();
        if (test_coordinate_system(ctx)) passed++;
        ctx.clear_state();
        if (test_bvh_basic(ctx)) passed++;
        ctx.clear_state();
        if (test_bvh_performance(ctx)) passed++;
        ctx.clear_state();
        if (test_simd_edge_equations()) passed++;
        ctx.clear_state();
        if (test_gpu_shadow_ray(ctx)) passed++;  // GPU compute ray-triangle (Phase I MVP)
        ctx.clear_state();
        // if (test_gpu_multi_triangle(ctx)) passed++;  // GPU multi-triangle (Phase I-B) - FILE MISSING
        // ctx.clear_state();
        if (test_gpu_parallel_rays(ctx)) passed++;  // GPU parallel rays (Phase I-C)
        ctx.clear_state();
        if (test_gpu_ray_batching(ctx)) passed++;  // GPU ray batching (Phase II-A)
        ctx.clear_state();
        if (test_gpu_rasterize_minimal(ctx)) passed++;  // GPU rasterization minimal (Phase III STEP 1)
        ctx.clear_state();
        if (test_gpu_rasterize_triangle(ctx)) passed++;  // GPU triangle rasterization (Phase III STEP 2)
        ctx.clear_state();
        if (test_gpu_barycentric(ctx)) passed++;         // GPU barycentric interpolation (Phase III STEP 3)
        ctx.clear_state();
        if (test_gpu_lighting(ctx)) passed++;            // GPU lighting integration (Phase III STEP 7)
        ctx.clear_state();
        if (test_kg_stable_particle_ids(ctx)) passed++;  // KG stable particle IDs (permaworld)

        auto end = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(end - start).count();
        coordinator_.add_module_result(TestModuleResult("Core 3D System", "Core Engine", passed, total, duration, true));
    }
    
    // Pixel rasterization tests
    {
        auto start = std::chrono::high_resolution_clock::now();
        int passed = 0, total = 4;
        
        ctx.clear_state();
        if (test_pixel_rasterization(ctx)) passed++;
        ctx.clear_state();
        if (test_pixel_rasterization_isometric_depth(ctx)) passed++;
        ctx.clear_state();
        if (test_pixel_rasterization_perspective(ctx)) passed++;
        ctx.clear_state();
        if (test_pixel_rasterization_cabinet(ctx)) passed++;
        
        auto end = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(end - start).count();
        coordinator_.add_module_result(TestModuleResult("Pixel Rasterization", "Rendering", passed, total, duration, true));
    }
    
    // Lighting tests
    {
        auto start = std::chrono::high_resolution_clock::now();
        int passed = 0, total = 8;
        // Retired 2026-07-15: test_light_intensity_falloff,
        // test_pixel_shadow_gradients, test_rendered_shadow_pixels — they
        // sampled the CPU pixel-lighting strategy, dead behind
        // USE_GPU_RASTERIZATION (lux always 0). Occlusion semantics live on
        // in test_shadow_ray_blocking / test_shadow_movement_direction /
        // test_bvh_shadow_accuracy via BVH::trace_shadow_ray (the vision
        // LOS primitive); image-level shadow ATs belong to the GPU lane.
        
        ctx.clear_state();
        if (test_surface_ray_tracing(ctx)) passed++;
        ctx.clear_state();
        if (test_triangle_shadow_artifacts(ctx)) passed++;
        ctx.clear_state();
        if (test_shadow_ray_blocking(ctx)) passed++;
        ctx.clear_state();
        if (test_bvh_shadow_accuracy(ctx)) passed++;
        ctx.clear_state();
        if (test_eden_shadows(ctx)) passed++;
        ctx.clear_state();
        if (test_shadow_movement_direction(ctx)) passed++;
        ctx.clear_state();
        if (test_uv_coordinates(ctx)) passed++;
        ctx.clear_state();
        if (test_occlusion_culling(ctx)) passed++;
        
        auto end = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(end - start).count();
        coordinator_.add_module_result(TestModuleResult("Lighting System", "Lighting & Shadows", passed, total, duration, true));
    }

    // Physics tests
    {
        auto start = std::chrono::high_resolution_clock::now();
        int passed = 0, total = 1;

        ctx.clear_state();
        // REMOVED: test_load_bearing_constraints - dead feature (is_load_bearing_ field removed)
        // REMOVED: test_eva_humanoid - old test
        if (test_stiffness_stability()) passed++;  // XPBD stability investigation
        // REMOVED: test_eva_floor_interaction - old test

        auto end = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(end - start).count();
        coordinator_.add_module_result(TestModuleResult("Physics System", "Physics & Constraints", passed, total, duration, true));
    }

    // Generate the unified report
    coordinator_.generate_final_report();
}

// --- Standalone test support (GPU crash prevention) ---
// Standalone tests create their own Engine internally and ignore TestContext.
// Running them while the harness engine is alive doubles GPU resources
// (4 Metal devices, 56 threads, 6+ frame buffers) which can crash the system.

static const std::unordered_set<std::string>& get_standalone_test_names() {
    static const std::unordered_set<std::string> names = {
        "test_physics_experiment_01",
        "test_turtle_single_particle",
        "test_oscillation_diagnostic",
        "test_physics_minimal",
        "test_physics_minimal_v2",
        "test_physics_tree_roots",
        "test_experiment_totem_builder",
        "test_totem_gluon_nails",
        "test_gluon_tree",
        "test_gluon_tree_v34",
        "test_physics_tree",
        "test_physics_tree_drift",
        "test_ancient_oak",
        "test_sleep_diagnostics",
        "test_tree_wiggly",
        "test_tree_shadow_wiggly",
        "test_falling_cube",
        "test_physics_rock",
        "test_layered_floor_v1",
        "test_layered_floor_v2",
        "test_layered_floor_v3",
        "test_strata_generator",
        "test_awake_onto_at_rest",
        "test_humanoid_strata_walk",
        "test_spider_eva_shin_crush",
        "test_stance_foot_invariance",
        "test_walk_forward_progress",
        "test_strafe_progress",
        "test_rotation_cascade_yaw",
        "test_turn_in_place_foot_step",
        "test_rotation_during_walk",
        "test_strafe_sidestep_pattern",
        "test_leg_shoot_out_during_rotation",
        "test_idle_pose_stability",
        "test_gluon_angular_drive_converges",
        "test_gluon_3axis_drive_converges",
        "test_particle_quat_euler_sync",
        "test_physics_drive_shoulder_multiaxis",
        "test_physics_drive_two_joints",
        "test_physics_drive_arm_chain",
        "test_gluon_chain_converges",
        "test_physics_drive_full_idle",
        "test_physics_drive_walk_upper_body",
        "test_gluon_distance_holds_offset",
        "test_pin_gluon_holds_particle",
        "test_physics_drive_walk_legs",
        "test_physics_drive_walk_legs_fast",
        "test_phase_e_diagnostic",
        "test_collision_event_swap_integrity",
        "test_gluon_removal_unindexes",
        "test_position_authority",
        "test_deferred_deletion_integrity",
        "test_pin_anchor_persistence",
        "test_shadow_edge_quantization",
        "test_multi_light_progressive",
        "test_sphere_lod_quality",
        "test_interaction_filtering",
        "test_interaction_volume_forces",
        "test_interaction_transformations",
        "test_physics_drive_neck_yaw",
        "test_joint_hierarchy_swap_integrity",
        "test_reverse_leg_chain",
        "test_ssgi_visual",
        "test_shadow_frame_stability",
        "test_shadow_penumbra_exists",
        "test_shadow_no_color_artifacts",
        "test_physics_experiment_03_eva_constraints",
        "test_humanoid_impact",
    };
    return names;
}

bool UnifiedTestRunner::is_standalone_test(const std::string& test_name) const {
    return get_standalone_test_names().count(test_name) > 0;
}

// Direct standalone function registry — bypasses TestContext entirely.
// These functions create their own Engine internally.
static const std::unordered_map<std::string, std::function<bool()>>& get_standalone_registry() {
    static const std::unordered_map<std::string, std::function<bool()>> reg = {
        {"test_physics_experiment_01", test_physics_experiment_01},
        {"test_turtle_single_particle", test_turtle_single_particle},
        {"test_oscillation_diagnostic", test_oscillation_diagnostic},
        {"test_physics_minimal", test_physics_minimal},
        {"test_physics_minimal_v2", test_physics_minimal_v2},
        {"test_physics_tree_roots", test_physics_tree_roots},
        {"test_experiment_totem_builder", test_experiment_totem_builder},
        {"test_totem_gluon_nails", test_totem_gluon_nails},
        {"test_gluon_tree", test_gluon_tree},
        {"test_gluon_tree_v34", test_gluon_tree_v34},
        {"test_physics_tree", test_physics_tree},
        {"test_physics_tree_drift", test_physics_tree_drift},
        {"test_ancient_oak", test_ancient_oak},
        {"test_sleep_diagnostics", test_sleep_diagnostics},
        {"test_tree_wiggly", test_tree_wiggly},
        {"test_tree_shadow_wiggly", test_tree_shadow_wiggly},
        {"test_falling_cube", test_falling_cube},
        {"test_physics_rock", test_physics_rock},
        {"test_layered_floor_v1", test_layered_floor_v1},
        {"test_layered_floor_v2", test_layered_floor_v2},
        {"test_layered_floor_v3", test_layered_floor_v3},
        {"test_strata_generator", test_strata_generator},
        {"test_awake_onto_at_rest", test_awake_onto_at_rest},
        {"test_humanoid_strata_walk", test_humanoid_strata_walk},
        {"test_spider_eva_shin_crush", test_spider_eva_shin_crush},
        {"test_stance_foot_invariance", test_stance_foot_invariance},
        {"test_walk_forward_progress", test_walk_forward_progress},
        {"test_strafe_progress", test_strafe_progress},
        {"test_rotation_cascade_yaw", test_rotation_cascade_yaw},
        {"test_turn_in_place_foot_step", test_turn_in_place_foot_step},
        {"test_rotation_during_walk", test_rotation_during_walk},
        {"test_strafe_sidestep_pattern", test_strafe_sidestep_pattern},
        {"test_leg_shoot_out_during_rotation", test_leg_shoot_out_during_rotation},
        {"test_idle_pose_stability", test_idle_pose_stability},
        {"test_gluon_angular_drive_converges", test_gluon_angular_drive_converges},
        {"test_gluon_3axis_drive_converges", test_gluon_3axis_drive_converges},
        {"test_particle_quat_euler_sync", test_particle_quat_euler_sync},
        {"test_physics_drive_shoulder_multiaxis", test_physics_drive_shoulder_multiaxis},
        {"test_physics_drive_two_joints", test_physics_drive_two_joints},
        {"test_physics_drive_arm_chain", test_physics_drive_arm_chain},
        {"test_gluon_chain_converges", test_gluon_chain_converges},
        {"test_physics_drive_full_idle", test_physics_drive_full_idle},
        {"test_physics_drive_walk_upper_body", test_physics_drive_walk_upper_body},
        {"test_gluon_distance_holds_offset", test_gluon_distance_holds_offset},
        {"test_pin_gluon_holds_particle", test_pin_gluon_holds_particle},
        {"test_physics_drive_walk_legs", test_physics_drive_walk_legs},
        {"test_physics_drive_walk_legs_fast", test_physics_drive_walk_legs_fast},
        {"test_phase_e_diagnostic", test_phase_e_diagnostic},
        {"test_collision_event_swap_integrity", test_collision_event_swap_integrity},
        {"test_gluon_removal_unindexes", test_gluon_removal_unindexes},
        {"test_position_authority", test_position_authority},
        {"test_deferred_deletion_integrity", test_deferred_deletion_integrity},
        {"test_pin_anchor_persistence", test_pin_anchor_persistence},
        {"test_shadow_edge_quantization", test_shadow_edge_quantization},
        {"test_multi_light_progressive", test_multi_light_progressive},
        {"test_sphere_lod_quality", test_sphere_lod_quality},
        {"test_interaction_filtering", test_interaction_filtering},
        {"test_interaction_volume_forces", test_interaction_volume_forces},
        {"test_interaction_transformations", test_interaction_transformations},
        {"test_physics_drive_neck_yaw", test_physics_drive_neck_yaw},
        {"test_joint_hierarchy_swap_integrity", test_joint_hierarchy_swap_integrity},
        {"test_reverse_leg_chain", test_reverse_leg_chain},
        {"test_ssgi_visual", test_ssgi_visual},
        {"test_shadow_frame_stability", test_shadow_frame_stability},
        {"test_shadow_penumbra_exists", test_shadow_penumbra_exists},
        {"test_shadow_no_color_artifacts", test_shadow_no_color_artifacts},
        {"test_physics_experiment_03_eva_constraints", test_physics_experiment_03_eva_constraints},
        {"test_humanoid_impact", test_humanoid_impact},
        {"test_stiffness_stability", test_stiffness_stability},
    };
    return reg;
}

bool UnifiedTestRunner::run_standalone_test(const std::string& test_name) {
    std::cout << "\n[STANDALONE TEST] Running: " << test_name << " (no harness engine)\n";

    const auto& reg = get_standalone_registry();
    auto it = reg.find(test_name);
    if (it == reg.end()) {
        std::cout << "ERROR: Unknown standalone test '" << test_name << "'\n";
        return false;
    }

    return it->second();
}

void UnifiedTestRunner::run_core_engine_tests() {
    // TODO: Convert to TestContext when engine is available in run_all_tests
    // For now, tests are only run via run_specific_test which has the engine
}

void UnifiedTestRunner::run_rendering_tests() {
    // Rendering tests are now covered by projection and rasterization tests in core_engine_tests
}

void UnifiedTestRunner::run_physics_tests() {
    // Old version without engine - does nothing
}

bool UnifiedTestRunner::run_physics_tests(Engine& engine) {
    // Run all physics tests as a group for regression testing
    std::cout << "\n========================================" << std::endl;
    std::cout << "  PHYSICS TEST GROUP" << std::endl;
    std::cout << "========================================" << std::endl;

    auto ctx = engine.create_test_context();
    int passed = 0, total = 0;
    auto start = std::chrono::high_resolution_clock::now();

    // Tree physics tests (STANDALONE - create own engine)
    std::cout << "\n--- Tree Physics ---" << std::endl;
    total++; if (test_tree_wiggly()) { passed++; std::cout << "  [PASS] test_tree_wiggly" << std::endl; } else { std::cout << "  [FAIL] test_tree_wiggly" << std::endl; }
    total++; if (test_tree_shadow_wiggly()) { passed++; std::cout << "  [PASS] test_tree_shadow_wiggly" << std::endl; } else { std::cout << "  [FAIL] test_tree_shadow_wiggly" << std::endl; }
    total++; if (test_physics_tree()) { passed++; std::cout << "  [PASS] test_physics_tree" << std::endl; } else { std::cout << "  [FAIL] test_physics_tree" << std::endl; }
    total++; if (test_physics_tree_drift()) { passed++; std::cout << "  [PASS] test_physics_tree_drift" << std::endl; } else { std::cout << "  [FAIL] test_physics_tree_drift" << std::endl; }
    total++; if (test_ancient_oak()) { passed++; std::cout << "  [PASS] test_ancient_oak" << std::endl; } else { std::cout << "  [FAIL] test_ancient_oak" << std::endl; }
    total++; if (test_gluon_tree()) { passed++; std::cout << "  [PASS] test_gluon_tree" << std::endl; } else { std::cout << "  [FAIL] test_gluon_tree" << std::endl; }

    // Rock/cube physics tests (STANDALONE)
    std::cout << "\n--- Rock/Cube Physics ---" << std::endl;
    total++; if (test_physics_rock()) { passed++; std::cout << "  [PASS] test_physics_rock" << std::endl; } else { std::cout << "  [FAIL] test_physics_rock" << std::endl; }
    total++; if (test_falling_cube()) { passed++; std::cout << "  [PASS] test_falling_cube" << std::endl; } else { std::cout << "  [FAIL] test_falling_cube" << std::endl; }
    total++; if (test_stiffness_stability()) { passed++; std::cout << "  [PASS] test_stiffness_stability" << std::endl; } else { std::cout << "  [FAIL] test_stiffness_stability" << std::endl; }

    // EVA humanoid physics tests
    std::cout << "\n--- EVA Humanoid Physics ---" << std::endl;
    // REMOVED: test_eva_physics - old test
    // REMOVED: test_eva_humanoid - old test
    // REMOVED: test_hunter_rotation - file does not exist
    // REMOVED: test_eva_floor_interaction - old test
    total++; if (test_eva_movement(ctx)) { passed++; std::cout << "  [PASS] test_eva_movement" << std::endl; } else { std::cout << "  [FAIL] test_eva_movement" << std::endl; }

    // Gluon constraint tests (STANDALONE)
    std::cout << "\n--- Gluon Constraints ---" << std::endl;
    total++; if (test_totem_gluon_nails()) { passed++; std::cout << "  [PASS] test_totem_gluon_nails" << std::endl; } else { std::cout << "  [FAIL] test_totem_gluon_nails" << std::endl; }

    auto end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  PHYSICS TESTS: " << passed << "/" << total << " passed";
    std::cout << " (" << std::fixed << std::setprecision(1) << duration << "ms)" << std::endl;
    std::cout << "========================================" << std::endl;

    return (passed == total);
}

void UnifiedTestRunner::run_interaction_tests() {
    // TODO: Add interaction tests using TestContext
}

void UnifiedTestRunner::run_lighting_and_shadow_tests() {
    // TODO: Light intensity tests require TestContext - only run via run_specific_test
}

void UnifiedTestRunner::run_mathematics_tests() {
    // TODO: Add math tests using TestContext
    // Old hexagon grid test removed - no longer relevant
}

