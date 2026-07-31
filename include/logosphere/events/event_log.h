// EventLog: typed ring journal for game events.
//
// The Information layer of the engine (Kreps' "The Log" applied to
// gameplay): every emit is stamped with a monotonic sequence number,
// the frame, and the game-time clock, and retained in a fixed-capacity
// ring (default 1024 per channel) that overwrites oldest-first.
// Retention is capacity-based, NOT frame-based: a consumer that looks
// in once every few seconds (an LLM director, a narrator, a replay
// scribe) still finds everything that happened since its last visit.
//
// Consumers create EventReader<T> cursors that track independent
// positions by sequence number. Multiple readers coexist without
// interfering. A reader that falls behind a full ring loses the
// overwritten events and says so: dropped_count() accumulates the
// loss — loud, never silent.
//
// The stamped-entry + sequence-cursor surface (head_seq / oldest_seq /
// collect_since) is the contract the Knowledge layer builds on: a view
// builder asks "what happened since cursor N" and renders the answer
// for a meta-agent. It is also the interface a silk-graph CRDT
// transport would implement; the backing store is a std::vector ring
// today.
//
// Consumer discipline: hold ONE reader per consumer for the life of
// that consumer. read() returns a reference that is only valid until
// the next emit on the log; drain() returns copies and is the right
// tool when consumption is deferred.

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace logosphere {

template <typename T>
class EventReader;

template <typename T>
class EventLog {
    friend class EventReader<T>;

public:
    // One retained event with its journal stamps.
    struct Entry {
        T event{};
        uint64_t seq = 0;        // monotonic per log, never reused
        uint64_t frame = 0;      // engine frame at emit
        double game_time = 0.0;  // scaled game clock at emit
    };

    static constexpr size_t kDefaultCapacity = 1024;

    // Append an event, stamped with the current clock. Overwrites the
    // oldest retained event when the ring is full.
    void emit(const T& event) { emit_entry(Entry{event, 0, 0, 0.0}); }
    void emit(T&& event) { emit_entry(Entry{std::move(event), 0, 0, 0.0}); }

    // Update the stamp clock. Retention does not depend on this; a log
    // that is never advanced still journals correctly (stamps stay 0).
    void advance_frame(uint64_t frame, double game_time) {
        frame_ = frame;
        game_time_ = game_time;
    }

    // Resize the ring, preserving the newest min(n, count()) events.
    // n must be >= 1.
    void set_capacity(size_t n) {
        assert(n >= 1);
        std::vector<Entry> kept;
        size_t keep = size_ < n ? size_ : n;
        kept.reserve(keep);
        for (size_t i = size_ - keep; i < size_; ++i) kept.push_back(at(i));
        capacity_ = n;
        ring_.assign(capacity_, Entry{});
        for (size_t i = 0; i < kept.size(); ++i) ring_[i] = std::move(kept[i]);
        write_ = kept.size() % capacity_;
        size_ = kept.size();
    }

    size_t capacity() const { return capacity_; }
    size_t count() const { return size_; }

    // Sequence of the NEXT event to be emitted. head_seq() - 1 is the
    // newest retained event (when count() > 0).
    uint64_t head_seq() const { return next_seq_; }
    // Sequence of the oldest retained event. Equals head_seq() when
    // the log is empty. Events with seq < oldest_seq() are gone.
    uint64_t oldest_seq() const { return next_seq_ - size_; }

    uint64_t frame() const { return frame_; }
    double game_time() const { return game_time_; }

    // Stateless query: copies of retained entries with seq >= since,
    // oldest first, at most max_n. The Knowledge-layer seam.
    std::vector<Entry> collect_since(uint64_t since,
                                     size_t max_n = SIZE_MAX) const {
        std::vector<Entry> out;
        uint64_t from = since > oldest_seq() ? since : oldest_seq();
        for (uint64_t s = from; s < next_seq_ && out.size() < max_n; ++s)
            out.push_back(at_seq(s));
        return out;
    }

    // Create an independent reader cursor starting from now (events
    // already in the log are considered read).
    EventReader<T> create_reader() const { return EventReader<T>(*this); }

private:
    void emit_entry(Entry&& e) {
        if (ring_.size() != capacity_) ring_.assign(capacity_, Entry{});
        e.seq = next_seq_++;
        e.frame = frame_;
        e.game_time = game_time_;
        ring_[write_] = std::move(e);
        write_ = (write_ + 1) % capacity_;
        if (size_ < capacity_) ++size_;
    }

    // Logical index 0 = oldest retained.
    const Entry& at(size_t i) const {
        assert(i < size_);
        size_t oldest = (write_ + capacity_ - size_) % capacity_;
        return ring_[(oldest + i) % capacity_];
    }
    const Entry& at_seq(uint64_t seq) const {
        assert(seq >= oldest_seq() && seq < next_seq_);
        return at(static_cast<size_t>(seq - oldest_seq()));
    }

    std::vector<Entry> ring_;   // allocated to capacity_ on first emit
    size_t capacity_ = kDefaultCapacity;
    size_t write_ = 0;          // next slot to write
    size_t size_ = 0;           // retained count
    uint64_t next_seq_ = 0;
    uint64_t frame_ = 0;
    double game_time_ = 0.0;
};

// Independent sequence cursor into an EventLog. Falling behind a full
// ring is detected, counted, and skipped past — never silent, never
// stuck.
template <typename T>
class EventReader {
public:
    explicit EventReader(const EventLog<T>& log)
        : log_(&log), next_seq_(log.head_seq()) {}

    bool has_unread() const {
        sync();
        return next_seq_ < log_->head_seq();
    }

    // Next unread event. Reference valid until the next emit on the
    // log. Advances the cursor.
    const T& read() {
        sync();
        assert(next_seq_ < log_->head_seq());
        return log_->at_seq(next_seq_++).event;
    }

    // Next unread entry with journal stamps (seq / frame / game_time).
    const typename EventLog<T>::Entry& read_entry() {
        sync();
        assert(next_seq_ < log_->head_seq());
        return log_->at_seq(next_seq_++);
    }

    // All unread events, copied, oldest first. Advances the cursor
    // past them. Copies because ring overwrite invalidates references.
    std::vector<T> drain() {
        sync();
        std::vector<T> out;
        out.reserve(static_cast<size_t>(log_->head_seq() - next_seq_));
        while (next_seq_ < log_->head_seq())
            out.push_back(log_->at_seq(next_seq_++).event);
        return out;
    }

    // All unread stamped entries, copied, oldest first.
    std::vector<typename EventLog<T>::Entry> drain_entries() {
        sync();
        std::vector<typename EventLog<T>::Entry> out;
        while (next_seq_ < log_->head_seq())
            out.push_back(log_->at_seq(next_seq_++));
        return out;
    }

    // Next unread event without advancing. nullptr when caught up.
    const T* peek() const {
        sync();
        if (next_seq_ >= log_->head_seq()) return nullptr;
        return &log_->at_seq(next_seq_).event;
    }

    size_t unread_count() const {
        sync();
        return static_cast<size_t>(log_->head_seq() - next_seq_);
    }

    // Events lost to ring overwrite before this reader consumed them,
    // accumulated over the reader's lifetime.
    uint64_t dropped_count() const {
        sync();
        return dropped_;
    }

    // The cursor: seq of the next event this reader will see.
    uint64_t cursor() const { return next_seq_; }

private:
    // Clamp the cursor forward when the ring has overwritten events we
    // never read; count the loss.
    void sync() const {
        uint64_t oldest = log_->oldest_seq();
        if (next_seq_ < oldest) {
            dropped_ += oldest - next_seq_;
            next_seq_ = oldest;
        }
    }

    const EventLog<T>* log_;
    mutable uint64_t next_seq_ = 0;
    mutable uint64_t dropped_ = 0;
};

}  // namespace logosphere
