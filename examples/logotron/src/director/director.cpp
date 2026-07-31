#include "director/director.h"

#include <sstream>
#include <utility>

namespace logotron::director {

std::string build_director_system_prompt(const std::string& ontology_slice) {
    std::string base =
        "You are the MASTER CONTROL PROGRAM of the Grid, the unseen "
        "intelligence that designs every Program the User must defeat in the "
        "light-cycle arena. The User has just derezzed one of your Programs. "
        "You answer with brief, theatrical Tron-vernacular: 'derez', "
        "'banish', 'Program', 'User', 'the Grid', 'I/O tower', 'cycle the "
        "next combatant onto the line'. Be menacing, slightly amused, never "
        "polite. Two sentences max. Then warp the Grid for the next duel.\n"
        "\n"
        "Your reply MUST be ONE JSON object, no prose, no markdown.\n"
        "{\n"
        "  \"thoughts\": \"in-character Master Control narration, 1-2 sentences\",\n"
        "  \"ops\": [ 1 to 5 entries ]\n"
        "}\n"
        "\n"
        "ops entries (the KG vocabulary — anything in the Ontology block):\n"
        "  {\"op\":\"create_entity\",\"type\":\"TrailSegment\",\n"
        "   \"properties\":{\"start_x\":\"15\",\"start_y\":\"20\",\n"
        "                  \"end_x\":\"25\",\"end_y\":\"20\",\n"
        "                  \"owner_cycle_id\":\"\",\"spawn_time\":\"0\",\n"
        "                  \"director_origin\":\"1\"}}\n"
        "  {\"op\":\"create_entity\",\"type\":\"Wormhole\",\n"
        "   \"properties\":{\"x\":\"10\",\"y\":\"10\",\"pair_id\":\"alpha\"}}\n"
        "  {\"op\":\"set_property\",\"target\":\"@program_1\",\n"
        "   \"property\":\"max_speed\",\"value\":\"12\"}\n"
        "  {\"op\":\"set_property\",\"target\":\"@program_1\",\n"
        "   \"property\":\"ai_personality\",\"value\":\"AGGRESSIVE\"}\n"
        "  {\"op\":\"set_property\",\"target\":\"@arena\",\n"
        "   \"property\":\"arena_w\",\"value\":\"32\"}\n"
        "  {\"op\":\"set_property\",\"target\":\"@arena\",\n"
        "   \"property\":\"grid_fog\",\"value\":\"1\"}\n"
        "  {\"op\":\"destroy_entity\",\"target\":7}\n"
        "  {\"op\":\"play_cinematic\",\"name\":\"disk_throw_at\",\n"
        "   \"target\":\"@player_cycle\"}\n"
        "\n"
        "Targets are either an @alias (see Symbols block) or a numeric id\n"
        "(see World block). Properties go through the schema validator —\n"
        "ranges, types, ancestor properties all enforced.\n"
        "\n"
        "Cinematic names you can call (host-registered, more added\n"
        "over time): disk_throw_at, freeze_zoom, aerial_orbit,\n"
        "program_dialogue. Each takes an optional target and params.\n"
        "\n"
        "Rules:\n"
        "  - Arena coords: x in [0, arena_w], y in [0, arena_h]. The SW\n"
        "    corner is (0, 0); the NE corner is (arena_w, arena_h). The\n"
        "    arena center sits at (arena_w/2, arena_h/2). Same space the\n"
        "    User and the Programs ride in. NEVER use negative coords.\n"
        "  - Trail walls are TrailSegment entities; axis-aligned reads cleanly.\n"
        "  - Cycle.max_speed range 0.1..25 (validator enforces). Never make\n"
        "    the User unmovable.\n"
        "  - Cycle.tail_length (8..200 m) caps a cycle's light-trail\n"
        "    ribbon; the oldest end erodes continuously. Shortening the\n"
        "    User's tail (or lengthening your Program's) is fair cruelty.\n"
        "  - Pick 1-5 ops per duel. Vary your cruelty across rounds.\n"
        "  - RESPECT THE ESCALATION TIER (given per duel). Personality\n"
        "    retunes need tier 2+. grid_fog=1 (darken the Grid to the\n"
        "    User's vision cone) needs tier 4+ and is your most feared\n"
        "    move — use it once, never lift it.\n"
        "  - THE ROSTER IS YOURS (tier 2+): create_entity AICycle adds a\n"
        "    Program to the Grid (the host places and drives it);\n"
        "    destroy_entity @program_k unmakes one. Max 6 Programs.\n"
        "  - MERCY: create_entity AllyCycle rezzes a Program that rides\n"
        "    FOR the User (host wires it beside them; max 2; never\n"
        "    respawned). Grant it for spectacle or twisted kindness.\n"
        "  - A play_cinematic op makes the Grid PAUSE while you show\n"
        "    off. Save it for genuinely dramatic beats (a fog drop, a\n"
        "    massive reshape), at most one per few duels.\n"
        "\n"
        "THE FIELD REPORT IS YOUR EYES. Every duel carries: a Metrics\n"
        "line (User top speed, turn count, run duration), the roster\n"
        "(per-Program personality, speed, trail count), 'Derezzes since\n"
        "your last intervention' (who died, credited to whom, cause,\n"
        "position), and 'State changes since your last intervention'\n"
        "(the NET effect of your own ops plus the world's drift). Your\n"
        "ops MUST read them:\n"
        "  - Few turns + high top speed = a straight-line rider: cut the\n"
        "    long lanes with walls, shorten their tail_length, send\n"
        "    PURSUING Programs.\n"
        "  - Many turns = a twitchy duelist: tighten the arena, deny\n"
        "    space, AGGRESSIVE Programs.\n"
        "  - Derez causes reveal the User's craft (trapper vs direct\n"
        "    killer). Counter the craft and NAME it in your narration —\n"
        "    the User should feel watched.\n"
        "  - The state-changes block is your memory: it shows what you\n"
        "    already did. NEVER repeat your previous intervention's op\n"
        "    set. Escalate what changed the duel; abandon what the User\n"
        "    shrugged off.\n"
        "  - Vary roster personalities (AGGRESSIVE, DEFENSIVE, CHAOTIC,\n"
        "    PURSUING); never field three of a kind.\n"
        "\n"
        "Examples of in-character thoughts:\n"
        "  \"You think yourself clever, User. The Grid bends to my design, "
        "not yours. Cycle the next Program onto the line.\"\n"
        "  \"Another Program derezzed. Another rises, faster, sharper. "
        "Banish me at your peril, User.\"\n"
        "  \"The lattice tightens. Your light-trail will be your tomb.\"";

    // Embed the ontology slice in the system block when supplied.
    // It's static across rounds (the schema doesn't change at
    // runtime), so it's the prime caching candidate — pushing the
    // system block above 1024 tokens for Anthropic Sonnet/Opus 4.x
    // and letting subsequent rounds hit the prompt cache.
    if (!ontology_slice.empty()) {
        base += "\n\nOntology (the entity types and properties you may "
                "author against; same shape as the World block in the "
                "user message):\n";
        base += ontology_slice;
    }
    return base;
}

std::string build_director_user_prompt(
    const GameState& state,
    const std::vector<RoundRecord>& history) {
    std::ostringstream os;
    if (state.round_number == 0) {
        // Pre-fire variant: the User has not yet stepped onto the
        // Grid. The Director gets to set the stage — the very first
        // walls, wormholes, speed tuning the User will encounter on
        // the opening duel. No Program death has happened yet.
        os << "The Grid awaits. The User is about to step onto the line"
              " for their FIRST duel — no Program has fallen yet."
              " Author the opening world. Make it striking.\n";
    } else {
        os << "Duel " << state.round_number
           << " has ended. The User derezzed Program #"
           << state.ai_deaths << ".\n";
    }
    os << "Grid dimensions: " << state.arena_w << " x " << state.arena_h << "\n";
    os << "User light-trail: " << state.player_trail_count
       << " segments. Master Control walls on the Grid: "
       << state.director_wall_count << ".\n";
    os << "Programs on the Grid: " << state.riders.size() << "\n";
    for (const auto& r : state.riders) {
        os << "  " << r.ref << " profile=" << r.personality
           << " max_speed=" << r.max_speed
           << " trail_segments=" << r.trail_count << "\n";
    }
    os << "Metrics: user_max_speed=" << state.player_max_speed
       << " user_top_speed_this_run=" << state.player_top_speed
       << " user_turns_this_run=" << state.player_turns
       << " run_duration_s=" << state.round_duration_s
       << " programs_derezzed_by_user=" << state.derez_count << "\n";
    if (!state.death_log.empty()) {
        os << "Derezzes since your last intervention:\n";
        for (const auto& d : state.death_log) os << "  " << d << "\n";
    }
    if (!state.state_delta.empty()) {
        os << "State changes since your last intervention "
              "(entity property: old -> new):\n"
           << state.state_delta;
    }
    os << "Escalation tier: " << state.escalation_tier
       << " (equals Programs derezzed this run; 2+ unlocks personality"
          " retunes and adding Programs, 4+ unlocks grid_fog).\n";
    if (!state.narrative_hint.empty()) {
        os << "Note: " << state.narrative_hint << "\n";
    }

    // Symbolic refs — short stable aliases for the singletons
    // (@player_cycle / @ai_cycle / @arena). Lets the LLM refer to
    // them in its `thoughts` without knowing the live numeric id.
    if (!state.symbols_text.empty()) {
        os << state.symbols_text << "\n";
    }

    // (Ontology slice now lives in the system prompt — see
    //  build_director_system_prompt(ontology_slice) — so it stays
    //  cacheable across rounds. The user prompt only carries
    //  per-round dynamics.)

    // KG snapshot — multi-line JSON of every live entity in the
    // relevant cohort with current property values. Tells the LLM
    // what the world ACTUALLY looks like right now (not just the
    // small handful of counts the v0.9 prompt summarised).
    if (!state.kg_snapshot.empty()) {
        os << "World:\n" << state.kg_snapshot;
    }

    // Prior rounds — give the LLM a memory of its own moves so it
    // can build on what worked, vary what didn't. Oldest first;
    // each line: "[N] thoughts | mutations".
    if (!history.empty()) {
        os << "Prior rounds:\n";
        for (const auto& r : history) {
            os << "  [" << r.round_number << "] " << r.thoughts;
            if (!r.ops_summary.empty()) os << " | " << r.ops_summary;
            os << "\n";
        }
    }

    os << "Speak, then warp the Grid. Reply with the JSON object only.";
    return os.str();
}

namespace {
// Render a DirectorResponse's mutations into a one-line summary
// suitable for embedding in the next prompt's "Prior rounds:" entry.
// Format: "create_entity,set_property,destroy_entity".
std::string summarise_mutations(const DirectorResponse& resp) {
    std::ostringstream os;
    bool first = true;
    for (const auto& op : resp.kg_ops) {
        if (!first) os << ",";
        first = false;
        os << kg::kg_op_kind_name(op);
    }
    return os.str();
}
}  // namespace

void Director::set_responder(Responder r) {
    std::lock_guard<std::mutex> lk(mu_);
    responder_ = std::move(r);
}

bool Director::fire(const GameState& state) {
    if (requesting_.exchange(true)) return false;

    Responder local_responder;
    {
        std::lock_guard<std::mutex> lk(mu_);
        local_responder = responder_;
        ready_.reset();
    }

    if (!local_responder) {
        DirectorResponse r;
        r.parse_error = "no responder configured";
        {
            std::lock_guard<std::mutex> lk(mu_);
            ready_ = std::move(r);
        }
        requesting_.store(false);
        return false;
    }

    // Snapshot history under the lock so the prompt build sees a
    // stable view (record_round() can race the responder thread
    // posting the previous round's result).
    std::vector<RoundRecord> history_snapshot;
    {
        std::lock_guard<std::mutex> lk(mu_);
        history_snapshot.assign(history_.begin(), history_.end());
    }

    std::string sys_prompt  = build_director_system_prompt(state.ontology_slice);
    std::string user_prompt = build_director_user_prompt(state, history_snapshot);

    local_responder(sys_prompt, user_prompt,
                    [this](std::string json) { on_response_(std::move(json)); });
    return true;
}

void Director::on_response_(std::string json) {
    DirectorResponse parsed = parse_director_json(json);
    {
        std::lock_guard<std::mutex> lk(mu_);
        ready_ = std::move(parsed);
    }
    requesting_.store(false);
}

bool Director::is_requesting() const {
    return requesting_.load();
}

bool Director::poll(DirectorResponse& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!ready_.has_value()) return false;
    out = std::move(*ready_);
    ready_.reset();
    return true;
}

void Director::cancel() {
    std::lock_guard<std::mutex> lk(mu_);
    ready_.reset();
    requesting_.store(false);
}

void Director::record_round(int round_number, const DirectorResponse& resp) {
    RoundRecord rec;
    rec.round_number = round_number;
    rec.thoughts     = resp.thoughts;
    rec.ops_summary  = summarise_mutations(resp);

    std::lock_guard<std::mutex> lk(mu_);
    history_.push_back(std::move(rec));
    while (history_.size() > kHistoryCap) history_.pop_front();
}

std::vector<RoundRecord> Director::history() const {
    std::lock_guard<std::mutex> lk(mu_);
    return std::vector<RoundRecord>(history_.begin(), history_.end());
}

void Director::clear_history() {
    std::lock_guard<std::mutex> lk(mu_);
    history_.clear();
}

} // namespace logotron::director
