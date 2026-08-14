// ============================================================================
// INV-29 STATIC GATE — no bare numeric literal with physical meaning in
// physics translation units
// ============================================================================
// INV-29 (constants-are-inputs): every constant with physical meaning is a
// named, grouped, documented INPUT in the registry (schema/physics.yaml ->
// src/generated/physics_constants.h), never an inline number. This test is
// the invariant's static mechanism: it scans the physics TUs and fails on
// any numeric literal the discrimination rule calls magic that is not on
// the KNOWN_RESIDUALS ratchet below.
//
// THE DISCRIMINATION RULE, honestly stated:
//
//   Scanned: FLOAT literals only (a '.' or exponent form), in code, with
//   comments, string literals and char literals stripped first — format
//   strings are not magic.
//
//   NOT magic:
//     - identity / algebra values: 0, 1, -1, 0.5, 2 (negation, halving of
//       extents, doubling, averaging). These appear in formulas, not as
//       tuning.
//     - precision guards, |v| <= 1e-5: below every physical scale this
//       engine resolves (SLOP is 1e-3 m); they guard float division and
//       normalization, not physics.
//     - sentinels, |v| >= 1e9: infinity stand-ins and "effectively off"
//       defaults, not quantities.
//
//   Magic: everything else — thresholds, coefficients, velocities, masses,
//   times, fractions. Those are inputs and belong in the registry.
//
//   STATED LIMITATION: integer literals are not scanned. In C++ they are
//   overwhelmingly indices, sizes, counts and enum values; every physical
//   integer input found by the census (frames, iterations) already lives
//   in the registry. A rule that scanned ints honestly would need semantic
//   judgment a static scan does not have. If an integer with physical
//   meaning is introduced inline, this gate will not see it; review must.
//
// THE RATCHET. KNOWN_RESIDUALS lists every magic float the scan finds
// today, per file, per token, with counts (35 sites; contexts and the
// extraction follow-ups are in tests/invariants/LEDGER.md, Stage E entry).
// The test passes only while the scan matches the table EXACTLY:
//   - a literal not in the table (or a count above it) = new magic number,
//     FAIL: put it in the registry instead.
//   - a count below the table = a residual was extracted, FAIL: shrink the
//     table in the same commit, and flip INV-29 to active when it empties.
//
// While the table is non-empty, INV-29 stays ASPIRATIONAL: the gate proves
// the residual set does not grow, not that it is zero.
//
// SELF-CHECK (the control, per docs/testing_guidelines.md): before
// scanning the tree, the scanner is run on synthetic snippets where the
// answer is known both ways — a magic 0.98f in code must be found; the
// same token in a comment or string, an identity 0.5f, a 1e-12f guard and
// a 1e10f sentinel must not.
//
// Headless-safe: pure file IO, no engine state.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Comment / string stripping: small state machine, no regex fragility.
// ---------------------------------------------------------------------------
static std::string strip_comments_and_strings(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    enum State { CODE, LINE_COMMENT, BLOCK_COMMENT, STRING, CHAR } st = CODE;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        char n = (i + 1 < in.size()) ? in[i + 1] : '\0';
        switch (st) {
            case CODE:
                if (c == '/' && n == '/') { st = LINE_COMMENT; ++i; out += ' '; }
                else if (c == '/' && n == '*') { st = BLOCK_COMMENT; ++i; out += ' '; }
                else if (c == '"') { st = STRING; out += ' '; }
                else if (c == '\'') { st = CHAR; out += ' '; }
                else out += c;
                break;
            case LINE_COMMENT:
                if (c == '\n') { st = CODE; out += '\n'; } else out += ' ';
                break;
            case BLOCK_COMMENT:
                if (c == '*' && n == '/') { st = CODE; ++i; out += ' '; }
                else out += (c == '\n') ? '\n' : ' ';
                break;
            case STRING:
                if (c == '\\') { ++i; out += ' '; }
                else if (c == '"') { st = CODE; out += ' '; }
                else out += (c == '\n') ? '\n' : ' ';
                break;
            case CHAR:
                if (c == '\\') { ++i; out += ' '; }
                else if (c == '\'') { st = CODE; out += ' '; }
                else out += ' ';
                break;
        }
    }
    return out;
}

static bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// ---------------------------------------------------------------------------
// Float-literal scanner. Returns each magic token found, normalized to its
// source spelling (so the ratchet table reads like the code does).
// ---------------------------------------------------------------------------
struct Hit { std::string token; int line; };

