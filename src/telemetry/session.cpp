#include "logosphere/telemetry/session.h"
#include "logosphere/build_info.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace logosphere::telemetry {

namespace {

// "2026-04-21T14:32:01Z" — ISO 8601 UTC, for the RECORD. Never for a
// path: see path_stamp below.
std::string iso_utc_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

// The same instant, safe to put in a directory name. Colons are ILLEGAL
// in Windows paths, so an ISO timestamp used as a folder makes
// create_directories throw, and nothing here catches it: the process
// dies with a fastfail and the test output says only that it crashed.
//
// This cost a Windows CI lane once. The session record keeps the real
// ISO stamp; only the path drops the punctuation.
std::string path_stamp(const std::string& iso) {
    std::string out;
    out.reserve(iso.size());
    for (const char c : iso) {
        if (c != ':') out += c;
    }
    return out;
}

std::string home_dir() {
#if defined(_WIN32)
    // Windows has no HOME by default. USERPROFILE is the equivalent,
    // and "/tmp" is not a writable absolute path there.
    if (const char* p = std::getenv("USERPROFILE")) return p;
    if (const char* p = std::getenv("LOCALAPPDATA")) return p;
    return ".";
#else
    if (const char* h = std::getenv("HOME")) return h;
    return "/tmp";  // harmless fallback
#endif
}

// JSONL-safe escape: double quotes and backslashes. No control-char
// handling for v1 — we control our own producers. Extend if we ever
// take user input into records.
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

} // namespace

// ---------- Instrument ----------

std::FILE* Instrument::get_or_open_(const std::string& filename) {
    for (auto& e : sinks_) {
        if (e.first == filename) return e.second.fp;
    }
    if (dir_.empty()) return nullptr;   // telemetry disabled
    try {
        std::filesystem::create_directories(dir_);
    } catch (const std::exception& e) {
        std::cerr << "[telemetry] cannot write to " << dir_ << " ("
                  << e.what() << ")" << std::endl;
        return nullptr;
    }
    std::filesystem::path p = std::filesystem::path(dir_) / filename;
    std::FILE* fp = std::fopen(p.string().c_str(), "ab");
    if (!fp) {
        std::cerr << "[telemetry] fopen failed for " << p << std::endl;
        return nullptr;
    }
    sinks_.push_back({filename, {fp}});
    return fp;
}

void Instrument::append_jsonl(const std::string& filename,
                              const std::string& record_json) {
    std::lock_guard<std::mutex> lock(mu_);
    std::FILE* fp = get_or_open_(filename);
    if (!fp) return;
    std::fwrite(record_json.data(), 1, record_json.size(), fp);
    std::fputc('\n', fp);
    std::fflush(fp);  // telemetry is low-volume; flush per line so a
                      // crash doesn't lose the records leading up to
                      // it. Worth the cost here.
}

void Instrument::close() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& e : sinks_) {
        if (e.second.fp) std::fclose(e.second.fp);
    }
    sinks_.clear();
}

// ---------- Session ----------

Session::Session() {
    id_.build_sha      = kBuildSha;
    id_.build_describe = kBuildDescribe;
    id_.launch_utc     = iso_utc_now();

    // Session number: if another session from the same second already
    // exists in the build_sha subtree, increment. Lets back-to-back
    // launches disambiguate without forcing sub-second precision.
    std::filesystem::path root = std::filesystem::path(home_dir())
        / ".logosphere" / "sessions" / id_.build_sha;
    // A path we cannot make is a reason to lose telemetry, never a
    // reason to kill the run. create_directories THROWS, and an
    // uncaught filesystem_error leaves a process dead with no message
    // at all: on Windows that reads as "Exit code 0xc0000409" and
    // nothing else, which is how an illegal colon in a directory name
    // went unnoticed until a test finally opened a Session there.
    try {
        std::filesystem::create_directories(root);
    } catch (const std::exception& e) {
        std::cerr << "[telemetry] no session directory (" << e.what()
                  << "); running without telemetry" << std::endl;
        return;
    }

    int n = 1;
    while (true) {
        std::ostringstream oss;
        oss << path_stamp(id_.launch_utc) << "_" << n;
        std::filesystem::path candidate = root / oss.str();
        if (!std::filesystem::exists(candidate)) {
            id_.session_num = n;
            dir_ = candidate.string();
            std::filesystem::create_directories(dir_);
            break;
        }
        ++n;
        if (n > 1000) {  // belt + suspenders
            id_.session_num = n;
            dir_ = (root / (oss.str() + "_fallback")).string();
            std::filesystem::create_directories(dir_);
            break;
        }
    }

    // Write session.json manifest. Minimal JSON hand-rolled to avoid
    // pulling in a dependency just for this.
    std::ostringstream j;
    j << "{"
      << "\"build_sha\":\""      << escape(id_.build_sha)      << "\","
      << "\"build_describe\":\"" << escape(id_.build_describe) << "\","
      << "\"launch_utc\":\""     << escape(id_.launch_utc)     << "\","
      << "\"session_num\":"      << id_.session_num
      << "}";
    std::filesystem::path manifest = std::filesystem::path(dir_) / "session.json";
    if (std::FILE* fp = std::fopen(manifest.string().c_str(), "wb")) {
        std::fputs(j.str().c_str(), fp);
        std::fputc('\n', fp);
        std::fclose(fp);
    }

    std::cerr << "[telemetry] session opened: " << dir_ << std::endl;
}

Session::~Session() {
    for (auto& i : instruments_) {
        i->close();
    }
    if (dir_.empty()) return;   // never opened; nothing to finish
    // Update manifest with end time — simple rewrite since the file
    // is tiny. If we ever need concurrent readers, switch to an
    // append-only end_marker line.
    std::filesystem::path manifest = std::filesystem::path(dir_) / "session.json";
    std::ostringstream j;
    j << "{"
      << "\"build_sha\":\""      << escape(id_.build_sha)      << "\","
      << "\"build_describe\":\"" << escape(id_.build_describe) << "\","
      << "\"launch_utc\":\""     << escape(id_.launch_utc)     << "\","
      << "\"session_num\":"      << id_.session_num             << ","
      << "\"end_utc\":\""        << escape(iso_utc_now())       << "\""
      << "}";
    if (std::FILE* fp = std::fopen(manifest.string().c_str(), "wb")) {
        std::fputs(j.str().c_str(), fp);
        std::fputc('\n', fp);
        std::fclose(fp);
    }
    std::cerr << "[telemetry] session closed: " << dir_ << std::endl;
}

Instrument* Session::register_instrument(std::unique_ptr<Instrument> instr) {
    Instrument* raw = instr.get();
    if (dir_.empty()) {
        // Telemetry is off. The instrument still registers and still
        // works; it simply writes nowhere, so a caller does not have
        // to branch on whether a session opened.
        instruments_.push_back(std::move(instr));
        return raw;
    }
    std::filesystem::path sub = std::filesystem::path(dir_) / instr->name();
    std::filesystem::create_directories(sub);
    instr->open(sub.string());
    instruments_.push_back(std::move(instr));
    return raw;
}

} // namespace logosphere::telemetry
