// RunRecorder: everything the engine saw, as a diffable trace.
//
// The claim is not "some events were written". It is that the trace is
// COMPLETE (a subscriber cannot miss an emit the way a capped journal
// can), ORDERED across channels (which per-channel journal numbering
// cannot express), and STABLE (two identical runs produce identical
// bytes, or it is useless for diffing).

#include "logosphere/events/event_bus.h"
#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_query.h"
#include "logosphere/kg/ontology_registry.h"
#include "logosphere/replay/run_recorder.h"
#include "logosphere/telemetry/session.h"
#include "generated/logosphere_ontology_registry.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace replay = logosphere::replay;
namespace onto = logosphere::ontology;

int passed = 0;
int failed = 0;

#define CHECK(condition, message)                                        \
    do {                                                                 \
        if (condition) { ++passed; }                                     \
        else { ++failed; std::cout << "FAIL: " << (message) << "\n"; }   \
    } while (false)

// A recorder writes into a telemetry session directory. Tests read it
// back off disk, because a trace nobody can read is the failure mode
// worth guarding.
struct Recorded {
    // ORDER MATTERS. The session owns the recorder and destroys it,
    // and the recorder unsubscribes from the bus on the way out. Bus
    // first means bus destroyed LAST, so that unsubscribe touches a
    // live object. Declared the other way round this is a
    // use-after-free that only shows under a sanitizer.
    logosphere::EventBus bus;
    logosphere::telemetry::Session session;
    replay::RunRecorder* recorder = nullptr;

    explicit Recorded(replay::RecordSpec spec = {}) {
        recorder = static_cast<replay::RunRecorder*>(
            session.register_instrument(
                std::make_unique<replay::RunRecorder>(bus, spec)));
    }

    std::vector<std::string> lines(const char* file = "run.jsonl") {
        recorder->detach();         // flush by closing the subscriptions
        std::vector<std::string> out;
        std::ifstream in(session.dir() + "/run/" + file);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) out.push_back(line);
        }
        return out;
    }
};

kg::OntologyRegistry registry() {
    auto out = logosphere::ontology::registry();
    kg::OntologyRegistry game("schema://run-recorder-test");
    game.addEntityType("Thing", "Entity", false);
    game.addAncestors("Thing", {"Entity", "Describable", "Identifiable",
                                "Temporal"});
    game.addProperty("Thing", "strength", kg::PropertyValueKind::Integer,
                     false);
    game.addFacets("Thing", {"world"});
    out.extend(game);
    return out;
}

// Every property write reaches the trace. The journal would evict once
// full; a subscriber cannot.
void every_change_reaches_the_trace() {
    Recorded r;
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    world.set_event_bus(&r.bus);

    const auto thing = world.createEntity("Thing");
    // More writes than a channel's default ring holds, so a
    // journal-draining recorder would lose the early ones.
    const int writes = 1500;
    for (int i = 0; i < writes; ++i) {
        world.setProperty(thing, "strength", std::to_string(i));
    }

    const auto lines = r.lines();
    std::cout << "  [measure] " << writes << " writes -> " << lines.size()
              << " records\n";
    CHECK(lines.size() == static_cast<size_t>(writes),
          "every write is in the trace, got " +
              std::to_string(lines.size()) + " of " +
              std::to_string(writes));
    CHECK(r.recorder->dropped() == 0, "and nothing was dropped");
}

// Cross-channel order is the thing per-channel journals cannot give.
void the_trace_is_ordered_across_channels() {
    Recorded r;
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    world.set_event_bus(&r.bus);

    const auto a = world.createEntity("Thing");
    const auto b = world.createEntity("Thing");
    world.setProperty(a, "strength", "1");        // state
    world.createRelation(a, "HAS_PART", b);       // relation
    world.setProperty(a, "strength", "2");        // state

    const auto lines = r.lines();
    CHECK(lines.size() == 3, "three facts, in one stream, got " +
                                 std::to_string(lines.size()));
    if (lines.size() != 3) return;
    CHECK(lines[0].find("\"channel\":\"state\"") != std::string::npos &&
              lines[1].find("\"channel\":\"relation\"") != std::string::npos &&
              lines[2].find("\"channel\":\"state\"") != std::string::npos,
          "interleaved channels keep the order they happened in");
    CHECK(lines[0].find("\"seq\":0") != std::string::npos &&
              lines[2].find("\"seq\":2") != std::string::npos,
          "with one dense sequence across all of them");
}

