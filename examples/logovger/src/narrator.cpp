#include "narrator.h"

#include "generated/voyager_ontology_registry.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

namespace logovger {
namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string hex16(const std::string& text) {
    // Not cryptography: a stable name for a cache file. std::hash is
    // allowed to vary between runs of some implementations, so mix by
    // hand and keep it reproducible across sessions.
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    std::ostringstream o;
    o << std::hex << std::setw(16) << std::setfill('0') << h;
    return o.str();
}

}  // namespace

bool Narrator::initialize(const std::string& lore_path, std::string& error) {
    lore_ = slurp(lore_path);
    if (lore_.empty()) {
        error = "no lore at " + lore_path +
                ": the narrator knows nothing about the universe except "
                "what that file says, so there is nothing to narrate with";
        return false;
    }

    // Assume a key. Missing means broken, said out loud, rather than a
    // quiet fallback that produces convincing prose from nowhere.
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (!key || !*key) {
        error = "ANTHROPIC_API_KEY is not set. The narrator is not "
                "optional decoration here; export the key and run again.";
        return false;
    }

    llm_ = std::make_unique<Logosphere::LLMSystemHTTP>();
    if (!llm_->initialize_anthropic(key, "claude-sonnet-5")) {
        error = "could not reach the Anthropic API";
        llm_.reset();
        return false;
    }
    const std::string voice =
        std::string(
        "You narrate a science-fiction roleplaying character's life as it "
        "is generated. You are given FACTS that already happened: dice "
        "results, throws made and missed, skills gained, careers joined "
        "and refused. Your job is to say what that looked like.\n\n"
        "WRITE FROM THE NUMBERS. A throw is a moment, not a statistic. "
        "A 3 on 2D6 is a bad day and should read like one; an 11 is "
        "someone getting away with it. A characteristic marked poor or "
        "crippling is a real limitation the character lives with, and a "
        "good or exceptional one is what they lean on. If a throw was "
        "made ON the modifier - passed only because of a good "
        "characteristic, or failed only because of a bad one - say so, "
        "because that is the story.\n\n"
        "Rules you do not break:\n"
        "- Never contradict a fact. The numbers are the truth; you are "
        "the colour on top of them.\n"
        "- Never invent a world, faction, or history that is not in the "
        "setting below.\n"
        "- Never say what happens next. You narrate what has happened.\n"
        "- NEVER name a game mechanic. No dice, no rolls, no numbers, "
        "no modifiers, no targets, no tables. NEVER name a "
        "characteristic, in full or abbreviated: not Strength, not "
        "Str, not Dex, not Endurance, not Int, not Edu, not Soc, not "
        "'high Soc, high Edu'. Say what a strong or clumsy or "
        "well-connected person is LIKE, never which score says so. "
        "The reader is looking at the numbers already; your job is "
        "the part the numbers cannot say. Write as if the rulebook "
        "does not exist.\n"
        "- THE TRADE SHAPES THE SCENE. A smuggler is not promoted by a "
        "recruiter with paperwork; someone stops calling them the new "
        "one. A naval officer is not handed a chit in a yard. Belters, "
        "nobles, physicians and pirates rise in ways that belong to "
        "them, and a scene that would fit any career equally fits "
        "none. Read the career before you write the moment.\n"
        "- Be specific and invent freely WITHIN the setting: a name "
        "for the station, the shift supervisor who noticed something, "
        "the smell of the compartment, what they did with the money. "
        "Concrete invented detail is what makes a life feel lived. "
        "Never invent a world, faction or history that contradicts "
        "the setting, and never invent an outcome the facts do not "
        "support.\n"
        "- SHORT. Two sentences, sixty words at the outside. You are "
        "writing the moment, not the biography. Cut every clause that "
        "explains rather than shows.\n"
        "- Do not retell what has already been said. The reader has "
        "the earlier entries. A promotion is not the place to "
        "reintroduce someone's schooling or their age.\n"
        "- Four years pass in a term. Say what those years DID to "
        "them, not what they are like in general.\n\n"
        "THE SETTING:\n") + lore_;
    llm_->set_narrative_system_prompt(voice);
    // The cache is keyed by everything that shapes the words, the
    // voice included. Change a rule in the prompt and every story is
    // written again instead of being served from a stale file.
    voice_version_ = hex16(voice);

    cache_dir_ = "/tmp/logovger-narration";
    ::mkdir(cache_dir_.c_str(), 0755);
    return true;
}

