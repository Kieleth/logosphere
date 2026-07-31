// EventLog + EventReader test suite
//
// Validates the typed ring journal and sequence-cursor readers:
// capacity-based retention, overwrite-oldest, loud loss accounting
// (dropped_count), stamped entries, and the collect_since query the
// Knowledge layer builds on. Pure data structure tests, no engine
// dependency.
//
// Usage:
//   ./build/test_event_log

#include "logosphere/events/event_log.h"
#include <iostream>
#include <string>
#include <cassert>

struct TestEvent {
    int id;
    float value;
    std::string tag;
};

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " << #name << "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg) + " [" + #cond + "]")

// --- Tests ---

void test_emit_and_read() {
    logosphere::EventLog<TestEvent> log;
    log.emit({1, 1.0f, "a"});
    log.emit({2, 2.0f, "b"});
    log.emit({3, 3.0f, "c"});

    auto reader = log.create_reader();
    // A reader starts at creation time; these events precede it.
    ASSERT(!reader.has_unread(), "reader starts at head");

    log.emit({4, 4.0f, "d"});
    ASSERT(reader.has_unread(), "sees post-creation event");
    ASSERT(reader.unread_count() == 1, "one unread");
    auto& e = reader.read();
    ASSERT(e.id == 4, "reads the new event");
    ASSERT(!reader.has_unread(), "drained");
}

void test_reader_from_start_via_collect() {
    logosphere::EventLog<TestEvent> log;
    log.emit({1, 1.0f, "a"});
    log.emit({2, 2.0f, "b"});
    log.emit({3, 3.0f, "c"});

    // History access is collect_since, not a reader.
    auto all = log.collect_since(0);
    ASSERT(all.size() == 3, "collect_since(0) sees all");
    ASSERT(all[0].event.id == 1 && all[2].event.id == 3, "oldest first");
    ASSERT(all[0].seq == 0 && all[2].seq == 2, "sequences stamped");
}

void test_retention_survives_many_frames() {
    // The journal upgrade's reason to exist: a slow consumer misses
    // nothing as long as capacity holds.
    logosphere::EventLog<TestEvent> log;
    auto reader = log.create_reader();

    for (int f = 0; f < 100; ++f) {
        log.advance_frame(static_cast<uint64_t>(f), f * (1.0 / 60.0));
        if (f % 10 == 0) log.emit({f, 0.0f, "sparse"});
    }
    ASSERT(reader.unread_count() == 10, "all 10 sparse events retained");
    auto items = reader.drain();
    ASSERT(items.size() == 10, "drained 10");
    ASSERT(items[0].id == 0 && items[9].id == 90, "ordered oldest first");
    ASSERT(reader.dropped_count() == 0, "nothing lost");
}

void test_ring_overwrite_and_dropped_count() {
    logosphere::EventLog<TestEvent> log;
    log.set_capacity(4);
    auto reader = log.create_reader();

    for (int i = 0; i < 10; ++i) log.emit({i, 0.0f, "x"});

    ASSERT(log.count() == 4, "ring holds capacity");
    ASSERT(log.oldest_seq() == 6, "oldest is seq 6");
    ASSERT(log.head_seq() == 10, "head is 10");

    // Reader was at seq 0; 6 events were overwritten before it looked.
    auto items = reader.drain();
    ASSERT(items.size() == 4, "sees the surviving 4");
    ASSERT(items[0].id == 6 && items[3].id == 9, "survivors are newest 4");
    ASSERT(reader.dropped_count() == 6, "loss is counted, not silent");
}

void test_independent_readers() {
    logosphere::EventLog<TestEvent> log;
    auto reader_a = log.create_reader();
    auto reader_b = log.create_reader();

    log.emit({1, 1.0f, "a"});
    log.emit({2, 2.0f, "b"});

    reader_a.read();
    ASSERT(reader_a.unread_count() == 1, "A has 1 left");
    ASSERT(reader_b.unread_count() == 2, "B has 2");

    auto items = reader_b.drain();
    ASSERT(items.size() == 2, "B drained 2");
    ASSERT(reader_a.unread_count() == 1, "A still has 1");
}

void test_cursor_continuation() {
    // Partial read, more emits, reader continues from its cursor.
    logosphere::EventLog<TestEvent> log;
    auto reader = log.create_reader();
    log.emit({1, 1.0f, "a"});
    log.emit({2, 2.0f, "b"});
    log.emit({3, 3.0f, "c"});

    reader.read();
    ASSERT(reader.unread_count() == 2, "2 remaining");

    log.advance_frame(1, 1.0 / 60.0);
    log.emit({4, 4.0f, "d"});

    auto items = reader.drain();
    ASSERT(items.size() == 3, "2 old + 1 new");
    ASSERT(items[0].id == 2 && items[1].id == 3 && items[2].id == 4,
           "continued from cursor");
}

void test_peek_without_advancing() {
    logosphere::EventLog<TestEvent> log;
    auto reader = log.create_reader();
    log.emit({1, 1.0f, "a"});

    auto* p = reader.peek();
    ASSERT(p != nullptr, "peek returns event");
    ASSERT(p->id == 1, "peek sees the event");
    ASSERT(reader.unread_count() == 1, "peek didn't advance cursor");

    auto& e = reader.read();
    ASSERT(e.id == 1, "read returns same as peek");
    ASSERT(!reader.has_unread(), "now drained");
}