// A property write carries what changed and what it was, so a trace
// can be read without replaying anything.
void a_state_record_says_what_changed_and_from_what() {
    Recorded r;
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    world.set_event_bus(&r.bus);
    const auto thing = world.createEntity("Thing");
    world.setProperty(thing, "strength", "7");
    world.setProperty(thing, "strength", "5");

    const auto lines = r.lines();
    CHECK(lines.size() == 2, "two writes recorded");
    if (lines.size() != 2) return;
    std::cout << "  [measure] " << lines[1] << "\n";
    CHECK(lines[1].find("strength") != std::string::npos &&
              lines[1].find("\"5\"") != std::string::npos &&
              lines[1].find("\"7\"") != std::string::npos,
          "the record names the property, its new value and its old one");
}

// Two identical runs must produce identical bytes, or the trace is
// useless for the golden-file testing it exists to enable.
void two_identical_runs_produce_identical_traces() {
    const auto run_once = []() {
        Recorded r;
        kg::OntologyRegistry ontology = registry();
        kg::KGModule world{ontology};
        world.setMode(kg::KGMode::MINIMAL);
        world.set_event_bus(&r.bus);
        const auto thing = world.createEntity("Thing");
        for (int i = 0; i < 20; ++i) {
            world.setProperty(thing, "strength", std::to_string(i));
        }
        const auto lines = r.lines();
        std::ostringstream joined;
        for (const auto& line : lines) joined << line << "\n";
        return joined.str();
    };
    const std::string first = run_once();
    const std::string second = run_once();
    CHECK(!first.empty() && first == second,
          "the same run twice is byte-identical (" +
              std::to_string(first.size()) + " vs " +
              std::to_string(second.size()) + " bytes)");
}

// The cap refuses rather than truncating silently, so a partial trace
// cannot be mistaken for a complete one.
void a_capped_trace_says_what_it_refused() {
    replay::RecordSpec spec;
    spec.max_records = 5;
    Recorded r(spec);
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    world.set_event_bus(&r.bus);
    const auto thing = world.createEntity("Thing");
    for (int i = 0; i < 20; ++i) {
        world.setProperty(thing, "strength", std::to_string(i));
    }
    const auto lines = r.lines();
    CHECK(lines.size() == 5, "the cap held, got " +
                                 std::to_string(lines.size()));
    CHECK(r.recorder->dropped() == 15,
          "and it counted what it refused, got " +
              std::to_string(r.recorder->dropped()));
}

// The ids in a trace are meaningless without knowing what they ARE,
// and creating an entity emits nothing, so the snapshot is the only
// thing that supplies types.
void the_snapshot_gives_the_ids_meaning() {
    Recorded r;
    kg::OntologyRegistry ontology = registry();
    kg::KGModule world{ontology};
    world.setMode(kg::KGMode::MINIMAL);
    world.set_event_bus(&r.bus);
    const auto thing = world.createEntity("Thing");
    world.setProperty(thing, "strength", "9");

    kg::Query q;
    q.facets = {"world"};
    r.recorder->snapshot_kg(world, q);

    const auto snapshot = r.lines("kg0.jsonl");
    CHECK(!snapshot.empty(), "the snapshot was written");
    if (snapshot.empty()) return;
    std::string all;
    for (const auto& line : snapshot) all += line;
    std::cout << "  [measure] kg0: " << all.substr(0, 120) << "\n";
    CHECK(all.find("Thing") != std::string::npos,
          "and it names the type the trace's ids refer to");
}

}  // namespace

int main() {
    std::cout << "=== RunRecorder: the run, as a diffable trace ===\n";
    every_change_reaches_the_trace();
    the_trace_is_ordered_across_channels();
    a_state_record_says_what_changed_and_from_what();
    two_identical_runs_produce_identical_traces();
    a_capped_trace_says_what_it_refused();
    the_snapshot_gives_the_ids_meaning();
    std::cout << "\n[measure] " << passed << " passed, " << failed
              << " failed\n";
    return failed == 0 ? 0 : 1;
}
