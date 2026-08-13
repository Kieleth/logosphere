// ============================================================================
// INV-15 STATIC PROVER — the solver is blind to game-layer ownership
// ============================================================================
// INV-15 (physics-blind-to-game): the solver reads solver_mode and physical
// state only; no ParticleOwner or game-category branch exists inside the
// physics translation units. Its stated mechanism has always been a static
// grep audit; this test IS that audit, running on every sweep. It closes
// the coverage hole the 2026-08-13 ledger named the cheapest to close.
//
// EXPECT-FAIL STYLE. Seven owner reads are alive in physics_system_v4.cpp
// today — the quat-gravity exemption family, every one of the form
//   p.is_quat_driven && p.owner ==/!= ParticleOwner::PHYSICS
// Ruled a FIX TASK (#43), not a sanctioned annotation (ledger 2026-08-13).
// Until that fix lands, this test PASSES by reporting that the count
// matches the known-violations list below, and it FAILS when the count
// CHANGES in either direction:
//   - count up   = a new ownership bleed into the solver. Fix the bleed:
//     write solver_mode from the game layer, branch on that.
//   - count down = task #43 (or part of it) landed. Celebrate, then update
//     KNOWN_OWNER_READS, TEST_AUDIT.jsonl and the ledger in the same
//     commit; at zero, INV-15's mechanism is fully enforced.
//
// WHAT IS COUNTED. Occurrences of the tokens "ParticleOwner" and ".owner"
// in CODE (comments and strings stripped) across the physics TUs:
// src/core/physics_system_v4.cpp, narrow_phase.cpp, explosion_detector.cpp
// and include/logosphere/physics/*.h. Both patterns are counted because a
// read can name the enum or just touch the field. Today the 7 lines carry
// both, so the counts are 7 and 7; any other file must show 0.
//
// SELF-CHECK (control): synthetic snippets prove the counter finds the
// token in code and ignores it in comments before the tree is trusted.
//
// Headless-safe: pure file IO, no engine state.
// ============================================================================

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Same stripping machine as the INV-29 gate: comments and strings are not
// code and must not count.
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

struct Counts { int particle_owner = 0; int dot_owner = 0; };

static Counts count_owner_reads(const std::string& code,
                                std::vector<int>* lines = nullptr) {
    Counts c;
    int line = 1;
    auto ident = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '_';
    };
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i] == '\n') { ++line; continue; }
        if (code.compare(i, 13, "ParticleOwner") == 0 &&
            (i == 0 || !ident(code[i - 1])) &&
            (i + 13 >= code.size() || !ident(code[i + 13]))) {
            c.particle_owner++;
            if (lines) lines->push_back(line);
        }
        if (code.compare(i, 6, ".owner") == 0 &&
            (i + 6 >= code.size() || !ident(code[i + 6]))) {
            c.dot_owner++;
        }
    }
    return c;
}

// ---------------------------------------------------------------------------
// THE KNOWN VIOLATIONS (task #43): the quat-gravity exemption family.
// file -> (ParticleOwner token count, .owner field count).
// ---------------------------------------------------------------------------
static const std::map<std::string, Counts> KNOWN_OWNER_READS = {
    {"src/core/physics_system_v4.cpp", {7, 7}},
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
    std::printf("=== INV-15 static prover: solver blindness to ownership ===\n");

    // ---- SELF-CHECK ----
    {
        Counts a = count_owner_reads(strip_comments_and_strings(
            "if (p.owner == ParticleOwner::PHYSICS) {}\n"
            "// ParticleOwner in a comment, p.owner too\n"
            "const char* s = \"ParticleOwner\";\n"));
        if (a.particle_owner != 1 || a.dot_owner != 1) {
            std::printf("FAIL: counter self-check — expected 1/1, got %d/%d\n",
                        a.particle_owner, a.dot_owner);
            return 1;
        }
        std::printf("[measure] self-check OK: counts code, ignores comments "
                    "and strings\n");
    }

    fs::path root = find_repo_root();
    if (root.empty()) {
        std::printf("FAIL: repo root not found (set LOGOSPHERE_ROOT)\n");
        return 1;
    }

    std::vector<std::string> files(SCAN_FILES, SCAN_FILES + 3);
    for (auto& e : fs::directory_iterator(root / "include/logosphere/physics"))
        if (e.path().extension() == ".h")
            files.push_back("include/logosphere/physics/" +
                            e.path().filename().string());
    std::sort(files.begin(), files.end());

    bool ok = true;
    for (auto& rel : files) {
        std::ifstream f(root / rel);
        if (!f) { std::printf("FAIL: cannot read %s\n", rel.c_str()); return 1; }
        std::stringstream ss; ss << f.rdbuf();
        std::vector<int> lines;
        Counts got = count_owner_reads(strip_comments_and_strings(ss.str()), &lines);
        Counts want;  // 0/0 unless listed
        auto it = KNOWN_OWNER_READS.find(rel);
        if (it != KNOWN_OWNER_READS.end()) want = it->second;

        if (got.particle_owner != want.particle_owner ||
            got.dot_owner != want.dot_owner) {
            ok = false;
            if (got.particle_owner > want.particle_owner ||
                got.dot_owner > want.dot_owner) {
                std::printf("FAIL: %s — owner reads GREW (ParticleOwner %d vs "
                            "%d known, .owner %d vs %d). A new game-layer "
                            "bleed into the solver: write solver_mode from "
                            "the game layer and branch on that (INV-15).\n",
                            rel.c_str(), got.particle_owner, want.particle_owner,
                            got.dot_owner, want.dot_owner);
            } else {
                std::printf("FAIL: %s — owner reads SHRANK (ParticleOwner %d "
                            "vs %d known, .owner %d vs %d). Task #43 progress: "
                            "update KNOWN_OWNER_READS, TEST_AUDIT.jsonl and "
                            "the ledger in this commit.\n",
                            rel.c_str(), got.particle_owner, want.particle_owner,
                            got.dot_owner, want.dot_owner);
            }
            for (int ln : lines)
                std::printf("       %s:%d\n", rel.c_str(), ln);
        } else if (got.particle_owner > 0) {
            std::printf("[measure] %s: %d ParticleOwner / %d .owner reads — "
                        "matches the known task-#43 list at lines:",
                        rel.c_str(), got.particle_owner, got.dot_owner);
            for (int ln : lines) std::printf(" %d", ln);
            std::printf("\n");
        }
    }

    if (!ok) return 1;
    std::printf("[measure] every other physics TU: 0 owner reads\n");
    std::printf("PASS (expect-fail style: green means the violation set is "
                "unchanged, not that it is empty)\n");
    return 0;
}