void test_peek_empty_log() {
    logosphere::EventLog<TestEvent> log;
    auto reader = log.create_reader();
    ASSERT(reader.peek() == nullptr, "peek on empty log returns null");
    ASSERT(!reader.has_unread(), "nothing unread");
}

void test_zero_events_no_overhead() {
    logosphere::EventLog<TestEvent> log;
    ASSERT(log.count() == 0, "empty log");
    ASSERT(log.head_seq() == 0 && log.oldest_seq() == 0, "seqs at zero");

    auto reader = log.create_reader();
    ASSERT(!reader.has_unread(), "nothing to read");
    ASSERT(reader.drain().empty(), "drain returns empty");

    log.advance_frame(1, 0.016);
    log.advance_frame(2, 0.033);
    ASSERT(log.count() == 0, "still empty after advances");
    ASSERT(log.frame() == 2, "stamp clock tracked");
}

void test_stamps() {
    logosphere::EventLog<TestEvent> log;
    log.advance_frame(7, 1.25);
    log.emit({1, 1.0f, "a"});
    log.advance_frame(8, 1.30);
    log.emit({2, 2.0f, "b"});

    auto entries = log.collect_since(0);
    ASSERT(entries.size() == 2, "two entries");
    ASSERT(entries[0].frame == 7 && entries[0].game_time == 1.25,
           "first stamped with clock at emit");
    ASSERT(entries[1].frame == 8 && entries[1].game_time == 1.30,
           "second stamped with advanced clock");
    ASSERT(entries[1].seq == entries[0].seq + 1, "sequences monotonic");
}

void test_collect_since_bounds() {
    logosphere::EventLog<TestEvent> log;
    log.set_capacity(4);
    for (int i = 0; i < 10; ++i) log.emit({i, 0.0f, "x"});

    // since below oldest clamps to oldest
    auto clamped = log.collect_since(0);
    ASSERT(clamped.size() == 4 && clamped[0].event.id == 6, "clamped to oldest");
    // since mid-ring
    auto mid = log.collect_since(8);
    ASSERT(mid.size() == 2 && mid[0].event.id == 8, "since mid-ring");
    // since head is empty
    ASSERT(log.collect_since(log.head_seq()).empty(), "since head empty");
    // max_n caps
    auto capped = log.collect_since(0, 2);
    ASSERT(capped.size() == 2 && capped[0].event.id == 6, "max_n caps");
}

void test_set_capacity_preserves_newest() {
    logosphere::EventLog<TestEvent> log;
    for (int i = 0; i < 6; ++i) log.emit({i, 0.0f, "x"});

    log.set_capacity(3);
    ASSERT(log.count() == 3, "shrunk to 3");
    ASSERT(log.oldest_seq() == 3 && log.head_seq() == 6, "newest kept");
    auto entries = log.collect_since(0);
    ASSERT(entries[0].event.id == 3 && entries[2].event.id == 5,
           "kept 3,4,5");

    // Grow again; ring keeps working
    log.set_capacity(8);
    log.emit({6, 0.0f, "x"});
    ASSERT(log.count() == 4 && log.head_seq() == 7, "grows cleanly");
    auto after = log.collect_since(0);
    ASSERT(after.back().event.id == 6, "new event lands after resize");
}

void test_reader_entry_access() {
    logosphere::EventLog<TestEvent> log;
    auto reader = log.create_reader();
    log.advance_frame(3, 0.05);
    log.emit({9, 1.0f, "stamped"});

    auto& entry = reader.read_entry();
    ASSERT(entry.event.id == 9, "entry carries the event");
    ASSERT(entry.frame == 3 && entry.seq == 0, "entry carries stamps");
}

void test_move_semantics() {
    logosphere::EventLog<TestEvent> log;
    auto reader = log.create_reader();
    TestEvent e{42, 3.14f, "moved"};
    std::string tag_copy = e.tag;
    log.emit(std::move(e));

    auto& read_e = reader.read();
    ASSERT(read_e.id == 42, "id preserved");
    ASSERT(read_e.tag == tag_copy, "tag preserved after move");
}

void test_many_events_default_capacity() {
    logosphere::EventLog<TestEvent> log;
    auto reader = log.create_reader();
    for (int i = 0; i < 1000; ++i) log.emit({i, static_cast<float>(i), "bulk"});
    ASSERT(log.count() == 1000, "1000 within default capacity (1024)");

    auto items = reader.drain();
    ASSERT(items.size() == 1000, "reader sees all 1000");
    ASSERT(items[999].id == 999, "last event correct");
    ASSERT(reader.dropped_count() == 0, "no loss under capacity");
}

// --- Main ---

int main() {
    std::cout << "=== EventLog + EventReader Tests (ring journal) ===" << std::endl;

    TEST(emit_and_read);
    TEST(reader_from_start_via_collect);
    TEST(retention_survives_many_frames);
    TEST(ring_overwrite_and_dropped_count);
    TEST(independent_readers);
    TEST(cursor_continuation);
    TEST(peek_without_advancing);
    TEST(peek_empty_log);
    TEST(zero_events_no_overhead);
    TEST(stamps);
    TEST(collect_since_bounds);
    TEST(set_capacity_preserves_newest);
    TEST(reader_entry_access);
    TEST(move_semantics);
    TEST(many_events_default_capacity);

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
