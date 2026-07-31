// Projection Mode Tests Header

#ifndef TEST_PROJECTIONS_H
#define TEST_PROJECTIONS_H

#include <string>

// Forward declarations
class Engine;
enum class ProjectionMode;

class TestProjections {
public:
    // Run all projection tests using the provided render system
    static bool run_all_tests(Engine& engine);
    
private:
    // Test a specific projection mode
    static bool test_projection(Engine& engine, 
                               ProjectionMode mode, 
                               const std::string& name);
    
    // Test for the specific Perspective Y-flip bug
    static bool test_perspective_wall_orientation(Engine& engine);
    
    // Test that North appears above South in all projection modes
    static bool test_north_south_orientation(Engine& engine);
    
    // Test isometric compass orientation (N=upper-right, W=upper-left, S=lower-left, E=lower-right)
    static bool test_isometric_compass_orientation(Engine& engine);
};

#endif // TEST_PROJECTIONS_H