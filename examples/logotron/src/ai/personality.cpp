#include "ai/personality.h"

#include "ai/look/look_at_last_seen_opponent.h"
#include "ai/look/look_at_travel.h"
#include "ai/tactics/avoid_imminent_crash.h"
#include "ai/tactics/avoid_seen_opponent.h"
#include "ai/tactics/chase_seen_opponent.h"
#include "ai/tactics/flee_own_recent_trail.h"
#include "ai/tactics/maintain_direction.h"

namespace logotron::ai {

namespace {

// Standard look blend used by every personality. Head behaviour
// isn't where the personality differences live (yet); they're all
// "watch where you're going + the opponent if you can see them."
// Per-personality head behavior can split later.
void add_default_look(Personality& p) {
    p.look.emplace_back(WeightedLookTactic{
        std::make_unique<LookAtTravelTactic>(),           0.4f});
    p.look.emplace_back(WeightedLookTactic{
        std::make_unique<LookAtLastSeenOpponentTactic>(), 0.6f});
}

}  // namespace

Personality default_personality() {
    Personality p;
    p.name = "default";
    p.head_max_rate = 4.0f;

    // DRIVE: dominant AvoidCrash so the AI clears the first 10 m of
    // wall ahead; mild Chase so it engages when it sees the User;
    // small Maintain to keep it committed (no zig-zag); small
    // FleeOwnTrail so it doesn't box itself in.
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<AvoidImminentCrashTactic>(), 1.5f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<MaintainDirectionTactic>(),  0.4f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<ChaseSeenOpponentTactic>(),  0.5f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<FleeOwnRecentTrailTactic>(), 0.3f});

    add_default_look(p);
    return p;
}

Personality aggressive_personality() {
    Personality p;
    p.name = "AGGRESSIVE";
    p.head_max_rate = 5.0f;
    // Heavy chase, moderate avoid. Cuts toward the User, accepting
    // risky turns. AvoidCrash still > 0 so it doesn't insta-die.
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<AvoidImminentCrashTactic>(), 1.0f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<MaintainDirectionTactic>(),  0.2f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<ChaseSeenOpponentTactic>(),  1.5f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<FleeOwnRecentTrailTactic>(), 0.1f});
    add_default_look(p);
    return p;
}

Personality defensive_personality() {
    Personality p;
    p.name = "DEFENSIVE";
    p.head_max_rate = 3.0f;
    // Heaviest avoid; ZERO chase; small avoid-seen-opponent so it
    // peels away when the User is in sight. Wall-hugging survivor.
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<AvoidImminentCrashTactic>(), 2.0f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<MaintainDirectionTactic>(),  0.6f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<AvoidSeenOpponentTactic>(),  0.8f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<FleeOwnRecentTrailTactic>(), 0.5f});
    add_default_look(p);
    return p;
}

Personality chaotic_personality() {
    Personality p;
    p.name = "CHAOTIC";
    p.head_max_rate = 8.0f;  // jittery swivel
    // Negative MaintainDirection inverts the score — the AI
    // PREFERS to turn over committing, producing erratic paths.
    // AvoidCrash stays moderate so it doesn't die immediately
    // from its own randomness.
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<AvoidImminentCrashTactic>(), 1.0f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<MaintainDirectionTactic>(), -0.3f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<ChaseSeenOpponentTactic>(),  0.4f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<FleeOwnRecentTrailTactic>(), 0.2f});
    add_default_look(p);
    return p;
}

Personality pursuing_personality() {
    Personality p;
    p.name = "PURSUING";
    p.head_max_rate = 6.0f;  // tracks the User aggressively
    // Tracks the User specifically. Even heavier chase than
    // aggressive; leans on visibility. AvoidCrash still on so it
    // doesn't auto-banish on its own first move.
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<AvoidImminentCrashTactic>(), 0.9f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<MaintainDirectionTactic>(),  0.1f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<ChaseSeenOpponentTactic>(),  2.0f});
    p.drive.emplace_back(WeightedTactic{
        std::make_unique<FleeOwnRecentTrailTactic>(), 0.2f});
    add_default_look(p);
    return p;
}

Personality personality_from_name(const std::string& name) {
    if (name == "AGGRESSIVE") return aggressive_personality();
    if (name == "DEFENSIVE")  return defensive_personality();
    if (name == "CHAOTIC")    return chaotic_personality();
    if (name == "PURSUING")   return pursuing_personality();
    return default_personality();
}

} // namespace logotron::ai
