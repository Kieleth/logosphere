// Signal test suite
//
// Validates synchronous multi-subscriber dispatch.
// Pure data structure tests, no engine dependency.
//
// Usage:
//   ./build/test_signal

#include "logosphere/events/signal.h"
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

struct TestEvent {
    int id;
    std::string msg;
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

void test_single_subscriber() {
    logosphere::Signal<TestEvent> signal;
    int called = 0;
    signal.subscribe([&](const TestEvent& e) {
        called++;
        ASSERT(e.id == 42, "event id");
    });
    signal.emit({42, "hello"});
    ASSERT(called == 1, "subscriber called once");
}

void test_multiple_subscribers() {
    logosphere::Signal<TestEvent> signal;
    std::vector<int> order;

    signal.subscribe([&](const TestEvent&) { order.push_back(1); });
    signal.subscribe([&](const TestEvent&) { order.push_back(2); });
    signal.subscribe([&](const TestEvent&) { order.push_back(3); });

    signal.emit({1, "test"});

    ASSERT(order.size() == 3, "all 3 called");
    ASSERT(order[0] == 1, "registration order: first");
    ASSERT(order[1] == 2, "registration order: second");
    ASSERT(order[2] == 3, "registration order: third");
}

void test_unsubscribe() {
    logosphere::Signal<TestEvent> signal;
    int a_count = 0, b_count = 0;

    auto id_a = signal.subscribe([&](const TestEvent&) { a_count++; });
    signal.subscribe([&](const TestEvent&) { b_count++; });

    signal.emit({1, "before"});
    ASSERT(a_count == 1 && b_count == 1, "both called before unsub");

    signal.unsubscribe(id_a);
    signal.emit({2, "after"});
    ASSERT(a_count == 1, "A not called after unsub");
    ASSERT(b_count == 2, "B still called");
}

void test_emit_with_zero_subscribers() {
    logosphere::Signal<TestEvent> signal;
    // Should not crash
    signal.emit({1, "nobody home"});
    ASSERT(signal.subscriber_count() == 0, "zero subscribers");
}

void test_subscriber_count() {
    logosphere::Signal<TestEvent> signal;
    ASSERT(signal.subscriber_count() == 0, "starts at 0");

    auto id1 = signal.subscribe([](const TestEvent&) {});
    ASSERT(signal.subscriber_count() == 1, "one subscriber");

    signal.subscribe([](const TestEvent&) {});
    ASSERT(signal.subscriber_count() == 2, "two subscribers");

    signal.unsubscribe(id1);
    ASSERT(signal.subscriber_count() == 1, "back to one");
}

void test_unsubscribe_during_emit() {
    logosphere::Signal<TestEvent> signal;
    int a_count = 0, b_count = 0;
    logosphere::Signal<TestEvent>::SubscriptionID id_a = 0;

    id_a = signal.subscribe([&](const TestEvent&) {
        a_count++;
        signal.unsubscribe(id_a); // unsubscribe self during emit
    });
    signal.subscribe([&](const TestEvent&) { b_count++; });

    signal.emit({1, "trigger"});
    ASSERT(a_count == 1, "A called during its last emit");
    ASSERT(b_count == 1, "B still called");

    signal.emit({2, "after"});
    ASSERT(a_count == 1, "A not called after self-unsub");
    ASSERT(b_count == 2, "B called again");
}

void test_unsubscribe_invalid_id() {
    logosphere::Signal<TestEvent> signal;
    signal.subscribe([](const TestEvent&) {});
    // Should not crash
    signal.unsubscribe(9999);
    ASSERT(signal.subscriber_count() == 1, "original still there");
}

void test_multiple_emits() {
    logosphere::Signal<TestEvent> signal;
    int count = 0;
    signal.subscribe([&](const TestEvent&) { count++; });

    for (int i = 0; i < 100; ++i) {
        signal.emit({i, "repeat"});
    }
    ASSERT(count == 100, "called 100 times");
}

void test_event_data_passed_correctly() {
    logosphere::Signal<TestEvent> signal;
    int received_id = -1;
    std::string received_msg;

    signal.subscribe([&](const TestEvent& e) {
        received_id = e.id;
        received_msg = e.msg;
    });

    signal.emit({7, "payload"});
    ASSERT(received_id == 7, "id passed through");
    ASSERT(received_msg == "payload", "msg passed through");
}

// --- Main ---

int main() {
    std::cout << "=== Signal Tests ===" << std::endl;

    TEST(single_subscriber);
    TEST(multiple_subscribers);
    TEST(unsubscribe);
    TEST(emit_with_zero_subscribers);
    TEST(subscriber_count);
    TEST(unsubscribe_during_emit);
    TEST(unsubscribe_invalid_id);
    TEST(multiple_emits);
    TEST(event_data_passed_correctly);

    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