std::string Narrator::cache_key(const Beat& beat, uint64_t seed) const {
    // The identity of a beat: the life it belongs to, what happened,
    // and the version of the lore it was written under. Change the
    // lore and every story is written again.
    std::ostringstream o;
    o << seed << '|' << beat.kind << '|' << beat.term << '|'
      << voice_version_ << '|' << build_prompt(beat);
    return hex16(o.str());
}

bool Narrator::cache_get(const std::string& key, std::string& prose) const {
    prose = slurp(cache_dir_ + "/" + key + ".txt");
    return !prose.empty();
}

void Narrator::cache_put(const std::string& key, const std::string& prose) {
    std::ofstream f(cache_dir_ + "/" + key + ".txt", std::ios::binary);
    f << prose;
}

std::string Narrator::build_prompt(const Beat& beat) const {
    std::ostringstream o;
    if (beat.kind == "bio") {
        o << "A new character has been rolled up. They are 18 and have "
             "done nothing yet.\n\n"
          << beat.sheet << "\n\n"
          << "Write who this person is, in two or three sentences. Their "
             "numbers are their whole history so far, so read them: a "
             "high Endurance and a low Education is a different life from "
             "the reverse. Do not give them a career; they have none yet.";
    o << "\n\nThen, on one final line by itself, write:\n"
         "FILE: a single clause of at most 55 characters for the "
         "personnel file, drawn from what you just wrote. Clipped, "
         "no full stop, the register of someone keeping a record on "
         "this person: \"passed over twice, said nothing\", \"good "
         "hands, no patience for rank\". It is a line in a dossier, "
         "not a sentence of prose.";
        return o.str();
    }
    if (beat.kind == "dossier") {
        o << "You are the assessor who opens a file on this person "
             "when they first come to notice. They are 18 and have "
             "done nothing yet.\n\n"
          << beat.sheet << "\n\n"
          << "Write the head of the file. EXACTLY four lines, each "
             "beginning with its label and nothing else, no other "
             "text before or after:\n"
             "NAME: a full name that belongs in this setting\n"
             "BORN: a place, and one short clause about what that "
             "place is like to be from. At most 70 characters.\n"
             "BUILD: how they look and carry themselves, read from "
             "what they are strong and weak at. At most 70 "
             "characters.\n"
             "NOTE: one dry line an assessor would actually write "
             "about them\n\n"
             "Invent the name and the place. Never mention a number, "
             "a characteristic name, or the file's own machinery.";
        return o.str();
    }
    if (beat.kind == "assessment") {
        o << "This life is over. The whole record:\n\n";
        for (const auto& f : beat.facts) o << "  " << f << "\n";
        o << "\n" << beat.sheet << "\n\n"
          << "Close the file in at most 280 characters, the assessor's "
             "own words: what this person turned out to be, and what "
             "the service made of them. Never mention a number, a "
             "characteristic name, a die or a rule.";
        return o.str();
    }
    o << "This just happened. Each line is a fact; where a throw is "
         "shown, the form is dice = raw, then the characteristic "
         "modifier, then the total against the target it had to "
         "beat:\n\n";
    for (const auto& f : beat.facts) o << "  " << f << "\n";
    o << "\nThe character as they now stand:\n" << beat.sheet << "\n\n"
      << "Write TWO SENTENCES, no more. Use the numbers to decide what "
         "happened and how narrowly, then write it without mentioning "
         "them: what these four years did to this person, in a scene "
         "that could only happen in THIS trade. End on where it leaves "
         "them, so the reader feels the next decision coming without "
         "being told what it is.";
    if (beat.kind == "refusal")
        o << " They were turned away. Do not soften it.";
    if (beat.kind == "death")
        o << " They died. State it plainly and stop.";
    o << "\n\nThen, on one final line by itself, write:\n"
         "FILE: a single clause of at most 55 characters for the "
         "personnel file, drawn from what you just wrote. Clipped, "
         "no full stop, the register of someone keeping a record on "
         "this person: \"passed over twice, said nothing\", \"good "
         "hands, no patience for rank\". It is a line in a dossier, "
         "not a sentence of prose.";
    return o.str();
}

