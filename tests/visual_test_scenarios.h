#ifndef VISUAL_TEST_SCENARIOS_H
#define VISUAL_TEST_SCENARIOS_H

// Forward declarations
class ParticleSystem;
class LightSystem;
enum class EngineMode;

// Visual test scenarios that can run in both headless and visual modes
namespace VisualTestScenarios {
    // Load a specific test scenario
    void load_scenario(ParticleSystem* particle_system, 
                      LightSystem* light_system,
                      EngineMode mode,
                      int category, 
                      int scenario_id);
}

#endif // VISUAL_TEST_SCENARIOS_H