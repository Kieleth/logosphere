#ifndef ENGINE_METRICS_H
#define ENGINE_METRICS_H

// Performance metrics structure
struct EngineMetrics {
    // System-level timing (in milliseconds)
    double input_time = 0.0;          // Time spent on input processing
    double physics_time = 0.0;        // Time spent on physics calculations
    double vision_time = 0.0;         // Time spent on vision calculations  
    double lighting_time = 0.0;       // Time spent on light propagation
    double kg_time = 0.0;             // Time spent on Knowledge Graph updates
    double ui_time = 0.0;             // Time spent on UI rendering
    double render_time = 0.0;         // Time spent on main rendering
    double display_time = 0.0;        // Time spent on screen presentation
    double total_frame_time = 0.0;    // Total time per frame
    
    // Render subsystem timing
    double render_clear_time = 0.0;   // Time to clear framebuffer
    double render_particles_time = 0.0; // Time to render all particles
    double render_surfaces_time = 0.0;  // Time spent on surface culling/sorting
    
    // Detailed render pipeline profiling
    double render_culling_time = 0.0;    // Time for backface culling
    double render_projection_time = 0.0;  // Time to project surfaces to screen
    double render_sorting_time = 0.0;     // Time to sort surfaces by depth
    double render_rasterization_time = 0.0; // Total pixel iteration time
    
    // Per-pixel breakdown (accumulated over frame)
    double pixel_edge_test_time = 0.0;    // Time in edge tests/scanline
    double pixel_uv_calc_time = 0.0;      // Time calculating UV coordinates
    double pixel_depth_time = 0.0;        // Time calculating depth
    double pixel_lighting_time = 0.0;     // Time getting surface color
    double pixel_write_time = 0.0;        // Time writing to framebuffer
    
    // UV calculation statistics
    int uv_calculations_per_frame = 0;    // Number of UV calcs this frame
    double uv_calc_time_per_frame = 0.0;  // Total UV calc time this frame (ms)
    
    // Lighting subsystem breakdown (accumulated over frame)
    double light_uv_to_world_time = 0.0;  // Time converting UV to 3D position
    double light_intensity_calc_time = 0.0; // Time calculating light contribution
    double light_shadow_ray_time = 0.0;   // Time tracing shadow rays
    double light_surface_fetch_time = 0.0; // Time getting surface data
    double light_tone_mapping_time = 0.0;  // Time in tone mapping
    double light_color_calc_time = 0.0;    // Time calculating final RGB
    double light_ray_count = 0;           // Number of shadow rays cast
    double light_call_count = 0;          // Number of lighting calls
    
    // Performance stats
    double current_fps = 0.0;         // Frames per second
    double min_fps = 1000.0;          // Minimum FPS over last second
    double max_fps = 0.0;             // Maximum FPS over last second
    int particle_count = 0;           // Number of active particles
    int surfaces_rendered = 0;        // Number of surfaces rendered this frame
    int surfaces_culled = 0;          // Surfaces rejected by backface culling
    int surfaces_projected = 0;       // Surfaces that made it to projection
    int pixels_tested = 0;             // Total pixels tested (DEPRECATED - use pixels_edge_tested)
    int pixels_rejected = 0;           // Pixels rejected by edge tests (DEPRECATED)
    int pixels_drawn = 0;              // Pixels actually drawn (DEPRECATED - use pixels_depth_passed)
    int rays_cast = 0;                // Number of rays cast for lighting

    // Overdraw metrics (CRITICAL for Early-Z optimization)
    int pixels_shaded = 0;             // Total pixels that went through shading
    int pixels_depth_rejected = 0;     // Pixels rejected by depth test
    int pixels_visible = 0;            // Final visible pixels in framebuffer
    double overdraw_factor = 0.0;      // pixels_shaded / pixels_visible ratio

    // DETAILED RASTERIZATION METRICS (from single-pass renderer)
    uint64_t pixels_edge_tested = 0;   // Total edge test attempts
    uint64_t pixels_inside = 0;        // Passed edge test (inside triangle)
    uint64_t pixels_depth_tested = 0;  // Depth calculation performed
    uint64_t pixels_depth_passed = 0;  // Passed depth test (actually drawn)

    // BVH TRAVERSAL METRICS (for shadow ray optimization)
    uint64_t bvh_aabb_tests = 0;       // Total AABB intersection tests performed
    uint64_t bvh_aabb_hits = 0;        // AABB tests that passed (ray hit box)
    uint64_t bvh_nodes_visited = 0;    // Total BVH nodes traversed
    uint64_t bvh_leaf_tests = 0;       // Leaf node particle intersection tests
    uint64_t bvh_rays_traced = 0;      // Shadow rays traced through BVH

    // Moving averages (over last 60 frames)
    double avg_frame_time = 0.0;      // Average frame time
    double frame_time_variance = 0.0; // Frame time variance (jitter)
};

#endif // ENGINE_METRICS_H