bool Narrator::narrate(const Beat& beat, uint64_t seed,
                       std::function<void(const std::string&)> on_prose) {
    if (!llm_) return false;

    const auto key = cache_key(beat, seed);
    std::string cached;
    if (cache_get(key, cached)) {
        on_prose(cached);          // the same life tells the same story
        return true;
    }

    // The callback runs on the main thread inside poll(), so capturing
    // by value is safe and the caller never touches a worker thread.
    auto* pending = new std::function<void(const std::string&)>(
        std::move(on_prose));
    const auto cache_key_copy = key;
    llm_->submit_request(
        build_prompt(beat), 300,
        [this, cache_key_copy](const std::string&, const std::string& response,
                               void* user_data) {
            auto* cb = static_cast<std::function<void(const std::string&)>*>(
                user_data);
            // A failure comes back as text like "[ERROR: ...]", which
            // is not prose and must never be cached: a cached failure
            // replays forever, for that beat, across restarts, and
            // reads on screen as though the narrator said it. Drop it
            // and let the caller carry on without a story.
            if (!response.empty() && !is_failure(response)) {
                const_cast<Narrator*>(this)->cache_put(cache_key_copy,
                                                       response);
                (*cb)(response);
            } else {
                (*cb)(std::string());
            }
            delete cb;
        },
        pending);
    return true;
}

bool Narrator::is_failure(const std::string& response) {
    return response.compare(0, 7, "[ERROR:") == 0;
}

void Narrator::split_file_line(const std::string& reply, std::string& prose,
                               std::string& file_line) {
    prose.clear();
    file_line.clear();
    std::istringstream in(reply);
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 5, "FILE:") == 0) {
            file_line = line.substr(5);
            while (!file_line.empty() && file_line.front() == ' ')
                file_line.erase(0, 1);
            while (!file_line.empty() &&
                   (file_line.back() == '\r' || file_line.back() == ' ' ||
                    file_line.back() == '.'))
                file_line.pop_back();
            continue;
        }
        if (!prose.empty()) prose += "\n";
        prose += line;
    }
    while (!prose.empty() && (prose.back() == '\n' || prose.back() == ' '))
        prose.pop_back();
}

void Narrator::poll() {
    if (llm_) llm_->process_completed_responses();
}

kg::EntityID Narrator::record(kg::KGModule& kg, kg::EntityID character,
                              const Beat& beat, const std::string& prose) {
    auto id = kg.createEntity("Narration");
    if (id == kg::INVALID_ENTITY) return id;
    kg.setProperty(id, "narrative_text", prose);
    kg.setProperty(id, "narrates", std::to_string(character));
    kg.setProperty(id, "narration_kind", beat.kind);
    kg.setProperty(id, "narration_term", std::to_string(beat.term));
    std::string rolls;
    for (auto r : beat.roll_ids)
        rolls += (rolls.empty() ? "" : ",") + std::to_string(r);
    kg.setProperty(id, "narration_rolls", rolls);
    // The character owns its story, so a reader who has the character
    // has the whole life without knowing to look elsewhere.
    kg.createRelation(character, "HAS_PART", id);
    return id;
}

}  // namespace logovger
