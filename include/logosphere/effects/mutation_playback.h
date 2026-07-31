#ifndef LOGOSPHERE_EFFECTS_MUTATION_PLAYBACK_H
#define LOGOSPHERE_EFFECTS_MUTATION_PLAYBACK_H

// Mutation playback registry — turns each KGOp into a small
// visual play during Phase 6 of a cinematic (the "rez-in"
// moment). Generic engine helper. Games register one callback
// per (entity-type, on-create) pair and one per (entity-type,
// property, on-set) pair; the registry routes each op to its
// handler.
//
// Playback model. A handler returns a Playback object that the
// registry ticks on REAL time each frame. The handler decides
// when it's done (Playback::is_done()). The registry is
// fire-and-forget from the cinematic's POV: begin_play() returns
// immediately, plays continue in parallel with whatever the
// cinematic is doing, and the cinematic can poll is_active() to
// know when to wrap.
//
// For unregistered op kinds the registry still emits a default
// 200 ms "rez fade" so an LLM-introduced entity type is at least
// visibly acknowledged in the world.

#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/kg_types.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Engine;

namespace Logosphere::Effects {

// Per-playback state. Subclass + override tick() / is_done() for
// custom plays. The base class implements the default 200 ms
// fade behaviour (no visible change yet — placeholder for the
// per-type plays that land in D.2).
class Playback {
public:
    virtual ~Playback() = default;
    // Returns false when the playback has finished and can be
    // dropped. Called with REAL delta seconds.
    virtual bool tick(float real_dt);

    float elapsed() const { return elapsed_; }

protected:
    float elapsed_ = 0.0f;
    float duration_ = 0.2f;  // default rez-in duration (D.1 stub)
};

// Context handed to a registered handler when it's invoked. The
// handler keeps any references it needs and returns a Playback
// it owns. Registry stores the unique_ptr.
//
// Engine is a pointer (not a reference) so headless tests can pass
// a dummy without dereferencing an incomplete type. Real game code
// always passes a live Engine*.
struct PlaybackContext {
    Engine*        engine;
    const kg::KGOp& op;
    kg::EntityID   created_entity;  // for create_entity ops; else INVALID_ENTITY
};

using PlaybackFactory =
    std::function<std::unique_ptr<Playback>(const PlaybackContext&)>;

class MutationPlaybackRegistry {
public:
    // Register a play for create_entity ops where type == `type`.
    void register_create(const std::string& type, PlaybackFactory factory);

    // Register a play for set_property ops where the entity type
    // is `type` and the property name is `property`.
    void register_set(const std::string& type,
                      const std::string& property,
                      PlaybackFactory factory);

    // Register a play for a play_cinematic op with the given name.
    // The name catalog lives entirely game-side; the engine
    // doesn't validate it. Unknown names fall through to the
    // default rez stub via begin_play's normal fallback.
    void register_cinematic(const std::string& name, PlaybackFactory factory);

    // Kick off playback for the given op. The registry stores the
    // resulting Playback and ticks it from update(). Called from
    // the cinematic's Phase 6 right after apply succeeds. If no
    // matching handler is registered, a default rez-in Playback
    // is queued (200 ms; visible-but-subtle).
    //
    // For set_property the host must pass the entity's type
    // (looked up KG-side) so the dispatcher can find Type.property
    // — the KGOp itself only carries the entity id.
    void begin_play(Engine* engine, const kg::KGOp& op,
                    const std::string& entity_type,
                    kg::EntityID created_entity);

    // Tick all active plays; drop the ones that report done.
    // Engine drives this each frame on real_dt (so plays survive
    // cinematic pause / frame-rate spikes equally).
    void update(float real_dt);

    // True while any play is still running.
    bool is_active() const { return !active_.empty(); }
    size_t active_count() const { return active_.size(); }

private:
    std::unordered_map<std::string, PlaybackFactory> create_handlers_;
    std::unordered_map<std::string, PlaybackFactory> set_handlers_;        // key: "Type.property"
    std::unordered_map<std::string, PlaybackFactory> cinematic_handlers_;  // key: name
    std::vector<std::unique_ptr<Playback>>           active_;
};

}  // namespace Logosphere::Effects

#endif  // LOGOSPHERE_EFFECTS_MUTATION_PLAYBACK_H
