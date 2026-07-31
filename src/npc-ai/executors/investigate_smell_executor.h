// InvestigateSmellExecutor - Walk toward smelled direction
//
// When a creature detects an odor via smell, it gets a direction
// (with error based on Discernment). This executor walks toward
// that direction, looking for the source.
//
// The target position is set by the brain at a point in the smelled
// direction. The executor walks toward it while the brain continues
// scanning for visual confirmation.
//
// Completes when:
// - Reached investigation point (success - brain should continue scanning)
// - Brain sets food_known=1 from visual spot (replan to PURSUE)

#ifndef INVESTIGATE_SMELL_EXECUTOR_H
#define INVESTIGATE_SMELL_EXECUTOR_H

#include "../action_executor.h"
#include <cmath>

namespace npc_ai {

class InvestigateSmellExecutor : public ActionExecutor {
public:
    InvestigateSmellExecutor() = default;

    const char* name() const override { return "INVESTIGATE_SMELL"; }

    void on_start(ExecutionContext& ctx) override;
    ExecutionResult execute(const ExecutionContext& ctx) override;

private:
    static float distance(float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        return std::sqrt(dx * dx + dy * dy);
    }

    static float angle_to(float x1, float y1, float x2, float y2) {
        float dx = x2 - x1;
        float dy = y2 - y1;
        return std::atan2(dx, dy);
    }

    static float normalize_angle(float a) {
        while (a > M_PI) a -= 2 * M_PI;
        while (a < -M_PI) a += 2 * M_PI;
        return a;
    }
};

} // namespace npc_ai

#endif // INVESTIGATE_SMELL_EXECUTOR_H
