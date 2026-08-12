#ifndef LOGOSPHERE_REPLAY_RUN_RECORDER_H
#define LOGOSPHERE_REPLAY_RUN_RECORDER_H

// RunRecorder: everything the engine saw during a run, as JSONL.
//
// This records what HAPPENED: dice rolled, properties changed,
// relations made, things damaged, spawned, died. It is the trace you
// diff, grep and assert against, and it is the half of a run that
// needs no cooperation from the game at all.
//
// It CANNOT replay anything and does not pretend to. Replaying a run
// needs its INPUTS (the seed, the answers a player gave, the replies a
// model sent), which are game-shaped and never reach the bus. Those
// are a separate mechanism, and the asymmetry is deliberate: a fact
// this recorder misses costs you fidelity, while an input a tape
// misses makes a replay silently wrong.
//
// WHY IT SUBSCRIBES RATHER THAN READING THE JOURNAL. Each channel's
// journal numbers its own entries, so "damage 7" and "collision 7" are
// unrelated and cross-channel order cannot be recovered from them.
// Signals fire inside emit, in order, so a subscriber sees true global
// order and stamps its own dense sequence. Journals also evict oldest
// first once full; a subscriber cannot miss an event that was emitted.
//
// USAGE
//
//   telemetry::Session session;                 // one per process
//   replay::RunRecorder recorder(bus, {});
//   session.register_instrument(&recorder);
//   recorder.snapshot_kg(kg, world_query);      // types, once, at t=0
//   ... run ...
//   // ~/.logosphere/sessions/<sha>/<ts>/run/run.jsonl
//
// Entity TYPES never appear on the bus: creating an entity emits
// nothing, so a trace is a stream of deltas against ids you cannot
// otherwise identify. snapshot_kg is what makes the ids mean
// something, and it is deterministic by construction.

#include "logosphere/telemetry/session.h"

#include <cstdint>
#include <string>
#include <vector>

namespace logosphere { class EventBus; }
namespace kg { class KGModule; struct Query; }

namespace logosphere::replay {

// Which channels to record. The defaults are chosen for signal, not
// for completeness: collisions run to thousands per second and carry
// no identity beyond ids, and two channels currently emit without
// naming the particles involved, so recording them costs volume and
// buys nothing.
struct RecordSpec {
    bool dice = true;             // every roll, with its citable id
    bool state_changes = true;    // property writes, with prev and new
    bool relations = true;        // HAS_PART and friends
    bool damage = true;
    bool deaths = true;
    bool spawns = true;
    bool transformations = true;

    bool collisions = false;      // thousands per second
    bool perception = false;      // no production emitter exists
    bool volume = false;          // emits without naming the particles
    bool contact_filtered = false;// same

    // 0 means unbounded. A non-zero cap stops recording and says so
    // rather than truncating a file that then looks complete.
    uint64_t max_records = 0;
};

class RunRecorder : public logosphere::telemetry::Instrument {
public:
    RunRecorder(logosphere::EventBus& bus, RecordSpec spec = {});
    ~RunRecorder() override;

    RunRecorder(const RunRecorder&) = delete;
    RunRecorder& operator=(const RunRecorder&) = delete;

    const char* name() const override { return "run"; }

    // The world as it stands, written once to kg0.jsonl. Without it
    // the trace is deltas against anonymous ids.
    void snapshot_kg(const kg::KGModule& world, const kg::Query& query);

    // How many records were written, and how many were refused by the
    // cap. `dropped` is not "lost": it is the count the cap stopped,
    // and it is reported so a truncated trace cannot be mistaken for a
    // complete one.
    uint64_t records() const { return records_; }
    uint64_t dropped() const { return dropped_; }

    // Recording stops here. Called by the destructor; safe twice.
    void detach();

private:
    void write(const std::string& channel, const std::string& payload);

    logosphere::EventBus& bus_;
    RecordSpec spec_;
    uint64_t records_ = 0;
    uint64_t dropped_ = 0;
    bool attached_ = false;
    std::vector<std::pair<int, size_t>> subscriptions_;  // channel tag, id
};

}  // namespace logosphere::replay

#endif  // LOGOSPHERE_REPLAY_RUN_RECORDER_H
