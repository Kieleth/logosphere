// The narrator: prose about what the dice already decided.
//
// It reads the world and never changes it. A beat arrives as FACTS -
// a throw with its roll id, a skill gained, a career that refused you -
// and prose comes back. It cannot alter a characteristic, grant a
// skill, or contradict a die, because it is never asked to produce
// anything but words. Every number on the sheet stays traceable to a
// cell in the book after the story lands on top of it.
//
// Three properties it must have to be worth having:
//
//   NEVER BLOCKS.  The mechanical line appears the instant the dice
//                  resolve. The request fires then, and the prose
//                  slots in beside it when it arrives. A slow or dead
//                  API costs you flavour, not play.
//   REPRODUCIBLE.  Cached by the identity of the beat, so the same
//                  seed replays the same life AND the same story, and
//                  a replay costs nothing.
//   IN THE GRAPH.  Every narration is written back as a Narration
//                  entity pointing at the character and carrying the
//                  roll ids it was written from, so the thread runs
//                  story -> dice -> rule -> book.

#ifndef LOGOVGER_NARRATOR_H
#define LOGOVGER_NARRATOR_H

#include "logosphere/kg/kg_module.h"
#include "logosphere/llm/llm_system_http.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace logovger {

// One thing to narrate: what happened, in the game's own words, plus
// the rolls that made it happen.
struct Beat {
    std::string           kind;      // bio, qualification, term, draft, ...
    int                   term = 0;
    std::vector<std::string> facts;  // "qualified: 2D6 = 8 +1 DM -> 9 vs 6+"
    std::vector<uint64_t> roll_ids;
    std::string           sheet;     // the character as it stands
};

class Narrator {
public:
    // The lore is the only thing it knows about the universe. No key
    // means no narrator: the caller is told plainly rather than being
    // handed invented prose or silence.
    bool initialize(const std::string& lore_path, std::string& error);

    bool ready() const { return llm_ != nullptr; }

    // Did the transport hand back a failure rather than a story? The
    // LLM system reports errors as text, and text is what this class
    // deals in, so the two must be told apart before either is shown
    // or cached.
    static bool is_failure(const std::string& response);

    // Fire a beat. `on_prose` runs later, on the main thread, when the
    // response arrives - or immediately, if this beat is already
    // cached. It is called with an EMPTY string when the request
    // failed, so the caller releases whatever it was holding rather
    // than waiting forever. Returns false only when the narrator is
    // not ready.
    bool narrate(const Beat& beat, uint64_t seed,
                 std::function<void(const std::string&)> on_prose);

    // A reply carries the prose and, on its last line, one clipped
    // clause for the personnel file. Split them; a reply without a
    // FILE line yields an empty clause rather than a guessed one.
    static void split_file_line(const std::string& reply,
                                std::string& prose, std::string& file_line);

    // Drive the async worker. Call every frame.
    void poll();

    // Write a narration into the graph, tied to the character and to
    // the rolls it came from.
    kg::EntityID record(kg::KGModule& kg, kg::EntityID character,
                        const Beat& beat, const std::string& prose);

    const std::string& lore() const { return lore_; }

private:
    std::string cache_key(const Beat& beat, uint64_t seed) const;
    bool        cache_get(const std::string& key, std::string& prose) const;
    void        cache_put(const std::string& key, const std::string& prose);
    std::string build_prompt(const Beat& beat) const;

    std::unique_ptr<Logosphere::LLMSystemHTTP> llm_;
    std::string                    lore_;
    // Hash of the whole system prompt, lore included: the identity of
    // the voice. It is part of every cache key.
    std::string                    voice_version_;
    std::string                    cache_dir_;
};

}  // namespace logovger

#endif  // LOGOVGER_NARRATOR_H
