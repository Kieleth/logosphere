// EventChannel test suite
//
// Validates two-tier dispatch: synchronous signal + ring journal.
// Readers are subscriptions (they start at creation time); history is
// a collect_since query. Pure data structure tests, no engine
// dependency.
//
// Usage:
//   ./build/test_event_channel

#include "logosphere/events/event_channel.h"
#include <iostream>
#include <string>
#include <vector>

struct TestEvent {
    int id;
    float value;
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

void test_emit_fires_signal_and_log() {
    logosphere::EventChannel<TestEvent> channel;

    int signal_count = 0;
    channel.subscribe([&](const TestEvent& e) {
        signal_count++;
        ASSERT(e.id == 1, "signal sees correct event");
    });

    auto reader = channel.create_reader();

    channel.emit({1, 1.0f});

    ASSERT(signal_count == 1, "signal fired");
    ASSERT(reader.has_unread(), "journal has event");
    auto& e = reader.read();
    ASSERT(e.id == 1, "reader sees same event");
}

void test_history_via_collect_since() {
    logosphere::EventChannel<TestEvent> channel;
    channel.emit({1, 1.0f});
    channel.emit({2, 2.0f});

    // A reader is a subscription: created now, it sees only the future.
    auto reader = channel.create_reader();
    ASSERT(!reader.has_unread(), "reader starts at head");

    // History is a query.
    auto history = channel.collect_since(0);
    ASSERT(history.size() == 2, "collect_since sees history");
    ASSERT(history[0].event.id == 1 && history[1].event.id == 2,
           "oldest first");
}

void test_retention_across_frames() {
    logosphere::EventChannel<TestEvent> channel;
    auto reader = channel.create_reader();

    channel.emit({1, 1.0f});
    channel.advance_frame(1, 1.0 / 60.0);
    channel.emit({2, 2.0f});
    channel.advance_frame(2, 2.0 / 60.0);

    // Journal retention is capacity-based: nothing expired.
    auto items = reader.drain();
    ASSERT(items.size() == 2, "both events retained across frames");
    ASSERT(items[0].id == 1 && items[1].id == 2, "ordered");
}

void test_signal_unaffected_by_advance() {
    logosphere::EventChannel<TestEvent> channel;
    int count = 0;
    channel.subscribe([&](const TestEvent&) { count++; });

    channel.emit({1, 1.0f});
    channel.advance_frame(1, 0.016);
    channel.emit({2, 2.0f});
    channel.advance_frame(2, 0.033);
    channel.emit({3, 3.0f});

    ASSERT(count == 3, "signal fires every emit regardless of frame");
}

void test_multiple_channels_independent() {
    logosphere::EventChannel<TestEvent> channel_a;
    logosphere::EventChannel<TestEvent> channel_b;

    auto reader_a = channel_a.create_reader();
    auto reader_b = channel_b.create_reader();

    channel_a.emit({1, 1.0f});
    channel_b.emit({2, 2.0f});
    channel_b.emit({3, 3.0f});

    ASSERT(reader_a.unread_count() == 1, "channel A has 1");
    ASSERT(reader_b.unread_count() == 2, "channel B has 2");
}

void test_subscriber_and_reader_coexist() {
    logosphere::EventChannel<TestEvent> channel;

    std::vector<int> signal_ids;
    channel.subscribe([&](const TestEvent& e) { signal_ids.push_back(e.id); });

    auto reader = channel.create_reader();

    channel.emit({1, 1.0f});
    channel.emit({2, 2.0f});

    // Signal got both
    ASSERT(signal_ids.size() == 2, "signal saw 2 events");
    ASSERT(signal_ids[0] == 1, "signal order");

    // Reader also got both
    auto items = reader.drain();
    ASSERT(items.size() == 2, "reader saw 2 events");
    ASSERT(items[0].id == 1, "reader order");
}

void test_capacity_passthrough() {
    logosphere::EventChannel<TestEvent> channel;
    channel.set_capacity(2);
    auto reader = channel.create_reader();

    channel.emit({1, 1.0f});
    channel.emit({2, 2.0f});
    channel.emit({3, 3.0f});

    ASSERT(channel.event_count() == 2, "capacity enforced");
    ASSERT(channel.oldest_seq() == 1 && channel.head_seq() == 3,
           "seq window exposed");
    auto items = reader.drain();
    ASSERT(items.size() == 2 && items[0].id == 2, "survivors newest");
    ASSERT(reader.dropped_count() == 1, "loss counted");
}

void test_diagnostics() {
    logosphere::EventChannel<TestEvent> channel;

    ASSERT(channel.event_count() == 0, "starts empty");
    ASSERT(channel.subscriber_count() == 0, "no subscribers");
    ASSERT(channel.frame() == 0, "frame 0");

    channel.subscribe([](const TestEvent&) {});
    channel.emit({1, 1.0f});

    ASSERT(channel.event_count() == 1, "1 event");
    ASSERT(channel.subscriber_count() == 1, "1 subscriber");

    channel.advance_frame(1, 0.016);
    ASSERT(channel.frame() == 1, "frame 1");
}

void test_emit_no_subscribers_still_logs() {
    logosphere::EventChannel<TestEvent> channel;
    auto reader = channel.create_reader();
    channel.emit({1, 1.0f});

    ASSERT(channel.event_count() == 1, "journaled even without subscribers");
    ASSERT(reader.has_unread(), "reader sees it");
}

// --- Main ---

int main() {
    std::cout << "=== EventChannel Tests (ring journal tier) ===" << std::endl;

    TEST(emit_fires_signal_and_log);
    TEST(history_via_collect_since);
    TEST(retention_across_frames);
    TEST(signal_unaffected_by_advance);
    TEST(multiple_channels_independent);
    TEST(subscriber_and_reader_coexist);
    TEST(capacity_passthrough);
    TEST(diagnostics);
    TEST(emit_no_subscribers_still_logs);

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