static std::vector<Hit> scan_magic_floats(const std::string& code) {
    std::vector<Hit> hits;
    int line = 1;
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i] == '\n') { ++line; continue; }
        // literal must not continue an identifier or a member access (x.5 is
        // not a literal start; p.z is not a number)
        if (i > 0 && (ident_char(code[i - 1]) || code[i - 1] == '.')) continue;
        bool digit = (code[i] >= '0' && code[i] <= '9');
        bool dot_start = (code[i] == '.' && i + 1 < code.size() &&
                          code[i + 1] >= '0' && code[i + 1] <= '9');
        if (!digit && !dot_start) continue;
        size_t j = i;
        bool has_dot = false, has_exp = false;
        while (j < code.size()) {
            char c = code[j];
            if (c >= '0' && c <= '9') { ++j; continue; }
            if (c == '.' && !has_dot && !has_exp) { has_dot = true; ++j; continue; }
            if ((c == 'e' || c == 'E') && !has_exp && j + 1 < code.size() &&
                (isdigit((unsigned char)code[j + 1]) ||
                 ((code[j + 1] == '+' || code[j + 1] == '-') && j + 2 < code.size() &&
                  isdigit((unsigned char)code[j + 2])))) {
                has_exp = true; ++j;
                if (code[j] == '+' || code[j] == '-') ++j;
                continue;
            }
            break;
        }
        std::string tok = code.substr(i, j - i);
        bool f_suffix = (j < code.size() && (code[j] == 'f' || code[j] == 'F'));
        size_t consumed_end = j + (f_suffix ? 1 : 0);
        // hex literals (0x...) never reach here with a dot/exp; plain ints skip:
        if (!has_dot && !has_exp) { i = consumed_end - 1; continue; }
        double v = std::atof(tok.c_str());
        bool identity = (v == 0.0 || v == 1.0 || v == -1.0 || v == 0.5 || v == 2.0);
        bool guard = (v != 0.0 && v <= 1e-5);      // literals are unsigned here
        bool sentinel = (v >= 1e9);
        if (!identity && !guard && !sentinel)
            hits.push_back({tok + (f_suffix ? "f" : ""), line});
        i = consumed_end - 1;
    }
    return hits;
}

// ---------------------------------------------------------------------------
// THE RATCHET: every magic float the scan finds today. file -> token -> count.
// Contexts + follow-up classification: LEDGER.md, Stage E entry (2026-08-13).
// ---------------------------------------------------------------------------
static const std::map<std::string, std::map<std::string, int>> KNOWN_RESIDUALS = {
    {"src/core/physics_system_v4.cpp", {
        {"0.0001f", 3},  // 0.1 mm distance guards + canary print floor
        {"0.001f", 5},   // mm-scale gates: up-vz, degenerate normal, angular err, gravity/dist
        {"0.01f", 5},    // canary floor, seg radius, half-length pad x2, turtle support reach (fallback inertia DESTROYED 2026-08-13, silent-fallback purge B3)
        {"0.05f", 1},    // wake check: mover-top vs sleeper-bottom gap (m)
        {"0.15f", 1},    // persistent-contact max gap 150 mm
        {"0.1f", 3},     // horizontal-normal classifier x2, elastic e_mag gate
        {"0.3f", 3},     // receding-speed wake gate, support-normal component x2
        {"0.95f", 1},    // damping impulse clamp fraction
        {"0.999f", 2},   // angular-bias cap guard fraction x2
        {"0.99f", 1},    // near-breaking-force warning fraction
        {"24.0f", 1},    // structural damping upper factor
        {"4.0f", 2},     // hysteresis square (2^2), I_sec = A^2/(4*pi) algebra
        {"5.0f", 1},     // structural damping floor (1/s)
    }},
    {"src/core/explosion_detector.cpp", {
        {"1000.0", 2},   // J -> kJ display conversion in the warning printf
        // 999.0 display fallback DESTROYED 2026-08-13 (silent-fallback
        // purge D1): the ratio off a zero base now prints the honest inf.
    }},
    // GluonConstraint angular defaults 100/10 DESTROYED 2026-08-13
    // (silent-fallback purge A1/A2, owner order): 0/0 is the UNDECLARED
    // sentinel and the bond doors refuse undeclared consumers. The
    // physics_system.h entry is gone because the header now carries no
    // magic float at those sites.
};

static const char* SCAN_FILES[] = {
    "src/core/physics_system_v4.cpp",
    "src/core/narrow_phase.cpp",
    "src/core/explosion_detector.cpp",
};

