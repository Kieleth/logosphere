#include "ai/tactics/maintain_direction.h"

namespace logotron::ai {

float MaintainDirectionTactic::score(logotron::Direction candidate,
                                     const PerceivedWorld& pw) const {
    return (candidate == pw.self.direction) ? 0.05f : 0.0f;
}

} // namespace logotron::ai
