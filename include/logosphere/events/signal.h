// Signal: Synchronous multi-subscriber dispatch.
//
// Tier 1 of the two-tier event system. When a signal fires,
// all subscribers execute immediately in registration order
// before the call returns. For things that must happen NOW:
// entity dies = game reacts synchronously, constraint breaks = particles release.
//
// Subscribers get a stable SubscriptionID for unsubscription.
// Unsubscribing during emission is deferred to avoid iterator
// invalidation (slot is nulled, compacted on next emit).

#pragma once

#include <algorithm>
#include <functional>
#include <vector>
#include <cstddef>

namespace logosphere {

template<typename T>
class Signal {
public:
    using SubscriptionID = size_t;

private:
    struct Slot {
        SubscriptionID id;
        std::function<void(const T&)> fn;
    };

    std::vector<Slot> slots_;
    SubscriptionID next_id_ = 1;
    bool emitting_ = false;

    // Remove nulled slots (from unsubscribe-during-emit).
    void compact() {
        slots_.erase(
            std::remove_if(slots_.begin(), slots_.end(),
                [](const Slot& s) { return !s.fn; }),
            slots_.end()
        );
    }

public:
    SubscriptionID subscribe(std::function<void(const T&)> fn) {
        SubscriptionID id = next_id_++;
        slots_.push_back({id, std::move(fn)});
        return id;
    }

    void unsubscribe(SubscriptionID id) {
        for (auto& slot : slots_) {
            if (slot.id == id) {
                slot.fn = nullptr;
                break;
            }
        }
        if (!emitting_) compact();
    }

    void emit(const T& event) {
        emitting_ = true;
        for (auto& slot : slots_) {
            if (slot.fn) slot.fn(event);
        }
        emitting_ = false;
        compact();
    }

    size_t subscriber_count() const {
        size_t count = 0;
        for (const auto& slot : slots_) {
            if (slot.fn) ++count;
        }
        return count;
    }
};

} // namespace logosphere
