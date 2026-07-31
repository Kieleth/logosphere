#include "ai/tactics/avoid_imminent_crash.h"

#include <algorithm>

namespace logotron::ai {

float AvoidImminentCrashTactic::score(logotron::Direction candidate,
                                      const PerceivedWorld& pw) const {
    int i = static_cast<int>(to_dir_index(candidate));
    float dist = pw.lethal_distance[i];
    if (dist >= kHorizon) return 0.0f;
    // Linear in (0, kHorizon): 0 m → -1, kHorizon → 0.
    float norm = std::max(0.0f, dist / kHorizon);
    return -1.0f + norm;
}

} // namespace logotron::ai
