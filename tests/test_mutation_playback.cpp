// Headless test — Logosphere::Effects::MutationPlaybackRegistry.
// The registry routes each KGOp to a registered playback factory
// and ticks active plays on real time. Catches regressions in:
//   - dispatch (create vs set, type+property keying)
//   - default fallback when no handler is registered
//   - completion sweeping (done plays really get dropped)
//   - update on real_dt (no engine wiring needed for this test)

#include "logosphere/effects/mutation_playback.h"
#include "logosphere/kg/kg_ops.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace fx = Logosphere::Effects;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " #name "... "; \
    try { name(); tests_passed++; std::cout << "PASS" << std::endl; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << std::endl; }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) throw std::runtime_error(std::string(msg))

// A play that flips a counter when constructed and destructed,
// so tests can observe lifecycle.
struct CountingPlay : fx::Playback {
    int* started_ptr;
    int* destroyed_ptr;
    explicit CountingPlay(int* s, int* d) : started_ptr(s), destroyed_ptr(d) {
        if (started_ptr) ++(*started_ptr);
        duration_ = 0.5f;
    }
    ~CountingPlay() override {
        if (destroyed_ptr) ++(*destroyed_ptr);
    }
};

// Engine pointer is unused in these tests — registry stores it
// for the factory but our factories never touch it. Pass nullptr.
Engine* dummy_engine() { return nullptr; }

void create_dispatches_to_registered_handler() {
    fx::MutationPlaybackRegistry reg;
    int started = 0, destroyed = 0;
    reg.register_create("Wormhole", [&](const fx::PlaybackContext&) {
        return std::make_unique<CountingPlay>(&started, &destroyed);
    });

    kg::KGOp op = kg::KGOpCreateEntity{"Wormhole", {}};
    reg.begin_play(dummy_engine(), op, /*entity_type=*/"Wormhole",
                   /*created=*/42);

    ASSERT_TRUE(started == 1, "registered factory must be invoked");
    ASSERT_TRUE(reg.is_active(), "play must be active right after begin");
    ASSERT_TRUE(reg.active_count() == 1, "exactly one play active");
}

void unregistered_create_falls_back_to_default() {
    fx::MutationPlaybackRegistry reg;
    kg::KGOp op = kg::KGOpCreateEntity{"NoHandlerType", {}};
    reg.begin_play(dummy_engine(), op, "NoHandlerType", 7);
    ASSERT_TRUE(reg.is_active(),
        "default fallback play should still be queued");
}

void set_property_dispatches_on_type_and_property() {
    fx::MutationPlaybackRegistry reg;
    int hit = 0;
    reg.register_set("Cycle", "max_speed",
        [&](const fx::PlaybackContext&) {
            ++hit;
            return std::make_unique<fx::Playback>();
        });

    kg::KGOp matching = kg::KGOpSetProperty{
        kg::EntityRef{7, ""}, "max_speed", "12"};
    reg.begin_play(dummy_engine(), matching, "Cycle", kg::INVALID_ENTITY);

    kg::KGOp wrong_prop = kg::KGOpSetProperty{
        kg::EntityRef{7, ""}, "x", "3.5"};
    reg.begin_play(dummy_engine(), wrong_prop, "Cycle", kg::INVALID_ENTITY);

    ASSERT_TRUE(hit == 1,
        std::string("registered Cycle.max_speed handler must fire ")
        + "exactly once; got " + std::to_string(hit));
}

void update_drops_done_plays_in_order() {
    fx::MutationPlaybackRegistry reg;
    int started = 0, destroyed = 0;
    reg.register_create("X", [&](const fx::PlaybackContext&) {
        return std::make_unique<CountingPlay>(&started, &destroyed);
    });

    for (int i = 0; i < 3; ++i) {
        kg::KGOp op = kg::KGOpCreateEntity{"X", {}};
        reg.begin_play(dummy_engine(), op, "X", static_cast<kg::EntityID>(i));
    }
    ASSERT_TRUE(reg.active_count() == 3, "three plays queued");

    // CountingPlay duration is 0.5s. Tick 0.6s → all done.
    reg.update(0.6f);
    ASSERT_TRUE(!reg.is_active(),
        "all plays must have completed");
    ASSERT_TRUE(destroyed == 3, "every play got destroyed");
}

void update_keeps_unfinished_plays() {
    fx::MutationPlaybackRegistry reg;
    int started = 0, destroyed = 0;
    reg.register_create("X", [&](const fx::PlaybackContext&) {
        return std::make_unique<CountingPlay>(&started, &destroyed);
    });

    kg::KGOp op = kg::KGOpCreateEntity{"X", {}};
    reg.begin_play(dummy_engine(), op, "X", 1);

    reg.update(0.1f);  // 0.1s of 0.5s
    ASSERT_TRUE(reg.is_active(), "still running at 0.1s");
    ASSERT_TRUE(destroyed == 0, "not yet destroyed");

    reg.update(0.5f);  // total 0.6s, past duration
    ASSERT_TRUE(!reg.is_active(), "done past 0.5s");
    ASSERT_TRUE(destroyed == 1, "destroyed after completion");
}

void play_cinematic_dispatches_on_name() {
    fx::MutationPlaybackRegistry reg;
    int hit = 0;
    reg.register_cinematic("disk_throw_at",
        [&](const fx::PlaybackContext&) {
            ++hit;
            return std::make_unique<fx::Playback>();
        });

    kg::KGOp matching = kg::KGOpPlayCinematic{
        "disk_throw_at", kg::EntityRef{7, ""}, {}};
    reg.begin_play(dummy_engine(), matching, "", kg::INVALID_ENTITY);

    kg::KGOp unknown = kg::KGOpPlayCinematic{
        "no_such_cinematic", kg::EntityRef{}, {}};
    reg.begin_play(dummy_engine(), unknown, "", kg::INVALID_ENTITY);

    ASSERT_TRUE(hit == 1, "registered name fires once");
    // Both ops still queue plays — known one via registered factory,
    // unknown via default rez stub.
    ASSERT_TRUE(reg.active_count() == 2, "both queued");
}

int main() {
    std::cout << "=== test_mutation_playback ===" << std::endl;
    TEST(create_dispatches_to_registered_handler);
    TEST(unregistered_create_falls_back_to_default);
    TEST(set_property_dispatches_on_type_and_property);
    TEST(update_drops_done_plays_in_order);
    TEST(update_keeps_unfinished_plays);
    TEST(play_cinematic_dispatches_on_name);
    std::cout << std::endl;
    std::cout << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
