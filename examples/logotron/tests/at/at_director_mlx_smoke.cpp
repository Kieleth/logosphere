// Acceptance Test: end-to-end smoke against an OpenAI-compatible
// local LLM (mlx_lm.server on Apple Silicon, llama-server, Ollama,
// LM Studio). Prints SKIPPED and exits 0 when no server is reachable
// so CI / Linux / no-MLX environments don't go red — and never wears
// a PASS banner for a round trip that did not run.
//
// What it proves when it runs for real:
//   1. LLMSystemHTTP wires up against a localhost server.
//   2. The Director responder dispatches a real request.
//   3. The worker thread completes within the timeout (default 30 s).
//   4. process_completed_responses() drains the response into the
//      Director's ready_, poll() returns it cached.
//   5. The parser ingests live LLM output (markdown fences, prose
//      preamble, etc) and produces ≥1 valid kg_op.
//   6. validate_kg_op + apply_kg_op land at least one op against the
//      live logotron ontology.
//
// Without this AT, every "the local LLM stack works" claim is a
// claim about a manual playtest. With it, the same proof runs in
// 10 s on the dev box and tells you immediately when something in
// the responder / drain / parse / validate / apply chain regresses.
//
// Env knobs:
//   LOGOTRON_AT_MLX_URL    default http://localhost:8081
//   LOGOTRON_AT_MLX_MODEL  default mlx-community/Qwen2.5-14B-Instruct-4bit
//   LOGOTRON_AT_MLX_TIMEOUT  seconds to wait for response, default 30
//
// Run: ./build/at_logotron_director_mlx_smoke

#include "at_common.h"

#include "director/director.h"
#include "director/director_parser.h"
#include "director/symbolic_refs.h"

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/kg_ops.h"
#include "logosphere/kg/kg_ops_apply.h"
#include "logosphere/kg/ontology_serialize.h"
#include "logosphere/kg/ontology_validator.h"
#include "logosphere/llm/llm_system_http.h"

#include "logotron_ontology_registry.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <variant>

static int tests_passed = 0;
static int tests_failed = 0;

namespace dir = logotron::director;

namespace {

const char* env_or(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : fallback;
}

// Is anything listening at host:port? Plain TCP connect, 1-second
// timeout. We don't issue an HTTP request — the smoke AT itself does
// that through LLMSystemHTTP. This probe just decides skip vs run.
bool is_reachable(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    auto port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }
    // mlx_lm.server binds IPv4 by default; "localhost" can resolve
    // to ::1 (IPv6) first on macOS, which fails to connect even
    // though the IPv4 socket is up. Walk every address until one
    // accepts a connection (or all are exhausted).
    bool ok = false;
    for (auto* ai = res; ai != nullptr; ai = ai->ai_next) {
        int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) continue;
        timeval tv{1, 0};  // 1 s
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        int rc = connect(sock, ai->ai_addr, ai->ai_addrlen);
        close(sock);
        if (rc == 0) { ok = true; break; }
    }
    freeaddrinfo(res);
    return ok;
}

// Strip "http://" or "https://", split host:port. Returns false
// if the URL doesn't parse cleanly (rare; no need for full URL
// parsing here, just sanity).
bool split_url(const std::string& url, std::string& host, int& port) {
    std::string s = url;
    auto pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);
    auto colon = s.find(':');
    if (colon == std::string::npos) {
        host = s; port = 80; return !host.empty();
    }
    host = s.substr(0, colon);
    try { port = std::stoi(s.substr(colon + 1)); }
    catch (...) { return false; }
    return true;
}

// Tiny KG harness for the apply assertion.
struct MiniWorld {
    kg::OntologyRegistry registry;
    kg::KGModule kg;
    kg::EntityID player = kg::INVALID_ENTITY;
    kg::EntityID ai     = kg::INVALID_ENTITY;
    kg::EntityID arena  = kg::INVALID_ENTITY;

    MiniWorld() : registry(), kg(registry) {
        kg.extendOntology(logotron::ontology::registry());
        kg.setMode(kg::KGMode::MINIMAL);
        player = kg.createEntity("PlayerCycle");
        ai     = kg.createEntity("AICycle");
        arena  = kg.createEntity("Arena");
        if (player == kg::INVALID_ENTITY ||
            ai     == kg::INVALID_ENTITY ||
            arena  == kg::INVALID_ENTITY) {
            throw std::runtime_error("MiniWorld createEntity returned INVALID");
        }
        kg.setProperty(player, "max_speed", "8");
        kg.setProperty(ai,     "max_speed", "8");
        kg.setProperty(arena,  "arena_w", "40");
        kg.setProperty(arena,  "arena_h", "40");
    }
};

