// KGCore::setProperty ontology gate (Malleus H1)
//
// Until 2026-08-13 setProperty validated nothing: any engine system
// could stamp an undeclared key or a wrong-typed value and the KG
// silently absorbed it, while the LLM KGOp path ran full validation.
// This test proves the engine-side door is now closed with the SAME
// check (validate_property_write), strict by default:
//
//   - undeclared property  -> rejected (process aborts, loud message)
//   - wrong-typed value    -> rejected
//   - value outside schema min/max -> rejected
//   - declared + well-typed write  -> succeeds (the control: proves
//     the gate is not vacuous in the pass direction)
//   - property declared on a mixin ancestor (Spatial.position_x)
//     -> succeeds (inheritance walk works)
//   - KG_GATE_LENIENT=1    -> violation printed, write proceeds
//     (inventory mode)
//   - createEntityAtPosition survives strict (its chunk_x/chunk_y/x/y
//     stamps are declared on Spatial — regression tripwire for the
//     H1 schema declarations)
//
// Rejection in strict mode is abort() (house turtle-door pattern), so
// the red-path cases fork a child, capture its stderr, and assert
// SIGABRT + the actionable [KG GATE VIOLATION] message. POSIX-only
// mechanics (fork/waitpid), same portability envelope as the current
// headless CI (Linux + macOS).
//
// Also prints [measure] costs: the shared check per call, full
// setProperty per call, and the KGOp-path validate_kg_op per call
// (whose resolve_type walks every entity of every type) — the numbers
// behind routing the engine gate through validate_property_write
// instead of the full KGOp validator.
//
// Usage:
//   ./build/test_kg_property_gate

#include "logosphere/kg/kg_module.h"
#include "logosphere/kg/ontology_validator.h"
#include "generated/logosphere_ontology_registry.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>

#ifdef _WIN32
// The header above is explicit: the red paths NEED fork/waitpid to
// observe abort() from outside the dying process. On MSVC the file
// compiles to a loud SKIP; the gate's abort contract stays proven by
// the POSIX lanes (Linux CI + macOS).
int main() {
    std::cout << "SKIP test_kg_property_gate: death tests are "
                 "fork()-based (see file header); the gate's abort "
                 "contract is proven on the POSIX lanes." << std::endl;
    return 0;
}
#else  // POSIX: fork/waitpid available

#include <sys/wait.h>
#include <unistd.h>

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAIL: " << msg << std::endl; \
        tests_failed++; \
    } else { \
        std::cout << "PASS: " << msg << std::endl; \
        tests_passed++; \
    } \
} while (0)

static int tests_passed = 0;
static int tests_failed = 0;