static fs::path find_repo_root() {
    if (const char* env = std::getenv("LOGOSPHERE_ROOT")) return fs::path(env);
    fs::path p = fs::current_path();
    for (int up = 0; up < 8; ++up) {
        if (fs::exists(p / "schema" / "physics.yaml")) return p;
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

int main() {
    std::printf("=== INV-29 static gate: no magic numbers in physics TUs ===\n");

    // ---- SELF-CHECK: the scanner must be able to find, and to ignore ----
    {
        auto hits = scan_magic_floats(strip_comments_and_strings(
            "float a = 0.98f; // 0.77f in comment\n"
            "const char* s = \"3.14 in string\";\n"
            "float b = x * 0.5f; float c = d / 1e-12f; float e = 1e10f;\n"));
        if (hits.size() != 1 || hits[0].token != "0.98f") {
            std::printf("FAIL: scanner self-check — expected exactly one hit "
                        "(0.98f), got %zu\n", hits.size());
            for (auto& h : hits) std::printf("   found: %s\n", h.token.c_str());
            return 1;
        }
        std::printf("[measure] self-check OK: finds 0.98f, ignores comment/"
                    "string/identity/guard/sentinel\n");
    }

    fs::path root = find_repo_root();
    if (root.empty()) {
        std::printf("FAIL: repo root not found (walked up from %s; set "
                    "LOGOSPHERE_ROOT)\n", fs::current_path().string().c_str());
        return 1;
    }

    // Collect the file list: the three physics .cpp TUs + every header in
    // include/logosphere/physics/.
    std::vector<std::string> files(SCAN_FILES, SCAN_FILES + 3);
    for (auto& e : fs::directory_iterator(root / "include/logosphere/physics"))
        if (e.path().extension() == ".h")
            files.push_back("include/logosphere/physics/" +
                            e.path().filename().string());
    std::sort(files.begin(), files.end());

    std::map<std::string, std::map<std::string, int>> found;
    std::map<std::string, std::vector<Hit>> found_hits;
    int total_sites = 0;
    for (auto& rel : files) {
        std::ifstream f(root / rel);
        if (!f) { std::printf("FAIL: cannot read %s\n", rel.c_str()); return 1; }
        std::stringstream ss; ss << f.rdbuf();
        auto hits = scan_magic_floats(strip_comments_and_strings(ss.str()));
        for (auto& h : hits) {
            found[rel][h.token]++;
            found_hits[rel].push_back(h);
            ++total_sites;
        }
    }
    std::printf("[measure] scanned %zu files, %d magic-float sites\n",
                files.size(), total_sites);

    // ---- exact match against the ratchet, both directions ----
    bool ok = true;
    for (auto& [file, toks] : found) {
        auto it = KNOWN_RESIDUALS.find(file);
        for (auto& [tok, n] : toks) {
            int known = 0;
            if (it != KNOWN_RESIDUALS.end()) {
                auto jt = it->second.find(tok);
                if (jt != it->second.end()) known = jt->second;
            }
            if (n > known) {
                ok = false;
                std::printf("FAIL: NEW magic number %s in %s (%d found, %d "
                            "known) — it is an input; put it in the registry "
                            "(schema/physics.yaml), regenerate, use the name.\n",
                            tok.c_str(), file.c_str(), n, known);
                for (auto& h : found_hits[file])
                    if (h.token == tok)
                        std::printf("       %s:%d\n", file.c_str(), h.line);
            }
        }
    }
    for (auto& [file, toks] : KNOWN_RESIDUALS) {
        for (auto& [tok, known] : toks) {
            int n = 0;
            auto it = found.find(file);
            if (it != found.end()) {
                auto jt = it->second.find(tok);
                if (jt != it->second.end()) n = jt->second;
            }
            if (n < known) {
                ok = false;
                std::printf("FAIL: residual %s in %s shrank (%d found, %d "
                            "known) — good news, but shrink KNOWN_RESIDUALS in "
                            "the same commit (and flip INV-29 active when the "
                            "table empties).\n", tok.c_str(), file.c_str(), n, known);
            }
        }
    }

    if (!ok) return 1;
    int residual_groups = 0;
    for (auto& [file, toks] : KNOWN_RESIDUALS) residual_groups += (int)toks.size();
    std::printf("[measure] residual table matched exactly: %d token-groups, "
                "%d sites — INV-29 remains aspirational until this reads 0\n",
                residual_groups, total_sites);
    std::printf("PASS\n");
    return 0;
}