void test_mlx_director_round_trip() {
    const std::string url   = env_or("LOGOTRON_AT_MLX_URL",
                                      "http://localhost:8081");
    const std::string model = env_or("LOGOTRON_AT_MLX_MODEL",
                                      "mlx-community/Qwen2.5-14B-Instruct-4bit");
    const int timeout_s = []() {
        const char* e = std::getenv("LOGOTRON_AT_MLX_TIMEOUT");
        if (e && *e) try { return std::stoi(e); } catch (...) {}
        return 30;
    }();

    // Reachability is decided in main() BEFORE this test runs — a skip
    // must never wear a PASS banner. If the server dies between that
    // probe and here, the failures below name it honestly.
    Logosphere::LLMSystemHTTP llm;
    if (!llm.initialize_mlx(url, model)) {
        throw std::runtime_error("LLMSystemHTTP::initialize_mlx failed: "
                                 + llm.get_last_error());
    }

    MiniWorld world;
    dir::Director director;
    director.set_responder(
        [&llm](const std::string& sys, const std::string& user,
               std::function<void(std::string)> done) {
            llm.set_narrative_system_prompt(sys);
            llm.submit_request(
                user, /*max_tokens=*/600,
                [done](const std::string& /*p*/, const std::string& response,
                       void* /*ud*/) { done(response); });
        });

    dir::GameState state;
    state.round_number       = 0;
    state.arena_w            = 40.0f;
    state.arena_h            = 40.0f;
    state.player_max_speed   = 8.0f;
    {
        dir::GameState::RiderInfo ri;
        ri.ref = "@program_1"; ri.personality = "default";
        ri.max_speed = 8.0f; ri.trail_count = 0;
        state.riders.push_back(ri);
    }
    state.ontology_slice = kg::serialize_ontology_slice(
        world.registry,
        std::vector<std::string>{"Cycle", "PlayerCycle", "AICycle",
                                  "TrailSegment", "Arena", "Wormhole"});
    auto refs = dir::build_symbolic_refs(world.player, {world.ai}, world.arena);
    state.symbols_text = refs.as_prompt_text();

    AT_ASSERT_TRUE(director.fire(state),
                   "Director::fire returned false");

    // Poll loop: drain LLMSystemHTTP every 50 ms, then poll Director.
    // Mirrors the live-game flow (poll_director step 0 → step 1).
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(timeout_s);
    dir::DirectorResponse resp;
    bool got_response = false;
    while (std::chrono::steady_clock::now() < deadline) {
        llm.process_completed_responses();
        if (director.poll(resp)) { got_response = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    AT_ASSERT_TRUE(got_response,
        "no Director response within " + std::to_string(timeout_s) + "s");
    AT_ASSERT_TRUE(resp.parse_error.empty(),
        "parse error: " + resp.parse_error);
    AT_ASSERT_TRUE(!resp.kg_ops.empty(),
        "Director response had zero kg_ops (LLM produced no actions)");

    // Validate + apply at least one op. The LLM may emit ops we
    // can't validate (unknown enums, out-of-range etc) — that's
    // fine; we just need >=1 to land.
    auto resolve_ref = [&](kg::EntityRef& ref) {
        if (ref.is_symbolic()) {
            auto resolved = refs.resolve("@" + ref.symbolic);
            if (resolved != kg::INVALID_ENTITY) {
                ref.id = resolved;
                ref.symbolic.clear();
            }
        }
    };
    int applied = 0;
    int rejected = 0;
    for (auto& op : resp.kg_ops) {
        std::visit([&](auto& concrete) {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, kg::KGOpDestroyEntity>) {
                resolve_ref(concrete.target);
            } else if constexpr (std::is_same_v<T, kg::KGOpSetProperty>) {
                resolve_ref(concrete.target);
            } else if constexpr (std::is_same_v<T, kg::KGOpSetRelation>) {
                resolve_ref(concrete.from);
                resolve_ref(concrete.to);
            }
        }, op);
        auto v = kg::validate_kg_op(op, world.kg, world.kg.getRegistry());
        if (!v.ok) { rejected++; continue; }
        auto a = kg::apply_kg_op(op, world.kg);
        if (a.ok) applied++;
    }

    std::cerr << "  [info] LLM produced " << resp.kg_ops.size()
              << " ops (" << applied << " applied, " << rejected
              << " rejected)" << std::endl;
    if (!resp.thoughts.empty()) {
        std::cerr << "  [info] LLM thoughts: " << resp.thoughts << std::endl;
    }
    AT_ASSERT_TRUE(applied >= 1,
        "no LLM ops survived validate+apply (rejected=" +
        std::to_string(rejected) + ")");
}

}  // namespace

int main() {
    std::cout << "Logotron AT — director_mlx_smoke" << std::endl;

    // Skip vs run is decided HERE, before any test banner: the runner
    // must see SKIPPED, never a PASS that proved nothing.
    {
        const std::string url = env_or("LOGOTRON_AT_MLX_URL",
                                       "http://localhost:8081");
        std::string host;
        int port = 0;
        if (!split_url(url, host, port)) {
            std::cout << "SKIPPED: could not parse LOGOTRON_AT_MLX_URL "
                      << url << std::endl;
            return 0;
        }
        if (!is_reachable(host, port)) {
            std::cout << "SKIPPED: no server at " << host << ":" << port
                      << " — start mlx_lm.server "
                         "(scripts/start_mlx_server.sh) to run this AT "
                         "for real." << std::endl;
            return 0;
        }
    }

    AT_TEST(test_mlx_director_round_trip);
    std::cout << tests_passed << " passed, " << tests_failed << " failed"
              << std::endl;
    return tests_failed == 0 ? 0 : 1;
}
