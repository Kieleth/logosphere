#ifndef LOGOTRON_DIRECTOR_PLAYBACK_REGISTRATIONS_H
#define LOGOTRON_DIRECTOR_PLAYBACK_REGISTRATIONS_H

// Register Logotron's per-KGOp visual plays with the engine's
// MutationPlaybackRegistry. Game-side; encodes the Tron-vernacular
// "rez-in" beats for each of the entity / property kinds the
// Director can author. Other games would register their own.
//
// Phase D.2 ships KISS plays — short-lived light particles in
// Tron-blue at the new entity's position. Visual enough to read
// as "something just rezzed in", cheap enough to fire 5 of them
// at once without breaking frame-pacing. Per-type fanciness
// (line-draw heads, perimeter pulls, expanding rings) lands
// incrementally.

class Engine;

namespace logotron::director {

// Call once during Logotron init. Idempotent.
void register_playback_handlers(Engine& engine);

}  // namespace logotron::director

#endif  // LOGOTRON_DIRECTOR_PLAYBACK_REGISTRATIONS_H
