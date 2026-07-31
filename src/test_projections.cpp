// Projection Mode Tests with Standardized Output
#include "test_projections.h"
#include "test_formatter.h"
#include "core/engine.h"
#include "logosphere/rendering/coordinate_transformer.h"
#include "logosphere/rendering/resolution_manager.h"
#include "core/particle_system.h"

bool TestProjections::run_all_tests(Engine& engine) {
    TestFormatter::print_header("PROJECTION SYSTEM TESTS");
    
    ProjectionMode original_mode = engine.get_projection_mode();
    
    int passed = 0, total = 0;
    
    // Test each projection mode
    total++; if (test_projection(engine, ProjectionMode::Isometric, "Isometric")) passed++;
    total++; if (test_projection(engine, ProjectionMode::IsometricWithDepth, "Isometric+Depth")) passed++;
    total++; if (test_projection(engine, ProjectionMode::Perspective, "Perspective")) passed++;
    total++; if (test_projection(engine, ProjectionMode::Cabinet, "Cabinet")) passed++;
    
    // Orientation tests
    total++; if (test_perspective_wall_orientation(engine)) passed++;
    total++; if (test_north_south_orientation(engine)) passed++;
    total++; if (test_isometric_compass_orientation(engine)) passed++;
    
    engine.set_projection_mode(original_mode);
    
    TestFormatter::print_summary(passed, total);
    return passed == total;
}

bool TestProjections::test_projection(Engine& engine, ProjectionMode mode, const std::string& name) {
    auto start = TestFormatter::start_timer();
    engine.set_projection_mode(mode);
    
    int center_x = engine.get_resolution_manager().get_window_width() / 2;
    int center_y = engine.get_resolution_manager().get_window_height() / 2;
    
    bool passed = true;
    
    // Basic tests without verbose output
    int sx, sy;
    engine.get_coord_transformer().world_to_screen_3d(0, 0, 0, sx, sy);
    if (sx != center_x || sy != center_y) passed = false;
    
    int x_right, y_right;
    engine.get_coord_transformer().world_to_screen_3d(10, 0, 0, x_right, y_right);
    if (x_right <= center_x) passed = false;
    
    int x1, y1, x2, y2;
    engine.get_coord_transformer().world_to_screen_3d(5, 5, 5, x1, y1);
    engine.get_coord_transformer().world_to_screen_3d(-5, -5, -5, x2, y2);
    if (x1 == x2 && y1 == y2) passed = false;
    
    double duration = TestFormatter::end_timer(start);
    TestFormatter::print_test_result(name + "_projection", "Basic coordinate transformation", passed, duration);
    return passed;
}

bool TestProjections::test_perspective_wall_orientation(Engine& engine) {
    auto start = TestFormatter::start_timer();
    engine.set_projection_mode(ProjectionMode::Perspective);
    
    int center_y = engine.get_resolution_manager().get_window_height() / 2;
    
    int north_x, north_y, south_x, south_y;
    engine.get_coord_transformer().world_to_screen_3d(0, 10, 0, north_x, north_y);
    engine.get_coord_transformer().world_to_screen_3d(0, -10, 0, south_x, south_y);
    
    bool passed = (north_y < center_y && south_y > center_y);
    
    double duration = TestFormatter::end_timer(start);
    TestFormatter::print_test_result("perspective_orientation", "Wall positioning accuracy", passed, duration);
    return passed;
}

bool TestProjections::test_north_south_orientation(Engine& engine) {
    auto start = TestFormatter::start_timer();
    bool all_passed = true;
    
    ProjectionMode modes[] = {
        ProjectionMode::Isometric,
        ProjectionMode::IsometricWithDepth,
        ProjectionMode::Perspective,
        ProjectionMode::Cabinet
    };
    
    for (int i = 0; i < 4; i++) {
        engine.set_projection_mode(modes[i]);
        
        int north_x, north_y, south_x, south_y;
        engine.get_coord_transformer().world_to_screen_3d(0, 10, 0, north_x, north_y);
        engine.get_coord_transformer().world_to_screen_3d(0, -10, 0, south_x, south_y);
        
        if (north_y >= south_y) {
            all_passed = false;
        }
    }
    
    double duration = TestFormatter::end_timer(start);
    TestFormatter::print_test_result("cardinal_directions", "North/South screen mapping", all_passed, duration);
    return all_passed;
}

bool TestProjections::test_isometric_compass_orientation(Engine& engine) {
    auto start = TestFormatter::start_timer();
    engine.set_projection_mode(ProjectionMode::Isometric);
    
    bool passed = true;
    
    int n_x, n_y, s_x, s_y, e_x, e_y, w_x, w_y;
    engine.get_coord_transformer().world_to_screen_3d(0, 10, 0, n_x, n_y);
    engine.get_coord_transformer().world_to_screen_3d(0, -10, 0, s_x, s_y);
    engine.get_coord_transformer().world_to_screen_3d(10, 0, 0, e_x, e_y);
    engine.get_coord_transformer().world_to_screen_3d(-10, 0, 0, w_x, w_y);
    
    // Basic sanity checks
    if (n_x == s_x && n_y == s_y) passed = false;
    if (e_x == w_x && e_y == w_y) passed = false;
    
    double duration = TestFormatter::end_timer(start);
    TestFormatter::print_test_result("isometric_compass", "Compass point differentiation", passed, duration);
    return passed;
}