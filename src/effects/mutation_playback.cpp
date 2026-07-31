#include "logosphere/effects/mutation_playback.h"

#include <variant>

namespace Logosphere::Effects {

bool Playback::tick(float real_dt) {
    elapsed_ += real_dt;
    return elapsed_ < duration_;
}

namespace {

// The default playback emits no visible effect — it's a placeholder
// for op kinds without a registered handler. Phase D ships the
// infrastructure; per-type plays land incrementally as games
// register them. The 200 ms timer keeps cinematic.is_active()
// semantically consistent (something is "playing" even when nothing
// visible happens yet).
struct DefaultRezPlay : Playback {};

PlaybackFactory default_factory() {
    return [](const PlaybackContext&) -> std::unique_ptr<Playback> {
        return std::make_unique<DefaultRezPlay>();
    };
}

}  // namespace

void MutationPlaybackRegistry::register_create(
    const std::string& type, PlaybackFactory factory) {
    create_handlers_[type] = std::move(factory);
}

void MutationPlaybackRegistry::register_set(
    const std::string& type, const std::string& property,
    PlaybackFactory factory) {
    set_handlers_[type + "." + property] = std::move(factory);
}

void MutationPlaybackRegistry::register_cinematic(
    const std::string& name, PlaybackFactory factory) {
    cinematic_handlers_[name] = std::move(factory);
}

void MutationPlaybackRegistry::begin_play(
    Engine* engine, const kg::KGOp& op,
    const std::string& entity_type, kg::EntityID created_entity) {
    PlaybackContext ctx{engine, op, created_entity};

    PlaybackFactory* factory = nullptr;

    std::visit([&](const auto& concrete) {
        using T = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, kg::KGOpCreateEntity>) {
            auto it = create_handlers_.find(concrete.type);
            if (it != create_handlers_.end()) factory = &it->second;
        } else if constexpr (std::is_same_v<T, kg::KGOpSetProperty>) {
            auto it = set_handlers_.find(entity_type + "." + concrete.property);
            if (it != set_handlers_.end()) factory = &it->second;
        } else if constexpr (std::is_same_v<T, kg::KGOpPlayCinematic>) {
            auto it = cinematic_handlers_.find(concrete.name);
            if (it != cinematic_handlers_.end()) factory = &it->second;
        }
        // destroy_entity / set_relation: no per-op visual today;
        // default rez carries the placeholder play.
    }, op);

    auto play = factory ? (*factory)(ctx) : default_factory()(ctx);
    if (play) active_.push_back(std::move(play));
}

void MutationPlaybackRegistry::update(float real_dt) {
    if (active_.empty()) return;
    // Tick + sweep done plays. Maintain order so a "directed"
    // sequence (e.g., a chain of create_entity walls) plays in
    // registration order even as some finish.
    auto write = active_.begin();
    for (auto read = active_.begin(); read != active_.end(); ++read) {
        if ((*read)->tick(real_dt)) {
            if (write != read) *write = std::move(*read);
            ++write;
        }
    }
    active_.erase(write, active_.end());
}

}  // namespace Logosphere::Effects
