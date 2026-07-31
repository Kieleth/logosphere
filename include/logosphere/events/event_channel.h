// EventChannel: Two-tier event dispatch for a single event type.
//
// Combines Signal<T> (tier 1: synchronous) with EventLog<T> (tier 2:
// ring journal). A single emit() fires all synchronous subscribers
// AND journals the event for deferred consumers.
//
// Synchronous subscribers handle immediate invariants (e.g. spawn
// corpse on death). Journal readers consume at their own pace (LLM
// director, narration, AI, UI) — retention is capacity-based, so
// "their own pace" is real (see event_log.h).

#pragma once

#include "logosphere/events/signal.h"
#include "logosphere/events/event_log.h"
#include <functional>

namespace logosphere {

template<typename T>
class EventChannel {
    Signal<T> signal_;
    EventLog<T> log_;

public:
    using SubscriptionID = typename Signal<T>::SubscriptionID;

    // Emit: fires synchronous subscribers, then appends to log.
    void emit(const T& event) {
        signal_.emit(event);
        log_.emit(event);
    }

    void emit(T&& event) {
        signal_.emit(event);  // pass by const ref to subscribers
        log_.emit(std::move(event));
    }

    // Tier 1: synchronous subscribers
    SubscriptionID subscribe(std::function<void(const T&)> fn) {
        return signal_.subscribe(std::move(fn));
    }

    void unsubscribe(SubscriptionID id) {
        signal_.unsubscribe(id);
    }

    size_t subscriber_count() const { return signal_.subscriber_count(); }

    // Tier 2: journal readers
    EventReader<T> create_reader() const { return log_.create_reader(); }

    // Journal query surface (Knowledge-layer seam; see event_log.h)
    uint64_t head_seq() const { return log_.head_seq(); }
    uint64_t oldest_seq() const { return log_.oldest_seq(); }
    std::vector<typename EventLog<T>::Entry> collect_since(
        uint64_t since, size_t max_n = SIZE_MAX) const {
        return log_.collect_since(since, max_n);
    }
    void set_capacity(size_t n) { log_.set_capacity(n); }

    // Stamp-clock update (retention no longer depends on this)
    void advance_frame(uint64_t frame, double game_time) {
        log_.advance_frame(frame, game_time);
    }

    // Diagnostics
    size_t event_count() const { return log_.count(); }
    uint64_t frame() const { return log_.frame(); }
};

} // namespace logosphere