// Run `violating_write` in a forked child with stderr captured.
// Returns true iff the child died of SIGABRT. err_out gets the child's
// stderr so callers can assert the message is actionable.
static bool write_aborts(const std::function<void()>& violating_write,
                         std::string& err_out) {
    int fds[2];
    if (pipe(fds) != 0) {
        std::cerr << "FAIL: pipe() failed: " << std::strerror(errno) << std::endl;
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "FAIL: fork() failed: " << std::strerror(errno) << std::endl;
        return false;
    }
    if (pid == 0) {
        // Child: stderr -> pipe, run the write. If the gate does NOT
        // fire we exit 0 and the parent reads that as gate-missing.
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        violating_write();
        _exit(0);
    }
    close(fds[1]);
    err_out.clear();
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        err_out.append(buf, static_cast<size_t>(n));
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

// KGModule declares a destructor (PIMPL over KGCore), which suppresses
// its move constructor — so no factory-by-value; construct in place.
#define MAKE_KG(var) \
    kg::KGModule var(logosphere::ontology::registry()); \
    var.setMode(kg::KGMode::MINIMAL)

// --- Red path: strict rejections -----------------------------------

void test_undeclared_property_rejected() {
    std::string err;
    bool aborted = write_aborts([] {
        MAKE_KG(kg);
        auto e = kg.createEntity("Humanoid");
        kg.setProperty(e, "flux_capacitance", "1.21");  // declared nowhere
    }, err);
    ASSERT(aborted, "undeclared property write aborts under strict gate");
    ASSERT(err.find("[KG GATE VIOLATION]") != std::string::npos,
           "violation message is printed");
    ASSERT(err.find("has no property") != std::string::npos,
           "message names the missing declaration");
    ASSERT(err.find("flux_capacitance") != std::string::npos,
           "message names the offending key");
}

void test_wrong_typed_value_rejected() {
    std::string err;
    bool aborted = write_aborts([] {
        MAKE_KG(kg);
        auto e = kg.createEntity("Humanoid");
        kg.setProperty(e, "health", "banana");  // health is float
    }, err);
    ASSERT(aborted, "wrong-typed value write aborts under strict gate");
    ASSERT(err.find("expected float") != std::string::npos,
           "message states the expected value type");
}

void test_out_of_range_value_rejected() {
    std::string err;
    bool aborted = write_aborts([] {
        MAKE_KG(kg);
        auto e = kg.createEntity("Hips");
        // Bondable.bond_strength: schema max 1000000
        kg.setProperty(e, "bond_strength", "2000000");
    }, err);
    ASSERT(aborted, "out-of-schema-range value aborts under strict gate");
    ASSERT(err.find("above schema maximum") != std::string::npos,
           "message states the violated bound");
}

// --- Green path: the controls --------------------------------------

void test_declared_well_typed_write_succeeds() {
    MAKE_KG(kg);
    auto e = kg.createEntity("Humanoid");
    kg.setProperty(e, "health", "75.5");
    ASSERT(kg.getProperty(e, "health") == "75.5",
           "declared + well-typed write stores and reads back");
}

void test_ancestor_mixin_property_succeeds() {
    MAKE_KG(kg);
    auto e = kg.createEntity("Hips");
    kg.setProperty(e, "position_x", "3.25");  // Spatial mixin, via ancestors
    ASSERT(kg.getProperty(e, "position_x") == "3.25",
           "property declared on a mixin ancestor passes the gate");
}

void test_create_at_position_survives_strict() {
    MAKE_KG(kg);
    auto e = kg.createEntityAtPosition("Humanoid", 123.0f, -45.0f);
    ASSERT(e != kg::INVALID_ENTITY, "createEntityAtPosition returns an entity");
    ASSERT(kg.getProperty(e, "chunk_x") == "2",
           "chunk_x stamp passes the gate (declared on Spatial)");
    ASSERT(kg.getProperty(e, "x") == std::to_string(123.0f),
           "x stamp passes the gate (declared on Spatial)");
}

// --- Lenient lever: inventory mode ---------------------------------

void test_lenient_downgrades_to_print() {
    setenv("KG_GATE_LENIENT", "1", 1);
    {
        MAKE_KG(kg);
        auto e = kg.createEntity("Humanoid");
        kg.setProperty(e, "flux_capacitance", "1.21");
        ASSERT(kg.getProperty(e, "flux_capacitance") == "1.21",
               "lenient mode prints the violation and lets the write proceed");
    }
    unsetenv("KG_GATE_LENIENT");
}

// --- [measure] the cost of the gate --------------------------------

void measure_gate_cost() {
    using clock = std::chrono::steady_clock;
    const auto& ont = logosphere::ontology::registry();

    MAKE_KG(kg);
    auto e = kg.createEntity("Hips");  // 14 ancestors: worst-case walk

    constexpr int N = 100000;

    // 1) The shared check alone.
    auto t0 = clock::now();
    for (int i = 0; i < N; i++) {
        auto r = kg::validate_property_write(ont, "Hips", "health", "50.0");
        if (!r.ok) std::abort();
    }
    auto t1 = clock::now();
    double check_ns =
        std::chrono::duration<double, std::nano>(t1 - t0).count() / N;

    // 2) Full production setProperty (lock + gate + map write).
    t0 = clock::now();
    for (int i = 0; i < N; i++) {
        kg.setProperty(e, "health", std::to_string(i % 100));
    }
    t1 = clock::now();
    double set_ns =
        std::chrono::duration<double, std::nano>(t1 - t0).count() / N;

    // 3) The full KGOp validator at 2000 entities — its resolve_type
    //    walks findByType over every registered type per call.
    for (int i = 0; i < 2000; i++) kg.createEntity("Hips");
    kg::KGOp op = kg::KGOpSetProperty{{e, ""}, "health", "50.0"};
    constexpr int M = 200;
    t0 = clock::now();
    for (int i = 0; i < M; i++) {
        auto r = kg::validate_kg_op(op, kg, ont);
        if (!r.ok) std::abort();
    }
    t1 = clock::now();
    double kgop_ns =
        std::chrono::duration<double, std::nano>(t1 - t0).count() / M;

    std::cout << "[measure] validate_property_write: " << check_ns
              << " ns/call" << std::endl;
    std::cout << "[measure] setProperty (gate + lock + write): " << set_ns
              << " ns/call" << std::endl;
    std::cout << "[measure] full KGOp validate_kg_op @2001 entities: "
              << kgop_ns << " ns/call ("
              << (kgop_ns / (check_ns > 0 ? check_ns : 1.0))
              << "x the shared check)" << std::endl;
}

int main() {
    unsetenv("KG_GATE_LENIENT");  // guarantee strict for the red path

    std::cout << "=== KG property gate (Malleus H1) ===" << std::endl;

    test_undeclared_property_rejected();
    test_wrong_typed_value_rejected();
    test_out_of_range_value_rejected();
    test_declared_well_typed_write_succeeds();
    test_ancestor_mixin_property_succeeds();
    test_create_at_position_survives_strict();
    test_lenient_downgrades_to_print();
    measure_gate_cost();

    std::cout << "Passed: " << tests_passed
              << "  Failed: " << tests_failed << std::endl;
    return tests_failed == 0 ? 0 : 1;
}

#endif  // POSIX: fork/waitpid available
